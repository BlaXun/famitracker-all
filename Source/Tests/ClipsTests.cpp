/*
** FamiTracker - NES/Famicom sound tracker
** Copyright (C) 2005-2014  Jonathan Liss
** Copyright (C) 2026  The beginner-friendly FamiTracker contributors
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
** Library General Public License for more details.  To obtain a
** copy of the GNU Library General Public License, write to the Free
** Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
** Any permitted reproduction of these routines, in whole or in part,
** must bear this legend.
*/

// Tests for the clip library (Source/Clips.cpp), run on a song in memory.
#include "Clips.cpp"
#include <cassert>
#include <cstdio>

static int g_checks = 0, g_fail = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static stChanNote N(int note, int octave, int inst = 0, int vol = MAX_VOLUME)
{
	stChanNote n = CFamiTrackerDoc::Empty();
	n.Note = (unsigned char)note; n.Octave = (unsigned char)octave; n.Instrument = (unsigned char)inst; n.Vol = (unsigned char)vol;
	return n;
}

static void MakeSong(CFamiTrackerDoc &doc, int frames)
{
	doc.tracks.assign(1, CFamiTrackerDoc::Track());
	doc.chanIDs = { 0, 1, 2, 3, 4, 5, 6, 7 };
	for (int f = 0; f < frames; ++f)
		doc.tracks[0].frames.push_back(std::vector<unsigned>(8, unsigned(f)));
}

// What the insert action does: the clip's cells from Row of Frame in Channel on
static void Write(CFamiTrackerDoc &doc, CClipLibrary &lib, int index, unsigned frame, unsigned row, int channel)
{
	for (int c = 0; c < lib.GetColumns(index); ++c)
		for (int r = 0; r < lib.GetRows(index); ++r) {
			const unsigned pos = row + unsigned(r), f = frame + pos / doc.length;
			if (f >= doc.GetFrameCount(0)) break;
			stChanNote cell; lib.GetCell(index, c, r, cell);
			doc.SetDataAtPattern(0, doc.GetPatternAtFrame(0, f, unsigned(channel + c)), unsigned(channel + c), pos % doc.length, &cell);
		}
}

static stChanNote At(const CFamiTrackerDoc &doc, unsigned frame, unsigned channel, unsigned row)
{
	stChanNote n; doc.GetDataAtPattern(0, doc.GetPatternAtFrame(0, frame, channel), channel, row, &n); return n;
}

static void Set(CFamiTrackerDoc &doc, unsigned frame, unsigned channel, unsigned row, const stChanNote &n)
{
	doc.SetDataAtPattern(0, doc.GetPatternAtFrame(0, frame, channel), channel, row, &n);
}

int main()
{
	// A one-channel clip of 8 rows: C-3 on row 0, E-3 on row 4
	{
		CFamiTrackerDoc doc; MakeSong(doc, 4);
		CClipLibrary lib; doc.clips = &lib;
		CHECK(lib.NewName("Take", true) == "Take 1");
		const int k = lib.Add("Take 1", 1, 8);
		CHECK(k == 0 && lib.GetCount() == 1 && lib.GetRows(0) == 8);
		CHECK(lib.NewName("Take", true) == "Take 2" && lib.NewName("Take 1", false) == "Take 1 2" && lib.NewName("Riff", false) == "Riff");
		lib.SetCell(0, 0, 0, N(C, 3)); lib.SetCell(0, 0, 4, N(E, 3));

		// Linked at frame 0 row 4, and at frame 1 row 12 (into frame 2)
		Write(doc, lib, 0, 0, 4, 0);
		CHECK(lib.AddLink(&doc, 0, 0, 0, 4, 0) > 0);
		Write(doc, lib, 0, 1, 12, 0);
		const int link2 = lib.AddLink(&doc, 0, 0, 1, 12, 0);
		CHECK(link2 > 0 && lib.GetLinkCount(0) == 2);
		CHECK(lib.GetLinkSegmentCount(0) == 3);	// one segment, then two (across frames 1 and 2)
		CHECK(!lib.Sync(&doc));	// nothing edited

		// Edit the second copy where it runs into frame 2: the clip and the first copy follow
		Set(doc, 2, 0, 0, N(G, 3, 1));	// clip row 4
		CHECK(lib.Sync(&doc));
		stChanNote cell; lib.GetCell(0, 0, 4, cell);
		CHECK(cell.Note == G && cell.Octave == 3 && cell.Instrument == 1);
		CHECK(At(doc, 0, 0, 8).Note == G);		// the first copy, clip row 4
		CHECK(doc.locks == 0);
		CHECK(!lib.Sync(&doc));

		// An octave on an empty row doesn't count as an edit
		stChanNote odd = CFamiTrackerDoc::Empty(); odd.Octave = 5;
		Set(doc, 0, 0, 5, odd);
		CHECK(!lib.Sync(&doc));

		// Undo of an edit: the first copy goes back, so the clip and the second copy do too
		Set(doc, 0, 0, 8, N(E, 3));
		CHECK(lib.Sync(&doc));
		CHECK(At(doc, 2, 0, 0).Note == E && At(doc, 2, 0, 0).Instrument == 0);

		// Clearing a copy unlinks it instead of clearing the others
		for (unsigned r = 4; r < 12; ++r) Set(doc, 0, 0, r, CFamiTrackerDoc::Empty());
		lib.Sync(&doc);
		CHECK(lib.GetLinkCount(0) == 1);
		CHECK(At(doc, 1, 0, 12).Note == C && At(doc, 2, 0, 0).Note == E);

		// Pushing a clip edit (the clip editor)
		lib.SetCell(0, 0, 0, N(D, 4));
		CHECK(lib.PushToLinks(&doc, 0));
		CHECK(At(doc, 1, 0, 12).Note == D && At(doc, 1, 0, 12).Octave == 4);
		CHECK(!lib.PushToLinks(&doc, 0));

		// Link segments for undo
		unsigned t, p; int ch;
		CHECK(lib.GetLinkSegment(&doc, 0, 0, t, ch, p) && t == 0 && ch == 0 && p == 1);
		CHECK(lib.GetLinkSegment(&doc, 0, 1, t, ch, p) && p == 2);
		CHECK(!lib.GetLinkSegment(&doc, 0, 2, t, ch, p));

		// Snapshots, and writing over a linked copy
		CClipLinkState before;
		lib.SaveLinks(before);
		lib.UnlinkArea(&doc, 0, 2, 4, 0, 4);	// frame 2 rows 4-7: past the copy's end (row 3 of frame 2)
		CHECK(lib.GetLinkCount(0) == 1);
		lib.UnlinkArea(&doc, 0, 2, 2, 1, 4);	// another channel
		CHECK(lib.GetLinkCount(0) == 1);
		lib.UnlinkArea(&doc, 0, 0, 30, 0, 6);	// frame 0 row 30 = frame 1 row 14: inside the copy
		CHECK(lib.GetLinkCount(0) == 0);
		lib.RestoreLinks(before);
		CHECK(lib.GetLinkCount(0) == 1);

		// Duplicates have no links; deleting a clip drops its links
		const int dup = lib.Duplicate(0, "Take 1 copy");
		CHECK(dup == 1 && lib.GetLinkCount(1) == 0 && lib.GetId(1) != lib.GetId(0));
		stChanNote d0; lib.GetCell(1, 0, 0, d0);
		CHECK(d0.Note == D);
		lib.Remove(0);
		CHECK(lib.GetCount() == 1 && lib.GetLinkCount(0) == 0);
		lib.RestoreLinks(before);	// links to the deleted clip stay gone
		CHECK(lib.GetLinkCount(0) == 0);
	}

	// Two columns, file round trip, damaged files
	{
		CFamiTrackerDoc doc; MakeSong(doc, 3);
		CClipLibrary lib; doc.clips = &lib;
		lib.Add("Chords", 2, 4);
		lib.SetSource(0, 0, 5); lib.SetSource(0, 1, 6);
		stChanNote fx = N(A, 2, 3, 9); fx.EffNumber[1] = EF_ARPEGGIO; fx.EffParam[1] = 0x47;
		lib.SetCell(0, 1, 2, fx);
		Write(doc, lib, 0, 0, 14, 5);
		CHECK(lib.AddLink(&doc, 0, 0, 0, 14, 5) > 0);
		CHECK(lib.GetLinkSegmentCount(0) == 4);	// two channels, each across two frames

		CDocumentFile file;
		CHECK(lib.Write(&file));
		CClipLibrary back;
		CHECK(back.Read(&file));
		CHECK(back.GetCount() == 1 && back.GetName(0) == "Chords" && back.GetColumns(0) == 2 && back.GetRows(0) == 4);
		CHECK(back.GetSource(0, 1) == 6 && back.GetLinkCount(0) == 1 && back.GetId(0) == lib.GetId(0));
		stChanNote c; back.GetCell(0, 1, 2, c);
		CHECK(c.Note == A && c.Octave == 2 && c.Instrument == 3 && c.Vol == 9 && c.EffNumber[1] == EF_ARPEGGIO && c.EffParam[1] == 0x47);
		CHECK(back.NewName("Chords", false) == "Chords 2");
		// New IDs stay unique after loading
		const int fresh = back.Add("New", 1, 1);
		CHECK(back.GetId(fresh) > back.GetId(0));

		// The links still work after loading
		doc.clips = &back;
		Set(doc, 1, 6, 0, N(B, 2, 3));	// column 1, clip row 2
		CHECK(!back.Sync(&doc));	// the clip changes; no other copy to change in the song
		back.GetCell(0, 1, 2, c);
		CHECK(c.Note == B && c.EffNumber[1] == EF_NONE);

		// Cut short anywhere: nothing loaded, and no crash
		for (size_t n = 0; n < file.data.size(); ++n) {
			CDocumentFile cut = file;
			cut.data.resize(n);
			cut.pos = 0;
			CClipLibrary partial;
			if (partial.Read(&cut))
				CHECK(false);
			CHECK(partial.IsEmpty());
		}
		// A value no pattern can hold
		CDocumentFile bad = file;
		bad.pos = 0;
		const size_t cells = 4 + 4 + 7 + 4 + 4 + 8;	// count, ID, "Chords\0", columns, rows, sources
		bad.data[cells] = 0x20;	// the first cell's note
		CClipLibrary none;
		CHECK(!none.Read(&bad) && none.IsEmpty());
		// A newer block version: left out
		CDocumentFile newer = file; newer.version = 2; newer.pos = 0;
		CClipLibrary later;
		CHECK(later.Read(&newer) && later.IsEmpty());
	}

	// Tracks
	{
		CFamiTrackerDoc doc; MakeSong(doc, 2);
		doc.tracks.push_back(doc.tracks[0]);
		CClipLibrary lib; doc.clips = &lib;
		lib.Add("Bass", 1, 2);
		lib.SetCell(0, 0, 0, N(C, 2));
		CHECK(lib.AddLink(&doc, 0, 1, 0, 0, 2) > 0);	// in track 1
		lib.TracksSwapped(0, 1);
		unsigned t, p; int ch;
		CHECK(lib.GetLinkSegment(&doc, 0, 0, t, ch, p) && t == 0);
		lib.TrackRemoved(1);
		CHECK(lib.GetLinkCount(0) == 1);
		lib.TrackRemoved(0);
		CHECK(lib.GetLinkCount(0) == 0);

		// A link into a channel that's gone (expansion chip removed) is dropped on the next sync
		CHECK(lib.AddLink(&doc, 0, 0, 0, 0, 7) > 0);
		doc.chanIDs.pop_back();
		lib.Sync(&doc);
		CHECK(lib.GetLinkCount(0) == 0);
		// ...and one past a shorter pattern length
		CHECK(lib.AddLink(&doc, 0, 0, 0, 14, 0) > 0);
		doc.length = 15;
		lib.Sync(&doc);
		CHECK(lib.GetLinkCount(0) == 0);
	}

	std::printf("%d checks, %d failures\n", g_checks, g_fail);
	return g_fail ? 1 : 0;
}

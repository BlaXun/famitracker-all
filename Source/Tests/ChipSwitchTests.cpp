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

// Tests for switching the expansion chips (Source/ChipSwitch.cpp): the
// channels a set of chips has, the notes counted, the channels emptied, the
// effects that steer the song moved to a channel that stays, and the channels
// that stay moved to their new columns, on a song in memory.
#include "Clips.cpp"
#include "ChipSwitch.cpp"
#include <cstdio>

// As CFamiTrackerDoc::SelectExpansionChip: the module's channels become those
// of the chips, and each keeps the frames, patterns and effect columns of the
// channel of its type before (CPatternData::MoveChannels); new ones are empty
void CFamiTrackerDoc::SelectExpansionChip(unsigned char Chips)
{
	const std::vector<int> New = ChipChannels(Chips, namco);
	const std::vector<int> From = ChannelSources(chanIDs, New);
	for (Track &T : tracks) {
		Track Moved;
		for (const std::vector<unsigned> &Frame : T.frames) {
			std::vector<unsigned> F(MAX_CHANNELS, 0);
			for (size_t c = 0; c < New.size(); ++c)
				if (From[c] >= 0) F[c] = Frame[size_t(From[c])];
			Moved.frames.push_back(F);
		}
		for (const auto &P : T.patterns)
			for (size_t c = 0; c < New.size(); ++c)
				if (From[c] == int(P.first.first)) Moved.patterns[{ unsigned(c), P.first.second }] = P.second;
		for (const auto &Col : T.columns)
			for (size_t c = 0; c < New.size(); ++c)
				if (From[c] == int(Col.first)) Moved.columns[unsigned(c)] = Col.second;
		T = Moved;
	}
	chanIDs = New;
	chips = Chips;
}

static int g_checks = 0, g_fail = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static stChanNote N(int note, int octave, int inst = 0)
{
	stChanNote n = CFamiTrackerDoc::Empty();
	n.Note = (unsigned char)note; n.Octave = (unsigned char)octave; n.Instrument = (unsigned char)inst;
	return n;
}

static stChanNote Fx(int number, int param, int column = 0)
{
	stChanNote n = CFamiTrackerDoc::Empty();
	n.EffNumber[column] = (unsigned char)number; n.EffParam[column] = (unsigned char)param;
	return n;
}

// Tracks of 4 frames, each frame its own pattern in every channel (the frame's number), with the
// 2A03's and the VRC6's channels
static void MakeSong(CFamiTrackerDoc &doc, int tracks, int frames = 4)
{
	doc.tracks.assign(size_t(tracks), CFamiTrackerDoc::Track());
	doc.chanIDs = { 0, 1, 2, 3, 4, 5, 6, 7 };
	for (int t = 0; t < tracks; ++t)
		for (int f = 0; f < frames; ++f)
			doc.tracks[size_t(t)].frames.push_back(std::vector<unsigned>(MAX_CHANNELS, unsigned(f)));
}

static stChanNote At(const CFamiTrackerDoc &doc, unsigned track, unsigned frame, unsigned channel, unsigned row)
{
	stChanNote n; doc.GetNoteData(track, frame, channel, row, &n); return n;
}

static void Set(CFamiTrackerDoc &doc, unsigned track, unsigned frame, unsigned channel, unsigned row, const stChanNote &n)
{
	doc.SetDataAtPattern(track, doc.GetPatternAtFrame(track, frame, channel), channel, row, &n);
}

static bool Has(const stChanNote &n, int number, int param)
{
	for (int k = 0; k < MAX_EFFECT_COLUMNS; ++k)
		if (n.EffNumber[k] == number && n.EffParam[k] == param) return true;
	return false;
}

static const std::vector<unsigned> VRC6 = { 5, 6, 7 };

int main()
{
	// Notes on the VRC6's channels, counted as they play
	{
		CFamiTrackerDoc doc; MakeSong(doc, 2);
		Set(doc, 0, 0, 5, 0, N(C, 4));
		Set(doc, 0, 0, 5, 4, N(RELEASE, 0));	// not notes
		Set(doc, 0, 0, 5, 8, N(HALT, 0));
		Set(doc, 0, 1, 7, 2, N(E, 2));
		Set(doc, 0, 0, 0, 0, N(G, 3));	// the 2A03's own don't count
		doc.SetPatternAtFrame(0, 3, 5, 0);	// frame 3 plays frame 0's pattern: the note counts twice
		std::vector<int> tracks;
		CHECK(CountChannelNotes(&doc, VRC6, tracks) == 3);
		CHECK(tracks.size() == 1 && tracks[0] == 0);
		Set(doc, 1, 2, 6, 1, N(A, 3));
		CHECK(CountChannelNotes(&doc, VRC6, tracks) == 4);
		CHECK(tracks.size() == 2 && tracks[1] == 1);
		CHECK(CountChannelNotes(&doc, std::vector<unsigned>(), tracks) == 0 && tracks.empty());
	}

	// The effects that steer the song, in the columns that play
	{
		CFamiTrackerDoc doc; MakeSong(doc, 2);
		doc.SetEffColumns(0, 6, 1);	// two columns
		Set(doc, 0, 3, 7, 15, Fx(EF_JUMP, 0));
		Set(doc, 0, 0, 6, 0, Fx(EF_SPEED, 5, 1));
		Set(doc, 0, 1, 7, 0, Fx(EF_VIBRATO, 0x47));	// not one
		Set(doc, 0, 2, 7, 0, Fx(EF_HALT, 0, 2));	// a hidden column doesn't play
		Set(doc, 1, 1, 5, 7, Fx(EF_SKIP, 0));
		Set(doc, 0, 0, 0, 3, Fx(EF_JUMP, 1));	// the 2A03's stay where they are
		std::vector<song_effect_t> effects;
		GatherSongEffects(&doc, VRC6, effects);
		CHECK(effects.size() == 3);
		bool jump = false, speed = false, skip = false;
		for (size_t i = 0; i < effects.size(); ++i) {
			const song_effect_t &e = effects[i];
			jump = jump || (e.Track == 0 && e.Frame == 3 && e.Row == 15 && e.Number == EF_JUMP && e.Param == 0);
			speed = speed || (e.Track == 0 && e.Frame == 0 && e.Row == 0 && e.Number == EF_SPEED && e.Param == 5);
			skip = skip || (e.Track == 1 && e.Frame == 1 && e.Row == 7 && e.Number == EF_SKIP);
		}
		CHECK(jump && speed && skip);
	}

	// Emptied channels: no patterns, every frame plays 00, linked copies there unlinked
	{
		CFamiTrackerDoc doc; MakeSong(doc, 2);
		CClipLibrary lib; doc.clips = &lib;
		const int clip = lib.Add("Riff", 1, 4);
		lib.SetCell(clip, 0, 0, N(D, 4));
		Set(doc, 0, 1, 5, 2, N(D, 4));
		CHECK(lib.AddLink(&doc, clip, 0, 1, 2, 5) > 0 && lib.GetLinkCount(clip) == 1);
		Set(doc, 0, 2, 6, 0, N(F, 3));
		Set(doc, 1, 3, 7, 9, N(G, 2));
		Set(doc, 0, 0, 0, 0, N(C, 3));
		doc.Pattern(0, 5, 100);	// one no frame plays
		std::vector<unsigned> channels = VRC6;
		for (unsigned c = 8; c < MAX_CHANNELS; ++c) channels.push_back(c);
		EmptyChannels(&doc, channels);
		CHECK(lib.GetLinkCount(clip) == 0);
		std::vector<int> tracks;
		CHECK(CountChannelNotes(&doc, VRC6, tracks) == 0);
		bool cleared = true, zero = true;
		for (unsigned t = 0; t < 2; ++t)
			for (unsigned c = 5; c < unsigned(MAX_CHANNELS); ++c) {
				for (unsigned p = 0; p < unsigned(MAX_PATTERN); ++p)
					cleared = cleared && doc.tracks[t].patterns.count({c, p}) == 0;
				for (unsigned f = 0; f < 4; ++f)
					zero = zero && doc.GetPatternAtFrame(t, f, c) == 0;
			}
		CHECK(cleared && zero);
		CHECK(At(doc, 0, 0, 0, 0).Note == C && doc.GetPatternAtFrame(0, 3, 0) == 3);	// the 2A03's untouched
	}

	// Moved to an emptied channel: the frames with them get patterns of their own
	{
		CFamiTrackerDoc doc; MakeSong(doc, 2);
		Set(doc, 0, 3, 7, 15, Fx(EF_JUMP, 0));
		Set(doc, 0, 0, 6, 0, Fx(EF_SPEED, 5));
		Set(doc, 1, 1, 5, 7, Fx(EF_HALT, 0));
		std::vector<song_effect_t> effects;
		GatherSongEffects(&doc, VRC6, effects);
		std::vector<unsigned> channels = VRC6;
		for (unsigned c = 8; c < MAX_CHANNELS; ++c) channels.push_back(c);
		EmptyChannels(&doc, channels);
		// Now VRC7: channel 10 is its last
		doc.chanIDs = { 0, 1, 2, 3, 4, 20, 21, 22, 23, 24, 25 };
		CHECK(PlaceSongEffects(&doc, 10, effects) == 0);
		CHECK(Has(At(doc, 0, 3, 10, 15), EF_JUMP, 0) && Has(At(doc, 0, 0, 10, 0), EF_SPEED, 5) && Has(At(doc, 1, 1, 10, 7), EF_HALT, 0));
		CHECK(doc.GetPatternAtFrame(0, 1, 10) == 0 && doc.GetPatternAtFrame(0, 2, 10) == 0);	// the others share the empty 00
		CHECK(doc.IsPatternEmpty(0, 10, 0) && doc.IsPatternEmpty(1, 10, 0));
		const unsigned p0 = doc.GetPatternAtFrame(0, 0, 10), p3 = doc.GetPatternAtFrame(0, 3, 10);
		CHECK(p0 != 0 && p3 != 0 && p0 != p3);
		CHECK(doc.GetPatternAtFrame(1, 1, 10) != 0);
		CHECK(doc.GetEffColumns(0, 10) == 0);	// one column was enough
		CHECK(!Has(At(doc, 0, 1, 10, 15), EF_JUMP, 0));
	}

	// Moved into a channel with notes and effects of its own
	{
		CFamiTrackerDoc doc; MakeSong(doc, 1);
		// DPCM: frames 0 and 2 share pattern 0, the same sample on row 0 with a pitch effect
		doc.SetPatternAtFrame(0, 2, 4, 0);
		stChanNote hit = N(C, 3, 1); hit.EffNumber[0] = EF_DPCM_PITCH; hit.EffParam[0] = 0x0F;
		Set(doc, 0, 0, 4, 0, hit);
		Set(doc, 0, 1, 4, 5, Fx(EF_VIBRATO, 0x23, 1));	// left in a hidden column
		Set(doc, 0, 3, 4, 6, Fx(EF_SLIDE_UP, 0x11, 2));	// stays hidden
		std::vector<song_effect_t> effects;
		effects.push_back(song_effect_t{ 0, 2, 0, EF_JUMP, 0 });	// lands next to the pitch effect
		effects.push_back(song_effect_t{ 0, 3, 3, EF_SPEED, 4 });
		CHECK(PlaceSongEffects(&doc, 4, effects) == 0);
		const stChanNote moved = At(doc, 0, 2, 4, 0);
		CHECK(moved.Note == C && moved.EffNumber[0] == EF_DPCM_PITCH && moved.EffNumber[1] == EF_JUMP);
		CHECK(doc.GetPatternAtFrame(0, 0, 4) == 0 && doc.GetPatternAtFrame(0, 2, 4) != 0);	// frame 2 has its own now
		CHECK(At(doc, 0, 0, 4, 0).EffNumber[1] == EF_NONE);	// frame 0 doesn't jump
		CHECK(doc.GetEffColumns(0, 4) == 1);	// two columns show
		CHECK(At(doc, 0, 1, 4, 5).EffNumber[1] == EF_NONE);	// the hidden vibrato didn't come back
		CHECK(At(doc, 0, 3, 4, 6).EffNumber[2] == EF_SLIDE_UP);	// still hidden, kept
		CHECK(At(doc, 0, 3, 4, 3).EffNumber[0] == EF_SPEED && At(doc, 0, 3, 4, 3).EffParam[0] == 4);
		CHECK(doc.GetPatternAtFrame(0, 1, 4) == 1 && doc.GetPatternAtFrame(0, 3, 4) == 3);	// rewritten where they were
	}

	// Frames with the same cells after share a pattern; the same effect isn't written twice; a full row loses it
	{
		CFamiTrackerDoc doc; MakeSong(doc, 1);
		for (unsigned f = 0; f < 4; ++f) doc.SetPatternAtFrame(0, f, 4, 0);
		std::vector<song_effect_t> effects;
		effects.push_back(song_effect_t{ 0, 1, 2, EF_SPEED, 3 });
		effects.push_back(song_effect_t{ 0, 3, 2, EF_SPEED, 3 });
		CHECK(PlaceSongEffects(&doc, 4, effects) == 0);
		CHECK(doc.GetPatternAtFrame(0, 1, 4) == doc.GetPatternAtFrame(0, 3, 4));
		CHECK(doc.GetPatternAtFrame(0, 1, 4) != 0 && doc.GetPatternAtFrame(0, 0, 4) == 0 && doc.GetPatternAtFrame(0, 2, 4) == 0);
		CHECK(PlaceSongEffects(&doc, 4, effects) == 0);
		CHECK(At(doc, 0, 1, 4, 2).EffNumber[1] == EF_NONE);	// there already
		stChanNote full = N(C, 3);
		for (int k = 0; k < MAX_EFFECT_COLUMNS; ++k) { full.EffNumber[k] = EF_VOLUME_SLIDE; full.EffParam[k] = (unsigned char)k; }
		doc.SetEffColumns(0, 5, 3);
		Set(doc, 0, 0, 5, 1, full);
		std::vector<song_effect_t> one(1, song_effect_t{ 0, 0, 1, EF_HALT, 0 });
		CHECK(PlaceSongEffects(&doc, 5, one) == 1);
		CHECK(!Has(At(doc, 0, 0, 5, 1), EF_HALT, 0));
	}

	// The channels of a set of chips, in chan_id_t order: 2A03, VRC6, MMC5, N163, FDS, VRC7, 5B
	{
		CHECK(ChipChannels(SNDCHIP_NONE, 4) == std::vector<int>({ 0, 1, 2, 3, 4 }));
		CHECK(ChipChannels(SNDCHIP_S5B, 1) == std::vector<int>({ 0, 1, 2, 3, 4, CHANID_S5B_CH1, CHANID_S5B_CH2, CHANID_S5B_CH3 }));
		CHECK(ChipChannels(SNDCHIP_VRC6 | SNDCHIP_N163 | SNDCHIP_S5B, 4) == std::vector<int>({ 0, 1, 2, 3, 4, 5, 6, 7,
			CHANID_N163_CHAN1, CHANID_N163_CHAN2, CHANID_N163_CHAN3, CHANID_N163_CHAN4, CHANID_S5B_CH1, CHANID_S5B_CH2, CHANID_S5B_CH3 }));
		CHECK(ChipChannels(SNDCHIP_MMC5, 1) == std::vector<int>({ 0, 1, 2, 3, 4, CHANID_MMC5_SQUARE1, CHANID_MMC5_SQUARE2 }));
		CHECK(ChipChannels(SNDCHIP_N163 | SNDCHIP_VRC7, 2) == std::vector<int>({ 0, 1, 2, 3, 4, CHANID_N163_CHAN1, CHANID_N163_CHAN2,
			CHANID_VRC7_CH1, CHANID_VRC7_CH2, CHANID_VRC7_CH3, CHANID_VRC7_CH4, CHANID_VRC7_CH5, CHANID_VRC7_CH6 }));
		CHECK(ChipChannels(0x3F, 8).size() == size_t(MAX_CHANNELS));	// every chip on fills the pattern editor
		CHECK(ChipChannels(SNDCHIP_N163, 0).size() == 6 && ChipChannels(SNDCHIP_N163, 9).size() == 13);
		// Where each new channel's notes come from
		const std::vector<int> Vrc6 = ChipChannels(SNDCHIP_VRC6, 1), Both = ChipChannels(SNDCHIP_VRC6 | SNDCHIP_N163, 2);
		CHECK(ChannelSources(Vrc6, Both) == std::vector<int>({ 0, 1, 2, 3, 4, 5, 6, 7, -1, -1 }));
		CHECK(ChannelSources(ChipChannels(SNDCHIP_N163, 2), Both) == std::vector<int>({ 0, 1, 2, 3, 4, -1, -1, -1, 5, 6 }));
		CHECK(ChannelSources(Both, Vrc6) == std::vector<int>({ 0, 1, 2, 3, 4, 5, 6, 7 }));
	}

	// The channels a switch takes away: those of the chips switched off, and the N163's past its new count
	{
		CFamiTrackerDoc doc; MakeSong(doc, 1);
		doc.chips = SNDCHIP_VRC6;
		CHECK(LeavingChannels(&doc, SNDCHIP_N163, 4) == std::vector<unsigned>({ 5, 6, 7 }));
		CHECK(LeavingChannels(&doc, SNDCHIP_VRC6 | SNDCHIP_N163 | SNDCHIP_S5B, 4).empty());
		CHECK(LeavingChannels(&doc, SNDCHIP_NONE, 1) == std::vector<unsigned>({ 5, 6, 7 }));
		doc.namco = 4;
		doc.SelectExpansionChip(SNDCHIP_VRC6 | SNDCHIP_N163);
		CHECK(doc.GetAvailableChannels() == 12 && doc.GetChannelType(8) == CHANID_N163_CHAN1);
		CHECK(LeavingChannels(&doc, SNDCHIP_VRC6 | SNDCHIP_N163, 2) == std::vector<unsigned>({ 10, 11 }));
		CHECK(LeavingChannels(&doc, SNDCHIP_N163 | SNDCHIP_S5B, 4) == std::vector<unsigned>({ 5, 6, 7 }));
	}

	// Switching: the chips that stay keep their notes, frames and columns where they move to
	{
		CFamiTrackerDoc doc; MakeSong(doc, 2);
		doc.chips = SNDCHIP_N163; doc.namco = 2;
		doc.chanIDs = ChipChannels(SNDCHIP_N163, 2);
		Set(doc, 0, 1, 5, 3, N(A, 4, 2));		// Namco 1
		Set(doc, 1, 0, 6, 0, N(C, 3, 2));		// Namco 2, in track 2
		doc.SetPatternAtFrame(0, 2, 5, 9);		// frame 2 plays another pattern
		Set(doc, 0, 2, 5, 0, N(E, 4, 2));
		doc.SetEffColumns(0, 5, 2);
		Set(doc, 0, 0, 0, 0, N(G, 3));			// Pulse 1
		std::vector<int> tracks;
		CHECK(CountChannelNotes(&doc, LeavingChannels(&doc, SNDCHIP_VRC6 | SNDCHIP_N163, 2), tracks) == 0);
		int moved = -1;
		CHECK(SwitchChips(&doc, SNDCHIP_VRC6 | SNDCHIP_N163, 2, moved) == 0 && moved == 0);
		CHECK(doc.chips == (SNDCHIP_VRC6 | SNDCHIP_N163) && doc.GetAvailableChannels() == 10);
		CHECK(doc.GetChannelType(5) == CHANID_VRC6_PULSE1 && doc.GetChannelType(8) == CHANID_N163_CHAN1);
		CHECK(At(doc, 0, 1, 8, 3).Note == A && At(doc, 0, 2, 8, 0).Note == E && At(doc, 1, 0, 9, 0).Note == C);
		CHECK(doc.GetPatternAtFrame(0, 2, 8) == 9 && doc.GetPatternAtFrame(0, 1, 8) == 1);
		CHECK(doc.GetEffColumns(0, 8) == 2 && doc.GetEffColumns(0, 5) == 0);
		CHECK(At(doc, 0, 1, 5, 3).Note == NONE && doc.IsPatternEmpty(0, 5, 1));	// the VRC6 starts empty
		CHECK(At(doc, 0, 0, 0, 0).Note == G);

		// Adding the 5B after them; then the VRC6 goes, with its notes and the loop on its sawtooth
		for (unsigned f = 0; f < 4; ++f) {		// the VRC6's frames all play 00 so far
			doc.SetPatternAtFrame(0, f, 6, f);
			doc.SetPatternAtFrame(0, f, 7, f);
		}
		Set(doc, 0, 3, 7, 15, Fx(EF_JUMP, 0));
		Set(doc, 0, 0, 6, 4, N(D, 2));
		CHECK(SwitchChips(&doc, SNDCHIP_VRC6 | SNDCHIP_N163 | SNDCHIP_S5B, 2, moved) == 0 && doc.GetAvailableChannels() == 13);
		Set(doc, 0, 0, 11, 0, N(F, 3));			// 5B Square 2
		const unsigned char Without = SNDCHIP_N163 | SNDCHIP_S5B;
		CHECK(LeavingChannels(&doc, Without, 2) == std::vector<unsigned>({ 5, 6, 7 }));
		CHECK(CountChannelNotes(&doc, LeavingChannels(&doc, Without, 2), tracks) == 1 && tracks.size() == 1);
		CHECK(SwitchChips(&doc, Without, 2, moved) == 0 && moved == 1);
		CHECK(doc.GetAvailableChannels() == 10 && doc.GetChannelType(5) == CHANID_N163_CHAN1 && doc.GetChannelType(9) == CHANID_S5B_CH3);
		CHECK(At(doc, 0, 1, 5, 3).Note == A && At(doc, 1, 0, 6, 0).Note == C && At(doc, 0, 0, 8, 0).Note == F);
		CHECK(Has(At(doc, 0, 3, 9, 15), EF_JUMP, 0));	// on the last channel now, 5B Square 3
		bool vrc6 = false;
		for (unsigned c = 0; c < doc.GetAvailableChannels(); ++c) vrc6 = vrc6 || At(doc, 0, 0, c, 4).Note == D;
		CHECK(!vrc6);

		// Fewer N163 channels: the ones past the count go
		CHECK(LeavingChannels(&doc, Without, 1) == std::vector<unsigned>({ 6 }));
		CHECK(SwitchChips(&doc, Without, 1, moved) == 0 && doc.GetAvailableChannels() == 9 && doc.namco == 1);
		CHECK(At(doc, 0, 1, 5, 3).Note == A && At(doc, 0, 0, 7, 0).Note == F);
	}

	std::printf("%d checks, %d failed\n", g_checks, g_fail);
	return g_fail == 0 ? 0 : 1;
}

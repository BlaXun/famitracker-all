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

// Tests for the piano roll's note editing (Source/NoteEdit.cpp) on one
// channel's cells in memory.
#include "NoteEdit.cpp"
#include <cstdio>
#include <string>

static int g_checks = 0, g_fail = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)
#define CHECK_EQ(a, b) do { ++g_checks; const std::string x_ = (a), y_ = (b); if (x_ != y_) { ++g_fail; \
	std::printf("FAIL line %d: %s\n   got  %s\n   want %s\n", __LINE__, #a, x_.c_str(), y_.c_str()); } } while (0)

static const char *const NAMES[] = { "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-" };

static int P(int note, int octave) { return octave * 12 + note - 1; }	// a pitch from note_t and octave

static stChanNote Cell(int note, int octave = 0, int inst = MAX_INSTRUMENTS, int vol = MAX_VOLUME)
{
	stChanNote n = CFamiTrackerDoc::Empty();
	n.Note = (unsigned char)note; n.Octave = (unsigned char)octave; n.Instrument = (unsigned char)inst; n.Vol = (unsigned char)vol;
	return n;
}

static stChanNote With(stChanNote n, int column, int effect, int param)
{
	n.EffNumber[column] = (unsigned char)effect; n.EffParam[column] = (unsigned char)param;
	return n;
}

static stChanNote At(const CChannelNotes &ch, int row) { stChanNote n; ch.GetCell(row, n); return n; }

static CChannelNotes Channel(int rows, int columns = 2)
{
	CChannelNotes ch;
	ch.Reset(rows);
	ch.SetEffectColumns(columns);
	return ch;
}

// "C-4 0-4>" per note: its pitch where it lands, rows, and how it ends: = release, - halt,
// > into the next note, | at the end
static std::string Notes(const CChannelNotes &ch)
{
	std::string s;
	for (int i = 0; i < ch.GetNoteCount(); ++i) {
		const CChannelNotes::Note &n = ch.GetNote(i);
		char b[64];
		const char how = n.Released ? '=' : n.Halted ? '-' : (n.End == ch.GetRows() ? '|' : '>');
		std::snprintf(b, sizeof b, "%s%s%d %d-%d%c", s.empty() ? "" : " ", NAMES[n.Pitch % 12], n.Pitch / 12, n.Start, n.End, how);
		s += b;
	}
	return s;
}

// "i v" per note: the instrument and volume in effect
static std::string Heard(const CChannelNotes &ch)
{
	std::string s;
	for (int i = 0; i < ch.GetNoteCount(); ++i) {
		char b[32];
		std::snprintf(b, sizeof b, "%s%d %d", s.empty() ? "" : ", ", ch.GetNote(i).Instrument, ch.GetNote(i).Volume);
		s += b;
	}
	return s;
}

static bool HasEffect(const CChannelNotes &ch, int row, int effect, int param)
{
	const stChanNote n = At(ch, row);
	for (int i = 0; i < MAX_EFFECT_COLUMNS; ++i)
		if (n.EffNumber[i] == effect && n.EffParam[i] == param)
			return true;
	return false;
}

static void TestReading()
{
	CChannelNotes ch = Channel(32);
	ch.SetCell(0, Cell(C, 4, 1, 12));
	ch.SetCell(4, Cell(E, 4));
	ch.SetCell(8, Cell(RELEASE));
	ch.SetCell(12, With(Cell(G, 4, 2), 0, EF_SLIDE_UP, 0x32));	// G-4 sliding up 2 to A-4
	ch.SetCell(16, Cell(HALT, 0, MAX_INSTRUMENTS, 6));		// a halt's volume counts later on
	ch.SetCell(20, With(Cell(D, 5), 1, EF_SLIDE_DOWN, 0x13));	// D-5 sliding down 3 to B-4
	CHECK_EQ(Notes(ch), "C-4 0-4> E-4 4-8= A-4 12-16- B-4 20-32|");
	CHECK_EQ(Heard(ch), "1 12, 1 12, 2 12, 2 6");
	CHECK(ch.GetNote(2).Written == P(G, 4) && ch.GetNote(3).Written == P(D, 5));
	CHECK(ch.FindNote(12) == 2 && ch.FindNote(13) == -1);
	CHECK(ch.NoteAt(3) == 0 && ch.NoteAt(8) == -1 && ch.NoteAt(15) == 2 && ch.NoteAt(31) == 3);

	// A slide in an effect column that doesn't play doesn't count
	ch.SetEffectColumns(1);
	CHECK(ch.GetNote(3).Pitch == P(D, 5));
}

static void TestAdd()
{
	// On an empty channel: a halt after it, or a release for an instrument that fades out on one
	CChannelNotes ch = Channel(32);
	ch.Add(4, 8, P(C, 4), 0);
	CHECK_EQ(Notes(ch), "C-4 4-8-");
	CHECK(At(ch, 4).Instrument == 0 && At(ch, 4).Vol == MAX_VOLUME);
	ch.SetCanRelease(1, true);
	ch.Add(12, 14, P(D, 4), 1);
	CHECK_EQ(Notes(ch), "C-4 4-8- D-4 12-14=");

	// Right after a note: its halt makes way, and it runs on into the new one
	ch.Add(8, 10, P(E, 4), 0);
	CHECK_EQ(Notes(ch), "C-4 4-8> E-4 8-10- D-4 12-14=");

	// Ending where the next note starts: no halt in between
	ch.Add(10, 12, P(F, 4), 0);
	CHECK_EQ(Notes(ch), "C-4 4-8> E-4 8-10> F-4 10-12> D-4 12-14=");

	// Over the middle of a long note: it is cut there; the notes starting under the new one make way
	CChannelNotes l = Channel(32);
	l.SetCell(0, Cell(C, 3, 0));
	l.SetCell(16, Cell(RELEASE));
	l.SetCell(20, Cell(G, 3, 0));
	l.SetCell(22, Cell(A, 3, 0));
	l.SetCell(24, Cell(HALT));
	l.Add(4, 8, P(E, 3), 0);
	CHECK_EQ(Notes(l), "C-3 0-4> E-3 4-8- G-3 20-22> A-3 22-24-");
	CHECK(At(l, 16).Note == NONE);	// the release of the long note went with its end
	l.Add(18, 23, P(B, 3), 0);
	CHECK_EQ(Notes(l), "C-3 0-4> E-3 4-8- B-3 18-23-");
	CHECK(At(l, 24).Note == NONE);	// the covered note's halt went with it

	// The noise channel's 16 pitches
	CChannelNotes noise = Channel(8);
	noise.SetPitchRange(0, 15);
	noise.Add(0, 1, 40, 0);
	CHECK(noise.GetNote(0).Pitch == 15);
}

static void TestRemove()
{
	CChannelNotes ch = Channel(32);
	ch.SetCanRelease(3, true);
	ch.SetCell(0, Cell(C, 4, 3, 10));
	ch.SetCell(4, Cell(D, 4, MAX_INSTRUMENTS, 7));
	ch.SetCell(8, Cell(E, 4));
	ch.SetCell(12, Cell(RELEASE));
	CHECK_EQ(Heard(ch), "3 10, 3 7, 3 7");

	// The middle one: the note before ends where it began (the instrument fades out: ===), and the
	// last note keeps its volume of 7, now written in its own cell
	ch.Remove(4);
	CHECK_EQ(Notes(ch), "C-4 0-4= E-4 8-12=");
	CHECK_EQ(Heard(ch), "3 10, 3 7");
	CHECK(At(ch, 8).Vol == 7);
	ch.Remove(5);	// no note starts there
	CHECK_EQ(Notes(ch), "C-4 0-4= E-4 8-12=");

	// A note with vibrato that ran on from the note before: its 400 stays, so the vibrato doesn't run on
	CChannelNotes v = Channel(32);
	v.SetCell(0, With(Cell(C, 4, 0), 0, EF_VIBRATO, 0x63));
	v.SetCell(8, Cell(D, 4));
	v.SetCell(15, With(Cell(NONE), 0, EF_VIBRATO, 0x00));
	v.SetCell(16, Cell(HALT));
	v.SetCell(20, Cell(E, 4));
	v.Remove(8);
	CHECK_EQ(Notes(v), "C-4 0-8- E-4 20-32|");
	CHECK(HasEffect(v, 8, EF_VIBRATO, 0) && At(v, 15).EffNumber[0] == EF_NONE && At(v, 16).Note == NONE);
}

static void TestResize()
{
	// A recorded note: vibrato from row 4, switched off on its last row, a release after it
	CChannelNotes ch = Channel(32);
	ch.SetCell(0, Cell(A, 4, 0));
	ch.SetCell(4, With(Cell(NONE), 0, EF_VIBRATO, 0x52));
	ch.SetCell(15, With(Cell(NONE), 0, EF_VIBRATO, 0x00));
	ch.SetCell(16, With(Cell(RELEASE), 1, EF_VOLUME_SLIDE, 0x00));
	ch.SetCell(24, Cell(B, 4));

	// Shorter: the release moves with its effects, and the 400 cut off goes there too
	ch.Resize(0, 10);
	CHECK_EQ(Notes(ch), "A-4 0-10= B-4 24-32|");
	CHECK(HasEffect(ch, 10, EF_VIBRATO, 0) && HasEffect(ch, 10, EF_VOLUME_SLIDE, 0));
	CHECK(HasEffect(ch, 4, EF_VIBRATO, 0x52) && At(ch, 15).EffNumber[0] == EF_NONE && At(ch, 16).Note == NONE);

	// Longer, but no further than the next note. Running on into it, what its release switched off
	// is switched off on its last row instead, and the next note isn't changed.
	ch.Resize(0, 40);
	CHECK_EQ(Notes(ch), "A-4 0-24> B-4 24-32|");
	CHECK(!HasEffect(ch, 10, EF_VIBRATO, 0) && At(ch, 10).Note == NONE);
	CHECK(HasEffect(ch, 23, EF_VIBRATO, 0) && HasEffect(ch, 23, EF_VOLUME_SLIDE, 0));
	CHECK(At(ch, 24).EffNumber[0] == EF_NONE && At(ch, 24).EffNumber[1] == EF_NONE);

	// Back to 20 rows, then 22: its end and what it switches off move along
	ch.Resize(0, 20);
	CHECK_EQ(Notes(ch), "A-4 0-20- B-4 24-32|");
	ch.Resize(0, 22);
	CHECK_EQ(Notes(ch), "A-4 0-22- B-4 24-32|");
	CHECK(At(ch, 20).Note == NONE && At(ch, 22).Note == HALT);

	// A song-wide effect inside stays where it is
	CChannelNotes s = Channel(32);
	s.SetCell(0, Cell(C, 4, 0));
	s.SetCell(6, With(Cell(NONE), 1, EF_JUMP, 0x02));
	s.SetCell(12, Cell(HALT));
	s.Resize(0, 4);
	CHECK_EQ(Notes(s), "C-4 0-4-");
	CHECK(HasEffect(s, 6, EF_JUMP, 0x02));
}

static void TestMove()
{
	// Three notes in a row, the first setting instrument 2 and volume 9
	CChannelNotes ch = Channel(48);
	ch.SetCell(0, Cell(C, 4, 2, 9));
	ch.SetCell(4, Cell(D, 4));
	ch.SetCell(8, Cell(E, 4));
	ch.SetCell(12, Cell(HALT));

	// The first one two beats later: the others keep instrument 2 and volume 9, and so does it
	const int first[] = { 0 };
	const int up[] = { P(C, 4) };
	ch.Move(first, 1, 16, up, false);
	CHECK_EQ(Notes(ch), "D-4 4-8> E-4 8-12- C-4 16-20-");
	CHECK_EQ(Heard(ch), "2 9, 2 9, 2 9");
	CHECK(At(ch, 4).Instrument == 2 && At(ch, 4).Vol == 9 && At(ch, 8).Vol == MAX_VOLUME);

	// Up a fifth, in place: a slide keeps sliding the same way
	CChannelNotes s = Channel(16);
	s.SetCell(0, With(Cell(C, 4, 0), 0, EF_SLIDE_UP, 0x42));	// C-4 to D-4
	s.SetCell(8, Cell(HALT));
	const int at0[] = { 0 };
	const int fifth[] = { P(A, 4) };	// lands on A-4: written G-4
	s.Move(at0, 1, 0, fifth, false);
	CHECK_EQ(Notes(s), "A-4 0-8-");
	CHECK(At(s, 0).Note == G && At(s, 0).Octave == 4 && HasEffect(s, 0, EF_SLIDE_UP, 0x42));

	// A note moved onto the middle of another: that one is cut and runs on into it
	CChannelNotes m = Channel(32);
	m.SetCell(0, Cell(C, 4, 0));
	m.SetCell(16, Cell(RELEASE));
	m.SetCell(20, Cell(G, 4, 1));
	m.SetCell(24, Cell(HALT));
	const int g[] = { 20 };
	const int gp[] = { P(G, 4) };
	m.Move(g, 1, -12, gp, false);
	CHECK_EQ(Notes(m), "C-4 0-8> G-4 8-12-");
	CHECK(At(m, 16).Note == NONE && At(m, 20).Note == NONE && At(m, 24).Note == NONE);

	// Notes that run into each other, moved together, still do
	CChannelNotes t = Channel(32);
	t.SetCell(0, Cell(C, 4, 0));
	t.SetCell(2, Cell(D, 4));
	t.SetCell(4, Cell(E, 4));
	t.SetCell(6, Cell(HALT));
	const int two[] = { 2, 4 };
	const int same[] = { P(D, 4), P(E, 4) };
	t.Move(two, 2, 8, same, false);
	CHECK_EQ(Notes(t), "C-4 0-2- D-4 10-12> E-4 12-14-");

	// Copies leave the notes where they were
	CChannelNotes c = Channel(32);
	c.SetCell(0, Cell(C, 4, 0, 5));
	c.SetCell(4, Cell(HALT));
	const int c0[] = { 0 };
	const int cp[] = { P(C, 5) };
	c.Move(c0, 1, 8, cp, true);
	CHECK_EQ(Notes(c), "C-4 0-4- C-5 8-12-");
	CHECK_EQ(Heard(c), "0 5, 0 5");

	// Effects go with the note; a song-wide one stays on its row
	CChannelNotes e = Channel(32, 3);
	e.SetCell(0, With(With(Cell(C, 4, 0), 0, EF_ARPEGGIO, 0x37), 2, EF_SPEED, 0x05));
	e.SetCell(2, With(Cell(NONE), 1, EF_VIBRATO, 0x44));
	e.SetCell(4, With(Cell(RELEASE), 0, EF_ARPEGGIO, 0x00));
	const int e0[] = { 0 };
	const int ep[] = { P(C, 4) };
	e.Move(e0, 1, 10, ep, false);
	CHECK_EQ(Notes(e), "C-4 10-14=");
	CHECK(HasEffect(e, 0, EF_SPEED, 0x05) && At(e, 0).EffNumber[0] == EF_ARPEGGIO && At(e, 0).EffParam[0] == 0x00);	// 000 stays
	CHECK(HasEffect(e, 10, EF_ARPEGGIO, 0x37) && HasEffect(e, 12, EF_VIBRATO, 0x44) && HasEffect(e, 14, EF_ARPEGGIO, 0x00));
	CHECK(!HasEffect(e, 2, EF_VIBRATO, 0x44));
}

static void TestColumns()
{
	// Effects in columns that don't play are left alone, and a moved note's effects don't land on a
	// song-wide one
	CChannelNotes ch = Channel(16, 2);
	stChanNote hidden = With(Cell(C, 4, 0), 3, EF_VIBRATO, 0x11);
	ch.SetCell(0, With(hidden, 0, EF_DUTY_CYCLE, 0x02));
	ch.SetCell(4, Cell(HALT));
	ch.SetCell(8, With(Cell(NONE), 0, EF_SKIP, 0x00));
	const int n[] = { 0 };
	const int p[] = { P(C, 4) };
	ch.Move(n, 1, 8, p, false);
	CHECK_EQ(Notes(ch), "C-4 8-12-");
	CHECK(At(ch, 0).EffNumber[3] == EF_VIBRATO);	// stayed
	CHECK(At(ch, 8).EffNumber[0] == EF_SKIP && At(ch, 8).EffNumber[1] == EF_DUTY_CYCLE && At(ch, 8).EffParam[1] == 0x02);
}

static void TestCopyPaste()
{
	CChannelNotes ch = Channel(64);
	ch.SetCell(0, Cell(C, 4, 1, 8));
	ch.SetCell(4, Cell(D, 4));			// runs into E-4
	ch.SetCell(8, Cell(E, 4));
	ch.SetCell(10, Cell(RELEASE));
	ch.SetCell(12, Cell(F, 4, 2));
	const int some[] = { 8, 4 };		// D-4 and E-4, in any order
	const CChannelNotes clip = ch.Copy(some, 2);
	CHECK_EQ(Notes(clip), "D-4 0-4> E-4 4-6=");
	CHECK(clip.GetRows() == 7);

	// Pasted after the F-4 (instrument 2), a fifth higher: they keep instrument 1 and volume 8
	ch.Paste(clip, 32, 7);
	CHECK_EQ(Notes(ch), "C-4 0-4> D-4 4-8> E-4 8-10= F-4 12-32> A-4 32-36> B-4 36-38=");
	CHECK_EQ(Heard(ch), "1 8, 1 8, 1 8, 2 8, 1 8, 1 8");
	CHECK(At(ch, 32).Instrument == 1 && At(ch, 32).Vol == MAX_VOLUME);	// volume 8 was in effect there anyway

	// A note that ran into one left out of the copy keeps its length
	const int d4[] = { 4 };
	CHECK_EQ(Notes(ch.Copy(d4, 1)), "D-4 0-4-");
	CHECK(ch.Copy(d4, 0).GetRows() == 0);

	// A copy made self-contained (for a clip): its first note says how it sounded, the next one
	// takes that over
	CChannelNotes own = clip;
	CHECK(At(own, 0).Instrument == MAX_INSTRUMENTS && At(own, 0).Vol == MAX_VOLUME);
	own.SelfContained();
	CHECK(At(own, 0).Instrument == 1 && At(own, 0).Vol == 8);
	CHECK(At(own, 4).Instrument == MAX_INSTRUMENTS && At(own, 4).Vol == MAX_VOLUME);
	CHECK_EQ(Heard(own), "1 8, 1 8");
}

static void TestInstrument()
{
	// Another instrument for the first note: the second, which took its instrument over, keeps it
	CChannelNotes ch = Channel(16);
	ch.SetCell(0, Cell(C, 4, 1));
	ch.SetCell(4, Cell(D, 4));
	ch.SetCell(8, Cell(E, 4, 3));
	const int first[] = { 0 };
	ch.SetInstrument(first, 1, 2);
	CHECK_EQ(Heard(ch), "2 15, 1 15, 3 15");
	CHECK(At(ch, 0).Instrument == 2 && At(ch, 4).Instrument == 1 && At(ch, 8).Instrument == 3);
	const int none[] = { 5 };
	ch.SetInstrument(none, 1, 4);	// no note starts there
	CHECK_EQ(Heard(ch), "2 15, 1 15, 3 15");
}

static void TestCompare()
{
	CChannelNotes a = Channel(32), b = Channel(32);
	int first = 0, last = 0;
	CHECK(!a.Compare(b, first, last));
	b.Add(6, 9, P(C, 4), 0);
	CHECK(a.Compare(b, first, last) && first == 6 && last == 9);
	b.SetRows(40);
	b.Add(36, 38, P(C, 4), 0);
	CHECK(a.Compare(b, first, last) && first == 6 && last == 38);
}

int main()
{
	TestReading();
	TestAdd();
	TestRemove();
	TestResize();
	TestMove();
	TestColumns();
	TestCopyPaste();
	TestInstrument();
	TestCompare();
	std::printf("%d checks, %d failed\n", g_checks, g_fail);
	return g_fail == 0 ? 0 : 1;
}

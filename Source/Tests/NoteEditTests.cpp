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

// The pattern editor's letter for an effect
static char Letter(int effect)
{
	switch (effect) {
	case EF_ARPEGGIO: return '0';
	case EF_PORTA_UP: return '1';
	case EF_PORTA_DOWN: return '2';
	case EF_PORTAMENTO: return '3';
	case EF_VIBRATO: return '4';
	case EF_TREMOLO: return '7';
	case EF_VOLUME_SLIDE: return 'A';
	case EF_DELAY: return 'G';
	case EF_PITCH: return 'P';
	case EF_SLIDE_UP: return 'Q';
	case EF_SLIDE_DOWN: return 'R';
	case EF_NOTE_CUT: return 'S';
	case EF_DUTY_CYCLE: return 'V';
	}
	return '?';
}

// "452 2-8 R22 10" per effect at work while a note sounds: the effect, and the rows it works on
// (one row: just that one)
static std::string Effects(const CChannelNotes &ch, int index)
{
	std::vector<CChannelNotes::EffectSpan> spans;
	ch.GetEffects(index, spans);
	std::string s;
	for (size_t i = 0; i < spans.size(); ++i) {
		char b[32];
		if (spans[i].To == spans[i].From + 1)
			std::snprintf(b, sizeof b, "%s%c%02X %d", s.empty() ? "" : " ", Letter(spans[i].Number), spans[i].Param, spans[i].From);
		else
			std::snprintf(b, sizeof b, "%s%c%02X %d-%d", s.empty() ? "" : " ", Letter(spans[i].Number), spans[i].Param, spans[i].From,
				spans[i].To);
		s += b;
	}
	return s;
}

static void TestEffectsShown()
{
	CChannelNotes ch = Channel(48);
	ch.SetCell(0, With(Cell(C, 4, 0), 0, EF_ARPEGGIO, 0x47));		// a chord...
	ch.SetCell(2, With(Cell(NONE), 1, EF_VIBRATO, 0x52));			// ...and vibrato from its third row
	ch.SetCell(4, Cell(D, 4));										// the next note takes both over
	ch.SetCell(8, With(Cell(E, 4), 0, EF_ARPEGGIO, 0x00));			// the chord stops
	ch.SetCell(10, With(Cell(NONE), 0, EF_SLIDE_DOWN, 0x22));		// a bend down
	ch.SetCell(12, With(With(Cell(F, 4), 0, EF_SLIDE_UP, 0x31), 1, EF_VIBRATO, 0x00));	// slides in, no vibrato
	ch.SetCell(16, With(Cell(RELEASE), 0, EF_DELAY, 0x02));		// not part of the note
	ch.SetCell(20, With(With(Cell(G, 4), 0, EF_PORTAMENTO, 0x06), 1, EF_DUTY_CYCLE, 0x01));	// glides in
	ch.SetCell(24, With(Cell(A, 4), 0, EF_VOLUME_SLIDE, 0x02));	// glides too, and fades
	ch.SetCell(28, With(Cell(HALT), 1, EF_SPEED, 0x05));
	CHECK_EQ(Notes(ch), "C-4 0-4> D-4 4-8> E-4 8-12> F#4 12-16= G-4 20-24> A-4 24-28-");
	CHECK_EQ(Effects(ch, 0), "047 0-4 452 2-4");
	CHECK_EQ(Effects(ch, 1), "047 4-8 452 4-8");
	CHECK_EQ(Effects(ch, 2), "452 8-12 R22 10");
	CHECK_EQ(Effects(ch, 3), "Q31 12");
	CHECK_EQ(Effects(ch, 4), "306 20-24 V01 20");
	CHECK_EQ(Effects(ch, 5), "306 24-28 A02 24-28");
	CHECK_EQ(Effects(ch, 6), "");	// no such note
	// Which ones a note took over from an earlier one
	std::vector<CChannelNotes::EffectSpan> spans;
	ch.GetEffects(0, spans);
	CHECK(spans.size() == 2 && !spans[0].Carried && !spans[1].Carried);
	ch.GetEffects(1, spans);
	CHECK(spans.size() == 2 && spans[0].Carried && spans[1].Carried);
	ch.GetEffects(4, spans);
	CHECK(spans.size() == 2 && !spans[0].Carried);
	ch.GetEffects(5, spans);
	CHECK(spans.size() == 2 && spans[0].Carried && !spans[1].Carried);

	// A new note ends a slide that isn't its own; effects in columns that don't play don't count
	CChannelNotes s = Channel(16, 1);
	s.SetCell(0, With(Cell(C, 4, 0), 0, EF_PORTA_UP, 0x10));
	s.SetCell(2, With(Cell(NONE), 0, EF_SLIDE_UP, 0x41));
	s.SetCell(4, With(Cell(D, 4), 1, EF_VIBRATO, 0x44));
	CHECK_EQ(Effects(s, 0), "110 0-2 Q41 2");
	CHECK_EQ(Effects(s, 1), "");
}

static void TestSetLasting()
{
	// Vibrato on the second of three notes, from its second row: the third doesn't take it over
	CChannelNotes ch = Channel(32);
	ch.SetCell(0, Cell(C, 4, 0));
	ch.SetCell(4, Cell(D, 4));
	ch.SetCell(8, Cell(E, 4));
	ch.SetCell(12, Cell(HALT));
	const int second[] = { 4 }, third[] = { 8 }, first[] = { 0 };
	const unsigned char gentle[] = { 0x52 }, off[] = { 0x00 };
	const int one[] = { 1 }, none[] = { 0 };
	CHECK(ch.SetLasting(second, 1, EF_VIBRATO, gentle, one));
	CHECK(HasEffect(ch, 5, EF_VIBRATO, 0x52) && HasEffect(ch, 8, EF_VIBRATO, 0x00) && !HasEffect(ch, 4, EF_VIBRATO, 0x00));
	CHECK_EQ(Effects(ch, 1), "452 5-8");
	CHECK_EQ(Effects(ch, 2), "");
	CHECK_EQ(Notes(ch), "C-4 0-4> D-4 4-8> E-4 8-12-");

	// On the last note too: it starts without (the one before had it), and stops at its end, so it
	// doesn't go on into the song's loop
	CHECK(ch.SetLasting(third, 1, EF_VIBRATO, gentle, one));
	CHECK(HasEffect(ch, 8, EF_VIBRATO, 0x00) && HasEffect(ch, 9, EF_VIBRATO, 0x52) && HasEffect(ch, 12, EF_VIBRATO, 0x00));
	CHECK_EQ(Effects(ch, 2), "452 9-12");

	// Taken off the second: the third keeps its own
	CHECK(ch.SetLasting(second, 1, EF_VIBRATO, off, none));
	CHECK(!HasEffect(ch, 5, EF_VIBRATO, 0x52));
	CHECK_EQ(Effects(ch, 1), "");
	CHECK_EQ(Effects(ch, 2), "452 9-12");

	// A note in the middle of one that came from earlier: switched off on it, back on for the next
	CChannelNotes v = Channel(32);
	v.SetCell(0, With(Cell(C, 4, 0), 0, EF_TREMOLO, 0x63));
	v.SetCell(4, Cell(D, 4));
	v.SetCell(8, Cell(E, 4));
	CHECK(v.SetLasting(second, 1, EF_TREMOLO, off, none));
	CHECK(HasEffect(v, 4, EF_TREMOLO, 0x00) && HasEffect(v, 8, EF_TREMOLO, 0x63));
	CHECK_EQ(Effects(v, 1), "");
	CHECK_EQ(Effects(v, 2), "763 8-32");

	// A chord per note, on two notes in a row; the note after them plays without
	CChannelNotes c = Channel(32, 1);
	c.SetCell(0, Cell(C, 4, 0));
	c.SetCell(4, Cell(D, 4));
	c.SetCell(8, Cell(E, 4));
	c.SetCell(12, Cell(HALT));
	const int two[] = { 4, 0 };
	const unsigned char chords[] = { 0x37, 0x47 };	// D minor, C major
	const int zero[] = { 0, 0 };
	CHECK(c.SetLasting(two, 2, EF_ARPEGGIO, chords, zero));
	CHECK(HasEffect(c, 0, EF_ARPEGGIO, 0x47) && HasEffect(c, 4, EF_ARPEGGIO, 0x37) && HasEffect(c, 8, EF_ARPEGGIO, 0x00));
	CHECK_EQ(Effects(c, 1), "037 4-8");
	CHECK_EQ(Effects(c, 2), "");

	// A chord on a note that slid in: it plays where it landed instead
	CChannelNotes q = Channel(16);
	q.SetCell(0, With(Cell(C, 4, 0), 0, EF_SLIDE_UP, 0x32));	// lands on D-4
	q.SetCell(8, Cell(HALT));
	CHECK(q.SetLasting(first, 1, EF_ARPEGGIO, chords, none));
	CHECK(At(q, 0).Note == D && !HasEffect(q, 0, EF_SLIDE_UP, 0x32) && HasEffect(q, 0, EF_ARPEGGIO, 0x37) && HasEffect(q, 8, EF_ARPEGGIO, 0x00));
	CHECK_EQ(Notes(q), "D-4 0-8-");

	// Gliding into the second note; the third doesn't glide
	CChannelNotes g = Channel(32);
	g.SetCell(0, Cell(C, 4, 0));
	g.SetCell(4, Cell(G, 4));
	g.SetCell(8, Cell(E, 4));
	g.SetCell(12, Cell(HALT));
	const unsigned char glide[] = { 0x06 };
	CHECK(g.SetLasting(second, 1, EF_PORTAMENTO, glide, one));	// a delay doesn't apply to it
	CHECK(HasEffect(g, 4, EF_PORTAMENTO, 0x06) && HasEffect(g, 8, EF_PORTAMENTO, 0x00));
	CHECK_EQ(Effects(g, 1), "306 4-8");

	// Fading out over the second half: the next note gets back the volume it had
	CChannelNotes f = Channel(32, 1);
	f.SetCell(0, Cell(C, 4, 0, 12));
	f.SetCell(8, Cell(D, 4));
	f.SetCell(16, Cell(HALT));
	const unsigned char fade[] = { 0x02 };
	const int half[] = { 4 };
	CHECK(f.SetLasting(first, 1, EF_VOLUME_SLIDE, fade, half));
	CHECK(HasEffect(f, 4, EF_VOLUME_SLIDE, 0x02) && HasEffect(f, 8, EF_VOLUME_SLIDE, 0x00) && At(f, 8).Vol == 12);
	CHECK_EQ(Effects(f, 0), "A02 4-8");

	// No room: one effect column, and the next note's is taken; the rest is done
	CChannelNotes full = Channel(16, 1);
	full.SetCell(0, Cell(C, 4, 0));
	full.SetCell(4, With(Cell(D, 4), 0, EF_DUTY_CYCLE, 0x02));
	full.SetCell(8, Cell(HALT));
	CHECK(!full.SetLasting(first, 1, EF_VIBRATO, gentle, none));
	CHECK(HasEffect(full, 0, EF_VIBRATO, 0x52) && HasEffect(full, 4, EF_DUTY_CYCLE, 0x02));

	// Kinds that aren't put on notes this way, and notes that aren't there, change nothing
	CChannelNotes same = ch;
	CHECK(ch.SetLasting(first, 1, EF_DUTY_CYCLE, gentle, none) && ch.SetLasting(first, 1, EF_SPEED, gentle, none));
	const int nowhere[] = { 3 };
	CHECK(ch.SetLasting(nowhere, 1, EF_VIBRATO, gentle, none));
	int a = 0, b = 0;
	CHECK(!ch.Compare(same, a, b));
}

static void TestSlidesAndBends()
{
	// Sliding into the second note from below: it starts lower and still lands on E-4; the chord the
	// first note had can't go on through a slide
	CChannelNotes s = Channel(16);
	s.SetCell(0, With(Cell(C, 4, 0), 0, EF_ARPEGGIO, 0x47));
	s.SetCell(4, Cell(E, 4));
	s.SetCell(8, Cell(HALT));
	const int e[] = { 4 };
	CHECK(s.SetSlideIn(e, 1, 2, 3));
	CHECK_EQ(Notes(s), "C-4 0-4> E-4 4-8-");
	CHECK(At(s, 4).Note == D && At(s, 4).Octave == 4 && HasEffect(s, 4, EF_SLIDE_UP, 0x32));
	CHECK_EQ(Effects(s, 1), "Q32 4");

	// From above, then none: it plays where it landed, still without the chord
	CHECK(s.SetSlideIn(e, 1, -1, 3));
	CHECK(At(s, 4).Note == F && HasEffect(s, 4, EF_SLIDE_DOWN, 0x31) && !HasEffect(s, 4, EF_SLIDE_UP, 0x32));
	CHECK(s.SetSlideIn(e, 1, 0, 3));
	CHECK(At(s, 4).Note == E && !HasEffect(s, 4, EF_SLIDE_DOWN, 0x31) && HasEffect(s, 4, EF_ARPEGGIO, 0x00));
	CHECK_EQ(Effects(s, 1), "");
	CHECK_EQ(Notes(s), "C-4 0-4> E-4 4-8-");

	// A slide can't start below the lowest note
	CChannelNotes low = Channel(8);
	low.SetCell(0, Cell(Cs, 0, 0));
	const int at0[] = { 0 };
	CHECK(low.SetSlideIn(at0, 1, 5, 2));
	CHECK(At(low, 0).Note == C && At(low, 0).Octave == 0 && HasEffect(low, 0, EF_SLIDE_UP, 0x21));

	// Bends two rows before the end; a note of one row has no room for one
	CChannelNotes b = Channel(32);
	b.SetCell(0, Cell(C, 4, 0));
	b.SetCell(8, Cell(D, 4));
	b.SetCell(9, Cell(HALT));
	const int both[] = { 0, 8 };
	CHECK(b.SetBend(both, 2, -2, 4, 2));
	CHECK(HasEffect(b, 6, EF_SLIDE_DOWN, 0x42) && At(b, 8).EffNumber[0] == EF_NONE && At(b, 8).EffNumber[1] == EF_NONE);
	CHECK_EQ(Effects(b, 0), "R42 6");
	CHECK(b.SetBend(both, 2, 1, 2, 2));
	CHECK(HasEffect(b, 6, EF_SLIDE_UP, 0x21) && !HasEffect(b, 6, EF_SLIDE_DOWN, 0x42));
	CHECK(b.SetBend(both, 2, 0, 4, 2));
	CHECK_EQ(Effects(b, 0), "");
}

static void TestClearEffects()
{
	CChannelNotes x = Channel(32, 3);
	x.SetCell(0, With(With(Cell(C, 4, 0), 0, EF_VIBRATO, 0x44), 1, EF_DUTY_CYCLE, 0x01));
	x.SetCell(4, With(With(Cell(D, 4), 0, EF_SLIDE_UP, 0x32), 2, EF_SPEED, 0x04));	// lands on E-4
	x.SetCell(6, With(Cell(NONE), 0, EF_NOTE_CUT, 0x03));
	x.SetCell(8, Cell(E, 4));
	x.SetCell(12, Cell(HALT));
	CHECK_EQ(Effects(x, 1), "444 4-8 Q32 4 S03 6");

	// The middle note: plain, where it landed; the vibrato that came from the first is switched off
	// for it and back on for the last, the speed stays
	const int mid[] = { 4 };
	CHECK(x.ClearEffects(mid, 1));
	CHECK(At(x, 4).Note == E && HasEffect(x, 4, EF_SPEED, 0x04) && HasEffect(x, 4, EF_VIBRATO, 0x00) && !HasEffect(x, 6, EF_NOTE_CUT, 0x03));
	CHECK(HasEffect(x, 8, EF_VIBRATO, 0x44));
	CHECK_EQ(Effects(x, 1), "");
	CHECK_EQ(Effects(x, 2), "444 8-12");
	CHECK_EQ(Notes(x), "C-4 0-4> E-4 4-8> E-4 8-12-");

	// The first: its duty goes, so the next note gets it, which it had taken over
	const int head[] = { 0 };
	CHECK(x.ClearEffects(head, 1));
	CHECK(!HasEffect(x, 0, EF_DUTY_CYCLE, 0x01) && !HasEffect(x, 0, EF_VIBRATO, 0x44) && HasEffect(x, 4, EF_DUTY_CYCLE, 0x01));
	CHECK_EQ(Effects(x, 0), "");
}

// "L R C 40" per note: its pan (L 00, C 80, R FF, otherwise in hex)
static std::string Pans(const CChannelNotes &ch)
{
	std::string s;
	for (int i = 0; i < ch.GetNoteCount(); ++i) {
		const int p = ch.GetNote(i).Pan;
		char b[8];
		if (p == PAN_LEFT || p == PAN_CENTER || p == PAN_RIGHT)
			std::snprintf(b, sizeof b, "%c", p == PAN_LEFT ? 'L' : p == PAN_CENTER ? 'C' : 'R');
		else
			std::snprintf(b, sizeof b, "%02X", p);
		s += (s.empty() ? "" : " ") + std::string(b);
	}
	return s;
}

static int PanEffects(const CChannelNotes &ch)
{
	int n = 0;
	for (int r = 0; r < ch.GetRows(); ++r)
		for (int i = 0; i < MAX_EFFECT_COLUMNS; ++i)
			n += At(ch, r).EffNumber[i] == EF_PAN;
	return n;
}

static void TestPan()
{
	// Read: a pan lasts from note to note, in the effect columns that play, the last on a row winning
	CChannelNotes ch = Channel(32);
	ch.SetCell(0, Cell(C, 4, 0));
	ch.SetCell(4, With(Cell(D, 4), 1, EF_PAN, PAN_LEFT));
	ch.SetCell(8, Cell(E, 4));
	ch.SetCell(12, With(With(Cell(F, 4), 0, EF_PAN, 0x40), 1, EF_PAN, PAN_RIGHT));
	ch.SetCell(16, Cell(G, 4));
	CHECK_EQ(Pans(ch), "C L L R R");
	ch.SetEffectColumns(1);
	CHECK_EQ(Pans(ch), "C C C 40 40");
	ch.SetEffectColumns(2);
	CHECK_EQ(Effects(ch, 1), "");	// the pan isn't one of the effects shown on notes
	const int at0[] = { 0 }, none[] = { 0 };
	const unsigned char right[] = { PAN_RIGHT };
	CHECK(ch.SetLasting(at0, 1, EF_PAN, right, none));	// not a lasting effect SetLasting puts on: SetPan does
	CHECK_EQ(Pans(ch), "C L L R R");

	// Set on a note: written on its row, the next note keeps the pan it had
	CChannelNotes s = Channel(32);
	s.SetCell(0, Cell(C, 4, 0));
	s.SetCell(4, Cell(D, 4));
	s.SetCell(8, Cell(E, 4));
	s.SetCell(12, Cell(HALT));
	const int d4[] = { 4 };
	CHECK(s.SetPan(d4, 1, PAN_LEFT));
	CHECK_EQ(Pans(s), "C L C");
	CHECK(HasEffect(s, 4, EF_PAN, PAN_LEFT) && HasEffect(s, 8, EF_PAN, PAN_CENTER) && PanEffects(s) == 2);
	CHECK_EQ(Notes(s), "C-4 0-4> D-4 4-8> E-4 8-12-");

	// Back to the middle: nothing is left to write
	CHECK(s.SetPan(d4, 1, PAN_CENTER));
	CHECK_EQ(Pans(s), "C C C");
	CHECK(!HasEffect(s, 4, EF_PAN, PAN_LEFT) && PanEffects(s) <= 1);

	// Two notes in a row to the right: one effect, on the first; a pan that changed in the middle
	// of a note goes
	CChannelNotes r = Channel(32);
	r.SetCell(0, Cell(C, 4, 0));
	r.SetCell(4, Cell(D, 4));
	r.SetCell(6, With(Cell(NONE), 0, EF_PAN, PAN_LEFT));
	r.SetCell(8, Cell(E, 4));
	r.SetCell(12, Cell(F, 4));
	r.SetCell(16, Cell(HALT));
	CHECK_EQ(Pans(r), "C C L L");
	const int de[] = { 8, 4 };
	CHECK(r.SetPan(de, 2, PAN_RIGHT));
	CHECK_EQ(Pans(r), "C R R L");
	CHECK(HasEffect(r, 4, EF_PAN, PAN_RIGHT) && !HasEffect(r, 6, EF_PAN, PAN_LEFT) && !HasEffect(r, 8, EF_PAN, PAN_RIGHT));
	CHECK(HasEffect(r, 12, EF_PAN, PAN_LEFT));

	// The last note panned: the first gets the middle written, for when the song loops
	CChannelNotes l = Channel(32);
	l.SetCell(0, Cell(C, 4, 0));
	l.SetCell(8, Cell(D, 4));
	l.SetCell(12, Cell(HALT));
	const int last[] = { 8 };
	CHECK(l.SetPan(last, 1, PAN_RIGHT));
	CHECK(HasEffect(l, 8, EF_PAN, PAN_RIGHT) && HasEffect(l, 0, EF_PAN, PAN_CENTER));
	CHECK_EQ(Pans(l), "C R");

	// No free effect column: false, the rest is done
	CChannelNotes f = Channel(8, 1);
	f.SetCell(0, With(Cell(C, 4, 0), 0, EF_VIBRATO, 0x44));
	f.SetCell(4, Cell(HALT));
	const int c4[] = { 0 };
	CHECK(!f.SetPan(c4, 1, PAN_LEFT));
	CHECK(HasEffect(f, 0, EF_VIBRATO, 0x44));

	// Edits keep the pans: a moved note plays at its pan where it lands, the notes it leaves at theirs
	CChannelNotes m = Channel(48);
	m.SetCell(0, With(Cell(C, 4, 0), 0, EF_PAN, PAN_LEFT));
	m.SetCell(4, Cell(D, 4));
	m.SetCell(8, Cell(E, 4));
	m.SetCell(12, Cell(HALT));
	m.SetCell(24, With(Cell(G, 4), 0, EF_PAN, PAN_RIGHT));
	m.SetCell(28, Cell(HALT));
	CHECK_EQ(Pans(m), "L L L R");
	const int c[] = { 0 };
	const int cp[] = { P(C, 4) };
	m.Move(c, 1, 32, cp, false);	// to row 32, after the G-4 (on the right)
	CHECK_EQ(Notes(m), "D-4 4-8> E-4 8-12- G-4 24-28- C-4 32-36-");
	CHECK_EQ(Pans(m), "L L R L");
	CHECK(HasEffect(m, 4, EF_PAN, PAN_LEFT) && HasEffect(m, 32, EF_PAN, PAN_LEFT));

	// Removed: the notes after it keep their pan
	m.Remove(4);
	CHECK_EQ(Pans(m), "L R L");
	CHECK(HasEffect(m, 8, EF_PAN, PAN_LEFT));

	// A new note plays where the channel is panned
	m.Add(14, 18, P(A, 4), 0);
	CHECK_EQ(Pans(m), "L L R L");
	CHECK(!HasEffect(m, 14, EF_PAN, PAN_LEFT));

	// Copied and pasted somewhere else, and in a clip made self-contained
	CChannelNotes p = Channel(64);
	p.SetCell(0, With(Cell(C, 4, 0), 0, EF_PAN, PAN_RIGHT));
	p.SetCell(4, Cell(D, 4));
	p.SetCell(8, With(Cell(E, 4), 0, EF_PAN, PAN_CENTER));
	p.SetCell(12, Cell(HALT));
	const int dd[] = { 4 };
	const CChannelNotes clip = p.Copy(dd, 1);
	p.Paste(clip, 20, 0);
	CHECK_EQ(Pans(p), "R R C R");
	CHECK(HasEffect(p, 20, EF_PAN, PAN_RIGHT));
	CChannelNotes own = clip;
	own.SelfContained();
	CHECK(HasEffect(own, 0, EF_PAN, PAN_RIGHT));

	// Effects taken off: the pan stays, the next note's too
	CChannelNotes x = Channel(32, 3);
	x.SetCell(0, With(With(Cell(C, 4, 0), 0, EF_VIBRATO, 0x44), 1, EF_PAN, PAN_LEFT));
	x.SetCell(4, Cell(D, 4));
	x.SetCell(8, Cell(HALT));
	CHECK(x.ClearEffects(c, 1));
	CHECK(HasEffect(x, 0, EF_PAN, PAN_LEFT) && !HasEffect(x, 0, EF_VIBRATO, 0x44));
	CHECK_EQ(Pans(x), "L L");

	// A song that doesn't pan gets no pan from any of these edits
	CChannelNotes plain = Channel(32);
	plain.SetCell(0, Cell(C, 4, 0));
	plain.SetCell(4, Cell(D, 4));
	plain.SetCell(8, Cell(HALT));
	plain.Add(12, 16, P(E, 4), 0);
	plain.Move(c, 1, 20, cp, false);
	plain.Paste(plain.Copy(d4, 1), 24, 2);
	CHECK(PanEffects(plain) == 0);
	CHECK_EQ(Pans(plain), "C C C C");
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

	// A note drawn up to a song's last row (32, read with a frame more to write into) ends past it: nothing but that end
	// lies there, so the song keeps its length; a note past it makes it longer
	CChannelNotes song = Channel(48);
	song.SetCell(16, Cell(D, 4, 0));
	song.SetCell(24, Cell(RELEASE));
	song.Add(24, 32, P(C, 4), 0);
	CHECK_EQ(Notes(song), "D-4 16-24> C-4 24-32-");
	CHECK(song.OnlyEnds(32, 47) && song.OnlyEnds(32, 32) && !song.OnlyEnds(24, 47));
	song.Add(36, 40, P(E, 4), 0);
	CHECK(!song.OnlyEnds(32, 47) && song.OnlyEnds(32, 35));
	CChannelNotes loud = Channel(48);
	loud.SetCell(32, Cell(RELEASE, 0, MAX_INSTRUMENTS, 4));	// a volume with it counts later on
	CHECK(!loud.OnlyEnds(32, 47) && loud.OnlyEnds(33, 47));
}

// A take over a selection (the recorder's "re-sing just a selection"): its rows replaced and nothing else
static bool SameRows(const CChannelNotes &a, const CChannelNotes &b, int from, int to)
{
	for (int r = from; r < to; ++r) {
		stChanNote x, y;
		a.GetCell(r, x);
		b.GetCell(r, y);
		if (!SameCell(x, y))
			return false;
	}
	return true;
}

static void TestReplaceRows()
{
	// Before: C-4 from row 0 (instrument 1, volume 12, a vibrato from its start), E-4 at 8 sounding into the
	// selection (rows 12-31), G-4 at 16 and A-4 at 24 inside it, A-4 sounding on to a release at 36, F06 at 20
	// (the song's speed), then D-4 at 40 that takes over instrument 1 and volume 12 and the vibrato
	CChannelNotes song = Channel(64, 3);
	song.SetCanRelease(1, true);
	song.SetCell(0, With(Cell(C, 4, 1, 12), 0, EF_VIBRATO, 0x52));
	song.SetCell(8, Cell(E, 4));
	song.SetCell(16, Cell(G, 4));
	song.SetCell(20, With(CFamiTrackerDoc::Empty(), 2, EF_SPEED, 0x06));
	song.SetCell(24, Cell(A, 4));
	song.SetCell(36, Cell(RELEASE));
	song.SetCell(40, Cell(D, 4));
	const CChannelNotes before = song;
	CHECK_EQ(Notes(song), "C-4 0-8> E-4 8-16> G-4 16-24> A-4 24-36= D-4 40-64|");

	// The take: B-3 two rows in (instrument 2, volume 15) with a release four rows later, then C-5 ten
	// rows in (volume 9) held to its end, and a note past the selection that's left out
	CChannelNotes take = Channel(24, 3);
	take.SetCell(2, Cell(B, 3, 2, 15));
	take.SetCell(6, Cell(RELEASE));
	take.SetCell(10, Cell(C, 5, 2, 9));
	take.SetCell(22, Cell(F, 5, 2, 15));
	song.ReplaceRows(12, 32, take);

	// E-4 ends where the selection starts (a release: instrument 1 fades out on one), the vibrato is
	// switched off for the take, its notes are as sung, and C-5 carries on to the release at 36 where A-4 did
	CHECK_EQ(Notes(song), "C-4 0-8> E-4 8-12= B-3 14-18= C-5 22-36= D-4 40-64|");
	CHECK(HasEffect(song, 12, EF_VIBRATO, 0x00));
	CHECK(HasEffect(song, 20, EF_SPEED, 0x06));	// the song's speed stays
	CHECK(SameRows(song, before, 0, 12));		// nothing before the selection changes
	CHECK(SameRows(song, before, 32, 40));		// nor after it, up to the next note
	// D-4 sounds as before: instrument 1, volume 12, the vibrato again
	CHECK(At(song, 40).Instrument == 1 && At(song, 40).Vol == 12);
	CHECK(HasEffect(song, 40, EF_VIBRATO, 0x52));
	CHECK_EQ(Heard(song), "1 12, 1 12, 2 15, 2 9, 1 12");
	CHECK(SameRows(song, before, 41, 64));
	int first = 0, last = 0;
	CHECK(song.Compare(before, first, last) && first == 12 && last == 40);

	// Where nothing sounded on after the selection, the take's last note ends there; a take that starts
	// on the selection's first row needs no end before it
	CChannelNotes quiet = Channel(32, 1);
	quiet.SetCell(0, Cell(C, 4, 0));
	quiet.SetCell(4, Cell(D, 4));
	quiet.SetCell(6, Cell(HALT));
	quiet.SetCell(12, Cell(E, 4));
	CChannelNotes held = Channel(8, 1);
	held.SetCell(0, Cell(G, 4, 3, 15));
	quiet.ReplaceRows(4, 8, held);
	CHECK_EQ(Notes(quiet), "C-4 0-4> G-4 4-8- E-4 12-32|");
	CHECK(At(quiet, 12).Instrument == 0);	// E-4 keeps the instrument it took over

	// A silent take clears the rows: what sounded into them stops there, and the notes in them go
	CChannelNotes silent = Channel(32, 1);
	silent.SetCell(0, Cell(C, 4, 0));
	silent.SetCell(10, Cell(D, 4));
	silent.SetCell(20, Cell(E, 4));
	const CChannelNotes loud = silent;
	silent.ReplaceRows(8, 16, Channel(8, 1));
	CHECK_EQ(Notes(silent), "C-4 0-8- E-4 20-32|");
	CHECK(SameRows(silent, loud, 16, 32) && SameRows(silent, loud, 0, 8));

	// Out of range or empty: nothing changes
	CChannelNotes same = before;
	same.ReplaceRows(20, 20, take);
	same.ReplaceRows(70, 80, take);
	CHECK(!same.Compare(before, first, last));
}

// The part of a take that goes over a selection: a note sung a little early starts with the selection
static void TestTakeRows()
{
	// A take from the beat before the selection (rows 0-3 before it): F-4 one row before it starts (a
	// vibrato on it), G-4 inside, A-4 past the selection's 8 rows
	CChannelNotes take = Channel(16, 1);
	take.SetCell(1, Cell(D, 4, 2, 15));
	take.SetCell(2, Cell(RELEASE));
	take.SetCell(3, With(Cell(F, 4, 2, 12), 0, EF_VIBRATO, 0x34));
	take.SetCell(7, Cell(G, 4, 2, 15));
	take.SetCell(12, Cell(A, 4, 2, 15));
	const CChannelNotes window = take.TakeRows(4, 8, 2);
	CHECK(window.GetRows() == 8);
	CHECK_EQ(Notes(window), "F-4 0-3> G-4 3-8|");
	CHECK(At(window, 0).Instrument == 2 && At(window, 0).Vol == 12 && HasEffect(window, 0, EF_VIBRATO, 0x34));
	CHECK_EQ(Heard(window), "2 12, 2 15");
	// Too early to count: F-4, two rows before the rows, moves to them only with Early 2
	CHECK_EQ(Notes(take.TakeRows(5, 8, 1)), "G-4 2-7> A-4 7-8|");
	CHECK_EQ(Notes(take.TakeRows(5, 8, 2)), "F-4 0-2> G-4 2-7> A-4 7-8|");
	// A note that starts on the selection's first row stays as it is; rows past the take are empty
	CHECK_EQ(Notes(take.TakeRows(7, 12, 4)), "G-4 0-5> A-4 5-12|");
	CHECK(take.TakeRows(20, 4, 2).GetNoteCount() == 0 && take.TakeRows(20, 4, 2).GetRows() == 4);
	// Replacing rows with it: the selection gets the take as sung there
	CChannelNotes song = Channel(32, 1);
	song.SetCell(0, Cell(C, 4, 1, 10));
	song.SetCell(16, Cell(E, 4));
	song.ReplaceRows(8, 16, window);
	CHECK_EQ(Notes(song), "C-4 0-8> F-4 8-11> G-4 11-16> E-4 16-32|");
	CHECK(At(song, 16).Instrument == 1 && At(song, 16).Vol == 10);	// E-4 sounds as before
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
	TestEffectsShown();
	TestSetLasting();
	TestSlidesAndBends();
	TestClearEffects();
	TestPan();
	TestCompare();
	TestReplaceRows();
	TestTakeRows();
	std::printf("%d checks, %d failed\n", g_checks, g_fail);
	return g_fail == 0 ? 0 : 1;
}

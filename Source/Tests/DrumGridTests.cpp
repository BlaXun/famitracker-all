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

// Tests for the drum grid's arithmetic (DrumGrid.cpp)

#include <cstdio>
#include <string>
#include <vector>
#include "DrumGrid.cpp"

namespace {

int g_checks = 0, g_failures = 0;

#define CHECK(x) do { ++g_checks; if (!(x)) { ++g_failures; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

stChanNote Blank()
{
	return CFamiTrackerDoc::Empty();
}

stChanNote NoiseNote(int Pitch, int Instrument)
{
	stChanNote Cell = Blank();
	Cell.Note = static_cast<unsigned char>(Pitch % 12 + 1);
	Cell.Octave = static_cast<unsigned char>(Pitch / 12);
	Cell.Instrument = static_cast<unsigned char>(Instrument);
	return Cell;
}

// The bar as letters: K kick, S snare, h hi-hat, o open hi-hat, ? other, . nothing
std::string Pattern(const std::vector<stChanNote> &Bar, const drum_kit_t &Kit)
{
	std::string Out;
	for (size_t i = 0; i < Bar.size(); ++i) {
		switch (DrumOf(Bar[i], Kit)) {
		case DRUM_KICK: Out += 'K'; break;
		case DRUM_SNARE: Out += 'S'; break;
		case DRUM_HIHAT: Out += 'h'; break;
		case DRUM_OPEN_HIHAT: Out += 'o'; break;
		case DRUM_OTHER: Out += '?'; break;
		default: Out += '.'; break;
		}
	}
	return Out;
}

drum_kit_t Kit()
{
	drum_kit_t K;
	K.Instruments[DRUM_KICK] = 2;
	K.Instruments[DRUM_SNARE] = 3;
	K.Instruments[DRUM_HIHAT] = 4;
	K.Instruments[DRUM_OPEN_HIHAT] = 5;
	return K;
}

void TestReading()
{
	const drum_kit_t K = Kit();
	// By instrument, whatever the pitch; another instrument is "other"
	CHECK(DrumOf(NoiseNote(0x7, 2), K) == DRUM_KICK);
	CHECK(DrumOf(NoiseNote(0xE, 4), K) == DRUM_HIHAT);
	CHECK(DrumOf(NoiseNote(0xE, 9), K) == DRUM_OTHER);
	// Without an instrument, by pitch
	CHECK(DrumOf(NoiseNote(0xA, MAX_INSTRUMENTS), K) == DRUM_SNARE);
	CHECK(DrumOf(NoiseNote(0xD, MAX_INSTRUMENTS), K) == DRUM_OPEN_HIHAT);
	CHECK(DrumOf(NoiseNote(0x1, MAX_INSTRUMENTS), K) == DRUM_OTHER);
	// Nothing, releases and halts aren't drums
	CHECK(DrumOf(Blank(), K) == DRUM_NONE);
	stChanNote Halt = Blank();
	Halt.Note = HALT;
	CHECK(DrumOf(Halt, K) == DRUM_NONE);
	// A kit without instruments yet reads by pitch
	CHECK(DrumOf(NoiseNote(0x4, MAX_INSTRUMENTS), drum_kit_t()) == DRUM_KICK);
	CHECK(std::string(DrumName(DRUM_OPEN_HIHAT)) == "Open hi-hat" && std::string(DrumPreset(DRUM_HIHAT)) == "hi-hat");
	CHECK(DrumPitch(DRUM_KICK) == 4 && DrumPitch(DRUM_SNARE) == 0xA);
}

void TestToggling()
{
	const drum_kit_t K = Kit();
	stChanNote Cell = Blank();
	Cell.EffNumber[0] = EF_SPEED;		// the song's speed change stays through it all
	Cell.EffParam[0] = 5;
	ToggleDrum(Cell, DRUM_SNARE, K);
	CHECK(DrumOf(Cell, K) == DRUM_SNARE && Cell.Instrument == 3 && Cell.Octave * 12 + Cell.Note - 1 == 0xA);
	ToggleDrum(Cell, DRUM_KICK, K);		// one drum at a time: the kick takes its place
	CHECK(DrumOf(Cell, K) == DRUM_KICK && Cell.Instrument == 2);
	ToggleDrum(Cell, DRUM_KICK, K);		// and again takes it away
	CHECK(DrumOf(Cell, K) == DRUM_NONE && Cell.Note == NONE && Cell.Instrument == MAX_INSTRUMENTS && Cell.Vol == MAX_VOLUME);
	CHECK(Cell.EffNumber[0] == EF_SPEED && Cell.EffParam[0] == 5);
	// Another instrument's note is replaced by a drum
	stChanNote Other = NoiseNote(0x2, 9);
	Other.Vol = 7;
	ToggleDrum(Other, DRUM_HIHAT, K);
	CHECK(DrumOf(Other, K) == DRUM_HIHAT && Other.Vol == MAX_VOLUME);
	// A drum the kit has no instrument for yet can't be put on
	drum_kit_t Partial;
	Partial.Instruments[DRUM_KICK] = 0;
	stChanNote Empty = Blank();
	ToggleDrum(Empty, DRUM_SNARE, Partial);
	CHECK(DrumOf(Empty, Partial) == DRUM_NONE);
}

void TestBeats()
{
	const drum_kit_t K = Kit();
	std::vector<stChanNote> Bar(16, Blank());
	Bar[3].EffNumber[1] = EF_JUMP;
	ApplyBeat(Bar, BEAT_BASIC, 4, K);
	CHECK(Pattern(Bar, K) == "K.h.S.h.K.h.S.h.");
	CHECK(Bar[3].EffNumber[1] == EF_JUMP);		// effects stay
	ApplyBeat(Bar, BEAT_FOUR_ON_FLOOR, 4, K);
	CHECK(Pattern(Bar, K) == "K.h.K.h.K.h.K.h.");
	ApplyBeat(Bar, BEAT_HALF_TIME, 4, K);
	CHECK(Pattern(Bar, K) == "K.h.h.h.S.h.h.h.");
	ApplyBeat(Bar, BEAT_BUSY, 4, K);
	CHECK(Pattern(Bar, K) == "KhhhShhKKhhhShhh");
	ApplyBeat(Bar, BEAT_CLEAR, 4, K);
	CHECK(Pattern(Bar, K) == "................");
	CHECK(Bar[3].EffNumber[1] == EF_JUMP);

	// A waltz: three beats a bar
	std::vector<stChanNote> Waltz(12, Blank());
	ApplyBeat(Waltz, BEAT_BASIC, 4, K);
	CHECK(Pattern(Waltz, K) == "K.h.S.h.S.h.");
	// Two rows a beat
	std::vector<stChanNote> Short(8, Blank());
	ApplyBeat(Short, BEAT_BASIC, 2, K);
	CHECK(Pattern(Short, K) == "KhShKhSh");
	// A row a beat: no room between beats
	std::vector<stChanNote> Tight(4, Blank());
	ApplyBeat(Tight, BEAT_FOUR_ON_FLOOR, 1, K);
	CHECK(Pattern(Tight, K) == "KKKK");
	// What each beat needs
	CHECK(BeatUses(BEAT_FOUR_ON_FLOOR, DRUM_KICK) && !BeatUses(BEAT_FOUR_ON_FLOOR, DRUM_SNARE) && !BeatUses(BEAT_CLEAR, DRUM_KICK));
	CHECK(std::string(BeatName(BEAT_HALF_TIME)) == "Half-time");
	// Without a snare instrument, its steps stay empty
	drum_kit_t NoSnare = K;
	NoSnare.Instruments[DRUM_SNARE] = -1;
	std::vector<stChanNote> Bar2(16, Blank());
	ApplyBeat(Bar2, BEAT_BASIC, 4, NoSnare);
	CHECK(Pattern(Bar2, NoSnare) == "K.h...h.K.h...h.");
}

// The bar the grid shows: the cursor's, and the one picked between the cursor's moves
void TestFollow()
{
	drum_follow_t F;
	CHECK(F.Bar == 0 && F.CursorBar == -1);
	CHECK(FollowCursor(F, 3, 8) && F.Bar == 3 && F.CursorBar == 3);	// the first look goes to the cursor
	CHECK(!FollowCursor(F, 3, 8) && F.Bar == 3);					// it stays there
	F.Bar = 2;														// the < button
	CHECK(!FollowCursor(F, 3, 8) && F.Bar == 2);					// stays while the cursor does
	CHECK(FollowCursor(F, 5, 8) && F.Bar == 5 && F.CursorBar == 5);	// and follows it when it moves
	F.Bar = 4;
	CHECK(!FollowCursor(F, 4, 8) && F.Bar == 4 && F.CursorBar == 4);	// into the bar shown: noted, nothing changes
	// Fewer bars (frames removed, longer bars): the last one
	F.Bar = 7;
	CHECK(FollowCursor(F, 4, 3) && F.Bar == 2);
	CHECK(FollowCursor(F, 9, 8) && F.Bar == 7 && F.CursorBar == 9);	// the cursor past the end
	// Without a cursor, the bar stays
	drum_follow_t None;
	None.Bar = 1;
	CHECK(!FollowCursor(None, -1, 4) && None.Bar == 1);
	CHECK(FollowCursor(None, -1, 0) && None.Bar == 0);				// no bars: the first
}

// The buttons along the top, in lines
void TestFlow()
{
	const int W[] = { 10, 10, 10, 20, 20, 30 };
	const int G[] = { 0, 0, 0, 1, 1, 2 };
	const std::vector<int> Widths(W, W + 6), Groups(G, G + 6);
	std::vector<drum_place_t> P;
	// All on one line: 2 apart in a group, 8 between groups
	CHECK(FlowDrumControls(Widths, Groups, 200, 2, 8, P) == 1);
	CHECK(P.size() == 6 && P[0].X == 0 && P[1].X == 12 && P[2].X == 24 && P[3].X == 42 && P[4].X == 64 && P[5].X == 92);
	CHECK(P[5].Line == 0);
	// Narrower: each group on a line of its own, kept together
	CHECK(FlowDrumControls(Widths, Groups, 60, 2, 8, P) == 3);
	CHECK(P[2].Line == 0 && P[2].X == 24 && P[3].Line == 1 && P[3].X == 0 && P[4].X == 22 && P[5].Line == 2 && P[5].X == 0);
	// Two groups fit on a line, the third doesn't
	CHECK(FlowDrumControls(Widths, Groups, 90, 2, 8, P) == 2);
	CHECK(P[3].Line == 0 && P[4].Line == 0 && P[4].X == 64 && P[5].Line == 1);
	// A group longer than a line breaks where it must
	CHECK(FlowDrumControls(Widths, Groups, 25, 2, 8, P) == 5);
	CHECK(P[0].Line == 0 && P[1].Line == 0 && P[1].X == 12 && P[2].Line == 1 && P[2].X == 0);
	CHECK(P[3].Line == 2 && P[4].Line == 3 && P[5].Line == 4);
	// A control wider than the line still gets a line of its own
	const std::vector<int> Wide(1, 50);
	CHECK(FlowDrumControls(Wide, std::vector<int>(1, 0), 20, 2, 8, P) == 1 && P[0].X == 0);
	// Without groups, each control is a group of its own
	CHECK(FlowDrumControls(Widths, std::vector<int>(), 200, 2, 8, P) == 1 && P[1].X == 18);
	// Nothing to place
	CHECK(FlowDrumControls(std::vector<int>(), std::vector<int>(), 100, 2, 8, P) == 1 && P.empty());
}

}	// namespace

int main()
{
	TestReading();
	TestToggling();
	TestBeats();
	TestFollow();
	TestFlow();
	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}

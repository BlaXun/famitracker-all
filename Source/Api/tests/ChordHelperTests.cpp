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

// Tests for the piano roll's chord helper (ChordHelper.h): the free channels
// found, the voices placed under the melody and over the bass, too few free
// channels, a progression over a selection, again on the next bar, the soft
// layer's voices each with an N163 sound of its own, and the cells written.

#include "../ChordHelper.h"
#include "../Chords.h"
#include "../Music.h"
#include "../Wavetables.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <set>
#include <string>
#include <vector>

using namespace ftapi;

namespace {

int g_Checks = 0, g_Failures = 0;

void Check(bool Condition, const char *What, int Line)
{
	++g_Checks;
	if (!Condition) {
		++g_Failures;
		std::printf("FAIL line %d: %s\n", Line, What);
	}
}

#define CHECK(x) Check((x), #x, __LINE__)
#define CHECK_EQ(a, b) Check((a) == (b), #a " == " #b, __LINE__)

bool Contains(const std::string &Text, const char *Part)
{
	return Text.find(Part) != std::string::npos;
}

#define CHECK_HAS(text, part) Check(Contains((text), (part)), "\"" part "\" in " #text, __LINE__)

int Mod12(int Value)
{
	return ((Value % 12) + 12) % 12;
}

// The channels of a song
enum { P1, P2, TRI, NOISE, DPCM, V1, V2, SAW, SOFT1, SOFT2, SOFT3, SOFT4 };

HelperChannel MakeChannel(const char *pName, const char *pChip, int Kind, int Rows, bool Soft = false)
{
	HelperChannel C;
	C.Name = pName;
	C.Chip = pChip;
	C.Kind = Kind;
	C.Soft = Soft;
	C.EffectColumns = 2;
	C.Cells.assign(static_cast<size_t>(Rows), Cell());
	return C;
}

HelperInstrument MakeInstrument(int Index, const char *pName, const char *pChip, bool Releases = false, int WavePos = -1)
{
	HelperInstrument I;
	I.Index = Index;
	I.Name = pName;
	I.Chip = pChip;
	I.Releases = Releases;
	I.WavePos = WavePos;
	I.WaveSize = WavePos >= 0 ? N163_WAVE_SIZE : -1;
	return I;
}

// A VRC6 song (or with Plus, a VRC6 Plus song) of Frames 64-row frames, 4 rows a beat, 16 a bar, in A minor
HelperSong MakeSong(int Frames, bool Plus = false)
{
	HelperSong S;
	S.PatternLength = 64;
	S.Frames = Frames;
	S.FirstHighlight = 4;
	S.SecondHighlight = 16;
	S.K = Key(9, MODE_MINOR);
	S.HasKey = true;
	const int Rows = S.Rows();
	S.Channels.push_back(MakeChannel("Pulse 1", "2A03", KIND_PULSE, Rows));
	S.Channels.push_back(MakeChannel("Pulse 2", "2A03", KIND_PULSE, Rows));
	S.Channels.push_back(MakeChannel("Triangle", "2A03", KIND_TRIANGLE, Rows));
	S.Channels.push_back(MakeChannel("Noise", "2A03", KIND_NOISE, Rows));
	S.Channels.push_back(MakeChannel("DPCM", "2A03", KIND_DPCM, Rows));
	S.Channels.push_back(MakeChannel("VRC6 Pulse 1", "VRC6", KIND_PULSE, Rows));
	S.Channels.push_back(MakeChannel("VRC6 Pulse 2", "VRC6", KIND_PULSE, Rows));
	S.Channels.push_back(MakeChannel("Sawtooth", "VRC6", KIND_SAWTOOTH, Rows));
	S.Instruments.push_back(MakeInstrument(0, "Soft lead", "VRC6"));
	S.Instruments.push_back(MakeInstrument(1, "Bass", "2A03"));
	S.Instruments.push_back(MakeInstrument(2, "Kick", "2A03"));
	if (Plus) {
		for (int i = 0; i < 4; ++i) {
			char Name[32];
			std::snprintf(Name, sizeof(Name), "Soft %d", i + 1);
			S.Channels.push_back(MakeChannel(Name, "N163", KIND_OTHER, Rows, true));
		}
		S.N163Channels = 4;
		S.Instruments.push_back(MakeInstrument(3, "Choir pad", "N163", true, 0));
		S.Instruments.push_back(MakeInstrument(4, "String pad", "N163", true, 16));
		S.Instruments.push_back(MakeInstrument(5, "Soft flute", "N163", false, 32));
		S.Instruments.push_back(MakeInstrument(6, "Sub bass", "N163", false, 48));
	}
	return S;
}

// The cell a test writes: a row outside the song fails (rather than writing past the cells)
Cell *CellAt(HelperSong &S, int Channel, int Row, int Line)
{
	const bool Inside = Channel >= 0 && Channel < static_cast<int>(S.Channels.size()) && Row >= 0 &&
		Row < static_cast<int>(S.Channels[static_cast<size_t>(Channel)].Cells.size());
	Check(Inside, "the cell written is in the song", Line);
	return Inside ? &S.Channels[static_cast<size_t>(Channel)].Cells[static_cast<size_t>(Row)] : 0;
}

void PutAt(HelperSong &S, int Channel, int Row, int Pitch, int Instrument, int Volume, int Line)
{
	Cell *pCell = CellAt(S, Channel, Row, Line);
	if (pCell == 0)
		return;
	pCell->SetPitch(Pitch);
	pCell->Instrument = static_cast<unsigned char>(Instrument);
	pCell->Volume = static_cast<unsigned char>(Volume);
}

void PutAt(HelperSong &S, int Channel, int Row, int Pitch, int Instrument, int Line)
{
	PutAt(S, Channel, Row, Pitch, Instrument, NO_VOLUME, Line);
}

void StopAt(HelperSong &S, int Channel, int Row, int Line)
{
	Cell *pCell = CellAt(S, Channel, Row, Line);
	if (pCell != 0)
		pCell->Note = NOTE_HALT;
}

#define Put(...) PutAt(__VA_ARGS__, __LINE__)
#define Stop(S, Channel, Row) StopAt(S, Channel, Row, __LINE__)

// A melody on VRC6 Pulse 1 over bars 1-4 (A minor: E A C B | A E | F A C A | G B D B) and a bass on the triangle (A A F G,
// written an octave up: it sounds an octave under what's written), halted after them where the song goes on
void AddMelodyAndBass(HelperSong &S, int Bars = 4)
{
	static const int MELODY[4][4] = { { 52, 57, 60, 59 }, { 57, 57, 52, 52 }, { 53, 57, 60, 57 }, { 55, 59, 62, 59 } };
	static const int BASS[4] = { 33, 33, 29, 31 };
	for (int b = 0; b < Bars; ++b) {
		for (int n = 0; n < 4; ++n)
			if (n == 0 || MELODY[b % 4][n] != MELODY[b % 4][n - 1])
				Put(S, V1, b * 16 + n * 4, MELODY[b % 4][n], 0, 15);
		Put(S, TRI, b * 16, BASS[b % 4], 1);
	}
	if (Bars * 16 < S.Rows()) {
		Stop(S, V1, Bars * 16);
		Stop(S, TRI, Bars * 16);
	}
}

ChordRequest Asking(const char *pChords, int First, int End)
{
	ChordRequest R;
	R.Chords = pChords;
	R.First = First;
	R.End = End;
	return R;
}

// The pitch classes the voices start with in rows [First, End), as they sound
std::set<int> ClassesIn(const HelperSong &S, const ChordPlan &P, int First, int End)
{
	std::set<int> Out;
	for (size_t v = 0; v < P.Voices.size(); ++v) {
		const HelperChannel &C = S.Channels[static_cast<size_t>(P.Voices[v].Channel)];
		const int Lift = C.Kind == KIND_FM || C.Kind == KIND_SQUARE ? 12 : C.Chip == "FDS" ? 24 : 0;
		for (size_t i = 0; i < P.Voices[v].Notes.size(); ++i)
			if (P.Voices[v].Notes[i].Row >= First && P.Voices[v].Notes[i].Row < End)
				Out.insert(Mod12(P.Voices[v].Notes[i].Pitch - Lift));
	}
	return Out;
}

std::set<int> Classes(std::initializer_list<int> List)
{
	return std::set<int>(List.begin(), List.end());
}

void TestFreeChannels()
{
	HelperSong S = MakeSong(2);
	AddMelodyAndBass(S);
	// The melody on VRC6 Pulse 1 and the bass on the triangle: the 2A03's pulses, VRC6 Pulse 2 and (last) the sawtooth are
	// free; never the triangle, the noise or the DPCM
	const std::vector<int> Free = FreeChannels(S, 0, 16);
	CHECK_EQ(Free.size(), 4u);
	CHECK(Free.size() == 4 && Free[0] == P1 && Free[1] == P2 && Free[2] == V2 && Free[3] == SAW);
	CHECK(!TakesVoice(S.Channels[TRI]) && !TakesVoice(S.Channels[NOISE]) && !TakesVoice(S.Channels[DPCM]));
	CHECK(TakesVoice(S.Channels[SAW]) && TakesVoice(S.Channels[V2]));

	// A note held from bar 1 into bar 2 isn't free in bar 2; one that ends with a release at the bar line leaves it free
	Put(S, P1, 8, 45, 1);
	CHECK(PlaysIn(S.Channels[P1], 16, 32));
	const std::vector<int> Later = FreeChannels(S, 16, 32);
	CHECK(std::find(Later.begin(), Later.end(), static_cast<int>(P1)) == Later.end());
	S.Channels[P1].Cells[16].Note = NOTE_RELEASE;
	CHECK(!PlaysIn(S.Channels[P1], 16, 32));
	CHECK(PlaysIn(S.Channels[P1], 0, 16));
	CHECK_EQ(FreeChannels(S, 16, 32).front(), static_cast<int>(P1));

	// Whole bars: from the bar a row is in to the end of the one the last row is in
	int First = 0, End = 0;
	WholeBars(S, 20, 21, First, End);
	CHECK(First == 16 && End == 32);
	WholeBars(S, 5, 40, First, End);
	CHECK(First == 0 && End == 48);
	// A frame of 40 rows: its third bar is 8 rows long, and the next frame's first bar starts after it
	HelperSong Short = MakeSong(2);
	Short.PatternLength = 40;
	WholeBars(Short, 33, 34, First, End);
	CHECK(First == 32 && End == 40);
	WholeBars(Short, 39, 45, First, End);
	CHECK(First == 32 && End == 56);
}

void TestVoicesPlaced()
{
	HelperSong S = MakeSong(2);
	AddMelodyAndBass(S);
	ChordPlan P;
	CHECK(PlanChords(S, Asking("Am", 20, 21), P));	// the bar the cursor is in: bar 2
	CHECK(P.Error.empty());
	CHECK(P.First == 16 && P.End == 32);
	CHECK_EQ(P.Wanted, 3);
	CHECK(!P.TooFew && !P.Arpeggio);
	CHECK_EQ(P.Melody, static_cast<int>(V1));
	CHECK_EQ(P.Bass, static_cast<int>(TRI));
	CHECK_EQ(P.Voices.size(), 3u);
	// One kind of channel: the pulses, lowest voice on the leftmost
	CHECK(P.Voices.size() == 3 && P.Voices[0].Channel == P1 && P.Voices[1].Channel == P2 && P.Voices[2].Channel == V2);
	CHECK(ClassesIn(S, P, 16, 32) == Classes({ 9, 0, 4 }));
	for (size_t v = 0; v < P.Voices.size(); ++v) {
		const std::vector<PartNote> &Notes = P.Voices[v].Notes;
		CHECK_EQ(Notes.size(), 1u);			// held: one note for the bar
		if (Notes.empty())
			continue;
		CHECK(Notes[0].Row == 16 && Notes[0].Rows == 16);
		CHECK(Notes[0].Pitch < 52);			// under the melody's lowest note there (E-4)
		CHECK(Notes[0].Pitch > 21 + 2);		// over the bass (A-2 written on the triangle sounds A-1)
		CHECK_EQ(Notes[0].Volume, 10);
		if (v > 0 && !P.Voices[v - 1].Notes.empty())
			CHECK(Notes[0].Pitch > P.Voices[v - 1].Notes[0].Pitch);	// lowest first, apart
		CHECK(P.Voices[v].Sound.Sound == "soft chords" && P.Voices[v].Sound.Instrument < 0);	// the song has none yet
	}
	CHECK_HAS(PlanChordsText(S, P), "Am on bar 2");
	CHECK_HAS(PlanVoicesText(S, P), " on Pulse 1, Pulse 2 and VRC6 Pulse 2");

	// The song's own "Soft chords" for each chip is the one they play
	S.Instruments.push_back(MakeInstrument(7, "Soft chords", "2A03", true));
	S.Instruments.push_back(MakeInstrument(8, "soft chords", "VRC6", true));
	CHECK(PlanChords(S, Asking("Am", 16, 32), P));
	CHECK(P.Voices.size() == 3 && P.Voices[0].Sound.Instrument == 7 && P.Voices[1].Sound.Instrument == 7 &&
		P.Voices[2].Sound.Instrument == 8);

	// The cells: each voice's note with its instrument and volume, and a release where the bar ends (the instruments
	// fade out on one); what follows plays on
	std::vector<int> Instruments;
	for (size_t v = 0; v < P.Voices.size(); ++v)
		Instruments.push_back(P.Voices[v].Sound.Instrument);
	std::vector<std::string> Warnings;
	const std::vector<VoiceCells> Cells = PlanCells(S, P, Instruments, Warnings);
	CHECK_EQ(Cells.size(), 3u);
	for (size_t i = 0; i < Cells.size(); ++i) {
		CHECK_EQ(Cells[i].Row, 16);
		CHECK(Cells[i].Cells.size() >= 17);
		if (Cells[i].Cells.size() < 17)
			continue;
		CHECK(Cells[i].Cells[0].HasNote());
		CHECK_EQ(static_cast<int>(Cells[i].Cells[0].Instrument), Instruments[i]);
		CHECK_EQ(static_cast<int>(Cells[i].Cells[0].Volume), 10);
		CHECK_EQ(static_cast<int>(Cells[i].Cells[16].Note), NOTE_RELEASE);
		for (int r = 1; r < 16; ++r)
			CHECK(!Cells[i].Cells[static_cast<size_t>(r)].HasNote() && !Cells[i].Cells[static_cast<size_t>(r)].IsEnd());
	}
	CHECK(Warnings.empty());

	// Degrees of the key, and a key's chord by name with a seventh
	S.K = Key(0, MODE_MAJOR);
	CHECK(PlanChords(S, Asking("vi", 16, 32), P));
	CHECK(ClassesIn(S, P, 16, 32) == Classes({ 9, 0, 4 }));
	S.HasKey = false;
	CHECK(!PlanChords(S, Asking("vi", 16, 32), P));
	CHECK_HAS(P.Error, "need a key");
	CHECK(!PlanChords(S, Asking("Hm9x", 16, 32), P));
	CHECK_HAS(P.Error, "isn't a chord");
	CHECK(!PlanChords(S, Asking("  ", 16, 32), P));
}

void TestTooFewChannels()
{
	HelperSong S = MakeSong(2);
	AddMelodyAndBass(S);
	// Am7 has four notes, and four channels are free: the sawtooth takes the fourth
	ChordPlan P;
	CHECK(PlanChords(S, Asking("Am7", 0, 16), P));
	CHECK_EQ(P.Wanted, 4);
	CHECK(!P.TooFew);
	CHECK(P.Voices.size() == 4 && P.Voices[3].Channel == SAW);
	CHECK(ClassesIn(S, P, 0, 16) == Classes({ 9, 0, 4, 7 }));

	// A counter-line on the sawtooth: three free for four notes. TooFew, and three voices
	for (int b = 0; b < 4; ++b)
		Put(S, SAW, b * 16, 45 + b, 0, 8);
	CHECK(PlanChords(S, Asking("Am7", 0, 16), P));
	CHECK(P.TooFew);
	CHECK_EQ(P.Free.size(), 3u);
	CHECK_EQ(P.Voices.size(), 3u);
	CHECK(ClassesIn(S, P, 0, 16).size() == 3);
	// Asked for fewer voices, it's no shortage
	ChordRequest Two = Asking("Am7", 0, 16);
	Two.Voices = 2;
	CHECK(PlanChords(S, Two, P));
	CHECK(!P.TooFew && P.Voices.size() == 2);
	// Asked for more than there are: too few all the same (the user is asked), with as many as there are
	ChordRequest Four = Asking("Am7", 0, 16);
	Four.Voices = 4;
	CHECK(PlanChords(S, Four, P));
	CHECK(P.TooFew);
	CHECK_EQ(P.Voices.size(), 3u);
	// A triad asked for on four voices where three are free: too few too
	ChordRequest TriadOnFour = Asking("Am", 0, 16);
	TriadOnFour.Voices = 4;
	CHECK(PlanChords(S, TriadOnFour, P));
	CHECK(P.TooFew && P.Wanted == 4 && P.Voices.size() == 3);
	CHECK(ClassesIn(S, P, 0, 16) == Classes({ 9, 0, 4 }));
	// Three asked for and three free: none too few
	TriadOnFour.Voices = 3;
	CHECK(PlanChords(S, TriadOnFour, P));
	CHECK(!P.TooFew && P.Voices.size() == 3);

	// One free channel: no voices, only the arpeggio
	for (int b = 0; b < 4; ++b) {
		Put(S, P1, b * 16, 48, 0, 8);
		Put(S, P2, b * 16, 50, 0, 8);
	}
	CHECK(PlanChords(S, Asking("Am", 0, 16), P));
	CHECK(P.TooFew && P.Voices.empty());
	CHECK(P.Free.size() == 1 && P.Free[0] == V2);
	ChordRequest Arp = Asking("Am", 0, 16);
	Arp.Arpeggio = true;
	CHECK(PlanChords(S, Arp, P));
	CHECK(P.Arpeggio && P.Voices.size() == 1 && P.Voices[0].Channel == V2);
	if (P.Voices.size() == 1 && !P.Voices[0].Notes.empty()) {
		const PartNote &N = P.Voices[0].Notes[0];
		CHECK(N.Row == 0 && N.Rows == 16);
		bool Arpeggio = false;
		for (size_t e = 0; e < N.Effects.size(); ++e)
			if (N.Effects[e].first == EFF_ARPEGGIO) {
				Arpeggio = true;
				// its three notes are A minor's
				const int Base = Mod12(N.Pitch), X = N.Effects[e].second >> 4, Y = N.Effects[e].second & 0x0F;
				CHECK((std::set<int>{ Base, Mod12(Base + X), Mod12(Base + Y) }) == Classes({ 9, 0, 4 }));
			}
		CHECK(Arpeggio);
		CHECK(!P.Voices[0].Notes.back().EndEffects.empty());	// the arpeggio stops after it (000)
		CHECK_HAS(PlanVoicesText(S, P), "an arpeggio (0");
	}

	// None free: an error that says so
	for (int b = 0; b < 4; ++b)
		Put(S, V2, b * 16, 47, 0, 8);
	CHECK(!PlanChords(S, Asking("Am", 0, 16), P));
	CHECK_HAS(P.Error, "No channel is free in bar 1");
}

void TestProgressionOverSelection()
{
	HelperSong S = MakeSong(2);
	AddMelodyAndBass(S);
	// A chord a bar over bars 1-4 (a selection from the middle of bar 1 to the middle of bar 4)
	ChordPlan P;
	CHECK(PlanChords(S, Asking("Am Am F G", 6, 54), P));
	CHECK(P.First == 0 && P.End == 64);
	CHECK_EQ(P.Bars.size(), 4u);
	CHECK_EQ(P.Chords.size(), 4u);
	CHECK_EQ(P.Voices.size(), 3u);
	CHECK(ClassesIn(S, P, 32, 48) == Classes({ 5, 9, 0 }));
	CHECK(ClassesIn(S, P, 48, 64) == Classes({ 7, 11, 2 }));
	for (size_t v = 0; v < P.Voices.size(); ++v) {
		const std::vector<PartNote> &Notes = P.Voices[v].Notes;
		CHECK_EQ(Notes.size(), 4u);			// struck once a bar
		for (size_t i = 1; i < Notes.size(); ++i)
			CHECK(std::abs(Notes[i].Pitch - Notes[i - 1].Pitch) <= 5);	// led by small steps
	}
	CHECK_HAS(PlanChordsText(S, P), "Am F G on bars 1-4");
	CHECK_HAS(PlanVoicesText(S, P), "on Pulse 1, Pulse 2 and VRC6 Pulse 2");

	// Bar lines: two chords in a bar
	CHECK(PlanChords(S, Asking("| Am | F G |", 0, 32), P));
	CHECK_EQ(P.Chords.size(), 3u);
	CHECK(P.Chords.size() == 3 && P.Chords[2].Row == 24 && P.Chords[2].Rows == 8);

	// More bars than chords: they repeat, and the plan says so
	CHECK(PlanChords(S, Asking("Am F", 0, 64), P));
	CHECK_EQ(P.Chords.size(), 4u);
	CHECK(!P.Remarks.empty() && Contains(P.Remarks[0], "repeat"));
	// More chords than bars: the last left out, said so; with Grow (the bar at the cursor) they go on
	CHECK(PlanChords(S, Asking("Am Am F G", 0, 32), P));
	CHECK_EQ(P.Chords.size(), 2u);
	CHECK(!P.Remarks.empty() && Contains(P.Remarks[0], "left out"));
	ChordRequest Grow = Asking("Am Am F G", 3, 4);
	Grow.Grow = true;
	CHECK(PlanChords(S, Grow, P));
	CHECK(P.First == 0 && P.End == 64 && P.Chords.size() == 4);
	CHECK(P.Remarks.empty());
	// ...as far as Limit: the song's two frames and one more
	Grow = Asking("Am F G Am", 112, 113);
	Grow.Grow = true;
	Grow.Limit = 3 * 64;
	CHECK(PlanChords(S, Grow, P));
	CHECK(P.First == 112 && P.End == 176 && P.Chords.size() == 4);
	Grow.Limit = 0;
	CHECK(PlanChords(S, Grow, P));
	CHECK(P.First == 112 && P.End == 128 && P.Chords.size() == 1);

	// Again on the next bar: the same channels and the same notes as the bar before, led from it
	ChordPlan First;
	CHECK(PlanChords(S, Asking("Am", 0, 16), First));
	ChordRequest Again = Asking("Am", 16, 32);
	for (size_t v = 0; v < First.Voices.size(); ++v)
		Again.Prefer.push_back(First.Voices[v].Channel);
	Again.LeadFrom = First.Chords;
	CHECK(PlanChords(S, Again, P));
	CHECK_EQ(P.Voices.size(), First.Voices.size());
	for (size_t v = 0; v < P.Voices.size() && v < First.Voices.size(); ++v) {
		CHECK_EQ(P.Voices[v].Channel, First.Voices[v].Channel);
		CHECK(P.Voices[v].Notes.size() == 1 && P.Voices[v].Notes[0].Row == 16);
		if (!P.Voices[v].Notes.empty() && !First.Voices[v].Notes.empty())
			CHECK_EQ(P.Voices[v].Notes[0].Pitch, First.Voices[v].Notes[0].Pitch);
	}
	// The last chord's channels stay its voices' while they're free, even where another kind has enough
	ChordRequest Prefer = Asking("C", 32, 48);
	Prefer.Prefer.push_back(SAW);
	Prefer.Prefer.push_back(P2);
	Prefer.Voices = 2;
	CHECK(PlanChords(S, Prefer, P));
	CHECK(P.Voices.size() == 2 && P.Voices[0].Channel == SAW && P.Voices[1].Channel == P2);
}

void TestSoftLayerVoices()
{
	HelperSong S = MakeSong(2, true);
	AddMelodyAndBass(S);
	// The bass on Soft 4 (the sub bass) instead of the triangle
	for (int r = 0; r < S.Rows(); ++r)
		S.Channels[TRI].Cells[static_cast<size_t>(r)] = Cell();
	for (int b = 0; b < 4; ++b)
		Put(S, SOFT4, b * 16, 21, 6);
	ChordPlan P;
	CHECK(PlanChords(S, Asking("C", 0, 16), P));
	// The soft layer takes the voices, each with an N163 instrument of its own: the song's string pad, then copies at free
	// places of the wave memory
	CHECK_EQ(P.Voices.size(), 3u);
	CHECK(P.Voices.size() == 3 && P.Voices[0].Channel == SOFT1 && P.Voices[1].Channel == SOFT2 && P.Voices[2].Channel == SOFT3);
	CHECK_EQ(P.Bass, static_cast<int>(SOFT4));	// a sub bass by name
	if (P.Voices.size() == 3) {
		CHECK_EQ(P.Voices[0].Sound.Instrument, 4);
		CHECK(P.Voices[1].Sound.Instrument < 0 && P.Voices[1].Sound.Name == "String pad 2" && P.Voices[1].Sound.WavePos == 64);
		CHECK(P.Voices[2].Sound.Instrument < 0 && P.Voices[2].Sound.Name == "String pad 3" && P.Voices[2].Sound.WavePos == 80);
		CHECK(P.Voices[1].Sound.Sound == "string pad");
	}
	CHECK(ClassesIn(S, P, 0, 16) == Classes({ 0, 4, 7 }));

	// Once they're in the song, they're the voices' again
	S.Instruments.push_back(MakeInstrument(7, "String pad 2", "N163", true, 64));
	S.Instruments.push_back(MakeInstrument(8, "String pad 3", "N163", true, 80));
	CHECK(PlanChords(S, Asking("C", 0, 16), P));
	CHECK(P.Voices.size() == 3 && P.Voices[0].Sound.Instrument == 4 && P.Voices[1].Sound.Instrument == 7 &&
		P.Voices[2].Sound.Instrument == 8);

	// Soft 1 plays the string pad there: two voices on Soft 2 and 3 can't share it
	Put(S, SOFT1, 0, 57, 4);
	Stop(S, SOFT1, 16);
	ChordRequest Two = Asking("C", 0, 16);
	Two.Voices = 2;
	CHECK(PlanChords(S, Two, P));
	CHECK(P.Voices.size() == 2 && P.Voices[0].Channel == SOFT2 && P.Voices[1].Channel == SOFT3);
	if (P.Voices.size() == 2)
		CHECK(P.Voices[0].Sound.Instrument == 7 && P.Voices[1].Sound.Instrument == 8);
	// Three voices: the soft layer has two free, the pulses three, so the pulses take them (one kind)
	CHECK(PlanChords(S, Asking("C", 0, 16), P));
	CHECK(P.Voices.size() == 3 && P.Voices[0].Channel == P1 && P.Voices[1].Channel == P2 && P.Voices[2].Channel == V2);

	// The wave memory full (eight N163 sounds), and the song's other N163 instruments played there by Soft 4: after Soft 1
	// takes the string pad, Soft 2 and Soft 3 find no sound of their own and give way, which the plan says, and the pulses
	// take the two voices
	HelperSong Full = MakeSong(2, true);
	AddMelodyAndBass(Full);
	Full.Instruments.push_back(MakeInstrument(7, "Glass bell", "N163", false, 64));
	Full.Instruments.push_back(MakeInstrument(8, "Organ", "N163", false, 80));
	Full.Instruments.push_back(MakeInstrument(9, "Music box", "N163", false, 96));
	Full.Instruments.push_back(MakeInstrument(10, "Breath", "N163", false, 112));
	// Soft 4 plays the other N163 sounds in turn in bar 1
	static const int OTHERS[] = { 3, 5, 6, 7, 8, 9, 10 };
	for (int i = 0; i < 7; ++i)
		Put(Full, SOFT4, i * 2, 40, OTHERS[i]);
	ChordRequest Soft = Asking("C", 0, 16);
	Soft.Voices = 2;
	CHECK(PlanChords(Full, Soft, P));
	CHECK_EQ(P.Voices.size(), 2u);
	CHECK(P.Voices.size() == 2 && P.Voices[0].Channel == P1 && P.Voices[1].Channel == P2);
	bool Said = false;
	for (size_t i = 0; i < P.Remarks.size(); ++i)
		Said = Said || Contains(P.Remarks[i], "wave memory");
	CHECK(Said);
}

void TestEndOfSong()
{
	HelperSong S = MakeSong(1);
	AddMelodyAndBass(S);
	// The last bar of a song that loops to its start, where the voices' channels have nothing: each voice stops on the last
	// row, rather than ringing on into the loop
	ChordPlan P;
	CHECK(PlanChords(S, Asking("G", 48, 64), P));
	std::vector<int> Instruments(P.Voices.size(), 7);
	S.Instruments.push_back(MakeInstrument(7, "Soft chords", "2A03", true));
	std::vector<std::string> Warnings;
	std::vector<VoiceCells> Cells = PlanCells(S, P, Instruments, Warnings);
	CHECK_EQ(Cells.size(), 3u);
	for (size_t i = 0; i < Cells.size(); ++i) {
		CHECK(Cells[i].Row == 48 && Cells[i].Cells.size() == 16);
		if (Cells[i].Cells.size() == 16) {
			CHECK(Cells[i].Cells[0].HasNote());
			CHECK_EQ(static_cast<int>(Cells[i].Cells[15].Note), NOTE_RELEASE);
		}
	}
	// One that has a note where the loop starts is cut by it
	Put(S, P1, 0, 45, 1);
	Stop(S, P1, 8);
	CHECK(PlanChords(S, Asking("G", 48, 64), P));
	Cells = PlanCells(S, P, Instruments, Warnings);
	for (size_t i = 0; i < Cells.size(); ++i)
		if (Cells[i].Channel == P1 && Cells[i].Cells.size() == 16)
			CHECK(!Cells[i].Cells[15].IsEnd());
}

void TestFmVoices()
{
	// The VRC7: its notes are written an octave over how they sound, and its volume steps are 3 dB each
	HelperSong S;
	S.PatternLength = 64;
	S.Frames = 1;
	S.K = Key(0, MODE_MAJOR);
	S.HasKey = true;
	S.Channels.push_back(MakeChannel("Pulse 1", "2A03", KIND_PULSE, 64));
	S.Channels.push_back(MakeChannel("Triangle", "2A03", KIND_TRIANGLE, 64));
	for (int i = 0; i < 6; ++i) {
		char Name[32];
		std::snprintf(Name, sizeof(Name), "FM %d", i + 1);
		S.Channels.push_back(MakeChannel(Name, "VRC7", KIND_FM, 64));
	}
	Put(S, 0, 0, 60, 0, 15);	// a melody note, C-5
	ChordPlan P;
	CHECK(PlanChords(S, Asking("F", 0, 16), P));
	CHECK(P.Voices.size() == 3 && P.Voices[0].Channel == 2);
	CHECK(ClassesIn(S, P, 0, 16) == Classes({ 5, 9, 0 }));
	for (size_t v = 0; v < P.Voices.size(); ++v)
		if (!P.Voices[v].Notes.empty()) {
			CHECK(P.Voices[v].Notes[0].Pitch - 12 < 60);	// sounds under the melody
			CHECK_EQ(P.Voices[v].Notes[0].Volume, 14);
		}
	CHECK(P.Voices.size() == 3 && P.Voices[0].Sound.Sound == "soft chords");
}

void TestOtherChips()
{
	// The 5B (written an octave over how it sounds, 3 dB volume steps), the FDS (two octaves over) and the MMC5 (pulses
	// with the 2A03's instruments): the 5B's squares take the voices first, all three of one kind
	HelperSong S;
	S.PatternLength = 64;
	S.Frames = 1;
	S.K = Key(0, MODE_MAJOR);
	S.HasKey = true;
	S.Channels.push_back(MakeChannel("Pulse 1", "2A03", KIND_PULSE, 64));
	S.Channels.push_back(MakeChannel("Pulse 2", "2A03", KIND_PULSE, 64));
	S.Channels.push_back(MakeChannel("Triangle", "2A03", KIND_TRIANGLE, 64));
	S.Channels.push_back(MakeChannel("MMC5 Pulse 1", "MMC5", KIND_PULSE, 64));
	S.Channels.push_back(MakeChannel("MMC5 Pulse 2", "MMC5", KIND_PULSE, 64));
	S.Channels.push_back(MakeChannel("FDS", "FDS", KIND_OTHER, 64));
	for (int i = 0; i < 3; ++i) {
		char Name[32];
		std::snprintf(Name, sizeof(Name), "5B %d", i + 1);
		S.Channels.push_back(MakeChannel(Name, "5B", KIND_SQUARE, 64));
	}
	Put(S, 0, 0, 60, 0, 15);	// the melody, C-5
	Put(S, 2, 0, 36, 1);		// the bass, C-3 written on the triangle (C-2 as it sounds)
	S.Instruments.push_back(MakeInstrument(0, "Lead", "2A03"));
	S.Instruments.push_back(MakeInstrument(1, "Bass", "2A03"));
	S.Instruments.push_back(MakeInstrument(2, "Soft chords", "5B", true));
	CHECK(TakesVoice(S.Channels[5]) && TakesVoice(S.Channels[3]) && TakesVoice(S.Channels[6]));
	const std::vector<int> Free = FreeChannels(S, 0, 16);
	CHECK(Free.size() == 7 && Free[0] == 6 && Free[1] == 7 && Free[2] == 8 && Free[3] == 1 && Free[4] == 3 && Free[5] == 4 &&
		Free[6] == 5);
	ChordPlan P;
	CHECK(PlanChords(S, Asking("F", 0, 16), P));
	CHECK(P.Voices.size() == 3 && P.Voices[0].Channel == 6 && P.Voices[1].Channel == 7 && P.Voices[2].Channel == 8);
	CHECK(ClassesIn(S, P, 0, 16) == Classes({ 5, 9, 0 }));
	for (size_t v = 0; v < P.Voices.size(); ++v) {
		CHECK_EQ(P.Voices[v].Sound.Instrument, 2);			// the song's own 5B soft chords
		if (P.Voices[v].Notes.empty())
			continue;
		CHECK(P.Voices[v].Notes[0].Pitch - 12 < 60 - 2);	// sounds under the melody...
		CHECK(P.Voices[v].Notes[0].Pitch - 12 > 24);		// ...and over the bass
		CHECK_EQ(P.Voices[v].Notes[0].Volume, 14);
	}

	// Four voices: no kind has four free, so the first four in their order take them, the 5B's and Pulse 2; the 2A03's
	// and the MMC5's sound is the 2A03's soft chords, made for them
	ChordRequest Four = Asking("Fmaj7", 0, 16);
	CHECK(PlanChords(S, Four, P));
	CHECK(P.Voices.size() == 4 && !P.TooFew);
	CHECK(P.Voices.size() == 4 && P.Voices[0].Channel == 1 && P.Voices[1].Channel == 6 && P.Voices[3].Channel == 8);
	if (P.Voices.size() == 4) {
		CHECK(P.Voices[0].Sound.Instrument < 0 && P.Voices[0].Sound.Sound == "soft chords");
		CHECK_EQ(P.Voices[0].Notes.size(), 1u);
		if (!P.Voices[0].Notes.empty())
			CHECK_EQ(P.Voices[0].Notes[0].Volume, 10);	// a pulse's volume as it is
	}
	CHECK(ClassesIn(S, P, 0, 16) == Classes({ 5, 9, 0, 4 }));

	// The 5B and the 2A03's Pulse 2 busy: the MMC5's pulses and the FDS, the FDS's notes written two octaves up (and
	// never over G#7)
	for (int c : { 1, 6, 7, 8 })
		Put(S, c, 0, 50, 0, 8);
	CHECK(PlanChords(S, Asking("F", 0, 16), P));
	CHECK(P.Voices.size() == 3 && P.Voices[0].Channel == 3 && P.Voices[1].Channel == 4 && P.Voices[2].Channel == 5);
	if (P.Voices.size() == 3 && !P.Voices[2].Notes.empty()) {
		const int Sounding = P.Voices[2].Notes[0].Pitch - 24;
		CHECK(Sounding < 60 && Sounding > 24);
		CHECK(P.Voices[2].Notes[0].Pitch <= 92);
		CHECK(P.Voices[2].Sound.Sound == "soft chords" && P.Voices[2].Sound.Instrument < 0);
		CHECK(VoiceFdsSound("soft chords") != 0);
	}
	CHECK(ClassesIn(S, P, 0, 16) == Classes({ 5, 9, 0 }));
}

}	// namespace

int main()
{
	std::setvbuf(stdout, 0, _IONBF, 0);	// every line out at once, so a test that dies still shows what it found
	TestFreeChannels();
	TestVoicesPlaced();
	TestTooFewChannels();
	TestProgressionOverSelection();
	TestSoftLayerVoices();
	TestEndOfSong();
	TestFmVoices();
	TestOtherChips();
	std::printf("%d checks, %d failed\n", g_Checks, g_Failures);
	return g_Failures == 0 ? 0 : 1;
}

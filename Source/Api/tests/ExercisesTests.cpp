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

// Tests for Help > Exercises' checks (Exercises.h) on songs described by hand
// as the tools describe them (ExerciseFacts): each check on a right answer, on
// typical mistakes with the feedback each gives, the links the texts make, and
// the verdict as JSON and back. The starter songs and the scripts are checked
// in ApiTests.cpp, which reads them as the tools do.

#include "../Exercises.h"
#include "../Glossary.h"
#include "../Json.h"

#include <cstdio>
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

bool Contains(const std::string &Text, const char *Part)
{
	return Text.find(Part) != std::string::npos;
}

#define CHECK_HAS(text, part) Check(Contains((text), (part)), "\"" part "\" in " #text, __LINE__)

// A 2A03 song of some bars (16 rows each, 4 a beat, 4 bars a frame) in C major, every channel empty
ExerciseFacts Song(int Bars, int Target = 0)
{
	ExerciseFacts F;
	F.PatternLength = 64;
	F.Frames = (Bars + 3) / 4;
	F.RowsPerBeat = 4;
	F.RowsPerBar = 16;
	const char *const NAMES[] = { "Pulse 1", "Pulse 2", "Triangle", "Noise", "DPCM" };
	const int KINDS[] = { KIND_PULSE, KIND_PULSE, KIND_TRIANGLE, KIND_NOISE, KIND_DPCM };
	for (int c = 0; c < 5; ++c) {
		ExerciseChannel C;
		C.Name = NAMES[c];
		C.Kind = KINDS[c];
		F.Channels.push_back(C);
	}
	F.Target = Target;
	F.HasKey = F.HasFound = true;
	F.K = F.Found = Key(0, MODE_MAJOR);
	F.EndBar = Bars;
	return F;
}

int Pitch(const char *Name)
{
	int Value = 0;
	ParsePitch(Name, Value);
	return Value;
}

// A note on a channel: bar and beat (from 1), its length in beats, its pitch
void Note(ExerciseFacts &F, int Channel, int Bar, double Beat, double Beats, const char *Name, int Volume = 12)
{
	ExerciseNote N;
	N.Row = (Bar - 1) * 16 + static_cast<int>((Beat - 1) * 4 + 0.5);
	N.Rows = N.Heard = static_cast<int>(Beats * 4 + 0.5);
	N.Pitch = Pitch(Name);
	N.Volume = Volume;
	N.Instrument = "Lead";
	F.Channels[static_cast<size_t>(Channel)].Notes.push_back(N);
}

// A tune in steps of eighths, a bar after another: "E3 - D3 C3 | ..." (- holds, . rests)
void Tune(ExerciseFacts &F, int Channel, int Bar, const std::string &Steps)
{
	int Step = (Bar - 1) * 8;
	std::vector<std::pair<int, std::pair<int, int> > > Made;	// first step, steps, pitch
	size_t At = 0;
	while (At < Steps.size()) {
		while (At < Steps.size() && Steps[At] == ' ')
			++At;
		size_t End = Steps.find(' ', At);
		if (End == std::string::npos)
			End = Steps.size();
		const std::string Token = Steps.substr(At, End - At);
		At = End;
		if (Token.empty() || Token == "|")
			continue;
		if (Token == "-") {
			if (!Made.empty())
				++Made.back().second.first;
		}
		else if (Token != ".")
			Made.push_back(std::make_pair(Step, std::make_pair(1, Pitch(Token.c_str()))));
		++Step;
	}
	for (size_t i = 0; i < Made.size(); ++i) {
		ExerciseNote N;
		N.Row = Made[i].first * 2;
		N.Rows = N.Heard = Made[i].second.first * 2;
		N.Pitch = Made[i].second.second;
		N.Volume = 12;
		F.Channels[static_cast<size_t>(Channel)].Notes.push_back(N);
	}
}

// A chord heard over some bars
void Chord(ExerciseFacts &F, int Bar, double Beat, double Beats, const char *Name, int Root, std::vector<int> Notes)
{
	ExerciseChord C;
	C.Row = (Bar - 1) * 16 + static_cast<int>((Beat - 1) * 4 + 0.5);
	C.Rows = static_cast<int>(Beats * 4 + 0.5);
	C.Name = Name;
	C.Root = Root;
	C.Notes = Notes;
	F.Chords.push_back(C);
}

// C F G C, a chord a bar; or the four bars' own
void Chords(ExerciseFacts &F, const char *Which)
{
	const std::string W = Which;
	for (int Bar = 1; Bar <= 4; ++Bar) {
		const char c = W[static_cast<size_t>(Bar - 1)];
		if (c == 'C')
			Chord(F, Bar, 1, 4, "C", 0, { 0, 4, 7 });
		else if (c == 'F')
			Chord(F, Bar, 1, 4, "F", 5, { 5, 9, 0 });
		else if (c == 'G')
			Chord(F, Bar, 1, 4, "G", 7, { 7, 11, 2 });
		else if (c == 'a')
			Chord(F, Bar, 1, 4, "Am", 9, { 9, 0, 4 });
		else if (c == 'g') {	// F then G
			Chord(F, Bar, 1, 2, "F", 5, { 5, 9, 0 });
			Chord(F, Bar, 3, 2, "G", 7, { 7, 11, 2 });
		}
	}
}

ExerciseVerdict Run(const char *Check, const ExerciseFacts &F)
{
	ExerciseVerdict V;
	const bool Known = CheckExercise(Check, F, V);
	if (!Known)
		std::printf("no check %s\n", Check);
	return V;
}

// Every link a verdict makes leads to a glossary entry
bool LinksLead(const ExerciseVerdict &V)
{
	std::vector<std::string> Texts(V.Right);
	Texts.push_back(V.Next);
	Texts.push_back(V.Learned);
	for (size_t t = 0; t < Texts.size(); ++t) {
		const std::vector<std::string> Ids = MarkedLinks(Texts[t]);
		for (size_t i = 0; i < Ids.size(); ++i)
			if (Ids[i].empty()) {
				std::printf("  a link leads nowhere in: %s\n", Texts[t].c_str());
				return false;
			}
	}
	return true;
}

std::string All(const ExerciseVerdict &V)
{
	std::string Text;
	for (size_t i = 0; i < V.Right.size(); ++i)
		Text += V.Right[i] + " ";
	return Text + "| " + V.Next + " | " + V.Learned;
}

void TestList()
{
	const std::vector<ExerciseInfo> &List = Exercises();
	CHECK(List.size() >= 8 && List.size() <= 10);
	std::vector<std::string> Seen;
	for (size_t i = 0; i < List.size(); ++i) {
		CHECK(FindExercise(List[i].Id) == &List[i]);
		CHECK(!List[i].Checks.empty() && List[i].Minutes > 0 && List[i].Minutes <= 10);
		CHECK(std::string(List[i].About).size() < 140);
		for (size_t c = 0; c < List[i].Checks.size(); ++c) {
			const ExerciseCheckInfo *pCheck = FindExerciseCheck(List[i].Checks[c]);
			CHECK(pCheck != 0);
			CHECK(ExerciseOfCheck(List[i].Checks[c]) == &List[i]);
			Seen.push_back(List[i].Checks[c]);
			if (pCheck != 0) {
				const std::vector<std::string> Ids = MarkedLinks(pCheck->Learned);
				CHECK(!Ids.empty());
				for (size_t k = 0; k < Ids.size(); ++k)
					CHECK(!Ids[k].empty());
			}
		}
		const ExerciseInfo *pAfter = i + 1 < List.size() ? &List[i + 1] : 0;
		CHECK(ExerciseAfter(List[i].Id) == pAfter);
	}
	CHECK(Seen.size() == ExerciseChecks().size());	// every check belongs to one exercise
	CHECK(FindExercise("nonsense") == 0 && FindExerciseCheck("bass") == 0 && FindExerciseCheck("bass-two") != 0);
	CHECK(ExerciseAfter("first-song") == 0 && ExerciseAfter("home-note") == FindExercise("four-bars"));
	CHECK(FindExercise("song-shape")->NewSong && FindExercise("first-song")->NewSong && !FindExercise("home-note")->NewSong);
	ExerciseVerdict V;
	CHECK(!CheckExercise("nonsense", Song(4), V));
}

void TestLinks()
{
	std::vector<ExerciseLink> Links;
	const std::string Plain = UnmarkLinks("End it on the [[home note]], or [[the note C|home-note]] [[nothing at all]].", &Links);
	CHECK(Plain == "End it on the home note, or the note C nothing at all.");
	CHECK(Links.size() == 2);
	if (Links.size() == 2) {
		CHECK(Plain.substr(Links[0].First, Links[0].Length) == "home note" && Links[0].Id == "home-note");
		CHECK(Plain.substr(Links[1].First, Links[1].Length) == "the note C" && Links[1].Id == "home-note");
	}
	const std::vector<std::string> Ids = MarkedLinks("[[chords|chord]] and [[kick]] and [[zzz]]");
	CHECK(Ids.size() == 3 && Ids[0] == "chord" && Ids[1] == "kick" && Ids[2].empty());
	CHECK(UnmarkLinks("no links [[ here") == "no links [[ here");
	CHECK(UnmarkLinks("") == "");
}

void TestDrumKinds()
{
	CHECK(ExerciseDrumKind("Kick", 0, true) == DRUM_HIT_KICK);
	CHECK(ExerciseDrumKind("Snare", 0, true) == DRUM_HIT_SNARE);
	CHECK(ExerciseDrumKind("Hi-hat", 4, true) == DRUM_HIT_HIHAT);
	CHECK(ExerciseDrumKind("Open hi-hat", 4, true) == DRUM_HIT_OPEN_HIHAT);
	CHECK(ExerciseDrumKind("Crash", 4, true) == DRUM_HIT_CRASH);
	// By the noise pitch where the instrument doesn't say: the drum grid's kick 4-#, snare A-#, hi-hat E-#
	CHECK(ExerciseDrumKind("Noise", 4, true) == DRUM_HIT_KICK);
	CHECK(ExerciseDrumKind("", 10, true) == DRUM_HIT_SNARE);
	CHECK(ExerciseDrumKind("", 14, true) == DRUM_HIT_HIHAT);
	CHECK(ExerciseDrumKind("Sample drums", 40, false) == DRUM_HIT_OTHER);
	CHECK(ExerciseDrumKind("Kick sample", 40, false) == DRUM_HIT_KICK);
	CHECK(std::string(ExerciseDrumName(DRUM_HIT_SNARE)) == "snare");
}

// The home note: a tune that stops on D, then on C
void TestHomeNote()
{
	ExerciseFacts F = Song(4);
	Chords(F, "CFGC");
	Tune(F, 0, 1, "E3 - G3 - C4 - G3 - | A3 - G3 F3 A3 - - - | G3 - F3 E3 D3 - - -");
	ExerciseVerdict V = Run("home-note", F);
	CHECK(!V.Passed);
	CHECK_HAS(V.Next, "Your tune ends on D (bar 3), which leaves it hanging");
	CHECK_HAS(V.Next, "End it on C, the [[home note]]: give bar 4 a C on its first beat and let it ring (C-3 in the pattern editor).");
	CHECK_HAS(All(V), "Its 12 notes all belong to the [[key]] of C major.");
	CHECK(V.PlayFirst == 2 && V.PlayLast == 4 && V.PlayChannels.empty());
	CHECK(LinksLead(V));
	// The last note on C: done, whatever its octave
	ExerciseFacts Done = F;
	Note(Done, 0, 4, 1, 4, "C-4");
	V = Run("home-note", Done);
	CHECK(V.Passed && V.Next.empty());
	CHECK_HAS(All(V), "It ends on C, the [[home note]] of C major, so it sounds finished.");
	CHECK_HAS(All(V), "Its last note rings for a while");
	CHECK_HAS(V.Learned, "[[home note]]");
	CHECK(V.NextExercise == "four-bars");
	CHECK(LinksLead(V));
	// A note after the C that leaves it hanging again, in the last bar
	ExerciseFacts After = Done;
	Note(After, 0, 4, 3, 2, "E-3");
	V = Run("home-note", After);
	CHECK(!V.Passed);
	CHECK_HAS(V.Next, "Your tune ends on E (bar 4)");
	CHECK_HAS(V.Next, "change that last note to C (C-3), or add one after it.");
	// A tune taken away
	ExerciseFacts Gone = Song(4);
	V = Run("home-note", Gone);
	CHECK(!V.Passed);
	CHECK_HAS(V.Next, "Pulse 1 has no notes now");
	// In another key: G major's home note is G
	ExerciseFacts InG = Song(4);
	InG.K = InG.Found = Key(7, MODE_MAJOR);
	Tune(InG, 0, 1, "B3 - D4 - | C4 - A3 - | A3 - F#3 - | D3 - - -");
	V = Run("home-note", InG);
	CHECK(!V.Passed);
	CHECK_HAS(V.Next, "End it on G, the [[home note]]");
}

// Four bars over C Am F G C: the tune fills them, ends at home, moves mostly by step
void TestFourBars()
{
	ExerciseFacts F = Song(4);
	Chords(F, "CagC");
	ExerciseVerdict V = Run("four-bars", F);
	CHECK(!V.Passed);
	CHECK_HAS(V.Next, "Write a tune on Pulse 1, 4 bars long");
	CHECK_HAS(All(V), "The [[chords|chord]] are ready for your tune: C, Am, F, G, C.");
	// Two bars only
	ExerciseFacts Short = F;
	Tune(Short, 0, 1, "E3 - D3 - E3 F3 G3 - | A3 - G3 - E3 - - -");
	V = Run("four-bars", Short);
	CHECK(!V.Passed);
	CHECK_HAS(V.Next, "Your tune plays in bars 1-2. Carry it on to bar 4");
	// Four bars that end on G
	ExerciseFacts OnG = F;
	Tune(OnG, 0, 1, "E3 - D3 - E3 F3 G3 - | A3 - G3 - E3 - - - | F3 - A3 - G3 - B3 - | A3 - - - G3 - - -");
	V = Run("four-bars", OnG);
	CHECK(!V.Passed);
	CHECK_HAS(V.Next, "Your tune ends on G (bar 4). End it on C, the [[home note]]");
	CHECK_HAS(All(V), "It moves mostly by [[step|step-leap]]");
	// Leaps all over: an arpeggio up and down
	ExerciseFacts Leaps = F;
	Tune(Leaps, 0, 1, "C3 - G3 - C4 - G3 - | A3 - E3 - C4 - E3 - | F3 - C4 - G3 - D4 - | G3 - C4 - - - - -");
	V = Run("four-bars", Leaps);
	CHECK(!V.Passed);
	CHECK_HAS(V.Next, "Your tune jumps a lot: ");
	CHECK_HAS(V.Next, "like C to G in bar 1");
	CHECK(V.PlayChannels.size() == 1 && V.PlayChannels[0] == 0);
	// A good answer: mostly steps, home at the end
	ExerciseFacts Good = F;
	Tune(Good, 0, 1, "E3 - D3 - E3 F3 G3 - | A3 - G3 - E3 - - - | F3 - A3 - G3 - B3 - | C4 - - - - - - -");
	V = Run("four-bars", Good);
	CHECK(V.Passed);
	CHECK_HAS(All(V), "Your tune fills all 4 bars.");
	CHECK_HAS(All(V), "It moves mostly by [[step|step-leap]] (9 of its 12 moves).");
	CHECK(LinksLead(V));
}

// Strong notes: notes of the chord on beats 1 and 3
void TestStrongNotes()
{
	ExerciseFacts F = Song(4);
	Chords(F, "CagC");
	Tune(F, 0, 1, "E3 - D3 E3 D3 - C3 - | B3 - C4 B3 A3 - - - | A3 - G3 A3 C4 - B3 - | D4 - - - C4 - - -");
	ExerciseVerdict V = Run("strong-notes", F);
	CHECK(!V.Passed);
	CHECK_HAS(V.Next, "Bar 1, beat 3: the tune plays D over C, a note from outside the [[chord]], so it rubs. Try C, E and G, the "
		"notes of C");
	CHECK_HAS(V.Next, "3 more like it: bar 2 beat 1, bar 3 beat 3 and bar 4 beat 1.");
	CHECK_HAS(All(V), "4 of the 8 notes on beats 1 and 3 are notes of their chords already.");
	CHECK(V.PlayFirst == 1 && V.PlayLast == 1);
	CHECK(LinksLead(V));
	ExerciseFacts Good = Song(4);
	Chords(Good, "CagC");
	Tune(Good, 0, 1, "E3 - D3 E3 C3 - D3 - | A3 - C4 B3 A3 - - - | A3 - G3 A3 B3 - D4 - | C4 - - - - - - -");
	V = Run("strong-notes", Good);
	CHECK(V.Passed);
	CHECK_HAS(All(V), "All 7 notes on beats 1 and 3 are notes of their [[chords|chord]]");
	// A note held into a strong beat isn't one that starts there
	ExerciseFacts Held = Good;
	Held.Channels[0].Notes.clear();
	Tune(Held, 0, 1, "E3 - D3 F3 - - D3 - | A3 - C4 B3 A3 - - - | A3 - G3 A3 B3 - D4 - | C4 - - - - - - -");
	V = Run("strong-notes", Held);
	CHECK(V.Passed);
	// Too few notes on strong beats to say
	ExerciseFacts Off = Song(4);
	Chords(Off, "CagC");
	Tune(Off, 0, 1, ". E3 . D3 . C3 . D3 | . E3 . - . - . -");
	V = Run("strong-notes", Off);
	CHECK(!V.Passed);
	CHECK_HAS(V.Next, "Your tune has no notes that start on beat 1 or 3");
}

// A phrase that breathes: the spans as BreathsOf counts them
void TestPhrases()
{
	ExerciseFacts F = Song(4);
	Tune(F, 0, 1, "C4 B3 A3 G3 E3 F3 G3 E3 | D3 E3 F3 G3 B3 A3 G3 F3 | E3 F3 A3 C4 B3 A3 G3 F3 | E3 D3 E3 G3 C4 B3 C4 G3");
	F.Spans = 2;
	F.Breaths = 0;
	F.RunOn = { 1, 3 };
	ExerciseVerdict V = Run("phrases", F);
	CHECK(!V.Passed);
	CHECK_HAS(V.Next, "Bars 1-2 run on without a breath");
	CHECK_HAS(V.Next, "drag the last note of bar 2 longer");
	CHECK(V.PlayFirst == 1 && V.PlayLast == 2 && V.PlayChannels.size() == 1);
	CHECK(LinksLead(V));
	F.Breaths = 1;
	F.RunOn = { 3 };
	V = Run("phrases", F);
	CHECK(!V.Passed);
	CHECK_HAS(V.Next, "Bars 3-4 run on without a breath");
	CHECK_HAS(All(V), "Bars 1-2 breathe at their end");
	F.Breaths = 2;
	F.RunOn.clear();
	V = Run("phrases", F);
	CHECK(V.Passed);
	CHECK_HAS(All(V), "Each of its 2 phrases ends with a breath");
	// Half the tune taken away
	ExerciseFacts Half = Song(4);
	Tune(Half, 0, 1, "C4 B3 A3 G3 E3 F3 G3 E3 | D3 - - - . . . .");
	Half.Spans = 1;
	Half.Breaths = 1;
	V = Run("phrases", Half);
	CHECK(!V.Passed);
	CHECK_HAS(V.Next, "Your tune plays only in bars 1-2 now");
}

// A bass line: roots on beat 1, then two notes a bar
void TestBass()
{
	ExerciseFacts F = Song(4, 2);
	Chords(F, "CaFG");
	ExerciseVerdict V = Run("bass-roots", F);
	CHECK(!V.Passed);
	CHECK_HAS(V.Next, "Bar 1 has no bass note on its first [[beat]]. The chord there is C, built on C, its [[root]]: put a C on "
		"Triangle at the start of bar 1 (C-3).");
	CHECK(V.PlayChannels.size() == 1 && V.PlayChannels[0] == 2);
	Note(F, 2, 1, 1, 4, "C-3");
	Note(F, 2, 2, 1, 4, "A-2");
	Note(F, 2, 3, 1, 4, "D-3");	// not F's root
	V = Run("bass-roots", F);
	CHECK(!V.Passed);
	CHECK_HAS(V.Next, "Bar 3's bass plays D on its first beat, but the chord there is F.");
	CHECK_HAS(V.Next, "the note it's built on: F (F-3)");
	CHECK_HAS(All(V), "Bars 1 and 2 have the chord's [[root]] on the first beat.");
	CHECK(LinksLead(V));
	F.Channels[2].Notes[2].Pitch = Pitch("F-2");
	Note(F, 2, 4, 1, 4, "G-2");
	V = Run("bass-roots", F);
	CHECK(V.Passed);
	CHECK_HAS(All(V), "Every bar starts with its chord's [[root]] in the bass (bars 1, 2, 3 and 4).");
	CHECK(V.NextExercise.empty());	// the bass's second check comes next
	// Two notes a bar
	V = Run("bass-two", F);
	CHECK(!V.Passed);
	CHECK_HAS(V.Next, "Bar 1 has one bass note. Add a second on beat 3: the chord's [[fifth]] (G) or its root again (C).");
	ExerciseFacts Two = Song(4, 2);
	Chords(Two, "CaFG");
	const char *const ROOTS[] = { "C-3", "A-2", "F-2", "G-2" }, *const FIFTHS[] = { "G-2", "E-2", "C-3", "D-3" };
	for (int Bar = 1; Bar <= 4; ++Bar) {
		Note(Two, 2, Bar, 1, 2, ROOTS[Bar - 1]);
		Note(Two, 2, Bar, 3, 2, Bar == 2 ? "B-2" : FIFTHS[Bar - 1]);
	}
	V = Run("bass-two", Two);
	CHECK(!V.Passed);
	CHECK_HAS(V.Next, "Bar 2's bass plays B after its root, a note outside Am, so it rubs against the chord. Try A, C and E.");
	Two.Channels[2].Notes[3].Pitch = Pitch("E-2");
	V = Run("bass-two", Two);
	CHECK(V.Passed);
	CHECK(V.NextExercise == "beat");
	CHECK(LinksLead(V));
}

// A beat: a kick on 1, snares on 2 and 4, hi-hats, in both bars
void TestBeat()
{
	ExerciseFacts F = Song(2, 3);
	auto Hit = [&](int Bar, int Step, int Kind) {
		ExerciseNote N;
		N.Row = (Bar - 1) * 16 + Step - 1;
		N.Rows = N.Heard = 1;
		N.Pitch = Kind == DRUM_HIT_KICK ? 4 : Kind == DRUM_HIT_SNARE ? 10 : 14;
		N.Volume = 15;
		F.Channels[3].Notes.push_back(N);
		F.Channels[3].Hits.push_back(Kind);
	};
	ExerciseVerdict V = Run("beat", F);
	CHECK(!V.Passed);
	CHECK_HAS(V.Next, "Bar 1 has no [[kick]] on beat 1. In the drum grid (View > Drum grid), click the Kick row at step 1");
	CHECK(V.PlayChannels.size() == 1 && V.PlayChannels[0] == 3);
	// Four on the floor: kicks only, and hi-hats
	for (int Bar = 1; Bar <= 2; ++Bar)
		for (int Step = 1; Step <= 16; Step += 2)
			Hit(Bar, Step, Step % 4 == 1 ? DRUM_HIT_KICK : DRUM_HIT_HIHAT);
	V = Run("beat", F);
	CHECK(!V.Passed);
	CHECK_HAS(V.Next, "Bar 1 has no [[snare]] on beat 2 (step 5 in the drum grid). The snare on beats 2 and 4 is the backbeat");
	CHECK(LinksLead(V));
	// The basic beat in bar 1, without hi-hats in bar 2
	F.Channels[3].Notes.clear();
	F.Channels[3].Hits.clear();
	for (int Bar = 1; Bar <= 2; ++Bar) {
		Hit(Bar, 1, DRUM_HIT_KICK);
		Hit(Bar, 5, DRUM_HIT_SNARE);
		Hit(Bar, 9, DRUM_HIT_KICK);
		Hit(Bar, 13, DRUM_HIT_SNARE);
		for (int Step = 3; Step <= 15 && Bar == 1; Step += 4)
			Hit(Bar, Step, DRUM_HIT_HIHAT);
	}
	V = Run("beat", F);
	CHECK(!V.Passed);
	CHECK_HAS(V.Next, "Bar 2 has no [[hi-hats|hi-hat]] yet. Click the Hi-hat row between the kicks and the snares (steps 3, 7, 11 "
		"and 15)");
	CHECK_HAS(All(V), "Bar 1 has the whole beat");
	for (int Step = 3; Step <= 15; Step += 4)
		Hit(2, Step, DRUM_HIT_OPEN_HIHAT);
	V = Run("beat", F);
	CHECK(V.Passed);
	CHECK_HAS(All(V), "Both bars have a [[kick]] on 1, [[snares|snare]] on 2 and 4, and [[hi-hats|hi-hat]] in between.");
	CHECK(LinksLead(V));
}

// Major and minor: the third over the home note lowered
void TestMinor()
{
	ExerciseFacts F = Song(4);
	F.HasKey = false;
	const char *const BASS[] = { "C-3", "F-2", "G-2", "C-3" };
	for (int Bar = 1; Bar <= 4; ++Bar)
		Note(F, 2, Bar, 1, 4, BASS[Bar - 1]);
	Tune(F, 0, 1, "C3 - E3 - G3 - E3 - | F3 - E3 D3 C3 - - - | D3 - E3 F3 G3 - F3 E3 | E3 D3 C3 - - - - -");
	ExerciseVerdict V = Run("minor", F);
	CHECK(!V.Passed);
	CHECK_HAS(V.Next, "Your tune still plays E in bars 1, 2, 3 and 4: E is the [[third]] above C that makes it sound bright and "
		"happy ([[major and minor|major-minor]]). Lower each E one semitone, to Eb");
	CHECK(LinksLead(V));
	// Some lowered
	for (size_t i = 0; i < F.Channels[0].Notes.size(); ++i)
		if (F.Channels[0].Notes[i].Pitch == Pitch("E-3") && F.Channels[0].Notes[i].Row < 32)
			F.Channels[0].Notes[i].Pitch = Pitch("D#3");
	V = Run("minor", F);
	CHECK(!V.Passed);
	CHECK_HAS(V.Next, "Your tune still plays E in bars 3 and 4");
	CHECK_HAS(All(V), "3 notes are Eb already (bars 1 and 2).");
	for (size_t i = 0; i < F.Channels[0].Notes.size(); ++i)
		if (F.Channels[0].Notes[i].Pitch == Pitch("E-3"))
			F.Channels[0].Notes[i].Pitch = Pitch("D#3");
	F.HasFound = true;
	F.Found = Key(0, MODE_MINOR);
	V = Run("minor", F);
	CHECK(V.Passed);
	CHECK_HAS(All(V), "Your tune plays Eb where it played E: the same tune, now in C minor.");
	CHECK(LinksLead(V));
	// Every third taken away: neither happy nor sad
	ExerciseFacts None = Song(4);
	for (int Bar = 1; Bar <= 4; ++Bar)
		Note(None, 2, Bar, 1, 4, BASS[Bar - 1]);
	Tune(None, 0, 1, "C3 - D3 - G3 - D3 - | F3 - G3 D3 C3 - - - | D3 - C3 F3 G3 - F3 D3 | D3 D3 C3 - - - - -");
	V = Run("minor", None);
	CHECK(!V.Passed);
	CHECK_HAS(V.Next, "neither happy nor sad");
	// The whole song moved down into A minor
	ExerciseFacts Moved = Song(4);
	const char *const LOW[] = { "A-2", "D-2", "E-2", "A-2" };
	for (int Bar = 1; Bar <= 4; ++Bar)
		Note(Moved, 2, Bar, 1, 4, LOW[Bar - 1]);
	Tune(Moved, 0, 1, "A2 - C3 - E3 - C3 - | D3 - C3 B2 A2 - - - | B2 - C3 D3 E3 - D3 C3 | C3 B2 A2 - - - - -");
	Moved.Found = Key(9, MODE_MINOR);
	V = Run("minor", Moved);
	CHECK(V.Passed);
}

// A hook that comes back with another ending
void TestHook()
{
	ExerciseFacts F = Song(4);
	Tune(F, 0, 1, "E3 - G3 E3 C3 - D3 E3 | D3 - B2 - D3 - - -");
	ExerciseVerdict V = Run("hook", F);
	CHECK(!V.Passed);
	CHECK_HAS(V.Next, "Bars 3-4 have no tune yet: play your [[hook]] again there.");
	CHECK_HAS(All(V), "Your [[hook]] is in bars 1-2: it starts E, G, E and C.");
	ExerciseFacts Same = F;
	Tune(Same, 0, 3, "E3 - G3 E3 C3 - D3 E3 | D3 - B2 - D3 - - -");
	V = Run("hook", Same);
	CHECK(!V.Passed);
	CHECK_HAS(V.Next, "Bars 3-4 repeat the hook note for note. Now give it a different ending");
	ExerciseFacts Other = F;
	Tune(Other, 0, 3, "G3 - - - A3 - G3 - | E3 - D3 - C3 - - -");
	V = Run("hook", Other);
	CHECK(!V.Passed);
	CHECK_HAS(V.Next, "Bar 3 doesn't start like your [[hook]]");
	ExerciseFacts Good = F;
	Tune(Good, 0, 3, "E3 - G3 E3 C3 - D3 E3 | D3 - B2 - C3 - - -");
	V = Run("hook", Good);
	CHECK(V.Passed);
	CHECK_HAS(All(V), "Bar 3 starts like the [[hook]], so it's heard again.");
	CHECK_HAS(All(V), "Bar 4 ends differently (on C)");
	// The hook again a step higher (a sequence) still starts like it
	ExerciseFacts Higher = F;
	Tune(Higher, 0, 3, "F3 - A3 F3 D3 - E3 F3 | E3 - D3 - C3 - - -");
	V = Run("hook", Higher);
	CHECK(V.Passed);
	CHECK(LinksLead(V));
}

// The song's shape: its parts and what each is for
void TestSongShape()
{
	ExerciseFacts F = Song(4, -1);
	ExerciseVerdict V = Run("song-shape", F);
	CHECK(!V.Passed);
	CHECK_HAS(V.Next, "Open Module > Song shape");
	CHECK(LinksLead(V));
	const char *const NAMES[] = { "Intro", "A", "B" };
	for (int i = 0; i < 3; ++i) {
		ExerciseSection S;
		S.Name = NAMES[i];
		S.FirstBar = i * 4 + 1;
		S.LastBar = i * 4 + 4;
		S.Line = i == 0 ? "Sets the scene." : "";
		F.Sections.push_back(S);
	}
	F.Shape = "Town";
	V = Run("song-shape", F);
	CHECK(V.Passed);
	CHECK_HAS(All(V), "Your song is laid out as a town, in 3 parts:");
	CHECK_HAS(All(V), "Intro (bars 1-4): Sets the scene.");
	CHECK_HAS(All(V), "B (bars 9-12).");
	CHECK(V.NextExercise == "first-song");
}

// A first small song: chords and a bass, a tune, a loop
void TestFirstSong()
{
	ExerciseFacts F = Song(8, -1);
	ExerciseVerdict V = Run("first-song", F);
	CHECK(!V.Passed);
	CHECK_HAS(V.Next, "Open View > What next and press Do it");
	// A tune alone: the chords and the bass come next
	ExerciseFacts Alone = F;
	Tune(Alone, 0, 1, "E3 - D3 - C3 - D3 - | E3 E3 E3 - - - - - | D3 D3 D3 - - - - -");
	Alone.Melody = 0;
	V = Run("first-song", Alone);
	CHECK(!V.Passed);
	CHECK_HAS(V.Next, "Next come the [[chords|chord]] and the [[bass line]]");
	// Chords and a bass, as What next writes them first: the tune next
	for (int Bar = 1; Bar <= 8; ++Bar) {
		Note(F, 1, Bar, 1, 4, "E-3");
		Note(F, 2, Bar, 1, 4, "C-3");
	}
	F.Melody = -1;	// the tools hear no melody in them
	V = Run("first-song", F);
	CHECK(!V.Passed);
	CHECK_HAS(V.Next, "Now a tune: when the What next panel gets to the [[melody]], press Grow one");
	CHECK_HAS(All(V), "It has [[chords|chord]] and a [[bass line]] (Pulse 2 and Triangle).");
	Tune(F, 0, 1, "E3 - D3 - C3 - D3 - | E3 E3 E3 - - - - - | D3 D3 D3 - - - - -");
	F.Melody = 0;
	F.Halts = true;
	V = Run("first-song", F);
	CHECK(!V.Passed);
	CHECK_HAS(V.Next, "Your song stops at its end");
	CHECK_HAS(All(V), "It has a tune (Pulse 1).");
	F.Halts = false;
	V = Run("first-song", F);
	CHECK(V.Passed);
	CHECK_HAS(All(V), "It goes round when it ends: a [[loop]], ready for a game.");
	CHECK(V.NextExercise.empty() && V.PlayFirst == 1 && V.PlayLast == 8);
	CHECK(LinksLead(V));
}

// The verdict as JSON for FamiTracker's note, read back; and as text for Claude
void TestVerdictJson()
{
	ExerciseVerdict V;
	V.Check = "bass-two";
	V.Passed = false;
	V.Right.push_back("Every bar starts with its chord's [[root]].");
	V.Next = "Bar 1 has one bass note.";
	V.PlayFirst = 1;
	V.PlayLast = 2;
	V.PlayChannels.push_back(2);
	const std::string Text = WriteJson(ExerciseVerdictJson(V));
	CHECK_HAS(Text, "\"exercise\":\"bass\"");
	CHECK_HAS(Text, "\"title\":\"A bass line\"");
	ExerciseVerdict Back;
	std::string Error;
	CHECK(ReadExerciseVerdict(Text, Back, Error));
	CHECK(Back.Check == "bass-two" && !Back.Passed && Back.Right == V.Right && Back.Next == V.Next && Back.PlayFirst == 1 &&
		Back.PlayLast == 2 && Back.PlayChannels == V.PlayChannels && Back.Learned.empty() && Back.NextExercise.empty());
	CHECK(!ReadExerciseVerdict("[1, 2]", Back, Error) && !Error.empty());
	CHECK(!ReadExerciseVerdict("{\"passed\": true}", Back, Error));
	CHECK(!ReadExerciseVerdict("{\"check\": \"beat\"", Back, Error));
	const std::string Words = ExerciseVerdictText(V);
	CHECK_HAS(Words, "Not yet: A bass line.");
	CHECK_HAS(Words, "What's right: Every bar starts with its chord's root.");
	CHECK_HAS(Words, "What to change next: Bar 1 has one bass note.");
	CHECK_HAS(Words, "Bars 1-2 show it best");
	CHECK(Words.find("[[") == std::string::npos);
	V.Passed = true;
	V.Next.clear();
	V.Learned = "Two notes a bar.";
	V.NextExercise = "beat";
	CHECK(ReadExerciseVerdict(WriteJson(ExerciseVerdictJson(V)), Back, Error) && Back.Passed && Back.Learned == V.Learned &&
		Back.NextExercise == "beat");
	CHECK_HAS(ExerciseVerdictText(V), "The next exercise: beat (A beat), from Help > Exercises.");
}

}	// namespace

int main()
{
	TestList();
	TestLinks();
	TestDrumKinds();
	TestHomeNote();
	TestFourBars();
	TestStrongNotes();
	TestPhrases();
	TestBass();
	TestBeat();
	TestMinor();
	TestHook();
	TestSongShape();
	TestFirstSong();
	TestVerdictJson();
	std::printf("%d checks, %d failures\n", g_Checks, g_Failures);
	return g_Failures == 0 ? 0 : 1;
}

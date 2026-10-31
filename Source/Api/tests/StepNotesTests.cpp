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

// Tests for melodies written in steps (StepNotes.h): the notation read (pitches,
// holds across bar lines, rests, volumes, bar numbers, line breaks, drums of two
// words), the mistakes it names by bar and step, and the text written back. The
// tools' side (the song's bars, the grid, the cells) is in ApiTests.cpp.

#include "../StepNotes.h"

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
#define CHECK_EQ(a, b) Check((a) == (b), #a " == " #b, __LINE__)

bool Contains(const std::string &Text, const char *Part)
{
	return Text.find(Part) != std::string::npos;
}

#define CHECK_HAS(text, part) Check(Contains((text), (part)), "\"" part "\" in " #text, __LINE__)

StepLine Parsed(const std::string &Text, int FirstBar = 0)
{
	StepLine Line;
	std::string Error;
	const bool Fine = ParseStepLine(Text, FirstBar, Line, Error);
	Check(Fine, ("parses: " + Text + " (" + Error + ")").c_str(), __LINE__);
	return Line;
}

std::string Refused(const std::string &Text, int FirstBar = 0)
{
	StepLine Line;
	std::string Error;
	const bool Fine = ParseStepLine(Text, FirstBar, Line, Error);
	Check(!Fine && !Error.empty(), ("refused: " + Text).c_str(), __LINE__);
	return Error;
}

bool HasNote(const StepLine &Line, int Step, int Steps, const char *Pitch, int Volume = -1)
{
	for (size_t i = 0; i < Line.Notes.size(); ++i)
		if (Line.Notes[i].Step == Step)
			return Line.Notes[i].Steps == Steps && Line.Notes[i].Pitch == Pitch && Line.Notes[i].Volume == Volume;
	return false;
}

// The example of the instructions: two bars of 4/4 in eighths, a note held over beats 2 and 3, rests, a note to the end
void TestExample()
{
	const StepLine Line = Parsed("D4 G4 A4 B4 - A4 G4 - | B4 - - - . . D5 -");
	CHECK_EQ(Line.Bars.size(), 2u);
	CHECK(Line.Bars[0].First == 0 && Line.Bars[0].Steps == 8 && Line.Bars[0].Number == 0);
	CHECK(Line.Bars[1].First == 8 && Line.Bars[1].Steps == 8);
	CHECK_EQ(Line.Steps(), 16);
	CHECK_EQ(Line.Notes.size(), 8u);
	CHECK(HasNote(Line, 0, 1, "D4") && HasNote(Line, 1, 1, "G4") && HasNote(Line, 2, 1, "A4"));
	CHECK(HasNote(Line, 3, 2, "B4") && HasNote(Line, 5, 1, "A4") && HasNote(Line, 6, 2, "G4"));
	CHECK(HasNote(Line, 8, 4, "B4") && HasNote(Line, 14, 2, "D5"));	// the rests at 12 and 13 end the B4
	CHECK_EQ(Line.Held, 0);
	CHECK_EQ(Line.FirstBar, 0);
	int InBar = -1;
	CHECK(StepLineBarOf(Line, 9, &InBar) == 1 && InBar == 1);
	CHECK(StepLineBarOf(Line, 0, &InBar) == 0 && InBar == 0);
	CHECK_EQ(StepLineBarOf(Line, 16), -1);
}

// - holds across bar lines; a note lasts until the next pitch or rest; - after a rest is more rest
void TestHolds()
{
	StepLine Line = Parsed("C4 - - - - - - - | - - - - E4 - - - | - - - - - - - -");
	CHECK(HasNote(Line, 0, 12, "C4") && HasNote(Line, 12, 12, "E4"));	// tied into the next bar, and over a whole bar
	CHECK_EQ(Line.Notes.size(), 2u);
	Line = Parsed(". - - - D4 . - -");
	CHECK(HasNote(Line, 4, 1, "D4") && Line.Notes.size() == 1u && Line.Held == 0);
	// Before any pitch or rest, - holds the note sounding into the line
	Line = Parsed("- - - D4 - - - - | . . . . . . . .");
	CHECK(Line.Held == 3 && HasNote(Line, 3, 5, "D4"));
	Line = Parsed("- - - - - - - - | - - G4 - - - - -");
	CHECK(Line.Held == 10 && HasNote(Line, 10, 6, "G4"));
	// Holds and rests written together: "--" two holds, "B4--" a note and two holds, "..." three rests
	Line = Parsed("B4-- -- ... .");
	CHECK(HasNote(Line, 0, 5, "B4") && Line.Steps() == 9 && Line.Notes.size() == 1u);
}

// Every way of writing a pitch is kept as written (the tools read it), with its volume taken off
void TestPitchesAndVolumes()
{
	StepLine Line = Parsed("D-4 C#4 Bb3 db5 B4v12 - C5V7 e-3v0 kickv9");
	CHECK(HasNote(Line, 0, 1, "D-4") && HasNote(Line, 1, 1, "C#4") && HasNote(Line, 2, 1, "Bb3") && HasNote(Line, 3, 1, "db5"));
	CHECK(HasNote(Line, 4, 2, "B4", 12) && HasNote(Line, 6, 1, "C5", 7) && HasNote(Line, 7, 1, "e-3", 0));
	CHECK(HasNote(Line, 8, 1, "kick", 9));
	CHECK_HAS(Refused("D4 B4v16 - -"), "volume 16");
	CHECK_HAS(Refused("D4 B4v16 - -"), "the line's bar 1, step 2");
	CHECK_HAS(Refused("D4 B4v16 - -", 5), "bar 5, step 2");
	// Drums whose names have two words are one step
	Line = Parsed("kick . open hihat . snare drum . low tom high tom | noise 10 - crash cymbal -");
	CHECK(HasNote(Line, 0, 1, "kick") && HasNote(Line, 2, 1, "open hihat") && HasNote(Line, 4, 1, "snare drum"));
	CHECK(HasNote(Line, 6, 1, "low tom") && HasNote(Line, 7, 1, "high tom"));
	CHECK(HasNote(Line, 8, 2, "noise 10") && HasNote(Line, 10, 2, "crash cymbal"));
	CHECK(Line.Bars.size() == 2u && Line.Bars[0].Steps == 8 && Line.Bars[1].Steps == 4);
	// Commas between steps are spaces
	Line = Parsed("D4, G4, A4, -");
	CHECK(HasNote(Line, 2, 2, "A4") && Line.Steps() == 4);
}

// Hits in a row are steps of their own: only the words of one drum's name join ("hihat hihat" ran together as one step,
// "kick hihat" too, which made a bar of hi-hats too short)
void TestDrumRuns()
{
	StepLine Line = Parsed("hihat hihat hihat open hihat - hihat kick hihat | snare snare drum hi-hat open hat closed hihat low tom tom");
	CHECK(Line.Bars.size() == 2u && Line.Bars[0].Steps == 8 && Line.Bars[1].Steps == 7);
	CHECK(HasNote(Line, 0, 1, "hihat") && HasNote(Line, 1, 1, "hihat") && HasNote(Line, 2, 1, "hihat"));
	CHECK(HasNote(Line, 3, 2, "open hihat") && HasNote(Line, 5, 1, "hihat") && HasNote(Line, 6, 1, "kick") && HasNote(Line, 7, 1, "hihat"));
	CHECK(HasNote(Line, 8, 1, "snare") && HasNote(Line, 9, 1, "snare drum") && HasNote(Line, 10, 1, "hi-hat") &&
		HasNote(Line, 11, 1, "open hat") && HasNote(Line, 12, 1, "closed hihat") && HasNote(Line, 13, 1, "low tom") &&
		HasNote(Line, 14, 1, "tom"));
}

// The noise's pitches as FamiTracker shows them and as a model writes them (from feat-tool-replies), and pitches spelled
// with flats: each one step
void TestNoiseAndFlats()
{
	StepLine Line = Parsed("F-# . F# . noise F noise 15 noise F-# 0-# | Eb4 - Bb3 Ab3 Db4 - . .");
	CHECK(Line.Bars.size() == 2u && Line.Bars[0].Steps == 8 && Line.Bars[1].Steps == 8);
	CHECK(HasNote(Line, 0, 1, "F-#") && HasNote(Line, 2, 1, "F#") && HasNote(Line, 4, 1, "noise F") && HasNote(Line, 5, 1, "noise 15") &&
		HasNote(Line, 6, 1, "noise F-#") && HasNote(Line, 7, 1, "0-#"));
	CHECK(HasNote(Line, 8, 2, "Eb4") && HasNote(Line, 10, 1, "Bb3") && HasNote(Line, 11, 1, "Ab3") && HasNote(Line, 12, 2, "Db4"));
}

// | ends a bar, as a new line does; | at a line's start or end, a double bar line and blank lines end no other
void TestBarLines()
{
	StepLine Line = Parsed("D4 - - -\nE4 - - -\n\nF4 - - -");
	CHECK(Line.Bars.size() == 3u && Line.Bars[2].First == 8);
	Line = Parsed("| D4 - - - |\n| E4 - - - |\nF4 - - - ||");
	CHECK(Line.Bars.size() == 3u && Line.Steps() == 12);
	Line = Parsed("D4|E4 - -|F4");
	CHECK(Line.Bars.size() == 3u && Line.Bars[0].Steps == 1 && Line.Bars[1].Steps == 3 && Line.Bars[2].Steps == 1);
	Line = Parsed("D4 - - - || E4 - - -");	// between sections
	CHECK(Line.Bars.size() == 2u);
	Line = Parsed("D4 - - - | | ");	// a closing bar line
	CHECK(Line.Bars.size() == 1u);
	// Bars of different lengths are the tools' to judge
	Line = Parsed("D4 - | E4 - - - - -");
	CHECK(Line.Bars[0].Steps == 2 && Line.Bars[1].Steps == 6);
	// An empty bar between steps is a mistake: a bar of rests is written
	std::string Error = Refused("D4 - - - | | E4 - - -");
	CHECK_HAS(Error, "the line's bar 2 is empty");
	CHECK_HAS(Error, ". . . .");
	CHECK_HAS(Refused("D4 - - - | | E4 - - -", 9), "bar 10 is empty");
	CHECK_HAS(Refused(""), "no steps");
	CHECK_HAS(Refused(" | \n "), "no steps");
}

// A bar may start with its number: the first says where the line starts, the others are checked
void TestBarNumbers()
{
	StepLine Line = Parsed("5: D4 G4 A4 B4 - A4 G4 -\n6: B4 - - - . . D5 -");
	CHECK(Line.FirstBar == 5 && Line.Bars[0].Number == 5 && Line.Bars[1].Number == 6);
	CHECK(HasNote(Line, 8, 4, "B4"));
	Line = Parsed("5:D4 - - - | 6:E4 - - -", 5);	// joined to the first step; given and written alike
	CHECK(Line.FirstBar == 5 && HasNote(Line, 0, 4, "D4") && HasNote(Line, 4, 4, "E4"));
	Line = Parsed("D4 - - - | 6: E4 - - -");	// a later number says where the first is
	CHECK(Line.FirstBar == 5 && Line.Bars[0].Number == 0);
	Line = Parsed("D4 - - -", 3);
	CHECK(Line.FirstBar == 3);
	// A bar missing or one too many shows as a number out of place
	std::string Error = Refused("5: D4 - - - | 7: E4 - - -");
	CHECK_HAS(Error, "says bar 7 where its bar 2 is bar 6");
	CHECK_HAS(Refused("6: D4 - - -", 5), "says bar 6 where its bar 1 is bar 5");
	CHECK_HAS(Refused("D4 - | 1: E4 - "), "before bar 1");
	CHECK_HAS(Refused("D4 - 2: E4 -"), "in the middle of");
	CHECK_HAS(Refused("0: D4 - - -"), "count from 1");
	CHECK_HAS(Refused("5: D4 - - - | 6: | 7: E4"), "(\"6:\") has no steps");
	CHECK_HAS(Refused("5: D4 - - - | 6:"), "(\"6:\") has no steps");
}

// The text written back reads as the same line, a bar to a text line with its number
void TestFormat()
{
	const std::string Text = "5: D4 G4 A4 B4 - A4 G4 -\n6: B4 - - - . . D5v9 -\n7: - - - . . . . .";
	const StepLine Line = Parsed(Text);
	CHECK_EQ(FormatStepLine(Line), Text);
	StepLine Again = Parsed(FormatStepLine(Line));
	CHECK(Again.Notes.size() == Line.Notes.size() && Again.Steps() == Line.Steps() && Again.FirstBar == 5);
	// Without numbers, and with a hold at its start
	StepLine Plain = Parsed("- - C4 - | . . . .");
	CHECK_EQ(FormatStepLine(Plain), "- - C4 -\n. . . .");
	// Made by hand, as read_notes makes it
	StepLine Made;
	Made.FirstBar = 2;
	for (int b = 0; b < 2; ++b) {
		StepLineBar Bar;
		Bar.First = b * 6;
		Bar.Steps = 6;
		Bar.Number = 2 + b;
		Made.Bars.push_back(Bar);
	}
	Made.Held = 1;
	StepLineNote Note;
	Note.Step = 3;
	Note.Steps = 5;
	Note.Pitch = "F#3";
	Note.Volume = 11;
	Made.Notes.push_back(Note);
	CHECK_EQ(FormatStepLine(Made), "2: - . . F#3v11 - -\n3: - - . . . .");
}

}	// namespace

int main()
{
	std::setvbuf(stdout, 0, _IONBF, 0);
	TestExample();
	TestHolds();
	TestPitchesAndVolumes();
	TestBarLines();
	TestBarNumbers();
	TestFormat();
	TestDrumRuns();
	TestNoiseAndFlats();
	std::printf("%d checks, %d failed\n", g_Checks, g_Failures);
	return g_Failures == 0 ? 0 : 1;
}

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

#pragma once

// A melody written a step at a time, as write_notes takes it and read_notes
// gives it (Tools.cpp): in
//   "D4 G4 A4 B4 - A4 G4 - | B4 - - - . . D5 -"
// every token is one step of a grid (an eighth unless the tool is told
// otherwise) and | ends a bar, as a new line does. A pitch starts a note, -
// holds the note before it one more step (across a bar line too, so a note
// ties into the next bar) and . is a step of rest; a note lasts until the next
// pitch or rest, so a phrase is legato unless a gap is written. A pitch may
// end in its volume, "B4v12" (0-15), and a bar may start with its number,
// "5:", which is checked against where the bar falls. Only the text is here:
// what a step lasts, which pitches a channel plays and the song's bars are the
// tools' business. The types are named apart from the tools' (namespace ftapi).

#include <string>
#include <vector>

namespace ftapi {

struct StepLineNote {
	int Step;			// its first step, counted from the line's first (0)
	int Steps;			// how many steps it lasts, at least 1
	std::string Pitch;	// as written, without its volume: "D4", "C#4", "D-4", "kick", "open hihat"
	int Volume;			// 0-15, -1 when it gives none
	StepLineNote() : Step(0), Steps(1), Volume(-1) {}
};

struct StepLineBar {
	int First;			// its first step, counted from the line's first
	int Steps;			// the steps written in it
	int Number;			// the bar number written at its start ("5:"), 0 for none
	StepLineBar() : First(0), Steps(0), Number(0) {}
};

struct StepLine {
	int FirstBar;		// the song's bar the line starts at: as given, or from the numbers written; 0 when neither says
	int Held;			// steps at its start that hold the note sounding before the line (- before any pitch or rest)
	std::vector<StepLineBar> Bars;
	std::vector<StepLineNote> Notes;	// in order
	StepLine() : FirstBar(0), Held(0) {}
	int Steps() const;	// in all its bars
};

// Reads a line. FirstBar is the song's bar it starts at (0 when the caller doesn't know: the bar numbers in
// the text say it, if any). False with the reason, which names the bar (by its number in the song when that's
// known, else counted in the line) and the step in it.
bool ParseStepLine(const std::string &Text, int FirstBar, StepLine &Line, std::string &Error);

// The text of a line, a bar to a text line: "5: D4 G4 A4 B4 - A4 G4 -" (with the bar's number when it has one)
std::string FormatStepLine(const StepLine &Line);

// The bar (from 0) a step of the line falls in, -1 past its end; pInBar gets the step's place in it (from 0)
int StepLineBarOf(const StepLine &Line, int Step, int *pInBar = 0);

}	// namespace ftapi

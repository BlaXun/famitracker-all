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

// Chords for Edit > Add chords and bass line (InsertRecording.cpp) that the
// user gives instead of FamiTracker finding them from the melody: typed by
// name, or strummed on a guitar (or played on a piano) and heard. Names are
// read as the tools read them (Chords.h), and the chords come back in the
// transcription engine's terms (Transcribe.h), whose bass line and arpeggios
// play them. Plain C++14, independent of MFC, tested on Linux.

#include "../Transcribe/Transcribe.h"

#include <string>
#include <vector>

namespace ftapi {

// Chords typed by name ("C G Am F", a chord a bar, or "| C | G | Am F |" with
// bar lines; "Am7", "Gsus4", "C/E", "N.C.", "." holding the chord before, "%"
// repeating a bar, or degrees of the key: "I vi IV V"), laid on Bars: the
// bars repeat the typed ones when there are more of them. The key reads the
// degrees and spells the names: *pKey when given, otherwise the one the
// chords are in (their notes against the key profiles, the first chord's root
// counting most), returned in KeyOut. Names: the chords bar by bar as laid out
// ("| C | G | Am F |"). False with the reason in Error.
bool LayOutTypedChords(const std::string &Text, const std::vector<transcribe::Bar> &Bars, int RowsPerBeat,
	const transcribe::Key *pKey, transcribe::Key &KeyOut, std::vector<transcribe::ChordSpan> &Chords, std::string &Names,
	std::string &Error);

// How many bars typed chords are ("C G Am F": 4, "| C | G F |": 2); 0 when
// there are none
int TypedChordBars(const std::string &Text);

// The chords of a strummed take, bar by bar
struct StrummedChords {
	int FirstRow;				// the row of the bar its first strum is in (song rows)
	int BarCount;				// bars from there to the last strum's
	transcribe::Key Key;		// the key they're in
	std::string Names;			// "| C | G | Am F |", as LayOutTypedChords reads them back
	StrummedChords() : FirstRow(0), BarCount(0) {}
};

// The chords heard in strums (transcribe::HearStrums), each bar's or half
// bar's from the notes sounding there (the lowest one counted as the bass),
// as the tools hear chords in a song's parts. Start: the second of the take
// that lands on song row StartRow; the song plays RowsPerSecond, its bars as
// FamiTracker highlights them (a bar starts on each pattern's first row and
// every RowsPerBar rows after it). False, with the reason, when no chord
// could be heard.
bool HearStrummedChords(const std::vector<transcribe::Strum> &Strums, double Start, int StartRow, double RowsPerSecond,
	int PatternLength, int RowsPerBar, int RowsPerBeat, StrummedChords &Out, std::string &Error);

}	// namespace ftapi

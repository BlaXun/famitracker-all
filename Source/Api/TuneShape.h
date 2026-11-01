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

// What makes a tune catchy, measured (from feat-hooks): a melody's opening
// motif and how often it comes back (as written, moved, or in its rhythm),
// its bars' rhythms and how often one comes back, its range and peak in each
// phrase, its leaps and repeated notes; and whether a tune or a hook is
// monotone by those measures, with what to change. The numbers come from the
// 44 songs the user keeps (TuneShape.cpp says what was measured). Plain C++14,
// independent of MFC, tested on Linux; its types are named apart from the
// tools' (namespace ftapi).

#include <string>
#include <vector>

namespace ftapi {

// A note of a tune: its bar (from 0), its first row from the tune's first
// bar's start, how long it sounds (rows) and its pitch
struct ShapeNote {
	int Bar;
	int Start;
	int Rows;
	int Pitch;
	ShapeNote() : Bar(0), Start(0), Rows(1), Pitch(0) {}
	ShapeNote(int InBar, int First, int Length, int Note) : Bar(InBar), Start(First), Rows(Length), Pitch(Note) {}
};

struct TuneMeasure {
	int Bars;				// the bars measured (from the first with a note)
	int Notes;
	double NotesPerBar;		// in the bars with a note
	int MotifNotes;			// the opening motif's notes: the longest opening (3-6 notes) that comes back in its rhythm
	int Returns;			// how often the opening motif (its first MotifNotes, at least 3) comes back in the bars measured
	int Exact, Moved, Rhythmic;	// of those: as written, moved (the same moves, or the same shape a step or so wider), in
							// its rhythm only
	int ShapeBars;			// bars of 3 notes or more that move whose rhythm and shape an earlier bar had (as written or moved)
	int RhythmBars;			// bars whose rhythm an earlier bar had, but not its shape
	int Rhythms;			// the bars' rhythms (where notes start in the bar), how many differ
	double RhythmReturn;	// the share of bars after the first whose rhythm an earlier bar had
	int Lengths;			// note lengths (onset to onset, a rest's end counted), how many differ
	double MainLength;		// the share of notes of the commonest length
	int Range;				// highest less lowest, semitones
	double PhraseRange;		// the mean of each 4-bar phrase's range (phrases with 3 notes or more)
	int Leaps;				// moves of a fourth or more (5 semitones)
	int Biggest;			// the biggest move
	int Answered;			// leaps followed by a move the other way, no bigger
	int Phrases;			// 4-bar phrases with 3 notes or more
	int Peaked;				// of those: a high point heard once (a note higher than any other in the phrase, not repeated)
	double PeakAt;			// where the phrases' high points sit, 0 (start) to 1 (end), on average
	double Repeats;			// the share of moves to the same pitch
	int SameRun;			// the longest run of notes on one pitch
	double Steps;			// the share of moves of one or two semitones
	double Zigzag;			// the share of pairs of moves that turn straight back by a step or two (E F E F: a trill, not a tune)
	double PhrasePitches;	// the pitches each 4-bar phrase plays, on average
	TuneMeasure() : Bars(0), Notes(0), NotesPerBar(0), MotifNotes(0), Returns(0), Exact(0), Moved(0), Rhythmic(0), ShapeBars(0), RhythmBars(0), Rhythms(0),
		RhythmReturn(0), Lengths(0), MainLength(0), Range(0), PhraseRange(0), Leaps(0), Biggest(0), Answered(0), Phrases(0),
		Peaked(0), PeakAt(0), Repeats(0), SameRun(0), Steps(0), Zigzag(0), PhrasePitches(0) {}
};

// The tune's notes (in order, the bars from 0) measured over Bars bars of
// BarRows rows each (a beat RowsPerBeat)
TuneMeasure MeasureTune(const std::vector<ShapeNote> &Notes, int Bars, int BarRows, int RowsPerBeat);

// One line of numbers, for the calibration (song_file_checks -tunes)
std::string TuneMeasureText(const TuneMeasure &M);

// What's monotone about a melody (8 bars or more measured from its start):
// each problem in a few words ("its opening motif never comes back"), empty
// when it's catchy enough by the kept songs' measures
std::vector<std::string> MonotoneMelody(const TuneMeasure &M);

// What's monotone about a hook (a bar or two, 2-7 notes): each problem in a
// few words, empty when it's fine
std::vector<std::string> MonotoneHook(const TuneMeasure &M);

// The advice for a hook or melody found monotone: how to write one, with a
// worked example in steps (a token an eighth) in C major
std::string HookAdvice();

}	// namespace ftapi

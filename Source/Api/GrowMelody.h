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

// A melody grown from a motif, for the Claude Code connection's grow_melody
// tool: a hook of 4-7 notes (a bar or two, as a line of steps gives it) made
// into phrases of 2 and 4 bars over the song's chords, the way a composer
// develops one: the motif, an answer, a sequence or a variation, a cadence;
// chord tones on the strong beats and steps between them; a breath where each
// phrase ends (a rest of a beat or more, or a note held two beats), one high
// point a section, long and short notes mixed, notes tied across bar lines
// inside a phrase, in the channel's register and the key's own notes. It
// gives two or three versions, each with a sentence on its shape; a variant
// gives others, the same ones every time. Steps count from the first bar's
// first step, pitches are FamiTracker's as written on the melody's channel.
// Plain C++14, independent of MFC, so it is tested on Linux. The types are
// named apart from the tools' (namespace ftapi).

#include "Chords.h"
#include "Music.h"
#include <string>
#include <vector>

namespace ftapi {

// A note on the grid of steps
struct GrowNote {
	int Step;		// its first step (a motif's: from its own first bar's start)
	int Steps;		// how long, at least 1
	int Pitch;		// as written on the melody's channel
	GrowNote() : Step(0), Steps(1), Pitch(0) {}
	GrowNote(int First, int Length, int Note) : Step(First), Steps(Length), Pitch(Note) {}
};

// A note another part sounds against the melody, as it's heard there: its
// pitch as written on the melody's channel (the triangle's and the VRC7's
// octave counted), its rows from the first bar's first row
struct GrowOther {
	int Row, End;
	int Pitch;
};

enum grow_kind { GROW_CALM, GROW_LIVELY, GROW_ACTION };

struct GrowRequest {
	std::vector<GrowNote> Motif;	// in order, in its first bar or two
	int Bars;						// how many to grow over
	int StepsPerBar;				// every bar's
	int StepsPerBeat;
	int RowsPerStep;
	int FirstBar;					// the song's number for the first bar, for the words
	std::vector<Chord> Chords;		// the chords heard
	std::vector<int> StepChord;		// per step, into Chords; -1 where none sounds
	std::vector<GrowOther> Others;
	int RubRows;					// how long a semitone against another part lasts before it's heard as a clash
	Key K;
	int Low, High;					// the channel's notes for a melody, as written
	std::vector<int> Sections;		// the bars (from 0) where sections after the first start: one high point each
	int SpanOffset;					// 1 when the first bar is the second of a two-bar span (review_arrangement's), else 0
	int Kind;						// grow_kind
	int Variant;					// from 1: each gives other versions
	GrowRequest() : Bars(8), StepsPerBar(8), StepsPerBeat(2), RowsPerStep(2), FirstBar(1), RubRows(2), Low(24), High(84),
		SpanOffset(0), Kind(GROW_LIVELY), Variant(1) {}
};

struct GrownVersion {
	std::vector<GrowNote> Notes;	// in order
	std::string Form;				// "question and answer"
	std::string Shape;				// a sentence on its shape
	int Spans, Breaths;				// the two-bar spans it plays in and those it breathes in
	std::vector<int> Peaks;			// each section's high point
	std::vector<int> PeakBars;		// where (from 0)
	std::vector<int> Hooks;			// the bars (from 0) where the motif comes back as written, its first bar
	std::vector<int> Phrases;		// each phrase's bars, in order
	GrownVersion() : Spans(0), Breaths(0) {}
};

// The motif grown over the bars: three versions (two over fewer than four
// bars), or false with the reason
bool GrowMelody(const GrowRequest &Q, std::vector<GrownVersion> &Out, std::string &Error);

// The two-bar spans a line plays in and those it breathes in, as
// review_arrangement counts a melody's: a rest of a beat or more, or a note
// held two beats or more, in the span where it starts (the spans from
// SpanOffset, as GrowRequest has it)
void GrowBreaths(const std::vector<GrowNote> &Notes, int StepsPerBar, int StepsPerBeat, int Bars, int SpanOffset, int &Spans,
	int &Breaths);

}	// namespace ftapi

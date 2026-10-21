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

// The pattern cell under the cursor in words, for the status bar: a note and
// how high it is, its instrument and volume, or an effect with what its
// number does ("452: vibrato, speed 5, depth 2: gentle"), for people who
// don't read tracker notation yet.
//
// Plain C++ on FamiTrackerTypes.h's effect_t and note_t, tested on Linux
// (Source/Tests/CellHintTests.cpp).

#include <string>

// The kind of channel, for what a note and some effects mean there
enum hint_channel_t {
	HINT_PULSE,				// the 2A03's and MMC5's pulses
	HINT_VRC6_PULSE,
	HINT_TRIANGLE,
	HINT_NOISE,
	HINT_DPCM,
	HINT_SAWTOOTH,			// the VRC6's
	HINT_OTHER,				// VRC7, FDS, N163, 5B
};

// A cell, as stChanNote holds it
struct hint_cell_t {
	int Note;				// note_t: NONE, C to B, RELEASE, HALT
	int Octave;
	int Instrument;			// MAX_INSTRUMENTS (64) for none
	int Volume;				// MAX_VOLUME (16) for none
	int Effect[4];			// effect_t
	int Param[4];

	hint_cell_t();			// empty
};

// What the cursor is on: 0 the note, 1 the instrument, 2 the volume, 3-6 the
// effect columns. InstrumentName is the instrument's when the song has it
// (InstrumentExists). SplitPoint is the song's speed/tempo split (Fxx).
std::string DescribeCell(const hint_cell_t &Cell, int Column, hint_channel_t Kind, bool InstrumentExists,
	const std::string &InstrumentName, int SplitPoint = 0x20);

// An effect with its parameter: "4: vibrato ..." (the code first, as the
// pattern shows it, then what it does)
std::string DescribeEffect(int Effect, int Param, hint_channel_t Kind, int SplitPoint = 0x20);

// A note as the pattern shows it: "C#4", "4-#" on the noise, "===", "---"
std::string HintNoteName(int Note, int Octave, hint_channel_t Kind);

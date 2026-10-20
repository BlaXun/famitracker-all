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

// Edit > Drum grid: a bar of the noise channel as steps, a row each, and the
// drums on them: a kick, a snare, a hi-hat and an open hi-hat, each an
// instrument of the song played at the noise pitch it sounds best at (as
// Claude Code and the recorder write them). The noise channel plays one
// sound at a time, so a step holds one drum at most.
//
// This is the grid's arithmetic on a bar of cells; DrumGridDlg.cpp shows it
// and writes the bars back into the song.

#include <vector>

struct stChanNote;

enum drum_lane_t {
	DRUM_KICK,
	DRUM_SNARE,
	DRUM_HIHAT,
	DRUM_OPEN_HIHAT,
	DRUM_LANES
};

const int DRUM_NONE = -1;				// no note on the step
const int DRUM_OTHER = DRUM_LANES;		// a note that's none of the kit's drums

// The song's instrument for each drum, -1 when it has none yet
struct drum_kit_t {
	int Instruments[DRUM_LANES];
	drum_kit_t();
};

int DrumPitch(int Lane);				// noise pitch 0-15: kick 4, snare A, hi-hat E, open hi-hat D
const char *DrumName(int Lane);			// "Kick", "Snare", "Hi-hat", "Open hi-hat" (the instruments' names too)
const char *DrumPreset(int Lane);		// the sound preset each is made from: "kick", "snare", "hi-hat", "open hi-hat"

// Which drum a cell plays: its instrument tells, or without one, its pitch
int DrumOf(const stChanNote &Cell, const drum_kit_t &Kit);

// Puts a drum on a step, replacing what was there, or takes it away when
// it's there already. Effects stay. The kit must have the drum's instrument.
void ToggleDrum(stChanNote &Cell, int Lane, const drum_kit_t &Kit);
void SetDrum(stChanNote &Cell, int Lane, const drum_kit_t &Kit);	// DRUM_NONE takes the note away

// Ready-made beats for a bar, a step a row, StepsPerBeat rows a beat (the
// first row highlight). They replace the bar's notes; effects stay.
enum drum_beat_t {
	BEAT_BASIC,				// kick and snare taking turns on the beats, hi-hats between
	BEAT_FOUR_ON_FLOOR,		// a kick on every beat, hi-hats between
	BEAT_HALF_TIME,			// a kick to start, a snare halfway, hi-hats around
	BEAT_BUSY,				// the basic beat with an extra kick and quicker hi-hats
	BEAT_CLEAR,				// no drums
	BEAT_COUNT
};

const char *BeatName(int Beat);			// "Basic", "Four on the floor", ...
void ApplyBeat(std::vector<stChanNote> &Bar, int Beat, int StepsPerBeat, const drum_kit_t &Kit);

// The drums a beat uses, which the kit needs instruments for
bool BeatUses(int Beat, int Lane);

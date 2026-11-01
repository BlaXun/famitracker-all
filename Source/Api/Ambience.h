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

// A quiet layer of ambience inside a music track (add_ambience, Tools.cpp): the sea's waves, rain, wind, a stream or a
// fire on the noise channel, under the music, in step with its bars and its loop. Made the way the user's soundtrack made
// its waves (Bobber on the Bay: a noise note swelling in over a second at a middling pitch, then one a little darker
// sinking away over two, then a rest, every few seconds, at about 8-9 dB under the lead), not from make_sound_effect's
// loops, whose random parts run for seconds a note: here each sound is an instrument whose sequences shape it, and its
// notes start on the song's rows. Every instrument is named "Ambience ..." (IsAmbienceName), which the checks, the mix
// and the review know as ambience rather than drums or a part. Plain C++14.

#include <string>
#include <vector>
#include "Presets.h"

namespace ftapi {

enum ambience_kind { AMBIENCE_WAVES, AMBIENCE_RAIN, AMBIENCE_WIND, AMBIENCE_STREAM, AMBIENCE_FIRE, AMBIENCE_KINDS };

struct AmbienceKind {
	const char *Name;		// "waves"
	const char *What;		// what it sounds like, for the replies
	const char *Words;		// the words that ask for it, a space between ("sea harbor bay ...")
};

const AmbienceKind &AmbienceKindInfo(int Kind);
std::string AmbienceKindNames();				// "waves, rain, wind, stream, fire"
// A kind by its name or one of its words ("harbor": waves); false for none
bool ParseAmbienceKind(const std::string &Text, int &Kind);
// The kind words name: the first whole word of Text that's one of a kind's words ("a quiet harbor at night": waves),
// -1 for none; Word: the word found
int AmbienceOfWords(const std::string &Text, std::string *pWord = 0);

// An instrument of an ambience layer: "Ambience waves in", any case
bool IsAmbienceName(const std::string &InstrumentName);

// A note of the layer: on Row for Rows rows, at noise pitch Pitch (0-15, 15 the brightest), playing Sounds[Sound], its
// volume Share of the layer's (a wave a little smaller than the one before: 0.75)
struct AmbienceNote {
	int Row, Rows, Pitch, Sound;
	double Share;
};

struct AmbiencePlan {
	int Kind;
	std::vector<Preset> Sounds;			// the instruments (noise; Drum set), named "ambience ..."
	std::vector<AmbienceNote> Notes;	// in order, each ending where the next starts or at its stretch's end
	int CycleRows;						// rows a cycle (a wave and its rest, a gust, a turn of the rain), whole bars
	int CycleBars;
	int PerCycle;						// notes of its kind a cycle (waves: 2 where a long bar holds two)
	int SoundFrames;					// frames each note's sound shapes (the waves': swelling in and sinking away)
};

// The layer over stretches of rows ([first, end) each, in order: the intro that plays once, then the loop), each
// starting its cycles afresh so the loop starts as the song first did; RowsPerBar rows a bar, FramesPerRow frames a row
// (its speed and tempo), FrameRate ticks a second. Seed picks the waves' pitches and the rain's drops, the same
// for the same song.
AmbiencePlan PlanAmbience(int Kind, const std::vector<std::pair<int, int> > &Stretches, int RowsPerBar, double FramesPerRow,
	int FrameRate, unsigned int Seed);

// How far under the melody an ambience layer plays as the tools hear it (PlayedDb over each sound's shape against the
// melody's usual level): Bobber on the Bay's waves play 8-9 dB under its whistle, so 9
const double AMBIENCE_UNDER_DB = 9.0;

}	// namespace ftapi

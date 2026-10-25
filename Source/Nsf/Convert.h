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

// Recorded NSF tracks as a readable FamiTracker module: the rows and speed
// found from when notes start, notes on rows (a Gxx delay where one starts
// between them), instruments rebuilt from the notes' volume, duty and pitch
// shapes, fast chord flickers as 0xy arpeggios, hardware sweeps as Hxy and
// Ixy, the drums on the noise channel and the DPCM samples as keys of an
// instrument, and the loop set with Bxx.

#include "Module.h"
#include "NsfFile.h"
#include "Player.h"

#include <string>
#include <vector>

namespace nsf {

struct ConvertOptions {
	int RowsPerBeat;		// what rows aim to be: 4 makes them sixteenth notes where the music allows
	int MaxInstruments;		// FamiTracker's limit is 64, for the whole module
	ConvertOptions() : RowsPerBeat(4), MaxInstruments(64) {}
};

// What became of one track
struct TrackReport {
	std::string Name;
	double TicksPerRow;		// as found in the music
	int RowsPerBeat;
	int BeatsPerBar;		// 4, unless the music says (a MIDI file's meter)
	int FirstBarRow;		// where the bars start when the music says, else -1 (found by the study)
	std::string Meter;		// "3/4", "6/8", when the music says
	int Speed, Tempo;
	double Bpm;
	int Rows;				// played once through
	int LoopRow;			// where it loops back to, -1 when it ends
	double Seconds;			// how long it plays: to its last sound when it ends, else the intro and the loop once
	double PlaylistSeconds;	// the length its playlist gives it, 0 when it has none
	double DrumLoopRows;	// the drums' own cycle, in rows, when they don't follow the loop; 0 when they do
	int Notes;
	int Delayed;			// notes that start between rows, given a Gxx
	int Dropped;			// notes too short to keep, on a row another note took
	std::vector<std::string> Channels;	// "Pulse 1: 120 notes, lead", ...
	std::vector<std::string> Problems;
	bool Written;			// a FamiTracker module's own track: its tempo is the speed and tempo written in it
	TrackReport() : TicksPerRow(0), RowsPerBeat(4), BeatsPerBar(4), FirstBarRow(-1), Speed(6), Tempo(150), Bpm(0), Rows(0), LoopRow(-1), Seconds(0),
		PlaylistSeconds(0), DrumLoopRows(0), Notes(0), Delayed(0), Dropped(0), Written(false) {}
};

struct ConvertReport {
	std::vector<TrackReport> Tracks;
	std::vector<std::string> Problems;	// for the whole module
	int Instruments;
	int Expansion;			// the chip the module uses (expansion_t)
	ConvertReport() : Instruments(0), Expansion(EXP_NONE) {}
};

struct TrackSource {
	std::string Name;
	const Recording *Rec;
};

// The tracks (recorded from File) as one module, a track each
bool ConvertTracks(const NsfFile &File, const std::vector<TrackSource> &Tracks, const ConvertOptions &Options, Module &Out,
	ConvertReport &Report, std::string &Error);

// The rows hidden in a list of note starts (ticks, with weights): the
// longest row length (in ticks, possibly fractional) that most starts fall
// on, and the tick of a row. Exposed for the tests.
bool FindGrid(const std::vector<std::pair<int, double> > &Onsets, double &TicksPerRow, double &Phase, double &Aligned);

// FamiTracker's speed and tempo that make TicksPerRow at an engine rate
void SpeedTempo(double TicksPerRow, double FrameRate, int &Speed, int &Tempo);

// A DPCM sample as a pitched sound: the period of the wave it plays, in the
// sample's bits (fractional), when it holds a tone that repeats (a sampled
// bass wave); 0 when it doesn't (a drum, a voice)
double SamplePeriod(const std::vector<uint8_t> &Data);
// The FamiTracker note (C-0 = 0, fractional) that a wave of PeriodBits bits
// sounds at a DPCM rate (0-15)
double SampleNote(double PeriodBits, int Rate, bool Pal);
// Whether a DPCM part plays its samples as a line of notes (a sampled bass)
// rather than as drums: most of its notes are pitched samples, sounding at
// least MinNotes different notes. Pitches has, for each note played, its
// sample's note at its rate (SampleNote, rounded), or UNPITCHED.
const int UNPITCHED = -1000;
bool PlaysALine(const std::vector<int> &Pitches, int MinNotes = 4);

}	// namespace nsf

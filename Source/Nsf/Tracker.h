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

// Music modules from the PC trackers (MOD, S3M, XM and IT), read for their
// pattern data alone: the notes, the instruments' names, the speed and the
// order the patterns play in. The samples aren't read. MIDI files are read as
// such a song too (Midi.h). The song becomes a Module of one track (Module.h)
// with a layout of its own: a channel for the notes each of the module's
// channels plays, and one for its drums, so that a study (Study.h) can
// describe how the music is made, for Claude to write music of its own in
// that style rather than to copy it.

#include "Convert.h"
#include "Module.h"

#include <cstdint>
#include <string>
#include <vector>

namespace nsf {

// A cell's note besides 0-119
enum { TRACKER_NONE = -1, TRACKER_OFF = -2, TRACKER_CUT = -3 };

struct TrackerCell {
	int Note;			// TRACKER_* or 0-119 (C-0 = 0), in FamiTracker's octaves: middle C is C-4
	int Instrument;		// from 0, -1 for none
	int Volume;			// 0-64, -1 for none
	char Effect;		// as FamiTracker writes its nearest effect ('0' arpeggio, '4' vibrato, 'F' speed or tempo,
						// 'D' pattern break, ...), 0 for none or one with no counterpart
	int Param;
	TrackerCell() : Note(TRACKER_NONE), Instrument(-1), Volume(-1), Effect(0), Param(0) {}
};

struct TrackerPattern {
	int Rows;
	std::vector<std::vector<TrackerCell> > Cells;	// [row][channel]
	TrackerPattern() : Rows(64) {}
};

struct TrackerSong {
	std::string Format;			// "MOD", "S3M", "XM", "IT" or "MIDI"
	std::string Title;
	std::string Tracker;		// what made it, when the file says (MIDI: "format 1, 9 tracks")
	std::string Message;		// IT's song message; MIDI: what reading it found (tempo changes, notes left out)
	std::string Copyright;		// MIDI's copyright notice
	int Channels;
	int Speed, Tempo;			// at the start: ticks a row and the tempo; a row lasts 2.5 / Tempo * Speed seconds
	int RowsPerBeat;			// the module's highlight (IT), MIDI's beat (a quarter note, 6/8's dotted quarter), else 4
	int BeatsPerBar;			// MIDI's meter in beats, else 4
	int FirstBarRow;			// the first bar line, as MIDI's meter says; -1 when the file doesn't say
	std::string Meter;			// MIDI's: "3/4", "6/8"; "" when the file doesn't say
	int Restart;				// the order it goes back to after the last, -1 when it stops there
	std::vector<std::string> ChannelNames;	// what each channel plays (MIDI's parts), empty when the file doesn't say
	std::vector<int> Orders;	// patterns in playing order
	std::vector<TrackerPattern> Patterns;
	std::vector<std::string> Instruments;	// names: the instruments' (XM, IT), else the samples'
	std::vector<std::string> Samples;		// the samples' names, when there are instruments too
	// Instruments with samples of their own (XM, IT): the sample each note
	// plays (in Samples, -1 for none) and the note it plays it at. The notes
	// are the music's own pitches: the samples' tunings (XM's relative notes,
	// IT's C-5 speeds) are there to make them sound as written.
	std::vector<std::vector<std::pair<int, int> > > KeyMaps;	// [instrument][note 0-119]
	TrackerSong() : Channels(0), Speed(6), Tempo(125), RowsPerBeat(4), BeatsPerBar(4), FirstBarRow(-1), Restart(0) {}
};

// MOD (ProTracker and its 1-32 channel kin), S3M, XM, IT or a MIDI file
// (Midi.h), by what the bytes say they are; false with the reason when
// they're none of them or broken
bool ParseTracker(const std::vector<uint8_t> &Bytes, TrackerSong &Out, std::string &Error);
bool ReadTrackerFile(const std::string &Path, TrackerSong &Out, std::string &Error);

// The module in a line: "Title: an XM module of 8 channels (FastTracker
// v2.00), 24 orders, 14 instruments"; a MIDI file with its meter and the
// instruments its parts play"
std::string DescribeTracker(const TrackerSong &Song);

// The drum an instrument's name says it is: "kick", "snare", "hi-hat",
// "cymbal", "tom" or "percussion", "" when the name doesn't say
std::string DrumOfName(const std::string &Name);

// The song as a Module of one track for StudyTrack, with what a conversion
// report says about its tempo: a melodic channel for each of the module's
// channels that plays notes, and a drum channel for each that plays drums
// (instruments named as drums, or that play only a note or two), named
// after the song's ChannelNames when it has them. Speed and tempo,
// arpeggios, slides and vibrato, volume slides, note cuts and delays,
// pattern breaks and jumps keep their FamiTracker effects; the song loops
// back to its restart order. A MIDI file's beat and meter pass to the report
// as they are.
bool TrackerToModule(const TrackerSong &Song, Module &Out, TrackReport &Report, std::string &Error);

}	// namespace nsf

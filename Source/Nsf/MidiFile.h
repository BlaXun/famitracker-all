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

// Standard MIDI files (.mid, .midi, and RIFF's .rmi), read for their notes
// as a tracker song (Tracker.h), so that a study describes how the music is
// made, as it does for MOD, S3M, XM and IT modules:
//
// - The notes go on a grid of rows: 4 a quarter note, or 6, 8 or 12 when
//   they fall between (triplets, thirty-seconds); for music played rather
//   than written, 4. The tempo is the one the file keeps longest; its
//   changes become tempo effects.
// - The meter (the time signature) sets the beat and the bar the study
//   counts in: 3/4 has bars of three beats, 6/8 two beats of three eighths.
//   The file's bar lines are the study's.
// - Each part (a track's MIDI channel) becomes channels of one note at a
//   time: a chord's notes spread over up to six, each note on the free one
//   nearest in pitch, and a note held a little into the next one (legato)
//   ends there. The drums (MIDI channel 10) go on a channel for each kind:
//   kick, snare, hi-hat, cymbal, tom, percussion. Past 32 channels, the
//   voices with the fewest notes are left out.
// - The instruments are named after General MIDI's programs and drums,
//   which tells the study what each part is; the channels after the tracks
//   (or the programs, when a track plays several parts).
// - A note's volume is its velocity at its channel's volume and expression.
// - A loop point (controller 111 as RPG Maker writes it, EMIDI's loop
//   controllers, or a "loopStart" marker) makes the song loop there, and the
//   loop's end ("loopEnd") ends the song.

#include "Tracker.h"

#include <cstdint>
#include <string>
#include <vector>

namespace nsf {

// The song's instruments: General MIDI's 128 programs, then its drums (keys
// 35-81) from here, then one for the drum keys outside them
const int MIDI_DRUM_INSTRUMENTS = 128;

// Is it a MIDI file (SMF, or one wrapped in RIFF)?
bool IsMidi(const std::vector<uint8_t> &Bytes);

// The file as a song of rows; false with the reason when it isn't one or it
// has no notes
bool ParseMidi(const std::vector<uint8_t> &Bytes, TrackerSong &Out, std::string &Error);

// General MIDI's names: a program (0-127), and a drum's (keys 35-81 on
// channel 10; 0 for others)
const char *MidiProgramName(int Program);
const char *MidiDrumName(int Key);

}	// namespace nsf

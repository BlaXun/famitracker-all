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

// A converted track studied for how it's made, in words a composer can use:
// its tempo and form, the key and chords, what each channel does (its role,
// rhythm, intervals and register), how its sounds are shaped (attacks,
// decays and echoes in the volume, duty clicks, vibrato with its delay,
// speed and depth, pitch flicks, bends into and out of notes, arpeggio
// chords), the drums and their groove, and the tricks between channels (an
// echo, harmony in thirds, notes traded between the pulses). The style of a
// track, to write in it, rather than its notes; its sounds come with their
// macros, to be built again.

#include "Convert.h"
#include "Module.h"
#include "NsfFile.h"

#include <functional>
#include <string>
#include <vector>

namespace nsf {

// A sound of the track worth borrowing: an instrument and how it's used
struct StudySound {
	std::string Channel;		// "Pulse 1"
	std::string Role;			// "the lead", "bass", ...
	int Kind;					// inst_kind: INST_2A03, INST_VRC6, ...
	bool Sawtooth, Noise, Triangle;
	int Notes, Share;			// notes that use it, and the percent of the channel's
	int TypicalNote;			// a note it plays
	std::string Words;			// its shape in words
	Macro Macros[SEQ_TYPES];
	std::vector<int> FdsWave;	// an FDS sound's wave (64 samples, 0-63) and modulation table (32 steps, 0-7), as its
	std::vector<int> FdsMod;	// instrument has them; empty for the other chips' sounds
	int ModRate, ModDepth, ModDelay;	// and its modulator's speed, depth and delay
	std::vector<std::vector<int> > N163Waves;	// an N163 sound's waves (its instrument's wave size each, 0-15) and its
	Macro N163WaveSteps;		// wave sequence (which of them plays, frame by frame; empty for one wave); empty for the others
	StudySound() : Kind(INST_2A03), Sawtooth(false), Noise(false), Triangle(false), Notes(0), Share(0), TypicalNote(48), ModRate(0),
		ModDepth(0), ModDelay(0) {}
};

struct TrackStudy {
	std::string Name;
	double Bpm;
	int RowsPerBeat;
	int BeatsPerBar;					// 4, unless the music says (a MIDI file's meter)
	std::string Meter;					// "3/4", "6/8", when the music says
	double TicksPerRow;
	bool Tracker;						// a tracker module's song (Tracker.h): its tempo is Speed and Tempo
	int Speed, Tempo;
	int FirstBarRow;					// rows before the first bar line (a pickup)
	int IntroBars, LoopBars;			// the intro plays once; LoopBars 0 when it doesn't loop
	std::string Form;					// the lead's four-bar phrases: "A A B A'"
	std::string Key;					// "D minor", "" when unclear
	std::vector<std::string> Chords;	// a chord a bar, from the first bar line
	int LoopStartBar;					// the chord where the loop starts, -1 when it doesn't loop
	std::vector<std::string> Channels;	// a paragraph per channel that plays
	std::vector<std::string> Drums;		// the kit and the groove
	std::vector<std::string> Tricks;	// techniques found, most used first
	std::vector<StudySound> Sounds;		// by channel, most used first
	TrackStudy() : Bpm(0), RowsPerBeat(4), BeatsPerBar(4), TicksPerRow(6), Tracker(false), Speed(6), Tempo(125), FirstBarRow(0), IntroBars(0), LoopBars(0),
		LoopStartBar(-1) {}
};

// Studies track Track of a converted module, with what the conversion said about it
TrackStudy StudyTrack(const Module &M, int Track, const TrackReport &Report);

// The study as text, headed by the track's name; with the sounds' macros
// when Macros is true
std::string DescribeStudy(const TrackStudy &Study, bool Macros = false);

// A sound's shape in words: "25% after a 50% click; blooms from 9 to 15 in
// 3 frames and holds 13; ..." (Note: a note it plays, for vibrato in cents)
std::string DescribeSound(const Macro Macros[SEQ_TYPES], bool Vrc6, int Note, bool Noise, bool Triangle, bool Sawtooth);

// A macro as FamiTracker writes it: "9 11 13 15 | 4 2 0" (| before the
// release part, [ where the loop starts)
std::string MacroText(const Macro &M);

// Which of an NSF's tracks are music to study, rather than jingles and sound
// effects: from the playlist's lengths, or else played for a while
struct TrackSurvey {
	int Track;				// from 0
	std::string Title;
	double Seconds;			// its length, or how long it played before it looped or the survey stopped
	double LoopSeconds;		// the loop's length, 0 when it doesn't loop or isn't known
	std::string Kind;		// "music", "jingle", "sound effect" or "silent"
	TrackSurvey() : Track(0), Seconds(0), LoopSeconds(0) {}
};
// Progress is called before each track is played; returning false stops
std::vector<TrackSurvey> SurveyTracks(const NsfFile &File, double MaxSeconds = 45,
	const std::function<bool(int Done, int Total)> &Progress = std::function<bool(int, int)>());

// Tracks studied one by one, each converted on its own so that its
// instruments stay its own
struct StudyOptions {
	double MaxSeconds;		// the longest a track is played to find its loop
	std::function<bool(int Done, int Total, const std::string &Track)> Progress;
	StudyOptions() : MaxSeconds(600) {}
};

struct StudiedTrack {
	int Track;				// from 0
	TrackStudy Study;
	std::string Problem;	// why it couldn't be studied; empty when it was
	StudiedTrack() : Track(0) {}
};

// False when stopped (Error "stopped"); tracks that can't be studied say why in theirs
bool StudyTracks(const NsfFile &File, const std::vector<int> &Tracks, const StudyOptions &Options, std::vector<StudiedTrack> &Out,
	std::string &Error);

}	// namespace nsf

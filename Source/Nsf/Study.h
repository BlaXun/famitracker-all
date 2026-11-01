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

// A stretch of a track, for how its texture changes (feat-like-reference): the intro, then the loop's four-bar phrases
// as the form letters them (or four-bar blocks of a track without a form)
struct StudyBlock {
	int FirstBar, Bars;				// from the first bar line, from 0
	std::string Name;				// "intro", or the phrase's letter in the form ("A", "B", "A'"; "-" where the lead rests), or ""
	std::vector<std::string> Parts;	// what plays in it, of "lead", "bass", "chords", "second", "echo", "drums", "noise layer"
	int Top;						// the lead's highest note in it, -1 when it rests
	int Notes;						// the notes and hits every channel starts in it (how full it is)
	StudyBlock() : FirstBar(0), Bars(4), Top(-1), Notes(0) {}
};

// How a track is built beyond its sounds and its harmony, in the tools' terms, so a song can be made like it
// (feat-like-reference): how the lead's opening motif comes back, which parts play in each stretch (when the chord part
// rests), where the peak is, the styles add_chords_and_bass and add_drums would play it in, and a noise layer apart from
// the drums (an ambience). The melodies themselves aren't kept.
struct StudyPlan {
	bool Has;						// worked out (false for a study without a lead or bars)
	int MotifNotes;					// the lead's opening motif: its notes (3-6) that come back in their rhythm; 0 when none does
	int MotifReturns;				// how often it comes back after the opening
	int MotifShaped;				// of those, in its shape too (as written, or moved: the same moves)
	double RhythmReturn;			// the share of the lead's bars (after its first) whose rhythm an earlier bar had
	double ShapeReturn;				// ... whose rhythm and moves (the shape) an earlier bar had
	int LeadLow, LeadHigh;			// the lead's range
	std::vector<StudyBlock> Blocks;
	int PeakBar, PeakNote;			// the lead's highest note in the loop (or the track) and the bar it first plays in; -1 for none
	int Fullest;					// the block with the most parts (then the most notes), -1 for none
	std::string ChordChannel;		// the channel playing the chords, "" for none
	std::string ChordStyle;			// add_chords_and_bass's style it plays them in: "arpeggio", "stabs", "lush", "broken", "guide"; "" none
	std::string BassStyle;			// add_chords_and_bass's bass_style: "roots", "root_fifth", "half", "octaves", "walking", "arpeggio",
									// "driving", "pedal", "syncopated"; "" for no bass
	std::string Groove;				// add_drums's groove: "basic", "four_on_floor", "driving", "calm", "half_time", "waltz", "march"; "" none
	std::string Intensity;			// add_drums's: "light", "normal" or "busy"
	double DrumHits;				// drum hits a bar in the loop
	std::string Ambience;			// a noise layer apart from the drums, in words; "" for none
	std::string AmbienceKind;		// what it's like: "waves", "wind", "rain"; "" for none
	StudyPlan() : Has(false), MotifNotes(0), MotifReturns(0), MotifShaped(0), RhythmReturn(0), ShapeReturn(0), LeadLow(-1), LeadHigh(-1),
		PeakBar(-1), PeakNote(-1), Fullest(-1), DrumHits(0) {}
};

// The plan in words, a line each (the study's description has them): "The lead's motif: ...", "Texture by phrase: ...",
// "The peak: ...", "To arrange like it: ...", "A noise layer apart from the drums: ..."
std::vector<std::string> DescribePlan(const StudyPlan &P);

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
	std::string Kind;					// "music", or for a short track that ends, "sting" (under 12 seconds) or "sound" (an effect)
	double Seconds;						// how long it plays (to its end, or the intro and the loop once), 0 when not known
	double PlaylistSeconds;				// the length its playlist gave it, 0 for none
	double DrumLoopBars;				// the drums' own cycle when they don't follow the loop, in bars; 0 when they do
	std::string Form;					// the lead's four-bar phrases: "A A B A'"
	std::string Key;					// "D minor", "C (major and minor)", "no clear key (around E)", "" when unclear
	std::vector<std::string> Chords;	// a chord a bar, from the first bar line
	int LoopStartBar;					// the chord where the loop starts, -1 when it doesn't loop
	std::vector<std::string> Channels;	// a paragraph per channel that plays
	std::vector<std::string> Drums;		// the kit and the groove
	std::vector<std::string> Tricks;	// techniques found, most used first
	std::vector<StudySound> Sounds;		// by channel, most used first
	StudyPlan Plan;						// how it's built: motif, texture, peak, styles (feat-like-reference)
	TrackStudy() : Bpm(0), RowsPerBeat(4), BeatsPerBar(4), TicksPerRow(6), Tracker(false), Speed(6), Tempo(125), FirstBarRow(0), IntroBars(0), LoopBars(0),
		Kind("music"), Seconds(0), PlaylistSeconds(0), DrumLoopBars(0), LoopStartBar(-1) {}
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
// effects: from the playlist's lengths, or else played for a while. A track
// that falls silent for good well before its playlist's length is what it
// plays: a jingle (a sting) or a sound effect of that length.
struct TrackSurvey {
	int Track;				// from 0
	std::string Title;
	double Seconds;			// its length, or how long it played before it looped, fell silent or the survey stopped
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

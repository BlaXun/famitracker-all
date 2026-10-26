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

// Help > Exercises: short tasks for someone new to music, in groups: First
// steps (a minute or two each, one word at a time, for someone who has never
// made music: a note, its pitch, its length, the beat...), Next steps (a few
// minutes each, from a tune's last note to a small song) and Making it your
// own (a heading for exercises to come). FamiTracker opens each one's starter
// song with a note that gives the task (ExercisesDlg.cpp, Tutorial.cpp; the
// scripts and starters are in res/exercises), and its Check button runs the
// check_exercise tool (Tools.cpp), which reads the song as the tools do (the
// key, the chords the other parts play, the melody's phrases, the drums'
// kinds) into ExerciseFacts. CheckExercise then says what's right already and
// the one thing to change next, kindly, in plain words, with the glossary's
// terms marked as links ("[[home note]]", Glossary.h) and the bars that show
// it best. The same checks can teach through Claude or a local model.
// Plain C++14, ASCII, tested on Linux (tests/ExercisesTests.cpp and the
// starters in tests/ApiTests.cpp).

#include "Music.h"

#include <string>
#include <vector>

namespace ftapi {

class Json;

// What a check looks at in the song: the tune (the 2A03's Pulse 1, where the
// starters have it, else the melody the tools hear), the bass (the triangle),
// the drums (the noise channel) or the whole song
enum exercise_role { EXERCISE_MELODY, EXERCISE_BASS, EXERCISE_DRUMS, EXERCISE_SONG };

// The groups Help > Exercises shows the exercises in, in order
enum exercise_group { EXERCISES_FIRST_STEPS, EXERCISES_NEXT_STEPS, EXERCISES_YOUR_OWN, EXERCISE_GROUPS };

struct ExerciseGroupInfo {
	const char *Id;			// "first-steps"
	const char *Title;		// "First steps"
	const char *About;		// who it's for, a line
	bool Later;				// a heading for exercises still to come: none are in it yet
};

struct ExerciseCheckInfo {
	const char *Id;			// "home-note": what check_exercise and the scripts' "check:" lines take
	int Role;				// exercise_role
	const char *Learned;	// what passing it teaches, said when it passes (links marked)
};

struct ExerciseInfo {
	const char *Id;			// "home-note": the script's and the starter's name in res/exercises, and the setting kept
	const char *Title;		// "The home note"
	const char *About;		// what it asks, a line
	int Minutes;			// about how long it takes
	bool NewSong;			// starts from a new song (File > New) rather than a starter song of its own
	std::vector<std::string> Checks;	// its checks, in the order its steps ask for them; it's done when the last passes
	int Group;				// exercise_group
};

// The exercises, in order, group after group: from the first note to a small song
const std::vector<ExerciseInfo> &Exercises();
// The groups, in order (Making it your own last, a heading for later)
const std::vector<ExerciseGroupInfo> &ExerciseGroups();
const ExerciseInfo *FindExercise(const std::string &Id);
// The exercise after Id; null after the last (or for none)
const ExerciseInfo *ExerciseAfter(const std::string &Id);
// Every check, and one by its id (null for none)
const std::vector<ExerciseCheckInfo> &ExerciseChecks();
const ExerciseCheckInfo *FindExerciseCheck(const std::string &Id);
// The exercise a check belongs to (null for none)
const ExerciseInfo *ExerciseOfCheck(const std::string &Check);

// Kinds of drum hit, as the drum grid and the tools tell them
enum exercise_drum { DRUM_HIT_KICK, DRUM_HIT_SNARE, DRUM_HIT_HIHAT, DRUM_HIT_OPEN_HIHAT, DRUM_HIT_CRASH, DRUM_HIT_TOM, DRUM_HIT_OTHER };
// A hit's kind: its instrument's name says it ("Kick", "Snare", "Hi-hat", "Open hi-hat", a "crash" or "tom"), or on
// the noise channel its pitch does, as the drum grid places them (kick 4-#, snare A-#, hi-hat E-#, open hi-hat D-#:
// 0-6 a kick, 7-B a snare, C-F a hi-hat); DRUM_HIT_OTHER for a sample of no kind
int ExerciseDrumKind(const std::string &Instrument, int Pitch, bool Noise);
const char *ExerciseDrumName(int Kind);	// "kick", "snare", "hi-hat", "open hi-hat", "crash", "tom", "drum"

// The song as a check reads it
struct ExerciseNote {
	int Row, Rows;			// song rows: where it starts, and until the next note, its release or its halt
	int Heard;				// rows it's heard for: its sound fades away sooner on a pluck (at least 1)
	int Pitch;				// as written (C-3 is middle C; a noise pitch 0-15 on the noise channel)
	int Volume;				// 0-15
	std::string Instrument;	// its instrument's name, "" for none
	ExerciseNote() : Row(0), Rows(1), Heard(1), Pitch(0), Volume(15) {}
};

struct ExerciseChannel {
	std::string Name;		// as FamiTracker's pattern editor names it: "Pulse 1", "Triangle" ("VRC6 Pulse 1" where two share a name)
	int Kind;				// channel_kind
	std::vector<ExerciseNote> Notes;
	std::vector<int> Hits;	// the noise's and the DPCM's: each note's exercise_drum
	ExerciseChannel() : Kind(KIND_PULSE) {}
};

struct ExerciseChord {
	int Row, Rows;
	std::string Name;		// "Am", spelled as the key spells it
	int Root;				// pitch class, 0 = C
	std::vector<int> Notes;	// its pitch classes, the root's first
	ExerciseChord() : Row(0), Rows(0), Root(0) {}
	bool Has(int PitchClass) const;
};

struct ExerciseSection {
	std::string Name;
	int FirstBar, LastBar;	// from 1
	std::string Line;		// what the part is for (SongShapes.h), "" when its name says nothing of it
	ExerciseSection() : FirstBar(1), LastBar(1) {}
};

struct ExerciseFacts {
	int PatternLength, Frames, RowsPerBeat, RowsPerBar;	// as Timing takes them
	std::vector<ExerciseChannel> Channels;
	int Target;				// the channel the check is about (exercise_role), -1 when the song has none
	int Melody;				// the channel the tools hear the melody on, -1 for none
	bool HasKey;			// a key is chosen for the track, or its notes fit one...
	Key K;					// ...this one
	bool KeyChosen;
	bool HasFound;			// the notes fit a key, chosen or not...
	Key Found;				// ...this one
	std::vector<ExerciseChord> Chords;	// the chords the other parts play (the target left out), in time order
	int Spans, Breaths;		// the target's two-bar phrase spans where it plays, and those that pause (a rest or a held note)
	std::vector<int> RunOn;	// the first bars of the spans it runs through without a pause
	std::vector<ExerciseSection> Sections;
	std::string Shape;		// the song shape its sections are (SongShapes.h: "Lively stage"), "" for none
	bool Halts;				// the song stops at its end rather than going round again
	int EndBar;				// the bar it ends or goes round after
	ExerciseFacts();
	Timing Time() const { return Timing(PatternLength, Frames, RowsPerBeat, RowsPerBar); }
};

// What a check found
struct ExerciseVerdict {
	std::string Check;		// its id
	bool Passed;
	std::vector<std::string> Right;	// what's right already, a sentence each
	std::string Next;		// the one thing to change next ("" when it passed)
	std::string Learned;	// when it passed: what it taught
	int PlayFirst, PlayLast;	// the bars that show it best (from 1, both in), 0 for none
	std::vector<int> PlayChannels;	// heard alone first (from 0), then in the mix; none for the mix alone
	std::string NextExercise;	// when it passed the exercise's last check: the one after, "" for none
	ExerciseVerdict() : Passed(false), PlayFirst(0), PlayLast(0) {}
};

// Runs a check on the song's facts; false when there's no such check
bool CheckExercise(const std::string &Check, const ExerciseFacts &Facts, ExerciseVerdict &Out);

// The verdict as check_exercise's format json gives it to FamiTracker's note, and read back there; the texts keep
// their links marked
Json ExerciseVerdictJson(const ExerciseVerdict &Verdict);
bool ReadExerciseVerdict(const std::string &Text, ExerciseVerdict &Out, std::string &Error);
// The verdict in plain words, for Claude and local models (the links as their words)
std::string ExerciseVerdictText(const ExerciseVerdict &Verdict);

// Texts with the glossary's terms marked as links: "[[home note]]" links its words to the entry they name
// (FindGlossaryEntry), "[[the note C|home-note]]" its words to that entry. The plain text, and where each link is in
// it; a mark whose words name no entry is left as its words
struct ExerciseLink {
	size_t First, Length;	// in the plain text
	std::string Id;			// the glossary entry
};
std::string UnmarkLinks(const std::string &Marked, std::vector<ExerciseLink> *pLinks = 0);
// The marks' entries, "" for each that names none (for the tests: every link a text makes leads somewhere)
std::vector<std::string> MarkedLinks(const std::string &Marked);

}	// namespace ftapi

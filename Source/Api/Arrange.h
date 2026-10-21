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

// Parts for the arranging tools of the Claude Code connection, as notes to
// write: chords in one of several styles (the 0xy arpeggio held or struck on
// a rhythm, broken chords, a guide-tone line), bass lines (from plain roots
// to walking and octave basses), drums with grooves, variations and fills, a
// harmony voice and a counter-melody that moves where the melody rests.
// Pitches are FamiTracker's (C-3 is middle C; noise 0-15), rows count from
// the song's first row. Plain C++14, independent of MFC, so it is tested on
// Linux.

#include "Chords.h"
#include "Music.h"

#include <string>
#include <vector>

namespace ftapi {

// A style chosen by name, and what it sounds like
struct StyleInfo {
	const char *Name;
	const char *Description;
	const char *Aliases;		// other words for it, separated by "|"
};

enum chord_style { CHORDS_ARPEGGIO, CHORDS_STABS, CHORDS_BROKEN, CHORDS_GUIDE };
enum chord_rhythm { RHYTHM_BARS, RHYTHM_BEATS, RHYTHM_OFFBEATS, RHYTHM_EIGHTHS, RHYTHM_HALF, RHYTHM_TRESILLO, RHYTHM_CHARLESTON,
	RHYTHM_WALTZ };
enum broken_pattern { BROKEN_UP, BROKEN_DOWN, BROKEN_UPDOWN, BROKEN_ALBERTI, BROKEN_ROLLING };
enum bass_style { BASS_ROOT_FIFTH, BASS_ROOTS, BASS_HALF, BASS_OCTAVES, BASS_WALKING, BASS_SYNCOPATED, BASS_ARPEGGIO,
	BASS_DRIVING, BASS_PEDAL };
enum groove { GROOVE_BASIC, GROOVE_FOUR_ON_FLOOR, GROOVE_HALF_TIME, GROOVE_SHUFFLE, GROOVE_MARCH, GROOVE_BREAKBEAT,
	GROOVE_CALM, GROOVE_DRIVING, GROOVE_WALTZ };
enum harmony_interval { HARMONY_THIRD_BELOW, HARMONY_SIXTH_BELOW, HARMONY_THIRD_ABOVE, HARMONY_FIFTH_BELOW, HARMONY_OCTAVE_BELOW };

// In the order of the enums above
const std::vector<StyleInfo> &ChordStyles();
const std::vector<StyleInfo> &ChordRhythms();
const std::vector<StyleInfo> &BrokenPatterns();
const std::vector<StyleInfo> &BassStyles();
const std::vector<StyleInfo> &Grooves();
const std::vector<StyleInfo> &HarmonyIntervals();

// A style by its name or another word for it, ignoring case, spaces, dashes
// and underscores ("four on the floor"); -1 if none
int FindStyle(const std::vector<StyleInfo> &Styles, const std::string &Name);
std::vector<std::string> StyleNames(const std::vector<StyleInfo> &Styles);
std::string StyleList(const std::vector<StyleInfo> &Styles);		// "arpeggio (...); stabs (...)"

// Chords on one channel
struct ChordPartOptions {
	int Style;			// chord_style
	int Rhythm;			// chord_rhythm; -1 for the style's own (arpeggio: bars, stabs: offbeats)
	int Pattern;		// broken_pattern
	int StepRows;		// broken: rows a note; 0 for half a beat
	int RowsPerBeat;
	int Low, High;		// where each chord's lowest note goes
	int Volume;			// 1-15 on the strongest notes, a little softer on the others
	bool RootInBass;	// a bass line plays the roots, so voicings may leave them out
	ChordPartOptions();
};
std::vector<PartNote> ChordPart(const std::vector<ChordAt> &Chords, const std::vector<BarSpan> &Bars, const ChordPartOptions &Options);

// The 0xy arpeggio for a chord: three of its tones, the lowest (Base) within
// [Low, High] near Near, and X and Y semitones above it (X < Y <= 15), led
// smoothly from the one before when there is one
struct Voicing {
	int Base;
	int X, Y;
	Voicing() : Base(0), X(4), Y(7) {}
	int Param() const { return (X << 4) | Y; }
};
Voicing ArpeggioVoicing(const Chord &C, const Voicing *pPrevious, int Near, int Low, int High, bool RootInBass);

// A bass line
struct BassOptions {
	int Style;			// bass_style
	int RowsPerBeat;
	int Low, High;		// its register
	Key K;				// for the pedal, and walking between chords
	int Volume;			// on the first note; -1 to leave the column alone
	BassOptions();
};
std::vector<PartNote> BassPart(const std::vector<ChordAt> &Chords, const std::vector<BarSpan> &Bars, const BassOptions &Options);

// Drums on the noise channel
struct DrumHit {
	int Row;
	int Pitch;			// noise 0-15
	int Volume;
	std::string Sound;	// the preset that plays it: kick, snare, hi-hat, open hi-hat, crash or tom
};
struct DrumOptions {
	int Groove;			// groove
	int Intensity;		// 1 light, 2 as written, 3 busy
	int FillEvery;		// a fill ends every so many bars; 0 for none
	int PhraseStart;	// bars of the phrase before the first one, so fills land where phrases end
	bool Crash;			// a crash where the drums start and after each fill
	int RowsPerBeat;
	DrumOptions();
};
std::vector<DrumHit> DrumPart(const std::vector<BarSpan> &Bars, const DrumOptions &Options);

// A second voice that follows the melody note for note, a third (or another
// interval) away in the key, on the chords' notes where the melody holds
struct HarmonyOptions {
	int Interval;		// harmony_interval
	int MinRows;		// only melody notes this long; 0 for all of them
	int RowsPerBeat;
	Key K;
	int Low, High;		// moved an octave to stay in here
	int VolumeDrop;		// this much softer than the melody
	HarmonyOptions();
};
std::vector<PartNote> HarmonyPart(const std::vector<NoteInfo> &Melody, const std::vector<ChordAt> &Chords, const HarmonyOptions &Options);

// A counter-melody: the chords' guide tones (thirds and sevenths) as a line
// that holds while the melody moves and moves, by step, while it holds or
// rests; below the melody, or above it as a descant, never a semitone from it
struct CounterOptions {
	bool Above;
	int RowsPerBeat;
	Key K;
	int Low, High;
	int Volume;
	CounterOptions();
};
std::vector<PartNote> CounterMelody(const std::vector<NoteInfo> &Melody, const std::vector<ChordAt> &Chords,
	const std::vector<BarSpan> &Bars, const CounterOptions &Options);

// Notes in order inside [First, End), none overlapping, at least a row long,
// a volume only where it changes
void TidyPart(std::vector<PartNote> &Notes, int First, int End);

}	// namespace ftapi

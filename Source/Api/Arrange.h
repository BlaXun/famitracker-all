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
// a rhythm, broken chords, a guide-tone line, lush chords that instruments'
// arpeggios play), bass lines (from plain roots
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

enum chord_style { CHORDS_ARPEGGIO, CHORDS_STABS, CHORDS_BROKEN, CHORDS_GUIDE, CHORDS_LUSH, CHORDS_VOICES };
enum chord_rhythm { RHYTHM_BARS, RHYTHM_BEATS, RHYTHM_OFFBEATS, RHYTHM_EIGHTHS, RHYTHM_HALF, RHYTHM_TRESILLO, RHYTHM_CHARLESTON,
	RHYTHM_WALTZ };
enum broken_pattern { BROKEN_UP, BROKEN_DOWN, BROKEN_UPDOWN, BROKEN_ALBERTI, BROKEN_ROLLING };
enum bass_style { BASS_ROOT_FIFTH, BASS_ROOTS, BASS_HALF, BASS_OCTAVES, BASS_WALKING, BASS_SYNCOPATED, BASS_ARPEGGIO,
	BASS_DRIVING, BASS_PEDAL };
enum groove { GROOVE_BASIC, GROOVE_FOUR_ON_FLOOR, GROOVE_HALF_TIME, GROOVE_SHUFFLE, GROOVE_MARCH, GROOVE_BREAKBEAT,
	GROOVE_CALM, GROOVE_DRIVING, GROOVE_WALTZ, GROOVE_LIVELY };
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
	int Rhythm;			// chord_rhythm; -1 for the style's own (arpeggio and lush: bars, stabs: offbeats)
	int Pattern;		// broken_pattern: the order of broken chords' notes, and of lush chords' (LushShape)
	int StepRows;		// broken: rows a note; 0 for half a beat
	int RowsPerBeat;
	int Low, High;		// where each chord's lowest note goes
	int Volume;			// 1-15 on the strongest notes, a little softer on the others
	bool RootInBass;	// a bass line plays the roots, so voicings may leave them out
	// The room the chords keep, in the part's own pitches (as it writes them): each chord's notes stay
	// CHORD_ROOM under the lowest of the melody's notes sounding while it sounds and over the highest of the
	// bass's, as far as its shape fits; Ceiling and Floor are the limits where neither plays (-1: none).
	// Empty and -1, the chords go by Low and High alone.
	std::vector<NoteInfo> Above, Below;
	int Ceiling, Floor;
	// The bars the melody doesn't play in (an intro, a break): a chord there keeps no Ceiling, its notes in their register
	// (the melody's usual low notes put an intro's chords an octave under the same chords later)
	std::vector<BarSpan> Open;
	// Broken chords: no note under this (as the part writes it; -1 for none), whatever the room, an octave up
	// where one would be: a pulse's ripple low under the bass turns to mud
	int Lowest;
	ChordPartOptions();
};
// Where a pulse's broken chords stop going down, as they sound: C-2
const int PULSE_BROKEN_FLOOR = 24;

// How far under the melody's lowest note a chord's notes stay (a minor third; a whole step where a chord's shape fits no
// lower, never a semitone), and over the bass's highest
const int CHORD_ROOM = 3, BASS_ROOM = 2;
// Lush chords are held like the arpeggio's, each note at its chord's root,
// its Arpeggio the chord's LushShape: the tool makes the instruments.
std::vector<PartNote> ChordPart(const std::vector<ChordAt> &Chords, const std::vector<BarSpan> &Bars, const ChordPartOptions &Options);

// Chords as voices on channels of their own (chord_style voices), as FM and
// wavetable chips play them: each chord in Voices notes (2-4), its color
// first (the third and seventh, then a ninth or the fifth, then the root
// when no bass plays it), each led to the next chord's by the smallest
// moves without crossing, in [Low, High] and the room the melody and the
// bass leave it (Above, Below), no two voices a semitone or a minor ninth
// apart where another voicing has none, struck on the rhythm (held, once
// a bar and at chord changes, by default). A part per voice, lowest first.
std::vector<std::vector<PartNote> > VoicedChordParts(const std::vector<ChordAt> &Chords, const std::vector<BarSpan> &Bars,
	const ChordPartOptions &Options, int Voices);

// The notes of a chord a voicing of Voices uses, as semitones above its
// root, most telling first
std::vector<int> VoiceTones(const Chord &C, int Voices, bool RootInBass);

// A lush chord: its notes as semitones above the root, in the order an
// instrument's arpeggio plays them. Root position and close, with a ninth,
// eleventh or thirteenth an octave up where a third or a seventh is there to
// carry the chord (a suspended second or fourth stays where it is); a power
// chord gets its octave. Pattern (broken_pattern): up; down; updown (up and
// back, the ends once); alberti, leaping (every other note, then the ones
// between); rolling, spread (the root and fifth where they are, the others an
// octave up, and a third that would rub a semitone against a ninth left out).
std::vector<int> LushShape(const Chord &C, int Pattern);

// The 0xy arpeggio for a chord: three of its tones, the lowest (Base) within
// [Low, High] near Near, and X and Y semitones above it (X < Y <= 15), led
// smoothly from the one before when there is one; with a room, its top
// (Base + Y) at Ceiling or under and Base at Floor or over where they fit
struct Voicing {
	int Base;
	int X, Y;
	Voicing() : Base(0), X(4), Y(7) {}
	int Param() const { return (X << 4) | Y; }
};
Voicing ArpeggioVoicing(const Chord &C, const Voicing *pPrevious, int Near, int Low, int High, bool RootInBass, int Floor = -1000,
	int Ceiling = 1000);

// A bass line
struct BassOptions {
	int Style;			// bass_style
	int RowsPerBeat;
	int Low, High;		// its register
	Key K;				// for the pedal, and walking between chords
	int Volume;			// on the first note; -1 to leave the column alone
	// The melody's notes as they sound, in the bass's own pitches (as it writes them): a walking or syncopated bass's
	// passing notes keep off a semitone (and octaves) from one sounding with them, as check_song hears a clash, as they
	// keep off one under the chord's notes. Empty for none
	std::vector<NoteInfo> Above;
	BassOptions();
};
std::vector<PartNote> BassPart(const std::vector<ChordAt> &Chords, const std::vector<BarSpan> &Bars, const BassOptions &Options);

// Drums on the noise channel, or with their kick, snare and toms sampled on
// the DPCM channel
struct DrumHit {
	int Row;
	int Pitch;			// noise 0-15 (a tom's: high 9, middle 7, low 5)
	int Volume;
	std::string Sound;	// the preset that plays it: kick, snare, hi-hat, open hi-hat, crash or tom
	bool Sampled;		// a sample on the DPCM channel plays it (Samples)
};
struct DrumOptions {
	int Groove;			// groove
	int Intensity;		// 1 light (fewer kicks and snares, the hi-hats kept), 2 as written, 3 busy
	int FillEvery;		// a fill ends every so many bars; 0 for none
	int PhraseStart;	// bars of the phrase before the first one, so fills land where phrases end
	bool Crash;			// a crash where the drums start and after each fill
	bool Samples;		// kicks, snares (not ghost notes) and toms sampled: the noise keeps the hi-hats on those rows
	int RowsPerBeat;
	DrumOptions();
};
std::vector<DrumHit> DrumPart(const std::vector<BarSpan> &Bars, const DrumOptions &Options);
// The loudest hit a groove's part has as DrumPart makes it (most grooves 15; lively, soft by design, 10), which a
// part's loudest volume scales from
int GrooveLoudest(int Groove);

// A second voice that follows the melody note for note, a third (or another
// interval) away in the key, on the chords' notes where the melody holds.
// It follows where each melody note lands (a scoop, written a semitone low
// and sliding up, counts where it ends): the voice comes in on its own note,
// a slide later in the note goes to the voice's own note for the melody's
// new one, a fall as the note ends is copied, and 1xx/2xx stay the melody's.
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
// rests; below the melody, or above it as a descant, never a semitone from it.
// It moves at least every bar (a note held longer steps to a neighbour that
// fits), and where the melody rests it does as Rests says.
enum counter_rests {
	RESTS_FILL,		// it moves on through the melody's rests (a pickup into the next chord)
	RESTS_ANSWER,	// it breathes with the melody: silent in its rests, but for a short answer in rests of two beats or more
	RESTS_LEAVE,	// silent in every rest of the melody
};
struct CounterOptions {
	bool Above;
	int RowsPerBeat;
	Key K;
	int Low, High;
	int Volume;
	int Rests;			// counter_rests
	// A calm line: a note a bar (or held longer), never steps between the beats under a melody that holds; a calm tune of
	// long notes left no rest to answer in, and a line moving in quarter notes under every one of them crowded it
	bool Slow;
	CounterOptions();
};
// The melody's rests: where none of its notes sound for half a beat or more, inside [First, End)
std::vector<BarSpan> MelodyRests(const std::vector<NoteInfo> &Melody, int First, int End, int RowsPerBeat);
std::vector<PartNote> CounterMelody(const std::vector<NoteInfo> &Melody, const std::vector<ChordAt> &Chords,
	const std::vector<BarSpan> &Bars, const CounterOptions &Options, int *pAnswers = 0);	// pAnswers: how many answers (RESTS_ANSWER)

// Notes in order inside [First, End), none overlapping, at least a row long,
// a volume only where it changes
void TidyPart(std::vector<PartNote> &Notes, int First, int End);

}	// namespace ftapi

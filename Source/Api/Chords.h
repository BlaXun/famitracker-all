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

// Chords for the arranging tools of the Claude Code connection: a chord as
// its root and the notes above it, read from a name ("F#m7", "Bbmaj7",
// "Dsus4", "C/E") or a degree of the key ("vi", "V7", "bVII", "V7/V") and
// named back, progressions laid out on bars, colors (sevenths, added
// ninths) given to plain chords where the melody lets them, and the chords
// heard in a song's notes. Pitches are FamiTracker's (C-3 is middle C), rows
// count from the song's first row. Plain C++14, independent of MFC, so it is
// tested on Linux.

#include "Music.h"

#include <string>
#include <vector>

namespace ftapi {

struct Chord {
	int Root;					// pitch class, 0 = C
	std::vector<int> Tones;		// semitones above the root, 0-11, ascending and from 0: {0, 4, 7, 11} is a major seventh
	int Bass;					// pitch class in the bass: the root, or another one (C/E)
	int RootSpelling;			// as written: 1 with a sharp (C#), -1 with a flat (Db), 0 as the key spells it
	int BassSpelling;

	Chord();					// C major
	Chord(int Root, const std::vector<int> &Tones);
	bool Has(int Interval) const;			// a tone Interval (0-11) above the root
	bool HasPitchClass(int PitchClass) const;
	int Third() const;			// 3 or 4; the suspension (2 or 5) when there's no third; -1 for none
	int Fifth() const;			// 7, 6 or 8; -1 for none
	int Seventh() const;		// 10 or 11 (9 in a diminished seventh); -1 for none
	bool IsMinor() const;		// a minor third and no major one
	bool IsTriad() const;		// three notes: major, minor, diminished or augmented
	std::vector<int> PitchClasses() const;	// the root's first
	bool operator==(const Chord &Other) const;
	bool operator!=(const Chord &Other) const { return !(*this == Other); }
};

// A chord on the rows [Row, Row + Rows)
struct ChordAt {
	int Row;
	int Rows;
	Chord C;
	ChordAt() : Row(0), Rows(0) {}
};

// A bar on the rows [Row, Row + Rows)
struct BarSpan {
	int Row;
	int Rows;
	BarSpan() : Row(0), Rows(16) {}
	BarSpan(int Row, int Rows) : Row(Row), Rows(Rows) {}
};

// Chord names: "C", "Am", "F#m7", "Bbmaj7", "G7", "Dsus4", "Cadd9", "Em7b5",
// "Bdim", "Caug", "C6", "C9", "A7b9", "C5", "C/E"; or degrees of K in Roman
// numerals, upper case major and lower case minor: "I", "vi", "IV", "V7",
// "ii7", "bVII", "viio", "V7/V" (the dominant of the dominant). False if
// Text isn't one.
bool ParseChord(const std::string &Text, const Key &K, Chord &Out);
std::string ChordName(const Chord &C, const Key &K);	// "F#m7", sharps or flats as K spells them
std::string ChordDegree(const Chord &C, const Key &K);	// "vi7", "IVmaj7", "bVII", "viio"

// A progression as written, bar by bar: with "|" between bars ("| C | Am7 |
// F G |"), or one chord a bar without ("C Am F G", "I-vi-IV-V").
std::vector<std::string> SplitProgression(const std::string &Text);

// Whether any chord of a progression is written as a degree ("vi", "V7")
bool WrittenAsDegrees(const std::vector<std::string> &BarTexts);

// The chords of BarTexts on Bars, the written bars repeating from the first
// when there are fewer. The chords of a bar take equal shares of it, the
// first ones the longer when its beats don't divide; "." holds the chord
// before for another share ("C . . G" is C for three beats of four and G for
// one), "%" repeats the bar before and "N.C." is a share without a chord.
// False with the reason in Error.
bool LayOutProgression(const std::vector<std::string> &BarTexts, const std::vector<BarSpan> &Bars, int RowsPerBeat,
	const Key &K, std::vector<ChordAt> &Out, std::string &Error);

// "| C | Am7 | F G7 | C |": the chords bar by bar, as LayOutProgression reads them
std::string ProgressionText(const std::vector<ChordAt> &Chords, const std::vector<BarSpan> &Bars, const Key &K,
	bool Degrees = false, size_t MaxBars = 32);

// The chords of a key as it has them, by degree (0-6): a major key's are
// I ii iii IV V vi viio, a minor key's i iio III iv v VI VII
Chord DiatonicChord(const Key &K, int Degree, bool Seventh);

// Colors for plain chords
enum chord_color {
	COLOR_PLAIN,		// as they are
	COLOR_SEVENTHS,		// the key's sevenths: Imaj7, ii7, V7...
	COLOR_RICH,			// colors that keep each chord's part: add9, maj7 and 6 on the tonic and subdominant, m7 and m9 on minor
						// chords, add9 on the V (its own seventh where that fits better), a seventh only on dominants
};
bool ParseColor(const std::string &Text, chord_color &Out);	// "plain", "sevenths", "rich"

// Gives Chords the color: a tone the color adds that a melody note heard for
// a while rubs against a semitone away is left out, and the next color tried
// instead. Only triads change, each into one chord.
void ColorChords(std::vector<ChordAt> &Chords, const std::vector<NoteInfo> &Melody, const Key &K, int RowsPerBeat,
	chord_color Color);

// How much the melody rubs against a pitch class on [Row, End): in beats, a
// note a semitone away weighing its length, more on the chord's first row
// and less for quick passing notes
double MelodyRub(const std::vector<NoteInfo> &Melody, int Row, int End, int PitchClass, int RowsPerBeat);

// The chord at Row, 0 for none
const ChordAt *ChordAtRow(const std::vector<ChordAt> &Chords, int Row);

// Whether a chord has a note outside the key (a borrowed bVI, a secondary dominant's leading tone)
bool OutsideKey(const Chord &C, const Key &K);

// The scale a chord sounds in over a key: the key's seven notes with each of the chord's own in place of the key's note of
// its letter, spelled from the chord's root (the third two letters over it, the fifth four, a ninth one): over Ebadd9 in
// G major G A Bb C D Eb F (G aeolian's), over F in G major G mixolydian's, over E major in A minor A harmonic minor's.
// The pitch classes by degree from the key's tonic; the key's own scale for a chord that can't be spelled so (two of its
// notes on one letter).
std::vector<int> ChordScale(const Key &K, const Chord &C);
bool InChordScale(int Pitch, const std::vector<int> &Scale);
// The next pitch of such a scale above (Direction 1) or below (-1) Pitch
int ChordScaleStep(int Pitch, int Direction, const std::vector<int> &Scale);

// The chords heard in some parts' notes: each slot (a bar, or with HalfBars
// each half of a bar of two beats or more) gets the chord whose tones explain
// most of what sounds there, a seventh or a suspension only when all of its
// tones are heard, the bass's note counting towards the root. Slots with too
// little to go on get none. Neighbouring slots with the same chord come back
// as one.
struct HeardNote {
	int Row;
	int Rows;
	int PitchClass;
	double Weight;		// 1 for a full-volume note
	bool Bass;			// in a bass part: its note is the chord's bass
};
std::vector<ChordAt> HearChords(const std::vector<HeardNote> &Notes, const std::vector<BarSpan> &Bars, int RowsPerBeat,
	const Key &K, bool HalfBars);

}	// namespace ftapi

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

// What a chord does in a key, in plain words for someone new to music.
// FamiTracker's piano roll colors its chord strip by it (PianoRoll.cpp), and
// the glossary and the tools can use the same words:
// - home: the key's own chord, where the music is at rest, and the chords that
//   share most of its notes (in C major C, then Am and Em; in A minor Am, then C)
// - away: the chords that move off from home (F and Dm in C major; Dm, F and
//   Bdim in A minor)
// - tension: the chords that lean back toward home (G, G7 and Bdim in C major;
//   E, E7 and G in A minor)
// - color: chords from outside the key, borrowed from the major or minor key
//   on the same home note (Fm or Bb in C major, A or D in A minor), a chord's
//   own tension on the way to it (D7 before G in C major), or a surprise
// Each mode the BF KEYS block keeps (Music.h's: major, dorian, phrygian,
// lydian, mixolydian, minor, locrian and harmonic minor) has chords of its
// own; minor and harmonic minor share theirs (both sevenths). A slash chord
// (C/E) does what its chord does, but a chord moving off from home over the
// tension's bass note (F/G, Dm7/G in C major) is a soft tension. Plain C++14,
// tested on Linux (tests/ChordRolesTests.cpp).

#include <string>
#include <vector>
#include "Chords.h"

namespace ftapi {

enum chord_role_t {
	CHORD_ROLE_HOME,
	CHORD_ROLE_AWAY,
	CHORD_ROLE_TENSION,
	CHORD_ROLE_COLOR,
	CHORD_ROLE_COUNT
};

// "home", "away", "tension", "color"
const char *ChordRoleName(chord_role_t Role);
// What each one does, for a legend: "settled, at rest", "moving off", "leans toward home", "from outside the key"
const char *ChordRoleMeaning(chord_role_t Role);
// The same in a sentence or two, for the glossary and the tools: "Home is the key's own chord..."
const char *ChordRoleExplained(chord_role_t Role);

struct ChordRoleInKey {
	chord_role_t Role;
	std::string Label;	// "home", "back home", "near home", "away", "tension", "borrowed from minor", "leads to G", "color"
	std::string Feel;	// a feel word or two: "settled, at rest", "leans toward home", "bittersweet"
	int Target;			// a chord leading to another (D7 to G): that one's root as a pitch class; -1 for the others
	ChordRoleInKey() : Role(CHORD_ROLE_HOME), Target(-1) {}
	std::string Words() const { return Label + ": " + Feel; }	// "tension: leans toward home"
};

// What C does in K. Before and After are the chords heard right before and
// after it (null: none, or not known). A home chord after tension is "back
// home", and a chord that could be borrowed or lead on to another (D in A
// minor: borrowed from A major, or leading to G) leads on when After is that
// chord.
ChordRoleInKey RoleOfChord(const Chord &C, const Key &K, const Chord *pBefore = 0, const Chord *pAfter = 0);

// The roles of chords in time order (HearChords', LayOutProgression's), each
// with the chords right before and after it where they touch
std::vector<ChordRoleInKey> RolesOfChords(const std::vector<ChordAt> &Chords, const Key &K);

// Strong notes on the beat: of chords in time order that don't overlap
// (anything with Row and Rows: ChordAt, ToolSet::Harmony's HeardChord), the
// one sounding at Row, by its index; -1 where none sounds
template <class Span>
int ChordIndexAtRow(const std::vector<Span> &Chords, int Row)
{
	size_t Low = 0, High = Chords.size();	// the first chord starting after Row
	while (Low < High) {
		const size_t Middle = (Low + High) / 2;
		if (Chords[Middle].Row <= Row)
			Low = Middle + 1;
		else
			High = Middle;
	}
	if (Low == 0)
		return -1;
	const Span &Found = Chords[Low - 1];
	return Row < Found.Row + Found.Rows ? static_cast<int>(Low - 1) : -1;
}

// How Pitch (any octave) stands under a chord of these pitch classes, the
// root's first, with this bass: 2 its root, 1 another of its notes or the
// bass, 0 neither (the root of an empty list is none)
int StrongNoteAt(const std::vector<int> &PitchClasses, int Bass, int Pitch);

}	// namespace ftapi

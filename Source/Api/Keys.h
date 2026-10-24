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

// The key a track is in, as the user chooses it in FamiTracker's piano roll
// (or Claude with set_key): the notes the piano roll shades as the key's and
// snaps drawn and moved notes to, and the key the tools use when they aren't
// given one. A track without a chosen key is on "auto": the piano roll and
// the tools find its key from its notes (FindKey in Tools.cpp). Each track
// has its own.
//
// FamiTracker keeps the choices in the module's "BF KEYS" block, which other
// FamiTracker versions skip; a module where no track has a chosen key has no
// such block. Plain C++14, shared by FamiTracker and the tools.

#include <cstddef>
#include <string>
#include <vector>
#include "Music.h"

namespace ftapi {

struct KeyChoice {
	Key K;				// tonic and mode (Music.h)
	bool Pentatonic;	// only the five notes of a major key (1 2 3 5 6) or a minor one (1 b3 4 5 b7), for the
						// piano roll's shading and snapping; the tools use the key itself

	KeyChoice() : Pentatonic(false) {}
	explicit KeyChoice(const Key &Which, bool Five = false) : K(Which), Pentatonic(Five) {}
};

inline bool operator==(const KeyChoice &a, const KeyChoice &b)
{
	return a.K.Tonic == b.K.Tonic && a.K.Mode == b.K.Mode && a.Pentatonic == b.Pentatonic;
}

inline bool operator!=(const KeyChoice &a, const KeyChoice &b)
{
	return !(a == b);
}

bool CanBePentatonic(const Key &K);				// major and minor keys
std::string KeyChoiceName(const KeyChoice &C);	// "C major", "A minor pentatonic", "F# dorian"
// ParseKey's keys ("C major", "F# dorian", "Bbm"), and a major or minor one
// with "pentatonic" after it ("A minor pentatonic", "C pentatonic", "Am pentatonic")
bool ParseKeyChoice(const std::string &Text, KeyChoice &Out);

// The pitches the piano roll shades as the key's and snaps to: the key's as
// the tools count them (InKey: a minor key's raised seventh too), or the
// pentatonic's five
bool KeyHasPitch(const KeyChoice &C, int Pitch);
bool IsTonic(const KeyChoice &C, int Pitch);
// The key's pitch nearest Pitch, a fraction (where the mouse is in a lane: a
// lane's middle is its pitch), among Low..High; of two as near, the lower.
// Pitch rounded, kept within Low..High, when none of the key's is there.
int SnapToKey(const KeyChoice &C, double Pitch, int Low, int High);
// Steps of the key up (or down, Steps < 0) from Pitch: each step to the
// key's next pitch that way, so a pitch outside the key lands on it with the
// first step
int StepInKey(const KeyChoice &C, int Pitch, int Steps);
// The steps of the key from From to To: its pitches above From up to To, To
// counted (negative going down)
int KeyStepsBetween(const KeyChoice &C, int From, int To);

// Every track's chosen key, as the module keeps them
class KeyStore {
public:
	bool Empty() const;
	void Clear();
	bool Get(int Track, KeyChoice &Out) const;	// false for a track on auto (Out stays as it was)
	void Set(int Track, const KeyChoice &Choice);
	void SetAuto(int Track);
	void TrackRemoved(int Track);				// the tracks after it move up
	void TracksSwapped(int Track1, int Track2);
	void FitTracks(int Tracks);					// keys of tracks the module doesn't have go (after loading)

	// The "BF KEYS" block: little-endian 32-bit numbers. The count of tracks
	// with a chosen key, then for each, in order of its number: its number
	// from 0, its tonic (0 = C ... 11 = B), its mode (Music.h's modes in
	// their order: 0 major, 1 dorian, 2 phrygian, 3 lydian, 4 mixolydian,
	// 5 minor, 6 locrian, 7 harmonic minor) and its flags (1: pentatonic, for
	// a major or minor key only).
	static const char *const BLOCK_ID;
	static const int BLOCK_VERSION = 1;
	std::vector<unsigned char> Write() const;
	// From a block's data: false when it's damaged or longer than its tracks
	// (and then every track is on auto). A newer block version is left out.
	bool Read(const unsigned char *pData, size_t Size, int Version);

private:
	struct Stored {
		bool Chosen;
		KeyChoice Choice;
		Stored() : Chosen(false) {}
	};
	std::vector<Stored> m_Tracks;
};

}	// namespace ftapi

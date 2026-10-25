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

// FamiTracker's own sound library: ready-made sounds of distinct characters
// for every chip's channels and every kind (leads, basses, pads, plucks,
// bells, keys, drums, effects), beside the presets of Presets.h. The 2A03's
// pulses (which the MMC5's play too), the triangle, the noise, the VRC6's
// pulses and sawtooth and the 5B have presets here, written as designs
// (Presets.h's SoundDesign, made by DesignPreset) or as sequences; the VRC7
// has custom patches (Vrc7Custom); the DPCM channel more kits of samples
// (DpcmKit.h's DrumKits); the N163's and the FDS's are in Wavetables.h
// (LibraryWaveSounds) and FdsSounds.h (LibraryFdsSounds). They're built on
// what the studies of the classic soundtracks found: blooms, duty clicks,
// stepped decays, held and struck shapes, echoes in the envelope, vibratos
// that wait, staccato basses, thumps and chirps, and the calm shapes. None
// copies a game's sound. The sound browser lists them (SoundIndex.h), and the
// tools find each by its name as they find the presets (FindPreset,
// Find5BSound and FindVrc7Custom look here after their own). The tests check
// that every one is heard for more than a few frames (unless its words say
// it's short), that no 2A03 pulse sound's vibrato clicks, and that no two of a
// channel sound alike. Plain C++14, tested on Linux.

#include <string>
#include <vector>
#include "Presets.h"

namespace ftapi {

// Where a library preset is made to play
enum library_home {
	HOME_PULSE,			// the 2A03's pulses (and the MMC5's); listed on the triangle and the VRC6's channels too, as the presets are
	HOME_TRIANGLE,		// the triangle alone
	HOME_NOISE,			// the noise alone (a drum or a sound effect)
	HOME_VRC6_PULSE,	// the VRC6's pulses alone (their eight widths); Sequences2A03 has the 2A03's nearest
	HOME_SAWTOOTH,		// the VRC6's sawtooth alone (no duty sequence)
	HOME_5B				// the 5B alone (Sequences5B)
};

struct LibraryPreset {
	Preset Sound;			// its name and description, and the sequences of the chips it plays on
	std::string Kind;		// a SoundKinds() id (SoundIndex.h): "lead", "bass", ...
	std::string Words;		// more words it's found by
	int Home;				// library_home
	bool Short;				// meant to be short (a blip, a stab): its notes are gone within a few frames, and its words say so
	LibraryPreset() : Home(HOME_PULSE), Short(false) {}
};
const std::vector<LibraryPreset> &LibraryPresets();
// By its name (as FindPreset compares names: letters and digits, in any case), of a home or any (-1); 0 for none
const LibraryPreset *FindLibraryPreset(const std::string &Name, int Home = -1);

// The VRC7's custom patches of the library (the chip holds one custom patch at a time), each with its kind
struct LibraryVrc7 {
	Vrc7Custom Sound;
	std::string Kind;
	std::string Words;
};
const std::vector<LibraryVrc7> &LibraryVrc7Sounds();
const LibraryVrc7 *FindLibraryVrc7(const std::string &Name);

// What a library design couldn't make of its sequences, one line each (none when every sound is whole): the tests
// check it stays empty
const std::vector<std::string> &LibraryProblems();

}	// namespace ftapi

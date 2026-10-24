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

// VRC6 Plus, this branch's own chip: the real VRC6 (its two pulses and its
// sawtooth) with a soft layer of four wave channels played by the N163, for
// pads, a choir, a soft echo or a sub bass under the VRC6. It only plays soft
// waves through a real chip's emulation, so it stays NES-authentic, and it's
// shown as one chip: Module > Module properties has a box for it, and its
// channels are named "Pulse 1", "Pulse 2" and "Sawtooth" of the VRC6 Plus,
// then "Soft 1" to "Soft 4". Underneath, the module's chips are the VRC6 and
// the N163 with 4 channels, so playback, export and every tool work as they
// do for those; a small block of its own (BF VRC6PLUS) says it's VRC6 Plus,
// which a build without it skips: it opens the song as VRC6 + N163.
// Plain C++14, tested on Linux.

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace ftapi {

struct InstrumentInfo;
struct WaveSound;

const char *const VRC6_PLUS = "VRC6 Plus";
const int VRC6_PLUS_SOFT_CHANNELS = 4;		// the N163's channels it has

// The name of the soft layer's channel Index (0-3): "Soft 1"
std::string SoftChannelName(int Index);
// Its name as an N163 channel ("Namco 1"), which the tools still take
std::string SoftChannelOldName(int Index);

// The block that marks a module as VRC6 Plus, saved only when it is (a song
// without it saves byte for byte as before). Version 1 holds one int32: the
// soft layer's channels, 4.
class Vrc6PlusMark {
public:
	static const char *const BLOCK_ID;		// "BF VRC6PLUS"
	static const int BLOCK_VERSION = 1;

	static std::vector<unsigned char> Write();
	// Whether the block says VRC6 Plus: read checked against its size (exactly
	// its int32), its version and its value; a damaged block or another
	// version says no, and the song opens as VRC6 + N163
	static bool Read(const unsigned char *pData, size_t Size, int Version);
};

// The soft layer's starter sounds (Wavetables.h's wave sounds, each with a
// wave of its own): a choir pad, a string pad, a soft flute and a round sub
// bass, added when a song becomes VRC6 Plus
const std::vector<std::string> &SoftLayerSounds();
bool IsSoftLayerSound(const std::string &Name);

// Its calm shapes, wave sounds made for it that a song adds by name (add_instrument on a soft channel): a breath that
// sinks and holds, a quick pad, a pulsing pad and a music box. Not added with the starters: a song holds 8 N163 sounds.
const std::vector<std::string> &SoftLayerCalmSounds();
bool IsSoftLayerCalmSound(const std::string &Name);

// Those the song's instruments (Song.h) don't have yet as N163 instruments of
// their name, each with the place of its wave: under sample 128 (where
// FamiTracker 0.4.6 loads N163 waves), where no other N163 instrument's wave
// is (FindWavePlace), in the order of SoftLayerSounds. A sound that finds no
// place is left out.
std::vector<std::pair<const WaveSound *, int> > MissingSoftSounds(const std::vector<InstrumentInfo> &Instruments);

}	// namespace ftapi

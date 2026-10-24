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

// The mixer window's settings (View > Mixer, or Claude's set_mixer): each
// channel's level, a fader in dB (0 as the channel plays), and the pan it
// starts at, before any 8xx (the middle unless moved). FamiTracker's mixer
// (CMixer) plays them in playback, File > Create WAV and the game's OGG
// files; they aren't edits to the notes, and NSF files don't have them
// ("Write into the notes" puts them there). They belong to the module's
// channels, the same in every track, and are kept by channel ID
// (FamiTracker's chan_id_t, whose order is fixed), so switching expansion
// chips keeps the settings of the channels that stay.

#include <cstddef>
#include <vector>

namespace ftapi {

const int MIX_CHANNELS = 29;			// FamiTracker's chan_id_t count (CHANNELS)
const int MIX_LEVEL_LOWEST = -240;		// tenths of a dB: -24 dB
const int MIX_LEVEL_HIGHEST = 60;		// +6 dB
const int MIX_PAN_CENTER = 0x80;		// 0x00 left, 0xFF right, as 8xx

class MixStore {
public:
	MixStore();

	bool Empty() const;						// every channel at 0 dB and in the middle
	void Clear();
	int Level(int ChanId) const;			// tenths of a dB
	int Pan(int ChanId) const;
	// Kept within the fader's range and 0-255; false when nothing changed
	bool Set(int ChanId, int Level, int Pan);
	// The channels not in the list (the module's) go back to 0 dB and the middle
	void KeepOnly(const std::vector<int> &ChanIds);

	bool operator==(const MixStore &Other) const;
	bool operator!=(const MixStore &Other) const { return !(*this == Other); }

	// What a level (tenths of a dB) multiplies the channel's sound by
	static double Gain(int Level);

	// The "BF MIXER" block: little-endian 32-bit numbers. The count of
	// channels off their default, then for each, in order of its channel
	// ID: the ID (chan_id_t), its level in tenths of a dB (-240 to 60) and
	// its starting pan (0-255, 0x80 the middle). Written only when a
	// channel is off its default.
	static const char *const BLOCK_ID;
	static const int BLOCK_VERSION = 1;
	std::vector<unsigned char> Write() const;
	// From a block's data: false when it's damaged (and then every channel is
	// at its default). Another block version is left out the same way.
	bool Read(const unsigned char *pData, size_t Size, int Version);

private:
	int m_Level[MIX_CHANNELS];
	int m_Pan[MIX_CHANNELS];
};

}	// namespace ftapi

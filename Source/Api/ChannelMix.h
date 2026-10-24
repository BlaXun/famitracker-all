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
// starts at, before any 8xx (the middle unless moved); its depth, Soft (a
// gentle low-pass) and Space (a send to the space all channels share, a
// reverb in a room with an echo), both off unless set; and that space's
// room, echo (a time in beats of the track's tempo, or none) and repeats,
// one set for the song (APU/Space.h plays them). FamiTracker's mixer
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
const int MIX_DEPTH_MOST = 100;			// Soft and Space: 0 (off) to 100

// The space's rooms, by number as CSpace (APU/Space.h) plays them and the
// block saves them: append only
const int SPACE_ROOMS = 5;
extern const char *const SPACE_ROOM_NAMES[SPACE_ROOMS];	// "small room", "hall", "cathedral", "cave", "none" (the echo alone)
const int SPACE_ROOM_DEFAULT = 1;		// a hall
const int SPACE_ROOM_NONE = 4;
const int ECHO_STEPS = 48;				// the echo's time in 48ths of a beat (0: no echo)
const int ECHO_LONGEST = 96;			// two beats
const int ECHO_REPEATS_DEFAULT = 3;
const int ECHO_REPEATS_MOST = 8;

class MixStore {
public:
	MixStore();

	bool Empty() const;						// every channel at 0 dB in the middle without Soft or Space, the space as it starts
	bool FadersDefault() const;				// every channel at 0 dB and in the middle (Soft, Space and the space aside)
	void Clear();
	int Level(int ChanId) const;			// tenths of a dB
	int Pan(int ChanId) const;
	int Soft(int ChanId) const;				// 0-100
	int Space(int ChanId) const;			// 0-100
	// Kept within the fader's range and 0-255; false when nothing changed
	bool Set(int ChanId, int Level, int Pan);
	// Soft and Space, kept within 0-100; false when nothing changed
	bool SetDepth(int ChanId, int Soft, int Space);
	// The space: its room (SPACE_ROOM_NAMES), the echo in 48ths of a beat (0 none, up to two beats) and its
	// repeats (1-8), kept in range; false when nothing changed
	int Room() const { return m_Room; }
	int Echo() const { return m_Echo; }
	int Repeats() const { return m_Repeats; }
	bool SetSpace(int Room, int Echo, int Repeats);
	// A channel sends to the space, and the space sounds (a room, or an echo)
	bool SpaceHeard() const;
	// The channels not in the list (the module's) go back to 0 dB, the middle and no Soft or Space
	void KeepOnly(const std::vector<int> &ChanIds);

	bool operator==(const MixStore &Other) const;
	bool operator!=(const MixStore &Other) const { return !(*this == Other); }

	// What a level (tenths of a dB) multiplies the channel's sound by
	static double Gain(int Level);

	// The "BF MIXER" block: little-endian 32-bit numbers. The count of
	// channels off their default, then for each, in order of its channel
	// ID: the ID (chan_id_t), its level in tenths of a dB (-240 to 60) and
	// its starting pan (0-255, 0x80 the middle). Written only when a
	// channel is off its default. Version 2, written only when Soft, Space
	// or the space is off its default (so a song with faders alone saves
	// as before): each channel also has its Soft and Space (0-100), and
	// after the channels come the room (0-4), the echo (0-96) and the
	// repeats (1-8).
	static const char *const BLOCK_ID;
	static const int BLOCK_VERSION = 2;		// the newest
	int WriteVersion() const;				// 1 unless the depth is used
	std::vector<unsigned char> Write() const;
	// From a block's data: false when it's damaged (and then every channel is
	// at its default). Another block version is left out the same way.
	bool Read(const unsigned char *pData, size_t Size, int Version);

private:
	bool ChannelDefault(int ChanId) const;

	int m_Level[MIX_CHANNELS];
	int m_Pan[MIX_CHANNELS];
	int m_Soft[MIX_CHANNELS];
	int m_Space[MIX_CHANNELS];
	int m_Room, m_Echo, m_Repeats;
};

}	// namespace ftapi

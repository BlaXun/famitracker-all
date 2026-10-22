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

// Sounds for the N163's wavetable channels: each plays waves drawn here (16
// samples of 0-15), one after the other as its wave sequence says, so a
// timbre can open up, mellow as it fades, shimmer or sweep, which the 2A03's
// and the VRC6's fixed shapes can't.
//
// FamiTracker 0.4.6 plays N163 notes in tune with 16-sample waves: its note
// table (CSoundGen::LoadMachineSettings) leaves the wave's length out, so a
// 32-sample wave sounds an octave lower than written. All the waves here are
// 16 samples.
//
// The chip's 128 bytes of memory hold every wave that plays and, at the
// top, 8 bytes of registers for each channel in use. Instruments that sound
// at once need waves at places of their own, so each new one goes where no
// other N163 instrument's wave is (FindWavePlace), from the bottom up, which
// stays clear when channels are added later.

#include <string>
#include <utility>
#include <vector>
#include "Presets.h"

namespace ftapi {

const int N163_WAVE_SIZE = 16;		// samples a wave (notes in tune)
const int N163_LOADABLE = 128;		// FamiTracker 0.4.6 loads an N163 wave only from a place under this sample
const int N163_MAX_WAVES = 16;		// waves an instrument has, at most

struct WaveSound {
	std::string Name;			// "glass bell"
	std::string Words;			// other words it's asked for by, with commas
	std::string Description;
	std::vector<std::vector<int> > Waves;	// N163_WAVE_SIZE samples each, 0-15
	std::vector<PresetSequence> Sequences;	// volume, arpeggio, and the wave that plays (SEQUENCE_DUTY)

	bool Releases() const;			// fades out on a release (===)
	std::string InstrumentName() const;	// "Glass bell"
};

const std::vector<WaveSound> &WaveSounds();
const WaveSound *FindWaveSound(const std::string &Name);	// by name or one of its words
std::string WaveSoundList();							// "soft flute, drawbar organ, ..."

// The samples of wave memory with Channels N163 channels in use (16 go to
// each channel's registers)
int N163WaveMemory(int Channels);
// Where a wave of Size samples goes in Memory without touching the waves in
// Used (place, size): the lowest place, a multiple of Size; -1 when full
int FindWavePlace(int Size, int Memory, const std::vector<std::pair<int, int> > &Used);

}	// namespace ftapi

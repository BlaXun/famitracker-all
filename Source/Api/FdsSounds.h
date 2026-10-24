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

// Sounds for the Famicom Disk System's wavetable channel, as CInstrumentFDS
// keeps them: a wave of 64 samples (0-63), the chip's modulator (a table of
// 32 steps that bends the pitch, its speed, depth and the frames it waits),
// and a volume (0-31), arpeggio and pitch sequence.
//
// FamiTracker 0.4.6 plays the FDS two octaves under the note written: with
// TRANSPOSE_FDS its note table (CSoundGen::LoadMachineSettings) gives a wave
// of one cycle a quarter of the note's frequency, so A-5 sounds 440 Hz, as a
// pulse's A-3 does, and A-3 110 Hz. The waves here have one cycle, as the
// instrument editor's own and every FDS module made with 0.4.x do, and the
// tools write FDS notes two octaves up. The register tops out at 4095, so
// G#7 is the highest note in tune.
//
// The modulator as 0.4.6 plays it (APU/FDSSound.cpp) bends the pitch by
// 64ths, rounding a bend away from the note: the smallest it makes is about
// 55 cents down or 79 up, whatever the depth. So these sounds' gentle
// vibratos are pitch sequences, and the modulator makes what it does well:
// wide wobbles that stay in tune on average, and at audio rates a growl or a
// metallic shimmer.

#include <string>
#include <vector>
#include "Presets.h"

namespace ftapi {

const int FDS_WAVE_SIZE = 64;		// samples a wave, 0-63
const int FDS_MOD_SIZE = 32;		// steps of the modulation table, 0-7
const int FDS_VOLUME_MAX = 31;		// the highest value of an FDS volume sequence
const int FDS_OCTAVES_DOWN = 2;		// FamiTracker 0.4.6 plays FDS notes two octaves under the note written

struct FdsSound {
	std::string Name;			// "soft flute"
	std::string Words;			// other words it's asked for by, with commas
	std::string Description;
	std::vector<int> Wave;		// FDS_WAVE_SIZE samples, 0-63
	std::vector<int> Modulation;	// FDS_MOD_SIZE steps, 0-7: the bend changes by 0, +1, +2, +4, back to none, -4, -2, -1
	int ModSpeed;				// 0-4095: the table goes round 0.427 times a second per step of it
	int ModDepth;				// 0-63
	int ModDelay;				// frames before the modulator starts, 0-255
	std::vector<PresetSequence> Sequences;	// volume (0-31), arpeggio and pitch
	bool Low;					// a bass, heard low

	FdsSound() : ModSpeed(0), ModDepth(0), ModDelay(0), Low(false) {}
	bool Releases() const;			// fades out on a release (===)
	std::string InstrumentName() const;	// "Soft flute"
};

const std::vector<FdsSound> &FdsSounds();
const FdsSound *FindFdsSound(const std::string &Name);	// by name or one of its words
std::string FdsSoundList();							// "soft flute, whistle, ..."

// A sound described in words for the FDS channel (design_instrument there):
// Design's volume envelope (the 2A03's 0-15 counted 0-31, its first frame at
// most half its peak), its attack's pitch, arpeggio and vibrato as the FDS's
// sequences play them (its pitch sequence's units turned around and four
// times finer), the wave named by Wave (FdsWaveWords; empty: the one Kind
// starts from) and the modulator by Modulation (FdsModulationWords), which
// waits ModDelay frames. False with Why for a word it doesn't know.
bool DesignFdsSound(const SoundDesign &Design, const std::string &Kind, const std::string &Wave, const std::string &Modulation,
	int ModDelay, int FrameRate, FdsSound &Out, std::string &Why);
std::string FdsWaveWords();			// "sine, round, hollow, ..."
std::string FdsModulationWords();	// "none, wobble, tremble, growl, rumble"
bool IsFdsWave(const std::string &Word);

// An FDS volume (0-31, or the 32 the chip holds) as the tools count an
// instrument's envelope (0-15): nearest, and at least 1 while it sounds
int FdsVolumeAs15(int Volume);
// The modulator wobbles a note's pitch slowly enough to hear as a vibrato
// (under about 16 times a second), rather than colouring its timbre
bool FdsModulatorWobbles(int Speed, int Depth, const std::vector<int> &Modulation);

}	// namespace ftapi

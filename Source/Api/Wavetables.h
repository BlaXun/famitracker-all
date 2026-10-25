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
	std::string Kind;			// a SoundKinds() id (SoundIndex.h) where the sound says so (the library's); empty: from its words

	bool Releases() const;			// fades out on a release (===)
	std::string InstrumentName() const;	// "Glass bell"
};

const std::vector<WaveSound> &WaveSounds();
// FamiTracker's own sound library for the N163 (SoundLibrary.h): more wave sounds of distinct characters, each with its
// kind, which FindWaveSound finds by their names after the sounds above and their words
const std::vector<WaveSound> &LibraryWaveSounds();
const WaveSound *FindWaveSound(const std::string &Name);	// by name or one of its words
std::string WaveSoundList();							// "soft flute, drawbar organ, ..."

// How loud a wave sound's waves are against a square wave of 0-15, in dB (their RMS around their mean; a square's is
// 7.5): those it plays while a note holds (the ones its wave sequence loops through, or the one it ends on), or for a
// sound that dies away (Struck) the one it's struck with
double WaveLevelDb(const WaveSound &Sound, bool Struck);
// Its envelope's level while a note holds (its loop's on average, or its release point's), or its peak for a sound that
// dies away by itself (Struck)
double HeldVolume(const WaveSound &Sound, bool &Struck);

// design_instrument on an N163 channel: the wave sound whose waves a sound of its Kind plays when no wave is named (the
// soft flute for a lead, the string pad for a pad, the breath and the music box for theirs); never 0
const WaveSound *WaveSoundForKind(const std::string &Kind);
// Base's waves, following each other as in Base, with Design's volume, arpeggio and pitch, named Name
WaveSound DesignedWaveSound(const WaveSound &Base, const SoundDesign &Design, const std::string &Name);

// Waves of 16 samples (0-15) for sounds from other chips (References.h's ReferenceWaveSound): a pulse with High of its
// samples up; the 2A03's triangle and the VRC6's sawtooth (its seven steps), resampled; and any wave of Samples (0-Top)
// as one of 16: one cycle of it (a wave that repeats itself within its length, once), each sample the mean of what it
// covers (a shorter wave's held), scaled to 0-15
std::vector<int> N163PulseWave(int High);
std::vector<int> N163TriangleWave();
std::vector<int> N163SawtoothWave();
std::vector<int> N163WaveOf(const std::vector<int> &Samples, int Top);
// The samples of one cycle of a wave: half its length while its halves are the same, down to 4 (a wave drawn twice
// plays an octave over its notes). FamiTracker plays a cycle of 16 in tune, of 32 an octave under its notes.
int N163CycleLength(const std::vector<int> &Samples);

// The samples of wave memory with Channels N163 channels in use (16 go to
// each channel's registers)
int N163WaveMemory(int Channels);
// Where a wave of Size samples goes in Memory without touching the waves in
// Used (place, size): the lowest place, a multiple of Size; -1 when full
int FindWavePlace(int Size, int Memory, const std::vector<std::pair<int, int> > &Used);

}	// namespace ftapi

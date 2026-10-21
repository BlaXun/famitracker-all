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

// What the NES's sound chips play, worked out from their register writes:
// for each channel, whether it sounds, how loud, at what pitch and with what
// timbre. No audio is made; the 2A03's frame counter is followed for its
// envelopes, length counters, linear counter and sweeps, since those change
// what is heard between writes. Pitches are FamiTracker's note numbers
// (C-0 = 0, C-3 is middle C), as FamiTracker's own note tables would give
// for each chip, so notes written with them make the same register values.

#include <array>
#include <cstdint>
#include <vector>

namespace nsf {

enum channel_t {
	CH_PULSE1, CH_PULSE2, CH_TRIANGLE, CH_NOISE, CH_DPCM,
	CH_VRC6_PULSE1, CH_VRC6_PULSE2, CH_VRC6_SAW,
	CH_MMC5_PULSE1, CH_MMC5_PULSE2,
	CH_N163_1, CH_N163_2, CH_N163_3, CH_N163_4, CH_N163_5, CH_N163_6, CH_N163_7, CH_N163_8,
	CH_FDS,
	CH_VRC7_1, CH_VRC7_2, CH_VRC7_3, CH_VRC7_4, CH_VRC7_5, CH_VRC7_6,
	CH_5B_1, CH_5B_2, CH_5B_3,
	CH_COUNT
};

const char *ChannelName(int Channel);		// "Pulse 1", "VRC6 Sawtooth", ...

enum tick_flag {
	TICK_TRIGGER = 0x01,	// the note was (re)started: phase reset, length reloaded, key on, sample started
	TICK_SWEEP = 0x02,		// 2A03 pulse: the hardware sweep is on (Extra holds $4001)
	TICK_KEY = 0x04,		// VRC7: the key is on
	TICK_SUSTAIN = 0x08,	// VRC7: the sustain bit is on
	TICK_LOOP = 0x10,		// DPCM: the sample loops
};

// A channel during one PLAY call
struct ChannelTick {
	float Pitch;		// FamiTracker note number, fractional; noise: 0-15 as FamiTracker's noise notes
	uint16_t Period;	// the period or frequency register
	uint8_t Volume;		// 0-15 as heard, 0 when silent (FDS: 0-32)
	uint8_t Timbre;		// pulse duty; noise mode; VRC6 duty (0-7); saw: rate's top bit; DPCM rate; VRC7 patch
	uint16_t Wave;		// N163 and FDS wave, DPCM sample, VRC7 custom patch: an index into the recording's lists
	uint8_t Flags;		// tick_flag
	uint8_t Extra;		// 2A03 pulse: $4001; FDS: modulation depth; N163: wave position;
						// DPCM: the delta counter written as the sample started, plus 1 (0: none)
	uint16_t ModRate;	// FDS: modulation frequency (0-4095)
	uint16_t ModTable;	// FDS: modulation table, an index into the recording's list
	ChannelTick() : Pitch(0), Period(0), Volume(0), Timbre(0), Wave(0), Flags(0), Extra(0), ModRate(0), ModTable(0) {}
	bool Sounding() const { return Volume > 0; }
};

// Data the chips play that the recording keeps once each
struct SoundData {
	std::vector<std::vector<uint8_t> > Samples;		// DPCM sample bytes
	std::vector<std::vector<uint8_t> > Waves;		// N163 waves (4-bit samples) and FDS waves (64 x 6-bit)
	std::vector<std::vector<uint8_t> > ModTables;	// FDS modulation tables (32 x 3-bit)
	std::vector<std::array<uint8_t, 8> > Patches;	// VRC7 custom patches
	int AddSample(const std::vector<uint8_t> &Bytes);
	int AddWave(const std::vector<uint8_t> &Samples);
	int AddModTable(const std::vector<uint8_t> &Table);
	int AddPatch(const std::array<uint8_t, 8> &Regs);
};

// Reads memory for the DPCM channel
class SampleReader {
public:
	virtual ~SampleReader() {}
	virtual uint8_t ReadSample(uint16_t Address) = 0;
};

class Chips {
public:
	Chips(int ChipFlags, bool Pal, SampleReader &Memory, SoundData &Data);
	~Chips();

	// Register accesses at CPU cycle Cycle (counted from the song's start);
	// true when the address belongs to a sound chip
	bool Write(uint16_t Address, uint8_t Value, uint64_t Cycle);
	bool Read(uint16_t Address, uint64_t Cycle, uint8_t &Value);

	// The channels as they sound at Cycle; flags gathered since the last call
	void Snapshot(uint64_t Cycle, std::array<ChannelTick, CH_COUNT> &Out);

	bool HasChannel(int Channel) const;

private:
	struct Impl;
	Impl *m_pImpl;
	Chips(const Chips &);
	Chips &operator=(const Chips &);
};

// FamiTracker note numbers from the chips' pitch registers
double PulseNote(int Period, bool Pal);		// 2A03 and MMC5 pulses, triangle, VRC6 pulses
double SawNote(int Period);
double FdsNote(int Frequency);
double N163Note(int Frequency, int Channels);
double Vrc7Note(int Fnum, int Block);

}	// namespace nsf

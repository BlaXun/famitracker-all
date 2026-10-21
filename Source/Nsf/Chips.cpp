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

#include "Chips.h"
#include "NsfFile.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace nsf {

namespace {

const double CPU_NTSC = 1789773.0;
const double CPU_PAL = 1662607.0;
const double C0 = 32.7032;		// FamiTracker's C-0, in Hz

const uint8_t LENGTH_TABLE[32] = {
	0x0A, 0xFE, 0x14, 0x02, 0x28, 0x04, 0x50, 0x06, 0xA0, 0x08, 0x3C, 0x0A, 0x0E, 0x0C, 0x1A, 0x0E,
	0x0C, 0x10, 0x18, 0x12, 0x30, 0x14, 0x60, 0x16, 0xC0, 0x18, 0x48, 0x1A, 0x10, 0x1C, 0x20, 0x1E
};
const uint16_t DMC_PERIODS_NTSC[16] = { 428, 380, 340, 320, 286, 254, 226, 214, 190, 160, 142, 128, 106, 84, 72, 54 };
const uint16_t DMC_PERIODS_PAL[16] = { 398, 354, 316, 298, 276, 236, 210, 198, 176, 148, 132, 118, 98, 78, 66, 50 };

// The frame counter's steps, in CPU cycles from when it starts
const int STEPS_NTSC_4[4] = { 7457, 14913, 22371, 29829 };
const int STEPS_NTSC_5[5] = { 7457, 14913, 22371, 29829, 37281 };
const int STEPS_PAL_4[4] = { 8313, 16627, 24939, 33253 };
const int STEPS_PAL_5[5] = { 8313, 16627, 24939, 33253, 41565 };

double Note(double Hz)
{
	return Hz > 0 ? 12.0 * std::log2(Hz / C0) : 0;
}

struct Envelope {
	bool Start;
	int Divider;
	int Decay;
	Envelope() : Start(false), Divider(0), Decay(0) {}
	void Clock(int Period, bool Loop)
	{
		if (Start) {
			Start = false;
			Decay = 15;
			Divider = Period;
		}
		else if (Divider == 0) {
			Divider = Period;
			if (Decay > 0)
				--Decay;
			else if (Loop)
				Decay = 15;
		}
		else
			--Divider;
	}
};

// A 2A03 or MMC5 pulse
struct Pulse {
	int Duty;
	bool Halt, Constant;
	int Vol;
	uint8_t SweepReg;
	bool SweepOn, Negate, SweepReload;
	int SweepPeriod, Shift, SweepDivider;
	int Period;
	int Length;
	bool Enabled;
	Envelope Env;
	bool OnesComplement;	// pulse 1 negates with one's complement
	bool HasSweep;			// the MMC5's have none
	bool Trigger;

	Pulse(bool Ones, bool Sweep) :
		Duty(0), Halt(false), Constant(false), Vol(0), SweepReg(0), SweepOn(false), Negate(false), SweepReload(false),
		SweepPeriod(0), Shift(0), SweepDivider(0), Period(0), Length(0), Enabled(false), OnesComplement(Ones),
		HasSweep(Sweep), Trigger(false) {}

	int Target() const
	{
		const int Change = Period >> Shift;
		return Negate ? Period - Change - (OnesComplement ? 1 : 0) : Period + Change;
	}
	// The sweep unit silences low periods and ones whose target overflows,
	// even while it's off
	bool Muted() const
	{
		return HasSweep && (Period < 8 || (!Negate && Target() > 0x7FF));
	}
	int Output() const
	{
		if (Length == 0 || Muted())
			return 0;
		return Constant ? Vol : Env.Decay;
	}
	void ClockQuarter() { Env.Clock(Vol, Halt); }
	void ClockHalf()
	{
		if (Length > 0 && !Halt)
			--Length;
		if (!HasSweep)
			return;
		if (SweepDivider == 0 && SweepOn && Shift > 0 && !Muted()) {
			const int T = Target();
			if (T >= 0 && T <= 0x7FF)
				Period = T;
		}
		if (SweepDivider == 0 || SweepReload) {
			SweepDivider = SweepPeriod;
			SweepReload = false;
		}
		else
			--SweepDivider;
	}
	void Write(int Reg, uint8_t V)
	{
		switch (Reg) {
			case 0:
				Duty = V >> 6;
				Halt = (V & 0x20) != 0;
				Constant = (V & 0x10) != 0;
				Vol = V & 0x0F;
				break;
			case 1:
				SweepReg = V;
				SweepOn = (V & 0x80) != 0;
				SweepPeriod = (V >> 4) & 7;
				Negate = (V & 0x08) != 0;
				Shift = V & 7;
				SweepReload = true;
				break;
			case 2:
				Period = (Period & 0x700) | V;
				break;
			case 3:
				Period = (Period & 0xFF) | ((V & 7) << 8);
				if (Enabled)
					Length = LENGTH_TABLE[V >> 3];
				Env.Start = true;
				Trigger = true;
				break;
		}
	}
	void Enable(bool On)
	{
		Enabled = On;
		if (!On)
			Length = 0;
	}
};

struct Triangle {
	bool Control;
	int Reload, Linear;
	bool ReloadFlag;
	int Period, Length;
	bool Enabled, Trigger;

	Triangle() : Control(false), Reload(0), Linear(0), ReloadFlag(false), Period(0), Length(0), Enabled(false), Trigger(false) {}

	void Write(int Reg, uint8_t V)
	{
		switch (Reg) {
			case 0:
				Control = (V & 0x80) != 0;
				Reload = V & 0x7F;
				break;
			case 2:
				Period = (Period & 0x700) | V;
				break;
			case 3:
				Period = (Period & 0xFF) | ((V & 7) << 8);
				if (Enabled)
					Length = LENGTH_TABLE[V >> 3];
				ReloadFlag = true;
				Trigger = true;
				break;
		}
	}
	void ClockQuarter()
	{
		if (ReloadFlag)
			Linear = Reload;
		else if (Linear > 0)
			--Linear;
		if (!Control)
			ReloadFlag = false;
	}
	void ClockHalf()
	{
		if (Length > 0 && !Control)
			--Length;
	}
	// Periods below 2 are too high to hear (some drivers silence it that way)
	bool Sounding() const { return Linear > 0 && Length > 0 && Period >= 2; }
	void Enable(bool On)
	{
		Enabled = On;
		if (!On)
			Length = 0;
	}
};

struct Noise {
	bool Halt, Constant;
	int Vol;
	bool Mode;
	int PeriodIndex;
	int Length;
	bool Enabled;
	Envelope Env;
	bool Trigger;

	Noise() : Halt(false), Constant(false), Vol(0), Mode(false), PeriodIndex(0), Length(0), Enabled(false), Trigger(false) {}

	void Write(int Reg, uint8_t V)
	{
		switch (Reg) {
			case 0:
				Halt = (V & 0x20) != 0;
				Constant = (V & 0x10) != 0;
				Vol = V & 0x0F;
				break;
			case 2:
				Mode = (V & 0x80) != 0;
				PeriodIndex = V & 0x0F;
				break;
			case 3:
				if (Enabled)
					Length = LENGTH_TABLE[V >> 3];
				Env.Start = true;
				Trigger = true;
				break;
		}
	}
	int Output() const
	{
		if (Length == 0)
			return 0;
		return Constant ? Vol : Env.Decay;
	}
	void ClockQuarter() { Env.Clock(Vol, Halt); }
	void ClockHalf()
	{
		if (Length > 0 && !Halt)
			--Length;
	}
	void Enable(bool On)
	{
		Enabled = On;
		if (!On)
			Length = 0;
	}
};

struct Dmc {
	bool Loop;
	int Rate;
	int AddressReg, LengthReg;
	int Dac;
	bool DacWritten;
	// The sample playing
	bool Active;
	uint64_t EndCycle;	// when it stops, unless it loops
	int Sample;
	int PlayRate;
	bool PlayLoop;
	bool Trigger;
	int StartDac;		// the delta counter written just before it started, -1 for none

	Dmc() : Loop(false), Rate(0), AddressReg(0), LengthReg(0), Dac(0), DacWritten(false), Active(false), EndCycle(0),
		Sample(-1), PlayRate(0), PlayLoop(false), Trigger(false), StartDac(-1) {}

	bool Playing(uint64_t Cycle) const { return Active && (PlayLoop || Cycle < EndCycle); }
};

struct Vrc6Pulse {
	bool Mode;
	int Duty, Vol, Period;
	bool Enabled, Trigger;
	Vrc6Pulse() : Mode(false), Duty(0), Vol(0), Period(0), Enabled(false), Trigger(false) {}
	void Write(int Reg, uint8_t V)
	{
		switch (Reg) {
			case 0:
				Mode = (V & 0x80) != 0;
				Duty = (V >> 4) & 7;
				Vol = V & 0x0F;
				break;
			case 1:
				Period = (Period & 0xF00) | V;
				break;
			case 2:
				Period = (Period & 0xFF) | ((V & 0x0F) << 8);
				if ((V & 0x80) && !Enabled)
					Trigger = true;
				Enabled = (V & 0x80) != 0;
				break;
		}
	}
};

struct Vrc6Saw {
	int Rate, Period;
	bool Enabled, Trigger;
	Vrc6Saw() : Rate(0), Period(0), Enabled(false), Trigger(false) {}
	void Write(int Reg, uint8_t V)
	{
		switch (Reg) {
			case 0:
				Rate = V & 0x3F;
				break;
			case 1:
				Period = (Period & 0xF00) | V;
				break;
			case 2:
				Period = (Period & 0xFF) | ((V & 0x0F) << 8);
				if ((V & 0x80) && !Enabled)
					Trigger = true;
				Enabled = (V & 0x80) != 0;
				break;
		}
	}
};

}	// namespace

const char *ChannelName(int Channel)
{
	static const char *const NAMES[CH_COUNT] = {
		"Pulse 1", "Pulse 2", "Triangle", "Noise", "DPCM",
		"VRC6 Pulse 1", "VRC6 Pulse 2", "VRC6 Sawtooth",
		"MMC5 Pulse 1", "MMC5 Pulse 2",
		"N163 1", "N163 2", "N163 3", "N163 4", "N163 5", "N163 6", "N163 7", "N163 8",
		"FDS",
		"VRC7 1", "VRC7 2", "VRC7 3", "VRC7 4", "VRC7 5", "VRC7 6",
		"5B A", "5B B", "5B C",
	};
	return Channel >= 0 && Channel < CH_COUNT ? NAMES[Channel] : "";
}

int SoundData::AddSample(const std::vector<uint8_t> &Bytes)
{
	for (size_t i = 0; i < Samples.size(); ++i)
		if (Samples[i] == Bytes)
			return int(i);
	Samples.push_back(Bytes);
	return int(Samples.size()) - 1;
}

int SoundData::AddWave(const std::vector<uint8_t> &Data)
{
	for (size_t i = 0; i < Waves.size(); ++i)
		if (Waves[i] == Data)
			return int(i);
	Waves.push_back(Data);
	return int(Waves.size()) - 1;
}

int SoundData::AddModTable(const std::vector<uint8_t> &Table)
{
	for (size_t i = 0; i < ModTables.size(); ++i)
		if (ModTables[i] == Table)
			return int(i);
	ModTables.push_back(Table);
	return int(ModTables.size()) - 1;
}

int SoundData::AddPatch(const std::array<uint8_t, 8> &Regs)
{
	for (size_t i = 0; i < Patches.size(); ++i)
		if (Patches[i] == Regs)
			return int(i);
	Patches.push_back(Regs);
	return int(Patches.size()) - 1;
}

double PulseNote(int Period, bool Pal)
{
	return Note((Pal ? CPU_PAL : CPU_NTSC) / (16.0 * (Period + 1)));
}

double SawNote(int Period)
{
	return Note(CPU_NTSC / (14.0 * (Period + 1)));
}

double FdsNote(int Frequency)
{
	// FamiTracker's FDS notes (with TRANSPOSE_FDS)
	return Note(Frequency * CPU_NTSC / (65536.0 * 16.0));
}

double N163Note(int Frequency, int Channels)
{
	// FamiTracker's N163 notes: the frequency register for a note doesn't
	// depend on the wave's length
	return Note(Frequency * CPU_NTSC / (15728640.0 * (Channels > 0 ? Channels : 1)));
}

double Vrc7Note(int Fnum, int Block)
{
	// FamiTracker's VRC7 notes: block = octave, 172 = C
	return Fnum > 0 ? Block * 12 + 12.0 * std::log2(Fnum / 172.0) : 0;
}

struct Chips::Impl {
	int ChipFlags;
	bool Pal;
	SampleReader &Memory;
	SoundData &Data;

	// 2A03
	Pulse Pulses[2];
	Triangle Tri;
	Noise Noi;
	Dmc Dpcm;
	bool FiveStep;
	uint64_t FrameStart;
	int FrameStep;
	uint64_t LastCycle;

	// VRC6
	Vrc6Pulse V6Pulses[2];
	Vrc6Saw V6Saw;
	int V6Control;

	// MMC5
	Pulse M5Pulses[2];
	uint64_t M5NextClock;

	// N163
	uint8_t N163Ram[128];
	int N163Address;
	bool N163AutoIncrement;
	bool N163Trigger[8];

	// FDS
	uint8_t FdsWave[64];
	uint8_t FdsMod[32];
	int FdsModPos;
	uint8_t FdsRegs[16];	// $4080-$408A
	int FdsGain;			// the volume envelope's level, 0-32 (and beyond)
	int FdsModGain;
	uint64_t FdsNextEnv, FdsNextModEnv;
	bool FdsTrigger;

	// VRC7
	uint8_t Vrc7Regs[0x40];
	int Vrc7Address;
	bool Vrc7Trigger[6];

	// 5B
	uint8_t S5BRegs[16];
	int S5BAddress;

	Impl(int Flags, bool IsPal, SampleReader &Reader, SoundData &Sounds) :
		ChipFlags(Flags), Pal(IsPal), Memory(Reader), Data(Sounds),
		Pulses{ Pulse(true, true), Pulse(false, true) }, FiveStep(false), FrameStart(0), FrameStep(0), LastCycle(0),
		V6Control(0), M5Pulses{ Pulse(false, false), Pulse(false, false) }, M5NextClock(0),
		N163Address(0), N163AutoIncrement(false), FdsModPos(0), FdsGain(0), FdsModGain(0), FdsNextEnv(0), FdsNextModEnv(0),
		FdsTrigger(false), Vrc7Address(0), S5BAddress(0)
	{
		std::memset(N163Ram, 0, sizeof(N163Ram));
		std::memset(N163Trigger, 0, sizeof(N163Trigger));
		std::memset(FdsWave, 0, sizeof(FdsWave));
		std::memset(FdsMod, 0, sizeof(FdsMod));
		std::memset(FdsRegs, 0, sizeof(FdsRegs));
		std::memset(Vrc7Regs, 0, sizeof(Vrc7Regs));
		std::memset(Vrc7Trigger, 0, sizeof(Vrc7Trigger));
		std::memset(S5BRegs, 0, sizeof(S5BRegs));
		FdsRegs[0x0A] = 0xE8;
		M5NextClock = M5Period();
	}

	const int *Steps() const
	{
		return Pal ? (FiveStep ? STEPS_PAL_5 : STEPS_PAL_4) : (FiveStep ? STEPS_NTSC_5 : STEPS_NTSC_4);
	}
	int StepCount() const { return FiveStep ? 5 : 4; }
	uint64_t M5Period() const { return uint64_t((Pal ? CPU_PAL : CPU_NTSC) / 240.0); }

	void ClockQuarter()
	{
		Pulses[0].ClockQuarter();
		Pulses[1].ClockQuarter();
		Tri.ClockQuarter();
		Noi.ClockQuarter();
	}
	void ClockHalf()
	{
		Pulses[0].ClockHalf();
		Pulses[1].ClockHalf();
		Tri.ClockHalf();
		Noi.ClockHalf();
	}

	// Runs the frame counters and envelopes up to Cycle
	void RunTo(uint64_t Cycle)
	{
		if (Cycle < LastCycle)
			Cycle = LastCycle;
		for (;;) {
			const uint64_t Next = FrameStart + Steps()[FrameStep];
			if (Next > Cycle)
				break;
			// 4-step: quarter on every step, half on 2 and 4; 5-step: quarter on 1, 2, 3, 5, half on 2 and 5
			const int Step = FrameStep;
			if (FiveStep) {
				if (Step != 3)
					ClockQuarter();
				if (Step == 1 || Step == 4)
					ClockHalf();
			}
			else {
				ClockQuarter();
				if (Step == 1 || Step == 3)
					ClockHalf();
			}
			if (++FrameStep == StepCount()) {
				FrameStart = Next + 1;
				FrameStep = 0;
			}
		}
		if (ChipFlags & CHIP_MMC5) {
			while (M5NextClock <= Cycle) {
				for (int i = 0; i < 2; ++i) {
					M5Pulses[i].ClockQuarter();
					M5Pulses[i].ClockHalf();
				}
				M5NextClock += M5Period();
			}
		}
		if (ChipFlags & CHIP_FDS)
			RunFdsEnvelopes(Cycle);
		LastCycle = Cycle;
	}

	// The FDS's volume and modulation envelopes, when they're on
	uint64_t FdsEnvPeriod(int Speed) const
	{
		return uint64_t(8) * (Speed + 1) * (FdsRegs[0x0A] ? FdsRegs[0x0A] : 1);
	}
	// Moves an envelope's level by the steps due before Cycle
	static void RunEnvelope(uint64_t &Next, uint64_t Period, bool Up, int &Gain, uint64_t Cycle)
	{
		if (Cycle < Next + Period)
			return;
		const uint64_t Steps = (Cycle - Next) / Period;
		Next += Steps * Period;
		const int Change = int(std::min<uint64_t>(Steps, 64));
		Gain = Up ? std::min(32, Gain + Change) : std::max(0, Gain - Change);
	}
	void RunFdsEnvelopes(uint64_t Cycle)
	{
		const bool Halted = (FdsRegs[3] & 0x40) != 0 || FdsRegs[0x0A] == 0;
		if ((FdsRegs[0] & 0x80) || Halted)
			FdsNextEnv = Cycle;
		else
			RunEnvelope(FdsNextEnv, FdsEnvPeriod(FdsRegs[0] & 0x3F), (FdsRegs[0] & 0x40) != 0, FdsGain, Cycle);
		if ((FdsRegs[4] & 0x80) || Halted)
			FdsNextModEnv = Cycle;
		else
			RunEnvelope(FdsNextModEnv, FdsEnvPeriod(FdsRegs[4] & 0x3F), (FdsRegs[4] & 0x40) != 0, FdsModGain, Cycle);
	}

	void StartSample(uint64_t Cycle)
	{
		const int Length = Dpcm.LengthReg * 16 + 1;
		std::vector<uint8_t> Bytes(Length);
		uint16_t Address = uint16_t(0xC000 + Dpcm.AddressReg * 64);
		for (int i = 0; i < Length; ++i) {
			Bytes[i] = Memory.ReadSample(Address);
			Address = Address == 0xFFFF ? 0x8000 : uint16_t(Address + 1);
		}
		Dpcm.Sample = Data.AddSample(Bytes);
		Dpcm.PlayRate = Dpcm.Rate;
		Dpcm.PlayLoop = Dpcm.Loop;
		Dpcm.Active = true;
		Dpcm.EndCycle = Cycle + uint64_t(Length) * 8 * (Pal ? DMC_PERIODS_PAL : DMC_PERIODS_NTSC)[Dpcm.Rate];
		Dpcm.Trigger = true;
		Dpcm.StartDac = Dpcm.DacWritten ? Dpcm.Dac : -1;
	}

	bool Write(uint16_t A, uint8_t V, uint64_t Cycle)
	{
		// 2A03
		if (A >= 0x4000 && A <= 0x4017) {
			RunTo(Cycle);
			if (A <= 0x4007)
				Pulses[(A - 0x4000) >> 2].Write(A & 3, V);
			else if (A <= 0x400B)
				Tri.Write(A & 3, V);
			else if (A <= 0x400F)
				Noi.Write(A & 3, V);
			else if (A == 0x4010) {
				Dpcm.Loop = (V & 0x40) != 0;
				Dpcm.Rate = V & 0x0F;
			}
			else if (A == 0x4011) {
				Dpcm.Dac = V & 0x7F;
				Dpcm.DacWritten = true;
			}
			else if (A == 0x4012)
				Dpcm.AddressReg = V;
			else if (A == 0x4013)
				Dpcm.LengthReg = V;
			else if (A == 0x4015) {
				Pulses[0].Enable((V & 1) != 0);
				Pulses[1].Enable((V & 2) != 0);
				Tri.Enable((V & 4) != 0);
				Noi.Enable((V & 8) != 0);
				if (V & 0x10) {
					if (!Dpcm.Playing(Cycle))
						StartSample(Cycle);
				}
				else
					Dpcm.Active = false;
			}
			else if (A == 0x4017) {
				FiveStep = (V & 0x80) != 0;
				FrameStart = Cycle + 3;
				FrameStep = 0;
				if (FiveStep) {
					ClockQuarter();
					ClockHalf();
				}
			}
			return A != 0x4014 && A != 0x4016;
		}

		if ((ChipFlags & CHIP_VRC6) && A >= 0x9000 && A <= 0xB002) {
			const int Reg = A & 0x0FFF;
			if (A == 0x9003)
				V6Control = V;
			else if (Reg <= 2) {
				if (A < 0xA000)
					V6Pulses[0].Write(Reg, V);
				else if (A < 0xB000)
					V6Pulses[1].Write(Reg, V);
				else
					V6Saw.Write(Reg, V);
			}
			else
				return false;
			return true;
		}

		if ((ChipFlags & CHIP_MMC5) && A >= 0x5000 && A <= 0x5015) {
			RunTo(Cycle);
			if (A <= 0x5007)
				M5Pulses[(A - 0x5000) >> 2].Write(A & 3, V);
			else if (A == 0x5015) {
				M5Pulses[0].Enable((V & 1) != 0);
				M5Pulses[1].Enable((V & 2) != 0);
			}
			return true;
		}

		if (ChipFlags & CHIP_N163) {
			if (A == 0xF800) {
				N163Address = V & 0x7F;
				N163AutoIncrement = (V & 0x80) != 0;
				return true;
			}
			if (A == 0x4800) {
				const int Addr = N163Address;
				N163Ram[Addr] = V;
				// A phase register written: the channel starts over
				if (Addr >= 0x40 && ((Addr & 7) == 1 || (Addr & 7) == 3 || (Addr & 7) == 5))
					N163Trigger[(0x7F - Addr) >> 3] = true;
				if (N163AutoIncrement)
					N163Address = (N163Address + 1) & 0x7F;
				return true;
			}
		}

		if ((ChipFlags & CHIP_FDS) && A >= 0x4040 && A <= 0x408A) {
			RunTo(Cycle);
			if (A < 0x4080) {
				if (FdsRegs[9] & 0x80)
					FdsWave[A - 0x4040] = V & 0x3F;
			}
			else {
				const int Reg = A - 0x4080;
				if (Reg == 0) {
					if (V & 0x80)
						FdsGain = V & 0x3F;
				}
				else if (Reg == 3) {
					// The wave starts over when it's let go
					if (!(V & 0x80) && (FdsRegs[3] & 0x80))
						FdsTrigger = true;
				}
				else if (Reg == 4) {
					if (V & 0x80)
						FdsModGain = V & 0x3F;
				}
				else if (Reg == 7) {
					if (V & 0x80)
						FdsModPos = 0;
				}
				else if (Reg == 8) {
					if (FdsRegs[7] & 0x80) {
						FdsMod[FdsModPos & 31] = V & 7;
						FdsModPos = (FdsModPos + 1) & 31;
					}
				}
				if (Reg != 8)
					FdsRegs[Reg] = V;
			}
			return true;
		}

		if ((ChipFlags & CHIP_VRC7) && (A == 0x9010 || A == 0x9030)) {
			if (A == 0x9010)
				Vrc7Address = V & 0x3F;
			else {
				const int R = Vrc7Address;
				if (R >= 0x20 && R <= 0x25 && (V & 0x10) && !(Vrc7Regs[R] & 0x10))
					Vrc7Trigger[R - 0x20] = true;
				Vrc7Regs[R] = V;
			}
			return true;
		}

		if ((ChipFlags & CHIP_5B) && (A == 0xC000 || A == 0xE000)) {
			if (A == 0xC000)
				S5BAddress = V & 0x0F;
			else
				S5BRegs[S5BAddress] = V;
			return true;
		}

		return false;
	}

	bool Read(uint16_t A, uint64_t Cycle, uint8_t &V)
	{
		if (A == 0x4015) {
			RunTo(Cycle);
			V = uint8_t((Pulses[0].Length > 0 ? 1 : 0) | (Pulses[1].Length > 0 ? 2 : 0) | (Tri.Length > 0 ? 4 : 0) |
				(Noi.Length > 0 ? 8 : 0) | (Dpcm.Playing(Cycle) ? 0x10 : 0));
			return true;
		}
		if ((ChipFlags & CHIP_MMC5) && A == 0x5015) {
			RunTo(Cycle);
			V = uint8_t((M5Pulses[0].Length > 0 ? 1 : 0) | (M5Pulses[1].Length > 0 ? 2 : 0));
			return true;
		}
		if ((ChipFlags & CHIP_N163) && A == 0x4800) {
			V = N163Ram[N163Address];
			if (N163AutoIncrement)
				N163Address = (N163Address + 1) & 0x7F;
			return true;
		}
		if ((ChipFlags & CHIP_FDS) && A >= 0x4040 && A <= 0x4092) {
			RunTo(Cycle);
			if (A < 0x4080)
				V = uint8_t(FdsWave[A - 0x4040] | 0x40);
			else if (A == 0x4090)
				V = uint8_t(FdsGain | 0x40);
			else if (A == 0x4092)
				V = uint8_t(FdsModGain | 0x40);
			else
				V = 0x40;
			return true;
		}
		return false;
	}

	void Snapshot(uint64_t Cycle, std::array<ChannelTick, CH_COUNT> &Out)
	{
		RunTo(Cycle);
		for (int i = 0; i < CH_COUNT; ++i)
			Out[i] = ChannelTick();

		for (int i = 0; i < 2; ++i) {
			ChannelTick &t = Out[CH_PULSE1 + i];
			const Pulse &p = Pulses[i];
			t.Volume = uint8_t(p.Output());
			t.Period = uint16_t(p.Period);
			t.Pitch = float(PulseNote(p.Period, Pal));
			t.Timbre = uint8_t(p.Duty);
			t.Extra = p.SweepReg;
			t.Flags = uint8_t((p.Trigger ? TICK_TRIGGER : 0) | (p.SweepOn && p.Shift > 0 ? TICK_SWEEP : 0));
			Pulses[i].Trigger = false;
		}
		{
			ChannelTick &t = Out[CH_TRIANGLE];
			t.Volume = Tri.Sounding() ? 15 : 0;
			t.Period = uint16_t(Tri.Period);
			t.Pitch = float(PulseNote(Tri.Period, Pal));	// written as FamiTracker writes it: an octave above how it sounds
			t.Flags = Tri.Trigger ? TICK_TRIGGER : 0;
			Tri.Trigger = false;
		}
		{
			ChannelTick &t = Out[CH_NOISE];
			t.Volume = uint8_t(Noi.Output());
			t.Period = uint16_t(Noi.PeriodIndex);
			t.Pitch = float(15 - Noi.PeriodIndex);
			t.Timbre = Noi.Mode ? 1 : 0;
			t.Flags = Noi.Trigger ? TICK_TRIGGER : 0;
			Noi.Trigger = false;
		}
		{
			ChannelTick &t = Out[CH_DPCM];
			const bool Playing = Dpcm.Playing(Cycle);
			t.Volume = Playing || Dpcm.Trigger ? 15 : 0;
			t.Timbre = uint8_t(Dpcm.PlayRate);
			t.Wave = uint16_t(Dpcm.Sample < 0 ? 0 : Dpcm.Sample);
			t.Period = uint16_t(Dpcm.Dac);
			t.Flags = uint8_t((Dpcm.Trigger ? TICK_TRIGGER : 0) | (Dpcm.PlayLoop ? TICK_LOOP : 0));
			t.Extra = uint8_t(Dpcm.Trigger && Dpcm.StartDac >= 0 ? Dpcm.StartDac + 1 : 0);
			Dpcm.Trigger = false;
			Dpcm.DacWritten = false;
		}

		if (ChipFlags & CHIP_VRC6) {
			const int Shift = (V6Control & 4) ? 8 : ((V6Control & 2) ? 4 : 0);
			const bool Halted = (V6Control & 1) != 0;
			for (int i = 0; i < 2; ++i) {
				ChannelTick &t = Out[CH_VRC6_PULSE1 + i];
				const Vrc6Pulse &p = V6Pulses[i];
				const int Period = p.Period >> Shift;
				t.Volume = uint8_t(p.Enabled && !p.Mode && !Halted ? p.Vol : 0);
				t.Period = uint16_t(Period);
				t.Pitch = float(PulseNote(Period, false));
				t.Timbre = uint8_t(p.Duty);
				t.Flags = p.Trigger ? TICK_TRIGGER : 0;
				V6Pulses[i].Trigger = false;
			}
			ChannelTick &t = Out[CH_VRC6_SAW];
			const int Period = V6Saw.Period >> Shift;
			int Rate = V6Saw.Enabled && !Halted ? V6Saw.Rate : 0;
			// As FamiTracker writes it: volume in bits 1-4, its duty in bit 5
			int Vol = (Rate & 0x1F) >> 1, Duty = Rate >> 5;
			if (Rate > 0 && Vol == 0) {
				Vol = Duty ? 15 : 1;
				Duty = 0;
			}
			t.Volume = uint8_t(Vol);
			t.Timbre = uint8_t(Duty);
			t.Period = uint16_t(Period);
			t.Pitch = float(SawNote(Period));
			t.Flags = V6Saw.Trigger ? TICK_TRIGGER : 0;
			V6Saw.Trigger = false;
		}

		if (ChipFlags & CHIP_MMC5) {
			for (int i = 0; i < 2; ++i) {
				ChannelTick &t = Out[CH_MMC5_PULSE1 + i];
				const Pulse &p = M5Pulses[i];
				t.Volume = uint8_t(p.Output());
				t.Period = uint16_t(p.Period);
				t.Pitch = float(PulseNote(p.Period, false));
				t.Timbre = uint8_t(p.Duty);
				t.Flags = p.Trigger ? TICK_TRIGGER : 0;
				M5Pulses[i].Trigger = false;
			}
		}

		if (ChipFlags & CHIP_N163) {
			const int Count = ((N163Ram[0x7F] >> 4) & 7) + 1;
			for (int i = 0; i < 8; ++i) {
				ChannelTick &t = Out[CH_N163_1 + i];
				if (i >= Count) {
					N163Trigger[i] = false;
					continue;
				}
				const int Base = 0x78 - 8 * i;
				const int Freq = N163Ram[Base] | (N163Ram[Base + 2] << 8) | ((N163Ram[Base + 4] & 3) << 16);
				const int Length = 256 - (N163Ram[Base + 4] & 0xFC);
				const int Position = N163Ram[Base + 6];
				t.Volume = uint8_t(Freq > 0 ? N163Ram[Base + 7] & 0x0F : 0);
				t.Period = uint16_t(std::min(Freq, 0xFFFF));
				t.Pitch = float(N163Note(Freq, Count));
				t.Extra = uint8_t(Position);
				t.Flags = N163Trigger[i] ? TICK_TRIGGER : 0;
				N163Trigger[i] = false;
				if (t.Volume > 0) {
					std::vector<uint8_t> Wave(Length);
					for (int s = 0; s < Length; ++s) {
						const int Addr = (Position + s) & 0xFF;
						const uint8_t Byte = N163Ram[Addr >> 1];
						Wave[s] = uint8_t((Addr & 1) ? Byte >> 4 : Byte & 0x0F);
					}
					t.Wave = uint16_t(Data.AddWave(Wave));
					t.Timbre = uint8_t(Count);
				}
			}
		}

		if (ChipFlags & CHIP_FDS) {
			ChannelTick &t = Out[CH_FDS];
			const int Freq = FdsRegs[2] | ((FdsRegs[3] & 0x0F) << 8);
			const bool Halted = (FdsRegs[3] & 0x80) != 0;
			t.Volume = uint8_t(Halted || Freq == 0 ? 0 : std::min(FdsGain, 32));
			t.Period = uint16_t(Freq);
			t.Pitch = float(FdsNote(Freq));
			t.Flags = FdsTrigger ? TICK_TRIGGER : 0;
			FdsTrigger = false;
			if (t.Volume > 0) {
				t.Wave = uint16_t(Data.AddWave(std::vector<uint8_t>(FdsWave, FdsWave + 64)));
				const bool ModOn = !(FdsRegs[7] & 0x80);
				t.Extra = uint8_t(ModOn ? std::min(FdsModGain, 63) : 0);
				t.ModRate = uint16_t(ModOn ? FdsRegs[6] | ((FdsRegs[7] & 0x0F) << 8) : 0);
				t.ModTable = uint16_t(Data.AddModTable(std::vector<uint8_t>(FdsMod, FdsMod + 32)));
			}
		}

		if (ChipFlags & CHIP_VRC7) {
			std::array<uint8_t, 8> Custom;
			for (int r = 0; r < 8; ++r)
				Custom[r] = Vrc7Regs[r];
			for (int i = 0; i < 6; ++i) {
				ChannelTick &t = Out[CH_VRC7_1 + i];
				const int Fnum = Vrc7Regs[0x10 + i] | ((Vrc7Regs[0x20 + i] & 1) << 8);
				const int Block = (Vrc7Regs[0x20 + i] >> 1) & 7;
				const bool Key = (Vrc7Regs[0x20 + i] & 0x10) != 0;
				const int Patch = Vrc7Regs[0x30 + i] >> 4;
				t.Volume = uint8_t(Key && Fnum > 0 ? 15 - (Vrc7Regs[0x30 + i] & 0x0F) : 0);
				if (Key && t.Volume == 0)
					t.Volume = 1;
				t.Period = uint16_t(Fnum | (Block << 9));
				t.Pitch = float(Vrc7Note(Fnum, Block));
				t.Timbre = uint8_t(Patch);
				t.Wave = uint16_t(Patch == 0 ? Data.AddPatch(Custom) : 0);
				t.Flags = uint8_t((Vrc7Trigger[i] ? TICK_TRIGGER : 0) | (Key ? TICK_KEY : 0) |
					((Vrc7Regs[0x20 + i] & 0x20) ? TICK_SUSTAIN : 0));
				Vrc7Trigger[i] = false;
			}
		}

		if (ChipFlags & CHIP_5B) {
			for (int i = 0; i < 3; ++i) {
				ChannelTick &t = Out[CH_5B_1 + i];
				const int Period = S5BRegs[i * 2] | ((S5BRegs[i * 2 + 1] & 0x0F) << 8);
				const bool Tone = !(S5BRegs[7] & (1 << i));
				t.Volume = uint8_t(Tone && Period > 0 ? ((S5BRegs[8 + i] & 0x10) ? 15 : S5BRegs[8 + i] & 0x0F) : 0);
				t.Period = uint16_t(Period);
				t.Pitch = float(Period > 0 ? Note(CPU_NTSC / (32.0 * Period)) : 0);
			}
		}
	}
};

Chips::Chips(int ChipFlags, bool Pal, SampleReader &Memory, SoundData &Data) :
	m_pImpl(new Impl(ChipFlags, Pal, Memory, Data))
{
}

Chips::~Chips()
{
	delete m_pImpl;
}

bool Chips::Write(uint16_t Address, uint8_t Value, uint64_t Cycle)
{
	return m_pImpl->Write(Address, Value, Cycle);
}

bool Chips::Read(uint16_t Address, uint64_t Cycle, uint8_t &Value)
{
	return m_pImpl->Read(Address, Cycle, Value);
}

void Chips::Snapshot(uint64_t Cycle, std::array<ChannelTick, CH_COUNT> &Out)
{
	m_pImpl->Snapshot(Cycle, Out);
}

bool Chips::HasChannel(int Channel) const
{
	const int Flags = m_pImpl->ChipFlags;
	if (Channel <= CH_DPCM)
		return true;
	if (Channel <= CH_VRC6_SAW)
		return (Flags & CHIP_VRC6) != 0;
	if (Channel <= CH_MMC5_PULSE2)
		return (Flags & CHIP_MMC5) != 0;
	if (Channel <= CH_N163_8)
		return (Flags & CHIP_N163) != 0;
	if (Channel == CH_FDS)
		return (Flags & CHIP_FDS) != 0;
	if (Channel <= CH_VRC7_6)
		return (Flags & CHIP_VRC7) != 0;
	return (Flags & CHIP_5B) != 0;
}

}	// namespace nsf

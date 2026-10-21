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

#include "Player.h"
#include "Cpu6502.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace nsf {

namespace {

const double CPU_NTSC = 1789773.0;
const double CPU_PAL = 1662607.0;
const uint64_t INIT_CYCLES = 30000000;	// INIT may unpack data for a while, but not for 17 seconds

// The NES's memory as an NSF player sets it up: RAM, the cartridge's RAM at
// $6000, the program in banks of 4 KB, and the sound chips' registers
class Machine : public Bus, public SampleReader {
public:
	Machine(const NsfFile &File, SoundData &Sounds) :
		m_File(File), m_bFds((File.Chips & CHIP_FDS) != 0), m_bBanked(File.Bankswitched),
		m_Chips(File.Chips, File.Pal && !File.Dual, *this, Sounds), m_iMultiplierA(0), m_iMultiplierB(0), m_bVblank(false)
	{
		std::memset(m_Ram, 0, sizeof(m_Ram));
		std::memset(m_Sram, 0, sizeof(m_Sram));
		std::memset(m_ExRam, 0, sizeof(m_ExRam));
		std::memset(m_Flat, 0, sizeof(m_Flat));
		if (m_bBanked) {
			// The data starts (LoadAddress & $FFF) bytes into its first bank
			const size_t Padding = File.LoadAddress & 0x0FFF;
			m_Rom.assign(Padding, 0);
			m_Rom.insert(m_Rom.end(), File.Data.begin(), File.Data.end());
			m_Rom.resize((m_Rom.size() + 0xFFF) & ~size_t(0xFFF), 0);
			for (int i = 0; i < 8; ++i)
				SwitchBank(8 + i, File.Banks[i]);
			if (m_bFds) {
				SwitchBank(6, File.Banks[6]);
				SwitchBank(7, File.Banks[7]);
			}
		}
		else {
			// The program sits at its load address; FDS programs may start at $6000
			for (size_t i = 0; i < File.Data.size(); ++i) {
				const size_t Address = size_t(File.LoadAddress) + i;
				if (Address > 0xFFFF)
					break;
				m_Flat[Address] = File.Data[i];
			}
		}
	}

	Chips &SoundChips() { return m_Chips; }

	uint8_t Read(uint16_t A, uint64_t Cycle) override
	{
		if (A < 0x2000)
			return m_Ram[A & 0x7FF];
		if (A < 0x4000) {
			// Some drivers wait for the vertical blank: say it comes every other read
			if ((A & 7) == 2) {
				m_bVblank = !m_bVblank;
				return m_bVblank ? 0x80 : 0x00;
			}
			return 0;
		}
		if (A < 0x6000) {
			uint8_t V = 0;
			if (m_Chips.Read(A, Cycle, V))
				return V;
			if ((m_File.Chips & CHIP_MMC5) && A == 0x5205)
				return uint8_t(m_iMultiplierA * m_iMultiplierB);
			if ((m_File.Chips & CHIP_MMC5) && A == 0x5206)
				return uint8_t((m_iMultiplierA * m_iMultiplierB) >> 8);
			if ((m_File.Chips & CHIP_MMC5) && A >= 0x5C00 && A <= 0x5FF5)
				return m_ExRam[A - 0x5C00];
			return 0;
		}
		if (m_bFds && A < 0xE000)
			return m_Flat[A];		// the FDS has RAM up to $DFFF
		if (A < 0x8000)
			return m_Sram[A - 0x6000];
		return ReadRom(A);
	}

	void Write(uint16_t A, uint8_t V, uint64_t Cycle) override
	{
		if (A < 0x2000) {
			m_Ram[A & 0x7FF] = V;
			return;
		}
		if (A < 0x4000)
			return;
		if (A >= 0x5FF6 && A <= 0x5FFF) {
			if (A >= 0x5FF8 && m_bBanked)
				SwitchBank(8 + (A - 0x5FF8), V);
			else if (A < 0x5FF8 && m_bFds && m_bBanked)
				SwitchBank(6 + (A - 0x5FF6), V);
			return;
		}
		if (m_Chips.Write(A, V, Cycle))
			return;
		if (A < 0x6000) {
			if ((m_File.Chips & CHIP_MMC5) && A == 0x5205)
				m_iMultiplierA = V;
			else if ((m_File.Chips & CHIP_MMC5) && A == 0x5206)
				m_iMultiplierB = V;
			else if ((m_File.Chips & CHIP_MMC5) && A >= 0x5C00 && A <= 0x5FF5)
				m_ExRam[A - 0x5C00] = V;
			return;
		}
		if (m_bFds && A < 0xE000) {
			m_Flat[A] = V;
			return;
		}
		if (A < 0x8000)
			m_Sram[A - 0x6000] = V;
	}

	uint8_t ReadSample(uint16_t A) override
	{
		return A >= 0x8000 ? (m_bFds && A < 0xE000 ? m_Flat[A] : ReadRom(A)) : 0;
	}

private:
	uint8_t ReadRom(uint16_t A) const
	{
		if (!m_bBanked || m_bFds)
			return m_Flat[A];
		const int Bank = m_iBank[(A >> 12) - 8];
		const size_t Offset = size_t(Bank) * 0x1000 + (A & 0x0FFF);
		return Offset < m_Rom.size() ? m_Rom[Offset] : 0;
	}

	// Page 8-15 ($8000-$FFFF) shows bank Bank. On the FDS, whose program
	// lives in RAM from $6000 on, the bank is copied in.
	void SwitchBank(int Page, int Bank)
	{
		if (m_bFds) {
			for (int i = 0; i < 0x1000; ++i) {
				const size_t Offset = size_t(Bank) * 0x1000 + i;
				m_Flat[Page * 0x1000 + i] = Offset < m_Rom.size() ? m_Rom[Offset] : 0;
			}
		}
		else if (Page >= 8)
			m_iBank[Page - 8] = Bank;
	}

	const NsfFile &m_File;
	bool m_bFds, m_bBanked;
	Chips m_Chips;
	uint8_t m_Ram[0x800];
	uint8_t m_Sram[0x2000];
	uint8_t m_ExRam[0x400];
	uint8_t m_Flat[0x10000];		// unbanked programs, and the FDS's RAM
	std::vector<uint8_t> m_Rom;	// banked programs
	int m_iBank[8] = {};
	int m_iMultiplierA, m_iMultiplierB;
	bool m_bVblank;
};

uint64_t Mix(uint64_t h, uint64_t v)
{
	h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
	return h;
}

// What a tick sounds like, ignoring what wobbles (vibrato phases and the
// like): the loop is found where these repeat
uint64_t Fingerprint(const Recording &R, int Tick)
{
	uint64_t h = 1469598103934665603ull;
	for (size_t i = 0; i < R.Channels.size(); ++i) {
		const int c = R.Channels[i];
		const ChannelTick &t = R.Ticks[c][Tick];
		if (!t.Sounding()) {
			h = Mix(h, uint64_t(c) << 40);
			continue;
		}
		uint64_t v = uint64_t(t.Volume) | (uint64_t(t.Timbre) << 8) | (uint64_t(t.Wave) << 16);
		if (c == CH_NOISE || c == CH_DPCM)
			v |= uint64_t(t.Period & 0xFF) << 32 | uint64_t(t.Flags & TICK_TRIGGER) << 48;
		else
			v |= uint64_t(int(std::floor(t.Pitch + 0.5)) & 0xFF) << 32;
		h = Mix(h, v ^ (uint64_t(c) << 56));
	}
	return h;
}

// What a channel plays at a tick, coarsely: its note (noise: its pitch;
// DPCM: its sample), -1 when silent
int Coarse(const Recording &R, int Channel, int Tick)
{
	const ChannelTick &t = R.Ticks[Channel][Tick];
	if (!t.Sounding())
		return -1;
	if (Channel == CH_DPCM)
		return t.Wave;
	return int(std::floor(t.Pitch + 0.5)) & 0xFFFF;
}

// Whether a channel plays at Tick what it plays a tick either side of Other
bool Near(const Recording &R, int Channel, int Tick, int Other, int Ticks)
{
	const int v = Coarse(R, Channel, Tick);
	if (v == Coarse(R, Channel, Other))
		return true;
	return (Other > 0 && v == Coarse(R, Channel, Other - 1)) || (Other + 1 < Ticks && v == Coarse(R, Channel, Other + 1));
}

}	// namespace

// A loop whose length isn't a whole number of ticks (a tempo that runs a
// fraction of a tick over each time round, a vibrato or tremolo on a clock
// of its own) never repeats tick for tick: here the notes repeat, a tick
// either way
bool FindLoopApprox(const Recording &R, int Ticks, int MinPeriod, int Margin, int &Start, int &Period)
{
	const int Window = std::min(Ticks / 3, 1200);
	if (Window < 60 || Ticks < 2 * MinPeriod + Margin)
		return false;
	const int From = Ticks - Window;
	std::vector<int> Channels;
	for (size_t i = 0; i < R.Channels.size(); ++i)
		Channels.push_back(R.Channels[i]);
	// The music in the window has to move, or anything would match
	int Changes = 0;
	for (int t = From + 1; t < Ticks; ++t)
		for (size_t i = 0; i < Channels.size(); ++i)
			if (Coarse(R, Channels[i], t) != Coarse(R, Channels[i], t - 1))
				++Changes;
	if (Changes < 8)
		return false;
	const int Samples = (Window + 1) / 2 * int(Channels.size());
	const int Allowed = Samples / 200;		// 99.5% of the window, sampled every other tick
	for (int P = MinPeriod; P <= From; ++P) {
		int Misses = 0;
		for (int t = From; t < Ticks && Misses <= Allowed; t += 2)
			for (size_t i = 0; i < Channels.size() && Misses <= Allowed; ++i)
				if (!Near(R, Channels[i], t - P, t, Ticks))
					++Misses;
		if (Misses > Allowed)
			continue;
		// Back from the window, to where it stops repeating
		int S = From - P;
		int Run = 0;
		for (int t = From - P - 1; t >= 0; --t) {
			bool Same = true;
			for (size_t i = 0; i < Channels.size() && Same; ++i)
				Same = Near(R, Channels[i], t, t + P, Ticks);
			if (Same) {
				S = t;
				Run = 0;
			}
			else if (++Run > 2)
				break;
		}
		if (Ticks - P - S >= P + Margin) {
			Start = S;
			Period = P;
			return true;
		}
	}
	return false;
}

bool FindLoop(const std::vector<uint64_t> &F, int MinPeriod, int Margin, int &Start, int &Period)
{
	const int N = int(F.size());
	if (MinPeriod < 1)
		MinPeriod = 1;
	const int W = std::max(8, std::min(MinPeriod, 240));
	if (N < 2 * MinPeriod + Margin || N < W + MinPeriod)
		return false;

	// Windows of W ticks that equal the last one start candidate periods
	const uint64_t B = 1099511628211ull;
	uint64_t Power = 1;
	for (int i = 0; i < W - 1; ++i)
		Power *= B;
	const int Last = N - W;
	uint64_t Target = 0;
	for (int i = 0; i < W; ++i)
		Target = Target * B + F[Last + i];
	std::vector<int> Periods;
	uint64_t R = 0;
	for (int i = 0; i < W; ++i)
		R = R * B + F[i];
	for (int t = 0; t + MinPeriod <= Last; ++t) {
		if (R == Target && std::equal(F.begin() + t, F.begin() + t + W, F.begin() + Last))
			Periods.push_back(Last - t);
		R = (R - F[t] * Power) * B + F[t + W];
	}
	std::sort(Periods.begin(), Periods.end());

	int Tried = 0;
	for (size_t i = 0; i < Periods.size() && Tried < 200; ++i, ++Tried) {
		const int P = Periods[i];
		int x = N - 1 - P;
		while (x >= 0 && F[x] == F[x + P])
			--x;
		const int S = x + 1;
		if (N - P - S >= P + Margin) {
			Start = S;
			Period = P;
			return true;
		}
	}
	return false;
}

bool RecordTrack(const NsfFile &File, int Track, const RecordOptions &Options, Recording &Out, std::string &Error)
{
	Out = Recording();
	if (Track < 0 || Track >= File.Songs) {
		Error = "there is no track " + std::to_string(Track + 1);
		return false;
	}
	const bool Pal = File.Pal && !File.Dual;
	Out.Pal = Pal;
	Out.ChipFlags = File.Chips;
	Out.TickRate = File.PlayRate();
	if (Out.TickRate < 10 || Out.TickRate > 1000)
		Out.TickRate = Pal ? 50.0 : 60.0;

	Machine M(File, Out.Sounds);
	Cpu6502 Cpu(M);
	Chips &C = M.SoundChips();
	Out.Ticks.assign(CH_COUNT, std::vector<ChannelTick>());
	for (int c = 0; c < CH_COUNT; ++c) {
		// No 5B: FamiTracker 0.4.6 can't play it
		if (C.HasChannel(c) && c < CH_5B_1)
			Out.Channels.push_back(c);
	}

	// The sound chips as a player leaves them before INIT
	for (uint16_t a = 0x4000; a <= 0x4013; ++a)
		M.Write(a, 0, 0);
	M.Write(0x4015, 0x00, 0);
	M.Write(0x4015, 0x0F, 0);
	M.Write(0x4017, 0x40, 0);
	if (File.Chips & CHIP_FDS) {
		M.Write(0x4089, 0x80, 0);
		M.Write(0x408A, 0xE8, 0);
	}

	if (!Cpu.Call(File.InitAddress, uint8_t(Track), Pal ? 1 : 0, 0, INIT_CYCLES)) {
		Error = Cpu.Jammed() ? "the player stopped the CPU while starting the track" : "the player's INIT routine didn't return";
		return false;
	}

	const double CpuClock = Pal ? CPU_PAL : CPU_NTSC;
	const double TickCycles = CpuClock / Out.TickRate;
	const uint64_t Begin = Cpu.Cycles() + 1;
	const int MaxTicks = int(Options.MaxSeconds * Out.TickRate);
	const int SilenceTicks = std::max(1, int(Options.SilenceSeconds * Out.TickRate));
	const int MinLoop = std::max(1, int(Options.MinLoopSeconds * Out.TickRate));
	const int Margin = int(4 * Out.TickRate);
	const int CheckEvery = int(5 * Out.TickRate);
	std::vector<uint64_t> Prints;
	int Quiet = 0, LastSound = -1, Overruns = 0;
	int NextApprox = int(30 * Out.TickRate);
	std::array<ChannelTick, CH_COUNT> Snap;

	for (int Tick = 0; Tick < MaxTicks; ++Tick) {
		const uint64_t Due = Begin + uint64_t(std::llround(Tick * TickCycles));
		if (Cpu.Cycles() < Due)
			Cpu.SetCycles(Due);
		if (!Cpu.Call(File.PlayAddress, 0, 0, 0, uint64_t(TickCycles * 8))) {
			if (Cpu.Jammed()) {
				Out.Problems.push_back("the player stopped the CPU");
				break;
			}
			++Overruns;
		}
		// What is heard through most of the tick
		C.Snapshot(std::max<uint64_t>(Cpu.Cycles(), Due + uint64_t(TickCycles / 2)), Snap);
		bool Sound = false;
		for (size_t i = 0; i < Out.Channels.size(); ++i) {
			const int c = Out.Channels[i];
			Out.Ticks[c].push_back(Snap[c]);
			Sound = Sound || Snap[c].Sounding();
		}
		Out.Length = Tick + 1;
		Prints.push_back(Fingerprint(Out, Tick));

		if (Sound) {
			Quiet = 0;
			LastSound = Tick;
		}
		else if (++Quiet >= SilenceTicks && LastSound >= 0) {
			// It ended: keep a moment of the silence
			Out.EndsInSilence = true;
			Out.Length = std::min(Out.Length, LastSound + 1 + int(Out.TickRate / 2));
			break;
		}
		if (LastSound < 0 && Tick >= SilenceTicks * 2) {
			Out.Problems.push_back("the track makes no sound");
			break;
		}

		if ((Tick + 1) % CheckEvery == 0) {
			int Start = 0, Period = 0;
			if (FindLoop(Prints, MinLoop, Margin, Start, Period)) {
				Out.LoopStart = Start;
				Out.LoopLength = Period;
				break;
			}
			// Now and then, a loop that repeats only a tick either way
			if (Tick + 1 >= NextApprox) {
				NextApprox *= 2;
				if (FindLoopApprox(Out, Tick + 1, MinLoop, Margin, Start, Period)) {
					Out.LoopStart = Start;
					Out.LoopLength = Period;
					break;
				}
			}
			if (Options.Progress && !Options.Progress(Tick / Out.TickRate)) {
				Error = "stopped";
				return false;
			}
		}
	}

	// Played to the end without finding one: one last look
	if (Out.LoopStart < 0 && !Out.EndsInSilence) {
		int Start = 0, Period = 0;
		if (FindLoopApprox(Out, Out.Length, MinLoop, Margin, Start, Period)) {
			Out.LoopStart = Start;
			Out.LoopLength = Period;
		}
	}
	if (Overruns > 0)
		Out.Problems.push_back("the player's PLAY routine ran past its frame " + std::to_string(Overruns) + " times");
	if (Out.LoopStart < 0 && !Out.EndsInSilence && Out.Length >= MaxTicks)
		Out.Problems.push_back("no loop was found in " + std::to_string(int(Options.MaxSeconds)) + " seconds");

	// Whatever was recorded past the loop's second time round isn't needed
	if (Out.LoopStart >= 0) {
		// A loop that holds one sound forever is an ending
		bool Constant = true;
		for (int t = Out.LoopStart + 1; t < Out.LoopStart + Out.LoopLength && Constant; ++t)
			Constant = Prints[t] == Prints[Out.LoopStart];
		if (Constant) {
			Out.Length = std::min(Out.Length, Out.LoopStart + Out.LoopLength);
			Out.LoopStart = -1;
			Out.LoopLength = 0;
			Out.EndsInSilence = true;
		}
		else
			Out.Length = std::min(Out.Length, Out.LoopStart + 2 * Out.LoopLength);
	}
	for (size_t i = 0; i < Out.Channels.size(); ++i)
		Out.Ticks[Out.Channels[i]].resize(Out.Length);
	return true;
}

}	// namespace nsf

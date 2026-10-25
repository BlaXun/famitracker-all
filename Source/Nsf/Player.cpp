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
#include <cstdio>
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

// Which channels a fingerprint takes: all, all but the drums (noise and
// DPCM), or the drums alone
enum print_parts { PARTS_ALL, PARTS_MELODIC, PARTS_DRUMS };

bool DrumChannel(int Channel) { return Channel == CH_NOISE || Channel == CH_DPCM; }

// What a tick sounds like, ignoring what wobbles (vibrato phases and the
// like): the loop is found where these repeat
uint64_t Fingerprint(const Recording &R, int Tick, int Parts = PARTS_ALL)
{
	uint64_t h = 1469598103934665603ull;
	for (size_t i = 0; i < R.Channels.size(); ++i) {
		const int c = R.Channels[i];
		if ((Parts == PARTS_MELODIC && DrumChannel(c)) || (Parts == PARTS_DRUMS && !DrumChannel(c)))
			continue;
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

// Where a loop of Period ticks starts that repeats exactly up to the end of
// the fingerprints, for at least a whole period and Margin; -1 when it doesn't
int ExactStart(const std::vector<uint64_t> &F, int Period, int Margin)
{
	const int N = int(F.size());
	if (Period < 1 || N < 2 * Period + Margin)
		return -1;
	int x = N - 1 - Period;
	while (x >= 0 && F[x] == F[x + Period])
		--x;
	return N - Period - (x + 1) >= Period + Margin ? x + 1 : -1;
}

// How long a loop of Period ticks has to have repeated after its first time
// through before it's taken: a whole period and a margin; while the track
// plays (Confirm), also two more times through and Confirm ticks, since a
// passage that repeats once and then moves on is no loop
int RepeatNeeded(int Period, int Margin, int Confirm)
{
	int Need = Period + Margin;
	if (Confirm > 0)
		Need = std::max(Need, std::max(2 * Period, Confirm));
	return Need;
}

// How loud a channel sounds at a tick, 0 to 1
double Level(const Recording &R, int Channel, int Tick)
{
	const ChannelTick &t = R.Ticks[Channel][Tick];
	return t.Sounding() ? std::min(1.0, t.Volume / (Channel == CH_FDS ? 32.0 : 15.0)) : 0.0;
}

// Whether a channel plays the same at two ticks but for a note that starts
// or ends a tick sooner or later in one of them
bool Shifted(const Recording &R, int Channel, int A, int B)
{
	const int Last = R.Length - 1;
	const int a = Coarse(R, Channel, A), b = Coarse(R, Channel, B);
	return (a == Coarse(R, Channel, std::max(0, B - 1)) || a == Coarse(R, Channel, std::min(Last, B + 1))) &&
		(b == Coarse(R, Channel, std::max(0, A - 1)) || b == Coarse(R, Channel, std::min(Last, A + 1)));
}

// How differently a channel sounds at two ticks: 0 the same, up to its level
// for another note (or noise, or sample) or a note against silence; a
// louder or softer note, or another duty, counts by how much it changes, a
// note a tick longer or shorter not at all, and a hardware sweep that steps
// a little sooner or later (its clock runs on its own) a quarter
double Difference(const Recording &R, int Channel, int A, int B)
{
	const ChannelTick &a = R.Ticks[Channel][A], &b = R.Ticks[Channel][B];
	const double La = Level(R, Channel, A), Lb = Level(R, Channel, B);
	if (!a.Sounding() || !b.Sounding())
		return Shifted(R, Channel, A, B) ? 0 : std::max(La, Lb);
	// The noise's mode and the DPCM's rate are what it plays
	const bool Same = Coarse(R, Channel, A) == Coarse(R, Channel, B) &&
		(!DrumChannel(Channel) || a.Timbre == b.Timbre);
	if (!Same && (a.Flags & TICK_SWEEP) && (b.Flags & TICK_SWEEP) && std::fabs(a.Pitch - b.Pitch) <= 2.5)
		return 0.25 * std::max(La, Lb) + std::fabs(La - Lb);
	if (!Same)
		return Shifted(R, Channel, A, B) ? 0 : std::max(La, Lb);
	double d = std::fabs(La - Lb);
	if (a.Timbre != b.Timbre || a.Wave != b.Wave)
		d += 0.3 * std::min(La, Lb);
	return d;
}

// Whether a channel's sound at tick B is a note that has been sounding
// since before tick From, without a new attack: the tail of a note from the
// time through before, which goes on over the loop's start
bool TailFrom(const Recording &R, int Channel, int B, int From)
{
	if (From < 1)
		return false;
	const int v = Coarse(R, Channel, B);
	for (int k = B; k >= From - 1; --k) {
		const ChannelTick &t = R.Ticks[Channel][k];
		if (!t.Sounding() || Coarse(R, Channel, k) != v || (k >= From && (t.Flags & TICK_TRIGGER)))
			return false;
	}
	return true;
}

}	// namespace

// A loop's first time through can differ from the later ones in ways too
// slight to hear: a quiet part's tails, a duty on a quiet pulse. Going back
// from where it repeats exactly, a tick passes as the same as the one a
// period later while what differs is quiet next to the music around it (a
// channel at 3 or under, or a duty at a middling volume, against the rest);
// the first tick that differs more ends it. Before a channel's first note,
// a note of the time through before ringing on over the loop's start is the
// same: the track looping there plays it the same way.
int AudibleLoopStart(const Recording &R, int Start, int Period, bool MelodicOnly)
{
	if (Start <= 0 || Period <= 0 || Start + Period > R.Length)
		return std::max(0, Start);
	std::vector<int> FirstSound(CH_COUNT, R.Length);
	for (size_t i = 0; i < R.Channels.size(); ++i) {
		const int c = R.Channels[i];
		for (int t = 0; t < Start && FirstSound[c] == R.Length; ++t)
			if (R.Ticks[c][t].Sounding())
				FirstSound[c] = t;
	}
	int S = Start;
	for (int t = Start - 1; t >= 0; --t) {
		double D = 0, Music = 0;
		for (size_t i = 0; i < R.Channels.size(); ++i) {
			const int c = R.Channels[i];
			if (MelodicOnly && DrumChannel(c))
				continue;
			const double Level2 = std::max(Level(R, c, t), Level(R, c, t + Period));
			Music += Level2;
			if (t < FirstSound[c] && R.Ticks[c][t + Period].Sounding() && TailFrom(R, c, t + Period, Period))
				continue;
			D += Difference(R, c, t, t + Period);
		}
		if (D > std::max(0.2, 0.12 * Music))
			break;
		S = t;
	}
	return S;
}

// A loop whose length isn't a whole number of ticks (a tempo that runs a
// fraction of a tick over each time round, a vibrato or tremolo on a clock
// of its own) never repeats tick for tick: here the notes repeat, a tick
// either way
bool FindLoopApprox(const Recording &R, int Ticks, int MinPeriod, int Margin, int &Start, int &Period, int Confirm)
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
	for (int First = MinPeriod; First <= From; ++First) {
		int Misses = 0;
		for (int t = From; t < Ticks && Misses <= Allowed; t += 2)
			for (size_t i = 0; i < Channels.size() && Misses <= Allowed; ++i)
				if (!Near(R, Channels[i], t - First, t, Ticks))
					++Misses;
		if (Misses > Allowed)
			continue;
		// A tick either way also matches the period a tick longer: that one
		// when its notes fall on the very same ticks clearly more often (a
		// loop a whole number of ticks long, its notes off by a tick now and
		// then), rather than about as often (a loop between two lengths)
		int P = First;
		auto Same = [&](int Q) {
			int n = 0;
			for (int t = From; t < Ticks; t += 2)
				for (size_t i = 0; i < Channels.size(); ++i)
					n += Coarse(R, Channels[i], t - Q) == Coarse(R, Channels[i], t);
			return n;
		};
		const int AtFirst = Same(First);
		for (int Q = First + 1; Q <= First + 2 && Q <= From; ++Q)
			if (Same(Q) * 50 > AtFirst * 51) {
				P = Q;
				break;
			}
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
		if (Ticks - P - S >= RepeatNeeded(P, Margin, Confirm)) {
			Start = S;
			Period = P;
			return true;
		}
	}
	return false;
}

bool FindLoop(const std::vector<uint64_t> &F, int MinPeriod, int Margin, int &Start, int &Period, int Confirm)
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
		if (N - P - S >= RepeatNeeded(P, Margin, Confirm)) {
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
	const int Confirm = std::max(1, int(Options.ConfirmSeconds * Out.TickRate));
	std::vector<uint64_t> Prints, MelodicPrints;
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
		MelodicPrints.push_back(Fingerprint(Out, Tick, PARTS_MELODIC));

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

		// A loop, once it has gone on repeating long enough to be sure of
		if ((Tick + 1) % CheckEvery == 0) {
			int Start = 0, Period = 0;
			if (FindLoop(Prints, MinLoop, Margin, Start, Period, Confirm)) {
				Out.LoopStart = Start;
				Out.LoopLength = Period;
				break;
			}
			// Now and then, a loop that repeats only a tick either way; one a
			// tick or two longer that repeats exactly, though not yet as often,
			// is the same loop
			if (Tick + 1 >= NextApprox) {
				NextApprox *= 2;
				if (FindLoopApprox(Out, Tick + 1, MinLoop, Margin, Start, Period, Confirm)) {
					for (int Q = Period; Q <= Period + 2; ++Q) {
						const int S = ExactStart(Prints, Q, Margin);
						if (S >= 0) {
							Start = S;
							Period = Q;
							break;
						}
					}
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

	// Played to the end without one: the melodic channels' loop, heard often
	// enough, when the drums repeat on a cycle of their own; else a loop
	// heard twice, as long a one as the recording has room for
	if (Out.LoopStart < 0 && !Out.EndsInSilence && LastSound >= 0) {
		int Start = 0, Period = 0;
		if (FindLoop(MelodicPrints, MinLoop, Margin, Start, Period, Confirm)) {
			bool Drums = false;
			for (size_t i = 0; i < Out.Channels.size() && !Drums; ++i)
				if (DrumChannel(Out.Channels[i]))
					for (int t = Start; t < Start + Period && !Drums; ++t)
						Drums = Out.Ticks[Out.Channels[i]][t].Sounding();
			if (Drums) {
				Out.LoopStart = Start;
				Out.LoopLength = Period;
				Out.DrumsApart = true;
				std::vector<uint64_t> DrumPrints(Out.Length);
				for (int t = 0; t < Out.Length; ++t)
					DrumPrints[t] = Fingerprint(Out, t, PARTS_DRUMS);
				int DrumStart = 0, DrumPeriod = 0;
				if (FindLoop(DrumPrints, MinLoop, Margin, DrumStart, DrumPeriod))
					Out.DrumLoop = DrumPeriod;
			}
		}
		if (Out.LoopStart < 0 && (FindLoop(Prints, MinLoop, Margin, Start, Period) ||
			FindLoopApprox(Out, Out.Length, MinLoop, Margin, Start, Period))) {
			Out.LoopStart = Start;
			Out.LoopLength = Period;
		}
	}
	// The loop starts where it can first be heard to repeat: from the start,
	// or a second or more before it repeats exactly (moved a moment, it would
	// only leave the bar line it found)
	if (Out.LoopStart > 0) {
		const int Heard = AudibleLoopStart(Out, Out.LoopStart, Out.LoopLength, Out.DrumsApart);
		if (Heard <= 2 || Out.LoopStart - Heard >= int(Out.TickRate))
			Out.LoopStart = Heard;
	}
	if (Out.DrumsApart) {
		char Words[200];
		if (Out.DrumLoop > 0)
			std::snprintf(Words, sizeof(Words), "the drums repeat every %.1f seconds, on a cycle of their own beside the music's loop "
				"of %.1f seconds; the module's loop starts them over", Out.DrumLoop / Out.TickRate, Out.LoopLength / Out.TickRate);
		else
			std::snprintf(Words, sizeof(Words), "the drums don't repeat with the music's loop of %.1f seconds; the module's loop starts "
				"them over", Out.LoopLength / Out.TickRate);
		Out.Problems.push_back(Words);
	}
	if (Overruns > 0)
		Out.Problems.push_back("the player's PLAY routine ran past its frame " + std::to_string(Overruns) + " times");
	if (Out.LoopStart < 0 && !Out.EndsInSilence && Out.Length >= MaxTicks)
		Out.Problems.push_back("no loop was found in " + std::to_string(int(Options.MaxSeconds)) + " seconds");

	// Whatever was recorded past the loop's second time round isn't needed
	if (Out.LoopStart >= 0) {
		// A loop that holds one sound forever is an ending
		const std::vector<uint64_t> &P = Out.DrumsApart ? MelodicPrints : Prints;
		bool Constant = true;
		for (int t = Out.LoopStart + 1; t < Out.LoopStart + Out.LoopLength && Constant; ++t)
			Constant = P[t] == P[Out.LoopStart];
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
	Out.LastSound = std::min(LastSound, Out.Length - 1);
	return true;
}

}	// namespace nsf

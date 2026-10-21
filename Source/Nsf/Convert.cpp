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

#include "Convert.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>

namespace nsf {

namespace {

const double PI = 3.14159265358979323846;
const int MAX_MACRO = 253;			// MAX_SEQUENCE_ITEMS
const int MAX_FRAMES = 128;
const int MAX_ROWS = 256;
const int MAX_PATTERNS = 128;
const int SHAPE_TICKS = 252;		// the longest stretch of a note that shapes its instrument

// What a channel is, for how its notes are read and written
enum chan_kind { K_PULSE, K_TRIANGLE, K_NOISE, K_DPCM, K_SAW, K_N163, K_FDS, K_VRC7 };

int KindOf(int Channel)
{
	switch (Channel) {
		case CH_TRIANGLE: return K_TRIANGLE;
		case CH_NOISE: return K_NOISE;
		case CH_DPCM: return K_DPCM;
		case CH_VRC6_SAW: return K_SAW;
		case CH_FDS: return K_FDS;
		default:
			if (Channel >= CH_N163_1 && Channel <= CH_N163_8) return K_N163;
			if (Channel >= CH_VRC7_1 && Channel <= CH_VRC7_6) return K_VRC7;
			return K_PULSE;
	}
}

// Channels whose pitch FamiTracker moves in period units (a pitch macro's
// steps), larger periods being lower
bool PeriodChannel(int Kind)
{
	return Kind == K_PULSE || Kind == K_TRIANGLE || Kind == K_SAW;
}

int Round(double x) { return int(std::floor(x + 0.5)); }

// FamiTracker's volume for an envelope value and the volume column
int FtVolume(int Env, int Column, int Max)
{
	int v = Env * Column / 15;
	if (Env > 0 && Column > 0 && v == 0)
		v = 1;
	return std::min(v, Max);
}

// A note as the channel played it, in ticks
struct Note {
	int Channel;
	int Start;			// its first tick
	int SoundEnd;		// the first tick after it that is silent or another note's
	int Next;			// the next note's first tick on the channel, or the end
	int Base;			// FamiTracker note (C-0 = 0); noise 0-15; DPCM: the key's index
	int Loudest;
	std::vector<int> Vol, Duty, Semi, Dev;	// a value per tick from the start: volume, duty, semitones from Base, period offset
	int Arp;			// a 0xy effect's parameter, 0 for none
	int Sweep;			// $4001 when the hardware sweep is on, -1
	int Wave;			// N163 or FDS wave, VRC7 custom patch (index into the recording's lists), -1
	int Patch;			// VRC7: 1-15 built in, 0 custom
	int ModRate, ModDepth, ModTable;	// FDS
	int WaveSize;		// N163, in samples
	int Key;			// DPCM: sample, rate and loop together; the key's index
	int ConstDuty;		// a duty that holds for the whole note, set with Vxx instead of a macro; -1
	// Decided later
	int Inst, Column;
	int Row, Delay;
	bool Keep;
	bool EndNeeded;		// the sound stops before the next note: a halt or release goes at EndRow
	int EndRow, EndDelay;
	bool FadesOut;		// its envelope ends in silence before the next note
	std::vector<int> Tail;	// how it fades after being let go: a release, cut from Vol
	int TailStart;		// the tick (from the start) where the release begins, -1 for none
	bool Released;		// a release (===) was written for it
	Note() : Channel(0), Start(0), SoundEnd(0), Next(0), Base(0), Loudest(0), Arp(0), Sweep(-1), Wave(-1), Patch(0),
		ModRate(0), ModDepth(0), ModTable(0), WaveSize(32), Key(-1), ConstDuty(-1), Inst(-1), Column(15), Row(0), Delay(0), Keep(true),
		EndNeeded(false), EndRow(0), EndDelay(0), FadesOut(false), TailStart(-1), Released(false) {}
	int Length() const { return SoundEnd - Start; }
};

// ---------------------------------------------------------------------------
// Notes from the ticks
// ---------------------------------------------------------------------------

// How often the rounded pitch changes in [From, To): flickers are arpeggios
int PitchChanges(const std::vector<ChannelTick> &T, int From, int To)
{
	int n = 0;
	for (int t = std::max(From, 1); t < To && t < int(T.size()); ++t)
		if (T[t].Sounding() && T[t - 1].Sounding() && Round(T[t].Pitch) != Round(T[t - 1].Pitch))
			++n;
	return n;
}

// A step to a new pitch that holds: not a flicker (an arpeggio), a wobble
// (vibrato) or a glide
bool PitchStep(const std::vector<ChannelTick> &T, int t, int Ticks)
{
	const double d = T[t].Pitch - T[t - 1].Pitch;
	if (std::fabs(d) < 0.75)
		return false;
	if (t + 1 >= Ticks || !T[t + 1].Sounding() || std::fabs(T[t + 1].Pitch - T[t].Pitch) > 0.5)
		return false;
	if (t < 2 || !T[t - 2].Sounding() || std::fabs(T[t - 1].Pitch - T[t - 2].Pitch) > 0.5)
		return false;
	return PitchChanges(T, t - 6, t + 7) <= 2;
}

// A step to a new pitch on a row that then holds: a note of its own, even
// without an attack (legato notes, an arpeggio as slow as the rows). U is
// the row's length in ticks and Phase the tick of row 0; none yet when U is 0
bool GridStep(const std::vector<ChannelTick> &T, int t, int Ticks, double U, double Phase)
{
	if (U <= 0 || t < 2)
		return false;
	if (std::fabs(T[t].Pitch - T[t - 1].Pitch) < 0.75)
		return false;
	const double x = (t - Phase) / U;
	if (std::fabs((x - Round(x)) * U) > 0.51)
		return false;
	// The new pitch holds for three ticks (two on rows shorter than that)...
	const int Hold = std::min(3, std::max(2, int(U)));
	for (int k = t; k < t + Hold; ++k)
		if (k >= Ticks || !T[k].Sounding() || std::fabs(T[k].Pitch - T[t].Pitch) > 0.5)
			return false;
	// ...and the one before it held for two
	for (int k = t - 2; k < t; ++k)
		if (!T[k].Sounding() || std::fabs(T[k].Pitch - T[t - 1].Pitch) > 0.5)
			return false;
	return true;
}

// In a chord played as a fast arpeggio, a row where notes come in that
// weren't in it just before: the next chord, a note of its own (its
// arpeggio macro then fits the chord, and is shared with chords like it)
bool ChordChange(const std::vector<ChannelTick> &T, int t, int Ticks, double U, double Phase)
{
	if (U <= 0 || t < 2)
		return false;
	const double x = (t - Phase) / U;
	if (std::fabs((x - Round(x)) * U) > 0.51)
		return false;
	const int Row = std::max(2, Round(U));
	const int Window = std::max(2 * Row, 8);
	if (t - Window < 0 || t + Row > Ticks)
		return false;
	if (PitchChanges(T, t - Window, t) < 3)
		return false;
	std::set<int> Before;
	for (int k = t - Window; k < t; ++k) {
		if (!T[k].Sounding())
			return false;
		Before.insert(Round(T[k].Pitch));
	}
	if (Before.size() < 2)
		return false;
	for (int k = t; k < t + Row && T[k].Sounding(); ++k)
		if (!Before.count(Round(T[k].Pitch)))
			return true;
	return false;
}

std::vector<Note> FindNotes(const Recording &R, int Channel, int Ticks, double U = 0, double Phase = 0)
{
	const std::vector<ChannelTick> &T = R.Ticks[Channel];
	const int Kind = KindOf(Channel);
	std::vector<Note> Notes;

	// Triggers only tell notes apart on channels that don't get one every tick
	int Sounding = 0, Triggered = 0;
	for (int t = 0; t < Ticks; ++t) {
		if (T[t].Sounding()) {
			++Sounding;
			if (T[t].Flags & TICK_TRIGGER)
				++Triggered;
		}
	}
	const bool UseTriggers = Kind == K_DPCM || Kind == K_VRC7 || (Sounding > 0 && Triggered * 10 < Sounding * 6);

	// Where notes start: after silence, on a trigger that changes something,
	// a fresh attack (the volume jumping up, unless it's swelling), a step
	// to a new pitch that holds
	std::vector<char> Starts(Ticks, 0);
	int LastStart = -100;
	for (int t = 0; t < Ticks; ++t) {
		const ChannelTick &k = T[t];
		if (!k.Sounding())
			continue;
		if (t == 0 || !T[t - 1].Sounding()) {
			Starts[t] = 1;
			LastStart = t;
			continue;
		}
		// The first ticks of a note can bend or chirp: that's its attack
		const bool InAttack = t - LastStart < 3;
		const ChannelTick &p = T[t - 1];
		const int dv = int(k.Volume) - int(p.Volume);
		const bool Trigger = UseTriggers && (k.Flags & TICK_TRIGGER);
		bool New = false, Attack = false;
		if (Kind == K_DPCM || Kind == K_VRC7)
			New = Trigger || (Kind == K_VRC7 && (PitchStep(T, t, Ticks) || GridStep(T, t, Ticks, U, Phase)));
		else if (Kind == K_NOISE)
			New = dv >= 3 || (Trigger && dv >= 0);
		else {
			bool Swelling = t >= 3 && T[t - 2].Sounding() && T[t - 3].Sounding() && p.Volume > T[t - 2].Volume &&
				T[t - 2].Volume > T[t - 3].Volume;
			// An attack that blooms: rising on one pitch since the note began a
			// tick or two ago (a grace note before it changes pitch)
			if (InAttack && LastStart >= 0) {
				bool Rising = true;
				for (int r = LastStart + 1; r <= t && Rising; ++r)
					Rising = T[r].Volume > T[r - 1].Volume && std::fabs(T[r].Pitch - T[LastStart].Pitch) < 0.5;
				Swelling = Swelling || Rising;
			}
			Attack = dv >= 3 && !Swelling;
			New = Attack || (!InAttack && ((Trigger && (std::fabs(k.Pitch - p.Pitch) >= 0.5 || dv > 0)) || PitchStep(T, t, Ticks) ||
				GridStep(T, t, Ticks, U, Phase) || ChordChange(T, t, Ticks, U, Phase)));
		}
		if (!New)
			continue;
		// Drivers often set the new pitch a tick before the attack: the note
		// starts with its pitch
		if (Attack && t >= 2 && !Starts[t - 1] && T[t - 2].Sounding() && Round(p.Pitch) == Round(k.Pitch) &&
			Round(T[t - 2].Pitch) != Round(p.Pitch)) {
			Starts[t - 1] = 1;
			LastStart = t - 1;
		}
		else {
			Starts[t] = 1;
			LastStart = t;
		}
	}
	for (int t = 0; t < Ticks; ++t) {
		if (!Starts[t])
			continue;
		Note n;
		n.Channel = Channel;
		n.Start = t;
		Notes.push_back(n);
	}

	for (size_t i = 0; i < Notes.size(); ++i) {
		Note &n = Notes[i];
		n.Next = i + 1 < Notes.size() ? Notes[i + 1].Start : Ticks;
		int e = n.Start;
		while (e < n.Next && T[e].Sounding())
			++e;
		n.SoundEnd = e;
	}
	return Notes;
}

// ---------------------------------------------------------------------------
// The grid
// ---------------------------------------------------------------------------

// The share of the onsets' weight that lies on rows TicksPerRow apart
double Alignment(const std::vector<std::pair<int, double> > &Onsets, double u, double &Phase)
{
	// The phase from the onsets' circular mean
	double Sx = 0, Sy = 0, W = 0;
	for (size_t i = 0; i < Onsets.size(); ++i) {
		const double a = 2 * PI * Onsets[i].first / u;
		Sx += Onsets[i].second * std::cos(a);
		Sy += Onsets[i].second * std::sin(a);
		W += Onsets[i].second;
	}
	if (W <= 0)
		return 0;
	double Angle = std::atan2(Sy, Sx);
	if (Angle < 0)
		Angle += 2 * PI;
	Phase = Angle * u / (2 * PI);
	// Integer rows: starts land on them; fractional ones round to a tick
	const bool Whole = std::fabs(u - Round(u)) < 0.01;
	if (Whole)
		Phase = Round(Phase);
	// A start a tick off (a drum layered just after another, say) counts half
	const double Tolerance = Whole ? 0.01 : 0.51;
	double Aligned = 0;
	for (size_t i = 0; i < Onsets.size(); ++i) {
		const double x = (Onsets[i].first - Phase) / u;
		const double e = std::fabs((x - Round(x)) * u);
		if (e <= Tolerance)
			Aligned += Onsets[i].second;
		else if (e <= Tolerance + 1.0 && u >= 4)
			Aligned += Onsets[i].second * 0.5;
	}
	return Aligned / W;
}

// ---------------------------------------------------------------------------
// Note shapes
// ---------------------------------------------------------------------------

// A note that holds a level and then fades before the next one was let go
// (a release): its fade is moved from Vol to Tail
void FindTail(Note &n)
{
	const std::vector<int> &v = n.Vol;
	const int L = int(v.size());
	if (L < 5)
		return;
	// The level it holds: the most common one after the attack
	std::map<int, int> Count;
	for (int k = 2; k < L; ++k)
		if (v[k] > 0)
			++Count[v[k]];
	int Level = 0, Most = 0;
	for (std::map<int, int>::const_iterator it = Count.begin(); it != Count.end(); ++it)
		if (it->second > Most) {
			Most = it->second;
			Level = it->first;
		}
	if (Most < 3)
		return;
	int Last = L - 1;
	while (Last >= 0 && v[Last] != Level)
		--Last;
	if (Last < 3 || Last >= L - 1 || v[Last - 1] != Level)
		return;
	for (int k = Last + 1; k < L; ++k)
		if (v[k] >= Level || (k > Last + 1 && v[k] > v[k - 1]))
			return;
	n.TailStart = Last + 1;
	n.Tail.assign(v.begin() + Last + 1, v.end());
	const size_t Body = size_t(Last + 1);
	n.Vol.resize(Body);
	if (n.Duty.size() > Body) n.Duty.resize(Body);
	if (n.Semi.size() > Body) n.Semi.resize(Body);
	if (n.Dev.size() > Body) n.Dev.resize(Body);
}

Macro LoopedMacro(const std::vector<int> &v, int Setting);

// Fills a note's base pitch and its shape from the ticks
void ShapeNote(Note &n, const Recording &R, const SoundData &Sounds)
{
	const std::vector<ChannelTick> &T = R.Ticks[n.Channel];
	const int Kind = KindOf(n.Channel);
	const int Length = std::min(n.Length(), SHAPE_TICKS);
	const ChannelTick &First = T[n.Start];

	n.Loudest = 0;
	for (int k = 0; k < Length; ++k) {
		n.Vol.push_back(T[n.Start + k].Volume);
		n.Duty.push_back(T[n.Start + k].Timbre);
		n.Loudest = std::max(n.Loudest, int(T[n.Start + k].Volume));
	}
	// A sound that fades away before the next note ends in silence by
	// itself (a drum, a pluck); one cut off while still loud was stopped
	if (n.SoundEnd < n.Next && Length == n.Length() && Length > 0 && Kind != K_TRIANGLE && Kind != K_DPCM && Kind != K_VRC7) {
		const int Last = n.Vol.back();
		if (Length <= 4 || Last <= std::max(2, n.Loudest / 3)) {
			n.Vol.push_back(0);
			n.Duty.push_back(n.Duty.back());
			n.FadesOut = true;
		}
	}

	if (Kind == K_DPCM) {
		n.Base = 0;
		n.Wave = First.Wave;
		return;
	}
	if (Kind == K_NOISE) {
		n.Base = Round(First.Pitch);
		for (int k = 0; k < Length; ++k)
			n.Semi.push_back(Round(T[n.Start + k].Pitch) - n.Base);
		if (n.FadesOut)
			n.Semi.push_back(n.Semi.empty() ? 0 : n.Semi.back());
		return;
	}

	// The pitch it settles on: the most common one, later ticks counting more
	// (an attack can bend into it)
	std::map<int, double> Count;
	for (int k = 0; k < Length; ++k)
		Count[Round(T[n.Start + k].Pitch)] += 1.0 + double(k) / std::max(1, Length);
	int Mode = Round(First.Pitch);
	double Best = -1;
	for (std::map<int, double>::const_iterator it = Count.begin(); it != Count.end(); ++it)
		if (it->second > Best) {
			Best = it->second;
			Mode = it->first;
		}
	n.Base = Mode;

	std::vector<int> Q(Length);
	for (int k = 0; k < Length; ++k)
		Q[k] = Round(T[n.Start + k].Pitch);

	// A chord flickering a step a tick: base, +x, +y (or base, +x)
	if (Length >= 6 && Kind != K_VRC7) {
		for (int Cycle = 3; Cycle >= 2; --Cycle) {
			int Match = 0, Total = 0;
			for (int k = 0; k + Cycle < Length; ++k) {
				++Total;
				if (Q[k] == Q[k + Cycle])
					++Match;
			}
			std::set<int> Values(Q.begin(), Q.begin() + Cycle);
			if (Total > 0 && Match * 10 >= Total * 9 && int(Values.size()) == Cycle) {
				// Start the cycle on its lowest note, as FamiTracker's 0xy does
				int Low = 0;
				for (int k = 1; k < Cycle; ++k)
					if (Q[k] < Q[Low])
						Low = k;
				const int Root = Q[Low];
				const int X = Q[(Low + 1) % Cycle] - Root;
				const int Y = Cycle == 3 ? Q[(Low + 2) % Cycle] - Root : 0;
				if (X > 0 && X <= 15 && Y >= 0 && Y <= 15 && (Cycle == 2 || Y > 0)) {
					n.Base = Root;
					n.Arp = (X << 4) | Y;
				}
				break;
			}
		}
	}

	// A chord played as a slower arpeggio that loops: on its lowest note, so
	// chords of the same shape get the same macro
	if (!n.Arp && Length >= 8 && Kind != K_VRC7 && PitchChanges(T, n.Start, n.Start + std::min(Length, 32)) >= 3) {
		const Macro Cycle = LoopedMacro(Q, ARP_ABSOLUTE);
		if (Cycle.Loop >= 0) {
			int Low = Cycle.Items[Cycle.Loop];
			for (size_t k = Cycle.Loop; k < Cycle.Items.size(); ++k)
				Low = std::min(Low, Cycle.Items[k]);
			n.Base = Low;
		}
	}

	for (int k = 0; k < Length; ++k)
		n.Semi.push_back(n.Arp ? 0 : Q[k] - n.Base);
	if (n.FadesOut)
		n.Semi.push_back(n.Semi.empty() ? 0 : n.Semi.back());

	// The period's wobble around the note: vibrato, bends
	if (PeriodChannel(Kind) && !n.Arp) {
		std::vector<int> Periods;
		for (int k = 0; k < Length; ++k)
			if (Q[k] == n.Base)
				Periods.push_back(T[n.Start + k].Period);
		int Center = First.Period;
		if (!Periods.empty()) {
			std::vector<int> Sorted = Periods;
			std::sort(Sorted.begin(), Sorted.end());
			Center = Sorted[Sorted.size() / 2];
		}
		for (int k = 0; k < Length; ++k)
			n.Dev.push_back(Q[k] == n.Base ? int(T[n.Start + k].Period) - Center : 0);
		if (n.FadesOut)
			n.Dev.push_back(n.Dev.empty() ? 0 : n.Dev.back());
	}

	if (Kind == K_PULSE && n.Channel <= CH_PULSE2 && (First.Flags & TICK_SWEEP))
		n.Sweep = First.Extra;
	if (Kind == K_N163) {
		n.Wave = First.Wave;
		n.WaveSize = First.Wave < Sounds.Waves.size() ? int(Sounds.Waves[First.Wave].size()) : 32;
	}
	if (Kind == K_FDS) {
		n.Wave = First.Wave;
		n.ModRate = First.ModRate;
		n.ModDepth = First.Extra;
		n.ModTable = First.ModTable;
	}
	if (Kind == K_VRC7) {
		n.Patch = First.Timbre;
		n.Wave = First.Timbre == 0 ? First.Wave : -1;
	}
	if (Kind == K_PULSE || Kind == K_SAW || Kind == K_N163 || Kind == K_FDS)
		FindTail(n);
}

// ---------------------------------------------------------------------------
// Instruments
// ---------------------------------------------------------------------------

// The macros an instrument gets from a note, besides its volume
struct Signature {
	int Kind;
	int Chip;			// for FamiTracker: 2A03, VRC6, MMC5 (2A03 instruments), N163, FDS, VRC7
	Macro Duty, Arp, Pitch;
	int Wave, Patch, ModRate, ModDepth, ModTable, WaveSize;
	bool operator<(const Signature &o) const
	{
		if (Kind != o.Kind) return Kind < o.Kind;
		if (Chip != o.Chip) return Chip < o.Chip;
		if (Wave != o.Wave) return Wave < o.Wave;
		if (Patch != o.Patch) return Patch < o.Patch;
		if (ModRate != o.ModRate) return ModRate < o.ModRate;
		if (ModDepth != o.ModDepth) return ModDepth < o.ModDepth;
		if (ModTable != o.ModTable) return ModTable < o.ModTable;
		if (WaveSize != o.WaveSize) return WaveSize < o.WaveSize;
		if (!(Duty == o.Duty)) return Duty.Items < o.Duty.Items || (Duty.Items == o.Duty.Items && Duty.Loop < o.Duty.Loop);
		if (!(Arp == o.Arp)) return Arp.Items < o.Arp.Items || (Arp.Items == o.Arp.Items && Arp.Loop < o.Arp.Loop);
		if (!(Pitch == o.Pitch)) return Pitch.Items < o.Pitch.Items || (Pitch.Items == o.Pitch.Items && Pitch.Loop < o.Pitch.Loop);
		return false;
	}
};

int ChipOf(int Channel)
{
	if (Channel >= CH_VRC6_PULSE1 && Channel <= CH_VRC6_SAW) return INST_VRC6;
	if (Channel >= CH_N163_1 && Channel <= CH_N163_8) return INST_N163;
	if (Channel == CH_FDS) return INST_FDS;
	if (Channel >= CH_VRC7_1 && Channel <= CH_VRC7_6) return INST_VRC7;
	return INST_2A03;
}

// A sequence that repeats from some point on: the shortest loop that
// explains its last values, or the values as they are (holding the last)
Macro LoopedMacro(const std::vector<int> &v, int Setting)
{
	Macro m;
	m.Setting = Setting;
	int n = int(v.size());
	if (n == 0)
		return m;
	// Drop a tail that just repeats the last value: FamiTracker holds it
	while (n > 1 && v[n - 1] == v[n - 2])
		--n;
	if (n == 1 || n < int(v.size()) - 1) {
		m.Items.assign(v.begin(), v.begin() + std::min(n, MAX_MACRO));
		return m;
	}
	// A cycle at the end, seen at least twice
	for (int Cycle = 2; Cycle * 2 <= n && Cycle <= 64; ++Cycle) {
		int Start = n - Cycle;
		while (Start > 0 && v[Start - 1] == v[Start - 1 + Cycle])
			--Start;
		if (n - Start >= Cycle * 2) {
			m.Items.assign(v.begin(), v.begin() + std::min(Start + Cycle, MAX_MACRO));
			m.Loop = Start < MAX_MACRO ? Start : -1;
			return m;
		}
	}
	m.Items.assign(v.begin(), v.begin() + std::min(n, MAX_MACRO));
	return m;
}

// The duty macro of a note: its duty per tick, held at the end
Macro DutyMacro(const Note &n, int Kind)
{
	if (Kind == K_TRIANGLE || Kind == K_DPCM || Kind == K_FDS || Kind == K_VRC7 || Kind == K_N163)
		return Macro();
	Macro m = LoopedMacro(n.Duty, 0);
	if (m.Items.size() == 1 && m.Items[0] == 0 && Kind != K_PULSE)
		return Macro();		// noise in its normal mode, the saw's default
	return m;
}

// The arpeggio macro of a note: semitones away from its base per tick
Macro ArpMacro(const Note &n)
{
	bool Any = false;
	for (size_t k = 0; k < n.Semi.size(); ++k)
		if (n.Semi[k] != 0)
			Any = true;
	if (!Any)
		return Macro();
	Macro m = LoopedMacro(n.Semi, ARP_ABSOLUTE);
	if (m.Loop < 0) {
		// It ends where the note settles: on its base
		while (!m.Items.empty() && m.Items.back() == 0 && m.Items.size() > 1 && m.Items[m.Items.size() - 2] == 0)
			m.Items.pop_back();
	}
	return m;
}

// The pitch macro of a note: its period's steps from tick to tick (a
// vibrato loops), or none
Macro PitchMacro(const Note &n)
{
	const std::vector<int> &d = n.Dev;
	int Biggest = 0;
	for (size_t k = 0; k < d.size(); ++k)
		Biggest = std::max(Biggest, std::abs(d[k]));
	if (Biggest == 0 || d.empty())
		return Macro();
	// Steps relative to where it starts out: the first tick sets the offset
	std::vector<int> Steps(d.size());
	for (size_t k = 0; k < d.size(); ++k)
		Steps[k] = d[k] - (k > 0 ? d[k - 1] : 0);
	// A vibrato: the steps repeat from some tick on
	const int n2 = int(Steps.size());
	for (int Cycle = 2; Cycle <= 48 && Cycle * 2 <= n2; ++Cycle) {
		int Start = n2 - Cycle;
		while (Start > 0 && Steps[Start - 1] == Steps[Start - 1 + Cycle])
			--Start;
		int Sum = 0;
		for (int k = Start; k < Start + Cycle; ++k)
			Sum += Steps[k];
		if (n2 - Start >= Cycle * 2 && Sum == 0) {
			std::vector<int> Items(Steps.begin(), Steps.begin() + std::min(Start + Cycle, MAX_MACRO));
			return Macro(Items, Start < MAX_MACRO ? Start : -1);
		}
	}
	// A bend that settles: its steps, then nothing more
	int Last = n2 - 1;
	while (Last >= 0 && Steps[Last] == 0)
		--Last;
	if (Last < 0)
		return Macro();
	if (d[n2 - 1] != 0 && Last > n2 - 4)
		return Macro();		// still moving at the end: a glide, which isn't kept
	std::vector<int> Items(Steps.begin(), Steps.begin() + std::min(Last + 1, MAX_MACRO));
	for (size_t k = 0; k < Items.size(); ++k)
		Items[k] = std::max(-127, std::min(127, Items[k]));
	return Macro(Items);
}

struct Inst {
	Signature Sig;
	std::vector<int> Env;		// volume per tick, 0-15 (FDS 0-32), held at its last value
	std::vector<int> Release;	// after a note release (===), at full volume
	int Uses;
	int Channel;				// the channel it's used on most
	std::map<int, int> ChannelUses;
	Inst() : Uses(0), Channel(0) {}
};

// How well an envelope plays a note at its best volume column: the summed
// difference, with the column found; large when it can't
double FitEnvelope(const std::vector<int> &Env, const std::vector<int> &Vol, int Max, int Loudest, int &Column)
{
	double Best = 1e9;
	Column = 15;
	if (Env.empty())
		return Best;
	for (int c = std::max(1, Loudest - 1); c <= 15; ++c) {
		double Sum = 0;
		int Worst = 0;
		for (size_t k = 0; k < Vol.size(); ++k) {
			const int e = k < Env.size() ? Env[k] : Env.back();
			const int v = FtVolume(e, c, Max);
			// Silent where the note is silent, sounding where it sounds
			if ((v == 0) != (Vol[k] == 0)) {
				Worst = 99;
				break;
			}
			const int d = std::abs(v - Vol[k]);
			Sum += d;
			Worst = std::max(Worst, d);
		}
		if (Worst > 2)
			continue;
		if (Sum < Best) {
			Best = Sum;
			Column = c;
		}
	}
	return Best;
}

std::string DutyName(int Duty, int Kind)
{
	if (Kind == K_NOISE)
		return Duty ? "metallic" : "";
	if (Kind != K_PULSE)
		return "";
	static const char *const NAMES[8] = { "12%", "25%", "50%", "75%", "31%", "37%", "44%", "50%" };
	return NAMES[Duty & 7];
}

// ---------------------------------------------------------------------------
// The track's patterns
// ---------------------------------------------------------------------------

int ModuleColumn(int Channel, int Expansion)
{
	if (Channel <= CH_DPCM)
		return Channel;
	int c = 5;
	if (Expansion & EXP_VRC6) {
		if (Channel >= CH_VRC6_PULSE1 && Channel <= CH_VRC6_SAW)
			return c + (Channel - CH_VRC6_PULSE1);
		c += 3;
	}
	if (Expansion & EXP_MMC5) {
		if (Channel >= CH_MMC5_PULSE1 && Channel <= CH_MMC5_PULSE2)
			return c + (Channel - CH_MMC5_PULSE1);
		c += 2;
	}
	if (Expansion & EXP_N163) {
		if (Channel >= CH_N163_1 && Channel <= CH_N163_8)
			return c + (Channel - CH_N163_1);
		c += 8;
	}
	if (Expansion & EXP_FDS) {
		if (Channel == CH_FDS)
			return c;
		c += 1;
	}
	if (Expansion & EXP_VRC7) {
		if (Channel >= CH_VRC7_1 && Channel <= CH_VRC7_6)
			return c + (Channel - CH_VRC7_1);
	}
	return -1;
}

int ExpansionOfChannel(int Channel)
{
	if (Channel >= CH_VRC6_PULSE1 && Channel <= CH_VRC6_SAW) return EXP_VRC6;
	if (Channel >= CH_MMC5_PULSE1 && Channel <= CH_MMC5_PULSE2) return EXP_MMC5;
	if (Channel >= CH_N163_1 && Channel <= CH_N163_8) return EXP_N163;
	if (Channel == CH_FDS) return EXP_FDS;
	if (Channel >= CH_VRC7_1 && Channel <= CH_VRC7_6) return EXP_VRC7;
	return EXP_NONE;
}

// A track being converted
struct Work {
	const Recording *Rec;
	std::string Name;
	int Ticks;					// what is converted: the intro and one time through the loop
	std::vector<Note> Notes;	// all channels
	double U, Phase;			// ticks a row, the tick of row 0
	int RowsPerBeat;
	int Rows, LoopRow;
	TrackReport Report;
};

double GridTime(const Work &w, int Row) { return w.Phase + Row * w.U; }

}	// namespace

bool FindGrid(const std::vector<std::pair<int, double> > &Onsets, double &TicksPerRow, double &Phase, double &Aligned)
{
	TicksPerRow = 6;
	Phase = 0;
	Aligned = 0;
	if (Onsets.size() < 2)
		return false;
	std::vector<std::pair<double, double> > Scores;		// (u, alignment)
	double Best = 0;
	for (int i = 0; i <= 3800; ++i) {
		const double u = 2.0 + i * 0.01;
		double p = 0;
		const double a = Alignment(Onsets, u, p);
		Scores.push_back(std::make_pair(u, a));
		if (u >= 3 && a > Best)
			Best = a;
	}
	// The longest row that nearly all starts fall on
	const double Needed = std::max(0.85, Best * 0.97);
	double Chosen = 0;
	for (size_t i = 0; i < Scores.size(); ++i)
		if (Scores[i].first >= 3 && Scores[i].second >= Needed)
			Chosen = Scores[i].first;
	if (Chosen == 0) {
		// Nothing lines up that well: the longest of the best
		for (size_t i = 0; i < Scores.size(); ++i)
			if (Scores[i].first >= 3 && Scores[i].second >= Best * 0.9)
				Chosen = Scores[i].first;
		if (Chosen == 0)
			return false;
	}
	// Of the lengths around it that score as well, the middle one; a whole
	// number of ticks when one is among them
	double Low = Chosen, High = Chosen;
	double Score = 0;
	for (size_t i = 0; i < Scores.size(); ++i)
		if (Scores[i].first == Chosen)
			Score = Scores[i].second;
	for (size_t i = 0; i < Scores.size(); ++i) {
		if (std::fabs(Scores[i].first - Chosen) < 0.3 && Scores[i].second >= std::min(Needed, Score)) {
			Low = std::min(Low, Scores[i].first);
			High = std::max(High, Scores[i].first);
		}
	}
	TicksPerRow = (Low + High) / 2;
	if (Round(TicksPerRow) >= Low - 0.001 && Round(TicksPerRow) <= High + 0.001)
		TicksPerRow = Round(TicksPerRow);
	else {
		// A fraction: where the starts lie closest to the rows on average
		double Best = 1e9;
		for (double u = Low; u <= High + 1e-9; u += 0.01) {
			double p = 0;
			Alignment(Onsets, u, p);
			double Sum = 0, W = 0;
			for (size_t i = 0; i < Onsets.size(); ++i) {
				const double x = (Onsets[i].first - p) / u;
				Sum += Onsets[i].second * std::fabs(x - Round(x)) * u;
				W += Onsets[i].second;
			}
			const double Error = W > 0 ? Sum / W : 0;
			if (Error < Best - 1e-9) {
				Best = Error;
				TicksPerRow = u;
			}
		}
	}
	Aligned = Alignment(Onsets, TicksPerRow, Phase);
	return true;
}

void SpeedTempo(double TicksPerRow, double FrameRate, int &Speed, int &Tempo)
{
	// FamiTracker plays a row every 2.5 * FrameRate * Speed / Tempo ticks
	const double k = 2.5 * FrameRate;
	Speed = std::max(1, std::min(31, Round(TicksPerRow)));
	Tempo = Round(k);
	double Best = 1e9;
	for (int s = 1; s <= 31; ++s) {
		const int t = Round(k * s / TicksPerRow);
		if (t < 32 || t > 255)
			continue;
		// FamiTracker divides tempo * 24 by speed
		const double Actual = (60.0 * FrameRate) / double((t * 24) / s);
		const double Error = std::fabs(Actual - TicksPerRow) + std::fabs(t - k) * 1e-5 + std::abs(s - Round(TicksPerRow)) * 1e-4;
		if (Error < Best) {
			Best = Error;
			Speed = s;
			Tempo = t;
		}
	}
}

bool ConvertTracks(const NsfFile &File, const std::vector<TrackSource> &Sources, const ConvertOptions &Options, Module &Out,
	ConvertReport &Report, std::string &Error)
{
	Out = Module();
	Report = ConvertReport();
	if (Sources.empty()) {
		Error = "no tracks to convert";
		return false;
	}

	// The expansion chip: FamiTracker 0.4.6 has room for one, so the one
	// that plays the most
	std::map<int, long> ChipUse;
	for (size_t s = 0; s < Sources.size(); ++s) {
		const Recording &R = *Sources[s].Rec;
		for (size_t i = 0; i < R.Channels.size(); ++i) {
			const int c = R.Channels[i];
			const int Exp = ExpansionOfChannel(c);
			if (Exp == EXP_NONE)
				continue;
			for (int t = 0; t < R.Length; ++t)
				if (R.Ticks[c][t].Sounding())
					++ChipUse[Exp];
		}
	}
	int Expansion = EXP_NONE;
	long Most = 0;
	for (std::map<int, long>::const_iterator it = ChipUse.begin(); it != ChipUse.end(); ++it) {
		if (it->second > Most) {
			Most = it->second;
			Expansion = it->first;
		}
	}
	for (std::map<int, long>::const_iterator it = ChipUse.begin(); it != ChipUse.end(); ++it) {
		if (it->first != Expansion && it->second > 0) {
			static const std::map<int, std::string> NAMES = { { EXP_VRC6, "VRC6" }, { EXP_VRC7, "VRC7" }, { EXP_FDS, "FDS" },
				{ EXP_MMC5, "MMC5" }, { EXP_N163, "N163" } };
			Report.Problems.push_back("FamiTracker plays one expansion chip at a time: the " + NAMES.at(it->first) +
				" parts were left out");
		}
	}
	if (File.Chips & CHIP_5B)
		Report.Problems.push_back("FamiTracker 0.4.6 can't play the Sunsoft 5B: its parts were left out");
	Out.Expansion = Expansion;
	Report.Expansion = Expansion;
	Out.Title = File.Title;
	Out.Author = File.Artist;
	Out.Copyright = File.Copyright;
	Out.Machine = File.Pal && !File.Dual ? 1 : 0;

	// N163: as many channels as any track uses
	int N163Channels = 0;
	if (Expansion == EXP_N163) {
		for (size_t s = 0; s < Sources.size(); ++s) {
			const Recording &R = *Sources[s].Rec;
			for (int c = CH_N163_1; c <= CH_N163_8; ++c)
				if (R.Has(c))
					for (int t = 0; t < R.Length; ++t)
						if (R.Ticks[c][t].Sounding())
							N163Channels = std::max(N163Channels, c - CH_N163_1 + 1);
		}
		N163Channels = std::max(1, N163Channels);
	}
	Out.N163Channels = N163Channels;

	// The engine's speed: the NSF's, when it isn't the machine's own
	const double Rate = Sources[0].Rec->TickRate;
	const double Default = Out.Machine ? 50.0 : 60.0;
	if (std::fabs(Rate - Default) > 1.0)
		Out.FrameRate = Round(Rate);
	const double FrameRate = Out.FrameRate ? Out.FrameRate : Default;

	SoundData Global;		// every track's waves, modulation tables and patches
	std::vector<Work> Works(Sources.size());
	for (size_t s = 0; s < Sources.size(); ++s) {
		Work &w = Works[s];
		const Recording &R = *Sources[s].Rec;
		w.Rec = &R;
		w.Name = Sources[s].Name;
		w.Report.Name = w.Name;
		w.Report.Problems = R.Problems;
		w.Ticks = R.LoopStart >= 0 ? std::min(R.Length, R.LoopStart + R.LoopLength) : R.Length;

		// The channels the module has
		std::vector<int> Channels;
		for (size_t i = 0; i < R.Channels.size(); ++i) {
			const int c = R.Channels[i];
			const int Exp = ExpansionOfChannel(c);
			if (Exp != EXP_NONE && Exp != Expansion)
				continue;
			if (Exp == EXP_N163 && c - CH_N163_1 >= N163Channels)
				continue;
			Channels.push_back(c);
		}

		// The rows, from where the notes start
		std::vector<std::pair<int, double> > Onsets;
		for (size_t i = 0; i < Channels.size(); ++i) {
			const int Kind = KindOf(Channels[i]);
			const std::vector<Note> Found = FindNotes(R, Channels[i], w.Ticks);
			std::vector<char> Started(w.Ticks, 0);
			for (size_t k = 0; k < Found.size(); ++k) {
				const double Weight = Found[k].Length() >= 3 || Kind == K_NOISE || Kind == K_DPCM ? 1.0 : 0.4;
				Onsets.push_back(std::make_pair(Found[k].Start, Weight));
				Started[Found[k].Start] = 1;
			}
			// Legato notes start without an attack: a step to a pitch that holds
			if (Kind == K_NOISE || Kind == K_DPCM)
				continue;
			const std::vector<ChannelTick> &T = R.Ticks[Channels[i]];
			for (int t = 2; t + 3 <= w.Ticks; ++t) {
				if (Started[t] || !T[t].Sounding() || std::fabs(T[t].Pitch - T[t - 1].Pitch) < 0.75)
					continue;
				bool Holds = T[t - 1].Sounding() && T[t - 2].Sounding() && std::fabs(T[t - 1].Pitch - T[t - 2].Pitch) <= 0.5;
				for (int k = t + 1; k < t + 3 && Holds; ++k)
					Holds = T[k].Sounding() && std::fabs(T[k].Pitch - T[t].Pitch) <= 0.5;
				if (Holds)
					Onsets.push_back(std::make_pair(t, 0.6));
			}
		}
		double U = 6, Phase = 0, Aligned = 0;
		FindGrid(Onsets, U, Phase, Aligned);

		// Rows as beats: aim for RowsPerBeat rows a beat at a tempo between
		// 70 and 200 BPM, splitting rows where they're too long
		int Beat = 1;
		for (int b = 1; b <= 8; b *= 2) {
			const double Bpm = 60.0 * FrameRate / (U * b);
			if (Bpm <= 200) {
				Beat = b;
				break;
			}
		}
		int Split = 1;
		while (Beat * Split < Options.RowsPerBeat && U / (Split * 2) >= 2.0)
			Split *= 2;
		w.U = U / Split;
		w.RowsPerBeat = Beat * Split;
		w.Phase = Phase;
		while (w.Phase >= w.U)
			w.Phase -= w.U;
		w.Report.TicksPerRow = w.U;
		w.Report.RowsPerBeat = w.RowsPerBeat;
		w.Report.Bpm = 60.0 * FrameRate / (w.U * w.RowsPerBeat);
		SpeedTempo(w.U, FrameRate, w.Report.Speed, w.Report.Tempo);

		// The notes, now that the rows are known: a held pitch stepping on a
		// row is a note of its own
		for (size_t i = 0; i < Channels.size(); ++i) {
			const int c = Channels[i];
			std::vector<Note> Found = FindNotes(R, c, w.Ticks, w.U, w.Phase);
			for (size_t k = 0; k < Found.size(); ++k) {
				Note &n = Found[k];
				ShapeNote(n, R, R.Sounds);
				// Waves, modulation tables and patches by their contents, the same in every track
				const int Kind = KindOf(c);
				if ((Kind == K_N163 || Kind == K_FDS) && n.Wave >= 0 && n.Wave < int(R.Sounds.Waves.size()))
					n.Wave = Global.AddWave(R.Sounds.Waves[n.Wave]);
				if (Kind == K_FDS && n.ModTable < int(R.Sounds.ModTables.size()))
					n.ModTable = Global.AddModTable(R.Sounds.ModTables[n.ModTable]);
				if (Kind == K_VRC7 && n.Patch == 0 && n.Wave >= 0 && n.Wave < int(R.Sounds.Patches.size()))
					n.Wave = Global.AddPatch(R.Sounds.Patches[n.Wave]);
			}
			w.Notes.insert(w.Notes.end(), Found.begin(), Found.end());
		}

		// Notes on rows
		for (size_t k = 0; k < w.Notes.size(); ++k) {
			Note &n = w.Notes[k];
			const double x = (n.Start - w.Phase) / w.U;
			int Row = Round(x);
			double Off = n.Start - GridTime(w, Row);
			if (Off < -1.0 && Row > 0) {
				--Row;
				Off = n.Start - GridTime(w, Row);
			}
			n.Row = std::max(0, Row);
			n.Delay = Off >= 1.5 ? Round(Off) : 0;
			if (n.Delay >= w.U)
				n.Delay = 0;
			if (n.Delay > 0)
				++w.Report.Delayed;
		}
		// One note a row on each channel: the one that lasts
		std::sort(w.Notes.begin(), w.Notes.end(), [](const Note &a, const Note &b) {
			return a.Channel != b.Channel ? a.Channel < b.Channel : a.Start < b.Start;
		});
		for (size_t k = 0; k + 1 < w.Notes.size(); ++k) {
			Note &a = w.Notes[k], &b = w.Notes[k + 1];
			if (a.Channel != b.Channel || a.Row != b.Row || !a.Keep)
				continue;
			if (a.Length() > b.Length()) {
				b.Keep = false;
				// The kept note sounds on through the dropped one
				a.SoundEnd = std::max(a.SoundEnd, b.SoundEnd);
				a.Next = b.Next;
			}
			else
				a.Keep = false;
			++w.Report.Dropped;
		}

		// How long the track is in rows, and where it loops
		const int LastTick = w.Ticks;
		w.Rows = std::max(1, Round((LastTick - w.Phase) / w.U));
		w.LoopRow = R.LoopStart >= 0 ? std::max(0, Round((R.LoopStart - w.Phase) / w.U)) : -1;
		if (w.LoopRow >= w.Rows)
			w.LoopRow = -1;
		w.Report.Rows = w.Rows;
		w.Report.LoopRow = w.LoopRow;
	}

	// Instruments, shared by all tracks: notes with the same macros and a
	// volume envelope that fits them
	std::vector<Inst> Insts;
	std::vector<Note *> All;
	for (size_t s = 0; s < Works.size(); ++s)
		for (size_t k = 0; k < Works[s].Notes.size(); ++k)
			if (Works[s].Notes[k].Keep)
				All.push_back(&Works[s].Notes[k]);
	// Long, loud notes show an instrument best
	std::stable_sort(All.begin(), All.end(), [](const Note *a, const Note *b) {
		if ((a->Loudest == 15) != (b->Loudest == 15))
			return a->Loudest == 15;
		return a->Length() > b->Length();
	});

	std::vector<int> DpcmKeys;		// sample + rate * 1024 + loop * 65536, by key index
	for (size_t i = 0; i < All.size(); ++i) {
		Note &n = *All[i];
		const int Kind = KindOf(n.Channel);
		const int Max = Kind == K_FDS ? 32 : 15;

		if (Kind == K_DPCM)
			continue;	// keyed below

		Signature Sig;
		Sig.Kind = Kind;
		Sig.Chip = ChipOf(n.Channel);
		Sig.Duty = DutyMacro(n, Kind);
		// A duty that holds for the whole note is the channel's (Vxx), so notes
		// that differ only in it share an instrument
		if ((Kind == K_PULSE || Kind == K_NOISE || Kind == K_SAW) && !n.Duty.empty() &&
			std::count(n.Duty.begin(), n.Duty.end(), n.Duty[0]) == int(n.Duty.size())) {
			n.ConstDuty = n.Duty[0];
			Sig.Duty = Macro();
		}
		Sig.Arp = ArpMacro(n);
		Sig.Pitch = PitchMacro(n);
		Sig.Wave = n.Wave;
		Sig.Patch = Kind == K_VRC7 ? n.Patch : 0;
		Sig.ModRate = n.ModRate;
		Sig.ModDepth = n.ModDepth;
		Sig.ModTable = Kind == K_FDS ? n.ModTable : 0;
		Sig.WaveSize = Kind == K_N163 ? n.WaveSize : 0;

		int Found = -1, Column = 15;
		double BestFit = 1e9;
		for (size_t k = 0; k < Insts.size(); ++k) {
			const Inst &I = Insts[k];
			if (I.Sig < Sig || Sig < I.Sig)
				continue;
			if (Kind == K_TRIANGLE || Kind == K_VRC7) {
				Found = int(k);
				break;
			}
			int c = 15;
			double Fit = FitEnvelope(I.Env, n.Vol, Max, n.Loudest, c);
			if (Fit > 0.25 * n.Vol.size() + 0.5)
				continue;
			// A release has to match the instrument's, if it has one
			if (!n.Tail.empty() && !I.Release.empty()) {
				double TailFit = 0;
				int Worst = 0;
				for (size_t t = 0; t < n.Tail.size(); ++t) {
					const int e = t < I.Release.size() ? I.Release[t] : I.Release.back();
					const int d = std::abs(FtVolume(e, c, Max) - n.Tail[t]);
					TailFit += d;
					Worst = std::max(Worst, d);
				}
				if (Worst > 2 || TailFit > 0.5 * n.Tail.size() + 0.5)
					continue;
				Fit += TailFit;
			}
			if (Fit < BestFit) {
				BestFit = Fit;
				Found = int(k);
				Column = c;
			}
		}
		if (Found < 0) {
			Inst I;
			I.Sig = Sig;
			if (Kind == K_TRIANGLE)
				I.Env.assign(1, 15);
			else if (Kind == K_VRC7)
				I.Env.clear();
			else {
				// At full volume, so the volume column can make it softer
				const int Loudest = std::max(1, n.Loudest);
				for (size_t k = 0; k < n.Vol.size(); ++k)
					I.Env.push_back(std::min(Max, Round(double(n.Vol[k]) * (Kind == K_FDS ? 1.0 : 15.0 / std::max(Loudest, 1)))));
				if (Kind == K_FDS)
					Column = 15;
				else
					Column = Loudest;
			}
			Insts.push_back(I);
			Found = int(Insts.size()) - 1;
		}
		// The first release heard for an instrument becomes its own
		Inst &Chosen = Insts[Found];
		if (!n.Tail.empty() && Chosen.Release.empty() && Kind != K_TRIANGLE && Kind != K_VRC7) {
			const int c = Kind == K_FDS ? 15 : std::max(1, Column);
			for (size_t t = 0; t < n.Tail.size(); ++t)
				Chosen.Release.push_back(std::min(Max, Round(double(n.Tail[t]) * (Kind == K_FDS ? 1.0 : 15.0 / c))));
		}
		n.Inst = Found;
		n.Column = Kind == K_VRC7 ? std::max(1, n.Loudest) : Column;
		Insts[Found].Uses++;
		Insts[Found].ChannelUses[n.Channel]++;
	}

	// Too many: merge the least used into the closest that fits them
	// (FamiTracker has room for 64, the DPCM instrument included)
	bool HaveDpcm = false;
	for (size_t i = 0; i < All.size(); ++i)
		if (KindOf(All[i]->Channel) == K_DPCM)
			HaveDpcm = true;
	const int Room = Options.MaxInstruments - (HaveDpcm ? 1 : 0);
	std::vector<int> Map(Insts.size());
	for (size_t i = 0; i < Insts.size(); ++i)
		Map[i] = int(i);
	{
		std::vector<int> Alive;
		for (size_t i = 0; i < Insts.size(); ++i)
			Alive.push_back(int(i));
		while (int(Alive.size()) > Room) {
			// The least used goes to the most similar of the same kind
			int Victim = 0;
			for (size_t i = 1; i < Alive.size(); ++i)
				if (Insts[Alive[i]].Uses < Insts[Alive[Victim]].Uses)
					Victim = int(i);
			const Inst &V = Insts[Alive[Victim]];
			int Target = -1;
			double Best = 1e18;
			for (size_t i = 0; i < Alive.size(); ++i) {
				if (int(i) == Victim)
					continue;
				const Inst &C = Insts[Alive[i]];
				if (C.Sig.Kind != V.Sig.Kind || C.Sig.Chip != V.Sig.Chip)
					continue;
				double d = 0;
				const size_t Len = std::max(C.Env.size(), V.Env.size());
				for (size_t k = 0; k < Len; ++k) {
					const int a = C.Env.empty() ? 0 : (k < C.Env.size() ? C.Env[k] : C.Env.back());
					const int b = V.Env.empty() ? 0 : (k < V.Env.size() ? V.Env[k] : V.Env.back());
					d += std::abs(a - b);
				}
				d += (C.Sig.Duty == V.Sig.Duty ? 0 : 50) + (C.Sig.Arp == V.Sig.Arp ? 0 : 50) + (C.Sig.Pitch == V.Sig.Pitch ? 0 : 30) +
					(C.Sig.Wave == V.Sig.Wave ? 0 : 40) + (C.Sig.Patch == V.Sig.Patch ? 0 : 40);
				if (d < Best) {
					Best = d;
					Target = Alive[i];
				}
			}
			if (Target < 0) {
				// Nothing of its kind: merge into the most used of any 2A03-like kind
				Target = Alive[Victim == 0 ? 1 : 0];
			}
			Map[Alive[Victim]] = Target;
			Insts[Target].Uses += V.Uses;
			Alive.erase(Alive.begin() + Victim);
		}
		for (size_t i = 0; i < Map.size(); ++i) {
			int m = Map[i];
			while (Map[m] != m)
				m = Map[m];
			Map[i] = m;
		}
		int Merged = 0;
		for (size_t i = 0; i < Map.size(); ++i)
			if (Map[i] != int(i))
				++Merged;
		if (Merged > 0)
			Report.Problems.push_back(std::to_string(Merged) + " instruments were merged into similar ones to fit FamiTracker's 64");
	}

	// The module's instruments, in order of first use
	std::vector<int> Slot(Insts.size(), -1);
	auto Name = [&](const Inst &I) {
		const int Kind = I.Sig.Kind;
		std::string Role;
		int Channel = 0, Most = -1;
		for (std::map<int, int>::const_iterator it = I.ChannelUses.begin(); it != I.ChannelUses.end(); ++it)
			if (it->second > Most) {
				Most = it->second;
				Channel = it->first;
			}
		std::string s;
		switch (Kind) {
			case K_TRIANGLE: s = "Triangle"; break;
			case K_NOISE: s = "Noise"; break;
			case K_SAW: s = "Saw"; break;
			case K_N163: s = "N163"; break;
			case K_FDS: s = "FDS"; break;
			case K_VRC7: s = "FM"; break;
			default: s = ChannelName(Channel); break;
		}
		if (Kind == K_PULSE && !I.Sig.Duty.Items.empty())
			s += " " + DutyName(I.Sig.Duty.Items[0], Kind);
		if (Kind == K_NOISE) {
			const std::string d = I.Sig.Duty.Items.empty() ? "" : DutyName(I.Sig.Duty.Items[0], Kind);
			if (!d.empty())
				s += " " + d;
		}
		// The envelope in a word
		if (!I.Env.empty() && Kind != K_TRIANGLE) {
			const std::vector<int> &e = I.Env;
			int Peak = 0, PeakAt = 0;
			for (size_t k = 0; k < e.size(); ++k)
				if (e[k] > Peak) {
					Peak = e[k];
					PeakAt = int(k);
				}
			const int End = e.back();
			if (PeakAt >= 3)
				s += " swell";
			else if (End == 0 && e.size() <= 8)
				s += " short";
			else if (End == 0)
				s += " decay";
			else if (End * 2 < Peak)
				s += " fade";
			else
				s += " held";
		}
		if (!I.Sig.Pitch.Items.empty())
			s += I.Sig.Pitch.Loop >= 0 ? " vibrato" : " bend";
		if (!I.Sig.Arp.Items.empty())
			s += I.Sig.Arp.Loop >= 0 ? " arp" : " chirp";
		return s;
	};

	// DPCM: one instrument, a key for each sample at each rate
	int DpcmSlot = -1;
	std::map<std::pair<int, int>, int> SampleIndex;		// (track, recording's sample) -> module sample
	std::map<std::vector<int>, int> KeyIndex;			// (module sample, rate, loop, delta) -> key
	Instrument DpcmInst;
	DpcmInst.Kind = INST_2A03;
	DpcmInst.Name = "DPCM samples";

	for (size_t s = 0; s < Works.size(); ++s) {
		Work &w = Works[s];
		for (size_t k = 0; k < w.Notes.size(); ++k) {
			Note &n = w.Notes[k];
			if (!n.Keep)
				continue;
			if (KindOf(n.Channel) == K_DPCM) {
				const ChannelTick &t = w.Rec->Ticks[CH_DPCM][n.Start];
				const std::pair<int, int> Id(int(s), t.Wave);
				int Sample;
				if (SampleIndex.count(Id))
					Sample = SampleIndex[Id];
				else {
					const std::vector<uint8_t> &Bytes = w.Rec->Sounds.Samples[t.Wave];
					Sample = -1;
					for (size_t i = 0; i < Out.Samples.size(); ++i)
						if (Out.Samples[i] == Bytes)
							Sample = int(i);
					if (Sample < 0 && Out.Samples.size() < 64) {
						Out.Samples.push_back(Bytes);
						char Buf[48];
						std::snprintf(Buf, sizeof(Buf), "Sample %d (%d bytes)", int(Out.Samples.size()), int(Bytes.size()));
						Out.SampleNames.push_back(Buf);
						Sample = int(Out.Samples.size()) - 1;
					}
					SampleIndex[Id] = Sample;
				}
				if (Sample < 0) {
					n.Keep = false;
					continue;
				}
				const std::vector<int> KeyId = { Sample, t.Timbre, (t.Flags & TICK_LOOP) ? 1 : 0, t.Extra ? t.Extra - 1 : -1 };
				int Key;
				if (KeyIndex.count(KeyId))
					Key = KeyIndex[KeyId];
				else {
					Key = int(KeyIndex.size());
					if (Key >= 96) {
						n.Keep = false;
						continue;
					}
					KeyIndex[KeyId] = Key;
					DpcmKey Dk;
					// Keys from C-3 up, so the notes read like a drum kit's
					Dk.Note = (36 + Key) % 96;
					Dk.Sample = Sample;
					Dk.Pitch = t.Timbre;
					Dk.Loop = KeyId[2] != 0;
					Dk.Delta = KeyId[3];
					DpcmInst.Keys.push_back(Dk);
				}
				n.Base = DpcmInst.Keys[Key].Note;
				n.Key = Key;
			}
		}
	}

	// Slots in the order instruments are first played
	for (size_t s = 0; s < Works.size(); ++s) {
		Work &w = Works[s];
		std::vector<Note *> ByTime;
		for (size_t k = 0; k < w.Notes.size(); ++k)
			if (w.Notes[k].Keep)
				ByTime.push_back(&w.Notes[k]);
		std::stable_sort(ByTime.begin(), ByTime.end(), [](const Note *a, const Note *b) { return a->Start < b->Start; });
		for (size_t k = 0; k < ByTime.size(); ++k) {
			Note &n = *ByTime[k];
			if (KindOf(n.Channel) == K_DPCM) {
				if (DpcmSlot < 0) {
					DpcmSlot = int(Out.Instruments.size());
					Out.Instruments.push_back(DpcmInst);
				}
				n.Inst = DpcmSlot;
				continue;
			}
			if (n.Inst < 0)
				continue;
			const int I = Map[n.Inst];
			if (Slot[I] < 0) {
				Slot[I] = int(Out.Instruments.size());
				Out.Instruments.push_back(Instrument());
			}
			n.Inst = Slot[I];
			n.Column = std::max(1, std::min(15, n.Column));
		}
	}
	if (DpcmSlot >= 0)
		Out.Instruments[DpcmSlot] = DpcmInst;

	// The instruments' macros and chip data
	for (size_t i = 0; i < Insts.size(); ++i) {
		if (Slot[i] < 0)
			continue;
		const Inst &I = Insts[i];
		Instrument &Ft = Out.Instruments[Slot[i]];
		const int Kind = I.Sig.Kind;
		Ft.Name = Name(I);
		switch (I.Sig.Chip) {
			case INST_VRC6: Ft.Kind = INST_VRC6; break;
			case INST_N163: Ft.Kind = INST_N163; break;
			case INST_FDS: Ft.Kind = INST_FDS; break;
			case INST_VRC7: Ft.Kind = INST_VRC7; break;
			default: Ft.Kind = INST_2A03; break;
		}
		if (Kind != K_VRC7) {
			Macro Vol = LoopedMacro(I.Env, 0);
			if (Kind == K_TRIANGLE)
				Vol = Macro(std::vector<int>(1, 15));
			else if (!I.Release.empty() && Vol.Loop < 0 && !Vol.Items.empty() && Vol.Items.size() + I.Release.size() <= size_t(MAX_MACRO)) {
				// It holds its last level until the note is released, then fades
				Vol.Release = int(Vol.Items.size()) - 1;
				Vol.Items.insert(Vol.Items.end(), I.Release.begin(), I.Release.end());
			}
			Ft.Macros[SEQ_VOLUME] = Vol;
			Ft.Macros[SEQ_ARPEGGIO] = I.Sig.Arp;
			Ft.Macros[SEQ_PITCH] = I.Sig.Pitch;
			Ft.Macros[SEQ_DUTY] = I.Sig.Duty;
		}
		const int Chip = Ft.Kind == INST_VRC6 ? MACROS_VRC6 : (Ft.Kind == INST_N163 ? MACROS_N163 : MACROS_2A03);
		if (Ft.Kind == INST_2A03 || Ft.Kind == INST_VRC6 || Ft.Kind == INST_N163)
			for (int t = 0; t < SEQ_TYPES; ++t)
				if (!Ft.Macros[t].Empty())
					Out.AddMacro(Chip, t, Ft.Macros[t]);
		if (Ft.Kind == INST_VRC7) {
			Ft.Patch = I.Sig.Patch;
		}
	}

	// Waves, patches and FDS settings, from the first note of each instrument
	for (size_t s = 0; s < Works.size(); ++s) {
		Work &w = Works[s];
		const SoundData &Sounds = Global;
		for (size_t k = 0; k < w.Notes.size(); ++k) {
			const Note &n = w.Notes[k];
			if (!n.Keep || n.Inst < 0)
				continue;
			Instrument &Ft = Out.Instruments[n.Inst];
			const int Kind = KindOf(n.Channel);
			if (Kind == K_N163 && Ft.Waves.empty() && n.Wave >= 0 && n.Wave < int(Sounds.Waves.size())) {
				std::vector<int> Wave(Sounds.Waves[n.Wave].begin(), Sounds.Waves[n.Wave].end());
				// FamiTracker's waves have up to 32 samples
				if (Wave.size() > 32) {
					std::vector<int> Short(32);
					for (int i = 0; i < 32; ++i)
						Short[i] = Wave[i * Wave.size() / 32];
					Wave = Short;
				}
				Ft.WaveSize = std::max(4, int(Wave.size()) & ~3);
				Wave.resize(Ft.WaveSize);
				Ft.Waves.push_back(Wave);
				Ft.WavePos = std::min(127 - Ft.WaveSize + 1, (n.Channel - CH_N163_1) * 16);
			}
			if (Kind == K_FDS && Ft.FdsWave.empty() && n.Wave >= 0 && n.Wave < int(Sounds.Waves.size())) {
				Ft.FdsWave.assign(Sounds.Waves[n.Wave].begin(), Sounds.Waves[n.Wave].end());
				if (n.ModTable < int(Sounds.ModTables.size()))
					Ft.FdsMod.assign(Sounds.ModTables[n.ModTable].begin(), Sounds.ModTables[n.ModTable].end());
				Ft.ModRate = n.ModRate;
				Ft.ModDepth = n.ModDepth;
			}
			if (Kind == K_VRC7 && Ft.Patch == 0 && n.Wave >= 0 && n.Wave < int(Sounds.Patches.size())) {
				for (int r = 0; r < 8; ++r)
					Ft.Regs[r] = Sounds.Patches[n.Wave][r];
			}
		}
	}

	// N163 notes: FamiTracker's note for a frequency doesn't depend on the
	// wave's length, but what is heard does; a shortened wave plays lower
	for (size_t s = 0; s < Works.size(); ++s)
		for (size_t k = 0; k < Works[s].Notes.size(); ++k) {
			Note &n = Works[s].Notes[k];
			if (!n.Keep || n.Inst < 0 || KindOf(n.Channel) != K_N163)
				continue;
			const int Size = Out.Instruments[n.Inst].WaveSize;
			if (n.WaveSize > 0 && Size > 0 && n.WaveSize != Size)
				n.Base += Round(12.0 * std::log2(double(Size) / n.WaveSize));
		}

	// The tracks' patterns
	const int Channels = Out.ChannelCount();
	for (size_t s = 0; s < Works.size(); ++s) {
		Work &w = Works[s];
		Track T;
		T.Name = w.Name;
		T.Speed = w.Report.Speed;
		T.Tempo = w.Report.Tempo;

		// Every row of the track, a channel at a time
		std::vector<std::vector<Cell> > Lines(Channels, std::vector<Cell>(w.Rows));
		std::vector<int> LastVolume(Channels, -1), LastArp(Channels, 0), LastDuty(Channels, 0);
		std::sort(w.Notes.begin(), w.Notes.end(), [](const Note &a, const Note &b) {
			return a.Channel != b.Channel ? a.Channel < b.Channel : a.Start < b.Start;
		});
		std::vector<bool> AnyArp(Channels, false), Looped(Channels, false);
		for (size_t k = 0; k < w.Notes.size(); ++k) {
			const int Col = ModuleColumn(w.Notes[k].Channel, Out.Expansion);
			if (Col >= 0 && w.Notes[k].Keep && w.Notes[k].Arp != 0)
				AnyArp[Col] = true;
		}
		for (size_t k = 0; k < w.Notes.size(); ++k) {
			Note &n = w.Notes[k];
			if (!n.Keep || n.Row >= w.Rows)
				continue;
			const int Col = ModuleColumn(n.Channel, Out.Expansion);
			if (Col < 0)
				continue;
			const int Kind = KindOf(n.Channel);
			// The loop's first note says the channel's volume, duty and arpeggio
			// again: the second time round they come from the loop's end
			if (w.LoopRow >= 0 && n.Row >= w.LoopRow && !Looped[Col]) {
				Looped[Col] = true;
				LastVolume[Col] = -1;
				LastDuty[Col] = -1;
				if (AnyArp[Col])
					LastArp[Col] = -1;
			}
			Cell &c = Lines[Col][n.Row];
			c.Note = std::max(0, std::min(Kind == K_NOISE ? 15 : 95, n.Base));
			c.Instrument = n.Inst;
			if (Kind != K_TRIANGLE && Kind != K_DPCM && n.Column != LastVolume[Col]) {
				c.Volume = n.Column;
				LastVolume[Col] = n.Column;
			}
			if (n.Delay > 0)
				c.AddEffect('G', n.Delay);
			if (n.Arp != LastArp[Col]) {
				c.AddEffect('0', n.Arp);
				LastArp[Col] = n.Arp;
			}
			if (n.ConstDuty >= 0 && n.ConstDuty != LastDuty[Col]) {
				c.AddEffect('V', n.ConstDuty);
				LastDuty[Col] = n.ConstDuty;
			}
			if (n.Sweep >= 0)
				c.AddEffect((n.Sweep & 0x08) ? 'H' : 'I', ((n.Sweep >> 4) & 7) << 4 | (n.Sweep & 7));
			++w.Report.Notes;

			// A note let go: a release (===) a tick before its fade, so the
			// instrument's release part starts with it
			if (n.TailStart >= 0 && n.Inst >= 0 && Out.Instruments[n.Inst].Macros[SEQ_VOLUME].Release >= 0) {
				const int Tick = n.Start + n.TailStart - 1;
				int Row = Round((Tick - w.Phase) / w.U);
				double Off = Tick - GridTime(w, Row);
				if (Off < -1.0) {
					--Row;
					Off = Tick - GridTime(w, Row);
				}
				const bool NextLater = k + 1 >= w.Notes.size() || w.Notes[k + 1].Channel != n.Channel || !w.Notes[k + 1].Keep ||
					w.Notes[k + 1].Row > Row;
				if (Row > n.Row && Row < w.Rows && NextLater && Lines[Col][Row].Note == NOTE_NONE) {
					Cell &r = Lines[Col][Row];
					r.Note = NOTE_RELEASE;
					if (Off >= 1.0 && Off < w.U)
						r.AddEffect('G', Round(Off));
					n.Released = true;
				}
			}

			// The sound stops before the next note: a halt where it stops
			// on a row, or a note cut (Sxx) when it stops between rows,
			// unless the instrument falls silent by itself by then
			if (n.SoundEnd < n.Next && Kind != K_DPCM) {
				bool Silent = false;
				if (n.Inst >= 0 && Kind != K_TRIANGLE && Kind != K_VRC7) {
					const Macro &V = Out.Instruments[n.Inst].Macros[SEQ_VOLUME];
					if (!V.Items.empty() && V.Loop < 0 && V.Release < 0 && V.Items.back() == 0 && int(V.Items.size()) <= n.Length() + 2)
						Silent = true;
					// Released: its fade ends in silence
					if (V.Release >= 0 && n.TailStart >= 0 && n.Released && V.Items.back() == 0)
						Silent = true;
				}
				if (!Silent) {
					// A rest of a row or more: a halt on the row nearest to where
					// the sound stops (delayed with Gxx when that's well off it).
					// A shorter gap, as before a note played detached: a cut.
					const double Gap = n.Next - n.SoundEnd;
					const double x = (n.SoundEnd - w.Phase) / w.U;
					int Row = Round(x);
					if (Row <= n.Row)
						Row = n.Row + 1;
					const double Off = n.SoundEnd - GridTime(w, Row);
					const bool NextOnLaterRow = k + 1 >= w.Notes.size() || w.Notes[k + 1].Channel != n.Channel ||
						!w.Notes[k + 1].Keep || w.Notes[k + 1].Row > Row;
					const bool RowFree = Row < w.Rows && NextOnLaterRow && Lines[Col][Row].Note == NOTE_NONE;
					if ((Gap >= w.U - 0.5 || Kind == K_VRC7 || n.Length() > 31) && RowFree) {
						Cell &h = Lines[Col][Row];
						h.Note = Kind == K_VRC7 ? NOTE_RELEASE : NOTE_HALT;
						if (Off >= 2 && Off < w.U)
							h.AddEffect('G', Round(Off));
					}
					else if (Kind != K_VRC7 && n.Length() >= 1 && n.Length() <= 31)
						c.AddEffect('S', n.Length());
				}
			}
		}
		// Patterns: rows per bar times four bars, the loop starting a frame
		const int RowsPerBar = w.RowsPerBeat * 4;
		int Length = std::min(MAX_ROWS, RowsPerBar * 4);
		const int Intro = w.LoopRow >= 0 ? w.LoopRow : w.Rows;
		while (Length < MAX_ROWS && (Intro + Length - 1) / Length + (w.Rows - Intro + Length - 1) / Length > MAX_FRAMES)
			Length = std::min(MAX_ROWS, Length * 2);
		T.PatternLength = Length;
		std::vector<std::pair<int, int> > Spans;		// (first row, rows) for each frame
		for (int r = 0; r < Intro; r += Length)
			Spans.push_back(std::make_pair(r, std::min(Length, Intro - r)));
		for (int r = Intro; r < w.Rows; r += Length)
			Spans.push_back(std::make_pair(r, std::min(Length, w.Rows - r)));
		if (int(Spans.size()) > MAX_FRAMES) {
			Spans.resize(MAX_FRAMES);
			w.Report.Problems.push_back("the track was cut short to fit FamiTracker's 128 frames");
		}
		const int LoopFrame = w.LoopRow >= 0 ? int((Intro + Length - 1) / Length) : -1;

		// The song's flow on the channel with the fewest effects
		int FlowColumn = Channels - 1;
		{
			int Fewest = 1 << 30;
			for (int c = Channels - 1; c >= 0; --c) {
				int n = 0;
				for (int r = 0; r < w.Rows; ++r)
					n += Lines[c][r].EffectCount();
				if (n < Fewest) {
					Fewest = n;
					FlowColumn = c;
				}
			}
		}

		T.Columns.assign(Channels, 1);
		T.Patterns.assign(Channels, std::vector<std::vector<Cell> >());
		for (size_t f = 0; f < Spans.size(); ++f) {
			std::vector<int> Frame(Channels, 0);
			const int First = Spans[f].first, Rows = Spans[f].second;
			const bool LastOfIntro = w.LoopRow >= 0 && First + Rows == Intro && f + 1 < Spans.size();
			const bool LastFrame = f + 1 == Spans.size();
			for (int c = 0; c < Channels; ++c) {
				std::vector<Cell> Pat(Length);
				for (int r = 0; r < Rows; ++r)
					Pat[r] = Lines[c][First + r];
				// At the loop's start, say the volume and the arpeggio again
				if (c == FlowColumn) {
					if (Rows < Length && LastOfIntro)
						Pat[Rows - 1].AddEffect('D', 0);
					else if (LastFrame && LoopFrame >= 0)
						Pat[Rows - 1].AddEffect('B', LoopFrame);
					else if (LastFrame)
						Pat[Rows - 1].AddEffect('C', 0);
					else if (Rows < Length)
						Pat[Rows - 1].AddEffect('D', 0);
				}
				for (int r = 0; r < Length; ++r)
					T.Columns[c] = std::max(T.Columns[c], std::max(1, Pat[r].EffectCount()));
				// The same pattern again gets the same number
				std::vector<std::vector<Cell> > &List = T.Patterns[c];
				int Number = -1;
				for (size_t p = 0; p < List.size(); ++p)
					if (List[p] == Pat)
						Number = int(p);
				if (Number < 0) {
					bool Empty = true;
					for (int r = 0; r < Length && Empty; ++r)
						Empty = Pat[r].Empty();
					if (List.empty() || !Empty) {
						if (int(List.size()) >= MAX_PATTERNS)
							Number = int(List.size()) - 1;
						else {
							List.push_back(Pat);
							Number = int(List.size()) - 1;
						}
					}
					else {
						// An empty pattern: the first empty one there is, or a new one
						for (size_t p = 0; p < List.size() && Number < 0; ++p) {
							bool E = true;
							for (int r = 0; r < Length && E; ++r)
								E = List[p][r].Empty();
							if (E)
								Number = int(p);
						}
						if (Number < 0) {
							List.push_back(Pat);
							Number = int(List.size()) - 1;
						}
					}
				}
				Frame[c] = Number;
			}
			T.Frames.push_back(Frame);
		}
		Out.Tracks.push_back(T);

		// What each channel plays, for the report
		for (int c = 0; c < Channels; ++c) {
			int Count = 0;
			for (int r = 0; r < w.Rows; ++r)
				if (Lines[c][r].Note >= 0)
					++Count;
			if (Count > 0)
				w.Report.Channels.push_back(ModuleChannelNames(Out.Expansion, Out.N163Channels)[c] + ": " + std::to_string(Count) + " notes");
		}
		Report.Tracks.push_back(w.Report);
	}
	Report.Instruments = int(Out.Instruments.size());

	// What the module came from
	Out.Comment.push_back("Imported from an NSF" + (File.Title.empty() ? std::string() : ": " + File.Title));
	if (!File.Artist.empty())
		Out.Comment.push_back("By " + File.Artist);
	if (!File.Copyright.empty())
		Out.Comment.push_back(File.Copyright);
	return true;
}

}	// namespace nsf

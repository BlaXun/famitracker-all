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

#include "Study.h"
#include "Import.h"
#include "Player.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>

namespace nsf {

namespace {

enum col_kind { C_PULSE, C_TRIANGLE, C_NOISE, C_DPCM, C_SAW, C_WAVE, C_FM, C_SAMPLE, C_DRUMS };	// C_SAMPLE, C_DRUMS: a tracker module's

std::string Format(const char *Fmt, ...)
#if defined(__GNUC__)
	__attribute__((format(printf, 1, 2)))
#endif
	;

std::string Format(const char *Fmt, ...)
{
	char Buf[1024];
	va_list Args;
	va_start(Args, Fmt);
	std::vsnprintf(Buf, sizeof(Buf), Fmt, Args);
	va_end(Args);
	return Buf;
}

std::string Join(const std::vector<std::string> &v, const std::string &Sep)
{
	std::string s;
	for (size_t i = 0; i < v.size(); ++i)
		s += (i ? Sep : "") + v[i];
	return s;
}

std::string Percent(int Part, int Whole)
{
	return Whole > 0 ? std::to_string((Part * 100 + Whole / 2) / Whole) + "%" : "0%";
}

std::string Count(int n, const char *One, const char *Many)
{
	return std::to_string(n) + " " + (n == 1 ? One : Many);
}

std::vector<int> ColumnKinds(const Module &M)
{
	if (!M.ChannelKinds.empty()) {
		std::vector<int> Own;
		for (size_t i = 0; i < M.ChannelKinds.size(); ++i)
			Own.push_back(M.ChannelKinds[i] == LAYOUT_DRUMS ? C_DRUMS : C_SAMPLE);
		return Own;
	}
	std::vector<int> k = { C_PULSE, C_PULSE, C_TRIANGLE, C_NOISE, C_DPCM };
	if (M.Expansion & EXP_VRC6) {
		k.push_back(C_PULSE);
		k.push_back(C_PULSE);
		k.push_back(C_SAW);
	}
	if (M.Expansion & EXP_MMC5) {
		k.push_back(C_PULSE);
		k.push_back(C_PULSE);
	}
	if (M.Expansion & EXP_N163)
		for (int i = 0; i < 8; ++i)
			k.push_back(C_WAVE);
	if (M.Expansion & EXP_FDS)
		k.push_back(C_WAVE);
	if (M.Expansion & EXP_VRC7)
		for (int i = 0; i < 6; ++i)
			k.push_back(C_FM);
	return k;
}

bool Melodic(int Kind) { return Kind != C_NOISE && Kind != C_DPCM && Kind != C_DRUMS; }

// A note as the track plays it
struct Played {
	int Row;			// in playing order from the start
	int Note;			// 0-95; noise 0-15; DPCM the key
	int Inst, Volume;	// what's in effect
	int Arp;			// 0xy in effect, 0 for none
	int Duty;			// Vxx in effect, -1 before any
	int Cut;			// Sxx: ticks, 0 for none
	int Length;			// rows until the channel's next event
	bool Sweep;			// Hxy or Ixy
	// Effects written in the patterns while it plays (tracker modules')
	bool Vibrato, Glide, Bend, Fade, Tremolo;	// 4xy, 3xx, 1xx or 2xx, Axy, 7xy
	Played() : Row(0), Note(0), Inst(-1), Volume(15), Arp(0), Duty(-1), Cut(0), Length(1), Sweep(false), Vibrato(false), Glide(false),
		Bend(false), Fade(false), Tremolo(false) {}
};

// Each channel's notes, rows in playing order up to the end or the loop's end
std::vector<std::vector<Played> > PlayedNotes(const Module &M, const Track &T, int &TotalRows, int &LoopRow)
{
	const int Columns = M.ChannelCount();
	std::vector<std::vector<Played> > Out(Columns);
	std::vector<int> Inst(Columns, -1), Volume(Columns, 15), Arp(Columns, 0), Duty(Columns, -1);
	std::vector<bool> Sounding(Columns, false);
	TotalRows = 0;
	LoopRow = -1;
	std::vector<int> FrameStart(T.Frames.size(), 0);
	for (size_t f = 0; f < T.Frames.size(); ++f) {
		FrameStart[f] = TotalRows;
		int Rows = T.PatternLength;
		int Jump = -1;
		bool Stop = false;
		for (int r = 0; r < T.PatternLength; ++r) {
			bool EndRow = false;
			const int Row = TotalRows + r;
			for (int c = 0; c < Columns; ++c) {
				const Cell &Cl = T.Patterns[c][T.Frames[f][c]][r];
				int Cut = 0;
				bool Sweep = false, Vibrato = false, Glide = false, Bend = false, Fade = false, Tremolo = false;
				for (int e = 0; e < MAX_EFFECTS; ++e) {
					const Effect &x = Cl.Effects[e];
					switch (x.Letter) {
						case 'D': EndRow = true; break;
						case 'B': EndRow = true; Jump = x.Param; break;
						case 'C': EndRow = true; Stop = true; break;
						case '0': Arp[c] = x.Param; break;
						case 'V': Duty[c] = x.Param; break;
						case 'S': Cut = x.Param; break;
						case 'H': case 'I': Sweep = true; break;
						case '4': Vibrato = (x.Param & 15) != 0; break;
						case '3': Glide = x.Param != 0; break;
						case '1': case '2': Bend = x.Param != 0; break;
						case 'A': Fade = x.Param != 0; break;
						case '7': Tremolo = (x.Param & 15) != 0; break;
						default: break;
					}
				}
				// What the effects do to the note playing, before a new one starts
				auto Mark = [&](Played &n) {
					n.Vibrato = n.Vibrato || Vibrato;
					n.Bend = n.Bend || Bend;
					n.Fade = n.Fade || Fade;
					n.Tremolo = n.Tremolo || Tremolo;
				};
				if (Sounding[c] && !Out[c].empty() && Cl.Note == NOTE_NONE)
					Mark(Out[c].back());
				if (Cl.Instrument >= 0)
					Inst[c] = Cl.Instrument;
				if (Cl.Volume >= 0)
					Volume[c] = Cl.Volume;
				if (Cl.Note == NOTE_NONE)
					continue;
				if (!Out[c].empty() && Out[c].back().Length < 0)
					Out[c].back().Length = Row - Out[c].back().Row;
				if (Cl.Note < 0) {
					Sounding[c] = false;
					continue;	// a halt or a release ends the note before
				}
				Played p;
				p.Row = Row;
				p.Note = Cl.Note;
				p.Inst = Inst[c];
				p.Volume = Volume[c];
				p.Arp = Arp[c];
				p.Duty = Duty[c];
				p.Cut = Cut;
				p.Length = -1;
				p.Sweep = Sweep;
				p.Glide = Glide && Sounding[c];		// slid into from the note before (3xx)
				Mark(p);
				Out[c].push_back(p);
				Sounding[c] = true;
			}
			if (EndRow) {
				Rows = r + 1;
				break;
			}
		}
		TotalRows += Rows;
		if (Jump >= 0 && Jump < int(FrameStart.size())) {
			LoopRow = FrameStart[Jump];
			break;
		}
		if (Stop)
			break;
	}
	for (int c = 0; c < Columns; ++c)
		if (!Out[c].empty() && Out[c].back().Length < 0)
			Out[c].back().Length = std::max(1, TotalRows - Out[c].back().Row);
	return Out;
}

// --- How an instrument's macros shape its notes ---

struct VolumeShape {
	int Start, Peak, PeakAt;
	bool Bloom, Swell, Pluck, Stepped, Echo, Restrike, Tremolo, ReleaseFade, ReleaseEcho;
	int Hold;			// the level it holds, -1 when it dies away
	int Length;			// frames to silence for a pluck
	int ReleaseLength;
	int TremoloEvery;
	std::vector<int> Steps;
	VolumeShape() : Start(0), Peak(0), PeakAt(0), Bloom(false), Swell(false), Pluck(false), Stepped(false), Echo(false),
		Restrike(false), Tremolo(false), ReleaseFade(false), ReleaseEcho(false), Hold(-1), Length(0), ReleaseLength(0),
		TremoloEvery(0) {}
};

// A soft bump after the sound has nearly gone: an echo of the note
bool HasEcho(const std::vector<int> &v, int From, int Peak)
{
	for (int i = std::max(0, From); i + 1 < int(v.size()); ++i) {
		if (v[i] > std::max(3, Peak / 4))
			continue;
		for (int j = i + 1; j < int(v.size()) && j <= i + 6; ++j)
			if (v[j] >= v[i] + 2 && v[j] <= Peak / 2 + 1)
				return true;
	}
	return false;
}

VolumeShape ShapeOf(const Macro &m)
{
	VolumeShape s;
	if (m.Items.empty())
		return s;
	const int End = m.Release >= 0 ? std::min(m.Release + 1, int(m.Items.size())) : int(m.Items.size());
	const std::vector<int> Body(m.Items.begin(), m.Items.begin() + End);
	for (size_t i = 0; i < Body.size(); ++i)
		if (Body[i] > s.Peak) {
			s.Peak = Body[i];
			s.PeakAt = int(i);
		}
	s.Start = Body[0];
	s.Bloom = s.PeakAt >= 1 && s.PeakAt <= 3 && s.Start < s.Peak;
	s.Swell = s.PeakAt >= 4;
	s.Pluck = Body.back() == 0 && m.Loop < 0 && m.Release < 0;
	if (s.Pluck)
		s.Length = int(Body.size());
	else
		s.Hold = m.Loop >= 0 && m.Loop < int(Body.size()) ? Body[m.Loop] : Body.back();
	// Plateaus of 6 frames or more that step down
	int Run = 1;
	for (size_t i = 1; i <= Body.size(); ++i) {
		if (i < Body.size() && Body[i] == Body[i - 1]) {
			++Run;
			continue;
		}
		if (Run >= 6 && Body[i - 1] > 0 && (s.Steps.empty() || Body[i - 1] <= s.Steps.back() - 2))
			s.Steps.push_back(Body[i - 1]);
		Run = 1;
	}
	s.Stepped = s.Steps.size() >= 2;
	s.Echo = HasEcho(Body, s.PeakAt + 1, s.Peak);
	// Struck again while it holds: a dip and a rise back
	for (int i = s.PeakAt + 1; i + 1 < int(Body.size()) && !s.Restrike; ++i) {
		if (Body[i] > Body[i - 1] - 3 || Body[i] <= std::max(3, s.Peak / 4))
			continue;
		for (int j = i + 1; j < int(Body.size()) && j <= i + 6; ++j)
			if (Body[j] >= Body[i] + 3 && Body[j] >= s.Peak * 6 / 10)
				s.Restrike = true;
	}
	// A loop of more than one level: tremolo
	if (m.Loop >= 0 && m.Loop + 1 < End) {
		std::set<int> Levels(Body.begin() + m.Loop, Body.end());
		if (Levels.size() >= 2) {
			s.Tremolo = true;
			s.TremoloEvery = End - m.Loop;
		}
	}
	if (m.Release >= 0 && m.Release + 1 < int(m.Items.size())) {
		const std::vector<int> Tail(m.Items.begin() + m.Release + 1, m.Items.end());
		s.ReleaseEcho = HasEcho(Tail, 0, s.Peak);
		s.ReleaseFade = !s.ReleaseEcho;
		s.ReleaseLength = int(Tail.size());
	}
	return s;
}

const char *DutyName(bool Vrc6, int Duty)
{
	if (Vrc6) {
		static const char *const V[8] = { "6%", "12.5%", "19%", "25%", "31%", "37.5%", "44%", "50%" };
		return V[Duty & 7];
	}
	static const char *const P[4] = { "12.5%", "25%", "50%", "75%" };
	return P[Duty & 3];
}

struct DutyShape {
	bool Click;			// the first frame(s) at another duty
	int ClickDuty, ClickFrames, Main;
	bool Moves;			// changes later in the note
	DutyShape() : Click(false), ClickDuty(0), ClickFrames(0), Main(-1), Moves(false) {}
};

DutyShape DutyOf(const Macro &m)
{
	DutyShape d;
	const std::vector<int> &v = m.Items;
	if (v.empty())
		return d;
	if (v.size() >= 2 && v[0] != v[1]) {
		int k = 1;
		while (k < int(v.size()) && v[k] == v[0])
			++k;
		d.Click = true;
		d.ClickDuty = v[0];
		d.ClickFrames = k;
	}
	else if (v.size() > 6 && v[0] == v[1] && v[1] != v[2]) {
		// Two frames at another duty, then a long stretch at the main one
		const std::set<int> Rest(v.begin() + 2, v.end());
		if (Rest.size() == 1) {
			d.Click = true;
			d.ClickDuty = v[0];
			d.ClickFrames = 2;
		}
	}
	const size_t From = d.Click ? size_t(d.ClickFrames) : 0;
	std::map<int, int> Uses;
	for (size_t i = From; i < v.size(); ++i)
		++Uses[v[i]];
	int Most = -1, MostCount = -1;
	for (std::map<int, int>::const_iterator it = Uses.begin(); it != Uses.end(); ++it)
		if (it->second > MostCount) {
			MostCount = it->second;
			Most = it->first;
		}
	d.Main = Most >= 0 ? Most : v[0];
	int Changes = 0;
	for (size_t i = From + 1; i < v.size(); ++i)
		if (v[i] != v[i - 1])
			++Changes;
	d.Moves = Changes >= 1;
	return d;
}

struct ArpShape {
	bool Chord;				// loops through a chord
	std::vector<int> Intervals;
	int FramesPerStep;
	bool Scoop;				// starts off the note and settles on it
	int ScoopFrom, ScoopFrames;
	bool Fall;				// moves off the note later
	int FallBy, FallAfter;
	ArpShape() : Chord(false), FramesPerStep(1), Scoop(false), ScoopFrom(0), ScoopFrames(0), Fall(false), FallBy(0), FallAfter(0) {}
};

ArpShape ArpOf(const Macro &m)
{
	ArpShape a;
	const std::vector<int> &v = m.Items;
	if (v.empty() || m.Setting != ARP_ABSOLUTE)
		return a;
	if (m.Loop >= 0 && m.Loop < int(v.size())) {
		const std::set<int> Values(v.begin() + m.Loop, v.end());
		if (Values.size() >= 2) {
			a.Chord = true;
			const int Low = *Values.begin();
			for (std::set<int>::const_iterator it = Values.begin(); it != Values.end(); ++it)
				a.Intervals.push_back(*it - Low);
			int Run = 1;
			while (m.Loop + Run < int(v.size()) && v[m.Loop + Run] == v[m.Loop])
				++Run;
			a.FramesPerStep = Run;
			return a;
		}
	}
	int k = 0;
	while (k < int(v.size()) && v[k] != 0)
		++k;
	if (k > 0 && k <= 4 && k < int(v.size())) {
		a.Scoop = true;
		a.ScoopFrom = v[0];
		a.ScoopFrames = k;
	}
	int Zero = k;
	while (Zero < int(v.size()) && v[Zero] == 0)
		++Zero;
	if (Zero < int(v.size()) && Zero > k) {
		a.Fall = true;
		a.FallBy = v.back();
		a.FallAfter = Zero;
	}
	return a;
}

struct PitchShape {
	bool Vibrato;
	int Delay, Period;
	double Depth;		// period steps either way
	bool Flick;			// off the pitch for a few frames at the attack, then back
	int FlickBy;
	bool Flutter;		// flicks again and again
	int FlutterEvery;
	bool Bend;
	int BendBy, BendFrames;
	PitchShape() : Vibrato(false), Delay(0), Period(0), Depth(0), Flick(false), FlickBy(0), Flutter(false), FlutterEvery(0),
		Bend(false), BendBy(0), BendFrames(0) {}
};

PitchShape PitchOf(const Macro &m)
{
	PitchShape p;
	std::vector<int> v = m.Items;
	if (v.empty())
		return p;
	if (m.Loop >= 0 && m.Loop < int(v.size()))
		for (int Twice = 0; Twice < 2; ++Twice)
			v.insert(v.end(), m.Items.begin() + m.Loop, m.Items.end());
	std::vector<int> Cum(v.size());
	int c = 0;
	for (size_t i = 0; i < v.size(); ++i) {
		c += v[i];
		Cum[i] = c;
	}
	int First = 0;
	while (First < int(Cum.size()) && Cum[First] == 0)
		++First;
	if (First >= int(Cum.size()))
		return p;
	// A flick: away and back within four frames, then nothing until the next
	int Back = First;
	int Extreme = 0;
	while (Back < int(Cum.size()) && Cum[Back] != 0) {
		if (std::abs(Cum[Back]) > std::abs(Extreme))
			Extreme = Cum[Back];
		++Back;
	}
	if (Back < int(Cum.size()) && Back - First <= 3 && std::abs(Extreme) >= 2) {
		p.Flick = true;
		p.FlickBy = Extreme;
		int Next = Back;
		while (Next < int(Cum.size()) && Cum[Next] == 0)
			++Next;
		if (Next < int(Cum.size())) {
			p.Flutter = true;
			p.FlutterEvery = Next - First;
		}
		return p;
	}
	// Vibrato: the deviation swings to and fro
	std::vector<int> Turns;
	int Dir = 0;
	for (size_t i = First + 1; i < Cum.size(); ++i) {
		const int d = Cum[i] > Cum[i - 1] ? 1 : (Cum[i] < Cum[i - 1] ? -1 : 0);
		if (d != 0 && d != Dir) {
			if (Dir != 0)
				Turns.push_back(int(i) - 1);
			Dir = d;
		}
	}
	int Low = 0, High = 0;
	for (size_t i = First; i < Cum.size(); ++i) {
		Low = std::min(Low, Cum[i]);
		High = std::max(High, Cum[i]);
	}
	if (Turns.size() >= 3) {
		p.Vibrato = true;
		p.Delay = First;
		p.Period = int(std::floor(2.0 * (Turns.back() - Turns.front()) / (Turns.size() - 1) + 0.5));
		p.Depth = (High - Low) / 2.0;
		return p;
	}
	if (Cum.back() != 0) {
		p.Bend = true;
		p.BendBy = Cum.back();
		p.BendFrames = int(m.Items.size());
	}
	return p;
}

// Cents for a period change of Steps around a note on a channel; -1 when
// the period is too short to say
double Cents(bool Triangle, bool Sawtooth, int Note, double Steps)
{
	const double f = 440.0 * std::pow(2.0, (Note - 45) / 12.0);
	const double Divider = Triangle ? 32 : (Sawtooth ? 14 : 16);
	const double Period = 1789773.0 / (Divider * f) - 1;
	if (Period <= Steps + 1)
		return -1;
	return 1200.0 * std::log2(Period / (Period - Steps));
}

std::string Items(const std::vector<int> &v, size_t From, size_t To)
{
	std::string s;
	for (size_t i = From; i < To && i < v.size(); ++i)
		s += (s.empty() ? "" : " ") + std::to_string(v[i]);
	return s;
}

// --- Key and chords ---

const char *const PITCH_NAMES[12] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };

std::string KeyOf(const std::vector<double> &Weights)
{
	// Krumhansl-Kessler profiles
	static const double MAJOR[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
	static const double MINOR[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
	double Total = 0;
	for (int i = 0; i < 12; ++i)
		Total += Weights[i];
	if (Total <= 0)
		return std::string();
	double Best = -1e9;
	std::string Name;
	for (int Root = 0; Root < 12; ++Root)
		for (int Minor = 0; Minor < 2; ++Minor) {
			const double *P = Minor ? MINOR : MAJOR;
			double mx = 0, my = 0;
			for (int i = 0; i < 12; ++i) {
				mx += Weights[i];
				my += P[i];
			}
			mx /= 12;
			my /= 12;
			double Sxy = 0, Sxx = 0, Syy = 0;
			for (int i = 0; i < 12; ++i) {
				const double x = Weights[(Root + i) % 12] - mx, y = P[i] - my;
				Sxy += x * y;
				Sxx += x * x;
				Syy += y * y;
			}
			const double r = Sxx > 0 && Syy > 0 ? Sxy / std::sqrt(Sxx * Syy) : 0;
			if (r > Best) {
				Best = r;
				Name = std::string(PITCH_NAMES[Root]) + (Minor ? " minor" : " major");
			}
		}
	return Best > 0.5 ? Name : std::string();
}

// The chord of a bar, from how long each pitch class sounds in it and the
// bass's main pitch class (-1 for none)
std::string ChordOf(const std::vector<double> &w, int Bass)
{
	double Total = 0;
	for (int i = 0; i < 12; ++i)
		Total += w[i];
	if (Total <= 0)
		return "-";
	struct Shape { const char *Suffix; int Tones[3]; };
	static const Shape SHAPES[] = { { "", { 0, 4, 7 } }, { "m", { 0, 3, 7 } }, { "dim", { 0, 3, 6 } }, { "sus4", { 0, 5, 7 } } };
	double Best = -1e9;
	std::string Name = "-";
	for (int Root = 0; Root < 12; ++Root)
		for (size_t s = 0; s < sizeof(SHAPES) / sizeof(SHAPES[0]); ++s) {
			double In = 0;
			bool BassIn = false;
			for (int k = 0; k < 3; ++k) {
				const int Tone = (Root + SHAPES[s].Tones[k]) % 12;
				In += w[Tone] * (k == 0 ? 1.2 : 1.0);
				BassIn = BassIn || Tone == Bass;
			}
			// A diminished chord needs all three of its notes; a suspended one its
			// fourth and no third
			if (s == 2 && (w[Root] < 0.15 * Total || w[(Root + 3) % 12] < 0.15 * Total || w[(Root + 6) % 12] < 0.15 * Total))
				continue;
			if (s == 3 && (w[(Root + 5) % 12] < 0.15 * Total || w[(Root + 3) % 12] + w[(Root + 4) % 12] > 0.05 * Total))
				continue;
			double Score = In - 0.6 * (Total - In) - (s >= 2 ? 0.12 * Total : 0);
			if (Bass >= 0)
				Score += Root == Bass ? 0.3 * Total : (BassIn ? 0.05 * Total : -0.1 * Total);
			if (Score > Best) {
				Best = Score;
				Name = std::string(PITCH_NAMES[Root]) + SHAPES[s].Suffix;
			}
		}
	return Name;
}

// "| Em x4 | C | D |": a bar each, repeats counted
std::string ChordLine(const std::vector<std::string> &Chords, size_t From, size_t To)
{
	std::string s = "|";
	for (size_t i = From; i < To && i < Chords.size();) {
		size_t j = i;
		while (j < To && j < Chords.size() && Chords[j] == Chords[i])
			++j;
		s += " " + Chords[i] + (j - i > 1 ? Format(" x%d", int(j - i)) : "") + " |";
		i = j;
	}
	return s;
}

// A part's name that calls it the melody: "Melody", "Lead", "Soprano",
// "Vocals" (a MIDI file's tracks are named so)
bool NamesTheMelody(const std::string &Name)
{
	static const char *const WORDS[] = { "melody", "lead", "soprano", "vocal", "vocals", "solo", "singer", "tune", "theme" };
	std::string w;
	for (size_t i = 0; i <= Name.size(); ++i) {
		const unsigned char c = i < Name.size() ? static_cast<unsigned char>(Name[i]) : ' ';
		if (std::isalpha(c)) {
			w += static_cast<char>(std::tolower(c));
			continue;
		}
		for (size_t k = 0; k < sizeof(WORDS) / sizeof(WORDS[0]); ++k)
			if (w == WORDS[k])
				return true;
		w.clear();
	}
	return false;
}

// --- Rhythm ---

// Steps in a beat for the rhythm grids: sixteenths, or eighth-note triplets
// when a beat has 3 or 6 rows (a compound meter's eighths); sixteenths in a
// meter of eighth-note beats (5/8, 7/8)
int StepsInBeat(int RowsPerBeat, int BeatsPerBar)
{
	if (RowsPerBeat % 3 == 0)
		return 3;
	if (BeatsPerBar == 4 || RowsPerBeat % 4 == 0)
		return 4;
	return RowsPerBeat % 2 == 0 ? 2 : 1;
}

std::string BeatName(int Step, int StepsPerBeat)
{
	const int Beat = Step / StepsPerBeat + 1, Part = Step % StepsPerBeat;
	if (Part == 0)
		return std::to_string(Beat);
	if (StepsPerBeat == 2)
		return std::to_string(Beat) + "&";
	if (StepsPerBeat == 4) {
		static const char *const SUFFIX[4] = { "", "e", "&", "a" };
		return std::to_string(Beat) + SUFFIX[Part];
	}
	static const char *const SUFFIX3[3] = { "", "-trip", "-let" };
	return std::to_string(Beat) + SUFFIX3[Part % 3];
}

// A bar's grid as "x..x ..x. ..x. x...", a beat a group
std::string GridText(const std::string &Grid, int StepsPerBeat)
{
	std::string s;
	for (size_t i = 0; i < Grid.size(); ++i) {
		if (i && i % StepsPerBeat == 0)
			s += ' ';
		s += Grid[i];
	}
	return s;
}

// The most common figure, and the bars that have it
std::string Commonest(const std::vector<std::string> &Figures, int &Bars)
{
	std::map<std::string, int> Uses;
	for (size_t i = 0; i < Figures.size(); ++i)
		if (!Figures[i].empty())
			++Uses[Figures[i]];
	std::string Best;
	Bars = 0;
	for (std::map<std::string, int>::const_iterator it = Uses.begin(); it != Uses.end(); ++it)
		if (it->second > Bars) {
			Bars = it->second;
			Best = it->first;
		}
	return Best;
}

// How bright a DPCM sample sounds: the RMS frequency of the levels it plays
// (smoothed over 8 steps, which takes out the DMC's own up-down ripple) at the
// rate of its pitch, in Hz. A kick is a low swing (around 120 Hz), a snare's
// noise puts it higher (250 Hz and up), a bongo or a clap brighter still.
double SampleBrightness(const std::vector<uint8_t> &Data, int Pitch)
{
	static const int PERIODS[16] = { 428, 380, 340, 320, 286, 254, 226, 214, 190, 160, 142, 128, 106, 84, 72, 54 };
	const double Rate = 1789773.0 / PERIODS[std::max(0, std::min(15, Pitch))];
	std::vector<double> Smooth;
	int Level = 64;
	double Sum = 0;
	std::vector<int> Last(8, 0);
	size_t n = 0;
	for (size_t i = 0; i < Data.size(); ++i)
		for (int Bit = 0; Bit < 8; ++Bit, ++n) {
			if ((Data[i] >> Bit) & 1) {
				if (Level <= 125)
					Level += 2;
			}
			else if (Level >= 2)
				Level -= 2;
			Sum += Level - Last[n % 8];
			Last[n % 8] = Level;
			if (n >= 7)
				Smooth.push_back(Sum / 8);
		}
	if (Smooth.size() < 16)
		return 0;
	double Mean = 0;
	for (size_t i = 0; i < Smooth.size(); ++i)
		Mean += Smooth[i];
	Mean /= Smooth.size();
	double X2 = 0, D2 = 0;
	for (size_t i = 0; i < Smooth.size(); ++i) {
		X2 += (Smooth[i] - Mean) * (Smooth[i] - Mean);
		if (i > 0)
			D2 += (Smooth[i] - Smooth[i - 1]) * (Smooth[i] - Smooth[i - 1]);
	}
	if (X2 <= 0)
		return 0;
	const double TwoPi = 6.283185307179586;
	return Rate / TwoPi * std::sqrt((D2 / (Smooth.size() - 1)) / (X2 / Smooth.size()));
}

// A sample's sound in words, from its brightness
const char *SampleSound(double Bright)
{
	return Bright < 180 ? "a low thump" : (Bright < 500 ? "a mid-range hit" : "a bright hit");
}

// A drum voice: one sample, or one kind of noise hit
struct Voice {
	int Channel;
	std::string Key;		// what tells it apart on its channel
	bool Dpcm;
	int Sample;				// DPCM: the sample, -1 when unknown
	double Bright;			// DPCM: how bright the sample sounds (SampleBrightness), 0 when unknown
	int PitchClass;			// noise: 0 low, 1 middle, 2 high
	int DecayClass;			// noise: 0 short (5 frames or less), 1 medium, 2 long or held
	std::vector<int> Rows;	// its hits, in playing order
	std::map<int, int> Insts;
	std::string Name;		// "snare", "hi-hat", ...
	int Backbeat;			// the first of two rows a bar it falls on, half a bar apart; -1 for none
	Voice() : Channel(0), Dpcm(false), Sample(-1), Bright(0), PitchClass(0), DecayClass(0), Backbeat(-1) {}
};

// The first of two places a bar, half a bar apart, that a voice falls on
// (a backbeat, or a kick on 1 and 3), -1 when it doesn't
int TwoPlaces(const Voice &V, int RowsPerBar, int LoopRow, int LoopBars)
{
	std::vector<int> H(RowsPerBar, 0);
	int n = 0;
	for (size_t i = 0; i < V.Rows.size(); ++i)
		if (LoopRow < 0 || V.Rows[i] >= LoopRow) {
			++H[V.Rows[i] % RowsPerBar];
			++n;
		}
	const double PerBar = double(n) / std::max(1, LoopBars);
	if (n < 4 || PerBar < 1.2 || PerBar > 3)
		return -1;
	const int Half = RowsPerBar / 2;
	int Best = 0;
	for (int q = 1; q < Half; ++q)
		if (H[q] + H[q + Half] > H[Best] + H[Best + Half])
			Best = q;
	if ((H[Best] + H[Best + Half]) * 10 >= n * 6 && H[Best] * 5 >= n && H[Best + Half] * 5 >= n)
		return Best;
	return -1;
}

// Where the bars start, in rows from the top. A backbeat says it best: a
// snare on two places a bar, half a bar apart, is on beats 2 and 4.
// Otherwise the beat is where the most notes start (long ones and drums
// counting most), and the bar line is the loop point, or the start, when
// it falls on a beat; else where long notes and bass notes fall. Known, when
// the music says where its bars start (a MIDI file's meter), stands for the
// loop point and keeps its beat; only a backbeat moves it.
int BarPhase(const std::vector<std::vector<Played> > &Notes, const std::vector<int> &Kinds, int Bass, const std::vector<Voice> &Voices,
	const std::vector<int> &SampleSizes, int RowsPerBar, int RowsPerBeat, int LoopRow, int Known)
{
	if (RowsPerBar <= 1)
		return 0;
	std::vector<double> W(RowsPerBar, 0), Beat(RowsPerBeat, 0);
	for (size_t c = 0; c < Notes.size(); ++c) {
		if (!Melodic(Kinds[c]))
			continue;
		for (size_t i = 0; i < Notes[c].size(); ++i) {
			const Played &p = Notes[c][i];
			double w = 0.5 + std::min(p.Length, RowsPerBar) / double(RowsPerBeat);
			if (int(c) == Bass)
				w *= 1.5;
			W[p.Row % RowsPerBar] += w;
			Beat[p.Row % RowsPerBeat] += std::min(w, 1.5);
		}
	}
	// The snare: the noise's, or else the longest of two or more samples (a
	// snare's tail is longer than a kick's). Telling them apart by how bright
	// they sound (SampleBrightness) did worse on the tracks studied: a tom or a
	// second snare can be as bright, and the bar line then moves.
	int Longest = -1, Samples = 0;
	for (size_t v = 0; v < Voices.size(); ++v)
		if (Voices[v].Dpcm && Voices[v].Sample >= 0) {
			++Samples;
			if (Longest < 0 || SampleSizes[Voices[v].Sample] > SampleSizes[Voices[Longest].Sample])
				Longest = int(v);
		}
	// The other drums mark the beats (a snare can sit between them)
	for (size_t v = 0; v < Voices.size(); ++v) {
		if (Voices[v].Name == "snare" || (Samples >= 2 && int(v) == Longest))
			continue;
		for (size_t i = 0; i < Voices[v].Rows.size(); ++i)
			Beat[Voices[v].Rows[i] % RowsPerBeat] += 2;
	}
	const int Half = RowsPerBar / 2;
	const int Default = Known >= 0 ? Known % RowsPerBar : (LoopRow > 0 ? LoopRow % RowsPerBar : 0);
	auto ScoreAt = [&](int ph) { return W[ph] + 0.5 * W[(ph + Half) % RowsPerBar]; };
	// A backbeat: the snare on two places a bar, half a bar apart
	int Back = -1;
	for (size_t v = 0; v < Voices.size() && Back < 0; ++v)
		if (Voices[v].Backbeat >= 0 && Voices[v].Name == "snare")
			Back = Voices[v].Backbeat;
	if (Back < 0 && Longest >= 0)
		Back = Voices[Longest].Backbeat;
	if (Back >= 0) {
		const int P1 = (Back - RowsPerBeat + RowsPerBar) % RowsPerBar, P2 = (P1 + Half) % RowsPerBar;
		if (Default == P1 || Default == P2)
			return Default;
		return ScoreAt(P2) > ScoreAt(P1) ? P2 : P1;
	}
	int On = Default % RowsPerBeat;
	for (int o = 0; o < RowsPerBeat && Known < 0; ++o)
		if (Beat[o] > 1.25 * Beat[Default % RowsPerBeat] && Beat[o] > Beat[On])
			On = o;
	if (Default % RowsPerBeat == On)
		return Default;
	int Best = On;
	for (int ph = On; ph < RowsPerBar; ph += RowsPerBeat)
		if (ScoreAt(ph) > ScoreAt(Best))
			Best = ph;
	return Best;
}

// The character of a sound, to group notes by: its duty, the shape of its
// volume, its arpeggio and its pitch, without the exact numbers
std::string SoundKey(const Macro Macros[SEQ_TYPES], bool Triangle)
{
	std::string k;
	const DutyShape d = DutyOf(Macros[SEQ_DUTY]);
	k += Format("d%d%s ", d.Main, d.Click ? "c" : "");
	const VolumeShape v = ShapeOf(Macros[SEQ_VOLUME]);
	if (Triangle)
		k += v.Pluck ? "short " : "";
	else {
		k += v.Bloom ? "bloom " : (v.Swell ? "swell " : "");
		if (v.Pluck)
			k += v.Length <= 6 ? "pluck1 " : (v.Length <= 16 ? "pluck2 " : "pluck3 ");
		else if (v.Hold >= 0)
			k += v.Peak - v.Hold <= 2 ? "hold1 " : (v.Peak - v.Hold <= 6 ? "hold2 " : "hold3 ");
		k += std::string(v.Stepped ? "steps " : "") + (v.Echo || v.ReleaseEcho ? "echo " : "") + (v.Restrike ? "restrike " : "") +
			(v.Tremolo ? "tremolo " : "");
	}
	const ArpShape a = ArpOf(Macros[SEQ_ARPEGGIO]);
	if (a.Chord) {
		k += "chord";
		for (size_t i = 0; i < a.Intervals.size(); ++i)
			k += "," + std::to_string(a.Intervals[i]);
		k += " ";
	}
	k += std::string(a.Scoop ? (a.ScoopFrom > 0 ? "from-above " : "from-below ") : "") + (a.Fall ? "fall " : "");
	const PitchShape p = PitchOf(Macros[SEQ_PITCH]);
	k += p.Vibrato ? "vibrato" : (p.Flick ? "flick" : (p.Bend ? "bend" : ""));
	return k;
}

}	// namespace

std::string MacroText(const Macro &M)
{
	std::string s;
	for (size_t i = 0; i < M.Items.size(); ++i) {
		if (int(i) == M.Loop)
			s += (s.empty() ? "" : " ") + std::string("[");
		s += (s.empty() || s.back() == '[' ? "" : " ") + std::to_string(M.Items[i]);
		if (int(i) == M.Release && i + 1 < M.Items.size())
			s += " |";
	}
	return s;
}

std::string DescribeSound(const Macro Macros[SEQ_TYPES], bool Vrc6, int Note, bool Noise, bool Triangle, bool Sawtooth)
{
	std::vector<std::string> w;
	// Duty
	const DutyShape d = DutyOf(Macros[SEQ_DUTY]);
	if (Noise) {
		if (d.Main == 1)
			w.push_back("metallic (looped) noise");
	}
	else if (!Triangle && !Sawtooth && d.Main >= 0) {
		std::string s = std::string(DutyName(Vrc6, d.Main)) + " duty";
		if (d.Click)
			s += Format(" after a %s click on the first %s", DutyName(Vrc6, d.ClickDuty),
				d.ClickFrames == 1 ? "frame" : Count(d.ClickFrames, "frame", "frames").c_str());
		if (d.Moves)
			s += Format(" (it moves: %s%s)", Items(Macros[SEQ_DUTY].Items, 0, 12).c_str(), Macros[SEQ_DUTY].Items.size() > 12 ? " ..." : "");
		w.push_back(s);
	}
	// Volume; on the triangle only whether it sounds
	const Macro &Vol = Macros[SEQ_VOLUME];
	if (Triangle) {
		const std::vector<int> &v = Vol.Items;
		size_t On = 0;
		while (On < v.size() && v[On] != 0)
			++On;
		if (!v.empty() && On < v.size() && On > 0) {
			const std::set<int> After(v.begin() + On, v.end());
			if (Vol.Loop >= 0 && Vol.Loop <= int(On))
				w.push_back(Format("chopped: on for %s, off, over and over", Count(int(On), "frame", "frames").c_str()));
			else if (After.size() == 1)
				w.push_back(Format("short: sounds for %s, then stops", Count(int(On), "frame", "frames").c_str()));
		}
	}
	else if (!Vol.Items.empty()) {
		const VolumeShape v = ShapeOf(Vol);
		std::string s;
		if (v.Bloom)
			s = Format("blooms from %d to %d in %s", v.Start, v.Peak, Count(v.PeakAt, "frame", "frames").c_str());
		else if (v.Swell)
			s = Format("swells in from %d to %d over %d frames", v.Start, v.Peak, v.PeakAt);
		else
			s = Format("starts at %d", v.Peak);
		if (v.Stepped) {
			std::vector<std::string> Steps;
			for (size_t i = 0; i < v.Steps.size(); ++i)
				Steps.push_back(std::to_string(v.Steps[i]));
			s += ", steps down " + Join(Steps, ", ");
		}
		else if (v.Pluck)
			s += Format(" and dies away in %s", Count(v.Length, "frame", "frames").c_str());
		else if (v.Hold >= 0 && v.Hold != v.Peak)
			s += Format(", holds %d", v.Hold);
		if (v.Restrike)
			s += ", struck again inside the note";
		if (v.Tremolo)
			s += Format(", pulsing every %s", Count(v.TremoloEvery, "frame", "frames").c_str());
		if (v.Echo)
			s += ", with a soft echo after it fades";
		if (v.ReleaseEcho)
			s += ", and an echo when it's let go";
		else if (v.ReleaseFade)
			s += Format(", fading over %s when it's let go", Count(v.ReleaseLength, "frame", "frames").c_str());
		w.push_back(s);
	}
	// Pitch: the arpeggio; on the noise channel it moves the noise's pitch
	const Macro &Arp = Macros[SEQ_ARPEGGIO];
	if (Noise) {
		if (!Arp.Items.empty())
			w.push_back(std::string(Arp.Setting == ARP_FIXED ? "noise pitches " : "noise pitch moving by ") + Items(Arp.Items, 0, 12) +
				(Arp.Items.size() > 12 ? " ..." : ""));
	}
	else {
		const ArpShape a = ArpOf(Arp);
		if (a.Chord) {
			std::vector<std::string> Iv;
			for (size_t i = 0; i < a.Intervals.size(); ++i)
				Iv.push_back(std::to_string(a.Intervals[i]));
			w.push_back(Format("a chord as an arpeggio (%s semitones, %s a note)", Join(Iv, ", ").c_str(),
				Count(a.FramesPerStep, "frame", "frames").c_str()));
		}
		if (a.Scoop) {
			if (Triangle && a.ScoopFrom >= 2)
				w.push_back(Format("a thump: starts %d semitones up and drops into the note in %s", a.ScoopFrom,
					Count(a.ScoopFrames, "frame", "frames").c_str()));
			else
				w.push_back(Format("%s into its pitch from %s %s in %s", a.ScoopFrom > 0 ? "falls" : "rises",
					Count(std::abs(a.ScoopFrom), "semitone", "semitones").c_str(), a.ScoopFrom > 0 ? "above" : "below",
					Count(a.ScoopFrames, "frame", "frames").c_str()));
		}
		if (a.Fall)
			w.push_back(Format("%s %s after %s", a.FallBy < 0 ? "falls" : "rises", Count(std::abs(a.FallBy), "semitone", "semitones").c_str(),
				Count(a.FallAfter, "frame", "frames").c_str()));
		// The period's motion
		const PitchShape p = PitchOf(Macros[SEQ_PITCH]);
		if (p.Vibrato) {
			std::string s = Format("vibrato after %s, %s %d-frame cycle", Count(p.Delay, "frame", "frames").c_str(),
				p.Period == 8 || p.Period == 11 || p.Period == 18 ? "an" : "a", p.Period);
			const double c = Cents(Triangle, Sawtooth, Note, p.Depth);
			if (c > 0)
				s += Format(", about %d cents either way", int(c + 0.5));
			w.push_back(s);
		}
		if (p.Flick) {
			std::string s = Format("a pitch flick %s on the attack", p.FlickBy < 0 ? "up" : "down");
			if (p.Flutter)
				s += Format(", again every %s (a flutter)", Count(p.FlutterEvery, "frame", "frames").c_str());
			w.push_back(s);
		}
		if (p.Bend)
			w.push_back(Format("bends %s over %s", p.BendBy > 0 ? "down" : "up", Count(p.BendFrames, "frame", "frames").c_str()));
	}
	if (w.empty())
		return Noise ? "a plain noise hit" : (Triangle ? "a plain triangle" : "a plain, steady tone");
	return Join(w, "; ");
}

TrackStudy StudyTrack(const Module &M, int TrackIndex, const TrackReport &Report)
{
	TrackStudy S;
	if (TrackIndex < 0 || TrackIndex >= int(M.Tracks.size()))
		return S;
	const Track &T = M.Tracks[TrackIndex];
	S.Name = T.Name;
	S.Bpm = Report.Bpm;
	S.RowsPerBeat = Report.RowsPerBeat > 0 ? Report.RowsPerBeat : 4;
	S.BeatsPerBar = Report.BeatsPerBar > 0 ? Report.BeatsPerBar : 4;
	S.Meter = Report.Meter;
	S.TicksPerRow = Report.TicksPerRow;
	S.Tracker = !M.ChannelKinds.empty();
	S.Speed = Report.Speed;
	S.Tempo = Report.Tempo;
	const std::vector<int> Kinds = ColumnKinds(M);
	const std::vector<std::string> Names = ChannelNamesOf(M);
	int TotalRows = 0, LoopRow = -1;
	const std::vector<std::vector<Played> > Notes = PlayedNotes(M, T, TotalRows, LoopRow);
	const int RowsPerBar = S.RowsPerBeat * S.BeatsPerBar;
	const int Columns = int(Notes.size());
	const int StepsPerBeat = StepsInBeat(S.RowsPerBeat, S.BeatsPerBar), Steps = StepsPerBeat * S.BeatsPerBar;
	// A compound meter's beat (6/8's) is three eighths rather than a triplet
	const bool Compound = S.Meter.find("/8") != std::string::npos && std::atoi(S.Meter.c_str()) % 3 == 0;
	auto IsVrc6 = [&](int c) { return (M.Expansion & EXP_VRC6) && c >= 5 && c <= 7; };
	auto Instr = [&](int i) -> const Instrument * { return i >= 0 && i < int(M.Instruments.size()) ? &M.Instruments[i] : nullptr; };

	// The bass: the triangle, or else the lowest channel
	std::vector<double> AvgPitch(Columns, 0);
	for (int c = 0; c < Columns; ++c) {
		if (!Melodic(Kinds[c]) || Notes[c].empty())
			continue;
		double Sum = 0;
		for (size_t i = 0; i < Notes[c].size(); ++i)
			Sum += Notes[c][i].Note;
		AvgPitch[c] = Sum / Notes[c].size();
	}
	int Bass = -1;
	for (int c = 0; c < Columns; ++c)
		if (Kinds[c] == C_TRIANGLE && Notes[c].size() >= 8)
			Bass = c;
	if (Bass < 0)
		for (int c = 0; c < Columns; ++c)
			if (Melodic(Kinds[c]) && Notes[c].size() >= 8 && (Bass < 0 || AvgPitch[c] < AvgPitch[Bass]))
				Bass = c;
	if (Bass >= 0 && Kinds[Bass] == C_SAMPLE && AvgPitch[Bass] >= 50)
		Bass = -1;		// a tracker module's lowest part, still high up: it has no bass

	if (LoopRow >= 0) {
		S.IntroBars = LoopRow > 0 ? std::max(1, (LoopRow + RowsPerBar / 2) / RowsPerBar) : 0;
		S.LoopBars = std::max(1, (TotalRows - LoopRow + RowsPerBar / 2) / RowsPerBar);
	}
	auto InLoop = [&](int Row) { return LoopRow < 0 || Row >= LoopRow; };
	const int LoopBarCount = std::max(1, S.LoopBars > 0 ? S.LoopBars : (TotalRows + RowsPerBar - 1) / RowsPerBar);

	// The drums' voices: each sample, and the kinds of noise hit
	std::vector<Voice> Voices;
	std::vector<std::vector<int> > VoiceOf(Columns);	// a voice for each note of a drum channel
	{
		std::map<std::pair<int, std::string>, int> Index;
		for (int c = 0; c < Columns; ++c) {
			if (Kinds[c] != C_NOISE && Kinds[c] != C_DPCM && Kinds[c] != C_DRUMS)
				continue;
			for (size_t i = 0; i < Notes[c].size(); ++i) {
				const Played &p = Notes[c][i];
				const Instrument *I = Instr(p.Inst);
				Voice V;
				V.Channel = c;
				V.Dpcm = Kinds[c] == C_DPCM;
				int Pitch = 15;
				if (Kinds[c] == C_DRUMS)
					V.Key = Format("i%d", p.Inst);		// a tracker module's drum: its instrument
				else if (V.Dpcm) {
					if (I)
						for (size_t k = 0; k < I->Keys.size(); ++k)
							if (I->Keys[k].Note == p.Note) {
								V.Sample = I->Keys[k].Sample;
								Pitch = I->Keys[k].Pitch;
							}
					V.Key = V.Sample >= 0 ? Format("s%d", V.Sample) : Format("k%d", p.Note);
				}
				else {
					const VolumeShape v = I ? ShapeOf(I->Macros[SEQ_VOLUME]) : VolumeShape();
					const int Decay = v.Pluck ? v.Length : 0;
					V.DecayClass = Decay == 0 || Decay > 16 ? 2 : (Decay > 5 ? 1 : 0);
					V.PitchClass = p.Note >= 10 ? 2 : (p.Note >= 5 ? 1 : 0);
					V.Key = Format("n%d%d", V.PitchClass, V.DecayClass);
				}
				const std::pair<int, std::string> Id(c, V.Key);
				std::map<std::pair<int, std::string>, int>::const_iterator it = Index.find(Id);
				int n;
				if (it == Index.end()) {
					n = int(Voices.size());
					Index[Id] = n;
					if (V.Dpcm && V.Sample >= 0 && V.Sample < int(M.Samples.size()))
						V.Bright = SampleBrightness(M.Samples[V.Sample], Pitch);
					Voices.push_back(V);
				}
				else
					n = it->second;
				Voices[n].Rows.push_back(p.Row);
				++Voices[n].Insts[p.Inst];
				VoiceOf[c].push_back(n);
			}
		}
		for (size_t n = 0; n < Voices.size(); ++n) {
			Voice &V = Voices[n];
			V.Backbeat = S.BeatsPerBar == 4 ? TwoPlaces(V, RowsPerBar, LoopRow, LoopBarCount) : -1;	// 4/4's backbeat
			if (V.Dpcm)
				continue;
			int Hits = 0;
			for (size_t i = 0; i < V.Rows.size(); ++i)
				Hits += InLoop(V.Rows[i]);
			const double PerBar = double(Hits) / LoopBarCount;
			if (Kinds[V.Channel] == C_DRUMS) {
				// What its name says, or a backbeat's snare
				const Instrument *I = Instr(V.Insts.empty() ? -1 : V.Insts.begin()->first);
				V.Name = I && !I->Drum.empty() ? I->Drum : "drum";
				if (V.Name == "drum" && V.Backbeat >= 0)
					V.Name = "snare";
				continue;
			}
			if (V.DecayClass == 2)
				V.Name = V.PitchClass == 2 ? "cymbal" : (V.PitchClass == 1 ? "open hi-hat or cymbal" : "long low noise");
			else if (V.Backbeat >= 0 && (V.DecayClass == 1 || V.PitchClass >= 1))
				V.Name = "snare";
			else if (V.DecayClass == 0 && PerBar >= 3)
				V.Name = "hi-hat";
			else if (V.PitchClass == 0)
				V.Name = "kick or low hit";
			else if (V.DecayClass == 1)
				V.Name = "snare";
			else
				V.Name = "hi-hat";
		}
	}

	// Bars, from the first bar line
	std::vector<int> SampleSizes;
	for (size_t i = 0; i < M.Samples.size(); ++i)
		SampleSizes.push_back(int(M.Samples[i].size()));
	const int Phase = BarPhase(Notes, Kinds, Bass, Voices, SampleSizes, RowsPerBar, S.RowsPerBeat, LoopRow, Report.FirstBarRow);
	S.FirstBarRow = Phase;
	auto BarOf = [&](int Row) { return Row >= Phase ? (Row - Phase) / RowsPerBar : -1; };
	auto StepOf = [&](int Row) { return ((((Row - Phase) % RowsPerBar) + RowsPerBar) % RowsPerBar) * Steps / RowsPerBar; };
	const int Bars = std::max(1, (TotalRows - Phase + RowsPerBar - 1) / RowsPerBar);
	if (LoopRow >= 0)
		S.LoopStartBar = std::max(0, BarOf(LoopRow));
	const int FirstLoopBar = std::max(0, S.LoopStartBar);

	// --- Key and chords, from the melodic channels ---
	std::vector<double> KeyWeights(12, 0);
	std::vector<std::vector<double> > BarWeights(Bars, std::vector<double>(12, 0)), BassWeights(Bars, std::vector<double>(12, 0));
	for (int c = 0; c < Columns; ++c) {
		if (!Melodic(Kinds[c]))
			continue;
		for (size_t i = 0; i < Notes[c].size(); ++i) {
			const Played &p = Notes[c][i];
			const int Len = std::max(1, std::min(p.Length, RowsPerBar));
			const double Weight = c == Bass ? 1.5 : 1.0;
			std::vector<int> Tones(1, p.Note % 12);
			if (p.Arp) {
				Tones.push_back((p.Note + (p.Arp >> 4)) % 12);
				if (p.Arp & 15)
					Tones.push_back((p.Note + (p.Arp & 15)) % 12);
			}
			for (size_t t = 0; t < Tones.size(); ++t) {
				KeyWeights[Tones[t]] += Len * Weight;
				for (int r = p.Row; r < p.Row + Len; ++r) {
					const int b = BarOf(r);
					if (b < 0 || b >= Bars)
						continue;
					BarWeights[b][Tones[t]] += Weight;
					if (c == Bass && t == 0)
						BassWeights[b][Tones[t]] += (r - Phase) % RowsPerBar == 0 ? 3 : 1;
				}
			}
		}
	}
	S.Key = KeyOf(KeyWeights);
	for (int b = 0; b < Bars && b < 96; ++b) {
		int BassTone = -1;
		double Most = 0;
		for (int k = 0; k < 12; ++k)
			if (BassWeights[b][k] > Most) {
				Most = BassWeights[b][k];
				BassTone = k;
			}
		S.Chords.push_back(ChordOf(BarWeights[b], BassTone));
	}

	// --- The lead: the melodic channel with the most going on, up high ---
	int Lead = -1, NamedLead = -1;
	double LeadScore = -1, NamedScore = -1;
	for (int c = 0; c < Columns; ++c) {
		if (!Melodic(Kinds[c]) || Notes[c].empty())
			continue;
		std::set<int> Pitches;
		int Long = 0;
		for (size_t i = 0; i < Notes[c].size(); ++i) {
			Pitches.insert(Notes[c][i].Note);
			Long += Notes[c][i].Length >= 2;
		}
		double Score = double(Long + 1) * Pitches.size() * (AvgPitch[c] >= 36 ? 1.0 : 0.3) * (Kinds[c] == C_TRIANGLE ? 0.4 : 1.0) *
			(1.0 + (AvgPitch[c] - 40) / 40.0);
		if (Kinds[c] == C_SAMPLE) {
			// A tracker module's quiet parts are seldom the lead: its usual volume counts
			std::vector<int> Volumes;
			for (size_t i = 0; i < Notes[c].size(); ++i)
				Volumes.push_back(Notes[c][i].Volume);
			std::sort(Volumes.begin(), Volumes.end());
			Score *= std::pow(std::max(1, Volumes[Volumes.size() / 2]) / 15.0, 2.0);
		}
		if (Score > LeadScore) {
			LeadScore = Score;
			Lead = c;
		}
		if (Score > NamedScore && NamesTheMelody(Names[c])) {
			NamedScore = Score;
			NamedLead = c;
		}
	}
	if (NamedLead >= 0)
		Lead = NamedLead;	// a part its name calls the melody (a MIDI file's "Melody", "Soprano")

	// Echoes: a channel that repeats another a little later. The lead is the
	// one echoed, not its echo.
	struct EchoFound { int Echoer, Source, Delay, Hits; };
	std::vector<EchoFound> Echoes;
	for (int a = 0; a < Columns; ++a)
		for (int b = a + 1; b < Columns; ++b) {
			if (!Melodic(Kinds[a]) || !Melodic(Kinds[b]) || Notes[a].size() < 8 || Notes[b].size() < 8)
				continue;
			EchoFound Best = { b, a, 0, 0 };
			for (int Way = 0; Way < 2; ++Way) {
				const int From = Way ? b : a, To = Way ? a : b;
				std::map<int, int> AtFrom;
				for (size_t i = 0; i < Notes[From].size(); ++i)
					AtFrom[Notes[From][i].Row] = Notes[From][i].Note;
				for (int d = 1; d <= 2 * S.RowsPerBeat; ++d) {
					int Hits = 0;
					for (size_t i = 0; i < Notes[To].size(); ++i) {
						std::map<int, int>::const_iterator it = AtFrom.find(Notes[To][i].Row - d);
						if (it != AtFrom.end() && (it->second == Notes[To][i].Note || std::abs(it->second - Notes[To][i].Note) == 12))
							++Hits;
					}
					if (Hits > Best.Hits) {
						EchoFound e = { To, From, d, Hits };
						Best = e;
					}
				}
			}
			if (Best.Hits * 2 >= int(Notes[Best.Echoer].size()))
				Echoes.push_back(Best);
		}
	for (size_t i = 0; i < Echoes.size(); ++i)
		if (Echoes[i].Echoer == Lead)
			Lead = Echoes[i].Source;

	// --- Each channel: its role, register, rhythm and intervals, and its sounds ---
	struct Traits { int Notes = 0, Click = 0, Bloom = 0, Swell = 0, Echo = 0, Restrike = 0, Stepped = 0, Pluck = 0, Scoop = 0,
		Fall = 0, Vibrato = 0, Flick = 0, Chord = 0, Arp = 0, Cut = 0, Sweep = 0, Thump = 0, Tremolo = 0,
		PatVibrato = 0, Glide = 0, Bend = 0, Fade = 0, PatTremolo = 0; };	// the last: effects written in the patterns
	struct MacroSet { Macro M[SEQ_TYPES]; };
	std::vector<Traits> Tr(Columns);
	std::vector<std::string> Roles(Columns);
	for (int c = 0; c < Columns; ++c) {
		if (Notes[c].empty() || !Melodic(Kinds[c]))
			continue;
		// The macros a note plays with: its instrument's, with the Vxx duty
		// when the instrument has none
		auto MacrosOf = [&](const Played &p, const Instrument &I) {
			MacroSet Set;
			for (int m = 0; m < SEQ_TYPES; ++m)
				Set.M[m] = I.Macros[m];
			if (I.Kind == INST_FDS || I.Kind == INST_N163 || Kinds[c] != C_PULSE)
				Set.M[SEQ_DUTY] = Macro();
			else if (Set.M[SEQ_DUTY].Empty())
				Set.M[SEQ_DUTY] = Macro(std::vector<int>(1, std::max(0, p.Duty)));
			return Set;
		};
		Traits &t = Tr[c];
		std::map<int, int> Duties, Lengths;
		int Low = 999, High = -1;
		std::vector<int> All;
		int OnEighth = 0, Off = 0, Syncopated = 0;
		int Repeat = 0, Step = 0, Skip = 0, Leap = 0, Octave = 0, Pairs = 0;
		for (size_t i = 0; i < Notes[c].size(); ++i) {
			const Played &p = Notes[c][i];
			++t.Notes;
			Low = std::min(Low, p.Note);
			High = std::max(High, p.Note);
			All.push_back(p.Note);
			++Lengths[std::min(p.Length, 32)];
			const int Pos = StepOf(p.Row);
			if (Pos % StepsPerBeat != 0) {
				if (StepsPerBeat == 4 && Pos % 2 == 0)
					++OnEighth;
				else
					++Off;
			}
			// Starts off the beat and holds over the next one
			const int RowInBeat = ((p.Row - Phase) % S.RowsPerBeat + S.RowsPerBeat) % S.RowsPerBeat;
			if (RowInBeat != 0 && RowInBeat + p.Length > S.RowsPerBeat)
				++Syncopated;
			if (i > 0 && p.Row - Notes[c][i - 1].Row <= 2 * S.RowsPerBeat) {
				const int d = std::abs(p.Note - Notes[c][i - 1].Note);
				++Pairs;
				if (d == 0)
					++Repeat;
				else if (d <= 2)
					++Step;
				else if (d <= 4)
					++Skip;
				else if (d % 12 == 0)
					++Octave;
				else
					++Leap;
			}
			if (p.Arp)
				++t.Arp;
			if (p.Cut > 0)
				++t.Cut;
			if (p.Sweep)
				++t.Sweep;
			t.PatVibrato += p.Vibrato;
			t.Glide += p.Glide;
			t.Bend += p.Bend;
			t.Fade += p.Fade;
			t.PatTremolo += p.Tremolo;
			const Instrument *I = Instr(p.Inst);
			if (!I)
				continue;
			if (Kinds[c] != C_TRIANGLE && Kinds[c] != C_FM) {
				const VolumeShape v = ShapeOf(I->Macros[SEQ_VOLUME]);
				t.Bloom += v.Bloom;
				t.Swell += v.Swell;
				t.Echo += v.Echo || v.ReleaseEcho;
				t.Restrike += v.Restrike;
				t.Stepped += v.Stepped;
				t.Pluck += v.Pluck;
				t.Tremolo += v.Tremolo;
			}
			const DutyShape d = DutyOf(MacrosOf(p, *I).M[SEQ_DUTY]);
			t.Click += d.Click;
			if (Kinds[c] == C_PULSE && d.Main >= 0)
				++Duties[d.Main];
			const ArpShape a = ArpOf(I->Macros[SEQ_ARPEGGIO]);
			if (a.Scoop) {
				++t.Scoop;
				if (Kinds[c] == C_TRIANGLE && a.ScoopFrom >= 2)
					++t.Thump;
			}
			t.Fall += a.Fall;
			t.Chord += a.Chord;
			const PitchShape ps = PitchOf(I->Macros[SEQ_PITCH]);
			t.Vibrato += ps.Vibrato;
			t.Flick += ps.Flick;
		}
		std::sort(All.begin(), All.end());
		const int Mid1 = All[All.size() / 4], Mid2 = All[All.size() * 3 / 4], Typical = All[All.size() / 2];
		int CommonLength = 1, Most = 0;
		for (std::map<int, int>::const_iterator it = Lengths.begin(); it != Lengths.end(); ++it)
			if (it->second > Most) {
				Most = it->second;
				CommonLength = it->first;
			}
		std::string &Role = Roles[c];
		int EchoOf = -1;
		for (size_t i = 0; i < Echoes.size(); ++i)
			if (Echoes[i].Echoer == c)
				EchoOf = Echoes[i].Source;
		if (c == Lead)
			Role = "the lead";
		else if (EchoOf >= 0)
			Role = "an echo of " + Names[EchoOf];
		else if (c == Bass)
			Role = "bass";
		else if (t.Arp * 2 >= t.Notes || t.Chord * 2 >= t.Notes)
			Role = "chords as arpeggios";
		else if (Kinds[c] == C_SAMPLE && CommonLength >= RowsPerBar / 2)
			Role = "held notes (a pad)";
		else if (AvgPitch[c] < 34)
			Role = "a low part";
		else
			Role = "a second part";
		std::string Line = Format("%s, %s: %s from %s to %s, mostly %s to %s; most often %s long", Names[c].c_str(),
			Role.c_str(), Count(t.Notes, "note", "notes").c_str(), NoteName(Low).c_str(), NoteName(High).c_str(),
			NoteName(Mid1).c_str(), NoteName(Mid2).c_str(), Count(CommonLength, "row", "rows").c_str());
		// Rhythm
		std::string Rhythm;
		if (Off * 4 >= t.Notes)
			Rhythm = StepsPerBeat != 3 ? "busy, on sixteenths" : (Compound ? "on the eighths between the beats" : "on the triplets");
		else if (OnEighth * 4 >= t.Notes)
			Rhythm = "on the beats and the eighths between";
		else
			Rhythm = "mostly on the beats";
		if (Syncopated * 5 >= t.Notes)
			Rhythm += Format(", syncopated (%s of its notes start off the beat and hold over it)", Percent(Syncopated, t.Notes).c_str());
		Line += "; rhythm " + Rhythm;
		// The bar it plays most, as a grid; for the bass, with its notes
		{
			std::vector<std::string> Grids(Bars), Figures(Bars);
			std::vector<int> FirstNote(Bars, -1);
			for (size_t i = 0; i < Notes[c].size(); ++i) {
				const Played &p = Notes[c][i];
				const int b = BarOf(p.Row);
				if (b < FirstLoopBar || b >= Bars || !InLoop(p.Row))
					continue;
				if (Grids[b].empty())
					Grids[b] = std::string(Steps, '.');
				Grids[b][StepOf(p.Row)] = 'x';
				if (FirstNote[b] < 0)
					FirstNote[b] = p.Note;
				Figures[b] += (Figures[b].empty() ? "" : " ") + Format("%+d", p.Note - FirstNote[b]);
			}
			int BarsPlayed = 0;
			for (int b = 0; b < Bars; ++b) {
				BarsPlayed += !Grids[b].empty();
				if (!Figures[b].empty())
					Figures[b] = GridText(Grids[b], StepsPerBeat) + " = " + Figures[b];
			}
			int Uses = 0;
			const std::string Grid = Commonest(Grids, Uses);
			if (Uses >= 2 && Uses * 5 >= BarsPlayed)
				Line += Format("; its commonest bar rhythm (%s of %d bars): %s", Percent(Uses, BarsPlayed).c_str(), BarsPlayed,
					GridText(Grid, StepsPerBeat).c_str());
			if (c == Bass) {
				const std::string Figure = Commonest(Figures, Uses);
				if (Uses >= 2 && Uses * 5 >= BarsPlayed)
					Line += Format("; its commonest bar, in semitones from the bar's first note (%s of the bars): %s",
						Percent(Uses, BarsPlayed).c_str(), Figure.c_str());
			}
		}
		// Intervals
		if (Pairs >= 4) {
			std::vector<std::string> Moves;
			if (Repeat * 5 >= Pairs)
				Moves.push_back(Percent(Repeat, Pairs) + " repeated notes");
			if (Step * 5 >= Pairs)
				Moves.push_back(Percent(Step, Pairs) + " steps");
			if (Skip * 5 >= Pairs)
				Moves.push_back(Percent(Skip, Pairs) + " thirds");
			if (Leap * 5 >= Pairs)
				Moves.push_back(Percent(Leap, Pairs) + " leaps");
			if (Octave * 5 >= Pairs)
				Moves.push_back(Percent(Octave, Pairs) + " octave jumps");
			if (!Moves.empty())
				Line += "; moves by " + Join(Moves, ", ");
		}
		if (!Duties.empty() && Kinds[c] == C_PULSE) {
			int Best = 0, BestCount = -1;
			for (std::map<int, int>::const_iterator it = Duties.begin(); it != Duties.end(); ++it)
				if (it->second > BestCount) {
					BestCount = it->second;
					Best = it->first;
				}
			Line += Format("; duty mostly %s", DutyName(IsVrc6(c), Best));
			if (Duties.size() > 1)
				Line += Format(" (%d duties in all)", int(Duties.size()));
		}
		{
			std::vector<int> Volumes;
			for (size_t i = 0; i < Notes[c].size(); ++i)
				Volumes.push_back(Notes[c][i].Volume);
			std::sort(Volumes.begin(), Volumes.end());
			const int Median = Volumes[Volumes.size() / 2], Quiet = Volumes[Volumes.size() / 10], Loud = Volumes[Volumes.size() * 9 / 10];
			if (Kinds[c] != C_TRIANGLE && (Median < 15 || Quiet < Loud - 3))
				Line += Quiet == Loud ? Format("; played at volume %d", Median) : Format("; played at volumes %d to %d, mostly %d", Quiet, Loud, Median);
		}
		if (t.Cut * 5 >= t.Notes)
			Line += Format("; %s of its notes cut short with Sxx (staccato)", Percent(t.Cut, t.Notes).c_str());
		if (t.Arp * 5 >= t.Notes)
			Line += Format("; %s as fast arpeggio chords (0xy)", Percent(t.Arp, t.Notes).c_str());
		if (t.Sweep > 0)
			Line += Format("; the hardware sweep on %d notes", t.Sweep);
		if (t.PatVibrato * 10 >= t.Notes)
			Line += Format("; vibrato on %s of its notes (4xy)", Percent(t.PatVibrato, t.Notes).c_str());
		if (t.Glide * 10 >= t.Notes)
			Line += Format("; %s of its notes glide in from the one before (3xx)", Percent(t.Glide, t.Notes).c_str());
		if (t.Bend * 10 >= t.Notes)
			Line += Format("; pitch slides on %s of its notes (1xx, 2xx)", Percent(t.Bend, t.Notes).c_str());
		if (t.Fade * 10 >= t.Notes)
			Line += Format("; volume slides on %s of its notes (Axy)", Percent(t.Fade, t.Notes).c_str());
		if (t.PatTremolo * 10 >= t.Notes)
			Line += Format("; tremolo on %s of its notes (7xy)", Percent(t.PatTremolo, t.Notes).c_str());
		if (Kinds[c] == C_SAMPLE) {
			// A tracker module's channel: the instruments it plays, by name
			std::map<int, int> Uses;
			for (size_t i = 0; i < Notes[c].size(); ++i)
				++Uses[Notes[c][i].Inst];
			std::vector<std::pair<int, int> > ByUse;
			for (std::map<int, int>::const_iterator it = Uses.begin(); it != Uses.end(); ++it)
				ByUse.push_back(std::make_pair(it->second, it->first));
			std::sort(ByUse.rbegin(), ByUse.rend());
			std::vector<std::string> Used;
			for (size_t k = 0; k < ByUse.size() && k < 4; ++k) {
				const Instrument *I = Instr(ByUse[k].second);
				if (I && ByUse[k].first * 10 >= t.Notes)
					Used.push_back("'" + I->Name + "' (" + Percent(ByUse[k].first, t.Notes) + ")");
			}
			if (!Used.empty())
				Line += "; its instruments: " + Join(Used, ", ");
			S.Channels.push_back(Line + ".");
			continue;	// sampled sounds: none to build again on a chip
		}
		S.Channels.push_back(Line + ".");

		// Its sounds: the notes grouped by the character of their instruments
		struct Group { int Notes = 0; std::map<std::pair<int, int>, int> Uses; };
		std::map<std::string, Group> Groups;
		for (size_t i = 0; i < Notes[c].size(); ++i) {
			const Played &p = Notes[c][i];
			const Instrument *I = Instr(p.Inst);
			if (!I || I->Kind == INST_VRC7)
				continue;
			const MacroSet Set = MacrosOf(p, *I);
			Group &g = Groups[SoundKey(Set.M, Kinds[c] == C_TRIANGLE)];
			++g.Notes;
			++g.Uses[std::make_pair(p.Inst, Set.M[SEQ_DUTY].Empty() ? -1 : Set.M[SEQ_DUTY].Items[0])];
		}
		std::vector<std::pair<int, std::string> > Order;
		for (std::map<std::string, Group>::const_iterator it = Groups.begin(); it != Groups.end(); ++it)
			Order.push_back(std::make_pair(it->second.Notes, it->first));
		std::sort(Order.rbegin(), Order.rend());
		for (size_t k = 0; k < Order.size() && k < 3; ++k) {
			if (k > 0 && Order[k].first * 100 < t.Notes * 15)
				break;
			const Group &g = Groups[Order[k].second];
			std::pair<int, int> Top(-1, -1);
			int TopCount = -1;
			for (std::map<std::pair<int, int>, int>::const_iterator it = g.Uses.begin(); it != g.Uses.end(); ++it)
				if (it->second > TopCount) {
					TopCount = it->second;
					Top = it->first;
				}
			const Instrument *I = Instr(Top.first);
			if (!I)
				continue;
			Played Probe;
			Probe.Duty = Top.second;
			StudySound Sd;
			Sd.Channel = Names[c];
			Sd.Role = Role;
			Sd.Kind = I->Kind;
			Sd.Sawtooth = Kinds[c] == C_SAW;
			Sd.Triangle = Kinds[c] == C_TRIANGLE;
			Sd.Notes = g.Notes;
			Sd.Share = (g.Notes * 100 + t.Notes / 2) / t.Notes;
			Sd.TypicalNote = Typical;
			const MacroSet Set = MacrosOf(Probe, *I);
			for (int m = 0; m < SEQ_TYPES; ++m)
				Sd.Macros[m] = Set.M[m];
			Sd.Words = DescribeSound(Set.M, I->Kind == INST_VRC6, Typical, false, Sd.Triangle, Sd.Sawtooth);
			S.Sounds.push_back(Sd);
		}
	}

	// --- The form: the lead's four-bar phrases in the loop ---
	if (Lead >= 0 && S.LoopBars >= 8) {
		std::vector<std::vector<std::pair<int, int> > > BarNotes(Bars);	// (step, note)
		for (size_t i = 0; i < Notes[Lead].size(); ++i) {
			const Played &p = Notes[Lead][i];
			const int b = BarOf(p.Row);
			if (b >= 0 && b < Bars)
				BarNotes[b].push_back(std::make_pair(StepOf(p.Row), p.Note));
		}
		std::vector<std::vector<std::pair<int, int> > > Phrases;	// (step in the phrase, note)
		std::vector<std::string> Letters;
		char Next = 'A';
		for (int b = FirstLoopBar; b + 4 <= Bars && b < FirstLoopBar + S.LoopBars; b += 4) {
			std::vector<std::pair<int, int> > Ph;
			for (int k = 0; k < 4; ++k)
				for (size_t i = 0; i < BarNotes[b + k].size(); ++i)
					Ph.push_back(std::make_pair(k * Steps + BarNotes[b + k][i].first, BarNotes[b + k][i].second));
			std::string Letter;
			double BestSame = -1;
			bool Exact = false;
			for (size_t q = 0; q < Phrases.size() && !Exact; ++q) {
				const std::vector<std::pair<int, int> > &O = Phrases[q];
				if (O.empty() || Ph.empty() || Letters[q] == "-")
					continue;
				// Notes on the same steps: the same notes, or all moved by one interval
				std::map<int, int> AtStep;
				for (size_t i = 0; i < O.size(); ++i)
					AtStep[O[i].first] = O[i].second;
				int Same = 0, Rhythm = 0;
				std::map<int, int> Shifts;
				for (size_t i = 0; i < Ph.size(); ++i) {
					std::map<int, int>::const_iterator it = AtStep.find(Ph[i].first);
					if (it == AtStep.end())
						continue;
					++Rhythm;
					Same += it->second == Ph[i].second;
					++Shifts[Ph[i].second - it->second];
				}
				const double Size = double(std::max(O.size(), Ph.size()));
				int Shifted = 0;
				for (std::map<int, int>::const_iterator it = Shifts.begin(); it != Shifts.end(); ++it)
					if (it->first != 0)
						Shifted = std::max(Shifted, it->second);
				const double Sameness = Same / Size;
				if (Sameness >= 0.85) {
					Letter = Letters[q].substr(0, 1);
					Exact = true;
				}
				else if ((Sameness >= 0.5 || (Rhythm / Size >= 0.8 && Shifted / Size >= 0.6)) && Sameness > BestSame) {
					BestSame = Sameness;
					Letter = Letters[q].substr(0, 1) + "'";
				}
			}
			if (Letter.empty())
				Letter = Ph.empty() ? "-" : std::string(1, Next++);
			Phrases.push_back(Ph);
			Letters.push_back(Letter);
		}
		S.Form = Join(Letters, " ");
	}

	// --- Tricks between channels: an echo, harmony, notes traded ---
	std::vector<std::string> PairTricks;
	for (size_t i = 0; i < Echoes.size(); ++i) {
		const EchoFound &e = Echoes[i];
		PairTricks.push_back(Format("an echo: %s repeats %s's notes %s later (%s of its notes)", Names[e.Echoer].c_str(),
			Names[e.Source].c_str(), Count(e.Delay, "row", "rows").c_str(), Percent(e.Hits, int(Notes[e.Echoer].size())).c_str()));
	}
	for (int a = 0; a < Columns; ++a) {
		for (int b = a + 1; b < Columns; ++b) {
			if (!Melodic(Kinds[a]) || !Melodic(Kinds[b]) || Notes[a].size() < 8 || Notes[b].size() < 8)
				continue;
			bool Echo = false;
			for (size_t i = 0; i < Echoes.size(); ++i)
				Echo = Echo || (Echoes[i].Echoer == a && Echoes[i].Source == b) || (Echoes[i].Echoer == b && Echoes[i].Source == a);
			if (Echo)
				continue;
			std::map<int, int> AtA;
			for (size_t i = 0; i < Notes[a].size(); ++i)
				AtA[Notes[a][i].Row] = Notes[a][i].Note;
			std::map<int, int> Intervals;
			int Together = 0;
			for (size_t i = 0; i < Notes[b].size(); ++i) {
				std::map<int, int>::const_iterator it = AtA.find(Notes[b][i].Row);
				if (it == AtA.end())
					continue;
				++Together;
				++Intervals[std::abs(it->second - Notes[b][i].Note) % 12];
			}
			if (Together * 2 >= int(std::min(Notes[a].size(), Notes[b].size()))) {
				const int Thirds = Intervals[3] + Intervals[4], Sixths = Intervals[8] + Intervals[9], Same = Intervals[0];
				if (Thirds * 2 >= Together)
					PairTricks.push_back(Format("harmony in thirds: %s and %s strike together a third apart (%s of the time)",
						Names[a].c_str(), Names[b].c_str(), Percent(Thirds, Together).c_str()));
				else if (Sixths * 2 >= Together)
					PairTricks.push_back(Format("harmony in sixths between %s and %s", Names[a].c_str(), Names[b].c_str()));
				else if (Same * 2 >= Together)
					PairTricks.push_back(Format("%s doubles %s (the same notes or octaves): a thicker sound", Names[b].c_str(), Names[a].c_str()));
				else if (a != Bass && b != Bass && Together * 3 >= int(Notes[b].size()) * 2)
					PairTricks.push_back(Format("%s and %s play in rhythm together, on different notes (chords between them)",
						Names[a].c_str(), Names[b].c_str()));
				continue;
			}
			if (std::fabs(AvgPitch[a] - AvgPitch[b]) <= 7 && Notes[b].size() >= 16) {
				std::set<int> RowsA;
				for (size_t i = 0; i < Notes[a].size(); ++i)
					RowsA.insert(Notes[a][i].Row);
				int Alternating = 0;
				for (size_t i = 0; i < Notes[b].size(); ++i) {
					const int r = Notes[b][i].Row;
					if (!RowsA.count(r) && (RowsA.count(r - 1) || RowsA.count(r + 1)))
						++Alternating;
				}
				if (Alternating * 2 >= int(Notes[b].size()))
					PairTricks.push_back(Format("notes traded between %s and %s: one plays while the other rests, a row apart (%s of %s's notes)",
						Names[a].c_str(), Names[b].c_str(), Percent(Alternating, int(Notes[b].size())).c_str(), Names[b].c_str()));
			}
		}
	}

	// --- The techniques over the whole track ---
	struct Trick { std::string Words; int Notes; };
	std::vector<Trick> Tricks;
	int MelodicNotes = 0;
	Traits Sum;
	for (int c = 0; c < Columns; ++c) {
		if (!Melodic(Kinds[c]))
			continue;
		MelodicNotes += Tr[c].Notes;
		Sum.Click += Tr[c].Click;
		Sum.Bloom += Tr[c].Bloom;
		Sum.Swell += Tr[c].Swell;
		Sum.Echo += Tr[c].Echo;
		Sum.Restrike += Tr[c].Restrike;
		Sum.Stepped += Tr[c].Stepped;
		Sum.Scoop += Tr[c].Scoop - Tr[c].Thump;
		Sum.Vibrato += Tr[c].Vibrato;
		Sum.Flick += Tr[c].Flick;
		Sum.Arp += Tr[c].Arp + Tr[c].Chord;
		Sum.Thump += Tr[c].Thump;
		Sum.Cut += Tr[c].Cut;
		Sum.Fall += Tr[c].Fall;
		Sum.Tremolo += Tr[c].Tremolo;
		Sum.PatVibrato += Tr[c].PatVibrato;
		Sum.Glide += Tr[c].Glide;
		Sum.Bend += Tr[c].Bend;
		Sum.Fade += Tr[c].Fade;
		Sum.PatTremolo += Tr[c].PatTremolo;
	}
	auto Add = [&](int Uses, const std::string &Words) {
		if (Uses > 0 && Uses * 20 >= MelodicNotes)
			Tricks.push_back(Trick{ Words + Format(" (%s of the notes)", Percent(Uses, MelodicNotes).c_str()), Uses });
	};
	Add(Sum.Click, "a duty click on the attack: the first frame at another duty, so the note speaks");
	Add(Sum.Bloom, "attacks that bloom to full volume in 2-3 frames instead of starting at it");
	Add(Sum.Swell, "notes that swell in slowly");
	Add(Sum.Echo, "an echo built into the volume envelope: the note drops, then comes back softly");
	Add(Sum.Restrike, "held notes struck again inside the envelope (a dip and a rise), so long notes keep the rhythm");
	Add(Sum.Tremolo, "a tremolo: the volume pulses while the note holds");
	Add(Sum.Stepped, "decays in steps: the volume holds, then drops a few levels at a time");
	Add(Sum.Scoop, "notes that slide into their pitch from a semitone or two away in the first frames");
	Add(Sum.Flick, "a quick pitch flick on the attack");
	Add(Sum.Fall, "notes that fall or turn at their end");
	Add(Sum.Vibrato, "vibrato built into the instruments, usually after a delay");
	Add(Sum.Arp, "chords played as fast arpeggios");
	Add(Sum.Thump, "a triangle thump under the beat (the bass note starts higher and drops)");
	Add(Sum.Cut, "staccato: notes cut short of the next one");
	Add(Sum.PatVibrato, "vibrato written into the patterns (4xy)");
	Add(Sum.Glide, "notes that glide into the next (3xx portamento)");
	Add(Sum.Bend, "pitch slides up or down (1xx, 2xx)");
	Add(Sum.Fade, "volume slides: notes that fade out or swell in (Axy)");
	Add(Sum.PatTremolo, "tremolo written into the patterns (7xy)");
	std::sort(Tricks.begin(), Tricks.end(), [](const Trick &a, const Trick &b) { return a.Notes > b.Notes; });
	for (size_t i = 0; i < Tricks.size(); ++i)
		S.Tricks.push_back(Tricks[i].Words);
	if (PairTricks.size() > 8)
		PairTricks.resize(8);		// many channels (a tracker module's): the first found
	S.Tricks.insert(S.Tricks.end(), PairTricks.begin(), PairTricks.end());

	// --- Drums: the voices of the noise channel and the samples, where they fall in the bar ---
	for (size_t n = 0; n < Voices.size(); ++n) {
		Voice &V = Voices[n];
		if (Kinds[V.Channel] != C_DRUMS)
			continue;
		// A tracker module's drum: its kind and its instrument's name; one its
		// name doesn't place, by where it falls
		int Hits = 0, OnBeats = 0, Offbeats = 0;
		for (size_t i = 0; i < V.Rows.size(); ++i) {
			if (!InLoop(V.Rows[i]))
				continue;
			const int Pos = StepOf(V.Rows[i]);
			++Hits;
			OnBeats += Pos % StepsPerBeat == 0;
			Offbeats += StepsPerBeat == 4 && Pos % 4 == 2;
		}
		if (V.Name == "drum" && Hits > 0) {
			if (Hits >= 6 * LoopBarCount)
				V.Name = "drum, steady eighths or faster";
			else if (OnBeats * 2 >= Hits)
				V.Name = "drum, kick-like";
			else if (Offbeats * 2 >= Hits)
				V.Name = "drum on the off-beats";
		}
		const Instrument *I = Instr(V.Insts.empty() ? -1 : V.Insts.begin()->first);
		if (I && !I->Name.empty())
			V.Name += " '" + I->Name + "'";
	}
	for (size_t n = 0; n < Voices.size(); ++n) {
		Voice &V = Voices[n];
		if (!V.Dpcm)
			continue;
		std::vector<int> AtStep(Steps, 0);
		int Hits = 0, OnBeats = 0, Backbeats = 0, Offbeats = 0;
		for (size_t i = 0; i < V.Rows.size(); ++i) {
			if (!InLoop(V.Rows[i]))
				continue;
			const int Pos = StepOf(V.Rows[i]);
			++Hits;
			OnBeats += Pos % StepsPerBeat == 0;
			Backbeats += Pos == StepsPerBeat || Pos == 3 * StepsPerBeat;
			Offbeats += StepsPerBeat == 4 && Pos % 4 == 2;
		}
		if (V.Sample >= 0 && V.Sample < int(M.Samples.size()))
			V.Name = V.Bright > 0 ? Format("sample %d (%d bytes, %s)", V.Sample, int(M.Samples[V.Sample].size()), SampleSound(V.Bright))
				: Format("sample %d (%d bytes)", V.Sample, int(M.Samples[V.Sample].size()));
		else
			V.Name = "a sample";
		if (Hits == 0 || (V.Sample >= 0 && V.Sample < int(M.Samples.size()) && M.Samples[V.Sample].size() < 17))
			continue;
		if (Backbeats * 2 >= Hits)
			V.Name += ", snare-like (on beats 2 and 4)";
		else if (Offbeats * 2 >= Hits)
			V.Name += ", on the off-beats (the 'and' of each beat)";
		else if (OnBeats * 2 >= Hits)
			V.Name += ", kick-like";
	}
	for (int c = 0; c < Columns; ++c) {
		if ((Kinds[c] != C_NOISE && Kinds[c] != C_DPCM && Kinds[c] != C_DRUMS) || Notes[c].empty())
			continue;
		struct Kit { std::vector<int> Steps; std::vector<std::string> Grids; std::map<int, int> Insts; int Hits = 0; };
		std::map<std::string, Kit> Kits;
		std::vector<std::string> Order;
		for (size_t i = 0; i < Notes[c].size(); ++i) {
			const Played &p = Notes[c][i];
			if (!InLoop(p.Row))
				continue;
			const std::string &Kind = Voices[VoiceOf[c][i]].Name;
			if (!Kits.count(Kind))
				Order.push_back(Kind);
			Kit &k = Kits[Kind];
			k.Steps.push_back(StepOf(p.Row));
			++k.Insts[p.Inst];
			++k.Hits;
			const int b = BarOf(p.Row);
			if (b >= 0 && b < Bars) {
				if (int(k.Grids.size()) < Bars)
					k.Grids.resize(Bars);
				if (k.Grids[b].empty())
					k.Grids[b] = std::string(Steps, '.');
				k.Grids[b][StepOf(p.Row)] = 'x';
			}
		}
		int ChannelHits = 0;
		for (std::map<std::string, Kit>::const_iterator it = Kits.begin(); it != Kits.end(); ++it)
			ChannelHits += it->second.Hits;
		std::vector<std::string> Parts;
		for (size_t o = 0; o < Order.size(); ++o) {
			const std::string &Name = Order[o];
			const Kit &k = Kits[Name];
			if (k.Hits < 2)
				continue;
			std::vector<int> AtStep(Steps, 0);
			for (size_t i = 0; i < k.Steps.size(); ++i)
				++AtStep[k.Steps[i]];
			std::string w = Format("%s: %s in the loop, about %.1f a bar", Name.c_str(), Count(k.Hits, "hit", "hits").c_str(),
				double(k.Hits) / LoopBarCount);
			int Uses = 0;
			const std::string Grid = Commonest(k.Grids, Uses);
			int BarsPlayed = 0;
			for (size_t b = 0; b < k.Grids.size(); ++b)
				BarsPlayed += !k.Grids[b].empty();
			if (Uses >= 2 && Uses * 5 >= BarsPlayed)
				w += Format(", most often %s (%s of the bars it plays in)", GridText(Grid, StepsPerBeat).c_str(), Percent(Uses, BarsPlayed).c_str());
			else {
				std::vector<std::string> Beats;
				for (int pos = 0; pos < Steps; ++pos)
					if (AtStep[pos] * 2 >= LoopBarCount)
						Beats.push_back(BeatName(pos, StepsPerBeat));
				if (!Beats.empty() && int(Beats.size()) < Steps * 3 / 4)
					w += ", most bars on " + Join(Beats, ", ");
			}
			// The instrument it uses most
			int Inst = -1, InstUses = -1;
			for (std::map<int, int>::const_iterator j = k.Insts.begin(); j != k.Insts.end(); ++j)
				if (j->second > InstUses) {
					InstUses = j->second;
					Inst = j->first;
				}
			const Instrument *I = Instr(Inst);
			if (Kinds[c] == C_NOISE && I) {
				const std::string Sound = DescribeSound(I->Macros, false, 0, true, false, false);
				w += " (" + Sound + ")";
				if (k.Hits * 10 >= ChannelHits) {
					StudySound Sd;
					Sd.Channel = Names[c];
					Sd.Role = Name;
					Sd.Kind = INST_2A03;
					Sd.Noise = true;
					Sd.Notes = k.Hits;
					Sd.Share = (k.Hits * 100 + ChannelHits / 2) / std::max(1, ChannelHits);
					Sd.TypicalNote = 0;
					for (int m = 0; m < SEQ_TYPES; ++m)
						Sd.Macros[m] = I->Macros[m];
					Sd.Words = Sound;
					S.Sounds.push_back(Sd);
				}
			}
			Parts.push_back(w);
		}
		if (!Parts.empty())
			S.Drums.push_back(Names[c] + ": " + Join(Parts, "; ") + ".");
	}
	return S;
}

std::string DescribeStudy(const TrackStudy &S, bool Macros)
{
	std::string s = S.Name + "\n";
	// The meter, when the music says: its beats when they aren't its top number (6/8's two)
	std::string In;
	if (!S.Meter.empty()) {
		In = " in " + S.Meter;
		if (std::atoi(S.Meter.c_str()) != S.BeatsPerBar)
			In += ", " + Count(S.BeatsPerBar, "beat", "beats") + " a bar";
	}
	if (S.Tracker)
		s += Format("  Tempo: %.0f BPM%s, %d rows a beat (speed %d, tempo %d)", S.Bpm, In.c_str(), S.RowsPerBeat, S.Speed, S.Tempo);
	else
		s += Format("  Tempo: %.0f BPM%s, %d rows a beat (rows of %.2g frames)", S.Bpm, In.c_str(), S.RowsPerBeat, S.TicksPerRow);
	if (S.LoopBars > 0)
		s += S.IntroBars > 0 ? Format("; an intro of %s, then a loop of %s", Count(S.IntroBars, "bar", "bars").c_str(),
			Count(S.LoopBars, "bar", "bars").c_str()) : Format("; a loop of %s", Count(S.LoopBars, "bar", "bars").c_str());
	else
		s += "; it plays once";
	s += ".";
	if (S.FirstBarRow > 0)
		s += Format(" The first bar line is %s in.", Count(S.FirstBarRow, "row", "rows").c_str());
	if (!S.Key.empty())
		s += " Key: " + S.Key + ".";
	s += "\n";
	if (!S.Form.empty())
		s += "  The lead's four-bar phrases through the loop: " + S.Form + "\n";
	if (!S.Chords.empty()) {
		const size_t Loop = S.LoopStartBar >= 0 ? std::min(size_t(S.LoopStartBar), S.Chords.size()) : 0;
		if (Loop > 0)
			s += "  Chords of the intro, a bar each: " + ChordLine(S.Chords, 0, Loop) + "\n";
		const size_t End = std::min(S.Chords.size(), Loop + 48);
		s += std::string("  Chords") + (S.LoopStartBar >= 0 ? " of the loop" : "") + ", a bar each: " + ChordLine(S.Chords, Loop, End) +
			(S.Chords.size() > End ? " ..." : "") + "\n";
	}
	for (size_t i = 0; i < S.Channels.size(); ++i)
		s += "  " + S.Channels[i] + "\n";
	for (size_t i = 0; i < S.Drums.size(); ++i)
		s += "  " + S.Drums[i] + "\n";
	if (!S.Tricks.empty()) {
		s += "  Techniques:\n";
		for (size_t i = 0; i < S.Tricks.size(); ++i)
			s += "  - " + S.Tricks[i] + "\n";
	}
	if (!S.Sounds.empty()) {
		s += "  Sounds:\n";
		for (size_t i = 0; i < S.Sounds.size(); ++i) {
			const StudySound &d = S.Sounds[i];
			s += Format("  - %s, %s (%d%% of its notes): ", d.Channel.c_str(), d.Role.c_str(), d.Share) + d.Words + "\n";
			if (Macros) {
				static const char *const NAMES[SEQ_TYPES] = { "volume", "arpeggio", "pitch", "hi-pitch", "duty" };
				std::vector<std::string> m;
				for (int t = 0; t < SEQ_TYPES; ++t)
					if (!d.Macros[t].Empty())
						m.push_back(std::string(NAMES[t]) + " " + MacroText(d.Macros[t]));
				if (!m.empty())
					s += "      " + Join(m, "; ") + "\n";
			}
		}
	}
	return s;
}

std::vector<TrackSurvey> SurveyTracks(const NsfFile &File, double MaxSeconds, const std::function<bool(int, int)> &Progress)
{
	std::vector<TrackSurvey> Out;
	for (int t = 0; t < File.Songs; ++t) {
		if (Progress && !Progress(t, File.Songs))
			break;
		TrackSurvey Sv;
		Sv.Track = t;
		Sv.Title = TrackTitle(File, t);
		const int Ms = File.TrackTime(t);
		if (Ms > 0) {
			// The playlist's length: a loop or two of a tune, a few seconds of a jingle
			Sv.Seconds = Ms / 1000.0;
			Sv.Kind = Sv.Seconds >= 20 ? "music" : (Sv.Seconds >= 2.5 ? "jingle" : "sound effect");
			Out.push_back(Sv);
			continue;
		}
		Recording R;
		RecordOptions Record;
		Record.MaxSeconds = MaxSeconds;
		std::string Why;
		if (!RecordTrack(File, t, Record, R, Why)) {
			Sv.Kind = "silent";
			Out.push_back(Sv);
			continue;
		}
		int Channels = 0, Melodic = 0, LastSound = -1;
		for (size_t c = 0; c < R.Channels.size(); ++c) {
			const int Ch = R.Channels[c];
			int Sounding = 0;
			for (int k = 0; k < R.Length; ++k)
				if (R.Ticks[Ch][k].Sounding()) {
					++Sounding;
					LastSound = std::max(LastSound, k);
				}
			if (Sounding > 0) {
				++Channels;
				Melodic += Ch != CH_NOISE && Ch != CH_DPCM;
			}
		}
		Sv.Seconds = (LastSound + 1) / R.TickRate;
		if (R.LoopStart >= 0)
			Sv.LoopSeconds = R.LoopLength / R.TickRate;
		if (Channels == 0)
			Sv.Kind = "silent";
		else if (R.LoopStart >= 0 && Sv.LoopSeconds >= 4 && Melodic >= 2)
			Sv.Kind = "music";
		else if (R.LoopStart >= 0)
			Sv.Kind = Melodic >= 2 ? "jingle" : "sound effect";
		else if (Sv.Seconds < 2.5 || Melodic <= 1)
			Sv.Kind = Sv.Seconds >= 20 && Melodic == 1 ? "music" : "sound effect";
		else
			Sv.Kind = Sv.Seconds >= 25 ? "music" : "jingle";
		Out.push_back(Sv);
	}
	return Out;
}

bool StudyTracks(const NsfFile &File, const std::vector<int> &Tracks, const StudyOptions &Options, std::vector<StudiedTrack> &Out,
	std::string &Error)
{
	Out.clear();
	for (size_t i = 0; i < Tracks.size(); ++i) {
		StudiedTrack T;
		T.Track = Tracks[i];
		const std::string Title = TrackTitle(File, Tracks[i]);
		if (Options.Progress && !Options.Progress(int(i), int(Tracks.size()), Title)) {
			Error = "stopped";
			return false;
		}
		ImportOptions Import;
		Import.Tracks.push_back(Tracks[i]);
		Import.MaxSeconds = Options.MaxSeconds;
		ImportResult Result;
		std::string Why;
		if (!ImportNsf(File, Import, Result, Why) || Result.Report.Tracks.empty())
			T.Problem = Title + ": " + (Why.empty() ? "it couldn't be converted" : Why);
		else
			T.Study = StudyTrack(Result.Song, 0, Result.Report.Tracks[0]);
		Out.push_back(T);
	}
	return true;
}

}	// namespace nsf

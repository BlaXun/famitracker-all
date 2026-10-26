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

#include "Listen.h"
#include "GameAudio.h"
#include "Music.h"
#include "../Nsf/FdsModulator.h"
#include "../Transcribe/Transcribe.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdarg>
#include <cstdio>

namespace ftapi {

namespace {

const double PI = 3.14159265358979323846;

std::string Text(const char *pFormat, ...)
{
	char Buffer[1024];
	va_list Args;
	va_start(Args, pFormat);
	std::vsnprintf(Buffer, sizeof(Buffer), pFormat, Args);
	va_end(Args);
	return Buffer;
}

// "4.5", "14", "0.47": a number as people write it
std::string Plain(double Value, int Decimals)
{
	std::string Out = Text("%.*f", Decimals, Value);
	if (Out.find('.') != std::string::npos) {
		while (!Out.empty() && Out[Out.size() - 1] == '0')
			Out.erase(Out.size() - 1);
		if (!Out.empty() && Out[Out.size() - 1] == '.')
			Out.erase(Out.size() - 1);
	}
	return Out == "-0" ? std::string("0") : Out;
}

std::string Count(int N, const char *One, const char *Many)
{
	return Text("%d %s", N, N == 1 ? One : Many);
}

double Db(double Power)
{
	return Power > 1e-20 ? 10.0 * std::log10(Power) : -200.0;
}

long long Clamp(long long Value, long long Low, long long High)
{
	return std::max(Low, std::min(High, Value));
}

// The mean square of samples [Begin, End), the span kept inside them
double MeanSquare(const std::vector<float> &X, long long Begin, long long End)
{
	Begin = Clamp(Begin, 0, static_cast<long long>(X.size()));
	End = Clamp(End, 0, static_cast<long long>(X.size()));
	if (End <= Begin)
		return 0.0;
	double Sum = 0.0;
	for (long long i = Begin; i < End; ++i)
		Sum += static_cast<double>(X[static_cast<size_t>(i)]) * X[static_cast<size_t>(i)];
	return Sum / static_cast<double>(End - Begin);
}

// The loudest mean square of Window-sample stretches in [Begin, End), stepping half a window
double LoudestMeanSquare(const std::vector<float> &X, long long Begin, long long End, long long Window)
{
	double Most = 0.0;
	Window = std::max<long long>(1, Window);
	for (long long At = Begin; At < End; At += std::max<long long>(1, Window / 2))
		Most = std::max(Most, MeanSquare(X, At, std::min(End, At + Window)));
	return Most;
}

// The mean square about its middle, 10 ms at a time (a level held still, as a chip's step, isn't heard)
double SwingSquare(const std::vector<float> &X, long long Begin, long long End, int Rate)
{
	Begin = std::max<long long>(0, Begin);
	End = std::min(End, static_cast<long long>(X.size()));
	const long long Block = std::max<long long>(1, Rate / 100);
	double Sum = 0.0;
	long long Count = 0;
	for (long long At = Begin; At + Block <= End; At += Block) {
		double Mean = 0.0, Square = 0.0;
		for (long long n = At; n < At + Block; ++n) {
			Mean += X[static_cast<size_t>(n)];
			Square += X[static_cast<size_t>(n)] * X[static_cast<size_t>(n)];
		}
		Mean /= static_cast<double>(Block);
		Sum += Square / static_cast<double>(Block) - Mean * Mean;
		++Count;
	}
	return Count > 0 ? Sum / static_cast<double>(Count) : 0.0;
}

// The loudest Window of [Begin, End) about its middle
double LoudestSwing(const std::vector<float> &X, long long Begin, long long End, long long Window, int Rate)
{
	double Most = 0.0;
	for (long long At = Begin; At + Window <= End; At += std::max<long long>(1, Window / 2))
		Most = std::max(Most, SwingSquare(X, At, At + Window, Rate));
	return Most;
}

double Percentile(std::vector<double> Values, double Share)
{
	if (Values.empty())
		return 0.0;
	std::sort(Values.begin(), Values.end());
	const double At = std::max(0.0, std::min(1.0, Share)) * static_cast<double>(Values.size() - 1);
	const size_t i = static_cast<size_t>(std::floor(At));
	const double f = At - static_cast<double>(i);
	return i + 1 < Values.size() ? Values[i] * (1.0 - f) + Values[i + 1] * f : Values[i];
}

double Median(const std::vector<double> &Values)
{
	return Percentile(Values, 0.5);
}

double Seconds(long long Samples, int Rate)
{
	return static_cast<double>(Samples) / std::max(1, Rate);
}

long long SamplesOf(double Seconds, int Rate)
{
	return static_cast<long long>(std::floor(Seconds * Rate + 0.5));
}

// Measuring a held note's pitch: its first 20 ms (the attack: a sound's own scoop, its envelope's first steps) and its last
// 10 ms are left out, and at most its first PITCH_LONGEST seconds are tracked (a long pad holds what it does); YIN looks
// every 5 ms over at least 8 ms (a window that keeps a 14.5 Hz warble's swing within 4%) within 520 cents of the note (deeper
// than the deepest warble heard, 38's 180; any wider and a thin pulse's weak fundamental let it take a fifth above the
// note for it, on the soundtrack's VRC6 pulses at 12.5%), anything further folded back by octaves. A frame has a pitch where YIN's dip is
// PITCH_APERIODIC deep or deeper (0 is a perfect period) and it's within PITCH_QUIET_DB of the note's loudest frame
const double PITCH_HEAD = 0.020, PITCH_TAIL = 0.010, PITCH_HOP = 0.005, PITCH_WINDOW = 0.008, PITCH_LONGEST = 4.0;
const double PITCH_SHORTEST = 0.1;		// notes heard for less aren't measured
const double PITCH_RANGE = 1.35;		// 520 cents either way
const double PITCH_APERIODIC = 0.3, PITCH_QUIET_DB = 18.0;
// A wobble: the pitch swinging WOBBLE_MIN cents or more either way within WOBBLE_WINDOW seconds (the 5th to the 95th
// percentile of its frames there, so one stray frame doesn't make one), for 1.5 of its cycles or more
const double WOBBLE_MIN = 12.0, WOBBLE_WINDOW = 0.25;

// A note's pitch track, kept for the clicks inside it
struct Track {
	ListenPitch Pitch;
	long long Begin;					// the sample its frames count from
	std::vector<double> Times;			// seconds from Begin, a frame each
	std::vector<double> Periods;		// samples, 0 where no pitch was heard
	double Loudest;						// the loudest frame's RMS
	Track() : Begin(0), Loudest(0.0) {}
};

// A held pitch's period measured over the whole stretch at once (up to a second), near Period: the lag where the wave
// matches itself best, between whole samples by a parabola, 0 when it doesn't match well. Frame by frame a wave of a few
// big steps measures a few cents off now and then (the N163's, whose steps land on its own update clock): two N163 parts
// an octave apart came out 13 cents apart, as if they beat, and over the stretch within a cent of their notes
double HeldPeriod(const std::vector<float> &X, long long Begin, long long End, double Period, int Rate)
{
	const int Low = static_cast<int>(std::floor(Period * 0.97)) - 1, High = static_cast<int>(std::ceil(Period * 1.03)) + 1;
	const long long Span = std::min<long long>(End - Begin - High, Rate);
	if (Low < 2 || Span < 4LL * High || Begin < 0 || End > static_cast<long long>(X.size()))
		return 0.0;
	std::vector<double> Match(static_cast<size_t>(High - Low + 1), 0.0);
	double First = 0.0;
	for (long long n = 0; n < Span; ++n)
		First += static_cast<double>(X[static_cast<size_t>(Begin + n)]) * X[static_cast<size_t>(Begin + n)];
	for (int Lag = Low; Lag <= High; ++Lag) {
		double Both = 0.0, Second = 0.0;
		for (long long n = 0; n < Span; ++n) {
			const double a = X[static_cast<size_t>(Begin + n)], b = X[static_cast<size_t>(Begin + n + Lag)];
			Both += a * b;
			Second += b * b;
		}
		Match[static_cast<size_t>(Lag - Low)] = First > 0.0 && Second > 0.0 ? Both / std::sqrt(First * Second) : 0.0;
	}
	size_t Best = 1;
	for (size_t i = 1; i + 1 < Match.size(); ++i)
		if (Match[i] > Match[Best])
			Best = i;
	if (Match[Best] < 0.8 || Match[Best] < Match[Best - 1] || Match[Best] < Match[Best + 1])
		return 0.0;
	const double a = Match[Best - 1], b = Match[Best], c = Match[Best + 1], Den = a - 2.0 * b + c;
	double Lag = static_cast<double>(Low) + static_cast<double>(Best);
	if (std::fabs(Den) > 1e-12) {
		const double Shift = 0.5 * (a - c) / Den;
		if (Shift > -1.0 && Shift < 1.0)
			Lag += Shift;
	}
	return Lag;
}

Track TrackNote(const std::vector<float> &Samples, int Rate, long long Begin, long long End, double Hz)
{
	Track Out;
	ListenPitch &P = Out.Pitch;
	if (Hz <= 0.0 || Rate <= 0)
		return Out;
	const long long B = std::max<long long>(0, Begin + SamplesOf(PITCH_HEAD, Rate));
	const long long E = std::min(std::min<long long>(static_cast<long long>(Samples.size()), End - SamplesOf(PITCH_TAIL, Rate)),
		B + SamplesOf(PITCH_LONGEST, Rate));
	if (E - B < SamplesOf(PITCH_SHORTEST, Rate))
		return Out;
	transcribe::PitchOptions Options;
	Options.minFrequency = std::max(20.0, Hz / PITCH_RANGE);
	Options.maxFrequency = std::min(Rate / 3.0, Hz * PITCH_RANGE);
	if (Options.maxFrequency <= Options.minFrequency * 1.5)
		return Out;		// too high to measure at this rate
	// Within the narrow range around the note no octave is in doubt, so the deepest dip is the period: YIN's usual rule (the
	// first dip under 0.2) took an N163 wave with a run of zeros a step short (44's organ, 6 channels: about a semitone sharp)
	Options.threshold = 0.02;
	Options.windowSeconds = PITCH_WINDOW;
	Options.hopSeconds = PITCH_HOP;
	transcribe::Audio Audio;
	Audio.sampleRate = Rate;
	Audio.samples.assign(Samples.begin() + static_cast<std::ptrdiff_t>(B), Samples.begin() + static_cast<std::ptrdiff_t>(E));
	const std::vector<transcribe::PitchFrame> Frames = transcribe::TrackPitch(Audio, Options);
	if (Frames.size() < 6)
		return Out;
	Out.Begin = Begin;
	P.Seconds = Seconds(E - B, Rate);
	double Loudest = 0.0;
	for (size_t i = 0; i < Frames.size(); ++i)
		Loudest = std::max(Loudest, Frames[i].rms);
	Out.Loudest = Loudest;
	const double Quiet = Loudest * std::pow(10.0, -PITCH_QUIET_DB / 20.0);
	std::vector<double> Times, Cents;
	std::vector<int> Index;		// into Frames
	Out.Times.resize(Frames.size());
	Out.Periods.assign(Frames.size(), 0.0);
	for (size_t i = 0; i < Frames.size(); ++i) {
		const transcribe::PitchFrame &F = Frames[i];
		Out.Times[i] = Seconds(B - Begin, Rate) + F.time;
		if (F.frequency <= 0.0 || F.aperiodicity > PITCH_APERIODIC || F.rms < Quiet || F.rms <= 1e-6)
			continue;
		double C = 1200.0 * std::log2(F.frequency / Hz);
		while (C > 600.0)
			C -= 1200.0;
		while (C <= -600.0)
			C += 1200.0;
		Out.Periods[i] = Rate / (Hz * std::pow(2.0, C / 1200.0));
		Times.push_back(Out.Times[i]);
		Cents.push_back(C);
		Index.push_back(static_cast<int>(i));
	}
	P.Tracked = static_cast<double>(Cents.size()) / static_cast<double>(Frames.size());
	if (Cents.size() < 6 || P.Tracked < 0.4)
		return Out;
	P.Valid = true;
	// Each frame with its neighbours' median: a frame YIN took a wrong dip in (a warble's fast swing smears it) doesn't
	// count as the pitch moving
	{
		std::vector<double> Smoothed(Cents.size());
		for (size_t i = 0; i < Cents.size(); ++i) {
			if (i == 0 || i + 1 == Cents.size()) {
				Smoothed[i] = Cents[i];
				continue;
			}
			double a = Cents[i - 1], b = Cents[i], c = Cents[i + 1];
			Smoothed[i] = std::max(std::min(a, b), std::min(std::max(a, b), c));
		}
		Cents.swap(Smoothed);
	}

	// Each frame's swing: the pitch's spread within a window around it
	const size_t n = Cents.size();
	std::vector<double> Swing(n, 0.0), Middle(n, 0.0);
	for (size_t i = 0; i < n; ++i) {
		std::vector<double> Near;
		for (size_t j = i; j-- > 0 && Times[i] - Times[j] <= WOBBLE_WINDOW / 2; )
			Near.push_back(Cents[j]);
		for (size_t j = i; j < n && Times[j] - Times[i] <= WOBBLE_WINDOW / 2; ++j)
			Near.push_back(Cents[j]);
		const double Low = Percentile(Near, 0.05), High = Percentile(Near, 0.95);
		Swing[i] = (High - Low) / 2.0;
		Middle[i] = (High + Low) / 2.0;
	}
	size_t First = n, Last = 0;
	for (size_t i = 0; i < n; ++i)
		if (Swing[i] >= WOBBLE_MIN) {
			First = std::min(First, i);
			Last = i;
		}
	// Where it holds before a wobble: its pitch there
	std::vector<double> Before;
	for (size_t i = 0; i < n && (First == n || Times[i] < Times[std::min(First, n - 1)]); ++i)
		if (Swing[i] < WOBBLE_MIN)
			Before.push_back(Cents[i]);
	if (First < n) {
		// The wobble: from where the pitch first leaves where it held (the note's start when it never held) to its last
		// swing. A frame's window sees a wobble coming half a window early, so it starts where the pitch moves 8 cents
		size_t From = 0;
		if (!Before.empty()) {
			const double Held = Median(Before);
			From = First;
			for (size_t i = First; i <= Last; ++i)
				if (std::fabs(Cents[i] - Held) >= 8.0) {
					From = i;
					break;
				}
		}
		std::vector<double> Stretch, Swings;
		for (size_t i = From; i <= Last; ++i) {
			Stretch.push_back(Cents[i]);
			if (Swing[i] >= WOBBLE_MIN)
				Swings.push_back(Swing[i]);
		}
		P.WobbleFrom = std::max(0.0, Times[From] - PITCH_WINDOW / 2);
		P.WobbleSeconds = Times[Last] - Times[From] + PITCH_HOP;
		P.Low = Percentile(Stretch, 0.02);
		P.High = Percentile(Stretch, 0.98);
		P.Swing = Median(Swings);
		// How often it swings: the first strong peak of the stretch's autocorrelation (its frames on an even grid, the gaps
		// filled in), between 30 and 1.6 times a second
		const double Hop = PITCH_HOP;
		const int Grid = static_cast<int>(std::floor((Times[Last] - Times[From]) / Hop + 0.5)) + 1;
		std::vector<double> Even(static_cast<size_t>(std::max(0, Grid)), 0.0);
		for (int g = 0; g < Grid; ++g) {
			const double t = Times[From] + g * Hop;
			size_t j = From;
			while (j < Last && Times[j + 1] <= t)
				++j;
			double Value = Cents[j] - Middle[j];
			if (j < Last && Times[j + 1] > Times[j]) {
				const double f = (t - Times[j]) / (Times[j + 1] - Times[j]);
				Value = (Cents[j] - Middle[j]) * (1.0 - f) + (Cents[j + 1] - Middle[j + 1]) * std::max(0.0, std::min(1.0, f));
			}
			Even[static_cast<size_t>(g)] = Value;
		}
		const int Shortest = static_cast<int>(std::floor(1.0 / 30.0 / Hop + 0.5));
		const int Longest = std::min(static_cast<int>(std::floor(1.0 / 1.6 / Hop + 0.5)), static_cast<int>(Even.size() * 2 / 3));
		double Energy = 0.0;
		for (size_t i = 0; i < Even.size(); ++i)
			Energy += Even[i] * Even[i];
		std::vector<double> Acf(static_cast<size_t>(std::max(0, Longest + 2)), 0.0);
		for (int k = 1; k <= Longest + 1 && Energy > 0.0; ++k) {
			double Sum = 0.0;
			for (size_t i = 0; i + static_cast<size_t>(k) < Even.size(); ++i)
				Sum += Even[i] * Even[i + static_cast<size_t>(k)];
			Acf[static_cast<size_t>(k)] = Sum / Energy * Even.size() / std::max<double>(1.0, static_cast<double>(Even.size() - k));
		}
		double Best = 0.0;
		for (int k = Shortest; k <= Longest; ++k)
			Best = std::max(Best, Acf[static_cast<size_t>(k)]);
		int Peak = -1;
		for (int k = std::max(2, Shortest); k <= Longest && Best >= 0.25; ++k) {
			if (Acf[static_cast<size_t>(k)] >= 0.5 * Best && Acf[static_cast<size_t>(k)] >= Acf[static_cast<size_t>(k - 1)] &&
				Acf[static_cast<size_t>(k)] >= Acf[static_cast<size_t>(k + 1)]) {
				Peak = k;
				break;
			}
		}
		if (Peak > 0) {
			double Lag = Peak;
			const double a = Acf[static_cast<size_t>(Peak - 1)], b = Acf[static_cast<size_t>(Peak)], c = Acf[static_cast<size_t>(Peak + 1)];
			const double Den = a - 2.0 * b + c;
			if (std::fabs(Den) > 1e-12) {
				const double Shift = 0.5 * (a - c) / Den;
				if (Shift > -1.0 && Shift < 1.0)
					Lag += Shift;
			}
			P.Rate = 1.0 / (Lag * Hop);
		}
		// It swings both ways around its middle (a third of the swing on the smaller side at least): a pitch that only jumps one
		// way now and then is a sound's noise (an N163 pad's first quiet frames) or a trill, not a wobble
		const double Middle = Median(Stretch), Up = P.High - Middle, Down = Middle - P.Low;
		const bool BothWays = std::min(Up, Down) >= std::max(Up, Down) / 3.0;
		P.Wobbles = P.WobbleSeconds >= 0.12 && (P.Rate <= 0.0 || P.WobbleSeconds * P.Rate >= 1.5) && P.Swing >= WOBBLE_MIN && BothWays;
		P.Steady = Before.size() >= 12 ? Median(Before) : Median(Stretch);
		P.HeldSeconds = Before.size() >= 12 ? static_cast<double>(Before.size()) * PITCH_HOP : 0.0;
		if (!P.Wobbles) {
			P.Low = P.High = P.Swing = P.Rate = 0.0;
			P.WobbleFrom = P.WobbleSeconds = 0.0;
			P.Steady = Median(Cents);
			P.HeldSeconds = P.Seconds;
		}
	}
	else {
		P.Steady = Median(Cents);
		P.HeldSeconds = P.Seconds;
	}
	// Where it holds, measured over the stretch it holds in (the whole note without a wobble, else before it)
	const long long Held = P.Wobbles ? B + SamplesOf(P.HeldSeconds, Rate) : E;
	if (!P.Wobbles || P.HeldSeconds >= 0.15) {
		const double Period = HeldPeriod(Samples, B, Held, Rate / (Hz * std::pow(2.0, P.Steady / 1200.0)), Rate);
		if (Period > 0.0) {
			const double Cents = 1200.0 * std::log2(Rate / Period / Hz);
			if (std::fabs(Cents - P.Steady) < 30.0)
				P.Steady = Cents;
		}
	}
	return Out;
}

// Clicks inside a held note: cycles that break from the wave's period (as a pulse that restarts its wave does), where the
// waves a few cycles before and after are the same shape (a change of sound, a duty or wave sequence's step, isn't one).
// Each cycle's break is how much it differs from the cycle before, against both's energy; a click breaks CLICK_BREAK or more
// (on a part whose wave restarts; CLICK_BREAK_OTHER on the others) and CLICK_OVER_USUAL times the note's usual break; the
// waves around it match within CLICK_SAME, shifted as the jump left them, and the cycles a little before and after it, one
// after another, keep their level (about their middle) within CLICK_LEVEL (a restrike's dip and strike, a soft layer's
// pulsing pad, steps its level, not its wave). Measured on the soundtrack: a 2A03 pulse restarting as its vibrato crosses
// 256 breaks a cycle by 0.18-0.4 (the cycle's middle moves with the jump, so its own level isn't the test); the N163's
// own grain with six channels (each updated 19.9 thousand times a second, so a wave's steps land up to 50 microseconds off
// from cycle to cycle) breaks one cycle in six by 0.15-0.25, which is its sound, not a click
const double CLICK_BREAK = 0.15, CLICK_BREAK_OTHER = 0.5, CLICK_OVER_USUAL = 6.0, CLICK_SAME = 0.05, CLICK_LEVEL = 1.44;

std::vector<long long> InsideClicks(const std::vector<float> &X, int Rate, const Track &T, long long End, double Least)
{
	std::vector<long long> Out;
	if (!T.Pitch.Valid || T.Periods.empty())
		return Out;
	const long long Size = static_cast<long long>(X.size());
	struct Cycle {
		long long At;
		int Length;
		double Break;
		double Energy;
		double Period;
		double Own;		// its own samples' spread about their middle (their variance)
	};
	std::vector<Cycle> Cycles;
	// The period at a sample: the nearest frame's (within 15 ms) that has one; the samples asked for only go forward
	size_t Near = 0;
	auto PeriodAt = [&](long long n) {
		const double t = Seconds(n - T.Begin, Rate);
		while (Near + 1 < T.Times.size() && T.Times[Near + 1] <= t)
			++Near;
		double Best = 0.0, Gap = 0.015;
		for (size_t i = Near > 3 ? Near - 3 : 0; i < T.Times.size() && i <= Near + 4; ++i)
			if (T.Periods[i] > 0.0 && std::fabs(T.Times[i] - t) <= Gap) {
				Gap = std::fabs(T.Times[i] - t);
				Best = T.Periods[i];
			}
		return Best;
	};
	const long long From = T.Begin + SamplesOf(PITCH_HEAD + 0.01, Rate);
	const long long To = std::min(Size, std::min(End - SamplesOf(PITCH_TAIL + 0.01, Rate), T.Begin + SamplesOf(PITCH_LONGEST, Rate)));
	long long n = From;
	while (n < To) {
		const double Period = PeriodAt(n);
		if (Period < 4.0) {
			n += SamplesOf(PITCH_HOP, Rate);
			continue;
		}
		const int Length = static_cast<int>(std::floor(Period + 0.5));
		if (n - Length - 2 < 0 || n + Length >= Size)
			break;
		double Diff = 0.0, Energy = 0.0, Own = 0.0, Middle = 0.0;
		for (int k = 0; k < Length; ++k) {
			const double Back = static_cast<double>(n + k) - Period;
			const long long b = static_cast<long long>(std::floor(Back));
			const double f = Back - static_cast<double>(b);
			const double Then = X[static_cast<size_t>(b)] * (1.0 - f) + X[static_cast<size_t>(b + 1)] * f;
			const double Now = X[static_cast<size_t>(n + k)];
			Diff += (Now - Then) * (Now - Then);
			Energy += Now * Now + Then * Then;
			Own += Now * Now;
			Middle += Now;
		}
		Middle /= Length;
		Cycle C = { n, Length, Energy > 1e-12 ? Diff / Energy : 0.0, Energy / Length, Period, Own / Length - Middle * Middle };
		Cycles.push_back(C);
		n += Length;
	}
	if (Cycles.size() < 8)
		return Out;
	std::vector<double> Energies;
	for (size_t i = 0; i < Cycles.size(); ++i)
		Energies.push_back(Cycles[i].Energy);
	const double Loud = Percentile(Energies, 0.9);
	long long LastClick = -1;
	for (size_t i = 3; i + 3 < Cycles.size(); ++i) {
		const Cycle &C = Cycles[i];
		if (C.Break < Least || C.Energy < Loud * 0.1)
			continue;
		// Against the cycles around it (8 either way): a wave that breaks all the while (a warble's fast swing, a noisy sound)
		// has no clicks to tell
		std::vector<double> Around;
		for (size_t j = i >= 8 ? i - 8 : 0; j < Cycles.size() && j <= i + 8; ++j)
			if (j + 1 < i || j > i + 1)
				Around.push_back(Cycles[j].Break);
		if (C.Break < CLICK_OVER_USUAL * Median(Around))
			continue;
		// A step of the pitch (an FDS modulator's bend jumping) breaks the period without a jump in the wave, and a step of the
		// level (an envelope's, which also moves an N163 wave's middle) changes its height: not clicks
		const Cycle &Early = Cycles[i - 2], &Late = Cycles[i + 2];
		if (std::fabs(Early.Period - Late.Period) > 0.01 * std::max(Early.Period, Late.Period))
			continue;
		bool Even = true;	// one cycle after another, those a little before and after it as loud as each other
		double Softest = 1e9, Loudest = 0.0;
		for (size_t j = i - 3; j <= i + 3; ++j) {
			if (j > i - 3 && Cycles[j].At != Cycles[j - 1].At + Cycles[j - 1].Length)
				Even = false;
			if (j + 2 <= i || j >= i + 2) {
				Softest = std::min(Softest, Cycles[j].Own);
				Loudest = std::max(Loudest, Cycles[j].Own);
			}
		}
		if (!Even || Loudest > CLICK_LEVEL * Softest)
			continue;
		if (LastClick >= 0 && C.At - LastClick < 4LL * C.Length)
			continue;
		// The same shape before and after, shifted any way
		const Cycle &A = Cycles[i - 3], &B = Cycles[i + 3];
		const int L = std::min(A.Length, B.Length);
		double MeanA = 0.0, MeanB = 0.0;	// each cycle about its own middle: a level both share isn't their shape
		for (int k = 0; k < L; ++k) {
			MeanA += X[static_cast<size_t>(A.At + k)];
			MeanB += X[static_cast<size_t>(B.At + k)];
		}
		MeanA /= L;
		MeanB /= L;
		double Unlike = 1e9;
		for (int s = 0; s < L; ++s) {
			double Diff = 0.0, Energy = 0.0;
			for (int k = 0; k < L; ++k) {
				const double a = X[static_cast<size_t>(A.At + k)] - MeanA, b = X[static_cast<size_t>(B.At + (k + s) % L)] - MeanB;
				Diff += (a - b) * (a - b);
				Energy += a * a + b * b;
			}
			if (Energy > 1e-12)
				Unlike = std::min(Unlike, Diff / Energy);
		}
		if (Unlike <= CLICK_SAME) {
			Out.push_back(C.At);
			LastClick = C.At;
		}
	}
	return Out;
}

// A small FFT for the spectra (a power of two)
void Fft(std::vector<std::complex<double> > &Data)
{
	const size_t n = Data.size();
	for (size_t i = 1, j = 0; i < n; ++i) {
		size_t Bit = n >> 1;
		for (; j & Bit; Bit >>= 1)
			j ^= Bit;
		j ^= Bit;
		if (i < j)
			std::swap(Data[i], Data[j]);
	}
	for (size_t Len = 2; Len <= n; Len <<= 1) {
		const double Angle = -2.0 * PI / static_cast<double>(Len);
		const std::complex<double> Step(std::cos(Angle), std::sin(Angle));
		for (size_t i = 0; i < n; i += Len) {
			std::complex<double> w(1.0, 0.0);
			for (size_t k = 0; k < Len / 2; ++k) {
				const std::complex<double> u = Data[i + k], v = Data[i + k + Len / 2] * w;
				Data[i + k] = u + v;
				Data[i + k + Len / 2] = u - v;
				w *= Step;
			}
		}
	}
}

// The power spectrum of a stretch, averaged over up to SPECTRUM_FRAMES Hann windows of SPECTRUM_SIZE samples spread over it
// (bins of Rate / SPECTRUM_SIZE Hz); A minus B when B is given (the mix without the melody)
const size_t SPECTRUM_SIZE = 4096;
const int SPECTRUM_FRAMES = 12;

std::vector<double> Spectrum(const std::vector<float> &A, const std::vector<float> *pB, long long Begin, long long End)
{
	std::vector<double> Power(SPECTRUM_SIZE / 2 + 1, 0.0);
	const long long Size = static_cast<long long>(A.size());
	Begin = Clamp(Begin, 0, Size);
	End = Clamp(End, 0, Size);
	const long long Room = End - Begin - static_cast<long long>(SPECTRUM_SIZE);
	if (Room < 0)
		return Power;
	const int Frames = static_cast<int>(std::min<long long>(SPECTRUM_FRAMES, 1 + Room / static_cast<long long>(SPECTRUM_SIZE / 4)));
	std::vector<std::complex<double> > Data(SPECTRUM_SIZE);
	for (int f = 0; f < Frames; ++f) {
		const long long At = Begin + (Frames > 1 ? Room * f / (Frames - 1) : 0);
		for (size_t k = 0; k < SPECTRUM_SIZE; ++k) {
			const double Window = 0.5 - 0.5 * std::cos(2.0 * PI * static_cast<double>(k) / static_cast<double>(SPECTRUM_SIZE));
			double Value = A[static_cast<size_t>(At) + k];
			if (pB != 0 && static_cast<size_t>(At) + k < pB->size())
				Value -= (*pB)[static_cast<size_t>(At) + k];
			Data[k] = std::complex<double>(Value * Window, 0.0);
		}
		Fft(Data);
		for (size_t k = 0; k < Power.size(); ++k)
			Power[k] += std::norm(Data[k]) / Frames;
	}
	return Power;
}

double BandPower(const std::vector<double> &Power, int Rate, double Low, double High)
{
	const double Bin = static_cast<double>(Rate) / static_cast<double>(SPECTRUM_SIZE);
	double Sum = 0.0;
	for (size_t k = 0; k < Power.size(); ++k) {
		const double f = static_cast<double>(k) * Bin;
		if (f >= Low && f < High)
			Sum += Power[k];
	}
	return Sum;
}

// The bands of the mix's balance: harsh is its share of power from 2 to 12 kHz, muddy its share from 160 to 500 Hz, both
// against all of it from 40 Hz to 12 kHz
const double BAND_LOW = 40.0, BAND_TOP = 12000.0, HARSH_FROM = 2000.0, MUD_FROM = 160.0, MUD_TO = 500.0;

}	// namespace

const char *ListenKindId(int Kind)
{
	static const char *const IDS[LISTEN_KINDS] = { "wobble", "tuning", "beating", "click", "clipping", "drone", "unheard", "covered",
		"jump", "harsh", "muddy" };
	return Kind >= 0 && Kind < LISTEN_KINDS ? IDS[Kind] : "";
}

const char *ListenKindTitle(int Kind)
{
	static const char *const TITLES[LISTEN_KINDS] = { "Warbles and deep vibratos", "Out of tune", "Parts beating against each other",
		"Clicks and pops", "Clipping", "Sound that holds on", "Notes hardly heard", "The melody covered", "Jumps in loudness",
		"Harsh stretches", "Muddy stretches" };
	return Kind >= 0 && Kind < LISTEN_KINDS ? TITLES[Kind] : "";
}

double ListenFinding::Number(const std::string &Name, double Default) const
{
	for (size_t i = 0; i < Numbers.size(); ++i)
		if (Numbers[i].first == Name)
			return Numbers[i].second;
	return Default;
}

void ListenFinding::Set(const std::string &Name, double Value)
{
	for (size_t i = 0; i < Numbers.size(); ++i)
		if (Numbers[i].first == Name) {
			Numbers[i].second = Value;
			return;
		}
	Numbers.push_back(std::make_pair(Name, Value));
}

ListenPitch MeasurePitch(const std::vector<float> &Samples, int Rate, long long Begin, long long End, double Hz)
{
	return TrackNote(Samples, Rate, Begin, End, Hz).Pitch;
}

std::string ListenCentsWords(double Cents)
{
	return nsf::CentsInWords(Cents);
}

std::string ListenTimesWords(double PerSecond)
{
	return nsf::TimesInWords(PerSecond);
}

namespace {

const double CPU_NTSC = 1789773.0, CPU_PAL = 1662607.0;

// A note's frequency in 0.4.6's tables: C-0 is 32.7032 Hz, A-3 440
double TableHz(int Pitch)
{
	return 32.7032 * std::pow(2.0, Pitch / 12.0);
}

// The VRC7's F-numbers for C to B (CChannelHandlerVRC7::GetFnum)
const int VRC7_FNUM[12] = { 172, 183, 194, 205, 217, 230, 244, 258, 274, 290, 307, 326 };

// The register a note sets, and the frequency a register plays, by chip: Up is the register's change for one step sharper
struct Register {
	double Value;		// the note's register: a period, or a frequency (FDS, N163, VRC7)
	bool Period;		// a period: a step sharper is a step down
	double Unit;		// register steps a Pxx or 4xy step moves (the N163's 64)
	double Hz;			// the equal-tempered pitch it should sound, as the chip sounds the note written
	double Played;		// what the register plays, 0 when not worked out
};

bool RegisterOf(const std::string &Chip, int Kind, int Pitch, int Channels, int Cycle, bool Pal, Register &Out)
{
	if (Pitch < 0 || Kind == KIND_NOISE || Kind == KIND_DPCM)
		return false;
	const double f = TableHz(Pitch);
	const bool OwnClock = Pal && (Chip == "2A03" || Chip == "MMC5");
	const double Cpu = OwnClock ? CPU_PAL : CPU_NTSC;
	Out.Unit = 1.0;
	Out.Period = true;
	if (Kind == KIND_PULSE || Kind == KIND_TRIANGLE || Kind == KIND_SAWTOOTH) {
		const double Div = Kind == KIND_SAWTOOTH ? 14.0 : 16.0;
		const int Limit = Chip == "VRC6" ? 0xFFF : 0x7FF;
		const int P = std::max(1, std::min(Limit, static_cast<int>(Cpu / Div / f - 0.5)));
		const double Steps = Kind == KIND_TRIANGLE ? 32.0 : Div;
		Out.Value = P + 1.0;
		Out.Hz = Kind == KIND_TRIANGLE ? f / 2.0 : f;
		Out.Played = Cpu / (Steps * (P + 1.0));
		return true;
	}
	if (Kind == KIND_SQUARE) {	// the 5B: CPU / (32 * period), an octave under
		const int P = std::max(1, std::min(0xFFF, static_cast<int>(Cpu / 16.0 / f + 0.5)));
		Out.Value = P;
		Out.Hz = f / 2.0;
		Out.Played = Cpu / (32.0 * P);
		return true;
	}
	if (Kind == KIND_FM) {		// the VRC7: an F-number in the note's octave's block, an octave under
		Out.Value = VRC7_FNUM[Pitch % 12];
		Out.Period = false;
		Out.Hz = f / 2.0;
		Out.Played = 0.0;
		return true;
	}
	if (Chip == "FDS") {		// two octaves under, its register a frequency
		const int R = std::max(1, static_cast<int>(f * 65536.0 / (CPU_NTSC / 16.0) + 0.5));
		Out.Value = R;
		Out.Period = false;
		Out.Hz = f / 4.0;
		Out.Played = R * CPU_NTSC / 4194304.0;
		return true;
	}
	if (Chip == "N163") {		// a frequency, four steps of the table's a step of 4xy's and Pxx's sixteen (64 of the register)
		const int Count = Channels <= 0 ? 4 : std::min(8, Channels);
		const unsigned int Table = std::min(0xFFFFu, static_cast<unsigned int>(f * Count * 983040.0 / (CPU_NTSC / 16.0)) / 4u);
		const int Wave = std::max(4, Cycle);
		Out.Value = 4.0 * Table;
		Out.Period = false;
		Out.Unit = 64.0;
		Out.Hz = f * 16.0 / Wave;
		Out.Played = CPU_NTSC * 4.0 * Table / (983040.0 * Count * Wave);
		return true;
	}
	return false;
}

// A register moved by Steps steps sharper, as a frequency ratio
double StepRatio(const Register &R, double Steps)
{
	const double Moved = R.Value + (R.Period ? -1.0 : 1.0) * Steps * R.Unit;
	if (Moved <= 0.0)
		return 1.0;
	return R.Period ? R.Value / Moved : Moved / R.Value;
}

// 4xy's peak at a depth, as CSoundGen's new vibrato table has it (its sine at a quarter turn, truncated)
int VibratoPeak(int Depth)
{
	static const double DEPTHS[16] = { 1.0, 1.5, 2.5, 4.0, 5.0, 7.0, 10.0, 12.0, 14.0, 17.0, 22.0, 30.0, 44.0, 64.0, 96.0, 128.0 };
	return static_cast<int>(std::sin(15.0 / 16.0 * (3.1415 / 2.0)) * DEPTHS[Depth & 15]);
}

}	// namespace

ListenTuning ListenNoteTuning(const std::string &Chip, int Kind, int Pitch, int Detune, int Channels, int Cycle, bool Pal)
{
	ListenTuning T;
	Register R;
	if (!RegisterOf(Chip, Kind, Pitch, Channels, Cycle, Pal, R))
		return T;
	const double Ratio = StepRatio(R, Detune);
	T.Hz = R.Hz * Ratio;
	T.ChipHz = R.Played > 0.0 ? R.Played * Ratio : 0.0;
	T.CentsPerStep = 1200.0 * std::log2(StepRatio(R, 1.0));
	return T;
}

double ListenVibratoCents(const std::string &Chip, int Kind, int Pitch, int Depth, int Channels, bool Pal)
{
	Register R;
	if (Depth <= 0 || !RegisterOf(Chip, Kind, Pitch, Channels, 16, Pal, R))
		return 0.0;
	return 1200.0 * std::log2(StepRatio(R, VibratoPeak(Depth)));
}

int ListenVibratoDepth(const std::string &Chip, int Kind, int Pitch, double Cents, int Channels, bool Pal)
{
	int Best = 1;
	double Gap = 1e9;
	for (int d = 1; d < 16; ++d) {
		const double Off = std::fabs(ListenVibratoCents(Chip, Kind, Pitch, d, Channels, Pal) - Cents);
		if (Off < Gap - 1e-9) {
			Gap = Off;
			Best = d;
		}
	}
	return Best;
}

namespace {

// The findings, kind by kind

// Wobbles. A warble: faster than WARBLE_RATE times a second and WARBLE_CENTS or more either way (38's FDS lead, its
// modulator at speed 34 and depth 5: 14.5 times a second, -170 to +180 cents, heard as a fluttering echo after each note's
// right start). A vibrato swinging more than DEEP_CENTS either way sounds out of tune rather than sung (a singer's is 20-50).
// Below SLOW_RATE times a second it's a bend, not a wobble
const double WARBLE_RATE = 8.0, WARBLE_CENTS = 40.0, DEEP_CENTS = 60.0, SLOW_RATE = 2.0;
// Out of tune: a held note's pitch TUNE_CENTS or more from where it should sound; TUNE_BAD_CENTS is clearly wrong. The chip's
// own rounding explains it when the pitch is within CHIP_CENTS of where its register puts the note
const double TUNE_CENTS = 20.0, TUNE_BAD_CENTS = 35.0, CHIP_CENTS = 6.0;
// Beating: two parts at one pitch (or octaves apart) BEAT_CENTS or more off each other, for BEAT_OVERLAP seconds together,
// the quieter within BEAT_LEVEL_DB of the louder, wavering between BEAT_SLOWEST and BEAT_FASTEST times a second. 38's ghost
// echo, 7 cents sharp on purpose (P81 on the N163) against its FDS lead 2 cents flat, beats 2.2 times a second 13 dB under
// the lead: a chorus, which passes
const double BEAT_CENTS = 12.0, BEAT_BAD_CENTS = 20.0, BEAT_OVERLAP = 0.25, BEAT_LEVEL_DB = 15.0, BEAT_SLOWEST = 1.0, BEAT_FASTEST = 30.0;
// A drone: after a note's release or halt, with nothing for DRONE_GAP seconds or more, its part still sounds within
// DRONE_UNDER_DB of the note a quarter second on, and doesn't fade by DRONE_FADE_DB (a space's ring fades faster); its
// level about its own middle, so a step the chip holds (the triangle stops on one) fading through the mixer's filter
// isn't a sound. A listener notices one that holds within DRONE_LOUD_DB of its note for DRONE_LONG seconds or more
const double DRONE_GAP = 0.6, DRONE_UNDER_DB = 18.0, DRONE_FADE_DB = 3.0, DRONE_FLOOR_DB = -60.0, DRONE_LOUD_DB = 6.0, DRONE_LONG = 1.5;
// Hardly heard: a note's loudest 10 ms UNHEARD_DB or more under its part's usual note
const double UNHEARD_DB = 24.0, UNHEARD_FLOOR_DB = -80.0;
// Clipping: samples at or past full scale
const double CLIP_LEVEL = 0.999;
// A jump: a bar JUMP_DB louder or quieter than the one before, where no section starts (both over JUMP_FLOOR LUFS)
const double JUMP_DB = 8.0, JUMP_FLOOR = -45.0;
// Harsh and muddy: a bar's share of power from 2 kHz up, and from 160 to 500 Hz, over these, two bars or more in a row.
// Measured on the soundtrack's 67 versions (2586 bars): half its bars have 5% of their power from 2 kHz up and 34% from
// 160 to 500 Hz; 99 in 100 under 29% (-5.4 dB) and 79% (-1 dB)
const double HARSH_DB = -5.0, MUDDY_DB = -1.0;
// Covered: in the melody's own band (from 0.7 to 2.8 times its notes' pitch), the other parts COVER_DB or more over it while
// the melody holds its own over all
const double COVER_DB = 3.0;

class Ears {
public:
	explicit Ears(const ListenSong &Song) : S(Song)
	{
		m_Mono.resize(S.Left.size());
		for (size_t n = 0; n < S.Left.size(); ++n)
			m_Mono[n] = 0.5f * (S.Left[n] + (n < S.Right.size() ? S.Right[n] : S.Left[n]));
	}
	std::vector<ListenFinding> Listen(ListenStats *pStats);

private:
	const ListenSong &S;
	std::vector<float> m_Mono;						// the mix, both sides averaged
	std::vector<std::vector<Track> > m_Tracks;		// per part, per note
	std::vector<std::vector<int> > m_Clicks;		// per part, per note: the clicks heard inside it
	std::vector<double> m_PartDb;					// each part's usual note, its loudest 10 ms in dB
	std::vector<double> m_HarshShares, m_MudShares;	// each bar's, in dB
	std::vector<ListenFinding> m_Out;

	bool Loud(int Part) const;		// a part a listener hears plainly: the melody, or within 10 dB of the loudest part
	void Measure();
	void Wobbles();
	void Tuning();
	void Beating();
	void Clicks();
	void Drones();
	void Unheard();
	void Clipping();
	void Jumps();
	void Balance();
	void Covered();
	ListenFinding &Add(int Kind, int Part);
	void Place(ListenFinding &F) const;	// Begin and End from its notes
};

ListenFinding &Ears::Add(int Kind, int Part)
{
	ListenFinding F;
	F.Kind = Kind;
	F.Part = Part;
	m_Out.push_back(F);
	return m_Out.back();
}

void Ears::Place(ListenFinding &F) const
{
	if (F.Part < 0 || F.Notes.empty())
		return;
	const ListenPart &P = S.Parts[static_cast<size_t>(F.Part)];
	F.Begin = P.Notes[static_cast<size_t>(F.Notes.front())].Begin;
	F.End = F.Begin;
	for (size_t i = 0; i < F.Notes.size(); ++i)
		F.End = std::max(F.End, P.Notes[static_cast<size_t>(F.Notes[i])].End);
}

bool Ears::Loud(int Part) const
{
	if (Part < 0 || Part >= static_cast<int>(S.Parts.size()))
		return false;
	if (S.Parts[static_cast<size_t>(Part)].Melody)
		return true;
	double Most = -200.0;
	for (size_t p = 0; p < m_PartDb.size(); ++p)
		Most = std::max(Most, m_PartDb[p]);
	return m_PartDb[static_cast<size_t>(Part)] >= Most - 10.0;
}

void Ears::Measure()
{
	m_Tracks.resize(S.Parts.size());
	m_PartDb.assign(S.Parts.size(), -200.0);
	for (size_t p = 0; p < S.Parts.size(); ++p) {
		const ListenPart &P = S.Parts[p];
		m_Tracks[p].resize(P.Notes.size());
		std::vector<double> Levels;
		for (size_t i = 0; i < P.Notes.size(); ++i) {
			const ListenNote &N = P.Notes[i];
			if (N.Begin < 0 || N.End <= N.Begin)
				continue;
			const double Level = Db(LoudestMeanSquare(P.Samples, N.Begin, std::min(N.End, N.Begin + SamplesOf(0.15, S.Rate)),
				SamplesOf(0.01, S.Rate)));
			if (Level > -150.0)
				Levels.push_back(Level);
			if (P.Pitched && N.Hz > 0.0 && N.Steady)
				m_Tracks[p][i] = TrackNote(P.Samples, S.Rate, N.Begin, N.End, N.Hz);
		}
		m_PartDb[p] = Levels.empty() ? -200.0 : Median(Levels);
	}
}

void Ears::Wobbles()
{
	for (size_t p = 0; p < S.Parts.size(); ++p) {
		const ListenPart &P = S.Parts[p];
		for (int Warble = 1; Warble >= 0; --Warble) {
			std::vector<int> Notes;
			std::vector<double> Lows, Highs, Swings, Rates, Froms, Tracked;
			double Longest = 0.0;
			int Motions[4] = { 0, 0, 0, 0 };
			for (size_t i = 0; i < m_Tracks[p].size(); ++i) {
				const ListenPitch &Q = m_Tracks[p][i].Pitch;
				if (!Q.Valid || !Q.Wobbles || Q.Rate < SLOW_RATE)
					continue;
				const double Half = (Q.High - Q.Low) / 2.0;
				const bool IsWarble = Q.Rate >= WARBLE_RATE && Half >= WARBLE_CENTS;
				const bool IsDeep = !IsWarble && Q.Rate < WARBLE_RATE && Half >= DEEP_CENTS;
				if (Warble ? !IsWarble : !IsDeep)
					continue;
				Notes.push_back(static_cast<int>(i));
				Lows.push_back(Q.Low);
				Highs.push_back(Q.High);
				Swings.push_back(Q.Swing);
				Rates.push_back(Q.Rate);
				Froms.push_back(Q.WobbleFrom);
				Tracked.push_back(Q.Tracked);
				Longest = std::max(Longest, Q.WobbleSeconds);
				const int Motion = P.Notes[i].Motion;
				if (Motion >= 0 && Motion < 4)
					++Motions[Motion];
			}
			if (Notes.size() < 2 && Longest < 0.8)
				continue;
			ListenFinding &F = Add(LISTEN_WOBBLE, static_cast<int>(p));
			F.What = Warble ? "warble" : "vibrato";
			F.Notes = Notes;
			Place(F);
			int Motion = LISTEN_STILL;
			for (int m = 1; m < 4; ++m)
				if (Motions[m] > Motions[Motion])
					Motion = m;
			const double Low = Median(Lows), High = Median(Highs), Rate = Median(Rates), From = Median(Froms);
			F.Set("notes", static_cast<double>(Notes.size()));
			F.Set("cents_low", std::floor(Low + 0.5));
			F.Set("cents_high", std::floor(High + 0.5));
			F.Set("swing", std::floor(Median(Swings) + 0.5));
			F.Set("rate", std::floor(Rate * 10.0 + 0.5) / 10.0);
			F.Set("from_seconds", std::floor(From * 100.0 + 0.5) / 100.0);
			F.Set("motion", Motion);
			const double Half = (High - Low) / 2.0;
			F.Sure = std::min(1.0, 0.5 + 0.5 * std::min(1.0, Notes.size() / 4.0) * Median(Tracked));
			const std::string Range = Text("%+d to %+d cents", static_cast<int>(std::floor(Low + 0.5)), static_cast<int>(std::floor(High + 0.5)));
			const std::string Start = From >= 0.15 ? Text("Each long note sounds right for its first %s s, then its pitch", Plain(From, 2).c_str()) :
				std::string("Its pitch");
			if (Warble) {
				F.Severity = Loud(static_cast<int>(p)) ? 3 : 2;
				F.Heard = Start + " shakes " + ListenCentsWords(Half) + " up and down (" + Range + "), " + ListenTimesWords(Rate) +
					(From >= 0.15 ? ": a short right note, then a fluttering echo of it." : ", so each note sounds like a fluttering echo.") +
					" That's a warble, not a vibrato (a vibrato swings about 5 times a second, a quarter of a semitone either way).";
				F.Summary = Text("its pitch warbles %s, %s", Range.c_str(), ListenTimesWords(Rate).c_str());
			}
			else {
				// Under a semitone either way it's a wide vibrato, a sound's character (42's slow FDS flute swings 80 cents)
				F.Severity = Half >= 120.0 ? (Loud(static_cast<int>(p)) ? 3 : 2) : Half >= 90.0 ? 2 : 1;
				F.Heard = Start + " swings " + ListenCentsWords(Half) + " up and down (" + Range + "), " + ListenTimesWords(Rate) +
					": a vibrato that wide sounds out of tune rather than sung (a gentle one swings about a quarter of a semitone).";
				F.Summary = Text("its vibrato swings %s, too wide", Range.c_str());
			}
		}
	}
}

void Ears::Tuning()
{
	// The sharp notes and the flat ones apart: a part with some of each isn't off one way
	for (size_t p = 0; p < S.Parts.size(); ++p)
	for (int Sign = 1; Sign >= -1; Sign -= 2) {
		const ListenPart &P = S.Parts[p];
		std::vector<int> Notes;
		std::vector<double> Offs, Tracked;
		double Longest = 0.0;
		bool Chip = true;
		for (size_t i = 0; i < m_Tracks[p].size(); ++i) {
			const ListenPitch &Q = m_Tracks[p][i].Pitch;
			const ListenNote &N = P.Notes[i];
			// Where it holds for a tenth of a second or more, or a gentle vibrato's middle once it has swung twice around it (a
			// warble's middle is no pitch)
			const bool Middle = Q.Wobbles && Q.Rate >= 3.0 && Q.Rate < WARBLE_RATE && Q.Swing < WARBLE_CENTS && Q.WobbleSeconds * Q.Rate >= 2.0;
			if (!Q.Valid || (Q.HeldSeconds < 0.1 && !Middle) || Q.Steady * Sign < TUNE_CENTS)
				continue;
			Notes.push_back(static_cast<int>(i));
			Offs.push_back(Q.Steady);
			Tracked.push_back(Q.Tracked);
			Longest = std::max(Longest, Q.Seconds);
			const double ChipOff = N.ChipHz > 0.0 ? 1200.0 * std::log2(N.ChipHz / N.Hz) : 0.0;
			Chip = Chip && N.ChipHz > 0.0 && std::fabs(Q.Steady - ChipOff) <= CHIP_CENTS;
		}
		if (Notes.size() < 2 && Longest < 0.6)
			continue;
		const double Off = Median(Offs);
		ListenFinding &F = Add(LISTEN_TUNING, static_cast<int>(p));
		F.What = Chip ? "chip" : "sound";
		F.Notes = Notes;
		Place(F);
		F.Set("notes", static_cast<double>(Notes.size()));
		F.Set("cents", std::floor(Off + 0.5));
		F.Set("cents_most", std::floor((std::fabs(Percentile(Offs, 0.0)) > std::fabs(Percentile(Offs, 1.0)) ? Percentile(Offs, 0.0) :
			Percentile(Offs, 1.0)) + 0.5));
		F.Sure = std::min(1.0, 0.4 + 0.6 * std::min(1.0, Notes.size() / 3.0) * Median(Tracked));
		F.Severity = std::fabs(Off) >= TUNE_BAD_CENTS ? (Loud(static_cast<int>(p)) ? 3 : 2) : (P.Melody ? 2 : 1);
		const std::string Way = Off > 0.0 ? "sharp" : "flat";
		F.Heard = Text("%s %s %s: %s %s (%+d cents), against where the other parts are tuned.", Count(static_cast<int>(Notes.size()),
			"held note", "held notes").c_str(), Notes.size() == 1 ? "sounds" : "sound", Way.c_str(), ListenCentsWords(Off).c_str(),
			Way.c_str(), static_cast<int>(std::floor(Off + 0.5))) + (Chip ? " The chip can't play them closer: its register rounds "
			"those notes (an octave lower plays them in tune)." : "");
		F.Summary = Text("held notes %+d cents %s", static_cast<int>(std::floor(Off + 0.5)), Way.c_str());
	}
}

void Ears::Beating()
{
	for (size_t a = 0; a < S.Parts.size(); ++a) {
		for (size_t b = a + 1; b < S.Parts.size(); ++b) {
			const ListenPart &A = S.Parts[a], &B = S.Parts[b];
			if (!A.Pitched || !B.Pitched)
				continue;
			std::vector<int> Notes;
			std::vector<double> Apart, Beats, Levels;
			double Together = 0.0;
			int Octaves = 0;
			size_t j0 = 0;
			for (size_t i = 0; i < A.Notes.size(); ++i) {
				const ListenNote &NA = A.Notes[i];
				const ListenPitch &QA = m_Tracks[a][i].Pitch;
				if (!QA.Valid || QA.Wobbles || NA.Begin < 0)
					continue;
				while (j0 < B.Notes.size() && B.Notes[j0].End <= NA.Begin)
					++j0;
				for (size_t j = j0; j < B.Notes.size() && B.Notes[j].Begin < NA.End; ++j) {
					const ListenNote &NB = B.Notes[j];
					const ListenPitch &QB = m_Tracks[b][j].Pitch;
					if (!QB.Valid || QB.Wobbles || NB.Begin < 0)
						continue;
					const long long From = std::max(NA.Begin, NB.Begin), To = std::min(NA.End, NB.End);
					if (Seconds(To - From, S.Rate) < BEAT_OVERLAP)
						continue;
					const int Semitones = static_cast<int>(std::floor(12.0 * std::log2(NB.Hz / NA.Hz) + 0.5));
					if (Semitones % 12 != 0)
						continue;
					const double Fa = NA.Hz * std::pow(2.0, QA.Steady / 1200.0), Fb = NB.Hz * std::pow(2.0, QB.Steady / 1200.0);
					const double Cents = 1200.0 * std::log2(Fb / Fa) - 100.0 * Semitones;
					const double Beat = Semitones >= 0 ? std::fabs(Fb - Fa * std::pow(2.0, Semitones / 12.0)) :
						std::fabs(Fa - Fb * std::pow(2.0, -Semitones / 12.0));
					const double LevelA = Db(MeanSquare(A.Samples, From, To)), LevelB = Db(MeanSquare(B.Samples, From, To));
					if (std::fabs(Cents) < BEAT_CENTS || std::fabs(Cents) > 60.0 || Beat < BEAT_SLOWEST || Beat > BEAT_FASTEST ||
						std::fabs(LevelA - LevelB) > BEAT_LEVEL_DB)
						continue;
					if (Notes.empty() || Notes.back() != static_cast<int>(i))
						Notes.push_back(static_cast<int>(i));
					Apart.push_back(Cents);
					Beats.push_back(Beat);
					Levels.push_back(std::fabs(LevelA - LevelB));
					Together += Seconds(To - From, S.Rate);
					Octaves += Semitones != 0 ? 1 : 0;
				}
			}
			if (Apart.size() < 2 && Together < 1.0)
				continue;
			ListenFinding &F = Add(LISTEN_BEATING, static_cast<int>(a));
			F.Other = static_cast<int>(b);
			F.Notes = Notes;
			Place(F);
			std::vector<double> Sizes;
			for (size_t k = 0; k < Apart.size(); ++k)
				Sizes.push_back(std::fabs(Apart[k]));
			const double Cents = Median(Sizes), Beat = Median(Beats), Level = Median(Levels);
			F.Set("notes", static_cast<double>(Apart.size()));
			F.Set("cents", std::floor(Cents + 0.5));
			F.Set("beats", std::floor(Beat * 10.0 + 0.5) / 10.0);
			F.Set("db_apart", std::floor(Level * 10.0 + 0.5) / 10.0);
			F.Set("seconds", std::floor(Together * 10.0 + 0.5) / 10.0);
			F.Sure = std::min(1.0, 0.5 + 0.5 * std::min(1.0, Apart.size() / 4.0));
			F.Severity = Cents >= BEAT_BAD_CENTS && Level <= 10.0 && (Loud(static_cast<int>(a)) || Loud(static_cast<int>(b))) ? 3 : 2;
			const bool Octave = Octaves * 2 > static_cast<int>(Apart.size());
			F.Heard = Text("%s and %s play the same notes %s, but %s apart (%d cents), so together they waver %s: they beat, and "
				"sound sour rather than in tune.", A.Name.c_str(), B.Name.c_str(), Octave ? "an octave apart" : "together",
				ListenCentsWords(Cents).c_str(), static_cast<int>(std::floor(Cents + 0.5)), ListenTimesWords(Beat).c_str());
			F.Summary = Text("beats against %s, %d cents apart", B.Name.c_str(), static_cast<int>(std::floor(Cents + 0.5)));
		}
	}
}

void Ears::Clicks()
{
	// Inside held notes
	m_Clicks.resize(S.Parts.size());
	for (size_t p = 0; p < S.Parts.size(); ++p) {
		const ListenPart &P = S.Parts[p];
		std::vector<int> Notes;
		int Clicks = 0;
		m_Clicks[p].assign(m_Tracks[p].size(), 0);
		for (size_t i = 0; i < m_Tracks[p].size(); ++i) {
			const Track &T = m_Tracks[p][i];
			if (!T.Pitch.Valid)
				continue;
			const std::vector<long long> Found = InsideClicks(P.Samples, S.Rate, T, P.Notes[i].End, P.Restarts ? CLICK_BREAK :
				CLICK_BREAK_OTHER);
			m_Clicks[p][i] = static_cast<int>(Found.size());
			if (Found.empty())
				continue;
			Notes.push_back(static_cast<int>(i));
			Clicks += static_cast<int>(Found.size());
		}
		if (Clicks >= 2) {
			ListenFinding &F = Add(LISTEN_CLICK, static_cast<int>(p));
			F.What = "inside";
			F.Notes = Notes;
			Place(F);
			F.Set("clicks", Clicks);
			F.Set("notes", static_cast<double>(Notes.size()));
			F.Sure = std::min(1.0, 0.5 + Clicks / 10.0);
			F.Severity = Clicks >= 3 && Loud(static_cast<int>(p)) ? 3 : 2;
			F.Heard = Text("%s inside held notes (%s): the wave jumps where it shouldn't, as a pulse does when its wave restarts, "
				"heard as hiccups in long tones.", Count(Clicks, "click", "clicks").c_str(), Count(static_cast<int>(Notes.size()), "note",
				"notes").c_str());
			F.Summary = Text("%s inside held notes", Count(Clicks, "click", "clicks").c_str());
		}
	}
	// As notes start, on a smooth wave: a jump far steeper than the wave's own steps, a good part of the note's height
	for (size_t p = 0; p < S.Parts.size(); ++p) {
		const ListenPart &P = S.Parts[p];
		if (!P.Smooth)
			continue;
		const long long Size = static_cast<long long>(P.Samples.size());
		std::vector<int> Notes;
		int Checked = 0;
		for (size_t i = 0; i < P.Notes.size(); ++i) {
			const ListenNote &N = P.Notes[i];
			const long long Steady = N.Begin + SamplesOf(0.03, S.Rate), SteadyEnd = std::min(N.End, N.Begin + SamplesOf(0.13, S.Rate));
			if (N.Begin < 3 || SteadyEnd - Steady < SamplesOf(0.03, S.Rate) || SteadyEnd > Size)
				continue;
			++Checked;
			std::vector<double> Steps;
			double Peak = 0.0;
			for (long long n = Steady; n < SteadyEnd; ++n) {
				Steps.push_back(std::fabs(P.Samples[static_cast<size_t>(n)] - P.Samples[static_cast<size_t>(n - 2)]));
				Peak = std::max(Peak, static_cast<double>(std::fabs(P.Samples[static_cast<size_t>(n)])));
			}
			const double Own = Percentile(Steps, 0.99);
			double Jump = 0.0;
			for (long long n = N.Begin; n < std::min(Size, N.Begin + SamplesOf(0.002, S.Rate)); ++n)
				Jump = std::max(Jump, static_cast<double>(std::fabs(P.Samples[static_cast<size_t>(n)] - P.Samples[static_cast<size_t>(n - 2)])));
			if (Jump >= 3.0 * Own && Jump >= 0.3 * Peak && Jump >= 0.004)
				Notes.push_back(static_cast<int>(i));
		}
		if (Notes.size() >= 3 && Notes.size() * 5 >= static_cast<size_t>(Checked)) {
			ListenFinding &F = Add(LISTEN_CLICK, static_cast<int>(p));
			F.What = "start";
			F.Notes = Notes;
			Place(F);
			F.Set("notes", static_cast<double>(Notes.size()));
			F.Set("of_notes", Checked);
			F.Sure = 0.6;
			F.Severity = 2;
			F.Heard = Text("%s of its %d notes start with a pop: its smooth wave jumps in at once rather than swelling, which a "
				"softer first frame (or two) of its volume would round off.", Count(static_cast<int>(Notes.size()), "note", "notes").c_str(),
				Checked);
			F.Summary = Text("%s start with a pop", Count(static_cast<int>(Notes.size()), "note", "notes").c_str());
		}
	}
	// At the loop's seam: a step of the sound's level as it goes round, far over what it makes where the loop starts the
	// first time and where the song plays on before it
	if (S.Seam > 0 && S.LoopStart > 0) {
		const long long Half = SamplesOf(0.0015, S.Rate);
		auto StepAt = [&](const std::vector<float> &X, long long At) {
			if (At - Half < 0 || At + Half > static_cast<long long>(X.size()))
				return 0.0;
			double Before = 0.0, After = 0.0;
			for (long long n = At - Half; n < At; ++n)
				Before += X[static_cast<size_t>(n)];
			for (long long n = At; n < At + Half; ++n)
				After += X[static_cast<size_t>(n)];
			return std::fabs(After - Before) / static_cast<double>(Half);
		};
		for (size_t p = 0; p < S.Parts.size(); ++p) {
			const std::vector<float> &X = S.Parts[p].Samples;
			const double AtSeam = StepAt(X, S.Seam), First = StepAt(X, S.LoopStart);
			std::vector<double> Usual;
			for (long long At = S.Seam - SamplesOf(1.0, S.Rate); At < S.Seam - Half * 4; At += Half)
				Usual.push_back(StepAt(X, At));
			const double Most = Percentile(Usual, 0.99);
			if (AtSeam >= 0.03 && AtSeam >= 4.0 * std::max(First, Most)) {
				ListenFinding &F = Add(LISTEN_CLICK, static_cast<int>(p));
				F.What = "seam";
				F.Begin = S.Seam;
				F.End = S.Seam + Half;
				F.Set("step", std::floor(AtSeam * 1000.0 + 0.5) / 1000.0);
				F.Sure = 0.6;
				F.Severity = AtSeam >= 0.08 ? 3 : 2;
				F.Heard = "A pop as the song goes round to its loop: the sound steps there, which it doesn't where the loop starts the "
					"first time.";
				F.Summary = "a pop at the loop's seam";
			}
		}
	}
}

void Ears::Drones()
{
	for (size_t p = 0; p < S.Parts.size(); ++p) {
		const ListenPart &P = S.Parts[p];
		const long long Size = static_cast<long long>(P.Samples.size());
		std::vector<int> Notes;
		std::vector<double> Unders, Lasts;
		for (size_t i = 0; i < P.Notes.size(); ++i) {
			const ListenNote &N = P.Notes[i];
			const bool Last = i + 1 == P.Notes.size();
			if (N.Begin < 0 || (!N.Released && !N.Halted && !Last))
				continue;
			// What comes after: the next note, or the loop going round (its music, not a drone), or the render's end
			long long Next = Last ? Size : P.Notes[i + 1].Begin;
			if (Last && S.Seam > N.Begin)
				Next = std::min(Next, S.Seam);
			if (Next < 0 || Seconds(Next - N.End, S.Rate) < DRONE_GAP)
				continue;
			const double Note = Db(LoudestSwing(P.Samples, std::max(N.Begin, N.End - SamplesOf(0.2, S.Rate)), N.End,
				SamplesOf(0.02, S.Rate), S.Rate));
			if (Note < -100.0)
				continue;	// it had died away: nothing to hold on
			const long long Until = std::min(Next, N.End + SamplesOf(2.0, S.Rate));
			const double Early = Db(SwingSquare(P.Samples, N.End + SamplesOf(0.25, S.Rate), N.End + SamplesOf(0.5, S.Rate), S.Rate));
			const double Late = Db(SwingSquare(P.Samples, Until - SamplesOf(0.25, S.Rate), Until, S.Rate));
			const double Fade = P.Rings ? 1.0 : DRONE_FADE_DB;
			// Louder than the note was is something else (another part's ring, the space)
			if (Early >= Note - (N.Halted ? 30.0 : DRONE_UNDER_DB) && Late >= Early - Fade && Late >= DRONE_FLOOR_DB && Late <= Note + 1.0) {
				Notes.push_back(static_cast<int>(i));
				Unders.push_back(Note - Late);
				Lasts.push_back(Seconds(Until - N.End, S.Rate));
			}
		}
		if (Notes.empty())
			continue;
		ListenFinding &F = Add(LISTEN_DRONE, static_cast<int>(p));
		F.Notes = Notes;
		Place(F);
		const double Under = Median(Unders), Lasting = Median(Lasts);
		F.Set("notes", static_cast<double>(Notes.size()));
		F.Set("db_under", std::floor(Under * 10.0 + 0.5) / 10.0);
		F.Set("seconds", std::floor(Lasting * 10.0 + 0.5) / 10.0);
		F.Sure = 0.7;
		F.Severity = Under <= DRONE_LOUD_DB && Lasting >= DRONE_LONG && Loud(static_cast<int>(p)) ? 3 : Under > 20.0 ? 1 : 2;
		F.Heard = Text("After %s ends (a release or a halt), its sound holds on, only %s dB softer, for %s s or more where nothing "
			"is written: a drone the notes don't show.", Notes.size() == 1 ? "a note" : Text("%d notes", static_cast<int>(Notes.size())).c_str(),
			Plain(Under, 0).c_str(), Plain(Lasting, 1).c_str());
		F.Summary = Text("sound holds on after %s", Notes.size() == 1 ? "a note" : Text("%d notes", static_cast<int>(Notes.size())).c_str());
	}
}

void Ears::Unheard()
{
	for (size_t p = 0; p < S.Parts.size(); ++p) {
		const ListenPart &P = S.Parts[p];
		if (m_PartDb[p] <= -150.0)
			continue;
		std::vector<int> Notes;
		for (size_t i = 0; i < P.Notes.size(); ++i) {
			const ListenNote &N = P.Notes[i];
			if (N.Begin < 0 || Seconds(N.End - N.Begin, S.Rate) < 0.03)
				continue;
			const double Level = Db(LoudestMeanSquare(P.Samples, N.Begin, std::min(N.End, N.Begin + SamplesOf(0.15, S.Rate)),
				SamplesOf(0.01, S.Rate)));
			if (Level <= m_PartDb[p] - UNHEARD_DB || Level <= UNHEARD_FLOOR_DB)
				Notes.push_back(static_cast<int>(i));
		}
		if (Notes.empty())
			continue;
		ListenFinding &F = Add(LISTEN_UNHEARD, static_cast<int>(p));
		F.Notes = Notes;
		Place(F);
		F.Set("notes", static_cast<double>(Notes.size()));
		F.Sure = 0.8;
		F.Severity = P.Melody ? 2 : 1;
		F.Heard = Text("%s %s hardly heard: %s dB or more under the part's other notes, as if %s weren't there.",
			Count(static_cast<int>(Notes.size()), "note", "notes").c_str(), Notes.size() == 1 ? "is" : "are", Plain(UNHEARD_DB, 0).c_str(),
			Notes.size() == 1 ? "it" : "they");
		F.Summary = Text("%s hardly heard", Count(static_cast<int>(Notes.size()), "note", "notes").c_str());
	}
}

void Ears::Clipping()
{
	const long long Size = static_cast<long long>(std::min(S.Left.size(), S.Right.size()));
	const long long End = S.Bars.empty() ? Size : std::min(Size, S.Bars.back());
	int Clipped = 0, Places = 0;
	long long First = -1, Last = -1, Previous = -1;
	for (long long n = S.Bars.empty() ? 0 : S.Bars.front(); n < End; ++n) {
		if (std::fabs(S.Left[static_cast<size_t>(n)]) < CLIP_LEVEL && std::fabs(S.Right[static_cast<size_t>(n)]) < CLIP_LEVEL)
			continue;
		++Clipped;
		if (Previous < 0 || n - Previous > SamplesOf(0.05, S.Rate))
			++Places;
		if (First < 0)
			First = n;
		Last = Previous = n;
	}
	if (Clipped < 2)
		return;
	ListenFinding &F = Add(LISTEN_CLIPPING, -1);
	F.Begin = First;
	F.End = Last + 1;
	F.Set("samples", Clipped);
	F.Set("places", Places);
	F.Sure = 1.0;
	F.Severity = Clipped >= 100 || Places >= 3 ? 3 : 2;
	F.Heard = Text("The mix goes past full scale %s (%d samples): heard as crackles where it's loudest.", Places == 1 ? "once" :
		Text("in %d places", Places).c_str(), Clipped);
	F.Summary = Text("clips in %s", Count(Places, "place", "places").c_str());
}

void Ears::Jumps()
{
	if (S.Bars.size() < 3)
		return;
	std::vector<double> Lufs;
	for (size_t b = 0; b + 1 < S.Bars.size(); ++b) {
		std::vector<std::pair<size_t, size_t> > Span(1, std::make_pair(static_cast<size_t>(std::max<long long>(0, S.Bars[b])),
			static_cast<size_t>(std::max<long long>(0, S.Bars[b + 1]))));
		Lufs.push_back(SpansLoudness(S.Left, S.Right, S.Rate, Span));
	}
	for (size_t b = 1; b < Lufs.size(); ++b) {
		const bool Section = b < S.SectionStarts.size() && S.SectionStarts[b];
		const bool Loop = S.LoopStart >= S.Bars[b] && S.LoopStart < S.Bars[b + 1];
		if (Section || Loop || Lufs[b] < JUMP_FLOOR || Lufs[b - 1] < JUMP_FLOOR)
			continue;
		const double Change = Lufs[b] - Lufs[b - 1];
		if (std::fabs(Change) < JUMP_DB)
			continue;
		ListenFinding &F = Add(LISTEN_JUMP, -1);
		F.Begin = S.Bars[b];
		F.End = S.Bars[b + 1];
		F.Set("bar_index", static_cast<double>(b));
		F.Set("db", std::floor(Change * 10.0 + 0.5) / 10.0);
		F.Sure = 0.8;
		F.Severity = std::fabs(Change) >= 12.0 ? 2 : 1;
		F.Heard = Text("The song gets %s dB %s from one bar to the next where no section starts: a jump the ear notices.",
			Plain(std::fabs(Change), 0).c_str(), Change > 0.0 ? "louder" : "softer");
		F.Summary = Text("%s dB %s at once", Plain(std::fabs(Change), 0).c_str(), Change > 0.0 ? "louder" : "softer");
	}
}

void Ears::Balance()
{
	if (S.Bars.size() < 2)
		return;
	// Each bar's shares, once
	std::vector<double> HarshShare(S.Bars.size() - 1, -200.0), MudShare(S.Bars.size() - 1, -200.0);
	for (size_t b = 0; b + 1 < S.Bars.size(); ++b) {
		const std::vector<double> Power = Spectrum(m_Mono, 0, S.Bars[b], S.Bars[b + 1]);
		const double All = BandPower(Power, S.Rate, BAND_LOW, BAND_TOP);
		if (All <= 1e-9)
			continue;
		HarshShare[b] = Db(BandPower(Power, S.Rate, HARSH_FROM, BAND_TOP) / All);
		MudShare[b] = Db(BandPower(Power, S.Rate, MUD_FROM, MUD_TO) / All);
	}
	m_HarshShares = HarshShare;
	m_MudShares = MudShare;
	for (int Harsh = 1; Harsh >= 0; --Harsh) {
		const double Limit = Harsh ? HARSH_DB : MUDDY_DB;
		std::vector<size_t> Bars;
		std::vector<double> Shares;
		for (size_t b = 0; b + 1 < S.Bars.size(); ++b) {
			const double Share = Harsh ? HarshShare[b] : MudShare[b];
			if (Share >= Limit) {
				Bars.push_back(b);
				Shares.push_back(Share);
			}
		}
		// Stretches of two bars or more in a row
		for (size_t i = 0; i < Bars.size(); ) {
			size_t j = i;
			while (j + 1 < Bars.size() && Bars[j + 1] == Bars[j] + 1)
				++j;
			if (j > i) {
				ListenFinding &F = Add(Harsh ? LISTEN_HARSH : LISTEN_MUDDY, -1);
				F.Begin = S.Bars[Bars[i]];
				F.End = S.Bars[Bars[j] + 1];
				std::vector<double> Here(Shares.begin() + static_cast<std::ptrdiff_t>(i), Shares.begin() + static_cast<std::ptrdiff_t>(j + 1));
				F.Set("share_db", std::floor(Median(Here) * 10.0 + 0.5) / 10.0);
				F.Set("bar_index", static_cast<double>(Bars[i]));
				F.Set("bars", static_cast<double>(j - i + 1));
				// The part that makes most of it there
				double Most = 0.0;
				for (size_t p = 0; p < S.Parts.size(); ++p) {
					const std::vector<double> Power = Spectrum(S.Parts[p].Samples, 0, F.Begin, F.End);
					const double Band = Harsh ? BandPower(Power, S.Rate, HARSH_FROM, BAND_TOP) : BandPower(Power, S.Rate, MUD_FROM, MUD_TO);
					if (Band > Most) {
						Most = Band;
						F.Other = static_cast<int>(p);
					}
				}
				F.Part = F.Other;
				F.Sure = 0.6;
				F.Severity = 1;
				const std::string Who = F.Other >= 0 ? S.Parts[static_cast<size_t>(F.Other)].Name : std::string("the mix");
				const int Share = static_cast<int>(std::floor(100.0 * std::pow(10.0, F.Number("share_db") / 10.0) + 0.5));
				F.Heard = Harsh ? Text("These bars are bright and edgy: %d%% of their sound's power is from 2 kHz up, where the "
					"soundtrack's own bars have 5%% on average and hardly ever pass 30%%; most of it is %s.", Share, Who.c_str()) :
					Text("These bars are boomy and thick: %d%% of their sound's power is from 160 to 500 Hz, where the soundtrack's own "
					"bars have about a third and hardly ever pass 80%%; most of it is %s.", Share, Who.c_str());
				F.Summary = Harsh ? "bright and edgy" : "boomy and thick";
			}
			i = j + 1;
		}
	}
}

void Ears::Covered()
{
	int Melody = -1;
	for (size_t p = 0; p < S.Parts.size(); ++p)
		if (S.Parts[p].Melody && S.Parts[p].Pitched)
			Melody = static_cast<int>(p);
	if (Melody < 0 || S.Bars.size() < 2)
		return;
	const ListenPart &M = S.Parts[static_cast<size_t>(Melody)];
	const std::vector<float> &Mono = m_Mono;
	std::vector<size_t> Bars;
	std::vector<double> Overs;
	for (size_t b = 0; b + 1 < S.Bars.size(); ++b) {
		const long long Begin = S.Bars[b], End = S.Bars[b + 1];
		// The melody's pitch there: its notes' middle, weighed by how long each sounds
		std::vector<double> Pitches;
		for (size_t i = 0; i < M.Notes.size(); ++i) {
			const ListenNote &N = M.Notes[i];
			const long long From = std::max(Begin, N.Begin), To = std::min(End, N.End);
			if (N.Hz <= 0.0 || To <= From)
				continue;
			for (long long k = 0; k < (To - From) / SamplesOf(0.05, S.Rate) + 1; ++k)
				Pitches.push_back(N.Hz);
		}
		if (Pitches.empty())
			continue;
		const double Pitch = Median(Pitches);
		const std::vector<double> Mine = Spectrum(M.Samples, 0, Begin, End), Others = Spectrum(Mono, &M.Samples, Begin, End);
		const double Low = Pitch * 0.7, High = Pitch * 2.8;
		const double InMine = BandPower(Mine, S.Rate, Low, High), InOthers = BandPower(Others, S.Rate, Low, High);
		const double AllMine = BandPower(Mine, S.Rate, BAND_LOW, BAND_TOP), AllOthers = BandPower(Others, S.Rate, BAND_LOW, BAND_TOP);
		if (InMine <= 1e-9 || AllMine <= 1e-9)
			continue;
		const double Over = Db(InOthers / InMine);
		if (Over >= COVER_DB && Db(AllMine / std::max(1e-12, AllOthers)) >= -3.0) {
			Bars.push_back(b);
			Overs.push_back(Over);
		}
	}
	for (size_t i = 0; i < Bars.size(); ) {
		size_t j = i;
		while (j + 1 < Bars.size() && Bars[j + 1] == Bars[j] + 1)
			++j;
		if (j > i) {
			ListenFinding &F = Add(LISTEN_COVERED, Melody);
			F.Begin = S.Bars[Bars[i]];
			F.End = S.Bars[Bars[j] + 1];
			std::vector<double> Here(Overs.begin() + static_cast<std::ptrdiff_t>(i), Overs.begin() + static_cast<std::ptrdiff_t>(j + 1));
			F.Set("db_over", std::floor(Median(Here) * 10.0 + 0.5) / 10.0);
			F.Set("bar_index", static_cast<double>(Bars[i]));
			F.Set("bars", static_cast<double>(j - i + 1));
			for (size_t k = 0; k < M.Notes.size(); ++k)
				if (M.Notes[k].End > F.Begin && M.Notes[k].Begin < F.End)
					F.Notes.push_back(static_cast<int>(k));
			// Who covers it most: the part with the most power in the melody's band there
			double Most = 0.0;
			for (size_t p = 0; p < S.Parts.size(); ++p) {
				if (static_cast<int>(p) == Melody)
					continue;
				std::vector<double> Pitches;
				for (size_t k = 0; k < F.Notes.size(); ++k)
					Pitches.push_back(M.Notes[static_cast<size_t>(F.Notes[k])].Hz);
				const double Pitch = Median(Pitches);
				const double Band = BandPower(Spectrum(S.Parts[p].Samples, 0, F.Begin, F.End), S.Rate, Pitch * 0.7, Pitch * 2.8);
				if (Band > Most) {
					Most = Band;
					F.Other = static_cast<int>(p);
				}
			}
			F.Sure = 0.6;
			F.Severity = 2;
			const std::string Who = F.Other >= 0 ? S.Parts[static_cast<size_t>(F.Other)].Name : std::string("the other parts");
			F.Heard = Text("The melody is covered here: in its own register %s plays %s dB over it, so the tune blurs into the "
				"accompaniment though the loudness balance looks right.", Who.c_str(), Plain(F.Number("db_over"), 1).c_str());
			F.Summary = Text("covered by %s in its register", Who.c_str());
		}
		i = j + 1;
	}
}

std::vector<ListenFinding> Ears::Listen(ListenStats *pStats)
{
	Measure();
	Wobbles();
	Tuning();
	Beating();
	Clicks();
	Clipping();
	Drones();
	Unheard();
	Covered();
	Jumps();
	Balance();
	if (pStats != 0) {
		pStats->Parts = static_cast<int>(S.Parts.size());
		pStats->NotesMeasured = 0;
		pStats->Notes.clear();
		for (size_t p = 0; p < m_Tracks.size(); ++p)
			for (size_t i = 0; i < m_Tracks[p].size(); ++i) {
				if (!m_Tracks[p][i].Pitch.Valid)
					continue;
				++pStats->NotesMeasured;
				ListenStats::Measured M;
				M.Part = static_cast<int>(p);
				M.Note = static_cast<int>(i);
				M.Pitch = m_Tracks[p][i].Pitch;
				M.Clicks = p < m_Clicks.size() && i < m_Clicks[p].size() ? m_Clicks[p][i] : 0;
				pStats->Notes.push_back(M);
			}
		pStats->PartDb = m_PartDb;
		pStats->HarshShares = m_HarshShares;
		pStats->MudShares = m_MudShares;
		pStats->Seconds = S.Bars.size() >= 2 ? Seconds(S.Bars.back() - S.Bars.front(), S.Rate) : Seconds(static_cast<long long>(S.Left.size()), S.Rate);
	}
	std::stable_sort(m_Out.begin(), m_Out.end(), [](const ListenFinding &a, const ListenFinding &b) {
		if (a.Kind != b.Kind)
			return a.Kind < b.Kind;
		if (a.Severity != b.Severity)
			return a.Severity > b.Severity;
		return a.Begin < b.Begin;
	});
	return m_Out;
}

}	// namespace

std::vector<ListenFinding> ListenToSong(const ListenSong &Song, ListenStats *pStats)
{
	Ears E(Song);
	return E.Listen(pStats);
}

}	// namespace ftapi

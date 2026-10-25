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

// Tapping the tempo: a key or a button tapped along with the beat, and the
// steady beat that fits the taps.

#include "Transcribe.h"

#include <algorithm>
#include <cmath>

namespace transcribe {

namespace {

const double LONGEST_PAUSE = 2.0;	// seconds without a tap that start a new run
const double PAUSE_BEATS = 2.5;		// or this many beats
const double SLOWEST_BEAT = 2.0;	// 30 BPM
const double QUICKEST_BEAT = 0.2;	// 300 BPM

double Median(std::vector<double> v)
{
	if (v.empty())
		return 0.0;
	std::sort(v.begin(), v.end());
	const size_t n = v.size();
	return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

// The steady beat (and its phase) that fits taps on beats best: least squares
// over the taps, each on the beat it is nearest to counting from the first
void FitBeat(const std::vector<double> &taps, double &beat, double &phase, std::vector<long long> &beats)
{
	const size_t n = taps.size();
	for (int pass = 0; pass < 3; ++pass) {
		beats.assign(n, 0);
		for (size_t i = 0; i < n; ++i)
			beats[i] = (long long)std::floor((taps[i] - taps[0]) / beat + 0.5);
		double kMean = 0.0, tMean = 0.0;
		for (size_t i = 0; i < n; ++i) {
			kMean += double(beats[i]);
			tMean += taps[i];
		}
		kMean /= double(n);
		tMean /= double(n);
		double kk = 0.0, kt = 0.0;
		for (size_t i = 0; i < n; ++i) {
			kk += (double(beats[i]) - kMean) * (double(beats[i]) - kMean);
			kt += (double(beats[i]) - kMean) * (taps[i] - tMean);
		}
		if (kk <= 0.0)
			return;
		beat = kt / kk;
		phase = tMean - beat * kMean;
	}
}

}	// namespace

TapTempo TempoOfTaps(const std::vector<double> &times, int minTaps)
{
	TapTempo result;
	if (times.size() < 2)
		return result;

	// The last run: back from the last tap to a pause
	size_t first = times.size() - 1;
	std::vector<double> gaps;
	while (first > 0) {
		const double gap = times[first] - times[first - 1];
		if (gap > LONGEST_PAUSE || gap <= 0.0)
			break;
		if (gaps.size() >= 2 && gap > PAUSE_BEATS * Median(gaps))
			break;
		gaps.push_back(gap);
		--first;
	}

	// Double taps out: a tap much sooner after the one before than the beat is
	const double typical = Median(gaps);
	std::vector<double> taps;
	for (size_t i = first; i < times.size(); ++i) {
		if (!taps.empty() && times[i] - taps.back() < 0.5 * typical)
			continue;
		taps.push_back(times[i]);
	}
	result.taps = int(taps.size());
	if (result.taps < std::max(2, minTaps))
		return result;

	// The beat: from the gaps that are about one beat, then fitted to all the taps
	std::vector<double> single;
	for (size_t i = 1; i < taps.size(); ++i) {
		const double gap = taps[i] - taps[i - 1];
		if (gap < 1.5 * typical)
			single.push_back(gap);
	}
	double beat = single.empty() ? typical : Median(single), phase = taps[0];
	std::vector<long long> beats;
	FitBeat(taps, beat, phase, beats);
	if (!(beat >= QUICKEST_BEAT && beat <= SLOWEST_BEAT))
		return result;

	// How unevenly they came
	std::vector<double> off;
	for (size_t i = 0; i < taps.size(); ++i)
		off.push_back(taps[i] - (phase + beat * double(beats[i])));
	std::sort(off.begin(), off.end());
	const size_t q1 = off.size() / 4, q3 = (off.size() * 3) / 4;
	result.spread = (off[std::min(q3, off.size() - 1)] - off[q1]) / beat;
	result.bpm = 60.0 / beat;
	return result;
}

}	// namespace transcribe

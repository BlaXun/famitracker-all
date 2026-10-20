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

// Percussive hit detection (finger taps, beatboxing) and rough drum sorting.

#include "Transcribe.h"

#include <algorithm>
#include <cmath>

namespace transcribe {

namespace {

double ToDb(double energy) { return 10.0 * std::log10(energy + 1e-12); }

// Share of high-frequency content in the attack: energy of the first
// difference over the signal energy (0 for DC, up to 4 for alternating samples).
double Brightness(const std::vector<float> &x, size_t from, size_t count)
{
	double energy = 0.0, diffEnergy = 0.0;
	const size_t end = std::min(x.size(), from + count);
	for (size_t i = from + 1; i < end; ++i) {
		energy += double(x[i]) * x[i];
		const double d = double(x[i]) - x[i - 1];
		diffEnergy += d * d;
	}
	return energy > 1e-12 ? diffEnergy / energy : 0.0;
}

// Best split of sorted values into k contiguous groups (k = 2 or 3) by least
// squares. Returns the group boundaries (indices where each group starts).
std::vector<size_t> SplitSorted(const std::vector<double> &v, int k)
{
	const size_t n = v.size();
	auto cost = [&v](size_t a, size_t b) {	// within-group squared error of v[a..b)
		double mean = 0.0;
		for (size_t i = a; i < b; ++i) mean += v[i];
		mean /= double(b - a);
		double c = 0.0;
		for (size_t i = a; i < b; ++i) c += (v[i] - mean) * (v[i] - mean);
		return c;
	};
	std::vector<size_t> best;
	double bestCost = 1e300;
	if (k == 2) {
		for (size_t s = 1; s < n; ++s) {
			const double c = cost(0, s) + cost(s, n);
			if (c < bestCost) { bestCost = c; best = { 0, s }; }
		}
	}
	else {
		for (size_t s1 = 1; s1 + 1 < n; ++s1)
			for (size_t s2 = s1 + 1; s2 < n; ++s2) {
				const double c = cost(0, s1) + cost(s1, s2) + cost(s2, n);
				if (c < bestCost) { bestCost = c; best = { 0, s1, s2 }; }
			}
	}
	return best;
}

double MeanOf(const std::vector<double> &v, size_t a, size_t b)
{
	double m = 0.0;
	for (size_t i = a; i < b; ++i) m += v[i];
	return m / double(b - a);
}

}	// namespace

std::vector<Hit> DetectHits(const Audio &audio, const HitOptions &options)
{
	std::vector<Hit> hits;
	const int rate = audio.sampleRate;
	const std::vector<float> &x = audio.samples;
	if (rate <= 0)
		return hits;

	const size_t frame = std::max<size_t>(16, size_t(std::lround(options.frameSeconds * rate)));
	const size_t hop = std::max<size_t>(1, size_t(std::lround(options.hopSeconds * rate)));
	if (x.size() < frame + hop)
		return hits;

	// Short-time energy, in dB
	std::vector<double> db;
	for (size_t start = 0; start + frame <= x.size(); start += hop) {
		double e = 0.0;
		for (size_t j = 0; j < frame; ++j)
			e += double(x[start + j]) * x[start + j];
		db.push_back(ToDb(e / frame));
	}

	const size_t history = std::max<size_t>(4, size_t(std::lround(0.050 / options.hopSeconds)));
	const size_t peakSpan = std::max<size_t>(2, size_t(std::lround(0.030 / options.hopSeconds)));
	const size_t minGap = std::max<size_t>(1, size_t(std::lround(options.minIntervalSeconds / options.hopSeconds)));
	const size_t attack = size_t(std::lround(0.030 * rate));

	size_t i = 1;
	while (i < db.size()) {
		// Background level: the quietest recent frame
		const size_t from = i > history ? i - history : 0;
		double level = db[from];
		for (size_t k = from; k < i; ++k)
			level = std::min(level, db[k]);

		if (db[i] - level < options.sensitivityDb) {
			++i;
			continue;
		}

		size_t peak = i;
		for (size_t k = i; k < std::min(db.size(), i + peakSpan); ++k)
			if (db[k] > db[peak])
				peak = k;

		// The window of frame i is the first to reach well above the background;
		// the hit itself starts near the end of that window.
		const size_t onsetSample = i * hop + frame - std::min(frame, hop);
		Hit hit;
		hit.time = double(onsetSample) / rate;
		hit.strength = std::pow(10.0, db[peak] / 20.0);	// RMS amplitude of the loudest frame
		hit.brightness = Brightness(x, onsetSample > frame ? onsetSample - frame / 2 : 0, attack);
		hits.push_back(hit);

		i = std::max(peak, i) + minGap;
	}

	// Drop hits far quieter than the loudest one (handling noise, breaths)
	double loudest = 0.0;
	for (size_t k = 0; k < hits.size(); ++k)
		loudest = std::max(loudest, hits[k].strength);
	const double floor = loudest * std::pow(10.0, options.silenceDb / 20.0);
	hits.erase(std::remove_if(hits.begin(), hits.end(), [floor](const Hit &h) { return h.strength < floor; }), hits.end());
	return hits;
}

int ClassifyHits(std::vector<Hit> &hits)
{
	for (size_t i = 0; i < hits.size(); ++i)
		hits[i].drum = Drum::Snare;
	if (hits.size() < 4)
		return 1;

	// Work in log-brightness so "twice as bright" is the same step everywhere
	std::vector<double> values;
	for (size_t i = 0; i < hits.size(); ++i)
		values.push_back(std::log(hits[i].brightness + 1e-6));
	std::vector<double> sorted(values);
	std::sort(sorted.begin(), sorted.end());

	const double MIN_STEP = 0.35;	// about 1.4x brighter
	const size_t MIN_GROUP = 2;

	std::vector<double> thresholds;	// brightness values that separate groups
	if (hits.size() >= 3 * MIN_GROUP) {
		const std::vector<size_t> s = SplitSorted(sorted, 3);
		if (s.size() == 3 && s[1] - s[0] >= MIN_GROUP && s[2] - s[1] >= MIN_GROUP && sorted.size() - s[2] >= MIN_GROUP) {
			const double m0 = MeanOf(sorted, s[0], s[1]), m1 = MeanOf(sorted, s[1], s[2]), m2 = MeanOf(sorted, s[2], sorted.size());
			if (m1 - m0 >= MIN_STEP && m2 - m1 >= MIN_STEP)
				thresholds = { 0.5 * (sorted[s[1] - 1] + sorted[s[1]]), 0.5 * (sorted[s[2] - 1] + sorted[s[2]]) };
		}
	}
	if (thresholds.empty()) {
		const std::vector<size_t> s = SplitSorted(sorted, 2);
		if (s.size() == 2 && s[1] - s[0] >= MIN_GROUP && sorted.size() - s[1] >= MIN_GROUP) {
			const double m0 = MeanOf(sorted, s[0], s[1]), m1 = MeanOf(sorted, s[1], sorted.size());
			if (m1 - m0 >= MIN_STEP)
				thresholds = { 0.5 * (sorted[s[1] - 1] + sorted[s[1]]) };
		}
	}

	for (size_t i = 0; i < hits.size(); ++i) {
		if (thresholds.size() == 2)
			hits[i].drum = values[i] < thresholds[0] ? Drum::Kick : (values[i] < thresholds[1] ? Drum::Snare : Drum::HiHat);
		else if (thresholds.size() == 1)
			hits[i].drum = values[i] < thresholds[0] ? Drum::Kick : Drum::Snare;
	}
	return int(thresholds.size()) + 1;
}

std::vector<GridHit> QuantizeHits(const std::vector<Hit> &input, const Grid &grid, bool alignToStart)
{
	std::vector<GridHit> result;
	const double rate = grid.RowsPerSecond();
	if (rate <= 0.0 || input.empty())
		return result;

	std::vector<Hit> hits(input);
	std::stable_sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b) { return a.time < b.time; });

	double loudest = 0.0;
	for (size_t i = 0; i < hits.size(); ++i)
		loudest = std::max(loudest, hits[i].strength);

	const int offset = alignToStart ? int(std::lround(hits.front().time * rate)) : 0;
	std::vector<double> strengths;
	for (size_t i = 0; i < hits.size(); ++i) {
		GridHit g;
		g.row = std::max(0, int(std::lround(hits[i].time * rate)) - offset);
		g.drum = hits[i].drum;
		// 2.5 dB per volume step below the loudest hit, never quieter than 5
		const double db = 20.0 * std::log10(std::max(hits[i].strength, 1e-9) / loudest);
		g.volume = std::max(5, std::min(15, 15 + int(std::lround(db / 2.5))));
		if (!result.empty() && result.back().row == g.row) {
			if (hits[i].strength > strengths.back()) {	// keep the stronger hit on a shared row
				result.back() = g;
				strengths.back() = hits[i].strength;
			}
			continue;
		}
		result.push_back(g);
		strengths.push_back(hits[i].strength);
	}
	return result;
}

}	// namespace transcribe

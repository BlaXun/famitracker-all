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

// Recording inside FamiTracker: the click that counts in and keeps time, the
// click as a microphone without headphones hears it, and lining a take up
// with the rows it was recorded against.

#include "Transcribe.h"

#include <algorithm>
#include <cmath>

namespace transcribe {

const double ClickTrack::HIGH_HZ = 1760.0;
const double ClickTrack::LOW_HZ = 1320.0;
const double ClickTrack::SECONDS = 0.040;

namespace {

const double PI = 3.14159265358979323846;

// How much of a sound, from start for seconds, is a tone at frequency:
// about 0.6 for one of the clicks (a decaying tone), a few percent for a tap.
double ToneShare(const Audio &audio, double start, double seconds, double frequency)
{
	const int rate = audio.sampleRate;
	if (rate <= 0)
		return 0.0;
	const long long from = std::max(0LL, (long long)std::llround(start * rate));
	const long long count = (long long)std::llround(seconds * rate);
	const long long end = std::min<long long>((long long)audio.samples.size(), from + count);
	if (end - from < 16)
		return 0.0;

	// Goertzel
	const double coeff = 2.0 * std::cos(2.0 * PI * frequency / rate);
	double s1 = 0.0, s2 = 0.0, energy = 0.0;
	for (long long i = from; i < end; ++i) {
		const double x = audio.samples[size_t(i)];
		const double s = x + coeff * s1 - s2;
		s2 = s1;
		s1 = s;
		energy += x * x;
	}
	const double power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
	const double n = double(end - from);
	return energy > 1e-12 ? power / (energy * n / 2.0) : 0.0;
}

// The click's share in the 30 ms from just before a hit
double ClickLikeness(const Audio &audio, double time)
{
	return std::max(ToneShare(audio, time - 0.002, 0.030, ClickTrack::HIGH_HZ),
		ToneShare(audio, time - 0.002, 0.030, ClickTrack::LOW_HZ));
}

const double CLICK_LIKENESS = 0.3;	// a tap scores a few hundredths, a heard click 0.4-0.6

double Median(std::vector<double> v)
{
	if (v.empty())
		return 0.0;
	std::sort(v.begin(), v.end());
	const size_t n = v.size();
	return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

}	// namespace

// ---------------------------------------------------------------------------
// The click
// ---------------------------------------------------------------------------

long long ClickTrack::BeatSample(int beat) const
{
	return (long long)std::llround((leadSeconds + beat * beatSeconds) * sampleRate);
}

double ClickTrack::BeatTime(int beat) const
{
	return double(BeatSample(beat)) / sampleRate;
}

long long ClickTrack::StartSample() const
{
	return BeatSample(countIn);
}

bool ClickTrack::Clicks(int beat) const
{
	return beat >= 0 && (beat < countIn || clickAlong);
}

void ClickTrack::Render(long long from, float *out, int count) const
{
	for (int i = 0; i < count; ++i)
		out[i] = 0.0f;
	if (sampleRate <= 0 || beatSeconds <= 0.0 || count <= 0)
		return;

	const long long length = (long long)std::llround(SECONDS * sampleRate);
	const long long attack = std::max(1LL, (long long)std::llround(0.001 * sampleRate));
	const double decay = 0.010 * sampleRate;	// samples

	// The first beat whose click may reach into the block
	const double first = (double(from - length) / sampleRate - leadSeconds) / beatSeconds;
	int beat = std::max(0, int(std::floor(first)));
	for (; BeatSample(beat) < from + count; ++beat) {
		if (!Clicks(beat))
			continue;
		const long long start = BeatSample(beat);
		const bool accent = beatsPerBar > 0 && beat % beatsPerBar == 0;
		const double frequency = accent ? HIGH_HZ : LOW_HZ;
		const double amplitude = level * (accent ? 1.0 : 0.7);
		const long long a = std::max(from, start), b = std::min(from + count, start + length);
		for (long long n = a; n < b; ++n) {
			const long long t = n - start;
			double envelope = std::exp(-double(t) / decay);
			if (t < attack)
				envelope *= double(t) / double(attack);
			out[n - from] += float(amplitude * envelope * std::sin(2.0 * PI * frequency * double(t) / sampleRate));
		}
	}
}

Beat BeatFor(double rowsPerSecond, int firstHighlight, int secondHighlight)
{
	Beat beat;
	beat.rows = (firstHighlight >= 1 && firstHighlight <= 64) ? firstHighlight : 4;
	const double rowSeconds = rowsPerSecond > 0.0 ? 1.0 / rowsPerSecond : 0.1;
	while (beat.rows * rowSeconds < 0.25 && beat.rows <= 32)
		beat.rows *= 2;
	while (beat.rows * rowSeconds > 1.5 && beat.rows % 2 == 0)
		beat.rows /= 2;
	beat.perBar = (secondHighlight > 0 && secondHighlight % beat.rows == 0) ? secondHighlight / beat.rows : 4;
	if (beat.perBar < 2 || beat.perBar > 8)
		beat.perBar = 4;
	beat.countIn = beat.perBar >= 3 ? beat.perBar : 2 * beat.perBar;
	return beat;
}

// ---------------------------------------------------------------------------
// The click on speakers
// ---------------------------------------------------------------------------

ClickLeak FindClickLeak(const Audio &audio, const std::vector<double> &expected, double window)
{
	ClickLeak leak;
	if (expected.empty() || audio.sampleRate <= 0)
		return leak;

	// Hits around the count-in only, so the take's own sounds don't drown it
	const double first = *std::min_element(expected.begin(), expected.end()) - window;
	const double last = *std::max_element(expected.begin(), expected.end()) + window + ClickTrack::SECONDS;
	const size_t a = size_t(std::max(0.0, first) * audio.sampleRate);
	const size_t b = std::min(audio.samples.size(), size_t(std::max(0.0, last) * audio.sampleRate));
	if (b <= a)
		return leak;
	Audio part;
	part.sampleRate = audio.sampleRate;
	part.samples.assign(audio.samples.begin() + std::ptrdiff_t(a), audio.samples.begin() + std::ptrdiff_t(b));
	const double offset = double(a) / audio.sampleRate;
	const std::vector<Hit> hits = DetectHits(part, HitOptions());

	std::vector<double> delays, strengths;
	for (size_t k = 0; k < expected.size(); ++k) {
		const Hit *nearest = 0;
		for (size_t i = 0; i < hits.size(); ++i) {
			const double d = std::fabs(hits[i].time + offset - expected[k]);
			if (d <= window && (nearest == 0 || d < std::fabs(nearest->time + offset - expected[k])))
				nearest = &hits[i];
		}
		if (nearest == 0 || ClickLikeness(audio, nearest->time + offset) < CLICK_LIKENESS)
			continue;
		delays.push_back(nearest->time + offset - expected[k]);
		strengths.push_back(nearest->strength);
	}

	const size_t needed = std::max<size_t>(2, (3 * expected.size() + 3) / 4);
	if (delays.size() < needed)
		return leak;
	if (*std::max_element(delays.begin(), delays.end()) - *std::min_element(delays.begin(), delays.end()) > 0.012)
		return leak;	// not the clicks: they would arrive equally late
	leak.heard = true;
	leak.delay = Median(delays);
	leak.strength = Median(strengths);
	return leak;
}

bool MeasureTapDelay(const Audio &audio, const std::vector<double> &expected, double window, double *delay)
{
	if (expected.empty())
		return false;
	const std::vector<Hit> hits = DetectHits(audio, HitOptions());
	std::vector<double> delays;
	for (size_t k = 0; k < expected.size(); ++k) {
		double best = window + 1.0;
		for (size_t i = 0; i < hits.size(); ++i) {
			const double d = hits[i].time - expected[k];
			if (std::fabs(d) <= window && std::fabs(d) < std::fabs(best))
				best = d;
		}
		if (best <= window)
			delays.push_back(best);
	}
	if (delays.size() < std::max<size_t>(4, (6 * expected.size() + 9) / 10))
		return false;	// too many taps missing
	std::sort(delays.begin(), delays.end());
	const size_t n = delays.size();
	if (delays[(3 * n) / 4] - delays[n / 4] > 0.060)
		return false;	// all over the place
	if (delay != 0)
		*delay = Median(delays);
	return true;
}

void RemoveClickHits(std::vector<Hit> &hits, const Audio &audio, const std::vector<double> &clickTimes,
	const ClickLeak &leak, double tolerance)
{
	if (!leak.heard || clickTimes.empty())
		return;
	std::vector<double> times(clickTimes);
	std::sort(times.begin(), times.end());
	hits.erase(std::remove_if(hits.begin(), hits.end(), [&](const Hit &h) {
		const double t = h.time - leak.delay;
		const std::vector<double>::const_iterator next = std::lower_bound(times.begin(), times.end(), t);
		double distance = 1e9;
		if (next != times.end())
			distance = *next - t;
		if (next != times.begin())
			distance = std::min(distance, t - *(next - 1));
		return distance <= tolerance && h.strength < 2.0 * leak.strength &&
			ClickLikeness(audio, h.time) >= CLICK_LIKENESS;
	}), hits.end());
}

// ---------------------------------------------------------------------------
// Lining a take up with rows
// ---------------------------------------------------------------------------

double RowOffset(const std::vector<double> &onsets, const std::vector<double> &weights, double rowsPerSecond)
{
	if (onsets.size() < 4 || rowsPerSecond <= 0.0)
		return 0.0;
	// Mean phase of the onsets within a row, as an angle
	double c = 0.0, s = 0.0, total = 0.0;
	for (size_t i = 0; i < onsets.size(); ++i) {
		const double w = i < weights.size() ? weights[i] : 1.0;
		const double angle = 2.0 * PI * onsets[i] * rowsPerSecond;
		c += w * std::cos(angle);
		s += w * std::sin(angle);
		total += w;
	}
	if (total <= 0.0 || std::hypot(c, s) / total < 0.2)
		return 0.0;	// spread all over the row: no common offset
	// -0.5 .. 0.5 rows. A take runs late more often than early (the sound
	// takes a moment to reach the speakers and come back through the
	// microphone), so up to half a row counts as late, but more than 0.35 rows
	// early is taken as too uncertain to move: it could as well be late.
	const double rows = std::atan2(s, c) / (2.0 * PI);
	if (rows < -0.35)
		return 0.0;
	return rows / rowsPerSecond;
}

double PlaceNotes(std::vector<Note> &notes, double start, const Grid &grid, bool settle)
{
	const double rate = grid.RowsPerSecond();
	if (rate <= 0.0)
		return 0.0;
	const double row = 1.0 / rate;

	std::vector<Note> kept;
	for (size_t i = 0; i < notes.size(); ++i) {
		Note n = notes[i];
		n.start -= start;
		n.end -= start;
		if (n.start < -row || n.end <= 0.5 * row)
			continue;	// from the count-in
		kept.push_back(n);
	}

	double shift = 0.0;
	if (settle) {
		std::vector<double> onsets, weights;
		for (size_t i = 0; i < kept.size(); ++i) {
			onsets.push_back(kept[i].start);
			weights.push_back(std::min(1.0, (kept[i].end - kept[i].start) / 0.25));	// longer notes say more
		}
		shift = RowOffset(onsets, weights, rate);
	}
	for (size_t i = 0; i < kept.size(); ++i) {
		kept[i].start = std::max(0.0, kept[i].start - shift);
		kept[i].end = std::max(kept[i].start + 0.5 * row, kept[i].end - shift);
	}
	notes.swap(kept);
	return shift;
}

double PlaceHits(std::vector<Hit> &hits, double start, const Grid &grid, bool settle)
{
	const double rate = grid.RowsPerSecond();
	if (rate <= 0.0)
		return 0.0;
	const double row = 1.0 / rate;

	std::vector<Hit> kept;
	double loudest = 0.0;
	for (size_t i = 0; i < hits.size(); ++i) {
		Hit h = hits[i];
		h.time -= start;
		if (h.time < -0.5 * row)
			continue;	// from the count-in
		kept.push_back(h);
		loudest = std::max(loudest, h.strength);
	}

	double shift = 0.0;
	if (settle && loudest > 0.0) {
		std::vector<double> onsets, weights;
		for (size_t i = 0; i < kept.size(); ++i) {
			onsets.push_back(kept[i].time);
			weights.push_back(std::max(0.2, kept[i].strength / loudest));
		}
		shift = RowOffset(onsets, weights, rate);
	}
	for (size_t i = 0; i < kept.size(); ++i)
		kept[i].time = std::max(0.0, kept[i].time - shift);
	hits.swap(kept);
	return shift;
}

}	// namespace transcribe

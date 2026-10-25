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

// Strummed chords: where each strum of a guitar (or a chord on a piano)
// starts, and the notes that sound together in it.
//
// Strums start where the spectrum rises suddenly: the spectral flux, the
// growth of the log spectrum from one 10 ms frame to the next, peaks there.
// The notes come from the spectrum just after the strum, on a scale of thirds
// of a semitone, explained as notes and their overtones: each note's
// harmonics, softer the higher they are, fitted to the spectrum without going
// negative (multiplicative updates for non-negative least squares), so the
// overtone of one note doesn't become a note of its own.

#include "Transcribe.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace transcribe {

namespace {

typedef std::complex<double> Complex;
const double PI = 3.14159265358979323846;

// In-place iterative radix-2 FFT; size is a power of two
class Transform {
public:
	explicit Transform(size_t size) : n_(size), twiddle_(size / 2), reversed_(size)
	{
		for (size_t k = 0; k < n_ / 2; ++k)
			twiddle_[k] = std::polar(1.0, -2.0 * PI * double(k) / double(n_));
		size_t bits = 0;
		while ((size_t(1) << bits) < n_)
			++bits;
		for (size_t i = 0; i < n_; ++i) {
			size_t r = 0;
			for (size_t b = 0; b < bits; ++b)
				if (i & (size_t(1) << b))
					r |= size_t(1) << (bits - 1 - b);
			reversed_[i] = r;
		}
	}

	void Run(std::vector<Complex> &data) const
	{
		for (size_t i = 0; i < n_; ++i)
			if (i < reversed_[i])
				std::swap(data[i], data[reversed_[i]]);
		for (size_t len = 2; len <= n_; len <<= 1) {
			const size_t half = len / 2, step = n_ / len;
			for (size_t start = 0; start < n_; start += len) {
				for (size_t k = 0; k < half; ++k) {
					const Complex t = twiddle_[k * step] * data[start + k + half];
					data[start + k + half] = data[start + k] - t;
					data[start + k] += t;
				}
			}
		}
	}

	size_t Size() const { return n_; }

private:
	size_t n_;
	std::vector<Complex> twiddle_;
	std::vector<size_t> reversed_;
};

size_t PowerOfTwoNear(double samples)
{
	size_t n = 256;
	while (double(n) * 1.5 < samples && n < (size_t(1) << 16))
		n <<= 1;
	return n;
}

// The magnitude spectrum (bins 0 to n/2) of the samples from first on, through a
// Hann window of length samples (at most the transform's size), zero padded
std::vector<double> Magnitudes(const Audio &audio, long long first, size_t length, const Transform &fft)
{
	const size_t n = fft.Size();
	length = std::min(length, n);
	std::vector<Complex> data(n, Complex(0.0, 0.0));
	for (size_t i = 0; i < length; ++i) {
		const long long at = first + (long long)i;
		if (at < 0 || at >= (long long)audio.samples.size())
			continue;
		const double w = 0.5 - 0.5 * std::cos(2.0 * PI * (double(i) + 0.5) / double(length));
		data[i] = Complex(w * audio.samples[size_t(at)], 0.0);
	}
	fft.Run(data);
	std::vector<double> out(n / 2 + 1);
	for (size_t k = 0; k <= n / 2; ++k)
		out[k] = std::abs(data[k]);
	return out;
}

double Mean(const std::vector<double> &v, size_t from, size_t to)
{
	double sum = 0.0;
	size_t count = 0;
	for (size_t i = from; i < to && i < v.size(); ++i, ++count)
		sum += v[i];
	return count > 0 ? sum / double(count) : 0.0;
}

// Where strums start (seconds), with the spectral flux
std::vector<double> StrumOnsets(const Audio &audio, const StrumOptions &options)
{
	std::vector<double> onsets;
	const int rate = audio.sampleRate;
	if (rate <= 0 || audio.samples.empty())
		return onsets;
	const Transform fft(PowerOfTwoNear(0.046 * rate));
	const size_t n = fft.Size();
	const long long hop = std::max(1LL, (long long)std::llround(0.010 * rate));
	const size_t low = std::max<size_t>(1, size_t(30.0 * double(n) / rate));
	const size_t high = std::min(n / 2, size_t(8000.0 * double(n) / rate));
	if (high <= low)
		return onsets;

	// Every frame's magnitudes, and their loudest
	std::vector<std::vector<float> > frames;
	std::vector<double> energy;
	double loudest = 0.0;
	for (long long start = -(long long)n / 2; start + (long long)n / 2 < (long long)audio.samples.size(); start += hop) {
		const std::vector<double> m = Magnitudes(audio, start, n, fft);
		std::vector<float> keep(high - low);
		double e = 0.0;
		for (size_t k = low; k < high; ++k) {
			keep[k - low] = float(m[k]);
			loudest = std::max(loudest, m[k]);
			e += m[k] * m[k];
		}
		frames.push_back(keep);
		energy.push_back(e);
	}
	if (frames.size() < 3 || loudest <= 0.0)
		return onsets;

	// The flux: how much the log spectrum grows, over a floor 60 dB under the loudest
	const double floor = loudest * 1e-3;
	std::vector<double> flux(frames.size(), 0.0);
	for (size_t t = 1; t < frames.size(); ++t) {
		double sum = 0.0;
		for (size_t k = 0; k < frames[t].size(); ++k) {
			const double now = std::log(std::max(double(frames[t][k]), floor));
			const double before = std::log(std::max(double(frames[t - 1][k]), floor));
			sum += std::max(0.0, now - before);
		}
		flux[t] = sum / double(frames[t].size());
	}
	double peak = 0.0, loudestEnergy = 0.0;
	for (size_t t = 0; t < flux.size(); ++t) {
		peak = std::max(peak, flux[t]);
		loudestEnergy = std::max(loudestEnergy, energy[t]);
	}
	if (peak <= 0.0)
		return onsets;

	// Peaks well over their surroundings, in sound loud enough
	const double gate = loudestEnergy * std::pow(10.0, options.silenceDb / 10.0);
	std::vector<std::pair<double, double> > found;	// time, strength
	for (size_t t = 1; t + 1 < flux.size(); ++t) {
		bool top = true;
		for (size_t d = 1; d <= 3 && top; ++d) {
			if (t >= d && flux[t - d] > flux[t])
				top = false;
			if (t + d < flux.size() && flux[t + d] >= flux[t])
				top = false;
		}
		if (!top)
			continue;
		const size_t from = t >= 20 ? t - 20 : 0;
		const double threshold = 1.5 * Mean(flux, from, t + 21) + 0.08 * peak;
		if (flux[t] < threshold)
			continue;
		double after = 0.0;	// the loudest of the next 100 ms
		for (size_t u = t; u < t + 10 && u < energy.size(); ++u)
			after = std::max(after, energy[u]);
		if (after < gate)
			continue;
		// The strum starts where the rise to this peak began (a strum's strings are struck one after another)
		size_t rise = t;
		while (rise > 1 && t - rise < 8 && flux[rise - 1] > 0.3 * flux[t])
			--rise;
		const double time = (double(rise) * double(hop)) / rate;	// frames are centred on t * hop
		if (!found.empty() && time - found.back().first < options.minIntervalSeconds) {
			if (flux[t] > found.back().second)
				found.back().second = flux[t];	// one strum: it starts at the first of its peaks
			continue;
		}
		found.push_back(std::make_pair(time, flux[t]));
	}
	for (size_t i = 0; i < found.size(); ++i)
		onsets.push_back(std::max(0.0, found[i].first));
	return onsets;
}

// The notes sounding in the samples [from, to): a spectrum on a scale of
// thirds of a semitone, explained as notes with their harmonics
std::vector<StrumNote> NotesBetween(const Audio &audio, double from, double to, const StrumOptions &options)
{
	std::vector<StrumNote> notes;
	const int rate = audio.sampleRate;
	const double seconds = to - from;
	if (rate <= 0 || seconds < 0.03)
		return notes;

	// The average magnitude spectrum, from windows as long as the strum allows (up to ~370 ms, which tells a
	// low note from its neighbours)
	const Transform fft(PowerOfTwoNear(std::min(0.37, std::max(0.09, seconds)) * rate));
	const size_t n = fft.Size();
	const size_t length = std::min(n, size_t(seconds * rate));
	std::vector<double> power(n / 2 + 1, 0.0);
	int count = 0;
	for (double at = from; count == 0 || at + double(length) / rate <= to + 1e-9; at += double(length) / (2.0 * rate)) {
		const std::vector<double> m = Magnitudes(audio, (long long)std::llround(at * rate), length, fft);
		for (size_t k = 0; k < m.size(); ++k)
			power[k] += m[k] * m[k];
		++count;
		if (count >= 6)
			break;
	}
	const double binHz = double(rate) / double(n);

	// On a scale of thirds of a semitone, from a semitone under the lowest note to the 10th harmonic of the highest
	const double step = 1.0 / 3.0;
	const double lowest = options.lowestMidi - 1.0;
	const double top = std::min(options.highestMidi + 12.0 * std::log2(10.0), 69.0 + 12.0 * std::log2(0.45 * rate / 440.0));
	const int bins = std::max(0, int((top - lowest) / step) + 1);
	if (bins < 12)
		return notes;
	// Each bin of the scale: the loudest of the spectrum's bins it covers, or between two of them where it's
	// narrower than they are
	std::vector<double> scale(static_cast<size_t>(bins), 0.0), binLow(static_cast<size_t>(bins), 0.0),
		binHigh(static_cast<size_t>(bins), 0.0);
	for (int j = 0; j < bins; ++j) {
		const double midi = lowest + j * step;
		const double hz = 440.0 * std::pow(2.0, (midi - 69.0) / 12.0);
		const double a = hz * std::pow(2.0, -step / 24.0) / binHz, b = hz * std::pow(2.0, step / 24.0) / binHz;
		double value = 0.0;
		if (b - a >= 1.0) {
			for (size_t k = size_t(std::ceil(a)); k <= size_t(std::floor(b)) && k < power.size(); ++k)
				value = std::max(value, power[k]);
			binLow[size_t(j)] = std::ceil(a) * binHz;
			binHigh[size_t(j)] = std::floor(b) * binHz;
		}
		else {
			const double at = hz / binHz;
			const size_t k = size_t(at);
			const double f = at - double(k);
			if (k + 1 < power.size())
				value = (1.0 - f) * power[k] + f * power[k + 1];
			binLow[size_t(j)] = binHigh[size_t(j)] = hz;
		}
		scale[size_t(j)] = std::sqrt(value / count);
	}
	// What stands out of its surroundings (half an octave each way): noise and the pick's scrape lie under it
	std::vector<double> peaks(scale.size(), 0.0);
	for (size_t j = 0; j < scale.size(); ++j) {
		const size_t a = j >= 18 ? j - 18 : 0;
		peaks[j] = std::max(0.0, scale[j] - Mean(scale, a, j + 19));
	}

	// Each note's harmonics on the scale, softer the higher they are, each as the window shows a steady tone
	// (the Hann window's main lobe, as wide in Hz as the window is short), a little sharp up high as strings are
	const int first = options.lowestMidi, last = options.highestMidi;
	const int noteCount = last - first + 1;
	if (noteCount < 1)
		return notes;
	const double windowBin = double(rate) / double(length);	// Hz: the main lobe is 2 of these each way
	std::vector<std::vector<double> > shape(size_t(noteCount), std::vector<double>(scale.size(), 0.0));
	for (int k = 0; k < noteCount; ++k) {
		double norm = 0.0;
		const double f0 = 440.0 * std::pow(2.0, (first + k - 69.0) / 12.0);
		for (int h = 1; h <= 10; ++h) {
			const double weight = std::pow(0.7, h - 1);
			const double low = h * f0, high = h * f0 * std::sqrt(1.0 + 0.0004 * h * h);
			const double from = 12.0 * std::log2(std::max(1.0, low - 2.0 * windowBin) / 440.0) + 69.0;
			const double to = 12.0 * std::log2((high + 2.0 * windowBin) / 440.0) + 69.0;
			for (int j = std::max(0, int(std::floor((from - lowest) / step)) - 1); j <= int(std::ceil((to - lowest) / step)) + 1 && j < bins; ++j) {
				// How far (in the window's bins) the bin is from where the harmonic can be
				double gap = 0.0;
				if (binHigh[size_t(j)] < low)
					gap = (low - binHigh[size_t(j)]) / windowBin;
				else if (binLow[size_t(j)] > high)
					gap = (binLow[size_t(j)] - high) / windowBin;
				if (gap >= 2.0)
					continue;
				double v = 1.0;	// the Hann window's spectrum: sinc(d) / (1 - d^2), 0.5 at d = 1
				if (gap > 1e-6)
					v = std::fabs(gap - 1.0) < 1e-6 ? 0.5 : std::fabs(std::sin(PI * gap) / (PI * gap) / (1.0 - gap * gap));
				shape[size_t(k)][size_t(j)] = std::max(shape[size_t(k)][size_t(j)], weight * v);
			}
		}
		for (size_t j = 0; j < scale.size(); ++j)
			norm += shape[size_t(k)][j] * shape[size_t(k)][j];
		norm = std::sqrt(norm);
		if (norm > 0.0)
			for (size_t j = 0; j < scale.size(); ++j)
				shape[size_t(k)][j] /= norm;
	}

	// The fit: activations x >= 0 with the shapes' sum near the peaks (Lee and Seung's multiplicative update,
	// with a little push towards fewer notes)
	std::vector<double> wy(size_t(noteCount), 0.0);
	double strongest = 0.0;
	for (int k = 0; k < noteCount; ++k) {
		for (size_t j = 0; j < scale.size(); ++j)
			wy[size_t(k)] += shape[size_t(k)][j] * peaks[j];
		strongest = std::max(strongest, wy[size_t(k)]);
	}
	if (strongest <= 0.0)
		return notes;
	std::vector<std::vector<double> > ww(size_t(noteCount), std::vector<double>(size_t(noteCount), 0.0));
	for (int a = 0; a < noteCount; ++a)
		for (int b = a; b < noteCount; ++b) {
			double sum = 0.0;
			for (size_t j = 0; j < scale.size(); ++j)
				sum += shape[size_t(a)][j] * shape[size_t(b)][j];
			ww[size_t(a)][size_t(b)] = ww[size_t(b)][size_t(a)] = sum;
		}
	const double sparse = 0.02 * strongest;
	std::vector<double> x(size_t(noteCount), 0.0);
	for (int k = 0; k < noteCount; ++k)
		x[size_t(k)] = std::max(0.0, wy[size_t(k)]) / strongest + 1e-6;
	for (int iteration = 0; iteration < 300; ++iteration) {
		for (int k = 0; k < noteCount; ++k) {
			double wwx = 0.0;
			for (int m = 0; m < noteCount; ++m)
				wwx += ww[size_t(k)][size_t(m)] * x[size_t(m)];
			x[size_t(k)] *= std::max(0.0, wy[size_t(k)]) / (wwx + sparse + 1e-12);
		}
	}

	// The notes: the strongest activations, at most maxNotes of them
	double most = 0.0;
	for (int k = 0; k < noteCount; ++k)
		most = std::max(most, x[size_t(k)]);
	if (most <= 0.0)
		return notes;
	for (int k = 0; k < noteCount; ++k) {
		if (x[size_t(k)] < options.noteShare * most)
			continue;
		StrumNote note;
		note.midi = first + k;
		note.weight = x[size_t(k)] / most;
		notes.push_back(note);
	}
	std::sort(notes.begin(), notes.end(), [](const StrumNote &a, const StrumNote &b) { return a.weight > b.weight; });
	if (int(notes.size()) > options.maxNotes)
		notes.resize(size_t(std::max(0, options.maxNotes)));
	std::sort(notes.begin(), notes.end(), [](const StrumNote &a, const StrumNote &b) { return a.midi < b.midi; });
	return notes;
}

// The loudness (dBFS, RMS) of [from, to)
double LevelDb(const Audio &audio, double from, double to)
{
	const long long a = std::max(0LL, (long long)std::llround(from * audio.sampleRate));
	const long long b = std::min((long long)audio.samples.size(), (long long)std::llround(to * audio.sampleRate));
	double sum = 0.0;
	for (long long i = a; i < b; ++i)
		sum += double(audio.samples[size_t(i)]) * audio.samples[size_t(i)];
	const double rms = b > a ? std::sqrt(sum / double(b - a)) : 0.0;
	return rms > 1e-9 ? 20.0 * std::log10(rms) : -180.0;
}

}	// namespace

std::vector<Strum> HearStrums(const Audio &audio, const StrumOptions &options)
{
	std::vector<Strum> strums;
	const std::vector<double> onsets = StrumOnsets(audio, options);
	const double length = audio.Duration();
	for (size_t i = 0; i < onsets.size(); ++i) {
		Strum strum;
		strum.time = onsets[i];
		strum.end = i + 1 < onsets.size() ? onsets[i + 1] : length;
		strum.level = LevelDb(audio, strum.time, std::min(strum.end, strum.time + 0.15));
		// Just after the pick's scrape, until the next strum (at most 0.6 s on)
		double from = strum.time + 0.03, to = std::min(strum.end - 0.01, strum.time + 0.6);
		if (to - from < 0.06) {
			from = strum.time;
			to = std::min(strum.end, strum.time + 0.6);
		}
		strum.notes = NotesBetween(audio, from, to, options);
		if (!strum.notes.empty())
			strums.push_back(strum);
	}
	// A strum whose notes were all too faint to hear leaves its time to the one before
	for (size_t i = 0; i + 1 < strums.size(); ++i)
		strums[i].end = strums[i + 1].time;
	if (!strums.empty())
		strums.back().end = length;
	return strums;
}

}	// namespace transcribe

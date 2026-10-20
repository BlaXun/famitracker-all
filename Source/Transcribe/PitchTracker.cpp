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

// YIN fundamental-frequency estimator (de Cheveigne & Kawahara, JASA 2002).
// Monophonic: one pitch per frame, which suits whistling, humming and singing.

#include "Transcribe.h"

#include <algorithm>
#include <cmath>

namespace transcribe {

std::vector<PitchFrame> TrackPitch(const Audio &audio, const PitchOptions &options)
{
	std::vector<PitchFrame> frames;
	const int rate = audio.sampleRate;
	if (rate <= 0 || options.minFrequency <= 0 || options.maxFrequency <= options.minFrequency)
		return frames;

	const int tauMin = std::max(2, int(std::floor(rate / options.maxFrequency)));
	const int tauMax = std::max(tauMin + 2, int(std::ceil(rate / options.minFrequency)));
	const int window = std::max(tauMax, int(std::lround(options.windowSeconds * rate)));
	const int hop = std::max(1, int(std::lround(options.hopSeconds * rate)));
	const size_t span = size_t(window) + size_t(tauMax) + 1;

	const std::vector<float> &x = audio.samples;
	if (x.size() < span)
		return frames;

	std::vector<double> diff(tauMax + 1), cmnd(tauMax + 1);

	for (size_t start = 0; start + span <= x.size(); start += hop) {
		const float *s = &x[start];
		PitchFrame frame;
		frame.time = (double(start) + window / 2.0) / rate;

		double energy = 0.0;
		for (int j = 0; j < window; ++j)
			energy += double(s[j]) * s[j];
		frame.rms = std::sqrt(energy / window);

		if (frame.rms <= 1e-9) {	// digital silence
			frames.push_back(frame);
			continue;
		}

		// Step 2: difference function d(tau)
		diff[0] = 0.0;
		for (int tau = 1; tau <= tauMax; ++tau) {
			double sum = 0.0;
			for (int j = 0; j < window; ++j) {
				const double d = double(s[j]) - s[j + tau];
				sum += d * d;
			}
			diff[tau] = sum;
		}

		// Step 3: cumulative mean normalized difference d'(tau)
		cmnd[0] = 1.0;
		double running = 0.0;
		for (int tau = 1; tau <= tauMax; ++tau) {
			running += diff[tau];
			cmnd[tau] = running > 0.0 ? diff[tau] * tau / running : 1.0;
		}

		// Step 4: first dip below the threshold, followed down to its local minimum
		int best = -1;
		for (int tau = tauMin; tau <= tauMax; ++tau) {
			if (cmnd[tau] < options.threshold) {
				while (tau + 1 <= tauMax && cmnd[tau + 1] < cmnd[tau])
					++tau;
				best = tau;
				break;
			}
		}
		if (best < 0) {
			// No dip below the threshold: report the global minimum as a weak candidate.
			best = tauMin;
			for (int tau = tauMin + 1; tau <= tauMax; ++tau)
				if (cmnd[tau] < cmnd[best])
					best = tau;
		}

		// Step 5: parabolic interpolation around the minimum
		double refined = best;
		if (best > tauMin && best < tauMax) {
			const double a = cmnd[best - 1], b = cmnd[best], c = cmnd[best + 1];
			const double denominator = a - 2.0 * b + c;
			if (std::fabs(denominator) > 1e-12) {
				const double shift = 0.5 * (a - c) / denominator;
				if (shift > -1.0 && shift < 1.0)
					refined += shift;
			}
		}

		frame.aperiodicity = cmnd[best];
		frame.frequency = refined > 0.0 ? rate / refined : 0.0;
		frames.push_back(frame);
	}
	return frames;
}

double FrequencyToMidi(double frequency)
{
	return 69.0 + 12.0 * std::log2(frequency / 440.0);
}

}	// namespace transcribe

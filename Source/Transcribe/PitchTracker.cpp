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
#include <complex>

namespace transcribe {

namespace {

typedef std::complex<double> Complex;

// In-place iterative radix-2 FFT; size is a power of two. inverse = true
// computes the unscaled inverse transform.
class FFT {
public:
	explicit FFT(size_t size) : n_(size), twiddle_(size / 2), reversed_(size)
	{
		const double pi = 3.14159265358979323846;
		for (size_t k = 0; k < n_ / 2; ++k)
			twiddle_[k] = std::polar(1.0, -2.0 * pi * double(k) / double(n_));
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

	void Transform(std::vector<Complex> &data, bool inverse) const
	{
		for (size_t i = 0; i < n_; ++i)
			if (i < reversed_[i])
				std::swap(data[i], data[reversed_[i]]);
		for (size_t len = 2; len <= n_; len <<= 1) {
			const size_t half = len / 2, step = n_ / len;
			for (size_t start = 0; start < n_; start += len) {
				for (size_t k = 0; k < half; ++k) {
					const Complex w = inverse ? std::conj(twiddle_[k * step]) : twiddle_[k * step];
					const Complex t = w * data[start + k + half];
					data[start + k + half] = data[start + k] - t;
					data[start + k] += t;
				}
			}
		}
	}

private:
	size_t n_;
	std::vector<Complex> twiddle_;
	std::vector<size_t> reversed_;
};

}	// namespace

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

	// The difference function d(tau) = sum (x[j] - x[j+tau])^2 over the window
	// expands to e(0) + e(tau) - 2 r(tau): two energies kept as running sums, and
	// the cross-correlation r(tau), computed for all lags with one FFT pair.
	size_t fftSize = 1;
	while (fftSize < size_t(window) + size_t(tauMax) + 1)
		fftSize <<= 1;
	const FFT fft(fftSize);
	std::vector<Complex> spectrum(fftSize);

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

		// Step 2: difference function d(tau). Window a = s[0, window) goes in the
		// real part and the longer stretch b = s[0, window + tauMax] in the
		// imaginary part; both spectra come out of one transform.
		for (size_t j = 0; j < fftSize; ++j) {
			const double re = j < size_t(window) ? double(s[j]) : 0.0;
			const double im = j < span ? double(s[j]) : 0.0;
			spectrum[j] = Complex(re, im);
		}
		fft.Transform(spectrum, false);
		for (size_t k = 0; k <= fftSize / 2; ++k) {
			const size_t mirror = (fftSize - k) % fftSize;
			const Complex z = spectrum[k], zm = std::conj(spectrum[mirror]);
			const Complex a = 0.5 * (z + zm), b = Complex(0.0, -0.5) * (z - zm);
			const Complex product = std::conj(a) * b;	// cross-spectrum: r(tau) = sum a[j] b[j+tau]
			spectrum[k] = product;
			spectrum[mirror] = std::conj(product);
		}
		fft.Transform(spectrum, true);
		double energy0 = 0.0;
		for (int j = 0; j < window; ++j)
			energy0 += double(s[j]) * s[j];
		double energyTau = energy0;	// sum of s[j]^2 for j in [tau, tau + window)
		diff[0] = 0.0;
		for (int tau = 1; tau <= tauMax; ++tau) {
			energyTau += double(s[tau + window - 1]) * s[tau + window - 1] - double(s[tau - 1]) * s[tau - 1];
			const double r = spectrum[size_t(tau)].real() / double(fftSize);
			diff[tau] = std::max(0.0, energy0 + energyTau - 2.0 * r);
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

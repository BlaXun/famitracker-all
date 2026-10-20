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

// What a recording holds: whistling, a voice, or taps.

#include "Transcribe.h"

#include <algorithm>
#include <cmath>

namespace transcribe {

namespace {

// Share of a pitched frame's energy that sits in overtones 2-5 instead of the
// fundamental, from Goertzel filters over a Hann-windowed stretch around the
// frame. A whistle is close to a pure tone (near 0); a voice is rich in
// overtones (0.3 and up). Returns -1 when the stretch doesn't fit.
double OvertoneShare(const Audio &audio, const PitchFrame &frame)
{
	const double pi = 3.14159265358979323846;
	const int rate = audio.sampleRate;
	const int length = std::max(int(0.03 * rate), int(4.0 * rate / frame.frequency));	// 30 ms, 4 periods at least
	const long start = long(frame.time * rate) - length / 2;
	if (start < 0 || size_t(start + length) > audio.samples.size())
		return -1.0;
	double total = 0.0, overtones = 0.0;
	for (int harmonic = 1; harmonic <= 5; ++harmonic) {
		const double f = frame.frequency * harmonic;
		if (f >= rate / 2.0)
			break;
		const double coefficient = 2.0 * std::cos(2.0 * pi * f / rate);
		double s1 = 0.0, s2 = 0.0;
		for (int i = 0; i < length; ++i) {
			const double window = 0.5 - 0.5 * std::cos(2.0 * pi * i / (length - 1));
			const double s0 = window * audio.samples[size_t(start + i)] + coefficient * s1 - s2;
			s2 = s1;
			s1 = s0;
		}
		const double power = s1 * s1 + s2 * s2 - coefficient * s1 * s2;
		total += power;
		if (harmonic > 1)
			overtones += power;
	}
	return total > 0.0 ? overtones / total : -1.0;
}

double Median(std::vector<double> values)
{
	if (values.empty())
		return 0.0;
	const size_t mid = values.size() / 2;
	std::nth_element(values.begin(), values.begin() + mid, values.end());
	return values[mid];
}

}	// namespace

PitchOptions PitchOptionsFor(Source source)
{
	PitchOptions options;
	switch (source) {
	case Source::Whistle:
		options.minFrequency = 300.0;	// a low whistle; nothing below can be one
		options.maxFrequency = 4200.0;
		break;
	case Source::Voice:
		options.minFrequency = 70.0;	// a low hum
		options.maxFrequency = 1100.0;	// a high sung note; above that it is overtones
		break;
	case Source::Taps:
		break;
	}
	return options;
}

const char *SourceName(Source source)
{
	switch (source) {
	case Source::Whistle: return "whistling";
	case Source::Voice: return "humming or singing";
	case Source::Taps: return "taps or beatboxing";
	}
	return "";
}

SourceGuess GuessSource(const Audio &audio, const std::vector<PitchFrame> &frames, const NoteOptions &options)
{
	SourceGuess guess;
	double loudest = 0.0;
	for (size_t i = 0; i < frames.size(); ++i)
		loudest = std::max(loudest, frames[i].rms);
	const double gate = std::max(loudest * std::pow(10.0, options.silenceDb / 20.0), std::pow(10.0, options.floorDb / 20.0));

	size_t sounding = 0;
	std::vector<double> pitches, overtones;
	for (size_t i = 0; i < frames.size(); ++i) {
		if (frames[i].rms < gate)
			continue;
		++sounding;
		if (frames[i].frequency > 0.0 && frames[i].aperiodicity <= options.maxAperiodicity) {
			pitches.push_back(frames[i].frequency);
			if (pitches.size() % 4 == 1) {	// every 20 ms is plenty
				const double share = OvertoneShare(audio, frames[i]);
				if (share >= 0.0)
					overtones.push_back(share);
			}
		}
	}
	if (sounding == 0)
		return guess;	// nothing to hear; whistling is as good a guess as any

	guess.pitchedShare = double(pitches.size()) / double(sounding);
	guess.medianFrequency = Median(pitches);
	guess.overtoneShare = Median(overtones);

	// Taps and beatboxing are mostly noise; whistling and humming are pitched
	// nearly all the time they sound. A whistle is an almost pure tone, a voice
	// has strong overtones. Very high notes count as whistling either way.
	if (guess.pitchedShare < 0.5)
		guess.source = Source::Taps;
	else if (guess.medianFrequency >= 900.0 || (guess.overtoneShare < 0.1 && guess.medianFrequency >= 300.0))
		guess.source = Source::Whistle;
	else
		guess.source = Source::Voice;
	return guess;
}

SourceGuess GuessSource(const Audio &audio)
{
	return GuessSource(audio, TrackPitch(audio, PitchOptions()), NoteOptions());
}

}	// namespace transcribe

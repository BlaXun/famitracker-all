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

// Expression: how loud each note was, slides into notes and vibrato, and
// the pattern cells (volume column, Qxy/Rxy, 4xy) that play them back.

#include "Transcribe.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace transcribe {

namespace {

// FamiTracker's vibrato depths for 4xy, as CSoundGen::NEW_VIBRATO_DEPTH:
// the peak period offset for y = 0..15
const double VIBRATO_DEPTH[16] = {
	1.0, 1.5, 2.5, 4.0, 5.0, 7.0, 10.0, 12.0, 14.0, 17.0, 22.0, 30.0, 44.0, 64.0, 96.0, 128.0
};

const double CENTS_PER_RELATIVE_PERIOD = 1731.234;	// 1200 / ln 2: cents for a small period change dP/P

// A slide into a note: lead-in pieces up to this long, gliding at least this
// fast towards a note at least this long
const double MAX_LEAD_IN_SECONDS = 0.25;
const double MIN_GLIDE_SEMITONES_PER_SECOND = 8.0;
const double MIN_TARGET_SECONDS = 0.08;

double MedianOf(std::vector<double> values)
{
	if (values.empty())
		return 0.0;
	const size_t mid = values.size() / 2;
	std::nth_element(values.begin(), values.begin() + mid, values.end());
	return values[mid];
}

double Level(double rms)
{
	return rms > 1e-9 ? 20.0 * std::log10(rms) : -120.0;
}

int Clamp(int value, int low, int high)
{
	return std::max(low, std::min(high, value));
}

// How fast a note's pitch moves, in semitones per second (least-squares slope
// over its pitched frames, 10 ms in from either end); 0 when too few frames
double Glide(const Note &note, const std::vector<PitchFrame> &frames)
{
	double n = 0.0, st = 0.0, sp = 0.0, stt = 0.0, stp = 0.0;
	for (size_t i = 0; i < frames.size(); ++i) {
		const PitchFrame &f = frames[i];
		if (f.time < note.start + 0.01 || f.time > note.end - 0.01 || f.frequency <= 0.0 || f.aperiodicity > 0.35)
			continue;
		const double t = f.time - note.start, p = FrequencyToMidi(f.frequency);
		n += 1.0;
		st += t;
		sp += p;
		stt += t * t;
		stp += t * p;
	}
	const double denominator = n * stt - st * st;
	if (n < 3.0 || denominator <= 0.0)
		return 0.0;
	return (n * stp - st * sp) / denominator;
}

// Period register value for a MIDI note on an NTSC chip
double Period(int midi, ChannelVoice voice)
{
	const double cpu = 1789773.0;
	const double divider = voice == ChannelVoice::Triangle ? 32.0 : (voice == ChannelVoice::Sawtooth ? 14.0 : 16.0);
	return cpu / (divider * 440.0 * std::pow(2.0, (midi - 69) / 12.0)) - 1.0;
}

// Performed vibrato: a 4-8 Hz wobble in the pitch of a held note.
// Returns false when there is none.
bool FindVibrato(const Note &note, const std::vector<PitchFrame> &frames, double hop, double &hz, double &cents)
{
	if (note.end - note.start < 0.35)
		return false;
	std::vector<double> pitch;
	for (size_t i = 0; i < frames.size(); ++i) {
		const PitchFrame &f = frames[i];
		if (f.time < note.start + 0.1 || f.time > note.end - 0.05)
			continue;
		if (f.frequency <= 0.0 || f.aperiodicity > 0.35)
			return false;	// a gap: not a steady held note
		pitch.push_back(1200.0 * std::log2(f.frequency / 440.0));
	}
	// Take out the slow drift with a moving average of about a third of a second
	const int half = std::max(1, int(std::lround(0.165 / hop)));
	if (int(pitch.size()) < 2 * half + int(std::lround(0.25 / hop)))
		return false;
	std::vector<double> wobble;
	for (int i = half; i + half < int(pitch.size()); ++i) {
		double sum = 0.0;
		for (int k = i - half; k <= i + half; ++k)
			sum += pitch[size_t(k)];
		wobble.push_back(pitch[size_t(i)] - sum / (2 * half + 1));
	}
	double mean = 0.0;
	for (size_t i = 0; i < wobble.size(); ++i)
		mean += wobble[i];
	mean /= double(wobble.size());
	double energy = 0.0;
	for (size_t i = 0; i < wobble.size(); ++i) {
		wobble[i] -= mean;
		energy += wobble[i] * wobble[i];
	}
	if (energy <= 0.0)
		return false;

	// Strongest repetition between 8 Hz and 4 Hz
	const int shortest = std::max(1, int(std::lround(0.125 / hop))), longest = int(std::lround(0.25 / hop));
	double bestScore = 0.0;
	int bestLag = 0;
	for (int lag = shortest; lag <= longest && lag < int(wobble.size()); ++lag) {
		double sum = 0.0;
		for (size_t i = 0; i + size_t(lag) < wobble.size(); ++i)
			sum += wobble[i] * wobble[i + size_t(lag)];
		const double score = sum / energy;
		if (score > bestScore) {
			bestScore = score;
			bestLag = lag;
		}
	}
	const double depth = std::sqrt(2.0 * energy / double(wobble.size()));	// sine amplitude from its RMS
	if (bestLag == 0 || bestScore < 0.4 || depth < 12.0)
		return false;
	hz = 1.0 / (bestLag * hop);
	cents = std::min(depth, 100.0);
	return true;
}

}	// namespace

void DescribeExpression(std::vector<Note> &notes, const std::vector<PitchFrame> &frames, const ExpressionOptions &options)
{
	if (notes.empty() || frames.size() < 2)
		return;
	const double hop = frames[1].time - frames[0].time;

	// 1. Slides: short pieces whose pitch glides straight into the next note
	//    (a scoop up to a whistled note, say) become a slide into that note. A
	//    short note that holds its pitch stays a note: that's a melody, not a slide,
	//    and so is a sung syllable of its own.
	if (!options.slides) {
		for (size_t m = 0; m < notes.size(); ++m) {
			notes[m].slideSemitones = 0;
			notes[m].slideSeconds = 0.0;
		}
	}
	else {
		std::vector<bool> folded(notes.size(), false);
		for (size_t m = 0; m < notes.size(); ++m) {
			Note &target = notes[m];
			const double length = target.end - target.start;
			if (length < MIN_TARGET_SECONDS)
				continue;
			size_t first = m;
			double side = 0.0;
			while (first > 0 && !folded[first - 1] && !notes[first].articulated) {
				const Note &lead = notes[first - 1];
				const double leadLength = lead.end - lead.start;
				const double offset = lead.pitch - target.pitch;
				if (leadLength > MAX_LEAD_IN_SECONDS || leadLength * 0.8 > length)
					break;
				if (notes[first].start - lead.end > 0.02)
					break;	// not touching
				if (std::fabs(offset) < 0.8 || std::fabs(offset) > 12.5)
					break;
				if (side != 0.0 && (offset < 0.0) != (side < 0.0))
					break;	// crosses the target
				if (first < m && std::fabs(offset) < std::fabs(notes[first].pitch - target.pitch))
					break;	// moving away from the target
				const double glide = Glide(lead, frames);	// must move towards the target
				if (offset < 0.0 ? glide < MIN_GLIDE_SEMITONES_PER_SECOND : glide > -MIN_GLIDE_SEMITONES_PER_SECOND)
					break;
				side = offset;
				--first;
			}
			if (first < m) {
				const Note &lead = notes[first];
				target.slideSemitones = int(std::lround(lead.pitch - target.pitch));
				target.slideSeconds = target.start + target.slideSeconds - lead.start;	// to where it holds its pitch
				target.start = lead.start;
				target.articulated = lead.articulated;
				for (size_t k = first; k < m; ++k)
					folded[k] = true;
			}
		}
		std::vector<Note> kept;
		for (size_t i = 0; i < notes.size(); ++i)
			if (!folded[i])
				kept.push_back(notes[i]);
		notes.swap(kept);
	}

	// 2. How loud each note is: the median level of its frames
	std::vector<double> levels;
	for (size_t n = 0; n < notes.size(); ++n) {
		std::vector<double> values;
		for (size_t i = 0; i < frames.size(); ++i)
			if (frames[i].time >= notes[n].start && frames[i].time <= notes[n].end)
				values.push_back(Level(frames[i].rms));
		notes[n].level = MedianOf(values);
		levels.push_back(notes[n].level);
	}

	// 3. Volume column: the loud notes (90th percentile and up) play at F, every
	//    3 dB quieter one step lower, but no lower than 6 so every note is heard
	std::vector<double> sorted(levels);
	std::sort(sorted.begin(), sorted.end());
	const double reference = sorted[std::min(sorted.size() - 1, size_t(0.9 * double(sorted.size())))];
	for (size_t n = 0; n < notes.size(); ++n) {
		notes[n].volume = options.dynamics ? Clamp(15 + int(std::lround((notes[n].level - reference) / 3.0)), 6, 15) : -1;
	}

	// 4. Vibrato: kept where it was performed, a gentle delayed one on other long notes
	for (size_t n = 0; n < notes.size(); ++n) {
		Note &note = notes[n];
		note.vibratoHz = note.vibratoCents = note.vibratoDelay = 0.0;
		double hz = 0.0, cents = 0.0;
		if (options.vibrato && FindVibrato(note, frames, hop, hz, cents)) {
			note.vibratoHz = hz;
			note.vibratoCents = cents;
			note.vibratoDelay = 0.15;
		}
		else if (options.gentleVibrato && note.end - note.start >= options.gentleVibratoSeconds) {
			note.vibratoHz = 5.6;
			note.vibratoCents = 12.0;
			note.vibratoDelay = 0.25;
		}
	}
}

std::vector<MelodyCell> MelodyCells(const std::vector<GridNote> &notes, ChannelVoice voice, int frameRate)
{
	std::vector<MelodyCell> cells;
	const double rate = frameRate > 0 ? double(frameRate) : 60.0;
	for (size_t i = 0; i < notes.size(); ++i) {
		const GridNote &n = notes[i];
		const int end = n.row + n.length;

		MelodyCell note;
		note.kind = MelodyCell::Note;
		note.row = n.row;
		note.midi = n.midi;
		note.volume = n.volume;
		const int steps = std::min(15, std::abs(n.slideSemitones));
		if (steps > 0) {
			// Start off the note and slide onto it: Qxy up, Rxy down, y semitones at
			// 2x+1 period units a frame
			note.midi = n.slideSemitones < 0 ? n.midi - steps : n.midi + steps;
			const double frames = std::max(1.0, n.slideSeconds * rate);
			const double distance = std::fabs(Period(note.midi, voice) - Period(n.midi, voice));
			const int speed = Clamp(int(std::lround((distance / frames - 1.0) / 2.0)), 0, 15);
			note.effect = n.slideSemitones < 0 ? 'Q' : 'R';
			note.effectParam = (speed << 4) | steps;
		}
		cells.push_back(note);

		if (n.vibratoHz > 0.0 && n.vibratoRow >= 1 && n.vibratoRow + 1 < n.length) {
			// 4xy where the vibrato begins: x sets the rate (64 steps a cycle, one a
			// frame per unit), y the depth as a period offset; 400 on the last row
			const int speed = Clamp(int(std::lround(n.vibratoHz * 64.0 / rate)), 1, 15);
			const double depth = n.vibratoCents * Period(n.midi, voice) / CENTS_PER_RELATIVE_PERIOD;
			int y = 1;
			for (int k = 2; k < 16; ++k)
				if (std::fabs(VIBRATO_DEPTH[k] - depth) < std::fabs(VIBRATO_DEPTH[y] - depth))
					y = k;
			MelodyCell on;
			on.kind = MelodyCell::Effect;
			on.row = n.row + n.vibratoRow;
			on.effect = '4';
			on.effectParam = (speed << 4) | y;
			cells.push_back(on);
			MelodyCell off;
			off.kind = MelodyCell::Effect;
			off.row = end - 1;
			off.effect = '4';
			off.effectParam = 0;
			cells.push_back(off);
		}

		if (!(i + 1 < notes.size() && notes[i + 1].row == end)) {
			MelodyCell release;
			release.kind = MelodyCell::Release;
			release.row = end;
			cells.push_back(release);
		}
	}
	return cells;
}

}	// namespace transcribe

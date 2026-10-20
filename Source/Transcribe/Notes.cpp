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

// Pitch frames -> notes -> FamiTracker rows.

#include "Transcribe.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace transcribe {

namespace {

double Median(std::vector<double> values)
{
	if (values.empty())
		return 0.0;
	const size_t mid = values.size() / 2;
	std::nth_element(values.begin(), values.begin() + mid, values.end());
	double m = values[mid];
	if (values.size() % 2 == 0) {
		std::nth_element(values.begin(), values.begin() + mid - 1, values.end());
		m = 0.5 * (m + values[mid - 1]);
	}
	return m;
}

const int SCALE_MAJOR[] = { 0, 2, 4, 5, 7, 9, 11 };
const int SCALE_MINOR[] = { 0, 2, 3, 5, 7, 8, 10 };
const int SCALE_MAJOR_PENTA[] = { 0, 2, 4, 7, 9 };
const int SCALE_MINOR_PENTA[] = { 0, 3, 5, 7, 10 };

bool InScale(int midi, int root, Scale scale)
{
	const int degree = ((midi - root) % 12 + 12) % 12;
	const int *begin = nullptr, *end = nullptr;
	switch (scale) {
	case Scale::Chromatic: return true;
	case Scale::Major: begin = SCALE_MAJOR; end = SCALE_MAJOR + 7; break;
	case Scale::Minor: begin = SCALE_MINOR; end = SCALE_MINOR + 7; break;
	case Scale::MajorPentatonic: begin = SCALE_MAJOR_PENTA; end = SCALE_MAJOR_PENTA + 5; break;
	case Scale::MinorPentatonic: begin = SCALE_MINOR_PENTA; end = SCALE_MINOR_PENTA + 5; break;
	}
	return std::find(begin, end, degree) != end;
}

// Nearest in-scale note to a (possibly fractional) pitch.
int SnapPitch(double pitch, int root, Scale scale)
{
	const int base = int(std::lround(pitch));
	int best = base;
	double bestDistance = 1e9;
	for (int candidate = base - 6; candidate <= base + 6; ++candidate) {
		if (!InScale(candidate, root, scale))
			continue;
		const double distance = std::fabs(candidate - pitch);
		if (distance < bestDistance - 1e-9) {	// ties keep the lower note
			bestDistance = distance;
			best = candidate;
		}
	}
	return best;
}

std::string Lower(const std::string &s)
{
	std::string r(s);
	for (size_t i = 0; i < r.size(); ++i)
		r[i] = char(std::tolower(static_cast<unsigned char>(r[i])));
	return r;
}

}	// namespace

// ---------------------------------------------------------------------------
// Segmentation
// ---------------------------------------------------------------------------

std::vector<Note> SegmentNotes(const std::vector<PitchFrame> &frames, const NoteOptions &options)
{
	std::vector<Note> notes;
	const size_t n = frames.size();
	if (n < 2)
		return notes;

	const double hop = frames[1].time - frames[0].time;
	if (hop <= 0.0)
		return notes;

	// 1. Which frames carry a pitched sound
	double loudest = 0.0;
	for (size_t i = 0; i < n; ++i)
		loudest = std::max(loudest, frames[i].rms);
	const double gate = std::max(loudest * std::pow(10.0, options.silenceDb / 20.0), std::pow(10.0, options.floorDb / 20.0));

	std::vector<bool> voiced(n, false);
	std::vector<double> pitch(n, 0.0);
	for (size_t i = 0; i < n; ++i) {
		const PitchFrame &f = frames[i];
		if (f.frequency > 0.0 && f.rms >= gate && f.aperiodicity <= options.maxAperiodicity) {
			voiced[i] = true;
			pitch[i] = FrequencyToMidi(f.frequency);
		}
	}

	// 2. Bridge short dropouts inside a sound, so a long hum with a wobble stays one note
	const size_t maxGap = size_t(std::floor(options.maxGapSeconds / hop + 1e-9));
	for (size_t i = 1; i < n; ++i) {
		if (voiced[i] || !voiced[i - 1])
			continue;
		size_t j = i;
		while (j < n && !voiced[j])
			++j;
		if (j < n && j - i <= maxGap && std::fabs(pitch[j] - pitch[i - 1]) < 1.0) {
			for (size_t k = i; k < j; ++k) {
				voiced[k] = true;
				pitch[k] = pitch[i - 1] + (pitch[j] - pitch[i - 1]) * double(k - i + 1) / double(j - i + 1);
			}
		}
		i = j;
	}

	// 3. Light median smoothing within voiced runs (removes single-frame octave slips)
	std::vector<double> smooth(pitch);
	for (size_t i = 0; i < n; ++i) {
		if (!voiced[i])
			continue;
		std::vector<double> window;
		for (size_t k = (i >= 2 ? i - 2 : 0); k <= std::min(n - 1, i + 2); ++k)
			if (voiced[k])
				window.push_back(pitch[k]);
		smooth[i] = Median(window);
	}

	// 4. Split voiced runs where the pitch moves to a new note and stays there
	const size_t changeFrames = std::max<size_t>(1, size_t(std::lround(options.changeSeconds / hop)));
	const size_t referenceFrames = std::max<size_t>(4, size_t(std::lround(0.2 / hop)));

	auto emit = [&](size_t first, size_t last) {
		std::vector<double> values(smooth.begin() + first, smooth.begin() + last + 1);
		Note note;
		note.start = frames[first].time - hop / 2.0;
		note.end = frames[last].time + hop / 2.0;
		note.pitch = Median(values);
		note.midi = int(std::lround(note.pitch));
		if (note.end - note.start + 1e-9 >= options.minNoteSeconds)
			notes.push_back(note);
	};

	size_t i = 0;
	while (i < n) {
		if (!voiced[i]) {
			++i;
			continue;
		}
		size_t segmentStart = i;
		size_t runEnd = i;
		while (runEnd + 1 < n && voiced[runEnd + 1])
			++runEnd;

		size_t k = i + 1;
		while (k <= runEnd) {
			// Reference pitch: median of the segment's recent frames
			const size_t from = (k - segmentStart > referenceFrames) ? k - referenceFrames : segmentStart;
			const double reference = Median(std::vector<double>(smooth.begin() + from, smooth.begin() + k));
			if (std::fabs(smooth[k] - reference) <= options.changeSemitones) {
				++k;
				continue;
			}
			// Does the new pitch hold for long enough?
			size_t hold = k;
			while (hold <= runEnd && std::fabs(smooth[hold] - smooth[k]) <= options.changeSemitones &&
				std::fabs(smooth[hold] - reference) > options.changeSemitones)
				++hold;
			if (hold - k >= changeFrames) {
				emit(segmentStart, k - 1);
				segmentStart = k;
			}
			k = std::max(hold, k + 1);
		}
		emit(segmentStart, runEnd);
		i = runEnd + 1;
	}

	// 5. Join touching notes that ended up on the same semitone
	std::vector<Note> joined;
	for (size_t j = 0; j < notes.size(); ++j) {
		if (!joined.empty() && joined.back().midi == notes[j].midi && notes[j].start - joined.back().end < hop * 1.5) {
			Note &prev = joined.back();
			const double a = prev.end - prev.start, b = notes[j].end - notes[j].start;
			prev.pitch = (prev.pitch * a + notes[j].pitch * b) / (a + b);
			prev.end = notes[j].end;
		}
		else {
			joined.push_back(notes[j]);
		}
	}
	return joined;
}

// ---------------------------------------------------------------------------
// Musical clean-up
// ---------------------------------------------------------------------------

int OctaveShiftFor(const std::vector<Note> &notes, int targetMidi)
{
	if (notes.empty())
		return 0;
	std::vector<double> pitches;
	for (size_t i = 0; i < notes.size(); ++i)
		pitches.push_back(notes[i].pitch);
	const double median = Median(pitches);
	const int octaves = int(std::lround((targetMidi - median) / 12.0));
	return octaves * 12;
}

void Transpose(std::vector<Note> &notes, int semitones)
{
	for (size_t i = 0; i < notes.size(); ++i) {
		notes[i].pitch += semitones;
		notes[i].midi += semitones;
	}
}

bool ParseScale(const std::string &name, Scale &scale)
{
	const std::string s = Lower(name);
	if (s == "chromatic" || s == "none") scale = Scale::Chromatic;
	else if (s == "major") scale = Scale::Major;
	else if (s == "minor") scale = Scale::Minor;
	else if (s == "pentatonic" || s == "major-pentatonic") scale = Scale::MajorPentatonic;
	else if (s == "minor-pentatonic") scale = Scale::MinorPentatonic;
	else return false;
	return true;
}

bool ParseKey(const std::string &name, int &root)
{
	if (name.empty() || name.size() > 2)
		return false;
	static const int NATURAL[7] = { 9, 11, 0, 2, 4, 5, 7 };	// A B C D E F G
	const char letter = char(std::toupper(static_cast<unsigned char>(name[0])));
	if (letter < 'A' || letter > 'G')
		return false;
	int value = NATURAL[letter - 'A'];
	if (name.size() == 2) {
		if (name[1] == '#') ++value;
		else if (name[1] == 'b') --value;
		else return false;
	}
	root = (value + 12) % 12;
	return true;
}

int SnapToScale(int midi, int root, Scale scale)
{
	return SnapPitch(midi, root, scale);
}

void SnapToScale(std::vector<Note> &notes, int root, Scale scale)
{
	for (size_t i = 0; i < notes.size(); ++i)
		notes[i].midi = SnapPitch(notes[i].pitch, root, scale);
}

// ---------------------------------------------------------------------------
// Grid
// ---------------------------------------------------------------------------

double Grid::RowsPerSecond() const
{
	if (tempo <= 0 || speed <= 0 || frameRate <= 0)
		return 0.0;
	const int decrement = (tempo * 24) / speed;
	const int remainder = (tempo * 24) % speed;
	return double(frameRate) * decrement / (60.0 * frameRate - remainder);
}

std::vector<GridNote> Quantize(const std::vector<Note> &input, const Grid &grid, bool alignToStart)
{
	std::vector<GridNote> result;
	const double rate = grid.RowsPerSecond();
	if (rate <= 0.0 || input.empty())
		return result;

	std::vector<Note> notes(input);
	std::stable_sort(notes.begin(), notes.end(), [](const Note &a, const Note &b) { return a.start < b.start; });

	const int offset = alignToStart ? int(std::lround(notes.front().start * rate)) : 0;
	for (size_t i = 0; i < notes.size(); ++i) {
		const int startRow = int(std::lround(notes[i].start * rate)) - offset;
		int endRow = int(std::lround(notes[i].end * rate)) - offset;
		if (endRow <= startRow)
			endRow = startRow + 1;
		GridNote g;
		g.row = std::max(0, startRow);
		g.length = std::max(1, endRow - g.row);
		g.midi = notes[i].midi;

		if (!result.empty()) {
			GridNote &prev = result.back();
			if (g.row <= prev.row) {
				// Two notes on one row: keep the longer one
				if (g.length > prev.length)
					prev = g;
				continue;
			}
			if (g.row < prev.row + prev.length)
				prev.length = g.row - prev.row;	// one note at a time per channel
		}
		result.push_back(g);
	}
	return result;
}

std::string FamiTrackerNoteName(int midi)
{
	static const char *NAMES[12] = { "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-" };
	int index = midi - 24;	// FamiTracker's C-0 is 32.7 Hz (MIDI 24), so A-3 is 440 Hz
	index = std::max(0, std::min(95, index));
	std::string name(NAMES[index % 12]);
	name += char('0' + index / 12);
	return name;
}

}	// namespace transcribe

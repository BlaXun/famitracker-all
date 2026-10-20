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

	struct Segment {
		size_t first, last;	// frame indices, inclusive
		double pitch;		// median pitch
		int midi;			// rounded pitch
	};
	std::vector<Segment> segments;

	auto emit = [&](size_t first, size_t last) {
		if (double(last - first + 1) * hop + 1e-9 < options.minNoteSeconds)
			return;
		std::vector<double> values(smooth.begin() + first, smooth.begin() + last + 1);
		Segment s;
		s.first = first;
		s.last = last;
		s.pitch = Median(values);
		s.midi = int(std::lround(s.pitch));
		segments.push_back(s);
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
	std::vector<Segment> joined;
	for (size_t j = 0; j < segments.size(); ++j) {
		const Segment &s = segments[j];
		if (!joined.empty() && joined.back().midi == s.midi && s.first <= joined.back().last + 2) {
			Segment &prev = joined.back();
			const double a = double(prev.last - prev.first + 1), b = double(s.last - s.first + 1);
			prev.pitch = (prev.pitch * a + s.pitch * b) / (a + b);
			prev.last = s.last;
		}
		else {
			joined.push_back(s);
		}
	}

	// 6. Give each note the quieter start and end of its tone. The silence gate above is
	//    set by the loudest moment of the whole recording, so on its own it cuts the
	//    fade-in and the decaying tail of every quieter note, and the note sounds cut short.
	//    Frames next to a note still belong to it while they are pitched, within a
	//    semitone of the note's edge, above the noise floor and not far below the note's
	//    own peak. A tail never runs into the next note.
	if (!joined.empty()) {
		std::vector<double> level(n);
		for (size_t f = 0; f < n; ++f)
			level[f] = frames[f].rms > 1e-9 ? 20.0 * std::log10(frames[f].rms) : -120.0;
		std::vector<double> sorted(level);
		const size_t tenth = sorted.size() / 10;
		std::nth_element(sorted.begin(), sorted.begin() + tenth, sorted.end());
		const double noiseFloor = sorted[tenth];

		auto midiAt = [&](size_t k) { return FrequencyToMidi(frames[k].frequency); };
		const size_t edgeFrames = std::max<size_t>(1, size_t(std::lround(0.05 / hop)));

		for (size_t s = 0; s < joined.size(); ++s) {
			Segment &seg = joined[s];
			const size_t coreFirst = seg.first, coreLast = seg.last;
			double peak = -120.0;
			for (size_t k = coreFirst; k <= coreLast; ++k)
				peak = std::max(peak, level[k]);
			const double edgeGate = std::max(noiseFloor + options.edgeNoiseMarginDb, peak + options.edgeDb);
			auto belongs = [&](size_t k, double reference) {
				return frames[k].frequency > 0.0 && frames[k].aperiodicity <= options.edgeAperiodicity &&
					level[k] >= edgeGate && std::fabs(midiAt(k) - reference) < 1.0;
			};

			// Backwards, up to where the previous note (already extended) ends
			const size_t lowest = s > 0 ? joined[s - 1].last + 1 : 0;
			double reference = Median(std::vector<double>(smooth.begin() + coreFirst,
				smooth.begin() + std::min(coreLast, coreFirst + edgeFrames - 1) + 1));
			size_t k = coreFirst;
			while (k > lowest && belongs(k - 1, reference)) {
				--k;
				reference = 0.8 * reference + 0.2 * midiAt(k);	// follow a slow drift
			}
			seg.first = k;

			// Forwards, up to where the next note starts
			const size_t highest = s + 1 < joined.size() ? joined[s + 1].first - 1 : n - 1;
			reference = Median(std::vector<double>(smooth.begin() + (coreLast + 1 > coreFirst + edgeFrames ? coreLast + 1 - edgeFrames : coreFirst),
				smooth.begin() + coreLast + 1));
			k = coreLast;
			while (k < highest && belongs(k + 1, reference)) {
				++k;
				reference = 0.8 * reference + 0.2 * midiAt(k);
			}
			seg.last = k;
		}
	}

	for (size_t j = 0; j < joined.size(); ++j) {
		Note note;
		note.start = frames[joined[j].first].time - hop / 2.0;
		note.end = frames[joined[j].last].time + hop / 2.0;
		note.pitch = joined[j].pitch;
		note.midi = joined[j].midi;
		notes.push_back(note);
	}
	return notes;
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

namespace {

GridNote MakeGridNote(const Note &n, double rate)
{
	GridNote g;
	g.volume = n.volume;
	g.slideSemitones = n.slideSemitones;
	g.slideSeconds = n.slideSeconds;
	g.vibratoHz = n.vibratoHz;
	g.vibratoCents = n.vibratoCents;
	g.vibratoRow = n.vibratoHz > 0.0 ? std::max(1, int(std::lround(n.vibratoDelay * rate))) : 0;
	return g;
}

}	// namespace

std::vector<GridNote> Quantize(const std::vector<Note> &input, const Grid &grid, const QuantizeOptions &options)
{
	std::vector<GridNote> result;
	const double rate = grid.RowsPerSecond();
	if (rate <= 0.0 || input.empty())
		return result;

	std::vector<Note> notes(input);
	std::stable_sort(notes.begin(), notes.end(), [](const Note &a, const Note &b) { return a.start < b.start; });

	// Touching notes always join up, whatever minRestSeconds says
	const double minRest = std::max(options.minRestSeconds, 1e-6);
	const int offset = options.alignToStart ? int(std::lround(notes.front().start * rate)) : 0;

	// The note waiting for its length, which depends on when the next one starts
	struct Pending {
		int row;
		int midi;
		double start, end;	// seconds, spanning every note merged into it
		double longest;		// duration of the note that chose the pitch
		Note source;		// that note, for its expression
	};
	auto length = [&](const Pending &p) { return std::max(1, int(std::lround((p.end - p.start) * rate))); };

	Pending pending = Pending();
	bool havePending = false;
	for (size_t i = 0; i < notes.size(); ++i) {
		const Note &n = notes[i];
		const int row = std::max(0, int(std::lround(n.start * rate)) - offset);
		if (havePending) {
			if (row <= pending.row) {
				// Two notes on one row (say, a quick slide into a note): they become one
				// note spanning both, at the pitch of the longer one.
				if (n.end - n.start > pending.longest) {
					pending.midi = n.midi;
					pending.longest = n.end - n.start;
					pending.source = n;
				}
				pending.end = std::max(pending.end, n.end);
				continue;
			}
			GridNote g = MakeGridNote(pending.source, rate);
			g.row = pending.row;
			g.midi = pending.midi;
			g.length = length(pending);
			// A short break is held through; an overlap is cut (one note at a time)
			if (n.start - pending.end < minRest || g.row + g.length > row)
				g.length = row - g.row;
			result.push_back(g);
		}
		pending.row = row;
		pending.midi = n.midi;
		pending.start = n.start;
		pending.end = n.end;
		pending.longest = n.end - n.start;
		pending.source = n;
		havePending = true;
	}
	GridNote last = MakeGridNote(pending.source, rate);
	last.row = pending.row;
	last.midi = pending.midi;
	last.length = length(pending);
	result.push_back(last);
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

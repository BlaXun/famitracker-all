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

// Backing for a melody: its key, a chord for each bar or half bar, a bass
// line and the chords as arpeggios.

#include "Transcribe.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace transcribe {

namespace {

// Krumhansl & Kessler's key profiles: how well each scale degree fits a key
const double MAJOR_PROFILE[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
const double MINOR_PROFILE[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };

const char *const SHARP_NAMES[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
const char *const FLAT_NAMES[12] = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };

int PitchClass(int midi)
{
	return ((midi % 12) + 12) % 12;
}

// Keys written with flats: F, Bb, Eb, Ab and Db major, and their relative minors
bool UsesFlats(const Key &key)
{
	const int major = key.minor ? (key.tonic + 3) % 12 : key.tonic;
	return major == 5 || major == 10 || major == 3 || major == 8 || major == 1;
}

const char *NoteName(int pitchClass, const Key &key)
{
	return (UsesFlats(key) ? FLAT_NAMES : SHARP_NAMES)[PitchClass(pitchClass)];
}

bool InScale(int pitchClass, const Key &key)
{
	static const bool MAJOR[12] = { true, false, true, false, true, true, false, true, false, true, false, true };
	// Natural and harmonic minor together: both the 7th and the raised 7th
	static const bool MINOR[12] = { true, false, true, true, false, true, false, true, true, false, true, true };
	return (key.minor ? MINOR : MAJOR)[PitchClass(pitchClass - key.tonic)];
}

double Correlation(const double *x, const double *y)
{
	double mx = 0.0, my = 0.0;
	for (int i = 0; i < 12; ++i) {
		mx += x[i];
		my += y[i];
	}
	mx /= 12.0;
	my /= 12.0;
	double sxy = 0.0, sxx = 0.0, syy = 0.0;
	for (int i = 0; i < 12; ++i) {
		sxy += (x[i] - mx) * (y[i] - my);
		sxx += (x[i] - mx) * (x[i] - mx);
		syy += (y[i] - my) * (y[i] - my);
	}
	return (sxx > 0.0 && syy > 0.0) ? sxy / std::sqrt(sxx * syy) : 0.0;
}

int Third(const Chord &chord) { return chord.quality == Chord::Major ? 4 : 3; }
int Fifth(const Chord &chord) { return chord.quality == Chord::Diminished ? 6 : 7; }

// How well a melody note fits a chord: chord tones fit, other notes of the key
// pass, notes outside it clash, and a long note a semitone above a chord tone
// (F over an E, C over a B) clashes most.
double Fit(int pitchClass, const Chord &chord, const Key &key, bool longNote)
{
	const int interval = PitchClass(pitchClass - chord.root);
	if (interval == 0 || interval == Third(chord) || interval == Fifth(chord))
		return 1.0;
	double fit = InScale(pitchClass, key) ? -0.35 : -0.8;
	if (longNote && (interval == 1 || interval == Third(chord) + 1 || interval == Fifth(chord) + 1))
		fit -= 0.3;
	return fit;
}

// The chords of a key and what they do: rest (tonic), lead away
// (subdominant) or pull back home (dominant)
enum Function { TONIC, SUBDOMINANT, DOMINANT };

// Indices into KeyChords
const int MAJOR_FUNCTIONS[6] = { TONIC, SUBDOMINANT, TONIC, SUBDOMINANT, DOMINANT, TONIC };	// I ii iii IV V vi
const int MINOR_FUNCTIONS[7] = { TONIC, SUBDOMINANT, DOMINANT, DOMINANT, SUBDOMINANT, TONIC, DOMINANT };	// i iv v V VI III VII
enum { I = 0, II = 1, III = 2, IV = 3, V = 4, VI = 5 };							// major
enum { Im = 0, IVm = 1, Vm = 2, V7 = 3, VIb = 4, IIIb = 5, VIIb = 6 };		// minor

// From one function to another: home -> away -> back home
const double FUNCTION_STEP[3][3] = {
	//   to T    S     D
	{ 0.0, 0.2, 0.15 },	// from tonic
	{ 0.1, 0.05, 0.3 },	// from subdominant
	{ 0.4, -0.2, -0.05 }	// from dominant
};

// Any change of chord costs a little more than the best step gains, so with
// no melody to follow the chord stays rather than wandering round the key
const double CHANGE = -0.4;
const double MIDBAR_CHANGE = -0.6;	// and more for one halfway through a bar

// A change of chord from p to c, by index into KeyChords
double Step(int p, int c, bool minor)
{
	const int *functions = minor ? MINOR_FUNCTIONS : MAJOR_FUNCTIONS;
	double step = CHANGE + FUNCTION_STEP[functions[p]][functions[c]];
	if (!minor) {
		if (p == V && c == I) step += 0.2;		// the cadence
		if (p == II && c == V) step += 0.1;
		if (p == VI && (c == IV || c == II)) step += 0.1;
		if (p == V && c == VI) step += 0.1;		// deceptive cadence
		if (p == III && c == VI) step += 0.1;
		if (c == II) step -= 0.05;				// IV first, when both fit
		if (c == III) step -= 0.15;				// the least used
	}
	else {
		if (p == V7 && c == Im) step += 0.2;
		if (p == VIIb && c == Im) step += 0.1;
		if (p == VIb && c == VIIb) step += 0.15;	// i-VI-VII-i
		if (p == VIIb && c == IIIb) step += 0.1;
		if (p == IVm && c == V7) step += 0.1;
		if (c == Vm) step -= 0.05;
	}
	return step;
}

double Opening(int c)
{
	return c == 0 ? 0.4 : -0.15;
}

double Closing(int c, bool minor)
{
	if (c == 0)
		return 0.4;
	const int *functions = minor ? MINOR_FUNCTIONS : MAJOR_FUNCTIONS;
	return functions[c] == DOMINANT ? -0.1 : -0.05;
}

// The note of a pitch class within [low, high] nearest to near
int Nearest(int pitchClass, int near, int low, int high)
{
	int best = -1;
	for (int midi = low; midi <= high; ++midi) {
		if (PitchClass(midi) != PitchClass(pitchClass))
			continue;
		if (best < 0 || std::abs(midi - near) < std::abs(best - near))
			best = midi;
	}
	if (best < 0)	// a range under an octave
		best = low + PitchClass(pitchClass - low);
	return best;
}

// The parts of the chord spans within each bar, in order
struct Segment {
	int row;
	int end;
	Chord chord;
	const Bar *bar;
};

std::vector<Segment> Segments(const std::vector<ChordSpan> &chords, const std::vector<Bar> &bars)
{
	std::vector<Segment> segments;
	for (size_t b = 0; b < bars.size(); ++b) {
		const int barEnd = bars[b].row + bars[b].length;
		for (size_t c = 0; c < chords.size(); ++c) {
			const int from = std::max(chords[c].row, bars[b].row);
			const int to = std::min(chords[c].row + chords[c].length, barEnd);
			if (to <= from)
				continue;
			Segment s;
			s.row = from;
			s.end = to;
			s.chord = chords[c].chord;
			s.bar = &bars[b];
			segments.push_back(s);
		}
	}
	return segments;
}

MelodyCell NoteCell(int row, int midi)
{
	MelodyCell cell;
	cell.kind = MelodyCell::Note;
	cell.row = row;
	cell.midi = midi;
	return cell;
}

}	// namespace

// ---------------------------------------------------------------------------
// Keys and chords
// ---------------------------------------------------------------------------

std::string KeyName(const Key &key)
{
	return std::string(NoteName(key.tonic, key)) + (key.minor ? " minor" : " major");
}

bool ParseKeyName(const std::string &name, Key &key)
{
	std::string text;
	for (size_t i = 0; i < name.size(); ++i)
		if (!std::isspace(static_cast<unsigned char>(name[i])))
			text += name[i];
	if (text.empty())
		return false;
	// The tonic: a letter and maybe a sharp or flat
	size_t tonicLength = 1;
	if (text.size() > 1 && (text[1] == '#' || text[1] == 'b'))
		tonicLength = 2;
	int root = 0;
	if (!ParseKey(text.substr(0, tonicLength), root))
		return false;
	std::string mode = text.substr(tonicLength);
	for (size_t i = 0; i < mode.size(); ++i)
		mode[i] = char(std::tolower(static_cast<unsigned char>(mode[i])));
	if (mode.empty() || mode == "maj" || mode == "major")
		key.minor = false;
	else if (mode == "m" || mode == "min" || mode == "minor")
		key.minor = true;
	else
		return false;
	key.tonic = root;
	return true;
}

std::vector<MelodyNote> MelodyOf(const std::vector<GridNote> &notes)
{
	std::vector<MelodyNote> melody;
	for (size_t i = 0; i < notes.size(); ++i) {
		MelodyNote m;
		m.row = notes[i].row;
		m.length = std::max(1, notes[i].length);
		m.midi = notes[i].midi;
		m.weight = notes[i].volume < 0 ? 1.0 : std::max(0.3, notes[i].volume / 15.0);
		melody.push_back(m);
	}
	return melody;
}

Key FindKey(const std::vector<MelodyNote> &melody)
{
	Key best;
	if (melody.empty())
		return best;

	double histogram[12] = { 0.0 };
	const MelodyNote *first = &melody.front(), *last = &melody.front();
	for (size_t i = 0; i < melody.size(); ++i) {
		histogram[PitchClass(melody[i].midi)] += melody[i].length * melody[i].weight;
		if (melody[i].row < first->row)
			first = &melody[i];
		if (melody[i].row >= last->row)
			last = &melody[i];
	}

	double bestScore = -1e9;
	for (int mode = 0; mode < 2; ++mode) {
		const double *profile = mode ? MINOR_PROFILE : MAJOR_PROFILE;
		for (int tonic = 0; tonic < 12; ++tonic) {
			double rotated[12];
			for (int pc = 0; pc < 12; ++pc)
				rotated[pc] = profile[PitchClass(pc - tonic)];
			double score = Correlation(histogram, rotated);
			if (PitchClass(last->midi) == tonic)
				score += 0.1;	// melodies like to end at home
			if (PitchClass(first->midi) == tonic)
				score += 0.05;
			if (score > bestScore + 1e-9) {	// ties keep the major key, tried first
				bestScore = score;
				best.tonic = tonic;
				best.minor = mode == 1;
			}
		}
	}
	return best;
}

std::string ChordName(const Chord &chord, const Key &key)
{
	std::string name = NoteName(chord.root, key);
	if (chord.quality == Chord::Minor)
		name += "m";
	else if (chord.quality == Chord::Diminished)
		name += "dim";
	return name;
}

std::vector<Chord> KeyChords(const Key &key)
{
	struct Degree { int interval; Chord::Quality quality; };
	static const Degree MAJOR[6] = {
		{ 0, Chord::Major }, { 2, Chord::Minor }, { 4, Chord::Minor }, { 5, Chord::Major }, { 7, Chord::Major }, { 9, Chord::Minor }
	};
	static const Degree MINOR[7] = {
		{ 0, Chord::Minor }, { 5, Chord::Minor }, { 7, Chord::Minor }, { 7, Chord::Major }, { 8, Chord::Major },
		{ 3, Chord::Major }, { 10, Chord::Major }
	};
	const Degree *degrees = key.minor ? MINOR : MAJOR;
	const int count = key.minor ? 7 : 6;
	std::vector<Chord> chords;
	for (int i = 0; i < count; ++i) {
		Chord c;
		c.root = PitchClass(key.tonic + degrees[i].interval);
		c.quality = degrees[i].quality;
		chords.push_back(c);
	}
	return chords;
}

std::vector<Bar> SongBars(int firstRow, int endRow, int patternLength, int barRows)
{
	std::vector<Bar> bars;
	if (patternLength <= 0 || barRows <= 0 || endRow <= firstRow)
		return bars;
	for (int frame = std::max(0, firstRow) / patternLength; frame * patternLength < endRow; ++frame) {
		for (int r = 0; r < patternLength; r += barRows) {
			Bar bar;
			bar.row = frame * patternLength + r;
			bar.length = std::min(barRows, patternLength - r);
			if (bar.row + bar.length > firstRow && bar.row < endRow)
				bars.push_back(bar);
		}
	}
	return bars;
}

std::vector<Bar> MelodyBars(const std::vector<MelodyNote> &melody, int patternLength, int barRows)
{
	if (melody.empty())
		return std::vector<Bar>();
	int first = melody.front().row, end = 0;
	for (size_t i = 0; i < melody.size(); ++i) {
		first = std::min(first, melody[i].row);
		end = std::max(end, melody[i].row + melody[i].length);
	}
	std::vector<Bar> bars = SongBars(first, end, patternLength, barRows);
	if (bars.size() > 1 && (first - bars.front().row) * 2 > bars.front().length)
		bars.erase(bars.begin());	// a pickup into the next bar
	return bars;
}

// ---------------------------------------------------------------------------
// Choosing chords
// ---------------------------------------------------------------------------

std::vector<ChordSpan> ChooseChords(const std::vector<MelodyNote> &melody, const std::vector<Bar> &bars,
	const Key &key, const HarmonyOptions &options)
{
	std::vector<ChordSpan> result;
	if (bars.empty())
		return result;
	const std::vector<Chord> chords = KeyChords(key);
	const int count = int(chords.size());
	const int beat = std::max(1, options.rowsPerBeat);

	// Each bar is one slot, or two halves
	struct Slot { int row, end; bool secondHalf; const Bar *bar; };
	std::vector<Slot> slots;
	for (size_t b = 0; b < bars.size(); ++b) {
		const Bar &bar = bars[b];
		const int beats = bar.length / beat;
		if (options.halfBars && beats >= 2) {
			const int middle = bar.row + (beats / 2) * beat;
			const Slot first = { bar.row, middle, false, &bar };
			const Slot second = { middle, bar.row + bar.length, true, &bar };
			slots.push_back(first);
			slots.push_back(second);
		}
		else {
			const Slot whole = { bar.row, bar.row + bar.length, false, &bar };
			slots.push_back(whole);
		}
	}

	// How well each chord fits the melody in each slot
	std::vector<std::vector<double> > fit(slots.size(), std::vector<double>(size_t(count), 0.0));
	for (size_t s = 0; s < slots.size(); ++s) {
		const Slot &slot = slots[s];
		for (size_t n = 0; n < melody.size(); ++n) {
			const MelodyNote &note = melody[n];
			const int from = std::max(note.row, slot.row);
			const int to = std::min(note.row + note.length, slot.end);
			if (to <= from)
				continue;
			double weight = double(to - from) / beat * note.weight;
			if (note.row <= slot.row && slot.row < note.row + note.length)
				weight *= 1.5;	// heard on the slot's first beat
			else if ((note.row - slot.bar->row) % beat == 0)
				weight *= 1.2;	// starts on a beat
			if (note.length * 2 < beat)
				weight *= 0.6;	// a quick passing note
			const bool longNote = note.length >= beat;
			for (int c = 0; c < count; ++c)
				fit[s][size_t(c)] += weight * Fit(note.midi, chords[size_t(c)], key, longNote);
		}
	}

	// The best path of chords through the slots (Viterbi)
	std::vector<std::vector<double> > score(slots.size(), std::vector<double>(size_t(count), 0.0));
	std::vector<std::vector<int> > from(slots.size(), std::vector<int>(size_t(count), 0));
	for (int c = 0; c < count; ++c)
		score[0][size_t(c)] = fit[0][size_t(c)] + Opening(c);
	for (size_t s = 1; s < slots.size(); ++s) {
		for (int c = 0; c < count; ++c) {
			double best = -1e18;
			int bestFrom = 0;
			for (int p = 0; p < count; ++p) {
				double step = 0.0;
				if (p != c) {
					step = Step(p, c, key.minor);
					if (slots[s].secondHalf)
						step += MIDBAR_CHANGE;
				}
				const double total = score[s - 1][size_t(p)] + step;
				if (total > best) {
					best = total;
					bestFrom = p;
				}
			}
			score[s][size_t(c)] = best + fit[s][size_t(c)];
			from[s][size_t(c)] = bestFrom;
		}
	}
	const size_t last = slots.size() - 1;
	int c = 0;
	double best = -1e18;
	for (int k = 0; k < count; ++k) {
		const double total = score[last][size_t(k)] + Closing(k, key.minor);
		if (total > best) {
			best = total;
			c = k;
		}
	}
	std::vector<int> path(slots.size());
	for (size_t s = slots.size(); s-- > 0; ) {
		path[s] = c;
		c = from[s][size_t(c)];
	}

	// Slots with the same chord in a row become one span
	for (size_t s = 0; s < slots.size(); ++s) {
		const Chord &chord = chords[size_t(path[s])];
		if (!result.empty() && result.back().chord == chord && result.back().row + result.back().length == slots[s].row) {
			result.back().length = slots[s].end - result.back().row;
			continue;
		}
		ChordSpan span;
		span.row = slots[s].row;
		span.length = slots[s].end - slots[s].row;
		span.chord = chord;
		result.push_back(span);
	}
	return result;
}

// ---------------------------------------------------------------------------
// Pattern cells
// ---------------------------------------------------------------------------

std::vector<MelodyCell> BassCells(const std::vector<ChordSpan> &chords, const std::vector<Bar> &bars,
	const HarmonyOptions &options, int lowMidi, int highMidi)
{
	std::vector<MelodyCell> cells;
	const int beat = std::max(1, options.rowsPerBeat);
	const std::vector<Segment> segments = Segments(chords, bars);
	int previous = lowMidi + 4;	// start low
	int end = 0;
	for (size_t i = 0; i < segments.size(); ++i) {
		const Segment &s = segments[i];
		const int root = Nearest(s.chord.root, previous, lowMidi, highMidi);
		cells.push_back(NoteCell(s.row, root));
		previous = root;
		end = s.end;

		// The fifth halfway through a bar of four or more beats
		const int beats = s.bar->length / beat;
		if (beats >= 4 && s.row == s.bar->row) {
			const int middle = s.bar->row + (beats / 2) * beat;
			if (middle < s.end) {
				const int fifth = root + Fifth(s.chord) <= highMidi ? root + Fifth(s.chord) : root + Fifth(s.chord) - 12;
				cells.push_back(NoteCell(middle, fifth));
			}
		}
	}
	if (!cells.empty()) {
		MelodyCell release;
		release.kind = MelodyCell::Release;
		release.row = end;
		cells.push_back(release);
	}
	return cells;
}

std::vector<MelodyCell> ArpeggioCells(const std::vector<ChordSpan> &chords, const std::vector<Bar> &bars,
	int lowMidi, int highMidi)
{
	std::vector<MelodyCell> cells;
	const std::vector<Segment> segments = Segments(chords, bars);
	int previous = (lowMidi + highMidi) / 2;
	int end = 0;
	for (size_t i = 0; i < segments.size(); ++i) {
		const Segment &s = segments[i];
		const int tones[3] = { s.chord.root, PitchClass(s.chord.root + Third(s.chord)), PitchClass(s.chord.root + Fifth(s.chord)) };

		// Root position or an inversion, its lowest note nearest the last one
		int bestBase = 0, bestParam = 0;
		double bestCost = 1e9;
		for (int inversion = 0; inversion < 3; ++inversion) {
			const int bass = tones[inversion];
			const int base = Nearest(bass, previous, lowMidi, highMidi);
			const int x = PitchClass(tones[(inversion + 1) % 3] - bass);
			const int y = PitchClass(tones[(inversion + 2) % 3] - bass);
			const double cost = std::abs(base - previous) + (inversion == 0 ? 0.0 : 0.5);
			if (cost < bestCost) {
				bestCost = cost;
				bestBase = base;
				bestParam = (std::min(x, y) << 4) | std::max(x, y);
			}
		}
		MelodyCell cell = NoteCell(s.row, bestBase);
		cell.effect = '0';
		cell.effectParam = bestParam;
		cells.push_back(cell);
		previous = bestBase;
		end = s.end;
	}
	if (!cells.empty()) {
		MelodyCell release;
		release.kind = MelodyCell::Release;
		release.row = end;
		release.effect = '0';	// 000: arpeggio off
		release.effectParam = 0;
		cells.push_back(release);
	}
	return cells;
}

}	// namespace transcribe

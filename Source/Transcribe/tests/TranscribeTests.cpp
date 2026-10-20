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

// Tests for the transcription engine, on synthesized audio with known notes.

#include "Transcribe.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>

using namespace transcribe;

namespace {

int g_checks = 0;
int g_failures = 0;

#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_NEAR(a, b, tol) do { ++g_checks; const double a_ = (a), b_ = (b); \
	if (std::fabs(a_ - b_) > (tol)) { ++g_failures; std::printf("FAIL %s:%d: %s = %g, expected %g +- %g\n", __FILE__, __LINE__, #a, a_, b_, double(tol)); } } while (0)

const int RATE = 44100;
const double PI = 3.14159265358979323846;

double MidiToFrequency(double midi) { return 440.0 * std::pow(2.0, (midi - 69.0) / 12.0); }

unsigned g_seed = 12345;
double Noise() { g_seed = g_seed * 1103515245u + 12345u; return ((g_seed >> 8) & 0xFFFF) / 32768.0 - 1.0; }

struct Synth {
	Audio audio;
	double phase = 0.0;
	Synth() { audio.sampleRate = RATE; }

	// A tone with optional vibrato, harmonics, noise and fades. Phase carries over
	// between calls so legato notes join without clicks.
	void Tone(double freq, double seconds, double amp = 0.5, bool fade = true, double vibratoCents = 0.0,
		double vibratoHz = 0.0, int harmonics = 1, double noise = 0.0)
	{
		const int n = int(seconds * RATE);
		const int ramp = fade ? int(0.005 * RATE) : 0;
		double norm = 0.0;
		for (int h = 1; h <= harmonics; ++h) norm += 1.0 / h;
		for (int i = 0; i < n; ++i) {
			const double t = double(i) / RATE;
			const double f = freq * std::pow(2.0, vibratoCents * std::sin(2.0 * PI * vibratoHz * t) / 1200.0);
			phase += 2.0 * PI * f / RATE;
			double v = 0.0;
			for (int h = 1; h <= harmonics; ++h) v += std::sin(h * phase) / h;
			v = v / norm + noise * Noise();
			double g = 1.0;
			if (i < ramp) g = 0.5 - 0.5 * std::cos(PI * i / ramp);
			if (i > n - ramp) g = 0.5 - 0.5 * std::cos(PI * (n - i) / ramp);
			audio.samples.push_back(float(amp * g * v));
		}
	}

	// Smooth pitch glide (log-frequency) between two frequencies.
	void Glide(double from, double to, double seconds, double amp = 0.5)
	{
		const int n = int(seconds * RATE);
		for (int i = 0; i < n; ++i) {
			const double f = from * std::pow(to / from, double(i) / n);
			phase += 2.0 * PI * f / RATE;
			audio.samples.push_back(float(amp * std::sin(phase)));
		}
	}

	void Silence(double seconds) { audio.samples.resize(audio.samples.size() + size_t(seconds * RATE), 0.0f); }

	// Continues the current tone with an exponential decay (time constant tau), the
	// way a whistle or hum fades out.
	void Decay(double freq, double seconds, double amp, double tau)
	{
		const int n = int(seconds * RATE);
		for (int i = 0; i < n; ++i) {
			phase += 2.0 * PI * freq / RATE;
			audio.samples.push_back(float(amp * std::exp(-double(i) / (tau * RATE)) * std::sin(phase)));
		}
	}

	// Room noise over the whole recording.
	void AddNoise(double amp)
	{
		for (float &v : audio.samples)
			v += float(amp * Noise());
	}

	// A 40 ms decaying noise burst: bright = high-passed (tap with a nail),
	// dark = low-passed (tap with the finger pad).
	void Tap(bool bright, double amp = 0.8)
	{
		const int n = int(0.040 * RATE);
		double prev = 0.0, low = 0.0;
		for (int i = 0; i < n; ++i) {
			const double w = Noise();
			double v;
			if (bright) { v = w - prev; prev = w; }
			else { low += 0.05 * (w - low); v = low * 6.0; }
			audio.samples.push_back(float(amp * v * std::exp(-double(i) / (0.008 * RATE))));
		}
	}
};

std::vector<Note> Transcribe(const Audio &audio)
{
	return SegmentNotes(TrackPitch(audio, PitchOptions()), NoteOptions());
}

std::vector<GridNote> ToRows(std::vector<Note> notes, int shift = 0, int tempo = 150)
{
	Transpose(notes, shift);
	Grid grid;
	grid.tempo = tempo;
	return Quantize(notes, grid, QuantizeOptions());
}

// Text of the given channel's cell on a ROW line ("ROW 05 : c0 : c1 ...").
std::string CellOf(const std::string &text, int pattern, int row, int channel)
{
	char header[16], rowTag[16];
	std::snprintf(header, sizeof(header), "PATTERN %02X\n", pattern);
	std::snprintf(rowTag, sizeof(rowTag), "ROW %02X ", row);
	const size_t p = text.find(header);
	if (p == std::string::npos) return "<no pattern>";
	const size_t r = text.find(rowTag, p);
	if (r == std::string::npos) return "<no row>";
	const std::string line = text.substr(r, text.find('\n', r) - r);
	size_t at = 0;
	for (int c = 0; c <= channel; ++c) {
		at = line.find(" : ", at);
		if (at == std::string::npos) return "<no cell>";
		at += 3;
	}
	const size_t end = line.find(" : ", at);
	return line.substr(at, end == std::string::npos ? std::string::npos : end - at);
}

// ---------------------------------------------------------------------------

void TestGridAndNames()
{
	Grid g;
	CHECK_NEAR(g.RowsPerSecond(), 10.0, 1e-9);	// tempo 150, speed 6
	g.tempo = 120;
	CHECK_NEAR(g.RowsPerSecond(), 8.0, 1e-9);
	g.tempo = 125;
	CHECK_NEAR(g.RowsPerSecond(), 500.0 * 60.0 / 3600.0, 1e-9);
	g.tempo = 100; g.speed = 7;	// 2400/7 = 342 r 6: exercises the remainder term
	CHECK_NEAR(g.RowsPerSecond(), 60.0 * 342 / (3600.0 - 6), 1e-9);

	CHECK(FamiTrackerNoteName(69) == "A-3");	// 440 Hz
	CHECK(FamiTrackerNoteName(60) == "C-3");
	CHECK(FamiTrackerNoteName(72) == "C-4");
	CHECK(FamiTrackerNoteName(61) == "C#3");
	CHECK(FamiTrackerNoteName(24) == "C-0");
	CHECK(FamiTrackerNoteName(0) == "C-0");	// clamped
	CHECK(FamiTrackerNoteName(200) == "B-7");	// clamped
}

void TestScales()
{
	int root = -1;
	CHECK(ParseKey("C", root) && root == 0);
	CHECK(ParseKey("F#", root) && root == 6);
	CHECK(ParseKey("Bb", root) && root == 10);
	CHECK(!ParseKey("H", root));
	Scale s = Scale::Chromatic;
	CHECK(ParseScale("pentatonic", s) && s == Scale::MajorPentatonic);
	CHECK(ParseScale("minor-pentatonic", s) && s == Scale::MinorPentatonic);
	CHECK(!ParseScale("lydian", s));

	CHECK(SnapToScale(60, 0, Scale::Major) == 60);
	CHECK(SnapToScale(61, 0, Scale::Major) == 60);	// tie between C and D keeps the lower
	CHECK(SnapToScale(65, 0, Scale::MajorPentatonic) == 64);	// F -> E
	CHECK(SnapToScale(66, 0, Scale::MajorPentatonic) == 67);	// F# -> G
	CHECK(SnapToScale(70, 9, Scale::MinorPentatonic) == 69);	// A minor pentatonic: Bb -> A

	std::vector<Note> notes(1);
	notes[0].pitch = 61.3;	// a slightly sharp C#: nearer D
	notes[0].midi = 61;
	SnapToScale(notes, 0, Scale::Major);
	CHECK(notes[0].midi == 62);
}

// YIN with the direct difference function, as TrackPitch computed it before it
// switched to an FFT cross-correlation. The two must agree.
std::vector<PitchFrame> DirectYin(const Audio &audio, const PitchOptions &options)
{
	std::vector<PitchFrame> frames;
	const int rate = audio.sampleRate;
	const int tauMin = std::max(2, int(std::floor(rate / options.maxFrequency)));
	const int tauMax = std::max(tauMin + 2, int(std::ceil(rate / options.minFrequency)));
	const int window = std::max(tauMax, int(std::lround(options.windowSeconds * rate)));
	const int hop = std::max(1, int(std::lround(options.hopSeconds * rate)));
	const size_t span = size_t(window) + size_t(tauMax) + 1;
	const std::vector<float> &x = audio.samples;
	std::vector<double> diff(tauMax + 1), cmnd(tauMax + 1);
	for (size_t start = 0; start + span <= x.size(); start += hop) {
		const float *s = &x[start];
		PitchFrame frame;
		frame.time = (double(start) + window / 2.0) / rate;
		double energy = 0.0;
		for (int j = 0; j < window; ++j)
			energy += double(s[j]) * s[j];
		frame.rms = std::sqrt(energy / window);
		if (frame.rms <= 1e-9) {
			frames.push_back(frame);
			continue;
		}
		for (int tau = 1; tau <= tauMax; ++tau) {
			double sum = 0.0;
			for (int j = 0; j < window; ++j) {
				const double d = double(s[j]) - s[j + tau];
				sum += d * d;
			}
			diff[tau] = sum;
		}
		cmnd[0] = 1.0;
		double running = 0.0;
		for (int tau = 1; tau <= tauMax; ++tau) {
			running += diff[tau];
			cmnd[tau] = running > 0.0 ? diff[tau] * tau / running : 1.0;
		}
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
			best = tauMin;
			for (int tau = tauMin + 1; tau <= tauMax; ++tau)
				if (cmnd[tau] < cmnd[best])
					best = tau;
		}
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

void TestFastDifferenceFunction()
{
	// A hum, a whistle with vibrato and noise, a glide, then noise alone
	Synth s;
	s.Silence(0.1);
	s.Tone(MidiToFrequency(50), 0.4, 0.4, true, 10.0, 5.0, 6, 0.01);
	s.Tone(MidiToFrequency(88), 0.4, 0.3, true, 25.0, 5.5, 1, 0.03);
	s.Glide(MidiToFrequency(60), MidiToFrequency(72), 0.3);
	for (int i = 0; i < RATE / 5; ++i)
		s.audio.samples.push_back(float(0.05 * Noise()));
	const std::vector<PitchFrame> fast = TrackPitch(s.audio, PitchOptions());
	const std::vector<PitchFrame> direct = DirectYin(s.audio, PitchOptions());
	CHECK(fast.size() == direct.size());
	double worstCents = 0.0, worstAperiodicity = 0.0, worstRms = 0.0;
	int differentLag = 0;
	for (size_t i = 0; i < fast.size() && i < direct.size(); ++i) {
		worstAperiodicity = std::max(worstAperiodicity, std::fabs(fast[i].aperiodicity - direct[i].aperiodicity));
		if (fast[i].frequency > 0.0 && direct[i].frequency > 0.0)
			worstCents = std::max(worstCents, 1200.0 * std::fabs(std::log2(fast[i].frequency / direct[i].frequency)));
		else if (fast[i].frequency != direct[i].frequency)
			++differentLag;
		worstRms = std::max(worstRms, std::fabs(fast[i].rms - direct[i].rms));
	}
	if (worstCents > 1e-6 || worstAperiodicity > 1e-9)
		std::printf("  FFT vs direct: %g cents, aperiodicity %g\n", worstCents, worstAperiodicity);
	CHECK(worstCents < 1e-6);
	CHECK(worstAperiodicity < 1e-9);
	CHECK(differentLag == 0);
	CHECK(worstRms < 1e-9);
}

void TestPitchAccuracy()
{
	struct Case { const char *name; double freq; int harmonics; double vibrato; double noise; int expectedMidi; };
	const Case cases[] = {
		{ "sine A4", 440.0, 1, 0.0, 0.0, 69 },
		{ "hum A2 with harmonics", 110.0, 8, 0.0, 0.0, 45 },
		{ "hum G3 with harmonics", 196.0, 6, 15.0, 0.01, 55 },
		{ "whistle A6 with vibrato and noise", 1760.0, 1, 20.0, 0.03, 93 },
		{ "whistle E7", 2637.0, 1, 10.0, 0.02, 100 },
	};
	for (const Case &c : cases) {
		Synth s;
		s.Silence(0.2);
		s.Tone(c.freq, 1.0, 0.5, true, c.vibrato, 5.5, c.harmonics, c.noise);
		s.Silence(0.2);
		const std::vector<Note> notes = Transcribe(s.audio);
		if (notes.size() != 1 || notes[0].midi != c.expectedMidi) {
			std::printf("  case '%s': %zu notes", c.name, notes.size());
			for (const Note &n : notes) std::printf(" [%d %.2f %.3f-%.3f]", n.midi, n.pitch, n.start, n.end);
			std::printf("\n");
		}
		CHECK(notes.size() == 1);
		if (!notes.empty()) {
			CHECK(notes[0].midi == c.expectedMidi);
			CHECK_NEAR(notes[0].pitch, c.expectedMidi + 12.0 * std::log2(c.freq / MidiToFrequency(c.expectedMidi)), 0.1);
			CHECK_NEAR(notes[0].start, 0.2, 0.02);
			CHECK_NEAR(notes[0].end, 1.2, 0.02);
		}
	}
}

// The user's requirement: a long hum becomes a long note.
void TestNoteLengths()
{
	{
		Synth s;
		s.Silence(0.3);
		s.Tone(MidiToFrequency(57), 2.0, 0.4, true, 0.0, 0.0, 6);	// 2-second hum
		s.Silence(0.3);
		const std::vector<GridNote> rows = ToRows(Transcribe(s.audio));
		CHECK(rows.size() == 1);
		if (rows.size() == 1) {
			CHECK(rows[0].row == 0);
			CHECK(std::abs(rows[0].length - 20) <= 1);	// 2.0 s at 10 rows/s
			CHECK(rows[0].midi == 57);
		}
	}
	{
		// Long, gap, short: lengths and the gap between them are kept
		Synth s;
		s.Silence(0.25);
		s.Tone(440.0, 1.5);
		s.Silence(0.3);
		s.Tone(MidiToFrequency(72), 0.3);
		s.Silence(0.25);
		const std::vector<GridNote> rows = ToRows(Transcribe(s.audio));
		CHECK(rows.size() == 2);
		if (rows.size() == 2) {
			CHECK(std::abs(rows[0].length - 15) <= 1);
			CHECK(std::abs(rows[1].row - 18) <= 1);
			CHECK(std::abs(rows[1].length - 3) <= 1);
			CHECK(rows[0].midi == 69 && rows[1].midi == 72);
		}
	}
	{
		// Same note twice with a short breath between: two notes, not one
		Synth s;
		s.Silence(0.2);
		s.Tone(440.0, 0.4);
		s.Silence(0.15);
		s.Tone(440.0, 0.4);
		s.Silence(0.2);
		const std::vector<Note> notes = Transcribe(s.audio);
		CHECK(notes.size() == 2);
	}
	{
		// A long note with vibrato and a 20 ms dropout stays one note
		Synth s;
		s.Silence(0.2);
		s.Tone(440.0, 1.0, 0.5, true, 40.0, 6.0);
		s.audio.samples.resize(s.audio.samples.size() + size_t(0.020 * RATE), 0.0f);
		s.Tone(440.0, 1.0, 0.5, true, 40.0, 6.0);
		s.Silence(0.2);
		const std::vector<GridNote> rows = ToRows(Transcribe(s.audio));
		CHECK(rows.size() == 1);
		if (rows.size() == 1)
			CHECK(std::abs(rows[0].length - 20) <= 1);
	}
}

// A quieter note keeps its fade-out, even below the silence gate that the loudest
// note sets for the whole recording.
void TestQuietEdges()
{
	Synth s;
	s.Silence(0.3);
	s.Tone(MidiToFrequency(81), 0.5, 0.5);	// loud: sets the gate at about -44 dBFS
	s.Silence(0.4);
	const double quietStart = s.audio.Duration();
	s.Tone(MidiToFrequency(84), 0.4, 0.03, false);	// about 24 dB quieter...
	const double toneEnd = s.audio.Duration();
	s.Decay(MidiToFrequency(84), 0.4, 0.03, 0.040);	// ...fading out with a 40 ms time constant
	s.Silence(0.3);
	s.AddNoise(0.0006);	// room noise, about -69 dBFS
	const std::vector<Note> notes = Transcribe(s.audio);
	CHECK(notes.size() == 2);
	if (notes.size() == 2) {
		CHECK_NEAR(notes[1].start, quietStart, 0.02);
		// The gate alone would end the note about 50 ms into the fade
		CHECK(notes[1].end > toneEnd + 0.08);
		CHECK(notes[1].end < toneEnd + 0.16);
		CHECK(notes[1].midi == 84);
	}
}

void TestMelodies()
{
	{
		// Legato arpeggio C4 E4 G4 C5, 0.4 s each
		Synth s;
		s.Silence(0.2);
		const int midis[4] = { 60, 64, 67, 72 };
		for (int i = 0; i < 4; ++i)
			s.Tone(MidiToFrequency(midis[i]), 0.4, 0.5, false);
		s.Silence(0.2);
		const std::vector<GridNote> rows = ToRows(Transcribe(s.audio));
		CHECK(rows.size() == 4);
		for (size_t i = 0; i < rows.size() && i < 4; ++i) {
			CHECK(rows[i].midi == midis[i]);
			CHECK(std::abs(rows[i].row - int(4 * i)) <= 1);
			CHECK(std::abs(rows[i].length - 4) <= 1);
		}
	}
	{
		// A quick slide between two notes gives two notes, not a smear
		Synth s;
		s.Silence(0.2);
		s.Tone(MidiToFrequency(60), 0.4, 0.5, false);
		s.Glide(MidiToFrequency(60), MidiToFrequency(62), 0.06);
		s.Tone(MidiToFrequency(62), 0.4, 0.5, false);
		s.Silence(0.2);
		const std::vector<Note> notes = Transcribe(s.audio);
		CHECK(notes.size() == 2);
		if (notes.size() == 2)
			CHECK(notes[0].midi == 60 && notes[1].midi == 62);
	}
	{
		// Whistled A6 C7 E7 lands on A-3 C-4 E-4 after the octave fit
		Synth s;
		s.Silence(0.3);
		s.Tone(MidiToFrequency(93), 0.5, 0.4, true, 15.0, 5.0, 1, 0.02);
		s.Tone(MidiToFrequency(96), 0.5, 0.4, true, 15.0, 5.0, 1, 0.02);
		s.Silence(0.2);
		s.Tone(MidiToFrequency(100), 1.0, 0.4, true, 15.0, 5.0, 1, 0.02);
		s.Silence(0.3);
		std::vector<Note> notes = Transcribe(s.audio);
		const int shift = OctaveShiftFor(notes, 67);
		CHECK(shift == -24);
		const std::vector<GridNote> rows = ToRows(notes, shift);
		CHECK(rows.size() == 3);
		if (rows.size() == 3) {
			CHECK(FamiTrackerNoteName(rows[0].midi) == "A-3");
			CHECK(FamiTrackerNoteName(rows[1].midi) == "C-4");
			CHECK(FamiTrackerNoteName(rows[2].midi) == "E-4");
			CHECK(std::abs(rows[0].length - 5) <= 1);
			CHECK(std::abs(rows[2].length - 10) <= 1);
		}
	}
}

void TestQuantizeRules()
{
	std::vector<Note> notes(3);
	notes[0].start = 0.00; notes[0].end = 0.50; notes[0].midi = 60;	// overlaps the next note
	notes[1].start = 0.30; notes[1].end = 0.60; notes[1].midi = 62;
	notes[2].start = 0.31; notes[2].end = 0.40; notes[2].midi = 64;	// same row as the previous, shorter
	Grid grid;
	const std::vector<GridNote> rows = Quantize(notes, grid, QuantizeOptions());
	CHECK(rows.size() == 2);
	if (rows.size() == 2) {
		CHECK(rows[0].row == 0 && rows[0].length == 3);	// cut where the next note starts
		CHECK(rows[1].row == 3 && rows[1].midi == 62);
	}
	{
		// A note keeps its rounded duration: 180 ms is 2 rows, even though it
		// starts late in one row and ends early in another
		std::vector<Note> n(2);
		n[0].start = 0.00; n[0].end = 0.20; n[0].midi = 60;
		n[1].start = 0.56; n[1].end = 0.74; n[1].midi = 62;
		const std::vector<GridNote> r = Quantize(n, grid, QuantizeOptions());
		CHECK(r.size() == 2);
		if (r.size() == 2)
			CHECK(r[1].row == 6 && r[1].length == 2);
	}
	{
		// A short break is held through, a longer one stays a rest
		std::vector<Note> n(3);
		n[0].start = 0.00; n[0].end = 0.32; n[0].midi = 60;	// 100 ms break
		n[1].start = 0.42; n[1].end = 0.80; n[1].midi = 64;	// 300 ms break
		n[2].start = 1.10; n[2].end = 1.50; n[2].midi = 67;
		std::vector<GridNote> r = Quantize(n, grid, QuantizeOptions());
		CHECK(r.size() == 3);
		if (r.size() == 3) {
			CHECK(r[0].row == 0 && r[0].length == 4);	// held until the next note
			CHECK(r[1].row == 4 && r[1].length == 4);	// 0.38 s, then a rest
			CHECK(r[2].row == 11 && r[2].length == 4);
		}
		QuantizeOptions keep;
		keep.minRestSeconds = 0.0;
		r = Quantize(n, grid, keep);
		CHECK(r.size() == 3);
		if (r.size() == 3)
			CHECK(r[0].length == 3);	// 0.32 s, and the break stays
	}
	{
		// Two notes on one row (a quick slide) become one note spanning both,
		// at the pitch of the longer one
		std::vector<Note> n(2);
		n[0].start = 0.00; n[0].end = 0.04; n[0].midi = 58;
		n[1].start = 0.04; n[1].end = 0.50; n[1].midi = 60;
		const std::vector<GridNote> r = Quantize(n, grid, QuantizeOptions());
		CHECK(r.size() == 1);
		if (r.size() == 1)
			CHECK(r[0].row == 0 && r[0].length == 5 && r[0].midi == 60);
	}
}

void TestHits()
{
	{
		Synth s;
		s.Silence(0.5);
		for (int i = 0; i < 8; ++i) {
			s.Tap(true);
			s.Silence(0.25 - 0.040);
		}
		std::vector<Hit> hits = DetectHits(s.audio, HitOptions());
		CHECK(hits.size() == 8);
		for (size_t i = 0; i < hits.size() && i < 8; ++i)
			CHECK_NEAR(hits[i].time, 0.5 + 0.25 * i, 0.012);
		CHECK(ClassifyHits(hits) == 1);	// all the same kind of tap
		Grid grid;
		grid.tempo = 120;	// 8 rows/s: a tap every 2 rows
		const std::vector<GridHit> rows = QuantizeHits(hits, grid, true);
		CHECK(rows.size() == 8);
		for (size_t i = 0; i < rows.size(); ++i)
			CHECK(rows[i].row == int(2 * i));
	}
	{
		// Alternating finger-pad and nail taps: two kinds of sound
		Synth s;
		s.Silence(0.3);
		for (int i = 0; i < 8; ++i) {
			s.Tap(i % 2 == 1);
			s.Silence(0.3 - 0.040);
		}
		std::vector<Hit> hits = DetectHits(s.audio, HitOptions());
		CHECK(hits.size() == 8);
		CHECK(ClassifyHits(hits) == 2);
		for (size_t i = 0; i < hits.size() && i < 8; ++i)
			CHECK(hits[i].drum == (i % 2 == 0 ? Drum::Kick : Drum::Snare));
	}
}

void TestGuessSource()
{
	struct Case { const char *name; double freq; int harmonics; double noise; Source expected; };
	const Case cases[] = {
		{ "whistle", 1500.0, 1, 0.02, Source::Whistle },
		{ "low whistle", 700.0, 1, 0.02, Source::Whistle },
		{ "hum", 150.0, 6, 0.01, Source::Voice },
		{ "high voice", 650.0, 5, 0.01, Source::Voice },
	};
	for (const Case &c : cases) {
		Synth s;
		s.Silence(0.2);
		for (int i = 0; i < 4; ++i) {
			s.Tone(c.freq * std::pow(2.0, i / 12.0), 0.4, 0.4, true, 15.0, 5.5, c.harmonics, c.noise);
			s.Silence(0.1);
		}
		const SourceGuess guess = GuessSource(s.audio);
		if (guess.source != c.expected)
			std::printf("  case '%s': guessed %s (pitched %.2f, %.0f Hz, overtones %.3f)\n", c.name, SourceName(guess.source),
				guess.pitchedShare, guess.medianFrequency, guess.overtoneShare);
		CHECK(guess.source == c.expected);
	}
	{
		Synth s;
		s.Silence(0.3);
		for (int i = 0; i < 8; ++i) {
			s.Tap(i % 2 == 1);
			s.Silence(0.25 - 0.040);
		}
		CHECK(GuessSource(s.audio).source == Source::Taps);
	}
	CHECK(PitchOptionsFor(Source::Whistle).minFrequency >= 300.0);
	CHECK(PitchOptionsFor(Source::Voice).maxFrequency <= 1100.0);
	CHECK(std::string(SourceName(Source::Voice)) == "humming or singing");
}

void TestWavRoundTrip()
{
	Synth s;
	s.Tone(440.0, 0.2);
	const std::string path = "transcribe_test_roundtrip.wav";
	std::string error;
	CHECK(WriteWav(path, s.audio, &error));
	Audio back;
	CHECK(ReadWav(path, back, &error));
	CHECK(back.sampleRate == RATE);
	CHECK(back.samples.size() == s.audio.samples.size());
	double worst = 0.0;
	for (size_t i = 0; i < back.samples.size() && i < s.audio.samples.size(); ++i)
		worst = std::max(worst, std::fabs(double(back.samples[i]) - s.audio.samples[i]));
	CHECK(worst <= 0.5 / 32768.0 + 1e-7);	// rounding error only
	std::remove(path.c_str());
	CHECK(!ReadWav("does_not_exist.wav", back, &error));
}

void TestTextModule()
{
	std::vector<GridNote> melody(3);
	melody[0].row = 0; melody[0].length = 4; melody[0].midi = 69;
	melody[1].row = 4; melody[1].length = 2; melody[1].midi = 72;
	melody[2].row = 8; melody[2].length = 3; melody[2].midi = 67;
	std::vector<GridHit> drums(2);
	drums[0].row = 0; drums[0].drum = Drum::Kick; drums[0].volume = 15;
	drums[1].row = 2; drums[1].drum = Drum::Snare; drums[1].volume = 12;

	TextModuleOptions o;
	o.patternLength = 16;
	o.title = "A title that is much longer than thirty-one characters";
	const std::string text = WriteTextModule(melody, drums, o);
	std::string error;
	const bool valid = ValidateTextModule(text, &error);
	if (!valid) std::printf("  validator: %s\n", error.c_str());
	CHECK(valid);

	CHECK(text.find("EXPANSION       1\n") != std::string::npos);
	CHECK(text.find("COLUMNS : 1 1 1 1 1 1 1 1\n") != std::string::npos);
	CHECK(text.find("INSTVRC6   0") != std::string::npos);
	// The lead holds at 10 (release point 5) until the note release fades it out
	CHECK(text.find("MACROVRC6   0   0  -1   5   0 : 15 14 13 12 11 10 7 4 2 0\n") != std::string::npos);
	CHECK(text.find("INST2A03   1") != std::string::npos);
	CHECK(text.find("TRACK  16   6 150") != std::string::npos);
	CHECK(text.find("ORDER 00 : 00 00 00 00 00 00 00 00\n") != std::string::npos);
	CHECK(text.find("TITLE           \"A title that is much longer tha\"\n") != std::string::npos);	// cut to 31 chars

	CHECK(CellOf(text, 0, 0, 5) == "A-3 00 . ...");
	CHECK(CellOf(text, 0, 4, 5) == "C-4 00 . ...");	// next note starts where the first ends: no note-off
	CHECK(CellOf(text, 0, 6, 5) == "=== .. . ...");	// note release after the 2-row note
	CHECK(CellOf(text, 0, 8, 5) == "G-3 00 . ...");
	CHECK(CellOf(text, 0, 11, 5) == "=== .. . ...");
	CHECK(CellOf(text, 0, 7, 5) == "... .. . ...");
	CHECK(CellOf(text, 0, 0, 3) == "4-# 01 F ...");	// kick on the noise channel
	CHECK(CellOf(text, 0, 2, 3) == "A-# 02 C ...");	// snare
	CHECK(CellOf(text, 0, 0, 0) == "... .. . ...");

	// Notes spill over into further frames
	std::vector<GridNote> later(1);
	later[0].row = 40; later[0].length = 8; later[0].midi = 69;
	TextModuleOptions o2;
	o2.patternLength = 16;
	o2.expansion = Expansion::None;
	o2.melodyChannel = 0;
	const std::string text2 = WriteTextModule(later, std::vector<GridHit>(), o2);
	CHECK(ValidateTextModule(text2, &error));
	CHECK(text2.find("ORDER 03 : 03 00 00 00 00\n") != std::string::npos);	// release on row 48 = frame 3
	CHECK(CellOf(text2, 2, 8, 0) == "A-3 00 . ...");
	CHECK(CellOf(text2, 3, 0, 0) == "=== .. . ...");
	CHECK(text2.find("INST2A03   0     0  -1  -1  -1   0") != std::string::npos);
	CHECK(text2.find("MACROVRC6") == std::string::npos);
	CHECK(text2.find("MACRO       0   0  -1   5   0 : 15 14 13 12 11 10 7 4 2 0\n") != std::string::npos);

	// A song from several recordings: lead on VRC6 pulse 1, bass on the sawtooth, drums
	{
		std::vector<MelodyPart> parts(2);
		parts[0].channel = 5;
		parts[0].notes.resize(1);
		parts[0].notes[0].row = 0; parts[0].notes[0].length = 4; parts[0].notes[0].midi = 69;
		parts[1].channel = 7;
		parts[1].instrument = 4;
		parts[1].name = "Bass";
		parts[1].notes.resize(2);
		parts[1].notes[0].row = 0; parts[1].notes[0].length = 8; parts[1].notes[0].midi = 45;
		parts[1].notes[1].row = 20; parts[1].notes[1].length = 4; parts[1].notes[1].midi = 43;
		std::vector<GridHit> beat(1);
		beat[0].row = 4; beat[0].drum = Drum::Kick; beat[0].volume = 15;
		TextModuleOptions o3;
		o3.patternLength = 16;
		const std::string song = WriteTextModule(parts, beat, o3);
		const bool songValid = ValidateTextModule(song, &error);
		if (!songValid) std::printf("  validator: %s\n", error.c_str());
		CHECK(songValid);
		CHECK(CellOf(song, 0, 0, 5) == "A-3 00 . ...");
		CHECK(CellOf(song, 0, 4, 5) == "=== .. . ...");
		CHECK(CellOf(song, 0, 0, 7) == "A-1 04 . ...");	// MIDI 45 on the sawtooth, instrument 04
		CHECK(CellOf(song, 0, 8, 7) == "=== .. . ...");
		CHECK(CellOf(song, 1, 4, 7) == "G-1 04 . ...");	// row 20 = frame 1, row 4
		CHECK(CellOf(song, 1, 8, 7) == "=== .. . ...");
		CHECK(CellOf(song, 0, 4, 3) == "4-# 01 F ...");
		CHECK(song.find("INSTVRC6   0     0  -1  -1  -1   0 \"Lead\"\n") != std::string::npos);
		CHECK(song.find("INSTVRC6   4     0  -1  -1  -1  -1 \"Bass\"\n") != std::string::npos);	// no duty on the sawtooth
		CHECK(song.find("ORDER 01 : 00 00 00 01 00 01 00 01\n") != std::string::npos);
		CHECK(song.find("MACROVRC6   4   0  -1  -1   0 : 3\n") != std::string::npos);
	}
	{
		// 2A03 only: lead on pulse 1, bass on the triangle
		std::vector<MelodyPart> parts(2);
		parts[0].channel = 0;
		parts[0].notes = melody;
		parts[1].channel = 2;
		parts[1].instrument = 4;
		parts[1].name = "Bass";
		parts[1].notes = melody;
		TextModuleOptions o4;
		o4.expansion = Expansion::None;
		o4.patternLength = 16;
		const std::string song = WriteTextModule(parts, std::vector<GridHit>(), o4);
		CHECK(ValidateTextModule(song, &error));
		CHECK(song.find("MACRO       0   0  -1   5   0 : 15 14 13 12 11 10 7 4 2 0\n") != std::string::npos);
		CHECK(song.find("MACRO       4   0  -1  -1   0 : 1\n") != std::string::npos);
		CHECK(song.find("INST2A03   4     0  -1  -1  -1  -1 \"Bass\"\n") != std::string::npos);
		CHECK(song.find("MACROVRC6") == std::string::npos);
		CHECK(CellOf(song, 0, 0, 2) == "A-3 04 . ...");
	}
	{
		// Only the sawtooth plays: no duty macro is written
		std::vector<MelodyPart> parts(1);
		parts[0].channel = 7;
		parts[0].notes = melody;
		const std::string song = WriteTextModule(parts, std::vector<GridHit>(), TextModuleOptions());
		CHECK(ValidateTextModule(song, &error));
		CHECK(song.find("MACROVRC6   4") == std::string::npos);
		CHECK(song.find("INSTVRC6   0     0  -1  -1  -1  -1 \"Lead\"\n") != std::string::npos);
	}

	// The validator rejects what FamiTracker's importer would reject
	std::string bad = text;
	bad.replace(bad.find("A-3 00"), 3, "A3 ");
	CHECK(!ValidateTextModule(bad, &error));
	bad = text;
	bad.replace(bad.find("COLUMNS : 1 1 1 1 1 1 1 1"), 25, "COLUMNS : 1 1 1 1 1 1 1");
	CHECK(!ValidateTextModule(bad, &error));
	bad = text;
	bad.replace(bad.find("TRACK  16   6 150"), 17, "TRACK  16   6 999");
	CHECK(!ValidateTextModule(bad, &error));
	CHECK(!ValidateTextModule(text + "BOGUS 1\n", &error));
	CHECK(!ValidateTextModule("# nothing\n", &error));
}

void TestEndToEnd()
{
	// Whistle -> WAV file -> notes -> text module, as wav2ftm does it
	Synth s;
	s.Silence(0.4);
	s.Tone(MidiToFrequency(91), 0.6, 0.3, true, 20.0, 5.0, 1, 0.02);	// G6
	s.Tone(MidiToFrequency(95), 0.3, 0.3, true, 20.0, 5.0, 1, 0.02);	// B6
	s.Silence(0.1);
	s.Tone(MidiToFrequency(98), 1.6, 0.3, true, 20.0, 5.0, 1, 0.02);	// D7, held
	s.Silence(0.4);
	const std::string path = "transcribe_test_whistle.wav";
	std::string error;
	CHECK(WriteWav(path, s.audio, &error));
	Audio audio;
	CHECK(ReadWav(path, audio, &error));
	std::remove(path.c_str());

	const auto t0 = std::chrono::steady_clock::now();
	std::vector<Note> notes = Transcribe(audio);
	const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	std::printf("  end-to-end: %.1f s of audio analysed in %.0f ms\n", audio.Duration(), ms);

	Transpose(notes, OctaveShiftFor(notes, 67));
	TextModuleOptions o;
	const std::vector<GridNote> rows = Quantize(notes, o.grid, QuantizeOptions());
	CHECK(rows.size() == 3);
	if (rows.size() == 3) {
		CHECK(FamiTrackerNoteName(rows[0].midi) == "G-3");
		CHECK(FamiTrackerNoteName(rows[1].midi) == "B-3");
		CHECK(FamiTrackerNoteName(rows[2].midi) == "D-4");
		CHECK(std::abs(rows[2].length - 16) <= 1);	// the held note stays long
	}
	const std::string text = WriteTextModule(rows, std::vector<GridHit>(), o);
	CHECK(ValidateTextModule(text, &error));
}

}	// namespace

int main()
{
	TestGridAndNames();
	TestScales();
	TestFastDifferenceFunction();
	TestPitchAccuracy();
	TestNoteLengths();
	TestQuietEdges();
	TestMelodies();
	TestQuantizeRules();
	TestHits();
	TestGuessSource();
	TestWavRoundTrip();
	TestTextModule();
	TestEndToEnd();
	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}

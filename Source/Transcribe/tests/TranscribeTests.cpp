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

// Pitch frames for DescribeExpression tests: 5 ms apart, pitch and level from functions of time
template <class PitchAt, class RmsAt>
std::vector<PitchFrame> MakeFrames(double seconds, PitchAt pitchAt, RmsAt rmsAt)
{
	std::vector<PitchFrame> frames;
	for (double t = 0.0025; t < seconds; t += 0.005) {
		PitchFrame f;
		f.time = t;
		f.frequency = pitchAt(t);
		f.aperiodicity = f.frequency > 0.0 ? 0.05 : 1.0;
		f.rms = rmsAt(t);
		frames.push_back(f);
	}
	return frames;
}

Note MakeNote(double start, double end, double pitch)
{
	Note n;
	n.start = start;
	n.end = end;
	n.pitch = pitch;
	n.midi = int(std::lround(pitch));
	return n;
}

void TestExpression()
{
	const auto steady = [](double) { return 0.1; };
	{
		// A scoop: the pitch glides up from 67 to 72 in 0.1 s (segmented into two short
		// pieces) and holds. It becomes one note sliding up 4 semitones.
		std::vector<Note> notes;
		notes.push_back(MakeNote(0.00, 0.05, 68.25));
		notes.push_back(MakeNote(0.05, 0.10, 70.75));
		notes.push_back(MakeNote(0.10, 0.60, 72.0));
		const auto scoop = [](double t) { return MidiToFrequency(t < 0.1 ? 67.0 + 50.0 * t : 72.0); };
		ExpressionOptions o;
		o.gentleVibrato = false;
		DescribeExpression(notes, MakeFrames(0.7, scoop, steady), o);
		CHECK(notes.size() == 1);
		if (notes.size() == 1) {
			CHECK(notes[0].midi == 72 && notes[0].slideSemitones == -4);
			CHECK_NEAR(notes[0].start, 0.0, 1e-9);
			CHECK_NEAR(notes[0].slideSeconds, 0.10, 1e-9);
		}
	}
	{
		// A short note that holds its pitch before the next one is part of the melody
		std::vector<Note> notes;
		notes.push_back(MakeNote(0.00, 0.12, 69.0));
		notes.push_back(MakeNote(0.12, 0.60, 72.0));
		const auto steps = [](double t) { return MidiToFrequency(t < 0.12 ? 69.0 : 72.0); };
		DescribeExpression(notes, MakeFrames(0.7, steps, steady), ExpressionOptions());
		CHECK(notes.size() == 2);
	}
	{
		// A short note that doesn't touch the next one stays a note of its own
		std::vector<Note> notes;
		notes.push_back(MakeNote(0.00, 0.08, 69.0));
		notes.push_back(MakeNote(0.15, 0.60, 72.0));
		const std::vector<PitchFrame> frames = MakeFrames(0.7, [](double t) { return MidiToFrequency(t < 0.1 ? 69 : 72); }, steady);
		DescribeExpression(notes, frames, ExpressionOptions());
		CHECK(notes.size() == 2);
		if (notes.size() == 2)
			CHECK(notes[1].slideSemitones == 0);
	}
	{
		// Louder notes play louder: 6 dB quieter is two steps down from F
		std::vector<Note> notes;
		notes.push_back(MakeNote(0.0, 0.3, 69.0));
		notes.push_back(MakeNote(0.4, 0.7, 71.0));
		const std::vector<PitchFrame> frames = MakeFrames(0.8, [](double) { return 440.0; },
			[](double t) { return t < 0.35 ? 0.1 : 0.05; });
		DescribeExpression(notes, frames, ExpressionOptions());
		CHECK(notes.size() == 2);
		if (notes.size() == 2) {
			CHECK(notes[0].volume == 15);
			CHECK(notes[1].volume == 13);
		}
		ExpressionOptions plain;
		plain.dynamics = false;
		DescribeExpression(notes, frames, plain);
		CHECK(notes[0].volume == -1);
	}
	{
		// A performed vibrato (30 cents at 5.5 Hz) is found; a steady long note gets the
		// gentle one; a short note none
		std::vector<Note> notes;
		notes.push_back(MakeNote(0.0, 1.0, 69.0));
		notes.push_back(MakeNote(1.1, 2.1, 72.0));
		notes.push_back(MakeNote(2.2, 2.5, 74.0));
		const std::vector<PitchFrame> frames = MakeFrames(2.6, [](double t) {
			if (t < 1.0) return 440.0 * std::pow(2.0, 30.0 * std::sin(2.0 * PI * 5.5 * t) / 1200.0);
			return MidiToFrequency(t < 2.15 ? 72 : 74);
		}, steady);
		DescribeExpression(notes, frames, ExpressionOptions());
		CHECK(notes.size() == 3);
		if (notes.size() == 3) {
			CHECK_NEAR(notes[0].vibratoHz, 5.5, 0.5);
			CHECK_NEAR(notes[0].vibratoCents, 30.0, 8.0);
			CHECK_NEAR(notes[1].vibratoHz, 5.6, 1e-9);	// gentle
			CHECK_NEAR(notes[1].vibratoDelay, 0.25, 1e-9);
			CHECK(notes[2].vibratoHz == 0.0);
		}
	}
	{
		// Pattern cells: slide up 2 semitones over 0.1 s (Q22), vibrato 461 from row 3, 400 on
		// the last row, release after
		std::vector<GridNote> rows(1);
		rows[0].row = 0;
		rows[0].length = 8;
		rows[0].midi = 72;
		rows[0].volume = 13;
		rows[0].slideSemitones = -2;
		rows[0].slideSeconds = 0.1;
		rows[0].vibratoHz = 5.6;
		rows[0].vibratoCents = 12.0;
		rows[0].vibratoRow = 3;
		const std::vector<MelodyCell> cells = MelodyCells(rows, ChannelVoice::Pulse, 60);
		CHECK(cells.size() == 4);
		if (cells.size() == 4) {
			CHECK(cells[0].kind == MelodyCell::Note && cells[0].row == 0 && cells[0].midi == 70 && cells[0].volume == 13);
			CHECK(cells[0].effect == 'Q' && cells[0].effectParam == 0x22);
			CHECK(cells[1].kind == MelodyCell::Effect && cells[1].row == 3 && cells[1].effect == '4' && cells[1].effectParam == 0x61);
			CHECK(cells[2].kind == MelodyCell::Effect && cells[2].row == 7 && cells[2].effectParam == 0x00);
			CHECK(cells[3].kind == MelodyCell::Release && cells[3].row == 8);
		}
		TextModuleOptions o;
		o.patternLength = 16;
		const std::string text = WriteTextModule(rows, std::vector<GridHit>(), o);
		std::string error;
		CHECK(ValidateTextModule(text, &error));
		CHECK(CellOf(text, 0, 0, 5) == "A#3 00 D Q22");
		CHECK(CellOf(text, 0, 3, 5) == "... .. . 461");
		CHECK(CellOf(text, 0, 7, 5) == "... .. . 400");
		CHECK(CellOf(text, 0, 8, 5) == "=== .. . ...");
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
	// The lead swells in, holds at 10 (release point 8) until the note release fades it out
	CHECK(text.find("MACROVRC6   0   0  -1   8   0 : 11 14 15 15 14 13 12 11 10 7 4 2 0\n") != std::string::npos);
	CHECK(text.find("MACROVRC6   4   0   0  -1   0 : 3 3 3 3 3 3 2 2 2 2 2 2\n") != std::string::npos);	// duty shimmer, looping
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
	CHECK(text2.find("MACRO       0   0  -1   8   0 : 11 14 15 15 14 13 12 11 10 7 4 2 0\n") != std::string::npos);

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
		CHECK(song.find("INSTVRC6   4     4  -1  -1  -1  -1 \"Bass\"\n") != std::string::npos);	// bass envelope, no duty
		CHECK(song.find("ORDER 01 : 00 00 00 01 00 01 00 01\n") != std::string::npos);
		CHECK(song.find("MACROVRC6   0   4  -1   5   0 : 15 14 13 12 11 10 7 4 2 0\n") != std::string::npos);
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
		CHECK(song.find("MACRO       0   0  -1   8   0 : 11 14 15 15 14 13 12 11 10 7 4 2 0\n") != std::string::npos);
		CHECK(song.find("MACRO       0   4  -1   5   0 : 15 14 13 12 11 10 7 4 2 0\n") != std::string::npos);
		CHECK(song.find("MACRO       4   0  -1  -1   0 : 1\n") != std::string::npos);
		CHECK(song.find("INST2A03   4     4  -1  -1  -1  -1 \"Bass\"\n") != std::string::npos);
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
		CHECK(song.find("INSTVRC6   0     4  -1  -1  -1  -1 \"Lead\"\n") != std::string::npos);	// bass envelope on the sawtooth
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

// ---------------------------------------------------------------------------
// Recording inside FamiTracker: click, leak, take timing
// ---------------------------------------------------------------------------

// Adds sound into audio at time (seconds), growing it as needed
void MixAt(Audio &audio, double time, const std::vector<float> &sound, double gain = 1.0)
{
	const size_t at = size_t(std::lround(time * audio.sampleRate));
	if (audio.samples.size() < at + sound.size())
		audio.samples.resize(at + sound.size(), 0.0f);
	for (size_t i = 0; i < sound.size(); ++i)
		audio.samples[at + i] += float(gain * sound[i]);
}

std::vector<float> TapSound(bool bright, double amp)
{
	Synth s;
	s.Tap(bright, amp);
	return s.audio.samples;
}

// The click track as a microphone hears it: delayed and quieter
Audio HeardClicks(const ClickTrack &click, double seconds, double delay, double gain)
{
	std::vector<float> rendered(size_t(seconds * click.sampleRate));
	click.Render(0, rendered.data(), int(rendered.size()));
	Audio audio;
	audio.sampleRate = click.sampleRate;
	MixAt(audio, delay, rendered, gain);
	audio.samples.resize(size_t(seconds * click.sampleRate));
	return audio;
}

ClickTrack TestClick(double beatSeconds, bool along)
{
	ClickTrack click;
	click.sampleRate = RATE;
	click.leadSeconds = 0.3;
	click.beatSeconds = beatSeconds;
	click.beatsPerBar = 4;
	click.countIn = 4;
	click.clickAlong = along;
	return click;
}

std::vector<double> CountInTimes(const ClickTrack &click)
{
	std::vector<double> times;
	for (int k = 0; k < click.countIn; ++k)
		times.push_back(click.BeatTime(k));
	return times;
}

void TestClickTrack()
{
	ClickTrack click = TestClick(0.5, false);
	CHECK(click.BeatSample(0) == 13230);
	CHECK(click.StartSample() == click.BeatSample(4));
	CHECK_NEAR(click.BeatTime(4), 2.3, 1e-9);
	CHECK(click.Clicks(0) && click.Clicks(3) && !click.Clicks(4) && !click.Clicks(-1));
	click.clickAlong = true;
	CHECK(click.Clicks(4) && click.Clicks(100));
	click.clickAlong = false;

	// Rendering in odd-sized blocks gives the same samples as all at once
	const int total = 3 * RATE;
	std::vector<float> whole(total), blocks(total);
	click.Render(0, whole.data(), total);
	for (int at = 0; at < total; at += 1013)
		click.Render(at, blocks.data() + at, std::min(1013, total - at));
	CHECK(whole == blocks);

	// Sound right after each count-in beat and nowhere else
	const long long length = std::llround(ClickTrack::SECONDS * RATE);
	int stray = 0;
	for (int i = 0; i < total; ++i) {
		bool inClick = false;
		for (int k = 0; k < 4; ++k)
			if (i > click.BeatSample(k) && i < click.BeatSample(k) + length)
				inClick = true;
		if (!inClick && whole[i] != 0.0f)
			++stray;
	}
	CHECK(stray == 0);
	for (int k = 0; k < 4; ++k) {
		float peak = 0.0f;
		for (long long i = click.BeatSample(k); i < click.BeatSample(k) + length; ++i)
			peak = std::max(peak, std::fabs(whole[size_t(i)]));
		CHECK(peak > (k == 0 ? 0.3f : 0.2f));	// the bar's first click is the loudest
		CHECK(peak <= float(click.level) + 1e-6f);
	}

	// The first beat of the bar is higher: count rising zero crossings over 20 ms
	auto crossings = [&](int beat) {
		int n = 0;
		const long long from = click.BeatSample(beat) + 10, to = from + RATE / 50;
		for (long long i = from; i < to; ++i)
			if (whole[size_t(i - 1)] < 0.0f && whole[size_t(i)] >= 0.0f)
				++n;
		return n;
	};
	CHECK(std::abs(crossings(0) - 35) <= 1);	// 1760 Hz
	CHECK(std::abs(crossings(1) - 26) <= 1);	// 1320 Hz
}

void TestBeatFor()
{
	Beat b = BeatFor(10.0, 4, 16);
	CHECK(b.rows == 4 && b.perBar == 4 && b.countIn == 4);
	b = BeatFor(10.0, 2, 8);	// 0.2 s is too quick: 4 rows a beat, 2 beats a bar
	CHECK(b.rows == 4 && b.perBar == 2 && b.countIn == 4);
	b = BeatFor(10.0, 3, 12);
	CHECK(b.rows == 3 && b.perBar == 4 && b.countIn == 4);
	b = BeatFor(10.0, 4, 12);	// three beats a bar
	CHECK(b.rows == 4 && b.perBar == 3 && b.countIn == 3);
	b = BeatFor(2.0, 8, 32);	// 4 s a beat is too slow: 2 rows (1 s)
	CHECK(b.rows == 2 && b.perBar == 4 && b.countIn == 4);
	b = BeatFor(10.0, 0, 0);
	CHECK(b.rows == 4 && b.perBar == 4 && b.countIn == 4);
}

void TestClickLeak()
{
	const ClickTrack click = TestClick(0.4, true);
	const std::vector<double> expected = CountInTimes(click);
	{
		// Speakers: the clicks arrive 45 ms late and quieter, over room noise
		Audio audio = HeardClicks(click, 4.0, 0.045, 0.3);
		for (float &v : audio.samples)
			v += float(0.002 * Noise());
		const ClickLeak leak = FindClickLeak(audio, expected);
		CHECK(leak.heard);
		CHECK_NEAR(leak.delay, 0.045, 0.004);
		CHECK(leak.strength > 0.0);
	}
	{
		// Headphones: only room noise
		Audio audio;
		audio.sampleRate = RATE;
		audio.samples.resize(4 * RATE);
		for (float &v : audio.samples)
			v = float(0.002 * Noise());
		CHECK(!FindClickLeak(audio, expected).heard);
	}
	{
		// Tapping along to the count-in is not the click
		Audio audio;
		audio.sampleRate = RATE;
		audio.samples.resize(4 * RATE);
		for (size_t k = 0; k < expected.size(); ++k)
			MixAt(audio, expected[k] + 0.03, TapSound(true, 0.6));
		CHECK(!FindClickLeak(audio, expected).heard);
	}
	{
		// One click heard out of four is not enough
		Audio audio = HeardClicks(click, 4.0, 0.045, 0.3);
		const size_t cut = size_t((expected[1] - 0.05) * RATE);
		std::fill(audio.samples.begin() + std::ptrdiff_t(cut), audio.samples.end(), 0.0f);
		CHECK(!FindClickLeak(audio, expected).heard);
	}

	// Drums on speakers: the clicks during the take are removed, taps stay,
	// even one on a click
	Audio audio = HeardClicks(click, 4.6, 0.045, 0.3);
	const double start = click.BeatTime(4) + 0.045;
	MixAt(audio, start + 0.4 + 0.01, TapSound(true, 0.8));	// on beat 5, with its click
	MixAt(audio, start + 0.6 + 0.01, TapSound(false, 0.8));	// between beats 5 and 6
	MixAt(audio, start + 1.4 + 0.01, TapSound(true, 0.8));	// on beat 8
	const ClickLeak leak = FindClickLeak(audio, expected);
	CHECK(leak.heard);
	std::vector<Hit> hits = DetectHits(audio, HitOptions());
	CHECK(hits.size() >= 11);	// 4 + 4 clicks before beat 8, and the taps
	std::vector<double> along;
	for (int k = click.countIn; k < 12; ++k)
		along.push_back(click.BeatTime(k));
	RemoveClickHits(hits, audio, along, leak);
	Grid grid;	// 10 rows a second: 4 rows a beat
	PlaceHits(hits, start, grid, false);
	std::vector<GridHit> rows = QuantizeHits(hits, grid, false);
	CHECK(rows.size() == 3);
	if (rows.size() == 3)
		CHECK(rows[0].row == 4 && rows[1].row == 6 && rows[2].row == 14);
}

void TestTapDelay()
{
	std::vector<double> expected;
	for (int k = 0; k < 8; ++k)
		expected.push_back(0.3 + 0.6 * k);
	auto taps = [&](int count, double late, double jitter) {
		Audio audio;
		audio.sampleRate = RATE;
		audio.samples.resize(6 * RATE);
		for (int k = 0; k < count; ++k)
			MixAt(audio, expected[size_t(k)] + late + jitter * ((k % 3) - 1), TapSound(k % 2 == 0, 0.7));
		for (float &v : audio.samples)
			v += float(0.002 * Noise());
		return audio;
	};
	double delay = 0.0;
	CHECK(MeasureTapDelay(taps(8, 0.110, 0.012), expected, 0.3, &delay));
	CHECK_NEAR(delay, 0.110, 0.010);
	CHECK(MeasureTapDelay(taps(8, -0.030, 0.010), expected, 0.3, &delay));	// a little ahead
	CHECK_NEAR(delay, -0.030, 0.010);
	CHECK(!MeasureTapDelay(taps(3, 0.110, 0.0), expected, 0.3, &delay));	// most taps missing
	CHECK(!MeasureTapDelay(taps(8, 0.110, 0.080), expected, 0.3, &delay));	// all over the place
}

void TestRowOffset()
{
	std::vector<double> onsets, weights;
	for (int k = 0; k < 8; ++k)
		onsets.push_back(0.1 * k * 3 + 0.02 + 0.004 * ((k % 3) - 1));
	CHECK_NEAR(RowOffset(onsets, weights, 10.0), 0.02, 0.004);
	std::vector<double> early(onsets);
	for (double &t : early)
		t -= 0.04;	// 20 ms early
	CHECK_NEAR(RowOffset(early, weights, 10.0), -0.02, 0.004);
	std::vector<double> late(onsets);
	for (double &t : late)
		t += 0.025;	// 45 ms late
	CHECK_NEAR(RowOffset(late, weights, 10.0), 0.045, 0.004);
	std::vector<double> unclear(onsets);
	for (double &t : unclear)
		t += 0.04;	// 60 ms late is also 40 ms early: left alone
	CHECK(RowOffset(unclear, weights, 10.0) == 0.0);
	std::vector<double> spread(10);
	for (int k = 0; k < 10; ++k)
		spread[k] = 0.1 * k + 0.01 * k;	// all over the row
	CHECK(RowOffset(spread, weights, 10.0) == 0.0);
	CHECK(RowOffset(std::vector<double>(onsets.begin(), onsets.begin() + 3), weights, 10.0) == 0.0);
}

void TestPlaceNotes()
{
	Grid grid;	// 10 rows a second
	const double start = 2.0;
	const int rows[] = { 0, 4, 6, 8, 12 };
	auto take = [&](double late) {
		std::vector<Note> notes;
		Note countIn;	// a sound during the count-in
		countIn.start = 1.2;
		countIn.end = 1.5;
		countIn.midi = 60;
		notes.push_back(countIn);
		for (int i = 0; i < 5; ++i) {
			Note n;
			n.start = start + 0.1 * rows[i] + late + 0.008 * ((i % 3) - 1);
			n.end = (i < 4 ? start + 0.1 * rows[i + 1] + late - 0.02 : n.start + 0.35);
			n.midi = 60 + i;
			notes.push_back(n);
		}
		return notes;
	};

	// Played 37-53 ms late: settling puts every note on its row
	std::vector<Note> notes = take(0.045);
	const double shift = PlaceNotes(notes, start, grid, true);
	CHECK_NEAR(shift, 0.045, 0.006);
	std::vector<GridNote> placed = Quantize(notes, grid, QuantizeOptions{ false, 0.15 });
	CHECK(placed.size() == 5);
	for (size_t i = 0; i < placed.size() && i < 5; ++i) {
		CHECK(placed[i].row == rows[i]);
		CHECK(placed[i].midi == 60 + int(i));
	}

	// Without settling, the note 53 ms late lands a row late
	notes = take(0.045);
	CHECK(PlaceNotes(notes, start, grid, false) == 0.0);
	placed = Quantize(notes, grid, QuantizeOptions{ false, 0.15 });
	CHECK(placed.size() == 5 && placed[1].row == 4 && placed[2].row == 7);

	// A note that starts a little early starts on row 0; one well before is dropped
	notes = take(0.0);
	notes[1].start = start - 0.07;
	notes.push_back(Note());
	notes.back().start = start - 0.25;
	notes.back().end = start - 0.12;
	PlaceNotes(notes, start, grid, true);
	placed = Quantize(notes, grid, QuantizeOptions{ false, 0.15 });
	CHECK(placed.size() == 5 && placed[0].row == 0 && placed[0].midi == 60);
}

void TestPlaceHits()
{
	Grid grid;
	const double start = 1.5;
	std::vector<Hit> hits;
	const int rows[] = { 0, 2, 4, 6, 8, 10, 12, 14 };
	Hit early;
	early.time = 0.9;	// count-in
	early.strength = 1.0;
	hits.push_back(early);
	for (int i = 0; i < 8; ++i) {
		Hit h;
		h.time = start + 0.1 * rows[i] + 0.035 + 0.005 * ((i % 3) - 1);
		h.strength = 0.5 + 0.05 * i;
		hits.push_back(h);
	}
	const double shift = PlaceHits(hits, start, grid, true);
	CHECK_NEAR(shift, 0.035, 0.006);
	const std::vector<GridHit> placed = QuantizeHits(hits, grid, false);
	CHECK(placed.size() == 8);
	for (size_t i = 0; i < placed.size() && i < 8; ++i)
		CHECK(placed[i].row == rows[i]);
}

// A whistled take recorded on speakers: count-in clicks in the recording, the
// melody a little late. It lands on the right rows.
void TestTakeEndToEnd()
{
	const ClickTrack click = TestClick(0.4, true);	// 4 rows a beat at 10 rows/s
	const double heardDelay = 0.040, late = 0.025;
	const int rows[] = { 0, 4, 8, 10, 12, 16 };
	const int lengths[] = { 4, 4, 2, 2, 4, 8 };
	const int midis[] = { 84, 86, 88, 86, 84, 91 };	// whistled, two octaves up

	Audio audio = HeardClicks(click, 5.0, heardDelay, 0.25);
	const double start = click.BeatTime(click.countIn) + heardDelay;
	for (int i = 0; i < 6; ++i) {
		Synth s;
		s.Tone(MidiToFrequency(midis[i]), 0.1 * lengths[i] - 0.03, 0.5);
		MixAt(audio, start + late + 0.1 * rows[i], s.audio.samples);
	}
	for (float &v : audio.samples)
		v += float(0.001 * Noise());

	const ClickLeak leak = FindClickLeak(audio, CountInTimes(click));
	CHECK(leak.heard);
	CHECK_NEAR(leak.delay, heardDelay, 0.004);

	std::vector<Note> notes = SegmentNotes(TrackPitch(audio, PitchOptionsFor(Source::Whistle)), NoteOptions());
	Grid grid;
	PlaceNotes(notes, click.BeatTime(click.countIn) + leak.delay, grid, true);
	const std::vector<GridNote> placed = Quantize(notes, grid, QuantizeOptions{ false, 0.15 });
	CHECK(placed.size() == 6);
	for (size_t i = 0; i < placed.size() && i < 6; ++i) {
		CHECK(placed[i].row == rows[i]);
		CHECK(placed[i].midi == midis[i]);
	}
}

// ---------------------------------------------------------------------------
// Backing: key, chords, bass and arpeggios
// ---------------------------------------------------------------------------

std::vector<MelodyNote> Tune(const int *midis, const int *lengths, int count, int startRow = 0)
{
	std::vector<MelodyNote> melody;
	int row = startRow;
	for (int i = 0; i < count; ++i) {
		MelodyNote n;
		n.row = row;
		n.length = lengths[i];
		n.midi = midis[i];
		melody.push_back(n);
		row += lengths[i];
	}
	return melody;
}

// Does the chord sounding at row contain the pitch class?
bool ChordHas(const std::vector<ChordSpan> &chords, int row, int pitchClass)
{
	for (size_t i = 0; i < chords.size(); ++i) {
		if (row < chords[i].row || row >= chords[i].row + chords[i].length)
			continue;
		const Chord &c = chords[i].chord;
		const int third = c.quality == Chord::Major ? 4 : 3, fifth = c.quality == Chord::Diminished ? 6 : 7;
		const int pc = ((pitchClass % 12) + 12) % 12;
		return pc == c.root || pc == (c.root + third) % 12 || pc == (c.root + fifth) % 12;
	}
	return false;
}

std::string Progression(const std::vector<ChordSpan> &chords, const Key &key)
{
	std::string s;
	for (size_t i = 0; i < chords.size(); ++i)
		s += (i ? " " : "") + ChordName(chords[i].chord, key);
	return s;
}

void TestKeys()
{
	Key key;
	CHECK(ParseKeyName("Am", key) && key.tonic == 9 && key.minor);
	CHECK(ParseKeyName("Bb", key) && key.tonic == 10 && !key.minor);
	CHECK(ParseKeyName("F#m", key) && key.tonic == 6 && key.minor);
	CHECK(ParseKeyName("C major", key) && key.tonic == 0 && !key.minor);
	CHECK(ParseKeyName("eb minor", key) && key.tonic == 3 && key.minor);
	CHECK(!ParseKeyName("H", key) && !ParseKeyName("Cx", key) && !ParseKeyName("", key));

	Key k;
	k.tonic = 10;
	CHECK(KeyName(k) == "Bb major");
	k.tonic = 6;
	CHECK(KeyName(k) == "F# major");
	k.tonic = 2;
	k.minor = true;
	CHECK(KeyName(k) == "D minor");	// relative of F major: flats, but D has none
	k.tonic = 9;
	CHECK(KeyName(k) == "A minor");

	k.tonic = 0;
	k.minor = false;
	std::string names;
	const std::vector<Chord> major = KeyChords(k);
	for (size_t i = 0; i < major.size(); ++i)
		names += ChordName(major[i], k) + " ";
	CHECK(names == "C Dm Em F G Am ");
	k.tonic = 9;
	k.minor = true;
	names.clear();
	const std::vector<Chord> minor = KeyChords(k);
	for (size_t i = 0; i < minor.size(); ++i)
		names += ChordName(minor[i], k) + " ";
	CHECK(names == "Am Dm Em E F C G ");
	k.tonic = 5;
	k.minor = false;
	Chord bb;
	bb.root = 10;
	CHECK(ChordName(bb, k) == "Bb");
	Chord dim;
	dim.root = 11;
	dim.quality = Chord::Diminished;
	CHECK(ChordName(dim, k) == "Bdim");

	// Finding the key of a melody
	const int scaleC[] = { 60, 62, 64, 65, 67, 69, 71, 72 };
	const int quarters[] = { 4, 4, 4, 4, 4, 4, 4, 8 };
	Key found = FindKey(Tune(scaleC, quarters, 8));
	CHECK(found.tonic == 0 && !found.minor);
	const int harmonicA[] = { 69, 71, 72, 74, 76, 77, 80, 81 };	// A minor with its raised 7th
	found = FindKey(Tune(harmonicA, quarters, 8));
	CHECK(found.tonic == 9 && found.minor);
	const int twinkleG[] = { 67, 67, 74, 74, 76, 76, 74, 72, 72, 71, 71, 69, 69, 67 };
	const int twinkleLengths[] = { 4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 8 };
	found = FindKey(Tune(twinkleG, twinkleLengths, 14));
	CHECK(found.tonic == 7 && !found.minor);
	CHECK(FindKey(std::vector<MelodyNote>()).tonic == 0);
}

void TestBars()
{
	std::vector<Bar> bars = SongBars(0, 128, 64, 16);
	CHECK(bars.size() == 8);
	CHECK(bars.size() == 8 && bars[0].row == 0 && bars[7].row == 112 && bars[7].length == 16);
	bars = SongBars(10, 70, 64, 16);	// the bars that rows 10-69 touch
	CHECK(bars.size() == 5 && bars[0].row == 0 && bars[4].row == 64);
	bars = SongBars(0, 100, 50, 16);	// a bar restarts with every pattern
	CHECK(bars.size() == 8);
	CHECK(bars.size() == 8 && bars[3].row == 48 && bars[3].length == 2 && bars[4].row == 50);
	CHECK(SongBars(0, 0, 64, 16).empty());

	// A melody's bars; a pickup in the second half of a bar starts with the next
	const int notes[] = { 60, 62, 64 };
	const int lengths[] = { 4, 4, 24 };
	bars = MelodyBars(Tune(notes, lengths, 3, 8), 64, 16);	// rows 8-39
	CHECK(bars.size() == 3 && bars[0].row == 0);
	bars = MelodyBars(Tune(notes, lengths, 3, 12), 64, 16);	// rows 12-43: a pickup
	CHECK(bars.size() == 2 && bars[0].row == 16 && bars[1].row == 32);
	CHECK(MelodyBars(std::vector<MelodyNote>(), 64, 16).empty());
}

void TestChooseChords()
{
	// Twinkle, twinkle in C: | C C G G | A A G- | F F E E | D D C- |
	const int twinkle[] = { 60, 60, 67, 67, 69, 69, 67, 65, 65, 64, 64, 62, 62, 60 };
	const int lengths[] = { 4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 8 };
	const std::vector<MelodyNote> melody = Tune(twinkle, lengths, 14);
	const Key key = FindKey(melody);
	CHECK(key.tonic == 0 && !key.minor);
	const std::vector<Bar> bars = SongBars(0, 64, 64, 16);
	HarmonyOptions options;
	const std::vector<ChordSpan> chords = ChooseChords(melody, bars, key, options);
	std::printf("  twinkle: %s\n", Progression(chords, key).c_str());

	// Starts and ends at home, and the chords hold the melody's long notes and downbeats
	CHECK(!chords.empty() && chords.front().row == 0 && chords.front().chord.root == 0);
	CHECK(!chords.empty() && chords.back().chord.root == 0 && chords.back().chord.quality == Chord::Major);
	CHECK(chords.back().row + chords.back().length == 64);
	for (size_t i = 0; i < melody.size(); ++i) {
		if (melody[i].row % 8 == 0 || melody[i].length >= 8)
			CHECK(ChordHas(chords, melody[i].row, melody[i].midi));
	}
	// The spans tile the bars
	for (size_t i = 1; i < chords.size(); ++i)
		CHECK(chords[i].row == chords[i - 1].row + chords[i - 1].length && chords[i].chord != chords[i - 1].chord);
	// One chord a bar when asked
	options.halfBars = false;
	const std::vector<ChordSpan> whole = ChooseChords(melody, bars, key, options);
	for (size_t i = 0; i < whole.size(); ++i)
		CHECK(whole[i].row % 16 == 0 && whole[i].length % 16 == 0);

	// A minor tune ends on its tonic chord, a minor chord
	const int minorTune[] = { 69, 72, 76, 72, 74, 77, 76, 74, 72, 71, 68, 71, 69 };
	const int minorLengths[] = { 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 16 };
	const std::vector<MelodyNote> sad = Tune(minorTune, minorLengths, 13);
	const Key minorKey = FindKey(sad);
	CHECK(minorKey.tonic == 9 && minorKey.minor);
	const std::vector<ChordSpan> sadChords = ChooseChords(sad, SongBars(0, 64, 64, 16), minorKey, HarmonyOptions());
	std::printf("  minor tune: %s\n", Progression(sadChords, minorKey).c_str());
	CHECK(!sadChords.empty() && sadChords.back().chord.root == 9 && sadChords.back().chord.quality == Chord::Minor);
	CHECK(ChordHas(sadChords, 40, 68));	// the G# gets the E major chord
	for (size_t i = 0; i < sad.size(); ++i)
		if (sad[i].row % 16 == 0)
			CHECK(ChordHas(sadChords, sad[i].row, sad[i].midi));

	// No melody in a bar: the chord before carries on
	std::vector<MelodyNote> gap = Tune(twinkle, lengths, 4);	// bar 1 only
	MelodyNote end;
	end.row = 48;
	end.length = 16;
	end.midi = 60;
	gap.push_back(end);
	const std::vector<ChordSpan> held = ChooseChords(gap, SongBars(0, 64, 64, 16), key, HarmonyOptions());
	std::printf("  with a gap: %s\n", Progression(held, key).c_str());
	CHECK(!held.empty() && held.front().chord.root == 0 && held.front().length >= 32);
}

void TestBackingCells()
{
	Key key;	// C major
	const std::vector<Bar> bars = SongBars(0, 64, 64, 16);
	std::vector<ChordSpan> chords;
	const int roots[] = { 0, 5, 7, 0 };
	for (int i = 0; i < 4; ++i) {
		ChordSpan span;
		span.row = 16 * i;
		span.length = 16;
		span.chord.root = roots[i];
		chords.push_back(span);
	}
	HarmonyOptions options;
	const std::vector<MelodyCell> bass = BassCells(chords, bars, options);
	// Root on each bar, fifth halfway, as little movement as possible, a release at the end
	const int bassRows[] = { 0, 8, 16, 24, 32, 40, 48, 56, 64 };
	const int bassNotes[] = { 36, 43, 41, 48, 43, 50, 48, 55 };
	CHECK(bass.size() == 9);
	for (size_t i = 0; i < bass.size() && i < 9; ++i) {
		CHECK(bass[i].row == bassRows[i]);
		if (i < 8)
			CHECK(bass[i].kind == MelodyCell::Note && bass[i].midi == bassNotes[i]);
	}
	CHECK(bass.size() == 9 && bass[8].kind == MelodyCell::Release);
	for (size_t i = 0; i < bass.size(); ++i)
		CHECK(bass[i].kind != MelodyCell::Note || (bass[i].midi >= 36 && bass[i].midi <= 55));

	// Arpeggios: C in root position, then F as C-F-A (0x59, no jump), G as B-D-G (0x38), C again
	const std::vector<MelodyCell> arp = ArpeggioCells(chords, bars);
	CHECK(arp.size() == 5);
	if (arp.size() == 5) {
		CHECK(arp[0].row == 0 && arp[0].midi == 60 && arp[0].effect == '0' && arp[0].effectParam == 0x47);
		CHECK(arp[1].row == 16 && arp[1].midi == 60 && arp[1].effectParam == 0x59);
		CHECK(arp[2].row == 32 && arp[2].midi == 59 && arp[2].effectParam == 0x38);
		CHECK(arp[3].row == 48 && arp[3].midi == 60 && arp[3].effectParam == 0x47);
		CHECK(arp[4].row == 64 && arp[4].kind == MelodyCell::Release && arp[4].effect == '0' && arp[4].effectParam == 0);
	}
	// A chord over two bars is struck again on the second
	std::vector<ChordSpan> long1(1);
	long1[0].row = 0;
	long1[0].length = 32;
	const std::vector<MelodyCell> twice = ArpeggioCells(long1, SongBars(0, 32, 64, 16));
	CHECK(twice.size() == 3 && twice[1].row == 16 && twice[1].midi == twice[0].midi);

	// MelodyOf: volume becomes weight
	std::vector<GridNote> grid(2);
	grid[0].row = 0;
	grid[0].length = 4;
	grid[0].midi = 72;
	grid[0].volume = 15;
	grid[1].row = 4;
	grid[1].length = 2;
	grid[1].midi = 74;
	grid[1].volume = 6;
	const std::vector<MelodyNote> m = MelodyOf(grid);
	CHECK(m.size() == 2 && m[0].weight == 1.0 && std::fabs(m[1].weight - 0.4) < 1e-9 && m[1].row == 4 && m[1].midi == 74);

	// In a text module: chords on VRC6 pulse 2 and bass on the sawtooth
	std::vector<MelodyPart> parts(3);
	parts[0].channel = 5;
	parts[0].notes = std::vector<GridNote>(1, grid[0]);
	parts[1].channel = 7;
	parts[1].instrument = 4;
	parts[1].name = "Bass";
	parts[1].cells = bass;
	parts[2].channel = 6;
	parts[2].instrument = 5;
	parts[2].name = "Chords";
	parts[2].cells = arp;
	parts[2].chords = true;
	TextModuleOptions options2;
	const std::string text = WriteTextModule(parts, std::vector<GridHit>(), options2);
	std::string error;
	CHECK(ValidateTextModule(text, &error));
	CHECK(CellOf(text, 0, 0, 6) == "C-3 05 . 047");
	CHECK(CellOf(text, 0, 16, 6) == "C-3 05 . 059");
	CHECK(CellOf(text, 1, 0, 6) == "=== .. . 000");
	CHECK(CellOf(text, 0, 0, 7) == "C-1 04 . ...");
	CHECK(text.find("MACROVRC6   0   5  -1   7   0 : 10 9 8 8 7 7 6 6 4 2 1 0") != std::string::npos);
	CHECK(text.find("MACROVRC6   4   1  -1  -1   0 : 1") != std::string::npos);
	CHECK(text.find("INSTVRC6   5     5  -1  -1  -1   1 \"Chords\"") != std::string::npos);
}

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
	TestExpression();
	TestHits();
	TestGuessSource();
	TestWavRoundTrip();
	TestTextModule();
	TestEndToEnd();
	TestClickTrack();
	TestBeatFor();
	TestClickLeak();
	TestTapDelay();
	TestRowOffset();
	TestPlaceNotes();
	TestPlaceHits();
	TestTakeEndToEnd();
	TestKeys();
	TestBars();
	TestChooseChords();
	TestBackingCells();
	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}

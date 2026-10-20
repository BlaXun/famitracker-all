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

#pragma once

//
// Audio-to-notes transcription for the recorder ("whistle it, get notes").
//
// Plain C++14 with no Windows or MFC dependencies, so it builds both inside
// FamiTracker and on its own (see CMakeLists.txt in this folder) for tests and
// the wav2ftm command-line tool.
//
// Pipeline:
//   ReadWav -> TrackPitch -> SegmentNotes -> (FitOctave, SnapToScale) -> Quantize
//   ReadWav -> DetectHits -> ClassifyHits -> QuantizeHits
//
// A note keeps the length of the sound that produced it: a long hum becomes a
// long note, ended by a note release where the hum stops.
//

#include <string>
#include <vector>

namespace transcribe {

// ---------------------------------------------------------------------------
// Audio input
// ---------------------------------------------------------------------------

// Mono audio, samples in [-1, 1].
struct Audio {
	std::vector<float> samples;
	int sampleRate = 0;
	double Duration() const { return sampleRate > 0 ? double(samples.size()) / sampleRate : 0.0; }
};

// Reads PCM (8/16/24/32-bit) or IEEE float (32/64-bit) WAV files, downmixing
// to mono. Returns false and sets *error on failure.
bool ReadWav(const std::string &path, Audio &out, std::string *error);

// Writes 16-bit mono PCM. Used by tests and tools.
bool WriteWav(const std::string &path, const Audio &audio, std::string *error);

// ---------------------------------------------------------------------------
// Pitch tracking (YIN, de Cheveigne & Kawahara 2002)
// ---------------------------------------------------------------------------

struct PitchOptions {
	double minFrequency = 70.0;		// Hz, low hum
	double maxFrequency = 4200.0;	// Hz, high whistle
	double threshold = 0.15;		// YIN absolute threshold on the normalized difference
	double windowSeconds = 0.020;	// integration window (at least one period of minFrequency is used)
	double hopSeconds = 0.005;		// analysis step
};

struct PitchFrame {
	double time = 0.0;		// seconds, centre of the analysis window
	double frequency = 0.0;	// Hz; 0 when no periodic sound was found
	double aperiodicity = 1.0;	// YIN's normalized difference at the chosen lag, 0 = perfectly periodic
	double rms = 0.0;		// signal level over the window
};

std::vector<PitchFrame> TrackPitch(const Audio &audio, const PitchOptions &options);

// ---------------------------------------------------------------------------
// Notes
// ---------------------------------------------------------------------------

struct NoteOptions {
	double maxAperiodicity = 0.25;	// frames above this are not pitched
	double silenceDb = -35.0;		// frames this far below the loudest frame are silence
	double floorDb = -60.0;			// absolute silence floor (dBFS)
	double minNoteSeconds = 0.060;	// shorter notes are dropped
	double maxGapSeconds = 0.040;	// shorter dropouts inside a note are bridged
	double changeSemitones = 0.6;	// pitch must move this far to start a new note...
	double changeSeconds = 0.035;	// ...for at least this long (keeps vibrato and glides together)
	// A note keeps the quieter start and end of its tone (fade-in, decaying tail), even
	// below the silence gate, while those frames are
	double edgeAperiodicity = 0.35;	// ...still this periodic,
	double edgeDb = -30.0;			// ...no further than this below the note's loudest frame,
	double edgeNoiseMarginDb = 6.0;	// ...and at least this far above the noise floor.
};

struct Note {
	double start = 0.0;		// seconds
	double end = 0.0;		// seconds
	double pitch = 0.0;		// MIDI note number (69 = A4 = 440 Hz), fractional
	int midi = 0;			// rounded pitch, after any octave fit or scale snap
};

std::vector<Note> SegmentNotes(const std::vector<PitchFrame> &frames, const NoteOptions &options);

double FrequencyToMidi(double frequency);

// Whole octaves (multiple of 12 semitones) to add so the median note lands
// nearest targetMidi. People whistle about two octaves above where a melody
// usually sits.
int OctaveShiftFor(const std::vector<Note> &notes, int targetMidi);
void Transpose(std::vector<Note> &notes, int semitones);

enum class Scale { Chromatic, Major, Minor, MajorPentatonic, MinorPentatonic };

bool ParseScale(const std::string &name, Scale &scale);
bool ParseKey(const std::string &name, int &root);	// "C", "F#", "Bb" -> 0..11
int SnapToScale(int midi, int root, Scale scale);
void SnapToScale(std::vector<Note> &notes, int root, Scale scale);

// ---------------------------------------------------------------------------
// Grid (FamiTracker rows)
// ---------------------------------------------------------------------------

struct Grid {
	int tempo = 150;
	int speed = 6;
	int frameRate = 60;		// NTSC
	// Mirrors CSoundGen's tempo accumulator: F * floor(24T/S) / (60F - (24T mod S)).
	double RowsPerSecond() const;
};

struct GridNote {
	int row = 0;		// first row
	int length = 1;		// rows until the note ends (release) or the next note starts
	int midi = 0;
};

struct QuantizeOptions {
	bool alignToStart = true;		// shift everything so the first note lands on row 0
	double minRestSeconds = 0.15;	// a shorter break between two notes is held through
									// (the first note lasts until the second starts);
									// 0 keeps every break as a rest
};

// Puts notes on rows. A note starts on its nearest row and keeps its rounded
// duration, so a short note doesn't lose half its length to rounding. One note
// plays at a time: a note that would overlap the next one ends where the next
// one starts.
std::vector<GridNote> Quantize(const std::vector<Note> &notes, const Grid &grid, const QuantizeOptions &options);

// ---------------------------------------------------------------------------
// Percussive hits (finger taps, beatboxing)
// ---------------------------------------------------------------------------

struct HitOptions {
	double frameSeconds = 0.010;	// energy frame
	double hopSeconds = 0.0025;
	double minIntervalSeconds = 0.070;	// two hits closer than this are one hit
	double sensitivityDb = 9.0;		// energy must jump this much above the local level
	double silenceDb = -40.0;		// hits quieter than this below the loudest hit are ignored
};

enum class Drum { Kick, Snare, HiHat };

struct Hit {
	double time = 0.0;		// seconds
	double strength = 0.0;	// peak level (linear)
	double brightness = 0.0;	// high-frequency share of the attack, higher = brighter
	Drum drum = Drum::Snare;
};

std::vector<Hit> DetectHits(const Audio &audio, const HitOptions &options);

// Sorts hits into kick/snare/hi-hat by brightness when the recording has clearly
// different sounds; otherwise every hit becomes a snare. Returns the number of
// classes used (1-3).
int ClassifyHits(std::vector<Hit> &hits);

struct GridHit {
	int row = 0;
	Drum drum = Drum::Snare;
	int volume = 15;	// 0-15, from relative strength
};

std::vector<GridHit> QuantizeHits(const std::vector<Hit> &hits, const Grid &grid, bool alignToStart);

// ---------------------------------------------------------------------------
// FamiTracker text module (File > Import Text in FamiTracker 0.4.6)
// ---------------------------------------------------------------------------

enum class Expansion { None = 0, VRC6 = 1 };

struct TextModuleOptions {
	std::string title = "Recording";
	std::string comment;
	Expansion expansion = Expansion::VRC6;
	int melodyChannel = 5;	// channel index: 0-4 = 2A03 (3 = noise), 5-7 = VRC6
	int patternLength = 64;
	Grid grid;
};

// Note name in FamiTracker's spelling, e.g. MIDI 69 (440 Hz) -> "A-3".
// FamiTracker's octave numbers are one below scientific pitch notation.
std::string FamiTrackerNoteName(int midi);

int ChannelCount(Expansion expansion);

std::string WriteTextModule(const std::vector<GridNote> &melody, const std::vector<GridHit> &drums,
	const TextModuleOptions &options);

// One melody line of a song made from several recordings (lead, bass, ...).
struct MelodyPart {
	int channel = 5;		// channel index, as TextModuleOptions::melodyChannel
	int instrument = 0;		// instrument slot; 1-3 are taken by the drums
	std::string name = "Lead";	// instrument name
	std::vector<GridNote> notes;
};

// Several melody parts, each on its own channel, plus drums on the noise channel.
std::string WriteTextModule(const std::vector<MelodyPart> &parts, const std::vector<GridHit> &drums,
	const TextModuleOptions &options);

// Checks text against the rules of FamiTracker 0.4.6's text importer
// (CTextExport::ImportFile). Returns true if FamiTracker would accept it.
bool ValidateTextModule(const std::string &text, std::string *error);

}	// namespace transcribe

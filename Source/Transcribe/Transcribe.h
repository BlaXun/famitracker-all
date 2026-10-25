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
// A take recorded in FamiTracker against a click is lined up with PlaceNotes
// or PlaceHits before quantizing.
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

// The left and right sides of a stereo WAV file (its first two channels; a
// mono file's one channel on both), in the formats ReadWav reads.
bool ReadWavSides(const std::string &path, Audio &left, Audio &right, std::string *error);

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
// What a recording holds
// ---------------------------------------------------------------------------

enum class Source { Whistle, Voice, Taps };	// Voice: humming or singing

// Pitch range to listen in: 300-4200 Hz for whistling, 70-1100 Hz for a
// voice. A narrower range avoids octave mistakes. Taps get the full range.
PitchOptions PitchOptionsFor(Source source);

const char *SourceName(Source source);	// "whistling", "humming or singing", ...

struct NoteOptions;

struct SourceGuess {
	Source source = Source::Whistle;
	double pitchedShare = 0.0;		// share of the sounding frames that have a pitch
	double medianFrequency = 0.0;	// Hz, over the pitched frames
	double overtoneShare = 0.0;		// energy in overtones 2-5: ~0 for a whistle, 0.3+ for a voice
};

// Guesses what a recording holds. Sounds that are pitched less than half the
// time are taps. Otherwise an almost pure tone (overtone share under 0.1, from
// 300 Hz up) or a median pitch from 900 Hz up is whistling, and the rest a
// voice. The frames come from TrackPitch with the default (full) range.
SourceGuess GuessSource(const Audio &audio, const std::vector<PitchFrame> &frames, const NoteOptions &options);
SourceGuess GuessSource(const Audio &audio);

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
	// Singing with syllables ("la la laaa"): a dip in loudness of at least
	// syllableDipDb between two syllables starts a new note, even on the same
	// pitch, and a short drift of the pitch inside a syllable stays part of
	// its note.
	bool syllables = false;
	double syllableDipDb = 6.0;
};

struct Note {
	double start = 0.0;		// seconds
	double end = 0.0;		// seconds
	double pitch = 0.0;		// MIDI note number (69 = A4 = 440 Hz), fractional
	int midi = 0;			// rounded pitch, after any octave fit or scale snap
	bool articulated = false;	// starts a sung syllable of its own (NoteOptions::syllables)

	// How it was played (DescribeExpression)
	double level = 0.0;			// dB, the note's median frame level
	int volume = -1;			// volume column 0-15, -1 = none
	int slideSemitones = 0;		// starts this far from midi and slides to it (negative: from below)
	double slideSeconds = 0.0;	// how long the slide takes
	double vibratoHz = 0.0;		// vibrato rate, 0 = none
	double vibratoCents = 0.0;	// vibrato depth: half the peak-to-peak swing
	double vibratoDelay = 0.0;	// seconds after the start where the vibrato begins
};

std::vector<Note> SegmentNotes(const std::vector<PitchFrame> &frames, const NoteOptions &options);

// Note options for what a recording holds: singing and humming split notes at
// syllables (NoteOptions::syllables), whistling and taps use the defaults.
NoteOptions NoteOptionsFor(Source source);

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

	// Expression, carried over from the Note
	int volume = -1;			// volume column 0-15, -1 = none
	int slideSemitones = 0;		// starts this far from midi and slides to it
	double slideSeconds = 0.0;
	double vibratoHz = 0.0;		// 0 = none
	double vibratoCents = 0.0;
	int vibratoRow = 0;			// rows after the first row where the vibrato begins
};

// Two notes that land on the same row become one, unless the second starts a
// sung syllable (Note::articulated): that one moves to the next row when it is
// free, so quick "la-la"s keep a note each.
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
// Expression: keeping how a melody was played
// ---------------------------------------------------------------------------

struct ExpressionOptions {
	bool dynamics = true;		// louder notes play louder (volume column, 3 dB a step)
	bool slides = true;			// a quick slide into a note stays a slide (Qxy/Rxy)
	bool vibrato = true;		// vibrato that was performed is kept (4xy)
	bool gentleVibrato = true;	// long notes without one get a gentle, delayed vibrato
	double gentleVibratoSeconds = 0.5;	// "long"
};

// Fills in each note's level, volume, slide and vibrato from the pitch frames
// it was segmented from. Short notes that lead straight into a longer one,
// rising or falling towards it, are folded into it as a slide, unless the
// longer one starts a syllable of its own. A scoop that SegmentNotes already
// made part of a note stays its slide; with slides off, notes have none.
void DescribeExpression(std::vector<Note> &notes, const std::vector<PitchFrame> &frames, const ExpressionOptions &options);

// Which period formula a channel uses, for effect speeds and depths
enum class ChannelVoice { Pulse, Triangle, Sawtooth };

// One pattern cell of a melody line
struct MelodyCell {
	enum Kind { Note, Release, Effect } kind = Note;	// Effect: an effect on an otherwise empty row
	int row = 0;
	int midi = 0;			// Note: the pitch to play (where a slide starts)
	int volume = -1;		// 0-15, -1 = none
	char effect = 0;		// FamiTracker effect letter ('Q', 'R' or '4'), 0 = none
	int effectParam = 0;	// 0x00-0xFF
};

// Turns rows of notes into pattern cells, sorted by row: each note, its
// slide (Qxy/Rxy on the note), its vibrato (4xy where it begins, 400 on the
// note's last row) and a release where the sound stops before the next note.
// frameRate is the engine speed (60 for NTSC).
std::vector<MelodyCell> MelodyCells(const std::vector<GridNote> &notes, ChannelVoice voice, int frameRate);

// A take put into a song that it would outrun (rows: the song's rows from the
// take's row 0 on) with nothing past the song's end but the end of its last
// note (its release, and its vibrato's 400): that end goes on the song's last
// row, so the song keeps its length and its loop instead of growing a frame
// for it. A last note starting on that row sounds on into the loop. Takes with
// notes past the end are left as they are (the song grows for them). True
// when the cells changed.
bool EndInside(std::vector<MelodyCell> &cells, int rows);

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
// Recording inside FamiTracker: the click, and lining a take up with rows
// ---------------------------------------------------------------------------

// The click that counts in and keeps time while recording. Beats are counted
// from the first count-in click; row 0 of the take is beat countIn.
struct ClickTrack {
	int sampleRate = 44100;
	double leadSeconds = 0.3;	// silence before the first click
	double beatSeconds = 0.5;
	int beatsPerBar = 4;		// the first beat of each bar clicks higher
	int countIn = 4;			// beats before row 0, a whole number of bars
	bool clickAlong = true;		// keep clicking after the count-in...
	bool barsOnly = false;		// ...on the first beat of each bar only
	double level = 0.25;		// peak level of an accented click, 0-1
	int barPhase = 0;			// the first count-in beat is this many beats into its bar (a take that starts
								// mid-bar keeps the song's bars), so beat k starts a bar when k + barPhase
								// is a multiple of beatsPerBar

	// A soft wooden "tock" rather than a beep: a tone that fades within a
	// few milliseconds, a fifth lower on the beats between bar starts
	static const double HIGH_HZ;	// 1046.5 Hz (C6), the first beat of a bar
	static const double LOW_HZ;		// 784 Hz (G5), the other beats
	static const double SECONDS;	// length of one click (40 ms)

	long long BeatSample(int beat) const;	// where a beat's click starts
	double BeatTime(int beat) const;		// the same in seconds
	long long StartSample() const;			// row 0: BeatSample(countIn)
	bool Clicks(int beat) const;			// does this beat click?
	bool StartsBar(int beat) const;			// is this beat the first of a bar (the higher click)?
	// Writes samples [from, from + count) of the click track to out
	void Render(long long from, float *out, int count) const;
};

// The beat for the click, from the song's row highlights (first: rows per
// beat, second: rows per bar). A beat is kept between 0.25 and 1.5 seconds
// by doubling or halving its rows.
struct Beat {
	int rows = 4;		// rows per beat
	int perBar = 4;		// beats per bar
	int countIn = 4;	// beats to count in: one bar, or two short ones
};
Beat BeatFor(double rowsPerSecond, int firstHighlight, int secondHighlight);

// The click as the microphone heard it, when it plays on speakers rather
// than headphones.
struct ClickLeak {
	bool heard = false;
	double delay = 0.0;		// seconds after the expected times
	double strength = 0.0;	// how loud, on the scale of Hit::strength
};

// Looks for the count-in clicks in a take. expected: when each click should
// be heard, in seconds into the recording; each is looked for within
// +-window seconds. The clicks count as heard when most of them are found,
// sound like the click (a tone at HIGH_HZ or LOW_HZ) and agree on the delay.
ClickLeak FindClickLeak(const Audio &audio, const std::vector<double> &expected, double window = 0.12);

// Measures how late someone's taps along to clicks arrive in a recording,
// sound system and all: for each expected click time (seconds into the
// recording), the nearest hit within +-window. Returns false unless most of
// the taps were found and they agree (the middle half within 60 ms).
bool MeasureTapDelay(const Audio &audio, const std::vector<double> &expected, double window, double *delay);

// Removes hits that are only the click leaking in: within tolerance of a
// click (clickTimes, as expected; the leak's delay is added), sounding like
// the click and not clearly louder than the count-in clicks were.
void RemoveClickHits(std::vector<Hit> &hits, const Audio &audio, const std::vector<double> &clickTimes,
	const ClickLeak &leak, double tolerance = 0.030);

// The shift, in seconds, that lines onsets up with rows best: how late (or,
// negative, how early) they sit on average, from 0.35 rows early to half a
// row late. 0 when there are fewer than four onsets, when they don't agree,
// or when they sit further early than that (which could as well be late).
double RowOffset(const std::vector<double> &onsets, const std::vector<double> &weights, double rowsPerSecond);

// Lines a take up with the rows: start (seconds into the recording) becomes
// time 0, the row the take was recorded at. Notes and hits from before it
// (the count-in) are dropped; ones that start just early (up to a row for a
// note, half a row for a hit) start on row 0. With settle, everything also
// moves by RowOffset, so notes played a little late land on their rows.
// Returns that shift in seconds. Quantize afterwards with alignToStart off.
double PlaceNotes(std::vector<Note> &notes, double start, const Grid &grid, bool settle);
double PlaceHits(std::vector<Hit> &hits, double start, const Grid &grid, bool settle);

// How a take lines up with the rows, for whoever turns it into notes
struct TakeTiming {
	bool lineUpFirstNote = true;	// no click and no song: the first note lands on row 0
	double start = 0.0;				// otherwise: seconds into the take that land on row 0
	ClickLeak leak;					// the click as the microphone heard it, if it did
	std::vector<double> clicks;		// when the clicks after the count-in were expected (seconds)
};

// ---------------------------------------------------------------------------
// Backing: a key, chords and a bass line for a melody
// ---------------------------------------------------------------------------

struct Key {
	int tonic = 0;		// pitch class, 0 = C
	bool minor = false;
};

std::string KeyName(const Key &key);	// "D major", "Bb major", "F# minor"
bool ParseKeyName(const std::string &name, Key &key);	// "Am", "C", "F#m", "Bb minor"

// A melody note to harmonize, on rows
struct MelodyNote {
	int row = 0;
	int length = 1;			// rows
	int midi = 60;
	double weight = 1.0;	// how much it counts, 0-1 (louder notes count more)
};

// The melody of quantized notes: each note's pitch where it lands, its
// volume as weight
std::vector<MelodyNote> MelodyOf(const std::vector<GridNote> &notes);

// The key that fits a melody best: the Krumhansl-Kessler key profiles against
// the melody's pitch classes, weighted by length and loudness. The last note
// and the first one count towards the keys they are the tonic of.
Key FindKey(const std::vector<MelodyNote> &melody);

struct Chord {
	enum Quality { Major, Minor, Diminished };
	int root = 0;		// pitch class
	Quality quality = Major;
	// A chord typed by name or heard in a recording can have other notes than
	// its quality's three: its notes as semitones above the root (0-11, from
	// 0, ascending: { 0, 4, 7, 10 } for C7). Empty: the triad of its quality.
	std::vector<int> tones;
	int bass = -1;		// the pitch class under it when that isn't the root (C/E), -1 for the root

	std::vector<int> Tones() const;		// its notes above the root: tones, or the triad's
	bool operator==(const Chord &other) const
	{
		return root == other.root && quality == other.quality && tones == other.tones && bass == other.bass;
	}
	bool operator!=(const Chord &other) const { return !(*this == other); }
};

// "C", "Am", "Bb", "F#m", "Bdim"; with tones "C7", "Fmaj7", "Gsus4", "Cadd9",
// "Am7", "C5"; and a bass "C/E"
std::string ChordName(const Chord &chord, const Key &key);

// The chords of a key: I ii iii IV V vi in major; i iv v V VI III VII in minor
std::vector<Chord> KeyChords(const Key &key);

// A bar of the song: the rows from row to row + length - 1
struct Bar {
	int row = 0;
	int length = 16;
};

// Bars as FamiTracker highlights them: one starts on the first row of every
// pattern and every barRows rows after that. Rows count from the first row of
// the song (frame * patternLength + row); the bars cover [firstRow, endRow).
std::vector<Bar> SongBars(int firstRow, int endRow, int patternLength, int barRows);

// The bars a melody covers, from the bar of its first note to the bar of its
// last. A first note in the second half of its bar is a pickup: the bars
// start with the next one.
std::vector<Bar> MelodyBars(const std::vector<MelodyNote> &melody, int patternLength, int barRows);

struct HarmonyOptions {
	int rowsPerBeat = 4;
	bool halfBars = true;	// a chord can change halfway through a bar of 2+ beats
};

struct ChordSpan {
	int row = 0;
	int length = 0;
	Chord chord;
};

// Picks a chord of the key for each bar, or half bar: the one whose notes fit
// the melody's notes best (long notes and notes on the beat count most, a
// note a semitone above a chord tone clashes), preferring progressions that
// sound like music (IV-V-I rather than V-IV) and starting and ending on the
// tonic. Bars without melody keep the chord going. Neighbouring bars with the
// same chord come back as one span.
std::vector<ChordSpan> ChooseChords(const std::vector<MelodyNote> &melody, const std::vector<Bar> &bars,
	const Key &key, const HarmonyOptions &options);

// The bass line: the chord's root (or the bass it's written over, C/E) on
// every chord change and bar, and in bars of four or more beats the fifth
// halfway through; a release where the chords end. Around lowMidi-highMidi,
// moving as little as it can.
std::vector<MelodyCell> BassCells(const std::vector<ChordSpan> &chords, const std::vector<Bar> &bars,
	const HarmonyOptions &options, int lowMidi = 36, int highMidi = 55);

// The chords on one channel, as the fast arpeggio of the 0xy effect: struck on
// every chord change and bar, each voiced (root position or an inversion) as
// close as it can to the one before, its lowest note around lowMidi-highMidi.
// Where the chords end: a release with 000, so later notes don't arpeggiate.
// The arpeggio plays three notes (ArpeggioTones), spread over an octave and
// a bit rather than bunched up a step or two apart.
std::vector<MelodyCell> ArpeggioCells(const std::vector<ChordSpan> &chords, const std::vector<Bar> &bars,
	int lowMidi = 53, int highMidi = 65);

// The three notes of a chord the 0xy arpeggio plays, as semitones above the
// root: a triad's own; a power chord's root, fifth and octave; of a bigger
// chord the root, its third (or suspension) and its most telling other note
// (a seventh, a sixth, a ninth, an altered fifth), the plain fifth left out.
std::vector<int> ArpeggioTones(const Chord &chord);

// ---------------------------------------------------------------------------
// Tapping the tempo
// ---------------------------------------------------------------------------

struct TapTempo {
	double bpm = 0.0;		// beats a minute, 0 until enough taps came
	int taps = 0;			// the taps it counted: the last run of them
	double spread = 0.0;	// how unevenly they came: the middle half of the taps' distances from a
							// steady beat, as a share of the beat
};

// The tempo of a key or button tapped along with the beat, from when each tap
// came (seconds, in order). The taps count from the last pause (over two
// seconds, or two and a half beats, without a tap), so tapping again later
// starts over. Each tap is on a beat: a gap about twice the others is a beat
// missed, and a tap far too soon after another (a double tap) is left out.
// The beat is the steady one that fits the taps best, once at least minTaps
// count; 30-300 BPM.
TapTempo TempoOfTaps(const std::vector<double> &times, int minTaps = 4);

// ---------------------------------------------------------------------------
// Strummed chords (a guitar, a piano)
// ---------------------------------------------------------------------------

struct StrumOptions {
	int lowestMidi = 36;			// C2: a guitar's low E is MIDI 40, a piano's left hand goes lower
	int highestMidi = 88;			// E6
	double minIntervalSeconds = 0.10;	// two strums closer than this are one
	double noteShare = 0.15;		// a note counts from this share of the strum's strongest one
	int maxNotes = 6;				// a guitar's six strings
	double silenceDb = -40.0;		// strums this far below the loudest are left out
};

struct StrumNote {
	int midi = 0;
	double weight = 0.0;	// how strongly it sounds, 0-1 (1: the strum's strongest note)
};

struct Strum {
	double time = 0.0;		// seconds, when it was struck
	double end = 0.0;		// seconds: when the next strum comes, or the recording ends
	double level = 0.0;		// dB (full scale), its loudness just after it was struck
	std::vector<StrumNote> notes;	// the notes that sound together, lowest first
};

// The strums in a recording of chords, and the notes heard sounding together
// in each. A strum starts where the sound's spectrum rises suddenly (spectral
// flux); its notes come from the spectrum just after it, explained as notes
// with their overtones (a non-negative fit of each note's harmonics, so a
// note's overtones don't count as notes of their own).
std::vector<Strum> HearStrums(const Audio &audio, const StrumOptions &options = StrumOptions());

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
	std::vector<MelodyCell> cells;	// written as they are instead of notes (BassCells, ArpeggioCells)
	bool chords = false;	// the soft, thin chord sound instead of the lead's
};

// Several melody parts, each on its own channel, plus drums on the noise channel.
std::string WriteTextModule(const std::vector<MelodyPart> &parts, const std::vector<GridHit> &drums,
	const TextModuleOptions &options);

// Checks text against the rules of FamiTracker 0.4.6's text importer
// (CTextExport::ImportFile). Returns true if FamiTracker would accept it.
bool ValidateTextModule(const std::string &text, std::string *error);

}	// namespace transcribe

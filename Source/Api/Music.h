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

// Pattern data in musical terms, for the Claude Code connection: cells as
// FamiTracker stores them, note names, bars and beats, tempo, keys and modes,
// reading a channel's notes and writing new ones into a range of rows.
// Plain C++14, independent of MFC, so it is tested on Linux.

#include <string>
#include <utility>
#include <vector>

namespace ftapi {

// Cell values as in FamiTrackerTypes.h (McpConnection.cpp checks they match)
const int NOTE_NONE = 0;		// 1-12: C to B
const int NOTE_RELEASE = 13;	// ===
const int NOTE_HALT = 14;		// ---
const int NO_INSTRUMENT = 64;	// MAX_INSTRUMENTS
const int NO_VOLUME = 16;		// MAX_VOLUME
const int EFFECT_COLUMNS = 4;	// MAX_EFFECT_COLUMNS
const int PITCHES = 96;			// C-0 to B-7
const int NOISE_PITCHES = 16;	// 0-# to F-#

// effect_t, in the same order
enum effect_id {
	EFF_NONE = 0,
	EFF_SPEED, EFF_JUMP, EFF_SKIP, EFF_HALT, EFF_VOLUME, EFF_PORTAMENTO, EFF_PORTAOFF, EFF_SWEEPUP, EFF_SWEEPDOWN,
	EFF_ARPEGGIO, EFF_VIBRATO, EFF_TREMOLO, EFF_PITCH, EFF_DELAY, EFF_DAC, EFF_PORTA_UP, EFF_PORTA_DOWN,
	EFF_DUTY_CYCLE, EFF_SAMPLE_OFFSET, EFF_SLIDE_UP, EFF_SLIDE_DOWN, EFF_VOLUME_SLIDE, EFF_NOTE_CUT,
	EFF_RETRIGGER, EFF_DELAYED_VOLUME, EFF_FDS_MOD_DEPTH, EFF_FDS_MOD_SPEED_HI, EFF_FDS_MOD_SPEED_LO,
	EFF_DPCM_PITCH, EFF_SUNSOFT_ENV_LO, EFF_SUNSOFT_ENV_HI, EFF_SUNSOFT_ENV_TYPE,
	EFF_COUNT
};

struct Cell {
	unsigned char Note;			// NOTE_NONE, 1-12, NOTE_RELEASE, NOTE_HALT
	unsigned char Octave;
	unsigned char Instrument;	// 0-63, NO_INSTRUMENT
	unsigned char Volume;		// 0-15, NO_VOLUME
	unsigned char Effect[EFFECT_COLUMNS];	// effect_id
	unsigned char Param[EFFECT_COLUMNS];

	Cell();
	bool IsEmpty() const;
	bool HasNote() const { return Note >= 1 && Note <= 12; }
	bool IsEnd() const { return Note == NOTE_RELEASE || Note == NOTE_HALT; }
	int Pitch() const;			// Octave * 12 + Note - 1; -1 without a note
	void SetPitch(int Pitch);
	bool operator==(const Cell &Other) const;
	bool operator!=(const Cell &Other) const { return !(*this == Other); }
};

// What a channel plays
enum channel_kind { KIND_PULSE, KIND_TRIANGLE, KIND_NOISE, KIND_DPCM, KIND_SAWTOOTH, KIND_OTHER, KIND_FM };	// KIND_FM: VRC7's

// Names. Pitches are FamiTracker's: 0 = C-0 ... 95 = B-7, where C-3 is middle
// C (MIDI 60) and A-3 is 440 Hz. Noise pitches are 0-15 (0-# to F-#).
std::string PitchName(int Pitch);			// "C-4", "C#4"
std::string NoisePitchName(int Pitch);		// "4-#"
bool ParsePitch(const std::string &Text, int &Pitch);	// "C-4", "C4", "c#4", "Db4", "Bb-3"
// Noise: "kick", "snare", "hihat", "open hihat", "crash", "high tom", "mid tom", "low tom" (Drum is then the
// preset that plays it: "kick", "snare", "hi-hat", "open hi-hat", "crash" or "tom"), "4-#", "A-#", "noise 10"
bool ParseNoisePitch(const std::string &Text, int &Pitch, std::string &Drum);
char EffectLetter(int Effect);				// 0 for none
std::string EffectText(int Effect, int Param);	// "421", "Q31"
bool ParseEffect(const std::string &Text, int &Effect, int &Param);	// "421", "Q31", "0 47"
std::string Lowercase(const std::string &Text);
std::string FormatNumber(double Value);		// "1", "1.5", "0.25"

// Effects that keep working after their row until turned off: arpeggio,
// portamento up and down, portamento, vibrato, tremolo and volume slide (with
// a 00 parameter), and fine pitch (with P80). Speed, jump, skip and halt
// steer the song instead of the channel; those stay where they are when
// notes are written.
bool IsStickyEffect(int Effect);
int StickyOff(int Effect);	// the parameter that turns a sticky effect off: 0x80 for fine pitch, 0 for the others
bool IsSongEffect(int Effect);

// Bars and beats as FamiTracker's row highlight shows them: a bar starts on
// the first row of every frame and every RowsPerBar rows after that (the last
// one in a frame can be shorter). Bars and beats count from 1. Rows count
// from the start of the song (frame * PatternLength + row).
struct Timing {
	int PatternLength;
	int Frames;
	int RowsPerBeat;
	int RowsPerBar;

	// From the row highlights; 0 or nonsense picks 16 rows a bar, 4 a beat
	Timing(int PatternLength, int Frames, int FirstHighlight, int SecondHighlight);

	int TotalRows() const { return PatternLength * Frames; }
	int BarsPerFrame() const;
	int BarCount() const;					// bars in the song
	int BarStart(int Bar) const;			// past the end too (frames that would be added)
	int BarLength(int Bar) const;
	int BarEnd(int Bar) const { return BarStart(Bar) + BarLength(Bar); }
	int BarOf(int Row) const;
	double BeatOf(int Row) const;			// within its bar, from 1
	int BeatsPerBar(int Bar) const;
	// A bar and beat (1, 2.5...) to a row; false outside the bar. Exact is
	// false when the beat fell between rows and was rounded to the nearest.
	bool PositionToRow(int Bar, double Beat, int &Row, bool &Exact) const;
	int BeatsToRows(double Beats, bool &Exact) const;	// at least 1
	std::string Position(int Row) const;	// "bar 3 beat 2.5"
	std::string Length(int Rows) const;		// "1.5 beats", "1 beat", "1/4 beat"
};

// Tempo as CSoundGen plays it: rows per minute from speed, tempo and the
// engine's frame rate; BPM with RowsPerBeat rows to a beat
double RowsPerMinute(int Speed, int Tempo, int FrameRate);
double BeatsPerMinute(int Speed, int Tempo, int FrameRate, int RowsPerBeat);
// Every row lasts the same number of ticks (frames of the engine) with this
// speed and tempo, so the rhythm is even. At the default tempo, 2.5 times the
// frame rate (150 for NTSC, 125 for PAL), the speed is the ticks a row.
bool EvenRows(int Speed, int Tempo, int FrameRate);
int DefaultTempo(int FrameRate);
// The speed (1 to SplitPoint - 1) and tempo (32-255) for Bpm. Among those
// within 1% of it, rows of even length come first, then the default tempo
// with another speed (as FamiTracker users mostly change it), then the
// current speed, then the closest; otherwise the closest of all. A tie rounds
// up. FixedSpeed or FixedTempo, when not 0, has to stay.
void ChooseSpeedTempo(double Bpm, int RowsPerBeat, int FrameRate, int CurrentSpeed, int SplitPoint, int &Speed, int &Tempo,
	int FixedSpeed = 0, int FixedTempo = 0);

// How the song plays: from frame 0, each frame from the row it starts at
// until a jump (Bxx), skip (Dxx) or halt (Cxx) ends it, or its last row,
// then on to the next frame (after the last one, frame 0) as CSoundGen does.
// It goes on until a frame starts where one started before, which is where
// the song loops, or until a halt.
struct FlowStep {
	int Frame;
	int FirstRow;		// in the frame; a skip in the frame before can start it later
	int EndRow;			// in the frame: after the row with the jump, skip or halt, or the pattern length
};

struct SongFlow {
	std::vector<FlowStep> Steps;	// in playing order
	int LoopStep;		// where playing goes on after the last step; -1 when the song halts

	SongFlow() : LoopStep(0) {}
	bool Halts() const { return LoopStep < 0; }
	int LoopRow(int PatternLength) const;	// song row where the loop starts, -1 when the song halts
	int EndRow(int PatternLength) const;	// song row after the last one played before it loops or halts
	int PlayedRows() const;					// rows from the start until the loop goes back (or the halt)
	int LoopRows() const;					// rows in the loop, 0 when the song halts
	std::vector<bool> Played(int TotalRows, int PatternLength) const;	// per song row: does it ever play
};

// Channels' cells from the song's first row, with each channel's visible
// effect columns (CSoundGen only reads those)
SongFlow FollowSong(const std::vector<std::vector<Cell> > &Channels, const std::vector<int> &EffectColumns, int PatternLength,
	int Frames);

// When rows play, in engine ticks from the start (FrameRate a second): the
// flow's rows in order, the intro once and the loop Passes times (a song that
// halts plays once), each as long as speed and tempo make it, Fxx changing
// them on the way, the way CSoundGen counts. EndTick is when the next row
// would start.
struct TimedRow {
	int Row;			// song row
	int Tick;			// when it starts
	int Pass;			// 0 for the intro and the first time through the loop, then 1, 2...
	bool LoopStart;		// the loop's first row
};
std::vector<TimedRow> TimeRows(const std::vector<std::vector<Cell> > &Channels, const std::vector<int> &EffectColumns,
	const SongFlow &Flow, int PatternLength, int Speed, int Tempo, int SplitPoint, int FrameRate, int Passes, int &EndTick);

// Keys and modes
struct Key {
	int Tonic;		// 0 = C ... 11 = B
	int Mode;		// into ModeNames: 0 major ... 7 harmonic minor
	Key() : Tonic(0), Mode(0) {}
	Key(int Tonic, int Mode) : Tonic(Tonic), Mode(Mode) {}
};
const int MODE_MAJOR = 0;
const int MODE_MINOR = 5;
int ModeCount();
const char *ModeName(int Mode);				// "major", "dorian", ...
const int *ModeSteps(int Mode);				// 7 semitone offsets from the tonic
bool ParseKey(const std::string &Text, Key &Out);	// "C major", "A minor", "F# dorian", "Bbm", "Am", "E"
bool ParseMode(const std::string &Text, int &Mode);	// "minor", "aeolian", "lydian"...
std::string KeyName(const Key &K);			// "C major", "F# dorian", "Bb minor"
std::string NoteClassName(int PitchClass, const Key &K);	// sharps or flats as the key spells them
bool InKey(int Pitch, const Key &K);		// in minor keys the raised 7th counts too
// The pitch moved from one key to another: each scale degree to the same
// degree of the new key (notes between degrees move with the one below),
// the tonic moved by the smallest step. Same key: the pitch itself.
int MapToKey(int Pitch, const Key &From, const Key &To);

// A channel's notes, read from its cells (from the first row of the song)
struct NoteInfo {
	int Row;			// first row
	int Rows;			// until the next note, release or halt (or the song's end)
	int Pitch;
	int Instrument;		// playing (set on this note or earlier), NO_INSTRUMENT if never set
	int Volume;			// in effect, 15 if never set
	bool InstrumentSet;	// on this note's row
	bool VolumeSet;
	int EndNote;		// NOTE_RELEASE or NOTE_HALT that ends it, NOTE_NONE for the next note or the song's end
	std::vector<std::pair<int, std::pair<int, int> > > Effects;	// row offset, (effect, param), on its rows
};
std::vector<NoteInfo> ReadNotes(const std::vector<Cell> &Cells, int EffectColumns);

// The channel's state entering Row: playing instrument and volume, sticky
// effects (param per effect id, 0 = off), whether a note is sounding
struct ChannelState {
	int Instrument;		// NO_INSTRUMENT if never set
	int Volume;			// 15 at the start
	unsigned char Sticky[EFF_COUNT];
	bool Sounding;		// a note started and nothing ended it yet
	int SoundingInstrument;
	ChannelState();
};
ChannelState StateAt(const std::vector<Cell> &Cells, int Row, int EffectColumns);

// A note to write
struct PartNote {
	int Row;			// first row (song row)
	int Rows;			// at least 1
	int Pitch;			// 0-95, noise 0-15
	int Instrument;		// 0-63, NO_INSTRUMENT to leave the column empty
	int Volume;			// 0-15, -1 to leave the column empty
	std::vector<std::pair<int, int> > Effects;	// on its first row
	std::vector<std::pair<int, std::pair<int, int> > > LaterEffects;	// row offset, (effect, param)
	std::vector<std::pair<int, int> > EndEffects;	// on the release or halt that ends it, if it gets one
	std::vector<int> Arpeggio;	// a lush chord's notes, semitones above Pitch, for an instrument's arpeggio to loop
								// through (the tool that writes the part makes the instrument); empty for none
	PartNote() : Row(0), Rows(1), Pitch(0), Instrument(NO_INSTRUMENT), Volume(-1) {}
};

struct PartOptions {
	int EffectColumns;					// the channel's visible effect columns, 1-4
	std::vector<bool> Releases;			// per instrument: ends notes with a release (===) rather than a halt (---)
	bool KeepFollowing;					// change cells after the range so later notes play as before (direct writes)
	bool StopAtEnd;						// the end marker after the range goes on the range's last row instead (clips)
	bool RingOut;						// nothing ends the notes: the DPCM channel's samples play out, and a halt there
									// would reset the DPCM level (0.4.6 writes $4011 = 0 as it cuts a sample)
	PartOptions() : EffectColumns(1), KeepFollowing(true), StopAtEnd(false), RingOut(false) {}
};

// A channel's new cells for rows [First, End), its notes replaced by Notes,
// from its current cells Old (from the first row of the song; rows past its
// size are empty). Note, instrument, volume and every effect but speed, jump,
// skip and halt are cleared in the range; the notes get theirs. Effects still
// on from before the range (vibrato, arpeggio...) are turned off at First, a
// note sounding into the range stops there, and the last note gets a release
// or halt when nothing follows it. With KeepFollowing, the next note after
// the range gets back the instrument, volume and effects it played with.
// Out holds the cells from First on and can be longer than End - First.
// Notes must be in order, inside [First, End) and not overlap.
void BuildPart(const std::vector<Cell> &Old, int First, int End, const std::vector<PartNote> &Notes,
	const PartOptions &Options, std::vector<Cell> &Out, std::vector<std::string> &Warnings);

// After rows [First, End) of a channel were changed into Out (cells from
// First on, Old as they were): the next note after the range gets back the
// instrument, volume and effects it played with, in Out (which grows up to
// it). False if effects didn't fit in the visible columns.
bool KeepFollowing(const std::vector<Cell> &Old, int First, int End, std::vector<Cell> &Out, int EffectColumns);

// Puts an effect in the first free one of the visible columns. False if none
// is free; an effect of the same kind already there is replaced.
bool AddEffect(Cell &Target, int Effect, int Param, int EffectColumns);
bool HasEffect(const Cell &Target, int Effect, int EffectColumns);

// The song's speed, jumps, skips and halts in Old (a row as it is) stay in
// New (what goes there instead), each in its own column: an effect New has in
// that column moves to a free one of the visible columns, or is left out when
// none is free. New's own effects of those kinds stay too, but one Old has
// already goes in once. False when an effect of New's was left out.
bool KeepSongEffects(const Cell &Old, Cell &New, int EffectColumns);

}	// namespace ftapi

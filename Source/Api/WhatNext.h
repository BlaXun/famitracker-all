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

// The what_next tool's portable pieces (the tool itself, which reads the song
// through the other tools' checks, is WhatNextTool in Tools.cpp): a dry run of
// a tool on the song, the kinds of song the recipes know with the choices
// what_next makes for each, the chords it gives a new song's parts, and the
// phrase it offers as the melody's step, for the composer to replace with a
// tune of their own. Plain C++14, tested on Linux.

#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "Music.h"
#include "Song.h"

namespace ftapi {

// The song as Host has it, with every change a tool asks for kept back and
// counted (Changes; the cells it would write are in Writes, the other changes
// counted in Others): a tool run on it shows whether it would change the song
// (mix, polish, vary_part, fix_loop, grow_melody) and leaves the song as it is. It reports
// that direct changes are allowed, so a tool takes the path that writes; tools
// that add instruments, tracks or chips, or play, can't run on it (those calls
// fail). With Overlay set (once the tool has run), it reads the cells as the writes kept back would leave them, so
// the checks can hear the song as the tool would leave it.
class DryRunHost : public SongHost {
public:
	explicit DryRunHost(SongHost &Host) : Changes(0), Others(0), Overlay(false), m_Host(Host) {}
	int Changes;
	int Others;		// changes that aren't cells written: sections, the key, the mixer, the tempo, names, clips, files
	std::vector<std::pair<SongWrite, std::vector<ChannelCells> > > Writes;
	bool Overlay;

	bool GetSong(SongInfo &Info, std::string &Error) override;
	void ReadCells(int Channel, int First, int Rows, std::vector<Cell> &Out) override;
	bool ReadClip(int Index, std::vector<std::vector<Cell> > &Columns) override;
	bool CanChange(std::string &Why) override;
	bool WriteSong(const std::vector<ChannelCells> &Parts, const SongWrite &Write, std::string &Error) override;
	bool SetSections(const SectionList &Sections, std::string &Error) override;
	bool SetKey(bool Chosen, const KeyChoice &Choice, std::string &Error) override;
	bool SetMixer(const MixerSettings &Mixer, std::string &Error) override;
	void BeginGroup() override {}
	void EndGroup() override {}
	bool AddClip(std::string &Name, const std::vector<int> &Sources, const std::vector<std::vector<Cell> > &Columns,
		std::string &Error) override;
	int AddInstrument(const Preset &Sound, const std::string &Chip, bool Sawtooth, std::string &Error) override;
	int AddN163Instrument(const WaveSound &Sound, int WavePos, std::string &Error) override;
	int AddFdsInstrument(const FdsSound &Sound, std::string &Error) override;
	int AddVrc7Instrument(int Patch, const std::string &Name, std::string &Error) override;
	int AddVrc7CustomInstrument(const std::vector<int> &Regs, const std::string &Name, std::string &Error) override;
	int AddDpcmKit(const DpcmKit &Kit, std::string &Error) override;
	int RefreshDpcmKit(const DpcmKit &Kit, std::string &Error) override;
	void CountInstrumentUses(std::vector<int> &InTracks, std::vector<int> &InClips) override;
	bool RemoveInstruments(const std::vector<int> &Indices, std::string &Error) override;
	int ExpansionNotes(const std::vector<std::string> &Chips, int Channels, std::vector<int> &Tracks) override;
	bool SetExpansion(const std::vector<std::string> &Chips, int Channels, int &Moved, int &Lost, std::string &Error) override;
	bool SetSpeedTempo(int Speed, int Tempo, std::string &Error) override;
	bool SetMeter(int RowsPerBeat, int RowsPerBar, int PatternLength, std::string &Error) override;
	bool SetNames(const SongNames &Names, std::string &Error) override;
	bool SelectTrack(int Track, std::string &Error) override;
	int AddTrack(const std::string &Name, const std::vector<std::vector<Cell> > &Channels, int Speed, int Tempo,
		std::string &Error) override;
	bool RenderTrack(int Track, double Seconds, const RenderOptions &Options, std::vector<float> &Left, std::vector<float> &Right,
		int &SampleRate, std::vector<std::pair<int, long long> > &Marks, std::string &Error) override;
	std::unique_ptr<RenderSet> RenderParts(int Track, double Seconds, const std::vector<RenderOptions> &Renders,
		std::string &Error) override;
	bool SaveFile(const std::string &FileName, const std::string &Bytes, std::string &Path, std::string &Error) override;
	bool ReadFile(const std::string &FileName, std::string &Bytes, std::string &Error) override;
	std::string LocalPath(const std::string &Path) override;
	bool SaveInbox(const std::string &Name, const std::string &Bytes, std::string &Path, std::string &Error) override;
	bool ReadExport(const std::string &Name, std::string &Bytes, std::string &Error) override;
	bool ListLibrary(std::vector<std::string> &Names, std::string &Error) override;
	bool ReadLibrary(const std::string &Name, std::string &Bytes, std::string &Error) override;
	bool SaveLibrary(const std::string &Name, const std::string &Bytes, std::string &Path, std::string &Error) override;
	bool DeleteLibrary(const std::string &Name, std::string &Error) override;
	bool LatestRecording(std::string &Path, std::string &Error) override;
	bool OpenText(const std::string &Text, std::string &Error) override;
	bool Play(const PlayRequest &Request, std::string &Error) override;
	void Stop() override {}
	bool Undo(std::string &Message) override;
	bool Save(const std::string &CopyName, std::string &Message) override;

private:
	SongHost &m_Host;
};

// The kinds of song what_next follows, as get_recipe has them; general when
// none is given and the track's name doesn't say calm (append only)
enum next_kind { NEXT_GENERAL, NEXT_CALM, NEXT_LIVELY, NEXT_ACTION, NEXT_COZY, NEXT_MYSTERIOUS, NEXT_ADVENTURE, NEXT_FLOATING,
	NEXT_PLAYFUL, NEXT_FESTIVE, NEXT_BITTERSWEET, NEXT_TENSE, NEXT_MELANCHOLIC,
	NEXT_ATMOSPHERIC, NEXT_KIND_COUNT };

struct NextKind {
	int Kind;					// next_kind
	const char *Name;			// "calm", "lively", "action", "cozy", "general"
	double SlowestBpm, FastestBpm;	// the tempos it suits (0 for any)
	double Bpm;					// set_tempo's when the song's isn't one of them
	const char *LeadName;		// the lead's sound what_next designs for it, and design_instrument's kind
	const char *LeadKind;
	const char *ChordStyle;		// add_chords_and_bass's styles: its chords, a bass, a bass that moves
	const char *BassStyle;
	const char *MovingBass;
	const char *Groove;			// add_drums's; "" for none (calm)
	const char *MixStyle;		// mix's
	const char *Style;			// check_song's, review_arrangement's and polish's: "calm", "lively", "any" or "atmospheric"
								// (calm's, the checks asking for no lead: TextureArg in Tools.cpp)
	double Loudness;			// export_for_game's, in LUFS
	const char *Keys[3];		// a new song's key, one of these (by its name)
	// The kinds after cozy (feat-cozy-kind) say the rest here, so what_next, finish_song and the recipes need no code of
	// their own for each (null or 0 for the older kinds, which keep theirs)
	const char *Aliases;		// other words ParseNextKind takes for it, a space between
	int KeyMode;				// +1: a major key (major or lydian), -1: a minor one (minor, dorian...), 0: either
	const char *Color;			// add_chords_and_bass's color for the chords written before the melody
	const char *Intensity;		// add_drums's ("" normal)
	bool Samples;				// add_drums with kit samples
	int Grow;					// grow_melody's kind: 1 calm (few, long notes, by step), 2 lively, 3 action; 0 lively
	const char *Title;			// "a mysterious track", as finish_song names it
	const char *Listen;			// what to listen for once it's finished
	const char *LeadWhy;		// the lead step's why: the sound (%s: the lead's words, LeadWhyOf) and what it's for
	const char *ProgMajor[2];	// its A's and B's chords (NextProgression), in a major key and a minor one; null: the
	const char *ProgMinor[2];	// general recipe's
	const char *Place;			// what it's for: "caves, sewers, ruins, forgotten places"
	const char *Feel;			// the recipe's line on how it should feel
	const char *Melody;			// the recipe's line on its tune
	const char *Depth;			// the recipe's line on its repeats and depth
	// Pools (a kind is guidance, not a preset): what_next picks one of each by the song (NextPicksOf), so two songs of a
	// kind differ and a song keeps its picks; '|' between the entries, the first the kind's own (the fields above). Null:
	// the single value above
	const char *Leads;			// design_instrument's kinds
	const char *ChordStyles;
	const char *BassStyles;
	const char *Grooves;		// an empty entry: no drums
	const char *Tempos;			// BPM, each an even one (rows of whole frames)
	const char *KeyPool;
	// Its chords stay plain triads all through (a seventh or a ninth over the drone rubs): no colors added after the tune,
	// and the review asks for none (fix-r4-recipe: the Caves' review led to sevenths everywhere against its recipe)
	bool StaysPlain;
};

// What what_next takes from a kind's pools for one song: picked by the track's name and the song's seed, each pool with a
// salt of its own so the picks don't move together. Count: how many the pool had (1 for a single value)
struct NextPick {
	std::string Value;
	int Count;
	bool Leaned;	// the flavour leans to it (Flavours.h): picked among entries it weighs more
	NextPick() : Count(1), Leaned(false) {}
};

struct NextPicks {
	NextPick Lead, ChordStyle, BassStyle, Groove, Tempo, Key;
	double Bpm() const;
};

// Flavour (Flavours.h): the pools' entries it leans to weigh more, and the tempo pool takes its lean; FLAVOUR_NONE (0): the
// kind's own picks, as before flavours
NextPicks NextPicksOf(int Kind, const std::string &TrackName, unsigned int Seed, int Flavour = 0);
// A pool's entries ("a|b|c")
std::vector<std::string> PoolEntries(const char *Pool);
// "a, b or c"
std::string PoolText(const char *Pool, const char *Single);
// What a design_instrument kind of lead does, with its kind: "breathes in and holds low (kind breath)"
std::string LeadSoundWords(const std::string &Kind);
// A later kind's lead why (NextKind::LeadWhy) for one lead, or for its whole pool when Lead is empty
std::string LeadWhyOf(const NextKind &K, const std::string &Lead);

const NextKind &NextKindInfo(int Kind);
// A kind as asked ("calm", "Lively", "cosy", "caves"); false when it's none of them
bool ParseNextKind(const std::string &Text, int &Kind);
// The kinds' names, "calm, lively, cozy, ... or floating"
std::string NextKindNames();
// Whether a kind's songs want a major key (calm, cozy and others whose keys are all major), or a minor one (minor or a
// minor mode: mysterious and others), for what_next's note on a track whose key goes against it
bool KindWantsMajor(int Kind);
bool KindWantsMinor(int Kind);

// The chords what_next gives a new song's A part (or its B part, on other
// chords), eight bars as degrees of its key: "| I | V | vi | IV | ... |"
// (Dorian: the key is dorian, whose later kinds' chords then sound its raised sixth: IV for a VII in each half)
std::string NextProgression(int Kind, bool Minor, bool B, bool Dorian = false);
// A progression of eight bars, a chord a bar, with its cadences on the half bar (the fourth bar into the fifth, the last into
// the loop) and a chord from outside the key in bar 6 or 7 (feat-variety); one written with half bars stays as it is
std::string EnrichedProgression(const std::string &Progression, bool Minor);

// A note of the phrase ExampleTune offers: its bar (from 1), its beat in the
// bar (from 1), how long it lasts in beats, and its pitch as written on a pulse
// (36 is C-3, middle C)
struct TuneNote {
	int Bar;
	double Beat;
	int Pitch;
	double Beats;
};

// A phrase of Bars bars (2 or 4) from FirstBar in K, as what_next offers a
// melody's step: a rhythm of the kind's (Calm: long notes; else a hook of
// quarters and eighths; Variant's bit 0 gives its first two bars another
// rhythm, a syncopated one, bit 1 its last two, and bit 2 ends it on the
// third rather than the tonic), its first two bars ending
// on a held note and its last on a held note and a rest, so it breathes; each
// note of a beat or more, and each on a strong beat, a tone of the chord under
// it, the others stepping between them; none a semitone from what another part
// sounds then; around the tonic from G-3 to F#4, rising to a high point and coming home.
// Chord and Sounding have a row each from the phrase's first row (RowsPerBeat
// rows a beat, BeatsPerBar beats a bar): the pitch classes of the chord there
// and of the notes the other parts sound (empty for none).
// The tonic the offered phrases are around (ExampleTune's home, the hooks' too): from G-3 to F#4 as a pulse plays it
int TuneHome(const Key &K);

std::vector<TuneNote> ExampleTune(const Key &K, int FirstBar, int Bars, int BeatsPerBar, int RowsPerBeat,
	const std::vector<std::vector<int> > &Chord, const std::vector<std::vector<int> > &Sounding, bool Calm, int Variant);

// A hook what_next offers a song with a seed (fix-gen4-melody): a bar or two (Bars says which) from FirstBar, drawn from
// pools by Pick (the song's seed and name hashed with its kind) and Variant: a rhythm figure of the kind's pace (0 slow, for
// calm songs; 1 medium; 2 busy, for action and festive ones: pickups, long-short, syncopated, dotted, a held opening note,
// call and answer), a contour (an arch, a rise, a fall, a leap then steps back, a turn around a neighbour, a dip), the
// degree it starts on and the one it ends on. Singable as ExampleTune's: chord tones on the strong beats and long notes,
// steps mostly, a leap answered by a step the other way, nothing a semitone from another part, from a fourth under the
// home tonic (TuneHome) to an octave over it. Other meters than 4/4 get ExampleTune's two bars. Pick 0 is ExampleTune's
// own two bars, as a song without a seed keeps. Top (0 for none): the highest pitch, the home an octave down where its
// fifth would pass it, for a kind whose recipe asks for a tune mostly in octave 3 (fix-r4-recipe: the Hut was offered F-5)
std::vector<TuneNote> HookTune(const Key &K, int FirstBar, int BeatsPerBar, int RowsPerBeat, const std::vector<std::vector<int> > &Chord,
	const std::vector<std::vector<int> > &Sounding, int Pace, unsigned int Pick, int Variant, int &Bars, int Top = 0);
// The highest note a kind's offered hooks reach (HookTune's Top): B-4 for the kinds whose recipes ask for a tune mostly in
// octave 3 (general, lively, action, cozy, adventure), else 0
int HookTop(int Kind);
// The melody's register by kind (fix-r6-bugs, measured on the user's kept songs: their calm openings' middle notes
// around E-3 to A#3, the lively ones' around D#4 to A-4): 3 for the calm kinds (calm, cozy, mysterious, floating,
// melancholic, bittersweet, atmospheric, tense), 4 for the lively ones (lively, adventure, action, festive, playful), 0 for
// general (either)
int MelodyOctave(int Kind);
// Its words for a recipe or a step: "mostly in octave 3, reaching into octave 4 at its high points" or "mostly in octave 4
// (its middle around C-4 to E-4), reaching into octave 5 at its high points"
const char *MelodyRegisterText(int Kind);
// How many hook figures each pace draws from (HookTune's pools), for the tests
int HookFigures(int Pace);

}	// namespace ftapi

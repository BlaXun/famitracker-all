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
enum next_kind { NEXT_GENERAL, NEXT_CALM, NEXT_LIVELY, NEXT_ACTION, NEXT_COZY };

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
	const char *Style;			// check_song's, review_arrangement's and polish's: "calm", "lively" or "any"
	double Loudness;			// export_for_game's, in LUFS
	const char *Keys[3];		// a new song's key, one of these (by its name)
};

const NextKind &NextKindInfo(int Kind);
// A kind as asked ("calm", "Lively", "cosy"); false when it's none of the four
bool ParseNextKind(const std::string &Text, int &Kind);
// Whether a kind's songs want a major key (calm and cozy, whose keys are all major), for what_next's note on a track
// whose key is minor
bool KindWantsMajor(int Kind);

// The chords what_next gives a new song's A part (or its B part, on other
// chords), eight bars as degrees of its key: "| I | V | vi | IV | ... |"
std::string NextProgression(int Kind, bool Minor, bool B);

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
std::vector<TuneNote> ExampleTune(const Key &K, int FirstBar, int Bars, int BeatsPerBar, int RowsPerBeat,
	const std::vector<std::vector<int> > &Chord, const std::vector<std::vector<int> > &Sounding, bool Calm, int Variant);

}	// namespace ftapi

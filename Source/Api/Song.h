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

// The song as the Claude Code connection's tools see it: what they read and
// the changes they ask for. McpConnection.cpp implements it over the open
// FamiTracker document; the tests implement it in memory.

#include <string>
#include <utility>
#include <vector>
#include "Music.h"
#include "Sections.h"

namespace ftapi {

struct Preset;
struct DpcmKit;
struct WaveSound;

struct ChannelInfo {
	std::string Name;		// "Pulse 1", "Sawtooth"
	std::string Chip;		// "2A03", "VRC6", "MMC5", "N163", "FDS", "VRC7", "5B"
	int Kind;				// channel_kind
	int ChanId;				// FamiTracker's channel ID (chan_id_t)
	int EffectColumns;		// visible effect columns, 1-4
	bool Muted;				// muted in FamiTracker, so the user doesn't hear it play

	ChannelInfo() : Kind(KIND_OTHER), ChanId(-1), EffectColumns(1), Muted(false) {}
	std::string FullName() const { return Chip + " " + Name; }	// "VRC6 Pulse 1"
};

// One of an instrument's sequences, as FamiTracker stores it
struct SequenceInfo {
	std::vector<int> Items;	// a value a frame; empty when the instrument has none
	int Loop, Release;		// where it loops back to and where it waits for a release (-1: none)
	int Setting;			// an arpeggio's: 0 absolute, 1 fixed, 2 relative (ARP_SETTING_*)

	SequenceInfo() : Loop(-1), Release(-1), Setting(0) {}
};

struct InstrumentInfo {
	int Index;				// 0-63
	std::string Name;
	std::string Chip;		// "2A03", "VRC6", ...
	bool Releases;			// fades out on a release (===)
	bool Vibrato;			// vibrato built in: its pitch sequence goes up and down in a loop
	int WavePos, WaveSize;	// an N163 instrument's place and length in the chip's wave memory (samples); -1 otherwise
	std::vector<int> Arpeggio;	// the notes its looping arpeggio plays besides each note it's given, in semitones ({4, 7,
								// 11}: a major seventh); empty when it has none, or one that doesn't loop
	bool OddDuty;			// a VRC6 instrument whose duty sequence plays odd values: on the sawtooth they add 32 to
							// its volume, and it overdrives
	std::vector<int> Volume;	// its volume sequence (0-15 a frame, times the volume column over 15) for 2A03, VRC6, N163
								// and 5B instruments (on the 5B, 3 dB a step: the column's steps under 15 and the
								// sequence's add up); empty when it has none
	int VolumeLoop, VolumeRelease;	// where that sequence loops back to and where it waits for a release (-1: none)
	SequenceInfo Steps, Pitch, HiPitch;	// its arpeggio, pitch and hi-pitch sequences (2A03, VRC6, N163 and 5B instruments)
	SequenceInfo Mode;		// a 5B instrument's noise / mode sequence: 64 the square, 128 the noise, 32 the envelope (whose
							// level replaces the volume's), and the noise's pitch 0-31 in the low bits; empty otherwise

	InstrumentInfo() : Index(0), Releases(false), Vibrato(false), WavePos(-1), WaveSize(-1), OddDuty(false), VolumeLoop(-1),
		VolumeRelease(-1) {}
};

struct ClipInfo {
	int Index;
	int Id;
	std::string Name;
	int Columns;
	int Rows;
	int Links;				// linked copies in the song
	std::vector<int> Sources;	// channel ID per column, -1 if not known

	ClipInfo() : Index(0), Id(0), Columns(1), Rows(1), Links(0) {}
};

struct SongInfo {
	std::string Title, Author, Copyright;
	std::string FileName;	// "" until saved
	bool Modified;
	int Track, Tracks;		// from 0
	std::string TrackName;
	std::vector<std::string> TrackNames;	// every track's, in order
	int FrameRate;			// engine ticks a second (60 NTSC, 50 PAL)
	bool Pal;				// a PAL module: the 2A07's clock sets the pulses' periods
	int Speed, Tempo, SplitPoint;
	int PatternLength, Frames, MaxFrames;
	int FirstHighlight, SecondHighlight;
	std::string Expansion;	// the expansion chips on: "VRC6", or several, "VRC6, N163, 5B" (in the order VRC6, VRC7,
							// FDS, MMC5, N163, 5B); "" for none
	std::vector<ChannelInfo> Channels;
	std::vector<InstrumentInfo> Instruments;
	std::vector<ClipInfo> Clips;
	SectionList Sections;	// the selected track's (Sections.h), names in UTF-8
	bool DirectChanges;		// the user lets Claude change the song (otherwise ideas become clips)
	bool Playing;
	int PlayRow;			// from the song's start, -1 when stopped

	SongInfo() : Modified(false), Track(0), Tracks(1), FrameRate(60), Pal(false), Speed(6), Tempo(150), SplitPoint(32),
		PatternLength(64), Frames(1), MaxFrames(128), FirstHighlight(4), SecondHighlight(16), DirectChanges(false),
		Playing(false), PlayRow(-1) {}
};

// Names for the module and the selected track (name_song), in UTF-8; what
// isn't Has... stays as it is
struct SongNames {
	bool HasTitle, HasAuthor, HasCopyright, HasTrack;
	std::string Title, Author, Copyright, Track;
	SongNames() : HasTitle(false), HasAuthor(false), HasCopyright(false), HasTrack(false) {}
};

// Cells to write into a channel from a row on. Exact cells are the rows as
// they're to be, the song's speed, jumps, skips and halts among them, and are
// written as they are. Otherwise (a clip placed) the song's own speed, jumps,
// skips and halts in those rows stay where they are and the cells' effects go
// around them, as when the user drags a clip into the song.
struct ChannelCells {
	int Channel;
	std::vector<Cell> Cells;
	bool Exact;

	ChannelCells() : Channel(0), Exact(false) {}
};

// How SongHost::WriteSong writes its parts
struct SongWrite {
	int First;          // the song row the parts' cells start at
	int LinkClipId;     // what's written is a linked copy of this clip; -1 for none
	// Before the cells are written, MoveBy frames are put in at frame
	// MoveFrame (MoveBy > 0), each with new empty patterns, or the -MoveBy
	// frames from MoveFrame on are taken out (MoveBy < 0), as FamiTracker's
	// frame list does it: the frames after keep their patterns (and linked
	// copies). The parts' rows count with the frames moved.
	int MoveFrame;
	int MoveBy;
	int Cursor;         // the song row the cursor goes to afterwards; -1 for First
	// With SetSections, the selected track's sections afterwards (Sections.h,
	// UTF-8 names): set once the frames have moved and the cells are written,
	// in the same undoable change. Otherwise they follow the frames moved.
	bool SetSections;
	SectionList Sections;

	SongWrite() : First(0), LinkClipId(-1), MoveFrame(0), MoveBy(0), Cursor(-1), SetSections(false) {}
};

// Rows of a track (from 0) to play
struct PlayPart {
	int Track;
	int First, Rows;
	PlayPart() : Track(0), First(0), Rows(1) {}
};

// What SongHost::Play plays
struct PlayRequest {
	int Track;                  // the track it plays (selected for it); -1 for the selected one
	int First;                  // the song row it starts at
	int Rows;                   // how many; 0 to the end
	std::vector<PlayPart> Then; // played after those, one after the other (the same bars of other versions)
	bool Loop;                  // all of it over and over, until it's stopped
	std::vector<bool> Muted;    // per channel: silent this time (empty: as the user has them); the user's own mutes come
								// back when it stops
	int ReturnTrack;            // selected again when it stops; -1 to leave the track it ends on
	PlayRequest() : Track(-1), First(0), Rows(0), Loop(false), ReturnTrack(-1) {}
};

class SongHost {
public:
	virtual ~SongHost() {}

	// The open song; false with the reason when there is none
	virtual bool GetSong(SongInfo &Info, std::string &Error) = 0;
	// Rows of a channel from song row First on (rows past the end are empty)
	virtual void ReadCells(int Channel, int First, int Rows, std::vector<Cell> &Out) = 0;
	// A clip's cells, a column at a time
	virtual bool ReadClip(int Index, std::vector<std::vector<Cell> > &Columns) = 0;

	// False with the reason when the song can't be changed right now
	virtual bool CanChange(std::string &Why) = 0;
	// One undoable change (Ctrl+Z): frames moved first when the write says so,
	// then each part's cells replace its channel's rows from the write's First
	// on (ChannelCells says what becomes of the song's speed, jumps and halts
	// there), frames are added when they run past the end. With a clip ID,
	// what's written is a linked copy of that clip. Parts can be empty when
	// frames move.
	virtual bool WriteSong(const std::vector<ChannelCells> &Parts, const SongWrite &Write, std::string &Error) = 0;
	// The selected track's sections become these (Sections.h, UTF-8 names), as
	// one undoable change; false with the reason
	virtual bool SetSections(const SectionList &Sections, std::string &Error) = 0;
	// A new clip; Name is made unique (the name used comes back in it)
	virtual bool AddClip(std::string &Name, const std::vector<int> &Sources, const std::vector<std::vector<Cell> > &Columns,
		std::string &Error) = 0;
	// The preset as an instrument for a chip ("2A03", "VRC6" or "5B": Preset5BSequences), or the one of
	// that name and chip already there: its index, -1 with the reason
	virtual int AddInstrument(const Preset &Sound, const std::string &Chip, bool Sawtooth, std::string &Error) = 0;
	// An N163 instrument playing a wave sound (Wavetables.h), its waves at WavePos (samples) of the chip's
	// memory, named after it, or the N163 one of that name already there: its index, -1 with the reason
	virtual int AddN163Instrument(const WaveSound &Sound, int WavePos, std::string &Error) = 0;
	// A VRC7 instrument playing one of the chip's patches (1-15), named Name, or the VRC7 one of that
	// name already there: its index, -1 with the reason
	virtual int AddVrc7Instrument(int Patch, const std::string &Name, std::string &Error) = 0;
	// A VRC7 instrument playing the chip's custom patch (patch 0) set to Regs (its 8 registers), named
	// Name, or the VRC7 one of that name already there: its index, -1 with the reason
	virtual int AddVrc7CustomInstrument(const std::vector<int> &Regs, const std::string &Name, std::string &Error) = 0;
	// A kit of samples (DpcmKit.h) as a 2A03 instrument for the DPCM channel, its samples added
	// (or ones of the same name and size reused), or the 2A03 instrument of the kit's name already
	// there: its index, -1 with the reason
	virtual int AddDpcmKit(const DpcmKit &Kit, std::string &Error) = 0;
	// The samples of the kit's instrument already in the song that an older version made (the kit's
	// names under its keys) brought up to date with Kit's: how many changed, -1 with the reason
	virtual int RefreshDpcmKit(const DpcmKit &Kit, std::string &Error) = 0;
	// How many cells name each instrument (0-63): in the module's tracks (the
	// patterns their frame lists play) and in its clips
	virtual void CountInstrumentUses(std::vector<int> &InTracks, std::vector<int> &InClips) = 0;
	// Removes instruments from the module, and the sequences no instrument
	// left uses (as Edit > Clean up > Remove unused instruments does). False
	// with the reason. FamiTracker can't undo it.
	virtual bool RemoveInstruments(const std::vector<int> &Indices, std::string &Error) = 0;

	// The expansion chips: any of "VRC6", "VRC7", "FDS", "MMC5", "N163" (with Channels of it, 1-8)
	// and "5B", none for the 2A03 alone. The module's channels are the 2A03's, then every chip's in
	// that order: VRC6, MMC5, N163, FDS, VRC7, 5B. Switching keeps the notes of the chips that stay
	// (their channels move to their new place) and clears the channels of chips switched off, and
	// the N163 channels past a new count. ExpansionNotes counts the notes on the channels that would
	// be cleared, in every track (Tracks: the ones they're in, from 0). SetExpansion clears them in
	// every track and switches; channels that come in start empty. The effects on the cleared
	// channels that steer the song (speed and tempo, loops, skips, ends) move to the song's last
	// channel: Moved of them, and Lost found no free effect column there. FamiTracker can't undo it.
	virtual int ExpansionNotes(const std::vector<std::string> &Chips, int Channels, std::vector<int> &Tracks) = 0;
	virtual bool SetExpansion(const std::vector<std::string> &Chips, int Channels, int &Moved, int &Lost, std::string &Error) = 0;
	// Undoable
	virtual bool SetSpeedTempo(int Speed, int Tempo, std::string &Error) = 0;
	// The meter, undoable: the row highlights (rows a beat, rows a bar; one pair for the whole module in
	// 0.4.6) and the selected track's pattern length (0 keeps it)
	virtual bool SetMeter(int RowsPerBeat, int RowsPerBar, int PatternLength, std::string &Error) = 0;
	// The module's title, author and copyright (FamiTracker's song information
	// boxes) and the selected track's name. False with Error when the song
	// can't change, or a name is longer than FamiTracker keeps.
	virtual bool SetNames(const SongNames &Names, std::string &Error) = 0;

	// Tracks (FamiTracker's songs in one module). The tools work on the
	// selected one; this selects another (from 0).
	virtual bool SelectTrack(int Track, std::string &Error) = 0;
	// A new track after the others, named Name, playing every channel's cells
	// (Channels in channel order, from the first row) at Speed and Tempo, with
	// the selected track's pattern length and effect columns. It becomes the
	// selected track. Its number from 0, or -1 with the reason. FamiTracker
	// can't undo it: the user removes a track in Module > Module properties.
	virtual int AddTrack(const std::string &Name, const std::vector<std::vector<Cell> > &Channels, int Speed, int Tempo,
		std::string &Error) = 0;

	// For games: a track (from 0) played from its start for Seconds as audio,
	// its left and right sides (-1 to 1; the same on both when nothing pans),
	// with the sample where each row began, as song rows in playing order.
	// Every channel plays, muted ones too.
	virtual bool RenderTrack(int Track, double Seconds, std::vector<float> &Left, std::vector<float> &Right, int &SampleRate,
		std::vector<std::pair<int, long long> > &Marks, std::string &Error) = 0;
	// Writes a file next to the song (FileName has no folders), replacing one
	// of that name; the path written comes back in Path
	virtual bool SaveFile(const std::string &FileName, const std::string &Bytes, std::string &Path, std::string &Error) = 0;
	// Reads a file next to the song (FileName has no folders) into Bytes; false when
	// there's none (Error empty) or it can't be read (Error says why)
	virtual bool ReadFile(const std::string &FileName, std::string &Bytes, std::string &Error) = 0;
	// A file on the user's computer named as the tools are given it (UTF-8,
	// with Windows' backslashes), as the C library opens it: FamiTracker turns
	// it into the Windows code page it runs in
	virtual std::string LocalPath(const std::string &Path) = 0;
	// A file sent from another computer (send_file), saved in the Inbox folder
	// next to FamiTracker.exe (Documents\FamiTracker inbox where FamiTracker's
	// folder can't be written), replacing one of that name (Name has no
	// folders); its path as the tools are given paths comes back in Path
	virtual bool SaveInbox(const std::string &Name, const std::string &Bytes, std::string &Path, std::string &Error) = 0;
	// A file next to the song (Name has no folders), all of it, for a client on
	// another computer to fetch what export_for_game saved: false when there's
	// none (Error empty) or it can't be read (Error says why)
	virtual bool ReadExport(const std::string &Name, std::string &Bytes, std::string &Error) = 0;

	// The reference library (References.h): files in the References folder
	// next to FamiTracker.exe (Documents\FamiTracker references where
	// FamiTracker's folder can't be written), shared by every song. Names have no
	// folders. Listing gives the names of the files in it (none when there's no
	// folder yet); reading is false when there's no such file (Error empty) or
	// it can't be read (Error says why); saving makes the folder when needed
	// and gives the path written; deleting is false with the reason.
	virtual bool ListLibrary(std::vector<std::string> &Names, std::string &Error) = 0;
	virtual bool ReadLibrary(const std::string &Name, std::string &Bytes, std::string &Error) = 0;
	virtual bool SaveLibrary(const std::string &Name, const std::string &Bytes, std::string &Path, std::string &Error) = 0;
	virtual bool DeleteLibrary(const std::string &Name, std::string &Error) = 0;

	// The newest take in the Recordings folder next to FamiTracker.exe (the
	// WAV files of Record from microphone and Sound effect from your voice),
	// its path as the tools are given paths; false with the reason when
	// there's none
	virtual bool LatestRecording(std::string &Path, std::string &Error) = 0;

	// Replaces the open song with a module in FamiTracker's text format, as
	// File > Import text does: a new, untitled module. FamiTracker can't undo
	// it; the tools first make sure the song has no unsaved changes.
	virtual bool OpenText(const std::string &Text, std::string &Error) = 0;

	virtual bool Play(const PlayRequest &Request, std::string &Error) = 0;
	virtual void Stop() = 0;
	virtual bool Undo(std::string &Message) = 0;
	// Saves the song, or a copy of it next to it (CopyName: a file name)
	virtual bool Save(const std::string &CopyName, std::string &Message) = 0;
};

}	// namespace ftapi

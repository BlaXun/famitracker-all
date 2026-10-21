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

namespace ftapi {

struct Preset;

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

struct InstrumentInfo {
	int Index;				// 0-63
	std::string Name;
	std::string Chip;		// "2A03", "VRC6", ...
	bool Releases;			// fades out on a release (===)

	InstrumentInfo() : Index(0), Releases(false) {}
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
	int Speed, Tempo, SplitPoint;
	int PatternLength, Frames, MaxFrames;
	int FirstHighlight, SecondHighlight;
	std::string Expansion;	// "VRC6", "" for none
	std::vector<ChannelInfo> Channels;
	std::vector<InstrumentInfo> Instruments;
	std::vector<ClipInfo> Clips;
	bool DirectChanges;		// the user lets Claude change the song (otherwise ideas become clips)
	bool Playing;
	int PlayRow;			// from the song's start, -1 when stopped

	SongInfo() : Modified(false), Track(0), Tracks(1), FrameRate(60), Speed(6), Tempo(150), SplitPoint(32),
		PatternLength(64), Frames(1), MaxFrames(128), FirstHighlight(4), SecondHighlight(16), DirectChanges(false),
		Playing(false), PlayRow(-1) {}
};

// Cells to write into a channel from a row on
struct ChannelCells {
	int Channel;
	std::vector<Cell> Cells;
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
	// One undoable change (Ctrl+Z): each part's cells replace its channel's
	// rows from First on, frames are added when they run past the end. With a
	// clip ID, what's written is a linked copy of that clip.
	virtual bool WriteSong(const std::vector<ChannelCells> &Parts, int First, int LinkClipId, std::string &Error) = 0;
	// A new clip; Name is made unique (the name used comes back in it)
	virtual bool AddClip(std::string &Name, const std::vector<int> &Sources, const std::vector<std::vector<Cell> > &Columns,
		std::string &Error) = 0;
	// The preset as an instrument for a chip ("2A03" or "VRC6"), or the one of
	// that name and chip already there: its index, -1 with the reason
	virtual int AddInstrument(const Preset &Sound, const std::string &Chip, bool Sawtooth, std::string &Error) = 0;
	// A VRC7 instrument playing one of the chip's patches (1-15), named Name, or the VRC7 one of that
	// name already there: its index, -1 with the reason
	virtual int AddVrc7Instrument(int Patch, const std::string &Name, std::string &Error) = 0;

	// The expansion chip: "" for none, "VRC6", "VRC7", "FDS", "MMC5" or "N163" (with Channels of it,
	// 1-8). Switching to another clears the old chip's channels, since the new chip's take their
	// place; a new N163 channel count clears the N163 channels past it. ExpansionNotes counts the
	// notes on the channels that would be cleared, in every track (Tracks: the ones they're in, from
	// 0). SetExpansion clears them in every track and switches; channels that come in start empty.
	// The effects on the cleared channels that steer the song (speed and tempo, loops, skips, ends)
	// move to the song's last channel: Moved of them, and Lost found no free effect column there.
	// FamiTracker can't undo it.
	virtual int ExpansionNotes(const std::string &Chip, int Channels, std::vector<int> &Tracks) = 0;
	virtual bool SetExpansion(const std::string &Chip, int Channels, int &Moved, int &Lost, std::string &Error) = 0;
	// Undoable
	virtual bool SetSpeedTempo(int Speed, int Tempo, std::string &Error) = 0;

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

	// For games: a track (from 0) played from its start for Seconds as audio
	// (mono, -1 to 1), with the sample where each row began, as song rows in
	// playing order. Every channel plays, muted ones too.
	virtual bool RenderTrack(int Track, double Seconds, std::vector<float> &Samples, int &SampleRate,
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

	virtual bool Play(int First, int Rows, std::string &Error) = 0;	// Rows 0: to the end
	virtual void Stop() = 0;
	virtual bool Undo(std::string &Message) = 0;
	// Saves the song, or a copy of it next to it (CopyName: a file name)
	virtual bool Save(const std::string &CopyName, std::string &Message) = 0;
};

}	// namespace ftapi

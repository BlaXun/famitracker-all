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

#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "Keys.h"
#include "Music.h"
#include "Sections.h"

namespace ftapi {

struct Preset;
struct DpcmKit;
struct WaveSound;
struct FdsSound;

struct ChannelInfo {
	std::string Name;		// "Pulse 1", "Sawtooth"
	std::string Chip;		// "2A03", "VRC6", "MMC5", "N163", "FDS", "VRC7", "5B"
	int Kind;				// channel_kind
	int ChanId;				// FamiTracker's channel ID (chan_id_t)
	int EffectColumns;		// visible effect columns, 1-4
	bool Muted;				// muted in FamiTracker, so the user doesn't hear it play
	// The mixer window's settings (ChannelMix.h), the same in every track: the channel's fader in tenths of a
	// dB (0 as it plays; FamiTracker's mixer makes it that much louder) and the pan it starts at (0x80 the middle)
	int MixLevel;
	int MixPan;
	// ...and its depth there, after the fader: Soft (a gentle low-pass) and Space (its send to the song's space),
	// 0 (off) to 100
	int MixSoft;
	int MixSpace;
	// The chip as the user sees it when that isn't Chip: "VRC6 Plus" (Vrc6Plus.h) for the VRC6's channels and the soft
	// layer's (whose Chip stays "VRC6" and "N163", as they play); "" otherwise
	std::string Group;
	std::string OldName;	// a VRC6 Plus soft channel's name as an N163 channel ("Namco 1"), which the tools still take
	int ChipChannels;		// the channels its chip has in the song (the tools count them when they read it; the N163's
							// count sets how loud it plays); 0 when not known

	ChannelInfo() : Kind(KIND_OTHER), ChanId(-1), EffectColumns(1), Muted(false), MixLevel(0), MixPan(0x80), MixSoft(0), MixSpace(0),
		ChipChannels(0) {}
	const std::string &ShownChip() const { return Group.empty() ? Chip : Group; }
	// "VRC6 Pulse 1"; "FDS"; "VRC6 Plus Soft 1"
	std::string FullName() const { return Name == ShownChip() ? Name : ShownChip() + " " + Name; }
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
	std::vector<int> Volume;	// its volume sequence (0-15 a frame, times the volume column over 15) for 2A03, VRC6,
								// N163, FDS and 5B instruments (an FDS one's 0-31 as FdsVolumeAs15 counts them; on the 5B, 3 dB
								// a step: the column's steps under 15 and the sequence's add up); empty when it has none
	int VolumeLoop, VolumeRelease;	// where that sequence loops back to and where it waits for a release (-1: none)
	SequenceInfo Steps, Pitch, HiPitch;	// its arpeggio, pitch and hi-pitch sequences (2A03, VRC6, N163, FDS and
										// 5B instruments; an FDS pitch sequence moves the frequency register, up is sharper)
	SequenceInfo Mode;		// a 5B instrument's noise / mode sequence: 64 the square, 128 the noise, 32 the envelope (whose
							// level replaces the volume's), and the noise's pitch 0-31 in the low bits; empty otherwise
	SequenceInfo Duty;		// its duty sequence (2A03 and VRC6 instruments: the pulse's width, 0-3 and 0-7) or, on an N163
							// instrument, the wave it plays (its index in Waves); empty when it has none
	std::vector<std::vector<int> > Waves;	// an N163 instrument's waves, 0-15 a sample; empty for the other chips
	// An FDS instrument's own wave (64 samples, 0-63) and modulator: its table (32 steps, 0-7), speed (0-4095), depth (0-63)
	// and the frames it waits into a note (FdsModulator.h says what it does); empty and 0 for the other chips
	std::vector<int> FdsWave, ModTable;
	int ModSpeed, ModDepth, ModDelay;

	InstrumentInfo() : Index(0), Releases(false), Vibrato(false), WavePos(-1), WaveSize(-1), OddDuty(false), VolumeLoop(-1),
		VolumeRelease(-1), ModSpeed(0), ModDepth(0), ModDelay(0) {}
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
	bool Vrc6Plus;			// the VRC6 and the N163 (4 channels) are the VRC6 Plus (Vrc6Plus.h): Expansion still names them
	std::vector<ChannelInfo> Channels;
	std::vector<InstrumentInfo> Instruments;
	std::vector<ClipInfo> Clips;
	SectionList Sections;	// the selected track's (Sections.h), names in UTF-8
	bool KeyChosen;			// the selected track's key was chosen (Keys.h: in the piano roll, or with set_key)...
	KeyChoice ChosenKey;	// ...and is this one; otherwise the tools find it from the notes
	bool DirectChanges;		// the user lets Claude change the song (otherwise ideas become clips)
	bool Playing;
	int PlayRow;			// from the song's start, -1 when stopped
	// The mixer's space, which channels' Space sends to (ChannelMix.h): its room (SPACE_ROOM_NAMES), its echo in
	// 48ths of a beat of the track's tempo (0: none) and the echo's repeats
	int SpaceRoom, SpaceEcho, SpaceRepeats;

	SongInfo() : Modified(false), Track(0), Tracks(1), FrameRate(60), Pal(false), Speed(6), Tempo(150), SplitPoint(32),
		PatternLength(64), Frames(1), MaxFrames(128), FirstHighlight(4), SecondHighlight(16), Vrc6Plus(false), KeyChosen(false),
		DirectChanges(false), Playing(false), PlayRow(-1), SpaceRoom(1), SpaceEcho(0), SpaceRepeats(3) {}
};

// The mixer window's settings as set_mixer sets them: a fader (tenths of a dB), a starting pan, Soft and Space
// for every channel of the module in order, and the space (SongInfo::SpaceRoom and the rest)
struct MixerSettings {
	std::vector<int> Levels, Pans, Softs, Spaces;
	int Room, Echo, Repeats;

	MixerSettings() : Room(1), Echo(0), Repeats(3) {}
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
	// With SetKey, the selected track's key afterwards (Keys.h): Key when
	// KeyChosen, otherwise back to auto, in the same undoable change (a whole
	// song moved to another key takes its chosen key along)
	bool SetKey;
	bool KeyChosen;
	KeyChoice Key;

	SongWrite() : First(0), LinkClipId(-1), MoveFrame(0), MoveBy(0), Cursor(-1), SetSections(false), SetKey(false), KeyChosen(false) {}
};

// Rows of a track (from 0) to play
struct PlayPart {
	int Track;
	int First, Rows;
	PlayPart() : Track(0), First(0), Rows(1) {}
};

// How SongHost::RenderTrack renders: the whole song as the game's files have it, or some parts alone (measure_mix's
// stems)
struct RenderOptions {
	// Per channel of the module (empty: none): true for each left out of the sound. Its notes and effects still play,
	// so a DPCM level (Zxx) still softens the triangle and the noise, and the song's speed, jumps and halts still steer
	// it; only its sound is gone, its Space send with it (the space then holds only the channels heard)
	std::vector<bool> Silent;
	// After Seconds, rendering goes on while the mixer's space rings on (up to 15 s), as the game's files need; false
	// stops at Seconds
	bool Tail;
	std::string What;			// what it renders, for the status bar meanwhile ("the chords alone"); empty for the game
	RenderOptions() : Tail(true) {}
};

// Renders made together (SongHost::RenderParts), each taken once when it's wanted
class RenderSet {
public:
	virtual ~RenderSet() {}
	// The Index-th render, as RenderTrack gives one; false with the reason when it can't be had
	virtual bool Take(size_t Index, std::vector<float> &Left, std::vector<float> &Right, int &SampleRate,
		std::vector<std::pair<int, long long> > &Marks, std::string &Error) = 0;
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

// A file next to the song (SongHost::ListExports)
struct SongFile {
	std::string Name;		// "Town.ogg"
	long long Bytes;
	long long Modified;		// seconds since 1970, UTC; 0 when unknown

	SongFile() : Bytes(0), Modified(0) {}
};

// The name SongHost::SaveNew tries the Number-th time (from 1): "Town", then "Town 2", "Town 3"... up to MAX_SONG_NUMBER
inline std::string NumberedSongName(const std::string &Name, int Number)
{
	return Number <= 1 ? Name : Name + " " + std::to_string(Number);
}
const int MAX_SONG_NUMBER = 999;
const size_t MAX_LISTED_EXPORTS = 200;	// the files SongHost::ListExports gives at most, the newest

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
	// The selected track's key (Keys.h): Choice when Chosen, otherwise back to
	// auto (found from the notes), as one undoable change; false with the reason
	virtual bool SetKey(bool Chosen, const KeyChoice &Choice, std::string &Error) = 0;
	// The mixer window's faders, starting pans, Soft and Space (ChannelInfo::MixLevel
	// and the rest), one of each for every channel of the module in order, and its
	// space, as one undoable change; false with the reason
	virtual bool SetMixer(const MixerSettings &Mixer, std::string &Error) = 0;
	// The changes made from BeginGroup to EndGroup (in any track) are undone
	// and redone together, as one Ctrl+Z
	virtual void BeginGroup() = 0;
	virtual void EndGroup() = 0;
	// A new clip; Name is made unique (the name used comes back in it)
	virtual bool AddClip(std::string &Name, const std::vector<int> &Sources, const std::vector<std::vector<Cell> > &Columns,
		std::string &Error) = 0;
	// The preset as an instrument for a chip ("2A03", "VRC6" or "5B": Preset5BSequences), or the one of
	// that name and chip already there: its index, -1 with the reason
	virtual int AddInstrument(const Preset &Sound, const std::string &Chip, bool Sawtooth, std::string &Error) = 0;
	// An N163 instrument playing a wave sound (Wavetables.h), its waves at WavePos (samples) of the chip's
	// memory, named after it, or the N163 one of that name already there: its index, -1 with the reason
	virtual int AddN163Instrument(const WaveSound &Sound, int WavePos, std::string &Error) = 0;
	// An FDS instrument playing a sound (FdsSounds.h): its wave, modulator and sequences, named after
	// it, or the FDS one of that name already there: its index, -1 with the reason
	virtual int AddFdsInstrument(const FdsSound &Sound, std::string &Error) = 0;
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
	// "VRC6 Plus" (Vrc6Plus.h, in place of "VRC6" and "N163") is the VRC6 and the N163 with 4 channels (Channels
	// is 4 then), marked as VRC6 Plus; without it, a VRC6 Plus song stops being one (its chips can stay).
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
	// Every channel plays, muted ones too, but those Options leave silent.
	// The song stays as it is: the mixer, the mutes and the selected track too.
	virtual bool RenderTrack(int Track, double Seconds, const RenderOptions &Options, std::vector<float> &Left, std::vector<float> &Right,
		int &SampleRate, std::vector<std::pair<int, long long> > &Marks, std::string &Error) = 0;
	// Several renders of a track for Seconds (measure_mix: the mix and each part alone), each as RenderTrack renders
	// it with its own Options' Silent; their Tail and What are the first's. FamiTracker makes them in one pass: its
	// mixer makes each part alone as it makes the mix (CAPU::SetStems), sample for sample as a render of it alone.
	// Null, with the reason, when they can't be made.
	virtual std::unique_ptr<RenderSet> RenderParts(int Track, double Seconds, const std::vector<RenderOptions> &Renders,
		std::string &Error) = 0;
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
	// Undoes the newest change in the song's undo history (Ctrl+Z), Message saying what it undid: the label the tools gave
	// it (LabelUndo), or that it was an edit made in FamiTracker; false with the reason when there's none
	virtual bool Undo(std::string &Message) = 0;
	// The undo history as the tools see it, so undo can say what it undoes and a call that failed leaves nothing of its
	// own to undo: a count of the changes taken into it so far, which only grows (each change, or each group of them
	// from BeginGroup to EndGroup, counts one; undoing takes none back), 0 for a host without one; and a label for the
	// newest change, what the call that made it did
	virtual long long UndoSerial() { return 0; }
	virtual void LabelUndo(const std::string &Label) { (void)Label; }
	// Forgets the changes undone last, so redo (Ctrl+Y) can't bring them back: what a call that failed had changed, taken
	// back with Undo, would otherwise come back half-finished. Nothing for a host without a redo history
	virtual void ForgetRedo() {}
	// Saves the song, or a copy of it next to it (CopyName: a file name)
	virtual bool Save(const std::string &CopyName, std::string &Message) = 0;
	// Saves a song that has no file yet (SongInfo::FileName empty), for a client that can't use File > Save As: into the
	// Songs folder next to FamiTracker.exe (Documents\FamiTracker songs where FamiTracker's folder can't be written), in a
	// folder of its own named as the file, under the first of Names this computer can make (each a plain file name without
	// ".ftm", the last the plainest), as "Name 2", "Name 3"... (NumberedSongName) when a folder of that name is there:
	// never over a file. The song then is that file, so later saves go there and export_for_game's files next to it. Path:
	// the file, as the tools are given paths; false with the reason in Message
	virtual bool SaveNew(const std::vector<std::string> &Names, std::string &Path, std::string &Message)
	{
		(void)Names;
		(void)Path;
		Message = "This FamiTracker can't save a song that has no file yet.";
		return false;
	}
	// The files next to the song a client may fetch (IsExportFile in Network.h: the OGG files export_for_game saves), the
	// newest first, at most MAX_LISTED_EXPORTS; none when the song has no file yet. False with the reason when
	// the folder can't be read
	virtual bool ListExports(std::vector<SongFile> &Files, std::string &Error)
	{
		(void)Error;
		Files.clear();
		return true;
	}
};

}	// namespace ftapi

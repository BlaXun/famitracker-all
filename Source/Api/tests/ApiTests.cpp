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

// Tests for the Claude Code connection's core: JSON, HTTP framing, the MCP
// protocol, the musical helpers and every tool, against a song in memory.

#include "../Arrange.h"
#include "../Bridge.h"
#include "../BridgeSocket.h"
#include "../Chords.h"
#include "../DpcmKit.h"
#include "../Effects.h"
#include "../GameAudio.h"
#include "../Http.h"
#include "../Jingles.h"
#include "../Json.h"
#include "../Mcp.h"
#include "../Music.h"
#include "../Ogg.h"
#include "../Picture.h"
#include "../Png.h"
#include "../Presets.h"
#include "../References.h"
#include "../Song.h"
#include "../SoundEffects.h"
#include "../Tools.h"
#include "../Wavetables.h"
#include "../../Nsf/tests/TestNsf.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

using namespace ftapi;

namespace {

int g_Checks = 0, g_Failures = 0;

void Check(bool Condition, const char *What, int Line)
{
	++g_Checks;
	if (!Condition) {
		++g_Failures;
		std::printf("FAIL line %d: %s\n", Line, What);
	}
}

#define CHECK(x) Check((x), #x, __LINE__)
#define CHECK_EQ(a, b) Check((a) == (b), #a " == " #b, __LINE__)

bool Contains(const std::string &Text, const char *Part)
{
	return Text.find(Part) != std::string::npos;
}

#define CHECK_HAS(text, part) Check(Contains((text), (part)), "\"" part "\" in " #text, __LINE__)
#define CHECK_LACKS(text, part) Check(!Contains((text), (part)), "no \"" part "\" in " #text, __LINE__)

// A song in memory: 2A03 and VRC6 channels, 64-row patterns, 4 rows a beat,
// 16 a bar (4 bars a frame)
class FakeSong : public SongHost {
public:
	struct Clip {
		std::string Name;
		int Id;
		std::vector<int> Sources;
		std::vector<std::vector<Cell> > Columns;
	};

	// The module's tracks; the selected one plays from Rows and Info
	struct StoredTrack {
		std::string Name;
		int Speed, Tempo;
		std::vector<std::vector<Cell> > Rows;
	};

	SongInfo Info;
	std::vector<std::vector<Cell> > Rows;	// per channel, the selected track's
	std::vector<StoredTrack> Tracks;
	int Current;
	std::vector<Clip> Clips;
	std::map<std::string, std::string> Files;	// saved next to the song
	int RenderLatency;							// samples of silence before the song starts
	std::vector<std::vector<std::vector<Cell> > > History;
	std::vector<std::pair<int, int> > TempoUndo;
	int Writes, LastLink, PlayFirst, PlayRows, NextClipId;
	SongWrite LastWrite;
	bool PlayLoop = false;
	std::vector<bool> PlayMuted;
	PlayRequest LastPlay;
	bool Busy, Stopped;
	std::string Saved;

	explicit FakeSong(int Frames = 2) : Current(0), RenderLatency(400), Writes(0), LastLink(-2), PlayFirst(-1), PlayRows(-1), NextClipId(1), Busy(false),
		Stopped(false)
	{
		Info.Title = "Test song";
		Info.TrackName = "Town";
		Info.FileName = "test.ftm";
		Info.PatternLength = 64;
		Info.Frames = Frames;
		Info.FirstHighlight = 4;
		Info.SecondHighlight = 16;
		Info.Expansion = "VRC6";
		const char *const NAMES[] = { "Pulse 1", "Pulse 2", "Triangle", "Noise", "DPCM", "Pulse 1", "Pulse 2", "Sawtooth" };
		const int KINDS[] = { KIND_PULSE, KIND_PULSE, KIND_TRIANGLE, KIND_NOISE, KIND_DPCM, KIND_PULSE, KIND_PULSE, KIND_SAWTOOTH };
		for (int c = 0; c < 8; ++c) {
			ChannelInfo Channel;
			Channel.Name = NAMES[c];
			Channel.Chip = c < 5 ? "2A03" : "VRC6";
			Channel.Kind = KINDS[c];
			Channel.ChanId = c;
			Channel.EffectColumns = 2;
			Info.Channels.push_back(Channel);
			Rows.push_back(std::vector<Cell>(static_cast<size_t>(Frames * 64)));
		}
		AddInstrumentInfo(0, "Lead", "VRC6", true);
		AddInstrumentInfo(1, "Bass", "2A03", true);
		AddInstrumentInfo(2, "Square", "2A03", false);
		StoredTrack First;
		First.Name = Info.TrackName;
		Tracks.push_back(First);
	}

	// The selected track goes back into Tracks, Track comes out
	void Switch(int Track)
	{
		Tracks[Current].Name = Info.TrackName;
		Tracks[Current].Speed = Info.Speed;
		Tracks[Current].Tempo = Info.Tempo;
		Tracks[Current].Rows = Rows;
		Current = Track;
		Info.TrackName = Tracks[Track].Name;
		Info.Speed = Tracks[Track].Speed;
		Info.Tempo = Tracks[Track].Tempo;
		Rows = Tracks[Track].Rows;
	}

	// The notes a preset's looping arpeggio plays besides each note, as FamiTracker reports them
	static std::vector<int> LoopingArpeggio(const Preset &Sound)
	{
		std::vector<int> Out;
		for (size_t s = 0; s < Sound.Sequences2A03.size(); ++s) {
			const PresetSequence &Seq = Sound.Sequences2A03[s];
			if (Seq.Type != SEQUENCE_ARPEGGIO || Seq.Loop < 0 || Seq.Setting != SEQUENCE_ABSOLUTE)
				continue;
			bool Root = false;
			for (size_t i = static_cast<size_t>(Seq.Loop); i < Seq.Items.size(); ++i) {
				if (Seq.Items[i] == 0)
					Root = true;
				else if (std::find(Out.begin(), Out.end(), Seq.Items[i]) == Out.end())
					Out.push_back(Seq.Items[i]);
			}
			if (Out.size() == 1 && !Root)
				Out.clear();
			std::sort(Out.begin(), Out.end());
		}
		return Out;
	}

	void AddInstrumentInfo(int Index, const char *Name, const char *Chip, bool Releases)
	{
		InstrumentInfo I;
		I.Index = Index;
		I.Name = Name;
		I.Chip = Chip;
		I.Releases = Releases;
		Info.Instruments.push_back(I);
	}

	// Puts a note (by name) on a row
	void Put(int Channel, int Row, const char *Pitch, int Instrument = NO_INSTRUMENT, int Volume = -1)
	{
		int Value = 0;
		if (!ParsePitch(Pitch, Value)) {
			std::string Drum;
			ParseNoisePitch(Pitch, Value, Drum);
		}
		Cell &C = Rows[Channel][Row];
		C.SetPitch(Value);
		if (Instrument != NO_INSTRUMENT)
			C.Instrument = static_cast<unsigned char>(Instrument);
		if (Volume >= 0)
			C.Volume = static_cast<unsigned char>(Volume);
	}

	void End(int Channel, int Row, int Note = NOTE_HALT)
	{
		Rows[Channel][Row].Note = static_cast<unsigned char>(Note);
	}

	bool GetSong(SongInfo &Out, std::string &) override
	{
		Info.Frames = static_cast<int>(Rows[0].size()) / Info.PatternLength;
		Info.Track = Current;
		Info.Tracks = static_cast<int>(Tracks.size());
		Info.TrackNames.clear();
		for (size_t t = 0; t < Tracks.size(); ++t)
			Info.TrackNames.push_back(static_cast<int>(t) == Current ? Info.TrackName : Tracks[t].Name);
		Info.Clips.clear();
		for (size_t i = 0; i < Clips.size(); ++i) {
			ClipInfo C;
			C.Index = static_cast<int>(i);
			C.Id = Clips[i].Id;
			C.Name = Clips[i].Name;
			C.Columns = static_cast<int>(Clips[i].Columns.size());
			C.Rows = static_cast<int>(Clips[i].Columns[0].size());
			C.Sources = Clips[i].Sources;
			Info.Clips.push_back(C);
		}
		Out = Info;
		return true;
	}

	void ReadCells(int Channel, int First, int Count, std::vector<Cell> &Out) override
	{
		Out.clear();
		for (int r = First; r < First + Count; ++r)
			Out.push_back(r < static_cast<int>(Rows[Channel].size()) ? Rows[Channel][r] : Cell());
	}

	bool ReadClip(int Index, std::vector<std::vector<Cell> > &Columns) override
	{
		if (Index < 0 || Index >= static_cast<int>(Clips.size()))
			return false;
		Columns = Clips[Index].Columns;
		return true;
	}

	bool CanChange(std::string &Why) override
	{
		if (Busy)
			Why = "A dialog is open in FamiTracker.";
		return !Busy;
	}

	// As FamiTracker writes them (CInsertRecordingAction::Redo, which this
	// follows): frames moved first; then exact parts as they are, and the
	// others keeping the song's own speed, jumps, skips and halts in their
	// rows, with their effects around them in the columns that show
	bool WriteSong(const std::vector<ChannelCells> &Parts, const SongWrite &Write, std::string &Error) override
	{
		const size_t Length = static_cast<size_t>(Info.PatternLength);
		const int Frames = static_cast<int>(Rows[0].size() / Length);
		if (Parts.empty() && Write.MoveBy == 0) {
			Error = "There was nothing to write.";
			return false;
		}
		if (Write.MoveBy != 0 && (Write.MoveFrame < 0 || Write.MoveFrame > Frames || (Write.MoveBy < 0 &&
			Write.MoveFrame - Write.MoveBy > Frames) || Frames + Write.MoveBy < 1 || Frames + Write.MoveBy > Info.MaxFrames)) {
			Error = "Those frames can't move.";
			return false;
		}
		History.push_back(Rows);
		const size_t At = static_cast<size_t>(Write.MoveFrame) * Length;
		for (size_t c = 0; c < Rows.size(); ++c) {
			if (Write.MoveBy > 0)
				Rows[c].insert(Rows[c].begin() + static_cast<std::ptrdiff_t>(At), static_cast<size_t>(Write.MoveBy) * Length, Cell());
			else if (Write.MoveBy < 0)
				Rows[c].erase(Rows[c].begin() + static_cast<std::ptrdiff_t>(At),
					Rows[c].begin() + static_cast<std::ptrdiff_t>(At + static_cast<size_t>(-Write.MoveBy) * Length));
		}
		const size_t First = static_cast<size_t>(Write.First);
		size_t Needed = Rows[0].size();
		for (size_t p = 0; p < Parts.size(); ++p)
			Needed = std::max(Needed, First + Parts[p].Cells.size());
		Needed = (Needed + Length - 1) / Length * Length;
		for (size_t c = 0; c < Rows.size(); ++c)
			Rows[c].resize(Needed);
		for (size_t p = 0; p < Parts.size(); ++p) {
			const int Shown = Info.Channels[static_cast<size_t>(Parts[p].Channel)].EffectColumns;
			for (size_t r = 0; r < Parts[p].Cells.size(); ++r) {
				Cell &Row = Rows[Parts[p].Channel][First + r];
				const Cell &New = Parts[p].Cells[r];
				if (Parts[p].Exact) {
					Row = New;
					continue;
				}
				for (int k = 0; k < EFFECT_COLUMNS; ++k)
					if (!IsSongEffect(Row.Effect[k])) {
						Row.Effect[k] = EFF_NONE;
						Row.Param[k] = 0;
					}
				if (New.IsEmpty()) {
					Row.Note = NOTE_NONE;
					Row.Octave = 0;
					Row.Instrument = NO_INSTRUMENT;
					Row.Volume = NO_VOLUME;
					continue;
				}
				Row.Note = New.Note;
				Row.Octave = New.Octave;
				Row.Instrument = New.Instrument;
				Row.Volume = New.Volume;
				for (int k = 0; k < EFFECT_COLUMNS; ++k) {
					if (New.Effect[k] == EFF_NONE || (IsSongEffect(New.Effect[k]) && HasSame(Row, New.Effect[k], New.Param[k])))
						continue;
					int Column = k;
					if (IsSongEffect(Row.Effect[Column])) {
						Column = -1;
						for (int j = 0; j < Shown && j < EFFECT_COLUMNS && Column < 0; ++j)
							if (Row.Effect[j] == EFF_NONE)
								Column = j;
						if (Column < 0)
							continue;
					}
					Row.Effect[Column] = New.Effect[k];
					Row.Param[Column] = New.Param[k];
				}
			}
		}
		// Effects in columns a channel doesn't show don't play: it shows more, as CInsertRecordingAction does
		for (size_t p = 0; p < Parts.size(); ++p) {
			int Needed = 0;
			for (size_t r = 0; r < Parts[p].Cells.size(); ++r)
				for (int k = EFFECT_COLUMNS - 1; k >= Needed; --k)
					if (Parts[p].Cells[r].Effect[k] != EFF_NONE) {
						Needed = k + 1;
						break;
					}
			int &Shown = Info.Channels[static_cast<size_t>(Parts[p].Channel)].EffectColumns;
			Shown = std::max(Shown, Needed);
		}
		++Writes;
		LastLink = Write.LinkClipId;
		LastWrite = Write;
		return true;
	}

	static bool HasSame(const Cell &C, int Effect, int Param)
	{
		for (int k = 0; k < EFFECT_COLUMNS; ++k)
			if (C.Effect[k] == Effect && C.Param[k] == Param)
				return true;
		return false;
	}

	bool AddClip(std::string &Name, const std::vector<int> &Sources, const std::vector<std::vector<Cell> > &Columns, std::string &) override
	{
		std::string Unique = Name;
		for (int n = 2; ; ++n) {
			bool Taken = false;
			for (size_t i = 0; i < Clips.size(); ++i)
				if (Clips[i].Name == Unique)
					Taken = true;
			if (!Taken)
				break;
			char Suffix[16];
			std::snprintf(Suffix, sizeof(Suffix), " %d", n);
			Unique = Name + Suffix;
		}
		Name = Unique;
		Clip C;
		C.Name = Name;
		C.Id = NextClipId++;
		C.Sources = Sources;
		C.Columns = Columns;
		Clips.push_back(C);
		return true;
	}

	int AddInstrument(const Preset &Sound, const std::string &Chip, bool Sawtooth, std::string &) override
	{
		const std::string Name = Sound.InstrumentName(Sawtooth);
		int Next = 0;
		for (size_t i = 0; i < Info.Instruments.size(); ++i) {
			if (Info.Instruments[i].Name == Name && Info.Instruments[i].Chip == Chip)
				return Info.Instruments[i].Index;
			Next = std::max(Next, Info.Instruments[i].Index + 1);
		}
		AddInstrumentInfo(Next, Name.c_str(), Chip.c_str(), Sound.Releases());
		Info.Instruments.back().Arpeggio = LoopingArpeggio(Sound);
		Made[Next] = Sound;
		return Next;
	}

	std::map<int, Preset> Made;		// instrument -> the preset it was made from
	std::map<int, WaveSound> MadeWaves;	// N163 instrument -> its sound

	int AddN163Instrument(const WaveSound &Sound, int WavePos, std::string &Error) override
	{
		if (Info.Expansion != "N163") {
			Error = "This song has no N163 channels.";
			return -1;
		}
		int Next = 0;
		for (size_t i = 0; i < Info.Instruments.size(); ++i) {
			if (Info.Instruments[i].Name == Sound.InstrumentName() && Info.Instruments[i].Chip == "N163")
				return Info.Instruments[i].Index;
			Next = std::max(Next, Info.Instruments[i].Index + 1);
		}
		AddInstrumentInfo(Next, Sound.InstrumentName().c_str(), "N163", Sound.Releases());
		Info.Instruments.back().WavePos = WavePos;
		Info.Instruments.back().WaveSize = N163_WAVE_SIZE;
		MadeWaves[Next] = Sound;
		return Next;
	}
	std::vector<std::string> Opened;	// the files LocalPath was asked about

	std::string LocalPath(const std::string &Path) override
	{
		Opened.push_back(Path);
		return Path;
	}
	std::map<int, int> Vrc7Patches;	// instrument -> patch
	std::map<int, std::vector<int> > Vrc7Regs;	// instrument -> its custom patch's registers

	int AddVrc7CustomInstrument(const std::vector<int> &Regs, const std::string &Name, std::string &Error) override
	{
		const size_t Before = Info.Instruments.size();
		const int Index = AddVrc7Instrument(1, Name, Error);
		if (Index >= 0 && Info.Instruments.size() > Before) {
			Vrc7Patches[Index] = 0;
			Vrc7Regs[Index] = Regs;
		}
		return Index;
	}

	int AddVrc7Instrument(int Patch, const std::string &Name, std::string &Error) override
	{
		if (Info.Expansion != "VRC7") {
			Error = "This song has no VRC7 channels.";
			return -1;
		}
		int Next = 0;
		for (size_t i = 0; i < Info.Instruments.size(); ++i) {
			if (Info.Instruments[i].Name == Name && Info.Instruments[i].Chip == "VRC7")
				return Info.Instruments[i].Index;
			Next = std::max(Next, Info.Instruments[i].Index + 1);
		}
		AddInstrumentInfo(Next, Name.c_str(), "VRC7", false);	// FamiTracker's VRC7 instruments don't release
		Vrc7Patches[Next] = Patch;
		return Next;
	}

	// An expansion chip's channels as FamiTracker names them
	static std::vector<ChannelInfo> ChipChannels(const std::string &Chip, int Namco)
	{
		std::vector<ChannelInfo> Out;
		auto Add = [&](const std::string &Name, int Kind, int Id) {
			ChannelInfo Channel;
			Channel.Name = Name;
			Channel.Chip = Chip;
			Channel.Kind = Kind;
			Channel.ChanId = Id;
			Out.push_back(Channel);
		};
		if (Chip == "VRC6") {
			Add("Pulse 1", KIND_PULSE, 5);
			Add("Pulse 2", KIND_PULSE, 6);
			Add("Sawtooth", KIND_SAWTOOTH, 7);
		}
		else if (Chip == "MMC5") {
			Add("Pulse 1", KIND_PULSE, 8);
			Add("Pulse 2", KIND_PULSE, 9);
		}
		else if (Chip == "N163") {
			for (int i = 0; i < Namco; ++i)
				Add("Namco " + std::to_string(i + 1), KIND_OTHER, 11 + i);
		}
		else if (Chip == "FDS")
			Add("FDS", KIND_OTHER, 19);
		else if (Chip == "VRC7") {
			for (int i = 0; i < 6; ++i)
				Add("FM Channel " + std::to_string(i + 1), KIND_FM, 20 + i);
		}
		return Out;
	}

	// The channels switching to Chip clears (see SongHost)
	std::vector<size_t> Leaving(const std::string &Chip, int Namco) const
	{
		std::vector<size_t> Out;
		int Kept = 0;
		for (size_t c = 0; c < Info.Channels.size(); ++c) {
			if (Info.Channels[c].Chip == "2A03")
				continue;
			if (Chip == Info.Expansion && (Chip != "N163" || Kept++ < Namco))
				continue;
			Out.push_back(c);
		}
		return Out;
	}

	int ExpansionNotes(const std::string &Chip, int Channels, std::vector<int> &Found) override
	{
		Found.clear();
		const std::vector<size_t> Going = Leaving(Chip, Channels);
		int Total = 0;
		for (size_t t = 0; t < Tracks.size(); ++t) {
			const std::vector<std::vector<Cell> > &Cells = static_cast<int>(t) == Current ? Rows : Tracks[t].Rows;
			int Count = 0;
			for (size_t i = 0; i < Going.size(); ++i) {
				for (size_t r = 0; Going[i] < Cells.size() && r < Cells[Going[i]].size(); ++r)
					Count += Cells[Going[i]][r].HasNote() ? 1 : 0;
			}
			if (Count > 0)
				Found.push_back(static_cast<int>(t));
			Total += Count;
		}
		return Total;
	}

	bool SetExpansion(const std::string &Chip, int Channels, int &Moved, int &Lost, std::string &) override
	{
		Moved = Lost = 0;
		Switch(Current);	// the selected track's cells into Tracks
		const std::vector<size_t> Going = Leaving(Chip, Channels);
		// The effects that steer the song, from the channels that go
		struct Steering { size_t Track, Row; unsigned char Number, Param; };
		std::vector<Steering> Effects;
		for (size_t t = 0; t < Tracks.size(); ++t) {
			for (size_t i = 0; i < Going.size(); ++i) {
				const std::vector<Cell> &Cells = Tracks[t].Rows[Going[i]];
				for (size_t r = 0; r < Cells.size(); ++r)
					for (int k = 0; k < Info.Channels[Going[i]].EffectColumns && k < EFFECT_COLUMNS; ++k)
						if (IsSongEffect(Cells[r].Effect[k]))
							Effects.push_back(Steering{ t, r, Cells[r].Effect[k], Cells[r].Param[k] });
			}
		}
		// The 2A03's channels and the ones that stay, then the new chip's
		std::vector<ChannelInfo> Next;
		std::vector<int> From;
		for (size_t c = 0; c < Info.Channels.size(); ++c) {
			if (Info.Channels[c].Chip == "2A03" || (Info.Channels[c].Chip == Chip && std::find(Going.begin(), Going.end(), c) == Going.end())) {
				Next.push_back(Info.Channels[c]);
				From.push_back(static_cast<int>(c));
			}
		}
		const std::vector<ChannelInfo> Added = ChipChannels(Chip, Channels);
		for (size_t i = Next.size() - 5; i < Added.size(); ++i) {
			Next.push_back(Added[i]);
			From.push_back(-1);
		}
		for (size_t t = 0; t < Tracks.size(); ++t) {
			const size_t Size = Tracks[t].Rows[0].size();
			std::vector<std::vector<Cell> > Cells;
			for (size_t c = 0; c < Next.size(); ++c)
				Cells.push_back(From[c] >= 0 ? Tracks[t].Rows[static_cast<size_t>(From[c])] : std::vector<Cell>(Size));
			Tracks[t].Rows.swap(Cells);
		}
		Info.Channels = Next;
		// They move to the last channel, each in a free effect column
		ChannelInfo &Last = Info.Channels.back();
		for (size_t e = 0; e < Effects.size(); ++e) {
			Cell &C = Tracks[Effects[e].Track].Rows.back()[Effects[e].Row];
			int Free = -1;
			bool There = false;
			for (int k = 0; k < EFFECT_COLUMNS; ++k) {
				if (C.Effect[k] == Effects[e].Number && C.Param[k] == Effects[e].Param)
					There = true;
				else if (C.Effect[k] == EFF_NONE && Free < 0)
					Free = k;
			}
			if (There) {
				++Moved;
				continue;
			}
			if (Free < 0) {
				++Lost;
				continue;
			}
			C.Effect[Free] = Effects[e].Number;
			C.Param[Free] = Effects[e].Param;
			Last.EffectColumns = std::max(Last.EffectColumns, Free + 1);
			++Moved;
		}
		Info.Expansion = Chip;
		if (!Chip.empty())
			Info.FrameRate = 60;
		Rows = Tracks[Current].Rows;
		return true;
	}

	bool SetNames(const SongNames &Names, std::string &) override
	{
		if (Names.HasTitle)
			Info.Title = Names.Title;
		if (Names.HasAuthor)
			Info.Author = Names.Author;
		if (Names.HasCopyright)
			Info.Copyright = Names.Copyright;
		if (Names.HasTrack)
			Info.TrackName = Names.Track;
		return true;
	}

	bool SetSpeedTempo(int Speed, int Tempo, std::string &) override
	{
		TempoUndo.push_back(std::make_pair(Info.Speed, Info.Tempo));
		Info.Speed = Speed;
		Info.Tempo = Tempo;
		return true;
	}

	bool SelectTrack(int Track, std::string &Error) override
	{
		if (Track < 0 || Track >= static_cast<int>(Tracks.size())) {
			Error = "No such track.";
			return false;
		}
		Switch(Track);
		return true;
	}

	int AddTrack(const std::string &Name, const std::vector<std::vector<Cell> > &Channels, int Speed, int Tempo, std::string &Error) override
	{
		if (Tracks.size() >= 64) {
			Error = "The module has 64 tracks, FamiTracker's most.";
			return -1;
		}
		StoredTrack Track;
		Track.Name = Name;
		Track.Speed = Speed;
		Track.Tempo = Tempo;
		size_t Length = 64;
		for (size_t c = 0; c < Channels.size(); ++c)
			Length = std::max(Length, (Channels[c].size() + 63) / 64 * 64);
		Track.Rows = Channels;
		Track.Rows.resize(Rows.size());
		for (size_t c = 0; c < Track.Rows.size(); ++c)
			Track.Rows[c].resize(Length);
		Tracks.push_back(Track);
		Switch(static_cast<int>(Tracks.size()) - 1);
		return Current;
	}

	// Plays the song as square waves, a little like the 2A03: each channel's
	// note at its volume, the noise channel from a free-running noise
	// generator, 8000 samples a second
	bool RenderTrack(int Track, double Seconds, std::vector<float> &Samples, int &SampleRate,
		std::vector<std::pair<int, long long> > &Marks, std::string &Error) override
	{
		if (Track != Current) {
			Error = "Only the selected track renders here.";
			return false;
		}
		SampleRate = 8000;
		Samples.assign(static_cast<size_t>(RenderLatency), 0.0f);
		Marks.clear();
		std::vector<int> Columns;
		for (size_t c = 0; c < Rows.size(); ++c)
			Columns.push_back(Info.Channels[c].EffectColumns);
		const SongFlow Flow = FollowSong(Rows, Columns, Info.PatternLength, static_cast<int>(Rows[0].size()) / Info.PatternLength);
		int EndTick = 0;
		const std::vector<TimedRow> Timed = TimeRows(Rows, Columns, Flow, Info.PatternLength, Info.Speed, Info.Tempo, Info.SplitPoint,
			Info.FrameRate, 6, EndTick);
		const long long Total = static_cast<long long>(Seconds * SampleRate) + RenderLatency;
		std::vector<int> Pitch(Rows.size(), -1), Volume(Rows.size(), 15);
		std::vector<double> Phase(Rows.size(), 0.0);
		unsigned int Noise = 1;
		for (size_t i = 0; i < Timed.size() && static_cast<long long>(Samples.size()) < Total; ++i) {
			Marks.push_back(std::make_pair(Timed[i].Row, static_cast<long long>(Samples.size())));
			for (size_t c = 0; c < Rows.size(); ++c) {
				const Cell &C = Rows[c][static_cast<size_t>(Timed[i].Row)];
				if (C.HasNote())
					Pitch[c] = C.Pitch();
				else if (C.IsEnd())
					Pitch[c] = -1;
				if (C.Volume != NO_VOLUME)
					Volume[c] = C.Volume;
			}
			const long long Until = RenderLatency + (i + 1 < Timed.size() ? Timed[i + 1].Tick : EndTick) * SampleRate / Info.FrameRate;
			while (static_cast<long long>(Samples.size()) < std::min(Until, Total)) {
				double Sum = 0.0;
				for (size_t c = 0; c < Rows.size(); ++c) {
					if (Pitch[c] < 0)
						continue;
					if (Info.Channels[c].Kind == KIND_NOISE) {
						Noise = (Noise >> 1) | (((Noise ^ (Noise >> 1)) & 1u) << 14);
						Sum += (Noise & 1u ? 0.05 : -0.05) * Volume[c] / 15.0;
						continue;
					}
					const double Frequency = 440.0 * std::pow(2.0, (Pitch[c] - 57) / 12.0);
					Phase[c] = std::fmod(Phase[c] + Frequency / SampleRate, 1.0);
					Sum += (Phase[c] < 0.5 ? 0.05 : -0.05) * Volume[c] / 15.0;
				}
				Samples.push_back(static_cast<float>(Sum));
			}
		}
		return true;
	}

	bool SaveFile(const std::string &FileName, const std::string &Bytes, std::string &Path, std::string &) override
	{
		Files[FileName] = Bytes;
		Path = "C:\\Music\\" + FileName;
		return true;
	}

	bool ReadFile(const std::string &FileName, std::string &Bytes, std::string &) override
	{
		std::map<std::string, std::string>::const_iterator it = Files.find(FileName);
		if (it == Files.end())
			return false;
		Bytes = it->second;
		return true;
	}

	std::vector<std::string> Kits;	// the kits of samples added

	int AddDpcmKit(const DpcmKit &Kit, std::string &) override
	{
		int Next = 0;
		for (size_t i = 0; i < Info.Instruments.size(); ++i) {
			if (Info.Instruments[i].Name == Kit.Name && Info.Instruments[i].Chip == "2A03")
				return Info.Instruments[i].Index;
			Next = std::max(Next, Info.Instruments[i].Index + 1);
		}
		AddInstrumentInfo(Next, Kit.Name.c_str(), "2A03", false);
		Kits.push_back(Kit.Name);
		return Next;
	}

	int OutdatedKitSamples = 0;		// samples of a kit an older version made, brought up to date once

	int RefreshDpcmKit(const DpcmKit &Kit, std::string &) override
	{
		bool Has = false;
		for (size_t i = 0; i < Info.Instruments.size(); ++i)
			Has = Has || (Info.Instruments[i].Name == Kit.Name && Info.Instruments[i].Chip == "2A03");
		const int Refreshed = Has ? OutdatedKitSamples : 0;
		if (Has)
			OutdatedKitSamples = 0;
		return Refreshed;
	}

	std::map<std::string, std::string> Library;		// the reference library's files
	std::string OpenedText;							// the module OpenText was given

	bool ListLibrary(std::vector<std::string> &Names, std::string &) override
	{
		Names.clear();
		for (std::map<std::string, std::string>::const_iterator it = Library.begin(); it != Library.end(); ++it)
			Names.push_back(it->first);
		return true;
	}

	bool ReadLibrary(const std::string &Name, std::string &Bytes, std::string &) override
	{
		std::map<std::string, std::string>::const_iterator it = Library.find(Name);
		if (it == Library.end())
			return false;
		Bytes = it->second;
		return true;
	}

	bool SaveLibrary(const std::string &Name, const std::string &Bytes, std::string &Path, std::string &) override
	{
		Library[Name] = Bytes;
		Path = "C:\\Games\\FamiTracker\\References\\" + Name;
		return true;
	}

	bool DeleteLibrary(const std::string &Name, std::string &Error) override
	{
		if (Library.erase(Name) == 0) {
			Error = "There's no such file.";
			return false;
		}
		return true;
	}

	std::string LatestTake;						// the newest recording, "" for none
	bool LatestRecording(std::string &Path, std::string &Error) override
	{
		if (LatestTake.empty()) {
			Error = "There are no recordings yet in C:\\Games\\FamiTracker\\Recordings.";
			return false;
		}
		Path = LatestTake;
		return true;
	}

	bool OpenText(const std::string &Text, std::string &) override
	{
		OpenedText = Text;
		Info.Modified = false;
		return true;
	}

	bool Play(const PlayRequest &Request, std::string &) override
	{
		PlayFirst = Request.First;
		PlayRows = Request.Rows;
		PlayLoop = Request.Loop;
		PlayMuted = Request.Muted;
		LastPlay = Request;
		if (Request.ReturnTrack >= 0)
			Switch(Request.ReturnTrack);	// as when it's over
		return true;
	}

	void Stop() override { Stopped = true; }

	bool Undo(std::string &Message) override
	{
		if (History.empty())
			return false;
		Rows = History.back();
		History.pop_back();
		Message = "Undid it.";
		return true;
	}

	bool Save(const std::string &CopyName, std::string &Message) override
	{
		Saved = CopyName.empty() ? "test.ftm" : CopyName + ".ftm";
		Message = "Saved " + Saved + ".";
		return true;
	}
};

Json Parse(const std::string &Text)
{
	Json Value;
	std::string Error;
	if (!ParseJson(Text, Value, &Error))
		std::printf("JSON error: %s in %s\n", Error.c_str(), Text.c_str());
	return Value;
}

ToolResult Run(FakeSong &Song, const char *Tool, const std::string &Arguments)
{
	ToolSet Tools(Song);
	return Tools.Call(Tool, Parse(Arguments));
}

// The notes a channel holds, as text: "0:C-4 4:=== ..."
std::string Notes(const FakeSong &Song, int Channel, int First = 0, int End = -1)
{
	std::string Out;
	const std::vector<Cell> &Rows = Song.Rows[Channel];
	if (End < 0)
		End = static_cast<int>(Rows.size());
	for (int r = First; r < End && r < static_cast<int>(Rows.size()); ++r) {
		const Cell &C = Rows[r];
		if (C.Note == NOTE_NONE)
			continue;
		char Text[32];
		if (C.HasNote())
			std::snprintf(Text, sizeof(Text), "%d:%s ", r, Song.Info.Channels[Channel].Kind == KIND_NOISE ?
				NoisePitchName(C.Pitch()).c_str() : PitchName(C.Pitch()).c_str());
		else
			std::snprintf(Text, sizeof(Text), "%d:%s ", r, C.Note == NOTE_RELEASE ? "===" : "---");
		Out += Text;
	}
	return Out;
}

bool HasEffectAt(const FakeSong &Song, int Channel, int Row, int Effect, int Param)
{
	const Cell &C = Song.Rows[Channel][Row];
	for (int k = 0; k < EFFECT_COLUMNS; ++k)
		if (C.Effect[k] == Effect && C.Param[k] == Param)
			return true;
	return false;
}

// JSON

void TestJson()
{
	Json Value;
	CHECK(ParseJson("{\"a\": [1, 2.5, -3e2, true, false, null, \"x\\ny\\u00e9\\ud83c\\udfb5\"], \"b\": {}}", Value));
	CHECK(Value.IsObject());
	const Json *pA = Value.Get("a");
	CHECK(pA != 0 && pA->Size() == 7);
	CHECK(pA->At(0).IsInteger() && pA->At(0).Integer() == 1);
	CHECK(pA->At(1).Number() == 2.5);
	CHECK(pA->At(2).Number() == -300.0);
	CHECK(pA->At(3).Bool() && !pA->At(4).Bool() && pA->At(5).IsNull());
	CHECK_EQ(pA->At(6).String(), std::string("x\ny\xC3\xA9\xF0\x9F\x8E\xB5"));
	CHECK_EQ(WriteJson(Value), std::string("{\"a\":[1,2.5,-3e2,true,false,null,\"x\\ny\xC3\xA9\xF0\x9F\x8E\xB5\"],\"b\":{}}"));

	// Numbers keep their text (request IDs), made ones are written plainly
	CHECK_EQ(WriteJson(Parse("12345678901234567890")), std::string("12345678901234567890"));
	CHECK_EQ(WriteJson(Json(42)), std::string("42"));
	CHECK_EQ(WriteJson(Json(0.5)), std::string("0.5"));

	// Strict: these are all errors
	const char *const BAD[] = { "", "{", "[1,]", "{\"a\" 1}", "01", "1.", "-", "\"\\x\"", "\"a", "tru", "[1] 2", "{\"a\":1,}",
		"\"\x01\"", "NaN" };
	for (size_t i = 0; i < sizeof(BAD) / sizeof(BAD[0]); ++i) {
		std::string Error;
		Json Out;
		Check(!ParseJson(BAD[i], Out, &Error) && !Error.empty(), BAD[i], __LINE__);
	}
	// Too deep
	CHECK(!ParseJson(std::string(100, '[') + std::string(100, ']'), Value));
	CHECK(ParseJson(std::string(50, '[') + std::string(50, ']'), Value));

	// Writing escapes control characters and repairs bad UTF-8
	CHECK_EQ(WriteJson(Json(std::string("a\x01\"\\b"))), std::string("\"a\\u0001\\\"\\\\b\""));
	CHECK_EQ(WriteJson(Json(std::string("caf\xE9"))), std::string("\"caf\xEF\xBF\xBD\""));
	CHECK_EQ(ValidUtf8("ok\xC3\xA9\xFF"), std::string("ok\xC3\xA9\xEF\xBF\xBD"));

	// A lone surrogate becomes U+FFFD
	CHECK(ParseJson("\"\\ud83c x\"", Value));
	CHECK_EQ(Value.String(), std::string("\xEF\xBF\xBD x"));

	// Objects: members replaced in place, order kept
	Json Object = Json::MakeObject();
	Object.Set("z", 1).Set("a", 2).Set("z", 3);
	CHECK_EQ(WriteJson(Object), std::string("{\"z\":3,\"a\":2}"));
	CHECK(Parse("{\"x\":[1,{\"y\":2}]}") == Parse("{ \"x\" : [ 1 , { \"y\" : 2 } ] }"));
}

// HTTP

void TestHttp()
{
	{
		HttpRequestParser Parser;
		const std::string Bytes = "POST /mcp HTTP/1.1\r\nHost: 127.0.0.1:46600\r\nContent-Type: application/json\r\n"
			"Content-Length: 7\r\n\r\n{\"a\":1}";
		// Byte by byte
		HttpRequestParser::Result Result = HttpRequestParser::NEED_MORE;
		for (size_t i = 0; i < Bytes.size(); ++i) {
			Result = Parser.Feed(&Bytes[i], 1);
			if (i + 1 < Bytes.size())
				CHECK(Result == HttpRequestParser::NEED_MORE);
		}
		CHECK(Result == HttpRequestParser::COMPLETE);
		const HttpRequest Request = Parser.Take();
		CHECK_EQ(Request.Method, std::string("POST"));
		CHECK_EQ(Request.Path(), std::string("/mcp"));
		CHECK_EQ(Request.Body, std::string("{\"a\":1}"));
		CHECK(Request.Header("content-type") != 0 && *Request.Header("CONTENT-TYPE") == "application/json");
		CHECK(Request.KeepAlive());
	}
	{
		// Chunked, then a second request in the same bytes
		HttpRequestParser Parser;
		const std::string Bytes = "POST /mcp?x=1 HTTP/1.1\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n"
			"3;ext=1\r\nabc\r\n2\r\nde\r\n0\r\nTrailer: x\r\n\r\n"
			"GET /mcp HTTP/1.1\r\n\r\n";
		CHECK(Parser.Feed(Bytes.data(), Bytes.size()) == HttpRequestParser::COMPLETE);
		const HttpRequest First = Parser.Take();
		CHECK_EQ(First.Body, std::string("abcde"));
		CHECK_EQ(First.Path(), std::string("/mcp"));
		CHECK(!First.KeepAlive());
		CHECK(Parser.Feed() == HttpRequestParser::COMPLETE);
		const HttpRequest Second = Parser.Take();
		CHECK_EQ(Second.Method, std::string("GET"));
		CHECK(Second.Body.empty());
		CHECK(Parser.Feed() == HttpRequestParser::NEED_MORE);
	}
	{
		HttpRequestParser Parser(16);
		const std::string Bytes = "POST / HTTP/1.1\r\nContent-Length: 17\r\n\r\n";
		CHECK(Parser.Feed(Bytes.data(), Bytes.size()) == HttpRequestParser::BAD);
		CHECK_EQ(Parser.ErrorStatus(), 413);
	}
	{
		HttpRequestParser Parser;
		const std::string Bytes = "POST / HTTP/1.1\r\nContent-Length: x\r\n\r\n";
		CHECK(Parser.Feed(Bytes.data(), Bytes.size()) == HttpRequestParser::BAD);
		CHECK_EQ(Parser.ErrorStatus(), 400);
	}
	{
		HttpRequestParser Parser(1024, 64);
		const std::string Bytes = "POST / HTTP/1.1\r\nX-Long: " + std::string(100, 'a');
		CHECK(Parser.Feed(Bytes.data(), Bytes.size()) == HttpRequestParser::BAD);
		CHECK_EQ(Parser.ErrorStatus(), 431);
	}
	{
		HttpRequestParser Parser;
		const std::string Bytes = "POST / HTTP/2.0\r\n\r\n";
		CHECK(Parser.Feed(Bytes.data(), Bytes.size()) == HttpRequestParser::BAD);
		CHECK_EQ(Parser.ErrorStatus(), 505);
	}
	{
		// Expect: 100-continue asks for the go-ahead before the body
		HttpRequestParser Parser;
		const std::string Head = "POST /mcp HTTP/1.1\r\nExpect: 100-continue\r\nContent-Length: 2\r\n\r\n";
		CHECK(Parser.Feed(Head.data(), Head.size()) == HttpRequestParser::NEED_MORE);
		CHECK(Parser.TakeContinue());
		CHECK(!Parser.TakeContinue());
		CHECK(Parser.Feed("{}", 2) == HttpRequestParser::COMPLETE);
		CHECK_EQ(Parser.Take().Body, std::string("{}"));
	}
	{
		HttpResponse Response;
		Response.Status = 202;
		const std::string Bytes = FormatHttpResponse(Response, true);
		CHECK_HAS(Bytes, "HTTP/1.1 202 Accepted\r\n");
		CHECK_HAS(Bytes, "Content-Length: 0\r\n");
		CHECK_HAS(Bytes, "Connection: keep-alive\r\n");
		CHECK(Bytes.size() >= 4 && Bytes.compare(Bytes.size() - 4, 4, "\r\n\r\n") == 0);
	}
}

// Music

void TestNames()
{
	for (int p = 0; p < PITCHES; ++p) {
		int Back = -1;
		Check(ParsePitch(PitchName(p), Back) && Back == p, PitchName(p).c_str(), __LINE__);
	}
	int Pitch = -1;
	CHECK(ParsePitch("C-3", Pitch) && Pitch == 36);
	CHECK(ParsePitch("c3", Pitch) && Pitch == 36);
	CHECK(ParsePitch("Db4", Pitch) && Pitch == 49);
	CHECK(ParsePitch("Bb-2", Pitch) && Pitch == 34);
	CHECK(ParsePitch("E\xE2\x99\xAD" "3", Pitch) && Pitch == 39);
	CHECK(ParsePitch("Cb3", Pitch) && Pitch == 35);
	CHECK(ParsePitch(" A#0 ", Pitch) && Pitch == 10);
	CHECK(!ParsePitch("H-4", Pitch));
	CHECK(!ParsePitch("C-8", Pitch));
	CHECK(!ParsePitch("Cb0", Pitch));
	CHECK(!ParsePitch("C", Pitch));
	CHECK(!ParsePitch("C-44", Pitch));
	CHECK_EQ(PitchName(57), std::string("A-4"));
	CHECK_EQ(PitchName(1), std::string("C#0"));

	std::string Drum;
	CHECK(ParseNoisePitch("kick", Pitch, Drum) && Pitch == 4 && Drum == "kick");
	CHECK(ParseNoisePitch("Hi-Hat", Pitch, Drum) && Pitch == 14 && Drum == "hi-hat");
	CHECK(ParseNoisePitch("A-#", Pitch, Drum) && Pitch == 10 && Drum.empty());
	CHECK(ParseNoisePitch("noise 12", Pitch, Drum) && Pitch == 12);
	CHECK(ParseNoisePitch("f", Pitch, Drum) && Pitch == 15);
	CHECK(!ParseNoisePitch("G-#", Pitch, Drum));
	CHECK(!ParseNoisePitch("16", Pitch, Drum));
	CHECK_EQ(NoisePitchName(10), std::string("A-#"));

	int Effect = 0, Param = 0;
	CHECK(ParseEffect("421", Effect, Param) && Effect == EFF_VIBRATO && Param == 0x21);
	CHECK(ParseEffect("q 3f", Effect, Param) && Effect == EFF_SLIDE_UP && Param == 0x3F);
	CHECK(ParseEffect("047", Effect, Param) && Effect == EFF_ARPEGGIO && Param == 0x47);
	CHECK(!ParseEffect("K12", Effect, Param));
	CHECK(!ParseEffect("4xy", Effect, Param));
	CHECK_EQ(EffectText(EFF_SLIDE_DOWN, 0x31), std::string("R31"));
	CHECK_EQ(EffectText(EFF_SPEED, 6), std::string("F06"));
	CHECK(EffectLetter(EFF_DPCM_PITCH) == 'W');
	CHECK_EQ(FormatNumber(1.0), std::string("1"));
	CHECK_EQ(FormatNumber(2.5), std::string("2.5"));
	CHECK_EQ(FormatNumber(0.25), std::string("0.25"));
	CHECK_EQ(FormatNumber(1.0 / 3.0), std::string("0.333"));
}

void TestTiming()
{
	const Timing T(64, 2, 4, 16);
	CHECK_EQ(T.BarsPerFrame(), 4);
	CHECK_EQ(T.BarCount(), 8);
	CHECK_EQ(T.BarStart(1), 0);
	CHECK_EQ(T.BarStart(5), 64);
	CHECK_EQ(T.BarStart(6), 80);
	CHECK_EQ(T.BarOf(79), 5);
	CHECK_EQ(T.BarOf(80), 6);
	CHECK(T.BeatOf(86) == 2.5);
	int Row = 0;
	bool Exact = false;
	CHECK(T.PositionToRow(2, 3.5, Row, Exact) && Row == 26 && Exact);
	CHECK(T.PositionToRow(2, 1.1, Row, Exact) && Row == 16 && !Exact);
	CHECK(!T.PositionToRow(2, 5.0, Row, Exact));
	CHECK(!T.PositionToRow(0, 1.0, Row, Exact));
	CHECK_EQ(T.BeatsToRows(1.5, Exact), 6);
	CHECK(Exact);
	CHECK_EQ(T.BeatsToRows(0.1, Exact), 1);
	CHECK(!Exact);
	CHECK_EQ(T.Position(86), std::string("bar 6 beat 2.5"));
	CHECK_EQ(T.Length(4), std::string("1 beat"));
	CHECK_EQ(T.Length(6), std::string("1.5 beats"));

	// A frame that isn't a whole number of bars: its last bar is shorter
	const Timing Odd(48, 2, 4, 32);
	CHECK_EQ(Odd.BarsPerFrame(), 2);
	CHECK_EQ(Odd.BarLength(2), 16);
	CHECK_EQ(Odd.BarStart(3), 48);
	CHECK_EQ(Odd.BarOf(47), 2);
	// No highlight: 16 rows a bar, 4 a beat
	const Timing None(64, 1, 0, 0);
	CHECK_EQ(None.RowsPerBar, 16);
	CHECK_EQ(None.RowsPerBeat, 4);
}

void TestTempo()
{
	CHECK(RowsPerMinute(6, 150, 60) == 600.0);
	CHECK(BeatsPerMinute(6, 150, 60, 4) == 150.0);
	CHECK(BeatsPerMinute(6, 120, 60, 4) == 120.0);
	// Even rows: at tempo 150 (125 for PAL) the speed is the ticks a row; speed 6 at tempo 180 has rows of 5
	CHECK(EvenRows(6, 150, 60) && EvenRows(7, 150, 60) && EvenRows(6, 180, 60) && EvenRows(6, 125, 50) && EvenRows(6, 150, 50));
	CHECK(!EvenRows(6, 120, 60) && !EvenRows(6, 128, 60) && !EvenRows(0, 150, 60));
	CHECK(DefaultTempo(60) == 150 && DefaultTempo(50) == 125);
	int Speed = 0, Tempo = 0;
	// Another speed at tempo 150, for even rows
	ChooseSpeedTempo(180.0, 4, 60, 6, 32, Speed, Tempo);
	CHECK(Speed == 5 && Tempo == 150);
	ChooseSpeedTempo(100.0, 4, 60, 6, 32, Speed, Tempo);
	CHECK(Speed == 9 && Tempo == 150);
	ChooseSpeedTempo(127.5, 4, 60, 6, 32, Speed, Tempo);	// 128.6 BPM is within 1%
	CHECK(Speed == 7 && Tempo == 150);
	ChooseSpeedTempo(150.0, 4, 60, 3, 32, Speed, Tempo);	// tempo 150 rather than speed 3 at tempo 75
	CHECK(Speed == 6 && Tempo == 150);
	// No even rows near 120 BPM (7.5 ticks a row): the speed stays, the tempo changes
	ChooseSpeedTempo(120.0, 4, 60, 6, 32, Speed, Tempo);
	CHECK(Speed == 6 && Tempo == 120);
	ChooseSpeedTempo(135.0, 4, 60, 6, 32, Speed, Tempo);
	CHECK(Speed == 6 && Tempo == 135);
	ChooseSpeedTempo(160.0, 4, 60, 3, 32, Speed, Tempo);
	CHECK(Speed == 3 && Tempo == 80);
	ChooseSpeedTempo(172.5, 4, 60, 6, 32, Speed, Tempo);	// a tie rounds up
	CHECK(Speed == 6 && Tempo == 173);
	// 3 rows a beat
	ChooseSpeedTempo(100.0, 3, 60, 6, 32, Speed, Tempo);
	CHECK(Speed == 12 && Tempo == 150);
	// A speed or tempo that has to stay
	ChooseSpeedTempo(180.0, 4, 60, 6, 32, Speed, Tempo, 6);
	CHECK(Speed == 6 && Tempo == 180);
	ChooseSpeedTempo(90.0, 4, 60, 6, 32, Speed, Tempo, 0, 150);
	CHECK(Speed == 10 && Tempo == 150);
	ChooseSpeedTempo(120.0, 4, 60, 6, 32, Speed, Tempo, 0, 150);	// as close as speeds get
	CHECK((Speed == 7 || Speed == 8) && Tempo == 150);
	// Too slow for speed 6 (tempo can't go below 32): another speed
	ChooseSpeedTempo(20.0, 4, 60, 6, 32, Speed, Tempo);
	CHECK(Speed != 6 && std::abs(BeatsPerMinute(Speed, Tempo, 60, 4) - 20.0) < 0.5 && EvenRows(Speed, Tempo, 60));
	// The old split point (speeds up to 20) keeps speeds below it
	ChooseSpeedTempo(36.0, 4, 60, 6, 21, Speed, Tempo);
	CHECK(Speed <= 20 && Tempo >= 32 && std::abs(BeatsPerMinute(Speed, Tempo, 60, 4) - 36.0) < 0.5);
	// PAL: the same BPM with the same speed and tempo; its default tempo is 125
	CHECK(BeatsPerMinute(6, 150, 50, 4) == 150.0);
	ChooseSpeedTempo(150.0, 4, 50, 6, 32, Speed, Tempo);
	CHECK(Speed == 5 && Tempo == 125);
}

void TestKeys()
{
	Key K;
	CHECK(ParseKey("C major", K) && K.Tonic == 0 && K.Mode == MODE_MAJOR);
	CHECK(ParseKey("Am", K) && K.Tonic == 9 && K.Mode == MODE_MINOR);
	CHECK(ParseKey("f# dorian", K) && K.Tonic == 6 && K.Mode == 1);
	CHECK(ParseKey("Bb minor", K) && K.Tonic == 10 && K.Mode == MODE_MINOR);
	CHECK(ParseKey("E flat major", K) && K.Tonic == 3);
	CHECK(ParseKey("G", K) && K.Tonic == 7 && K.Mode == MODE_MAJOR);
	CHECK(ParseKey("D harmonic minor", K) && K.Mode == 7);
	CHECK(!ParseKey("H major", K));
	CHECK(!ParseKey("C blues", K));
	CHECK_EQ(KeyName(Key(10, MODE_MAJOR)), std::string("Bb major"));
	CHECK_EQ(KeyName(Key(6, MODE_MINOR)), std::string("F# minor"));
	CHECK_EQ(KeyName(Key(2, MODE_MINOR)), std::string("D minor"));
	CHECK_EQ(KeyName(Key(5, 1)), std::string("F dorian"));
	CHECK_EQ(NoteClassName(3, Key(5, MODE_MAJOR)), std::string("Eb"));

	const Key CMajor(0, MODE_MAJOR), CMinor(0, MODE_MINOR), AMinor(9, MODE_MINOR);
	int E4 = 0, G4 = 0, F4 = 0;
	ParsePitch("E-4", E4);
	ParsePitch("G-4", G4);
	ParsePitch("F#4", F4);
	CHECK(InKey(E4, CMajor));
	CHECK(!InKey(F4, CMajor));
	CHECK(!InKey(E4, CMinor));
	int B3 = 0;
	ParsePitch("B-3", B3);
	CHECK(InKey(B3, CMinor));	// the raised 7th of minor counts

	// C major to C minor: E, A and B come down a semitone
	CHECK_EQ(MapToKey(E4, CMajor, CMinor), E4 - 1);
	CHECK_EQ(MapToKey(G4, CMajor, CMinor), G4);
	CHECK_EQ(MapToKey(E4 + 5, CMajor, CMinor), E4 + 4);	// A to Ab
	CHECK_EQ(MapToKey(B3, CMajor, CMinor), B3 - 1);
	// A note between degrees moves with the one below: F# (above F) stays above F
	CHECK_EQ(MapToKey(F4, CMajor, CMinor), F4);
	// And back
	CHECK_EQ(MapToKey(E4 - 1, CMinor, CMajor), E4);
	// To another tonic: C major to A minor moves down a minor third and keeps the shape
	int C4 = 0;
	ParsePitch("C-4", C4);
	CHECK_EQ(MapToKey(C4, CMajor, AMinor), C4 - 3);
	CHECK_EQ(MapToKey(E4, CMajor, AMinor), C4);	// the third of A minor
	// Same key: the same note
	for (int p = 0; p < 24; ++p)
		CHECK_EQ(MapToKey(p + 36, CMajor, CMajor), p + 36);
	// Lydian raises the 4th
	CHECK_EQ(MapToKey(C4 + 5, CMajor, Key(0, 3)), C4 + 6);
}

// Reading and writing a channel

void TestReadNotes()
{
	std::vector<Cell> Cells(32);
	Cells[0].SetPitch(48);
	Cells[0].Instrument = 1;
	Cells[0].Volume = 12;
	Cells[2].Effect[0] = EFF_VIBRATO;
	Cells[2].Param[0] = 0x31;
	Cells[4].SetPitch(50);
	Cells[8].Note = NOTE_RELEASE;
	Cells[12].SetPitch(52);
	Cells[12].Instrument = 2;
	const std::vector<NoteInfo> Notes = ReadNotes(Cells, 2);
	CHECK_EQ(Notes.size(), static_cast<size_t>(3));
	CHECK(Notes[0].Row == 0 && Notes[0].Rows == 4 && Notes[0].Pitch == 48 && Notes[0].Instrument == 1 && Notes[0].Volume == 12);
	CHECK(Notes[0].Effects.size() == 1 && Notes[0].Effects[0].first == 2);
	CHECK(Notes[1].Rows == 4 && Notes[1].EndNote == NOTE_RELEASE && Notes[1].Instrument == 1 && !Notes[1].InstrumentSet);
	CHECK(Notes[2].Rows == 20 && Notes[2].Instrument == 2);

	const ChannelState State = StateAt(Cells, 6, 2);
	CHECK(State.Sounding && State.Instrument == 1 && State.Volume == 12 && State.Sticky[EFF_VIBRATO] == 0x31);
	CHECK(!StateAt(Cells, 10, 2).Sounding);
	// Effects in hidden columns don't play
	Cells[2].Effect[0] = EFF_NONE;
	Cells[2].Effect[2] = EFF_VIBRATO;
	CHECK(StateAt(Cells, 6, 2).Sticky[EFF_VIBRATO] == 0);
}

PartNote MakeNote(int Row, int Rows, int Pitch, int Instrument = NO_INSTRUMENT)
{
	PartNote N;
	N.Row = Row;
	N.Rows = Rows;
	N.Pitch = Pitch;
	N.Instrument = Instrument;
	return N;
}

void TestBuildPart()
{
	PartOptions Direct, Clip;
	Direct.EffectColumns = Clip.EffectColumns = 2;
	Direct.Releases.assign(64, false);
	Direct.Releases[1] = true;
	Clip.Releases.assign(64, false);
	Clip.Releases[1] = true;
	Clip.KeepFollowing = false;
	Clip.StopAtEnd = true;
	std::vector<std::string> Warnings;
	std::vector<Cell> Out;

	{
		// Two notes and a rest on an empty channel: each ends with a halt
		std::vector<Cell> Old(64);
		std::vector<PartNote> Notes;
		Notes.push_back(MakeNote(0, 4, 48, 2));
		Notes.push_back(MakeNote(8, 8, 50));
		BuildPart(Old, 0, 16, Notes, Direct, Out, Warnings);
		CHECK_EQ(Out.size(), static_cast<size_t>(17));	// the last note's halt goes after the range
		CHECK(Out[0].Pitch() == 48 && Out[0].Instrument == 2);
		CHECK(Out[4].Note == NOTE_HALT);
		CHECK(Out[8].Pitch() == 50 && Out[8].Instrument == NO_INSTRUMENT);
		CHECK(Out[16].Note == NOTE_HALT);
		// As a clip it stays inside the range, the halt on its last row
		BuildPart(Old, 0, 16, Notes, Clip, Out, Warnings);
		CHECK_EQ(Out.size(), static_cast<size_t>(16));
		CHECK(Out[15].Note == NOTE_HALT);
	}
	{
		// Released instruments end with ===; legato notes need nothing
		std::vector<Cell> Old(64);
		std::vector<PartNote> Notes;
		Notes.push_back(MakeNote(0, 4, 48, 1));
		Notes.push_back(MakeNote(4, 4, 50));
		BuildPart(Old, 0, 16, Notes, Direct, Out, Warnings);
		CHECK(Out[4].Pitch() == 50);
		CHECK(Out[8].Note == NOTE_RELEASE);
	}
	{
		// What was there goes, but the song's jumps stay; a note held into the
		// range stops at its start; vibrato still on is turned off there
		std::vector<Cell> Old(64);
		Old[10].SetPitch(40);
		Old[10].Instrument = 1;
		Old[11].Effect[0] = EFF_VIBRATO;
		Old[11].Param[0] = 0x44;
		Old[20].SetPitch(41);
		Old[20].Volume = 3;
		Old[24].Effect[1] = EFF_JUMP;
		Old[24].Param[1] = 0x01;
		std::vector<PartNote> Notes;
		Notes.push_back(MakeNote(18, 2, 60, 2));
		BuildPart(Old, 16, 32, Notes, Direct, Out, Warnings);
		CHECK(Out[0].Note == NOTE_RELEASE);	// instrument 1 releases
		CHECK(HasEffect(Out[0], EFF_VIBRATO, 2));
		CHECK(Out[0].Effect[0] == EFF_VIBRATO && Out[0].Param[0] == 0);
		CHECK(Out[2].Pitch() == 60);
		CHECK(Out[4].Note == NOTE_HALT && Out[4].Volume == NO_VOLUME);	// the new note's end, the old note gone
		CHECK(Out[6].Note == NOTE_NONE);
		CHECK(Out[8].Effect[1] == EFF_JUMP && Out[8].Param[1] == 1);
	}
	{
		// The next note after the range plays with the instrument, volume and
		// arpeggio it had before
		std::vector<Cell> Old(64);
		Old[0].SetPitch(48);
		Old[0].Instrument = 1;
		Old[0].Volume = 9;
		Old[0].Effect[0] = EFF_ARPEGGIO;
		Old[0].Param[0] = 0x47;
		Old[40].SetPitch(50);
		std::vector<PartNote> Notes;
		Notes.push_back(MakeNote(16, 8, 55, 2));
		Notes[0].Volume = 15;
		BuildPart(Old, 16, 32, Notes, Direct, Out, Warnings);
		CHECK_EQ(Out.size(), static_cast<size_t>(25));	// up to the next note
		const Cell &Next = Out[24];
		CHECK(Next.Pitch() == 50 && Next.Instrument == 1 && Next.Volume == 9);
		CHECK(HasEffect(Next, EFF_ARPEGGIO, 2));
		// Entering the range: the arpeggio was turned off there, and the old note stopped
		CHECK(Out[0].Pitch() == 55 && HasEffect(Out[0], EFF_ARPEGGIO, 2) && Out[0].Param[0] == 0);
		// A clip leaves what follows alone
		BuildPart(Old, 16, 32, Notes, Clip, Out, Warnings);
		CHECK_EQ(Out.size(), static_cast<size_t>(16));
	}
	{
		// A note running to the range's end, with a note right after it: nothing to add
		std::vector<Cell> Old(64);
		Old[32].SetPitch(30);
		std::vector<PartNote> Notes;
		Notes.push_back(MakeNote(16, 16, 55, 2));
		BuildPart(Old, 16, 32, Notes, Direct, Out, Warnings);
		CHECK_EQ(Out.size(), static_cast<size_t>(16));
		// At the song's end, nothing either
		std::vector<Cell> Short(32);
		BuildPart(Short, 16, 32, Notes, Direct, Out, Warnings);
		CHECK_EQ(Out.size(), static_cast<size_t>(16));
	}
	{
		// Effects that don't fit in the visible columns are reported
		std::vector<Cell> Old(64);
		std::vector<PartNote> Notes;
		Notes.push_back(MakeNote(0, 8, 50, 2));
		Notes[0].Effects.push_back(std::make_pair(int(EFF_SLIDE_UP), 0x31));
		Notes[0].Effects.push_back(std::make_pair(int(EFF_VOLUME_SLIDE), 0x02));
		Notes[0].Effects.push_back(std::make_pair(int(EFF_TREMOLO), 0x22));
		std::vector<std::string> Lost;
		BuildPart(Old, 0, 16, Notes, Direct, Out, Lost);
		CHECK_EQ(Lost.size(), static_cast<size_t>(1));
	}
}

// The tools

void TestGetSong()
{
	FakeSong Song;
	// C major melody on VRC6 Pulse 1: C E G | F A C | G B D | C
	const char *const MELODY[] = { "C-4", "E-4", "G-4", "E-4", "F-4", "A-4", "C-5", "A-4", "G-4", "B-4", "D-5", "B-4", "C-5" };
	for (int i = 0; i < 13; ++i)
		Song.Put(5, i * 4, MELODY[i], i == 0 ? 0 : NO_INSTRUMENT);
	Song.End(5, 56, NOTE_RELEASE);
	Song.Put(2, 0, "C-2", 1);
	Song.End(2, 16);
	const ToolResult Result = Run(Song, "get_song", "{}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "\"Test song\"");
	CHECK_HAS(Result.Text, "150 BPM");
	CHECK_HAS(Result.Text, "8 bars (2 frames of 64 rows)");
	CHECK_HAS(Result.Text, "6 VRC6 Pulse 1: 13 notes in bars 1-4, C-4 to D-5, instruments \"Lead\" (00)");
	CHECK_HAS(Result.Text, "2 2A03 Pulse 2: empty");
	CHECK_HAS(Result.Text, "Key: C major");
	CHECK_HAS(Result.Text, "| C |");
	CHECK_HAS(Result.Text, "Changes: as clips");
	CHECK_HAS(Result.Text, "soft lead");
	Song.Info.DirectChanges = true;
	CHECK_HAS(Run(Song, "get_song", "{}").Text, "Changes: direct");
}

void TestReadNotesTool()
{
	FakeSong Song;
	Song.Put(0, 16, "C-4", 2, 12);
	Song.Rows[0][18].Effect[0] = EFF_VIBRATO;
	Song.Rows[0][18].Param[0] = 0x21;
	Song.Put(0, 22, "D#4");
	Song.End(0, 24);
	const ToolResult Result = Run(Song, "read_notes", "{\"channel\": \"pulse 1\", \"from_bar\": 1, \"to_bar\": 3}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "2A03 Pulse 1, bars 1-3");
	CHECK_HAS(Result.Text, "bar 1: rest");
	CHECK_HAS(Result.Text, "bar 2 beat 1: C-4, 1.5 beats, \"Square\" (02), volume 12, effects 421 at beat 1.5");
	CHECK_HAS(Result.Text, "bar 2 beat 2.5: D#4, 0.5 beat");
	CHECK_HAS(Result.Text, "bar 3: rest");
	// By number, and errors
	CHECK(!Run(Song, "read_notes", "{\"channel\": 1}").IsError);
	CHECK(Run(Song, "read_notes", "{\"channel\": \"Kazoo\"}").IsError);
	CHECK_HAS(Run(Song, "read_notes", "{\"channel\": 9}").Text, "1 2A03 Pulse 1");
	CHECK(Run(Song, "read_notes", "{}").IsError);
	CHECK(Run(Song, "read_notes", "{\"channel\": 1, \"from_bar\": 9}").IsError);
	// A note held into the range
	CHECK_HAS(Run(Song, "read_notes", "{\"channel\": 1, \"from_bar\": 2, \"to_bar\": 2}").Text, "bar 2 beat 1: C-4");
	FakeSong Held;
	Held.Put(0, 12, "G-3", 2);
	Held.End(0, 20);
	CHECK_HAS(Run(Held, "read_notes", "{\"channel\": 1, \"from_bar\": 2, \"to_bar\": 2}").Text, "(held from bar 1 beat 4: G-3, 1 beat more)");
}

void TestWriteNotes()
{
	const std::string Part = "{\"channel\": \"VRC6 Pulse 1\", \"instrument\": \"Lead\", \"notes\": ["
		"{\"bar\": 2, \"beat\": 1, \"pitch\": \"C-4\", \"beats\": 1},"
		"{\"bar\": 2, \"beat\": 2, \"pitch\": \"E-4\", \"beats\": 0.5, \"volume\": 10},"
		"{\"bar\": 2, \"beat\": 3, \"pitch\": \"G-4\", \"beats\": 2, \"effects\": [\"Q31\"]}]}";
	{
		// By default, a clip: the song stays as it was
		FakeSong Song;
		const ToolResult Result = Run(Song, "write_notes", Part);
		CHECK(!Result.IsError);
		CHECK_EQ(Song.Writes, 0);
		CHECK_EQ(Song.Clips.size(), static_cast<size_t>(1));
		CHECK_HAS(Result.Text, "Saved as the clip \"Claude: 3 notes on VRC6 Pulse 1, bar 2\"");
		CHECK_HAS(Result.Text, "VRC6 Pulse 1 at bar 2 beat 1 (frame 00, row 10)");
		CHECK_HAS(Result.Text, "hasn't allowed direct changes");
		const FakeSong::Clip &Clip = Song.Clips[0];
		CHECK(Clip.Columns.size() == 1 && Clip.Columns[0].size() == 16 && Clip.Sources[0] == 5);
		CHECK(Clip.Columns[0][0].Pitch() == 48 && Clip.Columns[0][0].Instrument == 0 && Clip.Columns[0][0].Volume == 15);
		CHECK(Clip.Columns[0][4].Pitch() == 52 && Clip.Columns[0][4].Volume == 10);
		CHECK(Clip.Columns[0][6].Note == NOTE_RELEASE);	// Lead releases
		CHECK(Clip.Columns[0][8].Pitch() == 55 && Clip.Columns[0][8].Effect[0] == EFF_SLIDE_UP && Clip.Columns[0][8].Param[0] == 0x31);
		CHECK(Clip.Columns[0][15].Note == NOTE_RELEASE);
		// A second one gets its own name
		Run(Song, "write_notes", Part);
		CHECK_EQ(Song.Clips[1].Name, std::string("Claude: 3 notes on VRC6 Pulse 1, bar 2 2"));
	}
	{
		// Direct changes write into the song, readable again
		FakeSong Song;
		Song.Info.DirectChanges = true;
		Song.Put(5, 40, "A-3", 0);
		const ToolResult Result = Run(Song, "write_notes", Part);
		CHECK(!Result.IsError);
		CHECK_HAS(Result.Text, "Changed the song: 3 notes on VRC6 Pulse 1, bar 2. One Ctrl+Z");
		CHECK_EQ(Song.Writes, 1);
		CHECK_EQ(Notes(Song, 5), std::string("16:C-4 20:E-4 22:=== 24:G-4 32:=== 40:A-3 "));
		const std::string Read = Run(Song, "read_notes", "{\"channel\": 6, \"to_bar\": 2}").Text;
		CHECK_HAS(Read, "bar 2 beat 3: G-4, 2 beats");
		CHECK_HAS(Read, "effects Q31");
		// Asked for as a clip anyway
		CHECK_HAS(Run(Song, "write_notes", Part.substr(0, Part.size() - 1) + ", \"as_clip\": true}").Text, "Saved as the clip");
		CHECK_EQ(Song.Writes, 1);
		// Undo goes back
		CHECK(!Run(Song, "undo", "{}").IsError);
		CHECK_EQ(Notes(Song, 5), std::string("40:A-3 "));
		// Not while a dialog is open
		Song.Busy = true;
		CHECK_HAS(Run(Song, "write_notes", Part).Text, "A dialog is open");
	}
	{
		// Drums by name on the noise channel get drum instruments
		FakeSong Song;
		Song.Info.DirectChanges = true;
		const ToolResult Result = Run(Song, "write_notes", "{\"channel\": \"noise\", \"notes\": ["
			"{\"bar\": 1, \"beat\": 1, \"pitch\": \"kick\", \"beats\": 0.5},"
			"{\"bar\": 1, \"beat\": 2, \"pitch\": \"snare\", \"beats\": 0.5},"
			"{\"bar\": 1, \"beat\": 2.5, \"pitch\": \"hihat\", \"beats\": 0.25}]}");
		CHECK(!Result.IsError);
		CHECK_HAS(Result.Text, "Added the instrument \"Kick\" (03) (2A03) from the preset \"kick\"");
		CHECK_EQ(Notes(Song, 3, 0, 16), std::string("0:4-# 2:--- 4:A-# 6:E-# 7:--- "));
	}
	{
		// Mistakes come back as errors Claude can fix
		FakeSong Song;
		CHECK_HAS(Run(Song, "write_notes", "{\"channel\": 1, \"notes\": [{\"bar\": 1, \"pitch\": \"X-4\", \"beats\": 1}]}").Text,
			"isn't a note");
		CHECK_HAS(Run(Song, "write_notes", "{\"channel\": 1, \"notes\": [{\"bar\": 1, \"pitch\": \"C-4\"}]}").Text, "length in beats");
		CHECK_HAS(Run(Song, "write_notes", "{\"channel\": 1, \"notes\": [{\"bar\": 1, \"beat\": 5, \"pitch\": \"C-4\", \"beats\": 1}]}").Text,
			"past the end of bar 1");
		CHECK_HAS(Run(Song, "write_notes", "{\"channel\": 1, \"notes\": [{\"bar\": 1, \"pitch\": \"C-4\", \"beats\": 1}, "
			"{\"bar\": 1, \"pitch\": \"D-4\", \"beats\": 1}]}").Text, "Two notes start");
		CHECK_HAS(Run(Song, "write_notes", "{\"channel\": 1, \"notes\": [{\"bar\": 1, \"pitch\": \"C-4\", \"beats\": 1}]}").Text,
			"no instrument to play these notes with");
		CHECK_HAS(Run(Song, "write_notes", "{\"channel\": 1, \"instrument\": \"Lead\", \"notes\": [{\"bar\": 1, \"pitch\": \"C-4\", "
			"\"beats\": 1}]}").Text, "is a VRC6 instrument; 2A03 Pulse 1 needs a 2A03 one");
		CHECK_HAS(Run(Song, "write_notes", "{\"channel\": 1, \"instrument\": \"kick\", \"notes\": []}").Text, "drum for the noise");
		CHECK_HAS(Run(Song, "write_notes", "{\"channel\": \"dpcm\", \"notes\": [{\"bar\": 1, \"pitch\": \"bongo\", \"beats\": 1}]}").Text,
			"On the DPCM channel: kick, snare, low tom or high tom");
		CHECK(Song.Clips.empty());
		// Overlaps are shortened, with a note about it
		const ToolResult Overlap = Run(Song, "write_notes", "{\"channel\": 1, \"instrument\": \"Square\", \"notes\": ["
			"{\"bar\": 1, \"pitch\": \"C-4\", \"beats\": 2}, {\"bar\": 1, \"beat\": 2, \"pitch\": \"D-4\", \"beats\": 1}]}");
		CHECK(!Overlap.IsError);
		CHECK_HAS(Overlap.Text, "overlapped the next one");
		// A preset as the instrument is added for the channel's chip
		const ToolResult Preset = Run(Song, "write_notes", "{\"channel\": \"sawtooth\", \"instrument\": \"plucky bass\", \"notes\": ["
			"{\"bar\": 1, \"pitch\": \"C-2\", \"beats\": 1}]}");
		CHECK(!Preset.IsError);
		CHECK_HAS(Preset.Text, "\"Plucky bass (saw)\"");
		CHECK_EQ(Song.Info.Instruments.back().Chip, std::string("VRC6"));
		// Clearing bars: no notes, a range
		FakeSong Clear;
		Clear.Info.DirectChanges = true;
		Clear.Put(0, 20, "C-4", 2);
		Clear.Put(0, 40, "D-4");
		CHECK(!Run(Clear, "write_notes", "{\"channel\": 1, \"from_bar\": 2, \"to_bar\": 2, \"notes\": []}").IsError);
		CHECK_EQ(Notes(Clear, 0), std::string("40:D-4 "));
		CHECK(Clear.Rows[0][40].Instrument == 2);	// D-4 keeps its instrument
	}
	{
		// Past the end of the song: frames are added
		FakeSong Song;
		Song.Info.DirectChanges = true;
		CHECK(!Run(Song, "write_notes", "{\"channel\": 1, \"instrument\": \"Square\", \"notes\": [{\"bar\": 10, \"pitch\": \"C-4\", \"beats\": 4}]}").IsError);
		CHECK_EQ(Song.Rows[0].size(), static_cast<size_t>(192));
		CHECK(Song.Rows[0][144].Pitch() == 48);
	}
}

void TestPlaceClip()
{
	FakeSong Song;
	Run(Song, "write_notes", "{\"channel\": 6, \"instrument\": \"Lead\", \"notes\": [{\"bar\": 1, \"pitch\": \"C-4\", \"beats\": 1}]}");
	CHECK_HAS(Run(Song, "place_clip", "{\"clip\": 1, \"channel\": 7, \"bar\": 3}").Text, "hasn't allowed direct changes");
	Song.Info.DirectChanges = true;
	const ToolResult Result = Run(Song, "place_clip", "{\"clip\": \"Claude: 1 note on VRC6 Pulse 1, bar 1\", \"channel\": 7, \"bar\": 3, \"linked\": true}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "Placed the clip \"Claude: 1 note on VRC6 Pulse 1, bar 1\" linked on VRC6 Pulse 2 at bar 3 beat 1");
	CHECK_EQ(Song.LastLink, Song.Clips[0].Id);
	CHECK(Song.Rows[6][32].Pitch() == 48);
	CHECK(Run(Song, "place_clip", "{\"clip\": 5, \"channel\": 1, \"bar\": 1}").IsError);
}

// A sequence FamiTracker takes: in range, a vibrato's loop summing to nothing
void CheckSequence(const PresetSequence &Seq, bool Drum)
{
	CHECK(!Seq.Items.empty() && Seq.Items.size() <= 252);
	CHECK(Seq.Release < static_cast<int>(Seq.Items.size()) && Seq.Loop < static_cast<int>(Seq.Items.size()));
	for (size_t k = 0; k < Seq.Items.size(); ++k) {
		if (Seq.Type == SEQUENCE_VOLUME)
			CHECK(Seq.Items[k] >= 0 && Seq.Items[k] <= 15);
		if (Seq.Type == SEQUENCE_ARPEGGIO && Seq.Setting == SEQUENCE_FIXED)
			CHECK(Seq.Items[k] >= 0 && Seq.Items[k] <= (Drum ? 15 : 95));
		if (Seq.Type == SEQUENCE_ARPEGGIO && Seq.Setting == SEQUENCE_ABSOLUTE)
			CHECK(Seq.Items[k] >= -24 && Seq.Items[k] <= 24);
		if (Seq.Type == SEQUENCE_PITCH)
			CHECK(Seq.Items[k] >= -15 && Seq.Items[k] <= 15);
	}
	if (Seq.Type == SEQUENCE_VOLUME)
		CHECK(Seq.Items.back() == 0);
	if (Seq.Type == SEQUENCE_PITCH && Seq.Loop >= 0) {
		int Sum = 0;
		for (size_t k = static_cast<size_t>(Seq.Loop); k < Seq.Items.size(); ++k)
			Sum += Seq.Items[k];
		CHECK_EQ(Sum, 0);	// pitch adds up every frame: the vibrato's loop must come back where it started
	}
}

void TestDesign()
{
	// Every kind, and variations of each, makes sequences FamiTracker takes
	const char *const KINDS[] = { "lead", "pad", "pluck", "bass", "keys", "bell", "brass", "strings", "organ", "chiptune", "bloom lead",
		"stepped lead", "click pluck", "echo pluck", "thump bass", "kick", "snare", "hihat", "open hihat", "crash", "tom" };
	for (size_t k = 0; k < sizeof(KINDS) / sizeof(KINDS[0]); ++k) {
		SoundDesign D;
		CHECK(DesignOfKind(KINDS[k], D));
		for (int Variant = 0; Variant < 4; ++Variant) {
			D.Name = KINDS[k];
			D.Motion = Variant;
			D.Vibrato = Variant;
			D.AttackPitch = Variant;
			D.Tone = Variant - 2;
			D.Length = Variant % 3 - 1;
			D.Sustain = Variant == 1 ? 0 : D.Sustain;
			const Preset P = DesignPreset(D);
			CHECK(P.Drum == D.Drum && !DescribeDesign(D, 60).empty());
			for (int Chip = 0; Chip < 2; ++Chip) {
				const std::vector<PresetSequence> &List = Chip == 0 ? P.Sequences2A03 : P.SequencesVRC6;
				CHECK(P.Drum ? (Chip == 1 ? List.empty() : List.size() == 2) : List.size() >= 2);
				for (size_t s = 0; s < List.size(); ++s) {
					CheckSequence(List[s], P.Drum);
					if (List[s].Type == SEQUENCE_DUTY)
						for (size_t i = 0; i < List[s].Items.size(); ++i)
							CHECK(List[s].Items[i] >= 0 && List[s].Items[i] <= (Chip == 0 ? 3 : 7));
				}
			}
		}
	}
	SoundDesign Unknown;
	CHECK(!DesignOfKind("kazoo", Unknown));

	// An envelope: attack, decay to the sustain level held at the release point, release
	SoundDesign D;
	DesignOfKind("lead", D);
	D.Attack = 2;
	D.Peak = 12;
	D.Decay = 4;
	D.Sustain = 8;
	D.Release = 3;
	Preset P = DesignPreset(D);
	const PresetSequence &Volume = P.Sequences2A03[0];
	CHECK(Volume.Type == SEQUENCE_VOLUME && Volume.Items[0] < 12 && Volume.Items[2] == 12 && Volume.Items[static_cast<size_t>(Volume.Release)] == 8);
	CHECK(Volume.Items.size() == static_cast<size_t>(Volume.Release) + 5);	// 6, 4, 2 and silence after the release
	D.Sustain = 0;
	P = DesignPreset(D);
	CHECK(P.Sequences2A03[0].Release == -1 && !P.Releases());

	// The tool: words to an instrument, added to the song under its own name
	FakeSong Song;
	ToolResult Result = Run(Song, "design_instrument", "{\"name\": \"hero lead\", \"channel\": \"VRC6 Pulse 1\", \"attack\": \"fast\", "
		"\"sustain\": \"high\", \"duty\": \"25%\", \"duty_motion\": \"sweep\", \"vibrato\": \"gentle\", \"vibrato_delay_beats\": 0.5, "
		"\"attack_pitch\": \"scoop\"}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "Added \"Hero lead\" (03) (VRC6 instrument): a 25% pulse that reaches 13 2 frames in");
	CHECK_HAS(Result.Text, "a gentle vibrato from 12 frames");	// half a beat at speed 6
	CHECK_HAS(Result.Text, "scoops up into the note");
	const Preset &Made = Song.Made[3];
	bool Pitch = false, Scoop = false;
	for (size_t i = 0; i < Made.SequencesVRC6.size(); ++i) {
		Pitch = Pitch || (Made.SequencesVRC6[i].Type == SEQUENCE_PITCH && Made.SequencesVRC6[i].Loop == 12);
		Scoop = Scoop || (Made.SequencesVRC6[i].Type == SEQUENCE_ARPEGGIO && Made.SequencesVRC6[i].Items[0] < 0);
	}
	CHECK(Pitch && Scoop);
	CHECK_HAS(Run(Song, "design_instrument", "{\"name\": \"Hero lead\", \"chip\": \"VRC6\"}").Text, "already has a VRC6 instrument called");
	CHECK(!Run(Song, "design_instrument", "{\"name\": \"Hero lead\"}").IsError);	// a 2A03 one may share the name
	Result = Run(Song, "design_instrument", "{\"name\": \"Thump\", \"channel\": \"Noise\", \"kind\": \"kick\", \"tone\": \"dark\", \"length\": \"long\"}");
	CHECK_HAS(Result.Text, "a dark long kick at volume 15");
	// As the description says it: darker, longer
	Result = Run(Song, "design_instrument", "{\"name\": \"Thump 2\", \"channel\": \"Noise\", \"kind\": \"kick\", \"tone\": \"darker\", \"length\": \"longer\"}");
	CHECK_HAS(Result.Text, "a dark long kick at volume 15");
	CHECK_HAS(Run(Song, "design_instrument", "{\"name\": \"X\", \"channel\": \"Noise\"}").Text, "say which drum");
	CHECK_HAS(Run(Song, "design_instrument", "{\"name\": \"X\", \"channel\": 1, \"kind\": \"snare\"}").Text, "for the noise channel");
	CHECK_HAS(Run(Song, "design_instrument", "{\"name\": \"X\", \"attack\": \"glacial\"}").Text, "attack: instant, fast");
	CHECK_HAS(Run(Song, "design_instrument", "{\"name\": \"X\", \"kind\": \"theremin\"}").Text, "kind: lead, pad");
	CHECK_HAS(Run(Song, "design_instrument", "{\"name\": \"X\", \"chip\": \"VRC7\"}").Text, "built-in sounds");
	Result = Run(Song, "design_instrument", "{\"name\": \"Shimmer\", \"kind\": \"chiptune\", \"arpeggio\": \"minor7\", \"arpeggio_frames\": 2}");
	CHECK_HAS(Result.Text, "it arpeggiates 0, 3, 7, 10 semitones");
	// The sawtooth and the triangle have no duty (and the triangle no volume): said so
	Result = Run(Song, "design_instrument", "{\"name\": \"Saw lead\", \"channel\": \"Sawtooth\", \"kind\": \"lead\", \"motion\": \"open\"}");
	CHECK_HAS(Result.Text, "a sawtooth that reaches");
	CHECK_LACKS(Result.Text, "pulse");
	CHECK_LACKS(Result.Text, "its tone opens");
	Result = Run(Song, "design_instrument", "{\"name\": \"Tri bass\", \"channel\": \"Triangle\", \"kind\": \"bass\"}");
	CHECK_HAS(Result.Text, "a triangle");
	CHECK_LACKS(Result.Text, "pulse");

	// The classic articulations: a bloom, a duty click, held notes struck again
	SoundDesign Bloom;
	CHECK(DesignOfKind("bloom lead", Bloom));
	Preset BP = DesignPreset(Bloom);
	const PresetSequence &BV = BP.Sequences2A03[0];
	CHECK(BV.Items[0] == 10 && BV.Items[1] == 12 && BV.Items[2] == 15);
	CHECK(BV.Loop > 2 && BV.Release > BV.Loop && BV.Items[static_cast<size_t>(BV.Release)] < 12 &&
		BV.Items[static_cast<size_t>(BV.Release - 1)] < BV.Items[static_cast<size_t>(BV.Release - 2)]);	// the dip before the loop goes round
	for (size_t i = 0; i < BP.Sequences2A03.size(); ++i)
		if (BP.Sequences2A03[i].Type == SEQUENCE_DUTY)
			CHECK(BP.Sequences2A03[i].Items == std::vector<int>({ 0, 1 }) && BP.SequencesVRC6[i].Items == std::vector<int>({ 1, 3 }));
	// Steps: the peak held, then a few levels lower held
	SoundDesign Steps;
	DesignOfKind("stepped lead", Steps);
	const std::vector<int> SV = DesignPreset(Steps).Sequences2A03[0].Items;
	CHECK(SV.size() > 37 && SV[0] == 15 && SV[17] == 15 && SV[18] == 12 && SV[35] == 12 && SV[36] == 9);
	// An echo after the note dies away, a thump, a flick
	SoundDesign Echo;
	DesignOfKind("echo pluck", Echo);
	const std::vector<int> EV = DesignPreset(Echo).Sequences2A03[0].Items;
	int Silent = 0, After = 0;
	for (size_t i = 1; i < EV.size(); ++i) {
		if (EV[i] == 0 && Silent == 0 && EV[i - 1] > 0)
			Silent = static_cast<int>(i);
		if (Silent > 0 && EV[i] > 0)
			After = std::max(After, EV[i]);
	}
	CHECK(Silent > 0 && After >= 2 && After <= 6 && EV.back() == 0);
	SoundDesign Thump;
	DesignOfKind("thump bass", Thump);
	CHECK(DesignPreset(Thump).Sequences2A03[1].Type == SEQUENCE_ARPEGGIO && DesignPreset(Thump).Sequences2A03[1].Items == std::vector<int>({ 5, 3, 1, 0 }));
	Thump.AttackPitch = PITCH_FLICK;
	Thump.Vibrato = 1;
	const Preset Flick = DesignPreset(Thump);
	for (size_t i = 0; i < Flick.Sequences2A03.size(); ++i) {
		CheckSequence(Flick.Sequences2A03[i], false);
		if (Flick.Sequences2A03[i].Type == SEQUENCE_PITCH)
			CHECK(Flick.Sequences2A03[i].Items[0] == -4 && Flick.Sequences2A03[i].Items[1] == 3 && Flick.Sequences2A03[i].Items[2] == 1);
	}
	Result = Run(Song, "design_instrument", "{\"name\": \"Konami lead\", \"kind\": \"bloom lead\", \"channel\": 1}");
	CHECK_HAS(Result.Text, "a 25% pulse that blooms from 10 to 15 2 frames in after a 12.5% duty click on the first frame");
	CHECK_HAS(Result.Text, "struck again every 12 frames while it holds");
	Result = Run(Song, "design_instrument", "{\"name\": \"Mine\", \"attack\": \"bloom\", \"volume\": 12, \"decay\": 30, \"decay_style\": "
		"\"steps\", \"sustain\": 6, \"click\": true, \"echo\": true, \"restrike_beats\": 0.5, \"attack_pitch\": \"flick\"}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "blooms from 8 to 12");
	CHECK_HAS(Result.Text, "stepping down a few levels at a time");
	CHECK_HAS(Result.Text, "a soft echo after it");
	CHECK_HAS(Result.Text, "a quick pitch flick up and back on the attack");
	for (std::map<int, Preset>::const_iterator it = Song.Made.begin(); it != Song.Made.end(); ++it)
		for (size_t i = 0; i < it->second.Sequences2A03.size(); ++i)
			CheckSequence(it->second.Sequences2A03[i], it->second.Drum);
}

// A looping tune on Pulse 1 with a triangle bass, written to Path, with a
// playlist that names its track
void WriteTestNsf(const std::string &Path)
{
	using namespace nsftest;
	static const int SCALE[8] = { 0, 2, 3, 5, 7, 8, 10, 12 };
	std::vector<Tone> Tones;
	for (int i = 0; i < 32; ++i) {
		Tone t;
		t.Start = i * 12;
		t.Length = 10;
		t.Note = 45 + SCALE[(i * 5 + i / 3) % 8];
		t.Envelope = std::vector<int>{ 9, 12, 15, 13, 13, 12 };
		t.Duty = 2;
		t.Trigger = true;
		Tones.push_back(t);
	}
	std::vector<Frame> Frames = PulseFrames(Tones, 32 * 12);
	for (int i = 0; i < 16; ++i) {	// the triangle: a bass note every two of the tune's
		const double f = 440.0 * std::pow(2.0, (21 + SCALE[(i * 3) % 5] - 45) / 12.0);
		const int Period = static_cast<int>(1789773.0 / (32.0 * f) - 1.0 + 0.5);
		Frame &Start = Frames[static_cast<size_t>(i * 24)];
		Start.push_back(std::make_pair(0x08, 0xFF));
		Start.push_back(std::make_pair(0x0A, Period & 0xFF));
		Start.push_back(std::make_pair(0x0B, ((Period >> 8) & 7) | 0x08));
		Frames[static_cast<size_t>(i * 24 + 20)].push_back(std::make_pair(0x08, 0x80));	// silent before the next
	}
	const std::vector<uint8_t> Bytes = BuildNsf(Frames, 0, "Tester Song");
	std::ofstream Nsf(Path.c_str(), std::ios::binary);
	Nsf.write(reinterpret_cast<const char *>(&Bytes[0]), static_cast<std::streamsize>(Bytes.size()));
	Nsf.close();
	std::ofstream M3u((Path.substr(0, Path.size() - 4) + ".m3u").c_str(), std::ios::binary);
	M3u << "api_tests_study.nsf::NSF,1,Lava Run,0:00:40,,0:00:05\n";
}

// A ProTracker module written to Path: four bars in A minor with a bass, a
// lead, and a kick and snare named as drums
void WriteTestMod(const std::string &Path)
{
	std::vector<uint8_t> b(1084 + 1024, 0);
	const char *Title = "Test Song";
	std::memcpy(&b[0], Title, std::strlen(Title));
	const char *Names[] = { "Bass", "Lead", "Kick", "Snare" };
	for (int s = 0; s < 4; ++s)
		std::memcpy(&b[20 + s * 30], Names[s], std::strlen(Names[s]));
	b[950] = 1;
	b[951] = 127;
	b[952] = 0;
	std::memcpy(&b[1080], "M.K.", 4);
	static const int PERIODS[12] = { 856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453 };
	auto Cell = [&](int Row, int Channel, int Sample, int Note, int Effect, int Param) {
		const size_t At = 1084 + (static_cast<size_t>(Row) * 4 + Channel) * 4;
		int Period = PERIODS[Note % 12];
		for (int o = 0; o < Note / 12; ++o)
			Period /= 2;
		b[At] = static_cast<uint8_t>((Sample & 0xF0) | ((Period >> 8) & 15));
		b[At + 1] = static_cast<uint8_t>(Period & 255);
		b[At + 2] = static_cast<uint8_t>(((Sample & 15) << 4) | (Effect & 15));
		b[At + 3] = static_cast<uint8_t>(Param);
	};
	static const int LEAD[8] = { 21, 24, 28, 26, 24, 23, 21, 19 };
	for (int r = 0; r < 64; ++r) {
		if (r % 4 == 0)
			Cell(r, 0, 1, (r / 16) % 2 ? 4 : 9, 0, 0);
		if (r % 8 == 0)
			Cell(r, 1, 2, LEAD[r / 8], 4, 0x46);
		if (r % 16 == 0 || r % 16 == 8)
			Cell(r, 2, 3, 12, 0, 0);
		if (r % 16 == 4 || r % 16 == 12)
			Cell(r, 3, 4, 12, 0, 0);
	}
	std::ofstream Mod(Path.c_str(), std::ios::binary);
	Mod.write(reinterpret_cast<const char *>(&b[0]), static_cast<std::streamsize>(b.size()));
}

// A MIDI file of a waltz: 150 BPM in 3/4, eight bars of a flute, a bass and drums
// A FamiTracker 0.4.6 module, written block by block as CDocumentFile writes
// one: a melody on Pulse 1 (a 12.5% click into 50%), its echo 2 rows later on
// Pulse 2 at volume 4, a bass on the triangle, a swing from speeds 6 and 3,
// and the DPCM level Z30 every bar
void WriteTestFtm(const std::string &Path)
{
	std::string B;
	size_t SizeAt = 0;
	auto U8 = [&](int v) { B += char(v & 0xFF); };
	auto Int = [&](long v) { for (int i = 0; i < 4; ++i) U8(int((static_cast<unsigned long>(v) >> (8 * i)) & 0xFF)); };
	auto Fixed = [&](const std::string &s, size_t n) { for (size_t i = 0; i < n; ++i) U8(i < s.size() ? s[i] : 0); };
	auto Begin = [&](const char *Id, int Version) { Fixed(Id, 16); Int(Version); SizeAt = B.size(); Int(0); };
	auto End = [&]() {
		const unsigned long Size = static_cast<unsigned long>(B.size() - SizeAt - 4);
		for (int i = 0; i < 4; ++i)
			B[SizeAt + static_cast<size_t>(i)] = char((Size >> (8 * i)) & 0xFF);
	};
	B = "FamiTracker Module";
	Int(0x0440);
	Begin("PARAMS", 6); U8(0); Int(5); Int(0); Int(0); Int(1); Int(4); Int(16); Int(32); End();
	Begin("INFO", 1); Fixed("Api Swing", 32); Fixed("Tester", 32); Fixed("2026", 32); End();
	Begin("HEADER", 3); U8(0); B += std::string("New song") + '\0';
	for (int c = 0; c < 5; ++c) { U8(c); U8(c == 4 ? 1 : 0); }
	End();
	Begin("INSTRUMENTS", 6); Int(1); Int(0); U8(1); Int(5);
	U8(1); U8(0); U8(0); U8(0); U8(0); U8(0); U8(0); U8(0); U8(1); U8(1);	// volume 0, duty 1
	for (int k = 0; k < 96; ++k) { U8(0); U8(0); U8(0xFF); }
	Int(4); B += "Lead";
	End();
	Begin("SEQUENCES", 6); Int(2);
	Int(0); Int(0); U8(6); Int(-1); U8(15); U8(13); U8(12); U8(11); U8(10); U8(8);	// volume
	Int(1); Int(4); U8(2); Int(-1); U8(0); U8(2);										// duty: a 12.5% click into 50%
	Int(-1); Int(0); Int(-1); Int(0);
	End();
	Begin("FRAMES", 3); Int(2); Int(6); Int(150); Int(64);
	for (int f = 0; f < 2; ++f) for (int c = 0; c < 5; ++c) U8(0);
	End();
	Begin("PATTERNS", 5);
	// Pulse 1: a note every 4 rows; Pulse 2: the same 2 rows later, soft; the triangle: a note a bar
	static const int MELODY[16] = { 1, 5, 8, 12, 10, 8, 5, 3, 1, 3, 5, 6, 8, 6, 5, 3 };
	for (int c = 0; c < 3; ++c) {
		const int Count = c == 2 ? 4 : 16;
		Int(0); Int(c); Int(0); Int(Count);
		for (int i = 0; i < Count; ++i) {
			Int(c == 2 ? i * 16 : i * 4 + (c == 1 ? 2 : 0));
			U8(c == 2 ? (i % 2 == 0 ? 1 : 8) : MELODY[i]); U8(c == 2 ? 2 : 4); U8(0); U8(c == 1 ? 4 : 0x10);
			U8(0); U8(0);
		}
	}
	// DPCM: the swing (F06 on the beat, F03 half a beat later) and Z30 every bar
	Int(0); Int(4); Int(0); Int(32);
	for (int r = 0; r < 64; r += 2) {
		Int(r); U8(0); U8(0); U8(0x40); U8(0x10);
		U8(1); U8(r % 4 == 0 ? 6 : 3);
		U8(r % 16 == 0 ? 15 : 0); U8(r % 16 == 0 ? 0x30 : 0);
	}
	End();
	B += "END";
	std::ofstream Out(Path.c_str(), std::ios::binary);
	Out.write(B.data(), static_cast<std::streamsize>(B.size()));
}

void WriteTestMidi(const std::string &Path)
{
	struct Ev { unsigned long Tick; int Order; std::vector<uint8_t> Data; };
	std::vector<Ev> Events;
	auto Add = [&](unsigned long Tick, int Order, const std::vector<uint8_t> &Data) {
		Ev e = { Tick, Order, Data };
		Events.push_back(e);
	};
	auto Note = [&](unsigned long Tick, unsigned long Length, int Channel, int Key) {
		Add(Tick, 1, { uint8_t(0x90 | Channel), uint8_t(Key), 100 });
		Add(Tick + Length, 0, { uint8_t(0x80 | Channel), uint8_t(Key), 0 });
	};
	const std::string Name = Path.find("untitled") == std::string::npos ? "Api Waltz" : "";
	std::vector<uint8_t> Title = { 0xFF, 0x03, uint8_t(Name.size()) };
	Title.insert(Title.end(), Name.begin(), Name.end());
	Add(0, 1, Title);
	Add(0, 1, { 0xFF, 0x51, 3, 0x06, 0x1A, 0x80 });		// 400000 microseconds a beat: 150 BPM
	Add(0, 1, { 0xFF, 0x58, 4, 3, 2, 24, 8 });			// 3/4
	Add(0, 1, { 0xC0, 73 });							// a flute
	Add(0, 1, { 0xC1, 32 });							// an acoustic bass
	const unsigned long Q = 96;
	static const int TUNE[8] = { 79, 81, 79, 76, 77, 76, 74, 72 };
	for (unsigned long b = 0; b < 8; ++b) {
		const unsigned long Bar = b * 3 * Q;
		Note(Bar, 2 * Q, 0, TUNE[b]);
		Note(Bar + 2 * Q, Q, 0, TUNE[b] - 3);
		Note(Bar, 2 * Q, 1, b % 2 ? 43 : 48);
		Note(Bar, Q / 2, 9, 36);
		Note(Bar + Q, Q / 2, 9, 42);
		Note(Bar + 2 * Q, Q / 2, 9, 42);
	}
	std::stable_sort(Events.begin(), Events.end(), [](const Ev &a, const Ev &b) {
		return a.Tick != b.Tick ? a.Tick < b.Tick : a.Order < b.Order;
	});
	std::vector<uint8_t> Body;
	unsigned long Last = 0;
	for (size_t i = 0; i < Events.size(); ++i) {
		unsigned long d = Events[i].Tick - Last;
		Last = Events[i].Tick;
		uint8_t Vlq[4];
		int n = 0;
		Vlq[n++] = uint8_t(d & 0x7F);
		while ((d >>= 7) != 0 && n < 4)
			Vlq[n++] = uint8_t(0x80 | (d & 0x7F));
		while (n > 0)
			Body.push_back(Vlq[--n]);
		Body.insert(Body.end(), Events[i].Data.begin(), Events[i].Data.end());
	}
	const uint8_t End[4] = { 0, 0xFF, 0x2F, 0 };
	Body.insert(Body.end(), End, End + 4);
	std::vector<uint8_t> File = { 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0, 96, 'M', 'T', 'r', 'k' };
	for (int s = 24; s >= 0; s -= 8)
		File.push_back(uint8_t(Body.size() >> s));
	File.insert(File.end(), Body.begin(), Body.end());
	std::ofstream Mid(Path.c_str(), std::ios::binary);
	Mid.write(reinterpret_cast<const char *>(&File[0]), static_cast<std::streamsize>(File.size()));
}

void TestReferences()
{
	// Ids, the file read back, a sound as a preset for either chip
	CHECK_EQ(ReferenceId("Bucky O'Hare", 8), std::string("bucky-o-hare-08"));
	CHECK_EQ(ReferenceId("", 3), std::string("nsf-03"));
	Reference R;
	R.Id = "game-02";
	R.Title = "Lava";
	R.Game = "Game";
	R.Track = 2;
	R.Tags.push_back("fast");
	R.Bpm = 150;
	R.Key = "E minor";
	R.LoopBars = 16;
	R.Study = "Lava\n  Tempo: 150 BPM.\n  Pulse 1, the lead: an echo a row behind.";
	ReferenceSound Lead;
	Lead.Channel = "Pulse 1";
	Lead.Role = "the lead";
	Lead.Words = "25% duty after a 12.5% click";
	Lead.Chip = "2A03";
	Lead.Share = 80;
	Lead.Sequences[SEQUENCE_VOLUME].Items = std::vector<int>{ 9, 12, 15, 13, 6, 0 };
	Lead.Sequences[SEQUENCE_VOLUME].Release = 3;
	Lead.Sequences[SEQUENCE_DUTY].Items = std::vector<int>{ 0, 1 };
	Lead.Sequences[SEQUENCE_PITCH].Items = std::vector<int>{ 0, 0, 1, -1, -1, 1 };
	Lead.Sequences[SEQUENCE_PITCH].Loop = 2;
	R.Sounds.push_back(Lead);
	Reference Back;
	std::string Error;
	CHECK(ReadReferenceFile(ReferenceFile(R), Back, Error));
	CHECK(Back.Title == "Lava" && Back.Track == 2 && Back.Bpm == 150 && Back.Study == R.Study && Back.Tags == R.Tags);
	CHECK(Back.Sounds.size() == 1 && Back.Sounds[0].Sequences[SEQUENCE_VOLUME].Items == Lead.Sequences[SEQUENCE_VOLUME].Items &&
		Back.Sounds[0].Sequences[SEQUENCE_VOLUME].Release == 3 && Back.Sounds[0].Sequences[SEQUENCE_PITCH].Loop == 2);
	CHECK(!ReadReferenceFile("{\"title\": \"x\"}", Back, Error) && !ReadReferenceFile("not json", Back, Error));
	CHECK_HAS(ReferenceFile(R), "\n\t\"study\": [\n");		// a line each, to read
	CHECK_HAS(ReferenceText(R), "volume 9 12 15 13 | 6 0; pitch 0 0 [1 -1 -1 1; duty 0 1");
	const Preset P = ReferencePreset(Lead, "Lava lead");
	CHECK(P.Name == "Lava lead" && P.Releases() && P.Sequences2A03.size() == 3 && P.SequencesVRC6.size() == 3);
	CHECK(P.Sequences2A03[2].Type == SEQUENCE_DUTY && P.Sequences2A03[2].Items == std::vector<int>({ 0, 1 }) &&
		P.SequencesVRC6[2].Items == std::vector<int>({ 1, 3 }));		// 12.5% and 25% on the VRC6
	CHECK(ReferenceMatch(R, "echo") > 0 && ReferenceMatch(R, "fast lava") > ReferenceMatch(R, "fast kazoo") &&
		ReferenceMatch(R, "kazoo") == 0 && ReferenceMatch(R, "the") == 0);
	CHECK_EQ(ReferenceSoundName(R, Lead), std::string("Lava lead"));

	// study_nsf: the track studied and kept in the library
	const std::string Path = "api_tests_study.nsf";
	WriteTestNsf(Path);
	FakeSong Song;
	ToolResult Result = Run(Song, "study_nsf", "{\"path\": \"" + Path + "\", \"tracks\": \"list\"}");
	CHECK_HAS(Result.Text, "1. Lava Run: music");
	Result = Run(Song, "study_nsf", "{\"path\": \"" + Path + "\", \"tags\": [\"Lava\", \"test\"], \"notes\": \"made for the tests\"}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "Lava Run from Tester Song (track 1), by Tester. Reference \"tester-song-01\"");
	CHECK_HAS(Result.Text, "Kept in the reference library (C:\\Games\\FamiTracker\\References)");
	CHECK_HAS(Result.Text, "Pulse 1, the lead");
	CHECK(Song.Library.count("tester-song-01.json") == 1);
	Reference Kept;
	CHECK(ReadReferenceFile(Song.Library["tester-song-01.json"], Kept, Error));
	CHECK(Kept.Title == "Lava Run" && Kept.Game == "Tester Song" && Kept.Track == 1 && Kept.Notes == "made for the tests");
	CHECK(std::find(Kept.Tags.begin(), Kept.Tags.end(), "lava") != Kept.Tags.end() &&
		std::find(Kept.Tags.begin(), Kept.Tags.end(), "bloom") != Kept.Tags.end());
	CHECK(!Kept.Sounds.empty() && Kept.Sounds[0].Channel == "Pulse 1" && Kept.Sounds[0].Sequences[SEQUENCE_VOLUME].Items.size() >= 3 &&
		Kept.Sounds[0].Sequences[SEQUENCE_VOLUME].Items[0] == 9 && Kept.Sounds[0].Sequences[SEQUENCE_VOLUME].Items[2] == 15);
	CHECK_HAS(Run(Song, "study_nsf", "{\"path\": \"no_such_file.nsf\"}").Text, "couldn't be read");
	CHECK_HAS(Run(Song, "study_nsf", "{\"path\": \"" + Path + "\", \"tracks\": \"7\"}").Text, "the NSF has tracks 1 to 1");
	CHECK_HAS(Run(Song, "study_nsf", "{}").Text, "Give the path of the NSF file");

	// references: list, search, show, tag, guide
	Result = Run(Song, "references", "{}");
	CHECK_HAS(Result.Text, "1 reference in the library:\nTester Song (Tester):\n- tester-song-01: Lava Run (Tester Song, track 1)");
	CHECK_HAS(Run(Song, "references", "{\"action\": \"search\", \"query\": \"blooming lava\"}").Text, "- tester-song-01: Lava Run");
	CHECK_HAS(Run(Song, "references", "{\"action\": \"search\", \"query\": \"kazoo\"}").Text, "No reference matches \"kazoo\"");
	Result = Run(Song, "references", "{\"action\": \"show\", \"reference\": \"tester-song-01\"}");
	CHECK_HAS(Result.Text, "Sounds (reference_sound adds one to the song as an instrument, by its number):\n  1. Pulse 1, the lead");
	CHECK_HAS(Result.Text, "Notes: made for the tests");
	CHECK_HAS(Run(Song, "references", "{\"action\": \"show\", \"reference\": \"lava run\"}").Text, "Reference \"tester-song-01\"");
	CHECK_HAS(Run(Song, "references", "{\"action\": \"show\", \"reference\": \"nope\"}").Text, "There's no reference \"nope\"");
	Result = Run(Song, "references", "{\"action\": \"tag\", \"reference\": \"tester-song-01\", \"tags\": \"boss, heroic\", \"remove_tags\": [\"test\"], "
		"\"notes\": \"the user's favourite\"}");
	CHECK_HAS(Result.Text, "boss, heroic]");
	CHECK_LACKS(Result.Text, "test,");
	CHECK_HAS(Run(Song, "references", "{\"action\": \"guide\"}").Text, "Topics");
	CHECK_HAS(Run(Song, "references", "{\"action\": \"guide\"}").Text, "- atmospheric: Atmospheric chiptune");
	Result = Run(Song, "references", "{\"action\": \"guide\", \"topic\": \"lush jazzy\"}");
	CHECK_HAS(Result.Text, "Atmospheric chiptune (warm, lush and nostalgic, made in FamiTracker)");
	CHECK_HAS(Result.Text, "chord_style lush");
	CHECK_HAS(Result.Text, "set_swing");
	CHECK_HAS(Run(Song, "references", "{\"action\": \"fly\"}").Text, "action: list, show, search");

	// Studied again: the user's tags and notes stay
	CHECK(!Run(Song, "study_nsf", "{\"path\": \"" + Path + "\", \"tracks\": \"1\"}").IsError);
	CHECK(ReadReferenceFile(Song.Library["tester-song-01.json"], Kept, Error));
	CHECK(std::find(Kept.Tags.begin(), Kept.Tags.end(), "heroic") != Kept.Tags.end() && Kept.Notes == "the user's favourite");
	CHECK(Run(Song, "study_nsf", "{\"path\": \"" + Path + "\", \"save\": false}").Text.find("Not kept") != std::string::npos);

	// reference_sound: the lead as an instrument for a VRC6 pulse, its duty translated
	Result = Run(Song, "reference_sound", "{\"reference\": \"tester-song-01\", \"sound\": 1, \"channel\": \"VRC6 Pulse 1\"}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "Added \"Lava Run lead\" (03), a VRC6 instrument for VRC6 Pulse 1: sound 1 of tester-song-01");
	const Preset &Made = Song.Made[3];
	bool Duty = false;
	for (size_t i = 0; i < Made.SequencesVRC6.size(); ++i)
		if (Made.SequencesVRC6[i].Type == SEQUENCE_DUTY)
			Duty = !Made.SequencesVRC6[i].Items.empty() && Made.SequencesVRC6[i].Items[0] == 7;	// 50%
	CHECK(Duty);
	CHECK_HAS(Run(Song, "reference_sound", "{\"reference\": \"tester-song-01\", \"channel\": \"Noise\"}").Text, "give a channel other than the noise");
	CHECK_HAS(Run(Song, "reference_sound", "{\"reference\": \"tester-song-01\", \"sound\": 9}").Text, "sound should be from 1 to");

	// import_nsf: not over unsaved changes; then the tracks open as a module
	Song.Info.Modified = true;
	CHECK_HAS(Run(Song, "import_nsf", "{\"path\": \"" + Path + "\"}").Text, "changes that aren't saved");
	CHECK(Song.OpenedText.empty());
	Song.Info.Modified = false;
	Result = Run(Song, "import_nsf", "{\"path\": \"" + Path + "\"}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "Imported 1 track of \"Tester Song\"");
	CHECK_HAS(Result.Text, "It's open in FamiTracker as a new module");
	CHECK_HAS(Song.OpenedText, "TRACK");

	// study_module: a tracker module's study, kept like an NSF's, without sounds
	const std::string ModPath = "api_tests_study.mod";
	WriteTestMod(ModPath);
	Result = Run(Song, "study_module", "{\"path\": \"" + ModPath + "\", \"tags\": \"upbeat\"}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "Test Song: a MOD module of 4 channels, 1 order, 4 named instruments");
	CHECK_HAS(Result.Text, "Reference \"test-song-mod\"");
	CHECK_HAS(Result.Text, "Tempo: 125 BPM, 4 rows a beat (speed 6, tempo 125)");
	CHECK_HAS(Result.Text, "Key: A minor");
	CHECK_HAS(Result.Text, "Channel 2, the lead");
	CHECK_HAS(Result.Text, "kick 'Kick'");
	CHECK_HAS(Result.Text, "snare 'Snare'");
	CHECK_HAS(Result.Text, "To write in its style for the NES");
	CHECK(Song.Library.count("test-song-mod.json") == 1);
	CHECK(ReadReferenceFile(Song.Library["test-song-mod.json"], Kept, Error));
	CHECK(Kept.Sounds.empty() && Kept.Game.empty() && Kept.Track == 0);
	CHECK(std::find(Kept.Tags.begin(), Kept.Tags.end(), "mod") != Kept.Tags.end() &&
		std::find(Kept.Tags.begin(), Kept.Tags.end(), "tracker-module") != Kept.Tags.end() &&
		std::find(Kept.Tags.begin(), Kept.Tags.end(), "vibrato") != Kept.Tags.end() &&
		std::find(Kept.Tags.begin(), Kept.Tags.end(), "upbeat") != Kept.Tags.end());
	CHECK_HAS(Run(Song, "references", "{}").Text, "Modules and others:\n- test-song-mod: Test Song, 125 BPM, A minor");
	CHECK_HAS(Run(Song, "references", "{\"action\": \"search\", \"query\": \"tracker module\"}").Text, "- test-song-mod");
	CHECK_HAS(Run(Song, "reference_sound", "{\"reference\": \"test-song-mod\"}").Text, "its sounds are samples");
	CHECK_HAS(Run(Song, "study_module", "{\"path\": \"no_such_file.xm\"}").Text, "couldn't be read");
	CHECK_HAS(Run(Song, "study_module", "{\"path\": \"" + Path + "\"}").Text, "not a module");
	CHECK_HAS(Run(Song, "study_module", "{}").Text, "Give the path of the module");
	CHECK_HAS(Run(Song, "references", "{\"action\": \"delete\", \"reference\": \"test-song-mod\"}").Text, "Deleted the reference");
	std::remove(ModPath.c_str());

	// study_module: a MIDI file's study, counted in its meter, tagged as MIDI
	const std::string MidiPath = "api_tests_study.mid";
	WriteTestMidi(MidiPath);
	Result = Run(Song, "study_module", "{\"path\": \"" + MidiPath + "\"}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "Api Waltz: a MIDI file (format 0, 1 track) in 3/4: 4 channels of a note at a time, 4 rows a beat, playing "
		"Acoustic Bass, Flute and drums.");
	CHECK_HAS(Result.Text, "Reference \"api-waltz-midi\"");
	CHECK_HAS(Result.Text, "Tempo: 150 BPM in 3/4, 4 rows a beat (speed 6, tempo 150)");
	CHECK_HAS(Result.Text, "Flute, the lead");
	CHECK_HAS(Result.Text, "kick 'Bass Drum 1'");
	CHECK_HAS(Result.Text, "the MIDI file is for its craft");
	CHECK(ReadReferenceFile(Song.Library["api-waltz-midi.json"], Kept, Error));
	CHECK(std::find(Kept.Tags.begin(), Kept.Tags.end(), "midi") != Kept.Tags.end() &&
		std::find(Kept.Tags.begin(), Kept.Tags.end(), "3/4") != Kept.Tags.end() &&
		std::find(Kept.Tags.begin(), Kept.Tags.end(), "tracker-module") == Kept.Tags.end());
	CHECK_HAS(Run(Song, "reference_sound", "{\"reference\": \"api-waltz-midi\"}").Text, "General MIDI's instruments");
	// Untitled: named after its file
	WriteTestMidi("untitled waltz.mid");
	Result = Run(Song, "study_module", "{\"path\": \"untitled waltz.mid\", \"save\": false}");
	CHECK_HAS(Result.Text, "untitled waltz: a MIDI file (format 0, 1 track) in 3/4");
	CHECK_HAS(Result.Text, "untitled waltz. Reference \"untitled-waltz-midi\"");
	CHECK_LACKS(Result.Text, "\nSong\n");
	std::remove("untitled waltz.mid");
	CHECK_HAS(Run(Song, "references", "{\"action\": \"delete\", \"reference\": \"api-waltz-midi\"}").Text, "Deleted the reference");
	std::remove(MidiPath.c_str());

	// study_module: a FamiTracker module, studied from its own patterns and instruments, kept with its sounds
	const std::string FtmPath = "api_tests_study.ftm";
	WriteTestFtm(FtmPath);
	Result = Run(Song, "study_module", "{\"path\": \"" + FtmPath + "\"}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "FamiTracker module \"Api Swing\" by Tester, 1 track:");
	CHECK_HAS(Result.Text, "Reference \"api-swing-ftm\"");
	CHECK_HAS(Result.Text, "(speed 6, tempo 150); a loop of 8 bars");
	CHECK_HAS(Result.Text, "a swing on the eighths from speeds that alternate (Fxx: 6 ticks a row, then 3), so the off-beats come late "
		"(a 67% swing");
	CHECK_HAS(Result.Text, "an echo: Pulse 2 repeats Pulse 1's notes 2 rows later");
	CHECK_HAS(Result.Text, "the DPCM level set with Zxx, mostly Z30");
	CHECK_HAS(Result.Text, "50% duty after a 12.5% click");
	CHECK_HAS(Result.Text, "reference_sound gives a song their sounds");
	CHECK(ReadReferenceFile(Song.Library["api-swing-ftm.json"], Kept, Error));
	CHECK(!Kept.Sounds.empty() && Kept.Game == "Api Swing" && Kept.Title == "Api Swing" && Kept.Composer == "Tester" && Kept.Track == 1);
	CHECK(std::find(Kept.Tags.begin(), Kept.Tags.end(), "famitracker") != Kept.Tags.end() &&
		std::find(Kept.Tags.begin(), Kept.Tags.end(), "echo-channel") != Kept.Tags.end());
	CHECK(!Run(Song, "reference_sound", "{\"reference\": \"api-swing-ftm\"}").IsError);
	CHECK_HAS(Run(Song, "study_module", "{\"path\": \"" + FtmPath + "\", \"tracks\": \"3\"}").Text, "tracks:");
	{
		// A module cut short says why
		{
			std::ofstream Out("api_tests_broken.ftm", std::ios::binary);
			Out.write("FamiTracker Module\x40\x04\x00\x00PARAMS", 28);
		}
		CHECK_HAS(Run(Song, "study_module", "{\"path\": \"api_tests_broken.ftm\"}").Text, "The FamiTracker module api_tests_broken.ftm "
			"couldn't be read");
		std::remove("api_tests_broken.ftm");
	}
	CHECK_HAS(Run(Song, "references", "{\"action\": \"delete\", \"reference\": \"api-swing-ftm\"}").Text, "Deleted the reference");
	std::remove(FtmPath.c_str());

	// delete
	CHECK_HAS(Run(Song, "references", "{\"action\": \"delete\", \"reference\": \"tester-song-01\"}").Text, "Deleted the reference tester-song-01");
	CHECK(Song.Library.empty());
	CHECK_HAS(Run(Song, "references", "{}").Text, "The reference library is empty");
	std::remove(Path.c_str());
	std::remove("api_tests_study.m3u");
}

void TestSampleDrums()
{
	// The kit: each sample the DMC's own length, back to silence at its end, deep or bright as its drum
	const DpcmKit &Kit = SampleDrums();
	CHECK(Kit.Samples.size() == 4 && Kit.Keys.size() == 4);
	for (size_t i = 0; i < Kit.Samples.size(); ++i) {
		const std::vector<unsigned char> &Data = Kit.Samples[i].Data;
		CHECK(Data.size() % 16 == 1 && Data.size() > 300 && Data.size() < 1200);
		const std::vector<int> Levels = DecodeDpcm(Data, 0);
		int Peak = 0;
		for (size_t k = 0; k < Levels.size(); ++k)
			Peak = std::max(Peak, Levels[k]);
		CHECK(Peak >= 40 && Peak <= 100 && Levels.back() <= 4);
	}
	CHECK(FindSampleDrum("Kick") == &Kit.Keys[0] && FindSampleDrum("bass drum") == &Kit.Keys[0] && FindSampleDrum("snare") == &Kit.Keys[1] &&
		FindSampleDrum("tom") == &Kit.Keys[2] && FindSampleDrum("hi tom") == &Kit.Keys[3] && FindSampleDrum("cowbell") == 0);
	CHECK(Kit.Keys[0].Pitch == 36 && Kit.Keys[1].Pitch == 38 && Kit.Keys[0].Rate == 15 && Kit.Keys[0].Delta == 0);
	// Encoding follows a wave within the DMC's steps of 2
	std::vector<double> Ramp;
	for (int i = 0; i < 100; ++i)
		Ramp.push_back(i < 50 ? i * 2.0 : 100.0);
	const std::vector<unsigned char> Encoded = EncodeDpcm(Ramp, 0);
	const std::vector<int> Back = DecodeDpcm(Encoded, 0);
	// (a step behind, as it holds at 0 for its first step down), and back at its start level after it
	CHECK(Encoded.size() % 16 == 1 && Back[48] >= 92 && Back[48] <= 100 && Back[99] >= 98 && Back.back() <= 2);
	// Started from a higher level (a Zxx on the sample's row, as mix writes), each sample walks the same way and ends
	// where it started: the toms used to slide to silence at their end, which clicked
	for (size_t i = 0; i < Kit.Samples.size(); ++i) {
		const std::vector<int> FromSilence = DecodeDpcm(Kit.Samples[i].Data, 0);
		for (int Start = 16; Start <= 48; Start += 16) {
			const std::vector<int> Played = DecodeDpcm(Kit.Samples[i].Data, Start);
			bool Same = Played.size() == FromSilence.size();
			for (size_t k = 0; k < Played.size() && Same; ++k)
				Same = Played[k] == FromSilence[k] + Start;
			CHECK(Same && std::abs(Played.back() - Start) <= 2);
		}
	}
	// A wave below the floor or above the ceiling holds there: the steps still cancel out
	{
		std::vector<double> Dip(200, 0.0);
		for (int i = 0; i < 60; ++i)
			Dip[static_cast<size_t>(i)] = -40.0;
		const std::vector<int> Low = DecodeDpcm(EncodeDpcm(Dip, 0), 0), High = DecodeDpcm(EncodeDpcm(Dip, 0), 60);
		CHECK(Low.size() == High.size() && std::abs(Low.back()) <= 2 && std::abs(High.back() - 60) <= 2);
	}

	// The tool, and write_notes on the DPCM channel by name
	FakeSong Song;
	ToolResult Result = Run(Song, "add_sample_drums", "{}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "Added \"Sample drums\" (03): kick on C-3, snare on D-3, low tom on F-3 and high tom on A-3 (2.6 KB of samples)");
	CHECK_HAS(Run(Song, "add_sample_drums", "{}").Text, "The song already has \"Sample drums\" (03)");
	CHECK(Song.Kits.size() == 1);
	// A kit an older version made has its samples brought up to date, and the tools say so
	Song.OutdatedKitSamples = 2;
	CHECK_HAS(Run(Song, "add_sample_drums", "{}").Text, "The song's sampled drums were brought up to date");
	CHECK_LACKS(Run(Song, "add_sample_drums", "{}").Text, "brought up to date");
	Song.OutdatedKitSamples = 3;
	Song.Info.DirectChanges = true;
	Result = Run(Song, "add_drums", "{\"kit\": \"samples\", \"from_bar\": 1, \"to_bar\": 2}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "The song's sampled drums were brought up to date");
	Song.Info.DirectChanges = false;
	FakeSong Fresh;
	Result = Run(Fresh, "write_notes", "{\"channel\": \"DPCM\", \"notes\": [{\"bar\": 1, \"beat\": 1, \"pitch\": \"kick\", "
		"\"beats\": 0.25}, {\"bar\": 1, \"beat\": 2, \"pitch\": \"snare\", \"beats\": 0.25}, {\"bar\": 1, \"beat\": 3, "
		"\"pitch\": \"kick\", \"beats\": 0.25}]}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "Added the instrument \"Sample drums\" (03)");
	CHECK(Fresh.Kits.size() == 1);
	int Dpcm = -1;
	for (size_t c = 0; c < Fresh.Info.Channels.size(); ++c)
		if (Fresh.Info.Channels[c].Kind == KIND_DPCM)
			Dpcm = static_cast<int>(c);
	CHECK(Dpcm >= 0);
	if (Dpcm >= 0) {
		const std::vector<Cell> &Rows = Fresh.Clips.empty() ? Fresh.Rows[static_cast<size_t>(Dpcm)] : Fresh.Clips.back().Columns[0];
		CHECK(Rows.size() >= 9 && Rows[0].Pitch() == 36 && Rows[0].Instrument == 3 && Rows[4].Pitch() == 38 && Rows[8].Pitch() == 36);
		bool Cut = false;
		for (size_t r = 1; r < 8 && r < Rows.size(); ++r)
			Cut = Cut || Rows[r].IsEnd();
		CHECK(!Cut);		// the hits ring to the next
		bool Halted = false;
		for (size_t r = 0; r < Rows.size(); ++r)
			Halted = Halted || Rows[r].IsEnd();
		CHECK(!Halted);	// and nothing ends the last: a halt on the DPCM channel would reset the DPCM level
	}
	CHECK_HAS(Run(Fresh, "write_notes", "{\"channel\": \"DPCM\", \"notes\": [{\"bar\": 1, \"beat\": 1, \"pitch\": \"cowbell\", "
		"\"beats\": 1}]}").Text, "isn't a drum or a key");

	// add_drums with the sampled kit: the kick and snare on the DPCM channel, the hi-hats kept on the noise where they'd give way
	FakeSong Kit2;
	Kit2.Info.DirectChanges = true;
	Result = Run(Kit2, "add_drums", "{\"groove\": \"basic\", \"kit\": \"samples\", \"from_bar\": 1, \"to_bar\": 4}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "basic drums: sampled kick and snare on 2A03 DPCM, hi-hats on 2A03 Noise, a fill every 4 bars, bars 1-4");
	CHECK_HAS(Result.Text, "Added the instrument \"Sample drums\" (06)");
	CHECK(Kit2.Rows[4][0].Pitch() == 36 && Kit2.Rows[4][0].Instrument == 6 && Kit2.Rows[4][4].Pitch() == 38 && Kit2.Rows[4][8].Pitch() == 36);
	CHECK(Kit2.Rows[3][4].HasNote() && Kit2.Rows[3][4].Pitch() == 0xE && Kit2.Rows[3][8].Pitch() == 0xE);	// hats under the snare and kick
	CHECK(Kit2.Rows[3][0].Pitch() == 0xC && !Kit2.Rows[3][2].HasNote());	// the crash, and no hat to cut it short
	{
		// Ghost snares stay soft on the noise; the fill's toms are sampled
		bool HighTom = false, LowTom = false;
		for (int r = 48; r < 64; ++r) {
			HighTom = HighTom || Kit2.Rows[4][r].Pitch() == 45;
			LowTom = LowTom || Kit2.Rows[4][r].Pitch() == 41;
		}
		CHECK(HighTom && LowTom);
	}
	CHECK_HAS(Run(Kit2, "add_drums", "{\"kit\": \"cowbells\", \"from_bar\": 1}").Text, "kit: noise");
	{
		// Drums over part of a song stop without a halt on the DPCM channel, where it would reset the DPCM level
		// (the triangle and the noise jumping to full volume): the last sample plays out
		FakeSong Part;
		Part.Info.DirectChanges = true;
		CHECK(!Run(Part, "add_drums", "{\"groove\": \"basic\", \"kit\": \"samples\", \"from_bar\": 1, \"to_bar\": 1}").IsError);
		bool Halted = false;
		for (size_t r = 0; r < Part.Rows[4].size(); ++r)
			Halted = Halted || Part.Rows[4][r].IsEnd();
		CHECK(Part.Rows[4][0].HasNote() && !Halted);
		// Nor does clearing a bar halt a sample from the bar before
		FakeSong Clear;
		Clear.Info.DirectChanges = true;
		CHECK(!Run(Clear, "add_drums", "{\"groove\": \"basic\", \"kit\": \"samples\", \"from_bar\": 1, \"to_bar\": 2}").IsError);
		CHECK(!Run(Clear, "write_notes", "{\"channel\": \"DPCM\", \"notes\": [], \"from_bar\": 2, \"to_bar\": 2}").IsError);
		Halted = false;
		for (size_t r = 0; r < Clear.Rows[4].size(); ++r)
			Halted = Halted || Clear.Rows[4][r].IsEnd() || (r >= 16 && Clear.Rows[4][r].HasNote());
		CHECK(Clear.Rows[4][0].HasNote() && !Halted);
	}
	// A groove with it; the mix holds its level through every sample
	FakeSong Groove;
	Groove.Info.DirectChanges = true;
	Result = Run(Groove, "make_groove", "{\"mood\": \"town\", \"key\": \"C major\", \"bars\": 4, \"kit\": \"samples\"}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "drums: sampled kick and snare on 2A03 DPCM, hi-hats on 2A03 Noise");
	Result = Run(Groove, "mix", "{}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "the DPCM level Z30 at bar 1 beat 1 and with each of its ");
	CHECK_HAS(Result.Text, "samples (which would reset it)");
	int Hits = 0, Held = 0;
	for (size_t r = 0; r < Groove.Rows[4].size(); ++r) {
		if (!Groove.Rows[4][r].HasNote())
			continue;
		++Hits;
		Held += Groove.Rows[4][r].Effect[0] == EFF_DAC && Groove.Rows[4][r].Param[0] == 0x30 ? 1 : 0;
	}
	CHECK(Hits >= 8 && Held == Hits);
	CHECK_HAS(Run(Groove, "review_arrangement", "{}").Text, "drums on 2A03 Noise with samples on 2A03 DPCM");
	// Every drum on the noise: the review suggests the samples
	FakeSong Noisy;
	Noisy.Info.DirectChanges = true;
	CHECK(!Run(Noisy, "make_groove", "{\"mood\": \"upbeat\", \"key\": \"C major\", \"bars\": 8}").IsError);
	CHECK_HAS(Run(Noisy, "review_arrangement", "{}").Text, "add_drums with kit samples puts a sampled kick and snare on the DPCM channel");
}

void TestInstruments()
{
	FakeSong Song;
	ToolResult Result = Run(Song, "add_instrument", "{\"preset\": \"soft lead\", \"channel\": \"VRC6 Pulse 2\"}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "Added \"Soft lead\" (03) (VRC6 instrument");
	Result = Run(Song, "add_instrument", "{\"preset\": \"Flute\", \"chip\": \"vrc6\"}");
	CHECK_HAS(Result.Text, "already has \"Soft lead\" (03)");
	CHECK(Run(Song, "add_instrument", "{\"preset\": \"kazoo\"}").IsError);
	CHECK_HAS(Run(Song, "add_instrument", "{\"preset\": \"snare\", \"channel\": 1}").Text, "drum for the noise channel");

	// Presets: every one has a volume envelope in range and sane points
	for (size_t i = 0; i < Presets().size(); ++i) {
		const Preset &P = Presets()[i];
		CHECK(FindPreset(P.Name) == &P);
		for (int Chip = 0; Chip < 2; ++Chip) {
			const std::vector<PresetSequence> &List = Chip == 0 ? P.Sequences2A03 : P.SequencesVRC6;
			if (Chip == 1 && P.Drum) {
				CHECK(List.empty());
				continue;
			}
			bool Volume = false;
			for (size_t s = 0; s < List.size(); ++s) {
				const PresetSequence &Seq = List[s];
				CHECK(!Seq.Items.empty() && Seq.Items.size() <= 252);
				CHECK(Seq.Release < static_cast<int>(Seq.Items.size()) && Seq.Loop < static_cast<int>(Seq.Items.size()));
				for (size_t k = 0; k < Seq.Items.size(); ++k) {
					if (Seq.Type == SEQUENCE_VOLUME)
						CHECK(Seq.Items[k] >= 0 && Seq.Items[k] <= 15);
					if (Seq.Type == SEQUENCE_DUTY)
						CHECK(Seq.Items[k] >= 0 && Seq.Items[k] <= (Chip == 0 ? 3 : 7));
				}
				if (Seq.Type == SEQUENCE_VOLUME) {
					Volume = true;
					CHECK(Seq.Items.back() == 0);	// fades to silence
				}
				CheckSequence(Seq, P.Drum);
			}
			CHECK(Volume);
		}
	}
	CHECK(FindPreset("Pad") != 0 && FindPreset("Pad")->Name == "warm pad");
	CHECK(FindPreset("hi hat") != 0);

	// set_instrument changes every note in the bars
	Song.Info.DirectChanges = true;
	Song.Put(0, 0, "C-4", 2);
	Song.Put(0, 4, "D-4");
	Song.Put(0, 20, "E-4");
	Result = Run(Song, "set_instrument", "{\"channel\": 1, \"instrument\": \"bright lead\", \"to_bar\": 1}");
	CHECK(!Result.IsError);
	const int Bright = Song.Info.Instruments.back().Index;
	CHECK(Song.Rows[0][0].Instrument == Bright && Song.Rows[0][4].Instrument == Bright);
	CHECK(Song.Rows[0][20].Instrument == 2);	// the next note keeps playing with the old one
	CHECK_HAS(Result.Text, "(2 notes)");
}

void TestTranspose()
{
	FakeSong Song;
	Song.Info.DirectChanges = true;
	const char *const MELODY[] = { "C-4", "D-4", "E-4", "F-4", "G-4", "A-4", "B-4", "C-5" };
	for (int i = 0; i < 8; ++i)
		Song.Put(5, i * 4, MELODY[i], i == 0 ? 0 : NO_INSTRUMENT);
	Song.Put(1, 0, "C-3", 2);
	Song.Rows[1][0].Effect[0] = EFF_ARPEGGIO;
	Song.Rows[1][0].Param[0] = 0x47;	// C major chord
	Song.Put(3, 0, "kick", 1);

	ToolResult Result = Run(Song, "transpose", "{\"channels\": [\"VRC6 Pulse 1\"], \"semitones\": 12, \"as_clip\": true}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "up 12 semitones, 1 channel, bars 1-8");
	CHECK(Song.Clips.size() == 1 && Song.Clips[0].Columns[0][0].Pitch() == 60);
	CHECK(Song.Rows[5][0].Pitch() == 48);	// the song stays

	Result = Run(Song, "transpose", "{\"to_mode\": \"minor\", \"key\": \"C major\"}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "from C major to C minor, 2 channels");
	CHECK_HAS(Result.Text, "E to Eb");
	CHECK_EQ(Notes(Song, 5, 0, 32), std::string("0:C-4 4:D-4 8:D#4 12:F-4 16:G-4 20:G#4 24:A#4 28:C-5 "));
	CHECK(Song.Rows[1][0].Param[0] == 0x37);	// the chord turned minor
	CHECK(Song.Rows[3][0].Pitch() == 4);	// drums stay
	CHECK_EQ(Song.Writes, 1);	// one undoable change for all channels

	CHECK(Run(Song, "transpose", "{\"semitones\": 2, \"to_mode\": \"major\"}").IsError);
	CHECK(Run(Song, "transpose", "{}").IsError);
	CHECK(Run(Song, "transpose", "{\"channels\": \"noise\", \"semitones\": 1}").IsError);
	CHECK(Run(Song, "transpose", "{\"to_key\": \"H minor\"}").IsError);
	CHECK_HAS(Run(Song, "transpose", "{\"key\": \"C minor\", \"to_mode\": \"minor\"}").Text, "already in C minor. Pick another");
	CHECK_HAS(Run(Song, "transpose", "{\"channels\": 2, \"semitones\": 1, \"from_bar\": 3}").Text, "No notes to transpose in bars 3-8");
	Result = Run(Song, "transpose", "{\"channels\": 6, \"semitones\": 40}");
	CHECK_HAS(Result.Text, "would have left FamiTracker's range");
}

void TestTempoTool()
{
	FakeSong Song;
	CHECK_HAS(Run(Song, "set_tempo", "{\"bpm\": 120}").Text, "speed and tempo boxes");
	Song.Info.DirectChanges = true;
	ToolResult Result = Run(Song, "set_tempo", "{\"bpm\": 120}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "Tempo set to 120 BPM (speed 6, tempo 120); it was 150 BPM (speed 6, tempo 150). One Ctrl+Z undoes it.");
	CHECK_HAS(Result.Text, "Its rows last 7 or 8 ticks, so the rhythm isn't quite even. Even rows nearby: 128.6 BPM (speed 7, tempo 150) "
		"or 112.5 BPM (speed 8, tempo 150).");
	CHECK(Song.Info.Speed == 6 && Song.Info.Tempo == 120);
	// Where rows can be even, the speed changes at tempo 150
	Result = Run(Song, "set_tempo", "{\"bpm\": 180}");
	CHECK_HAS(Result.Text, "Tempo set to 180 BPM (speed 5, tempo 150); it was 120 BPM (speed 6, tempo 120).");
	CHECK_LACKS(Result.Text, "isn't quite even");
	CHECK(Song.Info.Speed == 5 && Song.Info.Tempo == 150);
	// FamiTracker's own numbers, as the user names them
	Result = Run(Song, "set_tempo", "{\"speed\": 8}");
	CHECK_HAS(Result.Text, "Tempo set to 112.5 BPM (speed 8, tempo 150)");
	CHECK(Song.Info.Speed == 8 && Song.Info.Tempo == 150);
	Result = Run(Song, "set_tempo", "{\"speed\": 3, \"tempo\": 75}");
	CHECK_HAS(Result.Text, "150 BPM (speed 3, tempo 75)");
	CHECK(Song.Info.Speed == 3 && Song.Info.Tempo == 75);
	Result = Run(Song, "set_tempo", "{\"tempo\": 125}");
	CHECK(Song.Info.Speed == 3 && Song.Info.Tempo == 125);
	CHECK_HAS(Result.Text, "Its rows last 3 or 4 ticks");
	CHECK_HAS(Run(Song, "set_tempo", "{\"bpm\": 150, \"speed\": 6}").Text, "(speed 6, tempo 150)");
	CHECK_HAS(Run(Song, "set_tempo", "{\"bpm\": 90, \"tempo\": 150}").Text, "(speed 10, tempo 150)");
	CHECK_HAS(Run(Song, "set_tempo", "{\"speed\": 10}").Text, "The song plays at 90 BPM (speed 10, tempo 150) already.");
	CHECK_HAS(Run(Song, "set_tempo", "{\"speed\": 40}").Text, "speed");
	CHECK(Run(Song, "set_tempo", "{\"tempo\": 20}").IsError);
	CHECK(Run(Song, "set_tempo", "{\"bpm\": 120, \"speed\": 6, \"tempo\": 120}").IsError);
	CHECK(Run(Song, "set_tempo", "{}").IsError);
	// The song's own speed commands would set the speed back: then only the tempo changes
	Song.Rows[2][70].Effect[0] = EFF_SPEED;
	Song.Rows[2][70].Param[0] = 0x05;
	Result = Run(Song, "set_tempo", "{\"bpm\": 100}");
	CHECK_HAS(Result.Text, "F05 effect at bar 5 beat 2.5 on 2A03 Triangle");
	CHECK(Song.Info.Speed == 10 && Song.Info.Tempo == 167);
	CHECK_LACKS(Result.Text, "Even rows nearby");
	// Tempo commands: a note on them
	Song.Rows[2][70].Param[0] = 0x96;
	Result = Run(Song, "set_tempo", "{\"bpm\": 180}");
	CHECK_HAS(Result.Text, "Tempo set to 180 BPM (speed 5, tempo 150)");
	CHECK_HAS(Result.Text, "F96 effect at bar 5 beat 2.5");
}

void TestBacking()
{
	FakeSong Song;
	Song.Info.DirectChanges = true;
	// C major melody over 4 bars on VRC6 Pulse 1
	const char *const MELODY[] = { "E-4", "G-4", "C-5", "G-4", "F-4", "A-4", "C-5", "A-4", "D-4", "G-4", "B-4", "G-4", "C-4", "E-4", "G-4", "C-5" };
	for (int i = 0; i < 16; ++i)
		Song.Put(5, i * 4, MELODY[i], i == 0 ? 0 : NO_INSTRUMENT);
	Song.End(5, 64, NOTE_RELEASE);
	const ToolResult Result = Run(Song, "add_chords_and_bass", "{}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "arpeggio chords on 2A03 Pulse 1 and a root_fifth bass line on 2A03 Triangle, bars 1-4, in C major");
	CHECK_HAS(Result.Text, "Chords: | C");
	// Chords: notes with arpeggios; bass: notes in a low register
	CHECK(Song.Rows[0][0].HasNote() && Song.Rows[0][0].Effect[0] == EFF_ARPEGGIO);
	CHECK(Song.Rows[2][0].HasNote() && Song.Rows[2][0].Pitch() < 36);
	CHECK_EQ(Song.Writes, 1);
	bool Soft = false;
	for (size_t i = 0; i < Song.Info.Instruments.size(); ++i)
		Soft = Soft || Song.Info.Instruments[i].Name == "Soft chords";
	CHECK(Soft);
	// With every channel busy it says so
	FakeSong Busy;
	Busy.Info.DirectChanges = true;
	for (int c = 0; c < 8; ++c)
		if (c != 3 && c != 4)
			Busy.Put(c, 0, "C-4", c < 5 ? 2 : 0);
	CHECK_HAS(Run(Busy, "add_chords_and_bass", "{\"melody_channel\": 6}").Text, "already plays in those bars");
	CHECK(!Run(Busy, "add_chords_and_bass", "{\"melody_channel\": 6, \"chord_channel\": \"none\", \"bass_channel\": \"sawtooth\"}").IsError);
}

// Arranging: chords by name, progressions, colors, parts in styles

std::string ChordText(const char *Text, const Key &K = Key(0, MODE_MAJOR))
{
	ftapi::Chord C;
	if (!ParseChord(Text, K, C))
		return "-";
	std::string Out = ChordName(C, K) + " " + ChordDegree(C, K) + " {";
	for (size_t i = 0; i < C.Tones.size(); ++i)
		Out += (i == 0 ? "" : " ") + std::to_string(C.Tones[i]);
	return Out + "}";
}

std::vector<BarSpan> FourFour(int Count)
{
	std::vector<BarSpan> Bars;
	for (int b = 0; b < Count; ++b)
		Bars.push_back(BarSpan(b * 16, 16));
	return Bars;
}

std::vector<ChordAt> Progression(const char *Text, const Key &K = Key(0, MODE_MAJOR), int Bars = 4)
{
	std::vector<ChordAt> Out;
	std::string Error;
	if (!LayOutProgression(SplitProgression(Text), FourFour(Bars), 4, K, Out, Error))
		std::printf("progression error: %s\n", Error.c_str());
	return Out;
}

NoteInfo MelodyNote(int Row, int Rows, const char *Pitch, int Volume = 15)
{
	NoteInfo N = NoteInfo();
	N.Row = Row;
	N.Rows = Rows;
	ParsePitch(Pitch, N.Pitch);
	N.Instrument = 0;
	N.Volume = Volume;
	return N;
}

void TestChords()
{
	// Names, with the key's spelling and as degrees
	CHECK_EQ(ChordText("C"), "C I {0 4 7}");
	CHECK_EQ(ChordText("Am7"), "Am7 vi7 {0 3 7 10}");
	CHECK_EQ(ChordText("F#m7b5"), "F#m7b5 #ivø7 {0 3 6 10}");
	CHECK_EQ(ChordText("Bbmaj7"), "Bbmaj7 bVIImaj7 {0 4 7 11}");
	CHECK_EQ(ChordText("G7sus4"), "G7sus4 V7sus4 {0 5 7 10}");
	CHECK_EQ(ChordText("Cadd9"), "Cadd9 Iadd9 {0 2 4 7}");
	CHECK_EQ(ChordText("Dm9"), "Dm9 ii9 {0 2 3 7 10}");
	CHECK_EQ(ChordText("Bdim"), "Bdim viio {0 3 6}");
	CHECK_EQ(ChordText("Bdim7"), "Bdim7 viio7 {0 3 6 9}");
	CHECK_EQ(ChordText("E7b9"), "E7b9 III7b9 {0 1 4 7 10}");
	CHECK_EQ(ChordText("C6/9"), "C6/9 I6/9 {0 2 4 7 9}");
	CHECK_EQ(ChordText("C5"), "C5 I5 {0 7}");
	CHECK_EQ(ChordText("C/E"), "C/E I/E {0 4 7}");
	CHECK_EQ(ChordText("Cm(maj7)"), "Cm(maj7) imaj7 {0 3 7 11}");
	CHECK_EQ(ChordText("C-7"), "Cm7 i7 {0 3 7 10}");
	CHECK_EQ(ChordText("CM7"), "Cmaj7 Imaj7 {0 4 7 11}");
	CHECK_EQ(ChordText("C+"), "Caug I+ {0 4 8}");
	CHECK_EQ(ChordText("C\xC3\xB8" "7"), "Cm7b5 iø7 {0 3 6 10}");
	CHECK_EQ(ChordText("C\xCE\x94"), "Cmaj7 Imaj7 {0 4 7 11}");
	CHECK_EQ(ChordText("Ebadd9"), "Ebadd9 bIIIadd9 {0 2 4 7}");
	// Degrees of the key: upper case major, lower case minor; secondary dominants
	CHECK_EQ(ChordText("vi"), "Am vi {0 3 7}");
	CHECK_EQ(ChordText("V7"), "G7 V7 {0 4 7 10}");
	CHECK_EQ(ChordText("IVmaj7"), "Fmaj7 IVmaj7 {0 4 7 11}");
	CHECK_EQ(ChordText("bVII"), "Bb bVII {0 4 7}");
	CHECK_EQ(ChordText("viio"), "Bdim viio {0 3 6}");
	CHECK_EQ(ChordText("V7/V"), "D7 II7 {0 4 7 10}");
	CHECK_EQ(ChordText("IV/V"), "F/G IV/G {0 4 7}");
	CHECK_EQ(ChordText("bVII", Key(9, MODE_MINOR)), "G VII {0 4 7}");
	CHECK_EQ(ChordText("V", Key(9, MODE_MINOR)), "E V {0 4 7}");
	CHECK_EQ(ChordText("III", Key(9, MODE_MINOR)), "C III {0 4 7}");
	CHECK_EQ(ChordText("i"), "Cm i {0 3 7}");
	// Not chords
	CHECK_EQ(ChordText("H7"), "-");
	CHECK_EQ(ChordText("Cm5"), "-");
	CHECK_EQ(ChordText("C7x"), "-");
	CHECK_EQ(ChordText(""), "-");
	CHECK(!WrittenAsDegrees(SplitProgression("| C | Am | Bb |")));
	CHECK(WrittenAsDegrees(SplitProgression("C vi F G")));

	// Progressions: bars, shares, holds, repeats, no chord
	std::vector<std::string> Bars = SplitProgression("| C | Am7 | F G7 |");
	CHECK(Bars.size() == 3 && Bars[2] == "F G7");
	Bars = SplitProgression("I-vi-IV-V");
	CHECK(Bars.size() == 4 && Bars[3] == "V");
	Bars = SplitProgression("C, Am - F G");
	CHECK(Bars.size() == 4 && Bars[1] == "Am");
	std::vector<ChordAt> Chords = Progression("| C . . G | Am | % | N.C. |");
	CHECK_EQ(ProgressionText(Chords, FourFour(4), Key(0, MODE_MAJOR)), "| C G | Am | Am | N.C. |");
	CHECK(Chords.size() == 4 && Chords[0].Rows == 12 && Chords[1].Row == 12 && Chords[1].Rows == 4 && Chords[2].Rows == 16);
	CHECK(Chords[3].Row == 32 && Chords[3].Rows == 16);
	Chords = Progression("| F G Am |");	// three chords in four beats: the first one longer; the bar repeats
	CHECK(Chords.size() == 12 && Chords[0].Rows == 8 && Chords[1].Row == 8 && Chords[2].Row == 12 && Chords[3].Row == 16);
	Chords = Progression("C | .", Key(0, MODE_MAJOR), 4);	// held over the bar line; the progression repeats
	CHECK(Chords.size() == 2 && Chords[0].Rows == 32 && Chords[1].Row == 32 && Chords[1].Rows == 32);
	Chords = Progression("I vi IV V", Key(7, MODE_MAJOR));
	CHECK_EQ(ProgressionText(Chords, FourFour(4), Key(7, MODE_MAJOR)), "| G | Em | C | D |");
	CHECK_EQ(ProgressionText(Chords, FourFour(4), Key(7, MODE_MAJOR), true), "| I | vi | IV | V |");
	std::string Error;
	std::vector<ChordAt> Out;
	CHECK(!LayOutProgression(SplitProgression("C Xm7 G"), FourFour(4), 4, Key(), Out, Error));
	CHECK_HAS(Error, "\"Xm7\" isn't a chord");
	CHECK(!LayOutProgression(SplitProgression("| % | C |"), FourFour(4), 4, Key(), Out, Error));
	CHECK_HAS(Error, "first one");

	// The key's chords
	CHECK_EQ(ChordName(DiatonicChord(Key(0, MODE_MAJOR), 4, true), Key()), "G7");
	CHECK_EQ(ChordName(DiatonicChord(Key(9, MODE_MINOR), 0, true), Key(9, MODE_MINOR)), "Am7");
	CHECK_EQ(ChordName(DiatonicChord(Key(9, MODE_MINOR), 1, false), Key(9, MODE_MINOR)), "Bdim");

	// Colors: the key's sevenths; richer ones; a seventh the melody rubs against stays out
	chord_color Color;
	CHECK(ParseColor("Sevenths", Color) && Color == COLOR_SEVENTHS && ParseColor("lush", Color) && Color == COLOR_RICH);
	CHECK(!ParseColor("purple", Color));
	const Key C(0, MODE_MAJOR);
	Chords = Progression("C Am F G", C, 8);
	std::vector<ChordAt> Colored = Chords;
	ColorChords(Colored, std::vector<NoteInfo>(), C, 4, COLOR_SEVENTHS);
	CHECK_EQ(ProgressionText(Colored, FourFour(8), C), "| Cmaj7 | Am7 | Fmaj7 | G7 | Cmaj7 | Am7 | Fmaj7 | G7 |");
	Colored = Chords;
	ColorChords(Colored, std::vector<NoteInfo>(), C, 4, COLOR_RICH);
	CHECK_EQ(ProgressionText(Colored, FourFour(8), C), "| Cadd9 | Am9 | Fmaj7 | G7sus4 G7 | Cadd9 | Am9 | Fmaj7 | G7 |");
	std::vector<NoteInfo> Melody;
	Melody.push_back(MelodyNote(0, 16, "C-4"));		// C under a Cmaj7's B: a semitone
	Melody.push_back(MelodyNote(48, 8, "B-3"));		// B against the sus4's C
	Colored = Chords;
	ColorChords(Colored, Melody, C, 4, COLOR_SEVENTHS);
	CHECK_EQ(ProgressionText(Colored, FourFour(4), C), "| C | Am7 | Fmaj7 | G7 |");
	Colored = Chords;
	ColorChords(Colored, Melody, C, 4, COLOR_RICH);
	CHECK_EQ(ProgressionText(Colored, FourFour(4), C), "| Cadd9 | Am9 | Fmaj7 | G7 |");
	const Key Am(9, MODE_MINOR);
	Colored = Progression("Am F Dm E", Am, 4);
	ColorChords(Colored, std::vector<NoteInfo>(), Am, 4, COLOR_RICH);
	CHECK_EQ(ProgressionText(Colored, FourFour(4), Am), "| Am(add9) | Fmaj7 | Dm9 | E7 |");
	CHECK(MelodyRub(Melody, 0, 16, 11, 4) >= 1.0 && MelodyRub(Melody, 0, 16, 7, 4) == 0.0);

	// Hearing chords: an arpeggio and a bass
	std::vector<HeardNote> Heard;
	const int Pcs[4][3] = { { 0, 4, 7 }, { 9, 0, 4 }, { 5, 9, 0 }, { 7, 11, 2 } };
	for (int b = 0; b < 4; ++b) {
		for (int k = 0; k < 3; ++k) {
			HeardNote H;
			H.Row = b * 16;
			H.Rows = 16;
			H.PitchClass = Pcs[b][k];
			H.Weight = 0.6;
			H.Bass = false;
			Heard.push_back(H);
		}
		HeardNote Bass;
		Bass.Row = b * 16;
		Bass.Rows = 16;
		Bass.PitchClass = Pcs[b][0];
		Bass.Weight = 1.0;
		Bass.Bass = true;
		Heard.push_back(Bass);
	}
	CHECK_EQ(ProgressionText(HearChords(Heard, FourFour(4), 4, C, true), FourFour(4), C), "| C | Am | F | G |");
	Heard[3].PitchClass = 4;	// E in the bass under C
	CHECK_EQ(ProgressionText(HearChords(Heard, FourFour(4), 4, C, true), FourFour(4), C), "| C/E | Am | F | G |");
	CHECK(HearChords(std::vector<HeardNote>(), FourFour(4), 4, C, true).empty());
}

// Every note in order, inside [First, End), not overlapping
bool InOrder(const std::vector<PartNote> &Notes, int First, int End)
{
	for (size_t i = 0; i < Notes.size(); ++i) {
		if (Notes[i].Row < First || Notes[i].Row + Notes[i].Rows > End || Notes[i].Rows < 1)
			return false;
		if (i > 0 && Notes[i - 1].Row + Notes[i - 1].Rows > Notes[i].Row)
			return false;
	}
	return true;
}

int EffectOf(const PartNote &N, int Effect)
{
	for (size_t i = 0; i < N.Effects.size(); ++i)
		if (N.Effects[i].first == Effect)
			return N.Effects[i].second;
	return -1;
}

void TestArrange()
{
	const Key C(0, MODE_MAJOR);
	const std::vector<BarSpan> Bars = FourFour(4);
	std::vector<ChordAt> Chords = Progression("| C | Am7 | Fmaj7 | Dm7 G7 |");
	CHECK(FindStyle(ChordStyles(), "Broken") == CHORDS_BROKEN && FindStyle(Grooves(), "four on the floor") == GROOVE_FOUR_ON_FLOOR);
	CHECK(FindStyle(Grooves(), "battle") == GROOVE_DRIVING && FindStyle(BassStyles(), "walk") == BASS_WALKING);
	CHECK(FindStyle(ChordRhythms(), "kazoo") < 0);
	CHECK(StyleNames(BassStyles()).size() == 9 && StyleNames(Grooves()).size() == 9);

	// The 0xy voicings: three of the chord's tones, the lowest in range, no semitone steps
	Voicing V = ArpeggioVoicing(Chords[0].C, 0, 37, 31, 43, true);
	CHECK(V.Base == 36 && V.Param() == 0x47);
	for (size_t i = 0; i < Chords.size(); ++i) {
		const Voicing W = ArpeggioVoicing(Chords[i].C, &V, 37, 31, 43, true);
		CHECK(W.Base >= 31 && W.Base <= 43 && W.X > 1 && W.Y - W.X > 1 && W.Y <= 15);
		CHECK(Chords[i].C.HasPitchClass(W.Base % 12) && Chords[i].C.HasPitchClass((W.Base + W.X) % 12) &&
			Chords[i].C.HasPitchClass((W.Base + W.Y) % 12));
		V = W;
	}
	// A seventh chord keeps its third and seventh; a power chord is root, fifth and octave
	ftapi::Chord G7;
	ParseChord("G7", C, G7);
	V = ArpeggioVoicing(G7, 0, 37, 31, 43, false);
	CHECK(G7.HasPitchClass((V.Base + V.X) % 12) && (((V.Base % 12) == 11 || (V.Base + V.X) % 12 == 11 || (V.Base + V.Y) % 12 == 11)));
	CHECK((((V.Base % 12) == 5 || (V.Base + V.X) % 12 == 5 || (V.Base + V.Y) % 12 == 5)));
	ftapi::Chord Power;
	ParseChord("E5", C, Power);
	V = ArpeggioVoicing(Power, 0, 37, 31, 43, true);
	CHECK(V.Param() == 0x7C && V.Base % 12 == 4);

	// Held arpeggio: struck each bar and where a chord changes, 000 at the end
	ChordPartOptions O;
	O.Low = 31;
	O.High = 43;
	O.Volume = 12;
	std::vector<PartNote> Part = ChordPart(Chords, Bars, O);
	CHECK(InOrder(Part, 0, 64) && Part.size() == 5);
	CHECK(Part[0].Row == 0 && Part[0].Rows == 16 && EffectOf(Part[0], EFF_ARPEGGIO) == 0x47 && Part[0].Volume == 12);
	CHECK(Part[3].Row == 48 && Part[4].Row == 56 && Part[1].Volume == -1);
	CHECK(Part.back().EndEffects.size() == 1 && Part.back().EndEffects[0].first == EFF_ARPEGGIO && Part.back().EndEffects[0].second == 0);
	// Stabs on the offbeats: short, all as loud
	O.Style = CHORDS_STABS;
	Part = ChordPart(Chords, Bars, O);
	CHECK(InOrder(Part, 0, 64) && Part.size() == 16 && Part[0].Row == 2 && Part[0].Rows == 2 && Part[1].Row == 6);
	CHECK(Part[0].Volume == 12 && Part[1].Volume == -1 && EffectOf(Part[13], EFF_ARPEGGIO) > 0);
	// On beats: the bar's first strongest
	O.Rhythm = RHYTHM_BEATS;
	Part = ChordPart(Chords, Bars, O);
	CHECK(Part.size() == 16 && Part[0].Volume == 12 && Part[1].Volume == 11 && Part[4].Volume == 12);
	O.Rhythm = RHYTHM_TRESILLO;
	Part = ChordPart(Chords, Bars, O);
	CHECK(Part.size() == 12 && Part[1].Row == 6 && Part[2].Row == 12);
	// Broken chords: up the chord in eighths, again at each chord
	O.Style = CHORDS_BROKEN;
	O.Rhythm = -1;
	Part = ChordPart(Chords, Bars, O);
	CHECK(InOrder(Part, 0, 64) && Part.size() == 32 && Part[0].Rows == 2);
	CHECK(Part[0].Pitch < Part[1].Pitch && Part[1].Pitch < Part[2].Pitch && Part[2].Pitch < Part[3].Pitch && Part[4].Pitch == Part[0].Pitch);
	CHECK(Part[0].Volume == 12 && Part[1].Volume == 10 && Part[2].Volume == 11);
	O.Pattern = BROKEN_ALBERTI;
	Part = ChordPart(Chords, Bars, O);
	CHECK(Part[0].Pitch < Part[2].Pitch && Part[2].Pitch < Part[1].Pitch && Part[1].Pitch == Part[3].Pitch);
	O.Pattern = BROKEN_ROLLING;
	Part = ChordPart(Chords, Bars, O);
	CHECK(Part[3].Pitch - Part[0].Pitch > 12 && Part[4].Pitch == Part[2].Pitch);
	O.StepRows = 1;
	O.Pattern = BROKEN_UP;
	CHECK(ChordPart(Chords, Bars, O).size() == 64);
	// A guide-tone line: one note a chord, moving little
	O.Style = CHORDS_GUIDE;
	Part = ChordPart(Chords, Bars, O);
	CHECK(Part.size() == 5 && Part[0].Rows == 16);
	for (size_t i = 1; i < Part.size(); ++i)
		CHECK(std::abs(Part[i].Pitch - Part[i - 1].Pitch) <= 4);

	// Bass lines
	BassOptions B;
	B.K = C;
	B.Low = 24;
	B.High = 43;
	Part = BassPart(Chords, Bars, B);	// root and fifth
	CHECK(InOrder(Part, 0, 64) && Part[0].Pitch % 12 == 0 && Part[1].Row == 8 && Part[1].Pitch % 12 == 7 && Part[0].Volume == 15);
	for (size_t i = 0; i < Part.size(); ++i)
		CHECK(Part[i].Pitch >= 24 && Part[i].Pitch <= 45);
	B.Style = BASS_WALKING;
	Part = BassPart(Chords, Bars, B);
	CHECK(InOrder(Part, 0, 64) && Part.size() == 16 && Part[0].Pitch % 12 == 0 && Part[4].Pitch % 12 == 9);
	CHECK(PitchName(Part[1].Pitch) == "E-2" && PitchName(Part[2].Pitch) == "G-2" && PitchName(Part[3].Pitch) == "G#2");	// up to A
	CHECK(Part[3].Pitch == Part[4].Pitch - 1 || Part[3].Pitch == Part[4].Pitch + 1 || Part[3].Pitch == Part[4].Pitch + 2);	// an approach
	B.Style = BASS_OCTAVES;
	Part = BassPart(Chords, Bars, B);
	CHECK(Part.size() == 32 && Part[1].Pitch == Part[0].Pitch + 12 && Part[0].Rows == 2);
	B.Style = BASS_DRIVING;
	Part = BassPart(Chords, Bars, B);
	CHECK(Part.size() == 32 && Part[0].Rows == 1 && Part[1].Row == 2 && Part[1].Pitch == Part[0].Pitch);
	B.Style = BASS_PEDAL;
	Part = BassPart(Chords, Bars, B);
	for (size_t i = 0; i < Part.size(); ++i)
		CHECK(Part[i].Pitch % 12 == 0);
	B.Style = BASS_SYNCOPATED;
	Part = BassPart(Chords, Bars, B);
	CHECK(Part[1].Row == 6 && Part[2].Row == 12);
	Chords.push_back(Chords.back());	// nothing beyond the bars
	CHECK(InOrder(BassPart(Chords, Bars, B), 0, 64));

	// Drums: one sound a row, a crash to start, fills at the end of each phrase
	DrumOptions D;
	std::vector<DrumHit> Hits = DrumPart(FourFour(8), D);
	for (size_t i = 1; i < Hits.size(); ++i)
		CHECK(Hits[i].Row > Hits[i - 1].Row);
	CHECK(Hits[0].Row == 0 && Hits[0].Sound == "crash");
	int Kicks = 0, Snares = 0, Toms = 0, Crashes = 0, Open = 0;
	for (size_t i = 0; i < Hits.size(); ++i) {
		const DrumHit &H = Hits[i];
		CHECK(H.Volume >= 1 && H.Volume <= 15 && H.Pitch >= 0 && H.Pitch <= 15);
		Kicks += H.Sound == "kick";
		Snares += H.Sound == "snare" && H.Row < 48;
		Toms += H.Sound == "tom";
		Crashes += H.Sound == "crash";
		Open += H.Sound == "open hi-hat";
		if (H.Sound == "tom")
			CHECK(H.Row >= 60 && H.Row < 64);	// the first fill: the last beat of bar 4, in toms
	}
	CHECK(Kicks > 10 && Snares == 6 && Toms == 4 && Crashes == 2 && Open == 2);
	// No hi-hat for a beat after a crash: the noise channel would cut it off
	for (size_t i = 0; i < Hits.size(); ++i)
		if (Hits[i].Sound == "crash")
			for (size_t j = i + 1; j < Hits.size() && Hits[j].Row < Hits[i].Row + 4; ++j)
				CHECK(Hits[j].Sound != "hi-hat" && Hits[j].Sound != "open hi-hat");
	CHECK(Hits.back().Row == 127 && Hits.back().Sound == "snare" && Hits.back().Volume == 15);	// the second fill: a snare roll
	D.Intensity = 1;
	D.FillEvery = 0;
	D.Crash = false;
	Hits = DrumPart(FourFour(2), D);
	for (size_t i = 0; i < Hits.size(); ++i)
		CHECK(Hits[i].Row % 4 == 0 && Hits[i].Sound != "crash" && Hits[i].Sound != "tom");
	D.Intensity = 3;
	D.Groove = GROOVE_DRIVING;
	CHECK(DrumPart(FourFour(1), D).size() == 16);
	D.Groove = GROOVE_WALTZ;
	Hits = DrumPart(std::vector<BarSpan>(1, BarSpan(0, 12)), D);
	CHECK(Hits.size() >= 6 && Hits[0].Sound == "kick" && Hits.back().Row < 12);
	D.Intensity = 2;
	D.FillEvery = 2;
	D.PhraseStart = 1;	// the drums start in a phrase's second bar: the first bar ends it
	Hits = DrumPart(FourFour(2), D);
	bool FillFirst = false;
	for (size_t i = 0; i < Hits.size(); ++i)
		FillFirst = FillFirst || (Hits[i].Sound == "tom" && Hits[i].Row >= 12 && Hits[i].Row < 16);
	CHECK(FillFirst);

	// Harmony: a third under the melody in the key; a chord's note where the melody holds
	std::vector<NoteInfo> Melody;
	Melody.push_back(MelodyNote(0, 4, "E-4"));
	Melody.push_back(MelodyNote(4, 4, "G-4"));
	Melody.push_back(MelodyNote(8, 8, "C-5"));
	Melody.push_back(MelodyNote(16, 2, "A-4"));
	Melody.push_back(MelodyNote(18, 2, "G-4", 12));
	Melody.push_back(MelodyNote(32, 16, "F-4"));
	HarmonyOptions H;
	H.K = C;
	Part = HarmonyPart(Melody, Chords, H);
	CHECK_EQ(Part.size(), Melody.size());
	CHECK(PitchName(Part[0].Pitch) == "C-4" && PitchName(Part[1].Pitch) == "E-4" && Part[0].Volume == 12);
	CHECK(PitchName(Part[2].Pitch) == "E-4");	// the C chord's E (a sixth) rather than A, which isn't in it
	CHECK(PitchName(Part[4].Pitch) == "E-4" && Part[4].Volume == 9);	// a passing note: the third in the key
	CHECK(PitchName(Part[5].Pitch) == "A-3");	// D isn't in Fmaj7; A is
	H.Interval = HARMONY_OCTAVE_BELOW;
	CHECK(HarmonyPart(Melody, Chords, H)[2].Pitch == Melody[2].Pitch - 12);
	H.Interval = HARMONY_THIRD_ABOVE;
	CHECK(PitchName(HarmonyPart(Melody, Chords, H)[0].Pitch) == "G-4");
	H.Interval = HARMONY_THIRD_BELOW;
	H.MinRows = 4;
	CHECK(HarmonyPart(Melody, Chords, H).size() == 4);
	// Between the beats, over a chord that alters the key's scale (A major in A
	// minor): the chord's C#, not the scale's C, which would grate against it
	{
		const Key AMinorKey(9, MODE_MINOR);
		const std::vector<ChordAt> Major = Progression("| A | Dm | E | Am |", AMinorKey);
		std::vector<NoteInfo> Line;
		Line.push_back(MelodyNote(0, 4, "A-4"));
		Line.push_back(MelodyNote(5, 1, "E-4"));		// third below: C in the key, C# in the chord
		Line.push_back(MelodyNote(6, 1, "D-4"));		// B: no clash with A major, stays
		Line.push_back(MelodyNote(7, 1, "A-4"));		// F: a semitone over the chord's E, so F#, a third under A
		HarmonyOptions Ha;
		Ha.K = AMinorKey;
		const std::vector<PartNote> Under = HarmonyPart(Line, Major, Ha);
		CHECK(Under.size() == 4);
		if (Under.size() == 4) {
			CHECK_EQ(PitchName(Under[1].Pitch), std::string("C#4"));
			CHECK_EQ(PitchName(Under[2].Pitch), std::string("B-3"));
			CHECK_EQ(PitchName(Under[3].Pitch), std::string("F#4"));
		}
	}
	// Over sus chords of the key, the line stays in the key: under A over Dsus4 neither F (outside D major) nor G (a
	// step from A) fits, so the chord's D, a fifth under; under E over Dsus2, the chord's A rather than C
	{
		const Key DMajorKey(2, MODE_MAJOR);
		const std::vector<ChordAt> Sus = Progression("| Dsus4 | Dsus2 |", DMajorKey);
		std::vector<NoteInfo> Line;
		Line.push_back(MelodyNote(0, 4, "A-4"));
		Line.push_back(MelodyNote(16, 4, "E-4"));
		HarmonyOptions Hd;
		Hd.K = DMajorKey;
		const std::vector<PartNote> Under = HarmonyPart(Line, Sus, Hd);
		CHECK(Under.size() == 2);
		if (Under.size() == 2) {
			CHECK_EQ(PitchName(Under[0].Pitch), std::string("D-4"));
			CHECK_EQ(PitchName(Under[1].Pitch), std::string("A-3"));
		}
	}

	// A counter-melody: under the melody, never a semitone from it, holding where it's busy
	CounterOptions Counter;
	Counter.K = C;
	Counter.Low = 36;
	Counter.High = 50;
	Melody.clear();
	Melody.push_back(MelodyNote(0, 16, "E-4"));
	for (int i = 0; i < 8; ++i)
		Melody.push_back(MelodyNote(16 + i * 2, 2, i % 2 == 0 ? "A-4" : "G-4"));
	Melody.push_back(MelodyNote(32, 32, "C-5"));
	Part = CounterMelody(Melody, Chords, Bars, Counter);
	CHECK(InOrder(Part, 0, 64) && !Part.empty());
	for (size_t i = 0; i < Part.size(); ++i) {
		CHECK(Part[i].Pitch >= 34 && Part[i].Pitch <= 52);
		for (size_t m = 0; m < Melody.size(); ++m) {
			const NoteInfo &N = Melody[m];
			if (N.Row <= Part[i].Row && Part[i].Row < N.Row + N.Rows) {
				const int d = ((N.Pitch - Part[i].Pitch) % 12 + 12) % 12;
				CHECK(d != 1 && d != 11 && Part[i].Pitch < N.Pitch);
			}
		}
	}
	int InBusyBar = 0, InCalmBars = 0;
	for (size_t i = 0; i < Part.size(); ++i) {
		InBusyBar += Part[i].Row >= 16 && Part[i].Row < 24;
		InCalmBars += Part[i].Row >= 32;
	}
	CHECK(InBusyBar <= 1 && InCalmBars >= 2);
	// Nor does it double the melody an octave away where the chord has other notes: not E under the held E-4
	for (size_t i = 0; i < Part.size(); ++i)
		if (Part[i].Row < 16)
			CHECK(Part[i].Pitch % 12 != 4);
	Counter.Above = true;
	Counter.Low = 76;
	Counter.High = 88;
	Part = CounterMelody(Melody, Chords, Bars, Counter);
	for (size_t i = 0; i < Part.size(); ++i)
		CHECK(Part[i].Pitch >= 74);
	// Over chords from outside the key its passing notes don't rub against their tones: in G mixolydian, no C, D, A or
	// B over a C#dim7 (C# E G Bb), no G, F, A, B, C or D over an F#7 (F# A# C# E)
	{
		const Key GMix(7, 4);
		const std::vector<ChordAt> Chromatic = Progression("| G7 | C#dim7 | F#7 | G7 |", GMix);
		std::vector<NoteInfo> Held;
		Held.push_back(MelodyNote(0, 16, "B-4"));
		Held.push_back(MelodyNote(16, 16, "E-5"));
		Held.push_back(MelodyNote(32, 16, "E-5"));
		Held.push_back(MelodyNote(48, 16, "D-5"));
		CounterOptions Co;
		Co.K = GMix;
		Co.Low = 43;
		Co.High = 60;
		const std::vector<PartNote> Line = CounterMelody(Held, Chromatic, Bars, Co);
		int Moving = 0;
		for (size_t i = 0; i < Line.size(); ++i) {
			const int pc = Line[i].Pitch % 12;
			if (Line[i].Row >= 16 && Line[i].Row < 32) {
				++Moving;
				Check(pc != 0 && pc != 2 && pc != 9 && pc != 11, "no rub against the C#dim7", __LINE__);
			}
			if (Line[i].Row >= 32 && Line[i].Row < 48) {
				++Moving;
				Check(pc != 7 && pc != 5 && pc != 9 && pc != 11 && pc != 0 && pc != 2, "no rub against the F#7", __LINE__);
			}
		}
		CHECK(Moving >= 3);
	}

	// Tidying: order, overlaps, the range, volumes only where they change
	std::vector<PartNote> Untidy(3);
	Untidy[0].Row = 8;
	Untidy[0].Rows = 20;
	Untidy[0].Volume = 10;
	Untidy[1].Row = 0;
	Untidy[1].Rows = 12;
	Untidy[1].Volume = 10;
	Untidy[2].Row = 70;
	TidyPart(Untidy, 0, 64);
	CHECK(Untidy.size() == 2 && Untidy[0].Row == 0 && Untidy[0].Rows == 8 && Untidy[1].Rows == 20 && Untidy[1].Volume == -1);
}

void TestArrangingTools()
{
	// A backing in a style, from chords given by name; no melody needed
	FakeSong Song;
	Song.Info.DirectChanges = true;
	ToolResult Result = Run(Song, "add_chords_and_bass", "{\"chords\": \"| Am7 | Fmaj7 | C | G7sus4 G7 |\", \"chord_style\": \"broken\", "
		"\"pattern\": \"rolling\", \"bass_style\": \"walking\", \"key\": \"A minor\"}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "broken (rolling) chords on 2A03 Pulse 1 and a walking bass line on 2A03 Triangle, bars 1-4, in A minor");
	CHECK_HAS(Result.Text, "Chords: | Am7 | Fmaj7 | C | G7sus4 G7 |");
	CHECK_HAS(Result.Text, "As degrees of A minor: | i7 | VImaj7 | III | VII7sus4 VII7 |");
	CHECK_HAS(Result.Text, "Added the instrument");
	CHECK(Song.Rows[0][0].HasNote() && Song.Rows[0][2].HasNote() && Song.Rows[0][4].HasNote());	// eighths
	CHECK(Song.Rows[2][0].HasNote() && Song.Rows[2][4].HasNote() && Song.Rows[2][8].HasNote());	// a note a beat
	CHECK(Song.Rows[2][0].Pitch() % 12 == 9 && Song.Rows[2][0].Pitch() >= 21 && Song.Rows[2][0].Pitch() <= 40);	// A, where the triangle is a bass
	CHECK_EQ(Song.Writes, 1);
	// Repeats over the bars asked for; degrees need a key unless the notes give one
	Result = Run(Song, "add_chords_and_bass", "{\"chords\": [\"C\", \"G\"], \"from_bar\": 5, \"to_bar\": 8, \"bass_channel\": \"none\", "
		"\"chord_style\": \"stabs\", \"rhythm\": \"beats\"}");
	CHECK_HAS(Result.Text, "Chords: | C | G | C | G |");
	CHECK(Song.Rows[0][64].HasNote() && Song.Rows[0][68].HasNote() && Song.Rows[0][66].IsEnd());
	FakeSong Empty;
	CHECK_HAS(Run(Empty, "add_chords_and_bass", "{\"chords\": \"I IV V I\"}").Text, "need the key");
	CHECK(!Run(Empty, "add_chords_and_bass", "{\"chords\": \"I IV V I\", \"key\": \"D major\"}").IsError);
	CHECK_HAS(Run(Empty, "add_chords_and_bass", "{}").Text, "Give chords");
	CHECK_HAS(Run(Empty, "add_chords_and_bass", "{\"chords\": \"C Q7\"}").Text, "\"Q7\" isn't a chord");
	CHECK_HAS(Run(Empty, "add_chords_and_bass", "{\"chords\": \"C\", \"chord_style\": \"polka\"}").Text, "Choose from arpeggio, stabs");

	// Fitted to a melody, with colors; a melody rubbing against given chords is pointed out
	FakeSong Tune;
	Tune.Info.DirectChanges = true;
	const char *const MELODY[] = { "E-4", "G-4", "C-5", "G-4", "F-4", "A-4", "C-5", "A-4", "D-4", "G-4", "B-4", "G-4", "C-4", "E-4", "G-4", "C-5" };
	for (int i = 0; i < 16; ++i)
		Tune.Put(5, i * 4, MELODY[i], i == 0 ? 0 : NO_INSTRUMENT);
	Tune.End(5, 64, NOTE_RELEASE);
	Result = Run(Tune, "add_chords_and_bass", "{\"color\": \"sevenths\", \"as_clip\": true}");
	CHECK(!Result.IsError && Tune.Clips.size() == 1);
	CHECK_HAS(Result.Text, "7");
	Result = Run(Tune, "add_chords_and_bass", "{\"chords\": \"| C#m | C | C | C |\", \"as_clip\": true}");
	CHECK_HAS(Result.Text, "rubs a semitone against a chord tone in bar 1 (C#m)");

	// Drums: a groove over the bars the song plays, its instruments added
	Result = Run(Tune, "add_drums", "{\"groove\": \"four on the floor\", \"intensity\": \"busy\"}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "busy four_on_floor drums on 2A03 Noise, a fill every 4 bars, bars 1-4");
	CHECK(Tune.Rows[3][0].HasNote() && Tune.Rows[3][0].Pitch() == 0xC && Tune.Rows[3][4].Pitch() == 0x4);	// a crash, then kicks
	bool Kick = false, Crash = false, Tom = false;
	for (size_t i = 0; i < Tune.Info.Instruments.size(); ++i) {
		Kick = Kick || Tune.Info.Instruments[i].Name == "Kick";
		Crash = Crash || Tune.Info.Instruments[i].Name == "Crash";
		Tom = Tom || Tune.Info.Instruments[i].Name == "Tom";
	}
	CHECK(Kick && Crash && Tom);
	CHECK_HAS(Run(Tune, "add_drums", "{\"channel\": 1}").Text, "noise channel");
	CHECK_HAS(Run(Tune, "add_drums", "{\"groove\": \"zydeco\"}").Text, "Choose from basic");
	CHECK(!Run(Tune, "add_drums", "{\"groove\": \"calm\", \"fill_every\": 0, \"crash\": false, \"volume\": 10, \"from_bar\": 1, \"to_bar\": 2}").IsError);
	CHECK(Tune.Rows[3][0].Pitch() == 0x4 && Tune.Rows[3][0].Volume <= 10);

	// A harmony a third under the melody on the other VRC6 pulse, with the melody's instrument, on the chords there
	CHECK(!Run(Tune, "add_chords_and_bass", "{}").IsError);
	Result = Run(Tune, "add_harmony", "{}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "a harmony a third below VRC6 Pulse 1 on VRC6 Pulse 2, bars 1-4");
	CHECK_HAS(Result.Text, "16 notes, on the chords the other parts play");
	CHECK(Tune.Rows[6][0].HasNote() && Tune.Rows[6][0].Pitch() == Tune.Rows[5][0].Pitch() - 4 && Tune.Rows[6][0].Instrument == 0);
	CHECK(Tune.Rows[6][0].Volume == 12);
	// A counter-melody: on a free channel, or one that is named
	Result = Run(Tune, "add_counter_melody", "{\"channel\": \"VRC6 Pulse 2\", \"position\": \"above\"}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "a counter-melody above VRC6 Pulse 1 on VRC6 Pulse 2");
	CHECK_HAS(Run(Tune, "add_counter_melody", "{\"position\": \"sideways\"}").Text, "below or above");
	CHECK_HAS(Run(Tune, "add_harmony", "{\"melody_channel\": 4}").Text, "needs notes");

	// Finishing touches: accents, vibrato on long notes, gaps between repeated triangle notes
	FakeSong Plain;
	Plain.Info.DirectChanges = true;
	for (int i = 0; i < 8; ++i)
		Plain.Put(5, i * 2, "C-4", i == 0 ? 0 : NO_INSTRUMENT);
	Plain.Put(5, 16, "E-4");
	Plain.End(5, 32, NOTE_RELEASE);
	for (int i = 0; i < 4; ++i)
		Plain.Put(2, i * 4, "C-2", i == 0 ? 1 : NO_INSTRUMENT);
	Plain.End(2, 16);
	Result = Run(Plain, "polish", "{}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "accents");
	CHECK_HAS(Result.Text, "vibrato that comes in after half a beat on 1 long note of VRC6 Pulse 1");
	CHECK_HAS(Result.Text, "a short gap before 3 repeated notes on 2A03 Triangle");
	CHECK(Plain.Rows[5][0].Volume == 15 && Plain.Rows[5][4].Volume == 14 && Plain.Rows[5][2].Volume == 13);
	CHECK(HasEffectAt(Plain, 5, 18, EFF_VIBRATO, 0x52) && HasEffectAt(Plain, 5, 31, EFF_VIBRATO, 0));
	CHECK(Plain.Rows[2][3].Note == NOTE_HALT && Plain.Rows[2][4].HasNote() && Plain.Rows[2][15].Note == NOTE_NONE);
	CHECK_HAS(Run(Plain, "polish", "{\"touches\": [\"glitter\"]}").Text, "accents, vibrato, separate, fades, scoops, falls or staccato");
	CHECK_HAS(Run(Plain, "polish", "{\"touches\": [\"separate\"]}").Text, "nothing to polish");
}

void TestReview()
{
	// A plain song: one melody phrase four times over the same chords, held triads, roots, no drums
	FakeSong Song(4);
	Song.Info.DirectChanges = true;
	const char *const MELODY[] = { "E-4", "G-4", "C-5", "G-4", "F-4", "A-4", "C-5", "A-4", "D-4", "G-4", "B-4", "G-4", "C-4", "E-4", "G-4", "C-5" };
	for (int Copy = 0; Copy < 4; ++Copy)
		for (int i = 0; i < 16; ++i)
			Song.Put(5, Copy * 64 + i * 4, MELODY[i], i == 0 && Copy == 0 ? 0 : NO_INSTRUMENT);
	Song.End(5, 255, NOTE_RELEASE);
	CHECK(!Run(Song, "add_chords_and_bass", "{\"bass_style\": \"roots\", \"half_bars\": false}").IsError);
	ToolResult Result = Run(Song, "review_arrangement", "{}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "Depth review, bars 1-16 in C major: ");
	CHECK_HAS(Result.Text, "Parts: melody on VRC6 Pulse 1; chords on 2A03 Pulse 1; bass on 2A03 Triangle.");
	CHECK_HAS(Result.Text, "Harmony: | C | F | G | C |");
	CHECK_HAS(Result.Text, "all plain triads");
	CHECK_HAS(Result.Text, "The same four-bar progression plays 4 times.");
	CHECK_HAS(Result.Text, "No drums: add_drums");
	CHECK_HAS(Result.Text, "The melody has no company");
	CHECK_HAS(Result.Text, "The bass mostly holds roots");
	CHECK_HAS(Result.Text, "The melody keeps one or two note lengths");
	CHECK_HAS(Result.Text, "The melody never rests");
	CHECK_HAS(Result.Text, "repeat the bars four before them in every part");
	CHECK_HAS(Result.Text, "Length: 16 bars");
	const std::string Before = Result.Text;
	const int Plain = std::atoi(Before.substr(Before.find(": ") + 2).c_str());

	// Deeper: colored chords in stabs, a walking bass, drums, a counter-melody, finishing touches
	CHECK(!Run(Song, "add_chords_and_bass", "{\"color\": \"rich\", \"chord_style\": \"stabs\", \"bass_style\": \"walking\", "
		"\"chord_channel\": \"Pulse 1\", \"bass_channel\": \"Triangle\"}").IsError);
	CHECK(!Run(Song, "add_drums", "{}").IsError);
	CHECK(!Run(Song, "add_counter_melody", "{}").IsError);
	CHECK(!Run(Song, "polish", "{}").IsError);
	Result = Run(Song, "review_arrangement", "{}");
	const int Deeper = std::atoi(Result.Text.substr(Result.Text.find(": ") + 2).c_str());
	CHECK(Deeper >= Plain + 3);
	CHECK_LACKS(Result.Text, "No drums");
	CHECK_LACKS(Result.Text, "all plain triads");
	CHECK_LACKS(Result.Text, "The bass mostly holds roots");
	CHECK_HAS(Result.Text, "drums on 2A03 Noise");
	CHECK_HAS(Result.Text, "phrase ends change (fills)");
	// The picture's text names the chords too
	CHECK_HAS(Run(Song, "look_at_song", "{\"to_bar\": 4}").Text, "Chords heard (C major): |");
	FakeSong Empty;
	CHECK_HAS(Run(Empty, "review_arrangement", "{}").Text, "Nothing plays a melody");
	{
		// Vibrato built into the lead's instrument, or a 4xy still on from before, is vibrato
		FakeSong Long;
		for (int i = 0; i < 8; ++i)
			Long.Put(5, i * 16, i % 2 ? "E-4" : "C-4", i == 0 ? 0 : NO_INSTRUMENT);
		Long.End(5, 127, NOTE_RELEASE);
		CHECK_HAS(Run(Long, "review_arrangement", "{}").Text, "no vibrato");
		Long.Info.Instruments[0].Vibrato = true;
		CHECK_LACKS(Run(Long, "review_arrangement", "{}").Text, "no vibrato");
		Long.Info.Instruments[0].Vibrato = false;
		Long.Rows[5][0].Effect[0] = EFF_VIBRATO;
		Long.Rows[5][0].Param[0] = 0x42;
		CHECK_LACKS(Run(Long, "review_arrangement", "{}").Text, "no vibrato");
	}
	{
		// The melody isn't its echo (a channel repeating it later), nor a quiet busy part
		FakeSong Echoed;
		const char *const TUNE[] = { "C-4", "E-4", "G-4", "E-4", "F-4", "A-4", "G-4", "E-4" };
		for (int i = 0; i < 8; ++i) {
			Echoed.Put(6, i * 8, TUNE[i], i == 0 ? 0 : NO_INSTRUMENT, i == 0 ? 12 : -1);
			Echoed.Put(0, i * 8 + 3, TUNE[i], i == 0 ? 2 : NO_INSTRUMENT, i == 0 ? 12 : -1);	// the echo, as loud
		}
		for (int Row = 0; Row < 64; Row += 2)
			Echoed.Put(1, Row, Row % 4 ? "C-5" : "G-5", Row == 0 ? 2 : NO_INSTRUMENT, Row == 0 ? 3 : -1);	// busy, high, quiet
		CHECK_HAS(Run(Echoed, "review_arrangement", "{}").Text, "melody on VRC6 Pulse 2");
		// A calm track floats: no drums asked of it (by name, or with style calm)
		CHECK_HAS(Run(Echoed, "review_arrangement", "{}").Text, "No drums");
		CHECK_LACKS(Run(Echoed, "review_arrangement", "{\"style\": \"calm\"}").Text, "No drums");
		Echoed.Info.TrackName = "Town (night)";
		CHECK_LACKS(Run(Echoed, "review_arrangement", "{}").Text, "No drums");
		CHECK_HAS(Run(Echoed, "review_arrangement", "{\"style\": \"any\"}").Text, "No drums");
		CHECK_HAS(Run(Echoed, "review_arrangement", "{\"style\": \"loud\"}").Text, "style should be any or calm");
	}
}

void TestStructure()
{
	// A groove for a mood: chords, bass and drums in one change, in the mood's mode
	FakeSong Song(4);
	Song.Info.DirectChanges = true;
	ToolResult Result = Run(Song, "make_groove", "{\"mood\": \"battle\"}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "a battle groove: stabs (eighths) chords on 2A03 Pulse 1, a driving bass on 2A03 Triangle and driving drums on 2A03 Noise, bars 1-8, in A minor");
	CHECK_HAS(Result.Text, "Chords: | Am | F | G | Am | Am | F | G | Am |");
	CHECK_HAS(Result.Text, "The mood suits about 165 BPM");
	CHECK_HAS(Result.Text, "Next: a melody in A minor over these chords on 2A03 Pulse 2");
	CHECK_EQ(Song.Writes, 1);
	CHECK(Song.Rows[0][0].HasNote() && Song.Rows[2][0].HasNote() && Song.Rows[3][0].HasNote());
	CHECK(!Song.Rows[0][128].HasNote());	// eight bars, no more
	// Another mood by another word, in the song's key note; one without drums
	Result = Run(Song, "make_groove", "{\"mood\": \"village\", \"from_bar\": 9, \"bars\": 4, \"chord_channel\": \"VRC6 Pulse 1\", "
		"\"bass_channel\": \"Sawtooth\", \"drum_channel\": \"none\"}");
	CHECK_HAS(Result.Text, "a town groove: broken (updown) chords on VRC6 Pulse 1 and a root_fifth bass on VRC6 Sawtooth, bars 9-12, in A major");
	CHECK_HAS(Result.Text, "Chords: | Amaj7 | F#m7 | Dmaj7 | E7 |");
	CHECK_HAS(Run(Song, "make_groove", "{\"mood\": \"disco inferno\"}").Text, "Moods: town, overworld, battle");
	CHECK(!Run(Song, "make_groove", "{\"mood\": \"sad\", \"key\": \"E minor\", \"from_bar\": 13, \"bars\": 2}").IsError);

	// Copying bars: every channel, twice, the copy starting as the original played
	FakeSong Form(4);
	Form.Info.DirectChanges = true;
	Form.Put(5, 0, "C-4", 0, 10);
	Form.Put(5, 8, "E-4");
	Form.Put(5, 16, "G-4", 2);
	Form.Put(3, 0, "kick", 3);
	Form.Rows[5][12].Effect[0] = EFF_VIBRATO;
	Form.Rows[5][12].Param[0] = 0x42;
	Form.Rows[0][28].Effect[0] = EFF_JUMP;	// the song's own jump stays put
	Form.Rows[0][28].Param[0] = 0;
	Result = Run(Form, "copy_bars", "{\"from_bar\": 2, \"to_bar\": 2, \"at_bar\": 5, \"times\": 2, \"semitones\": 2}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "bar 2 copied to bars 5-6 (2 times), 2 semitones up");
	CHECK(Form.Rows[5][64].Pitch() == Form.Rows[5][16].Pitch() + 2 && Form.Rows[5][80].Pitch() == Form.Rows[5][64].Pitch());
	CHECK(Form.Rows[5][64].Instrument == 2);
	CHECK(Form.Rows[0][76].Effect[0] != EFF_JUMP && Form.Rows[0][28].Effect[0] == EFF_JUMP);
	Result = Run(Form, "copy_bars", "{\"from_bar\": 1, \"at_bar\": 9}");
	CHECK(!Result.IsError && Form.Rows[5][128].Pitch() == Form.Rows[5][0].Pitch() && Form.Rows[5][128].Instrument == 0);
	CHECK(Form.Rows[5][128].Volume == 10 && HasEffectAt(Form, 5, 140, EFF_VIBRATO, 0x42));
	CHECK(Form.Rows[3][128].HasNote());
	{
		// Bars that start silent: the copy ends the note playing where it goes
		// (keeping a release that was there); bars a note rings into: struck again
		FakeSong Hang(4);
		Hang.Info.DirectChanges = true;
		Hang.Put(5, 4, "C-4", 0);			// bar 1 starts silent
		Hang.End(5, 12);
		Hang.Put(5, 28, "G-4", 0);		// bar 2's note rings into bar 3
		Hang.Put(5, 40, "A-4");
		Hang.Put(5, 60, "B-4");			// rings into bar 5
		Hang.End(5, 64, NOTE_RELEASE);
		CHECK(!Run(Hang, "copy_bars", "{\"from_bar\": 1, \"at_bar\": 5}").IsError);
		CHECK(Hang.Rows[5][64].Note == NOTE_RELEASE && Hang.Rows[5][68].Pitch() == Hang.Rows[5][4].Pitch());
		CHECK(!Run(Hang, "copy_bars", "{\"from_bar\": 1, \"at_bar\": 7}").IsError);
		CHECK(Hang.Rows[5][96].Note == NOTE_HALT);
		CHECK(!Run(Hang, "copy_bars", "{\"from_bar\": 3, \"at_bar\": 8}").IsError);
		CHECK(Hang.Rows[5][112].Pitch() == Hang.Rows[5][28].Pitch() && Hang.Rows[5][112].Instrument == 0);
	}
	CHECK_HAS(Run(Form, "copy_bars", "{\"from_bar\": 1, \"to_bar\": 2, \"at_bar\": 2}").Text, "land on the bars it copies");
	CHECK_HAS(Run(Form, "copy_bars", "{\"from_bar\": 1}").Text, "at_bar");
	CHECK_HAS(Run(Form, "copy_bars", "{\"from_bar\": 4, \"at_bar\": 12, \"channels\": [\"Pulse 1\"]}").Text, "no notes");

	// Varying a part
	FakeSong Tune;
	Tune.Info.DirectChanges = true;
	const char *const MELODY[] = { "C-4", "E-4", "G-4", "E-4", "C-4", "D-4", "E-4", "G-4" };
	for (int i = 0; i < 8; ++i)
		Tune.Put(5, i * 4, MELODY[i], i == 0 ? 0 : NO_INSTRUMENT);
	Tune.End(5, 32, NOTE_RELEASE);
	Result = Run(Tune, "vary_part", "{\"channel\": \"VRC6 Pulse 1\", \"variation\": \"embellish\", \"from_bar\": 1, \"to_bar\": 2}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "VRC6 Pulse 1 passing and neighbour notes added, bars 1-2");
	CHECK_EQ(Notes(Tune, 5, 0, 8), std::string("0:C-4 2:D-4 4:E-4 6:F-4 "));
	Result = Run(Tune, "vary_part", "{\"channel\": \"VRC6 Pulse 1\", \"variation\": \"octave_up\", \"from_bar\": 2, \"to_bar\": 2}");
	CHECK_EQ(Notes(Tune, 5, 16, 32), std::string("16:C-5 20:D-5 24:E-5 26:F-5 28:G-5 "));	// with the passing note embellish added
	Result = Run(Tune, "vary_part", "{\"channel\": \"VRC6 Pulse 1\", \"variation\": \"ending\", \"from_bar\": 2, \"to_bar\": 2}");
	CHECK_HAS(Notes(Tune, 5, 28, 34), "28:C-6 ");	// the nearest tonic to G-5
	CHECK_HAS(Run(Tune, "vary_part", "{\"channel\": \"VRC6 Pulse 1\", \"variation\": \"jazzier\", \"from_bar\": 1}").Text, "variation: embellish");
	CHECK_HAS(Run(Tune, "vary_part", "{\"channel\": \"Noise\", \"variation\": \"simplify\", \"from_bar\": 1}").Text, "melodic parts");
	FakeSong Busy;
	Busy.Info.DirectChanges = true;
	for (int i = 0; i < 8; ++i)
		Busy.Put(5, i * 2, i % 2 == 0 ? "C-4" : "D-4", i == 0 ? 0 : NO_INSTRUMENT);
	Busy.End(5, 16, NOTE_RELEASE);
	CHECK(!Run(Busy, "vary_part", "{\"channel\": 6, \"variation\": \"simplify\", \"from_bar\": 1, \"to_bar\": 1}").IsError);
	CHECK_EQ(Notes(Busy, 5, 0, 16), std::string("0:C-4 4:C-4 8:C-4 12:C-4 "));
	FakeSong Held;
	Held.Info.DirectChanges = true;
	Held.Put(5, 0, "C-4", 0);
	Held.Put(5, 8, "E-4");
	Held.End(5, 16, NOTE_RELEASE);
	CHECK(!Run(Held, "vary_part", "{\"channel\": 6, \"variation\": \"syncopate\", \"from_bar\": 1, \"to_bar\": 1}").IsError);
	CHECK_EQ(Notes(Held, 5, 0, 17), std::string("0:C-4 6:E-4 16:=== "));

	// The soundtrack's notes: none, then added to, read back, and named in get_song
	FakeSong Game;
	CHECK_HAS(Run(Game, "soundtrack_notes", "{}").Text, "There are no soundtrack notes yet");
	CHECK_LACKS(Run(Game, "get_song", "{}").Text, "Soundtrack notes");
	CHECK_HAS(Run(Game, "soundtrack_notes", "{\"action\": \"add\", \"text\": \"likes bright leads with vibrato\"}").Text, "Added to the soundtrack notes");
	Run(Game, "soundtrack_notes", "{\"action\": \"add\", \"text\": \"the forest is in D dorian at 120 BPM\"}");
	Result = Run(Game, "soundtrack_notes", "{\"action\": \"read\"}");
	CHECK_HAS(Result.Text, "- likes bright leads with vibrato\r\n- the forest is in D dorian at 120 BPM");
	CHECK_HAS(Run(Game, "get_song", "{}").Text, "Soundtrack notes: \"Soundtrack notes.txt\" next to the song");
	CHECK(!Run(Game, "soundtrack_notes", "{\"action\": \"replace\", \"text\": \"start over\"}").IsError);
	CHECK_EQ(Game.Files["Soundtrack notes.txt"], std::string("start over\r\n"));
	CHECK_HAS(Run(Game, "soundtrack_notes", "{\"action\": \"shout\", \"text\": \"x\"}").Text, "read, add or replace");
}

void TestEchoAndVibrato()
{
	FakeSong Song;
	Song.Info.DirectChanges = true;
	Song.Put(5, 0, "C-4", 0, 12);
	Song.Put(5, 8, "E-4");
	Song.End(5, 16, NOTE_RELEASE);
	ToolResult Result = Run(Song, "add_echo", "{\"channel\": \"VRC6 Pulse 1\", \"to_bar\": 1}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "an echo of VRC6 Pulse 1 on VRC6 Pulse 2, 0.75 beats later at volume 6");
	CHECK(Song.Rows[6][3].Pitch() == 48 && Song.Rows[6][3].Instrument == 0 && Song.Rows[6][3].Volume == 5);	// 40% of 12
	CHECK(Song.Rows[6][11].Pitch() == 52);
	CHECK(Song.Rows[6][19].Note == NOTE_RELEASE);
	CHECK_HAS(Run(Song, "add_echo", "{\"channel\": 1}").Text, "has no notes");
	{
		// Detuned a step: sharp from its first note, in tune again where it ends, so what follows plays as before
		FakeSong Wide;
		Wide.Info.DirectChanges = true;
		Wide.Put(5, 0, "C-4", 0, 12);
		Wide.Put(5, 8, "E-4");
		Wide.End(5, 16, NOTE_RELEASE);
		Wide.Put(6, 32, "G-4", 0);
		Result = Run(Wide, "add_echo", "{\"channel\": \"VRC6 Pulse 1\", \"to_channel\": \"VRC6 Pulse 2\", \"to_bar\": 1, "
			"\"delay_beats\": 1, \"detune\": 1}");
		CHECK(!Result.IsError);
		CHECK_HAS(Result.Text, "later at volume 6 detuned P81, bar 1");
		CHECK(Wide.Rows[6][4].Pitch() == 48 && Wide.Rows[6][4].Effect[0] == EFF_PITCH && Wide.Rows[6][4].Param[0] == 0x81);
		CHECK(Wide.Rows[6][12].Pitch() == 52 && Wide.Rows[6][12].Effect[0] == EFF_NONE);
		CHECK(Wide.Rows[6][20].Note == NOTE_RELEASE && Wide.Rows[6][20].Effect[0] == EFF_PITCH && Wide.Rows[6][20].Param[0] == 0x80);
		CHECK(Wide.Rows[6][32].Pitch() == 55 && Wide.Rows[6][32].Effect[0] == EFF_NONE);
		CHECK_HAS(Run(Wide, "add_echo", "{\"channel\": \"VRC6 Pulse 1\", \"detune\": 9}").Text, "detune");
		// A detune still on where a part is written goes off there
		std::vector<Cell> Old(32);
		Old[0].SetPitch(48);
		Old[0].Effect[0] = EFF_PITCH;
		Old[0].Param[0] = 0x7E;
		std::vector<PartNote> One(1);
		One[0].Row = 8;
		One[0].Rows = 4;
		One[0].Pitch = 50;
		std::vector<Cell> Out;
		std::vector<std::string> Warnings;
		BuildPart(Old, 8, 16, One, PartOptions(), Out, Warnings);
		CHECK(Out[0].Effect[0] == EFF_PITCH && Out[0].Param[0] == 0x80);
		CHECK(StateAt(Old, 4, 1).Sticky[EFF_PITCH] == 0x7E && ChannelState().Sticky[EFF_PITCH] == 0x80);
	}
	{
		// At the song's end the echo stops: the song doesn't grow (which would move its loop)
		FakeSong Tail;
		Tail.Info.DirectChanges = true;
		Tail.Put(5, 120, "G-4", 0);
		Tail.Put(5, 124, "A-4");
		Tail.Put(5, 126, "B-4");	// starts too late for an echo before the end
		CHECK(!Run(Tail, "add_echo", "{\"channel\": \"VRC6 Pulse 1\"}").IsError);
		CHECK_EQ(Tail.Rows[6].size(), static_cast<size_t>(128));
		CHECK_EQ(Notes(Tail, 6), std::string("123:G-4 127:A-4 "));
		CHECK_HAS(Run(Tail, "add_echo", "{\"channel\": \"VRC6 Pulse 1\", \"to_channel\": \"VRC6 Pulse 2\", \"from_bar\": 8}").Text,
			"The echo is already there");
	}
	{
		// An echo note that would grate a semitone against the melody is left
		// out, or cut short where the melody moves against it
		FakeSong Rub;
		Rub.Info.DirectChanges = true;
		Rub.Put(5, 0, "C-4", 0);
		Rub.Put(5, 3, "B-3");		// the echo's C-4 would start against it: left out
		Rub.Put(5, 8, "E-4");
		Rub.Put(5, 13, "F-4");		// the echo's E-4 (from row 11) runs into it: cut short at row 13
		Rub.End(5, 20, NOTE_RELEASE);
		const ToolResult R = Run(Rub, "add_echo", "{\"channel\": \"VRC6 Pulse 1\", \"to_bar\": 1}");
		CHECK(!R.IsError);
		CHECK_HAS(R.Text, "Where the echo would grate a semitone against another part, 1 echo note was left out and 1 was cut short.");
		CHECK(!Rub.Rows[6][3].HasNote() && Rub.Rows[6][6].Pitch() == Rub.Rows[5][3].Pitch() && Rub.Rows[6][11].Pitch() == 52);
		CHECK(Rub.Rows[6][13].IsEnd() && Rub.Rows[6][16].Pitch() == 53);
	}
	{
		// An echo on another chip plays the melody's sound there, when it's a preset
		FakeSong Chips;
		Chips.Info.DirectChanges = true;
		CHECK(!Run(Chips, "write_notes", "{\"channel\": \"VRC6 Pulse 1\", \"instrument\": \"singing lead\", \"notes\": [{\"bar\": 1, "
			"\"pitch\": \"C-4\", \"beats\": 2}, {\"bar\": 1, \"beat\": 3, \"pitch\": \"E-4\", \"beats\": 2}]}").IsError);
		Chips.Put(6, 0, "G-3", 0);	// VRC6 Pulse 2 plays already
		ToolResult R = Run(Chips, "add_echo", "{\"channel\": \"VRC6 Pulse 1\", \"to_bar\": 1}");
		CHECK(!R.IsError);
		CHECK_HAS(R.Text, "an echo of VRC6 Pulse 1 on 2A03 Pulse 1");
		CHECK_HAS(R.Text, "(playing the melody's sound on the 2A03: \"Singing lead\" (");
		const InstrumentInfo *pEcho = 0;
		for (size_t i = 0; i < Chips.Info.Instruments.size(); ++i)
			if (Chips.Info.Instruments[i].Index == Chips.Rows[0][3].Instrument)
				pEcho = &Chips.Info.Instruments[i];
		CHECK(pEcho != 0 && pEcho->Chip == "2A03" && pEcho->Name == "Singing lead" && Chips.Rows[0][3].Pitch() == 48);
		// A sound of the song's own can't be found on another chip
		FakeSong Own;
		Own.Info.DirectChanges = true;
		Own.Put(5, 0, "C-4", 0);
		Own.Put(6, 0, "G-3", 0);
		CHECK_HAS(Run(Own, "add_echo", "{\"channel\": \"VRC6 Pulse 1\", \"to_bar\": 1}").Text,
			"2A03 Pulse 1 needs 2A03 instruments; give the echo an instrument");
	}

	Song.Put(0, 0, "C-4", 2);
	Song.Put(0, 2, "D-4");
	Song.End(0, 4);
	Song.Put(0, 16, "E-4");
	Song.End(0, 32);
	Result = Run(Song, "add_vibrato", "{\"channel\": 1, \"speed\": \"slow\", \"depth\": \"strong\"}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "vibrato 437");
	CHECK(HasEffectAt(Song, 0, 17, EFF_VIBRATO, 0x37));	// a quarter beat in
	CHECK(HasEffectAt(Song, 0, 31, EFF_VIBRATO, 0x00));	// off on its last row
	CHECK(!HasEffectAt(Song, 0, 1, EFF_VIBRATO, 0x37));	// too short
	CHECK_HAS(Result.Text, "(1 note)");
	Result = Run(Song, "add_vibrato", "{\"channel\": 1, \"depth\": \"none\"}");
	CHECK(!Result.IsError);
	CHECK(!HasEffectAt(Song, 0, 17, EFF_VIBRATO, 0x37));
	CHECK(Run(Song, "add_vibrato", "{\"channel\": \"noise\"}").IsError);
}

// change_mood: every step on one working copy, so they arrive together
void TestMood()
{
	FakeSong Song;
	const char *const MELODY[] = { "C-4", "D-4", "E-4", "F-4", "G-4", "A-4", "B-4", "C-5" };
	for (int i = 0; i < 8; ++i)
		Song.Put(5, i * 4, MELODY[i], i == 0 ? 0 : NO_INSTRUMENT);
	Song.End(5, 32, NOTE_RELEASE);
	Song.Put(2, 0, "C-3", 1);
	Song.End(2, 32);
	const std::vector<std::vector<Cell> > Before = Song.Rows;
	const std::string Underwater = "{\"mood\": \"underwater\", \"key\": \"C major\", \"to_mode\": \"minor\", "
		"\"moves\": [{\"channel\": \"Triangle\", \"semitones\": -12}], "
		"\"sounds\": [{\"channel\": \"VRC6 Pulse 1\", \"instrument\": \"warm pad\"}], "
		"\"vibrato\": {\"channel\": \"VRC6 Pulse 1\", \"speed\": \"slow\", \"depth\": \"strong\"}, "
		"\"echo\": {\"channel\": \"VRC6 Pulse 1\"}, \"tempo_percent\": 80, \"to_bar\": 2}";

	// As a clip (the default): one clip with all of it, the song as it was
	ToolResult Result = Run(Song, "change_mood", Underwater);
	CHECK(!Result.IsError);
	CHECK(Song.Rows == Before);
	CHECK(Song.Info.Tempo == 150 && Song.TempoUndo.empty());
	CHECK_EQ(Song.Clips.size(), static_cast<size_t>(1));
	CHECK_EQ(Song.Clips[0].Name, std::string("Claude: underwater, bars 1-2"));
	CHECK_HAS(Result.Text, "from C major to C minor (E to Eb, A to Ab, B to Bb), 2A03 Triangle down 1 octave, VRC6 Pulse 1 played by "
		"\"Warm pad\" (03), vibrato 437 on VRC6 Pulse 1 and an echo of VRC6 Pulse 1 on VRC6 Pulse 2, bars 1-2");
	CHECK_HAS(Result.Text, "dragging the card to 2A03 Triangle at bar 1 beat 1");
	CHECK_HAS(Result.Text, "Meant to play at 120 BPM: suggest the user sets speed 6 and tempo 120");
	const std::vector<std::vector<Cell> > &Clip = Song.Clips[0].Columns;
	CHECK_EQ(Clip.size(), static_cast<size_t>(5));	// Triangle to VRC6 Pulse 2
	CHECK(Song.Clips[0].Sources[0] == 2 && Song.Clips[0].Sources[4] == 6);
	CHECK(Clip[0][0].Pitch() == 24);	// the bass an octave lower
	CHECK(Clip[3][8].Pitch() == 51 && Clip[3][8].Instrument == 3);	// E-4 became D#4, played by the pad
	CHECK(Clip[3][1].Effect[0] == EFF_VIBRATO && Clip[3][1].Param[0] == 0x37);
	CHECK(Clip[1][0].IsEmpty() && Clip[2][0].IsEmpty());	// noise and DPCM come along as they are
	CHECK(Clip[4][3].Pitch() == 48 && Clip[4][3].Instrument == 3 && Clip[4][3].Volume == 6);	// the echo of the changed part
	CHECK(Clip[4][11].Pitch() == 51);
	CHECK(Clip[4][4].Effect[0] == EFF_VIBRATO && Clip[4][4].Param[0] == 0x37);
	CHECK(Clip[4].size() == 36 && Clip[4][35].Note == NOTE_RELEASE);

	// Directly: one undoable change for the notes, another for the tempo
	Song.Info.DirectChanges = true;
	Result = Run(Song, "change_mood", Underwater);
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "Changed the song: from C major to C minor");
	CHECK_HAS(Result.Text, "The tempo is now 120 BPM (speed 6, tempo 120), its own Ctrl+Z step.");
	CHECK_EQ(Song.Writes, 1);
	CHECK(Song.Info.Tempo == 120 && Song.TempoUndo.size() == 1);
	CHECK_EQ(Notes(Song, 5, 0, 33), std::string("0:C-4 4:D-4 8:D#4 12:F-4 16:G-4 20:G#4 24:A#4 28:C-5 32:=== "));
	CHECK(Song.Rows[2][0].Pitch() == 24 && Song.Rows[6][3].Pitch() == 48 && Song.Rows[6][35].Note == NOTE_RELEASE);
	CHECK_EQ(Song.Clips.size(), static_cast<size_t>(1));
	CHECK(Run(Song, "undo", "{}").Text == "Undid it.");
	CHECK(Song.Rows == Before);	// one Ctrl+Z takes all of the notes back

	// Mistakes change nothing, not even the instruments
	const size_t Instruments = Song.Info.Instruments.size();
	Result = Run(Song, "change_mood", "{\"sounds\": [{\"channel\": 6, \"instrument\": \"bell\"}], \"moves\": [{\"channel\": \"noise\", \"semitones\": 1}]}");
	CHECK(Result.IsError);
	CHECK_HAS(Result.Text, "2A03 Noise has no notes to move up or down");
	CHECK_EQ(Song.Info.Instruments.size(), Instruments);
	CHECK_HAS(Run(Song, "change_mood", "{\"moves\": [{\"channel\": 3}]}").Text, "Each move needs a channel and semitones");
	CHECK_HAS(Run(Song, "change_mood", "{\"sounds\": {\"channel\": 6}}").Text, "sounds should be a list");
	CHECK_HAS(Run(Song, "change_mood", "{\"vibrato\": [1]}").Text, "vibrato should be like");
	CHECK_HAS(Run(Song, "change_mood", "{\"key\": \"C minor\", \"to_mode\": \"minor\"}").Text, "already in C minor");
	Result = Run(Song, "change_mood", "{\"key\": \"C major\", \"to_mode\": \"mixolydian\", \"to_bar\": 1}");
	CHECK(Result.IsError);	// C major and C mixolydian differ in B, which bar 1 doesn't play
	CHECK_HAS(Result.Text, "Nothing changed in bar 1");
	CHECK_HAS(Result.Text, "The notes stayed as they were in C mixolydian");
	CHECK_HAS(Run(Song, "change_mood", "{}").Text, "Nothing changed in bars 1-8");
	CHECK(Song.Rows == Before && Song.Writes == 1);

	// An echo of nothing is a remark, the rest still happens
	Result = Run(Song, "change_mood", "{\"moves\": [{\"channel\": \"VRC6 Pulse 1\", \"semitones\": 12}], \"echo\": {\"channel\": \"Pulse 2\"}, "
		"\"to_bar\": 2}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "VRC6 Pulse 1 up 1 octave");
	CHECK_HAS(Result.Text, "2A03 Pulse 2 has no notes there to echo");
	CHECK(Song.Rows[5][0].Pitch() == 60);

	// The tempo alone, as a clip: a suggestion
	FakeSong Plain;
	Result = Run(Plain, "change_mood", "{\"bpm\": 120}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "suggest the user sets speed 6 and tempo 120");
	CHECK(Plain.Clips.empty() && Plain.Info.Tempo == 150);
}

void TestCheck()
{
	FakeSong Song;
	Song.Put(5, 0, "C-4", 0);
	Song.Put(5, 4, "E-4");
	Song.Put(5, 8, "G-4");
	Song.Put(5, 12, "C-5");
	Song.Put(5, 16, "F#4");	// outside C major
	Song.End(5, 20);
	Song.Put(6, 0, "C#4", 0);	// a semitone against C-4 for a beat
	Song.End(6, 4);
	Song.Put(0, 48, "G-0", 2);	// below a 2A03 pulse's lowest
	Song.End(0, 52);
	const ToolResult Result = Run(Song, "check_song", "{\"key\": \"C major\"}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "Outside C major: F#4 at VRC6 Pulse 1 bar 2 beat 1");
	CHECK_HAS(Result.Text, "Clash: C-4 on VRC6 Pulse 1 against C#4 on VRC6 Pulse 2 at bar 1 beat 1 for 1 beat");
	CHECK_HAS(Result.Text, "Range: G-0 at 2A03 Pulse 1 bar 4 beat 1 is below A-0");
	CHECK_HAS(Result.Text, "Silent: nothing plays in bar 3");
	CHECK_HAS(Result.Text, "Unused channels");
	CHECK_HAS(Result.Text, "2A03 Pulse 2");

	// The triangle sounds an octave under its written note: written a semitone over another part, it's a major
	// seventh under it; written a major seventh over, it's a semitone under
	FakeSong Low;
	Low.Put(2, 0, "C-4", 1);		// sounds as C-3
	Low.End(2, 8);
	Low.Put(5, 0, "B-3", 0);		// a major seventh over C-3
	Low.Put(5, 4, "C#3");		// a semitone over it, for a beat
	Low.End(5, 8);
	const ToolResult Octave = Run(Low, "check_song", "{\"key\": \"C major\", \"to_bar\": 1}");
	CHECK_HAS(Octave.Text, "Clash: C-4 on 2A03 Triangle against C#3 on VRC6 Pulse 1 at bar 1 beat 2 for 1 beat");
	CHECK_HAS(Octave.Text, ", 1 clashes,");

	// A scoop counts where it lands, and a fall as a note ends is no pitch to clash with
	FakeSong Slides;
	Slides.Put(5, 0, "D#4", 0);	// polish's scoop into E-4
	Slides.Rows[5][0].Effect[0] = EFF_SLIDE_UP;
	Slides.Rows[5][0].Param[0] = 0x61;
	Slides.Put(5, 8, "G-4");
	Slides.Rows[5][14].Effect[0] = EFF_SLIDE_DOWN;	// and its fall as G-4 ends
	Slides.Rows[5][14].Param[0] = 0x42;
	Slides.End(5, 16);
	Slides.Put(6, 0, "E-4", 0);
	Slides.Put(6, 14, "F#4");
	Slides.End(6, 16);
	const ToolResult Landed = Run(Slides, "check_song", "{\"key\": \"G major\", \"to_bar\": 1}");
	CHECK(!Landed.IsError);
	CHECK_HAS(Landed.Text, ": 0 outside the key, 0 clashes,");
	// A slide that goes on to hold its new note is that note
	Slides.Rows[5][14] = Cell();
	Slides.Rows[5][16] = Cell();
	Slides.End(5, 24);
	Slides.Rows[5][12].Effect[0] = EFF_SLIDE_DOWN;
	Slides.Rows[5][12].Param[0] = 0x41;	// G-4 down to F#4, held
	Slides.Rows[6][14] = Cell();
	Slides.Put(6, 12, "G-4");	// against that F#4 for a beat
	CHECK_HAS(Run(Slides, "check_song", "{\"key\": \"G major\", \"to_bar\": 2}").Text,
		"Clash: F#4 on VRC6 Pulse 1 against G-4 on VRC6 Pulse 2 at bar 1 beat 4 for 1 beat");
}

void TestNameSong()
{
	FakeSong Song;
	CHECK_HAS(Run(Song, "name_song", "{\"title\": \"Skylight\"}").Text, "hasn't allowed direct changes");
	Song.Info.DirectChanges = true;
	ToolResult Result = Run(Song, "name_song", "{\"title\": \" Skylight \", \"author\": \"Ada\", \"track\": \"Title screen\"}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "Set the title \"Skylight\", the author \"Ada\" and track 1's name \"Title screen\" (export_for_game names");
	CHECK(Song.Info.Title == "Skylight" && Song.Info.Author == "Ada" && Song.Info.TrackName == "Title screen" && Song.Info.Copyright.empty());
	// What isn't given stays
	CHECK(!Run(Song, "name_song", "{\"copyright\": \"2026 Ada\"}").IsError);
	CHECK(Song.Info.Title == "Skylight" && Song.Info.Copyright == "2026 Ada");
	// FamiTracker keeps 31 characters (é is one)
	CHECK_HAS(Run(Song, "name_song", "{\"title\": \"A title much longer than FamiTracker keeps\"}").Text, "title: at most 31 characters");
	CHECK(!Run(Song, "name_song", "{\"title\": \"\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\"}").IsError);
	CHECK_HAS(Run(Song, "name_song", "{}").Text, "Give the names to set");
	CHECK_HAS(Run(Song, "name_song", "{\"track\": \"  \"}").Text, "can't be empty");
}

void TestPlayback()
{
	FakeSong Song;
	CHECK(!Run(Song, "play", "{\"from_bar\": 2, \"bars\": 2}").IsError);
	CHECK(Song.PlayFirst == 16 && Song.PlayRows == 32);
	CHECK(!Run(Song, "play", "{}").IsError);
	CHECK(Song.PlayFirst == 0 && Song.PlayRows == 0 && !Song.PlayLoop && Song.PlayMuted.empty());
	CHECK_LACKS(Run(Song, "play", "{}").Text, "muted");
	// Listening closely: bars over and over, a part alone or the rest without it
	std::string Text = Run(Song, "play", "{\"from_bar\": 5, \"bars\": 2, \"loop\": true}").Text;
	CHECK_HAS(Text, "Playing bars 5-6 for the user over and over, until you call stop or the user stops it.");
	CHECK(Song.PlayFirst == 64 && Song.PlayRows == 32 && Song.PlayLoop);
	CHECK(!Run(Song, "play", "{\"from_bar\": 3, \"loop\": true}").IsError && Song.PlayFirst == 32 && Song.PlayRows == 96);
	Text = Run(Song, "play", "{\"channels\": [\"Triangle\", \"Noise\"]}").Text;
	CHECK_HAS(Text, "Only 2A03 Triangle and 2A03 Noise play; the user's own mutes come back when it stops.");
	CHECK(Song.PlayMuted.size() == 8u && Song.PlayMuted[0] && !Song.PlayMuted[2] && !Song.PlayMuted[3] && Song.PlayMuted[7]);
	Text = Run(Song, "play", "{\"mute\": \"VRC6 Pulse 1\"}").Text;
	CHECK(Song.PlayMuted.size() == 8u && Song.PlayMuted[5] && !Song.PlayMuted[0]);
	CHECK_HAS(Text, "Only 2A03 Pulse 1, 2A03 Pulse 2, 2A03 Triangle, 2A03 Noise, 2A03 DPCM, VRC6 Pulse 2 and VRC6 Sawtooth play");
	CHECK_HAS(Run(Song, "play", "{\"channels\": \"Noise\", \"mute\": \"Noise\"}").Text, "That leaves no channel playing.");
	CHECK_LACKS(Run(Song, "get_song", "{}").Text, "muted");
	// Channels the user muted: Claude knows they aren't heard
	Song.Info.Channels[1].Muted = true;
	Song.Info.Channels[7].Muted = true;
	CHECK_HAS(Run(Song, "play", "{}").Text, "The user has muted 2A03 Pulse 2 and VRC6 Sawtooth in FamiTracker, so that doesn't play;");
	CHECK_HAS(Run(Song, "get_song", "{}").Text, "\n  2 2A03 Pulse 2 (muted, the user doesn't hear it): ");
	CHECK_HAS(Run(Song, "get_song", "{}").Text, "\n  1 2A03 Pulse 1: ");
	Song.Info.Channels[1].Muted = Song.Info.Channels[7].Muted = false;
	CHECK(!Run(Song, "stop", "{}").IsError && Song.Stopped);
	CHECK_HAS(Run(Song, "undo", "{}").Text, "hasn't allowed direct changes");
	CHECK_HAS(Run(Song, "save_song", "{\"copy_name\": \"Darker\"}").Text, "Saved Darker.ftm");
	CHECK(Run(Song, "save_song", "{\"copy_name\": \"..\\\\x\"}").IsError);
	CHECK(Run(Song, "nonsense", "{}").IsError);
}

// The protocol

HttpRequest Post(const std::string &Body, const std::string &Token = "secret")
{
	HttpRequest Request;
	Request.Method = "POST";
	Request.Target = "/mcp";
	Request.Version = "HTTP/1.1";
	Request.Headers.push_back(std::make_pair(std::string("Host"), std::string("127.0.0.1:46600")));
	Request.Headers.push_back(std::make_pair(std::string("Content-Type"), std::string("application/json")));
	Request.Headers.push_back(std::make_pair(std::string("Accept"), std::string("application/json, text/event-stream")));
	if (!Token.empty())
		Request.Headers.push_back(std::make_pair(std::string("Authorization"), "Bearer " + Token));
	Request.Body = Body;
	return Request;
}

HttpRequest ModernPost(const std::string &Method, const std::string &Params, const std::string &Name = std::string(),
	const std::string &Version = MCP_MODERN_VERSION)
{
	HttpRequest Request = Post("{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"" + Method + "\",\"params\":{" + Params +
		(Params.empty() ? "" : ",") + "\"_meta\":{\"io.modelcontextprotocol/protocolVersion\":\"" + Version + "\","
		"\"io.modelcontextprotocol/clientCapabilities\":{}}}}");
	Request.Headers.push_back(std::make_pair(std::string("MCP-Protocol-Version"), Version));
	Request.Headers.push_back(std::make_pair(std::string("Mcp-Method"), Method));
	if (!Name.empty())
		Request.Headers.push_back(std::make_pair(std::string("Mcp-Name"), Name));
	return Request;
}

Json Body(const HttpResponse &Response)
{
	return Parse(Response.Body);
}

int ErrorCode(const HttpResponse &Response)
{
	const Json Message = Body(Response);
	const Json *pError = Message.Get("error");
	const Json *pCode = pError != 0 ? pError->Get("code") : 0;
	return pCode != 0 ? static_cast<int>(pCode->Integer()) : 0;
}

void TestProtocol()
{
	FakeSong Song;
	Song.Put(5, 0, "C-4", 0);
	ToolSet Tools(Song);
	McpConfig Config;
	Config.Port = 46600;
	Config.Token = "secret";
	Config.Version = "0.4.6-bf";

	// The handshake era: initialize, initialized, tools
	HttpResponse Response = HandleMcp(Post("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{"
		"\"protocolVersion\":\"2025-06-18\",\"capabilities\":{},\"clientInfo\":{\"name\":\"claude-code\",\"version\":\"2\"}}}"),
		Config, Tools);
	CHECK_EQ(Response.Status, 200);
	CHECK_EQ(Response.ContentType, std::string("application/json"));
	Json Message = Body(Response);
	CHECK(Message.Get("id") != 0 && Message.Get("id")->Integer() == 1);
	const Json *pResult = Message.Get("result");
	CHECK(pResult != 0 && pResult->Get("protocolVersion")->String() == "2025-06-18");
	CHECK(pResult->Get("serverInfo")->Get("name")->String() == "famitracker");
	CHECK(pResult->Get("capabilities")->Get("tools") != 0);
	CHECK_HAS(pResult->Get("instructions")->String(), "C-3 is middle C");
	// An unknown version gets ours
	Response = HandleMcp(Post("{\"jsonrpc\":\"2.0\",\"id\":\"a\",\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2024-01-01\"}}"),
		Config, Tools);
	CHECK(Body(Response).Get("result")->Get("protocolVersion")->String() == MCP_LEGACY_VERSION);
	CHECK(Body(Response).Get("id")->String() == "a");

	Response = HandleMcp(Post("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}"), Config, Tools);
	CHECK_EQ(Response.Status, 202);
	CHECK(Response.Body.empty());

	HttpRequest List = Post("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\"}");
	List.Headers.push_back(std::make_pair(std::string("MCP-Protocol-Version"), std::string("2025-06-18")));
	Response = HandleMcp(List, Config, Tools);
	Message = Body(Response);
	const Json *pTools = Message.Get("result")->Get("tools");
	CHECK(pTools != 0 && pTools->Size() == Tools.List().size() && pTools->Size() >= 15);
	CHECK(pTools->At(0).Get("name")->String() == "get_song");
	CHECK(pTools->At(0).Get("inputSchema")->Get("type")->String() == "object");
	CHECK(pTools->At(0).Get("annotations")->Get("readOnlyHint")->Bool());
	CHECK(Message.Get("result")->Get("resultType") == 0);	// not in the old shape
	// Every tool's schema is an object with properties, descriptions and no surprises
	for (size_t i = 0; i < pTools->Size(); ++i) {
		const Json &Tool = pTools->At(i);
		CHECK(!Tool.Get("description")->String().empty());
		const Json *pProperties = Tool.Get("inputSchema")->Get("properties");
		CHECK(pProperties != 0 && pProperties->IsObject());
		for (size_t p = 0; p < pProperties->Members().size(); ++p)
			CHECK(pProperties->Members()[p].second.Get("description") != 0 || pProperties->Members()[p].first == "notes");
	}

	Response = HandleMcp(Post("{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\",\"params\":{\"name\":\"get_song\",\"arguments\":{}}}"),
		Config, Tools);
	Message = Body(Response);
	CHECK(!Message.Get("result")->Get("isError")->Bool());
	CHECK_HAS(Message.Get("result")->Get("content")->At(0).Get("text")->String(), "VRC6 Pulse 1: 1 note");
	// A tool's own failure is a result with isError, an unknown tool a protocol error
	Response = HandleMcp(Post("{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"tools/call\",\"params\":{\"name\":\"read_notes\",\"arguments\":{}}}"),
		Config, Tools);
	CHECK(Body(Response).Get("result")->Get("isError")->Bool());
	Response = HandleMcp(Post("{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"tools/call\",\"params\":{\"name\":\"nope\"}}"), Config, Tools);
	CHECK_EQ(ErrorCode(Response), -32602);
	Response = HandleMcp(Post("{\"jsonrpc\":\"2.0\",\"id\":6,\"method\":\"resources/list\"}"), Config, Tools);
	CHECK_EQ(ErrorCode(Response), -32601);
	CHECK_EQ(Response.Status, 200);
	Response = HandleMcp(Post("{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"ping\"}"), Config, Tools);
	CHECK(Body(Response).Get("result")->IsObject());

	// Recipes as prompts: listed, and filled in with their arguments
	Response = HandleMcp(Post("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-06-18\"}}"),
		Config, Tools);
	Message = Body(Response);
	CHECK(Message.Get("result")->Get("capabilities")->Get("prompts") != 0);
	CHECK_HAS(Message.Get("result")->Get("instructions")->String(), "review_arrangement");
	Response = HandleMcp(Post("{\"jsonrpc\":\"2.0\",\"id\":8,\"method\":\"prompts/list\"}"), Config, Tools);
	const Json Listed = Body(Response);
	const Json *pPrompts = Listed.Get("result")->Get("prompts");
	CHECK(pPrompts != 0 && pPrompts->Size() == ToolSet::Prompts().size() && pPrompts->Size() >= 4);
	CHECK(pPrompts->At(0).Get("name")->String() == "compose_track" && pPrompts->At(0).Get("arguments")->At(0).Get("required")->Bool());
	Response = HandleMcp(Post("{\"jsonrpc\":\"2.0\",\"id\":9,\"method\":\"prompts/get\",\"params\":{\"name\":\"compose_track\","
		"\"arguments\":{\"idea\":\"a snowy village at night\",\"bars\":\"24\"}}}"), Config, Tools);
	Message = Body(Response);
	const Json *pMessages = Message.Get("result")->Get("messages");
	CHECK(pMessages != 0 && pMessages->Size() == 1 && pMessages->At(0).Get("role")->String() == "user");
	CHECK_HAS(pMessages->At(0).Get("content")->Get("text")->String(), "for this: a snowy village at night.");
	CHECK_HAS(pMessages->At(0).Get("content")->Get("text")->String(), "the form for 24 bars");
	CHECK_HAS(pMessages->At(0).Get("content")->Get("text")->String(), "an intro of 2-4 bars with one or two quiet parts");
	CHECK_HAS(pMessages->At(0).Get("content")->Get("text")->String(), "name a composer, a game or a reference, look it up");
	Response = HandleMcp(Post("{\"jsonrpc\":\"2.0\",\"id\":10,\"method\":\"prompts/get\",\"params\":{\"name\":\"compose_track\"}}"),
		Config, Tools);
	CHECK_EQ(ErrorCode(Response), -32602);
	Response = HandleMcp(Post("{\"jsonrpc\":\"2.0\",\"id\":11,\"method\":\"prompts/get\",\"params\":{\"name\":\"sing\"}}"), Config, Tools);
	CHECK_EQ(ErrorCode(Response), -32602);
	for (size_t i = 0; i < ToolSet::Prompts().size(); ++i) {
		std::string Text, Error;
		Json Arguments = Json::MakeObject();
		for (size_t a = 0; a < ToolSet::Prompts()[i].Arguments.size(); ++a)
			Arguments.Set(ToolSet::Prompts()[i].Arguments[a].Name, "4");
		CHECK(ToolSet::PromptText(ToolSet::Prompts()[i].Name, Arguments, Text, Error) && !Text.empty());
		// Every tool a recipe names exists
		size_t At = 0;
		while ((At = Text.find('_', At)) != std::string::npos) {
			size_t From = At, To = At;
			while (From > 0 && (std::islower(static_cast<unsigned char>(Text[From - 1])) || Text[From - 1] == '_'))
				--From;
			while (To < Text.size() && (std::islower(static_cast<unsigned char>(Text[To])) || Text[To] == '_'))
				++To;
			const std::string Word = Text.substr(From, To - From);
			const bool Argument = Word == "as_clip" || Word == "clip_name" || Word == "from_bar" || Word == "to_bar" ||
				Word == "chord_style" || Word == "bass_style";
			Check(Argument || Tools.Has(Word), ("the recipe's tool exists: " + Word).c_str(), __LINE__);
			At = To;
		}
	}

	// Per-request metadata (2026-07-28)
	Response = HandleMcp(ModernPost("server/discover", ""), Config, Tools);
	CHECK_EQ(Response.Status, 200);
	Message = Body(Response);
	CHECK(Message.Get("result")->Get("resultType")->String() == "complete");
	CHECK(Message.Get("result")->Get("supportedVersions")->At(0).String() == MCP_MODERN_VERSION);
	CHECK(Message.Get("result")->Get("_meta")->Get("io.modelcontextprotocol/serverInfo")->Get("version")->String() == "0.4.6-bf");
	Response = HandleMcp(ModernPost("tools/list", ""), Config, Tools);
	Message = Body(Response);
	CHECK(Message.Get("result")->Get("tools")->Size() == Tools.List().size());
	CHECK(Message.Get("result")->Get("ttlMs") != 0 && Message.Get("result")->Get("cacheScope")->String() == "private");
	Response = HandleMcp(ModernPost("prompts/list", ""), Config, Tools);
	CHECK(Body(Response).Get("result")->Get("prompts")->Size() == ToolSet::Prompts().size());
	CHECK(Body(Response).Get("result")->Get("resultType")->String() == "complete");
	Response = HandleMcp(ModernPost("prompts/get", "\"name\":\"deepen_track\",\"arguments\":{\"bars\":\"9-16\"}", "deepen_track"),
		Config, Tools);
	CHECK_HAS(Body(Response).Get("result")->Get("messages")->At(0).Get("content")->Get("text")->String(), "in bars 9-16");
	Response = HandleMcp(ModernPost("prompts/get", "\"name\":\"deepen_track\"", "polish"), Config, Tools);
	CHECK_EQ(Response.Status, 400);	// the header names another
	Response = HandleMcp(ModernPost("tools/call", "\"name\":\"get_song\",\"arguments\":{}", "get_song"), Config, Tools);
	Message = Body(Response);
	CHECK(Message.Get("result")->Get("resultType")->String() == "complete");
	CHECK_HAS(Message.Get("result")->Get("content")->At(0).Get("text")->String(), "Test song");
	// The name header may come in base64
	Response = HandleMcp(ModernPost("tools/call", "\"name\":\"get_song\",\"arguments\":{}", "=?base64?Z2V0X3Nvbmc=?="), Config, Tools);
	CHECK_EQ(Response.Status, 200);
	// Headers that don't match the body
	Response = HandleMcp(ModernPost("tools/call", "\"name\":\"get_song\",\"arguments\":{}", "stop"), Config, Tools);
	CHECK(Response.Status == 400 && ErrorCode(Response) == -32020);
	Response = HandleMcp(ModernPost("tools/call", "\"name\":\"get_song\",\"arguments\":{}"), Config, Tools);
	CHECK(Response.Status == 400 && ErrorCode(Response) == -32020);
	HttpRequest Mismatch = ModernPost("tools/list", "");
	Mismatch.Headers.back().second = "tools/call";
	CHECK_EQ(ErrorCode(HandleMcp(Mismatch, Config, Tools)), -32020);
	// A version we don't speak
	Response = HandleMcp(ModernPost("tools/list", "", "", "2099-01-01"), Config, Tools);
	CHECK(Response.Status == 400 && ErrorCode(Response) == -32022);
	CHECK(Body(Response).Get("error")->Get("data")->Get("supported")->Size() == 4);
	// Unknown methods: 404
	Response = HandleMcp(ModernPost("subscriptions/listen", ""), Config, Tools);
	CHECK(Response.Status == 404 && ErrorCode(Response) == -32601);
	// A modern header on a request without _meta
	HttpRequest NoMeta = Post("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\"}");
	NoMeta.Headers.push_back(std::make_pair(std::string("MCP-Protocol-Version"), std::string(MCP_MODERN_VERSION)));
	CHECK_EQ(HandleMcp(NoMeta, Config, Tools).Status, 400);
	NoMeta.Headers.back().second = "1999-01-01";
	CHECK_EQ(HandleMcp(NoMeta, Config, Tools).Status, 400);

	// Who may ask
	CHECK_EQ(HandleMcp(Post("{}", ""), Config, Tools).Status, 403);
	CHECK_EQ(HandleMcp(Post("{}", "wrong"), Config, Tools).Status, 403);
	{
		McpConfig Open = Config;	// without a token nothing is answered, not everything
		Open.Token.clear();
		CHECK_EQ(HandleMcp(Post("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}", ""), Open, Tools).Status, 403);
	}
	HttpRequest Lowercase = Post("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}", "");
	Lowercase.Headers.push_back(std::make_pair(std::string("authorization"), std::string("bearer   secret")));
	CHECK_EQ(HandleMcp(Lowercase, Config, Tools).Status, 200);
	HttpRequest Page = Post("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}");
	Page.Headers.push_back(std::make_pair(std::string("Origin"), std::string("https://evil.example")));
	CHECK_EQ(HandleMcp(Page, Config, Tools).Status, 403);
	Page.Headers.back().second = "null";
	CHECK_EQ(HandleMcp(Page, Config, Tools).Status, 403);
	Page.Headers.back().second = "http://localhost:3000";
	CHECK_EQ(HandleMcp(Page, Config, Tools).Status, 200);
	HttpRequest Rebound = Post("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}");
	Rebound.Headers[0].second = "attacker.example:46600";
	CHECK_EQ(HandleMcp(Rebound, Config, Tools).Status, 403);
	Rebound.Headers[0].second = "[::1]:46600";
	CHECK_EQ(HandleMcp(Rebound, Config, Tools).Status, 200);
	Rebound.Headers[0].second = "LOCALHOST";
	CHECK_EQ(HandleMcp(Rebound, Config, Tools).Status, 200);

	// What's malformed
	HttpRequest Get = Post("");
	Get.Method = "GET";
	Response = HandleMcp(Get, Config, Tools);
	CHECK_EQ(Response.Status, 405);
	CHECK(Response.Headers.size() == 1 && Response.Headers[0].second == "POST");
	HttpRequest Elsewhere = Post("{}");
	Elsewhere.Target = "/other";
	CHECK_EQ(HandleMcp(Elsewhere, Config, Tools).Status, 404);
	HttpRequest Text = Post("{}");
	Text.Headers[1].second = "text/plain";
	CHECK_EQ(HandleMcp(Text, Config, Tools).Status, 415);
	CHECK_EQ(ErrorCode(HandleMcp(Post("{nope"), Config, Tools)), -32700);
	CHECK_EQ(ErrorCode(HandleMcp(Post("[{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}]"), Config, Tools)), -32600);
	CHECK_EQ(ErrorCode(HandleMcp(Post("{\"jsonrpc\":\"1.0\",\"id\":1,\"method\":\"ping\"}"), Config, Tools)), -32600);
	CHECK_EQ(ErrorCode(HandleMcp(Post("{\"jsonrpc\":\"2.0\",\"id\":{},\"method\":\"ping\"}"), Config, Tools)), -32600);
	CHECK_EQ(HandleMcp(Post("{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{}}"), Config, Tools).Status, 202);

	// The command that connects Claude Code
	CHECK_EQ(ClaudeAddCommand(Config), std::string("claude mcp add --transport http --scope user famitracker "
		"http://127.0.0.1:46600/mcp --header \"Authorization: Bearer secret\""));
}

// A session as Claude Code would run it, from the handshake to a melody in the song
void TestSession()
{
	FakeSong Song;
	Song.Info.DirectChanges = true;
	ToolSet Tools(Song);
	McpConfig Config;
	Config.Port = 46600;
	Config.Token = "t";
	int Id = 0;
	auto Call = [&](const std::string &Tool, const std::string &Arguments) {
		char Number[16];
		std::snprintf(Number, sizeof(Number), "%d", ++Id);
		HttpRequest Request = Post(std::string("{\"jsonrpc\":\"2.0\",\"id\":") + Number + ",\"method\":\"tools/call\",\"params\":{\"name\":\"" +
			Tool + "\",\"arguments\":" + Arguments + "}}", "t");
		const Json Message = Body(HandleMcp(Request, Config, Tools));
		const Json *pResult = Message.Get("result");
		if (pResult == 0)
			return std::string("PROTOCOL ERROR");
		return std::string(pResult->Get("isError")->Bool() ? "ERROR: " : "") + pResult->Get("content")->At(0).Get("text")->String();
	};
	CHECK_HAS(Call("get_song", "{}"), "Key: none yet");
	CHECK_HAS(Call("write_notes", "{\"channel\":\"VRC6 Pulse 1\",\"instrument\":\"soft lead\",\"notes\":["
		"{\"bar\":1,\"beat\":1,\"pitch\":\"E-4\",\"beats\":1},{\"bar\":1,\"beat\":2,\"pitch\":\"G-4\",\"beats\":1},"
		"{\"bar\":1,\"beat\":3,\"pitch\":\"C-5\",\"beats\":2},{\"bar\":2,\"beat\":1,\"pitch\":\"A-4\",\"beats\":4}]}"), "Changed the song");
	CHECK_HAS(Call("add_chords_and_bass", "{\"melody_channel\":\"VRC6 Pulse 1\"}"), "Chords:");
	CHECK_HAS(Call("check_song", "{}"), "Checked bars 1-8");
	CHECK_HAS(Call("transpose", "{\"to_mode\":\"minor\"}"), "to C minor");
	CHECK_HAS(Call("add_vibrato", "{\"channel\":\"VRC6 Pulse 1\",\"speed\":\"slow\",\"depth\":\"strong\"}"), "vibrato 437");
	CHECK_HAS(Call("add_echo", "{\"channel\":\"VRC6 Pulse 1\"}"), "an echo of VRC6 Pulse 1 on VRC6 Pulse 2");
	CHECK_HAS(Call("set_tempo", "{\"bpm\":110}"), "Tempo set to 110 BPM");
	CHECK_HAS(Call("change_mood", "{\"mood\":\"darker\",\"to_mode\":\"phrygian\",\"moves\":[{\"channel\":\"Triangle\",\"semitones\":-12}],"
		"\"sounds\":[{\"channel\":\"VRC6 Pulse 1\",\"instrument\":\"warm pad\"}],\"tempo_percent\":85}"),
		"The tempo is now 94 BPM (speed 6, tempo 94)");
	CHECK_HAS(Call("play", "{\"from_bar\":1,\"bars\":2}"), "Playing for the user from bar 1 for 2 bars");
	CHECK_HAS(Call("read_notes", "{\"channel\":\"VRC6 Pulse 1\",\"to_bar\":2}"), "bar 1 beat 1: D#4");
}

// The song's flow: jumps, skips and halts

void TestFlow()
{
	const int PL = 16;
	auto Make = [](int Frames) { return std::vector<std::vector<Cell> >(2, std::vector<Cell>(static_cast<size_t>(Frames) * 16)); };
	auto Put = [](std::vector<std::vector<Cell> > &C, int Channel, int Row, int Column, int Effect, int Param) {
		C[Channel][Row].Effect[Column] = static_cast<unsigned char>(Effect);
		C[Channel][Row].Param[Column] = static_cast<unsigned char>(Param);
	};
	std::vector<int> Columns(2, 2);

	// Every frame, then back to the first
	std::vector<std::vector<Cell> > C = Make(3);
	SongFlow F = FollowSong(C, Columns, PL, 3);
	CHECK(F.Steps.size() == 3 && F.LoopStep == 0 && !F.Halts());
	CHECK(F.LoopRow(PL) == 0 && F.EndRow(PL) == 48 && F.PlayedRows() == 48 && F.LoopRows() == 48);

	// An intro: B01 at the end jumps back to frame 1
	Put(C, 1, 2 * PL + 15, 0, EFF_JUMP, 1);
	F = FollowSong(C, Columns, PL, 3);
	CHECK(F.Steps.size() == 3 && F.LoopStep == 1);
	CHECK(F.LoopRow(PL) == 16 && F.EndRow(PL) == 48 && F.PlayedRows() == 48 && F.LoopRows() == 32);

	// Jumping earlier in the frame: the rows after it never play
	C = Make(3);
	Put(C, 1, 2 * PL + 7, 0, EFF_JUMP, 1);
	F = FollowSong(C, Columns, PL, 3);
	CHECK(F.EndRow(PL) == 40 && F.LoopRows() == 24);
	std::vector<bool> Played = F.Played(48, PL);
	CHECK(Played[39] && !Played[40] && !Played[47] && Played[16]);

	// A skip starts the next frame later: D04 in frame 0
	Put(C, 0, 11, 0, EFF_SKIP, 4);
	F = FollowSong(C, Columns, PL, 3);
	CHECK(F.Steps.size() == 4 && F.Steps[0].EndRow == 12 && F.Steps[1].Frame == 1 && F.Steps[1].FirstRow == 4);
	// Frame 2 jumps to frame 1, which plays from its first row this time, then frame 2 again: that's the loop
	CHECK(F.Steps[3].Frame == 1 && F.Steps[3].FirstRow == 0 && F.Steps[3].EndRow == 16 && F.LoopStep == 2);
	CHECK_EQ(F.LoopRow(PL), 32);
	Played = F.Played(48, PL);
	CHECK(!Played[12] && Played[16] && Played[20]);

	// A halt ends it
	C = Make(3);
	Put(C, 0, PL + 3, 1, EFF_HALT, 0);
	F = FollowSong(C, Columns, PL, 3);
	CHECK(F.Halts() && F.LoopRow(PL) == -1 && F.EndRow(PL) == 20 && F.LoopRows() == 0 && F.PlayedRows() == 20);
	// ...but not from a column the channel doesn't show
	Columns[0] = 1;
	F = FollowSong(C, Columns, PL, 3);
	CHECK(!F.Halts() && F.EndRow(PL) == 48);
	Columns[0] = 2;

	// A jump and a skip on one row: the jump goes first; past the last frame it goes to the last one
	C = Make(3);
	Put(C, 0, 5, 0, EFF_SKIP, 2);
	Put(C, 1, 5, 0, EFF_JUMP, 9);
	F = FollowSong(C, Columns, PL, 3);
	CHECK(F.Steps.size() >= 2 && F.Steps[1].Frame == 2 && F.Steps[1].FirstRow == 0);
	// The last one read wins
	C = Make(3);
	Put(C, 0, 47, 0, EFF_JUMP, 0);
	Put(C, 1, 47, 1, EFF_JUMP, 1);
	F = FollowSong(C, Columns, PL, 3);
	CHECK(F.Steps.size() == 3 && F.LoopStep == 1);
	CHECK(FollowSong(C, Columns, PL, 0).Steps.empty());
}

// PNG

const int TEST_LENGTH_BASE[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115,
	131, 163, 195, 227, 258 };
const int TEST_LENGTH_EXTRA[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
const int TEST_DISTANCE_BASE[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537,
	2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
const int TEST_DISTANCE_EXTRA[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12,
	13, 13 };

// Inflates a zlib stream of fixed-code blocks (what ZlibCompress writes);
// false when it isn't one or its checksum is wrong
bool Inflate(const std::string &Zlib, std::string &Out)
{
	Out.clear();
	if (Zlib.size() < 7 || static_cast<unsigned char>(Zlib[0]) != 0x78 ||
		((static_cast<unsigned char>(Zlib[0]) << 8) | static_cast<unsigned char>(Zlib[1])) % 31 != 0)
		return false;
	const size_t DataEnd = Zlib.size() - 4;
	size_t Byte = 2;
	int Bit = 0;
	bool Short = false;
	auto Bits = [&](int Count) {		// lowest bit first
		long Value = 0;
		for (int i = 0; i < Count; ++i) {
			if (Byte >= DataEnd) {
				Short = true;
				return 0L;
			}
			Value |= static_cast<long>((static_cast<unsigned char>(Zlib[Byte]) >> Bit) & 1) << i;
			if (++Bit == 8) {
				Bit = 0;
				++Byte;
			}
		}
		return Value;
	};
	auto Code = [&](int Count) {		// a Huffman code, highest bit first
		long Value = 0;
		for (int i = 0; i < Count; ++i)
			Value = (Value << 1) | Bits(1);
		return Value;
	};
	for (bool Last = false; !Last; ) {
		Last = Bits(1) != 0;
		if (Bits(2) != 1 || Short)
			return false;
		for (;;) {
			long Value = Code(7), Symbol;
			if (Value <= 23)
				Symbol = 256 + Value;
			else {
				Value = (Value << 1) | Bits(1);
				if (Value >= 0x30 && Value <= 0xBF)
					Symbol = Value - 0x30;
				else if (Value >= 0xC0 && Value <= 0xC7)
					Symbol = 280 + Value - 0xC0;
				else {
					Value = (Value << 1) | Bits(1);
					if (Value < 0x190 || Value > 0x1FF)
						return false;
					Symbol = 144 + Value - 0x190;
				}
			}
			if (Short)
				return false;
			if (Symbol < 256)
				Out += static_cast<char>(Symbol);
			else if (Symbol == 256)
				break;
			else {
				const int L = static_cast<int>(Symbol - 257);
				if (L > 28)
					return false;
				const long Length = TEST_LENGTH_BASE[L] + Bits(TEST_LENGTH_EXTRA[L]);
				const long D = Code(5);
				if (D > 29)
					return false;
				const long Distance = TEST_DISTANCE_BASE[D] + Bits(TEST_DISTANCE_EXTRA[D]);
				if (Short || Distance > static_cast<long>(Out.size()))
					return false;
				for (long k = 0; k < Length; ++k)
					Out += Out[Out.size() - static_cast<size_t>(Distance)];
			}
		}
	}
	if (Bit != 0)
		++Byte;
	if (Byte != DataEnd)
		return false;
	unsigned long Stored = 0;
	for (int i = 0; i < 4; ++i)
		Stored = (Stored << 8) | static_cast<unsigned char>(Zlib[DataEnd + static_cast<size_t>(i)]);
	return Stored == Adler32(Out);
}

unsigned long BigEndian(const std::string &Data, size_t At)
{
	unsigned long Value = 0;
	for (size_t i = 0; i < 4; ++i)
		Value = (Value << 8) | static_cast<unsigned char>(Data[At + i]);
	return Value;
}

// A PNG read back: its size, palette and pixels (palette indices); false if
// it isn't one as EncodePng writes them
struct DecodedPng {
	int Width, Height;
	std::vector<Rgb> Palette;
	std::string Pixels;
};

bool DecodePng(const std::string &File, DecodedPng &Out)
{
	if (File.size() < 8 || File.compare(0, 8, std::string("\x89PNG\r\n\x1A\n", 8)) != 0)
		return false;
	std::string Data;
	bool Ended = false;
	for (size_t At = 8; At + 12 <= File.size() && !Ended; ) {
		const size_t Length = BigEndian(File, At);
		if (At + 12 + Length > File.size())
			return false;
		const std::string Type = File.substr(At + 4, 4);
		const std::string Body = File.substr(At + 8, Length);
		if (BigEndian(File, At + 8 + Length) != Crc32(Type + Body))
			return false;
		if (Type == "IHDR") {
			Out.Width = static_cast<int>(BigEndian(Body, 0));
			Out.Height = static_cast<int>(BigEndian(Body, 4));
			if (Body[8] != 8 || Body[9] != 3)
				return false;
		}
		else if (Type == "PLTE") {
			for (size_t i = 0; i + 2 < Body.size(); i += 3) {
				Rgb Color = { static_cast<unsigned char>(Body[i]), static_cast<unsigned char>(Body[i + 1]), static_cast<unsigned char>(Body[i + 2]) };
				Out.Palette.push_back(Color);
			}
		}
		else if (Type == "IDAT")
			Data += Body;
		else if (Type == "IEND")
			Ended = true;
		At += 12 + Length;
	}
	std::string Raw;
	if (!Ended || !Inflate(Data, Raw) || Raw.size() != static_cast<size_t>(Out.Height) * (static_cast<size_t>(Out.Width) + 1))
		return false;
	Out.Pixels.clear();
	for (int y = 0; y < Out.Height; ++y) {
		const size_t Row = static_cast<size_t>(y) * (static_cast<size_t>(Out.Width) + 1);
		if (Raw[Row] != 0)
			return false;
		Out.Pixels += Raw.substr(Row + 1, static_cast<size_t>(Out.Width));
	}
	return true;
}

// How many pixels have this color
int CountColor(const DecodedPng &Image, int R, int G, int B)
{
	int Count = 0;
	for (size_t i = 0; i < Image.Pixels.size(); ++i) {
		const Rgb &C = Image.Palette[static_cast<unsigned char>(Image.Pixels[i])];
		if (C.R == R && C.G == G && C.B == B)
			++Count;
	}
	return Count;
}

void TestPng()
{
	CHECK_EQ(Crc32("123456789"), 0xCBF43926ul);
	CHECK_EQ(Adler32("Wikipedia"), 0x11E60398ul);
	CHECK_EQ(Base64Encode(""), std::string());
	CHECK_EQ(Base64Encode("f"), std::string("Zg=="));
	CHECK_EQ(Base64Encode("fo"), std::string("Zm8="));
	CHECK_EQ(Base64Encode("foobar"), std::string("Zm9vYmFy"));

	// Compression that comes back the same: nothing, a little, runs, noise,
	// repeats far apart and more than the window holds
	std::vector<std::string> Samples;
	Samples.push_back(std::string());
	Samples.push_back("a");
	Samples.push_back("abcabcabcabcabcabcabcabcabcabcabcx");
	Samples.push_back(std::string(100000, 'z'));
	std::string Noise;
	unsigned long Seed = 12345;
	for (int i = 0; i < 70000; ++i) {
		Seed = Seed * 1103515245ul + 12345ul;
		Noise += static_cast<char>((Seed >> 16) & 0xFF);
	}
	Samples.push_back(Noise);
	Samples.push_back(Noise.substr(0, 3000) + std::string(500, 'q') + Noise.substr(0, 3000) + Noise.substr(10000, 40000) +
		Noise.substr(0, 3000));
	std::string AllBytes;
	for (int i = 0; i < 256; ++i)
		AllBytes += static_cast<char>(i);
	Samples.push_back(AllBytes + AllBytes + AllBytes);
	for (size_t i = 0; i < Samples.size(); ++i) {
		std::string Back;
		Check(Inflate(ZlibCompress(Samples[i]), Back) && Back == Samples[i], "compressed data comes back", __LINE__);
	}
	CHECK(ZlibCompress(std::string(100000, 'z')).size() < 1000);

	// A small image
	std::vector<Rgb> Palette;
	Rgb Black = { 0, 0, 0 }, Red = { 255, 0, 0 };
	Palette.push_back(Black);
	Palette.push_back(Red);
	std::vector<unsigned char> Pixels(12, 0);
	Pixels[5] = 1;
	DecodedPng Image;
	CHECK(DecodePng(EncodePng(4, 3, Pixels, Palette), Image));
	CHECK(Image.Width == 4 && Image.Height == 3 && Image.Palette.size() == 2 && Image.Pixels.size() == 12);
	CHECK(Image.Pixels[5] == 1 && Image.Pixels[4] == 0);
}

// The picture of the notes

std::string Environment(const char *pName)
{
#ifdef _MSC_VER
	char *pValue = 0;
	size_t Size = 0;
	if (_dupenv_s(&pValue, &Size, pName) != 0 || pValue == 0)
		return std::string();
	const std::string Value(pValue);
	std::free(pValue);
	return Value;
#else
	const char *pValue = std::getenv(pName);
	return pValue != 0 ? std::string(pValue) : std::string();
#endif
}

void TestPicture()
{
	FakeSong Song;
	// A melody on VRC6 Pulse 1, the last note quiet
	Song.Put(5, 0, "C-4", 0, 15);
	Song.Put(5, 4, "E-4");
	Song.Put(5, 8, "G-4", NO_INSTRUMENT, 6);
	Song.End(5, 16, NOTE_RELEASE);
	// Chords as an arpeggio on Pulse 2, a bass on the triangle, drums
	Song.Put(1, 0, "C-3", 2);
	Song.Rows[1][0].Effect[0] = EFF_ARPEGGIO;
	Song.Rows[1][0].Param[0] = 0x47;
	Song.End(1, 16);
	Song.Put(2, 0, "C-2", 1);
	Song.Put(2, 8, "G-1");
	Song.Put(3, 0, "kick");
	Song.Put(3, 4, "hihat");
	Song.Put(3, 8, "snare");
	// An intro: the end jumps back to the second frame (bar 5)
	Song.Rows[0][127].Effect[0] = EFF_JUMP;
	Song.Rows[0][127].Param[0] = 1;

	ToolSet Tools(Song);
	ToolResult Result = Tools.Call("look_at_song", Parse("{}"));
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "Piano roll of bars 1-8");
	CHECK_HAS(Result.Text, "blue = 2A03 Pulse 2, green = 2A03 Triangle, orange = VRC6 Pulse 1");
	CHECK_HAS(Result.Text, "DRUMS strip below is 2A03 Noise (brown)");
	CHECK_HAS(Result.Text, "arpeggio chord");
	CHECK_HAS(Result.Text, "Green line: where the song loops back to (bar 5 beat 1)");
	CHECK_HAS(Result.Text, "Nothing here on: 2A03 Pulse 1, 2A03 DPCM, VRC6 Pulse 2, VRC6 Sawtooth.");
	DecodedPng Image;
	CHECK(DecodePng(Result.Image, Image));
	CHECK(Image.Width > 1000 && Image.Width < 1200 && Image.Height > 300 && Image.Height < 800);
	CHECK(CountColor(Image, 255, 127, 14) > 100);		// the loud melody notes, orange
	CHECK(CountColor(Image, 255, 185, 122) > 10);		// the quieter one, paler (orange at 55%)
	CHECK(CountColor(Image, 31, 119, 180) > 50);		// the chord's root, blue
	CHECK(CountColor(Image, 192, 217, 234) > 50);		// its other notes, faint blue
	CHECK(CountColor(Image, 44, 160, 44) > 50);			// the bass, green
	CHECK(CountColor(Image, 140, 86, 75) > 5);			// drum hits, brown
	CHECK(CountColor(Image, 0, 155, 70) > 100);			// the loop's start
	CHECK(CountColor(Image, 210, 30, 30) > 100);		// where it jumps back
	CHECK(Result.Image.size() < 60000);
	const std::string Directory = Environment("FTAPI_PICTURES");	// to look at it
	if (!Directory.empty())
		std::ofstream(Directory + "/look_at_song.png", std::ios::binary) << Result.Image;

	// Fewer channels, other bars; in the protocol, as an image after the text
	Result = Tools.Call("look_at_song", Parse("{\"from_bar\": 5, \"to_bar\": 6, \"channels\": [\"VRC6 Pulse 1\"]}"));
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "Piano roll of bars 5-6");
	CHECK_HAS(Result.Text, "Nothing here on: VRC6 Pulse 1.");
	CHECK(DecodePng(Result.Image, Image));
	CHECK(Tools.Call("look_at_song", Parse("{\"from_bar\": 1, \"to_bar\": 40}")).IsError);
	CHECK(Tools.Call("look_at_song", Parse("{\"from_bar\": 3, \"to_bar\": 2}")).IsError);
	CHECK(Tools.Call("look_at_song", Parse("{\"channels\": \"nope\"}")).IsError);

	McpConfig Config;
	Config.Token = "t";
	HttpRequest Request = Post("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":\"look_at_song\",\"arguments\":{}}}", "t");
	const Json Message = Parse(HandleMcp(Request, Config, Tools).Body);
	const Json *pContent = Message.Get("result") != 0 ? Message.Get("result")->Get("content") : 0;
	CHECK(pContent != 0 && pContent->Size() == 2);
	if (pContent != 0 && pContent->Size() == 2) {
		CHECK(pContent->At(0).Get("type")->String() == "text");
		CHECK(pContent->At(1).Get("type")->String() == "image" && pContent->At(1).Get("mimeType")->String() == "image/png");
		CHECK_EQ(pContent->At(1).Get("data")->String().compare(0, 8, "iVBORw0K"), 0);
	}
}

// Versions as tracks of their own

int InstrumentIndex(const FakeSong &Song, const char *Name)
{
	for (size_t i = 0; i < Song.Info.Instruments.size(); ++i)
		if (Song.Info.Instruments[i].Name == Name)
			return Song.Info.Instruments[i].Index;
	return -1;
}

bool HasEffectNear(const std::vector<Cell> &Cells, int First, int End, int Effect)
{
	for (int r = First; r < End && r < static_cast<int>(Cells.size()); ++r)
		for (int k = 0; k < EFFECT_COLUMNS; ++k)
			if (Cells[static_cast<size_t>(r)].Effect[k] == Effect && Cells[static_cast<size_t>(r)].Param[k] != 0)
				return true;
	return false;
}

void TestVersions()
{
	FakeSong Song;
	// A melody with long notes, chords as an arpeggio, a bass, drums on and between the beats
	Song.Put(5, 0, "C-4", 0);
	Song.Put(5, 8, "E-4");
	Song.Put(5, 16, "G-4", NO_INSTRUMENT, 12);
	Song.Put(5, 32, "E-4");
	Song.End(5, 48, NOTE_RELEASE);
	Song.Put(0, 0, "C-3", 2);
	Song.Rows[0][0].Effect[0] = EFF_ARPEGGIO;
	Song.Rows[0][0].Param[0] = 0x47;
	Song.End(0, 64);
	Song.Put(2, 0, "C-2", 1);
	for (int Row = 0; Row < 64; Row += 2)
		Song.Put(3, Row, Row % 4 == 0 ? "kick" : "hihat", 2);
	// Speed and tempo commands at the start
	Song.Rows[3][0].Effect[0] = EFF_SPEED;
	Song.Rows[3][0].Param[0] = 6;
	Song.Rows[3][0].Effect[1] = EFF_SPEED;
	Song.Rows[3][0].Param[1] = 150;
	const std::vector<std::vector<Cell> > Day = Song.Rows;

	ToolSet Tools(Song);
	ToolResult Result = Tools.Call("make_version", Parse("{\"version\": \"night\"}"));
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "Made track 2 \"Town (night)\" from track 1 \"Town\", which stays as it was.");
	CHECK_HAS(Result.Text, "120 BPM instead of 150 (speed 6, tempo 120; 1 tempo command in the patterns changed too)");
	CHECK_HAS(Result.Text, "volumes at 75%");
	CHECK_HAS(Result.Text, "the drums a soft pulse on each bar's first beat (28 hits left out)");
	CHECK_HAS(Result.Text, "VRC6 Pulse 1 down 1 octave");
	CHECK_HAS(Result.Text, "VRC6 Pulse 1 played by \"Soft lead\"");
	CHECK_HAS(Result.Text, "2A03 Pulse 1 played by \"Warm pad\"");
	CHECK_HAS(Result.Text, "vibrato 434 on VRC6 Pulse 1");
	CHECK_HAS(Result.Text, "an echo of VRC6 Pulse 1 on VRC6 Pulse 2");
	CHECK(Song.Tracks.size() == 2 && Song.Current == 1 && Song.Info.TrackName == "Town (night)");
	CHECK(Song.Info.Speed == 6 && Song.Info.Tempo == 120);
	// The day version is as it was
	CHECK(Song.Tracks[0].Rows == Day && Song.Tracks[0].Speed == 6 && Song.Tracks[0].Tempo == 150);
	// The night version: quieter, other sounds, fewer and softer drums, slower tempo command
	const std::vector<std::vector<Cell> > &Night = Song.Rows;
	CHECK_EQ(Night[5][0].Volume, 11);			// 15 at 75%
	CHECK_EQ(Night[5][16].Volume, 9);			// 12 at 75%
	CHECK(Night[5][0].Instrument == InstrumentIndex(Song, "Soft lead"));
	CHECK(Night[0][0].Instrument == InstrumentIndex(Song, "Warm pad") && Night[0][0].Param[0] == 0x47);
	CHECK(Night[2][0].Volume == NO_VOLUME && Night[2][0].Pitch() == Day[2][0].Pitch());	// the triangle has no volume
	CHECK(Night[5][0].Pitch() == Day[5][0].Pitch() - 12 && Night[5][16].Pitch() == Day[5][16].Pitch() - 12);	// an octave lower
	CHECK(Night[3][0].HasNote() && !Night[3][2].HasNote() && !Night[3][4].HasNote() && Night[3][16].HasNote());	// a pulse
	CHECK_EQ(Night[3][0].Volume, 7);			// the drums at 45%
	CHECK(Night[3][0].Param[0] == 6 && Night[3][0].Param[1] == 120);
	CHECK(HasEffectNear(Night[5], 0, 8, EFF_VIBRATO));
	CHECK(Night[6][6].HasNote() && Night[6][6].Pitch() == Day[5][0].Pitch() - 12 && Night[6][6].Volume == 3);	// the echo, 6 rows later
	CHECK_HAS(Tools.Call("get_song", Parse("{}")).Text, "Tracks: 1 \"Town\", 2 \"Town (night)\" (selected)");

	// Switching tracks
	CHECK_HAS(Tools.Call("select_track", Parse("{\"track\": 1}")).Text, "Track 1 \"Town\" is selected");
	CHECK(Song.Current == 0 && Song.Rows == Day && Song.Info.Tempo == 150);
	CHECK(!Tools.Call("select_track", Parse("{\"track\": \"night\"}")).IsError && Song.Current == 1);
	CHECK(!Tools.Call("select_track", Parse("{\"track\": \"town\"}")).IsError && Song.Current == 0);
	CHECK_HAS(Tools.Call("select_track", Parse("{\"track\": 5}")).Text, "There's no track 5. The tracks: 1 \"Town\", 2 \"Town (night)\".");
	CHECK(Tools.Call("select_track", Parse("{\"track\": \"rain\"}")).IsError);
	CHECK(Tools.Call("select_track", Parse("{}")).IsError);

	// Another version changes only what it's given; names stay unique
	Result = Tools.Call("make_version", Parse("{\"version\": \"underwater\", \"to_mode\": \"minor\"}"));
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "Made track 3 \"Town (underwater)\" from track 1 \"Town\"");
	CHECK_HAS(Result.Text, "from C major to C minor");
	CHECK(Song.Info.Tempo == 150 && Song.Rows[5][8].Pitch() == Day[5][8].Pitch() - 1 && Song.Rows[3][2].HasNote());
	CHECK(Song.Rows[2] == Day[2] && Song.Rows[3] == Day[3]);
	Tools.Call("select_track", Parse("{\"track\": 1}"));
	Result = Tools.Call("make_version", Parse("{\"version\": \"night\", \"echo\": false, \"vibrato\": false, \"sounds\": false, "
		"\"drums\": \"off\", \"tempo_percent\": 90}"));
	CHECK_HAS(Result.Text, "Made track 4 \"Town (night 2)\"");
	CHECK_HAS(Result.Text, "no drums");
	CHECK_LACKS(Result.Text, "echo");
	CHECK_LACKS(Result.Text, "vibrato");
	CHECK(!Song.Rows[3][0].HasNote() && Song.Rows[5][0].Instrument == 0 && Song.Info.Tempo == 135);
	// A version of a version keeps one bracket
	CHECK_HAS(Tools.Call("make_version", Parse("{\"version\": \"rain\"}")).Text, "\"Town (rain)\"");
	CHECK(Tools.Call("make_version", Parse("{}")).IsError);
	CHECK(Tools.Call("make_version", Parse("{\"version\": \"night\", \"drums\": \"loud\"}")).IsError);
	Song.Busy = true;
	CHECK_HAS(Tools.Call("make_version", Parse("{\"version\": \"night\"}")).Text, "A dialog is open");
	Song.Busy = false;
	while (Song.Tracks.size() < 64)
		Song.Tracks.push_back(Song.Tracks[0]);
	CHECK_HAS(Tools.Call("make_version", Parse("{\"version\": \"night\"}")).Text, "64 tracks");
}

// A night version that's more than slower: lower, simpler, held, even, soft
void TestNightVersion()
{
	FakeSong Song;
	// A high melody with a sixteenth run, a cut note, halts and accents
	Song.Put(5, 0, "C-5", 0, 10);
	Song.Put(5, 2, "D-5");
	Song.Put(5, 3, "E-5");			// a sixteenth between the eighths: left out
	Song.Put(5, 4, "G-5");
	Song.Rows[5][4].Effect[0] = EFF_NOTE_CUT;
	Song.Rows[5][4].Param[0] = 2;		// cut short: legato takes it out
	Song.Put(5, 8, "E-5", NO_INSTRUMENT, 15);	// an accent
	Song.End(5, 11);				// a halt just before the next note: taken out
	Song.Put(5, 12, "D-5", NO_INSTRUMENT, 10);
	Song.Put(5, 16, "C-5");
	Song.End(5, 20);				// a halt before a rest: softened to a release
	Song.Put(5, 32, "C-5");
	// Chords struck on every beat, cut short, then another chord
	for (int Row = 0; Row < 32; Row += 4) {
		Song.Put(0, Row, Row < 16 ? "C-3" : "A-2", 2);
		Song.Rows[0][static_cast<size_t>(Row)].Effect[0] = EFF_ARPEGGIO;
		Song.Rows[0][static_cast<size_t>(Row)].Param[0] = Row < 16 ? 0x47 : 0x37;
		Song.End(0, Row + 2);
	}
	// A bass jumping octaves on the eighths
	for (int Row = 0; Row < 32; Row += 2)
		Song.Put(2, Row, Row % 4 == 0 ? (Row < 16 ? "C-2" : "A-1") : (Row < 16 ? "C-3" : "A-2"), 1);
	// Drums on every eighth, and sampled ones on beats 1 and 3
	for (int Row = 0; Row < 32; Row += 2) {
		Song.Put(3, Row, Row % 4 == 0 ? "kick" : "hihat", 2);
		if (Row % 8 == 0)
			Song.Put(4, Row, "C-3", 2);
	}
	ToolSet Tools(Song);
	ToolResult Result = Tools.Call("make_version", Parse("{\"version\": \"night\", \"chords\": \"richer\", \"echo\": false}"));
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "the melody's fast runs as their outline (1 short note left out, the notes before ring on)");
	CHECK_HAS(Result.Text, "legato (");
	CHECK_HAS(Result.Text, "even dynamics without accents (1 loud note brought down to the usual)");
	CHECK_HAS(Result.Text, "the chords on 2A03 Pulse 1 held as a pad (6 strikes left out)");
	CHECK_HAS(Result.Text, "richer chords on 2A03 Pulse 1 (major and minor triads as sevenths)");
	CHECK_HAS(Result.Text, "the bass on 2A03 Triangle in long notes (12 notes left out)");
	CHECK_HAS(Result.Text, "VRC6 Pulse 1 down 1 octave");
	CHECK_HAS(Result.Text, "the drums a soft pulse on each bar's first beat (14 hits left out)");
	CHECK_HAS(Result.Text, "2 sampled hits on the DPCM channel left out");
	const std::vector<std::vector<Cell> > &N = Song.Rows;
	// The melody: an octave lower, the run's outline, held, even
	CHECK(N[5][0].Pitch() == 48 + 12 - 12 && !N[5][3].HasNote() && N[5][2].HasNote() && N[5][4].HasNote());
	CHECK(N[5][4].Effect[0] != EFF_NOTE_CUT);
	CHECK(N[5][11].Note == NOTE_NONE && N[5][20].Note == NOTE_RELEASE);
	CHECK(N[5][8].Volume == (10 * 75 + 50) / 100);		// the accent at the usual 10, then at 75%
	// The chords: one strike a bar, richer
	CHECK(N[0][0].HasNote() && !N[0][4].HasNote() && !N[0][8].HasNote() && N[0][16].HasNote() && !N[0][20].HasNote());
	CHECK(N[0][2].Note != NOTE_HALT && N[0][0].Param[0] == 0x4B && N[0][16].Param[0] == 0x3A);
	// The bass: the beat's note held through the half bar
	CHECK(N[2][0].HasNote() && !N[2][2].HasNote() && !N[2][4].HasNote() && N[2][8].HasNote() && !N[2][12].HasNote() && N[2][16].HasNote());
	// The drums: a pulse
	CHECK(N[3][0].HasNote() && !N[3][4].HasNote() && !N[3][8].HasNote() && N[3][16].HasNote());
	CHECK(N[4][0].HasNote() && !N[4][8].HasNote() && N[4][16].HasNote() && !N[4][24].HasNote());
	// Each of them can stay as it was
	Tools.Call("select_track", Parse("{\"track\": 1}"));
	Result = Tools.Call("make_version", Parse("{\"version\": \"night\", \"melody\": \"keep\", \"chords\": \"keep\", \"bass\": \"keep\", "
		"\"register\": \"keep\", \"legato\": false, \"drums\": \"keep\", \"echo\": false}"));
	CHECK(!Result.IsError);
	CHECK_LACKS(Result.Text, "outline");
	CHECK_LACKS(Result.Text, "legato");
	CHECK_LACKS(Result.Text, "octave");
	CHECK(Song.Rows[5][3].HasNote() && Song.Rows[0][4].HasNote() && Song.Rows[2][2].HasNote() && Song.Rows[3][2].HasNote());
	CHECK_HAS(Tools.Call("make_version", Parse("{\"version\": \"night\", \"chords\": \"lush\"}")).Text, "chords should be keep, held or richer");
	{
		// Lush chords keep their own sounds: a pad would lose their arpeggios, leaving the roots
		FakeSong Lush;
		Lush.Info.DirectChanges = true;
		CHECK(!Run(Lush, "add_chords_and_bass", "{\"chords\": \"| Cmaj7 | Am7 |\", \"chord_style\": \"lush\", \"key\": \"C major\", "
			"\"bass_channel\": \"none\", \"to_bar\": 2}").IsError);
		Lush.Put(5, 0, "E-4", 0);
		Lush.Put(5, 8, "G-4");
		Lush.Put(5, 16, "C-5");
		Lush.End(5, 32, NOTE_RELEASE);
		int Chords = -1;
		for (int c = 0; c < 8 && Chords < 0; ++c)
			if (c != 5 && Lush.Rows[static_cast<size_t>(c)][0].HasNote())
				Chords = c;
		CHECK(Chords >= 0);
		if (Chords >= 0) {
			const int Instrument = Lush.Rows[static_cast<size_t>(Chords)][0].Instrument;
			ToolSet LushTools(Lush);
			const ToolResult Night = LushTools.Call("make_version", Parse("{\"version\": \"night\", \"echo\": false}"));
			CHECK(!Night.IsError);
			CHECK_HAS(Night.Text, "keep their own sounds (a pad would lose their arpeggios)");
			CHECK(Lush.Rows[static_cast<size_t>(Chords)][0].Instrument == Instrument);
		}
	}
	// A fast song slows more than 20%: to 120 BPM
	Tools.Call("select_track", Parse("{\"track\": 1}"));
	Song.Info.Tempo = 180;
	Result = Tools.Call("make_version", Parse("{\"version\": \"night\", \"echo\": false}"));
	CHECK_HAS(Result.Text, "BPM instead of 180");
	const double Bpm = Song.Info.Tempo * 6.0 / Song.Info.Speed;
	CHECK(Bpm > 115 && Bpm < 125);
}

void TestTenseVersions()
{
	FakeSong Song;
	// A melody in C major, a bass of long notes, drums on the beats of bar 1
	Song.Put(5, 0, "C-4", 0);
	Song.Put(5, 8, "E-4");
	Song.Put(5, 16, "G-4");
	Song.Put(5, 32, "E-4");
	Song.End(5, 48, NOTE_RELEASE);
	Song.Put(2, 0, "C-2", 1);
	Song.Put(2, 32, "G-1");
	Song.Rows[2][48].Effect[0] = EFF_SLIDE_DOWN;	// a note that slides isn't struck again
	Song.Rows[2][48].Param[0] = 0x21;
	Song.Put(2, 48, "C-2");
	Song.End(2, 64);
	Song.Put(3, 0, "kick", 2, 15);
	Song.Put(3, 4, "snare");
	Song.Put(3, 8, "kick");
	Song.Put(3, 12, "snare");
	const std::vector<std::vector<Cell> > Day = Song.Rows;

	ToolSet Tools(Song);
	ToolResult Result = Tools.Call("make_version", Parse("{\"version\": \"boss battle\"}"));
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "Made track 2 \"Town (boss battle)\" from track 1 \"Town\"");
	CHECK_HAS(Result.Text, "173 BPM instead of 150 (speed 6, tempo 173)");
	CHECK_HAS(Result.Text, "from C major to C minor");
	CHECK_HAS(Result.Text, "VRC6 Pulse 1 played by \"Bright lead\"");
	CHECK_HAS(Result.Text, "a driving bass on 2A03 Triangle (struck again on every eighth note)");
	CHECK_HAS(Result.Text, "busier drums (4 hi-hats between the beats)");
	CHECK_LACKS(Result.Text, "volumes at");
	CHECK_LACKS(Result.Text, "vibrato");
	CHECK_LACKS(Result.Text, "echo");
	CHECK(Song.Tracks[0].Rows == Day);
	const std::vector<std::vector<Cell> > &Battle = Song.Rows;
	// The bass pulses in eighths: struck, cut, struck... up to the next note
	const int C2 = Day[2][0].Pitch(), G1 = Day[2][32].Pitch();
	CHECK(Battle[2][0].Pitch() == C2 && Battle[2][1].Note == NOTE_HALT && Battle[2][2].Pitch() == C2 && Battle[2][3].Note == NOTE_HALT);
	CHECK(Battle[2][29].Note == NOTE_HALT && Battle[2][30].Pitch() == C2 && Battle[2][31].Note == NOTE_NONE);
	CHECK(Battle[2][32].Pitch() == G1 && Battle[2][33].Note == NOTE_HALT && Battle[2][46].Pitch() == G1 && Battle[2][47].Note == NOTE_NONE);
	CHECK(Battle[2][48].Pitch() == C2 && Battle[2][50].Note == NOTE_NONE && Battle[2][64].Note == NOTE_HALT);
	CHECK(Battle[2][0].Instrument == 1 && Battle[2][2].Instrument == NO_INSTRUMENT);
	// Hi-hats on the offbeats of bar 1 only, the snares still playing as before
	const int HiHat = InstrumentIndex(Song, "Hi-hat");
	CHECK(HiHat >= 0);
	CHECK(Battle[3][2].Pitch() == 0xE && Battle[3][2].Instrument == HiHat && Battle[3][2].Volume == 8);
	CHECK(Battle[3][14].Pitch() == 0xE && !Battle[3][18].HasNote() && !Battle[3][16].HasNote());
	CHECK(Battle[3][4].Instrument == 2 && Battle[3][4].Volume == 15 && Battle[3][4].Pitch() == Day[3][4].Pitch());
	CHECK(Battle[5][0].Instrument == InstrumentIndex(Song, "Bright lead") && Battle[5][8].Pitch() == Day[5][8].Pitch() - 1);

	// Its parts can be left as they are
	Tools.Call("select_track", Parse("{\"track\": 1}"));
	Result = Tools.Call("make_version", Parse("{\"version\": \"tense\", \"to_mode\": \"keep\", \"bass\": \"keep\", \"drums\": \"keep\", "
		"\"sounds\": false, \"tempo_percent\": 100}"));
	CHECK_HAS(Result.Text, "Made track 3 \"Town (tense)\"");
	CHECK_HAS(Result.Text, "It's a plain copy so far.");
	CHECK(Song.Rows == Day);
	Tools.Call("select_track", Parse("{\"track\": 1}"));
	CHECK_HAS(Tools.Call("make_version", Parse("{\"version\": \"tense\", \"bass\": \"loud\"}")).Text, "bass should be keep, driving or long");
	// Calm and tense words together: only what's given
	CHECK_HAS(Tools.Call("make_version", Parse("{\"version\": \"calm before the battle\"}")).Text, "It's a plain copy so far.");

	// A song in minor stays in its mode
	FakeSong Minor;
	Minor.Put(5, 0, "A-3", 0);
	Minor.Put(5, 8, "C-4");
	Minor.Put(5, 16, "E-4");
	Minor.Put(5, 24, "A-3");
	Minor.Put(5, 32, "B-3");
	Minor.Put(5, 40, "C-4");
	Minor.Put(5, 48, "A-3");
	Minor.End(5, 64);
	ToolSet MinorTools(Minor);
	Result = MinorTools.Call("make_version", Parse("{\"version\": \"danger\"}"));
	CHECK_HAS(Result.Text, "The song is in A minor already, so its mode stayed.");
	CHECK_LACKS(Result.Text, "from A minor");
	CHECK(Minor.Rows[5][8].Pitch() == Minor.Tracks[0].Rows[5][8].Pitch());
}

// Jingles

const JingleNote *SoundingAt(const std::vector<JingleNote> &Part, int Step)
{
	for (size_t i = 0; i < Part.size(); ++i)
		if (Part[i].Step <= Step && Step < Part[i].Step + Part[i].Steps)
			return &Part[i];
	return 0;
}

bool InOrder(const std::vector<JingleNote> &Part, int Steps)
{
	for (size_t i = 0; i < Part.size(); ++i) {
		if (Part[i].Steps < 1 || Part[i].Step < 0 || Part[i].Step + Part[i].Steps > Steps)
			return false;
		if (i + 1 < Part.size() && Part[i].Step + Part[i].Steps > Part[i + 1].Step)
			return false;
	}
	return true;
}

int EffectAt(const Cell &C, int Effect)
{
	for (int k = 0; k < EFFECT_COLUMNS; ++k)
		if (C.Effect[k] == Effect)
			return C.Param[k];
	return -1;
}

void TestJingleScores()
{
	const std::vector<Jingle> &All = Jingles();
	CHECK_EQ(All.size(), static_cast<size_t>(8));
	CHECK_EQ(JingleNames(), std::string("level up, victory, item, new quest, discovery, rest, defeat, danger"));
	for (size_t j = 0; j < All.size(); ++j) {
		const Jingle &J = All[j];
		const std::string Which = J.Name + ": ";
		Check(FindJingle(J.Name) == &J, (Which + "found by name").c_str(), __LINE__);
		for (size_t w = 0; w < J.Words.size(); ++w)
			Check(FindJingle(J.Words[w]) == &J, (Which + "found as " + J.Words[w]).c_str(), __LINE__);
		Check(!J.Lead.empty() && InOrder(J.Lead, J.Steps) && InOrder(J.Harmony, J.Steps) && InOrder(J.Bass, J.Steps),
			(Which + "one note at a time, within its length").c_str(), __LINE__);
		Check(J.Bpm >= 60.0 && J.Bpm <= 160.0 && J.Steps >= 8 && J.Steps <= 64 && J.LeadVolume > J.HarmonyVolume,
			(Which + "tempo, length and volumes").c_str(), __LINE__);
		for (size_t i = 0; i < J.Drums.size(); ++i)
			Check(J.Drums[i].Step < J.Steps && (i == 0 || J.Drums[i - 1].Step < J.Drums[i].Step) && J.Drums[i].Volume >= 1 &&
				J.Drums[i].Volume <= 15, (Which + "drums in order").c_str(), __LINE__);
		// The harmony stays below the lead, a third, fourth, fifth, sixth or
		// octave away (danger's minor thirds are dissonant on purpose: its
		// tritones)
		for (int Step = 0; Step < J.Steps; ++Step) {
			const JingleNote *pLead = SoundingAt(J.Lead, Step), *pHarmony = SoundingAt(J.Harmony, Step);
			if (pLead == 0 || pHarmony == 0)
				continue;
			const int Interval = pLead->Degree - pHarmony->Degree, Class = Interval % 12;
			const bool Fits = Interval > 0 && (Class == 0 || Class == 3 || Class == 4 || Class == 5 || Class == 7 || Class == 8 || Class == 9);
			char What[96];
			std::snprintf(What, sizeof(What), "%sthe harmony %d semitones below the lead at step %d", Which.c_str(), Interval, Step);
			Check(Fits, What, __LINE__);
		}
		// The last notes sound to near the end, which leaves time for their release
		const JingleNote &Last = J.Lead.back();
		Check(Last.Step + Last.Steps <= J.Steps && Last.Step + Last.Steps >= J.Steps - 4, (Which + "the ending").c_str(), __LINE__);
	}
	CHECK(FindJingle("Quest-Complete") == FindJingle("victory"));
	CHECK(FindJingle("GAME OVER") != 0 && FindJingle("GAME OVER")->Name == "defeat");
	CHECK(FindJingle("") == 0 && FindJingle("polka") == 0);
	// No other name leads to two jingles
	std::map<std::string, std::string> Owner;
	bool Unique = true;
	for (size_t j = 0; j < All.size(); ++j) {
		std::vector<std::string> Names(All[j].Words);
		Names.push_back(All[j].Name);
		for (size_t w = 0; w < Names.size(); ++w) {
			std::string Key;
			for (size_t i = 0; i < Names[w].size(); ++i)
				if (std::isalnum(static_cast<unsigned char>(Names[w][i])))
					Key += static_cast<char>(std::tolower(static_cast<unsigned char>(Names[w][i])));
			if (Owner.count(Key) && Owner[Key] != All[j].Name)
				Unique = false;
			Owner[Key] = All[j].Name;
		}
	}
	CHECK(Unique);
}

void TestJingles()
{
	// A song in G major: the melody on VRC6 Pulse 1 around D-4, a bass on the triangle
	FakeSong Song;
	const char *const MELODY[] = { "G-3", "B-3", "D-4", "G-4", "F#4", "D-4", "G-3" };
	for (int i = 0; i < 7; ++i)
		Song.Put(5, i * 8, MELODY[i], i == 0 ? 0 : NO_INSTRUMENT);
	Song.End(5, 64);
	Song.Put(2, 0, "G-2", 1);
	Song.Put(2, 32, "D-2");
	Song.End(2, 64);
	const std::vector<std::vector<Cell> > Day = Song.Rows;

	ToolSet Tools(Song);
	ToolResult Result = Tools.Call("make_jingle", Parse("{\"kind\": \"level up\"}"));
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "Made track 2 \"Level up\": the level up jingle, for a level up, a new skill or a power-up.");
	CHECK_HAS(Result.Text, "It's 3.2 s long, at 150 BPM (speed 6, tempo 150), in G major (the song is in G major).");
	CHECK_HAS(Result.Text, "Parts: lead on VRC6 Pulse 1 with \"Lead\" (00), harmony on VRC6 Pulse 2 with \"Lead\" (00), bass on "
		"2A03 Triangle with \"Bass\" (01), drums on 2A03 Noise.");
	CHECK_HAS(Result.Text, "It ends with a halt (C00), so it plays once");
	CHECK_HAS(Result.Text, "from the preset \"kick\"");
	CHECK(Song.Tracks.size() == 2 && Song.Current == 1 && Song.Info.TrackName == "Level up");
	CHECK(Song.Tracks[0].Rows == Day && Song.Info.Speed == 6 && Song.Info.Tempo == 150);
	const std::vector<std::vector<Cell> > &Up = Song.Rows;
	// The lead around the melody's register: G major's chords climbing from G-3, ending on G-4
	int Pitch = 0;
	ParsePitch("G-3", Pitch);
	CHECK(Up[5][0].Pitch() == Pitch && Up[5][0].Instrument == 0 && Up[5][0].Volume == 15);
	CHECK(Up[5][1].Pitch() == Pitch + 4 && Up[5][3].Pitch() == Pitch + 12 && Up[5][4].Pitch() == Pitch + 5 && Up[5][11].Pitch() == Pitch + 19);
	CHECK(Up[5][16].Pitch() == Pitch + 12 && Up[5][1].Instrument == NO_INSTRUMENT);
	CHECK(EffectAt(Up[5][20], EFF_VIBRATO) == 0x52 && Up[5][28].IsEnd());
	// The harmony below it, softer; the bass in the song's bass register
	CHECK(Up[6][0].Pitch() == Pitch - 8 && Up[6][0].Volume == 10 && Up[6][12].Pitch() == Pitch + 5 && Up[6][16].Pitch() == Pitch + 4);
	ParsePitch("G-2", Pitch);
	CHECK(Up[2][0].Pitch() == Pitch && Up[2][0].Instrument == 1 && Up[2][0].Volume == NO_VOLUME);
	CHECK(Up[2][4].Pitch() == Pitch + 5 && Up[2][8].Pitch() == Pitch + 7 && Up[2][16].Pitch() == Pitch);
	// Drums: kicks, a snare roll getting louder, a crash
	CHECK(Up[3][0].Pitch() == 4 && Up[3][0].Instrument == InstrumentIndex(Song, "Kick") && Up[3][0].Volume == 15);
	CHECK(Up[3][12].Pitch() == 0xA && Up[3][12].Volume == 9 && Up[3][15].Volume == 15 && Up[3][16].Pitch() == 0xD);
	// It halts after its last row, so it plays once
	CHECK(EffectAt(Up[5][32], EFF_HALT) == 0);
	std::vector<int> Columns;
	for (size_t c = 0; c < Up.size(); ++c)
		Columns.push_back(Song.Info.Channels[c].EffectColumns);
	const SongFlow Flow = FollowSong(Up, Columns, 64, 1);
	CHECK(Flow.Halts() && Flow.EndRow(64) == 33);
	for (size_t c = 0; c < Up.size(); ++c)
		for (size_t r = 33; r < Up[c].size(); ++r)
			Check(Up[c][r].IsEmpty(), "nothing after the halt", __LINE__);

	// As a sound for the game: it plays once
	std::string Text = Tools.Call("export_for_game", Parse("{\"tracks\": [\"Level up\"]}")).Text;
	CHECK_HAS(Text, "- Level up.ogg: 3.2 s");
	CHECK_HAS(Text, "plays once (the song halts)");
	CHECK_HAS(Text, "audio_play_sound(snd_level_up, 0, false);");
	CHECK(Song.Current == 1);

	// Another kind by another word for it; names stay unique
	Tools.Call("select_track", Parse("{\"track\": 1}"));
	Result = Tools.Call("make_jingle", Parse("{\"kind\": \"Quest complete\"}"));
	CHECK_HAS(Result.Text, "Made track 3 \"Victory\": the victory jingle");
	Tools.Call("select_track", Parse("{\"track\": 1}"));
	CHECK_HAS(Tools.Call("make_jingle", Parse("{\"kind\": \"levelup\"}")).Text, "Made track 4 \"Level up 2\"");
	// An item sparkles on a bell, unless the lead is asked for
	Tools.Call("select_track", Parse("{\"track\": 1}"));
	Result = Tools.Call("make_jingle", Parse("{\"kind\": \"treasure\"}"));
	CHECK_HAS(Result.Text, "lead on VRC6 Pulse 1 with \"Bell\"");
	CHECK_HAS(Result.Text, "from the preset \"bell\"");
	bool Rings = true;		// the bell rings out until the halt
	for (int r = 6; r < 16; ++r)
		Rings = Rings && Song.Rows[5][r].Note == NOTE_NONE;
	CHECK(Rings && Song.Rows[5][5].HasNote() && EffectAt(Song.Rows[5][16], EFF_HALT) == 0 && Song.Rows[6][12].IsEnd());
	Tools.Call("select_track", Parse("{\"track\": 1}"));
	CHECK_HAS(Tools.Call("make_jingle", Parse("{\"kind\": \"item\", \"lead_instrument\": \"Lead\"}")).Text,
		"lead on VRC6 Pulse 1 with \"Lead\" (00)");

	// A defeat in D minor, slower, without harmony or drums
	Tools.Call("select_track", Parse("{\"track\": 1}"));
	Result = Tools.Call("make_jingle", Parse("{\"kind\": \"game over\", \"key\": \"D\", \"bpm\": 60, \"harmony_channel\": \"none\", "
		"\"drums\": false, \"name\": \"Knocked out\"}"));
	CHECK_HAS(Result.Text, "Made track 7 \"Knocked out\": the defeat jingle");
	CHECK_HAS(Result.Text, "at 60 BPM");
	CHECK_HAS(Result.Text, "in D minor. Parts: lead on VRC6 Pulse 1 with \"Lead\" (00), bass on 2A03 Triangle");
	ParsePitch("A-4", Pitch);
	CHECK(Song.Rows[5][0].Pitch() == Pitch && Song.Rows[5][2].Pitch() == Pitch - 1 && Song.Rows[5][6].Pitch() == Pitch - 4);
	bool Empty = true;
	for (size_t r = 0; r < Song.Rows[6].size(); ++r)
		Empty = Empty && Song.Rows[6][r].IsEmpty() && Song.Rows[3][r].IsEmpty();
	CHECK(Empty);
	// Danger's bass pulses, each note cut off
	Tools.Call("select_track", Parse("{\"track\": 1}"));
	CHECK_HAS(Tools.Call("make_jingle", Parse("{\"kind\": \"alarm\"}")).Text, "Made track 8 \"Danger\"");
	CHECK(Song.Rows[2][0].HasNote() && Song.Rows[2][1].Note == NOTE_HALT && Song.Rows[2][22].HasNote() && Song.Rows[2][23].Note == NOTE_HALT);
	CHECK(Song.Rows[5][12].HasNote() && Song.Rows[5][24].Note == NOTE_RELEASE && EffectAt(Song.Rows[5][28], EFF_HALT) == 0);

	// What it can't do
	CHECK_HAS(Tools.Call("make_jingle", Parse("{}")).Text, "Say which jingle with kind: level up, victory");
	CHECK_HAS(Tools.Call("make_jingle", Parse("{\"kind\": \"polka\"}")).Text, "There's no \"polka\" jingle.");
	CHECK_HAS(Tools.Call("make_jingle", Parse("{\"kind\": \"rest\", \"key\": \"H\"}")).Text, "isn't a key");
	CHECK_HAS(Tools.Call("make_jingle", Parse("{\"kind\": \"rest\", \"harmony_channel\": \"VRC6 Pulse 1\"}")).Text,
		"harmony_channel needs a melodic channel other than the lead's");
	CHECK(Tools.Call("make_jingle", Parse("{\"kind\": \"rest\", \"bass_channel\": \"Noise\"}")).IsError);
	Song.Busy = true;
	CHECK_HAS(Tools.Call("make_jingle", Parse("{\"kind\": \"rest\"}")).Text, "A dialog is open");
	Song.Busy = false;

	// An empty song: C major, the first pulse leads with a preset
	FakeSong Empty2;
	Empty2.Info.Instruments.clear();
	ToolSet EmptyTools(Empty2);
	Result = EmptyTools.Call("make_jingle", Parse("{\"kind\": \"rest\"}"));
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "in C major (no notes to find a key from yet; give key for another)");
	CHECK_HAS(Result.Text, "lead on 2A03 Pulse 1 with \"Soft lead\"");
	CHECK_HAS(Result.Text, "harmony on 2A03 Pulse 2 with \"Soft lead\"");
	CHECK_HAS(Result.Text, "bass on 2A03 Triangle with \"Bass\"");
	CHECK_HAS(Result.Text, "It's 6.7 s long, at 72 BPM");
}

// Loops for games

void TestExpansion()
{
	// A VRC6 song: notes on the VRC6's channels, the loop on the sawtooth
	FakeSong Song;
	Song.Info.DirectChanges = true;
	Song.Put(5, 0, "C-4", 0);
	Song.Put(7, 16, "C-2", 0);
	Song.Put(0, 0, "E-4", 2);
	Song.Rows[7][127].Effect[0] = EFF_JUMP;
	ToolSet Tools(Song);

	// Notes on the channels that go: the user is asked first
	ToolResult Result = Tools.Call("set_expansion", Parse("{\"chip\": \"VRC7\"}"));
	CHECK(Result.IsError);
	CHECK_HAS(Result.Text, "The song has 2 notes on the VRC6's channels (track 1 \"Town\"). Switching to the VRC7 clears those channels");
	CHECK_HAS(Result.Text, "call set_expansion again with confirm: true");
	CHECK(Song.Info.Expansion == "VRC6" && Song.Rows[5][0].HasNote());
	Result = Tools.Call("set_expansion", Parse("{\"chip\": \"vrc7\", \"confirm\": true}"));
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "The song uses the VRC7 now: 6 more channels (VRC7 FM Channel 1, VRC7 FM Channel 2, VRC7 FM Channel 3, "
		"VRC7 FM Channel 4, VRC7 FM Channel 5, VRC7 FM Channel 6) besides the 2A03's five.");
	CHECK_HAS(Result.Text, "piano");
	CHECK_HAS(Result.Text, "Cleared 2 notes on the channels that went.");
	CHECK_HAS(Result.Text, "Moved 1 effect that steers the song (speed, loops, ends) from the channels that went to VRC7 FM Channel 6.");
	CHECK_HAS(Result.Text, "FamiTracker can't undo switching chips");
	CHECK(Song.Info.Expansion == "VRC7" && Song.Info.Channels.size() == 11 && Song.Info.Channels[5].Kind == KIND_FM);
	CHECK(Song.Rows[10][127].Effect[0] == EFF_JUMP && !Song.Rows[5][0].HasNote() && !Song.Rows[7][16].HasNote());
	CHECK(Song.Rows[0][0].HasNote());	// the 2A03's stay
	CHECK_HAS(Tools.Call("set_expansion", Parse("{\"chip\": \"VRC7\"}")).Text, "The song uses the VRC7 already.");
	CHECK_HAS(Tools.Call("get_song", Parse("{}")).Text, "VRC7 FM Channel 6");

	// The VRC7's own sounds by name; a preset's name picks the one most like it
	Result = Tools.Call("write_notes", Parse("{\"channel\": \"FM Channel 1\", \"instrument\": \"piano\", \"notes\": ["
		"{\"bar\": 1, \"pitch\": \"C-4\", \"beats\": 1}, {\"bar\": 1, \"beat\": 2, \"pitch\": \"E-4\", \"beats\": 1}]}"));
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "Added the instrument \"Piano\" (03) (VRC7 patch 3, piano)");
	const int Piano = InstrumentIndex(Song, "Piano");
	CHECK(Piano == 3 && Song.Vrc7Patches[Piano] == 3);
	CHECK(Song.Rows[5][0].Instrument == Piano && Song.Rows[5][4].Pitch() == 52 && Song.Rows[5][8].Note == NOTE_HALT);
	Result = Tools.Call("write_notes", Parse("{\"channel\": \"FM Channel 2\", \"instrument\": \"soft lead\", \"notes\": ["
		"{\"bar\": 1, \"pitch\": \"G-4\", \"beats\": 2}]}"));
	CHECK_HAS(Result.Text, "Added the instrument \"Flute\" (04) (VRC7 patch 4");
	CHECK(Song.Rows[6][0].Instrument == 4 && Song.Vrc7Patches[4] == 4);
	Result = Tools.Call("write_notes", Parse("{\"channel\": \"FM Channel 3\", \"instrument\": \"Piano\", \"notes\": ["
		"{\"bar\": 2, \"pitch\": \"C-3\", \"beats\": 4}]}"));
	CHECK(!Result.IsError && Song.Rows[7][16].Instrument == Piano);	// the one there already
	CHECK_LACKS(Result.Text, "Added the instrument");
	CHECK_HAS(Tools.Call("add_instrument", Parse("{\"preset\": \"horn\", \"chip\": \"vrc7\"}")).Text,
		"Added \"Brass\" (05) (VRC7 instrument, patch 12: brass section");
	CHECK_HAS(Tools.Call("add_instrument", Parse("{\"preset\": \"choir\", \"channel\": \"FM Channel 4\"}")).Text, "patch 15");
	CHECK_HAS(Tools.Call("add_instrument", Parse("{\"preset\": \"kazoo\", \"channel\": \"FM Channel 4\"}")).Text, "No VRC7 sound called \"kazoo\"");
	CHECK_HAS(Tools.Call("write_notes", Parse("{\"channel\": \"FM Channel 4\", \"instrument\": \"snare\", \"notes\": ["
		"{\"bar\": 1, \"pitch\": \"C-4\", \"beats\": 1}]}")).Text, "drum for the noise channel");
	// VRC6 sounds aren't for the VRC7 any more
	CHECK_HAS(Tools.Call("add_instrument", Parse("{\"preset\": \"soft lead\", \"chip\": \"VRC6\"}")).Text, "no VRC6 channels");

	// Back to the VRC6: three FM channels go, and the loop moves to the sawtooth
	Song.Rows[10][127] = Cell();
	Song.Rows[9][63].Effect[0] = EFF_HALT;	// on FM Channel 5
	Result = Tools.Call("set_expansion", Parse("{\"chip\": \"VRC6\", \"confirm\": true}"));
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "Cleared 4 notes on the channels that went.");
	CHECK_HAS(Result.Text, "Moved 1 effect that steers the song (speed, loops, ends) from the channels that went to VRC6 Sawtooth.");
	CHECK(Song.Info.Channels.size() == 8 && Song.Info.Channels[7].Kind == KIND_SAWTOOTH && Song.Rows[7][63].Effect[0] == EFF_HALT);
	CHECK(!Song.Rows[5][0].HasNote());

	// Nothing to lose: no question, and no expansion chip at all
	Result = Tools.Call("set_expansion", Parse("{\"chip\": \"none\"}"));
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "The song uses no expansion chip now: just the 2A03's five channels.");
	CHECK_HAS(Result.Text, "from the channels that went to 2A03 DPCM");
	CHECK(Song.Info.Channels.size() == 5 && Song.Info.Expansion.empty() && Song.Rows[4][63].Effect[0] == EFF_HALT);

	// The N163: its channel count, and asking only about the channels that go
	Result = Tools.Call("set_expansion", Parse("{\"chip\": \"N163\", \"n163_channels\": 8}"));
	CHECK_HAS(Result.Text, "The song uses the N163 now: 8 more channels");
	CHECK_HAS(Result.Text, "Wavetable channels");
	Song.Put(12, 0, "C-4");
	CHECK_HAS(Tools.Call("set_expansion", Parse("{\"chip\": \"N163\", \"n163_channels\": 8}")).Text, "already");
	CHECK(!Tools.Call("set_expansion", Parse("{\"chip\": \"Namco\", \"n163_channels\": 8}")).IsError);
	Result = Tools.Call("set_expansion", Parse("{\"chip\": \"N163\", \"n163_channels\": 4}"));
	CHECK(Result.IsError);
	CHECK_HAS(Result.Text, "The song has 1 note on the N163 channels after Namco 4");
	CHECK_HAS(Result.Text, "Switching to 4 N163 channels removes those channels in every track, and FamiTracker can't undo it.");
	Song.Rows[12][0] = Cell();
	Result = Tools.Call("set_expansion", Parse("{\"chip\": \"N163\", \"n163_channels\": 4}"));
	CHECK(!Result.IsError && Song.Info.Channels.size() == 9 && Song.Info.Channels[8].Name == "Namco 4");

	// Mistakes
	CHECK_HAS(Tools.Call("set_expansion", Parse("{\"chip\": \"5B\"}")).Text, "Sunsoft 5B");
	CHECK(Tools.Call("set_expansion", Parse("{\"chip\": \"SID\"}")).IsError);
	CHECK(Tools.Call("set_expansion", Parse("{}")).IsError);
	Song.Busy = true;
	CHECK_HAS(Tools.Call("set_expansion", Parse("{\"chip\": \"VRC6\"}")).Text, "A dialog is open");
	Song.Busy = false;

	// A PAL song becomes NTSC
	FakeSong Pal;
	Pal.Info.FrameRate = 50;
	ToolSet PalTools(Pal);
	CHECK_HAS(PalTools.Call("set_expansion", Parse("{\"chip\": \"VRC7\"}")).Text, "so the song is NTSC now");
	CHECK(Pal.Info.FrameRate == 60);
}

const PresetSequence *SequenceOf(const Preset &Sound, int Type)
{
	for (size_t i = 0; i < Sound.Sequences2A03.size(); ++i)
		if (Sound.Sequences2A03[i].Type == Type)
			return &Sound.Sequences2A03[i];
	return 0;
}

// Whether FamiTracker can hold the instrument made for a sound effect's layer
bool Holdable(const transcribe::SfxLayer &Layer)
{
	const transcribe::SfxInstrument I = transcribe::ToInstrument(Layer, false);
	const bool Noise = Layer.channel == transcribe::SfxChannel::Noise;
	bool Ok = !I.volume.empty() && I.volume.size() <= 252 && I.volume.back() == 0 && I.arpeggio.size() <= 252 &&
		I.pitch.size() <= 252 && !I.duty.empty() == (Layer.channel == transcribe::SfxChannel::Pulse || !I.duty.empty());
	for (size_t i = 0; i < I.volume.size(); ++i)
		Ok = Ok && I.volume[i] >= 0 && I.volume[i] <= 15;
	Ok = Ok && (Noise ? I.note >= 0 && I.note <= 15 : I.note >= 9 && I.note <= 95);
	for (size_t i = 0; i < I.arpeggio.size(); ++i)
		Ok = Ok && (I.fixedArpeggio ? I.arpeggio[i] >= 0 && I.arpeggio[i] <= 15 : I.note + I.arpeggio[i] >= 0 && I.note + I.arpeggio[i] <= 95);
	for (size_t i = 0; i < I.pitch.size(); ++i)
		Ok = Ok && I.pitch[i] >= -128 && I.pitch[i] <= 127;
	for (size_t i = 0; i < I.duty.size(); ++i)
		Ok = Ok && I.duty[i] >= 0 && I.duty[i] <= (Noise ? 1 : 3);
	// The arpeggio sets the period anew each tick the pitch sequence moves it from there
	Ok = Ok && (I.pitch.empty() || I.arpeggio.size() == I.pitch.size());
	return Ok;
}

// The pitch heard around a time in a rendering, in Hz (0 when there's none)
double PitchAround(const std::vector<transcribe::PitchFrame> &Frames, double Time)
{
	std::vector<double> Found;
	for (size_t i = 0; i < Frames.size(); ++i)
		if (std::fabs(Frames[i].time - Time) <= 0.012 && Frames[i].frequency > 0.0 && Frames[i].aperiodicity < 0.3)
			Found.push_back(Frames[i].frequency);
	if (Found.empty())
		return 0.0;
	std::sort(Found.begin(), Found.end());
	return Found[Found.size() / 2];
}

void TestSoundEffects()
{
	// Every kind and its variants, also changed far, make instruments FamiTracker can hold
	const std::vector<SoundEffectKind> &Kinds = SoundEffectKinds();
	CHECK(Kinds.size() >= 20);
	for (size_t k = 0; k < Kinds.size(); ++k) {
		for (int Variant = 1; Variant <= 9; ++Variant) {
			for (int Far = 0; Far < 3; ++Far) {
				SoundEffectChanges Changes;
				Changes.Variant = Variant;
				Changes.Semitones = Far == 0 ? 0 : (Far == 1 ? 24 : -24);
				Changes.LengthPercent = Far == 0 ? 100 : (Far == 1 ? 400 : 25);
				Changes.VolumePercent = Far == 0 ? 100 : (Far == 1 ? 200 : 10);
				const transcribe::SoundEffect Effect = ChangeSoundEffect(Kinds[k].Make(), Changes);
				bool Ok = !Effect.layers.empty() && Effect.length() > 0 && Effect.length() <= 300;
				for (size_t l = 0; l < Effect.layers.size(); ++l)
					Ok = Ok && Holdable(Effect.layers[l]);
				Check(Ok, Kinds[k].Name.c_str(), __LINE__);
			}
		}
		CHECK(FindSoundEffect(Kinds[k].Name) == &Kinds[k]);
	}
	CHECK(FindSoundEffect("pew") != 0 && FindSoundEffect("pew")->Name == "laser");
	CHECK(FindSoundEffect("Boing") != 0 && FindSoundEffect("Boing")->Name == "bounce");
	CHECK(FindSoundEffect("kssh") != 0 && FindSoundEffect("kssh")->Name == "explosion");
	CHECK(FindSoundEffect("power up") != 0 && FindSoundEffect("power up")->Name == "power-up");
	CHECK(FindSoundEffect("nope") == 0 && FindSoundEffect("") == 0);
	CHECK_EQ(VariantText(1), std::string());
	CHECK_EQ(VariantText(2), std::string("3 semitones higher, 15% shorter, another pulse duty"));
	CHECK_EQ(VariantText(3), std::string("3 semitones lower, 20% longer"));

	// Pitches between notes: the arpeggio picks the nearest note, the pitch sequence the period from it
	transcribe::SfxLayer Bend;
	Bend.volume = { 15, 15, 15 };
	Bend.pitch = { 45.0, 45.5, 46.0 };
	transcribe::SfxInstrument I = transcribe::ToInstrument(Bend, false);
	CHECK(I.note == 45 && I.volume == std::vector<int>({ 15, 15, 15, 0 }) && I.duty == std::vector<int>({ 2 }) && !I.fixedArpeggio);
	CHECK(I.arpeggio == std::vector<int>({ 0, 1, 1 }) && I.pitch.size() == 3 && I.pitch[0] == 0 && I.pitch[2] == 0);
	CHECK(I.pitch[1] > 3 && I.pitch[1] < 12);	// half a semitone below A#3: a longer period
	Bend.pitch = { 45.0 };
	I = transcribe::ToInstrument(Bend, false);
	CHECK(I.arpeggio.empty() && I.pitch.empty());	// one note needs neither
	transcribe::SfxLayer Hiss;
	Hiss.channel = transcribe::SfxChannel::Noise;
	Hiss.volume = { 12, 8, 4 };
	Hiss.pitch = { 12, 10, 8 };
	Hiss.duty = { 1 };
	I = transcribe::ToInstrument(Hiss, false);
	CHECK(I.note == 12 && I.fixedArpeggio && I.arpeggio == std::vector<int>({ 12, 10, 8 }) && I.duty == std::vector<int>({ 1 }));

	// Played as the 2A03 plays them
	transcribe::SoundEffect Tone;
	transcribe::SfxLayer A;
	A.volume.assign(20, 12);
	A.pitch = { 45.0 };
	Tone.layers.push_back(A);
	std::vector<transcribe::PitchFrame> Frames = transcribe::TrackPitch(transcribe::RenderEffect(Tone, 44100, false), transcribe::PitchOptions());
	CHECK(std::fabs(PitchAround(Frames, 0.15) - 440.0) < 4.0);
	Frames = transcribe::TrackPitch(transcribe::RenderEffect(Tone, 44100, true), transcribe::PitchOptions());
	CHECK(std::fabs(PitchAround(Frames, 0.15) - 440.0) < 4.0);	// PAL's table is in tune too
	Tone.layers[0].channel = transcribe::SfxChannel::Triangle;
	Tone.layers[0].pitch = { 57.0 };
	Frames = transcribe::TrackPitch(transcribe::RenderEffect(Tone, 44100, false), transcribe::PitchOptions());
	CHECK(std::fabs(PitchAround(Frames, 0.15) - 440.0) < 4.0);	// the triangle sounds an octave below what's written
	Frames = transcribe::TrackPitch(transcribe::RenderEffect(FindSoundEffect("jump")->Make(), 44100, false), transcribe::PitchOptions());
	CHECK(PitchAround(Frames, 0.03) > 250.0 && PitchAround(Frames, 0.2) > PitchAround(Frames, 0.03) * 1.6);	// it rises
	Frames = transcribe::TrackPitch(transcribe::RenderEffect(FindSoundEffect("laser")->Make(), 44100, false), transcribe::PitchOptions());
	CHECK(PitchAround(Frames, 0.05) > PitchAround(Frames, 0.19) * 1.6 && PitchAround(Frames, 0.19) > 0.0);	// it falls
	Frames = transcribe::TrackPitch(transcribe::RenderEffect(FindSoundEffect("explosion")->Make(), 44100, false), transcribe::PitchOptions());
	int Pitched = 0, Sounding = 0;
	for (size_t i = 0; i < Frames.size(); ++i) {
		if (Frames[i].rms < 0.01)
			continue;
		++Sounding;
		Pitched += Frames[i].aperiodicity < 0.2 ? 1 : 0;
	}
	CHECK(Sounding > 50 && Pitched * 4 < Sounding);	// noise

	// The tool: a track of its own that halts, its instrument named after it
	FakeSong Song;
	ToolSet Tools(Song);
	ToolResult Result = Tools.Call("make_sound_effect", Parse("{\"kind\": \"jump\"}"));
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "Made track 2 \"SFX Jump\": the jump sound (a quick rising blip for jumping), 0.27 s long, on 2A03 Pulse 1 with "
		"\"SFX Jump\" (03).");
	CHECK_HAS(Result.Text, "audio_play_sound(snd_sfx_jump, 0, false)");
	CHECK(Song.Tracks.size() == 2 && Song.Current == 1 && Song.Info.TrackName == "SFX Jump" && Song.Info.Speed == 1 && Song.Info.Tempo == 150);
	CHECK(Song.Rows[0][0].Pitch() == 38 && Song.Rows[0][0].Instrument == 3);
	CHECK(Song.Rows[0][16].Note == NOTE_HALT && EffectAt(Song.Rows[0][16], EFF_HALT) == 0);
	bool Quiet = true;
	for (size_t c = 1; c < Song.Rows.size(); ++c)
		for (size_t r = 0; r < Song.Rows[c].size(); ++r)
			Quiet = Quiet && Song.Rows[c][r].IsEmpty();
	CHECK(Quiet);
	const Preset &Jump = Song.Made[3];
	CHECK(Jump.Name == "SFX Jump" && SequenceOf(Jump, SEQUENCE_VOLUME) != 0 && SequenceOf(Jump, SEQUENCE_VOLUME)->Items.size() == 17);
	CHECK(SequenceOf(Jump, SEQUENCE_ARPEGGIO) != 0 && SequenceOf(Jump, SEQUENCE_ARPEGGIO)->Setting == SEQUENCE_ABSOLUTE);
	CHECK(SequenceOf(Jump, SEQUENCE_DUTY) != 0 && SequenceOf(Jump, SEQUENCE_DUTY)->Items == std::vector<int>({ 2 }));
	CHECK(SequenceOf(Jump, SEQUENCE_ARPEGGIO)->Items.back() >= 13);	// up more than an octave

	// It plays once in the game
	std::string Text = Tools.Call("export_for_game", Parse("{\"tracks\": [\"SFX Jump\"]}")).Text;
	CHECK_HAS(Text, "- SFX Jump.ogg: 0.3 s");
	CHECK_HAS(Text, "plays once (the song halts)");

	// Another take gets its own name and instruments; two parts on two channels
	Tools.Call("select_track", Parse("{\"track\": 1}"));
	Result = Tools.Call("make_sound_effect", Parse("{\"kind\": \"jump\", \"variant\": 3}"));
	CHECK_HAS(Result.Text, "Made track 3 \"SFX Jump 2\"");
	CHECK_HAS(Result.Text, "with \"SFX Jump 2\" (04)");
	CHECK_HAS(Result.Text, "Variant 3: 3 semitones lower, 20% longer.");
	CHECK(Song.Rows[0][0].Pitch() == 35);
	Result = Tools.Call("make_sound_effect", Parse("{\"kind\": \"treasure\"}"));
	CHECK_HAS(Result.Text, "on 2A03 Pulse 1 with \"SFX Chest\" (05), 2A03 Pulse 2 with \"SFX Chest (pulse 2)\" (06).");
	CHECK(Song.Rows[1][3].HasNote() && Song.Rows[1][3].Instrument == 6);	// the echo, three ticks later
	Result = Tools.Call("make_sound_effect", Parse("{\"kind\": \"kssh\", \"name\": \"Bomb\"}"));
	CHECK_HAS(Result.Text, "Made track 5 \"Bomb\": the explosion sound");
	CHECK_HAS(Result.Text, "on 2A03 Noise with \"Bomb\" (07), 2A03 Triangle with \"Bomb (triangle)\" (08).");
	CHECK(SequenceOf(Song.Made[7], SEQUENCE_ARPEGGIO) != 0 && SequenceOf(Song.Made[7], SEQUENCE_ARPEGGIO)->Setting == SEQUENCE_FIXED);
	CHECK(Song.Rows[3][0].HasNote() && Song.Rows[3][0].Pitch() == 9 && Song.Rows[2][0].Pitch() == 30);	// crackling: 8, up 1
	Result = Tools.Call("make_sound_effect", Parse("{\"kind\": \"coin\", \"semitones\": 12, \"length_percent\": 50, \"volume_percent\": 60}"));
	CHECK_HAS(Result.Text, "Changed: 12 semitones, 50% of the length, 60% of the volume.");
	CHECK(Song.Rows[0][0].Pitch() == 71 && Song.Made[9].Sequences2A03[0].Items[0] == 7);

	// From a recording of the user's voice: "pew", falling from 800 to 300 Hz
	{
		transcribe::Audio Pew;
		Pew.sampleRate = 44100;
		double Phase = 0.0;
		for (int i = 0; i < 13230; ++i) {
			const double x = i / 13230.0;
			Phase += 2.0 * 3.14159265358979 * 800.0 * std::pow(300.0 / 800.0, x) / 44100.0;
			Pew.samples.push_back(static_cast<float>(0.4 * (1.0 - 0.7 * x) * (std::sin(Phase) + 0.5 * std::sin(2.0 * Phase) +
				0.3 * std::sin(3.0 * Phase))));
		}
		std::string Error;
		CHECK(transcribe::WriteWav("pew.wav", Pew, &Error));
		Tools.Call("select_track", Parse("{\"track\": 1}"));
		Result = Tools.Call("make_sound_effect", Parse("{\"recording\": \"pew.wav\"}"));
		CHECK(!Result.IsError);
		CHECK_HAS(Result.Text, "\"SFX Pew\" from the recording (pew.wav): the voice's pitch and loudness on a pulse, as sung.");
		CHECK_HAS(Result.Text, "It's 0.28 s long, on 2A03 Pulse 1 with \"SFX Pew\"");
		CHECK_HAS(Result.Text, "audio_play_sound(snd_sfx_pew, 0, false)");
		const int Note = Song.Rows[0][0].Pitch();
		CHECK(Note >= 53 && Note <= 56);	// 800 Hz is about G-4 (55)
		const Preset &Voice = Song.Made[Song.Rows[0][0].Instrument];
		CHECK(SequenceOf(Voice, SEQUENCE_ARPEGGIO) != 0 && SequenceOf(Voice, SEQUENCE_ARPEGGIO)->Items.back() <= -14);	// it falls
		CHECK(SequenceOf(Voice, SEQUENCE_PITCH) != 0);	// with the steps between notes
		Tools.Call("select_track", Parse("{\"track\": 1}"));
		Result = Tools.Call("make_sound_effect", Parse("{\"recording\": \"pew.wav\", \"octaves\": 1, \"name\": \"Zap\"}"));
		CHECK_HAS(Result.Text, "Made track 8 \"Zap\" from the recording (pew.wav): the voice's pitch and loudness on a pulse, 1 "
			"octave higher.");
		CHECK(Song.Rows[0][0].Pitch() == Note + 12);
		// The newest take recorded in FamiTracker, named without its time
		CHECK(transcribe::WriteWav("Voice 2026-10-21 18-30-00.wav", Pew, &Error));
		Song.LatestTake = "Voice 2026-10-21 18-30-00.wav";
		Tools.Call("select_track", Parse("{\"track\": 1}"));
		Result = Tools.Call("make_sound_effect", Parse("{\"recording\": \"Latest\"}"));
		CHECK_HAS(Result.Text, "\"SFX Voice\" from the recording (Voice 2026-10-21 18-30-00.wav)");
		std::remove("Voice 2026-10-21 18-30-00.wav");
		Song.LatestTake.clear();
		Result = Tools.Call("make_sound_effect", Parse("{\"recording\": \"latest\"}"));
		CHECK(Result.IsError);
		CHECK_HAS(Result.Text, "There are no recordings yet in C:\\Games\\FamiTracker\\Recordings.");
		std::remove("pew.wav");
		CHECK(Tools.Call("make_sound_effect", Parse("{\"recording\": \"pew.wav\", \"kind\": \"jump\"}")).IsError);
		CHECK_HAS(Tools.Call("make_sound_effect", Parse("{\"recording\": \"/mnt/c/Takes/pew.wav\"}")).Text,
			"The recording C:\\Takes\\pew.wav couldn't be read");
		CHECK(!Song.Opened.empty() && Song.Opened.back() == "C:\\Takes\\pew.wav");	// FamiTracker names it its way
	}

	// An instrument left from a sound whose track was removed: its name isn't
	// free, or the new sound would play with the old instrument
	Song.AddInstrumentInfo(40, "SFX Hurt (noise)", "2A03", false);
	Result = Tools.Call("make_sound_effect", Parse("{\"kind\": \"hurt\"}"));
	CHECK_HAS(Result.Text, "Made track 10 \"SFX Hurt 2\"");
	Result = Tools.Call("make_sound_effect", Parse("{\"kind\": \"hurt\", \"name\": \"sfx hurt\"}"));
	CHECK(Result.IsError);
	CHECK_HAS(Result.Text, "There's an instrument called \"SFX Hurt (noise)\" already (from an earlier sound, perhaps)");
	CHECK(FreeSoundName(Song.Info, "SFX Hurt") == "SFX Hurt 3" && FreeSoundName(Song.Info, "SFX Bonk") == "SFX Bonk");
	CHECK(SoundNameProblem(Song.Info, "SFX Bonk").empty() && SoundNameProblem(Song.Info, "SFX Hurt (noise)") != "");
	CHECK_HAS(SoundNameProblem(Song.Info, "SFX HURT 2"), "There's a track called \"SFX Hurt 2\" already");
	CHECK_HAS(SoundNameProblem(Song.Info, " "), "can't be empty");

	// Mistakes
	CHECK_HAS(Tools.Call("make_sound_effect", Parse("{\"kind\": \"moo\"}")).Text, "There's no \"moo\" sound. The kinds: jump, land, coin");
	CHECK(Tools.Call("make_sound_effect", Parse("{}")).IsError);
	CHECK_HAS(Tools.Call("make_sound_effect", Parse("{\"kind\": \"coin\", \"name\": \"Bomb\"}")).Text, "There's a track called \"Bomb\"");
	CHECK(Tools.Call("make_sound_effect", Parse("{\"kind\": \"coin\", \"variant\": 12}")).IsError);
	Song.Busy = true;
	CHECK_HAS(Tools.Call("make_sound_effect", Parse("{\"kind\": \"coin\"}")).Text, "A dialog is open");
	Song.Busy = false;
	while (Song.Tracks.size() < 64)
		Song.Tracks.push_back(Song.Tracks[0]);
	CHECK_HAS(Tools.Call("make_sound_effect", Parse("{\"kind\": \"coin\"}")).Text, "64 tracks");
}

// The loudness of a rendering in windows of Seconds, from Start for Count of them
std::vector<double> Loudness(const transcribe::Audio &A, double Start, double Seconds, int Count)
{
	std::vector<double> Out;
	const size_t Size = static_cast<size_t>(Seconds * A.sampleRate);
	for (int w = 0; w < Count; ++w) {
		const size_t From = static_cast<size_t>((Start + w * Seconds) * A.sampleRate);
		double Sum = 0.0;
		size_t n = 0;
		for (size_t i = From; i < From + Size && i < A.samples.size(); ++i, ++n)
			Sum += A.samples[i] * A.samples[i];
		Out.push_back(n > 0 ? std::sqrt(Sum / n) : 0.0);
	}
	return Out;
}

// How many times a layer starts sounding after silence
int Bursts(const transcribe::SfxLayer &Layer)
{
	int Count = 0;
	bool Sounding = false;
	for (size_t t = 0; t < Layer.volume.size(); ++t) {
		if (Layer.volume[t] > 0 && !Sounding)
			++Count;
		Sounding = Layer.volume[t] > 0;
	}
	return Count;
}

const transcribe::SfxLayer *LayerOn(const transcribe::SoundEffect &E, transcribe::SfxChannel Channel)
{
	for (size_t l = 0; l < E.layers.size(); ++l)
		if (E.layers[l].channel == Channel)
			return &E.layers[l];
	return 0;
}

void TestDesignedSounds()
{
	using transcribe::SfxChannel;
	// Nature and machines among the kinds, found by the words people use; the ambience loops
	CHECK(FindSoundEffect("bird flying away") != 0 && FindSoundEffect("bird flying away")->Name == "wings");
	CHECK(FindSoundEffect("fire")->Name == "fire" && FindSoundEffect("pew")->Name == "laser" && FindSoundEffect("shoot")->Name == "laser");
	CHECK(FindSoundEffect("heavy wind")->Name == "wind" && FindSoundEffect("siren")->Name == "alarm" &&
		FindSoundEffect("low health")->Name == "heartbeat" && FindSoundEffect("thunderstorm")->Name == "thunder" &&
		FindSoundEffect("chirp")->Name == "birds" && FindSoundEffect("motor")->Name == "engine" &&
		FindSoundEffect("drizzle")->Name == "rain");
	const char *const LOOPING[] = { "wind", "rain", "fire", "engine", "alarm", "heartbeat" };
	for (size_t i = 0; i < 6; ++i) {
		const SoundEffectKind *K = FindSoundEffect(LOOPING[i]);
		const transcribe::SoundEffect E = K->Make();
		Check(K->Loops && E.length() <= MaxSoundTicks(true), LOOPING[i], __LINE__);
	}
	CHECK(!FindSoundEffect("thunder")->Loops && !FindSoundEffect("jump")->Loops && !FindSoundEffect("wings")->Loops);

	// As they sound: gusts come and go, the siren rises, the heart beats twice and rests
	const transcribe::Audio Wind = transcribe::RenderEffect(FindSoundEffect("wind")->Make(), 44100, false);
	const std::vector<double> Gusts = Loudness(Wind, 0.0, 0.1, 40);
	CHECK(*std::max_element(Gusts.begin(), Gusts.end()) > 2.0 * *std::min_element(Gusts.begin(), Gusts.end()));
	std::vector<transcribe::PitchFrame> Frames = transcribe::TrackPitch(Wind, transcribe::PitchOptions());
	int Pitched = 0, Sounding = 0;
	for (size_t i = 0; i < Frames.size(); ++i) {
		if (Frames[i].rms < 0.01)
			continue;
		++Sounding;
		Pitched += Frames[i].aperiodicity < 0.2 ? 1 : 0;
	}
	CHECK(Sounding > 300 && Pitched * 4 < Sounding);	// noise
	Frames = transcribe::TrackPitch(transcribe::RenderEffect(FindSoundEffect("alarm")->Make(), 44100, false), transcribe::PitchOptions());
	CHECK(PitchAround(Frames, 0.05) > 800.0 && PitchAround(Frames, 0.4) > PitchAround(Frames, 0.05) * 1.6);
	const std::vector<double> Beat = Loudness(transcribe::RenderEffect(FindSoundEffect("heartbeat")->Make(), 44100, false), 0.0, 0.05, 20);
	CHECK(Beat[0] > 0.02 && Beat[5] > 0.02 && Beat[10] < Beat[0] * 0.05 && Beat[18] < Beat[0] * 0.05);
	const transcribe::SoundEffect Thunder = FindSoundEffect("thunder")->Make();
	CHECK(Thunder.length() > 180 && LayerOn(Thunder, SfxChannel::Noise) != 0 && LayerOn(Thunder, SfxChannel::Triangle) != 0);
	const transcribe::SoundEffect Wings = FindSoundEffect("wings")->Make();
	CHECK(Bursts(*LayerOn(Wings, SfxChannel::Noise)) == 7);
	const transcribe::SfxLayer &Flaps = *LayerOn(Wings, SfxChannel::Noise);
	CHECK(Flaps.volume[0] > Flaps.volume[Flaps.volume.size() - 8] + 4);	// flying away: fainter
	CHECK(Bursts(*LayerOn(Wings, SfxChannel::Pulse)) == 2);	// a chirp, and a faint one far off, on one pulse

	// The designer: layers of a sound described in words
	std::vector<SfxLayerDesign> Layers(1);
	Layers[0].Channel = SfxChannel::Noise;
	Layers[0].Seconds = 1.0;
	Layers[0].Pitch = 6;
	Layers[0].Volume = 10;
	transcribe::SoundEffect E;
	std::string Problem;
	CHECK(DesignSoundEffect(Layers, 0.0, 60, 1, false, 2, E, Problem) && E.layers.size() == 1 && E.length() == 60);
	CHECK(E.layers[0].volume[30] == 10 && E.layers[0].volume[59] < 10);	// a steady sound ends with a short fade...
	CHECK(DesignSoundEffect(Layers, 0.0, 60, 1, true, 2, E, Problem) && E.layers[0].volume[59] == 10);	// ...unless it loops on
	Layers[0].Gusts = 3;	// gusts in whole cycles: a loop's end leads into its start
	CHECK(DesignSoundEffect(Layers, 0.0, 60, 1, true, 2, E, Problem));
	CHECK(std::abs(E.layers[0].volume[59] - E.layers[0].volume[0]) <= 2);
	transcribe::SoundEffect Again;
	CHECK(DesignSoundEffect(Layers, 0.0, 60, 1, true, 2, Again, Problem) && Again.layers[0].volume == E.layers[0].volume);	// the same each time
	CHECK(DesignSoundEffect(Layers, 0.0, 60, 2, true, 2, Again, Problem) && Again.layers[0].volume != E.layers[0].volume);	// another take
	// Repeats with a slide each time (chirps), and steps between them
	Layers[0] = SfxLayerDesign();
	Layers[0].Seconds = 1.0;
	Layers[0].Pitch = 60;
	Layers[0].PitchTo = 67;
	Layers[0].Repeats = 4;
	Layers[0].Step = -2;
	Layers[0].Envelope = SFX_HIT;
	CHECK(DesignSoundEffect(Layers, 0.0, 60, 1, false, 2, E, Problem) && Bursts(E.layers[0]) == 4);
	CHECK(std::fabs(E.layers[0].pitch[0] - 60.0) < 1e-9 && E.layers[0].pitch[10] > 66.0);	// up within the first...
	CHECK(std::fabs(E.layers[0].pitch[15] - 58.0) < 1e-9);	// ...and the second starts a whole step lower
	// Layers on one channel share it when they don't sound at once; two at once don't fit
	Layers.resize(2);
	Layers[1] = Layers[0];
	Layers[1].Start = 1.5;
	Layers[1].Seconds = 0.5;
	CHECK(DesignSoundEffect(Layers, 0.0, 60, 1, false, 2, E, Problem) && E.layers.size() == 1 && E.length() == 120);
	CHECK(E.layers[0].volume[70] == 0 && E.layers[0].volume[90] > 0);
	Layers[1].Channel = SfxChannel::Noise;
	Layers[0].Channel = SfxChannel::Noise;
	Layers[1].Start = 0.5;
	CHECK(!DesignSoundEffect(Layers, 0.0, 60, 1, false, 2, E, Problem));
	CHECK_HAS(Problem, "Two noise layers sound at once (at 0.5 s); the 2A03 has one noise channel");
	Layers.assign(3, SfxLayerDesign());	// three pulses at once
	CHECK(!DesignSoundEffect(Layers, 0.0, 60, 1, false, 2, E, Problem));
	CHECK_HAS(Problem, "More pulse layers sound at once (at 0 s) than the song's 2 pulse channels");
	CHECK(DesignSoundEffect(Layers, 0.0, 60, 1, false, 4, E, Problem) && E.layers.size() == 3);	// with the MMC5's
	Layers.resize(1);
	Layers[0].Seconds = 5.0;
	CHECK(!DesignSoundEffect(Layers, 0.0, 60, 1, false, 2, E, Problem));
	CHECK_HAS(Problem, "A layer can last at most 4.2 s; layer 1 lasts 5 s.");
	Layers[0].Seconds = 0.2;	// a whole length longer than its layers: silence after them
	CHECK(DesignSoundEffect(Layers, 1.0, 60, 1, true, 2, E, Problem) && E.length() == 60);
	Layers[0].Echo = true;		// an echo on the other pulse, softer
	CHECK(DesignSoundEffect(Layers, 0.0, 60, 1, false, 2, E, Problem) && E.layers.size() == 2 && E.layers[1].start == 6);
	CHECK(E.layers[1].volume[0] < E.layers[0].volume[0]);

	// The tool: a bird flying off, described in words
	FakeSong Song;
	ToolSet Tools(Song);
	ToolResult Result = Tools.Call("make_sound_effect", Parse("{\"name\": \"SFX Bird flies off\", \"layers\": ["
		"{\"channel\": \"noise\", \"seconds\": 1.1, \"pitch\": 7, \"pitch_to\": 5, \"envelope\": \"hit\", \"repeats\": 7, \"gap\": 0.3, "
		"\"volume\": 10, \"volume_to\": 2},"
		"{\"channel\": \"pulse\", \"seconds\": 0.12, \"pitch\": \"D-6\", \"pitch_to\": \"A-6\", \"glide\": \"fast\", \"envelope\": \"hit\", "
		"\"volume\": \"loud\"}]}"));
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "Made track 2 \"SFX Bird flies off\": a sound in 2 layers, 1.10 s long, on 2A03 Noise with \"SFX Bird flies off\" "
		"(03), 2A03 Pulse 1 with \"SFX Bird flies off (pulse 1)\" (04).");
	CHECK_HAS(Result.Text, "1. noise from 0 s for 1.1 s: 7 hits, noise pitch 7 to 5, volume 10 fading to 2");
	CHECK_HAS(Result.Text, "2. pulse from 0 s for 0.12 s: a hit, D-6 to A-6 (quickly), volume 13, 50% duty");
	CHECK_HAS(Result.Text, "halts at its end (C00)");
	CHECK(Song.Rows[3][0].HasNote() && Song.Rows[3][0].Pitch() == 7 && Song.Rows[0][0].Pitch() == 74);
	CHECK(EffectAt(Song.Rows[3][66], EFF_HALT) == 0 || EffectAt(Song.Rows[0][66], EFF_HALT) == 0);
	// Ambience loops: B00 on its last row, no halt
	Tools.Call("select_track", Parse("{\"track\": 1}"));
	Result = Tools.Call("make_sound_effect", Parse("{\"kind\": \"heavy wind\"}"));
	CHECK_HAS(Result.Text, "Made track 3 \"SFX Wind\": the wind sound (a heavy wind blowing in gusts (loops, for ambience)), 4.00 s long");
	CHECK_HAS(Result.Text, "and loops: B00 on its last row takes it back to its start");
	CHECK_HAS(Result.Text, "with the GameMaker code that plays snd_sfx_wind (audio_stop_sound ends it)");
	CHECK(EffectAt(Song.Rows[3][239], EFF_JUMP) == 0 && Song.Rows[3][0].HasNote());
	bool Halts = false;
	for (size_t c = 0; c < Song.Rows.size(); ++c)
		for (size_t r = 0; r < Song.Rows[c].size(); ++r)
			Halts = Halts || EffectAt(Song.Rows[c][r], EFF_HALT) >= 0 || Song.Rows[c][r].Note == NOTE_HALT;
	CHECK(!Halts);
	CHECK_HAS(Tools.Call("export_for_game", Parse("{\"tracks\": [\"SFX Wind\"]}")).Text, "- SFX Wind.ogg: 4 s");
	CHECK_HAS(Tools.Call("export_for_game", Parse("{\"tracks\": [\"SFX Wind\"]}")).Text, "loops as a whole");
	// ...unless asked to play once; a loop too long is shortened to the most a loop lasts
	Tools.Call("select_track", Parse("{\"track\": 1}"));
	CHECK_HAS(Tools.Call("make_sound_effect", Parse("{\"kind\": \"wind\", \"loop\": false}")).Text, "halts at its end (C00)");
	Tools.Call("select_track", Parse("{\"track\": 1}"));
	Result = Tools.Call("make_sound_effect", Parse("{\"kind\": \"rain\", \"length_percent\": 200}"));
	CHECK_HAS(Result.Text, "A sound can last at most 4.2 s as a loop, so it was made shorter to fit.");
	CHECK_HAS(Result.Text, "4.17 s long");
	CHECK_HAS(Result.Text, "104% of the length");
	Tools.Call("select_track", Parse("{\"track\": 1}"));
	CHECK_HAS(Tools.Call("make_sound_effect", Parse("{\"kind\": \"thunder\", \"length_percent\": 400}")).Text,
		"A sound can last at most 4.2 s, so it was made shorter to fit.");
	// A designed sound that loops, as long as asked
	Tools.Call("select_track", Parse("{\"track\": 1}"));
	Result = Tools.Call("make_sound_effect", Parse("{\"name\": \"Clock\", \"loop\": true, \"seconds\": 1, \"layers\": ["
		"{\"channel\": \"noise\", \"seconds\": 0.05, \"pitch\": \"bright\", \"envelope\": \"hit\", \"duty\": \"metallic\"},"
		"{\"channel\": \"noise\", \"start\": 0.5, \"seconds\": 0.05, \"pitch\": 10, \"envelope\": \"hit\", \"duty\": \"metallic\"}]}"));
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "1.00 s long, on 2A03 Noise with \"Clock\"");
	CHECK_HAS(Result.Text, "noise pitch 12, volume 12, metallic");
	CHECK(EffectAt(Song.Rows[3][59], EFF_JUMP) == 0);
	// Mistakes
	CHECK_HAS(Tools.Call("make_sound_effect", Parse("{\"kind\": \"wind\", \"layers\": [{\"channel\": \"noise\"}]}")).Text,
		"Give one of kind, recording and layers.");
	CHECK_HAS(Tools.Call("make_sound_effect", Parse("{\"layers\": [{\"channel\": \"flute\"}]}")).Text,
		"Layer 1: channel should be pulse, triangle or noise.");
	CHECK_HAS(Tools.Call("make_sound_effect", Parse("{\"layers\": [{\"channel\": \"pulse\"}, {\"channel\": \"pulse\", \"pitch\": \"H-9\"}]}")).Text,
		"Layer 2: pitch: a note like \"C-6\"");
	CHECK_HAS(Tools.Call("make_sound_effect", Parse("{\"layers\": [{\"channel\": \"noise\", \"pitch\": 20}]}")).Text,
		"Layer 1: pitch on the noise: 0-15");
	CHECK_HAS(Tools.Call("make_sound_effect", Parse("{\"layers\": [{\"channel\": \"noise\", \"echo\": true}]}")).Text,
		"Layer 1: echo is for a pulse");
	CHECK_HAS(Tools.Call("make_sound_effect", Parse("{\"layers\": [{\"channel\": \"noise\", \"volume\": \"thunderous\"}]}")).Text,
		"volume should be a number from 1 to 15 or one of silent, soft, quiet, medium, loud, full, loudest.");
	CHECK_HAS(Tools.Call("make_sound_effect", Parse("{\"layers\": [{\"channel\": \"noise\"}, {\"channel\": \"noise\"}]}")).Text,
		"Two noise layers sound at once");
	CHECK_HAS(Tools.Call("make_sound_effect", Parse("{\"layers\": []}")).Text, "layers should be a list of layers");
	CHECK_HAS(Tools.Call("make_sound_effect", Parse("{\"loop\": true, \"layers\": [{\"channel\": \"noise\", \"seconds\": 4.2}]}")).Text,
		"A layer can last at most 4.18 s in a sound that loops");
}

ChannelInfo MakeChannel(const char *Name, const char *Chip, int Kind)
{
	ChannelInfo C;
	C.Name = Name;
	C.Chip = Chip;
	C.Kind = Kind;
	return C;
}

void TestEffects()
{
	const ChannelInfo Pulse = MakeChannel("Pulse 1", "2A03", KIND_PULSE), Vrc6 = MakeChannel("Pulse 1", "VRC6", KIND_PULSE);
	const ChannelInfo Triangle = MakeChannel("Triangle", "2A03", KIND_TRIANGLE), Noise = MakeChannel("Noise", "2A03", KIND_NOISE);
	const ChannelInfo Dpcm = MakeChannel("DPCM", "2A03", KIND_DPCM), Fds = MakeChannel("FDS", "FDS", KIND_OTHER);
	const ChannelInfo Saw = MakeChannel("Sawtooth", "VRC6", KIND_SAWTOOTH), N163 = MakeChannel("Namco 1", "N163", KIND_OTHER);
	int Effect = 0, Param = 0;
	std::string Why;
	// What FamiTracker 0.4.6 plays where
	CHECK(ParseChannelEffect("Q31", Pulse, 6, Effect, Param, Why) && Effect == EFF_SLIDE_UP && Param == 0x31);
	CHECK(ParseChannelEffect(" q 3 1 ", Pulse, 6, Effect, Param, Why) && Effect == EFF_SLIDE_UP);
	CHECK(ParseChannelEffect("H71", Pulse, 6, Effect, Param, Why) && Effect == EFF_SWEEPUP);
	CHECK(ParseChannelEffect("H10", Fds, 6, Effect, Param, Why) && Effect == EFF_FDS_MOD_DEPTH && Param == 0x10);
	CHECK(ParseChannelEffect("I0F", Fds, 6, Effect, Param, Why) && Effect == EFF_FDS_MOD_SPEED_HI);
	CHECK(ParseChannelEffect("J80", Fds, 6, Effect, Param, Why) && Effect == EFF_FDS_MOD_SPEED_LO);
	CHECK(ParseChannelEffect("V03", Pulse, 6, Effect, Param, Why) && ParseChannelEffect("V07", Vrc6, 6, Effect, Param, Why));
	CHECK(ParseChannelEffect("V01", Noise, 6, Effect, Param, Why) && ParseChannelEffect("V0F", N163, 6, Effect, Param, Why));
	CHECK(ParseChannelEffect("Z00", Dpcm, 6, Effect, Param, Why) && Effect == EFF_DAC);
	CHECK(ParseChannelEffect("G05", Pulse, 6, Effect, Param, Why) && ParseChannelEffect("S02", Dpcm, 6, Effect, Param, Why));
	CHECK(ParseChannelEffect("047", Noise, 6, Effect, Param, Why) && ParseChannelEffect("443", Triangle, 6, Effect, Param, Why));
	// What it doesn't, and what to use instead
	CHECK(!ParseChannelEffect("L05", Pulse, 6, Effect, Param, Why));
	CHECK_HAS(Why, "L05 (release the note after xx ticks) comes from later FamiTrackers (0CC-FamiTracker), not 0.4.6. End the note "
		"with a release (===)");
	CHECK(!ParseChannelEffect("O02", Pulse, 6, Effect, Param, Why) && Contains(Why, "hold the off-beat notes back with Gxx"));
	CHECK(!ParseChannelEffect("K10", Pulse, 6, Effect, Param, Why) && Contains(Why, "K10 isn't one of FamiTracker 0.4.6's effects"));
	CHECK(!ParseChannelEffect("844", Pulse, 6, Effect, Param, Why) && Contains(Why, "the NES plays in mono"));
	CHECK(!ParseChannelEffect("E0A", Pulse, 6, Effect, Param, Why) && Contains(Why, "give the note a volume (1-15) instead"));
	CHECK(!ParseChannelEffect("F06", Pulse, 6, Effect, Param, Why) && Contains(Why, "set_tempo"));
	CHECK(!ParseChannelEffect("B00", Pulse, 6, Effect, Param, Why) && Contains(Why, "set_loop"));
	CHECK(!ParseChannelEffect("H71", Vrc6, 6, Effect, Param, Why));
	CHECK_HAS(Why, "Hxy and Ixy start the sweep of the 2A03's pulses, which VRC6 Pulse 1 doesn't have");
	CHECK(!ParseChannelEffect("H81", Pulse, 6, Effect, Param, Why) && Contains(Why, "x is how often the sweep moves (0-7)"));
	CHECK(!ParseChannelEffect("I70", Pulse, 6, Effect, Param, Why));
	CHECK(!ParseChannelEffect("J10", Pulse, 6, Effect, Param, Why) && Contains(Why, "the FDS's modulator"));
	CHECK(!ParseChannelEffect("V04", Pulse, 6, Effect, Param, Why) && Contains(Why, "2A03 Pulse 1 takes V00-V03 (12.5%, 25%, 50%, 75%)."));
	CHECK(!ParseChannelEffect("V00", Triangle, 6, Effect, Param, Why) && Contains(Why, "2A03 Triangle has none to change"));
	CHECK(!ParseChannelEffect("V01", Saw, 6, Effect, Param, Why));
	CHECK(!ParseChannelEffect("744", Triangle, 6, Effect, Param, Why) && Contains(Why, "The triangle has no volume"));
	CHECK(!ParseChannelEffect("A20", Triangle, 6, Effect, Param, Why));
	CHECK(!ParseChannelEffect("A22", Pulse, 6, Effect, Param, Why) && Contains(Why, "one way at a time"));
	CHECK(!ParseChannelEffect("047", Dpcm, 6, Effect, Param, Why) && Contains(Why, "pitch effects don't reach it"));
	CHECK(!ParseChannelEffect("Z00", Pulse, 6, Effect, Param, Why) && Contains(Why, "the DPCM channel's"));
	CHECK(!ParseChannelEffect("Z80", Dpcm, 6, Effect, Param, Why));
	CHECK(!ParseChannelEffect("H40", Fds, 6, Effect, Param, Why) && Contains(Why, "H00 to H3F"));
	CHECK(!ParseChannelEffect("G06", Pulse, 6, Effect, Param, Why));
	CHECK_HAS(Why, "at speed 6 a row lasts 6, so G06 would push it past its row. Use G01-G05");
	CHECK(!ParseChannelEffect("Q30", Pulse, 6, Effect, Param, Why) && Contains(Why, "Q30 slides nowhere"));
	CHECK(!ParseChannelEffect("Q3", Pulse, 6, Effect, Param, Why) && Contains(Why, "a letter or digit and two hex digits"));
	CHECK(!ParseChannelEffect("QZZ", Pulse, 6, Effect, Param, Why));

	// The reference: the overview, an effect, a technique, what a channel plays
	std::string Text;
	CHECK(EffectsText("", "", 0, Text) && Contains(Text, "FamiTracker 0.4.6's effects") && Contains(Text, "Techniques: echo, chorus"));
	CHECK(EffectsText("q", "", 0, Text) && Contains(Text, "Qxy note slide up: Slides from the note up y semitones"));
	CHECK(EffectsText("Hxy", "", 0, Text) && Contains(Text, "Hxy hardware sweep up") && Contains(Text, "On the FDS channel, Hxx is its modulator"));
	CHECK(EffectsText("H", "", &Fds, Text) && Contains(Text, "Hxx FDS modulation depth") && !Contains(Text, "sweep"));
	CHECK(EffectsText("", "swing", 0, Text) && Contains(Text, "Swing: Alternate the speed every half beat") && Contains(Text, "set_swing"));
	CHECK(EffectsText("", "jazz chords", 0, Text) && Contains(Text, "Lush chords: For chords of four or more notes"));
	CHECK(EffectsText("", "mixing", 0, Text) && Contains(Text, "Quiet mixing: Pulses well under full volume"));
	CHECK(EffectsText("", "shuffle", 0, Text) && Contains(Text, "Swing:"));
	CHECK(EffectsText("", "Triangle Kick", 0, Text) && Contains(Text, "Triangle kick:"));
	CHECK(!EffectsText("", "moonwalk", 0, Text) && Contains(Text, "No technique \"moonwalk\". The techniques: echo"));
	CHECK(!EffectsText("L", "", 0, Text) && Contains(Text, "comes from later FamiTrackers"));
	CHECK(EffectsText("F", "", 0, Text) && Contains(Text, "set_tempo"));
	CHECK(EffectsText("", "", &Triangle, Text));
	CHECK_HAS(Text, "2A03 Triangle plays 0xy, 1xx, 2xx, 3xx, 4xy, Gxx, Pxx, Qxy, Rxy and Sxx. The triangle has no volume");
	CHECK(EffectsText("", "", &Dpcm, Text) && Contains(Text, "2A03 DPCM plays Gxx, Sxx, Wxx, Xxx, Yxx and Zxx."));
	CHECK(EffectsText("", "", &Pulse, Text) && Contains(Text, "Hxy, Ixy") && Contains(Text, "Vxx: V00-V03."));
	CHECK(EffectsText("", "", &Fds, Text) && Contains(Text, "Hxx, ") && Contains(Text, "Ixx and Jxx.") && !Contains(Text, "Hxy"));
	for (size_t i = 0; i < EffectReference().size(); ++i)
		Check(std::string(EffectReference()[i].Uses).size() > 20, EffectReference()[i].Form, __LINE__);

	// In write_notes: refused with what to use, and the tool
	FakeSong Song;
	Song.Info.DirectChanges = true;
	ToolSet Tools(Song);
	ToolResult Result = Tools.Call("write_notes", Parse("{\"channel\": \"Pulse 1\", \"notes\": [{\"bar\": 1, \"pitch\": \"C-4\", \"beats\": 2, "
		"\"effects\": [\"L05\"]}]}"));
	CHECK(Result.IsError);
	CHECK_HAS(Result.Text, "Note 1: L05 (release the note after xx ticks) comes from later FamiTrackers");
	CHECK_HAS(Result.Text, "(the effects tool explains them all.)");
	Result = Tools.Call("write_notes", Parse("{\"channel\": \"VRC6 Pulse 1\", \"notes\": [{\"bar\": 1, \"pitch\": \"C-4\", \"beats\": 2, "
		"\"effects\": [\"I71\"]}]}"));
	CHECK(Result.IsError && Contains(Result.Text, "which VRC6 Pulse 1 doesn't have"));
	Result = Tools.Call("write_notes", Parse("{\"channel\": \"Pulse 1\", \"instrument\": \"Square\", \"notes\": [{\"bar\": 1, "
		"\"pitch\": \"C-4\", \"beats\": 2, \"effects\": [\"I71\", \"V01\"]}]}"));
	CHECK(!Result.IsError && HasEffectAt(Song, 0, 0, EFF_SWEEPDOWN, 0x71) && HasEffectAt(Song, 0, 0, EFF_DUTY_CYCLE, 1));
	CHECK_HAS(Tools.Call("effects", Parse("{}")).Text, "FamiTracker 0.4.6's effects");
	CHECK_HAS(Tools.Call("effects", Parse("{\"channel\": \"Triangle\"}")).Text, "2A03 Triangle plays 0xy");
	CHECK_HAS(Tools.Call("effects", Parse("{\"effect\": \"V\", \"channel\": \"Noise\"}")).Text, "Vxx timbre");
	CHECK(Tools.Call("effects", Parse("{\"technique\": \"moonwalk\"}")).IsError);
}

void TestMmc5()
{
	// An MMC5 song: two more pulses, which play the 2A03's instruments and presets
	FakeSong Song;
	Song.Info.DirectChanges = true;
	ToolSet Tools(Song);
	ToolResult Result = Tools.Call("set_expansion", Parse("{\"chip\": \"mmc5\"}"));
	CHECK_HAS(Result.Text, "The song uses the MMC5 now: 2 more channels (MMC5 Pulse 1, MMC5 Pulse 2) besides the 2A03's five. Two more "
		"pulses; the 2A03 presets work on them.");
	CHECK(Song.Info.Channels.size() == 7 && Song.Info.Channels[5].Kind == KIND_PULSE && Song.Info.Channels[6].Chip == "MMC5");
	CHECK_HAS(Tools.Call("get_song", Parse("{}")).Text, "MMC5 Pulse 2");

	Result = Tools.Call("write_notes", Parse("{\"channel\": \"MMC5 Pulse 1\", \"instrument\": \"soft lead\", \"notes\": ["
		"{\"bar\": 1, \"pitch\": \"C-4\", \"beats\": 2}, {\"bar\": 1, \"beat\": 3, \"pitch\": \"E-4\", \"beats\": 2},"
		"{\"bar\": 2, \"pitch\": \"G-4\", \"beats\": 4}]}"));
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "Added the instrument \"Soft lead\" (03) (2A03) from the preset \"soft lead\"");
	CHECK(Song.Rows[5][0].Instrument == 3 && Song.Rows[5][16].Pitch() == 55);
	// The song's 2A03 instruments play on it too, but not the VRC6's
	CHECK(!Tools.Call("write_notes", Parse("{\"channel\": \"MMC5 Pulse 2\", \"instrument\": \"Square\", \"notes\": ["
		"{\"bar\": 3, \"pitch\": \"C-3\", \"beats\": 4}]}")).IsError);
	CHECK(Song.Rows[6][32].Instrument == 2);
	CHECK_HAS(Tools.Call("write_notes", Parse("{\"channel\": \"MMC5 Pulse 2\", \"instrument\": \"Lead\", \"notes\": ["
		"{\"bar\": 3, \"pitch\": \"C-3\", \"beats\": 4}]}")).Text, "\"Lead\" is a VRC6 instrument; MMC5 Pulse 2 needs a 2A03 one");
	CHECK_HAS(Tools.Call("add_instrument", Parse("{\"preset\": \"pluck\", \"chip\": \"MMC5\"}")).Text, "(2A03 instrument");
	CHECK_HAS(Tools.Call("add_instrument", Parse("{\"preset\": \"bell\", \"channel\": \"MMC5 Pulse 2\"}")).Text, "(2A03 instrument");

	// Chords, an echo, checks and versions use its pulses like the 2A03's
	Result = Tools.Call("add_chords_and_bass", Parse("{\"melody_channel\": \"MMC5 Pulse 1\", \"to_bar\": 2}"));
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "Chords:");
	Result = Tools.Call("add_echo", Parse("{\"channel\": \"MMC5 Pulse 1\", \"to_bar\": 2}"));
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "an echo of MMC5 Pulse 1 on");
	Song.Put(6, 48, "G#0", 2);
	CHECK_HAS(Tools.Call("check_song", Parse("{}")).Text, "MMC5 Pulse 2 bar 4 beat 1 is below A-0, the lowest note its pulse can play");
	CHECK(!Tools.Call("make_version", Parse("{\"version\": \"night\"}")).IsError);
	Result = Tools.Call("make_jingle", Parse("{\"kind\": \"coin\"}"));
	CHECK(Result.IsError);	// not a jingle kind; the real ones work
	CHECK(!Tools.Call("make_jingle", Parse("{\"kind\": \"item\"}")).IsError);
}

void TestTiming2()
{
	// 6 ticks a row at speed 6, tempo 150; F03 halves it; tempo 120 alternates 8 and 7
	std::vector<std::vector<Cell> > C(1, std::vector<Cell>(32));
	std::vector<int> Columns(1, 1);
	SongFlow F = FollowSong(C, Columns, 16, 2);
	int End = 0;
	std::vector<TimedRow> Rows = TimeRows(C, Columns, F, 16, 6, 150, 32, 60, 1, End);
	CHECK(Rows.size() == 32 && Rows[1].Tick == 6 && Rows[31].Tick == 186 && End == 192);
	C[0][4].Effect[0] = EFF_SPEED;
	C[0][4].Param[0] = 3;
	Rows = TimeRows(C, Columns, F, 16, 6, 150, 32, 60, 1, End);
	CHECK(Rows[4].Tick == 24 && Rows[5].Tick == 27 && End == 24 + 28 * 3);
	C[0][4].Param[0] = 120;
	Rows = TimeRows(C, Columns, F, 16, 6, 150, 32, 60, 1, End);
	CHECK(Rows[5].Tick - Rows[4].Tick == 8 && Rows[6].Tick - Rows[5].Tick == 7 && Rows[7].Tick - Rows[6].Tick == 8);
	// Passes through the loop
	C[0][4].Effect[0] = EFF_NONE;
	C[0][31].Effect[0] = EFF_JUMP;
	C[0][31].Param[0] = 1;
	F = FollowSong(C, Columns, 16, 2);
	Rows = TimeRows(C, Columns, F, 16, 6, 150, 32, 60, 3, End);
	CHECK(Rows.size() == 16 + 3 * 16 && End == 64 * 6);
	CHECK(Rows[16].LoopStart && Rows[16].Row == 16 && Rows[16].Pass == 0 && Rows[32].LoopStart && Rows[32].Pass == 1 && Rows[48].Pass == 2);
	CHECK(!Rows[0].LoopStart && Rows[15].Pass == 0);
	// A song that halts plays once
	C[0][31].Effect[0] = EFF_HALT;
	F = FollowSong(C, Columns, 16, 2);
	Rows = TimeRows(C, Columns, F, 16, 6, 150, 32, 60, 3, End);
	CHECK(Rows.size() == 32 && End == 192);
}

void TestLoops()
{
	FakeSong Song;		// 8 bars, 4 a frame, 0.1 s a row
	Song.Info.DirectChanges = true;
	// The melody, ending on G with vibrato still on
	Song.Put(5, 0, "C-4", 0);
	Song.Put(5, 16, "E-4");
	Song.Put(5, 32, "G-4");
	Song.Put(5, 64, "E-4");
	Song.Put(5, 96, "D-4");
	Song.Put(5, 112, "G-4");
	Song.Rows[5][112].Effect[0] = EFF_VIBRATO;
	Song.Rows[5][112].Param[0] = 0x43;
	// The bass: C, then G at the end
	Song.Put(2, 0, "C-2", 1);
	Song.Put(2, 112, "G-1");
	// A pad on Pulse 2 that starts in bar 2 and sounds over the end
	Song.Put(1, 16, "C-3", 2);
	Song.Put(1, 112, "B-2");

	ToolSet Tools(Song);
	std::string Text = Tools.Call("check_loop", Parse("{}")).Text;
	CHECK_HAS(Text, "The whole song loops: bars 1-8, 12.8 s, then back to bar 1.");
	CHECK_HAS(Text, "2A03 Pulse 2: B-2 from the loop's end keeps sounding into bar 1 beat 1 on repeat (until bar 2 beat 1), where the first time nothing sounds.");
	CHECK_HAS(Text, "VRC6 Pulse 1: vibrato 443 is still on at the loop's end");
	CHECK_LACKS(Text, "2A03 Triangle:");
	CHECK_HAS(Text, "In C major, the loop ends on G (V) and starts on C (I): the end leads back into the start.");
	CHECK_HAS(Text, "The melody (VRC6 Pulse 1) goes from G-4 at the end to C-4 at the start: a leap of 7 semitones.");

	// An intro: bars 1-4 once, then bars 5-8
	CHECK(Tools.Call("set_loop", Parse("{\"from_bar\": 3}")).IsError);
	Text = Tools.Call("set_loop", Parse("{\"from_bar\": 5}")).Text;
	CHECK_HAS(Text, "bars 1-4 play once as an intro, then bars 5-8 loop (B01 on the last row of bar 8)");
	CHECK(HasEffectAt(Song, 0, 127, EFF_JUMP, 1));
	Text = Tools.Call("check_loop", Parse("{}")).Text;
	CHECK_HAS(Text, "Bars 1-4 play once as an intro (6.4 s), then bars 5-8 loop (6.4 s): after bar 8 it goes back to bar 5.");
	// The pad's B-2 and the bass's G now hang into bar 5 on repeat, where the intro's notes sound the first time
	CHECK_HAS(Text, "2A03 Pulse 2: B-2 from the loop's end keeps sounding into bar 5 beat 1 on repeat (until bar 8 beat 1), where the first time C-3 from the intro does.");
	CHECK_HAS(Text, "2A03 Triangle: G-1 from the loop's end keeps sounding into bar 5 beat 1 on repeat (until bar 8 beat 1), where the first time C-2 from the intro does.");
	CHECK_HAS(Tools.Call("set_loop", Parse("{\"from_bar\": 5}")).Text, "already loops");
	// Back to the whole song: the jump goes
	Text = Tools.Call("set_loop", Parse("{}")).Text;
	CHECK_HAS(Text, "the song loops from bar 1 after bar 8; 1 jump or halt that ended it elsewhere went");
	CHECK(!HasEffectAt(Song, 0, 127, EFF_JUMP, 1));

	// A song that stops
	Song.Rows[0][63].Effect[0] = EFF_HALT;
	CHECK_HAS(Tools.Call("check_loop", Parse("{}")).Text, "The song plays once and stops after bar 4 (a halt, Cxx): 6.4 s.");
	// Ending the loop earlier: the bars after it don't play
	Text = Tools.Call("set_loop", Parse("{\"to_bar\": 6}")).Text;
	CHECK_HAS(Text, "B00 on the last row of bar 6");
	CHECK_HAS(Text, "Bars after 6 don't play now.");
	CHECK(HasEffectAt(Song, 0, 95, EFF_JUMP, 0) && !HasEffectAt(Song, 0, 63, EFF_HALT, 0));
	// Without direct changes it says how
	Song.Info.DirectChanges = false;
	CHECK_HAS(Tools.Call("set_loop", Parse("{\"from_bar\": 5}")).Text, "B01 effect on the last row of bar 8 (frame 01, row 3F)");

	// A song moved a frame later with copy_bars, from its end, to make room
	// for an intro: the song's old jump stays where it was (the copies that
	// don't play yet are named), until set_loop takes it away
	{
		FakeSong Moved(3);	// 12 bars
		Moved.Info.DirectChanges = true;
		for (int Bar = 0; Bar < 12; ++Bar)
			Moved.Put(5, Bar * 16, Bar % 2 == 0 ? "C-4" : "G-4", 0);
		Moved.Rows[1][191].Effect[1] = EFF_JUMP;	// after bar 12, back to bar 5
		Moved.Rows[1][191].Param[1] = 1;
		ToolSet Again(Moved);
		Text = Again.Call("copy_bars", Parse("{\"from_bar\": 9, \"to_bar\": 12, \"at_bar\": 13}")).Text;
		CHECK_HAS(Text, "Bars 13-16 don't play yet: the song goes back to bar 5 after bar 12, before them. set_loop with to_bar 16 "
			"takes the loop on through them.");
		CHECK(HasEffectAt(Moved, 1, 191, EFF_JUMP, 1) && !HasEffectAt(Moved, 1, 255, EFF_JUMP, 1));
		Text = Again.Call("copy_bars", Parse("{\"from_bar\": 5, \"to_bar\": 8, \"at_bar\": 9}")).Text;
		CHECK(!Contains(Text, "play yet") && Moved.Rows[5][128].Pitch() == Moved.Rows[5][64].Pitch());
		CHECK(!Again.Call("copy_bars", Parse("{\"from_bar\": 1, \"to_bar\": 4, \"at_bar\": 5}")).IsError);
		CHECK(HasEffectAt(Moved, 1, 191, EFF_JUMP, 1));	// the song's own jump, still
		Text = Again.Call("set_loop", Parse("{\"from_bar\": 9, \"to_bar\": 16}")).Text;
		CHECK_HAS(Text, "bars 1-8 play once as an intro, then bars 9-16 loop (B02 on the last row of bar 16); 1 jump or halt that "
			"ended it elsewhere went");
		CHECK_LACKS(Text, "FamiTracker's song");
		CHECK(!HasEffectAt(Moved, 1, 191, EFF_JUMP, 1) && HasEffectAt(Moved, 0, 255, EFF_JUMP, 2));
		CHECK_HAS(Again.Call("check_loop", Parse("{}")).Text, "after bar 16 it goes back to bar 9.");
		CHECK_HAS(Again.Call("set_loop", Parse("{\"from_bar\": 9, \"to_bar\": 16}")).Text, "already loops");
		// A halt before copies: they don't play either
		Moved.Rows[3][63].Effect[0] = EFF_HALT;
		CHECK_HAS(Again.Call("copy_bars", Parse("{\"from_bar\": 1, \"at_bar\": 17}")).Text,
			"Bar 17 doesn't play yet: the song stops after bar 4 (a halt, Cxx), before it. set_loop with to_bar 17 makes it "
			"play on through it.");
	}
	{
		// set_loop says so when FamiTracker's song doesn't loop as asked: here a skip ends bar 16 early
		FakeSong Skip(4);
		Skip.Info.DirectChanges = true;
		Skip.Put(5, 0, "C-4", 0);
		Skip.Rows[3][240].Effect[0] = EFF_SKIP;
		CHECK_HAS(Run(Skip, "set_loop", "{\"from_bar\": 5, \"to_bar\": 16}").Text,
			"FamiTracker's song doesn't loop that way: after bar 16 beat 1 it goes back to bar 1. A skip (Dxx) at bar 16 beat 1 "
			"ends that frame before the jump; set_loop leaves skips alone. check_loop shows how it plays.");
	}

	// Changes keep the song's speed and jumps where they are, once
	{
		FakeSong Steer(2);
		Steer.Info.DirectChanges = true;
		Steer.Put(5, 0, "C-4", 0);
		Steer.Put(5, 32, "E-4");
		Steer.Rows[5][0].Effect[0] = EFF_SPEED;
		Steer.Rows[5][0].Param[0] = 5;
		Steer.Rows[5][127].Effect[0] = EFF_JUMP;
		Steer.Rows[5][127].Param[0] = 0;
		CHECK(!Run(Steer, "transpose", "{\"semitones\": 2, \"channels\": [\"VRC6 Pulse 1\"]}").IsError);
		CHECK(Steer.Rows[5][0].Pitch() == Steer.Rows[5][32].Pitch() - 4 && Steer.Rows[5][0].Effect[1] == EFF_NONE);
		CHECK(HasEffectAt(Steer, 5, 0, EFF_SPEED, 5) && HasEffectAt(Steer, 5, 127, EFF_JUMP, 0));
		// A clip placed there: its effects go around the song's, and a speed it has too goes in once
		FakeSong::Clip Riff;
		Riff.Name = "Riff";
		Riff.Id = Steer.NextClipId++;
		Riff.Sources.push_back(5);
		Riff.Columns.push_back(std::vector<Cell>(128));
		Riff.Columns[0][0].SetPitch(50);
		Riff.Columns[0][0].Effect[0] = EFF_SPEED;
		Riff.Columns[0][0].Param[0] = 5;
		Riff.Columns[0][127].SetPitch(52);
		Riff.Columns[0][127].Effect[0] = EFF_VIBRATO;
		Riff.Columns[0][127].Param[0] = 0x42;
		Steer.Clips.push_back(Riff);
		CHECK(!Run(Steer, "place_clip", "{\"clip\": \"Riff\", \"channel\": \"VRC6 Pulse 1\", \"bar\": 1}").IsError);
		CHECK(Steer.Rows[5][0].Pitch() == 50 && Steer.Rows[5][0].Effect[0] == EFF_SPEED && Steer.Rows[5][0].Effect[1] == EFF_NONE);
		CHECK(Steer.Rows[5][127].Effect[0] == EFF_JUMP && HasEffectAt(Steer, 5, 127, EFF_VIBRATO, 0x42));
		CHECK(Steer.Rows[5][32].IsEmpty());
	}

	// The song's speed, jumps and halts kept in a cell that changes
	Cell Old, New;
	Old.Effect[0] = EFF_JUMP;
	Old.Param[0] = 2;
	Old.Effect[2] = EFF_JUMP;		// twice (an older write): once is enough
	Old.Param[2] = 2;
	New.SetPitch(48);
	New.Effect[0] = EFF_VIBRATO;
	New.Param[0] = 0x31;
	CHECK(KeepSongEffects(Old, New, 2));
	CHECK(New.Effect[0] == EFF_JUMP && New.Param[0] == 2 && New.Effect[1] == EFF_VIBRATO && New.Effect[2] == EFF_NONE);
	New.Effect[1] = EFF_VOLUME_SLIDE;	// no room for the vibrato now
	New.Effect[0] = EFF_VIBRATO;
	CHECK(!KeepSongEffects(Old, New, 2) && New.Effect[0] == EFF_JUMP && New.Effect[1] == EFF_VOLUME_SLIDE);
	New = Cell();
	New.Effect[1] = EFF_JUMP;		// the same one, in another column
	New.Param[1] = 2;
	CHECK(KeepSongEffects(Old, New, 2) && New.Effect[0] == EFF_NONE && New.Effect[1] == EFF_JUMP);
}

// fix_loop: what carries over the loop's seam, written so the repeats start
// as the first time does; drum fills counted from where the loop starts
void TestFixLoop()
{
	FakeSong Song;		// 8 bars, 4 a frame
	Song.Info.DirectChanges = true;
	// The melody from bar 5, its last note fading (A02) into the loop's end
	Song.Put(5, 64, "C-4", 0, 9);
	Song.Put(5, 80, "E-4");
	Song.Put(5, 96, "G-4");
	Song.Put(5, 112, "E-4");
	Song.Rows[5][120].Effect[0] = EFF_VOLUME_SLIDE;
	Song.Rows[5][120].Param[0] = 0x02;
	// Its echo a beat behind from bar 6, detuned, its last note hanging over the loop's end
	Song.Put(1, 84, "C-4", 2, 3);
	Song.Rows[1][84].Effect[0] = EFF_PITCH;
	Song.Rows[1][84].Param[0] = 0x81;
	Song.Put(1, 100, "G-4");
	Song.Put(1, 116, "E-4");
	// A second line: "Square" at 10 in the intro, "Bass" at 5 at the loop's end; its first note in the loop sets neither
	Song.Put(6, 0, "G-3", 2, 10);
	Song.End(6, 16);
	Song.Put(6, 72, "A-3");
	Song.End(6, 80);
	Song.Put(6, 104, "B-3", 1, 5);
	Song.End(6, 112);
	// The speed drops to 5 in bar 7: the repeats would start slower
	Song.Rows[0][96].Effect[0] = EFF_SPEED;
	Song.Rows[0][96].Param[0] = 5;
	// A sampled kick in the loop's last bar plays out by itself: nothing to halt (a halt would reset the DPCM level)
	Song.Put(4, 120, "C-3", 1);
	ToolSet Tools(Song);
	CHECK(!Tools.Call("set_loop", Parse("{\"from_bar\": 5}")).IsError);

	std::string Text = Tools.Call("check_loop", Parse("{}")).Text;
	CHECK_LACKS(Text, "DPCM");
	CHECK_HAS(Text, "2A03 Pulse 2: E-4 from the loop's end keeps sounding into bar 5 beat 1 on repeat (until bar 6 beat 2), where the "
		"first time nothing sounds.");
	CHECK_HAS(Text, "VRC6 Pulse 1: volume slide A02 is still on at the loop's end");
	CHECK_HAS(Text, "VRC6 Pulse 2: the loop's first note (bar 5 beat 3) plays with \"Square\" (02) the first time and \"Bass\" (01) on "
		"repeat; set the instrument on it.");
	CHECK_HAS(Text, "VRC6 Pulse 2: the loop starts at volume 10 the first time and 5 on repeat; set the volume at bar 5 beat 1.");
	CHECK_HAS(Text, "The loop starts at speed 6, tempo 150 the first time and speed 5, tempo 150 on repeat");
	// The echo's first note in the loop sets its own volume and detune, and its last one is halted: those don't matter
	CHECK_LACKS(Text, "2A03 Pulse 2: the loop starts at volume");
	CHECK_LACKS(Text, "2A03 Pulse 2: effect P");
	CHECK_HAS(Text, "fix_loop writes all of that");

	Text = Tools.Call("fix_loop", Parse("{}")).Text;
	CHECK_HAS(Text, "a halt (---) on 2A03 Pulse 2 at bar 5 beat 1, where E-4 from the loop's end hung over");
	CHECK_HAS(Text, "A00 on VRC6 Pulse 1 at bar 5 beat 1");
	CHECK_HAS(Text, "\"Square\" (02) on VRC6 Pulse 2's first note in the loop (bar 5 beat 3)");
	CHECK_HAS(Text, "volume 10 on VRC6 Pulse 2 at bar 5 beat 1");
	CHECK_HAS(Text, "speed 6 (F06) at bar 5 beat 1");
	CHECK_HAS(Text, "The seam is clean now");
	CHECK(Song.Rows[1][64].Note == NOTE_HALT && !Song.Rows[4][64].IsEnd());
	CHECK(HasEffectAt(Song, 5, 64, EFF_VOLUME_SLIDE, 0) && Song.Rows[5][64].Pitch() == Song.Rows[5][80].Pitch() - 4 &&
		Song.Rows[5][64].Volume == 9);
	CHECK(Song.Rows[6][72].Instrument == 2 && Song.Rows[6][64].Volume == 10 && !Song.Rows[6][64].HasNote());
	CHECK(HasEffectAt(Song, 0, 64, EFF_SPEED, 6) && HasEffectAt(Song, 0, 96, EFF_SPEED, 5) && HasEffectAt(Song, 0, 127, EFF_JUMP, 1));
	CHECK_HAS(Tools.Call("check_loop", Parse("{}")).Text, "The seam is clean: every channel starts the loop the same way both times.");
	CHECK_HAS(Tools.Call("fix_loop", Parse("{}")).Text, "The seam is clean already");

	// An intro's note ringing into the loop's start the first time can't be matched by writing there
	{
		FakeSong Ring;
		Ring.Info.DirectChanges = true;
		Ring.Put(0, 0, "C-3", 2, 8);
		Ring.Put(0, 112, "G-3");
		Ring.Rows[0][127].Effect[0] = EFF_JUMP;
		Ring.Rows[0][127].Param[0] = 1;
		ToolSet RingTools(Ring);
		Text = RingTools.Call("fix_loop", Parse("{}")).Text;
		CHECK_HAS(Text, "Nothing at the seam can be fixed by writing at the loop's start:");
		CHECK_HAS(Text, "2A03 Pulse 1: G-3 from the loop's end keeps sounding into bar 5 beat 1 on repeat (until bar 8 beat 1), where the "
			"first time C-3 from the intro does.");
		CHECK(!Ring.Rows[0][64].IsEnd());
		CHECK_LACKS(RingTools.Call("check_loop", Parse("{}")).Text, "fix_loop");
		// A song that stops has no loop to fix; one that isn't to be changed directly is told so
		Ring.Rows[0][127].Effect[0] = EFF_HALT;
		CHECK_HAS(RingTools.Call("fix_loop", Parse("{}")).Text, "it has no loop to fix");
		Ring.Info.DirectChanges = false;
		CHECK_HAS(RingTools.Call("fix_loop", Parse("{}")).Text, "needs direct changes");
	}

	// Drum fills end the phrases that start where the loop does: after a 4-bar intro, every 3 bars ends bar 7 (not bar 6)
	{
		FakeSong Drums;
		Drums.Info.DirectChanges = true;
		for (int Bar = 4; Bar < 8; ++Bar)
			Drums.Put(5, Bar * 16, "C-4", 0);
		Drums.Rows[0][127].Effect[0] = EFF_JUMP;
		Drums.Rows[0][127].Param[0] = 1;
		ToolSet DrumTools(Drums);
		CHECK(!DrumTools.Call("add_drums", Parse("{\"fill_every\": 3, \"crash\": false, \"from_bar\": 5, \"to_bar\": 8}")).IsError);
		bool Fill6 = false, Fill7 = false;
		for (int r = 92; r < 96; ++r)
			Fill6 = Fill6 || (Drums.Rows[3][r].HasNote() && (Drums.Rows[3][r].Pitch() == 0x9 || Drums.Rows[3][r].Pitch() == 0x7 ||
				Drums.Rows[3][r].Pitch() == 0x5));
		for (int r = 108; r < 112; ++r)
			Fill7 = Fill7 || (Drums.Rows[3][r].HasNote() && (Drums.Rows[3][r].Pitch() == 0x9 || Drums.Rows[3][r].Pitch() == 0x7 ||
				Drums.Rows[3][r].Pitch() == 0x5));
		CHECK(Fill7 && !Fill6);
		// Without an intro they count from bar 1, as before
		Drums.Rows[0][127].Effect[0] = EFF_NONE;
		CHECK(!DrumTools.Call("add_drums", Parse("{\"fill_every\": 3, \"crash\": false, \"from_bar\": 5, \"to_bar\": 8}")).IsError);
		Fill6 = false;
		for (int r = 92; r < 96; ++r)
			Fill6 = Fill6 || (Drums.Rows[3][r].HasNote() && (Drums.Rows[3][r].Pitch() == 0x9 || Drums.Rows[3][r].Pitch() == 0x7 ||
				Drums.Rows[3][r].Pitch() == 0x5));
		CHECK(Fill6);
	}
}

// The N163: wave sounds, and where their waves go
void TestWaveSounds()
{
	// Every sound: waves of 16 samples (in tune) of 0-15, at most 16; its wave sequence only plays waves it has
	const std::vector<WaveSound> &All = WaveSounds();
	CHECK(All.size() >= 12u);
	for (size_t i = 0; i < All.size(); ++i) {
		const WaveSound &W = All[i];
		bool Fine = !W.Waves.empty() && W.Waves.size() <= static_cast<size_t>(N163_MAX_WAVES) && !W.Description.empty();
		int Low = 15, High = 0;
		for (size_t w = 0; w < W.Waves.size(); ++w) {
			Fine = Fine && W.Waves[w].size() == static_cast<size_t>(N163_WAVE_SIZE);
			for (size_t k = 0; k < W.Waves[w].size(); ++k) {
				Low = std::min(Low, W.Waves[w][k]);
				High = std::max(High, W.Waves[w][k]);
			}
		}
		Fine = Fine && Low >= 0 && High <= 15 && High - Low >= 12;	// they fill the range: loud enough
		bool Volume = false;
		for (size_t s = 0; s < W.Sequences.size(); ++s) {
			const PresetSequence &Seq = W.Sequences[s];
			Fine = Fine && !Seq.Items.empty() && Seq.Items.size() <= 252u;
			if (Seq.Type == SEQUENCE_VOLUME)
				Volume = true;
			if (Seq.Type == SEQUENCE_DUTY)
				for (size_t k = 0; k < Seq.Items.size(); ++k)
					Fine = Fine && Seq.Items[k] >= 0 && Seq.Items[k] < static_cast<int>(W.Waves.size());
		}
		Check(Fine && Volume, W.Name.c_str(), __LINE__);
		CHECK(FindWaveSound(W.Name) == &W);
	}
	// Asked for by a word, or by a preset's name, which picks the one most like it
	CHECK(FindWaveSound("Bell") != 0 && FindWaveSound("Bell")->Name == "glass bell");
	CHECK(FindWaveSound("soft lead") != 0 && FindWaveSound("soft lead")->Name == "soft flute");
	CHECK(FindWaveSound("e-piano") != 0 && FindWaveSound("e-piano")->Name == "electric piano");
	CHECK(FindWaveSound("kazoo") == 0 && FindWaveSound("") == 0);
	CHECK(FindWaveSound("glass bell")->InstrumentName() == "Glass bell" && !FindWaveSound("glass bell")->Releases());
	CHECK(FindWaveSound("choir")->Releases());
	// The organ shimmers: its wave sequence loops through four waves
	const WaveSound &Organ = *FindWaveSound("organ");
	CHECK(Organ.Waves.size() == 4u && Organ.Waves[0] != Organ.Waves[2]);

	// The chip's memory: 16 samples of registers a channel, waves from the bottom up
	CHECK(N163WaveMemory(1) == 240 && N163WaveMemory(6) == 160 && N163WaveMemory(8) == 128 && N163WaveMemory(12) == 128);
	std::vector<std::pair<int, int> > Used;
	CHECK(FindWavePlace(16, 160, Used) == 0);
	Used.push_back(std::make_pair(0, 16));
	Used.push_back(std::make_pair(32, 32));		// a 32-sample wave someone made in FamiTracker
	CHECK(FindWavePlace(16, 160, Used) == 16);
	Used.push_back(std::make_pair(16, 16));
	CHECK(FindWavePlace(16, 160, Used) == 64);
	CHECK(FindWavePlace(16, 64, Used) == -1);

	// In a song with N163 channels
	FakeSong Song;
	Song.Info.DirectChanges = true;
	Song.Info.Expansion = "N163";
	for (int c = 5; c < 8; ++c) {
		Song.Info.Channels[c].Name = "Namco " + std::to_string(c - 4);
		Song.Info.Channels[c].Chip = "N163";
		Song.Info.Channels[c].Kind = KIND_OTHER;
	}
	Song.Info.Instruments.clear();
	std::string Text = Run(Song, "add_instrument", "{\"preset\": \"glass bell\", \"channel\": \"Namco 1\"}").Text;
	CHECK_HAS(Text, "Added \"Glass bell\" (00) (N163 instrument: a glassy bell: a hollow, metallic ring that mellows as it dies "
		"away; its wave takes samples 0-15 of the N163's 208, which have room for 12 more sounds).");
	CHECK(Song.Info.Instruments[0].WavePos == 0 && Song.MadeWaves[0].Name == "glass bell");
	CHECK_HAS(Run(Song, "add_instrument", "{\"preset\": \"bell\", \"chip\": \"N163\"}").Text, "The song already has \"Glass bell\" (00)");
	// In write_notes by name: added, its wave in a place of its own
	CHECK(!Run(Song, "write_notes", "{\"channel\": \"Namco 2\", \"instrument\": \"string ensemble\", \"notes\": [{\"bar\": 1, "
		"\"pitch\": \"C-4\", \"beats\": 4}]}").IsError);
	CHECK(Song.Info.Instruments.size() == 2u && Song.Info.Instruments[1].Chip == "N163" && Song.Info.Instruments[1].WavePos == 16);
	CHECK(Song.Rows[6][0].Instrument == 1);
	// A preset's name on an N163 channel picks the N163 sound most like it
	CHECK(!Run(Song, "write_notes", "{\"channel\": \"Namco 3\", \"instrument\": \"bass\", \"notes\": [{\"bar\": 1, "
		"\"pitch\": \"C-2\", \"beats\": 4}]}").IsError && Song.Info.Instruments.back().Name == "Round bass");
	CHECK_HAS(Run(Song, "write_notes", "{\"channel\": \"Namco 3\", \"instrument\": \"kazoo\", \"notes\": [{\"bar\": 1, "
		"\"pitch\": \"C-2\", \"beats\": 4}]}").Text, "N163 sounds: soft flute, drawbar organ");
	CHECK_HAS(Run(Song, "add_instrument", "{\"preset\": \"kazoo\", \"chip\": \"N163\"}").Text, "No N163 sound called \"kazoo\"");
	CHECK_HAS(Run(Song, "get_song", "{}").Text, "N163 sounds (for the Namco channels): soft flute");
	// When the memory is full, it says how to make room
	for (int i = 0; i < 10; ++i) {
		InstrumentInfo Wave;
		Wave.Index = 10 + i;
		Wave.Name = "Wave " + std::to_string(i);
		Wave.Chip = "N163";
		Wave.WavePos = 48 + 16 * i;
		Wave.WaveSize = 16;
		Song.Info.Instruments.push_back(Wave);
	}
	CHECK_HAS(Run(Song, "add_instrument", "{\"preset\": \"organ\", \"chip\": \"N163\"}").Text, "The N163's wave memory is full: with 3 "
		"channels it holds 208 samples, and the song's N163 instruments take them all. Fewer N163 channels make room");
	// Not without N163 channels
	FakeSong Plain;
	CHECK_HAS(Run(Plain, "add_instrument", "{\"preset\": \"organ\", \"chip\": \"N163\"}").Text, "This song has no N163 channels");
}

// Versions to compare
void TestKeptVersions()
{
	FakeSong Song;		// "Town", 8 bars
	Song.Info.DirectChanges = true;
	Song.Put(5, 0, "C-4", 0);
	CHECK_HAS(Run(Song, "compare_versions", "{}").Text, "\"Town\" has no kept version to compare with: keep_version keeps one");
	std::string Text = Run(Song, "keep_version", "{}").Text;
	CHECK_HAS(Text, "Kept track 1 \"Town\" as it is now, as track 2 \"Town (version 1)\". You're still on \"Town\"");
	CHECK(Song.Tracks.size() == 2u && Song.Current == 0 && Song.Tracks[1].Name == "Town (version 1)");
	CHECK(Song.Tracks[1].Rows[5][0].Pitch() == Song.Rows[5][0].Pitch());
	// Changed afterwards: the kept one stays as it was
	CHECK(!Run(Song, "transpose", "{\"semitones\": 2}").IsError);
	CHECK(Song.Tracks[1].Rows[5][0].Pitch() + 2 == Song.Rows[5][0].Pitch());
	// The kept one, then the track as it is
	Text = Run(Song, "compare_versions", "{\"from_bar\": 3, \"bars\": 2}").Text;
	CHECK_HAS(Text, "Playing bars 3-4 of track 2 \"Town (version 1)\", then of track 1 \"Town\" for the user; afterwards "
		"FamiTracker shows \"Town\" again.");
	CHECK(Song.LastPlay.Track == 1 && Song.LastPlay.First == 32 && Song.LastPlay.Rows == 32 && Song.LastPlay.Then.size() == 1u);
	CHECK(Song.LastPlay.Then[0].Track == 0 && Song.LastPlay.Then[0].First == 32 && Song.LastPlay.Then[0].Rows == 32);
	CHECK(Song.LastPlay.ReturnTrack == 0 && Song.Current == 0 && !Song.LastPlay.Loop);
	// Named versions, in the order asked, over and over
	CHECK_HAS(Run(Song, "keep_version", "{\"name\": \"calm\"}").Text, "as track 3 \"Town (calm)\"");
	CHECK_HAS(Run(Song, "keep_version", "{\"name\": \"calm\"}").Text, "as track 4 \"Town (calm 2)\"");
	Text = Run(Song, "compare_versions", "{\"versions\": [\"Town\", \"calm 2\", 2], \"loop\": true}").Text;
	CHECK_HAS(Text, "Playing bars 1-8 of track 1 \"Town\", then of track 4 \"Town (calm 2)\", then of track 2 \"Town (version 1)\" "
		"for the user, over and over until you call stop;");
	CHECK(Song.LastPlay.Track == 0 && Song.LastPlay.Then.size() == 2u && Song.LastPlay.Then[0].Track == 3 && Song.LastPlay.Loop);
	// One named: with the track worked on; the newest kept one by default
	CHECK(!Run(Song, "compare_versions", "{\"versions\": \"version 1\"}").IsError && Song.LastPlay.Track == 1 &&
		Song.LastPlay.Then[0].Track == 0);
	CHECK(!Run(Song, "compare_versions", "{}").IsError && Song.LastPlay.Track == 3);
	// Bars a version doesn't have
	CHECK_HAS(Run(Song, "compare_versions", "{\"from_bar\": 9}").Text, "has only 8 bars.");
	CHECK(Song.Current == 0);
	CHECK_HAS(Run(Song, "compare_versions", "{\"versions\": \"Town\"}").Text, "Name two tracks or more to compare.");
}

// insert_bars and remove_bars: the song's form

// A song of Frames frames (4 bars each) with a note at each bar's start that
// tells the bar (C-3 for bar 1, C#3 for bar 2...); with Loop, bars 1-4 are an
// intro and the rest loops (B01 after the last bar, in Pulse 2's second
// effect column)
FakeSong BarSong(int Frames, bool Loop)
{
	FakeSong Song(Frames);
	Song.Info.DirectChanges = true;
	for (int Bar = 0; Bar < Frames * 4; ++Bar) {
		Song.Rows[5][static_cast<size_t>(Bar * 16)].SetPitch(36 + Bar);
		Song.Rows[5][static_cast<size_t>(Bar * 16)].Instrument = 0;
	}
	if (Loop) {
		Song.Rows[1][static_cast<size_t>(Frames * 64 - 1)].Effect[1] = EFF_JUMP;
		Song.Rows[1][static_cast<size_t>(Frames * 64 - 1)].Param[1] = 1;
	}
	return Song;
}

// The bar whose music starts at Row (by its note), 0 for none
int BarHeard(const FakeSong &Song, int Row)
{
	const Cell &C = Song.Rows[5][static_cast<size_t>(Row)];
	return C.HasNote() ? C.Pitch() - 36 + 1 : 0;
}

void TestBars()
{
	// A whole frame before the loop: the frame list makes room, and the jump goes to where the loop's bars are now
	{
		FakeSong Song = BarSong(3, true);
		std::string Text = Run(Song, "insert_bars", "{\"at_bar\": 5, \"bars\": 4}").Text;
		CHECK_HAS(Text, "Changed the song: 4 empty bars put in at bar 5; what was bar 5 on is bar 9 on now. One Ctrl+Z in "
			"FamiTracker undoes it. Now bars 1-8 play once, then bars 9-16 loop.");
		CHECK_HAS(Text, "The new bars play once, before the loop; set_loop with from_bar 5 takes them into it.");
		CHECK_HAS(Text, "Fill bars 5-8 with write_notes");
		CHECK(Song.LastWrite.MoveFrame == 1 && Song.LastWrite.MoveBy == 1 && Song.LastWrite.Cursor == 64);
		CHECK(Song.Rows[5].size() == 256u && BarHeard(Song, 64) == 0 && BarHeard(Song, 128) == 5 && BarHeard(Song, 240) == 12);
		CHECK(HasEffectAt(Song, 1, 255, EFF_JUMP, 2) && !HasEffectAt(Song, 1, 191, EFF_JUMP, 1));
		CHECK_HAS(Run(Song, "check_loop", "{}").Text, "after bar 16 it goes back to bar 9.");
		// One undo takes it all back
		CHECK(!Run(Song, "undo", "{}").IsError);
		CHECK(Song.Rows[5].size() == 192u && BarHeard(Song, 64) == 5 && HasEffectAt(Song, 1, 191, EFF_JUMP, 1));
		// Taken out again: the frame goes from the frame list
		CHECK(!Run(Song, "insert_bars", "{\"at_bar\": 5, \"bars\": 4}").IsError);
		Text = Run(Song, "remove_bars", "{\"from_bar\": 5, \"to_bar\": 8}").Text;
		CHECK_HAS(Text, "bars 5-8 taken out; what came after starts at bar 5 now");
		CHECK_HAS(Text, "Now bars 1-4 play once, then bars 5-12 loop.");
		CHECK(Song.LastWrite.MoveFrame == 1 && Song.LastWrite.MoveBy == -1);
		CHECK(Song.Rows[5].size() == 192u && BarHeard(Song, 64) == 5 && HasEffectAt(Song, 1, 191, EFF_JUMP, 1));
	}
	// Bars inside the loop that aren't a whole frame: the rows after move, frames added at the end
	{
		FakeSong Song = BarSong(3, true);
		const std::string Text = Run(Song, "insert_bars", "{\"at_bar\": 7, \"bars\": 2}").Text;
		CHECK_HAS(Text, "Now bars 1-4 play once, then bars 5-14 loop.");
		CHECK(Song.LastWrite.MoveFrame == 3 && Song.LastWrite.MoveBy == 1);
		CHECK(BarHeard(Song, 96) == 0 && BarHeard(Song, 112) == 0 && BarHeard(Song, 128) == 7 && BarHeard(Song, 208) == 12);
		CHECK(HasEffectAt(Song, 1, 223, EFF_JUMP, 1) && !HasEffectAt(Song, 1, 191, EFF_JUMP, 1));
		CHECK_HAS(Run(Song, "check_loop", "{}").Text, "after bar 14 it goes back to bar 5.");
		// And out again: the frame added goes
		CHECK_HAS(Run(Song, "remove_bars", "{\"from_bar\": 7, \"to_bar\": 8}").Text, "Now bars 1-4 play once, then bars 5-12 loop.");
		CHECK(Song.Rows[5].size() == 192u && BarHeard(Song, 96) == 7 && HasEffectAt(Song, 1, 191, EFF_JUMP, 1));
		CHECK(Song.LastWrite.MoveFrame == 3 && Song.LastWrite.MoveBy == -1);
	}
	// A bar at the loop's start: it begins the loop, since FamiTracker jumps back only to a frame's start
	{
		FakeSong Song = BarSong(3, true);
		const std::string Text = Run(Song, "insert_bars", "{\"at_bar\": 5}").Text;
		CHECK_HAS(Text, "1 empty bar put in at bar 5; what was bar 5 on is bar 6 on now");
		CHECK_HAS(Text, "Now bars 1-4 play once, then bars 5-13 loop. The new bars begin the loop");
		CHECK(BarHeard(Song, 64) == 0 && BarHeard(Song, 80) == 5 && HasEffectAt(Song, 1, 207, EFF_JUMP, 1));
	}
	// ...but not before it, inside a frame of the intro
	{
		FakeSong Song = BarSong(3, true);
		const ToolResult Result = Run(Song, "insert_bars", "{\"at_bar\": 3, \"bars\": 2}");
		CHECK(Result.IsError && Song.Writes == 0);
		CHECK_HAS(Result.Text, "The song loops back to bar 5, and bars put in before it would move that inside a frame: FamiTracker "
			"jumps back only to a frame's start. Put in a whole frame (4 bars), or put the bars in at bar 5, where they begin the loop.");
		CHECK_HAS(Run(Song, "remove_bars", "{\"from_bar\": 2}").Text, "Take out a whole frame (4 bars) instead.");
		// A whole frame's worth there is fine: frame 1 comes in after frame 0, and bars 3-4 move into it
		CHECK_HAS(Run(Song, "insert_bars", "{\"at_bar\": 3, \"bars\": 4}").Text, "Now bars 1-8 play once, then bars 9-16 loop.");
		CHECK(Song.LastWrite.MoveFrame == 1 && Song.LastWrite.MoveBy == 1);
		CHECK(BarHeard(Song, 32) == 0 && BarHeard(Song, 96) == 3 && BarHeard(Song, 112) == 4 && BarHeard(Song, 128) == 5);
		CHECK(HasEffectAt(Song, 1, 255, EFF_JUMP, 2));
	}
	// A song that loops as a whole by itself: the new end gets a jump back
	{
		FakeSong Song = BarSong(3, false);
		const std::string Text = Run(Song, "insert_bars", "{\"at_bar\": 3}").Text;
		CHECK_HAS(Text, "Now the whole song loops (bars 1-13).");
		CHECK(Song.Rows[5].size() == 256u && HasEffectAt(Song, 0, 207, EFF_JUMP, 0) && BarHeard(Song, 48) == 3);
		// Bar 13 out again: back to three frames, looping by itself
		CHECK_HAS(Run(Song, "remove_bars", "{\"from_bar\": 3}").Text, "Now the whole song loops (bars 1-12).");
		CHECK(Song.Rows[5].size() == 192u && BarHeard(Song, 32) == 3 && BarHeard(Song, 176) == 12);
	}
	// The last bar taken out, its jump with it: the jump comes back on the new last row
	{
		FakeSong Song = BarSong(3, true);
		const std::string Text = Run(Song, "remove_bars", "{\"from_bar\": 12}").Text;
		CHECK_HAS(Text, "bar 12 taken out at the end. One Ctrl+Z in FamiTracker undoes it. Now bars 1-4 play once, then bars 5-11 loop.");
		CHECK(HasEffectAt(Song, 0, 175, EFF_JUMP, 1) && !HasEffectAt(Song, 1, 191, EFF_JUMP, 1) && BarHeard(Song, 176) == 0);
		// Bars taken out where the loop starts: it starts with what came after them
		CHECK_HAS(Run(Song, "remove_bars", "{\"from_bar\": 5, \"to_bar\": 8}").Text, "Now bars 1-4 play once, then bars 5-7 loop.");
		CHECK(Song.Rows[5].size() == 128u && BarHeard(Song, 64) == 9 && HasEffectAt(Song, 0, 111, EFF_JUMP, 1));
		// Not the whole loop, nor every bar
		CHECK_HAS(Run(Song, "remove_bars", "{\"from_bar\": 5, \"to_bar\": 8}").Text, "That takes out the whole loop (bars 5-7)");
		CHECK_HAS(Run(Song, "remove_bars", "{\"from_bar\": 1, \"to_bar\": 8}").Text, "That's every bar of the song.");
	}
	// Bars at the end of a song that stops: its halt moves after them, so they play
	{
		FakeSong Song = BarSong(2, false);
		Song.Rows[3][127].Effect[0] = EFF_HALT;
		const std::string Text = Run(Song, "insert_bars", "{\"at_bar\": 9, \"bars\": 4}").Text;
		CHECK_HAS(Text, "4 empty bars added at the end (bars 9-12). One Ctrl+Z in FamiTracker undoes it. Now it plays once and stops "
			"after bar 12.");
		CHECK(HasEffectAt(Song, 3, 191, EFF_HALT, 0) && !HasEffectAt(Song, 3, 127, EFF_HALT, 0));
		CHECK(Song.LastWrite.MoveFrame == 2 && Song.LastWrite.MoveBy == 1);
	}
	// Bars after a song's end don't play; it says so
	{
		FakeSong Song = BarSong(3, false);
		Song.Rows[0][127].Effect[0] = EFF_JUMP;	// the song ends after bar 8, bars 9-12 left over
		Song.Rows[0][127].Param[0] = 0;
		CHECK_HAS(Run(Song, "insert_bars", "{\"at_bar\": 13, \"bars\": 2}").Text, "Bars 13-14 don't play: the song goes back after bar "
			"8, before them. set_loop with to_bar 14 takes it on through them.");
	}
	// A skip ends a frame early: only whole frames there
	{
		FakeSong Song = BarSong(3, true);
		Song.Rows[3][100].Effect[0] = EFF_SKIP;
		CHECK_HAS(Run(Song, "remove_bars", "{\"from_bar\": 6}").Text, "The song skips ahead (D00) at bar 7 beat 2, which ends that "
			"frame early, so bars go in or out there only a whole frame (4 bars) at a time.");
		CHECK(!Run(Song, "insert_bars", "{\"at_bar\": 9, \"bars\": 4}").IsError && HasEffectAt(Song, 3, 100, EFF_SKIP, 0));
	}
	// Frames of three bars and a bit: whole frames only
	{
		FakeSong Song = BarSong(3, false);
		Song.Info.PatternLength = 48;
		Song.Info.SecondHighlight = 20;
		CHECK_HAS(Run(Song, "insert_bars", "{\"at_bar\": 2}").Text, "A frame here is 48 rows, not whole bars of 20, so bars go in a "
			"frame (3 bars) at a time");
	}
	// Asked properly, with direct changes
	{
		FakeSong Song = BarSong(2, true);
		CHECK_HAS(Run(Song, "insert_bars", "{}").Text, "Say where with at_bar");
		Song.Info.DirectChanges = false;
		CHECK_HAS(Run(Song, "insert_bars", "{\"at_bar\": 5}").Text, "needs direct changes");
		CHECK_HAS(Run(Song, "remove_bars", "{\"from_bar\": 5}").Text, "needs direct changes");
	}
}

// OGG files for games

void TestOgg()
{
	// A tone comes back as it went in, with its comments
	std::vector<float> Tone;
	for (int i = 0; i < 44100; ++i)
		Tone.push_back(static_cast<float>(0.5 * std::sin(2.0 * 3.14159265358979 * 440.0 * i / 44100.0)));
	OggComments Comments;
	Comments.push_back(std::make_pair(std::string("TITLE"), std::string("Town (night)")));
	Comments.push_back(std::make_pair(std::string("LOOPSTART"), std::string("1234")));
	std::string File, Error;
	CHECK(EncodeOgg(Tone, 44100, 0.6f, Comments, File, Error));
	CHECK(File.compare(0, 4, "OggS") == 0 && File.size() > 2000 && File.size() < 40000);
	std::vector<float> Back;
	int Rate = 0, Channels = 0;
	OggComments Read;
	CHECK(DecodeOgg(File, Back, Rate, Channels, Read, Error));
	CHECK(Rate == 44100 && Channels == 1 && Back.size() == Tone.size());
	double Error2 = 0.0, Power = 0.0;
	for (size_t i = 0; i < std::min(Back.size(), Tone.size()); ++i) {
		Error2 += (Back[i] - Tone[i]) * (Back[i] - Tone[i]);
		Power += Tone[i] * Tone[i];
	}
	CHECK(Error2 < Power * 0.001);	// 30 dB or better
	CHECK(Read.size() == 2 && Read[0].first == "TITLE" && Read[0].second == "Town (night)" && Read[1].second == "1234");
	// The same audio makes the same file
	std::string Again;
	CHECK(EncodeOgg(Tone, 44100, 0.6f, Comments, Again, Error) && Again == File);
	// Nothing at all is a file too; junk isn't
	CHECK(EncodeOgg(std::vector<float>(), 48000, 0.6f, OggComments(), File, Error));
	CHECK(DecodeOgg(File, Back, Rate, Channels, Read, Error) && Back.empty() && Rate == 48000);
	CHECK(!DecodeOgg("not an ogg", Back, Rate, Channels, Read, Error));
	CHECK(!EncodeOgg(Tone, 1000, 0.6f, OggComments(), File, Error));

	CHECK_EQ(GameMakerName("Town (night).ogg"), std::string("snd_town_night"));
	CHECK_EQ(GameMakerName("Boss Theme 2.ogg"), std::string("snd_boss_theme_2"));
	CHECK_EQ(GameMakerName("???.ogg"), std::string("snd_music"));
	CHECK_EQ(SafeFileName("a/b:c?"), std::string("a b c"));
	CHECK_EQ(SafeFileName(" Town. "), std::string("Town"));
	CHECK_EQ(SafeFileName("con"), std::string("con music"));
	CHECK_EQ(SafeFileName("..."), std::string("music"));
}

// The seconds GameMaker code gives a function for a sound: "audio_sound_loop_start(snd_town, 6.400000)"
double CodeSeconds(const std::string &Text, const std::string &Call)
{
	const size_t At = Text.find(Call);
	if (At == std::string::npos)
		return -1.0;
	return std::atof(Text.c_str() + At + Call.size());
}

bool Near(double a, double b, double Within)
{
	return std::fabs(a - b) <= Within;
}

void TestCut()
{
	// A tone with a quiet moment just after each of the loop's starts: the seam moves there
	std::vector<float> Render;
	for (int i = 0; i < 24000; ++i) {
		const bool Quiet = (i >= 1110 && i < 1140) || (i >= 9123 && i < 9153);
		Render.push_back(Quiet ? 0.0f : static_cast<float>(0.5 * std::sin(2.0 * 3.14159265358979 * 440.0 * i / 8000.0)));
	}
	std::vector<TimedRow> Rows(3);
	Rows[0].Row = 0;
	Rows[1].Row = 16;
	Rows[1].LoopStart = true;
	Rows[2].Row = 16;
	Rows[2].LoopStart = true;
	std::vector<std::pair<int, long long> > Marks;
	Marks.push_back(std::make_pair(0, 100LL));
	Marks.push_back(std::make_pair(16, 1100LL));
	Marks.push_back(std::make_pair(16, 9113LL));
	GameCut Cut;
	std::string Error;
	CHECK(CutForGame(Render, 8000, Rows, Marks, false, true, Cut, Error));
	CHECK(Cut.Loops && !Cut.SecondPass && Cut.Begin == 100);
	CHECK_EQ(Cut.End - (Cut.LoopStart + Cut.Begin), 8013LL);		// the loop keeps its length
	CHECK_EQ(Cut.LoopStart, 1016LL);								// 16 samples later, in the quiet
	CHECK(std::fabs(Render[1100] - Render[9113]) > 0.2 && Render[static_cast<size_t>(Cut.Begin + Cut.LoopStart)] == 0.0f &&
		Render[static_cast<size_t>(Cut.End)] == 0.0f);
	// Where the waves meet already, nothing moves
	Render.clear();
	for (int i = 0; i < 24000; ++i)
		Render.push_back(static_cast<float>(0.5 * std::sin(2.0 * 3.14159265358979 * 440.0 * i / 8000.0)));
	Marks[2].second = 9100;
	CHECK(CutForGame(Render, 8000, Rows, Marks, false, true, Cut, Error) && Cut.LoopStart == 1000 && Cut.End == 9100);
	// Rows that don't fit, a render too short, a song that halts
	Marks[1].first = 15;
	CHECK(!CutForGame(Render, 8000, Rows, Marks, false, true, Cut, Error));
	Marks[1].first = 16;
	Marks.pop_back();
	CHECK(!CutForGame(Render, 8000, Rows, Marks, false, true, Cut, Error));
	CHECK(CutForGame(Render, 8000, Rows, Marks, true, true, Cut, Error) && !Cut.Loops && Cut.Begin == 100 && Cut.End == 24000);
	// With the row that halts rendered, the sound ends where it starts
	Rows.pop_back();
	CHECK(CutForGame(Render, 8000, Rows, Marks, true, true, Cut, Error) && !Cut.Loops && Cut.Begin == 100 && Cut.End == 1100);
	CHECK(Cut.FadeIn == 0);
	CHECK(CutForGame(Render, 8000, Rows, Marks, true, true, Cut, Error, true) && Cut.FadeIn == 320);

	// A song whose start pops (the DPCM level set from silence) fades in over its first 40 ms, outside the loop: one that
	// loops as a whole repeats from its second time through
	std::vector<TimedRow> Whole(2);
	Whole[0].Row = 0;
	Whole[0].LoopStart = true;
	Whole[1].Row = 0;
	Whole[1].LoopStart = true;
	std::vector<std::pair<int, long long> > Thrice;
	Thrice.push_back(std::make_pair(0, 100LL));
	Thrice.push_back(std::make_pair(0, 8100LL));
	Thrice.push_back(std::make_pair(0, 16100LL));
	CHECK(CutForGame(Render, 8000, Whole, Thrice, false, true, Cut, Error) && !Cut.SecondPass && Cut.LoopStart < 20 && Cut.FadeIn == 0);
	CHECK(CutForGame(Render, 8000, Whole, Thrice, false, true, Cut, Error, true) && Cut.SecondPass && Cut.StartPops);
	CHECK(Cut.LoopStart == 8000 && Cut.End == 16100 && Cut.FadeIn == 320);
	// With an intro the loop stays the first time through
	Marks.clear();
	Marks.push_back(std::make_pair(0, 100LL));
	Marks.push_back(std::make_pair(16, 1100LL));
	Marks.push_back(std::make_pair(16, 9100LL));
	Rows.push_back(Rows.back());
	Rows[2].LoopStart = true;
	CHECK(CutForGame(Render, 8000, Rows, Marks, false, true, Cut, Error, true) && !Cut.SecondPass && !Cut.StartPops);
	CHECK(Cut.LoopStart == 1000 && Cut.FadeIn == 320);
}

std::string Comment(const OggComments &Comments, const char *pKey)
{
	for (size_t i = 0; i < Comments.size(); ++i)
		if (Comments[i].first == pKey)
			return Comments[i].second;
	return "(none)";
}

void TestLoudness()
{
	// EBU R128: a full-scale 997 Hz sine in one channel measures -3.01 LUFS, at any sample rate
	const int Rates[] = { 44100, 48000, 8000 };
	for (int r = 0; r < 3; ++r) {
		std::vector<float> Sine(static_cast<size_t>(Rates[r] * 10));
		for (size_t i = 0; i < Sine.size(); ++i)
			Sine[i] = static_cast<float>(std::sin(2.0 * 3.14159265358979 * 997.0 * static_cast<double>(i) / Rates[r]));
		CHECK(Near(IntegratedLoudness(Sine, Rates[r], 0, Sine.size()), -3.01, 0.05));
		for (size_t i = 0; i < Sine.size(); ++i)
			Sine[i] *= 0.1f;
		CHECK(Near(IntegratedLoudness(Sine, Rates[r], 0, Sine.size()), -23.01, 0.05));
		// From a sample on, and no further than the end
		CHECK(Near(IntegratedLoudness(Sine, Rates[r], Sine.size() / 2, Sine.size()), -23.01, 0.05));
	}
	// Silence doesn't count (the gate at -70 LUFS): 5 s of a sine and 5 of silence measure about as the sine
	std::vector<float> Half(44100 * 10, 0.0f);
	for (size_t i = 0; i < Half.size() / 2; ++i)
		Half[i] = static_cast<float>(0.1 * std::sin(2.0 * 3.14159265358979 * 997.0 * static_cast<double>(i) / 44100.0));
	CHECK(Near(IntegratedLoudness(Half, 44100, 0, Half.size()), -23.0, 0.3));
	// Nor what's more than 10 LU under the rest
	for (size_t i = Half.size() / 2; i < Half.size(); ++i)
		Half[i] = static_cast<float>(0.001 * std::sin(2.0 * 3.14159265358979 * 997.0 * static_cast<double>(i) / 44100.0));
	CHECK(Near(IntegratedLoudness(Half, 44100, 0, Half.size()), -23.0, 0.3));
	CHECK(IntegratedLoudness(std::vector<float>(44100, 0.0f), 44100, 0, 44100) <= -70.0);
	CHECK(IntegratedLoudness(Half, 44100, 0, 100) <= -70.0);	// shorter than a block
	CHECK(IntegratedLoudness(Half, 44100, Half.size(), 100) <= -70.0);
}

void TestExport()
{
	// Bars 1-4 an intro, bars 5-8 the loop; the bass's last note hangs over the seam
	FakeSong Song;
	Song.Info.DirectChanges = true;
	Song.Put(5, 0, "C-4", 0);
	Song.Put(5, 64, "E-4");
	Song.Put(5, 96, "G-4");
	Song.Put(2, 0, "C-2", 1);
	Song.Put(2, 112, "G-1");
	Song.Rows[0][127].Effect[0] = EFF_JUMP;
	Song.Rows[0][127].Param[0] = 1;
	ToolSet Tools(Song);
	std::string Text = Tools.Call("export_for_game", Parse("{}")).Text;
	CHECK_HAS(Text, "Saved next to test.ftm:\n- Town.ogg: 19.2 s");
	// The bass's G-1 hangs into the loop's start on repeat: the loop is the second time through
	CHECK_HAS(Text, "the first 12.8 s once, then the loop from 12.8 s to the end. The loop is the song's second time through");
	// The loop points, within the 2 ms they may move to where the waves meet
	CHECK(Near(CodeSeconds(Text, "audio_sound_loop_start(snd_town, "), 12.8, 0.002));
	CHECK(Near(CodeSeconds(Text, "audio_sound_loop_end(snd_town, ") - CodeSeconds(Text, "audio_sound_loop_start(snd_town, "), 6.4, 0.0001));
	CHECK_HAS(Text, "\naudio_play_sound(snd_town, 0, true);");
	CHECK(Song.Files.count("Town.ogg") == 1);
	std::vector<float> Samples;
	int Rate = 0, Channels = 0;
	OggComments Comments;
	std::string Error;
	CHECK(DecodeOgg(Song.Files["Town.ogg"], Samples, Rate, Channels, Comments, Error));
	const long long Start = std::atoll(Comment(Comments, "LOOPSTART").c_str());
	CHECK(Rate == 8000 && Near(static_cast<double>(Samples.size()), 153600.0, 16.0));		// 19.2 s
	CHECK(std::llabs(Start - 102400) <= 16 && Comment(Comments, "LOOPLENGTH") == "51200");
	CHECK(std::atoll(Comment(Comments, "LOOPEND").c_str()) == Start + 51200 && static_cast<long long>(Samples.size()) == Start + 51200);
	CHECK(Comment(Comments, "TITLE") == "Town" && Comment(Comments, "ALBUM") == "Test song");
	{
		// Brought up to a peak of -1 dBFS (0.89), or kept at the level FamiTracker renders
		float Peak = 0.0f;
		for (size_t i = 0; i < Samples.size(); ++i)
			Peak = std::max(Peak, std::fabs(Samples[i]));
		CHECK_HAS(Text, "dB louder than FamiTracker plays it (its loudest peak at -1 dBFS)");
		CHECK(Peak < 0.97f);
		CHECK_HAS(Text, "12.0 dB louder");	// this quiet render: as much as it may be raised
		const std::string Plain = Tools.Call("export_for_game", Parse("{\"normalize\": false}")).Text;
		CHECK_LACKS(Plain, "dB louder");
		std::vector<float> Quiet;
		CHECK(DecodeOgg(Song.Files["Town 2.ogg"].empty() ? Song.Files["Town.ogg"] : Song.Files["Town 2.ogg"], Quiet, Rate, Channels, Comments, Error));
		float QuietPeak = 0.0f;
		for (size_t i = 0; i < Quiet.size(); ++i)
			QuietPeak = std::max(QuietPeak, std::fabs(Quiet[i]));
		CHECK(Near(QuietPeak * 3.98, Peak, 0.05));
	}
	CHECK_LACKS(Text, "muted");
	CHECK_LACKS(Text, "fading in");
	// To a loudness instead: the loop measures it; where the peak would pass -1 dBFS, the peak holds it back
	{
		const std::string Quiet = Tools.Call("export_for_game", Parse("{\"loudness\": -30}")).Text;
		CHECK_HAS(Quiet, ", its loop at -30.0 LUFS (");
		CHECK_LACKS(Quiet, "LU short of");
		std::vector<float> Back;
		CHECK(DecodeOgg(Song.Files["Town.ogg"], Back, Rate, Channels, Comments, Error));
		const long long From = std::atoll(Comment(Comments, "LOOPSTART").c_str());
		CHECK(Near(IntegratedLoudness(Back, Rate, static_cast<size_t>(From), Back.size()), -30.0, 0.3));
		// These square waves peak little over their loudness, so even -6 LUFS stays under -1 dBFS
		CHECK_HAS(Tools.Call("export_for_game", Parse("{\"loudness\": -6}")).Text, ", its loop at -6.0 LUFS (");
		CHECK(DecodeOgg(Song.Files["Town.ogg"], Back, Rate, Channels, Comments, Error));
		float Peak = 0.0f;
		for (size_t i = 0; i < Back.size(); ++i)
			Peak = std::max(Peak, std::fabs(Back[i]));
		CHECK(Peak > 0.3f && Peak < 0.97f);	// the encoding rings a little over a square wave's edges
		// A peak that would pass -1 dBFS holds the gain back there
		bool Held = false;
		CHECK(Near(LoudnessGain(-30.0, -20.0, 0.1, Held), 10.0, 1e-9) && !Held);	// the peak to -10 dBFS
		CHECK(Near(LoudnessGain(-30.0, -20.0, 0.5, Held), -1.0 + 6.0206, 0.001) && Held);	// from -6 dBFS to -1
		CHECK(Near(LoudnessGain(-14.0, -20.0, 0.9, Held), -6.0, 1e-9) && !Held);	// quieter: no peak holds that back
		CHECK_HAS(Tools.Call("export_for_game", Parse("{\"loudness\": -3}")).Text, "loudness");	// out of its range
		CHECK(Tools.Call("export_for_game", Parse("{\"loudness\": -3}")).IsError);
	}
	// The DPCM level set on the first row pops as the song starts: the file fades in over it
	{
		Song.Rows[4][0].Effect[0] = EFF_DAC;
		Song.Rows[4][0].Param[0] = 0x30;
		const std::string Faded = Tools.Call("export_for_game", Parse("{}")).Text;
		CHECK_HAS(Faded, "fading in over its first 40 ms (the DPCM level set as it starts pops)");
		CHECK_HAS(Faded, "the first 12.8 s once, then the loop from 12.8 s to the end.");
		std::vector<float> Start;
		CHECK(DecodeOgg(Song.Files["Town.ogg"], Start, Rate, Channels, Comments, Error));
		float Early = 0.0f, Later = 0.0f;
		for (size_t i = 0; i < 16 && i < Start.size(); ++i)
			Early = std::max(Early, std::fabs(Start[i]));
		for (size_t i = 400; i < 800 && i < Start.size(); ++i)
			Later = std::max(Later, std::fabs(Start[i]));
		CHECK(Early < 0.05f && Later > 0.2f);
		Song.Rows[4][0].Effect[0] = EFF_NONE;
		Song.Rows[4][0].Param[0] = 0;
	}
	// A muted channel is in the game's file all the same
	Song.Info.Channels[2].Muted = true;
	CHECK_HAS(Tools.Call("export_for_game", Parse("{}")).Text, "\nEvery channel is in the files, the muted 2A03 Triangle too.\n");
	Song.Info.Channels[2].Muted = false;

	// With the bass ending before the loop comes round, the first time through will do
	Song.End(2, 120);
	Song.Put(2, 64, "C-2");
	Text = Tools.Call("export_for_game", Parse("{}")).Text;
	CHECK_HAS(Text, "- Town.ogg: 12.8 s");
	CHECK(Near(CodeSeconds(Text, "audio_sound_loop_start(snd_town, "), 6.4, 0.002));
	CHECK(Near(CodeSeconds(Text, "audio_sound_loop_end(snd_town, "), 12.8, 0.002));
	CHECK_LACKS(Text, "second time through");
	CHECK(DecodeOgg(Song.Files["Town.ogg"], Samples, Rate, Channels, Comments, Error) && Near(static_cast<double>(Samples.size()), 102400.0, 16.0));

	// Every track: the day version and the night one, slower, as their own files
	CHECK(!Tools.Call("make_version", Parse("{\"version\": \"night\", \"echo\": false}")).IsError);
	Text = Tools.Call("export_for_game", Parse("{\"tracks\": \"all\"}")).Text;
	CHECK_HAS(Text, "- Town.ogg: 12.8 s");
	CHECK_HAS(Text, "- Town (night).ogg: 16 s");
	CHECK(Near(CodeSeconds(Text, "audio_sound_loop_end(snd_town_night, "), 16.0, 0.002));
	CHECK(Song.Current == 1);	// the selection is back where it was
	CHECK(Song.Files.count("Town (night).ogg") == 1);

	// A song that halts plays once; an unsaved song has no place for the files
	Song.Switch(0);
	Song.Rows[0][127].Effect[0] = EFF_HALT;
	Song.Rows[0][127].Param[0] = 0;
	CHECK_HAS(Tools.Call("export_for_game", Parse("{\"tracks\": [1]}")).Text, ", plays once (the song halts).\n");
	CHECK_HAS(Tools.Call("export_for_game", Parse("{\"tracks\": [1]}")).Text, "audio_play_sound(snd_town, 0, false);");
	CHECK(Tools.Call("export_for_game", Parse("{\"tracks\": [5]}")).IsError);
	Song.Info.FileName.clear();
	CHECK_HAS(Tools.Call("export_for_game", Parse("{}")).Text, "hasn't been saved yet");
}

// The connection for Claude Code in WSL (famitracker-mcp): lines in, HTTP to
// FamiTracker, lines out

std::string HeaderOf(const HttpRequest &Request, const char *pName)
{
	const std::string *pValue = Request.Header(pName);
	return pValue != 0 ? *pValue : std::string("(none)");
}

// A line through the bridge and FamiTracker's endpoint, as bytes both ways
std::string ThroughEndpoint(const std::string &Line, ToolSet &Tools, const McpConfig &Config, const std::string &Token)
{
	BridgeMessage Message;
	if (!BridgeRequest(Line, Config.Port, Token, Message))
		return "(blank)";
	const std::string Bytes = FormatHttpRequest(Message.Request);
	HttpRequestParser Parser;
	if (Parser.Feed(Bytes.data(), Bytes.size()) != HttpRequestParser::COMPLETE)
		return "(unreadable request)";
	const HttpRequest Request = Parser.Take();
	const std::string Wire = FormatHttpResponse(HandleMcp(Request, Config, Tools), Request.KeepAlive());
	HttpResponse Answer;
	if (ReadHttpResponse(Wire, false, Answer) != RESPONSE_COMPLETE)
		return "(unreadable answer)";
	return BridgeAnswer(Message, Answer);
}

// One line, ending in a line break: what Claude Code reads
bool OneLine(const std::string &Text)
{
	return !Text.empty() && Text.find('\n') == Text.size() - 1 && Text.find('\r') == std::string::npos;
}

const char MODERN_META[] = "\"_meta\":{\"io.modelcontextprotocol/protocolVersion\":\"2026-07-28\"}";

void TestBridge()
{
	// Lines become requests
	BridgeMessage Message;
	CHECK(!BridgeRequest("", 46600, "t", Message));
	CHECK(!BridgeRequest(" \t\r", 46600, "t", Message));
	const std::string Initialize = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-06-18\","
		"\"capabilities\":{},\"clientInfo\":{\"name\":\"claude-code\",\"version\":\"2.0\"}}}";
	CHECK(BridgeRequest(Initialize + "\r", 46601, "secret", Message));
	CHECK(Message.Readable && Message.WantsAnswer && Message.Id == Json(1));
	CHECK_EQ(Message.Request.Method, std::string("POST"));
	CHECK_EQ(Message.Request.Target, std::string("/mcp"));
	CHECK_EQ(Message.Request.Body, Initialize);
	CHECK_EQ(HeaderOf(Message.Request, "Host"), std::string("127.0.0.1:46601"));
	CHECK_EQ(HeaderOf(Message.Request, "Authorization"), std::string("Bearer secret"));
	CHECK_EQ(HeaderOf(Message.Request, "Content-Type"), std::string("application/json"));
	CHECK_EQ(HeaderOf(Message.Request, "Connection"), std::string("close"));
	CHECK_EQ(HeaderOf(Message.Request, "MCP-Protocol-Version"), std::string("(none)"));
	const std::string Bytes = FormatHttpRequest(Message.Request);
	CHECK_EQ(Bytes.compare(0, 22, "POST /mcp HTTP/1.1\r\nHo"), 0);
	char Length[48];
	std::snprintf(Length, sizeof(Length), "\r\nContent-Length: %d\r\n\r\n{", static_cast<int>(Initialize.size()));
	Check(Contains(Bytes, Length), "the length before the body", __LINE__);
	CHECK(BridgeRequest(Initialize, 46600, "", Message));
	CHECK_EQ(HeaderOf(Message.Request, "Authorization"), std::string("(none)"));

	// The 2026-07-28 protocol: the headers say what the body says
	CHECK(BridgeRequest(std::string("{\"jsonrpc\":\"2.0\",\"id\":\"x\",\"method\":\"tools/call\",\"params\":{\"name\":\"get_song\","
		"\"arguments\":{},") + MODERN_META + "}}", 46600, "t", Message));
	CHECK(Message.WantsAnswer && Message.Id == Json("x"));
	CHECK_EQ(HeaderOf(Message.Request, "MCP-Protocol-Version"), std::string("2026-07-28"));
	CHECK_EQ(HeaderOf(Message.Request, "Mcp-Method"), std::string("tools/call"));
	CHECK_EQ(HeaderOf(Message.Request, "Mcp-Name"), std::string("get_song"));
	CHECK(BridgeRequest(std::string("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\",\"params\":{\"name\":\"caf\\u00e9 x\",") +
		MODERN_META + "}}", 46600, "t", Message));
	CHECK_EQ(HeaderOf(Message.Request, "Mcp-Name"), std::string("=?base64?Y2Fmw6kgeA==?="));
	CHECK(BridgeRequest(std::string("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"resources/read\",\"params\":{\"uri\":\"=?base64?x\",") +
		MODERN_META + "}}", 46600, "t", Message));
	CHECK_EQ(HeaderOf(Message.Request, "Mcp-Name"), std::string("=?base64?PT9iYXNlNjQ/eA==?="));
	CHECK(BridgeRequest(std::string("{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/list\",\"params\":{") + MODERN_META + "}}",
		46600, "t", Message));
	CHECK_EQ(HeaderOf(Message.Request, "Mcp-Method"), std::string("tools/list"));
	CHECK_EQ(HeaderOf(Message.Request, "Mcp-Name"), std::string("(none)"));
	// Nothing in a value ends the head early
	CHECK(BridgeRequest(std::string("{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"a\\r\\nX-Injected: 1\",\"params\":{") + MODERN_META + "}}",
		46600, "t", Message));
	CHECK_LACKS(FormatHttpRequest(Message.Request), "\r\nX-Injected");

	// Notifications and Claude Code's own answers wait for nothing
	CHECK(BridgeRequest("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}", 46600, "t", Message));
	CHECK(Message.Readable && !Message.WantsAnswer);
	CHECK(BridgeRequest("{\"jsonrpc\":\"2.0\",\"id\":9,\"result\":{}}", 46600, "t", Message));
	CHECK(Message.Readable && !Message.WantsAnswer);
	CHECK(BridgeRequest("{nope", 46600, "t", Message));
	CHECK(!Message.Readable && !Message.WantsAnswer);

	// Through FamiTracker's endpoint
	FakeSong Song;
	Song.Put(5, 0, "C-4", 0);
	ToolSet Tools(Song);
	McpConfig Config;
	Config.Port = 46600;
	Config.Token = "secret";
	std::string Out = ThroughEndpoint(Initialize, Tools, Config, "secret");
	CHECK(OneLine(Out));
	Json Answer = Parse(Out);
	CHECK(Answer.Get("id") != 0 && *Answer.Get("id") == Json(1));
	CHECK(Answer.Get("result") != 0 && Answer.Get("result")->Get("protocolVersion") != 0 &&
		Answer.Get("result")->Get("protocolVersion")->String() == "2025-06-18");
	CHECK_EQ(ThroughEndpoint("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}", Tools, Config, "secret"), std::string());
	Out = ThroughEndpoint("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\"}", Tools, Config, "secret");
	Answer = Parse(Out);
	CHECK(OneLine(Out) && Answer.Get("result") != 0 && Answer.Get("result")->Get("tools")->Size() == Tools.List().size());
	Out = ThroughEndpoint("{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\",\"params\":{\"name\":\"get_song\",\"arguments\":{}}}",
		Tools, Config, "secret");
	CHECK(OneLine(Out));
	CHECK_HAS(Out, "\"isError\":false");
	CHECK_HAS(Out, "VRC6 Pulse 1");
	Out = ThroughEndpoint(std::string("{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"server/discover\",\"params\":{") + MODERN_META + "}}",
		Tools, Config, "secret");
	CHECK_HAS(Out, "\"supportedVersions\":[\"2026-07-28\"]");
	Out = ThroughEndpoint(std::string("{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"tools/call\",\"params\":{\"name\":\"get_song\","
		"\"arguments\":{},") + MODERN_META + "}}", Tools, Config, "secret");
	CHECK_HAS(Out, "\"resultType\":\"complete\"");
	CHECK_HAS(Out, "\"isError\":false");
	// A name sent in base64 still matches: the tool is unknown, the headers fine
	Out = ThroughEndpoint(std::string("{\"jsonrpc\":\"2.0\",\"id\":6,\"method\":\"tools/call\",\"params\":{\"name\":\"caf\\u00e9\","
		"\"arguments\":{},") + MODERN_META + "}}", Tools, Config, "secret");
	CHECK_HAS(Out, "Unknown tool");
	CHECK_LACKS(Out, "Mcp-Name");
	// Refused before it was read: still the answer to that request
	for (int Wrong = 0; Wrong < 2; ++Wrong) {
		Out = ThroughEndpoint("{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"tools/list\"}", Tools, Config, Wrong == 0 ? "nope" : "");
		Answer = Parse(Out);
		CHECK(OneLine(Out) && Answer.Get("id") != 0 && *Answer.Get("id") == Json(7));
		CHECK_HAS(Out, "access token");
	}
	// A line that isn't JSON gets its parse error
	Out = ThroughEndpoint("{nope", Tools, Config, "secret");
	CHECK(OneLine(Out));
	CHECK_HAS(Out, "\"id\":null");
	CHECK_HAS(Out, "-32700");
	CHECK_EQ(ThroughEndpoint("{\"jsonrpc\":\"2.0\",\"id\":9,\"result\":{}}", Tools, Config, "secret"), std::string());
	CHECK_EQ(ThroughEndpoint("", Tools, Config, "secret"), std::string("(blank)"));

	// Answers that aren't JSON-RPC become errors for the request
	HttpResponse NotFound;
	NotFound.Status = 404;
	NotFound.ContentType = "text/plain; charset=utf-8";
	NotFound.Body = "FamiTracker's MCP endpoint is /mcp.\n";
	CHECK(BridgeRequest("{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"ping\"}", 46600, "t", Message));
	Out = BridgeAnswer(Message, NotFound);
	CHECK(OneLine(Out));
	CHECK_HAS(Out, "\"id\":3");
	CHECK_HAS(Out, "HTTP 404 Not Found: FamiTracker's MCP endpoint is /mcp.");
	HttpResponse Empty;
	Empty.Status = 202;
	CHECK_HAS(BridgeAnswer(Message, Empty), "HTTP 202 Accepted.");
	Out = BridgeFailure(Message, "FamiTracker isn't running.");
	CHECK(OneLine(Out));
	CHECK_HAS(Out, "{\"jsonrpc\":\"2.0\",\"id\":3,\"error\":{\"code\":-32000,\"message\":\"FamiTracker isn't running.\"}}");
	CHECK(BridgeRequest("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/cancelled\"}", 46600, "t", Message));
	CHECK_EQ(BridgeAnswer(Message, NotFound), std::string());
	CHECK_EQ(BridgeAnswer(Message, Empty), std::string());
	CHECK_EQ(BridgeFailure(Message, "FamiTracker isn't running."), std::string());

	// Responses read as their bytes arrive
	HttpResponse Response;
	Response.ContentType = "application/json";
	Response.Body = "{\"a\":1}";
	const std::string Wire = FormatHttpResponse(Response, false);
	HttpResponse Read;
	bool Waits = true;
	for (size_t Size = 0; Size < Wire.size(); ++Size)
		Waits = Waits && ReadHttpResponse(Wire.substr(0, Size), false, Read) == RESPONSE_NEED_MORE;
	CHECK(Waits);
	CHECK_EQ(ReadHttpResponse(Wire, false, Read), RESPONSE_COMPLETE);
	CHECK(Read.Status == 200 && Read.ContentType == "application/json" && Read.Body == "{\"a\":1}");
	CHECK_EQ(ReadHttpResponse(Wire, true, Read), RESPONSE_COMPLETE);
	CHECK_EQ(ReadHttpResponse(Wire.substr(0, Wire.size() - 1), true, Read), RESPONSE_BAD);
	CHECK_EQ(ReadHttpResponse("HTTP/1.1 100 Continue\r\n\r\n" + Wire, false, Read), RESPONSE_COMPLETE);
	CHECK(Read.Status == 200 && Read.Body == "{\"a\":1}");
	const std::string Unsized = "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\nX-Other:  yes \r\n\r\nhello";
	CHECK_EQ(ReadHttpResponse(Unsized, false, Read), RESPONSE_NEED_MORE);
	CHECK_EQ(ReadHttpResponse(Unsized, true, Read), RESPONSE_COMPLETE);
	CHECK(Read.Body == "hello" && Read.ContentType == "text/plain" && Read.Headers.size() == 1 && Read.Headers[0].second == "yes");
	CHECK_EQ(ReadHttpResponse("HTTP/1.1 204 No Content\r\n\r\n", false, Read), RESPONSE_COMPLETE);
	CHECK(Read.Status == 204 && Read.Body.empty());
	CHECK_EQ(ReadHttpResponse("HTTP/1.1 503\r\nContent-Length: 2\r\n\r\nno", false, Read), RESPONSE_COMPLETE);
	CHECK(Read.Status == 503 && Read.Body == "no");
	const char *const BROKEN[] = {
		"HTTP/1.1 2x0 OK\r\n\r\n", "FTP/1.1 200 OK\r\n\r\n", "HTTP/1.1 2000 OK\r\n\r\n", "HTTP/1.1 200 OK\r\nBroken\r\n\r\n",
		"HTTP/1.1 200 OK\r\n: x\r\n\r\n", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2\r\nno\r\n0\r\n\r\n",
		"HTTP/1.1 200 OK\r\nContent-Length: x\r\n\r\n", "HTTP/1.1 200 OK\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\nab" };
	for (size_t i = 0; i < sizeof(BROKEN) / sizeof(BROKEN[0]); ++i)
		Check(ReadHttpResponse(BROKEN[i], false, Read) == RESPONSE_BAD, BROKEN[i], __LINE__);
	CHECK_EQ(ReadHttpResponse("HTTP/1.1 200 OK\r\n" + std::string(70000, 'x'), false, Read), RESPONSE_BAD);
	CHECK_EQ(ReadHttpResponse("", true, Read), RESPONSE_BAD);

	// The setup command for WSL
	CHECK_EQ(WslAddCommand("C:\\Users\\Me\\FamiTracker\\famitracker-mcp.exe"),
		std::string("claude mcp remove --scope user famitracker >/dev/null 2>&1; claude mcp add --transport stdio --scope user "
		"famitracker -- \"$(wslpath 'C:\\Users\\Me\\FamiTracker\\famitracker-mcp.exe')\""));
	CHECK_HAS(WslAddCommand("C:\\Users\\O'Neil\\FT\\famitracker-mcp.exe"), "$(wslpath 'C:\\Users\\O'\\''Neil\\FT\\famitracker-mcp.exe')");
}

// FamiTracker's endpoint on a free port of 127.0.0.1, on a thread of its own,
// one request per connection (as famitracker-mcp asks)
#ifdef _WIN32
typedef SOCKET test_socket_t;
const test_socket_t NO_TEST_SOCKET = INVALID_SOCKET;
void CloseTestSocket(test_socket_t Socket) { closesocket(Socket); }
#else
typedef int test_socket_t;
const test_socket_t NO_TEST_SOCKET = -1;
void CloseTestSocket(test_socket_t Socket) { close(Socket); }
#endif

class TestEndpoint {
public:
	TestEndpoint(ToolSet &Tools, const McpConfig &Config) : m_Tools(Tools), m_Config(Config), m_Listener(NO_TEST_SOCKET), m_iPort(0),
		m_bStop(false), m_iAnswered(0) {}
	~TestEndpoint() { Stop(); }

	bool Start()
	{
#ifdef _WIN32
		WSADATA Data;
		if (WSAStartup(MAKEWORD(2, 2), &Data) != 0)
			return false;
#endif
		m_Listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (m_Listener == NO_TEST_SOCKET)
			return false;
		sockaddr_in Address;
		std::memset(&Address, 0, sizeof(Address));
		Address.sin_family = AF_INET;
		Address.sin_port = 0;	// any free port
		Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		socklen_t Size = sizeof(Address);
		if (bind(m_Listener, reinterpret_cast<const sockaddr*>(&Address), sizeof(Address)) != 0 || listen(m_Listener, 8) != 0 ||
			getsockname(m_Listener, reinterpret_cast<sockaddr*>(&Address), &Size) != 0)
			return false;
		m_iPort = ntohs(Address.sin_port);
		m_Thread = std::thread(&TestEndpoint::Run, this);
		return true;
	}

	void Stop()
	{
		m_bStop = true;
		if (m_Thread.joinable())
			m_Thread.join();
		if (m_Listener != NO_TEST_SOCKET) {
			CloseTestSocket(m_Listener);
			m_Listener = NO_TEST_SOCKET;
		}
	}

	int Port() const { return m_iPort; }
	int Answered() const { return m_iAnswered; }

private:
	void Run()
	{
		while (!m_bStop) {
			fd_set Readable;
			FD_ZERO(&Readable);
			FD_SET(m_Listener, &Readable);
			timeval Wait = { 0, 20000 };
			if (select(static_cast<int>(m_Listener) + 1, &Readable, 0, 0, &Wait) <= 0)
				continue;
			const test_socket_t Client = accept(m_Listener, 0, 0);
			if (Client == NO_TEST_SOCKET)
				continue;
			HttpRequestParser Parser;
			HttpRequestParser::Result Result = HttpRequestParser::NEED_MORE;
			char Buffer[4096];
			while (Result == HttpRequestParser::NEED_MORE) {
				const long Count = static_cast<long>(recv(Client, Buffer, static_cast<int>(sizeof(Buffer)), 0));
				if (Count <= 0)
					break;
				Result = Parser.Feed(Buffer, static_cast<size_t>(Count));
			}
			if (Result == HttpRequestParser::COMPLETE) {
				const std::string Bytes = FormatHttpResponse(HandleMcp(Parser.Take(), m_Config, m_Tools), false);
				++m_iAnswered;	// before the answer goes out: the client may check as soon as it has it
				for (size_t Sent = 0; Sent < Bytes.size(); ) {
					const long Count = static_cast<long>(send(Client, Bytes.data() + Sent, static_cast<int>(Bytes.size() - Sent), 0));
					if (Count <= 0)
						break;
					Sent += static_cast<size_t>(Count);
				}
			}
			CloseTestSocket(Client);
		}
	}

	ToolSet &m_Tools;
	McpConfig m_Config;
	test_socket_t m_Listener;
	int m_iPort;
	std::atomic<bool> m_bStop;
	std::atomic<int> m_iAnswered;
	std::thread m_Thread;
};

// famitracker-mcp's side over real sockets
void TestBridgeSockets()
{
	FakeSong Song;
	ToolSet Tools(Song);
	McpConfig Config;
	Config.Token = "t";
	TestEndpoint Endpoint(Tools, Config);
	if (!Endpoint.Start()) {
		Check(false, "the test endpoint couldn't start", __LINE__);
		return;
	}
	const int Port = Endpoint.Port();
	auto Exchange = [&](const std::string &Line, std::string &Error) {
		BridgeMessage Message;
		BridgeRequest(Line, Port, "t", Message);
		HttpResponse Answer;
		const BridgeExchangeResult Result = BridgeExchange(Port, FormatHttpRequest(Message.Request), 5000, Answer, Error);
		return Result == EXCHANGE_ANSWERED ? BridgeAnswer(Message, Answer) : "(" + Error + ")";
	};
	std::string Error;
	std::string Out = Exchange("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-11-25\"}}", Error);
	CHECK(OneLine(Out));
	CHECK_HAS(Out, "\"protocolVersion\":\"2025-11-25\"");
	CHECK_HAS(Out, "\"name\":\"famitracker\"");
	// The tool list is longer than one read
	Out = Exchange("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\"}", Error);
	CHECK(OneLine(Out) && Out.size() > 16384);
	CHECK_HAS(Out, "\"name\":\"write_notes\"");
	// So is a long request
	Out = Exchange("{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"ping\",\"params\":{\"padding\":\"" + std::string(200000, 'x') + "\"}}", Error);
	CHECK_EQ(Out, std::string("{\"jsonrpc\":\"2.0\",\"id\":3,\"result\":{}}\n"));
	CHECK_EQ(Endpoint.Answered(), 3);
	Endpoint.Stop();

	// Nobody listens: FamiTracker isn't running
	BridgeMessage Message;
	BridgeRequest("{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"ping\"}", Port, "t", Message);
	HttpResponse Answer;
	CHECK_EQ(BridgeExchange(Port, FormatHttpRequest(Message.Request), 5000, Answer, Error), EXCHANGE_UNREACHABLE);
	CHECK_HAS(Error, "Nothing listens on 127.0.0.1:");
}

}	// namespace

void TestLushChords()
{
	const Key C(0, MODE_MAJOR);
	// The shapes: root position and close, ninths and up an octave where a third carries the chord
	ftapi::Chord Q;
	CHECK(ParseChord("Cmaj7", C, Q) && LushShape(Q, BROKEN_UP) == std::vector<int>({ 0, 4, 7, 11 }));
	CHECK(LushShape(Q, BROKEN_DOWN) == std::vector<int>({ 11, 7, 4, 0 }) && LushShape(Q, BROKEN_UPDOWN) == std::vector<int>({ 0, 4, 7, 11, 7, 4 }));
	CHECK(LushShape(Q, BROKEN_ALBERTI) == std::vector<int>({ 0, 7, 4, 11 }) && LushShape(Q, BROKEN_ROLLING) == std::vector<int>({ 0, 7, 16, 23 }));
	CHECK(ParseChord("Am9", C, Q) && LushShape(Q, BROKEN_UP) == std::vector<int>({ 0, 3, 7, 10, 14 }));
	// Rolling, the minor third would land a semitone over the ninth: the ninth goes up an octave, since without the
	// third the chord would sound like a 7sus2
	CHECK(LushShape(Q, BROKEN_ROLLING) == std::vector<int>({ 0, 7, 15, 22, 26 }));
	CHECK(ParseChord("Cmadd9", C, Q) && LushShape(Q, BROKEN_ROLLING) == std::vector<int>({ 0, 7, 15, 26 }));
	CHECK(ParseChord("C9", C, Q) && LushShape(Q, BROKEN_ROLLING) == std::vector<int>({ 0, 7, 14, 16, 22 }));
	CHECK(ParseChord("Cadd9", C, Q) && LushShape(Q, BROKEN_UP) == std::vector<int>({ 0, 4, 7, 14 }));
	CHECK(ParseChord("Csus2", C, Q) && LushShape(Q, BROKEN_UP) == std::vector<int>({ 0, 2, 7 }));
	CHECK(ParseChord("G7sus4", C, Q) && LushShape(Q, BROKEN_UP) == std::vector<int>({ 0, 5, 7, 10 }));
	CHECK(ParseChord("C6", C, Q) && LushShape(Q, BROKEN_UP) == std::vector<int>({ 0, 4, 7, 9 }));
	CHECK(ParseChord("C13", C, Q) && LushShape(Q, BROKEN_UP) == std::vector<int>({ 0, 4, 7, 10, 14, 21 }));
	CHECK(ParseChord("Bdim7", C, Q) && LushShape(Q, BROKEN_UP) == std::vector<int>({ 0, 3, 6, 9 }));
	CHECK(ParseChord("Bm7b5", C, Q) && LushShape(Q, BROKEN_UP) == std::vector<int>({ 0, 3, 6, 10 }));
	CHECK(ParseChord("E5", C, Q) && LushShape(Q, BROKEN_UP) == std::vector<int>({ 0, 7, 12 }));
	CHECK(ParseChord("Dm", C, Q) && LushShape(Q, BROKEN_UP) == std::vector<int>({ 0, 3, 7 }));

	// The part: held on each chord's root, near the one before, the arpeggio left to the instrument
	std::vector<ChordAt> Chords = Progression("| C | Am7 | Fmaj7 | Dm7 G7 |");
	ChordPartOptions O;
	O.Style = CHORDS_LUSH;
	O.Low = 31;
	O.High = 43;
	O.Volume = 6;
	std::vector<PartNote> Part = ChordPart(Chords, FourFour(4), O);
	CHECK(Part.size() == 5 && Part[0].Row == 0 && Part[0].Rows == 16 && Part[0].Volume == 6);
	CHECK(Part[0].Pitch == 36 && Part[1].Pitch == 33 && Part[2].Pitch == 41 && Part[3].Pitch == 38 && Part[4].Pitch == 43);
	for (size_t i = 0; i < Part.size(); ++i)
		CHECK(Part[i].Effects.empty() && Part[i].EndEffects.empty());
	CHECK(Part[1].Arpeggio == std::vector<int>({ 0, 3, 7, 10 }) && Part[4].Arpeggio == std::vector<int>({ 0, 4, 7, 10 }));
	O.Rhythm = RHYTHM_HALF;
	CHECK(ChordPart(Chords, FourFour(4), O).size() == 8);

	// The tool: plain chords get color (rich), an instrument a shape, two frames a note, soft
	FakeSong Song;
	Song.Info.DirectChanges = true;
	ToolResult Result = Run(Song, "add_chords_and_bass", "{\"chords\": \"| C | Am | F | G |\", \"chord_style\": \"lush\", "
		"\"key\": \"C major\", \"bass_channel\": \"none\"}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "lush chords on 2A03 Pulse 1, bars 1-4, in C major");
	CHECK_HAS(Result.Text, "Chords: | Cadd9 | Am9 | Fmaj7 | G7 |");
	CHECK_HAS(Result.Text, "Added 4 lush chord instruments (2A03, from the preset \"lush chords\"): \"Lush add9\" (03), "
		"\"Lush m9\" (04), \"Lush maj7\" (05), \"Lush 7\" (06).");
	const PresetSequence *pArp = SequenceOf(Song.Made[3], SEQUENCE_ARPEGGIO);
	CHECK(pArp != 0 && pArp->Items == std::vector<int>({ 0, 0, 4, 4, 7, 7, 14, 14 }) && pArp->Loop == 0 && pArp->Setting == SEQUENCE_ABSOLUTE);
	CHECK(SequenceOf(Song.Made[3], SEQUENCE_VOLUME) != 0 && SequenceOf(Song.Made[3], SEQUENCE_DUTY) != 0);
	CHECK(Song.Made[3].SequencesVRC6.size() == Song.Made[3].Sequences2A03.size());
	CHECK(Song.Rows[0][0].HasNote() && Song.Rows[0][0].Pitch() % 12 == 0 && Song.Rows[0][0].Instrument == 3 && Song.Rows[0][0].Volume == 6);
	CHECK(Song.Rows[0][16].Instrument == 4 && Song.Rows[0][16].Pitch() % 12 == 9 && Song.Rows[0][48].Instrument == 6);
	bool Arpeggio = false;
	for (int r = 0; r < 64; ++r)
		Arpeggio = Arpeggio || Song.Rows[0][r].Effect[0] == EFF_ARPEGGIO || Song.Rows[0][r].Effect[1] == EFF_ARPEGGIO;
	CHECK(!Arpeggio);
	// Again: the instruments are there already
	Result = Run(Song, "add_chords_and_bass", "{\"chords\": \"| C | Am |\", \"chord_style\": \"lush\", \"key\": \"C major\", "
		"\"bass_channel\": \"none\", \"from_bar\": 5, \"to_bar\": 6}");
	CHECK(!Result.IsError && Result.Text.find("lush chord instrument") == std::string::npos && Song.Rows[0][64].Instrument == 3);
	// Slower, another order, another sound: instruments of their own
	Result = Run(Song, "add_chords_and_bass", "{\"chords\": \"Fmaj7\", \"chord_style\": \"lush\", \"pattern\": \"down\", "
		"\"arp_frames\": 3, \"chord_instrument\": \"pluck\", \"key\": \"C major\", \"bass_channel\": \"none\", \"from_bar\": 7, \"to_bar\": 7}");
	CHECK_HAS(Result.Text, "lush (down) chords on 2A03 Pulse 1");
	CHECK_HAS(Result.Text, "\"Pluck maj7 down, 3 frames\" (07)");
	pArp = SequenceOf(Song.Made[7], SEQUENCE_ARPEGGIO);
	CHECK(pArp != 0 && pArp->Items == std::vector<int>({ 11, 11, 11, 7, 7, 7, 4, 4, 4, 0, 0, 0 }));
	CHECK_HAS(Run(Song, "add_chords_and_bass", "{\"chords\": \"C\", \"chord_style\": \"lush\", \"chord_instrument\": \"kazoo\", "
		"\"key\": \"C major\", \"bass_channel\": \"none\"}").Text, "made from a preset's sound");
	CHECK_HAS(Run(Song, "add_chords_and_bass", "{\"chords\": \"C\", \"chord_style\": \"lush\", \"chord_instrument\": \"snare\", "
		"\"key\": \"C major\", \"bass_channel\": \"none\"}").Text, "\"snare\" isn't one");
	// Plain when asked
	Result = Run(Song, "add_chords_and_bass", "{\"chords\": \"C\", \"chord_style\": \"lush\", \"color\": \"plain\", "
		"\"key\": \"C major\", \"bass_channel\": \"none\", \"from_bar\": 8, \"to_bar\": 8}");
	CHECK_HAS(Result.Text, "\"Lush major\"");

	// The instruments report the chords their arpeggios play, and the analyses hear them
	CHECK(Song.Info.Instruments[3].Arpeggio == std::vector<int>({ 4, 7, 14 }) && Song.Info.Instruments[1].Arpeggio.empty());
	const char *const TUNE[] = { "E-4", "G-4", "D-4", "C-4" };
	for (int b = 0; b < 4; ++b)
		Song.Put(5, b * 16, TUNE[b], b == 0 ? 0 : NO_INSTRUMENT);
	Song.End(5, 64, NOTE_RELEASE);
	Result = Run(Song, "review_arrangement", "{\"to_bar\": 4}");
	CHECK_HAS(Result.Text, "in C major");
	CHECK_HAS(Result.Text, "chords on 2A03 Pulse 1");
	CHECK_HAS(Result.Text, "Fmaj7");
	{
		// An echo note in the chord isn't a rub: the F# of a Gmaj7 stays
		FakeSong Rub;
		Rub.Info.DirectChanges = true;
		CHECK(!Run(Rub, "add_chords_and_bass", "{\"chords\": \"Gmaj7\", \"chord_style\": \"lush\", \"key\": \"G major\", "
			"\"bass_channel\": \"none\", \"to_bar\": 1}").IsError);
		Rub.Put(5, 0, "F#4", 0);
		Rub.Put(5, 4, "B-4");
		Rub.Put(5, 8, "F#4");
		Rub.Put(5, 12, "D-4");
		Rub.End(5, 16, NOTE_RELEASE);
		Result = Run(Rub, "add_echo", "{\"channel\": \"VRC6 Pulse 1\", \"to_channel\": \"VRC6 Pulse 2\", \"to_bar\": 1, \"delay_beats\": 1}");
		CHECK(!Result.IsError && Result.Text.find("left out") == std::string::npos && Rub.Rows[6][4].Pitch() == Rub.Rows[5][0].Pitch());
	}
	{
		// A chord tone in the echo still grates against a passing note the melody moves to: the
		// echo's E-4 under the melody's F-4 is left out, and so is its F-4 against the chord's E
		FakeSong Pass;
		Pass.Info.DirectChanges = true;
		CHECK(!Run(Pass, "add_chords_and_bass", "{\"chords\": \"C\", \"chord_style\": \"lush\", \"key\": \"C major\", "
			"\"bass_channel\": \"none\", \"to_bar\": 1}").IsError);
		Pass.Put(5, 0, "E-4", 0);
		Pass.Put(5, 4, "F-4");
		Pass.Put(5, 8, "G-4");
		Pass.End(5, 12, NOTE_RELEASE);
		Result = Run(Pass, "add_echo", "{\"channel\": \"VRC6 Pulse 1\", \"to_channel\": \"VRC6 Pulse 2\", \"to_bar\": 1, \"delay_beats\": 1}");
		CHECK(!Result.IsError);
		CHECK_HAS(Result.Text, "2 echo notes were left out");
		CHECK(!Pass.Rows[6][4].HasNote() && !Pass.Rows[6][8].HasNote() && Pass.Rows[6][12].Pitch() == Pass.Rows[5][8].Pitch());
	}

	// Into another mode: the chords move with the key and get the instruments for their new shapes
	{
		FakeSong Minor;
		Minor.Info.DirectChanges = true;
		CHECK(!Run(Minor, "add_chords_and_bass", "{\"chords\": \"| C | Am | F | G |\", \"chord_style\": \"lush\", "
			"\"key\": \"C major\", \"bass_channel\": \"none\"}").IsError);
		Result = Run(Minor, "transpose", "{\"to_mode\": \"minor\", \"key\": \"C major\"}");
		CHECK(!Result.IsError);
		CHECK_HAS(Result.Text, "4 lush chords now play the instrument for the chord's new shape");
		auto Named = [&](const char *Name) {
			for (size_t i = 0; i < Minor.Info.Instruments.size(); ++i)
				if (Minor.Info.Instruments[i].Name == Name)
					return Minor.Info.Instruments[i].Index;
			return -1;
		};
		CHECK(Named("Lush m(add9)") >= 0 && Named("Lush maj9") >= 0 && Named("Lush m7") >= 0);
		CHECK(Minor.Rows[0][0].Pitch() % 12 == 0 && Minor.Rows[0][0].Instrument == Named("Lush m(add9)"));
		CHECK(Minor.Rows[0][16].Pitch() % 12 == 8 && Minor.Rows[0][16].Instrument == Named("Lush maj9"));
		CHECK(Minor.Rows[0][32].Instrument == Named("Lush m7") && Minor.Rows[0][48].Pitch() % 12 == 7);
		CHECK(Minor.Rows[0][48].Instrument == NO_INSTRUMENT || Minor.Rows[0][48].Instrument == Named("Lush m7"));
		const PresetSequence *pMinor9 = SequenceOf(Minor.Made[static_cast<int>(Named("Lush m(add9)"))], SEQUENCE_ARPEGGIO);
		CHECK(pMinor9 != 0 && pMinor9->Items == std::vector<int>({ 0, 0, 3, 3, 7, 7, 14, 14 }));
		// Back again
		Result = Run(Minor, "transpose", "{\"to_mode\": \"major\", \"key\": \"C minor\"}");
		CHECK_HAS(Result.Text, "lush chords now play the instrument");
		CHECK(Minor.Rows[0][0].Instrument == Named("Lush add9") && Minor.Rows[0][32].Instrument == Named("Lush maj7"));
	}

	{
		// The chord's own notes in the melody aren't rubs: C# over Dmaj7 is its seventh
		FakeSong Color;
		Color.Info.DirectChanges = true;
		Color.Put(5, 0, "C#4", 0);
		Color.End(5, 16, NOTE_RELEASE);
		Result = Run(Color, "add_chords_and_bass", "{\"chords\": \"Dmaj7\", \"chord_style\": \"lush\", \"key\": \"D major\", "
			"\"to_bar\": 1, \"melody_channel\": \"VRC6 Pulse 1\"}");
		CHECK(!Result.IsError && Result.Text.find("rubs a semitone") == std::string::npos);
		// An echo note of the chord over its bass root stays (F# over Gmaj7's G)
		FakeSong Over;
		Over.Info.DirectChanges = true;
		Over.Put(5, 0, "F#4", 0);
		Over.Put(5, 8, "D-4");
		Over.End(5, 16, NOTE_RELEASE);
		CHECK(!Run(Over, "add_chords_and_bass", "{\"chords\": \"Gmaj7\", \"chord_style\": \"lush\", \"key\": \"G major\", "
			"\"to_bar\": 1, \"melody_channel\": \"VRC6 Pulse 1\", \"bass_style\": \"roots\"}").IsError);
		CHECK(Over.Rows[2][0].HasNote() && Over.Rows[2][0].Pitch() % 12 == 7);
		Result = Run(Over, "add_echo", "{\"channel\": \"VRC6 Pulse 1\", \"to_channel\": \"VRC6 Pulse 2\", \"to_bar\": 1, \"delay_beats\": 1}");
		CHECK(Result.Text.find("left out") == std::string::npos && Over.Rows[6][4].Pitch() == Over.Rows[5][0].Pitch());
		// polish leaves a vibrato built into the instrument alone
		FakeSong Sung;
		Sung.Info.DirectChanges = true;
		CHECK(!Run(Sung, "write_notes", "{\"channel\": \"VRC6 Pulse 1\", \"instrument\": \"singing lead\", \"notes\": ["
			"{\"bar\": 1, \"pitch\": \"C-4\", \"beats\": 4}, {\"bar\": 2, \"pitch\": \"E-4\", \"beats\": 4}]}").IsError);
		Sung.Info.Instruments.back().Vibrato = true;	// as FamiTracker reports the preset's pitch sequence
		Result = Run(Sung, "polish", "{\"touches\": [\"vibrato\"], \"melody_channel\": \"VRC6 Pulse 1\"}");
		bool Vibrato = false;
		for (int r = 0; r < 32; ++r)
			Vibrato = Vibrato || Sung.Rows[5][r].Effect[0] == EFF_VIBRATO || Sung.Rows[5][r].Effect[1] == EFF_VIBRATO;
		CHECK(!Vibrato);
	}

	// A VRC7 song: lush chords go on a 2A03 pulse; an FM channel can't play them
	FakeSong Fm;
	Fm.Info.DirectChanges = true;
	CHECK(!Run(Fm, "set_expansion", "{\"chip\": \"vrc7\", \"confirm\": true}").IsError);
	CHECK_HAS(Run(Fm, "add_chords_and_bass", "{\"chords\": \"C\", \"chord_style\": \"lush\", \"chord_channel\": \"FM Channel 1\", "
		"\"key\": \"C major\", \"bass_channel\": \"none\"}").Text, "chord_channel needs a 2A03, MMC5 or VRC6 pulse channel");
	Result = Run(Fm, "add_chords_and_bass", "{\"chords\": \"C\", \"chord_style\": \"lush\", \"key\": \"C major\", \"bass_channel\": \"none\"}");
	CHECK_HAS(Result.Text, "lush chords on 2A03 Pulse 1");

	// The nostalgic groove plays lush chords
	FakeSong Groove(4);
	Groove.Info.DirectChanges = true;
	Result = Run(Groove, "make_groove", "{\"mood\": \"wistful\", \"bars\": 4}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "a nostalgic groove: lush (half) chords on 2A03 Pulse 1");
	CHECK_HAS(Result.Text, "Chords: | Fmaj7 | G7 | Em7 | Am7 |");
	CHECK_HAS(Result.Text, "lush chord instruments");
	CHECK(Groove.Rows[0][0].HasNote() && Groove.Rows[0][8].HasNote() && Groove.Rows[0][0].Volume == 6);
	// A melody over lush chords: the key heard from both (the chords' sevenths too)
	FakeSong Heard(4);
	Heard.Info.DirectChanges = true;
	CHECK(!Run(Heard, "make_groove", "{\"mood\": \"nostalgic\", \"key\": \"D major\", \"bars\": 16}").IsError);
	CHECK(!Run(Heard, "write_notes", "{\"channel\": \"VRC6 Pulse 1\", \"instrument\": \"singing lead\", \"notes\": [{\"bar\": 1, \"beat\": 1, \"pitch\": \"F#4\", \"beats\": 1.5}, {\"bar\": 1, \"beat\": 2.5, \"pitch\": \"E-4\", \"beats\": 0.5}, {\"bar\": 1, \"beat\": 3, \"pitch\": \"D-4\", \"beats\": 1}, {\"bar\": 1, \"beat\": 4, \"pitch\": \"B-3\", \"beats\": 1}, {\"bar\": 2, \"beat\": 1, \"pitch\": \"C#4\", \"beats\": 1.5}, {\"bar\": 2, \"beat\": 2.5, \"pitch\": \"E-4\", \"beats\": 0.5}, {\"bar\": 2, \"beat\": 3, \"pitch\": \"A-4\", \"beats\": 2}, {\"bar\": 3, \"beat\": 1, \"pitch\": \"E-4\", \"beats\": 1.5}, {\"bar\": 3, \"beat\": 2.5, \"pitch\": \"F#4\", \"beats\": 0.5}, {\"bar\": 3, \"beat\": 3, \"pitch\": \"C#5\", \"beats\": 1.5}, {\"bar\": 3, \"beat\": 4.5, \"pitch\": \"B-4\", \"beats\": 0.5}, {\"bar\": 4, \"beat\": 1, \"pitch\": \"A-4\", \"beats\": 2}, {\"bar\": 4, \"beat\": 3, \"pitch\": \"F#4\", \"beats\": 1}, {\"bar\": 5, \"beat\": 1, \"pitch\": \"F#4\", \"beats\": 1.5}, {\"bar\": 5, \"beat\": 2.5, \"pitch\": \"E-4\", \"beats\": 0.5}, {\"bar\": 5, \"beat\": 3, \"pitch\": \"D-4\", \"beats\": 1}, {\"bar\": 5, \"beat\": 4, \"pitch\": \"B-3\", \"beats\": 1}, {\"bar\": 6, \"beat\": 1, \"pitch\": \"C#4\", \"beats\": 1.5}, {\"bar\": 6, \"beat\": 2.5, \"pitch\": \"E-4\", \"beats\": 0.5}, {\"bar\": 6, \"beat\": 3, \"pitch\": \"A-4\", \"beats\": 2}, {\"bar\": 7, \"beat\": 1, \"pitch\": \"E-4\", \"beats\": 1.5}, {\"bar\": 7, \"beat\": 2.5, \"pitch\": \"F#4\", \"beats\": 0.5}, {\"bar\": 7, \"beat\": 3, \"pitch\": \"A-4\", \"beats\": 1}, {\"bar\": 7, \"beat\": 4, \"pitch\": \"C#5\", \"beats\": 1}, {\"bar\": 8, \"beat\": 1, \"pitch\": \"D-5\", \"beats\": 3}, {\"bar\": 9, \"beat\": 1, \"pitch\": \"B-4\", \"beats\": 1}, {\"bar\": 9, \"beat\": 2, \"pitch\": \"D-5\", \"beats\": 1}, {\"bar\": 9, \"beat\": 3, \"pitch\": \"F#5\", \"beats\": 1.5}, {\"bar\": 9, \"beat\": 4.5, \"pitch\": \"E-5\", \"beats\": 0.5}, {\"bar\": 10, \"beat\": 1, \"pitch\": \"C#5\", \"beats\": 1.5}, {\"bar\": 10, \"beat\": 2.5, \"pitch\": \"B-4\", \"beats\": 0.5}, {\"bar\": 10, \"beat\": 3, \"pitch\": \"A-4\", \"beats\": 1}, {\"bar\": 10, \"beat\": 4, \"pitch\": \"G-4\", \"beats\": 1}, {\"bar\": 11, \"beat\": 1, \"pitch\": \"F#4\", \"beats\": 1.5}, {\"bar\": 11, \"beat\": 2.5, \"pitch\": \"A-4\", \"beats\": 0.5}, {\"bar\": 11, \"beat\": 3, \"pitch\": \"C#5\", \"beats\": 1}, {\"bar\": 11, \"beat\": 4, \"pitch\": \"E-5\", \"beats\": 1}, {\"bar\": 12, \"beat\": 1, \"pitch\": \"F#5\", \"beats\": 2}, {\"bar\": 12, \"beat\": 3, \"pitch\": \"D-5\", \"beats\": 1}, {\"bar\": 13, \"beat\": 1, \"pitch\": \"F#4\", \"beats\": 1.5}, {\"bar\": 13, \"beat\": 2.5, \"pitch\": \"E-4\", \"beats\": 0.5}, {\"bar\": 13, \"beat\": 3, \"pitch\": \"D-4\", \"beats\": 1}, {\"bar\": 13, \"beat\": 4, \"pitch\": \"B-3\", \"beats\": 1}, {\"bar\": 14, \"beat\": 1, \"pitch\": \"C#4\", \"beats\": 1.5}, {\"bar\": 14, \"beat\": 2.5, \"pitch\": \"E-4\", \"beats\": 0.5}, {\"bar\": 14, \"beat\": 3, \"pitch\": \"G-4\", \"beats\": 1}, {\"bar\": 14, \"beat\": 4, \"pitch\": \"A-4\", \"beats\": 1}, {\"bar\": 15, \"beat\": 1, \"pitch\": \"A-4\", \"beats\": 1}, {\"bar\": 15, \"beat\": 2, \"pitch\": \"C#5\", \"beats\": 1}, {\"bar\": 15, \"beat\": 3, \"pitch\": \"E-5\", \"beats\": 1.5}, {\"bar\": 15, \"beat\": 4.5, \"pitch\": \"C#5\", \"beats\": 0.5}, {\"bar\": 16, \"beat\": 1, \"pitch\": \"D-5\", \"beats\": 3}]}").IsError);
	CHECK_HAS(Run(Heard, "review_arrangement", "{}").Text, "bars 1-16 in D major");
}

void TestSwing()
{
	// Eighths at speed 6: 8 then 4 (a triplet feel), a speed change every half beat on the last channel
	FakeSong Song;
	Song.Info.DirectChanges = true;
	Song.Put(5, 0, "C-4", 0);
	ToolResult Result = Run(Song, "set_swing", "{\"amount\": \"hard\"}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "a 67% swing on the eighths, bars 1-8: speed 8 then 4 (F08, F04) in turn, which averages the speed it had (6)");
	CHECK(Song.Rows[7][0].Effect[0] == EFF_SPEED && Song.Rows[7][0].Param[0] == 8);
	CHECK(Song.Rows[7][2].Effect[0] == EFF_SPEED && Song.Rows[7][2].Param[0] == 4);
	CHECK(Song.Rows[7][1].Effect[0] == EFF_NONE && Song.Rows[7][4].Param[0] == 8 && Song.Rows[7][126].Param[0] == 4);
	// Lighter: swung again around the same speed
	Result = Run(Song, "set_swing", "{\"amount\": \"light\"}");
	CHECK_HAS(Result.Text, "a 58% swing on the eighths, bars 1-8: speed 7 then 5");
	CHECK(Song.Rows[7][0].Param[0] == 7 && Song.Rows[7][2].Param[0] == 5);
	CHECK_HAS(Run(Song, "set_swing", "{\"amount\": \"medium\"}").Text, "swings that way there already (F07 and F05)");
	// Sixteenths in one bar: every row; the bar after starts as it did
	Result = Run(Song, "set_swing", "{\"amount\": 67, \"unit\": \"sixteenths\", \"from_bar\": 2, \"to_bar\": 2}");
	CHECK_HAS(Result.Text, "a 67% swing on the sixteenths, bar 2");
	CHECK(Song.Rows[7][16].Param[0] == 8 && Song.Rows[7][17].Param[0] == 4 && Song.Rows[7][18].Param[0] == 8 && Song.Rows[7][32].Param[0] == 7);
	// Off again: the track's speed plays
	Result = Run(Song, "set_swing", "{\"amount\": \"none\"}");
	CHECK_HAS(Result.Text, "swing taken off, bars 1-8: straight at speed 6");
	CHECK(Song.Rows[7][0].Effect[0] == EFF_NONE && Song.Rows[7][2].Effect[0] == EFF_NONE && Song.Rows[7][17].Effect[0] == EFF_NONE);
	CHECK_HAS(Run(Song, "set_swing", "{\"amount\": \"none\"}").Text, "plays straight there already");

	// Speed 3 splits a beat coarsely: a hard swing fits, a light one doesn't
	FakeSong Fast;
	Fast.Info.DirectChanges = true;
	Fast.Info.Speed = 3;
	CHECK_HAS(Run(Fast, "set_swing", "{\"amount\": \"light\"}").Text,
		"At speed 3 a beat can't swing that little; the swings that fit are 67%, 83%.");
	CHECK_HAS(Run(Fast, "set_swing", "{\"amount\": \"hard\"}").Text, "speed 4 then 2");
	// At speed 9 a light swing is 61% (11 then 7): 56% hardly swings. A percentage can come as text too
	FakeSong Nine;
	Nine.Info.DirectChanges = true;
	Nine.Info.Speed = 9;
	CHECK_HAS(Run(Nine, "set_swing", "{\"amount\": \"light\"}").Text, "speed 11 then 7");
	CHECK_HAS(Run(Nine, "set_swing", "{\"amount\": \"67%\"}").Text, "speed 12 then 6");
	CHECK_HAS(Run(Nine, "set_swing", "{\"amount\": \"61\"}").Text, "speed 11 then 7");
	CHECK_HAS(Run(Nine, "set_swing", "{\"amount\": \"90%\"}").Text, "amount: a percentage from 50 (straight) to 75");
	FakeSong Slowest;
	Slowest.Info.DirectChanges = true;
	Slowest.Info.Speed = 1;
	CHECK_HAS(Run(Slowest, "set_swing", "{\"amount\": \"hard\"}").Text, "At speed 1 a beat can't swing");

	// The song's own speed command: replaced there, and played again after the bars, on its channel
	FakeSong Mid;
	Mid.Info.DirectChanges = true;
	Mid.Rows[0][0].Effect[0] = EFF_SPEED;
	Mid.Rows[0][0].Param[0] = 5;
	Result = Run(Mid, "set_swing", "{\"amount\": \"hard\", \"to_bar\": 1}");
	CHECK_HAS(Result.Text, "a 70% swing on the eighths, bar 1: speed 7 then 3");
	CHECK_HAS(Result.Text, "the swings that fit are 60%, 70%, 80%, 90%");
	CHECK(Mid.Rows[0][0].Param[0] == 7 && Mid.Rows[0][2].Param[0] == 3 && Mid.Rows[0][16].Effect[0] == EFF_SPEED && Mid.Rows[0][16].Param[0] == 5);
	CHECK(Mid.Rows[7][0].Effect[0] == EFF_NONE);

	// Beats that don't halve; no direct changes; no amount
	FakeSong Three;
	Three.Info.DirectChanges = true;
	Three.Info.FirstHighlight = 3;
	Three.Info.SecondHighlight = 12;
	CHECK_HAS(Run(Three, "set_swing", "{\"amount\": \"light\"}").Text, "doesn't split into halves");
	FakeSong Ask;
	CHECK_HAS(Run(Ask, "set_swing", "{\"amount\": \"light\"}").Text, "needs direct changes");
	CHECK_HAS(Run(Song, "set_swing", "{}").Text, "Give amount");
	CHECK_HAS(Run(Song, "set_swing", "{\"amount\": \"wobbly\"}").Text, "amount: none, light");

	// By delays, on one channel: its notes on the second sixteenth of each pair start late, the others stay straight
	FakeSong Shuffle;
	Shuffle.Info.DirectChanges = true;
	for (int r = 0; r < 16; ++r)
		Shuffle.Put(3, r, "E-#");			// sixteenth hi-hats on the noise
	Shuffle.Put(5, 0, "C-4", 0);
	Shuffle.Put(5, 2, "D-4");
	Result = Run(Shuffle, "set_swing", "{\"amount\": \"hard\", \"unit\": \"sixteenths\", \"by\": \"delays\", \"channels\": "
		"\"Noise\", \"to_bar\": 1}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "a 67% swing on the sixteenths of 2A03 Noise, bar 1, by delays: 8 notes late by 2 frames (G02)");
	CHECK_HAS(Result.Text, "The other channels play straight");
	CHECK(HasEffectAt(Shuffle, 3, 1, EFF_DELAY, 2) && HasEffectAt(Shuffle, 3, 15, EFF_DELAY, 2) && !HasEffectAt(Shuffle, 3, 2, EFF_DELAY, 2));
	CHECK(!HasEffectAt(Shuffle, 5, 2, EFF_DELAY, 2));
	for (int r = 0; r < 128; ++r)
		for (int c = 0; c < 8; ++c)
			CHECK(!HasEffectAt(Shuffle, c, r, EFF_SPEED, 8) && !HasEffectAt(Shuffle, c, r, EFF_SPEED, 4));	// no speed changes
	// Eighths: the "and" of each beat, by a third of half a beat; none takes the delays off
	Result = Run(Shuffle, "set_swing", "{\"amount\": \"hard\", \"by\": \"delays\", \"channels\": \"Noise\", \"to_bar\": 1}");
	CHECK_HAS(Result.Text, "a 67% swing on the eighths of 2A03 Noise, bar 1, by delays: 4 notes late by 4 frames (G04)");
	CHECK(HasEffectAt(Shuffle, 3, 2, EFF_DELAY, 4) && HasEffectAt(Shuffle, 3, 1, EFF_DELAY, 2));	// the sixteenths' stay
	Result = Run(Shuffle, "set_swing", "{\"amount\": \"none\", \"by\": \"delays\", \"channels\": \"Noise\", \"to_bar\": 1}");
	CHECK_HAS(Result.Text, "the delays of a swing taken off 2A03 Noise, bar 1");
	CHECK(!HasEffectAt(Shuffle, 3, 2, EFF_DELAY, 4) && HasEffectAt(Shuffle, 3, 1, EFF_DELAY, 2));
	CHECK(!Run(Shuffle, "set_swing", "{\"amount\": \"none\", \"unit\": \"sixteenths\", \"by\": \"delays\", \"channels\": \"Noise\"}").IsError);
	CHECK(!HasEffectAt(Shuffle, 3, 1, EFF_DELAY, 2));
	CHECK_HAS(Run(Shuffle, "set_swing", "{\"amount\": \"hard\", \"by\": \"magic\"}").Text, "by: speed");
}

void TestMix()
{
	// A melody, its echo, chords, a second line, a bass on the triangle and drums
	FakeSong Song;
	Song.Info.DirectChanges = true;
	const char *const TUNE[] = { "C-4", "E-4", "G-4", "E-4", "F-4", "A-4", "C-5", "A-4" };
	for (int i = 0; i < 8; ++i) {
		Song.Put(5, i * 4, TUNE[i], i == 0 ? 0 : NO_INSTRUMENT, i == 4 ? 12 : -1);
		Song.Put(6, i * 4 + 3, TUNE[i], i == 0 ? 0 : NO_INSTRUMENT, i == 0 ? 6 : -1);
	}
	Song.Put(0, 0, "C-3", 2);
	Song.Rows[0][0].Effect[0] = EFF_ARPEGGIO;
	Song.Rows[0][0].Param[0] = 0x47;
	Song.Put(0, 16, "F-3");
	Song.Put(1, 0, "E-3", 2, 12);
	Song.Put(1, 8, "G-3");
	Song.Put(1, 16, "A-3");
	Song.Put(1, 24, "C-4");
	Song.Put(2, 0, "C-3", 1);
	Song.Put(3, 0, "kick", 2, 15);
	Song.Put(3, 4, "snare", 2, 12);
	ToolResult Result = Run(Song, "mix", "{\"melody_channel\": \"VRC6 Pulse 1\"}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "a soft mix, bars 1-8: 2A03 Pulse 1 (chords) loudest 15 to 6; 2A03 Pulse 2 (a second line) loudest 12 to 7; "
		"2A03 Noise (drums) loudest 15 to 10; VRC6 Pulse 1 (melody) loudest 15 to 9; VRC6 Pulse 2 (echo) loudest 6 to 4; the DPCM "
		"level Z30 at bar 1 beat 1, so the triangle and the noise play at about 70%");
	CHECK(Song.Rows[5][0].Volume == 9 && Song.Rows[5][16].Volume == 7 && Song.Rows[5][4].Volume == NO_VOLUME);
	CHECK(Song.Rows[6][3].Volume == 4 && Song.Rows[0][0].Volume == 6 && Song.Rows[1][0].Volume == 7);
	CHECK(Song.Rows[3][0].Volume == 10 && Song.Rows[3][4].Volume == 8);
	CHECK(Song.Rows[4][0].Effect[0] == EFF_DAC && Song.Rows[4][0].Param[0] == 0x30);
	CHECK(Song.Rows[2][0].Volume == NO_VOLUME);	// the triangle has none
	CHECK_HAS(Run(Song, "mix", "{\"melody_channel\": \"VRC6 Pulse 1\"}").Text, "mixed that way already");
	// Near full volume again, the level back to 0
	Result = Run(Song, "mix", "{\"style\": \"full\", \"melody_channel\": \"VRC6 Pulse 1\"}");
	CHECK_HAS(Result.Text, "a full mix, bars 1-8:");
	CHECK_HAS(Result.Text, "VRC6 Pulse 1 (melody) loudest 9 to 15");
	CHECK_HAS(Result.Text, "Z00 at bar 1 beat 1, so the triangle and the noise play at about 100%");
	CHECK(Song.Rows[5][0].Volume == 15 && Song.Rows[4][0].Param[0] == 0 && Song.Rows[4][0].Effect[0] == EFF_DAC);
	// One channel by hand: the rest and the level stay
	Result = Run(Song, "mix", "{\"levels\": {\"Noise\": 8}}");
	CHECK_HAS(Result.Text, "the mix, bars 1-8: 2A03 Noise (drums) loudest 15 to 8.");
	CHECK(Song.Rows[3][0].Volume == 8 && Song.Rows[5][0].Volume == 15);
	// The level alone, as a number
	Result = Run(Song, "mix", "{\"dac\": 127}");
	CHECK_HAS(Result.Text, "Z7F at bar 1 beat 1, so the triangle and the noise play at about 43%");
	CHECK_HAS(Run(Song, "mix", "{\"levels\": {\"Triangle\": 8}}").Text, "has no volume of its own");
	CHECK_HAS(Run(Song, "mix", "{\"style\": \"funky\"}").Text, "style: soft");
	CHECK_HAS(Run(Song, "mix", "{\"dac\": \"loudest\"}").Text, "dac: full (Z00)");
	// The level as text too: a number, or the effect as FamiTracker shows it
	CHECK_HAS(Run(Song, "mix", "{\"dac\": \"64\"}").Text, "the DPCM level Z40");
	CHECK_HAS(Run(Song, "mix", "{\"dac\": \"Z30\"}").Text, "the DPCM level Z30");
	CHECK_HAS(Run(Song, "mix", "{\"dac\": \"200\"}").Text, "dac: a level 0-127");
	FakeSong Ask;
	CHECK_HAS(Run(Ask, "mix", "{}").Text, "needs direct changes");
	{
		// A sawtooth line over a triangle bass is a second line, a sawtooth doubling it the bass
		FakeSong Saw;
		Saw.Info.DirectChanges = true;
		for (int i = 0; i < 8; ++i)
			Saw.Put(5, i * 4, TUNE[i], i == 0 ? 0 : NO_INSTRUMENT);
		Saw.Put(2, 0, "C-2", 1);
		Saw.Put(2, 16, "F-2");
		Saw.Put(7, 0, "E-3", 0, 15);	// a counter-line on the chords' thirds
		Saw.Put(7, 8, "G-3");
		Saw.Put(7, 16, "A-3");
		Saw.Put(7, 24, "C-4");
		Result = Run(Saw, "mix", "{\"melody_channel\": \"VRC6 Pulse 1\"}");
		CHECK_HAS(Result.Text, "VRC6 Sawtooth (a second line) loudest 15 to 7");
		const char *const ROOTS[] = { "C-2", "C-2", "F-2", "F-2" };
		for (int i = 0; i < 4; ++i)
			Saw.Put(7, i * 8, ROOTS[i]);
		Saw.Rows[7][0].Volume = 15;
		Result = Run(Saw, "mix", "{\"melody_channel\": \"VRC6 Pulse 1\"}");
		CHECK_HAS(Result.Text, "VRC6 Sawtooth (bass) loudest 15 to 10");
		// With no triangle, the register decides
		Saw.Rows[2] = std::vector<Cell>(Saw.Rows[2].size());
		Saw.Rows[7][0].Volume = 15;
		CHECK_HAS(Run(Saw, "mix", "{\"melody_channel\": \"VRC6 Pulse 1\"}").Text, "VRC6 Sawtooth (bass) loudest 15 to 10");
		for (int i = 0; i < 4; ++i)
			Saw.Put(7, i * 8, "E-3");
		Saw.Rows[7][0].Volume = 15;
		CHECK_HAS(Run(Saw, "mix", "{\"melody_channel\": \"VRC6 Pulse 1\"}").Text, "VRC6 Sawtooth (a second line)");
	}
	// Guessing the melody works too
	CHECK(!Run(Song, "mix", "{}").IsError);

	// The review points out a mix where everything plays near full volume
	FakeSong Loud;
	Loud.Info.DirectChanges = true;
	for (int i = 0; i < 16; ++i)
		Loud.Put(5, i * 4, TUNE[i % 8], i == 0 ? 0 : NO_INSTRUMENT);
	for (int b = 0; b < 4; ++b)
		Loud.Put(0, b * 16, "C-3", b == 0 ? 2 : NO_INSTRUMENT);
	CHECK_HAS(Run(Loud, "review_arrangement", "{}").Text, "Mix: both parts with a volume play near full volume.");
	CHECK(!Run(Loud, "mix", "{}").IsError);
	CHECK(Run(Loud, "review_arrangement", "{}").Text.find("Mix:") == std::string::npos);
}

void TestVrc7Custom()
{
	// The VRC7's custom patch, from sounds of this connection's design
	FakeSong Song;
	Song.Info.DirectChanges = true;
	CHECK(!Run(Song, "set_expansion", "{\"chip\": \"vrc7\", \"confirm\": true}").IsError);
	const size_t Before = Song.Info.Instruments.size();
	ToolResult Result = Run(Song, "write_notes", "{\"channel\": \"FM Channel 1\", \"instrument\": \"rhodes\", \"notes\": ["
		"{\"bar\": 1, \"pitch\": \"C-4\", \"beats\": 2}]}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "(a VRC7 custom patch: a bright, bell-like attack that mellows");
	CHECK(Song.Info.Instruments.size() == Before + 1);
	const int Ep = Song.Info.Instruments.back().Index;
	CHECK(Song.Info.Instruments.back().Name == "Electric piano" && Song.Vrc7Patches[Ep] == 0);
	CHECK(Song.Vrc7Regs[Ep] == std::vector<int>({ 0x01, 0x01, 0x48, 0x01, 0xF7, 0xF2, 0x95, 0x23 }));
	CHECK(Song.Rows[5][0].Instrument == Ep);
	// Again by its name: the same instrument
	Result = Run(Song, "write_notes", "{\"channel\": \"FM Channel 1\", \"instrument\": \"electric piano\", \"notes\": ["
		"{\"bar\": 2, \"pitch\": \"D-4\", \"beats\": 2}]}");
	CHECK(Song.Info.Instruments.size() == Before + 1 && Song.Rows[5][16].Instrument == Ep);
	// A second custom patch: the chip holds one at a time, which the reply says
	Result = Run(Song, "write_notes", "{\"channel\": \"FM Channel 2\", \"instrument\": \"fm pad\", \"notes\": ["
		"{\"bar\": 1, \"pitch\": \"E-3\", \"beats\": 4}]}");
	CHECK_HAS(Result.Text, "The VRC7 holds one custom patch at a time, and \"Electric piano\"");
	CHECK(Song.Vrc7Regs[Song.Info.Instruments.back().Index] == std::vector<int>({ 0x21, 0x61, 0x64, 0x01, 0x52, 0x42, 0x12, 0x13 }));
	// The built-in sounds still come by name
	Result = Run(Song, "write_notes", "{\"channel\": \"FM Channel 3\", \"instrument\": \"piano\", \"notes\": ["
		"{\"bar\": 1, \"pitch\": \"G-3\", \"beats\": 4}]}");
	CHECK(!Result.IsError && Song.Vrc7Patches[Song.Info.Instruments.back().Index] == 3);
	CHECK(Result.Text.find("custom patch") == std::string::npos);
	CHECK_HAS(Run(Song, "write_notes", "{\"channel\": \"FM Channel 1\", \"instrument\": \"kazoo\", \"notes\": ["
		"{\"bar\": 3, \"pitch\": \"C-4\", \"beats\": 1}]}").Text,
		"custom ones: electric piano, soft electric piano, glass bell, fm pad, fm bass, wooden mallet");
}

void TestVoicedChords()
{
	const Key C(0, MODE_MAJOR);
	// The notes a voicing uses, the color first
	ftapi::Chord Q;
	CHECK(ParseChord("Cmaj7", C, Q) && VoiceTones(Q, 2, true) == std::vector<int>({ 4, 11 }));
	CHECK(VoiceTones(Q, 3, true) == std::vector<int>({ 4, 11, 7 }) && VoiceTones(Q, 4, true) == std::vector<int>({ 4, 11, 7, 0 }));
	CHECK(ParseChord("C", C, Q) && VoiceTones(Q, 3, false) == std::vector<int>({ 0, 4, 7 }) && VoiceTones(Q, 2, true) == std::vector<int>({ 4, 7 }));
	CHECK(ParseChord("Am9", C, Q) && VoiceTones(Q, 3, true) == std::vector<int>({ 3, 10, 2 }));

	// ii V I: three voices that move by the smallest steps and never cross
	const std::vector<ChordAt> Chords = Progression("| Dm7 | G7 | Cmaj7 | Cmaj7 |");
	ChordPartOptions O;
	O.Style = CHORDS_VOICES;
	O.Low = 40;
	O.High = 54;
	O.Volume = 10;
	const std::vector<std::vector<PartNote> > Parts = VoicedChordParts(Chords, FourFour(4), O, 3);
	CHECK(Parts.size() == 3 && Parts[0].size() == 4 && Parts[1].size() == 4 && Parts[2].size() == 4);	// struck each bar
	for (size_t h = 0; h < 3; ++h) {
		CHECK(Parts[0][h].Pitch < Parts[1][h].Pitch && Parts[1][h].Pitch < Parts[2][h].Pitch);
		CHECK(Parts[0][h].Pitch >= 38 && Parts[2][h].Pitch <= 56);
		if (h > 0)
			for (size_t v = 0; v < 3; ++v)
				CHECK(std::abs(Parts[v][h].Pitch - Parts[v][h - 1].Pitch) <= 3);
	}
	CHECK(Parts[0][2].Rows == 16 && Parts[0][0].Volume == 10 && Parts[0][3].Pitch == Parts[0][2].Pitch);	// the same chord again, the same voicing
	// Each voice a note of its chord
	for (size_t v = 0; v < 3; ++v) {
		CHECK(Chords[0].C.HasPitchClass(Parts[v][0].Pitch % 12) && Chords[1].C.HasPitchClass(Parts[v][1].Pitch % 12));
		CHECK(Chords[2].C.HasPitchClass(Parts[v][2].Pitch % 12));
	}

	// The tool on the VRC7: an electric piano comping on free FM channels
	FakeSong Song;
	Song.Info.DirectChanges = true;
	CHECK(!Run(Song, "set_expansion", "{\"chip\": \"vrc7\", \"confirm\": true}").IsError);
	ToolResult Result = Run(Song, "add_chords_and_bass", "{\"chords\": \"| Dm7 | G7 | Cmaj7 | Cmaj7 |\", \"chord_style\": \"voices\", "
		"\"key\": \"C major\", \"rhythm\": \"charleston\"}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "voices (charleston) chords on VRC7 FM Channel 1, VRC7 FM Channel 2 and VRC7 FM Channel 3, strummed upward 1 "
		"frame apart (Gxx) and a root_fifth bass line on 2A03 Triangle");
	// Strummed: the middle voice a frame late, the top one two
	CHECK(!HasEffectAt(Song, 5, 0, EFF_DELAY, 1) && HasEffectAt(Song, 6, 0, EFF_DELAY, 1) && HasEffectAt(Song, 7, 0, EFF_DELAY, 2));
	CHECK_HAS(Result.Text, "(a VRC7 custom patch: a bright, bell-like attack");
	const int Ep = Song.Rows[5][0].Instrument;
	CHECK(Song.Vrc7Patches[Ep] == 0 && Song.Rows[6][0].Instrument == Ep && Song.Rows[7][0].Instrument == Ep);
	CHECK(Song.Rows[5][0].Pitch() < Song.Rows[6][0].Pitch() && Song.Rows[6][0].Pitch() < Song.Rows[7][0].Pitch());
	CHECK(Song.Rows[5][6].HasNote());	// charleston: beat 1 and the "and" of 2
	// Channels named; too few; not the triangle
	Result = Run(Song, "add_chords_and_bass", "{\"chords\": \"C\", \"chord_style\": \"voices\", \"chord_channels\": [\"FM Channel 4\", "
		"\"FM Channel 5\"], \"bass_channel\": \"none\", \"key\": \"C major\", \"chord_instrument\": \"vibraphone\", \"strum\": 0}");
	CHECK_HAS(Result.Text, "voices chords on VRC7 FM Channel 4 and VRC7 FM Channel 5, bar 1");
	CHECK(!HasEffectAt(Song, 9, 0, EFF_DELAY, 1));	// struck together
	CHECK(Song.Vrc7Patches[Song.Rows[8][0].Instrument] == 11);
	CHECK_HAS(Run(Song, "add_chords_and_bass", "{\"chords\": \"C\", \"chord_style\": \"voices\", \"chord_channels\": \"FM Channel 4\", "
		"\"key\": \"C major\"}").Text, "voiced chords take 2 to 4 channels");
	CHECK_HAS(Run(Song, "add_chords_and_bass", "{\"chords\": \"C\", \"chord_style\": \"voices\", \"chord_channels\": [\"Triangle\", "
		"\"FM Channel 4\"], \"key\": \"C major\"}").Text, "can't take a voice");
	// On a 2A03 song: the pulses
	FakeSong Plain;
	Plain.Info.DirectChanges = true;
	Result = Run(Plain, "add_chords_and_bass", "{\"chords\": \"| Am7 | D7 |\", \"chord_style\": \"voices\", \"key\": \"G major\", "
		"\"bass_channel\": \"none\"}");
	CHECK_HAS(Result.Text, "voices chords on 2A03 Pulse 1, 2A03 Pulse 2 and VRC6 Pulse 1");
}

void TestFadesAndScoops()
{
	// Fades: a held note slides down from halfway through, the next one gets its volume back
	FakeSong Song;
	Song.Info.DirectChanges = true;
	Song.Put(5, 0, "C-4", 0, 12);
	Song.Put(5, 16, "E-4");
	Song.Put(5, 20, "G-4");
	Song.End(5, 24, NOTE_RELEASE);
	ToolResult Result = Run(Song, "polish", "{\"touches\": [\"fades\"], \"channels\": \"VRC6 Pulse 1\"}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "fades on 1 held note (a volume slide from halfway through, A0y, the next note at its own volume) on VRC6 Pulse 1");
	CHECK(Song.Rows[5][8].Effect[0] == EFF_VOLUME_SLIDE && Song.Rows[5][8].Param[0] == 0x02);
	CHECK(Song.Rows[5][16].Effect[0] == EFF_VOLUME_SLIDE && Song.Rows[5][16].Param[0] == 0 && Song.Rows[5][16].Volume == 12);
	CHECK(Song.Rows[5][20].Effect[0] == EFF_NONE);	// a short note: no fade
	{
		// A held note that ends in a release and a rest: the slide stops at the release, and the note after the
		// rest plays at its own volume again, not where the fade left it
		FakeSong Rest;
		Rest.Info.DirectChanges = true;
		Rest.Put(5, 0, "C-4", 0, 12);
		Rest.End(5, 12, NOTE_RELEASE);
		Rest.Put(5, 20, "E-4");
		Rest.End(5, 24, NOTE_RELEASE);
		CHECK(!Run(Rest, "polish", "{\"touches\": [\"fades\"], \"channels\": \"VRC6 Pulse 1\"}").IsError);
		CHECK(Rest.Rows[5][6].Effect[0] == EFF_VOLUME_SLIDE && Rest.Rows[5][6].Param[0] != 0);
		CHECK(Rest.Rows[5][12].Effect[0] == EFF_VOLUME_SLIDE && Rest.Rows[5][12].Param[0] == 0);
		CHECK(Rest.Rows[5][20].Volume == 12);
	}
	// Scoops: a held note after a leap up slides into place from below
	FakeSong Leap;
	Leap.Info.DirectChanges = true;
	Leap.Put(5, 0, "C-4", 0);
	Leap.Put(5, 4, "G-4");
	Leap.Put(5, 12, "A-4");
	Leap.End(5, 16, NOTE_RELEASE);
	Result = Run(Leap, "polish", "{\"touches\": [\"scoops\"], \"melody_channel\": \"VRC6 Pulse 1\"}");
	CHECK_HAS(Result.Text, "1 scoop after leaps up on VRC6 Pulse 1");
	CHECK(Leap.Rows[5][4].Pitch() == 53 && Leap.Rows[5][4].Effect[0] == EFF_SLIDE_UP && Leap.Rows[5][4].Param[0] == 0x62);
	CHECK(Leap.Rows[5][12].Pitch() == 57 && Leap.Rows[5][12].Effect[0] == EFF_NONE);	// a step, not a leap
	// Staccato: the bass bounces, its notes cut after half a beat
	FakeSong Bass;
	Bass.Info.DirectChanges = true;
	Bass.Put(2, 0, "C-3", 1);
	Bass.Put(2, 8, "G-2");
	Bass.Put(2, 16, "A-2");
	Bass.Put(2, 18, "A-2");
	Bass.End(2, 32, NOTE_HALT);
	Result = Run(Bass, "polish", "{\"touches\": [\"staccato\"]}");
	CHECK_HAS(Result.Text, "3 bass notes on 2A03 Triangle cut short after half a beat (staccato)");
	CHECK(Bass.Rows[2][2].Note == NOTE_HALT && Bass.Rows[2][10].Note == NOTE_HALT && Bass.Rows[2][20].Note == NOTE_HALT);
	CHECK(Bass.Rows[2][17].Note == NOTE_NONE);	// a note shorter than a beat stays
	// Read back as written: the note and its slide
	CHECK_HAS(Run(Leap, "read_notes", "{\"channel\": \"VRC6 Pulse 1\"}").Text, "bar 1 beat 2: F-4, 2 beats, effects Q62");

	// Falls: a phrase's last note before a rest of a beat drops as it ends; a breath or a note into the next doesn't
	FakeSong Sigh;
	Sigh.Info.DirectChanges = true;
	Sigh.Put(5, 0, "E-4", 0, 12);
	Sigh.End(5, 8, NOTE_RELEASE);		// two beats, then a rest of two
	Sigh.Put(5, 16, "D-4");
	Sigh.End(5, 22, NOTE_RELEASE);		// a rest of two rows: a breath
	Sigh.Put(5, 24, "C-4");
	Sigh.Put(5, 32, "G-4");			// rings into the next
	Sigh.Put(5, 40, "C-5");
	Sigh.End(5, 48, NOTE_HALT);
	Result = Run(Sigh, "polish", "{\"touches\": [\"falls\"], \"channels\": \"VRC6 Pulse 1\", \"melody_channel\": \"VRC6 Pulse 1\"}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "2 falls where phrases of VRC6 Pulse 1 end (a drop of two semitones as the last note ends, R42)");
	CHECK(HasEffectAt(Sigh, 5, 6, EFF_SLIDE_DOWN, 0x42) && HasEffectAt(Sigh, 5, 46, EFF_SLIDE_DOWN, 0x42));
	for (int r = 8; r < 46; ++r)
		CHECK(!HasEffectAt(Sigh, 5, r, EFF_SLIDE_DOWN, 0x42));	// the breath, and the notes into the next
	CHECK_HAS(Run(Sigh, "polish", "{\"touches\": [\"sighs\"]}").Text, "falls");
}

void TestReharmonize()
{
	// New chords over a groove's take the place of its chords and bass rather than stacking a second part on them
	FakeSong Song;
	Song.Info.DirectChanges = true;
	CHECK(!Run(Song, "make_groove", "{\"mood\": \"nostalgic\", \"key\": \"D major\", \"bars\": 8}").IsError);
	CHECK(Song.Rows[0][64].HasNote() && !Song.Rows[1][64].HasNote());	// its chords on 2A03 Pulse 1
	ToolResult Result = Run(Song, "add_chords_and_bass", "{\"chords\": \"| Em9 | A7 |\", \"chord_style\": \"lush\", \"from_bar\": 5, "
		"\"to_bar\": 6, \"key\": \"D major\"}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "chords on 2A03 Pulse 1 and a root_fifth bass line on 2A03 Triangle, bars 5-6, in D major (in place of the "
		"chords and bass line that played there)");
	CHECK(Song.Rows[0][64].Pitch() % 12 == 4 && Song.Rows[0][80].Pitch() % 12 == 9);	// Em9, A7
	for (int r = 0; r < 128; ++r)
		CHECK(!Song.Rows[1][r].HasNote() && !Song.Rows[7][r].HasNote());	// no second chord part, no second bass
	// With a melody over them, named or not
	for (int b = 0; b < 8; ++b)
		Song.Put(5, b * 16, b % 2 == 0 ? "F#4" : "A-4", 0, 12);
	Result = Run(Song, "add_chords_and_bass", "{\"chords\": \"| Gmaj7 | F#m7 |\", \"chord_style\": \"lush\", \"from_bar\": 7, "
		"\"to_bar\": 8, \"key\": \"D major\", \"bass_channel\": \"none\"}");
	CHECK_HAS(Result.Text, "chords on 2A03 Pulse 1, bars 7-8, in D major (in place of the chords that played there)");
	CHECK(Song.Rows[5][96].HasNote() && !Song.Rows[1][96].HasNote());

	// Voiced chords give way to voiced chords on the same channels
	FakeSong Fm;
	Fm.Info.DirectChanges = true;
	CHECK(!Run(Fm, "set_expansion", "{\"chip\": \"vrc7\", \"confirm\": true}").IsError);
	CHECK(!Run(Fm, "add_chords_and_bass", "{\"chords\": \"| Dm7 | G7 | Cmaj7 | Cmaj7 |\", \"chord_style\": \"voices\", "
		"\"key\": \"C major\"}").IsError);
	Result = Run(Fm, "add_chords_and_bass", "{\"chords\": \"| Am7 | D7 |\", \"chord_style\": \"voices\", \"key\": \"C major\", "
		"\"from_bar\": 3, \"to_bar\": 4}");
	CHECK_HAS(Result.Text, "voices chords on VRC7 FM Channel 1, VRC7 FM Channel 2 and VRC7 FM Channel 3, strummed upward 1 frame apart "
		"(Gxx) and a root_fifth bass line on 2A03 Triangle, bars 3-4, in C major (in place of the chords and bass line that played there)");
	for (int r = 0; r < 64; ++r)
		CHECK(!Fm.Rows[8][r].HasNote());	// FM Channel 4 stays free
	CHECK(Fm.Rows[5][32].HasNote() && Fm.Rows[6][32].HasNote() && Fm.Rows[7][32].HasNote());

	// Lush chords can't go where voices played on three channels: the old ones are named, kept
	FakeSong Pulses;
	Pulses.Info.DirectChanges = true;
	CHECK(!Run(Pulses, "add_chords_and_bass", "{\"chords\": \"| Am7 | D7 |\", \"chord_style\": \"voices\", \"key\": \"G major\", "
		"\"bass_channel\": \"none\"}").IsError);
	Result = Run(Pulses, "add_chords_and_bass", "{\"chords\": \"| Cmaj7 | D7 |\", \"chord_style\": \"lush\", \"key\": \"G major\", "
		"\"bass_channel\": \"none\"}");
	CHECK_HAS(Result.Text, "lush chords on VRC6 Pulse 2");
	CHECK_HAS(Result.Text, "The chords on 2A03 Pulse 1, 2A03 Pulse 2, VRC6 Pulse 1 there still play under the new ones");
}

void TestBreathing()
{
	// Every part all the way through: the review asks for a build and a break; an intro on the chords alone answers it
	FakeSong Song;
	Song.Info.DirectChanges = true;
	CHECK(!Run(Song, "make_groove", "{\"mood\": \"town\", \"key\": \"C major\", \"bars\": 16}").IsError);
	for (int b = 0; b < 16; ++b)
		Song.Put(5, b * 16, b % 2 == 0 ? "E-4" : "G-4", 0, 12);
	CHECK(!Run(Song, "add_echo", "{\"channel\": \"VRC6 Pulse 1\"}").IsError);
	ToolResult Result = Run(Song, "review_arrangement", "{}");
	CHECK_HAS(Result.Text, "The song keeps about one loudness all the way through (0 dB between its quietest and loudest bars)");
	CHECK_HAS(Result.Text, "Loudness: about 0 dB between the quietest bars");
	// Four bars without the drums still sound about as loud: the triangle and the melody carry them
	CHECK(!Run(Song, "write_notes", "{\"channel\": \"Noise\", \"from_bar\": 5, \"to_bar\": 8, \"notes\": []}").IsError);
	CHECK_HAS(Run(Song, "review_arrangement", "{}").Text, "never breathes");
	// An intro on the chords alone plays several dB under the rest
	const char *const INTRO[] = { "Triangle", "VRC6 Pulse 1", "VRC6 Pulse 2", "2A03 Pulse 2", "Noise" };
	for (size_t i = 0; i < sizeof(INTRO) / sizeof(INTRO[0]); ++i)
		Run(Song, "write_notes", std::string("{\"channel\": \"") + INTRO[i] + "\", \"from_bar\": 1, \"to_bar\": 4, \"notes\": []}");
	Result = Run(Song, "review_arrangement", "{}");
	CHECK_LACKS(Result.Text, "never breathes");
	CHECK_HAS(Result.Text, "dB between the quietest bars (bars 1-2) and the loudest");
}

void TestChipLevels()
{
	// The VRC7 plays louder than the pulses and counts its volume in steps of 3 dB
	FakeSong Song;
	Song.Info.DirectChanges = true;
	CHECK(!Run(Song, "set_expansion", "{\"chip\": \"VRC7\", \"confirm\": true}").IsError);
	CHECK(Song.Info.Channels[5].Kind == KIND_FM);
	ToolResult Result = Run(Song, "write_notes", "{\"channel\": \"FM Channel 1\", \"instrument\": \"clarinet\", \"notes\": ["
		"{\"bar\": 1, \"beat\": 1, \"pitch\": \"C-4\", \"beats\": 1}, {\"bar\": 1, \"beat\": 2, \"pitch\": \"E-4\", \"beats\": 0.5}, "
		"{\"bar\": 1, \"beat\": 2.5, \"pitch\": \"F-4\", \"beats\": 0.5}, {\"bar\": 1, \"beat\": 3, \"pitch\": \"G-4\", \"beats\": 1}, "
		"{\"bar\": 1, \"beat\": 4, \"pitch\": \"A-4\", \"beats\": 1}, {\"bar\": 2, \"beat\": 1, \"pitch\": \"G-4\", \"beats\": 4}]}");
	CHECK(!Result.IsError);
	CHECK(!Run(Song, "write_notes", "{\"channel\": \"Triangle\", \"instrument\": \"bass\", \"notes\": [{\"bar\": 1, \"pitch\": \"C-3\", "
		"\"beats\": 4}, {\"bar\": 2, \"pitch\": \"G-2\", \"beats\": 4}]}").IsError);
	// An echo on another FM channel: three steps (9 dB) under the melody, as volume 6 is on a pulse, not 27 dB
	Result = Run(Song, "add_echo", "{\"channel\": \"FM Channel 1\", \"to_channel\": \"FM Channel 5\"}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "at volume 6 (as much softer as 6 is than 15 on a pulse: 12 where the melody plays 15)");
	CHECK(Song.Rows[9][3].HasNote() && Song.Rows[9][3].Volume == 12);
	// Accents: the notes between beats a step softer, the beats as loud; a fade three steps over the end
	Result = Run(Song, "polish", "{\"touches\": [\"accents\", \"fades\"], \"melody_channel\": \"FM Channel 1\"}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "accents (the notes between beats a step, 3 dB, softer than the beats) on VRC7 FM Channel 1");
	CHECK_HAS(Result.Text, "; on the VRC7 three steps, 9 dB, over the note's end)");
	CHECK(Song.Rows[5][0].Volume == 15 && Song.Rows[5][4].Volume == 15 && Song.Rows[5][6].Volume == 14 && Song.Rows[5][8].Volume == 15);
	CHECK(HasEffectAt(Song, 5, 28, EFF_VOLUME_SLIDE, 1) && !HasEffectAt(Song, 5, 24, EFF_VOLUME_SLIDE, 1));
	// A soft mix: the melody at 12 (as loud as 9 on a pulse), the echo at 10, each volume as far under the loudest
	Result = Run(Song, "mix", "{\"melody_channel\": \"FM Channel 1\"}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "VRC7 FM Channel 1 (melody) loudest 15 to 12");
	CHECK_HAS(Result.Text, "VRC7 FM Channel 5 (echo) loudest 11 to 10");	// its notes all between beats
	CHECK_HAS(Result.Text, "The VRC7 plays louder than the pulses and counts its volume in steps of 3 dB");
	CHECK(Song.Rows[5][0].Volume == 12 && Song.Rows[5][6].Volume == 11);
	CHECK_LACKS(Run(Song, "review_arrangement", "{}").Text, "Levels:");
	// A melody at 9 there plays far under the triangle: the review says so
	CHECK(!Run(Song, "mix", "{\"levels\": {\"FM Channel 1\": 9}}").IsError);
	Result = Run(Song, "review_arrangement", "{}");
	CHECK_HAS(Result.Text, "Levels: the melody plays about 12 dB under 2A03 Triangle.");
	CHECK_HAS(Result.Text, "mix (style soft) sets each part's level for its chip (the VRC7 counts its volume in steps of 3 dB");
	// A counter-melody and a harmony on FM channels: a step under the melody, not four or three
	FakeSong Lines;
	Lines.Info.DirectChanges = true;
	CHECK(!Run(Lines, "set_expansion", "{\"chip\": \"VRC7\", \"confirm\": true}").IsError);
	CHECK(!Run(Lines, "write_notes", "{\"channel\": \"FM Channel 1\", \"instrument\": \"flute\", \"notes\": ["
		"{\"bar\": 1, \"pitch\": \"E-4\", \"beats\": 2}, {\"bar\": 1, \"beat\": 3, \"pitch\": \"G-4\", \"beats\": 2}, "
		"{\"bar\": 2, \"pitch\": \"A-4\", \"beats\": 2}, {\"bar\": 2, \"beat\": 3, \"pitch\": \"G-4\", \"beats\": 2}]}").IsError);
	CHECK(!Run(Lines, "add_harmony", "{\"melody_channel\": \"FM Channel 1\", \"channel\": \"FM Channel 2\", \"key\": \"C major\"}").IsError);
	int Harmony = -1, Counter = -1;
	for (int r = 0; r < 32 && Harmony < 0; ++r)
		if (Lines.Rows[6][r].HasNote())
			Harmony = Lines.Rows[6][r].Volume;
	CHECK_EQ(Harmony, 14);
	CHECK(!Run(Lines, "add_counter_melody", "{\"melody_channel\": \"FM Channel 1\", \"channel\": \"FM Channel 3\", "
		"\"key\": \"C major\"}").IsError);
	for (int r = 0; r < 32 && Counter < 0; ++r)
		if (Lines.Rows[7][r].HasNote())
			Counter = Lines.Rows[7][r].Volume;
	CHECK_EQ(Counter, 14);

	// The N163's wave sounds play quieter: its melody stays at 15, its other parts as far under it
	FakeSong Waves;
	Waves.Info.DirectChanges = true;
	CHECK(!Run(Waves, "set_expansion", "{\"chip\": \"N163\", \"confirm\": true}").IsError);
	CHECK(!Run(Waves, "write_notes", "{\"channel\": \"Namco 1\", \"instrument\": \"soft flute\", \"notes\": ["
		"{\"bar\": 1, \"pitch\": \"E-4\", \"beats\": 1}, {\"bar\": 1, \"beat\": 2, \"pitch\": \"G-4\", \"beats\": 1}, "
		"{\"bar\": 1, \"beat\": 3, \"pitch\": \"A-4\", \"beats\": 1}, {\"bar\": 1, \"beat\": 4, \"pitch\": \"C-5\", \"beats\": 1}, "
		"{\"bar\": 2, \"pitch\": \"B-4\", \"beats\": 4}]}").IsError);
	CHECK(!Run(Waves, "write_notes", "{\"channel\": \"Namco 2\", \"instrument\": \"soft flute\", \"notes\": ["
		"{\"bar\": 1, \"pitch\": \"C-3\", \"beats\": 4}, {\"bar\": 2, \"pitch\": \"D-3\", \"beats\": 4}]}").IsError);
	Result = Run(Waves, "mix", "{\"melody_channel\": \"Namco 1\"}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "N163 Namco 1 (melody) stays at 15");
	CHECK_HAS(Result.Text, "N163 Namco 2 (a second line) loudest 15 to 12");
	CHECK_HAS(Result.Text, "The N163's wave sounds play quieter than the pulses");

	// A sawtooth sound designed by name plays by that name
	FakeSong Saw;
	CHECK(!Run(Saw, "design_instrument", "{\"name\": \"Grit\", \"kind\": \"bass\", \"channel\": \"Sawtooth\"}").IsError);
	Result = Run(Saw, "write_notes", "{\"channel\": \"Sawtooth\", \"instrument\": \"Grit\", \"notes\": [{\"bar\": 1, \"pitch\": \"C-2\", "
		"\"beats\": 2}]}");
	CHECK(!Result.IsError);
	CHECK_LACKS(Result.Text, "Added the instrument");
}

void TestGrooveUnderMelody()
{
	// A melody written first: the groove's chords go under it, not around its notes
	auto Middle = [](const FakeSong &Song, int Channel) {
		int Sum = 0, Count = 0;
		for (size_t r = 0; r < Song.Rows[static_cast<size_t>(Channel)].size(); ++r)
			if (Song.Rows[static_cast<size_t>(Channel)][r].HasNote() && Song.Rows[static_cast<size_t>(Channel)][r].Note <= 12) {
				Sum += Song.Rows[static_cast<size_t>(Channel)][r].Pitch();
				++Count;
			}
		return Count == 0 ? 0.0 : Sum / static_cast<double>(Count);
	};
	FakeSong Song;
	Song.Info.DirectChanges = true;
	CHECK(!Run(Song, "write_notes", "{\"channel\": \"VRC6 Pulse 1\", \"instrument\": \"soft lead\", \"notes\": ["
		"{\"bar\": 1, \"pitch\": \"E-3\", \"beats\": 2}, {\"bar\": 1, \"beat\": 3, \"pitch\": \"G-3\", \"beats\": 2}, "
		"{\"bar\": 2, \"pitch\": \"A-3\", \"beats\": 2}, {\"bar\": 2, \"beat\": 3, \"pitch\": \"E-3\", \"beats\": 2}, "
		"{\"bar\": 3, \"pitch\": \"F-3\", \"beats\": 4}, {\"bar\": 4, \"pitch\": \"E-3\", \"beats\": 4}]}").IsError);
	CHECK(!Run(Song, "make_groove", "{\"mood\": \"town\", \"key\": \"C major\", \"bars\": 4}").IsError);
	// Without one they sit higher
	FakeSong Plain;
	Plain.Info.DirectChanges = true;
	CHECK(!Run(Plain, "make_groove", "{\"mood\": \"town\", \"key\": \"C major\", \"bars\": 4}").IsError);
	const double Under = Middle(Song, 0), Alone = Middle(Plain, 0);
	CHECK(Under > 0 && Under <= 36.0 && Alone >= Under + 5);
}

void TestEffectRoom()
{
	// A channel that shows one effect column: the tools use all four, and FamiTracker shows more where they're
	// needed; an effect in a column it hides (which doesn't play) isn't read as playing
	FakeSong Song;
	Song.Info.DirectChanges = true;
	Song.Info.Channels[5].EffectColumns = 1;
	Song.Rows[5][40].Effect[1] = EFF_VOLUME_SLIDE;
	Song.Rows[5][40].Param[1] = 0x0F;
	ToolResult Result = Run(Song, "write_notes", "{\"channel\": \"VRC6 Pulse 1\", \"instrument\": \"soft lead\", \"notes\": ["
		"{\"bar\": 1, \"pitch\": \"C-4\", \"beats\": 2, \"effects\": [\"P81\", \"V01\"]}, {\"bar\": 1, \"beat\": 3, "
		"\"pitch\": \"E-4\", \"beats\": 2}]}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "More effect columns show for the effects on VRC6 Pulse 1 (2).");
	CHECK_EQ(Song.Info.Channels[5].EffectColumns, 2);
	CHECK(HasEffectAt(Song, 5, 0, EFF_PITCH, 0x81) && HasEffectAt(Song, 5, 0, EFF_DUTY_CYCLE, 1));
	CHECK_LACKS(Run(Song, "read_notes", "{\"channel\": \"VRC6 Pulse 1\", \"from_bar\": 3, \"to_bar\": 3}").Text, "A0F");
	// Touches on the notes find room beside those effects
	Result = Run(Song, "polish", "{\"touches\": [\"vibrato\", \"fades\"], \"melody_channel\": \"VRC6 Pulse 1\"}");
	CHECK(!Result.IsError);
	CHECK_LACKS(Result.Text, "no free effect column");
	// Nothing past the columns shown: nothing said
	FakeSong Plain;
	Plain.Info.DirectChanges = true;
	CHECK_LACKS(Run(Plain, "write_notes", "{\"channel\": \"VRC6 Pulse 1\", \"instrument\": \"soft lead\", \"notes\": ["
		"{\"bar\": 1, \"pitch\": \"C-4\", \"beats\": 2}]}").Text, "More effect columns");
}

void TestHarpUnderMelody()
{
	// A melody from bar 3 and chords for six bars, the first two an intro: they start at bar 1 and end with the melody
	FakeSong Song(4);
	Song.Info.DirectChanges = true;
	CHECK(!Run(Song, "write_notes", "{\"channel\": \"VRC6 Pulse 1\", \"instrument\": \"soft lead\", \"notes\": ["
		"{\"bar\": 3, \"pitch\": \"A-3\", \"beats\": 2}, {\"bar\": 3, \"beat\": 3, \"pitch\": \"C-4\", \"beats\": 1.5}, "
		"{\"bar\": 4, \"pitch\": \"D-4\", \"beats\": 3}, {\"bar\": 5, \"pitch\": \"C-4\", \"beats\": 1}, "
		"{\"bar\": 5, \"beat\": 2, \"pitch\": \"A-3\", \"beats\": 0.5}, {\"bar\": 5, \"beat\": 2.5, \"pitch\": \"G-3\", \"beats\": 1.5}, "
		"{\"bar\": 6, \"pitch\": \"F-3\", \"beats\": 4}]}").IsError);
	ToolResult Result = Run(Song, "add_chords_and_bass", "{\"chords\": \"| F | Bb | F | Bb | C | F |\", \"chord_style\": \"broken\", "
		"\"pattern\": \"rolling\", \"notes_per_beat\": 4, \"chord_channel\": \"Pulse 1\", \"bass_channel\": \"none\", "
		"\"key\": \"F major\"}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "bars 1-6");
	CHECK_HAS(Result.Text, "The chords are 6 bars and the melody plays in bars 3-6, so they start at bar 1 and end with it");
	CHECK(Song.Rows[0][0].HasNote() && Song.Rows[0][1].HasNote());
	// The harp's steady sixteenths are accompaniment: the melody is the line over them, and they aren't too busy
	Result = Run(Song, "review_arrangement", "{\"from_bar\": 3}");
	CHECK_HAS(Result.Text, "Parts: melody on VRC6 Pulse 1");
	CHECK_LACKS(Run(Song, "check_song", "{}").Text, "Busy: 2A03 Pulse 1");
	// A harp rolling a Dm7 over a bass that moves to the fifth: still Dm7 (over A), not a chord on A
	FakeSong Fifth;
	const char *const ROLL[] = { "D-2", "A-2", "F-3", "C-4" };
	for (int r = 0; r < 16; ++r)
		Fifth.Put(0, r, ROLL[r % 4 == 0 ? 0 : r % 4], r == 0 ? 0 : NO_INSTRUMENT, r == 0 ? 7 : -1);
	Fifth.Put(2, 0, "D-3", 2);
	Fifth.Put(2, 8, "A-3");
	Fifth.Put(5, 0, "F-3", 0, 12);
	Fifth.Put(5, 8, "A-3");
	Result = Run(Fifth, "get_song", "{}");
	CHECK_HAS(Result.Text, "Chords the parts play: | Dm7");
	CHECK_LACKS(Result.Text, "Dm7 A");
	CHECK_LACKS(Result.Text, "Dm7 Am");
	// Chords too long to fit before the melody: the last are left out, and the reply says so
	FakeSong Late(4);
	Late.Info.DirectChanges = true;
	CHECK(!Run(Late, "write_notes", "{\"channel\": \"VRC6 Pulse 1\", \"instrument\": \"soft lead\", \"notes\": ["
		"{\"bar\": 1, \"pitch\": \"A-3\", \"beats\": 4}, {\"bar\": 2, \"pitch\": \"C-4\", \"beats\": 4}]}").IsError);
	CHECK_HAS(Run(Late, "add_chords_and_bass", "{\"chords\": \"| F | Bb | C | F |\", \"key\": \"F major\"}").Text,
		"The chords are 4 bars, more than the melody's 2 (bars 1-2), so the last 2 were left out");
}

void TestVrc7Octave()
{
	// The VRC7 sounds an octave under what's written (its A-3 is 220 Hz, a pulse's 440), as the triangle does: the parts
	// the tools write there from a pulse's notes go an octave up, and the checks count notes as they sound
	FakeSong Song;
	Song.Info.DirectChanges = true;
	ToolResult Result = Run(Song, "set_expansion", "{\"chip\": \"vrc7\", \"confirm\": true}");
	CHECK_HAS(Result.Text, "Like the triangle, they sound an octave under the note written (A-3 is 220 Hz there, 440 on a pulse)");
	CHECK(!Run(Song, "write_notes", "{\"channel\": \"Pulse 1\", \"instrument\": \"soft lead\", \"notes\": ["
		"{\"bar\": 1, \"pitch\": \"C-4\", \"beats\": 1}, {\"bar\": 1, \"beat\": 2, \"pitch\": \"E-4\", \"beats\": 1}, "
		"{\"bar\": 1, \"beat\": 3, \"pitch\": \"G-4\", \"beats\": 1}, {\"bar\": 1, \"beat\": 4, \"pitch\": \"E-4\", \"beats\": 1}, "
		"{\"bar\": 2, \"pitch\": \"F-4\", \"beats\": 2}, {\"bar\": 2, \"beat\": 3, \"pitch\": \"D-4\", \"beats\": 2}]}").IsError);
	const std::vector<NoteInfo> Melody = ReadNotes(Song.Rows[0], Song.Info.Channels[0].EffectColumns);

	// An echo on an FM channel plays the melody's notes an octave up, where they sound as the pulse's do, and is still
	// taken for the echo
	Result = Run(Song, "add_echo", "{\"channel\": \"Pulse 1\", \"to_channel\": \"FM Channel 1\", \"delay_beats\": 0.5, "
		"\"instrument\": \"flute\"}");
	CHECK(!Result.IsError);
	CHECK(Song.Rows[5][2].HasNote() && Song.Rows[5][2].Pitch() == Song.Rows[0][0].Pitch() + 12);
	CHECK(Song.Rows[5][18].HasNote() && Song.Rows[5][18].Pitch() == Song.Rows[0][16].Pitch() + 12);
	CHECK_HAS(Run(Song, "mix", "{\"melody_channel\": \"Pulse 1\"}").Text, "VRC7 FM Channel 1 (echo)");

	// A harmony on FM Channel 2 (the song has a 2A03 "Soft lead", but the preset of that name is a VRC7 sound too): a third
	// or a sixth under each melody note as they sound
	Result = Run(Song, "add_harmony", "{\"melody_channel\": \"Pulse 1\", \"channel\": \"FM Channel 2\", \"key\": \"C major\"}");
	CHECK(!Result.IsError);
	int Harmonized = 0;
	for (size_t i = 0; i < Melody.size(); ++i) {
		const Cell &H = Song.Rows[6][static_cast<size_t>(Melody[i].Row)];
		if (!H.HasNote())
			continue;
		++Harmonized;
		const int Under = Melody[i].Pitch - (H.Pitch() - 12);
		Check(Under >= 3 && Under <= 9, "an FM harmony note sounds a third to a sixth under the pulse's", __LINE__);
	}
	CHECK(Harmonized >= 4);

	// A counter-melody on FM Channel 3, under the melody as they sound
	CHECK(!Run(Song, "add_counter_melody", "{\"melody_channel\": \"Pulse 1\", \"channel\": \"FM Channel 3\", \"key\": \"C major\"}").IsError);
	const std::vector<NoteInfo> Counter = ReadNotes(Song.Rows[7], Song.Info.Channels[7].EffectColumns);
	CHECK(!Counter.empty());
	for (size_t i = 0; i < Counter.size(); ++i)
		Check(Counter[i].Pitch - 12 < 52 && Counter[i].Pitch - 12 >= 52 - 20, "an FM counter-melody sounds under the melody's middle",
			__LINE__);

	// Voiced chords on FM Channels 4-6 sound under the melody: between A-2 and A#3, written an octave up
	Result = Run(Song, "add_chords_and_bass", "{\"chords\": \"| C | Dm |\", \"chord_style\": \"voices\", \"chord_channels\": "
		"[\"FM Channel 4\", \"FM Channel 5\", \"FM Channel 6\"], \"bass_channel\": \"none\", \"key\": \"C major\", \"strum\": 0}");
	CHECK(!Result.IsError);
	int Voiced = 0;
	for (int c = 8; c <= 10; ++c) {
		const std::vector<NoteInfo> Voice = ReadNotes(Song.Rows[static_cast<size_t>(c)], Song.Info.Channels[static_cast<size_t>(c)].EffectColumns);
		for (size_t i = 0; i < Voice.size(); ++i, ++Voiced)
			Check(Voice[i].Pitch - 12 >= 33 && Voice[i].Pitch - 12 <= 46, "an FM chord voice sounds between A-2 and A#3", __LINE__);
	}
	CHECK(Voiced >= 6);

	// check_song: an FM note written a semitone over a pulse's sounds a major seventh under it; written an octave and
	// a semitone over, it sounds a semitone over: a clash
	FakeSong Clashing;
	Clashing.Info.DirectChanges = true;
	CHECK(!Run(Clashing, "set_expansion", "{\"chip\": \"vrc7\", \"confirm\": true}").IsError);
	Clashing.Put(0, 0, "C-4", 0);
	Clashing.End(0, 8);
	Clashing.Put(5, 0, "C#4");		// sounds as C#3, a major seventh under C-4
	Clashing.Put(5, 4, "C#5");		// sounds as C#4
	Clashing.End(5, 8);
	Result = Run(Clashing, "check_song", "{\"key\": \"C major\", \"to_bar\": 1}");
	CHECK_HAS(Result.Text, "Clash: C-4 on 2A03 Pulse 1 against C#5 on VRC7 FM Channel 1 at bar 1 beat 2 for 1 beat");
	CHECK_HAS(Result.Text, ", 1 clashes,");

	// A jingle's harmony on an FM channel under a pulse lead: an octave up from where a pulse would play it
	FakeSong Tune;
	Tune.Info.DirectChanges = true;
	CHECK(!Run(Tune, "set_expansion", "{\"chip\": \"vrc7\", \"confirm\": true}").IsError);
	const char *const LINE[] = { "G-3", "B-3", "D-4", "G-4", "F#4", "D-4", "G-3" };
	for (int i = 0; i < 7; ++i)
		Tune.Put(0, i * 8, LINE[i], i == 0 ? 0 : NO_INSTRUMENT);
	Tune.End(0, 64);
	Result = Run(Tune, "make_jingle", "{\"kind\": \"level up\", \"lead_channel\": \"Pulse 1\", \"harmony_channel\": \"FM Channel 1\", "
		"\"bass_channel\": \"none\", \"drums\": false}");
	CHECK(!Result.IsError);
	int G3 = 0;
	ParsePitch("G-3", G3);
	CHECK(Tune.Rows[0][0].Pitch() == G3 && Tune.Rows[5][0].Pitch() == G3 - 8 + 12);	// as a pulse's would be G3 - 8
}

int main()
{
	TestJson();
	TestVrc7Octave();
	TestHarpUnderMelody();
	TestEffectRoom();
	TestChipLevels();
	TestGrooveUnderMelody();
	TestBreathing();
	TestHttp();
	TestNames();
	TestTiming();
	TestTempo();
	TestKeys();
	TestReadNotes();
	TestBuildPart();
	TestGetSong();
	TestReadNotesTool();
	TestWriteNotes();
	TestPlaceClip();
	TestInstruments();
	TestDesign();
	TestReferences();
	TestSampleDrums();
	TestTranspose();
	TestTempoTool();
	TestBacking();
	TestChords();
	TestArrange();
	TestArrangingTools();
	TestReview();
	TestStructure();
	TestEchoAndVibrato();
	TestMood();
	TestCheck();
	TestNameSong();
	TestLoudness();
	TestPlayback();
	TestProtocol();
	TestSession();
	TestBridge();
	TestBridgeSockets();
	TestFlow();
	TestPng();
	TestPicture();
	TestVersions();
	TestNightVersion();
	TestTenseVersions();
	TestJingleScores();
	TestJingles();
	TestExpansion();
	TestMmc5();
	TestSoundEffects();
	TestDesignedSounds();
	TestEffects();
	TestTiming2();
	TestLoops();
	TestFixLoop();
	TestBars();
	TestKeptVersions();
	TestWaveSounds();
	TestOgg();
	TestCut();
	TestExport();
	TestLushChords();
	TestSwing();
	TestMix();
	TestVrc7Custom();
	TestVoicedChords();
	TestFadesAndScoops();
	TestReharmonize();
	std::printf("%d checks, %d failed\n", g_Checks, g_Failures);
	return g_Failures == 0 ? 0 : 1;
}

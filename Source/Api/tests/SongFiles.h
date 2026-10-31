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

// A FamiTracker module (.ftm) on disk as the tools see it, read only: what
// McpConnection.cpp's GetSong gives for the open document, from the module as
// nsf::ReadFtmFile reads it (Source/Nsf/FtmFile.h) and this fork's own blocks
// (sections, keys, the mixer, the VRC6 Plus mark). For checking real songs
// against the tools' checks (SongFileChecks.cpp); every change is refused.

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "ChannelMix.h"
#include "FdsSounds.h"
#include "Keys.h"
#include "Music.h"
#include "Sections.h"
#include "Song.h"
#include "Vrc6Plus.h"
#include "../../Nsf/FtmFile.h"

namespace ftapi {

class FileSong : public SongHost {
public:
	SongInfo Info;
	std::vector<std::vector<Cell> > Rows;	// per channel, the selected track's

	// The module at Path, its track Track (from 0); false with the reason
	bool Load(const std::string &Path, int Track, std::string &Error)
	{
		nsf::Module M;
		nsf::FtmInfo FileInfo;
		if (!nsf::ReadFtmFile(Path, M, FileInfo, Error))
			return false;
		if (Track < 0 || Track >= static_cast<int>(M.Tracks.size())) {
			Error = "no such track";
			return false;
		}
		SectionStore Sections;
		KeyStore Keys;
		MixStore Mix;
		bool Plus = false;
		ReadOwnBlocks(Path, Sections, Keys, Mix, Plus);

		const nsf::Track &T = M.Tracks[static_cast<size_t>(Track)];
		Info = SongInfo();
		Info.Title = M.Title;
		Info.Author = M.Author;
		Info.FileName = Path.substr(Path.find_last_of("/\\") == std::string::npos ? 0 : Path.find_last_of("/\\") + 1);
		Info.Track = Track;
		Info.Tracks = static_cast<int>(M.Tracks.size());
		Info.TrackName = T.Name;
		for (size_t t = 0; t < M.Tracks.size(); ++t)
			Info.TrackNames.push_back(M.Tracks[t].Name);
		Info.Pal = M.Machine == 1;
		Info.FrameRate = M.FrameRate != 0 ? M.FrameRate : (Info.Pal ? 50 : 60);
		Info.Speed = T.Speed;
		Info.Tempo = T.Tempo;
		Info.SplitPoint = M.SplitPoint;
		Info.PatternLength = T.PatternLength;
		Info.Frames = static_cast<int>(T.Frames.size());
		Info.FirstHighlight = FileInfo.FirstHighlight;
		Info.SecondHighlight = FileInfo.SecondHighlight;
		Info.DirectChanges = true;	// as the user lets Claude change it, so what_next gives its steps (nothing is written)
		Info.Vrc6Plus = Plus && (M.Expansion & nsf::EXP_VRC6) != 0 && (M.Expansion & nsf::EXP_N163) != 0 && M.N163Channels == 4;
		const int CHIPS[] = { nsf::EXP_VRC6, nsf::EXP_VRC7, nsf::EXP_FDS, nsf::EXP_MMC5, nsf::EXP_N163, nsf::EXP_S5B };
		const char *const CHIP_NAMES[] = { "VRC6", "VRC7", "FDS", "MMC5", "N163", "5B" };
		for (int i = 0; i < 6; ++i)
			if (M.Expansion & CHIPS[i])
				Info.Expansion += (Info.Expansion.empty() ? "" : ", ") + std::string(CHIP_NAMES[i]);
		Sections.FitFrames(Track, Info.Frames);
		Info.Sections = Sections.Get(Track);
		Info.KeyChosen = Keys.Get(Track, Info.ChosenKey);
		Info.SpaceRoom = Mix.Room();
		Info.SpaceEcho = Mix.Echo();
		Info.SpaceRepeats = Mix.Repeats();

		// The channels, in the order FamiTracker registers them
		AddChannels(M, Plus);
		for (size_t c = 0; c < Info.Channels.size(); ++c) {
			ChannelInfo &Channel = Info.Channels[c];
			Channel.EffectColumns = c < T.Columns.size() ? T.Columns[c] : 1;
			Channel.MixLevel = Mix.Level(Channel.ChanId);
			Channel.MixPan = Mix.Pan(Channel.ChanId);
			Channel.MixSoft = Mix.Soft(Channel.ChanId);
			Channel.MixSpace = Mix.Space(Channel.ChanId);
		}

		// The instruments
		for (size_t i = 0; i < M.Instruments.size(); ++i)
			if (!M.Instruments[i].Unused)
				Info.Instruments.push_back(InstrumentOf(M.Instruments[i], static_cast<int>(i)));

		// The cells, frame after frame
		Rows.assign(Info.Channels.size(), std::vector<Cell>());
		for (size_t c = 0; c < Info.Channels.size(); ++c)
			for (size_t f = 0; f < T.Frames.size(); ++f) {
				const int Pattern = c < T.Frames[f].size() ? T.Frames[f][c] : -1;
				for (int r = 0; r < T.PatternLength; ++r) {
					Cell Out;
					if (Pattern >= 0 && c < T.Patterns.size() && static_cast<size_t>(Pattern) < T.Patterns[c].size() &&
						static_cast<size_t>(r) < T.Patterns[c][static_cast<size_t>(Pattern)].size())
						Out = CellOf(T.Patterns[c][static_cast<size_t>(Pattern)][static_cast<size_t>(r)]);
					Rows[c].push_back(Out);
				}
			}
		return true;
	}

	bool GetSong(SongInfo &Out, std::string &) override
	{
		Out = Info;
		return true;
	}

	void ReadCells(int Channel, int First, int Count, std::vector<Cell> &Out) override
	{
		Out.clear();
		for (int r = First; r < First + Count; ++r)
			Out.push_back(Channel >= 0 && Channel < static_cast<int>(Rows.size()) && r >= 0 &&
				r < static_cast<int>(Rows[static_cast<size_t>(Channel)].size()) ? Rows[static_cast<size_t>(Channel)][static_cast<size_t>(r)] :
				Cell());
	}

	bool ReadClip(int, std::vector<std::vector<Cell> > &) override { return false; }
	bool CanChange(std::string &Why) override { Why = ReadOnly(); return false; }
	bool WriteSong(const std::vector<ChannelCells> &, const SongWrite &, std::string &Error) override { return Refuse(Error); }
	bool SetSections(const SectionList &, std::string &Error) override { return Refuse(Error); }
	bool SetKey(bool, const KeyChoice &, std::string &Error) override { return Refuse(Error); }
	bool SetMixer(const MixerSettings &, std::string &Error) override { return Refuse(Error); }
	void BeginGroup() override {}
	void EndGroup() override {}
	bool AddClip(std::string &, const std::vector<int> &, const std::vector<std::vector<Cell> > &, std::string &Error) override
	{
		return Refuse(Error);
	}
	int AddInstrument(const Preset &, const std::string &, bool, std::string &Error) override { Refuse(Error); return -1; }
	int AddN163Instrument(const WaveSound &, int, std::string &Error) override { Refuse(Error); return -1; }
	int AddFdsInstrument(const FdsSound &, std::string &Error) override { Refuse(Error); return -1; }
	int AddVrc7Instrument(int, const std::string &, std::string &Error) override { Refuse(Error); return -1; }
	int AddVrc7CustomInstrument(const std::vector<int> &, const std::string &, std::string &Error) override { Refuse(Error); return -1; }
	int AddDpcmKit(const DpcmKit &, std::string &Error) override { Refuse(Error); return -1; }
	int RefreshDpcmKit(const DpcmKit &, std::string &Error) override { Refuse(Error); return -1; }
	void CountInstrumentUses(std::vector<int> &InTracks, std::vector<int> &InClips) override
	{
		InTracks.assign(64, 0);
		InClips.assign(64, 0);
		for (size_t c = 0; c < Rows.size(); ++c)
			for (size_t r = 0; r < Rows[c].size(); ++r)
				if (Rows[c][r].Instrument < 64)
					++InTracks[Rows[c][r].Instrument];
	}
	bool RemoveInstruments(const std::vector<int> &, std::string &Error) override { return Refuse(Error); }
	int ExpansionNotes(const std::vector<std::string> &, int, std::vector<int> &Tracks) override { Tracks.clear(); return 0; }
	bool SetExpansion(const std::vector<std::string> &, int, int &, int &, std::string &Error) override { return Refuse(Error); }
	bool SetSpeedTempo(int, int, std::string &Error) override { return Refuse(Error); }
	bool SetMeter(int, int, int, std::string &Error) override { return Refuse(Error); }
	bool SetNames(const SongNames &, std::string &Error) override { return Refuse(Error); }
	bool SelectTrack(int Track, std::string &Error) override { return Track == Info.Track || Refuse(Error); }
	int AddTrack(const std::string &, const std::vector<std::vector<Cell> > &, int, int, std::string &Error) override
	{
		Refuse(Error);
		return -1;
	}
	bool RenderTrack(int, double, const RenderOptions &, std::vector<float> &, std::vector<float> &, int &,
		std::vector<std::pair<int, long long> > &, std::string &Error) override
	{
		Error = "This check doesn't render.";
		return false;
	}
	std::unique_ptr<RenderSet> RenderParts(int, double, const std::vector<RenderOptions> &, std::string &Error) override
	{
		Error = "This check doesn't render.";
		return std::unique_ptr<RenderSet>();
	}
	bool SaveFile(const std::string &, const std::string &, std::string &, std::string &Error) override { return Refuse(Error); }
	bool ReadFile(const std::string &, std::string &, std::string &Error) override { return Refuse(Error); }
	std::string LocalPath(const std::string &Path) override { return Path; }
	bool SaveInbox(const std::string &, const std::string &, std::string &, std::string &Error) override { return Refuse(Error); }
	bool ReadExport(const std::string &, std::string &, std::string &Error) override { return Refuse(Error); }
	bool ListLibrary(std::vector<std::string> &Names, std::string &) override { Names.clear(); return true; }
	bool ReadLibrary(const std::string &, std::string &, std::string &Error) override { return Refuse(Error); }
	bool SaveLibrary(const std::string &, const std::string &, std::string &, std::string &Error) override { return Refuse(Error); }
	bool DeleteLibrary(const std::string &, std::string &Error) override { return Refuse(Error); }
	bool LatestRecording(std::string &, std::string &Error) override { return Refuse(Error); }
	bool OpenText(const std::string &, std::string &Error) override { return Refuse(Error); }
	bool Play(const PlayRequest &, std::string &Error) override { return Refuse(Error); }
	void Stop() override {}
	bool Undo(std::string &Message) override { return Refuse(Message); }
	bool Save(const std::string &, std::string &Message) override { return Refuse(Message); }

private:
	static const char *ReadOnly() { return "The song is read from its file for a check; nothing in it changes."; }

	static bool Refuse(std::string &Error)
	{
		Error = ReadOnly();
		return false;
	}

	static unsigned Word(const std::vector<unsigned char> &Bytes, size_t At)
	{
		return Bytes[At] | (Bytes[At + 1] << 8) | (Bytes[At + 2] << 16) | (static_cast<unsigned>(Bytes[At + 3]) << 24);
	}

	// This fork's blocks, which nsf::ReadFtmFile passes over: the header (18 bytes and the version), then blocks of a
	// 16-byte name, a version and a size
	static void ReadOwnBlocks(const std::string &Path, SectionStore &Sections, KeyStore &Keys, MixStore &Mix, bool &Plus)
	{
		std::ifstream File(Path.c_str(), std::ios::binary);
		const std::vector<unsigned char> Bytes((std::istreambuf_iterator<char>(File)), std::istreambuf_iterator<char>());
		for (size_t At = 22; At + 24 <= Bytes.size(); ) {
			std::string Id;
			for (size_t i = At; i < At + 16 && Bytes[i] != 0; ++i)
				Id += static_cast<char>(Bytes[i]);
			const int Version = static_cast<int>(Word(Bytes, At + 16));
			const size_t Size = Word(Bytes, At + 20);
			if (Size > Bytes.size() - At - 24)
				break;
			const unsigned char *pData = &Bytes[At + 24];
			if (Id == SectionStore::BLOCK_ID)
				Sections.Read(pData, Size, Version);
			else if (Id == KeyStore::BLOCK_ID)
				Keys.Read(pData, Size, Version);
			else if (Id == MixStore::BLOCK_ID)
				Mix.Read(pData, Size, Version);
			else if (Id == Vrc6PlusMark::BLOCK_ID)
				Plus = true;
			At += 24 + Size;
		}
	}

	void AddChannel(const std::string &Name, const char *pChip, int Kind, int ChanId, const std::string &Group = std::string())
	{
		ChannelInfo C;
		C.Name = Name;
		C.Chip = pChip;
		C.Kind = Kind;
		C.ChanId = ChanId;
		C.Group = Group;
		Info.Channels.push_back(C);
	}

	// As McpConnection.cpp names them (chan_id_t numbers)
	void AddChannels(const nsf::Module &M, bool Plus)
	{
		Info.Channels.clear();
		const bool AsPlus = Plus && (M.Expansion & nsf::EXP_VRC6) != 0 && (M.Expansion & nsf::EXP_N163) != 0 && M.N163Channels == 4;
		AddChannel("Pulse 1", "2A03", KIND_PULSE, 0);
		AddChannel("Pulse 2", "2A03", KIND_PULSE, 1);
		AddChannel("Triangle", "2A03", KIND_TRIANGLE, 2);
		AddChannel("Noise", "2A03", KIND_NOISE, 3);
		AddChannel("DPCM", "2A03", KIND_DPCM, 4);
		if (M.Expansion & nsf::EXP_VRC6) {
			const std::string Group = AsPlus ? VRC6_PLUS : "";
			AddChannel("Pulse 1", "VRC6", KIND_PULSE, 5, Group);
			AddChannel("Pulse 2", "VRC6", KIND_PULSE, 6, Group);
			AddChannel("Sawtooth", "VRC6", KIND_SAWTOOTH, 7, Group);
		}
		if (M.Expansion & nsf::EXP_MMC5) {
			AddChannel("Pulse 1", "MMC5", KIND_PULSE, 8);
			AddChannel("Pulse 2", "MMC5", KIND_PULSE, 9);
		}
		if (M.Expansion & nsf::EXP_N163)
			for (int i = 0; i < M.N163Channels && i < 8; ++i) {
				AddChannel(AsPlus ? SoftChannelName(i) : "Namco " + std::to_string(i + 1), "N163", KIND_OTHER, 11 + i,
					AsPlus ? VRC6_PLUS : "");
				if (AsPlus)
					Info.Channels.back().OldName = SoftChannelOldName(i);
			}
		if (M.Expansion & nsf::EXP_FDS)
			AddChannel("FDS", "FDS", KIND_OTHER, 19);
		if (M.Expansion & nsf::EXP_VRC7)
			for (int i = 0; i < 6; ++i)
				AddChannel("FM Channel " + std::to_string(i + 1), "VRC7", KIND_FM, 20 + i);
		if (M.Expansion & nsf::EXP_S5B)
			for (int i = 0; i < 3; ++i)
				AddChannel("Square " + std::to_string(i + 1), "5B", KIND_SQUARE, 26 + i);
		for (size_t c = 0; c < Info.Channels.size(); ++c) {
			int Same = 0;
			for (size_t k = 0; k < Info.Channels.size(); ++k)
				Same += Info.Channels[k].Chip == Info.Channels[c].Chip ? 1 : 0;
			Info.Channels[c].ChipChannels = Same;
		}
	}

	static void SequenceOf(const nsf::Macro &In, SequenceInfo &Out)
	{
		Out.Items = In.Items;
		Out.Loop = In.Items.empty() ? -1 : In.Loop;
		Out.Release = In.Items.empty() ? -1 : In.Release;
		Out.Setting = In.Setting;
	}

	static bool LoopsUpAndDown(const nsf::Macro &Seq)
	{
		bool Up = false, Down = false;
		for (int i = Seq.Loop; i >= 0 && i < static_cast<int>(Seq.Items.size()); ++i) {
			Up = Up || Seq.Items[static_cast<size_t>(i)] > 0;
			Down = Down || Seq.Items[static_cast<size_t>(i)] < 0;
		}
		return Up && Down;
	}

	static InstrumentInfo InstrumentOf(const nsf::Instrument &In, int Index)
	{
		static const char *const CHIPS[] = { "2A03", "VRC6", "VRC7", "FDS", "N163", "5B" };
		InstrumentInfo Out;
		Out.Index = Index;
		Out.Name = In.Name;
		Out.Chip = In.Kind >= 0 && In.Kind < 6 ? CHIPS[In.Kind] : "?";
		if (In.Kind == nsf::INST_VRC7)
			return Out;
		const nsf::Macro &Volume = In.Macros[nsf::SEQ_VOLUME];
		Out.Releases = !Volume.Empty() && Volume.Release >= 0;
		Out.Vibrato = LoopsUpAndDown(In.Macros[nsf::SEQ_PITCH]);
		Out.Volume = Volume.Items;
		Out.VolumeLoop = Volume.Empty() ? -1 : Volume.Loop;
		Out.VolumeRelease = Volume.Empty() ? -1 : Volume.Release;
		if (In.Kind == nsf::INST_FDS) {
			for (size_t i = 0; i < Out.Volume.size(); ++i)
				Out.Volume[i] = FdsVolumeAs15(Out.Volume[i]);
			Out.FdsWave = In.FdsWave;
			Out.ModTable = In.FdsMod;
			Out.ModSpeed = In.ModRate;
			Out.ModDepth = In.ModDepth;
			Out.ModDelay = In.ModDelay;
		}
		SequenceOf(In.Macros[nsf::SEQ_ARPEGGIO], Out.Steps);
		SequenceOf(In.Macros[nsf::SEQ_PITCH], Out.Pitch);
		SequenceOf(In.Macros[nsf::SEQ_HIPITCH], Out.HiPitch);
		if (In.Kind == nsf::INST_S5B)
			SequenceOf(In.Macros[nsf::SEQ_DUTY], Out.Mode);
		else if (In.Kind == nsf::INST_2A03 || In.Kind == nsf::INST_VRC6 || In.Kind == nsf::INST_N163)
			SequenceOf(In.Macros[nsf::SEQ_DUTY], Out.Duty);
		if (In.Kind == nsf::INST_VRC6)
			for (size_t i = 0; i < In.Macros[nsf::SEQ_DUTY].Items.size(); ++i)
				Out.OddDuty = Out.OddDuty || (In.Macros[nsf::SEQ_DUTY].Items[i] & 1) != 0;
		// The looping arpeggio's notes besides the note (McpConnection.cpp's LoopingArpeggio)
		const nsf::Macro &Arp = In.Macros[nsf::SEQ_ARPEGGIO];
		if (!Arp.Empty() && Arp.Setting == nsf::ARP_ABSOLUTE && Arp.Loop >= 0 && Arp.Loop < static_cast<int>(Arp.Items.size())) {
			bool Root = false;
			for (size_t i = static_cast<size_t>(Arp.Loop); i < Arp.Items.size(); ++i) {
				if (Arp.Items[i] == 0)
					Root = true;
				else if (std::find(Out.Arpeggio.begin(), Out.Arpeggio.end(), Arp.Items[i]) == Out.Arpeggio.end())
					Out.Arpeggio.push_back(Arp.Items[i]);
			}
			if (Out.Arpeggio.size() == 1 && !Root)
				Out.Arpeggio.clear();
			std::sort(Out.Arpeggio.begin(), Out.Arpeggio.end());
		}
		if (In.Kind == nsf::INST_N163) {
			Out.WavePos = In.WavePos;
			Out.WaveSize = In.WaveSize;
			Out.Waves = In.Waves;
		}
		return Out;
	}

	static Cell CellOf(const nsf::Cell &In)
	{
		Cell Out;
		if (In.Note == nsf::NOTE_HALT)
			Out.Note = NOTE_HALT;
		else if (In.Note == nsf::NOTE_RELEASE)
			Out.Note = NOTE_RELEASE;
		else if (In.Note >= 0)
			Out.SetPitch(In.Note);
		if (In.Instrument >= 0 && In.Instrument < 64)
			Out.Instrument = static_cast<unsigned char>(In.Instrument);
		if (In.Volume >= 0 && In.Volume < 16)
			Out.Volume = static_cast<unsigned char>(In.Volume);
		for (int e = 0; e < nsf::MAX_EFFECTS && e < EFFECT_COLUMNS; ++e) {
			if (In.Effects[e].Letter == 0)
				continue;
			char Text[8];
			std::snprintf(Text, sizeof(Text), "%c%02X", In.Effects[e].Letter, In.Effects[e].Param & 0xFF);
			int Effect = 0, Param = 0;
			if (ParseEffect(Text, Effect, Param)) {
				Out.Effect[e] = static_cast<unsigned char>(Effect);
				Out.Param[e] = static_cast<unsigned char>(Param);
			}
		}
		return Out;
	}
};

}	// namespace ftapi

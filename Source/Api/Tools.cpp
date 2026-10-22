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

#include "Tools.h"
#include "Arrange.h"
#include "Chords.h"
#include "DpcmKit.h"
#include "GameAudio.h"
#include "Jingles.h"
#include "Music.h"
#include "Ogg.h"
#include "Picture.h"
#include "Presets.h"
#include "References.h"
#include "Song.h"
#include "SoundEffects.h"
#include "Effects.h"
#include "Wavetables.h"
#include "../Nsf/FtmFile.h"
#include "../Nsf/Import.h"
#include "../Nsf/NsfFile.h"
#include "../Nsf/Study.h"
#include "../Nsf/Tracker.h"
#include "../Transcribe/Transcribe.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <initializer_list>
#include <map>
#include <set>

namespace ftapi {

namespace {

const int MAX_READ_BARS = 32;
const int MAX_PICTURE_BARS = 32;
const int MAX_LISTED = 15;
const char *const SOUNDTRACK_NOTES = "Soundtrack notes.txt";

// A tool's arguments or the song don't allow it: the reason goes back to Claude
struct Problem {
	std::string Text;
	explicit Problem(const std::string &Reason) : Text(Reason) {}
};

std::string Format(const char *pFormat, ...)
{
	char Buffer[1024];
	va_list Args;
	va_start(Args, pFormat);
	std::vsnprintf(Buffer, sizeof(Buffer), pFormat, Args);
	va_end(Args);
	return Buffer;
}

std::string Trim(const std::string &Text)
{
	size_t First = 0, Last = Text.size();
	while (First < Last && (Text[First] == ' ' || Text[First] == '\t'))
		++First;
	while (Last > First && (Text[Last - 1] == ' ' || Text[Last - 1] == '\t'))
		--Last;
	return Text.substr(First, Last - First);
}

// Lower case letters and digits only: "Hi-Hat" -> "hihat"
std::string Squeezed(const std::string &Text)
{
	std::string Out;
	for (size_t i = 0; i < Text.size(); ++i) {
		const unsigned char c = static_cast<unsigned char>(Text[i]);
		if (std::isalnum(c))
			Out += static_cast<char>(std::tolower(c));
	}
	return Out;
}

std::string Plural(int Count, const char *One, const char *Many)
{
	return Format("%d %s", Count, Count == 1 ? One : Many);
}

// Building tool schemas

Json Prop(const char *Type, const char *Description)
{
	Json P = Json::MakeObject();
	P.Set("type", Type);
	P.Set("description", Description);
	return P;
}

Json EnumProp(const char *Description, const std::vector<std::string> &Values)
{
	Json P = Prop("string", Description);
	Json List = Json::MakeArray();
	for (size_t i = 0; i < Values.size(); ++i)
		List.Push(Values[i]);
	P.Set("enum", List);
	return P;
}

Json ChannelProp(const char *Description)
{
	Json P = Json::MakeObject();
	Json Types = Json::MakeArray();
	Types.Push("string");
	Types.Push("integer");
	P.Set("type", Types);
	P.Set("description", Description);
	return P;
}

// One channel, several in a list, or "all"
// Words, as a list or as one text with commas
Json WordListProp(const char *Description)
{
	Json P = Prop("array", Description);
	Json Types = Json::MakeArray();
	Types.Push("array");
	Types.Push("string");
	P.Set("type", Types);
	Json Item = Json::MakeObject();
	Item.Set("type", "string");
	P.Set("items", Item);
	return P;
}

Json ChannelListProp(const char *Description)
{
	Json P = ChannelProp(Description);
	Json Types = Json::MakeArray();
	Types.Push("array");
	Types.Push("string");
	Types.Push("integer");
	P.Set("type", Types);
	Json Item = Json::MakeObject();
	Json ItemTypes = Json::MakeArray();
	ItemTypes.Push("string");
	ItemTypes.Push("integer");
	Item.Set("type", ItemTypes);
	P.Set("items", Item);
	return P;
}

// Words or a number, like vibrato's "slow" or 3
Json LevelProp(const char *Description)
{
	return ChannelProp(Description);
}

Json BarProp(const char *Description)
{
	Json P = Prop("integer", Description);
	P.Set("minimum", 1);
	return P;
}

class Schema {
public:
	Schema() : m_Properties(Json::MakeObject()), m_Required(Json::MakeArray()) {}
	Schema &Add(const char *Name, const Json &Property, bool Required = false)
	{
		m_Properties.Set(Name, Property);
		if (Required)
			m_Required.Push(Name);
		return *this;
	}
	Schema &Range()
	{
		Add("from_bar", BarProp("First bar (from 1). Default: the start of the song."));
		Add("to_bar", BarProp("Last bar, included. Default: the end of the song."));
		return *this;
	}
	Schema &ClipChoice()
	{
		Add("as_clip", Prop("boolean", "Save the result as a clip even when direct changes are allowed (e.g. to offer an alternative)."));
		Add("clip_name", Prop("string", "Name for the clip, when the result becomes one."));
		return *this;
	}
	Json Get() const
	{
		Json S = Json::MakeObject();
		S.Set("type", "object");
		S.Set("properties", m_Properties);
		if (m_Required.Size() > 0)
			S.Set("required", m_Required);
		S.Set("additionalProperties", false);
		return S;
	}

private:
	Json m_Properties;
	Json m_Required;
};

std::vector<std::string> PresetNames()
{
	std::vector<std::string> Names;
	for (size_t i = 0; i < Presets().size(); ++i)
		Names.push_back(Presets()[i].Name);
	return Names;
}

std::vector<std::string> ModeNames()
{
	std::vector<std::string> Names;
	for (int m = 0; m < ModeCount(); ++m)
		Names.push_back(ModeName(m));
	return Names;
}

// The song as one tool call sees it

class Session {
	SongHost &m_Host;

public:
	explicit Session(SongHost &Host) : m_Host(Host), Time(64, 1, 4, 16)
	{
		Load();
	}

	void Load()
	{
		std::string Error;
		if (!m_Host.GetSong(Song, Error))
			throw Problem(Error.empty() ? "No song is open in FamiTracker." : Error);
		Time = Timing(Song.PatternLength, Song.Frames, Song.FirstHighlight, Song.SecondHighlight);
		Unhide();
		m_Cache.clear();
		m_Notes.clear();
	}

	// After instruments were added; the cells stay as they are
	void Refresh()
	{
		std::string Error;
		SongInfo Info;
		if (m_Host.GetSong(Info, Error)) {
			Song = Info;
			Unhide();
		}
	}

	// The effect columns a channel shows in FamiTracker, the ones that play. The tools use all
	// four (FamiTracker shows more where the effects written need them), so the effects of the
	// columns hidden now are left out of the cells they read
	int Shown(int Channel) const
	{
		return Channel >= 0 && static_cast<size_t>(Channel) < m_Shown.size() ? m_Shown[static_cast<size_t>(Channel)] : 1;
	}

	SongHost &Host() { return m_Host; }

	const std::vector<Cell> &Cells(int Channel)
	{
		std::map<int, std::vector<Cell> >::iterator it = m_Cache.find(Channel);
		if (it == m_Cache.end()) {
			std::vector<Cell> Read;
			m_Host.ReadCells(Channel, 0, Time.TotalRows(), Read);
			Read.resize(Time.TotalRows());
			for (size_t r = 0; r < Read.size(); ++r)
				for (int k = std::max(1, Shown(Channel)); k < EFFECT_COLUMNS; ++k) {
					Read[r].Effect[k] = EFF_NONE;
					Read[r].Param[k] = 0;
				}
			it = m_Cache.insert(std::make_pair(Channel, Read)).first;
		}
		return it->second;
	}

	const std::vector<NoteInfo> &Notes(int Channel)
	{
		std::map<int, std::vector<NoteInfo> >::iterator it = m_Notes.find(Channel);
		if (it == m_Notes.end())
			it = m_Notes.insert(std::make_pair(Channel, ReadNotes(Cells(Channel), Song.Channels[Channel].EffectColumns))).first;
		return it->second;
	}

	int ChannelCount() const { return static_cast<int>(Song.Channels.size()); }
	const ChannelInfo &Channel(int Index) const { return Song.Channels[Index]; }
	int MaxRows() const { return Song.MaxFrames * Song.PatternLength; }
	int MaxBars() const { return Song.MaxFrames * Time.BarsPerFrame(); }

	SongInfo Song;
	Timing Time;
	std::vector<std::string> Remarks;	// told along with the result

private:
	void Unhide()
	{
		m_Shown.clear();
		for (size_t c = 0; c < Song.Channels.size(); ++c) {
			m_Shown.push_back(std::max(1, std::min(EFFECT_COLUMNS, Song.Channels[c].EffectColumns)));
			Song.Channels[c].EffectColumns = EFFECT_COLUMNS;
		}
	}

	std::map<int, std::vector<Cell> > m_Cache;
	std::map<int, std::vector<NoteInfo> > m_Notes;
	std::vector<int> m_Shown;
};

bool IsMelodic(const ChannelInfo &Channel)
{
	return Channel.Kind != KIND_NOISE && Channel.Kind != KIND_DPCM;
}

bool IsBass(const ChannelInfo &Channel)
{
	return Channel.Kind == KIND_TRIANGLE || Channel.Kind == KIND_SAWTOOTH;
}

std::string InstrumentChip(const ChannelInfo &Channel)
{
	return Channel.Chip == "MMC5" ? "2A03" : Channel.Chip;
}

// How loud a channel plays at a volume, in dB against a 2A03 pulse at 15, as FamiTracker renders
// it (measured): the VRC7's sounds about 5 dB louder at 15, each volume step 3 dB softer; the
// N163's wave sounds about 8 dB quieter; everything else in proportion to the volume
const double FM_FULL_DB = 5.0, FM_STEP_DB = 3.0, N163_FULL_DB = -8.0;

double LevelDb(const ChannelInfo &Channel, int Volume)
{
	if (Volume <= 0)
		return -60.0;
	if (Channel.Kind == KIND_FM)
		return FM_FULL_DB - FM_STEP_DB * (15 - std::min(15, Volume));
	return (Channel.Chip == "N163" ? N163_FULL_DB : 0.0) + 20.0 * std::log10(std::min(15, Volume) / 15.0);
}

// The volume (1-15) at which a channel plays nearest a level in dB
int VolumeForDb(const ChannelInfo &Channel, double Db)
{
	double Volume;
	if (Channel.Kind == KIND_FM)
		Volume = 15.0 - (FM_FULL_DB - Db) / FM_STEP_DB;
	else
		Volume = 15.0 * std::pow(10.0, (Db - (Channel.Chip == "N163" ? N163_FULL_DB : 0.0)) / 20.0);
	return std::max(1, std::min(15, static_cast<int>(std::floor(Volume + 1e-9 + 0.5))));
}

// The volume on To that sounds like Volume on From made Ratio as loud, the way a pulse's volume
// scales (11.0 / 15: four steps under full): the same number between chips that count volume alike
int MatchVolume(const ChannelInfo &From, int Volume, double Ratio, const ChannelInfo &To)
{
	return VolumeForDb(To, LevelDb(From, Volume) + 20.0 * std::log10(std::max(0.01, Ratio)));
}

// A part's default volume on a channel, given as on a pulse: the same, but nearer 15 on the VRC7,
// whose steps are 3 dB each
int PartVolume(const ChannelInfo &Channel, int Volume)
{
	return MatchVolume(Channel, 15, Volume / 15.0, Channel);
}

const InstrumentInfo *FindInstrument(const SongInfo &Song, int Index)
{
	for (size_t i = 0; i < Song.Instruments.size(); ++i)
		if (Song.Instruments[i].Index == Index)
			return &Song.Instruments[i];
	return 0;
}

std::string InstrumentLabel(const SongInfo &Song, int Index)
{
	const InstrumentInfo *pInfo = FindInstrument(Song, Index);
	if (pInfo == 0)
		return Format("instrument %02X", Index);
	return Format("\"%s\" (%02X)", pInfo->Name.c_str(), Index);
}

// The notes an instrument's looping arpeggio adds above each note it plays ({4, 7, 11}); empty for none
const std::vector<int> &InstrumentChord(const Session &S, int Instrument)
{
	static const std::vector<int> NONE;
	const InstrumentInfo *pInfo = FindInstrument(S.Song, Instrument);
	return pInfo != 0 ? pInfo->Arpeggio : NONE;
}

// The semitones above a note that sound with it: its 0xy arpeggio's (Arpeggio, 0 for none), else its instrument's
std::vector<int> ChordAbove(const Session &S, const NoteInfo &N, int Arpeggio)
{
	if (Arpeggio == 0)
		return InstrumentChord(S, N.Instrument);
	std::vector<int> Out(1, Arpeggio >> 4);
	if ((Arpeggio & 15) != 0)
		Out.push_back(Arpeggio & 15);
	return Out;
}

// Whether a note sounds a chord: an 0xy arpeggio on its rows, or its instrument's own
bool SoundsChord(const Session &S, const NoteInfo &N)
{
	for (size_t e = 0; e < N.Effects.size(); ++e)
		if (N.Effects[e].second.first == EFF_ARPEGGIO && N.Effects[e].second.second != 0)
			return true;
	return !InstrumentChord(S, N.Instrument).empty();
}

std::vector<bool> ReleasingInstruments(const SongInfo &Song)
{
	std::vector<bool> Releases(NO_INSTRUMENT, false);
	for (size_t i = 0; i < Song.Instruments.size(); ++i)
		if (Song.Instruments[i].Index >= 0 && Song.Instruments[i].Index < NO_INSTRUMENT)
			Releases[Song.Instruments[i].Index] = Song.Instruments[i].Releases;
	return Releases;
}

std::string ChannelList(const Session &S)
{
	std::string List;
	for (int c = 0; c < S.ChannelCount(); ++c) {
		if (c > 0)
			List += ", ";
		List += Format("%d %s", c + 1, S.Channel(c).FullName().c_str());
	}
	return List;
}

// "2A03 Pulse 2 and VRC6 Sawtooth", "" when the user hears every channel
std::string MutedChannels(const Session &S)
{
	std::vector<std::string> Names;
	for (int c = 0; c < S.ChannelCount(); ++c)
		if (S.Channel(c).Muted)
			Names.push_back(S.Channel(c).FullName());
	std::string List;
	for (size_t i = 0; i < Names.size(); ++i)
		List += (i == 0 ? "" : (i + 1 == Names.size() ? " and " : ", ")) + Names[i];
	return List;
}

std::string PositionText(const Session &S, int Row)
{
	return Format("%s (frame %02X, row %02X)", S.Time.Position(Row).c_str(), Row / S.Time.PatternLength,
		Row % S.Time.PatternLength);
}

std::string PitchText(const ChannelInfo &Channel, int Pitch)
{
	return Channel.Kind == KIND_NOISE ? NoisePitchName(Pitch & 15) : PitchName(Pitch);
}

// Arguments

const Json *Arg(const Json &Args, const char *Name)
{
	const Json *pValue = Args.Get(Name);
	return (pValue == 0 || pValue->IsNull()) ? 0 : pValue;
}

std::string StringArg(const Json &Args, const char *Name, const std::string &Default = std::string())
{
	const Json *pValue = Arg(Args, Name);
	if (pValue == 0)
		return Default;
	if (pValue->IsString())
		return Trim(pValue->String());
	if (pValue->IsNumber())
		return FormatNumber(pValue->Number());
	throw Problem(Format("%s should be text.", Name));
}

bool BoolArg(const Json &Args, const char *Name, bool Default)
{
	const Json *pValue = Arg(Args, Name);
	if (pValue == 0)
		return Default;
	if (pValue->IsBool())
		return pValue->Bool();
	if (pValue->IsString()) {
		const std::string Text = Lowercase(pValue->String());
		if (Text == "true" || Text == "yes")
			return true;
		if (Text == "false" || Text == "no")
			return false;
	}
	throw Problem(Format("%s should be true or false.", Name));
}

double NumberArg(const Json &Args, const char *Name, double Default, double Min, double Max)
{
	const Json *pValue = Arg(Args, Name);
	if (pValue == 0)
		return Default;
	double Value;
	if (pValue->IsNumber())
		Value = pValue->Number();
	else if (pValue->IsString()) {
		char *pEnd = 0;
		Value = std::strtod(pValue->String().c_str(), &pEnd);
		if (pEnd == pValue->String().c_str() || *pEnd != 0)
			throw Problem(Format("%s should be a number.", Name));
	}
	else
		throw Problem(Format("%s should be a number.", Name));
	if (!(Value >= Min && Value <= Max))
		throw Problem(Format("%s should be from %s to %s.", Name, FormatNumber(Min).c_str(), FormatNumber(Max).c_str()));
	return Value;
}

int IntArg(const Json &Args, const char *Name, int Default, int Min, int Max)
{
	const double Value = NumberArg(Args, Name, Default, Min, Max);
	if (Value != std::floor(Value))
		throw Problem(Format("%s should be a whole number.", Name));
	return static_cast<int>(Value);
}

int ParseChannel(const Session &S, const Json &Value, const char *Name)
{
	int Number = 0;
	if (Value.IsNumber()) {
		if (Value.Number() != std::floor(Value.Number()))
			throw Problem(Format("%s: channels are numbered 1-%d.", Name, S.ChannelCount()));
		Number = static_cast<int>(Value.Number());
	}
	else if (Value.IsString()) {
		std::string Text;
		const std::string Raw = Lowercase(Trim(Value.String()));
		for (size_t i = 0; i < Raw.size(); ++i)	// "VRC6  Pulse-1" -> "vrc6 pulse 1"
			if (Raw[i] != ' ' && Raw[i] != '-' && Raw[i] != '_')
				Text += Raw[i];
		if (!Text.empty() && Text.find_first_not_of("0123456789") == std::string::npos)
			Number = std::atoi(Text.c_str());
		else {
			std::string Alias = Text;
			const char *const ALIASES[][2] = {
				{ "square1", "pulse1" }, { "square2", "pulse2" }, { "tri", "triangle" }, { "saw", "sawtooth" },
				{ "vrc6saw", "vrc6sawtooth" }, { "drums", "noise" }, { "samples", "dpcm" }, { "dmc", "dpcm" },
			};
			for (size_t a = 0; a < sizeof(ALIASES) / sizeof(ALIASES[0]); ++a)
				if (Text == ALIASES[a][0])
					Alias = ALIASES[a][1];
			for (int Pass = 0; Pass < 2 && Number == 0; ++Pass) {
				for (int c = 0; c < S.ChannelCount(); ++c) {
					std::string Full = Lowercase(Pass == 0 ? S.Channel(c).FullName() : S.Channel(c).Name), Squeezed;
					for (size_t i = 0; i < Full.size(); ++i)
						if (Full[i] != ' ' && Full[i] != '-' && Full[i] != '_')
							Squeezed += Full[i];
					if (Squeezed == Text || Squeezed == Alias) {
						Number = c + 1;
						break;
					}
				}
			}
		}
	}
	if (Number < 1 || Number > S.ChannelCount())
		throw Problem(Format("Unknown channel for %s. The song's channels: %s.", Name, ChannelList(S).c_str()));
	return Number - 1;
}

int ChannelArg(const Session &S, const Json &Args, const char *Name, int Default = -1)
{
	const Json *pValue = Arg(Args, Name);
	if (pValue == 0) {
		if (Default >= 0)
			return Default;
		throw Problem(Format("Say which channel with %s. The song's channels: %s.", Name, ChannelList(S).c_str()));
	}
	return ParseChannel(S, *pValue, Name);
}

// The bars to work on: from_bar and to_bar, defaulting to the whole song
void BarRange(const Session &S, const Json &Args, int &From, int &To, bool PastEnd = false)
{
	const int Last = PastEnd ? S.MaxBars() : S.Time.BarCount();
	From = IntArg(Args, "from_bar", 1, 1, Last);
	To = IntArg(Args, "to_bar", std::max(From, S.Time.BarCount()), 1, Last);
	if (To < From)
		throw Problem("to_bar comes before from_bar.");
}

// The sampled drum kit's instrument (DpcmKit.h), added with its samples when
// the song has none
int SampleDrumsInstrument(Session &S)
{
	const DpcmKit &Kit = SampleDrums();
	for (size_t i = 0; i < S.Song.Instruments.size(); ++i)
		if (Lowercase(S.Song.Instruments[i].Name) == Lowercase(Kit.Name) && S.Song.Instruments[i].Chip == "2A03")
			return S.Song.Instruments[i].Index;
	std::string Error;
	const int Index = S.Host().AddDpcmKit(Kit, Error);
	if (Index < 0)
		throw Problem("The sampled drums couldn't be added: " + (Error.empty() ? std::string("FamiTracker refused.") : Error));
	S.Refresh();
	S.Remarks.push_back(Format("Added the instrument %s: sampled drums for the DPCM channel (kick on C-3, snare on D-3, low tom on "
		"F-3, high tom on A-3).", InstrumentLabel(S.Song, Index).c_str()));
	return Index;
}

// An N163 sound as an instrument, or the N163 one of its name already there:
// its wave goes where no other N163 instrument's is. Where says where (empty
// when it was there already).
int AddWaveInstrument(Session &S, const WaveSound &Sound, std::string &Where)
{
	const std::string Name = Lowercase(Sound.InstrumentName());
	std::vector<std::pair<int, int> > Used;
	for (size_t i = 0; i < S.Song.Instruments.size(); ++i) {
		const InstrumentInfo &I = S.Song.Instruments[i];
		if (I.Chip != "N163")
			continue;
		if (Lowercase(I.Name) == Name) {
			Where.clear();
			return I.Index;
		}
		if (I.WaveSize > 0)
			Used.push_back(std::make_pair(I.WavePos, I.WaveSize));
	}
	int Channels = 0;
	for (int c = 0; c < S.ChannelCount(); ++c)
		Channels += S.Channel(c).Chip == "N163" ? 1 : 0;
	const int Memory = N163WaveMemory(Channels);
	const int Place = FindWavePlace(N163_WAVE_SIZE, Memory, Used);
	if (Place < 0)
		throw Problem(Format("The N163's wave memory is full: with %d channels it holds %d samples, and the song's N163 instruments "
			"take them all. Fewer N163 channels make room (set_expansion), or use one of the song's N163 instruments.", Channels,
			Memory));
	std::string Error;
	const int Index = S.Host().AddN163Instrument(Sound, Place, Error);
	if (Index < 0)
		throw Problem(Error.empty() ? "The instrument couldn't be added." : Error);
	S.Refresh();
	Used.push_back(std::make_pair(Place, N163_WAVE_SIZE));
	int Room = 0;
	for (std::vector<std::pair<int, int> > More(Used); FindWavePlace(N163_WAVE_SIZE, Memory, More) >= 0; ++Room)
		More.push_back(std::make_pair(FindWavePlace(N163_WAVE_SIZE, Memory, More), N163_WAVE_SIZE));
	Where = Format("its wave takes samples %d-%d of the N163's %d, which have room for %d more %s", Place, Place + N163_WAVE_SIZE - 1,
		Memory, Room, Room == 1 ? "sound" : "sounds");
	return Index;
}

// An instrument for notes on a channel: one in the song (by name, or by its
// number as FamiTracker shows it), or a preset, which is added
int ResolveInstrument(Session &S, const std::string &Wanted, int Channel)
{
	const ChannelInfo &Info = S.Channel(Channel);
	const std::string Chip = InstrumentChip(Info);
	const std::string Lower = Lowercase(Trim(Wanted));
	if (Lower.empty())
		throw Problem("The instrument name is empty.");
	const InstrumentInfo *pWrongChip = 0;
	for (size_t i = 0; i < S.Song.Instruments.size(); ++i) {
		const InstrumentInfo &I = S.Song.Instruments[i];
		if (Lowercase(I.Name) == Lower) {
			if (I.Chip == Chip)
				return I.Index;
			pWrongChip = &I;
		}
	}
	// The sawtooth's sounds carry " (saw)" after the name they were made under
	if (Info.Kind == KIND_SAWTOOTH)
		for (size_t i = 0; i < S.Song.Instruments.size(); ++i)
			if (S.Song.Instruments[i].Chip == Chip && Lowercase(S.Song.Instruments[i].Name) == Lower + " (saw)")
				return S.Song.Instruments[i].Index;
	// VRC7 channels play the chip's own patches (a preset's name picks the one most like it), or a
	// custom patch of this connection's design
	if (Chip == "VRC7") {
		const Vrc7Custom *pCustom = FindVrc7Custom(Wanted);
		if (pCustom != 0 && pWrongChip != 0 && Squeezed(pCustom->Name) != Squeezed(Wanted))
			pCustom = 0;	// the song's instrument of that name was meant
		if (pCustom != 0) {
			// Other custom patches the song plays already: the chip holds one at a time
			std::string Others;
			for (size_t i = 0; i < S.Song.Instruments.size(); ++i) {
				const InstrumentInfo &I = S.Song.Instruments[i];
				const Vrc7Custom *pOther = I.Chip == "VRC7" ? FindVrc7Custom(I.Name) : 0;
				if (pOther != 0 && pOther != pCustom)
					Others += (Others.empty() ? "" : ", ") + InstrumentLabel(S.Song, I.Index);
			}
			const size_t Before = S.Song.Instruments.size();
			std::string Error;
			const std::vector<int> Regs(pCustom->Regs, pCustom->Regs + 8);
			const int Index = S.Host().AddVrc7CustomInstrument(Regs, pCustom->InstrumentName(), Error);
			if (Index < 0)
				throw Problem(Error.empty() ? "The instrument couldn't be added." : Error);
			S.Refresh();
			if (S.Song.Instruments.size() > Before) {
				S.Remarks.push_back(Format("Added the instrument %s (a VRC7 custom patch: %s).", InstrumentLabel(S.Song, Index).c_str(),
					pCustom->Description.c_str()));
				if (!Others.empty())
					S.Remarks.push_back("The VRC7 holds one custom patch at a time, and " + Others + " plays another: where two "
						"channels play both at once, both sound like the one struck last. Keep custom patches to different "
						"sections, or give one part a built-in sound.");
			}
			return Index;
		}
		const Vrc7Patch *pPatch = FindVrc7Patch(Wanted);
		if (pPatch != 0 && pWrongChip != 0 && Squeezed(pPatch->Name) != Squeezed(Wanted))
			pPatch = 0;	// the song's instrument of that name was meant
		if (pPatch != 0) {
			const size_t Before = S.Song.Instruments.size();
			std::string Error;
			const int Index = S.Host().AddVrc7Instrument(pPatch->Number, pPatch->InstrumentName(), Error);
			if (Index < 0)
				throw Problem(Error.empty() ? "The instrument couldn't be added." : Error);
			S.Refresh();
			if (S.Song.Instruments.size() > Before)
				S.Remarks.push_back(Format("Added the instrument %s (VRC7 patch %d, %s).", InstrumentLabel(S.Song, Index).c_str(),
					pPatch->Number, pPatch->Description.c_str()));
			return Index;
		}
	}
	// N163 channels play wave sounds drawn for them (a preset's name picks the one most like it)
	if (Chip == "N163") {
		const WaveSound *pSound = FindWaveSound(Wanted);
		if (pSound != 0) {
			const size_t Before = S.Song.Instruments.size();
			std::string Where;
			const int Index = AddWaveInstrument(S, *pSound, Where);
			if (S.Song.Instruments.size() > Before)
				S.Remarks.push_back(Format("Added the instrument %s (N163: %s; %s).", InstrumentLabel(S.Song, Index).c_str(),
					pSound->Description.c_str(), Where.c_str()));
			return Index;
		}
	}
	const Preset *pPreset = FindPreset(Wanted);
	if (pPreset != 0 && pWrongChip != 0 && Squeezed(pPreset->Name) != Squeezed(Wanted))
		pPreset = 0;	// the song's instrument of that name was meant, not a preset it's a nickname of
	if (pPreset != 0) {
		if (pPreset->Drum && Info.Kind != KIND_NOISE)
			throw Problem(Format("The preset \"%s\" is a drum for the noise channel.", pPreset->Name.c_str()));
		if (!pPreset->Drum && Info.Kind == KIND_NOISE)
			throw Problem(Format("The noise channel needs a drum preset (kick, snare, hi-hat, open hi-hat), not \"%s\".", pPreset->Name.c_str()));
		if (Chip != "2A03" && Chip != "VRC6")
			throw Problem(Format("Presets are for 2A03, MMC5, VRC6 and VRC7 channels; %s needs one of the song's %s instruments.",
				Info.FullName().c_str(), Chip.c_str()));
		const size_t Before = S.Song.Instruments.size();
		std::string Error;
		const int Index = S.Host().AddInstrument(*pPreset, Chip, Info.Kind == KIND_SAWTOOTH, Error);
		if (Index < 0)
			throw Problem(Error.empty() ? "The instrument couldn't be added." : Error);
		S.Refresh();
		if (S.Song.Instruments.size() > Before)
			S.Remarks.push_back(Format("Added the instrument %s (%s) from the preset \"%s\".", InstrumentLabel(S.Song, Index).c_str(),
				Chip.c_str(), pPreset->Name.c_str()));
		return Index;
	}
	int Number = -1;
	if (Lower.size() <= 2 && Lower.find_first_not_of("0123456789abcdef") == std::string::npos)
		Number = static_cast<int>(std::strtol(Lower.c_str(), 0, 16));	// as FamiTracker shows it: "0A"
	if (Number >= 0) {
		const InstrumentInfo *pInfo = FindInstrument(S.Song, Number);
		if (pInfo != 0) {
			if (pInfo->Chip != Chip)
				throw Problem(Format("Instrument %02X \"%s\" is a %s instrument; %s needs a %s one. A preset name (like \"soft lead\") adds one.",
					Number, pInfo->Name.c_str(), pInfo->Chip.c_str(), Info.FullName().c_str(), Chip.c_str()));
			return Number;
		}
	}
	if (pWrongChip != 0)
		throw Problem(Format("\"%s\" is a %s instrument; %s needs a %s one. A preset name (like \"soft lead\") adds one.",
			pWrongChip->Name.c_str(), pWrongChip->Chip.c_str(), Info.FullName().c_str(), Chip.c_str()));
	std::string Names;
	for (size_t i = 0; i < S.Song.Instruments.size(); ++i) {
		if (S.Song.Instruments[i].Chip != Chip)
			continue;
		if (!Names.empty())
			Names += ", ";
		Names += "\"" + S.Song.Instruments[i].Name + "\"";
	}
	if (Chip == "VRC7")
		throw Problem(Format("No instrument or VRC7 sound called \"%s\". VRC7 instruments in the song: %s. VRC7 sounds: %s (or a preset's "
			"name, like \"soft lead\"); custom ones: %s.", Wanted.c_str(), Names.empty() ? "none" : Names.c_str(), Vrc7PatchList().c_str(),
			Vrc7CustomList().c_str()));
	if (Chip == "N163")
		throw Problem(Format("No instrument or N163 sound called \"%s\". N163 instruments in the song: %s. N163 sounds: %s.",
			Wanted.c_str(), Names.empty() ? "none" : Names.c_str(), WaveSoundList().c_str()));
	throw Problem(Format("No instrument or preset called \"%s\". %s instruments in the song: %s. Presets: %s.", Wanted.c_str(),
		Chip.c_str(), Names.empty() ? "none" : Names.c_str(), PresetList().c_str()));
}

int InstrumentArg(Session &S, const Json &Args, const char *Name, int Channel, int Default)
{
	const Json *pValue = Arg(Args, Name);
	if (pValue == 0)
		return Default;
	if (pValue->IsNumber())
		return ResolveInstrument(S, Format("%02X", static_cast<int>(pValue->Number())), Channel);
	return ResolveInstrument(S, StringArg(Args, Name), Channel);
}

// The instrument a channel plays with at Row, if it fits the channel
int InstrumentAt(Session &S, int Channel, int Row)
{
	const ChannelState State = StateAt(S.Cells(Channel), Row, S.Channel(Channel).EffectColumns);
	int Index = State.Instrument;
	if (Index == NO_INSTRUMENT) {
		// Or the first one of the channel's notes after it
		const std::vector<NoteInfo> &Notes = S.Notes(Channel);
		for (size_t i = 0; i < Notes.size() && Index == NO_INSTRUMENT; ++i)
			if (Notes[i].Row >= Row)
				Index = Notes[i].Instrument;
	}
	const InstrumentInfo *pInfo = FindInstrument(S.Song, Index);
	return (pInfo != 0 && pInfo->Chip == InstrumentChip(S.Channel(Channel))) ? Index : NO_INSTRUMENT;
}

// Keys and melodies for the harmony code

std::vector<transcribe::MelodyNote> MelodyOf(const std::vector<NoteInfo> &Notes, int First, int End)
{
	std::vector<transcribe::MelodyNote> Melody;
	for (size_t i = 0; i < Notes.size(); ++i) {
		const NoteInfo &N = Notes[i];
		if (N.Row < First || N.Row >= End)
			continue;
		transcribe::MelodyNote M;
		M.row = N.Row;
		M.length = std::max(1, N.Rows);
		M.midi = N.Pitch + 24;	// C-0 is MIDI note 24
		for (size_t e = 0; e < N.Effects.size(); ++e) {
			if (N.Effects[e].first != 0)
				continue;
			if (N.Effects[e].second.first == EFF_SLIDE_UP)
				M.midi += N.Effects[e].second.second & 0x0F;	// a slide counts where it lands
			else if (N.Effects[e].second.first == EFF_SLIDE_DOWN)
				M.midi -= N.Effects[e].second.second & 0x0F;
		}
		M.weight = std::max(0.3, N.Volume / 15.0);
		Melody.push_back(M);
	}
	return Melody;
}

transcribe::Key HarmonyKey(const Key &K)
{
	transcribe::Key Out;
	Out.tonic = K.Tonic;
	Out.minor = K.Mode == MODE_MINOR || K.Mode == 1 || K.Mode == 2 || K.Mode == 6 || K.Mode == 7;	// dorian, phrygian, locrian, harmonic minor
	return Out;
}

// The key from the notes of every melodic channel in [First, End), or the
// whole song when that's too few. False when there are no notes.
bool FindKey(Session &S, int First, int End, Key &Out)
{
	for (int Pass = 0; Pass < 2; ++Pass) {
		std::vector<transcribe::MelodyNote> All;
		for (int c = 0; c < S.ChannelCount(); ++c) {
			if (!IsMelodic(S.Channel(c)))
				continue;
			const int From = Pass == 0 ? First : 0, To = Pass == 0 ? End : S.Time.TotalRows();
			const std::vector<transcribe::MelodyNote> Part = MelodyOf(S.Notes(c), From, To);
			All.insert(All.end(), Part.begin(), Part.end());
			// The chords arpeggios sound (0xy, or the instrument's own) count too, a little less
			const std::vector<NoteInfo> &Notes = S.Notes(c);
			int Arpeggio = 0;
			for (size_t i = 0; i < Notes.size(); ++i) {
				const NoteInfo &N = Notes[i];
				for (size_t e = 0; e < N.Effects.size(); ++e)
					if (N.Effects[e].first == 0 && N.Effects[e].second.first == EFF_ARPEGGIO)
						Arpeggio = N.Effects[e].second.second;
				if (N.Row < From || N.Row >= To)
					continue;
				const std::vector<int> Above = ChordAbove(S, N, Arpeggio);
				for (size_t a = 0; a < Above.size(); ++a) {
					transcribe::MelodyNote M;
					M.row = N.Row;
					M.length = std::max(1, N.Rows);
					M.midi = N.Pitch + Above[a] + 24;
					M.weight = 0.7 * std::max(0.3, N.Volume / 15.0);
					All.push_back(M);
				}
			}
		}
		if (All.size() >= 4 || (Pass == 1 && !All.empty())) {
			std::sort(All.begin(), All.end(), [](const transcribe::MelodyNote &a, const transcribe::MelodyNote &b) { return a.row < b.row; });
			const transcribe::Key K = transcribe::FindKey(All);
			Out = Key(K.tonic, K.minor ? MODE_MINOR : MODE_MAJOR);
			// A key the notes keep leaving (the G naturals of a song heard as F# minor) gives way to one they
			// fit, the one whose tonic chord sounds most: progressions that circle the tonic without
			// landing on it (IVmaj7 V7 iii7 vi7) fool the profile of the notes alone
			double Weight[12] = { 0 }, Total = 0.0;
			for (size_t i = 0; i < All.size(); ++i) {
				const double w = All[i].length * All[i].weight;
				Weight[(All[i].midi % 12 + 12) % 12] += w;
				Total += w;
			}
			auto Outside = [&](const Key &Candidate) {
				double Off = 0.0;
				const int *pSteps = ModeSteps(Candidate.Mode);
				for (int pc = 0; pc < 12; ++pc) {
					bool In = false;	// the scale itself (a minor key's raised seventh would favour it over its relative major)
					for (int s = 0; s < 7; ++s)
						In = In || (Candidate.Tonic + pSteps[s]) % 12 == pc;
					if (!In)
						Off += Weight[pc];
				}
				return Off / std::max(Total, 1e-9);
			};
			const double Own = Outside(Out);
			double Least = Own;
			for (int t = 0; t < 12; ++t)
				for (int m = 0; m < 2; ++m)
					Least = std::min(Least, Outside(Key(t, m ? MODE_MINOR : MODE_MAJOR)));
			if (Own > Least + 0.04 && Own > 1.5 * Least) {
				double BestTriad = -1.0;
				for (int t = 0; t < 12; ++t)
					for (int m = 0; m < 2; ++m) {
						const Key Candidate(t, m ? MODE_MINOR : MODE_MAJOR);
						if (Outside(Candidate) > Least + 1e-9)
							continue;
						const double Triad = 1.5 * Weight[t] + Weight[(t + (m ? 3 : 4)) % 12] + Weight[(t + 7) % 12];
						if (Triad > BestTriad) {
							BestTriad = Triad;
							Out = Candidate;
						}
					}
			}
			return true;
		}
	}
	return false;
}

Key KeyArg(Session &S, const Json &Args, const char *Name, int First, int End, bool &Found)
{
	const std::string Text = StringArg(Args, Name);
	Key K;
	Found = true;
	if (!Text.empty()) {
		if (!ParseKey(Text, K))
			throw Problem(Format("%s: \"%s\" isn't a key. Write it like \"C major\", \"A minor\", \"F# dorian\" or \"Bbm\".", Name, Text.c_str()));
		return K;
	}
	Found = FindKey(S, First, End, K);
	return K;
}

// "| C | F C | G | C |", up to 32 bars
std::string ChordsText(const std::vector<transcribe::ChordSpan> &Chords, const std::vector<transcribe::Bar> &Bars,
	const transcribe::Key &K)
{
	std::string Text;
	for (size_t b = 0; b < Bars.size(); ++b) {
		if (b == 32) {
			Text += "| ...";
			return Text;
		}
		Text += "|";
		const int BarEnd = Bars[b].row + Bars[b].length;
		for (size_t c = 0; c < Chords.size(); ++c) {
			const transcribe::ChordSpan &Span = Chords[c];
			const int From = std::max(Span.row, Bars[b].row);
			if (From < std::min(Span.row + Span.length, BarEnd) && (From == Bars[b].row || From == Span.row)) {
				Text += " ";
				Text += transcribe::ChordName(Span.chord, K);
			}
		}
		Text += " ";
	}
	Text += "|";
	return Text;
}

// A channel's notes that start in [First, End)
std::vector<NoteInfo> NotesIn(const std::vector<NoteInfo> &Notes, int First, int End)
{
	std::vector<NoteInfo> Out;
	for (size_t i = 0; i < Notes.size(); ++i)
		if (Notes[i].Row >= First && Notes[i].Row < End)
			Out.push_back(Notes[i]);
	return Out;
}

// The pitch half of the notes' time is spent below, or Default
int MiddleOf(const std::vector<NoteInfo> &Notes, int Default)
{
	std::vector<std::pair<int, int> > Pitches;
	int Total = 0;
	for (size_t i = 0; i < Notes.size(); ++i) {
		Pitches.push_back(std::make_pair(Notes[i].Pitch, std::max(1, Notes[i].Rows)));
		Total += std::max(1, Notes[i].Rows);
	}
	if (Pitches.empty())
		return Default;
	std::sort(Pitches.begin(), Pitches.end());
	int Sum = 0;
	for (size_t i = 0; i < Pitches.size(); ++i) {
		Sum += Pitches[i].second;
		if (Sum * 2 >= Total)
			return Pitches[i].first;
	}
	return Pitches.back().first;
}

// The channel most likely to play the melody in [First, End): of the
// melodic channels that aren't bass channels or mostly 0xy chords (any of
// them when only those play), the one with the most notes, notes higher up
// counting for more: a melody is usually the top line, and an accompaniment
// of many quick notes sits under it
// Whether a channel mostly repeats another's notes a little later (an echo of it)
bool EchoesAnother(Session &S, int Channel, const std::vector<NoteInfo> &Notes, int First, int End)
{
	if (Notes.size() < 4)
		return false;
	for (int o = 0; o < S.ChannelCount(); ++o) {
		if (o == Channel || !IsMelodic(S.Channel(o)))
			continue;
		const std::vector<NoteInfo> Theirs = NotesIn(S.Notes(o), First, End);
		if (Theirs.size() < 4)
			continue;
		std::map<int, int> PitchAt;
		for (size_t i = 0; i < Theirs.size(); ++i)
			PitchAt[Theirs[i].Row] = Theirs[i].Pitch;
		for (int Delay = 1; Delay <= std::max(1, S.Time.RowsPerBar); ++Delay) {
			size_t Same = 0;
			for (size_t i = 0; i < Notes.size(); ++i) {
				std::map<int, int>::const_iterator it = PitchAt.find(Notes[i].Row - Delay);
				if (it != PitchAt.end() && (it->second - Notes[i].Pitch) % 12 == 0)
					++Same;
			}
			if (Same * 10 >= Notes.size() * 7)
				return true;
		}
	}
	return false;
}

int GuessMelodyChannel(Session &S, int First, int End)
{
	for (int Pass = 0; Pass < 3; ++Pass) {
		std::vector<int> Channels, Counts, Middles, Loudness;
		int Lowest = 1000;
		for (int c = 0; c < S.ChannelCount(); ++c) {
			const ChannelInfo &Info = S.Channel(c);
			if (!IsMelodic(Info) || (Pass < 2 && IsBass(Info)))
				continue;
			const std::vector<NoteInfo> Notes = NotesIn(S.Notes(c), First, End);
			if (Notes.empty())
				continue;
			int Chords = 0;
			for (size_t i = 0; i < Notes.size(); ++i)
				if (SoundsChord(S, Notes[i]))
					++Chords;
			if (Pass == 0 && Chords * 3 >= static_cast<int>(Notes.size()))
				continue;
			// An echo of another part isn't the melody
			if (Pass < 2 && EchoesAnother(S, c, Notes, First, End))
				continue;
			const int Middle = MiddleOf(Notes, 48) - (Info.Kind == KIND_TRIANGLE ? 12 : 0);
			// How loud it usually plays (the triangle has no volume)
			std::vector<int> Volumes;
			for (size_t i = 0; i < Notes.size(); ++i)
				Volumes.push_back(Info.Kind == KIND_TRIANGLE ? 15 : Notes[i].Volume);
			std::sort(Volumes.begin(), Volumes.end());
			Channels.push_back(c);
			Counts.push_back(static_cast<int>(Notes.size()));
			Middles.push_back(Middle);
			Loudness.push_back(std::max(1, Volumes[Volumes.size() / 2]));
			Lowest = std::min(Lowest, Middle);
		}
		int Best = -1;
		double BestScore = 0.0;
		for (size_t i = 0; i < Channels.size(); ++i) {
			// Busy and high, and loud: a quiet part is seldom the melody
			const double Score = Counts[i] * std::pow(2.0, (Middles[i] - Lowest) / 8.0) * std::pow(Loudness[i] / 15.0, 2.0);
			if (Score > BestScore) {
				Best = Channels[i];
				BestScore = Score;
			}
		}
		if (Best >= 0)
			return Best;
	}
	return -1;
}

bool NoteStartsIn(Session &S, int Channel, int First, int End)
{
	const std::vector<NoteInfo> &Notes = S.Notes(Channel);
	for (size_t i = 0; i < Notes.size(); ++i)
		if (Notes[i].Row >= First && Notes[i].Row < End)
			return true;
	return false;
}

bool HasNotesIn(Session &S, int Channel, int First, int End)
{
	const std::vector<NoteInfo> &Notes = S.Notes(Channel);
	for (size_t i = 0; i < Notes.size(); ++i)
		if (Notes[i].Row < End && Notes[i].Row + Notes[i].Rows > First)
			return true;
	return false;
}

// Changes go into the song, or become a clip

struct Change {
	int Channel;
	std::vector<Cell> Cells;	// from the first row on
};

bool Direct(const Session &S, bool AsClip)
{
	return S.Song.DirectChanges && !AsClip;
}

PartOptions OptionsFor(const Session &S, int Channel, bool DirectChange)
{
	PartOptions Options;
	Options.EffectColumns = S.Channel(Channel).EffectColumns;
	Options.Releases = ReleasingInstruments(S.Song);
	Options.KeepFollowing = DirectChange;
	Options.StopAtEnd = !DirectChange;
	return Options;
}

std::string Commit(Session &S, std::vector<Change> &Changes, int First, bool AsClip, const std::string &ClipName,
	const std::string &What)
{
	if (Changes.empty())
		throw Problem("There was nothing to change.");
	std::string Result;
	if (Direct(S, AsClip)) {
		std::string Why;
		if (!S.Host().CanChange(Why))
			throw Problem(Why);
		std::vector<ChannelCells> Parts;
		for (size_t i = 0; i < Changes.size(); ++i) {
			ChannelCells Part;
			Part.Channel = Changes[i].Channel;
			Part.Cells = Changes[i].Cells;
			Part.Exact = true;	// worked out from the song's cells, its speed, jumps and halts among them
			Parts.push_back(Part);
		}
		SongWrite Write;
		Write.First = First;
		// Effects past the columns a channel shows: FamiTracker shows more there
		std::string Wider;
		for (size_t i = 0; i < Parts.size(); ++i) {
			int Needed = 0;
			for (size_t r = 0; r < Parts[i].Cells.size(); ++r)
				for (int k = EFFECT_COLUMNS - 1; k >= Needed; --k)
					if (Parts[i].Cells[r].Effect[k] != EFF_NONE) {
						Needed = k + 1;
						break;
					}
			if (Needed > S.Shown(Parts[i].Channel))
				Wider += Format("%s%s (%d)", Wider.empty() ? "" : ", ", S.Channel(Parts[i].Channel).FullName().c_str(), Needed);
		}
		std::string Error;
		if (!S.Host().WriteSong(Parts, Write, Error))
			throw Problem(Error.empty() ? "The song couldn't be changed." : Error);
		Result = "Changed the song: " + What + ". One Ctrl+Z in FamiTracker undoes it.";
		if (!Wider.empty())
			Result += " More effect columns show for the effects on " + Wider + ".";
	}
	else {
		// One clip over the channels from the first changed one to the last;
		// channels in between come along as they are
		int Low = Changes[0].Channel, High = Changes[0].Channel;
		size_t Rows = 0;
		for (size_t i = 0; i < Changes.size(); ++i) {
			Low = std::min(Low, Changes[i].Channel);
			High = std::max(High, Changes[i].Channel);
			Rows = std::max(Rows, Changes[i].Cells.size());
		}
		std::vector<std::vector<Cell> > Columns;
		std::vector<int> Sources;
		for (int c = Low; c <= High; ++c) {
			std::vector<Cell> Column;
			for (size_t i = 0; i < Changes.size(); ++i)
				if (Changes[i].Channel == c)
					Column = Changes[i].Cells;
			const std::vector<Cell> &Song = S.Cells(c);
			while (Column.size() < Rows) {
				const size_t Row = First + Column.size();
				Column.push_back(Row < Song.size() ? Song[Row] : Cell());
			}
			Columns.push_back(Column);
			Sources.push_back(S.Channel(c).ChanId);
		}
		std::string Name = ClipName.empty() ? "Claude: " + What : ClipName;
		if (Name.size() > 80)
			Name = Name.substr(0, 77) + "...";
		std::string Error;
		if (!S.Host().AddClip(Name, Sources, Columns, Error))
			throw Problem(Error.empty() ? "The clip couldn't be saved." : Error);
		Result = Format("Saved as the clip \"%s\" (%s). It shows in the clips panel beside the song: the play button on its "
			"card plays it, and dragging the card to %s at %s puts it there; Ctrl+Z takes it out again.", Name.c_str(), What.c_str(),
			S.Channel(Low).FullName().c_str(), PositionText(S, First).c_str());
		if (!S.Song.DirectChanges)
			Result += " (Changes arrive as clips: the user hasn't allowed direct changes in File > Connect Claude Code.)";
	}
	return Result;
}

std::string WithRemarks(const Session &S, const std::string &Text, const std::vector<std::string> &Warnings = std::vector<std::string>())
{
	std::string Out = Text;
	for (size_t i = 0; i < S.Remarks.size(); ++i)
		Out += "\n" + S.Remarks[i];
	for (size_t i = 0; i < Warnings.size(); ++i)
		Out += "\nNote: " + Warnings[i];
	return Out;
}

std::string RangeText(int From, int To)
{
	return From == To ? Format("bar %d", From) : Format("bars %d-%d", From, To);
}

// The song

std::vector<BarSpan> SongBarSpans(const Session &S, int First, int End);
std::vector<ChordAt> SongChords(Session &S, int First, int End, const Key &K);

std::string SongOverview(Session &S, const Json &Args)
{
	const SongInfo &Song = S.Song;
	const Timing &T = S.Time;
	std::string Text;
	Text += Format("\"%s\"", Song.Title.empty() ? "(untitled)" : Song.Title.c_str());
	if (!Song.Author.empty())
		Text += " by " + Song.Author;
	Text += Song.FileName.empty() ? ", not saved yet" : ", file " + Song.FileName;
	if (Song.Modified)
		Text += " (unsaved changes)";
	if (Song.Tracks > 1)
		Text += Format("; track %d of %d \"%s\"", Song.Track + 1, Song.Tracks, Song.TrackName.c_str());
	Text += "\n";
	if (Song.TrackNames.size() > 1) {
		Text += "Tracks:";
		for (size_t t = 0; t < Song.TrackNames.size(); ++t)
			Text += Format(" %d \"%s\"%s%s", static_cast<int>(t) + 1, Song.TrackNames[t].c_str(),
				static_cast<int>(t) == Song.Track ? " (selected)" : "", t + 1 < Song.TrackNames.size() ? "," : "");
		Text += "\n";
	}
	Text += Format("Tempo: %s BPM (speed %d, tempo %d, %s %d Hz); %d rows a beat, %d beats a bar (%d rows)\n",
		FormatNumber(std::floor(BeatsPerMinute(Song.Speed, Song.Tempo, Song.FrameRate, T.RowsPerBeat) * 10 + 0.5) / 10).c_str(),
		Song.Speed, Song.Tempo, Song.FrameRate == 50 ? "PAL" : (Song.FrameRate == 60 ? "NTSC" : "engine"), Song.FrameRate,
		T.RowsPerBeat, T.RowsPerBar / T.RowsPerBeat, T.RowsPerBar);
	Text += Format("Length: %s (%s of %d rows)\n", Plural(T.BarCount(), "bar", "bars").c_str(),
		Plural(T.Frames, "frame", "frames").c_str(), T.PatternLength);

	Text += Format("Channels%s:\n", Song.Expansion.empty() ? "" : (" (expansion: " + Song.Expansion + ")").c_str());
	for (int c = 0; c < S.ChannelCount(); ++c) {
		const ChannelInfo &Channel = S.Channel(c);
		const std::vector<NoteInfo> &Notes = S.Notes(c);
		Text += Format("  %d %s%s: ", c + 1, Channel.FullName().c_str(), Channel.Muted ? " (muted, the user doesn't hear it)" : "");
		if (Notes.empty()) {
			Text += "empty\n";
			continue;
		}
		int Low = 999, High = -1;
		std::set<int> Instruments;
		for (size_t i = 0; i < Notes.size(); ++i) {
			Low = std::min(Low, Notes[i].Pitch);
			High = std::max(High, Notes[i].Pitch);
			if (Notes[i].Instrument != NO_INSTRUMENT)
				Instruments.insert(Notes[i].Instrument);
		}
		Text += Format("%s in %s", Plural(static_cast<int>(Notes.size()), Channel.Kind == KIND_NOISE ? "hit" : "note",
			Channel.Kind == KIND_NOISE ? "hits" : "notes").c_str(),
			RangeText(T.BarOf(Notes.front().Row), T.BarOf(Notes.back().Row)).c_str());
		if (Channel.Kind != KIND_DPCM)
			Text += Format(", %s to %s", PitchText(Channel, Low).c_str(), PitchText(Channel, High).c_str());
		if (!Instruments.empty()) {
			Text += ", instruments";
			for (std::set<int>::const_iterator it = Instruments.begin(); it != Instruments.end(); ++it)
				Text += " " + InstrumentLabel(Song, *it);
		}
		Text += "\n";
	}

	// Key and chords
	Key K;
	if (FindKey(S, 0, T.TotalRows(), K)) {
		Text += "Key: " + KeyName(K) + " (found from the notes)";
		int Melody = -1;
		if (Arg(Args, "melody_channel") != 0)
			Melody = ChannelArg(S, Args, "melody_channel");
		else
			Melody = GuessMelodyChannel(S, 0, T.TotalRows());
		if (Melody >= 0) {
			const std::vector<transcribe::MelodyNote> Line = MelodyOf(S.Notes(Melody), 0, T.TotalRows());
			const std::vector<transcribe::Bar> Bars = transcribe::MelodyBars(Line, T.PatternLength, T.RowsPerBar);
			transcribe::HarmonyOptions Options;
			Options.rowsPerBeat = T.RowsPerBeat;
			const transcribe::Key HK = HarmonyKey(K);
			const std::vector<transcribe::ChordSpan> Chords = transcribe::ChooseChords(Line, Bars, HK, Options);
			if (!Bars.empty() && !Chords.empty())
				Text += Format("; chords that fit the melody on %s, from bar %d: %s", S.Channel(Melody).FullName().c_str(),
					T.BarOf(Bars.front().row), ChordsText(Chords, Bars, HK).c_str());
		}
		Text += "\n";
	}
	else
		Text += "Key: none yet (no notes)\n";
	{
		Key Played;
		if (FindKey(S, 0, T.TotalRows(), Played)) {
			const int Melody = GuessMelodyChannel(S, 0, T.TotalRows());
			bool Backing = false;
			for (int c = 0; c < S.ChannelCount() && !Backing; ++c)
				Backing = c != Melody && IsMelodic(S.Channel(c)) && !S.Notes(c).empty();
			if (Backing) {
				const std::vector<ChordAt> Heard = SongChords(S, 0, T.TotalRows(), Played);
				if (!Heard.empty())
					Text += "Chords the parts play: " + ProgressionText(Heard, SongBarSpans(S, 0, T.TotalRows()), Played) + "\n";
			}
		}
	}
	{
		std::string Notes, Error;
		if (S.Host().ReadFile(SOUNDTRACK_NOTES, Notes, Error) && !Trim(Notes).empty())
			Text += std::string("Soundtrack notes: \"") + SOUNDTRACK_NOTES + "\" next to the song holds the user's taste and the decisions "
				"about this game's music; read it with soundtrack_notes before writing.\n";
	}

	Text += "Instruments:";
	if (Song.Instruments.empty())
		Text += " none";
	for (size_t i = 0; i < Song.Instruments.size(); ++i)
		Text += Format(" %02X \"%s\" (%s)%s", Song.Instruments[i].Index, Song.Instruments[i].Name.c_str(),
			Song.Instruments[i].Chip.c_str(), i + 1 < Song.Instruments.size() ? "," : "");
	Text += "\n";
	if (!Song.Clips.empty()) {
		Text += "Clips:";
		for (size_t i = 0; i < Song.Clips.size(); ++i)
			Text += Format(" %d \"%s\" (%s, %s%s)%s", Song.Clips[i].Index + 1, Song.Clips[i].Name.c_str(),
				Plural(Song.Clips[i].Columns, "channel", "channels").c_str(), T.Length(Song.Clips[i].Rows).c_str(),
				Song.Clips[i].Links > 0 ? Format(", %s in the song", Plural(Song.Clips[i].Links, "linked copy", "linked copies").c_str()).c_str() : "",
				i + 1 < Song.Clips.size() ? "," : "");
		Text += "\n";
	}
	Text += Song.DirectChanges ?
		"Changes: direct. Your edits change the song, each one undoable with Ctrl+Z.\n" :
		"Changes: as clips. Your edits are saved as clips for the user to listen to and place from the clips panel "
		"beside the song; they can allow direct changes in File > Connect Claude Code.\n";
	if (Song.Playing && Song.PlayRow >= 0)
		Text += "Playing, now at " + T.Position(Song.PlayRow) + ".\n";
	Text += "Instrument presets: " + PresetList() + ".";
	if (Song.Expansion == "N163")
		Text += " N163 sounds (for the Namco channels): " + WaveSoundList() + ".";
	return Text;
}

// "Q31", "431 at beat 3.5"
std::string EffectsText(const Session &S, const NoteInfo &Note)
{
	std::string Text;
	for (size_t e = 0; e < Note.Effects.size(); ++e) {
		if (!Text.empty())
			Text += ", ";
		Text += EffectText(Note.Effects[e].second.first, Note.Effects[e].second.second);
		if (Note.Effects[e].first > 0)
			Text += " at beat " + FormatNumber(S.Time.BeatOf(Note.Row + Note.Effects[e].first));
	}
	return Text;
}

std::string ReadChannel(Session &S, const Json &Args)
{
	const int Channel = ChannelArg(S, Args, "channel");
	int From, To;
	BarRange(S, Args, From, To);
	std::string More;
	if (To - From + 1 > MAX_READ_BARS) {
		To = From + MAX_READ_BARS - 1;
		More = Format("\n(Up to %d bars at a time: the rest from from_bar %d on.)", MAX_READ_BARS, To + 1);
	}
	const ChannelInfo &Info = S.Channel(Channel);
	const int First = S.Time.BarStart(From), End = S.Time.BarEnd(To);
	const std::vector<NoteInfo> &Notes = S.Notes(Channel);
	std::string Text = Format("%s, %s (%d beats a bar, %d rows a beat):", Info.FullName().c_str(), RangeText(From, To).c_str(),
		S.Time.RowsPerBar / S.Time.RowsPerBeat, S.Time.RowsPerBeat);
	int Instrument = -1, Volume = -1;
	size_t Shown = 0;
	for (int Bar = From; Bar <= To; ++Bar) {
		const int BarFirst = S.Time.BarStart(Bar), BarEnd = S.Time.BarEnd(Bar);
		bool Any = false;
		for (size_t i = 0; i < Notes.size(); ++i) {
			const NoteInfo &N = Notes[i];
			const bool Held = Bar == From && N.Row < First && N.Row + N.Rows > First;
			if (!Held && (N.Row < BarFirst || N.Row >= BarEnd))
				continue;
			Any = true;
			++Shown;
			std::string Line;
			if (Held)
				Line = Format("\n(held from %s: %s, %s more)", S.Time.Position(N.Row).c_str(), PitchText(Info, N.Pitch).c_str(),
					S.Time.Length(N.Row + N.Rows - First).c_str());
			else {
				Line = Format("\nbar %d beat %s: %s, %s", Bar, FormatNumber(S.Time.BeatOf(N.Row)).c_str(),
					PitchText(Info, N.Pitch).c_str(), S.Time.Length(N.Rows).c_str());
				if (N.Instrument != Instrument && N.Instrument != NO_INSTRUMENT)
					Line += ", " + InstrumentLabel(S.Song, N.Instrument);
				if (N.VolumeSet || (Volume < 0 && N.Volume != 15))
					Line += Format(", volume %d", N.Volume);
				const std::string Effects = EffectsText(S, N);
				if (!Effects.empty())
					Line += ", effects " + Effects;
				if (N.EndNote == NOTE_RELEASE)
					Line += ", released";
			}
			Instrument = N.Instrument;
			Volume = N.Volume;
			Text += Line;
		}
		if (!Any)
			Text += Format("\nbar %d: rest", Bar);
	}
	if (Shown == 0)
		Text += "\n(No notes in these bars.)";
	if (End > S.Time.TotalRows())
		Text += "\n(The song ends before that.)";
	return Text + More;
}

// Writing a part

std::string WriteNotes(Session &S, const Json &Args)
{
	const int Channel = ChannelArg(S, Args, "channel");
	const ChannelInfo &Info = S.Channel(Channel);
	const bool Dpcm = Info.Kind == KIND_DPCM;
	const Json *pNotes = Arg(Args, "notes");
	if (pNotes == 0 || !pNotes->IsArray())
		throw Problem("notes should be a list of notes like {\"bar\": 1, \"beat\": 1, \"pitch\": \"C-4\", \"beats\": 1}.");
	const bool AsClip = BoolArg(Args, "as_clip", false);
	const bool DirectChange = Direct(S, AsClip);
	const bool Noise = Info.Kind == KIND_NOISE;
	const int MaxRows = S.MaxRows();

	// The default instrument: as asked, or what the channel plays with
	int DefaultInstrument = NO_INSTRUMENT;
	if (Arg(Args, "instrument") != 0)
		DefaultInstrument = InstrumentArg(S, Args, "instrument", Channel, NO_INSTRUMENT);

	std::vector<PartNote> Notes;
	std::vector<std::string> Warnings;
	bool Rounded = false;
	int FirstBar = S.MaxBars() + 1, LastRow = 0;
	for (size_t i = 0; i < pNotes->Size(); ++i) {
		const Json &Item = pNotes->At(i);
		if (!Item.IsObject())
			throw Problem(Format("Note %d should be an object like {\"bar\": 1, \"beat\": 1, \"pitch\": \"C-4\", \"beats\": 1}.", int(i + 1)));
		const std::string Which = Format("Note %d", int(i + 1));
		const int Bar = IntArg(Item, "bar", 0, 0, S.MaxBars());
		if (Bar < 1)
			throw Problem(Which + " needs a bar (from 1).");
		const double Beat = NumberArg(Item, "beat", 1.0, 1.0, 64.0);
		int Row;
		bool Exact;
		if (!S.Time.PositionToRow(Bar, Beat, Row, Exact))
			throw Problem(Format("%s: beat %s is past the end of bar %d (%d beats).", Which.c_str(), FormatNumber(Beat).c_str(), Bar,
				S.Time.BeatsPerBar(Bar)));
		if (!Exact)
			Rounded = true;
		if (Arg(Item, "beats") == 0)
			throw Problem(Which + " needs its length in beats (\"beats\": 1).");
		const int Rows = S.Time.BeatsToRows(NumberArg(Item, "beats", 1.0, 0.001, 1024.0), Exact);
		if (!Exact)
			Rounded = true;
		if (Row + Rows > MaxRows)
			throw Problem(Format("%s runs past the longest a song can be (%d bars).", Which.c_str(), S.MaxBars()));

		PartNote Note;
		Note.Row = Row;
		Note.Rows = Rows;
		const std::string Pitch = StringArg(Item, "pitch");
		if (Pitch.empty())
			throw Problem(Which + " needs a pitch, like \"C-4\" or \"F#3\"" + (Noise ? " (on the noise channel: kick, snare, hihat or 0-# to F-#)." :
				(Dpcm ? " (on the DPCM channel: kick, snare, low tom or high tom, from add_sample_drums)." : ".")));
		std::string Drum;
		if (Dpcm) {
			const DpcmKey *pKey = FindSampleDrum(Pitch);
			if (pKey != 0) {
				Note.Pitch = pKey->Pitch;
				Drum = "sample drums";
			}
			else if (!ParsePitch(Pitch, Note.Pitch))
				throw Problem(Format("%s: \"%s\" isn't a drum or a key. On the DPCM channel: kick, snare, low tom or high tom (the "
					"sampled drums of add_sample_drums), or the key a sample is on, like C-3.", Which.c_str(), Pitch.c_str()));
		}
		else if (Noise) {
			if (!ParseNoisePitch(Pitch, Note.Pitch, Drum))
				throw Problem(Format("%s: \"%s\" isn't a noise sound. Use kick, snare, hihat, open hihat or a noise pitch 0-# to F-#.",
					Which.c_str(), Pitch.c_str()));
		}
		else if (!ParsePitch(Pitch, Note.Pitch))
			throw Problem(Format("%s: \"%s\" isn't a note. Write it like C-4, C#4, Db4 (FamiTracker octaves: C-3 is middle C, "
				"from C-0 to B-7).", Which.c_str(), Pitch.c_str()));
		if (Arg(Item, "instrument") != 0)
			Note.Instrument = InstrumentArg(S, Item, "instrument", Channel, NO_INSTRUMENT);
		else if (DefaultInstrument != NO_INSTRUMENT)
			Note.Instrument = DefaultInstrument;
		else if (Drum == "sample drums")
			Note.Instrument = SampleDrumsInstrument(S);
		else if (!Drum.empty())
			Note.Instrument = ResolveInstrument(S, Drum, Channel);
		if (Arg(Item, "volume") != 0)
			Note.Volume = IntArg(Item, "volume", 15, 0, 15);
		const Json *pEffects = Arg(Item, "effects");
		if (pEffects != 0) {
			if (!pEffects->IsArray())
				throw Problem(Which + ": effects should be a list like [\"Q31\"].");
			for (size_t e = 0; e < pEffects->Size(); ++e) {
				int Effect = EFF_NONE, Param = 0;
				std::string Why;
				if (!pEffects->At(e).IsString())
					throw Problem(Which + ": effects are written as in FamiTracker, like \"Q31\" or \"047\".");
				// Only what FamiTracker 0.4.6 has, and what this channel plays
				if (!ParseChannelEffect(pEffects->At(e).String(), Info, S.Song.Speed, Effect, Param, Why))
					throw Problem(Which + ": " + Why + " (the effects tool explains them all.)");
				Note.Effects.push_back(std::make_pair(Effect, Param));
			}
		}
		Notes.push_back(Note);
		FirstBar = std::min(FirstBar, Bar);
		LastRow = std::max(LastRow, Row + Rows);
	}
	if (Notes.empty() && Arg(Args, "from_bar") == 0)
		throw Problem("No notes to write. To clear bars, give from_bar and to_bar with an empty list.");

	// In time order, one note at a time
	std::stable_sort(Notes.begin(), Notes.end(), [](const PartNote &a, const PartNote &b) { return a.Row < b.Row; });
	for (size_t i = 0; i + 1 < Notes.size(); ++i) {
		if (Notes[i + 1].Row == Notes[i].Row)
			throw Problem(Format("Two notes start at %s; a channel plays one note at a time.", S.Time.Position(Notes[i].Row).c_str()));
		if (Notes[i].Row + Notes[i].Rows > Notes[i + 1].Row) {
			Notes[i].Rows = Notes[i + 1].Row - Notes[i].Row;
			Warnings.push_back(Format("The note at %s overlapped the next one and was shortened; a channel plays one note at a time.",
				S.Time.Position(Notes[i].Row).c_str()));
		}
	}

	// The bars it covers
	const int From = IntArg(Args, "from_bar", Notes.empty() ? 1 : FirstBar, 1, S.MaxBars());
	const int LastBar = Notes.empty() ? From : S.Time.BarOf(LastRow - 1);
	const int To = IntArg(Args, "to_bar", std::max(From, LastBar), 1, S.MaxBars());
	if (To < From)
		throw Problem("to_bar comes before from_bar.");
	const int First = S.Time.BarStart(From);
	int End = S.Time.BarEnd(To);
	if (!Notes.empty() && Notes.front().Row < First)
		throw Problem(Format("A note starts before from_bar %d.", From));
	if (LastRow > End)
		End = LastRow;
	// A sample plays to its end: hits ring until the next one, rather than being cut
	if (Dpcm)
		for (size_t i = 0; i < Notes.size(); ++i)
			Notes[i].Rows = (i + 1 < Notes.size() ? Notes[i + 1].Row : End) - Notes[i].Row;

	// The instrument when none was named: what the channel played with
	const int Playing = InstrumentAt(S, Channel, First);
	bool NeedInstrument = false;
	for (size_t i = 0; i < Notes.size(); ++i) {
		if (Notes[i].Instrument == NO_INSTRUMENT) {
			if (Playing == NO_INSTRUMENT)
				NeedInstrument = true;
			else if (i == 0)
				Notes[i].Instrument = Playing;
		}
	}
	if (NeedInstrument)
		throw Problem(Format("%s has no instrument to play these notes with yet: give an instrument (one of the song's, or a preset: %s).",
			Info.FullName().c_str(), PresetList().c_str()));
	if (!Notes.empty() && Notes.front().Volume < 0)
		Notes.front().Volume = 15;	// the part starts at full volume unless it says otherwise

	std::vector<Cell> Out;
	BuildPart(S.Cells(Channel), First, End, Notes, OptionsFor(S, Channel, DirectChange), Out, Warnings);
	if (Rounded)
		Warnings.push_back(Format("Some beats fell between rows and were rounded to the nearest row (%d rows a beat).", S.Time.RowsPerBeat));
	std::vector<Change> Changes(1);
	Changes[0].Channel = Channel;
	Changes[0].Cells = Out;
	const std::string What = Format("%s on %s, %s", Plural(static_cast<int>(Notes.size()), "note", "notes").c_str(),
		Info.FullName().c_str(), RangeText(From, S.Time.BarOf(End - 1)).c_str());
	return WithRemarks(S, Commit(S, Changes, First, AsClip, StringArg(Args, "clip_name"), What), Warnings);
}

std::string PlaceClipTool(Session &S, const Json &Args)
{
	if (!S.Song.DirectChanges)
		throw Problem("Placing clips changes the song, and the user hasn't allowed direct changes. Ask them to drag it from "
			"the clips panel into the song, or to allow direct changes in File > Connect Claude Code.");
	const Json *pClip = Arg(Args, "clip");
	if (pClip == 0)
		throw Problem("Say which clip (its name or number).");
	int Index = -1;
	if (pClip->IsNumber())
		Index = static_cast<int>(pClip->Number()) - 1;
	else {
		const std::string Wanted = Lowercase(StringArg(Args, "clip"));
		for (size_t i = 0; i < S.Song.Clips.size() && Index < 0; ++i)
			if (Lowercase(S.Song.Clips[i].Name) == Wanted)
				Index = S.Song.Clips[i].Index;
		if (Index < 0 && !Wanted.empty() && Wanted.find_first_not_of("0123456789") == std::string::npos)
			Index = std::atoi(Wanted.c_str()) - 1;
	}
	const ClipInfo *pInfo = 0;
	for (size_t i = 0; i < S.Song.Clips.size(); ++i)
		if (S.Song.Clips[i].Index == Index)
			pInfo = &S.Song.Clips[i];
	if (pInfo == 0)
		throw Problem("No such clip. get_song lists the song's clips.");
	const int Channel = ChannelArg(S, Args, "channel");
	const int Bar = IntArg(Args, "bar", 0, 1, S.MaxBars());
	const double Beat = NumberArg(Args, "beat", 1.0, 1.0, 64.0);
	int Row;
	bool Exact;
	if (!S.Time.PositionToRow(Bar, Beat, Row, Exact))
		throw Problem(Format("Beat %s is past the end of bar %d.", FormatNumber(Beat).c_str(), Bar));
	const bool Linked = BoolArg(Args, "linked", false);

	std::vector<std::vector<Cell> > Columns;
	if (!S.Host().ReadClip(Index, Columns) || Columns.empty())
		throw Problem("The clip couldn't be read.");
	std::vector<ChannelCells> Parts;
	std::vector<std::string> Warnings;
	for (size_t c = 0; c < Columns.size(); ++c) {
		if (Channel + static_cast<int>(c) >= S.ChannelCount()) {
			Warnings.push_back("Its last channels didn't fit to the right of that channel.");
			break;
		}
		ChannelCells Part;
		Part.Channel = Channel + static_cast<int>(c);
		Part.Cells = Columns[c];
		if (Row + static_cast<int>(Part.Cells.size()) > S.MaxRows())
			Part.Cells.resize(static_cast<size_t>(std::max(0, S.MaxRows() - Row)));
		Parts.push_back(Part);
	}
	if (Parts.empty() || Parts[0].Cells.empty())
		throw Problem("The clip doesn't fit there.");
	std::string Why, Error;
	if (!S.Host().CanChange(Why))
		throw Problem(Why);
	SongWrite Write;
	Write.First = Row;
	Write.LinkClipId = Linked ? pInfo->Id : -1;
	if (!S.Host().WriteSong(Parts, Write, Error))
		throw Problem(Error.empty() ? "The clip couldn't be placed." : Error);
	return WithRemarks(S, Format("Placed the clip \"%s\" %s on %s at %s. One Ctrl+Z undoes it.", pInfo->Name.c_str(),
		Linked ? "linked" : "as a copy", S.Channel(Channel).FullName().c_str(), PositionText(S, Row).c_str()), Warnings);
}

std::string AddInstrumentTool(Session &S, const Json &Args)
{
	const std::string Name = StringArg(Args, "preset");
	std::string Chip = "2A03";
	bool Sawtooth = false;
	int Channel = -1;
	if (Arg(Args, "channel") != 0) {
		Channel = ChannelArg(S, Args, "channel");
		Chip = InstrumentChip(S.Channel(Channel));
		Sawtooth = S.Channel(Channel).Kind == KIND_SAWTOOTH;
	}
	else if (Arg(Args, "chip") != 0) {
		Chip = StringArg(Args, "chip");
		for (size_t i = 0; i < Chip.size(); ++i)
			Chip[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(Chip[i])));
		if (Chip == "MMC5")
			Chip = "2A03";
	}

	// The VRC7 plays its own patches
	if (Chip == "VRC7") {
		if (S.Song.Expansion.find("VRC7") == std::string::npos)
			throw Problem("This song has no VRC7 channels; set_expansion switches it to the VRC7.");
		const Vrc7Patch *pPatch = FindVrc7Patch(Name);
		if (pPatch == 0)
			throw Problem(Format("No VRC7 sound called \"%s\". VRC7 sounds: %s (or a preset's name, like \"soft lead\").", Name.c_str(),
				Vrc7PatchList().c_str()));
		const size_t Before = S.Song.Instruments.size();
		std::string Error;
		const int Index = S.Host().AddVrc7Instrument(pPatch->Number, pPatch->InstrumentName(), Error);
		if (Index < 0)
			throw Problem(Error.empty() ? "The instrument couldn't be added." : Error);
		S.Refresh();
		return Format("%s %s (VRC7 instrument, patch %d: %s). Use it by that name in write_notes or set_instrument.",
			S.Song.Instruments.size() > Before ? "Added" : "The song already has", InstrumentLabel(S.Song, Index).c_str(), pPatch->Number,
			pPatch->Description.c_str());
	}

	// The N163 plays wave sounds drawn for it
	if (Chip == "N163") {
		if (S.Song.Expansion != "N163")
			throw Problem("This song has no N163 channels; set_expansion switches it to the N163.");
		const WaveSound *pSound = FindWaveSound(Name);
		if (pSound == 0)
			throw Problem(Format("No N163 sound called \"%s\". N163 sounds: %s.", Name.c_str(), WaveSoundList().c_str()));
		const size_t Before = S.Song.Instruments.size();
		std::string Where;
		const int Index = AddWaveInstrument(S, *pSound, Where);
		return Format("%s %s (N163 instrument: %s%s). Use it by that name in write_notes or set_instrument.",
			S.Song.Instruments.size() > Before ? "Added" : "The song already has", InstrumentLabel(S.Song, Index).c_str(),
			pSound->Description.c_str(), Where.empty() ? "" : ("; " + Where).c_str());
	}

	const Preset *pPreset = FindPreset(Name);
	if (pPreset == 0)
		throw Problem(Format("No preset called \"%s\". Presets: %s; on VRC7 channels also: %s.", Name.c_str(), PresetList().c_str(),
			Vrc7PatchList().c_str()));
	if (Channel >= 0 && pPreset->Drum && S.Channel(Channel).Kind != KIND_NOISE)
		throw Problem(Format("\"%s\" is a drum for the noise channel.", pPreset->Name.c_str()));
	if (Chip != "2A03" && Chip != "VRC6")
		throw Problem("Presets are made for the 2A03 (also used by MMC5 channels), the VRC6 and the VRC7.");
	if (pPreset->Drum)
		Chip = "2A03";
	if (Chip == "VRC6" && S.Song.Expansion.find("VRC6") == std::string::npos)
		throw Problem("This song has no VRC6 channels.");
	const size_t Before = S.Song.Instruments.size();
	std::string Error;
	const int Index = S.Host().AddInstrument(*pPreset, Chip, Sawtooth, Error);
	if (Index < 0)
		throw Problem(Error.empty() ? "The instrument couldn't be added." : Error);
	S.Refresh();
	return Format("%s %s (%s instrument, %s). Use it by that name in write_notes or set_instrument.",
		S.Song.Instruments.size() > Before ? "Added" : "The song already has", InstrumentLabel(S.Song, Index).c_str(), Chip.c_str(),
		pPreset->Description.c_str());
}

// Changing notes that are there

// A working copy of the channels a change touches, from the song's first row
// on. Steps change it one after the other; at the end it is compared with the
// song, and what differs becomes one clip or one undoable change.
class Draft {
public:
	explicit Draft(Session &S) : m_S(S) {}

	std::vector<Cell> &Cells(int Channel)
	{
		std::map<int, std::vector<Cell> >::iterator it = m_Work.find(Channel);
		if (it == m_Work.end())
			it = m_Work.insert(std::make_pair(Channel, m_S.Cells(Channel))).first;
		return it->second;
	}

	// Cells from Row on; the channel grows past the song's end when needed
	void Put(int Channel, int Row, const std::vector<Cell> &Part)
	{
		std::vector<Cell> &Work = Cells(Channel);
		if (Work.size() < Row + Part.size())
			Work.resize(Row + Part.size());
		std::copy(Part.begin(), Part.end(), Work.begin() + Row);
	}

	std::vector<NoteInfo> Notes(int Channel)
	{
		return ReadNotes(Cells(Channel), m_S.Channel(Channel).EffectColumns);
	}

	// A channel as the draft has it, without taking it into the draft
	const std::vector<Cell> &View(int Channel)
	{
		std::map<int, std::vector<Cell> >::const_iterator it = m_Work.find(Channel);
		return it != m_Work.end() ? it->second : m_S.Cells(Channel);
	}

	// How the song plays with the draft in (jumps, skips and halts), and
	// the rows it then has
	SongFlow Flow(int &TotalRows)
	{
		std::vector<std::vector<Cell> > Channels;
		std::vector<int> Columns;
		size_t Rows = 0;
		for (int c = 0; c < m_S.ChannelCount(); ++c) {
			Channels.push_back(View(c));
			Columns.push_back(m_S.Channel(c).EffectColumns);
			Rows = std::max(Rows, Channels.back().size());
		}
		const int Length = std::max(1, m_S.Time.PatternLength);
		const int Frames = static_cast<int>((Rows + Length - 1) / Length);
		TotalRows = Frames * Length;
		return FollowSong(Channels, Columns, Length, Frames);
	}

	// What changed: the rows from the first changed one (or RangeFirst, when
	// that's earlier) to the last, in every channel that changed. Direct
	// changes also keep the notes after it playing as before. False when
	// nothing changed.
	// The changed rows from RangeFirst (or the first changed one) to the last changed one, or to RangeEnd when that's
	// later: the notes after them play as they did before (direct changes)
	bool Collect(int RangeFirst, bool DirectChange, std::vector<Change> &Changes, int &First, std::vector<std::string> &Warnings,
		int RangeEnd = -1)
	{
		int Low = -1, High = -1;
		std::vector<int> Changed;
		for (std::map<int, std::vector<Cell> >::const_iterator it = m_Work.begin(); it != m_Work.end(); ++it) {
			const std::vector<Cell> &Old = m_S.Cells(it->first), &New = it->second;
			const size_t Rows = std::max(Old.size(), New.size());
			bool Any = false;
			for (size_t r = 0; r < Rows; ++r) {
				const Cell a = r < Old.size() ? Old[r] : Cell();
				const Cell b = r < New.size() ? New[r] : Cell();
				if (a != b) {
					if (Low < 0 || static_cast<int>(r) < Low)
						Low = static_cast<int>(r);
					High = std::max(High, static_cast<int>(r));
					Any = true;
				}
			}
			if (Any)
				Changed.push_back(it->first);
		}
		if (High < 0)
			return false;
		First = std::min(Low, std::max(0, RangeFirst));
		const int End = std::max(High + 1, RangeEnd);
		bool Lost = false;
		for (size_t i = 0; i < Changed.size(); ++i) {
			const std::vector<Cell> &New = m_Work[Changed[i]];
			Change C;
			C.Channel = Changed[i];
			for (int r = First; r < End; ++r)
				C.Cells.push_back(r < static_cast<int>(New.size()) ? New[r] : Cell());
			if (DirectChange && !KeepFollowing(m_S.Cells(C.Channel), First, End, C.Cells, m_S.Channel(C.Channel).EffectColumns))
				Lost = true;
			Changes.push_back(C);
		}
		if (Lost)
			Warnings.push_back("Some effects didn't fit in the channels' four effect columns and were left out.");
		return true;
	}

private:
	Session &m_S;
	std::map<int, std::vector<Cell> > m_Work;
};

// Moves the notes that start in Cells[First, End) to Map(pitch); arpeggios
// (0xy) on their rows follow when MoveChords. True if anything moved.
template <class MapFunction>
bool MapPitches(std::vector<Cell> &Cells, int First, int End, int Columns, MapFunction Map, bool MoveChords, int &Count,
	int &Skipped, std::map<int, int> *pMoves)
{
	int Original = -1;	// the playing note's pitch before
	bool Changed = false;
	for (int r = std::max(0, First); r < End && r < static_cast<int>(Cells.size()); ++r) {
		Cell &C = Cells[r];
		if (C.HasNote()) {
			Original = C.Pitch();
			const int Moved = Map(Original);
			if (Moved < 0 || Moved >= PITCHES) {
				++Skipped;
				Original = -1;
				continue;
			}
			if (pMoves != 0)
				(*pMoves)[Original % 12] = Moved - Original;
			Changed = Changed || Moved != Original;
			C.SetPitch(Moved);
			++Count;
		}
		else if (C.IsEnd())
			Original = -1;
		if (!MoveChords || Original < 0)
			continue;
		for (int k = 0; k < Columns && k < EFFECT_COLUMNS; ++k) {
			if (C.Effect[k] != EFF_ARPEGGIO || C.Param[k] == 0)
				continue;
			const int Root = Map(Original);
			const int x = Map(Original + (C.Param[k] >> 4)) - Root, y = Map(Original + (C.Param[k] & 15)) - Root;
			if (x >= 0 && x <= 15 && y >= 0 && y <= 15 && C.Param[k] != ((x << 4) | y)) {
				C.Param[k] = static_cast<unsigned char>((x << 4) | y);
				Changed = true;
			}
		}
	}
	return Changed;
}

// The notes that start in Cells[First, End) get Instrument; how many there are
int SetInstruments(std::vector<Cell> &Cells, int First, int End, int Instrument)
{
	int Count = 0;
	for (int r = std::max(0, First); r < End && r < static_cast<int>(Cells.size()); ++r) {
		if (Cells[r].HasNote()) {
			Cells[r].Instrument = static_cast<unsigned char>(Instrument);
			++Count;
		}
	}
	return Count;
}

// Vibrato 4xy on the notes that start in [First, End) and last MinRows or
// more, Delay rows in, with 400 on the note's last row; the vibrato a note
// had goes first. Depth 0 only takes it off. How many notes it changed.
int ApplyVibrato(std::vector<Cell> &Cells, const std::vector<NoteInfo> &Notes, int First, int End, int Speed, int Depth,
	int MinRows, int Delay, int Columns, bool &Lost)
{
	int Count = 0;
	for (size_t i = 0; i < Notes.size(); ++i) {
		const NoteInfo &N = Notes[i];
		if (N.Row < First || N.Row >= End)
			continue;
		const int Last = std::min(N.Row + N.Rows, static_cast<int>(Cells.size())) - 1;
		for (int r = N.Row; r <= Last; ++r) {
			for (int k = 0; k < EFFECT_COLUMNS; ++k) {
				if (Cells[r].Effect[k] == EFF_VIBRATO) {
					Cells[r].Effect[k] = EFF_NONE;
					Cells[r].Param[k] = 0;
				}
			}
		}
		if (Depth == 0) {
			++Count;
			continue;
		}
		if (N.Rows < MinRows || N.Row + Delay >= Last)
			continue;
		if (!AddEffect(Cells[N.Row + Delay], EFF_VIBRATO, (Speed << 4) | Depth, Columns) ||
			!AddEffect(Cells[Last], EFF_VIBRATO, 0, Columns))
			Lost = true;
		++Count;
	}
	return Count;
}

std::vector<int> ChannelsArg(Session &S, const Json &Args, const char *Name)
{
	std::vector<int> Channels;
	const Json *pValue = Arg(Args, Name);
	if (pValue == 0 || (pValue->IsString() && Lowercase(pValue->String()) == "all")) {
		for (int c = 0; c < S.ChannelCount(); ++c)
			if (IsMelodic(S.Channel(c)))
				Channels.push_back(c);
		return Channels;
	}
	if (pValue->IsArray()) {
		for (size_t i = 0; i < pValue->Size(); ++i)
			Channels.push_back(ParseChannel(S, pValue->At(i), Name));
	}
	else
		Channels.push_back(ParseChannel(S, *pValue, Name));
	std::sort(Channels.begin(), Channels.end());
	Channels.erase(std::unique(Channels.begin(), Channels.end()), Channels.end());
	return Channels;
}

// Sounds described in words

// A word from Words (as Values), or a number from 0 to Max
int WordArg(const Json &Args, const char *Name, int Default, const char *const *Words, const int *Values, int Count, int Max)
{
	const Json *pValue = Arg(Args, Name);
	if (pValue == 0)
		return Default;
	if (pValue->IsString()) {
		const std::string Text = Squeezed(pValue->String());
		for (int i = 0; i < Count; ++i)
			if (Text == Squeezed(Words[i]))
				return Values[i];
		if (Text.empty() || Text.find_first_not_of("0123456789") != std::string::npos) {
			std::string List;
			for (int i = 0; i < Count; ++i)
				List += (i == 0 ? "" : ", ") + std::string(Words[i]);
			throw Problem(std::string(Name) + ": " + List + (Max > 0 ? Format(", or a number up to %d.", Max) : std::string(".")));
		}
	}
	return IntArg(Args, Name, Default, 0, Max);
}

std::string DesignInstrumentTool(Session &S, const Json &Args)
{
	const std::string Name = StringArg(Args, "name");
	if (Name.empty())
		throw Problem("Give the instrument a name.");
	if (Name.size() > 60)
		throw Problem("name: up to 60 characters.");
	std::string Chip = "2A03";
	bool Sawtooth = false, Triangle = false, Noise = false, Low = false;
	if (Arg(Args, "channel") != 0) {
		const int Channel = ChannelArg(S, Args, "channel");
		Chip = InstrumentChip(S.Channel(Channel));
		Sawtooth = S.Channel(Channel).Kind == KIND_SAWTOOTH;
		Triangle = S.Channel(Channel).Kind == KIND_TRIANGLE;
		Noise = S.Channel(Channel).Kind == KIND_NOISE;
		Low = IsBass(S.Channel(Channel));
		if (S.Channel(Channel).Kind == KIND_DPCM)
			throw Problem("The DPCM channel plays samples; design_instrument makes sounds for the other channels.");
	}
	else if (Arg(Args, "chip") != 0) {
		Chip = StringArg(Args, "chip");
		for (size_t i = 0; i < Chip.size(); ++i)
			Chip[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(Chip[i])));
		if (Chip == "MMC5")
			Chip = "2A03";
	}
	if (Chip == "VRC7")
		throw Problem("VRC7 channels play the chip's own built-in sounds: add_instrument picks one by name (" + Vrc7PatchList() + ").");
	if (Chip != "2A03" && Chip != "VRC6")
		throw Problem("design_instrument makes sounds for 2A03 (and MMC5) and VRC6 channels.");
	if (Chip == "VRC6" && S.Song.Expansion.find("VRC6") == std::string::npos)
		throw Problem("This song has no VRC6 channels.");

	SoundDesign D;
	const std::string Kind = StringArg(Args, "kind");
	if (!Kind.empty()) {
		if (!DesignOfKind(Kind, D))
			throw Problem("kind: " + DesignKinds() + ".");
	}
	else if (Noise)
		throw Problem("For the noise channel, say which drum with kind: kick, snare, hihat, open hihat, crash or tom.");
	else
		DesignOfKind(Low ? "bass" : "lead", D);
	if (D.Drum && Arg(Args, "channel") != 0 && !Noise)
		throw Problem("A drum is for the noise channel.");
	if (!D.Drum && Noise)
		throw Problem("The noise channel plays drums: kind kick, snare, hihat, open hihat, crash or tom.");
	if (D.Drum) {
		Chip = "2A03";
		Sawtooth = false;
	}
	D.Name = Name;

	static const char *const ATTACKS[] = { "instant", "fast", "medium", "slow", "swell" };
	static const int ATTACK_FRAMES[] = { 0, 2, 5, 10, 20 };
	static const char *const DECAYS[] = { "none", "short", "medium", "long" };
	static const int DECAY_FRAMES[] = { 0, 4, 10, 24 };
	static const char *const SUSTAINS[] = { "none", "low", "half", "high", "full" };
	static const int SUSTAIN_LEVELS[] = { 0, 4, 8, 12, 15 };
	static const char *const RELEASES[] = { "short", "medium", "long" };
	static const int RELEASE_FRAMES[] = { 3, 8, 20 };
	static const char *const DUTIES[] = { "12.5%", "25%", "50%", "75%", "12.5", "25", "50", "75", "thin", "bright", "round", "square",
		"hollow", "nasal" };
	static const int DUTY_VALUES[] = { 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 2, 2, 0 };
	static const char *const MOTIONS[] = { "still", "none", "sweep", "open", "wobble", "chorus", "fade" };
	static const int MOTION_VALUES[] = { DUTY_STILL, DUTY_STILL, DUTY_SWEEP, DUTY_SWEEP, DUTY_WOBBLE, DUTY_WOBBLE, DUTY_FADE };
	static const char *const VIBRATOS[] = { "none", "gentle", "medium", "strong" };
	static const int VIBRATO_DEPTHS[] = { 0, 1, 2, 3 };
	static const char *const PITCHES_WORDS[] = { "none", "blip", "scoop", "drop", "flick", "thump" };
	static const int PITCH_VALUES[] = { PITCH_PLAIN, PITCH_BLIP, PITCH_SCOOP, PITCH_DROP, PITCH_FLICK, PITCH_THUMP };
	static const char *const DECAY_STYLES[] = { "smooth", "steps", "stepped" };
	static const int DECAY_STYLE_VALUES[] = { DECAY_SMOOTH, DECAY_STEPS, DECAY_STEPS };
	static const char *const TONES[] = { "dark", "normal", "bright" };
	static const int TONE_VALUES[] = { -1, 0, 1 };
	static const char *const LENGTHS[] = { "short", "normal", "long" };
	static const int LENGTH_VALUES[] = { -1, 0, 1 };
	if (Lowercase(StringArg(Args, "attack")) == "bloom") {	// from about two thirds of the peak, in two frames
		D.Attack = 2;
		D.AttackFrom = std::max(1, (IntArg(Args, "volume", D.Peak, 1, 15) * 2 + 1) / 3);
	}
	else
		D.Attack = WordArg(Args, "attack", D.Attack, ATTACKS, ATTACK_FRAMES, 5, 60);
	D.AttackFrom = IntArg(Args, "attack_from", D.AttackFrom, 0, 15);
	D.DecayStyle = WordArg(Args, "decay_style", D.DecayStyle, DECAY_STYLES, DECAY_STYLE_VALUES, 3, 0);
	D.Click = BoolArg(Args, "click", D.Click);
	D.Echo = BoolArg(Args, "echo", D.Echo);
	D.Decay = WordArg(Args, "decay", D.Decay, DECAYS, DECAY_FRAMES, 4, 120);
	D.Peak = IntArg(Args, "volume", D.Peak, 1, 15);
	if (Arg(Args, "decay") != 0 && D.Decay == 0 && Arg(Args, "sustain") == 0)
		D.Sustain = D.Peak;	// no decay: it holds at its peak
	D.Sustain = std::min(D.Peak, WordArg(Args, "sustain", D.Sustain, SUSTAINS, SUSTAIN_LEVELS, 5, 15));
	D.Release = WordArg(Args, "release", D.Release, RELEASES, RELEASE_FRAMES, 3, 60);
	D.Duty = WordArg(Args, "duty", D.Duty, DUTIES, DUTY_VALUES, 14, 0);
	D.Motion = WordArg(Args, "duty_motion", D.Motion, MOTIONS, MOTION_VALUES, 7, 0);
	D.Vibrato = WordArg(Args, "vibrato", D.Vibrato, VIBRATOS, VIBRATO_DEPTHS, 4, 0);
	if (Arg(Args, "vibrato_delay_beats") != 0) {
		const double Beats = NumberArg(Args, "vibrato_delay_beats", 0.5, 0.0, 8.0);
		const double Rows = RowsPerMinute(S.Song.Speed, S.Song.Tempo, S.Song.FrameRate);
		const double FramesPerBeat = Rows > 0 ? S.Time.RowsPerBeat * 60.0 * S.Song.FrameRate / Rows : 24.0;
		D.VibratoDelay = std::max(0, std::min(200, static_cast<int>(std::floor(Beats * FramesPerBeat + 0.5))));
	}
	D.AttackPitch = WordArg(Args, "attack_pitch", D.AttackPitch, PITCHES_WORDS, PITCH_VALUES, 6, 0);
	if (Arg(Args, "restrike_beats") != 0) {
		const double Beats = NumberArg(Args, "restrike_beats", 0.5, 0.0, 4.0);
		const double Rows = RowsPerMinute(S.Song.Speed, S.Song.Tempo, S.Song.FrameRate);
		const double FramesPerBeat = Rows > 0 ? S.Time.RowsPerBeat * 60.0 * S.Song.FrameRate / Rows : 24.0;
		D.Restrike = Beats <= 0 ? 0 : std::max(4, std::min(60, static_cast<int>(std::floor(Beats * FramesPerBeat + 0.5))));
	}
	const std::string Chord = Squeezed(StringArg(Args, "arpeggio"));
	if (!Chord.empty()) {
		static const struct { const char *Word; std::initializer_list<int> Tones; } CHORDS[] = {
			{ "none", {} }, { "octave", { 0, 12 } }, { "major", { 0, 4, 7 } }, { "minor", { 0, 3, 7 } }, { "fifth", { 0, 7 } },
			{ "power", { 0, 7, 12 } }, { "sus4", { 0, 5, 7 } }, { "major7", { 0, 4, 7, 11 } }, { "minor7", { 0, 3, 7, 10 } },
		};
		bool Known = false;
		for (size_t i = 0; i < sizeof(CHORDS) / sizeof(CHORDS[0]) && !Known; ++i) {
			if (Chord == CHORDS[i].Word) {
				D.Arpeggio.assign(CHORDS[i].Tones.begin(), CHORDS[i].Tones.end());
				Known = true;
			}
		}
		if (!Known)
			throw Problem("arpeggio: none, octave, major, minor, fifth, power, sus4, major7 or minor7.");
	}
	D.ArpeggioFrames = IntArg(Args, "arpeggio_frames", D.ArpeggioFrames, 1, 8);
	D.Tone = WordArg(Args, "tone", D.Tone, TONES, TONE_VALUES, 3, 0);
	D.Length = WordArg(Args, "length", D.Length, LENGTHS, LENGTH_VALUES, 3, 0);

	// A new name, so an instrument the song uses doesn't change under it
	const Preset Sound = DesignPreset(D);
	const std::string Final = Lowercase(Sound.InstrumentName(Sawtooth));
	for (size_t i = 0; i < S.Song.Instruments.size(); ++i)
		if (Lowercase(S.Song.Instruments[i].Name) == Final && S.Song.Instruments[i].Chip == Chip)
			throw Problem(Format("The song already has a %s instrument called \"%s\"; give the new sound another name.", Chip.c_str(),
				S.Song.Instruments[i].Name.c_str()));
	std::string Error;
	const int Index = S.Host().AddInstrument(Sound, Chip, Sawtooth, Error);
	if (Index < 0)
		throw Problem(Error.empty() ? "The instrument couldn't be added." : Error);
	S.Refresh();
	return "Added " + InstrumentLabel(S.Song, Index) + " (" + Chip + " instrument): " + DescribeDesign(D, S.Song.FrameRate,
		Sawtooth ? "sawtooth" : (Triangle ? "triangle" : "pulse")) +
		". Use it by name in write_notes, set_instrument or a part tool's instrument; play a few notes so the user hears it.";
}

std::string SetInstrumentTool(Session &S, const Json &Args)
{
	const int Channel = ChannelArg(S, Args, "channel");
	if (Arg(Args, "instrument") == 0)
		throw Problem("Say which instrument (one of the song's, or a preset).");
	const int Instrument = InstrumentArg(S, Args, "instrument", Channel, NO_INSTRUMENT);
	int From, To;
	BarRange(S, Args, From, To);
	const int First = S.Time.BarStart(From), End = S.Time.BarEnd(To);
	const bool AsClip = BoolArg(Args, "as_clip", false);
	Draft D(S);
	const int Count = SetInstruments(D.Cells(Channel), First, End, Instrument);
	if (Count == 0)
		throw Problem(Format("%s has no notes in %s.", S.Channel(Channel).FullName().c_str(), RangeText(From, To).c_str()));
	std::vector<Change> Changes;
	std::vector<std::string> Warnings;
	int Start;
	if (!D.Collect(First, Direct(S, AsClip), Changes, Start, Warnings))
		throw Problem(Format("Those notes already play %s.", InstrumentLabel(S.Song, Instrument).c_str()));
	const std::string What = Format("%s on %s, %s", InstrumentLabel(S.Song, Instrument).c_str(), S.Channel(Channel).FullName().c_str(),
		RangeText(From, To).c_str());
	return WithRemarks(S, Commit(S, Changes, Start, AsClip, StringArg(Args, "clip_name"), What) +
		Format(" (%s)", Plural(Count, "note", "notes").c_str()), Warnings);
}

// to_key and to_mode, from the key the notes are in (key, or found)
bool KeyChangeArgs(Session &S, const Json &Args, int First, int End, Key &FromKey, Key &ToKey)
{
	if (Arg(Args, "to_key") == 0 && Arg(Args, "to_mode") == 0)
		return false;
	bool Found;
	FromKey = KeyArg(S, Args, "key", First, End, Found);
	if (!Found)
		throw Problem("There are no notes to find the key from; give key.");
	ToKey = FromKey;
	if (Arg(Args, "to_key") != 0) {
		const std::string Text = StringArg(Args, "to_key");
		if (!ParseKey(Text, ToKey))
			throw Problem(Format("to_key: \"%s\" isn't a key. Write it like \"A minor\" or \"F# dorian\".", Text.c_str()));
	}
	if (Arg(Args, "to_mode") != 0) {
		const std::string Text = StringArg(Args, "to_mode");
		if (!ParseMode(Text, ToKey.Mode))
			throw Problem(Format("to_mode: \"%s\" isn't a mode. Modes: major, dorian, phrygian, lydian, mixolydian, minor, locrian, "
				"harmonic minor.", Text.c_str()));
	}
	if (ToKey.Tonic == FromKey.Tonic && ToKey.Mode == FromKey.Mode)
		throw Problem("The notes are already in " + KeyName(FromKey) + (Arg(Args, "key") != 0 ? "" :
			" (found from the notes; give key if that's wrong)") + ". Pick another to_mode or to_key.");
	return true;
}

std::string MovesText(const std::map<int, int> &Moves, const Key &FromKey, const Key &ToKey)
{
	std::string Summary;
	for (std::map<int, int>::const_iterator it = Moves.begin(); it != Moves.end(); ++it) {
		if (it->second == 0)
			continue;
		if (!Summary.empty())
			Summary += ", ";
		Summary += NoteClassName(it->first, FromKey) + " to " + NoteClassName(it->first + it->second, ToKey);
	}
	return Summary;
}

int RevoiceLushChords(Session &S, Draft &D, int Channel, int First, int End, const Key &FromKey, const Key &ToKey, int &Unknown);

std::string TransposeTool(Session &S, const Json &Args)
{
	std::vector<int> Channels = ChannelsArg(S, Args, "channels");
	int From, To;
	BarRange(S, Args, From, To);
	const int First = S.Time.BarStart(From), End = S.Time.BarEnd(To);
	const bool BySemitones = Arg(Args, "semitones") != 0;
	Key FromKey, ToKey;
	const bool ByKey = KeyChangeArgs(S, Args, First, End, FromKey, ToKey);
	if (BySemitones == ByKey)
		throw Problem("Give either semitones, or to_key or to_mode.");
	const int Semitones = IntArg(Args, "semitones", 0, -48, 48);
	const std::string How = ByKey ? "from " + KeyName(FromKey) + " to " + KeyName(ToKey) :
		Format("%s %s", Semitones >= 0 ? "up" : "down", Plural(std::abs(Semitones), "semitone", "semitones").c_str());

	std::vector<int> Melodic;
	for (size_t i = 0; i < Channels.size(); ++i)
		if (IsMelodic(S.Channel(Channels[i])) && NoteStartsIn(S, Channels[i], First, End))
			Melodic.push_back(Channels[i]);
	if (Melodic.empty()) {
		for (size_t i = 0; i < Channels.size(); ++i)
			if (IsMelodic(S.Channel(Channels[i])))
				throw Problem(Format("No notes to transpose in %s.", RangeText(From, To).c_str()));
		throw Problem("Noise and DPCM have no notes to transpose; pick melodic channels.");
	}

	const bool AsClip = BoolArg(Args, "as_clip", false);
	int Count = 0, Skipped = 0;
	std::map<int, int> Moves;	// pitch class -> semitones moved, for the summary
	Draft D(S);
	int Revoiced = 0, Unknown = 0;
	for (size_t i = 0; i < Melodic.size(); ++i) {
		const int Columns = S.Channel(Melodic[i]).EffectColumns;
		if (ByKey) {
			MapPitches(D.Cells(Melodic[i]), First, End, Columns, [&](int Pitch) { return MapToKey(Pitch, FromKey, ToKey); }, true, Count,
				Skipped, &Moves);
			Revoiced += RevoiceLushChords(S, D, Melodic[i], First, End, FromKey, ToKey, Unknown);
		}
		else
			MapPitches(D.Cells(Melodic[i]), First, End, Columns, [&](int Pitch) { return Pitch + Semitones; }, false, Count, Skipped, 0);
	}
	std::vector<std::string> Warnings;
	if (Revoiced > 0)
		S.Remarks.push_back(Format("%s now play%s the instrument for the chord's new shape.",
			Plural(Revoiced, "lush chord", "lush chords").c_str(), Revoiced == 1 ? "s" : ""));
	if (Unknown > 0)
		Warnings.push_back(Format("%s played by arpeggio instruments of the song's own keep their shapes, which may not fit %s.",
			Plural(Unknown, "chord", "chords").c_str(), KeyName(ToKey).c_str()));
	if (Skipped > 0)
		Warnings.push_back(Format("%s would have left FamiTracker's range (C-0 to B-7) and stayed as they were.",
			Plural(Skipped, "note", "notes").c_str()));
	std::vector<Change> Changes;
	int Start;
	if (!D.Collect(First, Direct(S, AsClip), Changes, Start, Warnings))
		throw Problem(Skipped > 0 ? "Every note would have left FamiTracker's range (C-0 to B-7); nothing moved." :
			ByKey ? "Nothing moved: those notes are the same in both keys." : "Nothing moved: give semitones other than 0.");
	const std::string What = Format("%s, %s, %s", How.c_str(), Plural(static_cast<int>(Changes.size()), "channel", "channels").c_str(),
		RangeText(From, To).c_str());
	std::string Result = Commit(S, Changes, Start, AsClip, StringArg(Args, "clip_name"), What) +
		Format(" (%s)", Plural(Count, "note", "notes").c_str());
	const std::string Summary = ByKey ? MovesText(Moves, FromKey, ToKey) : std::string();
	if (!Summary.empty())
		Result += " Notes moved: " + Summary + ".";
	return WithRemarks(S, Result, Warnings);
}

// The song's speed and tempo commands (Fxx) in the effect columns that play:
// speeds below the split point, tempos from it on
void SpeedCommands(Session &S, bool &Speeds, bool &Tempos)
{
	Speeds = Tempos = false;
	for (int c = 0; c < S.ChannelCount(); ++c) {
		const std::vector<Cell> &Cells = S.Cells(c);
		for (size_t r = 0; r < Cells.size(); ++r) {
			for (int k = 0; k < S.Channel(c).EffectColumns && k < EFFECT_COLUMNS; ++k) {
				if (Cells[r].Effect[k] != EFF_SPEED)
					continue;
				if (std::max(1, static_cast<int>(Cells[r].Param[k])) >= S.Song.SplitPoint)
					Tempos = true;
				else
					Speeds = true;
			}
		}
	}
}

// The speed and tempo for a BPM (ChooseSpeedTempo). Speed commands in the
// song would set another speed back, so then the speed stays and the tempo
// changes.
void SpeedTempoFor(Session &S, double Bpm, int RowsPerBeat, int &Speed, int &Tempo)
{
	bool Speeds = false, Tempos = false;
	SpeedCommands(S, Speeds, Tempos);
	ChooseSpeedTempo(Bpm, RowsPerBeat, S.Song.FrameRate, S.Song.Speed, S.Song.SplitPoint, Speed, Tempo, Speeds ? S.Song.Speed : 0);
}

// "120 BPM (speed 6, tempo 120)"
std::string TempoText(const Session &S, int Speed, int Tempo)
{
	const double Bpm = BeatsPerMinute(Speed, Tempo, S.Song.FrameRate, S.Time.RowsPerBeat);
	return Format("%s BPM (speed %d, tempo %d)", FormatNumber(std::floor(Bpm * 10 + 0.5) / 10).c_str(), Speed, Tempo);
}

// When rows don't all last the same number of ticks: how long they last, and
// (Nearby) the speeds at the default tempo that would be even around there
std::string UnevenText(const Session &S, int Speed, int Tempo, bool Nearby)
{
	const double Rows = RowsPerMinute(Speed, Tempo, S.Song.FrameRate);
	if (EvenRows(Speed, Tempo, S.Song.FrameRate) || Rows <= 0.0)
		return std::string();
	const double Ticks = 60.0 * S.Song.FrameRate / Rows;
	const int Shorter = std::max(1, static_cast<int>(std::floor(Ticks))), Longer = Shorter + 1;
	std::string Text = Format(" Its rows last %d or %d ticks, so the rhythm isn't quite even.", Shorter, Longer);
	const int Default = DefaultTempo(S.Song.FrameRate);
	if (!Nearby || Default < std::max(32, S.Song.SplitPoint) || Default > 255)
		return Text;
	std::vector<std::string> Even;
	const int Speeds[] = { Shorter, Longer };
	for (int i = 0; i < 2; ++i) {
		if (Speeds[i] < S.Song.SplitPoint && Speeds[i] <= 31)
			Even.push_back(TempoText(S, Speeds[i], Default));
	}
	if (!Even.empty())
		Text += " Even rows nearby: " + Even[0] + (Even.size() > 1 ? " or " + Even[1] : std::string()) + ".";
	return Text;
}

std::string SetTempoTool(Session &S, const Json &Args)
{
	if (!S.Song.DirectChanges)
		throw Problem(Format("Changing the tempo changes the song, and the user hasn't allowed direct changes. Suggest the user sets "
			"it with the speed and tempo boxes in FamiTracker's toolbar (now speed %d, tempo %d), or allows direct changes in "
			"File > Connect Claude Code.", S.Song.Speed, S.Song.Tempo));
	const bool ByBpm = Arg(Args, "bpm") != 0, BySpeed = Arg(Args, "speed") != 0, ByTempo = Arg(Args, "tempo") != 0;
	if (!ByBpm && !BySpeed && !ByTempo)
		throw Problem("Give the tempo as bpm, like {\"bpm\": 120}, or FamiTracker's speed and tempo, like {\"speed\": 5, \"tempo\": 150}.");
	if (ByBpm && BySpeed && ByTempo)
		throw Problem("Give bpm, or speed and tempo. With bpm, give at most one of speed and tempo: it stays, and the other is picked "
			"for the bpm.");
	const int MaxSpeed = std::max(1, std::min(31, S.Song.SplitPoint - 1)), MinTempo = std::max(32, S.Song.SplitPoint);
	int Speed = BySpeed ? IntArg(Args, "speed", S.Song.Speed, 1, MaxSpeed) : S.Song.Speed;
	int Tempo = ByTempo ? IntArg(Args, "tempo", S.Song.Tempo, MinTempo, 255) : S.Song.Tempo;
	if (ByBpm) {
		const double Bpm = NumberArg(Args, "bpm", 0.0, 10.0, 1000.0);
		if (BySpeed || ByTempo)
			ChooseSpeedTempo(Bpm, S.Time.RowsPerBeat, S.Song.FrameRate, S.Song.Speed, S.Song.SplitPoint, Speed, Tempo,
				BySpeed ? Speed : 0, ByTempo ? Tempo : 0);
		else
			SpeedTempoFor(S, Bpm, S.Time.RowsPerBeat, Speed, Tempo);
	}
	bool Speeds = false, Tempos = false;
	SpeedCommands(S, Speeds, Tempos);
	const bool Nearby = !Speeds && !Tempos;	// with speed commands, other speeds wouldn't last
	if (Speed == S.Song.Speed && Tempo == S.Song.Tempo)
		return "The song plays at " + TempoText(S, Speed, Tempo) + " already." + UnevenText(S, Speed, Tempo, Nearby);
	std::string Why, Error;
	if (!S.Host().CanChange(Why))
		throw Problem(Why);
	if (!S.Host().SetSpeedTempo(Speed, Tempo, Error))
		throw Problem(Error.empty() ? "The tempo couldn't be set." : Error);
	std::string Text = "Tempo set to " + TempoText(S, Speed, Tempo) + "; it was " + TempoText(S, S.Song.Speed, S.Song.Tempo) +
		". One Ctrl+Z undoes it." + UnevenText(S, Speed, Tempo, Nearby);
	// Fxx effects change it again while playing
	for (int c = 0; c < S.ChannelCount(); ++c) {
		const std::vector<Cell> &Cells = S.Cells(c);
		for (size_t r = 0; r < Cells.size(); ++r) {
			for (int k = 0; k < S.Channel(c).EffectColumns && k < EFFECT_COLUMNS; ++k) {
				if (Cells[r].Effect[k] == EFF_SPEED) {
					return Text + Format(" Note: the song sets speed or tempo with an F%02X effect at %s on %s, which still applies there.",
						Cells[r].Param[k], S.Time.Position(static_cast<int>(r)).c_str(), S.Channel(c).FullName().c_str());
				}
			}
		}
	}
	return Text;
}

// Swing

// The speed a row plays at: the last speed command (Fxx under the split
// point) before it in the song's rows, else the track's speed
int SpeedBefore(Session &S, int Row)
{
	for (int r = Row - 1; r >= 0; --r) {
		int Speed = 0;
		for (int c = 0; c < S.ChannelCount(); ++c) {
			const std::vector<Cell> &Cells = S.Cells(c);
			if (r >= static_cast<int>(Cells.size()))
				continue;
			for (int k = 0; k < std::min(EFFECT_COLUMNS, S.Channel(c).EffectColumns); ++k) {
				const int Param = std::max(1, static_cast<int>(Cells[r].Param[k]));
				if (Cells[r].Effect[k] == EFF_SPEED && Param < S.Song.SplitPoint)
					Speed = Param;	// a later channel's wins on its row, as FamiTracker reads them
			}
		}
		if (Speed > 0)
			return Speed;
	}
	return S.Song.Speed;
}

// The speeds of a swing: A for the first of each pair of units, B for the
// second, A + B twice Speed so the beat keeps its length; the pair whose A
// share is nearest Share (A == B == Speed when none is nearer than straight)
void SwingSpeeds(int Speed, int MaxSpeed, double Share, int &A, int &B)
{
	A = B = Speed;
	double Best = std::fabs(Share - 0.5);
	for (int a = Speed + 1; a <= MaxSpeed && 2 * Speed - a >= 1; ++a) {
		const double Miss = std::fabs(Share - static_cast<double>(a) / (2 * Speed));
		if (Miss < Best - 1e-9) {
			Best = Miss;
			A = a;
			B = 2 * Speed - a;
		}
	}
}

// A swing made with note delays (Gxx) on some channels only: each note that
// falls on the second unit of a pair (an eighth's "and", or a sixteenth's)
// starts late by the ticks the swing asks for at the row's speed, as
// FamiTracker composers shuffle a busy part while the rest plays straight.
// Share 0.5 takes the delays off those rows.
std::string SwingByDelays(Session &S, const Json &Args, double Share, int Parts, int First, int End, int From, int To)
{
	const int R = S.Time.RowsPerBeat, Unit = R / Parts;
	std::vector<int> Channels;
	if (Arg(Args, "channels") != 0)
		Channels = ChannelsArg(S, Args, "channels");
	else
		for (int c = 0; c < S.ChannelCount(); ++c)
			if (HasNotesIn(S, c, First, End))
				Channels.push_back(c);
	if (Channels.empty())
		throw Problem("No channel plays in those bars to swing.");
	// The speed of each row, as the song's speed commands set it
	std::vector<int> Speeds(static_cast<size_t>(End), S.Song.Speed);
	int Speed = S.Song.Speed;
	for (int r = 0; r < End; ++r) {
		for (int c = 0; c < S.ChannelCount(); ++c) {
			const std::vector<Cell> &Cells = S.Cells(c);
			if (r < static_cast<int>(Cells.size()))
				for (int k = 0; k < std::min(EFFECT_COLUMNS, S.Channel(c).EffectColumns); ++k)
					if (Cells[r].Effect[k] == EFF_SPEED && std::max(1, static_cast<int>(Cells[r].Param[k])) < S.Song.SplitPoint)
						Speed = std::max(1, static_cast<int>(Cells[r].Param[k]));
		}
		Speeds[static_cast<size_t>(r)] = Speed;
	}
	Draft D(S);
	int Delayed = 0, Lost = 0, Low = 99, High = 0;
	for (size_t i = 0; i < Channels.size(); ++i) {
		const int c = Channels[i];
		std::vector<Cell> &Cells = D.Cells(c);
		const int Columns = S.Channel(c).EffectColumns;
		for (int r = First; r < End && r < static_cast<int>(Cells.size()); ++r) {
			const int InBeat = (r - S.Time.BarStart(S.Time.BarOf(r))) % R;
			if (InBeat % (2 * Unit) != Unit)
				continue;	// not the second unit of a pair
			Cell &C = Cells[static_cast<size_t>(r)];
			for (int k = 0; k < EFFECT_COLUMNS; ++k)
				if (C.Effect[k] == EFF_DELAY) {
					C.Effect[k] = EFF_NONE;
					C.Param[k] = 0;
				}
			if (Share <= 0.5 || C.Note == NOTE_NONE)
				continue;
			const int Ticks = Speeds[static_cast<size_t>(r)];
			const int Delay = std::min(Ticks - 1, static_cast<int>(std::floor((2.0 * Share - 1.0) * Unit * Ticks + 0.5)));
			if (Delay < 1)
				continue;
			if (!AddEffect(C, EFF_DELAY, Delay, Columns)) {
				++Lost;
				continue;
			}
			Low = std::min(Low, Delay);
			High = std::max(High, Delay);
			++Delayed;
		}
	}
	std::vector<Change> Changes;
	std::vector<std::string> Warnings;
	if (Lost > 0)
		Warnings.push_back(Format("%s had no free effect column for the delay (all four were full).",
			Plural(Lost, "note", "notes").c_str()));
	int Start = First;
	std::string Names;
	for (size_t i = 0; i < Channels.size(); ++i)
		Names += (i == 0 ? "" : i + 1 == Channels.size() ? " and " : ", ") + S.Channel(Channels[i]).FullName();
	const std::string Where = RangeText(From, To);
	if (!D.Collect(First, true, Changes, Start, Warnings))
		return Share <= 0.5 ? "No delays to take off there, " + Where + "." :
			(Delayed == 0 ? "Nothing to delay there: no notes fall on the second " + std::string(Parts == 2 ? "eighth" : "sixteenth") +
			" of a pair, or the rows are too short for a delay, " + Where + "." : "It swings that way there already, " + Where + ".");
	std::string What;
	if (Share <= 0.5)
		What = Format("the delays of a swing taken off %s, %s", Names.c_str(), Where.c_str());
	else
		What = Format("a %d%% swing on the %s of %s, %s, by delays: %s late by %s (G%02X%s)",
			static_cast<int>(std::floor(100.0 * Share + 0.5)), Parts == 2 ? "eighths" : "sixteenths", Names.c_str(), Where.c_str(),
			Plural(Delayed, "note", "notes").c_str(), Low == High ? Plural(Low, "frame", "frames").c_str() :
			Format("%d-%d frames", Low, High).c_str(), Low, Low == High ? "" : Format("-G%02X", High).c_str());
	std::string Text = Commit(S, Changes, Start, false, "", What);
	if (Share > 0.5)
		Text += " The other channels play straight; set_swing without by swings them all with the speed.";
	return WithRemarks(S, Text, Warnings);
}

std::string SetSwingTool(Session &S, const Json &Args)
{
	if (!S.Song.DirectChanges)
		throw Problem("Swing changes the song's speed every half beat, which needs direct changes; the user can allow them in "
			"File > Connect Claude Code.");
	// How much: the long unit's share of each pair
	double Share = 0.5;
	const Json *pAmount = Arg(Args, "amount");
	if (pAmount == 0)
		throw Problem("Give amount: none, light, medium, hard (triplet), or a percentage from 50 (straight) to 75.");
	if (pAmount->IsNumber())
		Share = NumberArg(Args, "amount", 50.0, 50.0, 80.0) / 100.0;
	else {
		const std::string Word = Squeezed(StringArg(Args, "amount"));
		if (Word == "none" || Word == "straight" || Word == "off" || Word == "even")
			Share = 0.5;
		else if (Word == "light" || Word == "subtle" || Word == "gentle" || Word == "lazy")
			Share = 0.58;
		else if (Word == "medium" || Word == "normal")
			Share = 0.625;
		else if (Word == "hard" || Word == "triplet" || Word == "triplets" || Word == "shuffle" || Word == "heavy" || Word == "full")
			Share = 2.0 / 3.0;
		else
			throw Problem("amount: none, light, medium, hard (triplet), or a percentage from 50 (straight) to 75.");
	}
	const std::string UnitWord = Squeezed(StringArg(Args, "unit", "eighths"));
	const int R = S.Time.RowsPerBeat;
	int Parts;
	if (UnitWord == "eighths" || UnitWord == "eighth" || UnitWord == "8ths" || UnitWord == "halfbeats")
		Parts = 2;
	else if (UnitWord == "sixteenths" || UnitWord == "sixteenth" || UnitWord == "16ths" || UnitWord == "quarterbeats")
		Parts = 4;
	else
		throw Problem("unit: eighths (half beats, the usual swing) or sixteenths (quarter beats).");
	if (R % Parts != 0)
		throw Problem(Format("A beat here is %s, which doesn't split into %s; swing needs beats that do (row highlight %d is %d "
			"rows a beat).", Plural(R, "row", "rows").c_str(), Parts == 2 ? "halves" : "quarters", R, R));
	const int Unit = R / Parts;
	int From = 1, To = S.Time.BarCount();
	if (Arg(Args, "from_bar") != 0 || Arg(Args, "to_bar") != 0)
		BarRange(S, Args, From, To);
	const int First = S.Time.BarStart(From), End = std::min(S.Time.TotalRows(), S.Time.BarEnd(To));
	const int Split = S.Song.SplitPoint;
	const std::string By = Squeezed(StringArg(Args, "by", "speed"));
	if (By == "delays" || By == "delay" || By == "gxx" || By == "notedelays")
		return SwingByDelays(S, Args, Share, Parts, First, End, From, To);
	if (By != "speed" && By != "speeds" && By != "fxx")
		throw Problem("by: speed (every channel swings, by speed changes) or delays (only the channels named, by note delays).");

	// The speed the rows play at on average there now (a swing there comes out as its middle)
	long long Ticks = 0;
	{
		int Speed = SpeedBefore(S, First);
		std::vector<int> Set(static_cast<size_t>(End - First), 0);
		for (int c = 0; c < S.ChannelCount(); ++c) {
			const std::vector<Cell> &Cells = S.Cells(c);
			for (int r = First; r < End && r < static_cast<int>(Cells.size()); ++r)
				for (int k = 0; k < std::min(EFFECT_COLUMNS, S.Channel(c).EffectColumns); ++k)
					if (Cells[r].Effect[k] == EFF_SPEED && std::max(1, static_cast<int>(Cells[r].Param[k])) < Split)
						Set[static_cast<size_t>(r - First)] = std::max(1, static_cast<int>(Cells[r].Param[k]));
		}
		for (int r = First; r < End; ++r) {
			if (Set[static_cast<size_t>(r - First)] != 0)
				Speed = Set[static_cast<size_t>(r - First)];
			Ticks += Speed;
		}
	}
	const int Base = std::max(1, static_cast<int>((Ticks + (End - First) / 2) / std::max(1, End - First)));
	int A, B;
	SwingSpeeds(Base, std::min(31, Split - 1), Share, A, B);
	std::string Fits;	// the swings the speed's ticks allow
	for (int a = Base + 1; a <= std::min(31, Split - 1) && 2 * Base - a >= 1; ++a)
		Fits += (Fits.empty() ? "" : ", ") + Format("%d%%", static_cast<int>(std::floor(100.0 * a / (2 * Base) + 0.5)));
	if (A == B && Share > 0.5)
		throw Problem(Fits.empty() ? Format("At speed %d a beat can't swing: its rows have too few ticks to split unevenly. A higher "
			"speed (with a higher tempo to keep the BPM) can.", Base) :
			Format("At speed %d a beat can't swing that little; the swings that fit are %s.", Base, Fits.c_str()));
	const int After = End < S.Time.TotalRows() ? SpeedBefore(S, End + 1) : -1;	// what plays after the bars, as it was

	// The speed commands there go; the swing's come in, on the channel that has
	// the song's speed commands (else the last one), or another with room
	int Home = S.ChannelCount() - 1;
	for (int c = S.ChannelCount() - 1; c >= 0; --c) {
		bool Has = false;
		for (size_t r = 0; r < S.Cells(c).size() && !Has; ++r)
			for (int k = 0; k < std::min(EFFECT_COLUMNS, S.Channel(c).EffectColumns); ++k)
				Has = Has || S.Cells(c)[r].Effect[k] == EFF_SPEED;
		if (Has) {
			Home = c;
			break;
		}
	}
	Draft D(S);
	int Removed = 0, Lost = 0;
	for (int c = 0; c < S.ChannelCount(); ++c) {
		std::vector<Cell> &Cells = D.Cells(c);
		for (int r = First; r < End && r < static_cast<int>(Cells.size()); ++r)
			for (int k = 0; k < EFFECT_COLUMNS; ++k)
				if (Cells[r].Effect[k] == EFF_SPEED && std::max(1, static_cast<int>(Cells[r].Param[k])) < Split) {
					Cells[r].Effect[k] = EFF_NONE;
					Cells[r].Param[k] = 0;
					++Removed;
				}
	}
	auto Put = [&](int Row, int Speed) {
		if (AddEffect(D.Cells(Home)[static_cast<size_t>(Row)], EFF_SPEED, Speed, S.Channel(Home).EffectColumns))
			return;
		for (int c = S.ChannelCount() - 1; c >= 0; --c)
			if (c != Home && AddEffect(D.Cells(c)[static_cast<size_t>(Row)], EFF_SPEED, Speed, S.Channel(c).EffectColumns))
				return;
		++Lost;
	};
	int Written = 0;
	if (A != B) {
		for (int r = First; r < End; ++r) {
			const int Bar = S.Time.BarOf(r), InBar = r - S.Time.BarStart(Bar);
			if ((InBar % R) % Unit != 0)
				continue;
			Put(r, ((InBar % R) / Unit) % 2 == 0 ? A : B);
			++Written;
		}
	}
	else if (SpeedBefore(S, First) != Base) {
		Put(First, Base);
		++Written;
	}
	if (After > 0) {
		// The rows after play at the speed they did
		bool Own = false;
		for (int c = 0; c < S.ChannelCount(); ++c)
			for (int k = 0; k < EFFECT_COLUMNS; ++k)
				Own = Own || (D.Cells(c)[static_cast<size_t>(End)].Effect[k] == EFF_SPEED &&
					std::max(1, static_cast<int>(D.Cells(c)[static_cast<size_t>(End)].Param[k])) < Split);
		if (!Own && (A != B ? B : Base) != After)
			Put(End, After);
	}
	if (Lost > 0)
		throw Problem(Format("%s had no free effect column for the swing's speed changes (all four were full; on %s, say).", Plural(Lost, "row", "rows").c_str(), S.Channel(Home).FullName().c_str()));
	std::vector<Change> Changes;
	std::vector<std::string> Warnings;
	int Start = First;
	const std::string Where = RangeText(From, To);
	if (!D.Collect(First, true, Changes, Start, Warnings))
		return A == B ? "The song plays straight there already (no swing), " + Where + "." :
			Format("The song swings that way there already (F%02X and F%02X), %s.", A, B, Where.c_str());
	std::string What;
	if (A == B)
		What = Format("swing taken off, %s: straight at speed %d", Where.c_str(), Base);
	else
		What = Format("a %d%% swing on the %s, %s: speed %d then %d (F%02X, F%02X) in turn, which averages the speed it had (%d), "
			"so the tempo stays", static_cast<int>(std::floor(100.0 * A / (A + B) + 0.5)), Parts == 2 ? "eighths" : "sixteenths",
			Where.c_str(), A, B, A, B, Base);
	std::string Text = Commit(S, Changes, Start, false, "", What);
	if (A != B && std::fabs(static_cast<double>(A) / (A + B) - Share) > 0.03)
		Text += Format(" At speed %d the swings that fit are %s; a higher speed (with a higher tempo to keep the BPM) splits "
			"the beat finer.", Base, Fits.c_str());
	if (Removed > Written && A != B)
		Warnings.push_back(Format("%s there went for the swing's.", Plural(Removed, "speed change", "speed changes").c_str()));
	return WithRemarks(S, Text, Warnings);
}

int FreeChannel(Session &S, int First, int End, int Avoid, bool (*Fits)(const ChannelInfo &))
{
	for (int Pass = 0; Pass < 2; ++Pass) {
		for (int c = 0; c < S.ChannelCount(); ++c) {
			if (c == Avoid || !Fits(S.Channel(c)))
				continue;
			if (Pass == 1 || !HasNotesIn(S, c, First, End))
				return Pass == 0 ? c : -1 - c;	// -1 - c: taken, but the only kind there is
		}
	}
	return -1000;
}

bool IsChordChannel(const ChannelInfo &Channel)
{
	return (Channel.Kind == KIND_PULSE && (Channel.Chip == "2A03" || Channel.Chip == "VRC6" || Channel.Chip == "MMC5")) ||
		Channel.Kind == KIND_FM;
}

// A channel for lush chords: a pulse whose instruments can have arpeggios
bool IsLushChordChannel(const ChannelInfo &Channel)
{
	return Channel.Kind == KIND_PULSE && (Channel.Chip == "2A03" || Channel.Chip == "VRC6" || Channel.Chip == "MMC5");
}

bool IsBassChannel(const ChannelInfo &Channel)
{
	return IsBass(Channel);
}

int EchoDelayOf(Session &S, int Channel, int Source, int First, int End);

// The parts a song already plays in [First, End), which new chords and a new
// bass line there replace rather than stack on

// The chord part: the channel beside the melody with the most notes there that
// sound a chord (a lush or arpeggio instrument, or 0xy); -1 if none
int ChordPartIn(Session &S, int First, int End, int Melody)
{
	int Best = -1, BestCount = 0;
	for (int c = 0; c < S.ChannelCount(); ++c) {
		if (c == Melody || !IsMelodic(S.Channel(c)) || IsBass(S.Channel(c)))
			continue;
		int Count = 0;
		const std::vector<NoteInfo> &Notes = S.Notes(c);
		for (size_t i = 0; i < Notes.size(); ++i)
			if (Notes[i].Row >= First && Notes[i].Row < End && SoundsChord(S, Notes[i]))
				++Count;
		if (Count > BestCount) {
			Best = c;
			BestCount = Count;
		}
	}
	return Best;
}

// The bass line: the triangle or sawtooth beside the melody with the most notes there; -1 if none
int BassPartIn(Session &S, int First, int End, int Melody)
{
	int Best = -1, BestCount = 0;
	for (int c = 0; c < S.ChannelCount(); ++c) {
		if (c == Melody || !IsBass(S.Channel(c)))
			continue;
		const int Count = static_cast<int>(NotesIn(S.Notes(c), First, End).size());
		if (Count > BestCount) {
			Best = c;
			BestCount = Count;
		}
	}
	return Best;
}

// The instrument most of Notes play
int UsualInstrument(const std::vector<NoteInfo> &Notes)
{
	std::map<int, int> Count;
	int Best = NO_INSTRUMENT, BestCount = 0;
	for (size_t i = 0; i < Notes.size(); ++i) {
		const int n = ++Count[Notes[i].Instrument];
		if (n > BestCount) {
			Best = Notes[i].Instrument;
			BestCount = n;
		}
	}
	return Best;
}

// Voiced chords: two to four channels of one kind (FM, N163 or pulses) beside
// the melody, not an echo of it, whose notes there mostly start together; empty if none
std::vector<int> ChordVoicesIn(Session &S, int First, int End, int Melody)
{
	for (int Group = 0; Group < 3; ++Group) {
		std::vector<int> Channels;
		std::vector<std::set<int> > Starts;
		for (int c = 0; c < S.ChannelCount(); ++c) {
			const ChannelInfo &Info = S.Channel(c);
			const bool Fits = Group == 0 ? Info.Kind == KIND_FM : Group == 1 ? Info.Chip == "N163" : IsLushChordChannel(Info);
			if (!Fits || c == Melody || (Melody >= 0 && EchoDelayOf(S, c, Melody, First, End) > 0))
				continue;
			const std::vector<NoteInfo> Notes = NotesIn(S.Notes(c), First, End);
			if (Notes.size() < 2)
				continue;
			std::set<int> Rows;
			for (size_t i = 0; i < Notes.size(); ++i)
				Rows.insert(Notes[i].Row);
			Channels.push_back(c);
			Starts.push_back(Rows);
		}
		// The channels that start most of their notes with another's
		std::vector<int> Voices;
		for (size_t a = 0; a < Channels.size(); ++a) {
			bool Together = false;
			for (size_t b = 0; b < Channels.size() && !Together; ++b) {
				if (a == b)
					continue;
				int Shared = 0;
				for (std::set<int>::const_iterator it = Starts[a].begin(); it != Starts[a].end(); ++it)
					Shared += Starts[b].count(*it) > 0 ? 1 : 0;
				Together = Shared * 10 >= static_cast<int>(Starts[a].size()) * 7;
			}
			if (Together && Voices.size() < 4)
				Voices.push_back(Channels[a]);
		}
		if (Voices.size() >= 2)
			return Voices;
	}
	return std::vector<int>();
}

// Arranging: chords in a style, bass lines, drums, harmony and counter-melodies

std::vector<BarSpan> BarSpans(const std::vector<transcribe::Bar> &Bars)
{
	std::vector<BarSpan> Out;
	for (size_t i = 0; i < Bars.size(); ++i)
		Out.push_back(BarSpan(Bars[i].row, Bars[i].length));
	return Out;
}

std::vector<BarSpan> SongBarSpans(const Session &S, int First, int End)
{
	return BarSpans(transcribe::SongBars(First, End, S.Time.PatternLength, S.Time.RowsPerBar));
}

// The harmony code's chords (triads), to arrange
std::vector<ChordAt> ChordsOf(const std::vector<transcribe::ChordSpan> &Spans)
{
	std::vector<ChordAt> Out;
	for (size_t i = 0; i < Spans.size(); ++i) {
		ChordAt A;
		A.Row = Spans[i].row;
		A.Rows = Spans[i].length;
		const transcribe::Chord &C = Spans[i].chord;
		std::vector<int> Tones;
		Tones.push_back(C.quality == transcribe::Chord::Major ? 4 : 3);
		Tones.push_back(C.quality == transcribe::Chord::Diminished ? 6 : 7);
		A.C = Chord(C.root, Tones);
		Out.push_back(A);
	}
	return Out;
}

// The triangle sounds an octave below what's written: a pitch that sounds
// as Sounding does elsewhere, as written on Channel
int Written(const ChannelInfo &Channel, int Sounding)
{
	return Channel.Kind == KIND_TRIANGLE ? Sounding + 12 : Sounding;
}

// A style named by an argument, or Default
int StyleArg(const Json &Args, const char *Name, const std::vector<StyleInfo> &Styles, int Default)
{
	const std::string Text = StringArg(Args, Name);
	if (Text.empty())
		return Default;
	const int Index = FindStyle(Styles, Text);
	if (Index < 0) {
		std::string Names;
		for (size_t i = 0; i < Styles.size(); ++i)
			Names += (i == 0 ? "" : ", ") + std::string(Styles[i].Name);
		throw Problem(std::string(Name) + ": \"" + Text + "\" isn't one. Choose from " + Names + ".");
	}
	return Index;
}

// A progression from an argument: text with bar lines ("| C | Am7 | F G |"),
// chords a bar each without them ("C Am F G", "I-vi-IV-V"), or a list of bars
std::vector<std::string> ProgressionArg(const Json &Args, const char *Name)
{
	const Json *pValue = Arg(Args, Name);
	std::vector<std::string> Bars;
	if (pValue == 0)
		return Bars;
	if (pValue->IsString())
		Bars = SplitProgression(pValue->String());
	else if (pValue->IsArray()) {
		for (size_t i = 0; i < pValue->Size(); ++i) {
			const Json &Item = pValue->At(i);
			if (!Item.IsString())
				throw Problem(std::string(Name) + ": each bar is text, like \"Am7\" or \"F G7\".");
			const std::string Text = Trim(Item.String());
			if (Text.find('|') != std::string::npos) {
				const std::vector<std::string> Split = SplitProgression(Text);
				Bars.insert(Bars.end(), Split.begin(), Split.end());
			}
			else
				Bars.push_back(Text.empty() ? "." : Text);
		}
	}
	else
		throw Problem(std::string(Name) + " should be text like \"| C | Am7 | F G7 | C |\" or a list of bars like [\"C\", \"Am7\", \"F G7\", \"C\"].");
	if (Bars.empty())
		throw Problem(std::string(Name) + " has no chords in it.");
	return Bars;
}

// Where chords go on a channel: their lowest notes about an octave and a
// half under the melody's middle (a rolling pattern spreads two octaves up,
// so it starts lower)
void ChordRegister(ChordPartOptions &O, const ChannelInfo &Info, const std::vector<NoteInfo> &Melody)
{
	int Top = Melody.empty() ? 43 : MiddleOf(Melody, 52) - 7;
	Top = std::max(36, std::min(48, Top));
	if (O.Style == CHORDS_BROKEN && O.Pattern == BROKEN_ROLLING)
		Top -= 5;
	else if (O.Style == CHORDS_LUSH)
		Top -= O.Pattern == BROKEN_ROLLING ? 7 : 3;	// the arpeggio climbs an octave and more above its root
	O.High = Written(Info, Top);
	O.Low = O.High - 12;
}

// A bass line's register: C-1 to G-2 as a pulse or the sawtooth plays them,
// written an octave up on the triangle, where it sounds the same
void BassRegister(BassOptions &O, const ChannelInfo &Info)
{
	O.Low = Written(Info, 9);
	O.High = Written(Info, 28);
}

// Drum hits as notes, each ringing until the next, with their instruments
// (added from the presets) and volumes scaled to Loudest
std::vector<PartNote> DrumNotes(Session &S, int Channel, const std::vector<DrumHit> &Hits, int First, int End, int Loudest)
{
	std::map<std::string, int> Instruments;
	std::vector<PartNote> Notes;
	for (size_t i = 0; i < Hits.size(); ++i) {
		const DrumHit &H = Hits[i];
		if (Instruments.find(H.Sound) == Instruments.end())
			Instruments[H.Sound] = ResolveInstrument(S, H.Sound, Channel);
		PartNote N;
		N.Row = H.Row;
		N.Rows = (i + 1 < Hits.size() ? Hits[i + 1].Row : End) - H.Row;
		N.Pitch = H.Pitch;
		N.Instrument = Instruments[H.Sound];
		N.Volume = std::max(1, (H.Volume * Loudest + 7) / 15);
		Notes.push_back(N);
	}
	TidyPart(Notes, First, End);
	return Notes;
}

// The drum kit asked for: the noise channel's (false), or sampled kick, snare
// and toms (true)
bool SampledKitArg(const Json &Args)
{
	const std::string Kit = Squeezed(StringArg(Args, "kit", "noise"));
	if (Kit == "noise" || Kit == "synth" || Kit == "noisechannel")
		return false;
	if (Kit == "samples" || Kit == "sampled" || Kit == "sample" || Kit == "dpcm" || Kit == "sampledrums")
		return true;
	throw Problem("kit: noise (every drum on the noise channel) or samples (a sampled kick, snare and toms on the DPCM channel).");
}

// The song's DPCM channel, for sampled drums
int DpcmChannel(Session &S)
{
	for (int c = 0; c < S.ChannelCount(); ++c)
		if (S.Channel(c).Kind == KIND_DPCM)
			return c;
	throw Problem("The song has no DPCM channel for sampled drums.");
}

// A drum part's changes: the noise channel's hits, and the sampled ones on the
// DPCM channel (Dpcm, -1 for none), which play the kit of add_sample_drums.
// Where says where they went.
void DrumChanges(Session &S, int Noise, int Dpcm, const std::vector<DrumHit> &Hits, int First, int End, int Loudest, bool DirectChange,
	std::vector<Change> &Changes, std::string &Where, std::vector<std::string> &Warnings)
{
	std::vector<DrumHit> NoiseHits, Sampled;
	for (size_t i = 0; i < Hits.size(); ++i)
		(Hits[i].Sampled && Dpcm >= 0 ? Sampled : NoiseHits).push_back(Hits[i]);
	Change C;
	C.Channel = Noise;
	BuildPart(S.Cells(Noise), First, End, DrumNotes(S, Noise, NoiseHits, First, End, Loudest), OptionsFor(S, Noise, DirectChange), C.Cells,
		Warnings);
	Changes.push_back(C);
	Where = " on " + S.Channel(Noise).FullName();
	if (Dpcm < 0 || Sampled.empty())
		return;
	const int Kit = SampleDrumsInstrument(S);
	std::vector<PartNote> Notes;
	for (size_t i = 0; i < Sampled.size(); ++i) {
		const DrumHit &H = Sampled[i];
		const DpcmKey *pKey = FindSampleDrum(H.Sound == "tom" ? (H.Pitch >= 8 ? "high tom" : "low tom") : H.Sound);
		if (pKey == 0)
			continue;
		PartNote N;
		N.Row = H.Row;
		N.Rows = (i + 1 < Sampled.size() ? Sampled[i + 1].Row : End) - H.Row;	// each rings until the next
		N.Pitch = pKey->Pitch;
		N.Instrument = Kit;
		Notes.push_back(N);
	}
	TidyPart(Notes, First, End);
	if (HasNotesIn(S, Dpcm, First, End))
		Warnings.push_back(S.Channel(Dpcm).FullName() + "'s notes in those bars were replaced by the sampled drums.");
	Change D;
	D.Channel = Dpcm;
	BuildPart(S.Cells(Dpcm), First, End, Notes, OptionsFor(S, Dpcm, DirectChange), D.Cells, Warnings);
	Changes.push_back(D);
	Where = ": sampled kick and snare on " + S.Channel(Dpcm).FullName() + ", hi-hats on " + S.Channel(Noise).FullName();
}

// A lush chord instrument's name: the sound's, the chord's ("maj7", "m9",
// "major"), and the pattern and frames when they aren't the usual ones:
// "Lush maj7", "Pluck m7 down", "Lush m9, 3 frames"
std::string LushName(const Preset &Sound, const std::vector<int> &Notes, int Pattern, int Frames)
{
	std::vector<int> Tones;
	for (size_t i = 0; i < Notes.size(); ++i)
		Tones.push_back((Notes[i] % 12 + 12) % 12);
	std::sort(Tones.begin(), Tones.end());
	Tones.erase(std::unique(Tones.begin(), Tones.end()), Tones.end());
	std::string Quality = ChordName(Chord(0, Tones), Key(0, MODE_MAJOR)).substr(1);
	if (Quality.empty())
		Quality = "major";
	else if (Quality == "m")
		Quality = "minor";
	std::string Name = Sound.Name == "lush chords" ? std::string("lush") : Sound.Name;
	Name += " " + Quality;
	if (Pattern != BROKEN_UP)
		Name += std::string(" ") + BrokenPatterns()[static_cast<size_t>(Pattern)].Name;
	if (Frames != 2)
		Name += Format(", %d frame%s", Frames, Frames == 1 ? "" : "s");
	return Name;
}

// The lush chord instrument for a shape: Sound's with the shape looping as
// its arpeggio, Frames frames a note, named by LushName; added when the song
// has none of that name (its label goes into Added and Count counts it)
int LushInstrument(Session &S, const std::string &Chip, bool Sawtooth, const Preset &Sound, const std::vector<int> &Shape, int Pattern,
	int Frames, std::string &Added, int &Count)
{
	const Preset P = WithArpeggio(Sound, Shape, Frames, LushName(Sound, Shape, Pattern, Frames));
	const size_t Before = S.Song.Instruments.size();
	std::string Error;
	const int Index = S.Host().AddInstrument(P, Chip, Sawtooth, Error);
	if (Index < 0)
		throw Problem(Error.empty() ? "The instrument couldn't be added." : Error);
	S.Refresh();
	if (S.Song.Instruments.size() > Before) {
		Added += (Count == 0 ? "" : ", ") + InstrumentLabel(S.Song, Index);
		++Count;
	}
	return Index;
}

// Lush chords' instruments: Sound's (a preset) with each chord's notes
// looping as its arpeggio, Frames frames a note, one instrument a chord
// shape, added when first needed and played by the notes with that shape
void LushInstruments(Session &S, int Channel, const std::string &Sound, int Pattern, int Frames, std::vector<PartNote> &Notes)
{
	const std::string Chip = InstrumentChip(S.Channel(Channel));
	const std::string ChannelName = S.Channel(Channel).FullName();
	const bool Sawtooth = S.Channel(Channel).Kind == KIND_SAWTOOTH;
	if (Chip != "2A03" && Chip != "VRC6")
		throw Problem(Format("Lush chords are instruments with arpeggios, which %s can't play; give them a pulse channel "
			"(2A03, MMC5 or VRC6), or choose another chord_style.", ChannelName.c_str()));
	const Preset *pSound = FindPreset(Sound);
	if (pSound == 0 || pSound->Drum)
		throw Problem(Format("Lush chords are made from a preset's sound (lush chords, soft chords, pluck, bell, warm pad...); "
			"\"%s\" isn't one.", Sound.c_str()));
	std::map<std::vector<int>, int> Made;
	std::string Added;
	int Count = 0;
	for (size_t i = 0; i < Notes.size(); ++i) {
		if (Notes[i].Arpeggio.empty())
			continue;
		std::map<std::vector<int>, int>::const_iterator Found = Made.find(Notes[i].Arpeggio);
		if (Found == Made.end())
			Found = Made.insert(std::make_pair(Notes[i].Arpeggio, LushInstrument(S, Chip, Sawtooth, *pSound, Notes[i].Arpeggio, Pattern,
				Frames, Added, Count))).first;
		Notes[i].Instrument = Found->second;
	}
	if (Count > 0)
		S.Remarks.push_back(Format("Added %d lush chord instrument%s (%s, from the preset \"%s\"): %s.", Count, Count == 1 ? "" : "s",
			Chip.c_str(), pSound->Name.c_str(), Added.c_str()));
}

// A lush chord's instrument's name (LushName) read back: its preset, pattern
// and frames; false for other instruments
bool ParseLushName(const std::string &Name, const Preset *&pSound, int &Pattern, int &Frames)
{
	std::string Rest = Lowercase(Trim(Name));
	Frames = 2;
	const size_t Comma = Rest.rfind(", ");
	if (Comma != std::string::npos) {
		const char *pFrom = Rest.c_str() + Comma + 2;
		char *pAfter = 0;
		const long n = std::strtol(pFrom, &pAfter, 10);
		if (pAfter == pFrom || std::string(pAfter).compare(0, 6, " frame") != 0 || n < 1 || n > 6)
			return false;
		Frames = static_cast<int>(n);
		Rest.erase(Comma);
	}
	Pattern = BROKEN_UP;
	const size_t Space = Rest.rfind(' ');
	if (Space == std::string::npos)
		return false;
	for (size_t p = 0; p < BrokenPatterns().size(); ++p)
		if (p != BROKEN_UP && Rest.compare(Space + 1, std::string::npos, BrokenPatterns()[p].Name) == 0) {
			Pattern = static_cast<int>(p);
			Rest.erase(Space);
			break;
		}
	if (Rest.compare(0, 5, "lush ") == 0 && Rest.size() > 5) {
		pSound = FindPreset("lush chords");
		return pSound != 0;
	}
	const std::vector<Preset> &All = Presets();
	for (size_t i = 0; i < All.size(); ++i) {
		const std::string Prefix = Lowercase(All[i].Name) + " ";
		if (!All[i].Drum && Rest.size() > Prefix.size() && Rest.compare(0, Prefix.size(), Prefix) == 0) {
			pSound = &All[i];
			return true;
		}
	}
	return false;
}

// Lush chords a key change moved (the cells in the draft moved by MapToKey):
// each chord's notes moved as the key moves them, and the chord given the
// lush instrument for its new shape (made when the song has none). How many
// chords changed instruments; Unknown counts those played by arpeggio
// instruments that aren't the lush style's, which keep their shapes.
int RevoiceLushChords(Session &S, Draft &D, int Channel, int First, int End, const Key &FromKey, const Key &ToKey, int &Unknown)
{
	const std::string Chip = InstrumentChip(S.Channel(Channel));
	if (Chip != "2A03" && Chip != "VRC6")
		return 0;
	const std::vector<Cell> Old = S.Cells(Channel);
	std::vector<Cell> &Cells = D.Cells(Channel);
	const bool Sawtooth = S.Channel(Channel).Kind == KIND_SAWTOOTH;
	int Instrument = StateAt(Old, std::max(0, First), S.Channel(Channel).EffectColumns).Instrument;	// as the song had it
	int Playing = Instrument;	// as the draft has it
	int Changed = 0, Count = 0;
	std::string Added;
	const Preset *pSoundAdded = 0;
	for (int r = std::max(0, First); r < End && r < static_cast<int>(Old.size()) && r < static_cast<int>(Cells.size()); ++r) {
		if (Old[r].Instrument != NO_INSTRUMENT && !Old[r].IsEnd())
			Instrument = Old[r].Instrument;
		if (Cells[r].Instrument != NO_INSTRUMENT && !Cells[r].IsEnd())
			Playing = Cells[r].Instrument;
		if (!Old[r].HasNote() || !Cells[r].HasNote())
			continue;
		int Wanted = Instrument;
		const std::vector<int> Above = InstrumentChord(S, Instrument);
		if (!Above.empty()) {
			const int OldRoot = Old[r].Pitch(), NewRoot = Cells[r].Pitch();
			std::vector<int> Tones(1, 0);
			bool Same = true;
			for (size_t a = 0; a < Above.size(); ++a) {
				const int Moved = MapToKey(OldRoot + Above[a], FromKey, ToKey) - NewRoot;
				Same = Same && Moved == Above[a];
				Tones.push_back((Moved % 12 + 12) % 12);
			}
			const InstrumentInfo *pInfo = FindInstrument(S.Song, Instrument);
			const Preset *pSound = 0;
			int Pattern = BROKEN_UP, Frames = 2;
			if (Same)
				Wanted = Instrument;
			else if (pInfo == 0 || !ParseLushName(pInfo->Name, pSound, Pattern, Frames))
				++Unknown;
			else {
				std::sort(Tones.begin(), Tones.end());
				Tones.erase(std::unique(Tones.begin(), Tones.end()), Tones.end());
				const int Before = Count;
				Wanted = LushInstrument(S, Chip, Sawtooth, *pSound, LushShape(Chord(NewRoot % 12, Tones), Pattern), Pattern, Frames, Added,
					Count);
				if (Count > Before)
					pSoundAdded = pSound;
				++Changed;
			}
		}
		if (Wanted != Playing) {
			Cells[r].Instrument = static_cast<unsigned char>(Wanted);
			Playing = Wanted;
		}
	}
	if (Count > 0)
		S.Remarks.push_back(Format("Added %d lush chord instrument%s (%s, from the preset \"%s\"): %s.", Count, Count == 1 ? "" : "s",
			Chip.c_str(), pSoundAdded != 0 ? pSoundAdded->Name.c_str() : "lush chords", Added.c_str()));
	return Changed;
}

// An instrument for a part: the one named by the argument, else the preset
// (which is only added when it's used)
int PartInstrument(Session &S, const Json &Args, const char *Name, int Channel, const char *Preset)
{
	if (Arg(Args, Name) != 0)
		return InstrumentArg(S, Args, Name, Channel, NO_INSTRUMENT);
	return ResolveInstrument(S, Preset, Channel);
}

// Where a long melody note rubs a semitone against chords that were given
void RubWarnings(const Session &S, const std::vector<ChordAt> &Chords, const std::vector<NoteInfo> &Melody, const Key &K,
	std::vector<std::string> &Warnings)
{
	std::string Places;
	int Count = 0;
	for (size_t i = 0; i < Chords.size(); ++i) {
		const ChordAt &A = Chords[i];
		const std::vector<int> Classes = A.C.PitchClasses();
		// A melody note that is itself one of the chord's (the C# of a Dmaj7) is its color, not a rub
		std::vector<NoteInfo> Outside;
		for (size_t m = 0; m < Melody.size(); ++m)
			if (!A.C.HasPitchClass(Melody[m].Pitch % 12))
				Outside.push_back(Melody[m]);
		for (size_t t = 0; t < Classes.size(); ++t) {
			if (MelodyRub(Outside, A.Row, A.Row + A.Rows, Classes[t], S.Time.RowsPerBeat) < 1.0)
				continue;
			if (Count < 4)
				Places += Format("%sbar %d (%s)", Count == 0 ? "" : ", ", S.Time.BarOf(A.Row), ChordName(A.C, K).c_str());
			++Count;
			break;
		}
	}
	if (Count > 0)
		Warnings.push_back("A long melody note rubs a semitone against a chord tone in " + Places + (Count > 4 ? " and more" : "") +
			". Another chord there, or another melody note, would sound cleaner.");
}

// The notes the song's parts other than one play, as pitch classes, with
// the chords their 0xy arpeggios and arpeggio instruments sound
std::vector<HeardNote> HeardNotes(Session &S, int Except, int First, int End)
{
	std::vector<HeardNote> Out;
	for (int c = 0; c < S.ChannelCount(); ++c) {
		if (c == Except || !IsMelodic(S.Channel(c)))
			continue;
		const bool Bass = IsBass(S.Channel(c));
		const std::vector<NoteInfo> &Notes = S.Notes(c);
		int Arpeggio = 0;
		for (size_t i = 0; i < Notes.size(); ++i) {
			const NoteInfo &N = Notes[i];
			for (size_t e = 0; e < N.Effects.size(); ++e)
				if (N.Effects[e].first == 0 && N.Effects[e].second.first == EFF_ARPEGGIO)
					Arpeggio = N.Effects[e].second.second;
			if (N.Row >= End || N.Row + N.Rows <= First)
				continue;
			HeardNote H;
			H.Row = N.Row;
			H.Rows = N.Rows;
			H.PitchClass = N.Pitch % 12;
			H.Weight = std::max(0.3, N.Volume / 15.0);
			H.Bass = Bass;
			Out.push_back(H);
			const std::vector<int> Above = ChordAbove(S, N, Arpeggio);
			for (size_t a = 0; a < Above.size(); ++a) {
				H.Bass = false;
				H.PitchClass = (N.Pitch + Above[a]) % 12;
				Out.push_back(H);
			}
		}
	}
	return Out;
}

// The chords a part around a melody follows: the ones given (chords), the
// ones the song's other parts play when they cover most of the bars, or
// chords fitted to the melody. Source says which.
std::vector<ChordAt> ChordsForMelody(Session &S, const Json &Args, int Melody, const std::vector<BarSpan> &Bars, const Key &K,
	std::string &Source)
{
	std::vector<ChordAt> Chords;
	const std::vector<std::string> Progression = ProgressionArg(Args, "chords");
	if (!Progression.empty()) {
		std::string Error;
		if (!LayOutProgression(Progression, Bars, S.Time.RowsPerBeat, K, Chords, Error))
			throw Problem(Error);
		Source = "the chords given";
		return Chords;
	}
	const int First = Bars.front().Row, End = Bars.back().Row + Bars.back().Rows;
	Chords = HearChords(HeardNotes(S, Melody, First, End), Bars, S.Time.RowsPerBeat, K, true);
	int Covered = 0;
	for (size_t i = 0; i < Chords.size(); ++i)
		Covered += Chords[i].Rows;
	if (Covered * 2 >= End - First) {
		Source = "the chords the other parts play";
		return Chords;
	}
	std::vector<transcribe::MelodyNote> Line = MelodyOf(S.Notes(Melody), First, End);
	std::sort(Line.begin(), Line.end(), [](const transcribe::MelodyNote &a, const transcribe::MelodyNote &b) { return a.row < b.row; });
	std::vector<transcribe::Bar> TBars;
	for (size_t i = 0; i < Bars.size(); ++i) {
		transcribe::Bar B;
		B.row = Bars[i].Row;
		B.length = Bars[i].Rows;
		TBars.push_back(B);
	}
	transcribe::HarmonyOptions Options;
	Options.rowsPerBeat = S.Time.RowsPerBeat;
	Chords = ChordsOf(transcribe::ChooseChords(Line, TBars, HarmonyKey(K), Options));
	Source = "chords fitted to the melody";
	return Chords;
}

std::string AddBackingTool(Session &S, const Json &Args)
{
	const bool AsClip = BoolArg(Args, "as_clip", false);
	const bool DirectChange = Direct(S, AsClip);
	const int R = S.Time.RowsPerBeat;
	const std::vector<std::string> Progression = ProgressionArg(Args, "chords");
	const bool Given = !Progression.empty();
	const int Style = StyleArg(Args, "chord_style", ChordStyles(), CHORDS_ARPEGGIO);
	const int BassStyle = StyleArg(Args, "bass_style", BassStyles(), BASS_ROOT_FIFTH);

	// The melody: the chords are fitted to it, or with chords given it colors
	// them (then only a channel that plays in the bars counts as one)
	const bool Range = Arg(Args, "from_bar") != 0 || Arg(Args, "to_bar") != 0;
	int From = 1, To = 1;
	if (Range) {
		BarRange(S, Args, From, To, Given);
		if (Given && Arg(Args, "to_bar") == 0)
			To = std::max(To, std::min(S.MaxBars(), From + static_cast<int>(Progression.size()) - 1));
	}
	int Melody = -1;
	if (Arg(Args, "melody_channel") != 0) {
		Melody = ChannelArg(S, Args, "melody_channel");
		if (!IsMelodic(S.Channel(Melody)))
			throw Problem("The melody channel needs notes, not drums or samples.");
	}
	else {
		const int GuessFrom = Given && Range ? S.Time.BarStart(From) : 0, GuessEnd = Given && Range ? S.Time.BarEnd(To) : S.Time.TotalRows();
		Melody = GuessMelodyChannel(S, GuessFrom, GuessEnd);
		if (Given && Melody >= 0) {
			// Only chords play there (a groove with no melody yet): they're what the new chords replace
			const std::vector<NoteInfo> Notes = NotesIn(S.Notes(Melody), GuessFrom, GuessEnd);
			int Chords = 0;
			for (size_t i = 0; i < Notes.size(); ++i)
				Chords += SoundsChord(S, Notes[i]) ? 1 : 0;
			if (Chords * 3 >= static_cast<int>(Notes.size()))
				Melody = -1;
		}
		if (Melody >= 0) {
			// Nor is the top voice of voiced chords with no melody over them (it plays their sound, on any chip)
			const std::vector<int> Voices = ChordVoicesIn(S, GuessFrom, GuessEnd, -1);
			auto SoundOf = [&](int Channel) {
				const InstrumentInfo *pInfo = FindInstrument(S.Song, UsualInstrument(NotesIn(S.Notes(Channel), GuessFrom, GuessEnd)));
				return pInfo == 0 ? std::string() : Lowercase(pInfo->Name);
			};
			if (std::find(Voices.begin(), Voices.end(), Melody) != Voices.end()) {
				const std::string Mine = SoundOf(Melody);
				for (size_t v = 0; v < Voices.size() && Melody >= 0; ++v)
					if (Voices[v] != Melody && SoundOf(Voices[v]) == Mine)
						Melody = -1;
			}
		}
	}
	if (Melody < 0 && !Given)
		throw Problem("There's no melody to fit chords to yet. Give chords (like \"| C | Am | F | G |\") to write a backing without one.");

	// The bars: as asked, the melody's, or the progression's from bar 1
	std::vector<transcribe::MelodyNote> Line;
	if (Melody >= 0)
		Line = MelodyOf(S.Notes(Melody), 0, S.Time.TotalRows());
	std::vector<transcribe::Bar> Bars;
	if (Range) {
		Bars = transcribe::SongBars(S.Time.BarStart(From), S.Time.BarEnd(To), S.Time.PatternLength, S.Time.RowsPerBar);
		if (Melody >= 0)
			Line = MelodyOf(S.Notes(Melody), S.Time.BarStart(From), S.Time.BarEnd(To));
	}
	else if (!Line.empty())
		Bars = transcribe::MelodyBars(Line, S.Time.PatternLength, S.Time.RowsPerBar);
	else {
		const int To = std::min(S.MaxBars(), static_cast<int>(Progression.size()));
		Bars = transcribe::SongBars(0, S.Time.BarEnd(To), S.Time.PatternLength, S.Time.RowsPerBar);
	}
	if (!Given && (Line.empty() || Bars.empty()))
		throw Problem(Format("%s has no notes there to fit chords to.", S.Channel(Melody).FullName().c_str()));
	if (Bars.empty())
		throw Problem("There are no bars there.");
	const int First = Bars.front().row, End = Bars.back().row + Bars.back().length;
	const std::vector<BarSpan> Spans = BarSpans(Bars);

	// The channels: as asked ("none" leaves one out), the ones that play the
	// chords and the bass there already (new ones replace them rather than
	// stack a second part on them), or free ones
	const int OldChords = ChordPartIn(S, First, End, Melody), OldBass = BassPartIn(S, First, End, Melody);
	const std::vector<int> OldVoices = ChordVoicesIn(S, First, End, Melody);
	std::vector<int> Replaced;
	auto Pick = [&](const char *Name, bool (*Fits)(const ChannelInfo &), const char *Kind, int Playing) -> int {
		const Json *pValue = Arg(Args, Name);
		if (pValue != 0 && pValue->IsString() && Lowercase(pValue->String()) == "none")
			return -1;
		if (pValue != 0) {
			const int Channel = ParseChannel(S, *pValue, Name);
			if (!Fits(S.Channel(Channel)) || Channel == Melody)
				throw Problem(Format("%s needs %s other than the melody's.", Name, Kind));
			return Channel;
		}
		if (Playing >= 0 && Fits(S.Channel(Playing))) {
			Replaced.push_back(Playing);
			return Playing;
		}
		const int Free = FreeChannel(S, First, End, Melody, Fits);
		if (Free >= 0)
			return Free;
		if (Free == -1000)
			return -1;	// the song has none
		throw Problem(Format("Every %s already plays in those bars. Name one with %s (its notes there are replaced), or \"none\".",
			Kind, Name));
	};
	// Voiced chords take channels of their own: as asked, or free ones of the chip that plays chords
	// best (the VRC7's FM channels, the N163's, then the pulses)
	std::vector<int> VoiceChannels;
	if (Style == CHORDS_VOICES) {
		const Json *pVoices = Arg(Args, "chord_channels");
		if (pVoices != 0 && !(pVoices->IsString() && Lowercase(pVoices->String()) == "none")) {
			VoiceChannels = ChannelsArg(S, Args, "chord_channels");
			for (size_t i = 0; i < VoiceChannels.size(); ++i) {
				const ChannelInfo &Info = S.Channel(VoiceChannels[i]);
				if (!IsMelodic(Info) || Info.Kind == KIND_TRIANGLE || VoiceChannels[i] == Melody)
					throw Problem(Format("chord_channels: %s can't take a voice of the chords (pick pulses, FM or N163 channels other "
						"than the melody's).", Info.FullName().c_str()));
			}
			if (VoiceChannels.size() < 2 || VoiceChannels.size() > 4)
				throw Problem("chord_channels: voiced chords take 2 to 4 channels.");
		}
		else if (pVoices == 0 && !OldVoices.empty()) {
			VoiceChannels = OldVoices;	// the voices that play there already
			Replaced.insert(Replaced.end(), OldVoices.begin(), OldVoices.end());
		}
		else if (pVoices == 0) {
			// Free channels, or the one whose chords these replace
			for (int Group = 0; Group < 3 && VoiceChannels.size() < 2; ++Group) {
				VoiceChannels.clear();
				for (int c = 0; c < S.ChannelCount() && VoiceChannels.size() < 3; ++c) {
					const ChannelInfo &Info = S.Channel(c);
					const bool Fits = Group == 0 ? Info.Kind == KIND_FM : Group == 1 ? Info.Chip == "N163" : IsLushChordChannel(Info);
					if (Fits && c != Melody && (c == OldChords || !HasNotesIn(S, c, First, End)))
						VoiceChannels.push_back(c);
				}
			}
			if (OldChords >= 0 && std::find(VoiceChannels.begin(), VoiceChannels.end(), OldChords) != VoiceChannels.end())
				Replaced.push_back(OldChords);
			if (VoiceChannels.size() < 2)
				throw Problem("Voiced chords need two to four free channels; name them with chord_channels (their notes there are "
					"replaced).");
		}
	}
	const int ChordChannel = Style == CHORDS_VOICES ? (VoiceChannels.empty() ? -1 : VoiceChannels[0]) :
		Style == CHORDS_LUSH ? Pick("chord_channel", IsLushChordChannel, "a 2A03, MMC5 or VRC6 pulse channel", OldChords) :
		Pick("chord_channel", IsChordChannel, "a pulse channel", OldChords);
	const int BassChannel = Pick("bass_channel", IsBassChannel, "the triangle or the sawtooth", OldBass);
	if (ChordChannel < 0 && BassChannel < 0)
		throw Problem("There's no channel for chords or a bass line.");
	std::vector<std::string> Warnings;
	{
		// The old chords that still play under the new ones
		std::vector<int> Old = OldVoices;
		if (OldChords >= 0 && std::find(Old.begin(), Old.end(), OldChords) == Old.end())
			Old.push_back(OldChords);
		std::string Kept;
		int KeptCount = 0;
		for (size_t i = 0; i < Old.size(); ++i) {
			const int c = Old[i];
			const bool Writes = c == ChordChannel || std::find(VoiceChannels.begin(), VoiceChannels.end(), c) != VoiceChannels.end();
			if (Writes || c == BassChannel || (ChordChannel < 0 && VoiceChannels.empty()))
				continue;
			Kept += (Kept.empty() ? "" : ", ") + S.Channel(c).FullName();
			++KeptCount;
		}
		if (!Kept.empty())
			Warnings.push_back(Format("The chords on %s there still play under the new ones; clear %s (write_notes with an empty "
				"list) or name %s as the chord channel to replace them.", Kept.c_str(), KeptCount > 1 ? "them" : "it",
				KeptCount > 1 ? "one of them" : "it"));
	}

	// The key, and the chords
	bool Found;
	Key K = KeyArg(S, Args, "key", First, End, Found);
	if (!Found) {
		if (Given && WrittenAsDegrees(Progression))
			throw Problem("Chords written as degrees (I, vi, V7...) need the key: give key, like \"C major\".");
		K = Key(0, MODE_MAJOR);
	}
	const std::vector<NoteInfo> MelodyNotes = Melody >= 0 ? NotesIn(S.Notes(Melody), First, End) : std::vector<NoteInfo>();
	std::vector<ChordAt> Chords;
	if (Given) {
		std::string Error;
		if (!LayOutProgression(Progression, Spans, R, K, Chords, Error))
			throw Problem(Error);
	}
	else {
		const transcribe::Key HK = HarmonyKey(K);
		transcribe::HarmonyOptions Options;
		Options.rowsPerBeat = R;
		Options.halfBars = BoolArg(Args, "half_bars", true);
		std::sort(Line.begin(), Line.end(), [](const transcribe::MelodyNote &a, const transcribe::MelodyNote &b) { return a.row < b.row; });
		Chords = ChordsOf(transcribe::ChooseChords(Line, Bars, HK, Options));
		if (Chords.empty())
			throw Problem("The melody is too short to fit chords to.");
	}
	chord_color Color;
	if (!ParseColor(StringArg(Args, "color", Style == CHORDS_LUSH || Style == CHORDS_VOICES ? "rich" : "plain"), Color))
		throw Problem("color: plain (triads), sevenths or rich.");
	ColorChords(Chords, MelodyNotes, K, R, Color);
	if (Given && !MelodyNotes.empty())
		RubWarnings(S, Chords, MelodyNotes, K, Warnings);

	std::vector<Change> Changes;
	std::string Where;
	if (Style == CHORDS_VOICES && !VoiceChannels.empty()) {
		ChordPartOptions O;
		O.Style = Style;
		O.Rhythm = StyleArg(Args, "rhythm", ChordRhythms(), -1);
		O.RowsPerBeat = R;
		// Under the melody, a voice to a channel: well under its middle, and under the low notes it often dips to,
		// so the top voice doesn't blur it
		int Top = MelodyNotes.empty() ? 50 : MiddleOf(MelodyNotes, 57) - 9;
		if (!MelodyNotes.empty()) {
			std::vector<int> Pitches;
			for (size_t i = 0; i < MelodyNotes.size(); ++i)
				Pitches.push_back(MelodyNotes[i].Pitch);
			std::sort(Pitches.begin(), Pitches.end());
			Top = std::min(Top, Pitches[Pitches.size() / 5] - 4);
		}
		Top = std::max(43, std::min(55, Top));
		O.Low = Top - 10;
		O.High = Top + 3;
		O.RootInBass = BassChannel >= 0;
		for (int c = 0; c < S.ChannelCount() && !O.RootInBass; ++c)
			O.RootInBass = std::find(VoiceChannels.begin(), VoiceChannels.end(), c) == VoiceChannels.end() && IsBass(S.Channel(c)) &&
				HasNotesIn(S, c, First, End);
		O.Volume = Arg(Args, "chord_volume") != 0 ? IntArg(Args, "chord_volume", 10, 1, 15) : PartVolume(S.Channel(VoiceChannels[0]), 10);
		const std::vector<std::vector<PartNote> > Voices = VoicedChordParts(Chords, Spans, O, static_cast<int>(VoiceChannels.size()));
		// Strummed: each voice a frame or more after the one below it (Gxx), as a hand rolls a chord, within the row
		const int Strum = std::min(IntArg(Args, "strum", 1, 0, 3), std::max(0, (S.Song.Speed - 1) / std::max(1,
			static_cast<int>(VoiceChannels.size()) - 1)));
		std::string Names;
		for (size_t v = 0; v < VoiceChannels.size(); ++v) {
			const int c = VoiceChannels[v];
			const ChannelInfo &Info = S.Channel(c);
			const std::string Sound = Arg(Args, "chord_instrument") != 0 ? StringArg(Args, "chord_instrument") :
				Info.Kind == KIND_FM || Info.Chip == "N163" ? "electric piano" : "soft chords";
			const int Instrument = ResolveInstrument(S, Sound, c);
			std::vector<PartNote> Notes = Voices[v];
			for (size_t i = 0; i < Notes.size(); ++i) {
				Notes[i].Instrument = Instrument;
				if (Strum > 0 && v > 0)
					Notes[i].Effects.push_back(std::make_pair(static_cast<int>(EFF_DELAY), Strum * static_cast<int>(v)));
			}
			Change C;
			C.Channel = c;
			BuildPart(S.Cells(c), First, End, Notes, OptionsFor(S, c, DirectChange), C.Cells, Warnings);
			Changes.push_back(C);
			Names += (v == 0 ? "" : v + 1 == VoiceChannels.size() ? " and " : ", ") + S.Channel(c).FullName();
		}
		std::string Kind = "voices";
		if (O.Rhythm >= 0)
			Kind += std::string(" (") + ChordRhythms()[static_cast<size_t>(O.Rhythm)].Name + ")";
		Where = Kind + " chords on " + Names;
		if (Strum > 0)
			Where += Format(", strummed upward %s apart (Gxx)", Plural(Strum, "frame", "frames").c_str());
	}
	else if (ChordChannel >= 0) {
		const ChannelInfo &Info = S.Channel(ChordChannel);
		ChordPartOptions O;
		O.Style = Style;
		O.Rhythm = StyleArg(Args, "rhythm", ChordRhythms(), -1);
		O.Pattern = StyleArg(Args, "pattern", BrokenPatterns(), BROKEN_UP);
		O.StepRows = std::max(1, R / IntArg(Args, "notes_per_beat", 2, 1, 4));
		O.RowsPerBeat = R;
		ChordRegister(O, Info, MelodyNotes);
		O.RootInBass = BassChannel >= 0;
		for (int c = 0; c < S.ChannelCount() && !O.RootInBass; ++c)
			O.RootInBass = c != ChordChannel && IsBass(S.Channel(c)) && HasNotesIn(S, c, First, End);
		const int DefaultVolume = Style == CHORDS_STABS ? 11 : Style == CHORDS_BROKEN ? 10 : Style == CHORDS_LUSH ? 6 : 15;
		O.Volume = Arg(Args, "chord_volume") != 0 ? IntArg(Args, "chord_volume", DefaultVolume, 1, 15) : PartVolume(Info, DefaultVolume);
		const char *Sound = Style == CHORDS_GUIDE ? "warm pad" : Style == CHORDS_ARPEGGIO ? "soft chords" :
			Style == CHORDS_LUSH ? "lush chords" : "pluck";
		std::vector<PartNote> Notes = ChordPart(Chords, Spans, O);
		if (Style == CHORDS_LUSH) {
			const Json *pSound = Arg(Args, "chord_instrument");
			if (pSound != 0 && !pSound->IsString())
				throw Problem("chord_instrument: lush chords are made from a preset's sound, named like \"lush chords\" or \"pluck\".");
			LushInstruments(S, ChordChannel, StringArg(Args, "chord_instrument", Sound), O.Pattern, IntArg(Args, "arp_frames", 2, 1, 6),
				Notes);
		}
		else {
			const int Instrument = PartInstrument(S, Args, "chord_instrument", ChordChannel, Sound);
			for (size_t i = 0; i < Notes.size(); ++i)
				Notes[i].Instrument = Instrument;
		}
		Change C;
		C.Channel = ChordChannel;
		BuildPart(S.Cells(ChordChannel), First, End, Notes, OptionsFor(S, ChordChannel, DirectChange), C.Cells, Warnings);
		Changes.push_back(C);
		std::string Kind = ChordStyles()[static_cast<size_t>(Style)].Name;
		if (Style == CHORDS_BROKEN || (Style == CHORDS_LUSH && O.Pattern != BROKEN_UP))
			Kind += std::string(" (") + BrokenPatterns()[static_cast<size_t>(O.Pattern)].Name + ")";
		if (Style != CHORDS_BROKEN && O.Rhythm >= 0)
			Kind += std::string(" (") + ChordRhythms()[static_cast<size_t>(O.Rhythm)].Name + ")";
		Where = Kind + " chords on " + S.Channel(ChordChannel).FullName();
	}
	if (BassChannel >= 0) {
		const ChannelInfo &Info = S.Channel(BassChannel);
		BassOptions O;
		O.Style = BassStyle;
		O.RowsPerBeat = R;
		O.K = K;
		BassRegister(O, Info);
		O.Volume = 15;
		const int Instrument = PartInstrument(S, Args, "bass_instrument", BassChannel, "bass");
		std::vector<PartNote> Notes = BassPart(Chords, Spans, O);
		for (size_t i = 0; i < Notes.size(); ++i)
			Notes[i].Instrument = Instrument;
		Change C;
		C.Channel = BassChannel;
		BuildPart(S.Cells(BassChannel), First, End, Notes, OptionsFor(S, BassChannel, DirectChange), C.Cells, Warnings);
		Changes.push_back(C);
		Where += (Where.empty() ? "a " : " and a ") + std::string(BassStyles()[static_cast<size_t>(BassStyle)].Name) +
			" bass line on " + Info.FullName();
	}
	std::string What = Where + ", " + RangeText(S.Time.BarOf(First), S.Time.BarOf(End - 1)) + ", in " + KeyName(K);
	{
		// Which parts it took the place of
		bool Chords = false, Bass = false;
		for (size_t i = 0; i < Replaced.size(); ++i) {
			Bass = Bass || Replaced[i] == BassChannel;
			Chords = Chords || Replaced[i] != BassChannel;
		}
		if (Chords || Bass)
			What += Format(" (in place of the %s%s%s that played there)", Chords ? "chords" : "", Chords && Bass ? " and " : "",
				Bass ? "bass line" : "");
	}
	std::sort(Changes.begin(), Changes.end(), [](const Change &a, const Change &b) { return a.Channel < b.Channel; });
	const std::string Result = Commit(S, Changes, First, AsClip, StringArg(Args, "clip_name"), What);
	return WithRemarks(S, Result + "\nChords: " + ProgressionText(Chords, Spans, K) + "\nAs degrees of " + KeyName(K) + ": " +
		ProgressionText(Chords, Spans, K, true), Warnings);
}

bool SameKind(const ChannelInfo &a, const ChannelInfo &b)
{
	return a.Kind == b.Kind && InstrumentChip(a) == InstrumentChip(b);
}

// A channel for an echo of Source over [First, End): one of the same kind
// with nothing there, else another melodic one with nothing there; -1 if none
int EchoTarget(Session &S, int Source, int First, int End)
{
	for (int Pass = 0; Pass < 2; ++Pass) {
		for (int c = 0; c < S.ChannelCount(); ++c) {
			if (c == Source || !IsMelodic(S.Channel(c)) || IsBass(S.Channel(c)) != IsBass(S.Channel(Source)))
				continue;
			if (Pass == 0 && !SameKind(S.Channel(c), S.Channel(Source)))
				continue;
			if (!HasNotesIn(S, c, First, End))
				return c;
		}
	}
	return -1;
}

// The echo settings from an object (add_echo's arguments, or change_mood's echo)
struct EchoPlan {
	int Source, Target, Delay, Volume, Instrument;
	int Detune;		// fine pitch steps up (+) or down (-), 0 for none
	bool OtherChip;	// Instrument is the source's sound on the target's chip, picked by name
};

EchoPlan EchoArgs(Session &S, const Json &Args, int First, int End)
{
	EchoPlan Plan;
	Plan.Source = ChannelArg(S, Args, "channel");
	if (!IsMelodic(S.Channel(Plan.Source)))
		throw Problem("The echo copies a melodic channel's notes.");
	bool Exact;
	Plan.Delay = S.Time.BeatsToRows(NumberArg(Args, "delay_beats", 3.0 / S.Time.RowsPerBeat, 0.01, 16.0), Exact);
	Plan.Volume = IntArg(Args, "volume", 6, 1, 15);
	Plan.Detune = IntArg(Args, "detune", 0, -8, 8);
	if (Arg(Args, "to_channel") != 0) {
		Plan.Target = ChannelArg(S, Args, "to_channel");
		if (Plan.Target == Plan.Source || !IsMelodic(S.Channel(Plan.Target)))
			throw Problem("The echo's to_channel needs another melodic channel.");
	}
	else {
		Plan.Target = EchoTarget(S, Plan.Source, First + Plan.Delay, End + Plan.Delay);
		if (Plan.Target < 0)
			throw Problem("No free channel for the echo in those bars; name one with to_channel (its notes there are replaced).");
	}
	Plan.Instrument = Arg(Args, "instrument") != 0 ? InstrumentArg(S, Args, "instrument", Plan.Target, NO_INSTRUMENT) : NO_INSTRUMENT;
	Plan.OtherChip = false;
	if (Plan.Instrument == NO_INSTRUMENT && InstrumentChip(S.Channel(Plan.Target)) != InstrumentChip(S.Channel(Plan.Source))) {
		// The melody's own sound on the echo's chip, when it's a preset (or one of the VRC7's or N163's sounds)
		const InstrumentInfo *pInfo = FindInstrument(S.Song, UsualInstrument(NotesIn(S.Notes(Plan.Source), First, End)));
		if (pInfo != 0) {
			try {
				Plan.Instrument = ResolveInstrument(S, pInfo->Name, Plan.Target);
				Plan.OtherChip = true;
			}
			catch (const Problem &) {
				Plan.Instrument = NO_INSTRUMENT;
			}
		}
		if (Plan.Instrument == NO_INSTRUMENT)
			throw Problem(Format("%s needs %s instruments; give the echo an instrument (a preset works).",
				S.Channel(Plan.Target).FullName().c_str(), InstrumentChip(S.Channel(Plan.Target)).c_str()));
	}
	return Plan;
}

// The echo of the source's notes that start in [First, End) in the draft:
// Delay rows later on the target, quieter (Volume at full loudness, following
// the original's), replacing what the target plays there. It stops at the
// song's end rather than making the song longer (which would change where it
// loops). How many notes.
// A sticky effect's parameter in effect as each note starts (0 when it's off)
std::vector<int> StickyAt(const std::vector<Cell> &Cells, const std::vector<NoteInfo> &Notes, int Columns, int Effect)
{
	std::vector<int> Out(Notes.size(), 0);
	int Current = 0;
	size_t n = 0;
	for (size_t r = 0; r < Cells.size() && n < Notes.size(); ++r) {
		for (int k = 0; k < Columns && k < EFFECT_COLUMNS; ++k)
			if (Cells[r].Effect[k] == Effect)
				Current = Cells[r].Param[k];
		while (n < Notes.size() && static_cast<int>(r) == Notes[n].Row)
			Out[n++] = Current;
	}
	return Out;
}

// Echo notes that wouldn't grate: one a semitone (or a major seventh) from a
// note another part plays as it starts is left out, one another part moves
// against on its way is cut short there. Dropped and Shortened count them.
std::vector<PartNote> EchoWithoutRubs(Session &S, Draft &D, int Target, const std::vector<PartNote> &Notes, int &Dropped,
	int &Shortened)
{
	struct Other { int Row, End; std::vector<int> Pitches; };
	std::vector<Other> Others;
	for (int c = 0; c < S.ChannelCount(); ++c) {
		if (c == Target || !IsMelodic(S.Channel(c)))
			continue;
		const std::vector<NoteInfo> Theirs = D.Notes(c);
		const std::vector<int> Arps = StickyAt(D.Cells(c), Theirs, S.Channel(c).EffectColumns, EFF_ARPEGGIO);
		for (size_t i = 0; i < Theirs.size(); ++i) {
			Other o;
			o.Row = Theirs[i].Row;
			o.End = Theirs[i].Row + Theirs[i].Rows;
			o.Pitches.push_back(Theirs[i].Pitch);
			const std::vector<int> Above = ChordAbove(S, Theirs[i], Arps[i]);
			for (size_t a = 0; a < Above.size(); ++a)
				o.Pitches.push_back(Theirs[i].Pitch + Above[a]);
			Others.push_back(o);
		}
	}
	Dropped = Shortened = 0;
	std::vector<PartNote> Out;
	for (size_t i = 0; i < Notes.size(); ++i) {
		PartNote E = Notes[i];
		int CutAt = E.Row + E.Rows;
		// A note the harmony sounds as the echo starts (the F# of a Gmaj7 over its G bass) doesn't rub
		bool Sounded = false;
		for (size_t k = 0; k < Others.size() && !Sounded; ++k) {
			const Other &o = Others[k];
			if (o.Row <= E.Row && E.Row < o.End && o.Pitches.size() > 1)
				for (size_t p = 0; p < o.Pitches.size(); ++p)
					Sounded = Sounded || (E.Pitch - o.Pitches[p]) % 12 == 0;
		}
		if (Sounded) {
			Out.push_back(E);
			continue;
		}
		for (size_t k = 0; k < Others.size(); ++k) {
			const Other &o = Others[k];
			if (o.Row >= E.Row + E.Rows || o.End <= E.Row)
				continue;
			// A note of the other part's chord doesn't rub against it (the F# of a Gmaj7)
			bool Rub = false, InChord = false;
			for (size_t p = 0; p < o.Pitches.size(); ++p) {
				const int d = ((E.Pitch - o.Pitches[p]) % 12 + 12) % 12;
				Rub = Rub || d == 1 || d == 11;
				InChord = InChord || (d == 0 && o.Pitches.size() > 1);
			}
			Rub = Rub && !InChord;
			if (Rub)
				CutAt = std::min(CutAt, std::max(o.Row, E.Row));
		}
		if (CutAt <= E.Row) {
			++Dropped;
			continue;
		}
		if (CutAt < E.Row + E.Rows) {
			E.Rows = CutAt - E.Row;
			++Shortened;
		}
		Out.push_back(E);
	}
	return Out;
}

int MakeEcho(Session &S, Draft &D, const EchoPlan &Plan, int First, int End, std::vector<std::string> &Warnings)
{
	const int Stop = std::min(End + Plan.Delay, std::max(End, S.Time.TotalRows()));
	const std::vector<NoteInfo> Played = D.Notes(Plan.Source);
	std::vector<PartNote> Notes;
	for (size_t i = 0; i < Played.size(); ++i) {
		const NoteInfo &N = Played[i];
		if (N.Row < First || N.Row >= End || N.Row + Plan.Delay >= Stop)
			continue;
		PartNote E;
		E.Row = N.Row + Plan.Delay;
		E.Rows = std::min(N.Rows, Stop - E.Row);
		E.Pitch = N.Pitch;
		E.Instrument = Plan.Instrument != NO_INSTRUMENT ? Plan.Instrument : N.Instrument;
		E.Volume = MatchVolume(S.Channel(Plan.Source), N.Volume, Plan.Volume / 15.0, S.Channel(Plan.Target));
		for (size_t e = 0; e < N.Effects.size(); ++e) {
			if (N.Effects[e].first == 0)
				E.Effects.push_back(N.Effects[e].second);
			else if (N.Effects[e].first < E.Rows)
				E.LaterEffects.push_back(N.Effects[e]);
		}
		Notes.push_back(E);
	}
	if (Notes.empty())
		return 0;
	for (size_t i = 0; i + 1 < Notes.size(); ++i)
		Notes[i].Rows = std::min(Notes[i].Rows, Notes[i + 1].Row - Notes[i].Row);
	int Dropped, Shortened;
	Notes = EchoWithoutRubs(S, D, Plan.Target, Notes, Dropped, Shortened);
	if (Dropped > 0 || Shortened > 0)
		Warnings.push_back(Format("Where the echo would grate a semitone against another part, %s%s%s.",
			Dropped > 0 ? (Plural(Dropped, "echo note was", "echo notes were") + " left out").c_str() : "",
			Dropped > 0 && Shortened > 0 ? " and " : "",
			Shortened > 0 ? (Plural(Shortened, "was", "were") + " cut short").c_str() : ""));
	if (Notes.empty())
		return 0;
	if (Plan.Detune != 0) {
		// A little off the melody's pitch, so the two shimmer where they overlap; back in tune after
		Notes.front().Effects.push_back(std::make_pair(static_cast<int>(EFF_PITCH), 0x80 + Plan.Detune));
		Notes.back().EndEffects.push_back(std::make_pair(static_cast<int>(EFF_PITCH), 0x80));
	}
	if (HasNotesIn(S, Plan.Target, First + Plan.Delay, Stop))
		Warnings.push_back(Format("%s's own notes in those bars were replaced by the echo.", S.Channel(Plan.Target).FullName().c_str()));
	PartOptions Options = OptionsFor(S, Plan.Target, false);
	Options.KeepFollowing = false;	// the draft's comparison with the song takes care of what follows
	Options.StopAtEnd = Stop < End + Plan.Delay;	// at the song's end, the last row ends it
	std::vector<Cell> Out;
	BuildPart(D.Cells(Plan.Target), First + Plan.Delay, Stop, Notes, Options, Out, Warnings);
	D.Put(Plan.Target, First + Plan.Delay, Out);
	return static_cast<int>(Notes.size());
}

std::string AddEchoTool(Session &S, const Json &Args)
{
	int From, To;
	BarRange(S, Args, From, To);
	const int First = S.Time.BarStart(From), End = S.Time.BarEnd(To);
	const bool AsClip = BoolArg(Args, "as_clip", false);
	const EchoPlan Plan = EchoArgs(S, Args, First, End);
	std::vector<std::string> Warnings;
	Draft D(S);
	if (MakeEcho(S, D, Plan, First, End, Warnings) == 0)
		throw Problem(Format("%s has no notes in %s to echo.", S.Channel(Plan.Source).FullName().c_str(), RangeText(From, To).c_str()));
	std::vector<Change> Changes;
	int Start;
	if (!D.Collect(First + Plan.Delay, Direct(S, AsClip), Changes, Start, Warnings))
		throw Problem("The echo is already there.");
	// Where a chip counts volume otherwise (the VRC7 in 3 dB steps, the N163 quieter), the number written differs
	std::string Level = Format("at volume %d", Plan.Volume);
	const ChannelInfo &Source = S.Channel(Plan.Source), &Target = S.Channel(Plan.Target);
	if (Source.Kind == KIND_FM || Target.Kind == KIND_FM || Source.Chip == "N163" || Target.Chip == "N163") {
		int Loudest = 0;
		const std::vector<NoteInfo> Played = NotesIn(S.Notes(Plan.Source), First, End);
		for (size_t i = 0; i < Played.size(); ++i)
			Loudest = std::max(Loudest, Played[i].Volume);
		if (Loudest > 0)
			Level += Format(" (as much softer as %d is than 15 on a pulse: %d where the melody plays %d)", Plan.Volume,
				MatchVolume(Source, Loudest, Plan.Volume / 15.0, Target), Loudest);
	}
	std::string What = Format("an echo of %s on %s, %s later %s%s, %s", Source.FullName().c_str(), Target.FullName().c_str(),
		S.Time.Length(Plan.Delay).c_str(), Level.c_str(), Plan.Detune == 0 ? "" : Format(" detuned P%02X", 0x80 + Plan.Detune).c_str(),
		RangeText(From, To).c_str());
	if (Plan.OtherChip)
		What += Format(" (playing the melody's sound on the %s: %s)", InstrumentChip(S.Channel(Plan.Target)).c_str(),
			InstrumentLabel(S.Song, Plan.Instrument).c_str());
	return WithRemarks(S, Commit(S, Changes, Start, AsClip, StringArg(Args, "clip_name"), What), Warnings);
}

// Mixing

SongFlow Flow(Session &S);

// How many rows later Channel copies Source's notes in [First, End), as an
// echo does (most of its notes Source's pitches that many rows on); 0 if not
int EchoDelayOf(Session &S, int Channel, int Source, int First, int End)
{
	const std::vector<NoteInfo> &Mine = S.Notes(Channel), &Theirs = S.Notes(Source);
	std::map<int, int> Pitches;
	for (size_t i = 0; i < Theirs.size(); ++i)
		if (Theirs[i].Row < End)
			Pitches[Theirs[i].Row] = Theirs[i].Pitch;
	int Count = 0;
	for (size_t i = 0; i < Mine.size(); ++i)
		if (Mine[i].Row >= First && Mine[i].Row < End)
			++Count;
	if (Count < 4)
		return 0;
	int Best = 0, BestHits = 0;
	for (int Delay = 1; Delay <= 3 * S.Time.RowsPerBeat; ++Delay) {
		int Hits = 0;
		for (size_t i = 0; i < Mine.size(); ++i) {
			if (Mine[i].Row < First || Mine[i].Row >= End)
				continue;
			std::map<int, int>::const_iterator it = Pitches.find(Mine[i].Row - Delay);
			if (it != Pitches.end() && it->second == Mine[i].Pitch)
				++Hits;
		}
		if (Hits > BestHits) {
			BestHits = Hits;
			Best = Delay;
		}
	}
	return BestHits * 2 >= Count ? Best : 0;
}

enum mix_role { ROLE_NONE, ROLE_MELODY, ROLE_ECHO, ROLE_CHORDS, ROLE_LINE, ROLE_BASS, ROLE_DRUMS };

// What a channel plays in [First, End), for mixing
int MixRole(Session &S, int Channel, int Melody, int First, int End)
{
	const ChannelInfo &Info = S.Channel(Channel);
	if (Info.Kind == KIND_DPCM || Info.Kind == KIND_TRIANGLE || !HasNotesIn(S, Channel, First, End))
		return ROLE_NONE;	// no volume of their own (the DPCM level softens the triangle)
	if (Info.Kind == KIND_NOISE)
		return ROLE_DRUMS;
	if (Channel == Melody)
		return ROLE_MELODY;
	if (Melody >= 0 && EchoDelayOf(S, Channel, Melody, First, End) > 0)
		return ROLE_ECHO;
	if (IsBass(Info))
		return ROLE_BASS;
	const std::vector<NoteInfo> &Notes = S.Notes(Channel);
	for (size_t i = 0; i < Notes.size(); ++i)
		if (Notes[i].Row >= First && Notes[i].Row < End && SoundsChord(S, Notes[i]))
			return ROLE_CHORDS;
	return ROLE_LINE;
}

const char *RoleName(int Role)
{
	switch (Role) {
	case ROLE_MELODY: return "melody";
	case ROLE_ECHO: return "echo";
	case ROLE_CHORDS: return "chords";
	case ROLE_LINE: return "a second line";
	case ROLE_BASS: return "bass";
	case ROLE_DRUMS: return "drums";
	}
	return "";
}

// The triangle's and the noise's loudness at a DPCM level, against level 0:
// the NES mixes the three together, and the triangle's swing shrinks as the
// DPCM's output rises
double TriangleShare(int Level)
{
	auto Mix = [](double Triangle, double Dpcm) {
		const double Sum = Triangle / 8227.0 + Dpcm / 22638.0;
		return Sum <= 0.0 ? 0.0 : 159.79 / (1.0 / Sum + 100.0);
	};
	const double Full = Mix(15.0, 0.0);
	return (Mix(15.0, Level) - Mix(0.0, Level)) / Full;
}

std::string MixTool(Session &S, const Json &Args)
{
	if (!S.Song.DirectChanges)
		throw Problem("Mixing changes the volumes of the whole song, which needs direct changes; the user can allow them in "
			"File > Connect Claude Code.");
	const std::string Style = Squeezed(StringArg(Args, "style", Arg(Args, "levels") != 0 || Arg(Args, "dac") != 0 ? "" : "soft"));
	if (!Style.empty() && Style != "soft" && Style != "full" && Style != "quiet" && Style != "loud" && Style != "atmospheric")
		throw Problem("style: soft (quiet pulses under a warm triangle, as atmospheric tracks mix) or full (everything near "
			"full volume).");
	const bool Soft = Style == "soft" || Style == "quiet" || Style == "atmospheric";
	int From = 1, To = S.Time.BarCount();
	if (Arg(Args, "from_bar") != 0 || Arg(Args, "to_bar") != 0)
		BarRange(S, Args, From, To);
	const int First = S.Time.BarStart(From), End = std::min(S.Time.TotalRows(), S.Time.BarEnd(To));
	const int Melody = Arg(Args, "melody_channel") != 0 ? ChannelArg(S, Args, "melody_channel") : GuessMelodyChannel(S, First, End);

	// The loudest volume each channel gets: by its role for a style, as loud on its chip as that
	// volume on a pulse (a chip too quiet for its parts' levels plays the loudest at 15 and the rest as
	// far under it), or as given
	std::vector<int> Target(static_cast<size_t>(S.ChannelCount()), -1), Roles(static_cast<size_t>(S.ChannelCount()), ROLE_NONE);
	std::vector<double> Level(static_cast<size_t>(S.ChannelCount()), 0.0);
	std::map<std::string, double> Short;	// per chip: how far its loudest part falls short of its level at 15
	for (int c = 0; c < S.ChannelCount(); ++c) {
		Roles[static_cast<size_t>(c)] = MixRole(S, c, Melody, First, End);
		if (Style.empty() || Roles[static_cast<size_t>(c)] == ROLE_NONE)
			continue;
		static const int SOFT[] = { -1, 9, 4, 6, 7, 10, 10 }, FULL[] = { -1, 15, 6, 12, 13, 15, 15 };
		const int AsPulse = (Soft ? SOFT : FULL)[Roles[static_cast<size_t>(c)]];
		Level[static_cast<size_t>(c)] = 20.0 * std::log10(AsPulse / 15.0);
		const double Over = Level[static_cast<size_t>(c)] - LevelDb(S.Channel(c), 15);
		double &Most = Short[S.Channel(c).Chip];
		Most = std::max(Most, Over);
		Target[static_cast<size_t>(c)] = AsPulse;
	}
	bool Fm = false, Quiet = false;
	for (int c = 0; c < S.ChannelCount(); ++c) {
		if (Target[static_cast<size_t>(c)] < 1)
			continue;
		const ChannelInfo &Info = S.Channel(c);
		if (Info.Kind == KIND_FM || Info.Chip == "N163") {
			Target[static_cast<size_t>(c)] = VolumeForDb(Info, Level[static_cast<size_t>(c)] - Short[Info.Chip]);
			Fm = Fm || Info.Kind == KIND_FM;
			Quiet = Quiet || Info.Chip == "N163";
		}
	}
	const Json *pLevels = Arg(Args, "levels");
	if (pLevels != 0) {
		if (!pLevels->IsObject())
			throw Problem("levels: an object of channels and their loudest volume, like {\"VRC6 Pulse 1\": 9, \"Noise\": 10}.");
		for (size_t i = 0; i < pLevels->Members().size(); ++i) {
			const int c = ParseChannel(S, Json(pLevels->Members()[i].first), "levels");
			const Json &Value = pLevels->Members()[i].second;
			if (!Value.IsNumber() || Value.Number() < 1 || Value.Number() > 15)
				throw Problem("levels: each channel's loudest volume is 1-15.");
			if (S.Channel(c).Kind == KIND_TRIANGLE || S.Channel(c).Kind == KIND_DPCM)
				throw Problem(Format("%s has no volume of its own; dac softens the triangle (and the noise).", S.Channel(c).FullName().c_str()));
			Target[static_cast<size_t>(c)] = static_cast<int>(Value.Number());
		}
	}
	// The DPCM level: soft mixes soften the triangle and the noise a little
	int Dac = Soft ? 0x30 : (Style == "full" || Style == "loud") ? 0 : -1;
	const Json *pDac = Arg(Args, "dac");
	if (pDac != 0) {
		if (pDac->IsNumber())
			Dac = IntArg(Args, "dac", 0, 0, 127);
		else {
			const std::string Word = Squeezed(StringArg(Args, "dac"));
			if (Word == "full" || Word == "off" || Word == "zero")
				Dac = 0;
			else if (Word == "warm" || Word == "light")
				Dac = 0x30;
			else if (Word == "soft" || Word == "medium")
				Dac = 0x50;
			else if (Word == "softest" || Word == "strong" || Word == "max")
				Dac = 0x7F;
			else if (Word == "keep" || Word == "none" || Word == "leave")
				Dac = -1;
			else
				throw Problem("dac: full (Z00), warm (Z30), soft (Z50), softest (Z7F), keep, or a level 0-127.");
		}
	}

	Draft D(S);
	std::vector<std::string> Done;
	for (int c = 0; c < S.ChannelCount(); ++c) {
		const int Want = Target[static_cast<size_t>(c)];
		if (Want < 1)
			continue;
		const std::vector<NoteInfo> &Notes = S.Notes(c);
		int Loudest = 0;
		for (size_t i = 0; i < Notes.size(); ++i)
			if (Notes[i].Row >= First && Notes[i].Row < End)
				Loudest = std::max(Loudest, Notes[i].Volume);
		if (Loudest <= 0)
			continue;
		const std::string Label = S.Channel(c).FullName() + (Roles[static_cast<size_t>(c)] != ROLE_NONE ?
			std::string(" (") + RoleName(Roles[static_cast<size_t>(c)]) + ")" : std::string());
		if (Loudest == Want) {
			Done.push_back(Format("%s stays at %d", Label.c_str(), Want));
			continue;
		}
		std::vector<Cell> &Cells = D.Cells(c);
		// Each volume as much under the loudest as it was (the VRC7 counts 3 dB a step, the others in proportion)
		const ChannelInfo &Info = S.Channel(c);
		auto Scaled = [&](int Volume) {
			return Volume <= 0 ? 0 : VolumeForDb(Info, LevelDb(Info, Volume) + LevelDb(Info, Want) - LevelDb(Info, Loudest));
		};
		bool FirstNote = true;
		const int Entering = StateAt(Cells, First, S.Channel(c).EffectColumns).Volume;
		for (int r = First; r < End && r < static_cast<int>(Cells.size()); ++r) {
			Cell &C = Cells[static_cast<size_t>(r)];
			if (C.Volume < NO_VOLUME)
				C.Volume = static_cast<unsigned char>(Scaled(C.Volume));
			else if (FirstNote && C.HasNote())
				C.Volume = static_cast<unsigned char>(Scaled(Entering));
			if (C.HasNote())
				FirstNote = false;
		}
		Done.push_back(Format("%s loudest %d to %d", Label.c_str(), Loudest, Want));
	}

	// The level at the start and where the loop comes back
	std::string DacText, DacWarning;
	if (Dac >= 0) {
		int Dpcm = -1;
		for (int c = 0; c < S.ChannelCount(); ++c)
			if (S.Channel(c).Kind == KIND_DPCM)
				Dpcm = c;
		if (Dpcm < 0)
			throw Problem("The song has no DPCM channel to set the level on.");
		std::vector<int> Rows(1, First);
		const SongFlow Flow_ = Flow(S);
		const int Loop = Flow_.LoopRow(S.Time.PatternLength);
		if (Loop > First && Loop < End)
			Rows.push_back(Loop);
		// And on every sample the channel plays: a sample can start from a level of its own (the sampled drums
		// start from silence) and leave the channel there, which would undo the level until the loop comes round
		std::vector<int> Hits;
		if (Dac > 0) {
			const std::vector<NoteInfo> Played = NotesIn(S.Notes(Dpcm), First, End);
			for (size_t i = 0; i < Played.size(); ++i)
				if (std::find(Rows.begin(), Rows.end(), Played[i].Row) == Rows.end())
					Hits.push_back(Played[i].Row);
		}
		const size_t Marks = Rows.size();
		Rows.insert(Rows.end(), Hits.begin(), Hits.end());
		std::vector<Cell> &Cells = D.Cells(Dpcm);
		int Missed = 0;
		for (size_t i = 0; i < Rows.size(); ++i) {
			Cell &C = Cells[static_cast<size_t>(Rows[i])];
			bool Placed = false;
			for (int k = 0; k < EFFECT_COLUMNS && !Placed; ++k)
				if (C.Effect[k] == EFF_DAC) {
					C.Param[k] = static_cast<unsigned char>(Dac);
					Placed = true;
				}
			if (!Placed && !AddEffect(C, EFF_DAC, Dac, S.Channel(Dpcm).EffectColumns)) {
				if (i >= Marks) {
					++Missed;
					continue;
				}
				throw Problem(Format("The DPCM channel has no free effect column at %s for the level (all four are full).",
					S.Time.Position(Rows[i]).c_str()));
			}
		}
		if (Missed > 0)
			DacWarning = Format("%s had no free effect column for the level (all four were full), so the level "
				"goes back to the sample's own after %s.", Plural(Missed, "sample", "samples").c_str(), Missed == 1 ? "it" : "them");
		const int Share = static_cast<int>(std::floor(100.0 * TriangleShare(Dac) + 0.5));
		DacText = Format("the DPCM level Z%02X at %s%s%s, so the triangle and the noise play at about %d%%", Dac,
			S.Time.Position(First).c_str(), Marks > 1 ? (" and where the loop starts (" + S.Time.Position(Rows[1]) + ")").c_str() : "",
			Hits.empty() ? "" : (" and with each of its " + Plural(static_cast<int>(Hits.size()), "sample", "samples") +
				" (which would reset it)").c_str(), Share);
		if (HasNotesIn(S, Dpcm, 0, S.Time.TotalRows()) && Dac > 0x40)
			DacText += " (samples on the DPCM channel start from that level: at a high one they have less room and can clip; "
				"Z30-Z40 suits songs with samples)";
	}

	std::vector<Change> Changes;
	std::vector<std::string> Warnings;
	if (!DacWarning.empty())
		Warnings.push_back(DacWarning);
	int Start = First;
	std::string What = Soft ? "a soft mix" : (Style == "full" || Style == "loud") ? "a full mix" : "the mix";
	What += ", " + RangeText(From, To) + ": ";
	for (size_t i = 0; i < Done.size(); ++i)
		What += (i == 0 ? "" : "; ") + Done[i];
	if (!DacText.empty())
		What += (Done.empty() ? "" : "; ") + DacText;
	if (!D.Collect(First, true, Changes, Start, Warnings, End))
		return "The song is mixed that way already: " + What.substr(What.find(": ") + 2) + ".";
	std::string Text = Commit(S, Changes, Start, false, "", What);
	if (Soft)
		Text += " Soft pulses leave the low end to the triangle and make the sound warmer; turn the playback volume up rather "
			"than the pulses. Volumes the tools write later start from the song's own, so mix again at the end.";
	if (Fm && !Style.empty())
		Text += " The VRC7 plays louder than the pulses and counts its volume in steps of 3 dB, so its parts sit nearer 15 "
			"for the same balance (9 there is 18 dB under 15).";
	if (Quiet && !Style.empty())
		Text += " The N163's wave sounds play quieter than the pulses, so its parts sit nearer 15 for the same balance.";
	return WithRemarks(S, Text, Warnings);
}

int LevelArg(const Json &Args, const char *Name, int Default, const char *const *Words, const int *Values, int Count)
{
	const Json *pValue = Arg(Args, Name);
	if (pValue == 0)
		return Default;
	if (pValue->IsString()) {
		const std::string Text = Lowercase(Trim(pValue->String()));
		for (int i = 0; i < Count; ++i)
			if (Text == Words[i])
				return Values[i];
	}
	return IntArg(Args, Name, Default, 0, 15);
}

// The vibrato settings from an object (add_vibrato's arguments, or change_mood's vibrato)
struct VibratoPlan {
	int Channel, Speed, Depth, MinRows, Delay;
};

VibratoPlan VibratoArgs(Session &S, const Json &Args)
{
	static const char *const SPEEDS[] = { "slow", "medium", "fast" };
	static const int SPEED_VALUES[] = { 3, 5, 8 };
	static const char *const DEPTHS[] = { "none", "gentle", "medium", "strong" };
	static const int DEPTH_VALUES[] = { 0, 2, 4, 7 };
	VibratoPlan Plan;
	Plan.Channel = ChannelArg(S, Args, "channel");
	if (!IsMelodic(S.Channel(Plan.Channel)))
		throw Problem("Vibrato is for melodic channels.");
	Plan.Speed = std::max(1, LevelArg(Args, "speed", 5, SPEEDS, SPEED_VALUES, 3));
	Plan.Depth = LevelArg(Args, "depth", 2, DEPTHS, DEPTH_VALUES, 4);
	bool Exact;
	Plan.MinRows = S.Time.BeatsToRows(NumberArg(Args, "min_beats", 1.0, 0.01, 64.0), Exact);
	Plan.Delay = S.Time.BeatsToRows(NumberArg(Args, "delay_beats", 0.25, 0.01, 16.0), Exact);
	return Plan;
}

// Vibrato in the draft; how many notes it changed
int MakeVibrato(Session &S, Draft &D, const VibratoPlan &Plan, int First, int End, bool &Lost, bool SkipBuiltIn = false)
{
	const int Columns = S.Channel(Plan.Channel).EffectColumns;
	std::vector<Cell> &Cells = D.Cells(Plan.Channel);
	std::vector<NoteInfo> Notes = D.Notes(Plan.Channel);
	if (SkipBuiltIn) {
		// Notes whose instrument has a vibrato of its own keep just that one
		std::vector<NoteInfo> Plain;
		for (size_t i = 0; i < Notes.size(); ++i) {
			const InstrumentInfo *pInfo = FindInstrument(S.Song, Notes[i].Instrument);
			if (pInfo == 0 || !pInfo->Vibrato)
				Plain.push_back(Notes[i]);
		}
		Notes = Plain;
	}
	const int Count = ApplyVibrato(Cells, Notes, First, End, Plan.Speed, Plan.Depth, Plan.MinRows, Plan.Delay, Columns, Lost);
	if (Plan.Depth == 0 && First < static_cast<int>(Cells.size())) {
		// A vibrato still on from before the bars stops too
		const ChannelState State = StateAt(Cells, First, Columns);
		if (State.Sticky[EFF_VIBRATO] != 0 && !HasEffect(Cells[First], EFF_VIBRATO, Columns) && !AddEffect(Cells[First], EFF_VIBRATO, 0, Columns))
			Lost = true;
	}
	return Count;
}

std::string AddVibratoTool(Session &S, const Json &Args)
{
	const VibratoPlan Plan = VibratoArgs(S, Args);
	int From, To;
	BarRange(S, Args, From, To);
	const int First = S.Time.BarStart(From), End = S.Time.BarEnd(To);
	const bool AsClip = BoolArg(Args, "as_clip", false);
	bool Lost = false;
	Draft D(S);
	const int Count = MakeVibrato(S, D, Plan, First, End, Lost);
	std::vector<std::string> Warnings;
	if (Lost)
		Warnings.push_back("Some notes had no free effect column for the vibrato (all four were full).");
	std::vector<Change> Changes;
	int Start;
	if (Count == 0 || !D.Collect(First, Direct(S, AsClip), Changes, Start, Warnings))
		throw Problem(Plan.Depth == 0 ? "There's no vibrato to take off there." :
			Format("No notes there are long enough for vibrato (at least %s).", S.Time.Length(Plan.MinRows).c_str()));
	const std::string What = Plan.Depth == 0 ?
		Format("vibrato taken off %s, %s", S.Channel(Plan.Channel).FullName().c_str(), RangeText(From, To).c_str()) :
		Format("vibrato 4%X%X on the longer notes of %s, %s", Plan.Speed, Plan.Depth, S.Channel(Plan.Channel).FullName().c_str(),
			RangeText(From, To).c_str());
	return WithRemarks(S, Commit(S, Changes, Start, AsClip, StringArg(Args, "clip_name"), What) +
		Format(" (%s)", Plural(Count, "note", "notes").c_str()), Warnings);
}

// Drums, harmony, counter-melodies and finishing touches

// The bars the song's melodic parts play in, first to last; the whole song when none do
void PlayedBars(Session &S, int &From, int &To)
{
	int First = -1, Last = -1;
	for (int c = 0; c < S.ChannelCount(); ++c) {
		if (!IsMelodic(S.Channel(c)))
			continue;
		const std::vector<NoteInfo> &Notes = S.Notes(c);
		for (size_t i = 0; i < Notes.size(); ++i) {
			if (First < 0 || Notes[i].Row < First)
				First = Notes[i].Row;
			Last = std::max(Last, Notes[i].Row + std::max(1, Notes[i].Rows) - 1);
		}
	}
	if (First < 0) {
		From = 1;
		To = S.Time.BarCount();
		return;
	}
	From = S.Time.BarOf(First);
	To = S.Time.BarOf(std::min(Last, S.Time.TotalRows() - 1));
}

std::string AddDrumsTool(Session &S, const Json &Args)
{
	const bool AsClip = BoolArg(Args, "as_clip", false);
	int Channel = -1;
	if (Arg(Args, "channel") != 0) {
		Channel = ChannelArg(S, Args, "channel");
		if (S.Channel(Channel).Kind != KIND_NOISE)
			throw Problem("Drums go on the noise channel.");
	}
	for (int c = 0; c < S.ChannelCount() && Channel < 0; ++c)
		if (S.Channel(c).Kind == KIND_NOISE)
			Channel = c;
	if (Channel < 0)
		throw Problem("The song has no noise channel.");
	int From, To;
	if (Arg(Args, "from_bar") != 0 || Arg(Args, "to_bar") != 0)
		BarRange(S, Args, From, To, true);
	else
		PlayedBars(S, From, To);
	const int First = S.Time.BarStart(From), End = S.Time.BarEnd(To);
	const int R = S.Time.RowsPerBeat;

	DrumOptions O;
	O.Groove = StyleArg(Args, "groove", Grooves(), S.Time.RowsPerBar == 3 * R ? GROOVE_WALTZ : GROOVE_BASIC);
	static const char *const LEVELS[] = { "light", "normal", "busy", "soft", "medium", "intense" };
	static const int LEVEL_VALUES[] = { 1, 2, 3, 1, 2, 3 };
	O.Intensity = std::max(1, std::min(3, LevelArg(Args, "intensity", 2, LEVELS, LEVEL_VALUES, 6)));
	O.FillEvery = IntArg(Args, "fill_every", 4, 0, 64);
	O.PhraseStart = O.FillEvery > 0 ? (From - 1) % O.FillEvery : 0;
	O.Crash = BoolArg(Args, "crash", true);
	O.Samples = SampledKitArg(Args);
	O.RowsPerBeat = R;
	const int Dpcm = O.Samples ? DpcmChannel(S) : -1;
	const std::vector<DrumHit> Hits = DrumPart(SongBarSpans(S, First, End), O);
	if (Hits.empty())
		throw Problem("There are no beats in those bars.");
	std::vector<std::string> Warnings;
	if (HasNotesIn(S, Channel, First, End))
		Warnings.push_back(S.Channel(Channel).FullName() + "'s drums in those bars were replaced.");
	std::vector<Change> Changes;
	std::string Where;
	DrumChanges(S, Channel, Dpcm, Hits, First, End, IntArg(Args, "volume", 15, 1, 15), Direct(S, AsClip), Changes, Where, Warnings);
	static const char *const INTENSITY[] = { "", "light", "", "busy" };
	std::string What = std::string(INTENSITY[O.Intensity]) + (O.Intensity == 2 ? "" : " ") + Grooves()[static_cast<size_t>(O.Groove)].Name +
		" drums" + Where;
	What += O.FillEvery > 0 ? Format(", a fill every %d bars", O.FillEvery) : std::string(", no fills");
	What += ", " + RangeText(From, To);
	return WithRemarks(S, Commit(S, Changes, First, AsClip, StringArg(Args, "clip_name"), What), Warnings);
}

// The melody a part goes with: as named, or the busiest channel
int MelodyArg(Session &S, const Json &Args)
{
	const int Melody = Arg(Args, "melody_channel") != 0 ? ChannelArg(S, Args, "melody_channel") : GuessMelodyChannel(S, 0, S.Time.TotalRows());
	if (Melody < 0)
		throw Problem("There's no melody yet.");
	if (!IsMelodic(S.Channel(Melody)))
		throw Problem("The melody channel needs notes, not drums or samples.");
	return Melody;
}

// The channel for a new part beside the melody: as named, or a free one like it
int PartChannel(Session &S, const Json &Args, int Melody, int First, int End, const char *What)
{
	if (Arg(Args, "channel") != 0) {
		const int Channel = ChannelArg(S, Args, "channel");
		if (Channel == Melody || !IsMelodic(S.Channel(Channel)))
			throw Problem(Format("The %s needs a melodic channel other than the melody's.", What));
		return Channel;
	}
	const int Channel = EchoTarget(S, Melody, First, End);
	if (Channel < 0)
		throw Problem(Format("No free channel for the %s in those bars; name one with channel (its notes there are replaced).", What));
	return Channel;
}

std::string AddHarmonyTool(Session &S, const Json &Args)
{
	const bool AsClip = BoolArg(Args, "as_clip", false);
	const int Melody = MelodyArg(S, Args);
	int From, To;
	if (Arg(Args, "from_bar") != 0 || Arg(Args, "to_bar") != 0)
		BarRange(S, Args, From, To);
	else
		PlayedBars(S, From, To);
	const int First = S.Time.BarStart(From), End = S.Time.BarEnd(To);
	const std::vector<NoteInfo> Line = NotesIn(S.Notes(Melody), First, End);
	if (Line.empty())
		throw Problem(Format("%s has no notes in %s to harmonize.", S.Channel(Melody).FullName().c_str(), RangeText(From, To).c_str()));
	const int Target = PartChannel(S, Args, Melody, First, End, "harmony");
	bool Found;
	Key K = KeyArg(S, Args, "key", First, End, Found);
	if (!Found)
		K = Key(0, MODE_MAJOR);
	std::string Source;
	const std::vector<ChordAt> Chords = ChordsForMelody(S, Args, Melody, SongBarSpans(S, First, End), K, Source);

	HarmonyOptions O;
	O.Interval = StyleArg(Args, "interval", HarmonyIntervals(), HARMONY_THIRD_BELOW);
	O.MinRows = BoolArg(Args, "long_notes_only", false) ? S.Time.RowsPerBeat : 0;
	O.RowsPerBeat = S.Time.RowsPerBeat;
	O.K = K;
	// In sounding pitches: the triangle sounds an octave below what's written
	const int Shift = Written(S.Channel(Target), 0) - Written(S.Channel(Melody), 0);
	O.Low = std::max(0, Written(S.Channel(Target), 12)) - Shift;
	O.High = std::min(PITCHES - 1, Written(S.Channel(Target), 84)) - Shift;
	int Loudest = 0;
	for (size_t i = 0; i < Line.size(); ++i)
		Loudest = std::max(Loudest, Line[i].Volume);
	O.VolumeDrop = Arg(Args, "volume") != 0 ? std::max(0, Loudest - IntArg(Args, "volume", 12, 1, 15)) : 3;
	std::vector<PartNote> Notes = HarmonyPart(Line, Chords, O);
	if (Notes.empty())
		throw Problem("No melody notes there are long enough to harmonize.");

	// The melody's own instruments when the channel takes them, else the one asked for or a soft lead
	const bool SameChip = InstrumentChip(S.Channel(Target)) == InstrumentChip(S.Channel(Melody));
	const int Instrument = Arg(Args, "instrument") != 0 || !SameChip ? PartInstrument(S, Args, "instrument", Target, "soft lead") : NO_INSTRUMENT;
	for (size_t i = 0; i < Notes.size(); ++i) {
		Notes[i].Pitch = std::max(0, std::min(PITCHES - 1, Notes[i].Pitch + Shift));
		for (size_t m = 0; m < Line.size(); ++m) {
			if (Line[m].Row != Notes[i].Row)
				continue;
			if (Instrument == NO_INSTRUMENT)
				Notes[i].Instrument = Line[m].Instrument;
			if (Arg(Args, "volume") == 0 && Line[m].Volume > 0)
				Notes[i].Volume = MatchVolume(S.Channel(Melody), Line[m].Volume, std::max(1, Line[m].Volume - O.VolumeDrop) /
					static_cast<double>(Line[m].Volume), S.Channel(Target));
		}
		if (Instrument != NO_INSTRUMENT)
			Notes[i].Instrument = Instrument;
	}
	TidyPart(Notes, First, End);
	std::vector<std::string> Warnings;
	if (HasNotesIn(S, Target, First, End))
		Warnings.push_back(S.Channel(Target).FullName() + "'s own notes in those bars were replaced.");
	Change C;
	C.Channel = Target;
	BuildPart(S.Cells(Target), First, End, Notes, OptionsFor(S, Target, Direct(S, AsClip)), C.Cells, Warnings);
	std::vector<Change> Changes(1, C);
	std::string Interval = HarmonyIntervals()[static_cast<size_t>(O.Interval)].Name;
	std::replace(Interval.begin(), Interval.end(), '_', ' ');
	const std::string What = "a harmony a " + Interval + " " + S.Channel(Melody).FullName() + " on " + S.Channel(Target).FullName() +
		", " + RangeText(From, To);
	return WithRemarks(S, Commit(S, Changes, First, AsClip, StringArg(Args, "clip_name"), What) +
		Format(" (%s, on %s in %s)", Plural(static_cast<int>(Notes.size()), "note", "notes").c_str(), Source.c_str(),
			KeyName(K).c_str()), Warnings);
}

std::string AddCounterMelodyTool(Session &S, const Json &Args)
{
	const bool AsClip = BoolArg(Args, "as_clip", false);
	const int Melody = MelodyArg(S, Args);
	int From, To;
	if (Arg(Args, "from_bar") != 0 || Arg(Args, "to_bar") != 0)
		BarRange(S, Args, From, To);
	else
		PlayedBars(S, From, To);
	const int First = S.Time.BarStart(From), End = S.Time.BarEnd(To);
	const std::vector<NoteInfo> Line = NotesIn(S.Notes(Melody), First, End);
	if (Line.empty())
		throw Problem(Format("%s has no notes in %s to answer.", S.Channel(Melody).FullName().c_str(), RangeText(From, To).c_str()));
	const int Target = PartChannel(S, Args, Melody, First, End, "counter-melody");
	bool Found;
	Key K = KeyArg(S, Args, "key", First, End, Found);
	if (!Found)
		K = Key(0, MODE_MAJOR);
	const std::vector<BarSpan> Bars = SongBarSpans(S, First, End);
	std::string Source;
	const std::vector<ChordAt> Chords = ChordsForMelody(S, Args, Melody, Bars, K, Source);

	CounterOptions O;
	const std::string Place = Lowercase(StringArg(Args, "position", "below"));
	if (Place != "below" && Place != "above" && Place != "under" && Place != "over" && Place != "descant")
		throw Problem("position: below or above.");
	O.Above = Place == "above" || Place == "over" || Place == "descant";
	O.RowsPerBeat = S.Time.RowsPerBeat;
	O.K = K;
	// Around a sixth to a tenth under the melody, or over it; in the melody's pitches
	const int Middle = MiddleOf(Line, 52);
	const int Shift = Written(S.Channel(Target), 0) - Written(S.Channel(Melody), 0);
	O.Low = O.Above ? Middle + 3 : Middle - 17;
	O.High = O.Above ? Middle + 15 : Middle - 4;
	O.Low = std::max(12 - Shift, O.Low);
	O.High = std::min(PITCHES - 13 - Shift, std::max(O.Low + 7, O.High));
	int Loudest = 0;
	for (size_t i = 0; i < Line.size(); ++i)
		Loudest = std::max(Loudest, Line[i].Volume);
	O.Volume = Arg(Args, "volume") != 0 ? IntArg(Args, "volume", 11, 1, 15) :
		MatchVolume(S.Channel(Melody), Loudest, std::max(4, Loudest - 4) / static_cast<double>(std::max(1, Loudest)), S.Channel(Target));
	std::vector<PartNote> Notes = CounterMelody(Line, Chords, Bars, O);
	if (Notes.empty())
		throw Problem("There are no chords there for a counter-melody to follow.");
	const int Instrument = PartInstrument(S, Args, "instrument", Target, IsBass(S.Channel(Target)) ? "bass" : "soft lead");
	for (size_t i = 0; i < Notes.size(); ++i) {
		Notes[i].Pitch = std::max(0, std::min(PITCHES - 1, Notes[i].Pitch + Shift));
		Notes[i].Instrument = Instrument;
	}
	std::vector<std::string> Warnings;
	if (HasNotesIn(S, Target, First, End))
		Warnings.push_back(S.Channel(Target).FullName() + "'s own notes in those bars were replaced.");
	Change C;
	C.Channel = Target;
	BuildPart(S.Cells(Target), First, End, Notes, OptionsFor(S, Target, Direct(S, AsClip)), C.Cells, Warnings);
	std::vector<Change> Changes(1, C);
	const std::string What = std::string("a counter-melody ") + (O.Above ? "above " : "below ") + S.Channel(Melody).FullName() + " on " +
		S.Channel(Target).FullName() + ", " + RangeText(From, To);
	return WithRemarks(S, Commit(S, Changes, First, AsClip, StringArg(Args, "clip_name"), What) +
		Format(" (%s on %s in %s; it moves where the melody holds and holds where it moves. Shape it further with write_notes.)",
			Plural(static_cast<int>(Notes.size()), "note", "notes").c_str(), Source.c_str(), KeyName(K).c_str()), Warnings);
}

// Finishing touches

// Accents in the draft: notes on a bar's first row as loud as the channel
// plays, other beats a step softer, between the beats two, quarter beats
// three. Channels that already shape their volumes are left alone. How
// many notes; -1 when the channel was left alone.
int Accents(Session &S, Draft &D, int Channel, int First, int End)
{
	const int R = S.Time.RowsPerBeat;
	const std::vector<NoteInfo> Notes = D.Notes(Channel);
	std::set<int> Volumes;
	int Loudest = 0;
	for (size_t i = 0; i < Notes.size(); ++i) {
		if (Notes[i].Row < First || Notes[i].Row >= End)
			continue;
		Volumes.insert(Notes[i].Volume);
		Loudest = std::max(Loudest, Notes[i].Volume);
	}
	if (Volumes.size() >= 3)
		return -1;
	std::vector<Cell> &Cells = D.Cells(Channel);
	int Count = 0;
	for (size_t i = 0; i < Notes.size(); ++i) {
		const NoteInfo &N = Notes[i];
		if (N.Row < First || N.Row >= End || N.Row >= static_cast<int>(Cells.size()))
			continue;
		const int InBar = N.Row - S.Time.BarStart(S.Time.BarOf(N.Row));
		int Softer = InBar == 0 ? 0 : InBar % R == 0 ? 1 : (2 * InBar) % R == 0 ? 2 : 3;
		if (S.Channel(Channel).Kind == KIND_FM)
			Softer = Softer >= 2 ? 1 : 0;	// a VRC7 step is 3 dB: the notes between beats one softer
		const int Volume = std::max(1, std::min(15, N.Volume - Softer));
		Cells[N.Row].Volume = static_cast<unsigned char>(Volume);
		++Count;
	}
	// The note after the bars plays as loud as before
	for (size_t i = 0; i < Notes.size(); ++i) {
		if (Notes[i].Row >= End && Notes[i].Row < static_cast<int>(Cells.size())) {
			if (Cells[Notes[i].Row].Volume == NO_VOLUME)
				Cells[Notes[i].Row].Volume = static_cast<unsigned char>(Notes[i].Volume);
			break;
		}
	}
	(void)Loudest;
	return Count;
}

// Repeated notes that run into each other on the triangle sound as one (it
// has no attack): each gets a halt on its last row. How many.
// Fades on a channel's held notes (two beats or more): a volume slide down
// (A0y) from halfway through, to about a third of the note's loudness by its
// end, and the next note given its own volume back (A00 stops the slide).
// How many notes; Lost when an effect column was full.
int AddFades(Session &S, Draft &D, int Channel, int First, int End, bool &Lost)
{
	const int R = S.Time.RowsPerBeat;
	const int Columns = S.Channel(Channel).EffectColumns;
	std::vector<Cell> &Cells = D.Cells(Channel);
	const std::vector<NoteInfo> Notes = D.Notes(Channel);
	const int Ticks = std::max(1, S.Song.Speed);	// a row's ticks
	int Count = 0;
	for (size_t i = 0; i < Notes.size(); ++i) {
		const NoteInfo &N = Notes[i];
		if (N.Row < First || N.Row >= End || N.Rows < 2 * R || N.Volume < 3)
			continue;
		bool Shaped = false;	// a slide or tremolo of its own
		for (size_t e = 0; e < N.Effects.size(); ++e)
			Shaped = Shaped || N.Effects[e].second.first == EFF_VOLUME_SLIDE || N.Effects[e].second.first == EFF_TREMOLO;
		if (Shaped)
			continue;
		int From = N.Row + N.Rows / 2;
		const bool Fm = S.Channel(Channel).Kind == KIND_FM;
		if (Fm)	// three steps of 3 dB, over the note's last frames
			From = std::max(From, N.Row + N.Rows - std::max(1, (24 + Ticks - 1) / Ticks));
		const int Rows = N.Row + N.Rows - From;
		// Two thirds of the loudness in eighths of a step, over the rows left
		const int Eighths = Fm ? 24 : N.Volume * 2 * 8 / 3;
		const int Down = std::max(1, std::min(4, (Eighths + Rows * Ticks - 1) / (Rows * Ticks)));
		if (From >= static_cast<int>(Cells.size()) || !AddEffect(Cells[static_cast<size_t>(From)], EFF_VOLUME_SLIDE, Down, Columns)) {
			Lost = true;
			continue;
		}
		// The next note, or the row after this one's end: the slide stops, the volume comes back
		const int After = N.Row + N.Rows;
		if (After < static_cast<int>(Cells.size())) {
			Cell &Next = Cells[static_cast<size_t>(After)];
			if (!HasEffect(Next, EFF_VOLUME_SLIDE, Columns) && !AddEffect(Next, EFF_VOLUME_SLIDE, 0, Columns))
				Lost = true;
			if (Next.HasNote() && Next.Volume == NO_VOLUME)
				Next.Volume = static_cast<unsigned char>(i + 1 < Notes.size() ? Notes[i + 1].Volume : N.Volume);
		}
		++Count;
	}
	return Count;
}

// Scoops into a channel's held notes that come after a leap up: the note
// written a semitone or two lower, sliding up into it at once (Q61, Q62),
// at most one every two bars. How many; Lost when an effect column was full.
int AddScoops(Session &S, Draft &D, int Channel, int First, int End, bool &Lost)
{
	const int R = S.Time.RowsPerBeat;
	const int Columns = S.Channel(Channel).EffectColumns;
	std::vector<Cell> &Cells = D.Cells(Channel);
	const std::vector<NoteInfo> Notes = D.Notes(Channel);
	int Count = 0, Last = -1000;
	for (size_t i = 1; i < Notes.size(); ++i) {
		const NoteInfo &N = Notes[i], &Before = Notes[i - 1];
		if (N.Row < First || N.Row >= End || N.Rows < R || N.Row - Last < 2 * S.Time.RowsPerBar)
			continue;
		const int Leap = N.Pitch - Before.Pitch;
		if (Leap < 3 || N.Row - (Before.Row + Before.Rows) > R || N.Pitch < 3)
			continue;
		bool Sliding = false;
		for (size_t e = 0; e < N.Effects.size(); ++e) {
			const int Effect = N.Effects[e].second.first;
			Sliding = Sliding || Effect == EFF_SLIDE_UP || Effect == EFF_SLIDE_DOWN || Effect == EFF_PORTAMENTO || Effect == EFF_PORTA_UP ||
				Effect == EFF_PORTA_DOWN || Effect == EFF_ARPEGGIO;
		}
		if (Sliding)
			continue;
		Cell &C = Cells[static_cast<size_t>(N.Row)];
		const int Below = Leap >= 5 ? 2 : 1;
		if (!AddEffect(C, EFF_SLIDE_UP, 0x60 | Below, Columns)) {
			Lost = true;
			continue;
		}
		C.SetPitch(N.Pitch - Below);
		Last = N.Row;
		++Count;
	}
	return Count;
}

// Falls where a channel's phrases end: a note of a beat or more that a rest
// of a beat or more follows drops two semitones as it ends (R42 half a beat
// before its end, quick and short, a sigh), at most one every two bars. How
// many; Lost when an effect column was full.
int AddFalls(Session &S, Draft &D, int Channel, int First, int End, bool &Lost)
{
	const int R = S.Time.RowsPerBeat;
	const int Columns = S.Channel(Channel).EffectColumns;
	std::vector<Cell> &Cells = D.Cells(Channel);
	const std::vector<NoteInfo> Notes = D.Notes(Channel);
	int Count = 0, Last = -1000;
	for (size_t i = 0; i < Notes.size(); ++i) {
		const NoteInfo &N = Notes[i];
		const int Stop = N.Row + N.Rows;	// where a halt or release ends it
		if (N.Row < First || N.Row >= End || N.Rows < R || N.Row - Last < 2 * S.Time.RowsPerBar || N.Pitch < 3 ||
			Stop >= static_cast<int>(Cells.size()) || !Cells[static_cast<size_t>(Stop)].IsEnd())
			continue;
		const int Next = i + 1 < Notes.size() ? Notes[i + 1].Row : static_cast<int>(Cells.size());
		if (Next - Stop < R)
			continue;	// a breath, not a phrase's end
		bool Sliding = false;
		for (size_t e = 0; e < N.Effects.size(); ++e) {
			const int Effect = N.Effects[e].second.first;
			Sliding = Sliding || Effect == EFF_SLIDE_UP || Effect == EFF_SLIDE_DOWN || Effect == EFF_PORTAMENTO || Effect == EFF_PORTA_UP ||
				Effect == EFF_PORTA_DOWN || Effect == EFF_ARPEGGIO;
		}
		const int At = std::max(N.Row + 1, Stop - std::max(1, R / 2));
		if (Sliding || At >= Stop)
			continue;
		if (!AddEffect(Cells[static_cast<size_t>(At)], EFF_SLIDE_DOWN, 0x42, Columns)) {
			Lost = true;
			continue;
		}
		Last = N.Row;
		++Count;
	}
	return Count;
}

int SeparateRepeats(Session &S, Draft &D, int Channel, int First, int End)
{
	const std::vector<NoteInfo> Notes = D.Notes(Channel);
	std::vector<Cell> &Cells = D.Cells(Channel);
	int Count = 0;
	for (size_t i = 0; i + 1 < Notes.size(); ++i) {
		const NoteInfo &N = Notes[i];
		if (N.Row < First || N.Row >= End || Notes[i + 1].Row != N.Row + N.Rows || Notes[i + 1].Pitch != N.Pitch || N.Rows < 2)
			continue;
		Cell &Last = Cells[static_cast<size_t>(N.Row + N.Rows - 1)];
		if (Last.Note != NOTE_NONE)
			continue;
		Last.Note = NOTE_HALT;
		++Count;
	}
	(void)S;
	return Count;
}

// A bass played short: each note of a beat or more cut after three
// quarters of a beat (a halt), so the line bounces as a triangle bass in
// atmospheric and action tracks does. How many notes.
int StaccatoBass(Session &S, Draft &D, int Channel, int First, int End)
{
	const int R = S.Time.RowsPerBeat;
	const int Keep = std::max(1, R / 2);
	const std::vector<NoteInfo> Notes = D.Notes(Channel);
	std::vector<Cell> &Cells = D.Cells(Channel);
	int Count = 0;
	for (size_t i = 0; i < Notes.size(); ++i) {
		const NoteInfo &N = Notes[i];
		if (N.Row < First || N.Row >= End || N.Rows < R || N.Row + Keep >= static_cast<int>(Cells.size()))
			continue;
		Cell &Cut = Cells[static_cast<size_t>(N.Row + Keep)];
		if (Cut.Note != NOTE_NONE)
			continue;
		Cut.Note = NOTE_HALT;
		++Count;
	}
	return Count;
}

std::string PolishTool(Session &S, const Json &Args)
{
	int From, To;
	BarRange(S, Args, From, To);
	const int First = S.Time.BarStart(From), End = S.Time.BarEnd(To);
	const bool AsClip = BoolArg(Args, "as_clip", false);
	std::set<std::string> What;
	const Json *pWhat = Arg(Args, "touches");
	if (pWhat == 0) {
		What.insert("accents");
		What.insert("vibrato");
		What.insert("separate");
	}
	else {
		std::vector<std::string> Words;
		if (pWhat->IsArray()) {
			for (size_t i = 0; i < pWhat->Size(); ++i)
				if (pWhat->At(i).IsString())
					Words.push_back(pWhat->At(i).String());
		}
		else if (pWhat->IsString())
			Words.push_back(pWhat->String());
		for (size_t i = 0; i < Words.size(); ++i) {
			const std::string Word = Squeezed(Words[i]);
			if (Word != "accents" && Word != "vibrato" && Word != "separate" && Word != "fades" && Word != "scoops" &&
				Word != "staccato" && Word != "falls")
				throw Problem("touches: accents, vibrato, separate, fades, scoops, falls or staccato.");
			What.insert(Word);
		}
	}
	const std::vector<int> Channels = ChannelsArg(S, Args, "channels");
	int Melody = Arg(Args, "melody_channel") != 0 ? ChannelArg(S, Args, "melody_channel") : GuessMelodyChannel(S, First, End);

	Draft D(S);
	std::vector<std::string> Done, Warnings;
	if (What.count("accents")) {
		std::string List, Fm;
		for (size_t i = 0; i < Channels.size(); ++i) {
			const int c = Channels[i];
			if (S.Channel(c).Kind == KIND_TRIANGLE || !IsMelodic(S.Channel(c)))
				continue;	// the triangle has no volume
			const int Count = Accents(S, D, c, First, End);
			std::string &Into = S.Channel(c).Kind == KIND_FM ? Fm : List;
			if (Count > 0)
				Into += (Into.empty() ? "" : ", ") + S.Channel(c).FullName();
			else if (Count < 0)
				Warnings.push_back(S.Channel(c).FullName() + " already shapes its volumes, so it kept them.");
		}
		if (!List.empty())
			Done.push_back("accents (beats a step softer than a bar's first note, the notes between them softer still) on " + List);
		if (!Fm.empty())	// a VRC7 step is 3 dB
			Done.push_back("accents (the notes between beats a step, 3 dB, softer than the beats) on " + Fm);
	}
	if (What.count("vibrato") && Melody >= 0 && std::find(Channels.begin(), Channels.end(), Melody) != Channels.end()) {
		VibratoPlan Plan;
		Plan.Channel = Melody;
		Plan.Speed = 5;
		Plan.Depth = 2;
		Plan.MinRows = S.Time.RowsPerBeat * 3 / 2;
		Plan.Delay = std::max(1, S.Time.RowsPerBeat / 2);
		bool Lost = false;
		const int Count = MakeVibrato(S, D, Plan, First, End, Lost, true);
		if (Lost)
			Warnings.push_back("Some notes had no free effect column for the vibrato (all four were full).");
		if (Count > 0)
			Done.push_back(Format("a gentle vibrato that comes in after half a beat on %s of %s", Plural(Count, "long note", "long notes").c_str(),
				S.Channel(Melody).FullName().c_str()));
	}
	if (What.count("fades")) {
		std::string List;
		int Total = 0;
		bool Lost = false, Fm = false;
		for (size_t i = 0; i < Channels.size(); ++i) {
			const int c = Channels[i];
			if (!IsMelodic(S.Channel(c)) || S.Channel(c).Kind == KIND_TRIANGLE)
				continue;
			const int Count = AddFades(S, D, c, First, End, Lost);
			if (Count > 0) {
				List += (List.empty() ? "" : ", ") + S.Channel(c).FullName();
				Total += Count;
				Fm = Fm || S.Channel(c).Kind == KIND_FM;
			}
		}
		if (Lost)
			Warnings.push_back("Some held notes had no free effect column for a fade (all four were full).");
		if (Total > 0)
			Done.push_back(Format("fades on %s (a volume slide from halfway through, A0y, the next note at its own volume%s) on %s",
				Plural(Total, "held note", "held notes").c_str(), Fm ? "; on the VRC7 three steps, 9 dB, over the note's end" : "",
				List.c_str()));
	}
	if (What.count("scoops") && Melody >= 0 && std::find(Channels.begin(), Channels.end(), Melody) != Channels.end()) {
		bool Lost = false;
		const int Count = AddScoops(S, D, Melody, First, End, Lost);
		if (Lost)
			Warnings.push_back("Some notes had no free effect column for a scoop (all four were full).");
		if (Count > 0)
			Done.push_back(Format("%s after leaps up on %s (written a semitone or two low, sliding up with Q6y)",
				Plural(Count, "scoop", "scoops").c_str(), S.Channel(Melody).FullName().c_str()));
	}
	if (What.count("falls") && Melody >= 0 && std::find(Channels.begin(), Channels.end(), Melody) != Channels.end()) {
		bool Lost = false;
		const int Count = AddFalls(S, D, Melody, First, End, Lost);
		if (Lost)
			Warnings.push_back("Some phrase ends had no free effect column for a fall (all four were full).");
		if (Count > 0)
			Done.push_back(Format("%s where phrases of %s end (a drop of two semitones as the last note ends, R42)",
				Plural(Count, "fall", "falls").c_str(), S.Channel(Melody).FullName().c_str()));
	}
	if (What.count("staccato")) {
		for (size_t i = 0; i < Channels.size(); ++i) {
			const int c = Channels[i];
			if (!IsBass(S.Channel(c)))
				continue;
			const int Count = StaccatoBass(S, D, c, First, End);
			if (Count > 0)
				Done.push_back(Format("%s on %s cut short after half a beat (staccato)",
					Plural(Count, "bass note", "bass notes").c_str(), S.Channel(c).FullName().c_str()));
		}
	}
	if (What.count("separate")) {
		for (size_t i = 0; i < Channels.size(); ++i) {
			const int c = Channels[i];
			if (S.Channel(c).Kind != KIND_TRIANGLE)
				continue;
			const int Count = SeparateRepeats(S, D, c, First, End);
			if (Count > 0)
				Done.push_back(Format("a short gap before %s on %s, so each is heard", Plural(Count, "repeated note", "repeated notes").c_str(),
					S.Channel(c).FullName().c_str()));
		}
	}
	std::vector<Change> Changes;
	int Start;
	if (!D.Collect(First, Direct(S, AsClip), Changes, Start, Warnings))
		throw Problem("There was nothing to polish there: no notes for accents, no long melody notes for vibrato and no repeated "
			"triangle notes.");
	std::string Summary;
	for (size_t i = 0; i < Done.size(); ++i)
		Summary += (i == 0 ? "" : "; ") + Done[i];
	return WithRemarks(S, Commit(S, Changes, Start, AsClip, StringArg(Args, "clip_name"), "finishing touches, " + RangeText(From, To)) +
		"\nTouches: " + Summary + ".", Warnings);
}

// The chords a song's parts play in [First, End): the melody left out when
// others play too, else the chords it implies by itself
std::vector<ChordAt> SongChords(Session &S, int First, int End, const Key &K)
{
	const int Melody = GuessMelodyChannel(S, First, End);
	bool Backing = false;
	for (int c = 0; c < S.ChannelCount() && !Backing; ++c)
		Backing = c != Melody && IsMelodic(S.Channel(c)) && HasNotesIn(S, c, First, End);
	return HearChords(HeardNotes(S, Backing ? Melody : -1, First, End), SongBarSpans(S, First, End), S.Time.RowsPerBeat, K, true);
}

// The depth review: what a part, or the arrangement, is missing

std::string SecondsText(double Seconds);

// Bars as "3, 5-8, 12"
std::string BarList(const std::vector<int> &Bars)
{
	std::string Out;
	for (size_t i = 0; i < Bars.size(); ) {
		size_t j = i;
		while (j + 1 < Bars.size() && Bars[j + 1] == Bars[j] + 1)
			++j;
		Out += (Out.empty() ? "" : ", ") + (j == i ? Format("%d", Bars[i]) : Format("%d-%d", Bars[i], Bars[j]));
		i = j + 1;
	}
	return Out;
}

// A bar of a channel as text: its notes' rows, pitches, lengths and
// volumes, for finding bars that repeat note for note
std::string BarPrint(const std::vector<NoteInfo> &Notes, int Row, int End)
{
	std::string Out;
	for (size_t i = 0; i < Notes.size(); ++i) {
		const NoteInfo &N = Notes[i];
		if (N.Row < Row || N.Row >= End)
			continue;
		Out += Format("%d:%d:%d:%d ", N.Row - Row, N.Pitch, std::min(N.Rows, End - N.Row), N.Volume);
	}
	return Out;
}

// What a version's name says about it: calm (night), tense (battle) or neither
enum version_mood { MOOD_OTHER, MOOD_CALM, MOOD_TENSE };

int VersionMood(const std::string &Version)
{
	static const char *const CALM[] = { "night", "calm", "evening", "sleep", "sleepy", "peaceful", "quiet", "relaxed" };
	static const char *const TENSE[] = { "tense", "battle", "danger", "boss", "fight", "combat", "chase", "alarm", "panic" };
	bool Calm = false, Tense = false;
	std::string Word;
	const std::string Text = Lowercase(Version) + " ";
	for (size_t i = 0; i < Text.size(); ++i) {
		const char c = Text[i];
		if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
			Word += c;
			continue;
		}
		for (size_t w = 0; w < sizeof(CALM) / sizeof(CALM[0]); ++w)
			Calm = Calm || Word == CALM[w];
		for (size_t w = 0; w < sizeof(TENSE) / sizeof(TENSE[0]); ++w)
			Tense = Tense || Word == TENSE[w];
		Word.clear();
	}
	if (Calm == Tense)
		return MOOD_OTHER;
	return Calm ? MOOD_CALM : MOOD_TENSE;
}

std::string ReviewTool(Session &S, const Json &Args)
{
	int From, To;
	if (Arg(Args, "from_bar") != 0 || Arg(Args, "to_bar") != 0)
		BarRange(S, Args, From, To);
	else
		PlayedBars(S, From, To);
	const int First = S.Time.BarStart(From), End = std::min(S.Time.BarEnd(To), S.Time.TotalRows());
	const int R = S.Time.RowsPerBeat;
	const int BarCount = To - From + 1;
	bool Found;
	Key K = KeyArg(S, Args, "key", First, End, Found);
	if (!Found)
		K = Key(0, MODE_MAJOR);
	const std::vector<BarSpan> Bars = SongBarSpans(S, First, End);
	// A calm track (a night version, an ambient one) floats: no drums, fills or accents are asked of it
	const std::string Style = Lowercase(StringArg(Args, "style", VersionMood(S.Song.TrackName) == MOOD_CALM ? "calm" : "any"));
	if (Style != "any" && Style != "calm" && Style != "ambient")
		throw Problem("style should be any or calm.");
	const bool Calm = Style != "any";
	std::vector<std::string> Lines, Tips;
	double Missing = 0.0;	// what the tips take off the score, out of 10
	auto Tip = [&](double Weight, const std::string &Text) {
		Tips.push_back(Text);
		Missing += Weight;
	};

	// The parts and what each does
	const int Melody = GuessMelodyChannel(S, First, End);
	if (Melody < 0)
		throw Problem(Format("Nothing plays a melody in %s yet. Write one with write_notes (or ask for a backing with "
			"add_chords_and_bass and chords) first.", RangeText(From, To).c_str()));
	const std::vector<NoteInfo> Tune = NotesIn(S.Notes(Melody), First, End);
	int Bass = -1, Drums = -1, Samples = -1;
	std::vector<int> Chords, Voices, Echoes, Playing;
	std::vector<std::string> Free;
	for (int c = 0; c < S.ChannelCount(); ++c) {
		const ChannelInfo &Info = S.Channel(c);
		const std::vector<NoteInfo> Notes = NotesIn(S.Notes(c), First, End);
		if (Notes.empty()) {
			if (Info.Kind != KIND_DPCM)
				Free.push_back(Info.FullName());
			continue;
		}
		Playing.push_back(c);
		if (c == Melody)
			continue;
		if (Info.Kind == KIND_NOISE) {
			Drums = c;
			continue;
		}
		if (Info.Kind == KIND_DPCM) {
			Samples = c;
			continue;
		}
		if (IsBass(Info) && Bass < 0) {
			Bass = c;
			continue;
		}
		int Arpeggios = 0;	// notes that sound chords (0xy, or arpeggio instruments)
		for (size_t i = 0; i < Notes.size(); ++i)
			if (SoundsChord(S, Notes[i]))
				++Arpeggios;
		// An echo: the melody's notes again, a little later
		int Delayed = 0;
		for (size_t i = 0; i < Notes.size(); ++i) {
			for (size_t m = 0; m < Tune.size(); ++m) {
				if (Notes[i].Row > Tune[m].Row && Notes[i].Row - Tune[m].Row <= 2 * R && Notes[i].Pitch == Tune[m].Pitch) {
					++Delayed;
					break;
				}
			}
		}
		if (Arpeggios * 3 >= static_cast<int>(Notes.size()))
			Chords.push_back(c);
		else if (Delayed * 2 >= static_cast<int>(Notes.size()))
			Echoes.push_back(c);
		else
			Voices.push_back(c);
	}
	auto Names = [&](const std::vector<int> &List) {
		std::string Out;
		for (size_t i = 0; i < List.size(); ++i)
			Out += (i == 0 ? "" : ", ") + S.Channel(List[i]).FullName();
		return Out;
	};
	std::string Parts = "Parts: melody on " + S.Channel(Melody).FullName();
	if (!Chords.empty())
		Parts += "; chords on " + Names(Chords);
	if (Bass >= 0)
		Parts += "; bass on " + S.Channel(Bass).FullName();
	if (Drums >= 0)
		Parts += "; drums on " + S.Channel(Drums).FullName() + (Samples >= 0 ? " with samples on " + S.Channel(Samples).FullName() : "");
	else if (Samples >= 0)
		Parts += "; samples on " + S.Channel(Samples).FullName();
	if (!Voices.empty())
		Parts += "; another line on " + Names(Voices);
	if (!Echoes.empty())
		Parts += "; an echo on " + Names(Echoes);
	Parts += ".";
	if (!Free.empty()) {
		Parts += " Free: ";
		for (size_t i = 0; i < Free.size(); ++i)
			Parts += (i == 0 ? "" : ", ") + Free[i];
		Parts += ".";
	}
	Lines.push_back(Parts);
	const int Layers = static_cast<int>(Playing.size());
	if (Chords.empty() && Voices.empty())
		Tip(2.0, "Nothing harmonizes the melody: add_chords_and_bass fits chords to it (choose a chord_style and color).");
	if (Bass < 0)
		Tip(2.0, "No bass line: add_chords_and_bass with chord_channel \"none\" and a bass_style.");
	if (Drums < 0 && !Calm)
		Tip(1.0, "No drums: add_drums (groove calm for a quiet track, driving for action), unless the track should float without a beat.");
	if (Voices.empty() && Echoes.empty())
		Tip(1.0, "The melody has no company: add_counter_melody (a line of its own) or add_harmony (thirds under it) on " +
			(Free.empty() ? std::string("a channel") : Free[0]) + " adds depth; add_echo makes it spacious.");

	// Thin bars: two parts or fewer; and how much the number of parts changes
	std::vector<int> Thin;
	int Most = 0;
	for (size_t b = 0; b < Bars.size(); ++b) {
		int Sounding = 0;
		for (size_t p = 0; p < Playing.size(); ++p)
			if (HasNotesIn(S, Playing[p], Bars[b].Row, Bars[b].Row + Bars[b].Rows))
				++Sounding;
		if (Sounding <= 2 && Layers > 2)
			Thin.push_back(S.Time.BarOf(Bars[b].Row));
		Most = std::max(Most, Sounding);
	}
	if (!Thin.empty() && static_cast<int>(Thin.size()) < BarCount)
		Lines.push_back("Thin: only two parts or fewer play in bars " + BarList(Thin) + ".");

	// Loudness bar by bar, from the parts that sound in it at their levels (as FamiTracker plays each chip; the
	// triangle a little under a pulse at 15, drums at a steady share), two bars at a time: how far the song swings
	double Swing = 0.0;
	int Quietest = -1, Loudest = -1;
	{
		std::vector<double> Power(Bars.size(), 0.0);
		for (size_t p = 0; p < Playing.size(); ++p) {
			const int c = Playing[p];
			const ChannelInfo &Info = S.Channel(c);
			const std::vector<NoteInfo> Notes = NotesIn(S.Notes(c), First, End);
			for (size_t b = 0; b < Bars.size(); ++b) {
				const int Begin = Bars[b].Row, Stop = Bars[b].Row + Bars[b].Rows;
				if (Info.Kind == KIND_NOISE || Info.Kind == KIND_DPCM) {
					if (HasNotesIn(S, c, Begin, Stop))
						Power[b] += 0.25;	// -6 dB
					continue;
				}
				for (size_t i = 0; i < Notes.size(); ++i) {
					const int A = std::max(Begin, Notes[i].Row), Z = std::min(Stop, Notes[i].Row + Notes[i].Rows);
					if (Z <= A || (Info.Kind != KIND_TRIANGLE && Notes[i].Volume <= 0))
						continue;
					const double Db = Info.Kind == KIND_TRIANGLE ? -2.0 : LevelDb(Info, Notes[i].Volume);
					Power[b] += std::pow(10.0, Db / 10.0) * (Z - A) / static_cast<double>(std::max(1, Bars[b].Rows));
				}
			}
		}
		double Low = 0.0, High = 0.0;
		for (size_t b = 0; b + 1 < Power.size() || (b == 0 && Power.size() == 1); b += 2) {
			const double Mean = b + 1 < Power.size() ? (Power[b] + Power[b + 1]) / 2 : Power[b];
			if (Mean <= 0.0)
				continue;
			const double Db = 10.0 * std::log10(Mean);
			if (Quietest < 0 || Db < Low) {
				Low = Db;
				Quietest = static_cast<int>(b);
			}
			if (Loudest < 0 || Db > High) {
				High = Db;
				Loudest = static_cast<int>(b);
			}
		}
		Swing = Quietest >= 0 ? High - Low : 0.0;
	}
	if (Quietest >= 0 && Bars.size() >= 4) {
		auto Pair = [&](int b) {
			const int Bar = S.Time.BarOf(Bars[static_cast<size_t>(b)].Row);
			return static_cast<size_t>(b) + 1 < Bars.size() ? Format("bars %d-%d", Bar, Bar + 1) : Format("bar %d", Bar);
		};
		Lines.push_back(Format("Loudness: about %d dB between the quietest bars (%s) and the loudest (%s).",
			static_cast<int>(std::floor(Swing + 0.5)), Pair(Quietest).c_str(), Pair(Loudest).c_str()));
	}
	if (BarCount >= 16 && Most >= 3 && Swing < 3.0)
		Tip(0.5, Format("The song keeps about one loudness all the way through (%d dB between its quietest and loudest bars), "
			"so it never breathes: let it build and fall back, a few bars with fewer and quieter parts (an intro on the chords "
			"alone, the bass joining later; a break without the drums and the melody) and the full sound where it matters. "
			"Well-arranged tracks swing 8-12 dB in loudness between sections; the triangle is loud, so bars with it stay "
			"loud. write_notes with an empty list of notes clears a part from bars.", static_cast<int>(std::floor(Swing + 0.5))));

	// Harmony: the chords the parts play (with the melody when nothing else does)
	const bool Backing = !Chords.empty() || Bass >= 0 || !Voices.empty();
	const std::vector<ChordAt> Heard = SongChords(S, First, End, K);
	if (!Heard.empty()) {
		std::set<std::string> Vocabulary;
		bool Colored = false;
		for (size_t i = 0; i < Heard.size(); ++i) {
			Vocabulary.insert(ChordName(Heard[i].C, K));
			Colored = Colored || !Heard[i].C.IsTriad();
		}
		std::string Line = "Harmony" + std::string(Backing ? "" : " (implied by the melody alone)") + ": " +
			ProgressionText(Heard, Bars, K, false, 16) + Format(" — %d different chords%s.", static_cast<int>(Vocabulary.size()),
			Colored ? "" : ", all plain triads");
		// The same chords every four bars, all the way
		if (BarCount >= 12) {
			std::vector<std::string> Blocks;
			for (int b = 0; b + 4 <= static_cast<int>(Bars.size()); b += 4) {
				const std::vector<BarSpan> Four(Bars.begin() + b, Bars.begin() + b + 4);
				Blocks.push_back(ProgressionText(Heard, Four, K));
			}
			bool Same = Blocks.size() >= 3;
			for (size_t i = 1; i < Blocks.size(); ++i)
				Same = Same && Blocks[i] == Blocks[0];
			if (Same) {
				Line += Format(" The same four-bar progression plays %d times.", static_cast<int>(Blocks.size()));
				Tip(1.0, "The chords loop unchanged: a contrasting section (a B part on IV, V or vi, or the relative "
					"minor or major) for a few bars gives the song somewhere to go. add_chords_and_bass takes chords by name.");
			}
		}
		Lines.push_back(Line);
		if (!Colored && (!Chords.empty() || !Voices.empty()))
			Tip(1.0, "Every chord is a plain triad: add_chords_and_bass with color \"sevenths\" or \"rich\" (add9, maj7, "
				"m9, V7sus4) sounds less bare.");
	}

	// Bass: how it moves
	if (Bass >= 0) {
		const std::vector<NoteInfo> Notes = NotesIn(S.Notes(Bass), First, End);
		const double PerBar = Notes.size() / static_cast<double>(BarCount);
		std::set<int> Pitches;
		for (size_t i = 0; i < Notes.size(); ++i)
			Pitches.insert(Notes[i].Pitch);
		const std::string Rounded = FormatNumber(std::floor(PerBar * 10 + 0.5) / 10);
		std::string Line = "Bass: " + Rounded + (Rounded == "1" ? " note" : " notes") + Format(" a bar on average, %d different notes",
			static_cast<int>(Pitches.size()));
		const int Middle = MiddleOf(Notes, 30) - (S.Channel(Bass).Kind == KIND_TRIANGLE ? 12 : 0);
		Line += ", around " + PitchName(Middle + (S.Channel(Bass).Kind == KIND_TRIANGLE ? 12 : 0)) + " as written.";
		Lines.push_back(Line);
		if (PerBar <= 2.0)
			Tip(1.0, "The bass mostly holds roots: bass_style walking (towns, jazz), octaves (action), syncopated (pop) "
				"or arpeggio (adventure) in add_chords_and_bass gives it a pulse.");
		if (Middle < 8)
			Tip(0.5, S.Channel(Bass).FullName() + " plays very low, where small speakers lose it: an octave up (transpose).");
	}

	// Drums: variety, fills
	if (Drums >= 0) {
		const std::vector<NoteInfo> &Notes = S.Notes(Drums);
		std::map<std::string, int> Prints;
		int Fills = 0, PhraseEnds = 0;
		std::string Before;
		for (size_t b = 0; b < Bars.size(); ++b) {
			const std::string Print = BarPrint(Notes, Bars[b].Row, Bars[b].Row + Bars[b].Rows);
			++Prints[Print];
			const int Bar = S.Time.BarOf(Bars[b].Row);
			if (Bar % 4 == 0) {
				++PhraseEnds;
				if (b > 0 && Print != Before)
					++Fills;
			}
			Before = Print;
		}
		int Most = 0;
		for (std::map<std::string, int>::const_iterator it = Prints.begin(); it != Prints.end(); ++it)
			Most = std::max(Most, it->second);
		Lines.push_back(Format("Drums: %d different bars in %d; %s.", static_cast<int>(Prints.size()), BarCount,
			Fills > 0 ? Plural(Fills, "phrase ends with a change (a fill)", "phrase ends change (fills)").c_str() : "no fills where phrases end"));
		if (Fills == 0 && BarCount >= 8 && !Calm)
			Tip(1.0, "The drums never change where a phrase ends: add_drums with fill_every 4 (or 8) adds fills and crashes.");
		else if (Most * 10 >= BarCount * 9 && BarCount >= 8 && !Calm)
			Tip(1.0, "The drums repeat one bar almost all the way: add_drums varies every other bar and fills phrase ends.");
		// Every drum on the noise, one at a time
		int Struck = 0;
		for (size_t i = 0; i < Notes.size() && Samples < 0; ++i) {
			const InstrumentInfo *pInfo = Notes[i].Row >= First && Notes[i].Row < End ? FindInstrument(S.Song, Notes[i].Instrument) : 0;
			const std::string Name = pInfo == 0 ? std::string() : Lowercase(pInfo->Name);
			if (Name.find("kick") != std::string::npos || Name.find("snare") != std::string::npos)
				++Struck;
		}
		if (Samples < 0 && Struck >= 2 * BarCount && !Calm)
			Tip(0.5, "Every drum plays on the noise channel, one at a time, so the hi-hats drop out under the kicks and snares: "
				"add_drums with kit samples puts a sampled kick and snare on the DPCM channel, punchier, and leaves the noise to "
				"the hi-hats.");
	}

	// The melody
	{
		int Low = 1000, High = -1, Rests = 0, LongBare = 0;
		std::set<int> Lengths;
		// Vibrato from a 4xy still on from before, or built into the instrument
		const std::vector<int> Sticky = StickyAt(S.Cells(Melody), Tune, S.Channel(Melody).EffectColumns, EFF_VIBRATO);
		std::set<int> Wobbly;
		for (size_t i = 0; i < S.Song.Instruments.size(); ++i)
			if (S.Song.Instruments[i].Vibrato)
				Wobbly.insert(S.Song.Instruments[i].Index);
		for (size_t i = 0; i < Tune.size(); ++i) {
			const NoteInfo &N = Tune[i];
			Low = std::min(Low, N.Pitch);
			High = std::max(High, N.Pitch);
			Lengths.insert(N.Rows);
			if (i + 1 < Tune.size()) {
				if (Tune[i + 1].Row - (N.Row + N.Rows) >= R || (N.EndNote != NOTE_NONE && Tune[i + 1].Row - N.Row - N.Rows >= R / 2))
					++Rests;
			}
			bool Expression = (Sticky[i] & 0x0F) != 0 || Wobbly.count(N.Instrument) > 0;
			for (size_t e = 0; e < N.Effects.size(); ++e)
				Expression = Expression || N.Effects[e].second.first == EFF_VIBRATO;
			if (N.Rows >= 2 * R && !Expression)
				++LongBare;
		}
		std::string Line = "Melody: " + PitchName(Low) + " to " + PitchName(High) + Format(" (%d semitones), %s, %s", High - Low,
			Plural(static_cast<int>(Lengths.size()), "note length", "note lengths").c_str(),
			Rests == 0 ? "no rests" : Plural(Rests, "rest", "rests").c_str());
		// Phrases that come back note for note
		std::vector<int> Repeats;
		const std::vector<NoteInfo> &All = S.Notes(Melody);
		for (size_t b = 4; b < Bars.size(); ++b) {
			const std::string Print = BarPrint(All, Bars[b].Row, Bars[b].Row + Bars[b].Rows);
			if (!Print.empty() && Print == BarPrint(All, Bars[b - 4].Row, Bars[b - 4].Row + Bars[b - 4].Rows))
				Repeats.push_back(S.Time.BarOf(Bars[b].Row));
		}
		if (Repeats.size() == 1)
			Line += Format("; bar %d repeats the bar four before it", Repeats[0]);
		else if (!Repeats.empty())
			Line += "; bars " + BarList(Repeats) + " repeat the bars four before them";
		Lines.push_back(Line + ".");
		if (Lengths.size() <= 2 && Tune.size() >= 8)
			Tip(1.0, "The melody keeps one or two note lengths: mix long and short notes (a dotted rhythm, a held note "
				"at each phrase end, a run of quick notes) with write_notes.");
		if (Rests == 0 && BarCount >= 8)
			Tip(1.0, "The melody never rests: a breath of a beat or two between phrases (every 2 or 4 bars) helps it "
				"sound sung, and leaves room for a counter-melody to answer.");
		if (High - Low < 7 && Tune.size() >= 8)
			Tip(1.0, "The melody stays within a few notes: let a phrase climb to a high point (a climax) and fall back.");
		if (!Repeats.empty() && Repeats.size() * 4 >= static_cast<size_t>(BarCount))
			Tip(1.0, "Phrases come back unchanged: vary the repeat (another ending, an octave up, a counter-melody "
				"only the second time, busier drums).");
		if (LongBare > 0)
			Tip(0.5, Plural(LongBare, "long melody note has", "long melody notes have") + std::string(" no vibrato: polish adds a "
				"gentle one that comes in after half a beat."));
	}

	// Dynamics: parts at one volume throughout
	std::vector<int> Flat;
	for (size_t p = 0; p < Playing.size(); ++p) {
		const int c = Playing[p];
		if (S.Channel(c).Kind == KIND_TRIANGLE || S.Channel(c).Kind == KIND_DPCM ||
			std::find(Chords.begin(), Chords.end(), c) != Chords.end())
			continue;	// chord stabs keep a level on purpose
		const std::vector<NoteInfo> Notes = NotesIn(S.Notes(c), First, End);
		std::set<int> Volumes;
		for (size_t i = 0; i < Notes.size(); ++i)
			Volumes.insert(Notes[i].Volume);
		if (Notes.size() >= 8 && Volumes.size() == 1)
			Flat.push_back(c);
	}
	if (!Flat.empty()) {
		Lines.push_back("Dynamics: every note at one volume on " + Names(Flat) + ".");
		if (!Calm)
			Tip(1.0, "Flat dynamics sound typed: polish adds accents (the bar's first note loudest, the notes between beats "
				"softer).");
	}

	// The mix: every part near full volume buries the triangle and sounds harsh
	{
		int Parts = 0, Loud = 0;
		for (size_t p = 0; p < Playing.size(); ++p) {
			const int Kind = S.Channel(Playing[p]).Kind;
			if (Kind == KIND_TRIANGLE || Kind == KIND_DPCM || Kind == KIND_NOISE)
				continue;
			const std::vector<NoteInfo> Notes = NotesIn(S.Notes(Playing[p]), First, End);
			int Loudest = 0;
			for (size_t i = 0; i < Notes.size(); ++i)
				Loudest = std::max(Loudest, Notes[i].Volume);
			++Parts;
			if (Loudest >= 13)
				++Loud;
		}
		if (Parts >= 2 && Loud == Parts) {
			Lines.push_back(Format("Mix: %s near full volume.", Parts == 2 ? "both parts with a volume play" :
				"every part with a volume plays"));
			Tip(0.5, "Everything near full volume buries the triangle and sounds harsh: mix (style soft) brings the melody to 9, "
				"an echo to 4 and the rest to 6-7, and softens the triangle and the noise a little with the DPCM level.");
		}
	}

	// Levels: the melody well under another part, counted as FamiTracker plays each chip (the VRC7
	// louder and 3 dB a step, the N163 quieter); a soft mix keeps the triangle a little over the melody
	if (S.Channel(Melody).Kind != KIND_TRIANGLE) {
		auto Typical = [&](int c) {
			const std::vector<NoteInfo> Notes = NotesIn(S.Notes(c), First, End);
			std::vector<int> Volumes;
			for (size_t i = 0; i < Notes.size(); ++i)
				Volumes.push_back(Notes[i].Volume);
			if (Volumes.empty())
				return -60.0;
			std::sort(Volumes.begin(), Volumes.end());
			return LevelDb(S.Channel(c), Volumes[Volumes.size() * 3 / 4]);	// its louder notes
		};
		const double Lead = Typical(Melody);
		int Over = -1;
		double Most = 0.0;
		for (size_t p = 0; p < Playing.size(); ++p) {
			const int c = Playing[p];
			const int Kind = S.Channel(c).Kind;
			if (c == Melody || Kind == KIND_NOISE || Kind == KIND_DPCM || std::find(Echoes.begin(), Echoes.end(), c) != Echoes.end())
				continue;
			const double Above = (Kind == KIND_TRIANGLE ? -1.0 : Typical(c)) - Lead - (Kind == KIND_TRIANGLE ? 8.0 : 3.0);
			if (Above > Most) {
				Most = Above;
				Over = c;
			}
		}
		if (Over >= 0) {
			const double Under = Most + (S.Channel(Over).Kind == KIND_TRIANGLE ? 8.0 : 3.0);
			Lines.push_back(Format("Levels: the melody plays about %d dB under %s.", static_cast<int>(std::floor(Under + 0.5)),
				S.Channel(Over).FullName().c_str()));
			Tip(1.0, Format("The melody plays about %d dB under %s, so it hardly comes through: mix (style soft) sets each part's "
				"level for its chip%s.", static_cast<int>(std::floor(Under + 0.5)), S.Channel(Over).FullName().c_str(),
				S.Channel(Melody).Kind == KIND_FM ? " (the VRC7 counts its volume in steps of 3 dB: 9 is 18 dB under 15)" : ""));
		}
	}

	// Register: another part sitting on the melody
	const int TuneMiddle = MiddleOf(Tune, 52) - (S.Channel(Melody).Kind == KIND_TRIANGLE ? 12 : 0);
	std::vector<int> Crowding;
	for (size_t p = 0; p < Playing.size(); ++p) {
		const int c = Playing[p];
		if (c == Melody || c == Bass || !IsMelodic(S.Channel(c)))
			continue;
		const int Middle = MiddleOf(NotesIn(S.Notes(c), First, End), 0) - (S.Channel(c).Kind == KIND_TRIANGLE ? 12 : 0);
		if (std::abs(Middle - TuneMiddle) <= 3 && std::find(Echoes.begin(), Echoes.end(), c) == Echoes.end())
			Crowding.push_back(c);
	}
	if (!Crowding.empty()) {
		Lines.push_back("Register: " + Names(Crowding) + " plays around the melody's own notes (" + PitchName(TuneMiddle) + ").");
		Tip(1.0, "Parts in the melody's register blur it: move them an octave down (transpose with semitones -12) or "
			"the melody up.");
	}

	// Parallel octaves with the bass
	if (Bass >= 0) {
		const std::vector<NoteInfo> Low = NotesIn(S.Notes(Bass), First, End);
		int Parallel = 0;
		for (size_t i = 0; i + 1 < Tune.size(); ++i) {
			const NoteInfo *pA = 0, *pB = 0;
			for (size_t k = 0; k < Low.size(); ++k) {
				if (Low[k].Row == Tune[i].Row)
					pA = &Low[k];
				if (Low[k].Row == Tune[i + 1].Row)
					pB = &Low[k];
			}
			if (pA != 0 && pB != 0 && (Tune[i].Pitch - pA->Pitch) % 12 == 0 && (Tune[i + 1].Pitch - pB->Pitch) % 12 == 0 &&
				Tune[i].Pitch != Tune[i + 1].Pitch)
				++Parallel;
		}
		if (Parallel >= 3) {
			Lines.push_back(Format("The bass doubles the melody in octaves %d times as both move.", Parallel));
			Tip(0.5, "A bass moving in octaves with the melody sounds hollow: let it move the other way, or hold.");
		}
	}

	// Everything repeating: the whole arrangement four bars on
	std::vector<int> Copies;
	for (size_t b = 4; b < Bars.size(); ++b) {
		bool Same = true;
		for (size_t p = 0; p < Playing.size() && Same; ++p) {
			const std::vector<NoteInfo> &Notes = S.Notes(Playing[p]);
			Same = BarPrint(Notes, Bars[b].Row, Bars[b].Row + Bars[b].Rows) == BarPrint(Notes, Bars[b - 4].Row, Bars[b - 4].Row + Bars[b - 4].Rows);
		}
		if (Same)
			Copies.push_back(S.Time.BarOf(Bars[b].Row));
	}
	if (!Copies.empty()) {
		Lines.push_back(Copies.size() == 1 ? Format("Bar %d repeats the bar four before it in every part.", Copies[0]) :
			"Bars " + BarList(Copies) + " repeat the bars four before them in every part.");
		Tip(1.0, "A repeat with nothing new is where a loop starts to sound like a loop: change one or two layers the "
			"second time (a counter-melody, drums one step busier, chords in another style, the melody an octave up).");
	}

	// Length
	const double Bpm = BeatsPerMinute(S.Song.Speed, S.Song.Tempo, S.Song.FrameRate, R);
	const double Seconds = Bpm > 0 ? BarCount * (S.Time.RowsPerBar / static_cast<double>(R)) * 60.0 / Bpm : 0.0;
	Lines.push_back(Format("Length: %d bars, %s at %s BPM.", BarCount, SecondsText(Seconds).c_str(), FormatNumber(std::floor(Bpm + 0.5)).c_str()));
	if (Seconds > 0 && Seconds < 30 && BarCount <= 16)
		Tip(0.0, "Under half a minute before it loops: game music heard for minutes wants more material, an A part and "
			"a B part (16 to 32 bars or more).");

	const int Score = std::max(1, std::min(10, static_cast<int>(std::floor(10.0 - Missing + 0.5))));
	std::string Text = "Depth review, " + RangeText(From, To) + " in " + KeyName(K) + Format(": %d/10.\n", Score);
	for (size_t i = 0; i < Lines.size(); ++i)
		Text += Lines[i] + "\n";
	if (Tips.empty())
		Text += "Nothing obvious is missing. Listen with the user (play) and ask what they'd change.";
	else {
		Text += "What would help, most first:\n";
		for (size_t i = 0; i < Tips.size(); ++i)
			Text += Format("%d. ", static_cast<int>(i) + 1) + Tips[i] + "\n";
	}
	return Text;
}

// Grooves to start from, sections repeated and varied, the soundtrack's notes

struct Mood {
	const char *Name;
	const char *Aliases;	// separated by "|"
	const char *Use;
	bool Minor;
	const char *Chords;		// as degrees, four bars
	const char *Color;
	int Style, Rhythm, Pattern, Bass, Groove, Intensity, Bpm;
	const char *ChordSound;
};

const Mood MOODS[] = {
	{ "town", "village|peaceful|home|friendly|inn", "a peaceful, friendly town", false, "| I | vi | IV | V |", "sevenths",
		CHORDS_BROKEN, -1, BROKEN_UPDOWN, BASS_ROOT_FIFTH, GROOVE_CALM, 1, 112, "pluck" },
	{ "overworld", "adventure|field|heroic|hero|journey|exploration|world", "setting out on an adventure", false,
		"| I | bVII | IV | I |", "plain", CHORDS_ARPEGGIO, RHYTHM_TRESILLO, BROKEN_UP, BASS_OCTAVES, GROOVE_BASIC, 2, 140, "soft chords" },
	{ "battle", "fight|combat|enemy", "a fight", true, "| i | VI | VII | i |", "plain", CHORDS_STABS, RHYTHM_EIGHTHS, BROKEN_UP,
		BASS_DRIVING, GROOVE_DRIVING, 3, 165, "pluck" },
	{ "boss", "final|showdown|villain", "a boss fight: darker and tenser", true, "| i | iv | bII | V |", "plain", CHORDS_STABS,
		RHYTHM_EIGHTHS, BROKEN_UP, BASS_OCTAVES, GROOVE_DRIVING, 3, 175, "pluck" },
	{ "dungeon", "cave|mysterious|mystery|spooky|haunted|ruins|creepy", "a dark cave, ruins or a haunted place", true,
		"| i | bII | i | bII |", "plain", CHORDS_GUIDE, -1, BROKEN_UP, BASS_PEDAL, GROOVE_CALM, 1, 90, "warm pad" },
	{ "sad", "melancholy|tragic|lament|loss|sorrow", "a sad or tragic scene", true, "| i | VI | III | VII |", "rich", CHORDS_BROKEN,
		-1, BROKEN_ROLLING, BASS_ROOTS, -1, 0, 80, "pluck" },
	{ "calm", "night|rest|ambient|sleep|quiet|gentle", "night, rest or a quiet place", false, "| Imaj7 | IVmaj7 | Imaj7 | IVmaj7 |",
		"plain", CHORDS_ARPEGGIO, -1, BROKEN_UP, BASS_ROOTS, GROOVE_CALM, 1, 85, "soft chords" },
	{ "chase", "tense|action|escape|hurry|danger|race", "a chase or a race against time", true, "| i | i | VI | VII |", "plain",
		CHORDS_STABS, RHYTHM_EIGHTHS, BROKEN_UP, BASS_OCTAVES, GROOVE_FOUR_ON_FLOOR, 3, 170, "pluck" },
	{ "upbeat", "happy|pop|cheerful|fun|bright|catchy", "something cheerful and catchy", false, "| I | V | vi | IV |", "plain",
		CHORDS_STABS, RHYTHM_OFFBEATS, BROKEN_UP, BASS_SYNCOPATED, GROOVE_FOUR_ON_FLOOR, 2, 140, "pluck" },
	{ "shop", "jazzy|jazz|cafe|store|lounge|casino|bar", "a shop, a cafe or a casino: jazzy", false, "| ii7 | V7 | Imaj7 | VI7 |",
		"plain", CHORDS_STABS, RHYTHM_CHARLESTON, BROKEN_UP, BASS_WALKING, GROOVE_SHUFFLE, 2, 120, "pluck" },
	{ "underwater", "dreamy|dream|water|floating|sky|clouds", "underwater or a dream: floating", false, "| Imaj7 | II | Imaj7 | II |",
		"plain", CHORDS_BROKEN, -1, BROKEN_ROLLING, BASS_ROOTS, -1, 0, 90, "pluck" },
	{ "castle", "royal|march|kingdom|throne|palace", "a castle or a royal march", false, "| I | IV | V | I |", "plain",
		CHORDS_STABS, RHYTHM_BEATS, BROKEN_UP, BASS_ROOT_FIFTH, GROOVE_MARCH, 2, 110, "pluck" },
	{ "desert", "exotic|sand|bazaar|ancient", "a desert or an exotic place", true, "| i | bII | i | V |", "plain", CHORDS_BROKEN,
		-1, BROKEN_UPDOWN, BASS_PEDAL, GROOVE_HALF_TIME, 1, 100, "pluck" },
	{ "nostalgic", "atmospheric|wistful|bittersweet|memories|memory|sentimental|longing|lush|warm|hometown",
		"nostalgic and warm: lush, jazzy chords ringing softly under the melody", false, "| IVmaj7 | V | iii7 | vi7 |", "rich",
		CHORDS_LUSH, RHYTHM_HALF, BROKEN_UP, BASS_SYNCOPATED, GROOVE_BASIC, 1, 96, "lush chords" },
};

const Mood *FindMood(const std::string &Name)
{
	const std::string Wanted = Squeezed(Name);
	for (size_t i = 0; i < sizeof(MOODS) / sizeof(MOODS[0]); ++i)
		if (Squeezed(MOODS[i].Name) == Wanted)
			return &MOODS[i];
	for (size_t i = 0; i < sizeof(MOODS) / sizeof(MOODS[0]); ++i) {
		const std::string Aliases = MOODS[i].Aliases;
		size_t From = 0;
		while (From <= Aliases.size()) {
			size_t To = Aliases.find('|', From);
			if (To == std::string::npos)
				To = Aliases.size();
			if (Squeezed(Aliases.substr(From, To - From)) == Wanted)
				return &MOODS[i];
			From = To + 1;
		}
	}
	return 0;
}

std::string MakeGrooveTool(Session &S, const Json &Args)
{
	const std::string Name = StringArg(Args, "mood");
	const Mood *pMood = FindMood(Name);
	if (pMood == 0) {
		std::string List;
		for (size_t i = 0; i < sizeof(MOODS) / sizeof(MOODS[0]); ++i)
			List += (i == 0 ? "" : ", ") + std::string(MOODS[i].Name);
		throw Problem("mood: \"" + Name + "\" isn't one. Moods: " + List + " (other words for them work too).");
	}
	const Mood &M = *pMood;
	const bool AsClip = BoolArg(Args, "as_clip", false);
	const bool DirectChange = Direct(S, AsClip);
	const int R = S.Time.RowsPerBeat;
	const int From = IntArg(Args, "from_bar", 1, 1, S.MaxBars());
	const int To = std::min(S.MaxBars(), From + IntArg(Args, "bars", 8, 1, 64) - 1);
	const int First = S.Time.BarStart(From), End = S.Time.BarEnd(To);
	const std::vector<BarSpan> Bars = SongBarSpans(S, First, End);
	const bool Sampled = SampledKitArg(Args);

	// The key: as given, else the song's key note in the mood's mode
	Key K;
	const std::string KeyText = StringArg(Args, "key");
	if (!KeyText.empty()) {
		if (!ParseKey(KeyText, K))
			throw Problem("key: \"" + KeyText + "\" isn't a key. Write it like \"C major\", \"A minor\" or \"Bbm\".");
	}
	else {
		Key Found;
		const bool Any = FindKey(S, 0, S.Time.TotalRows(), Found);
		K = Key(Any ? Found.Tonic : (M.Minor ? 9 : 0), M.Minor ? MODE_MINOR : MODE_MAJOR);
	}

	auto Pick = [&](const char *Arg_, bool (*Fits)(const ChannelInfo &), const char *Kind) -> int {
		const Json *pValue = Arg(Args, Arg_);
		if (pValue != 0 && pValue->IsString() && Lowercase(pValue->String()) == "none")
			return -1;
		if (pValue != 0) {
			const int Channel = ParseChannel(S, *pValue, Arg_);
			if (!Fits(S.Channel(Channel)))
				throw Problem(Format("%s needs %s.", Arg_, Kind));
			return Channel;
		}
		const int Free = FreeChannel(S, First, End, -1, Fits);
		return Free >= 0 ? Free : -1;
	};
	struct Local {
		static bool IsNoise(const ChannelInfo &Info) { return Info.Kind == KIND_NOISE; }
	};
	// Lush chords need a pulse; on another channel (a VRC7 one) they're held as the 0xy arpeggio
	int ChordChannel = Pick("chord_channel", M.Style == CHORDS_LUSH ? IsLushChordChannel : IsChordChannel, "a pulse channel");
	int ChordStyle = M.Style;
	if (M.Style == CHORDS_LUSH && ChordChannel < 0 && Arg(Args, "chord_channel") == 0) {
		ChordChannel = Pick("chord_channel", IsChordChannel, "a pulse channel");
		ChordStyle = CHORDS_ARPEGGIO;
	}
	const int BassChannel = Pick("bass_channel", IsBassChannel, "the triangle or the sawtooth");
	const int DrumChannel = M.Groove >= 0 ? Pick("drum_channel", Local::IsNoise, "the noise channel") : -1;
	if (ChordChannel < 0 && BassChannel < 0 && DrumChannel < 0)
		throw Problem("Every channel the groove needs already plays in those bars; name them (chord_channel, bass_channel, "
			"drum_channel), or start at another bar.");
	// A melody already written there: the chords go under it and keep their added tones off its notes
	std::vector<NoteInfo> Tune;
	const int Melody = GuessMelodyChannel(S, First, End);
	if (Melody >= 0 && Melody != ChordChannel && Melody != BassChannel && !IsBass(S.Channel(Melody)))
		Tune = NotesIn(S.Notes(Melody), First, End);

	std::vector<ChordAt> Chords;
	std::string Error;
	if (!LayOutProgression(SplitProgression(M.Chords), Bars, R, K, Chords, Error))
		throw Problem(Error);
	chord_color Color;
	ParseColor(M.Color, Color);
	ColorChords(Chords, Tune, K, R, Color);

	std::vector<Change> Changes;
	std::vector<std::string> Warnings, Parts;
	if (ChordChannel >= 0) {
		ChordPartOptions O;
		O.Style = ChordStyle;
		O.Rhythm = M.Rhythm;
		O.Pattern = M.Pattern;
		O.StepRows = std::max(1, R / 2);
		O.RowsPerBeat = R;
		O.RootInBass = BassChannel >= 0;
		O.Volume = PartVolume(S.Channel(ChordChannel), ChordStyle == CHORDS_STABS ? 11 : ChordStyle == CHORDS_BROKEN ? 10 :
			ChordStyle == CHORDS_LUSH ? 6 : 15);
		ChordRegister(O, S.Channel(ChordChannel), Tune);
		std::vector<PartNote> Notes = ChordPart(Chords, Bars, O);
		if (ChordStyle == CHORDS_LUSH)
			LushInstruments(S, ChordChannel, M.ChordSound, O.Pattern, 2, Notes);
		else {
			const int Instrument = ResolveInstrument(S, ChordStyle == M.Style ? M.ChordSound : "soft chords", ChordChannel);
			for (size_t i = 0; i < Notes.size(); ++i)
				Notes[i].Instrument = Instrument;
		}
		Change C;
		C.Channel = ChordChannel;
		BuildPart(S.Cells(ChordChannel), First, End, Notes, OptionsFor(S, ChordChannel, DirectChange), C.Cells, Warnings);
		Changes.push_back(C);
		std::string Kind = ChordStyles()[static_cast<size_t>(ChordStyle)].Name;
		if (ChordStyle == CHORDS_BROKEN)
			Kind += std::string(" (") + BrokenPatterns()[static_cast<size_t>(M.Pattern)].Name + ")";
		else if (M.Rhythm >= 0)
			Kind += std::string(" (") + ChordRhythms()[static_cast<size_t>(M.Rhythm)].Name + ")";
		Parts.push_back(Kind + " chords on " + S.Channel(ChordChannel).FullName());
	}
	if (BassChannel >= 0) {
		BassOptions O;
		O.Style = M.Bass;
		O.RowsPerBeat = R;
		O.K = K;
		BassRegister(O, S.Channel(BassChannel));
		const int Instrument = ResolveInstrument(S, "bass", BassChannel);
		std::vector<PartNote> Notes = BassPart(Chords, Bars, O);
		for (size_t i = 0; i < Notes.size(); ++i)
			Notes[i].Instrument = Instrument;
		Change C;
		C.Channel = BassChannel;
		BuildPart(S.Cells(BassChannel), First, End, Notes, OptionsFor(S, BassChannel, DirectChange), C.Cells, Warnings);
		Changes.push_back(C);
		Parts.push_back(std::string("a ") + BassStyles()[static_cast<size_t>(M.Bass)].Name + " bass on " + S.Channel(BassChannel).FullName());
	}
	if (DrumChannel >= 0) {
		DrumOptions O;
		O.Groove = M.Groove;
		O.Intensity = M.Intensity;
		O.FillEvery = 4;
		O.PhraseStart = (From - 1) % 4;
		O.RowsPerBeat = R;
		O.Samples = Sampled;
		std::string Where;
		DrumChanges(S, DrumChannel, Sampled ? DpcmChannel(S) : -1, DrumPart(Bars, O), First, End, 15, DirectChange, Changes, Where, Warnings);
		Parts.push_back(std::string(Grooves()[static_cast<size_t>(M.Groove)].Name) + " drums" + Where);
	}
	std::string Summary;
	for (size_t i = 0; i < Parts.size(); ++i)
		Summary += (i == 0 ? "" : i + 1 == Parts.size() ? " and " : ", ") + Parts[i];
	std::sort(Changes.begin(), Changes.end(), [](const Change &a, const Change &b) { return a.Channel < b.Channel; });
	const std::string What = std::string("a ") + M.Name + " groove: " + Summary + ", " + RangeText(From, To) + ", in " + KeyName(K);
	std::string Text = Commit(S, Changes, First, AsClip, StringArg(Args, "clip_name"), What);
	Text += "\nChords: " + ProgressionText(Chords, Bars, K) + "\nAs degrees: " + ProgressionText(Chords, Bars, K, true);
	const double Now = BeatsPerMinute(S.Song.Speed, S.Song.Tempo, S.Song.FrameRate, R);
	if (std::fabs(Now - M.Bpm) > 8)
		Text += Format("\nThe mood suits about %d BPM; the song plays at %s (set_tempo with bpm %d changes it).", M.Bpm,
			FormatNumber(std::floor(Now + 0.5)).c_str(), M.Bpm);
	// Where the melody can go
	int Free = -1;
	for (int c = 0; c < S.ChannelCount() && Free < 0; ++c)
		if (c != ChordChannel && c != BassChannel && IsChordChannel(S.Channel(c)) && !HasNotesIn(S, c, First, End))
			Free = c;
	Text += "\nNext: a melody in " + KeyName(K) + " over these chords" + (Free >= 0 ? " on " + S.Channel(Free).FullName() : std::string()) +
		" with write_notes (chord tones on the strong beats, steps between them, a rest at the end of each phrase), then "
		"review_arrangement.";
	return WithRemarks(S, Text, Warnings);
}

// copy_bars: a section again, later (A A B A)

std::string EndPosition(const Session &S, int LastRow);
std::string StartPosition(const Session &S, int Row);

std::string CopyBarsTool(Session &S, const Json &Args)
{
	int From, To;
	BarRange(S, Args, From, To);
	if (Arg(Args, "to_bar") == 0)
		To = From;
	const int At = IntArg(Args, "at_bar", 0, 1, S.MaxBars());
	if (At == 0)
		throw Problem("Say where the copy goes with at_bar.");
	const int Times = IntArg(Args, "times", 1, 1, 16);
	const int Semitones = IntArg(Args, "semitones", 0, -24, 24);
	const bool AsClip = BoolArg(Args, "as_clip", false);
	const int First = S.Time.BarStart(From), End = S.Time.BarEnd(To);
	const int Length = End - First;
	const int Dest = S.Time.BarStart(At);
	if (Dest < End && Dest + Length * Times > First)
		throw Problem("The copy would land on the bars it copies; choose an at_bar after them (or before).");
	if (Dest + Length * Times > S.MaxRows())
		throw Problem(Format("That runs past the most a song can hold (%d bars).", S.MaxBars()));
	std::vector<int> Channels;
	const Json *pChannels = Arg(Args, "channels");
	if (pChannels == 0 || (pChannels->IsString() && Lowercase(pChannels->String()) == "all"))
		for (int c = 0; c < S.ChannelCount(); ++c)
			Channels.push_back(c);
	else
		Channels = ChannelsArg(S, Args, "channels");

	Draft D(S);
	int Notes = 0;
	bool Crowded = false;
	for (size_t i = 0; i < Channels.size(); ++i) {
		const int c = Channels[i];
		const ChannelInfo &Info = S.Channel(c);
		const int Columns = Info.EffectColumns;
		const std::vector<Cell> &Song = S.Cells(c);
		std::vector<Cell> Part;
		for (int r = First; r < End; ++r)
			Part.push_back(r < static_cast<int>(Song.size()) ? Song[static_cast<size_t>(r)] : Cell());
		// Bars that start without a note of their own start the copy as they
		// did: silent (with the end that silenced them, so a note playing where
		// the copy goes stops) or with the note ringing into them, struck again
		int StartEnd = NOTE_NONE;
		if (!Part.empty() && Part[0].Note == NOTE_NONE && IsMelodic(Info)) {
			int Back = std::min(First, static_cast<int>(Song.size())) - 1;
			while (Back >= 0 && Song[static_cast<size_t>(Back)].Note == NOTE_NONE)
				--Back;
			if (Back < 0 || Song[static_cast<size_t>(Back)].IsEnd())
				StartEnd = Back < 0 ? NOTE_HALT : Song[static_cast<size_t>(Back)].Note;
			else
				Part[0].SetPitch(Song[static_cast<size_t>(Back)].Pitch());	// moved with the others below
		}
		// The copy plays as the original does: its first note gets the instrument and volume it played with
		const ChannelState State = StateAt(Song, First, Columns);
		for (size_t r = 0; r < Part.size(); ++r) {
			Cell &C = Part[r];
			for (int k = 0; k < EFFECT_COLUMNS; ++k)
				if (C.Effect[k] == EFF_JUMP || C.Effect[k] == EFF_SKIP || C.Effect[k] == EFF_HALT) {
					C.Effect[k] = EFF_NONE;	// the song's own jumps and ends stay where they are
					C.Param[k] = 0;
				}
			if (C.HasNote()) {
				++Notes;
				if (Semitones != 0 && IsMelodic(Info)) {
					const int Pitch = C.Pitch() + Semitones;
					if (Pitch >= 0 && Pitch < PITCHES)
						C.SetPitch(Pitch);
				}
			}
		}
		for (size_t r = 0; r < Part.size(); ++r) {
			if (!Part[r].HasNote())
				continue;
			if (Part[r].Instrument == NO_INSTRUMENT && State.Instrument != NO_INSTRUMENT)
				Part[r].Instrument = static_cast<unsigned char>(State.Instrument);
			if (Part[r].Volume == NO_VOLUME && State.Volume != 15)
				Part[r].Volume = static_cast<unsigned char>(State.Volume);
			for (int e = 0; e < EFF_COUNT; ++e)
				if (State.Sticky[e] != StickyOff(e) && !HasEffect(Part[r], e, Columns))
					AddEffect(Part[r], e, State.Sticky[e], Columns);
			break;
		}
		for (int t = 0; t < Times; ++t) {
			std::vector<Cell> Copy(Part);
			const size_t Row = static_cast<size_t>(Dest + t * Length);
			if (StartEnd != NOTE_NONE) {
				// The end the place had already, or the original's
				const std::vector<Cell> &Here = D.Cells(c);
				const bool Ends = Row < Here.size() && Here[Row].IsEnd();
				Copy[0].Note = static_cast<unsigned char>(Ends ? Here[Row].Note : StartEnd);
				Copy[0].Octave = 0;
			}
			// The song's own speed, jumps and halts there stay
			for (size_t r = 0; r < Copy.size() && Row + r < Song.size(); ++r)
				if (!KeepSongEffects(Song[Row + r], Copy[r], Columns))
					Crowded = true;
			D.Put(c, static_cast<int>(Row), Copy);
		}
	}
	if (Notes == 0)
		throw Problem(Format("There are no notes in %s to copy.", RangeText(From, To).c_str()));
	std::vector<Change> Changes;
	std::vector<std::string> Warnings;
	int Start;
	if (!D.Collect(Dest, Direct(S, AsClip), Changes, Start, Warnings))
		throw Problem("Those bars already hold the same notes.");
	if (Crowded)
		Warnings.push_back("Some copied effects found no free effect column beside the song's speed, jump and halt effects there "
			"and were left out (all four were full).");
	// Copies the song never gets to: after its loop's jump back, or its halt
	if (Direct(S, AsClip)) {
		int Total = 0;
		const SongFlow After = D.Flow(Total);
		const std::vector<bool> Played = After.Played(Total, S.Time.PatternLength);
		int Low = -1, High = -1;
		for (int r = Dest; r < Dest + Length * Times; ++r)
			if (r >= Total || !Played[static_cast<size_t>(r)]) {
				Low = Low < 0 ? r : Low;
				High = r;
			}
		if (Low >= 0) {
			const int LastPlayed = After.EndRow(S.Time.PatternLength) - 1;
			std::string Bars = RangeText(S.Time.BarOf(Low), S.Time.BarOf(High));
			Bars[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(Bars[0])));
			const bool One = S.Time.BarOf(Low) == S.Time.BarOf(High);
			const char *Verb = One ? "doesn't" : "don't", *Them = One ? "it" : "them";
			if (After.Halts())
				Warnings.push_back(Format("%s %s play yet: the song stops after %s (a halt, Cxx), before %s. set_loop with "
					"to_bar %d makes it play on through %s.", Bars.c_str(), Verb, EndPosition(S, LastPlayed).c_str(), Them,
					S.Time.BarOf(High), Them));
			else
				Warnings.push_back(Format("%s %s play yet: the song goes back to %s after %s, before %s. set_loop with "
					"to_bar %d takes the loop on through %s.", Bars.c_str(), Verb,
					StartPosition(S, After.LoopRow(S.Time.PatternLength)).c_str(), EndPosition(S, LastPlayed).c_str(), Them,
					S.Time.BarOf(High), Them));
		}
	}
	const int Last = At + Times * (To - From + 1) - 1;
	std::string What = RangeText(From, To) + " copied to " + RangeText(At, Last);
	if (Times > 1)
		What += Format(" (%d times)", Times);
	if (Semitones != 0)
		What += Format(", %d semitones %s", std::abs(Semitones), Semitones > 0 ? "up" : "down");
	return WithRemarks(S, Commit(S, Changes, Start, AsClip, StringArg(Args, "clip_name"), What) +
		" A repeat sounds fresher with something changed: vary_part on the melody, a counter-melody, busier drums.", Warnings);
}

// vary_part: the same part, a little different (for a repeat)

std::string VaryPartTool(Session &S, const Json &Args)
{
	const int Channel = ChannelArg(S, Args, "channel");
	const ChannelInfo &Info = S.Channel(Channel);
	if (!IsMelodic(Info))
		throw Problem("vary_part changes melodic parts; add_drums makes drums busier or lighter.");
	if (Arg(Args, "from_bar") == 0)
		throw Problem("Say which bars with from_bar and to_bar.");
	int From, To;
	BarRange(S, Args, From, To);
	const int First = S.Time.BarStart(From), End = S.Time.BarEnd(To);
	const int R = S.Time.RowsPerBeat;
	const bool AsClip = BoolArg(Args, "as_clip", false);
	static const char *const KINDS[] = { "embellish", "simplify", "syncopate", "octave_up", "octave_down", "ending" };
	const std::string Kind = Squeezed(StringArg(Args, "variation"));
	int Variation = -1;
	for (int i = 0; i < 6; ++i)
		if (Kind == Squeezed(KINDS[i]))
			Variation = i;
	if (Variation < 0)
		throw Problem("variation: embellish, simplify, syncopate, octave_up, octave_down or ending.");
	bool Found;
	Key K = KeyArg(S, Args, "key", First, End, Found);
	if (!Found)
		K = Key(0, MODE_MAJOR);

	const std::vector<NoteInfo> Old = NotesIn(S.Notes(Channel), First, End);
	if (Old.empty())
		throw Problem(Format("%s has no notes in %s.", Info.FullName().c_str(), RangeText(From, To).c_str()));
	std::vector<PartNote> Notes;
	for (size_t i = 0; i < Old.size(); ++i) {
		const NoteInfo &N = Old[i];
		PartNote P;
		P.Row = N.Row;
		P.Rows = std::min(N.Rows, End - N.Row);
		P.Pitch = N.Pitch;
		P.Instrument = N.InstrumentSet || i == 0 ? N.Instrument : NO_INSTRUMENT;
		P.Volume = N.VolumeSet || i == 0 ? N.Volume : -1;
		for (size_t e = 0; e < N.Effects.size(); ++e) {
			if (IsSongEffect(N.Effects[e].second.first))
				continue;
			if (N.Effects[e].first == 0)
				P.Effects.push_back(N.Effects[e].second);
			else
				P.LaterEffects.push_back(N.Effects[e]);
		}
		if (N.EndNote == NOTE_NONE && i + 1 < Old.size())
			P.Rows = Old[i + 1].Row - N.Row;
		Notes.push_back(P);
	}
	auto Step = [&](int Pitch, int Direction) {
		for (int s = 1; s <= 2; ++s)
			if (InKey(Pitch + Direction * s, K))
				return Pitch + Direction * s;
		return Pitch + Direction;
	};
	int Changed = 0;
	std::vector<PartNote> Out;
	switch (Variation) {
	case 0: {	// passing notes between notes a third apart, a neighbour on a held one, at most two a bar
		std::map<int, int> PerBar;
		for (size_t i = 0; i < Notes.size(); ++i) {
			PartNote A = Notes[i];
			const int Bar = S.Time.BarOf(A.Row);
			const bool Next = i + 1 < Notes.size() && Notes[i + 1].Row == A.Row + A.Rows;
			const int Gap = Next ? Notes[i + 1].Pitch - A.Pitch : 99;
			if (A.Rows >= R && Next && PerBar[Bar] < 2 && (std::abs(Gap) == 3 || std::abs(Gap) == 4 || Gap == 0)) {
				const int Half = std::max(1, (A.Rows / 2) / std::max(1, R / 2) * std::max(1, R / 2));
				PartNote B = A;
				B.Row = A.Row + Half;
				B.Rows = A.Rows - Half;
				B.Pitch = Gap == 0 ? Step(A.Pitch, 1) : Step(A.Pitch, Gap > 0 ? 1 : -1);
				B.Instrument = NO_INSTRUMENT;
				B.Volume = -1;
				B.Effects.clear();
				B.LaterEffects.clear();
				A.Rows = Half;
				A.LaterEffects.clear();
				Out.push_back(A);
				Out.push_back(B);
				++PerBar[Bar];
				++Changed;
				continue;
			}
			Out.push_back(A);
		}
		break;
	}
	case 1:	// only the notes on beats, each held until the next
		for (size_t i = 0; i < Notes.size(); ++i) {
			const int InBar = Notes[i].Row - S.Time.BarStart(S.Time.BarOf(Notes[i].Row));
			if (InBar % R != 0 && !Out.empty() && Out.back().Row + Out.back().Rows == Notes[i].Row) {
				Out.back().Rows += Notes[i].Rows;
				++Changed;
				continue;
			}
			Out.push_back(Notes[i]);
		}
		break;
	case 2: {	// notes on the middle of a bar, and on every other bar's first beat, come half a beat early
		const int Half = std::max(1, R / 2);
		for (size_t i = 0; i < Notes.size(); ++i) {
			PartNote N = Notes[i];
			const int Bar = S.Time.BarOf(N.Row);
			const int InBar = N.Row - S.Time.BarStart(Bar);
			const int Middle = S.Time.BarLength(Bar) / R / 2 * R;
			const bool Strong = (InBar == Middle && Middle > 0) || (InBar == 0 && Bar % 2 == 0);
			if (Strong && !Out.empty() && Out.back().Row + Out.back().Rows == N.Row && Out.back().Rows >= R && N.Row - Half >= First) {
				Out.back().Rows -= Half;
				N.Row -= Half;
				N.Rows += Half;
				++Changed;
			}
			Out.push_back(N);
		}
		break;
	}
	case 3:
	case 4:
		for (size_t i = 0; i < Notes.size(); ++i) {
			const int Pitch = Notes[i].Pitch + (Variation == 3 ? 12 : -12);
			if (Pitch >= 0 && Pitch < PITCHES) {
				Notes[i].Pitch = Pitch;
				++Changed;
			}
			Out.push_back(Notes[i]);
		}
		break;
	default: {	// the last note on the key's tonic, held to the end of the bars
		Out = Notes;
		PartNote &Last = Out.back();
		int Best = Last.Pitch;
		for (int d = 0; d <= 6; ++d) {
			if (((Last.Pitch - d) % 12 + 12) % 12 == K.Tonic) {
				Best = Last.Pitch - d;
				break;
			}
			if (((Last.Pitch + d) % 12 + 12) % 12 == K.Tonic) {
				Best = Last.Pitch + d;
				break;
			}
		}
		Changed = Best != Last.Pitch || Last.Row + Last.Rows != End ? 1 : 0;
		Last.Pitch = Best;
		Last.Rows = End - Last.Row;
		break;
	}
	}
	if (Changed == 0)
		throw Problem("That variation found nothing to change in those bars (embellish needs notes of a beat or more a third "
			"apart; syncopate a long note before a strong beat).");
	std::vector<std::string> Warnings;
	Change C;
	C.Channel = Channel;
	BuildPart(S.Cells(Channel), First, End, Out, OptionsFor(S, Channel, Direct(S, AsClip)), C.Cells, Warnings);
	std::vector<Change> Changes(1, C);
	static const char *const DONE[] = { "passing and neighbour notes added", "simplified to the notes on the beats",
		"syncopated (strong-beat notes half a beat early)", "an octave up", "an octave down", "ending on the tonic" };
	const std::string What = Info.FullName() + " " + DONE[Variation] + ", " + RangeText(From, To);
	return WithRemarks(S, Commit(S, Changes, First, AsClip, StringArg(Args, "clip_name"), What) +
		Format(" (%s)", Plural(Changed, "change", "changes").c_str()), Warnings);
}

// The soundtrack's notes (SOUNDTRACK_NOTES): a text file beside the song,
// shared by every module in its folder, where the user's taste and the
// decisions about the game's music are kept between sessions

std::string SoundtrackNotesTool(Session &S, const Json &Args)
{
	const std::string Action = Lowercase(StringArg(Args, "action", "read"));
	std::string Text, Error;
	const bool Exists = S.Host().ReadFile(SOUNDTRACK_NOTES, Text, Error);
	if (!Exists && !Error.empty())
		throw Problem(Error);
	if (Action == "read") {
		if (!Exists || Trim(Text).empty())
			return std::string("There are no soundtrack notes yet (\"") + SOUNDTRACK_NOTES + "\" next to the song). Add what the user "
				"tells you they like and dislike, and what you decide together (keys, tempos, sounds, motifs for places and "
				"characters), with action \"add\", so later sessions keep to it.";
		return std::string("Soundtrack notes (\"") + SOUNDTRACK_NOTES + "\" next to the song):\n" + Text;
	}
	const std::string Line = StringArg(Args, "text");
	if (Line.empty())
		throw Problem("Give the text to write.");
	std::string New;
	if (Action == "add") {
		New = Text;
		if (!New.empty() && New[New.size() - 1] != '\n')
			New += "\r\n";
		New += "- " + Line + "\r\n";
	}
	else if (Action == "replace")
		New = Line + (Line.empty() || Line[Line.size() - 1] == '\n' ? "" : "\r\n");
	else
		throw Problem("action: read, add or replace.");
	std::string Path;
	if (!S.Host().SaveFile(SOUNDTRACK_NOTES, New, Path, Error))
		throw Problem(Error.empty() ? "The notes couldn't be saved." : Error);
	return Action == "add" ? "Added to the soundtrack notes (" + Path + ")." : "Rewrote the soundtrack notes (" + Path + ").";
}

// Moods: several changes at once, so they arrive together

// The changes for a mood or a version, read from change_mood's arguments
struct MoodPlan {
	bool ByKey;
	Key FromKey, ToKey;
	std::vector<std::pair<int, int> > Moves;	// channel, semitones
	bool HasVibrato;
	VibratoPlan Vibrato;
	std::vector<std::pair<int, int> > Sounds;	// channel, instrument
	bool HasEcho;
	EchoPlan Echo;
	double Bpm;									// 0: as it is

	MoodPlan() : ByKey(false), HasVibrato(false), Vibrato(), HasEcho(false), Echo(), Bpm(0.0) {}
};

// false for "none" (make_version's defaults are turned off that way)
bool Off(const Json *pValue)
{
	return pValue != 0 && pValue->IsBool() && !pValue->Bool();
}

// Everything is read first, so a mistake in one part changes nothing;
// instruments from presets are added last, since that changes the song
MoodPlan ReadMoodPlan(Session &S, const Json &Args, int First, int End)
{
	MoodPlan Plan;
	Plan.ByKey = KeyChangeArgs(S, Args, First, End, Plan.FromKey, Plan.ToKey);

	if (const Json *pMoves = Arg(Args, "moves")) {
		if (!pMoves->IsArray())
			throw Problem("moves should be a list like [{\"channel\": \"Triangle\", \"semitones\": -12}].");
		for (size_t i = 0; i < pMoves->Size(); ++i) {
			const Json &Move = pMoves->At(i);
			if (!Move.IsObject() || Arg(Move, "channel") == 0 || Arg(Move, "semitones") == 0)
				throw Problem("Each move needs a channel and semitones, like {\"channel\": \"Triangle\", \"semitones\": -12}.");
			const int Channel = ChannelArg(S, Move, "channel");
			if (!IsMelodic(S.Channel(Channel)))
				throw Problem(Format("%s has no notes to move up or down.", S.Channel(Channel).FullName().c_str()));
			Plan.Moves.push_back(std::make_pair(Channel, IntArg(Move, "semitones", 0, -48, 48)));
		}
	}

	const Json *pVibrato = Arg(Args, "vibrato");
	if (pVibrato != 0 && !Off(pVibrato)) {
		if (!pVibrato->IsObject())
			throw Problem("vibrato should be like {\"channel\": \"VRC6 Pulse 1\", \"speed\": \"slow\", \"depth\": \"strong\"}.");
		Plan.Vibrato = VibratoArgs(S, *pVibrato);
		Plan.HasVibrato = true;
	}

	const Json *pSounds = Arg(Args, "sounds");
	if (Off(pSounds))
		pSounds = 0;
	if (pSounds != 0) {
		if (!pSounds->IsArray())
			throw Problem("sounds should be a list like [{\"channel\": \"VRC6 Pulse 1\", \"instrument\": \"warm pad\"}].");
		for (size_t i = 0; i < pSounds->Size(); ++i) {
			const Json &Sound = pSounds->At(i);
			if (!Sound.IsObject() || Arg(Sound, "channel") == 0 || Arg(Sound, "instrument") == 0)
				throw Problem("Each sound needs a channel and an instrument, like {\"channel\": \"VRC6 Pulse 1\", \"instrument\": \"warm pad\"}.");
			ChannelArg(S, Sound, "channel");
		}
	}
	const Json *pEcho = Arg(Args, "echo");
	if (Off(pEcho))
		pEcho = 0;
	if (pEcho != 0 && !pEcho->IsObject())
		throw Problem("echo should be like {\"channel\": \"VRC6 Pulse 1\"}.");

	const double Now = BeatsPerMinute(S.Song.Speed, S.Song.Tempo, S.Song.FrameRate, S.Time.RowsPerBeat);
	if (Arg(Args, "bpm") != 0)
		Plan.Bpm = NumberArg(Args, "bpm", 0.0, 10.0, 1000.0);
	else if (Arg(Args, "tempo_percent") != 0)
		Plan.Bpm = Now * NumberArg(Args, "tempo_percent", 100.0, 20.0, 300.0) / 100.0;

	// Instruments last, since presets are added to the song
	for (size_t i = 0; pSounds != 0 && i < pSounds->Size(); ++i) {
		const int Channel = ChannelArg(S, pSounds->At(i), "channel");
		Plan.Sounds.push_back(std::make_pair(Channel, InstrumentArg(S, pSounds->At(i), "instrument", Channel, NO_INSTRUMENT)));
	}
	if (pEcho != 0) {
		Plan.Echo = EchoArgs(S, *pEcho, First, End);
		Plan.HasEcho = true;
	}
	return Plan;
}

// The plan's note changes in the draft: key or mode, parts moved, sounds,
// vibrato, then an echo of the part as it is by then
void ApplyMoodPlan(Session &S, Draft &D, const MoodPlan &Plan, int First, int End, std::vector<std::string> &Done,
	std::vector<std::string> &Warnings)
{
	// 1. Key or mode, for every melodic channel
	if (Plan.ByKey) {
		int Count = 0, Skipped = 0;
		bool Changed = false;
		std::map<int, int> Moved;
		const Key FromKey = Plan.FromKey, ToKey = Plan.ToKey;
		int Revoiced = 0, Unknown = 0;
		for (int c = 0; c < S.ChannelCount(); ++c) {
			if (IsMelodic(S.Channel(c)) && NoteStartsIn(S, c, First, End) &&
				MapPitches(D.Cells(c), First, End, S.Channel(c).EffectColumns, [&](int Pitch) { return MapToKey(Pitch, FromKey, ToKey); },
					true, Count, Skipped, &Moved)) {
				Changed = true;
				Revoiced += RevoiceLushChords(S, D, c, First, End, FromKey, ToKey, Unknown);
			}
		}
		if (Revoiced > 0)
			S.Remarks.push_back(Format("%s now play%s the instrument for the chord's new shape.",
				Plural(Revoiced, "lush chord", "lush chords").c_str(), Revoiced == 1 ? "s" : ""));
		if (Unknown > 0)
			Warnings.push_back(Format("%s played by arpeggio instruments of the song's own keep their shapes, which may not fit %s.",
				Plural(Unknown, "chord", "chords").c_str(), KeyName(ToKey).c_str()));
		if (Changed) {
			const std::string Summary = MovesText(Moved, FromKey, ToKey);
			Done.push_back("from " + KeyName(FromKey) + " to " + KeyName(ToKey) + (Summary.empty() ? "" : " (" + Summary + ")"));
		}
		else if (Skipped == 0)
			Warnings.push_back("The notes stayed as they were in " + KeyName(ToKey) + ": the notes that differ from " + KeyName(FromKey) +
				" aren't played in those bars.");
		if (Skipped > 0)
			Warnings.push_back(Format("%s would have left FamiTracker's range (C-0 to B-7) and stayed as they were.",
				Plural(Skipped, "note", "notes").c_str()));
	}

	// 2. Parts moved up or down
	for (size_t i = 0; i < Plan.Moves.size(); ++i) {
		const int Channel = Plan.Moves[i].first, Semitones = Plan.Moves[i].second;
		const ChannelInfo &Info = S.Channel(Channel);
		int Count = 0, Skipped = 0;
		if (Semitones != 0 && MapPitches(D.Cells(Channel), First, End, Info.EffectColumns, [&](int Pitch) { return Pitch + Semitones; },
			false, Count, Skipped, 0)) {
			const int Size = std::abs(Semitones);
			Done.push_back(Format("%s %s %s", Info.FullName().c_str(), Semitones > 0 ? "up" : "down",
				(Size % 12 == 0 ? Plural(Size / 12, "octave", "octaves") : Plural(Size, "semitone", "semitones")).c_str()));
		}
		if (Skipped > 0)
			Warnings.push_back(Format("On %s, %s would have left FamiTracker's range (C-0 to B-7) and stayed as they were.",
				Info.FullName().c_str(), Plural(Skipped, "note", "notes").c_str()));
	}

	// 3. Sounds
	for (size_t i = 0; i < Plan.Sounds.size(); ++i) {
		std::vector<Cell> &Cells = D.Cells(Plan.Sounds[i].first);
		const std::vector<Cell> Before(Cells);
		SetInstruments(Cells, First, End, Plan.Sounds[i].second);
		if (Cells != Before)
			Done.push_back(S.Channel(Plan.Sounds[i].first).FullName() + " played by " + InstrumentLabel(S.Song, Plan.Sounds[i].second));
	}

	// 4. Vibrato
	if (Plan.HasVibrato) {
		const VibratoPlan &Vibrato = Plan.Vibrato;
		const std::vector<Cell> Before(D.Cells(Vibrato.Channel));
		bool Lost = false;
		MakeVibrato(S, D, Vibrato, First, End, Lost);
		if (D.Cells(Vibrato.Channel) != Before)
			Done.push_back(Vibrato.Depth == 0 ? "no vibrato on " + S.Channel(Vibrato.Channel).FullName() :
				Format("vibrato 4%X%X on %s", Vibrato.Speed, Vibrato.Depth, S.Channel(Vibrato.Channel).FullName().c_str()));
		if (Lost)
			Warnings.push_back("Some notes had no free effect column for the vibrato (all four were full).");
	}

	// 5. An echo of the part as it is now
	if (Plan.HasEcho) {
		if (MakeEcho(S, D, Plan.Echo, First, End, Warnings) > 0)
			Done.push_back(Format("an echo of %s on %s", S.Channel(Plan.Echo.Source).FullName().c_str(),
				S.Channel(Plan.Echo.Target).FullName().c_str()));
		else
			Warnings.push_back(Format("%s has no notes there to echo.", S.Channel(Plan.Echo.Source).FullName().c_str()));
	}
}

std::string ChangeMoodTool(Session &S, const Json &Args)
{
	int From, To;
	BarRange(S, Args, From, To);
	const int First = S.Time.BarStart(From), End = S.Time.BarEnd(To);
	const bool AsClip = BoolArg(Args, "as_clip", false);
	const bool DirectChange = Direct(S, AsClip);
	const MoodPlan Plan = ReadMoodPlan(S, Args, First, End);

	std::vector<std::string> Done, Warnings;
	Draft D(S);
	ApplyMoodPlan(S, D, Plan, First, End, Done, Warnings);

	// 6. The tempo
	int Speed = S.Song.Speed, Tempo = S.Song.Tempo;
	if (Plan.Bpm > 0.0)
		SpeedTempoFor(S, Plan.Bpm, S.Time.RowsPerBeat, Speed, Tempo);
	const bool NewTempo = Speed != S.Song.Speed || Tempo != S.Song.Tempo;
	const std::string NewBpm = FormatNumber(std::floor(BeatsPerMinute(Speed, Tempo, S.Song.FrameRate, S.Time.RowsPerBeat) * 10 + 0.5) / 10);

	std::vector<Change> Changes;
	int Start = First;
	const bool NotesChanged = D.Collect(First, DirectChange, Changes, Start, Warnings);
	if (!NotesChanged && !NewTempo)
		throw Problem(WithRemarks(S, Format("Nothing changed in %s. Give at least one of to_mode, to_key, moves, sounds, vibrato, "
			"echo, bpm or tempo_percent, for bars that have notes.", RangeText(From, To).c_str()), Warnings));

	std::string Result;
	if (NotesChanged) {
		std::string What;
		for (size_t i = 0; i < Done.size(); ++i)
			What += (i == 0 ? "" : (i + 1 == Done.size() ? " and " : ", ")) + Done[i];
		const std::string Mood = StringArg(Args, "mood");
		std::string ClipName = StringArg(Args, "clip_name");
		if (ClipName.empty())
			ClipName = "Claude: " + (Mood.empty() ? std::string("new mood") : Mood) + ", " + RangeText(From, To);
		Result = Commit(S, Changes, Start, AsClip, ClipName, What + ", " + RangeText(From, To));
	}
	if (NewTempo) {
		if (!Result.empty())
			Result += " ";
		std::string Why;
		if (DirectChange && S.Host().CanChange(Why)) {
			std::string Error;
			if (S.Host().SetSpeedTempo(Speed, Tempo, Error))
				Result += Format("The tempo is now %s BPM (speed %d, tempo %d), its own Ctrl+Z step.", NewBpm.c_str(), Speed, Tempo);
			else
				Warnings.push_back("The tempo couldn't be changed: " + (Error.empty() ? std::string("FamiTracker refused.") : Error));
		}
		else if (DirectChange)
			Warnings.push_back("The tempo couldn't be changed: " + Why);
		else
			Result += Format("Meant to play at %s BPM: suggest the user sets speed %d and tempo %d in FamiTracker's toolbar (now %d "
				"and %d), since the tempo changes only with direct changes.", NewBpm.c_str(), Speed, Tempo, S.Song.Speed, S.Song.Tempo);
	}
	return WithRemarks(S, Result, Warnings);
}

// Versions of a song, as tracks of their own

// Every volume in the channel at Percent, the notes staying audible; the
// first note gets one when it had none (a channel starts at 15). How many
// volumes changed.
int ScaleVolumes(std::vector<Cell> &Cells, int Percent)
{
	int Changed = 0;
	bool Before = true;		// nothing has set the volume yet
	for (size_t r = 0; r < Cells.size(); ++r) {
		Cell &C = Cells[r];
		int Old = C.Volume;
		if (Old == NO_VOLUME) {
			if (!Before || !C.HasNote())
				continue;
			Old = 15;
		}
		Before = false;
		const int New = Old == 0 ? 0 : std::max(1, (Old * Percent + 50) / 100);
		if (New != C.Volume && !(C.Volume == NO_VOLUME && New == 15)) {
			C.Volume = static_cast<unsigned char>(New);
			++Changed;
		}
	}
	return Changed;
}

// After notes were taken out of a channel, the ones left play with the
// instrument and volume they had
void KeepNoteSounds(std::vector<Cell> &Cells, const std::vector<NoteInfo> &Before, int Columns)
{
	std::map<int, const NoteInfo*> Was;
	for (size_t i = 0; i < Before.size(); ++i)
		Was[Before[i].Row] = &Before[i];
	const std::vector<NoteInfo> After = ReadNotes(Cells, Columns);
	for (size_t i = 0; i < After.size(); ++i) {
		std::map<int, const NoteInfo*>::const_iterator it = Was.find(After[i].Row);
		if (it == Was.end())
			continue;
		Cell &C = Cells[static_cast<size_t>(After[i].Row)];
		if (it->second->Instrument != After[i].Instrument && it->second->Instrument != NO_INSTRUMENT)
			C.Instrument = static_cast<unsigned char>(it->second->Instrument);
		if (it->second->Volume != After[i].Volume)
			C.Volume = static_cast<unsigned char>(it->second->Volume);
	}
}

enum drum_change { DRUMS_KEEP, DRUMS_SOFTER, DRUMS_SPARSER, DRUMS_HALF, DRUMS_OFF, DRUMS_BUSIER, DRUMS_PULSE };

// A drum channel calmer: quieter, without the hits between beats (sparser),
// on beats 1 and 3 only (half), on the bar's first beat only and very soft
// (pulse), or none (off). The DPCM channel has no volume to lower (Volumes
// false). How many hits went.
int ChangeDrums(Session &S, std::vector<Cell> &Cells, int Columns, int Change, bool Volumes = true)
{
	if (Change == DRUMS_KEEP)
		return 0;
	const std::vector<NoteInfo> Before = ReadNotes(Cells, Columns);
	int Removed = 0;
	for (size_t i = 0; i < Before.size(); ++i) {
		const int Row = Before[i].Row;
		const int InBar = Row - S.Time.BarStart(S.Time.BarOf(Row));
		const int PerBeat = std::max(1, S.Time.RowsPerBeat);
		bool Keep = true;
		if (Change == DRUMS_OFF)
			Keep = false;
		else if (Change == DRUMS_SPARSER)
			Keep = InBar % PerBeat == 0;
		else if (Change == DRUMS_HALF)
			Keep = InBar % (2 * PerBeat) == 0;
		else if (Change == DRUMS_PULSE)
			Keep = InBar == 0;
		if (!Keep) {
			Cell &C = Cells[static_cast<size_t>(Row)];
			C.Note = NOTE_NONE;
			C.Octave = 0;
			C.Instrument = NO_INSTRUMENT;
			++Removed;
		}
	}
	if (Removed > 0)
		KeepNoteSounds(Cells, Before, Columns);
	if (Change != DRUMS_OFF && Volumes)
		ScaleVolumes(Cells, Change == DRUMS_PULSE ? 45 : 60);
	return Removed;
}

// The noise channel busier: a soft hi-hat (instrument HiHat) on every
// eighth-note offbeat it leaves empty, in the bars where it plays. How many
// hits it added.
int BusierDrums(Session &S, std::vector<Cell> &Cells, int Columns, int HiHat)
{
	const int Beat = S.Time.RowsPerBeat;
	if (Beat < 2 || Beat % 2 != 0)
		return 0;
	const std::vector<NoteInfo> Before = ReadNotes(Cells, Columns);
	std::set<int> Bars;
	for (size_t i = 0; i < Before.size(); ++i)
		Bars.insert(S.Time.BarOf(Before[i].Row));
	int Added = 0;
	for (std::set<int>::const_iterator it = Bars.begin(); it != Bars.end(); ++it) {
		const int End = std::min(S.Time.BarEnd(*it), static_cast<int>(Cells.size()));
		for (int Row = S.Time.BarStart(*it) + Beat / 2; Row < End; Row += Beat) {
			Cell &C = Cells[static_cast<size_t>(Row)];
			if (C.Note != NOTE_NONE)
				continue;
			int Pitch;
			std::string Drum;
			ParseNoisePitch("hi-hat", Pitch, Drum);
			C.SetPitch(Pitch);
			C.Instrument = static_cast<unsigned char>(HiHat);
			C.Volume = 8;
			++Added;
		}
	}
	if (Added > 0)
		KeepNoteSounds(Cells, Before, Columns);
	return Added;
}

// A driving bass: each note struck again on every eighth note (every beat
// when beats don't split in two), cut short just before, so it pulses. Notes
// that slide stay as they are. How many notes changed.
int DriveBass(Session &S, std::vector<Cell> &Cells, int Columns)
{
	const int Beat = S.Time.RowsPerBeat;
	const int Step = (Beat % 2 == 0 && Beat >= 4) ? Beat / 2 : Beat;
	if (Step < 2)
		return 0;
	const int Gap = std::max(1, Step / 4);
	const std::vector<NoteInfo> Notes = ReadNotes(Cells, Columns);
	int Changed = 0;
	for (size_t i = 0; i < Notes.size(); ++i) {
		const NoteInfo &N = Notes[i];
		bool Slides = false;
		for (size_t e = 0; e < N.Effects.size(); ++e) {
			const int Effect = N.Effects[e].second.first;
			Slides = Slides || Effect == EFF_PORTAMENTO || Effect == EFF_PORTA_UP || Effect == EFF_PORTA_DOWN ||
				Effect == EFF_SLIDE_UP || Effect == EFF_SLIDE_DOWN || Effect == EFF_SWEEPUP || Effect == EFF_SWEEPDOWN;
		}
		if (Slides || N.Rows <= Step)
			continue;
		int Last = N.Row;	// the latest strike
		bool Any = false;
		const int End = std::min(N.Row + N.Rows, static_cast<int>(Cells.size()));
		for (int Row = N.Row + 1; Row < End; ++Row) {
			if ((Row - S.Time.BarStart(S.Time.BarOf(Row))) % Step != 0 || Row - Gap <= Last)
				continue;
			Cell &Cut = Cells[static_cast<size_t>(Row - Gap)], &Strike = Cells[static_cast<size_t>(Row)];
			if (Cut.Note != NOTE_NONE || Strike.Note != NOTE_NONE)
				continue;
			Cut.Note = NOTE_HALT;
			Cut.Octave = 0;
			Strike.SetPitch(N.Pitch);
			Last = Row;
			Any = true;
		}
		if (Any)
			++Changed;
	}
	return Changed;
}

// A calm (night) version is more than slower: the parts below

// The rows of an eighth note: half a beat, or a beat when it doesn't split in two
int EighthRows(Session &S)
{
	const int Beat = std::max(1, S.Time.RowsPerBeat);
	return Beat % 2 == 0 ? Beat / 2 : Beat;
}

// A note taken out of its row, so the one before rings on through it
void TakeOut(Cell &C)
{
	C.Note = NOTE_NONE;
	C.Octave = 0;
	C.Instrument = NO_INSTRUMENT;
	C.Volume = NO_VOLUME;
}

// Whether a note's previous one sounds right up to it (no release, halt or rest between)
bool RingsInto(const NoteInfo &Before, const NoteInfo &Note)
{
	return Before.EndNote == NOTE_NONE && Before.Row + Before.Rows == Note.Row;
}

// The melody's fast runs as their outline: a note shorter than an eighth
// that starts between the eighths, straight after another, is taken out and
// the one before rings on. How many went.
int SimplerRhythm(Session &S, std::vector<Cell> &Cells, int Columns)
{
	const int Eighth = EighthRows(S);
	if (Eighth < 2)
		return 0;
	const std::vector<NoteInfo> Notes = ReadNotes(Cells, Columns);
	int Removed = 0;
	for (size_t i = 1; i < Notes.size(); ++i) {
		const NoteInfo &N = Notes[i];
		const int InBar = N.Row - S.Time.BarStart(S.Time.BarOf(N.Row));
		if (N.Rows >= Eighth || InBar % Eighth == 0 || !RingsInto(Notes[i - 1], N))
			continue;
		TakeOut(Cells[static_cast<size_t>(N.Row)]);
		++Removed;
	}
	if (Removed > 0)
		KeepNoteSounds(Cells, Notes, Columns);
	return Removed;
}

// Legato: the notes hold into the next. Note cuts (Sxx) go; a halt just
// before the next note goes too, and one before a longer rest softens to a
// release (on the triangle, which has no release, it stays). How many changed.
int Legato(Session &S, std::vector<Cell> &Cells, int Columns, bool Triangle)
{
	const int Beat = std::max(1, S.Time.RowsPerBeat);
	int Changed = 0;
	for (size_t r = 0; r < Cells.size(); ++r) {
		Cell &C = Cells[r];
		for (int k = 0; k < Columns && k < EFFECT_COLUMNS; ++k)
			if (C.Effect[k] == EFF_NOTE_CUT) {
				C.Effect[k] = EFF_NONE;
				C.Param[k] = 0;
				++Changed;
			}
		if (C.Note != NOTE_HALT)
			continue;
		size_t Next = r + 1;
		while (Next < Cells.size() && Cells[Next].Note == NOTE_NONE)
			++Next;
		if (Next < Cells.size() && Cells[Next].HasNote() && static_cast<int>(Next - r) < Beat) {
			C.Note = NOTE_NONE;
			C.Octave = 0;
			++Changed;
		}
		else if (!Triangle) {
			C.Note = NOTE_RELEASE;
			C.Octave = 0;
			++Changed;
		}
	}
	return Changed;
}

// Even dynamics, no accents: the notes louder than the channel's usual (its
// median volume) come down to it. How many changed.
int EvenVolumes(std::vector<Cell> &Cells, int Columns)
{
	const std::vector<NoteInfo> Notes = ReadNotes(Cells, Columns);
	if (Notes.size() < 4)
		return 0;
	std::vector<int> Volumes;
	for (size_t i = 0; i < Notes.size(); ++i)
		Volumes.push_back(Notes[i].Volume);
	std::sort(Volumes.begin(), Volumes.end());
	const int Median = Volumes[Volumes.size() / 2];
	int Changed = 0;
	for (size_t i = 0; i < Notes.size(); ++i)
		if (Notes[i].Volume > Median) {
			Cells[static_cast<size_t>(Notes[i].Row)].Volume = static_cast<unsigned char>(Median);
			++Changed;
		}
	return Changed;
}

// The arpeggio (0xy) in effect at each note
std::vector<int> ArpeggiosAt(const std::vector<Cell> &Cells, const std::vector<NoteInfo> &Notes, int Columns)
{
	return StickyAt(Cells, Notes, Columns, EFF_ARPEGGIO);
}

// Chords held as pads: a chord struck again in its bar (the same note and
// arpeggio, ringing up to it) is taken out, so it rings on. How many strikes went.
int HeldChords(Session &S, std::vector<Cell> &Cells, int Columns)
{
	const std::vector<NoteInfo> Notes = ReadNotes(Cells, Columns);
	const std::vector<int> Arp = ArpeggiosAt(Cells, Notes, Columns);
	int Removed = 0;
	size_t Kept = 0;
	for (size_t i = 1; i < Notes.size(); ++i) {
		const NoteInfo &N = Notes[i];
		if (RingsInto(Notes[i - 1], N) && N.Pitch == Notes[Kept].Pitch && Arp[i] == Arp[Kept] &&
			S.Time.BarOf(N.Row) == S.Time.BarOf(Notes[Kept].Row)) {
			TakeOut(Cells[static_cast<size_t>(N.Row)]);
			++Removed;
			continue;
		}
		Kept = i;
	}
	if (Removed > 0)
		KeepNoteSounds(Cells, Notes, Columns);
	return Removed;
}

// Richer chords: a major triad's arpeggio (047) becomes a major seventh's
// shell (04B), a minor one's (037) a minor seventh's (03A). How many changed.
int RicherChords(std::vector<Cell> &Cells, int Columns)
{
	int Changed = 0;
	for (size_t r = 0; r < Cells.size(); ++r)
		for (int k = 0; k < Columns && k < EFFECT_COLUMNS; ++k) {
			Cell &C = Cells[r];
			if (C.Effect[k] != EFF_ARPEGGIO || (C.Param[k] != 0x47 && C.Param[k] != 0x37))
				continue;
			C.Param[k] = static_cast<unsigned char>(C.Param[k] == 0x47 ? 0x4B : 0x3A);
			++Changed;
		}
	return Changed;
}

// A bass of long notes: the notes between the beats taken out (the one
// before rings on through them), and a note that repeats the one before in
// its half bar. How many went.
int LongBass(Session &S, std::vector<Cell> &Cells, int Columns)
{
	const int Beat = std::max(1, S.Time.RowsPerBeat);
	const std::vector<NoteInfo> Notes = ReadNotes(Cells, Columns);
	int Removed = 0;
	size_t Kept = 0;
	auto HalfBar = [&](int Row) { return (Row - S.Time.BarStart(S.Time.BarOf(Row))) / (2 * Beat); };
	for (size_t i = 1; i < Notes.size(); ++i) {
		const NoteInfo &N = Notes[i];
		const int InBar = N.Row - S.Time.BarStart(S.Time.BarOf(N.Row));
		const bool Again = N.Pitch == Notes[Kept].Pitch && S.Time.BarOf(N.Row) == S.Time.BarOf(Notes[Kept].Row) &&
			HalfBar(N.Row) == HalfBar(Notes[Kept].Row);
		if (RingsInto(Notes[i - 1], N) && (InBar % Beat != 0 || Again)) {
			TakeOut(Cells[static_cast<size_t>(N.Row)]);
			++Removed;
			continue;
		}
		Kept = i;
	}
	if (Removed > 0)
		KeepNoteSounds(Cells, Notes, Columns);
	return Removed;
}

// Tempo commands (Fxx from the split point up) at Factor; speed ones stay,
// so the whole song plays that much faster or slower. How many changed.
int ScaleTempoEffects(std::vector<Cell> &Cells, int Columns, int SplitPoint, double Factor)
{
	int Changed = 0;
	for (size_t r = 0; r < Cells.size(); ++r)
		for (int k = 0; k < Columns; ++k) {
			Cell &C = Cells[r];
			if (C.Effect[k] != EFF_SPEED || C.Param[k] < SplitPoint)
				continue;
			const int New = std::max(std::max(SplitPoint, 32), std::min(255, static_cast<int>(C.Param[k] * Factor + 0.5)));
			if (New != C.Param[k]) {
				C.Param[k] = static_cast<unsigned char>(New);
				++Changed;
			}
		}
	return Changed;
}

// "Town" -> "Town (night)", "Town (day)" -> "Town (night)", "Town (night 2)"
// when that's taken
std::string VersionName(const SongInfo &Song, const std::string &Version)
{
	std::string Base = Trim(Song.TrackName);
	const size_t Open = Base.rfind('(');
	if (Open != std::string::npos && !Base.empty() && Base[Base.size() - 1] == ')')
		Base = Trim(Base.substr(0, Open));
	const bool Plain = Base.empty() || Lowercase(Base) == "new song";
	for (int n = 1; ; ++n) {
		const std::string Label = n == 1 ? Version : Format("%s %d", Version.c_str(), n);
		std::string Name = Plain ? Label : Base + " (" + Label + ")";
		if (Plain)
			Name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(Name[0])));
		bool Taken = false;
		for (size_t t = 0; t < Song.TrackNames.size(); ++t)
			Taken = Taken || Lowercase(Song.TrackNames[t]) == Lowercase(Name);
		if (!Taken)
			return Name;
	}
}

// Channels that play chords as arpeggios (0xy)
bool PlaysChords(Session &S, int Channel)
{
	const std::vector<NoteInfo> &Notes = S.Notes(Channel);
	for (size_t i = 0; i < Notes.size(); ++i)
		if (SoundsChord(S, Notes[i]))
			return true;
	return false;
}

// The channel that already plays the melody a little later (an echo): most
// of its notes repeat the melody's, a fixed number of rows after them; -1 if none
int EchoOf(Session &S, int Melody)
{
	const std::vector<NoteInfo> &Lead = S.Notes(Melody);
	std::map<int, int> PitchAt;		// row, pitch
	for (size_t i = 0; i < Lead.size(); ++i)
		PitchAt[Lead[i].Row] = Lead[i].Pitch;
	for (int c = 0; c < S.ChannelCount(); ++c) {
		if (c == Melody || !IsMelodic(S.Channel(c)))
			continue;
		const std::vector<NoteInfo> &Notes = S.Notes(c);
		if (Notes.size() < 4)
			continue;
		for (int Delay = 1; Delay <= S.Time.RowsPerBar; ++Delay) {
			size_t Same = 0;
			for (size_t i = 0; i < Notes.size(); ++i) {
				std::map<int, int>::const_iterator it = PitchAt.find(Notes[i].Row - Delay);
				if (it != PitchAt.end() && it->second == Notes[i].Pitch)
					++Same;
			}
			if (Same * 10 >= Notes.size() * 7)
				return c;
		}
	}
	return -1;
}

bool HasPresets(const ChannelInfo &Channel)
{
	const std::string Chip = InstrumentChip(Channel);
	return (Chip == "2A03" || Chip == "VRC6" || Chip == "VRC7") && Channel.Kind != KIND_NOISE && Channel.Kind != KIND_DPCM;
}

std::string MakeVersionTool(Session &S, const Json &Args)
{
	const std::string Version = StringArg(Args, "version");
	if (Version.empty())
		throw Problem("Say which version with version, like \"night\".");
	if (S.Song.Tracks >= 64)
		throw Problem("The module already has 64 tracks, FamiTracker's most; the user can remove one in Module > Module properties.");
	std::string Why;
	if (!S.Host().CanChange(Why))
		throw Problem(Why);
	const int Mood = VersionMood(Version);
	const bool Calm = Mood == MOOD_CALM, Tense = Mood == MOOD_TENSE;
	const std::string Name = Arg(Args, "name") != 0 ? StringArg(Args, "name") : VersionName(S.Song, Version);
	if (Name.empty())
		throw Problem("The version's name can't be empty.");
	const int Total = S.Time.TotalRows();
	const int Melody = Arg(Args, "melody_channel") != 0 ? ChannelArg(S, Args, "melody_channel") : GuessMelodyChannel(S, 0, Total);

	// How the parts change: a calm version's own choices where nothing is said
	auto Word = [&](const char *Name, const char *Default, const std::vector<std::string> &Allowed) {
		const std::string W = Lowercase(StringArg(Args, Name, Default));
		if (std::find(Allowed.begin(), Allowed.end(), W) == Allowed.end()) {
			std::string List;
			for (size_t i = 0; i < Allowed.size(); ++i)
				List += (i == 0 ? "" : (i + 1 == Allowed.size() ? " or " : ", ")) + Allowed[i];
			throw Problem(Format("%s should be %s.", Name, List.c_str()));
		}
		return W;
	};
	const std::string MelodyWord = Word("melody", Calm ? "simpler" : "keep", std::vector<std::string>{ "keep", "simpler" });
	const std::string ChordsWord = Word("chords", Calm ? "held" : "keep", std::vector<std::string>{ "keep", "held", "richer" });
	const std::string RegisterWord = Word("register", Calm ? "lower" : "keep", std::vector<std::string>{ "keep", "lower" });
	const bool Legatos = BoolArg(Args, "legato", Calm);

	// What's asked for, and for a calm version its own changes where nothing is said
	Json Plan = Args;
	std::vector<std::string> Remarks;
	if (RegisterWord == "lower" && Arg(Args, "moves") == 0) {
		// The parts that sit high (not the bass) an octave down, where it fits
		Json Moves = Json::MakeArray();
		for (int c = 0; c < S.ChannelCount(); ++c) {
			const ChannelInfo &Info = S.Channel(c);
			const std::vector<NoteInfo> &Notes = S.Notes(c);
			if (!IsMelodic(Info) || IsBass(Info) || Notes.size() < 4)
				continue;
			std::vector<int> Pitches;
			for (size_t i = 0; i < Notes.size(); ++i)
				Pitches.push_back(Notes[i].Pitch);
			std::sort(Pitches.begin(), Pitches.end());
			if (Pitches[Pitches.size() / 2] < 48 || Pitches[0] - 12 < 24)	// C-4 and up, above C-2 after
				continue;
			Json Move = Json::MakeObject();
			Move.Set("channel", c + 1);
			Move.Set("semitones", -12);
			Moves.Push(Move);
		}
		if (Moves.Size() > 0)
			Plan.Set("moves", Moves);
	}
	if (Calm) {
		if (Arg(Args, "tempo_percent") == 0 && Arg(Args, "bpm") == 0) {
			// 20% slower, and a fast song slower still: no faster than 120 BPM, but at least 60% of the day's tempo
			const double Now = BeatsPerMinute(S.Song.Speed, S.Song.Tempo, S.Song.FrameRate, S.Time.RowsPerBeat);
			double Target = Now * 0.8;
			if (Target > 120.0)
				Target = std::max(120.0, Now * 0.6);
			Plan.Set("tempo_percent", std::floor(100.0 * Target / Now + 0.5));
		}
		if (Melody >= 0 && Arg(Args, "vibrato") == 0) {
			// Slower and wider than a day's, and later into the note
			Json Vibrato = Json::MakeObject();
			Vibrato.Set("channel", Melody + 1);
			Vibrato.Set("speed", "slow");
			Vibrato.Set("depth", "medium");
			Vibrato.Set("delay_beats", 0.5);
			Plan.Set("vibrato", Vibrato);
		}
		if (Arg(Args, "sounds") == 0) {
			// Sounds that swell in: a soft lead, chords as a warm pad
			Json Sounds = Json::MakeArray();
			for (int c = 0; c < S.ChannelCount(); ++c) {
				if (!HasPresets(S.Channel(c)) || IsBass(S.Channel(c)))
					continue;
				Json Sound = Json::MakeObject();
				Sound.Set("channel", c + 1);
				if (c == Melody)
					Sound.Set("instrument", "soft lead");
				else if (PlaysChords(S, c))
					Sound.Set("instrument", "warm pad");
				else
					continue;
				Sounds.Push(Sound);
			}
			Plan.Set("sounds", Sounds);
		}
		const int Existing = Melody >= 0 && Arg(Args, "echo") == 0 ? EchoOf(S, Melody) : -1;
		if (Existing >= 0)
			Remarks.push_back(Format("%s already echoes the melody, so no other echo was added.", S.Channel(Existing).FullName().c_str()));
		else if (Melody >= 0 && Arg(Args, "echo") == 0) {
			const int Delay = 3;	// as add_echo's default
			const int Target = EchoTarget(S, Melody, Delay, Total + Delay);
			if (Target < 0)
				Remarks.push_back("No channel was free for an echo of the melody.");
			else if (InstrumentChip(S.Channel(Target)) == InstrumentChip(S.Channel(Melody)) || HasPresets(S.Channel(Target))) {
				// Longer and softer than a day's
				Json Echo = Json::MakeObject();
				Echo.Set("channel", Melody + 1);
				Echo.Set("to_channel", Target + 1);
				Echo.Set("delay_beats", 1.5);
				Echo.Set("volume", 4);		// 3 once the version is quieter
				if (InstrumentChip(S.Channel(Target)) != InstrumentChip(S.Channel(Melody)))
					Echo.Set("instrument", "soft lead");
				Plan.Set("echo", Echo);
			}
		}
	}
	if (Tense) {
		if (Arg(Args, "tempo_percent") == 0 && Arg(Args, "bpm") == 0)
			Plan.Set("tempo_percent", 115);
		if (Arg(Args, "to_mode") == 0 && Arg(Args, "to_key") == 0) {
			bool Found;
			const Key K = KeyArg(S, Args, "key", 0, Total, Found);
			if (Found && ModeSteps(K.Mode)[2] == 4)		// a major third: major, lydian, mixolydian
				Plan.Set("to_mode", "minor");
			else if (Found)
				Remarks.push_back("The song is in " + KeyName(K) + " already, so its mode stayed.");
		}
		if (Melody >= 0 && Arg(Args, "sounds") == 0 && HasPresets(S.Channel(Melody)) && !IsBass(S.Channel(Melody))) {
			Json Sound = Json::MakeObject();
			Sound.Set("channel", Melody + 1);
			Sound.Set("instrument", "bright lead");
			Json Sounds = Json::MakeArray();
			Sounds.Push(Sound);
			Plan.Set("sounds", Sounds);
		}
	}
	const Json *pMode = Arg(Args, "to_mode");
	if (Off(pMode) || (pMode != 0 && pMode->IsString() && Lowercase(Trim(pMode->String())) == "keep"))
		Plan.Set("to_mode", Json());	// "keep" (or false) keeps the mode
	static const char *const DRUM_WORDS[] = { "keep", "softer", "sparser", "half", "off", "busier", "pulse" };
	const std::string DrumWord = Lowercase(StringArg(Args, "drums", Calm ? "pulse" : (Tense ? "busier" : "keep")));
	int Drums = -1;
	for (int i = 0; i < 7; ++i)
		if (DrumWord == DRUM_WORDS[i])
			Drums = i;
	if (Drums < 0)
		throw Problem("drums should be keep, softer, sparser, half, pulse, off or busier.");
	const std::string BassWord = Word("bass", Tense ? "driving" : (Calm ? "long" : "keep"),
		std::vector<std::string>{ "keep", "driving", "long" });
	const int VolumePercent = IntArg(Args, "volume_percent", Calm ? 75 : 100, 10, 100);
	const MoodPlan Changes = ReadMoodPlan(S, Plan, 0, Total);

	std::vector<std::string> Done, Warnings(Remarks);
	Draft D(S);
	// The parts themselves first (a calm version's), so the sounds, the
	// vibrato and the echo follow them
	if (MelodyWord == "simpler" && Melody >= 0 && IsMelodic(S.Channel(Melody))) {
		const int Removed = SimplerRhythm(S, D.Cells(Melody), S.Channel(Melody).EffectColumns);
		if (Removed > 0)
			Done.push_back(Format("the melody's fast runs as their outline (%s left out, the notes before ring on)",
				Plural(Removed, "short note", "short notes").c_str()));
	}
	if (Legatos) {
		int Joined = 0, Evened = 0;
		for (int c = 0; c < S.ChannelCount(); ++c) {
			const ChannelInfo &Info = S.Channel(c);
			if (!IsMelodic(Info) || S.Notes(c).empty())
				continue;
			Joined += Legato(S, D.Cells(c), Info.EffectColumns, Info.Kind == KIND_TRIANGLE);
			if (Info.Kind != KIND_TRIANGLE)
				Evened += EvenVolumes(D.Cells(c), Info.EffectColumns);
		}
		if (Joined > 0)
			Done.push_back(Format("legato (%s taken out or softened)", Plural(Joined, "note cut or halt", "note cuts and halts").c_str()));
		if (Evened > 0)
			Done.push_back(Format("even dynamics without accents (%s brought down to the usual)", Plural(Evened, "loud note", "loud notes").c_str()));
	}
	for (int c = 0; c < S.ChannelCount() && ChordsWord != "keep"; ++c) {
		const ChannelInfo &Info = S.Channel(c);
		if (!IsMelodic(Info) || IsBass(Info) || !PlaysChords(S, c))
			continue;
		const int Removed = HeldChords(S, D.Cells(c), Info.EffectColumns);
		if (Removed > 0)
			Done.push_back(Format("the chords on %s held as a pad (%s left out)", Info.FullName().c_str(),
				Plural(Removed, "strike", "strikes").c_str()));
		if (ChordsWord == "richer" && RicherChords(D.Cells(c), Info.EffectColumns) > 0)
			Done.push_back("richer chords on " + Info.FullName() + " (major and minor triads as sevenths)");
	}
	for (int c = 0; c < S.ChannelCount() && BassWord == "long"; ++c) {
		const ChannelInfo &Info = S.Channel(c);
		if (!IsBass(Info) || S.Notes(c).empty())
			continue;
		const int Removed = LongBass(S, D.Cells(c), Info.EffectColumns);
		if (Removed > 0)
			Done.push_back(Format("the bass on %s in long notes (%s left out)", Info.FullName().c_str(),
				Plural(Removed, "note", "notes").c_str()));
	}
	ApplyMoodPlan(S, D, Changes, 0, Total, Done, Warnings);

	if (VolumePercent != 100) {
		int Changed = 0;
		for (int c = 0; c < S.ChannelCount(); ++c)
			if (IsMelodic(S.Channel(c)) && S.Channel(c).Kind != KIND_TRIANGLE)
				Changed += ScaleVolumes(D.Cells(c), VolumePercent);
		if (Changed > 0)
			Done.push_back(Format("volumes at %d%%", VolumePercent));
	}
	for (int c = 0; c < S.ChannelCount() && BassWord == "driving"; ++c) {
		if (IsBass(S.Channel(c)) && !S.Notes(c).empty() && DriveBass(S, D.Cells(c), S.Channel(c).EffectColumns) > 0)
			Done.push_back("a driving bass on " + S.Channel(c).FullName() + " (struck again on every eighth note)");
	}
	for (int c = 0; c < S.ChannelCount() && Drums != DRUMS_KEEP; ++c) {
		if (S.Channel(c).Kind == KIND_DPCM && Drums != DRUMS_BUSIER && Drums != DRUMS_SOFTER && !S.Notes(c).empty()) {
			// Sampled drums: fewer, the same way (a sample has no volume to lower)
			const int Removed = ChangeDrums(S, D.Cells(c), S.Channel(c).EffectColumns, Drums, false);
			if (Removed > 0)
				Done.push_back(Format("%s on the DPCM channel left out", Plural(Removed, "sampled hit", "sampled hits").c_str()));
			continue;
		}
		if (S.Channel(c).Kind != KIND_NOISE || S.Notes(c).empty())
			continue;
		if (Drums == DRUMS_BUSIER) {
			const int Added = BusierDrums(S, D.Cells(c), S.Channel(c).EffectColumns, ResolveInstrument(S, "hi-hat", c));
			if (Added > 0)
				Done.push_back(Format("busier drums (%s between the beats)", Plural(Added, "hi-hat", "hi-hats").c_str()));
			continue;
		}
		const int Removed = ChangeDrums(S, D.Cells(c), S.Channel(c).EffectColumns, Drums);
		if (Drums == DRUMS_OFF)
			Done.push_back("no drums");
		else if (Drums == DRUMS_SOFTER)
			Done.push_back("softer drums");
		else if (Drums == DRUMS_PULSE)
			Done.push_back(Format("the drums a soft pulse on each bar's first beat (%s left out)", Plural(Removed, "hit", "hits").c_str()));
		else
			Done.push_back(Format("%s drums (%s left out, the rest softer)", Drums == DRUMS_HALF ? "half the" : "sparser",
				Plural(Removed, "hit", "hits").c_str()));
	}

	// The tempo, in the track's settings and in its tempo commands
	const double Now = BeatsPerMinute(S.Song.Speed, S.Song.Tempo, S.Song.FrameRate, S.Time.RowsPerBeat);
	int Speed = S.Song.Speed, Tempo = S.Song.Tempo;
	if (Changes.Bpm > 0.0 && std::fabs(Changes.Bpm - Now) > 0.05) {
		const double Factor = Changes.Bpm / Now;
		bool Speeds = false, Tempos = false;
		SpeedCommands(S, Speeds, Tempos);
		if (!Speeds && !Tempos)
			ChooseSpeedTempo(Changes.Bpm, S.Time.RowsPerBeat, S.Song.FrameRate, S.Song.Speed, S.Song.SplitPoint, Speed, Tempo);
		else {
			// The tempo and its commands in the patterns scale together, exactly for round percentages
			Tempo = static_cast<int>(std::floor(S.Song.Tempo * Changes.Bpm / Now + 0.5));
			if (Tempo < std::max(32, S.Song.SplitPoint) || Tempo > 255)
				ChooseSpeedTempo(Changes.Bpm, S.Time.RowsPerBeat, S.Song.FrameRate, S.Song.Speed, S.Song.SplitPoint, Speed, Tempo);
		}
		int Commands = 0;
		for (int c = 0; c < S.ChannelCount(); ++c)
			Commands += ScaleTempoEffects(D.Cells(c), S.Channel(c).EffectColumns, S.Song.SplitPoint, Factor);
		const double NewBpm = BeatsPerMinute(Speed, Tempo, S.Song.FrameRate, S.Time.RowsPerBeat);
		Done.insert(Done.begin(), Format("%s BPM instead of %s (speed %d, tempo %d%s)",
			FormatNumber(std::floor(NewBpm * 10 + 0.5) / 10).c_str(), FormatNumber(std::floor(Now * 10 + 0.5) / 10).c_str(), Speed, Tempo,
			Commands > 0 ? Format("; %s in the patterns changed too", Plural(Commands, "tempo command", "tempo commands").c_str()).c_str() : ""));
	}

	std::vector<std::vector<Cell> > Channels;
	for (int c = 0; c < S.ChannelCount(); ++c)
		Channels.push_back(D.Cells(c));
	const int Day = S.Song.Track;
	std::string Error;
	const int Track = S.Host().AddTrack(Name, Channels, Speed, Tempo, Error);
	if (Track < 0)
		throw Problem("The version couldn't be added: " + (Error.empty() ? std::string("FamiTracker refused.") : Error));

	std::string What;
	for (size_t i = 0; i < Done.size(); ++i)
		What += (i == 0 ? "" : (i + 1 == Done.size() ? " and " : ", ")) + Done[i];
	std::string Result = Format("Made track %d \"%s\" from track %d \"%s\", which stays as it was. FamiTracker shows the new track now, "
		"and the other tools work on it; select_track switches back. ", Track + 1, Name.c_str(), Day + 1, S.Song.TrackName.c_str());
	Result += What.empty() ? std::string("It's a plain copy so far.") : "Changes: " + What + ".";
	Result += " FamiTracker can't undo adding a track: the user removes one in Module > Module properties.";
	return WithRemarks(S, Result, Warnings);
}

// Jingles, as tracks of their own

std::string SecondsText(double Seconds);

// A track name no track has yet: "Level up", "Level up 2"
std::string FreeTrackName(const SongInfo &Song, const std::string &Name)
{
	for (int n = 1; ; ++n) {
		const std::string Candidate = n == 1 ? Name : Format("%s %d", Name.c_str(), n);
		bool Taken = false;
		for (size_t t = 0; t < Song.TrackNames.size(); ++t)
			Taken = Taken || Lowercase(Song.TrackNames[t]) == Lowercase(Candidate);
		if (!Taken)
			return Candidate;
	}
}

// The middle one of a channel's pitches, Default when it has no notes
int MiddlePitch(Session &S, int Channel, int Default)
{
	std::vector<int> Pitches;
	const std::vector<NoteInfo> &Notes = S.Notes(Channel);
	for (size_t i = 0; i < Notes.size(); ++i)
		Pitches.push_back(Notes[i].Pitch);
	if (Pitches.empty())
		return Default;
	std::nth_element(Pitches.begin(), Pitches.begin() + static_cast<std::ptrdiff_t>(Pitches.size() / 2), Pitches.end());
	return Pitches[Pitches.size() / 2];
}

// The pitch for a jingle part's key note: its notes (on average, by how long
// they sound) around Center, they and the Along part's within Low-High
int PlaceKeyNote(const std::vector<JingleNote> &Part, const std::vector<JingleNote> &Along, int Tonic, int Center, int Low, int High)
{
	double Sum = 0.0, Weight = 0.0;
	int Lowest = 0, Highest = 0;
	for (size_t i = 0; i < Part.size(); ++i) {
		Sum += static_cast<double>(Part[i].Degree) * Part[i].Steps;
		Weight += Part[i].Steps;
		Lowest = std::min(Lowest, Part[i].Degree);
		Highest = std::max(Highest, Part[i].Degree);
	}
	for (size_t i = 0; i < Along.size(); ++i) {
		Lowest = std::min(Lowest, Along[i].Degree);
		Highest = std::max(Highest, Along[i].Degree);
	}
	const double Mean = Weight > 0.0 ? Sum / Weight : 0.0;
	int Best = Tonic + 48;
	double BestScore = 1e9;
	for (int Octave = 0; Octave < 8; ++Octave) {
		const int Base = Tonic + 12 * Octave;
		const int Outside = std::max(0, Low - (Base + Lowest)) + std::max(0, Base + Highest - High);
		const double Score = Outside * 100.0 + std::fabs(Base + Mean - Center);
		if (Score < BestScore) {
			BestScore = Score;
			Best = Base;
		}
	}
	return Best;
}

std::string MakeJingleTool(Session &S, const Json &Args)
{
	const std::string Kind = StringArg(Args, "kind");
	if (Kind.empty())
		throw Problem("Say which jingle with kind: " + JingleNames() + ".");
	const Jingle *pJingle = FindJingle(Kind);
	if (pJingle == 0)
		throw Problem(Format("There's no \"%s\" jingle. The kinds: %s. For something else, start from the nearest one and change "
			"it with write_notes.", Kind.c_str(), JingleNames().c_str()));
	const Jingle &J = *pJingle;
	if (S.Song.Tracks >= 64)
		throw Problem("The module already has 64 tracks, FamiTracker's most; the user can remove one in Module > Module properties.");
	std::string Why;
	if (!S.Host().CanChange(Why))
		throw Problem(Why);
	const int Total = S.Time.TotalRows();

	// The key note: as asked, or the song's; the jingle keeps its own mode
	Key Used(0, J.Minor ? MODE_MINOR : MODE_MAJOR);
	std::string KeyFrom = " (no notes to find a key from yet; give key for another)";
	if (Arg(Args, "key") != 0) {
		const std::string Text = StringArg(Args, "key");
		Key Asked;
		if (!ParseKey(Text, Asked))
			throw Problem(Format("key: \"%s\" isn't a key. Write it like \"A\", \"F#\" or \"Bb minor\".", Text.c_str()));
		Used.Tonic = Asked.Tonic;
		KeyFrom = "";
	}
	else {
		Key Found;
		if (FindKey(S, 0, Total, Found)) {
			Used.Tonic = Found.Tonic;
			KeyFrom = " (the song is in " + KeyName(Found) + ")";
		}
	}

	// The channels: as asked ("none" leaves a part out), or the song's own
	auto Optional = [&](const char *Name) -> bool {
		const Json *pValue = Arg(Args, Name);
		return pValue != 0 && pValue->IsString() && Lowercase(Trim(pValue->String())) == "none";
	};
	int Lead = -1;
	if (Arg(Args, "lead_channel") != 0)
		Lead = ChannelArg(S, Args, "lead_channel");
	else {
		Lead = GuessMelodyChannel(S, 0, Total);
		if (Lead >= 0 && IsBass(S.Channel(Lead)))
			Lead = -1;
		for (int c = 0; c < S.ChannelCount() && Lead < 0; ++c)	// the VRC7's first, in a VRC7 song
			if (S.Channel(c).Kind == KIND_FM)
				Lead = c;
		for (int c = 0; c < S.ChannelCount() && Lead < 0; ++c)
			if (S.Channel(c).Kind == KIND_PULSE)
				Lead = c;
	}
	if (Lead < 0 || !IsMelodic(S.Channel(Lead)))
		throw Problem("The lead needs a melodic channel; give lead_channel.");
	int Harmony = -1;
	if (Optional("harmony_channel"))
		Harmony = -1;
	else if (Arg(Args, "harmony_channel") != 0) {
		Harmony = ChannelArg(S, Args, "harmony_channel");
		if (Harmony == Lead || !IsMelodic(S.Channel(Harmony)))
			throw Problem("harmony_channel needs a melodic channel other than the lead's, or \"none\".");
	}
	else {
		// A pulse or FM channel: of the lead's chip and playing in the song if one is
		int Best = -1;
		for (int c = 0; c < S.ChannelCount(); ++c) {
			if (c == Lead || (S.Channel(c).Kind != KIND_PULSE && S.Channel(c).Kind != KIND_FM))
				continue;
			const int Score = (S.Channel(c).Chip == S.Channel(Lead).Chip ? 2 : 0) + (S.Notes(c).empty() ? 0 : 1);
			if (Harmony < 0 || Score > Best) {
				Harmony = c;
				Best = Score;
			}
		}
	}
	int Bass = -1;
	if (Optional("bass_channel"))
		Bass = -1;
	else if (Arg(Args, "bass_channel") != 0) {
		Bass = ChannelArg(S, Args, "bass_channel");
		if (Bass == Lead || Bass == Harmony || !IsMelodic(S.Channel(Bass)))
			throw Problem("bass_channel needs a melodic channel other than the lead's and the harmony's, or \"none\".");
	}
	else {
		// The song's bass channel: the triangle or sawtooth playing most
		size_t Most = 0;
		for (int c = 0; c < S.ChannelCount(); ++c) {
			if (c == Lead || c == Harmony || !IsBass(S.Channel(c)))
				continue;
			if (Bass < 0 || S.Notes(c).size() > Most) {
				Bass = c;
				Most = S.Notes(c).size();
			}
		}
	}
	int Noise = -1;
	if (BoolArg(Args, "drums", true) && !J.Drums.empty())
		for (int c = 0; c < S.ChannelCount() && Noise < 0; ++c)
			if (S.Channel(c).Kind == KIND_NOISE)
				Noise = c;
	const double Bpm = NumberArg(Args, "bpm", J.Bpm, 30.0, 300.0);
	const std::string Name = Arg(Args, "name") != 0 ? StringArg(Args, "name") :
		FreeTrackName(S.Song, std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(J.Name[0])))) + J.Name.substr(1));
	if (Name.empty())
		throw Problem("The jingle's name can't be empty.");

	// The sounds: as asked; or what the channel plays in the song; or the
	// lead's (Fallback, for the harmony); or a preset; or one of the song's
	// instruments for the chip
	auto Sound = [&](const char *ArgName, int Channel, const char *Preset, bool PresetFirst, int Fallback) -> int {
		if (Arg(Args, ArgName) != 0)
			return InstrumentArg(S, Args, ArgName, Channel, NO_INSTRUMENT);
		const bool Presets = HasPresets(S.Channel(Channel));
		if (PresetFirst && Presets)
			return ResolveInstrument(S, Preset, Channel);
		if (!S.Notes(Channel).empty()) {
			const int Playing = InstrumentAt(S, Channel, S.Notes(Channel).front().Row);
			if (Playing != NO_INSTRUMENT)
				return Playing;
		}
		if (Fallback != NO_INSTRUMENT)
			return Fallback;
		if (Presets)
			return ResolveInstrument(S, Preset, Channel);
		for (size_t i = 0; i < S.Song.Instruments.size(); ++i)
			if (S.Song.Instruments[i].Chip == InstrumentChip(S.Channel(Channel)))
				return S.Song.Instruments[i].Index;
		throw Problem(Format("%s has no instrument for the jingle: give %s (one of the song's %s instruments).",
			S.Channel(Channel).FullName().c_str(), ArgName, InstrumentChip(S.Channel(Channel)).c_str()));
	};
	const int LeadSound = Sound("lead_instrument", Lead, J.LeadSound != 0 ? J.LeadSound : "bright lead", J.LeadSound != 0, NO_INSTRUMENT);
	const int HarmonySound = Harmony < 0 ? NO_INSTRUMENT : Sound("harmony_instrument", Harmony, "soft chords", false,
		InstrumentChip(S.Channel(Harmony)) == InstrumentChip(S.Channel(Lead)) ? LeadSound : NO_INSTRUMENT);
	const int BassSound = Bass < 0 ? NO_INSTRUMENT : Sound("bass_instrument", Bass, "bass", false, NO_INSTRUMENT);
	int DrumSounds[4] = { NO_INSTRUMENT, NO_INSTRUMENT, NO_INSTRUMENT, NO_INSTRUMENT };
	static const char *const DRUMS[] = { "kick", "snare", "hi-hat", "open hi-hat" };
	for (size_t i = 0; Noise >= 0 && i < J.Drums.size(); ++i)
		if (DrumSounds[J.Drums[i].Drum] == NO_INSTRUMENT)
			DrumSounds[J.Drums[i].Drum] = ResolveInstrument(S, DRUMS[J.Drums[i].Drum], Noise);

	// The parts as cells: sixteenths on rows (one each unless the beat has
	// more), then a halt
	const int PerStep = S.Time.RowsPerBeat % 4 == 0 ? std::max(1, S.Time.RowsPerBeat / 4) : 1;
	const int Rows = J.Steps * PerStep + 1;
	std::vector<std::vector<Cell> > Channels(static_cast<size_t>(S.ChannelCount()), std::vector<Cell>(static_cast<size_t>(Rows)));
	std::vector<std::string> Warnings;
	auto Write = [&](int Channel, const std::vector<PartNote> &Notes, bool Crisp) {
		PartOptions Options;
		Options.EffectColumns = S.Channel(Channel).EffectColumns;
		Options.Releases = Crisp ? std::vector<bool>(NO_INSTRUMENT, false) : ReleasingInstruments(S.Song);	// crisp: cut, not released
		Options.KeepFollowing = false;
		std::vector<Cell> Out;
		BuildPart(std::vector<Cell>(static_cast<size_t>(Rows)), 0, Rows - 1, Notes, Options, Out, Warnings);
		for (size_t r = 0; r < Out.size() && r + 1 < static_cast<size_t>(Rows); ++r)
			Channels[static_cast<size_t>(Channel)][r] = Out[r];
	};
	auto Melodic = [&](int Channel, const std::vector<JingleNote> &Part, int KeyNote, int Instrument, int Volume) {
		std::vector<PartNote> Notes;
		bool Pulsing = true;	// only short notes, like a driving bass: each one cut off
		for (size_t i = 0; i < Part.size(); ++i) {
			Pulsing = Pulsing && Part[i].Steps <= 2;
			PartNote N;
			N.Row = Part[i].Step * PerStep;
			N.Rows = Part[i].Steps * PerStep;
			N.Pitch = KeyNote + Part[i].Degree;
			while (N.Pitch < 0)
				N.Pitch += 12;
			while (N.Pitch >= PITCHES)
				N.Pitch -= 12;
			if (i == 0) {
				N.Instrument = Instrument;
				N.Volume = S.Channel(Channel).Kind == KIND_TRIANGLE ? -1 : Volume;
			}
			if (Part[i].Vibrato && Part[i].Steps > 4) {
				N.LaterEffects.push_back(std::make_pair(4 * PerStep, std::make_pair(static_cast<int>(EFF_VIBRATO), 0x52)));
				N.EndEffects.push_back(std::make_pair(static_cast<int>(EFF_VIBRATO), 0));
			}
			Notes.push_back(N);
		}
		Write(Channel, Notes, Pulsing);
	};
	const int LeadKey = PlaceKeyNote(J.Lead, J.Harmony, Used.Tonic, MiddlePitch(S, Lead, 48), 12, 84);
	Melodic(Lead, J.Lead, LeadKey, LeadSound, J.LeadVolume);
	if (Harmony >= 0)
		Melodic(Harmony, J.Harmony, LeadKey, HarmonySound, J.HarmonyVolume);
	if (Bass >= 0) {
		const int Center = MiddlePitch(S, Bass, S.Channel(Bass).Kind == KIND_TRIANGLE ? 36 : 26);
		Melodic(Bass, J.Bass, PlaceKeyNote(J.Bass, std::vector<JingleNote>(), Used.Tonic, Center, 9, 60), BassSound, 15);
	}
	if (Noise >= 0) {
		std::vector<PartNote> Hits;
		for (size_t i = 0; i < J.Drums.size(); ++i) {
			PartNote N;
			N.Row = J.Drums[i].Step * PerStep;
			N.Rows = ((i + 1 < J.Drums.size() ? J.Drums[i + 1].Step : J.Steps) - J.Drums[i].Step) * PerStep;
			std::string Drum;
			ParseNoisePitch(DRUMS[J.Drums[i].Drum], N.Pitch, Drum);
			N.Instrument = DrumSounds[J.Drums[i].Drum];
			N.Volume = J.Drums[i].Volume;
			Hits.push_back(N);
		}
		Write(Noise, Hits, false);
	}
	AddEffect(Channels[static_cast<size_t>(Lead)][static_cast<size_t>(Rows - 1)], EFF_HALT, 0, S.Channel(Lead).EffectColumns);

	// Its tempo, then the track
	int Speed = S.Song.Speed, Tempo = S.Song.Tempo;
	ChooseSpeedTempo(Bpm, 4 * PerStep, S.Song.FrameRate, S.Song.Speed, S.Song.SplitPoint, Speed, Tempo);
	const double Played = BeatsPerMinute(Speed, Tempo, S.Song.FrameRate, 4 * PerStep);
	std::string Error;
	const int Track = S.Host().AddTrack(Name, Channels, Speed, Tempo, Error);
	if (Track < 0)
		throw Problem("The jingle couldn't be added: " + (Error.empty() ? std::string("FamiTracker refused.") : Error));

	std::string Parts = Format("lead on %s with %s", S.Channel(Lead).FullName().c_str(), InstrumentLabel(S.Song, LeadSound).c_str());
	if (Harmony >= 0)
		Parts += Format(", harmony on %s with %s", S.Channel(Harmony).FullName().c_str(), InstrumentLabel(S.Song, HarmonySound).c_str());
	if (Bass >= 0)
		Parts += Format(", bass on %s with %s", S.Channel(Bass).FullName().c_str(), InstrumentLabel(S.Song, BassSound).c_str());
	if (Noise >= 0)
		Parts += ", drums on " + S.Channel(Noise).FullName();
	std::string Result = Format("Made track %d \"%s\": the %s jingle, for %s. It's %s long, at %s BPM (speed %d, tempo %d), in %s%s. "
		"Parts: %s. It ends with a halt (C00), so it plays once, and export_for_game saves it as a sound that doesn't loop. "
		"FamiTracker shows the new track now, and the other tools work on it (select_track switches back): play it for the "
		"user, and change it with write_notes. FamiTracker can't undo adding a track: the user removes one in Module > "
		"Module properties.", Track + 1, Name.c_str(), J.Name.c_str(), J.Use.c_str(),
		SecondsText(J.Steps / 4.0 * 60.0 / std::max(1.0, Played)).c_str(), FormatNumber(std::floor(Played * 10 + 0.5) / 10).c_str(),
		Speed, Tempo, KeyName(Used).c_str(), KeyFrom.c_str(), Parts.c_str());
	return WithRemarks(S, Result, Warnings);
}

// Sound effects, as tracks of their own

PresetSequence SoundSequence(int Type, const std::vector<int> &Items, int Setting = SEQUENCE_ABSOLUTE)
{
	PresetSequence S;
	S.Type = Type;
	S.Items = Items;
	S.Loop = -1;
	S.Release = -1;
	S.Setting = Setting;
	return S;
}

// Writes a sound effect as a track of its own at speed 1, a row a tick: a
// 2A03 instrument for each layer whose sequences shape it, its note where the
// layer starts, a halt where it ends and C00 after the last; one that loops
// has B00 on its last row instead. Its number from 0; Parts says which
// channels play what.
int WriteSoundEffect(Session &S, const transcribe::SoundEffect &Effect, const std::string &Name, std::string &Parts, bool Loop = false)
{
	if (Effect.layers.empty() || Effect.length() <= 0)
		throw Problem("The sound effect is empty.");
	if (S.Song.Tracks >= 64)
		throw Problem("The module already has 64 tracks, FamiTracker's most; the user can remove one in Module > Module properties.");
	std::string Why;
	if (!S.Host().CanChange(Why))
		throw Problem(Why);

	// The channels: the 2A03's pulses (then the MMC5's, which play the same
	// instruments), its triangle and noise
	std::vector<int> Pulses;
	int Triangle = -1, Noise = -1;
	for (int c = 0; c < S.ChannelCount(); ++c) {
		const ChannelInfo &Info = S.Channel(c);
		if (InstrumentChip(Info) != "2A03")
			continue;
		if (Info.Kind == KIND_PULSE)
			Pulses.push_back(c);
		else if (Info.Kind == KIND_TRIANGLE && Triangle < 0)
			Triangle = c;
		else if (Info.Kind == KIND_NOISE && Noise < 0)
			Noise = c;
	}
	std::vector<int> Used(Effect.layers.size(), -1);
	size_t NextPulse = 0;
	for (size_t l = 0; l < Effect.layers.size(); ++l) {
		switch (Effect.layers[l].channel) {
		case transcribe::SfxChannel::Pulse:
			if (NextPulse < Pulses.size())
				Used[l] = Pulses[NextPulse++];
			break;
		case transcribe::SfxChannel::Triangle:
			Used[l] = Triangle;
			Triangle = -1;
			break;
		case transcribe::SfxChannel::Noise:
			Used[l] = Noise;
			Noise = -1;
			break;
		}
		if (Used[l] < 0)
			throw Problem("The song has no free 2A03 channel for a part of this sound.");
	}

	// An instrument for each layer, named after the track
	const bool Pal = S.Song.FrameRate == 50;
	const int Rows = Loop ? Effect.length() : Effect.length() + 1;
	std::vector<std::vector<Cell> > Channels(static_cast<size_t>(S.ChannelCount()), std::vector<Cell>(static_cast<size_t>(Rows)));
	for (size_t l = 0; l < Effect.layers.size(); ++l) {
		const transcribe::SfxLayer &Layer = Effect.layers[l];
		const transcribe::SfxInstrument Shape = transcribe::ToInstrument(Layer, Pal);
		const ChannelInfo &Info = S.Channel(Used[l]);
		Preset Sound;
		Sound.Name = l == 0 ? Name : Name + " (" + Lowercase(Info.Name) + ")";
		Sound.Description = "a sound effect";
		Sound.Drum = Layer.channel == transcribe::SfxChannel::Noise;
		Sound.Vrc7Patch = 0;
		Sound.Sequences2A03.push_back(SoundSequence(SEQUENCE_VOLUME, Shape.volume));
		if (!Shape.arpeggio.empty())
			Sound.Sequences2A03.push_back(SoundSequence(SEQUENCE_ARPEGGIO, Shape.arpeggio, Shape.fixedArpeggio ? SEQUENCE_FIXED : SEQUENCE_ABSOLUTE));
		if (!Shape.pitch.empty())
			Sound.Sequences2A03.push_back(SoundSequence(SEQUENCE_PITCH, Shape.pitch));
		if (!Shape.duty.empty())
			Sound.Sequences2A03.push_back(SoundSequence(SEQUENCE_DUTY, Shape.duty));
		std::string Error;
		const int Instrument = S.Host().AddInstrument(Sound, "2A03", false, Error);
		if (Instrument < 0)
			throw Problem(Error.empty() ? "The sound effect's instrument couldn't be added." : Error);
		S.Refresh();

		std::vector<Cell> &Cells = Channels[static_cast<size_t>(Used[l])];
		const size_t Start = static_cast<size_t>(std::max(0, Layer.start));
		if (Start >= Cells.size())
			continue;
		Cells[Start].SetPitch(Shape.note);
		Cells[Start].Instrument = static_cast<unsigned char>(Instrument);
		const size_t End = Start + Shape.volume.size() - 1;	// where its volume reaches 0
		if (End < Cells.size())
			Cells[End].Note = NOTE_HALT;
		Parts += Format("%s%s with %s", Parts.empty() ? "" : ", ", Info.FullName().c_str(), InstrumentLabel(S.Song, Instrument).c_str());
	}
	if (Loop)
		AddEffect(Channels[static_cast<size_t>(Used[0])][static_cast<size_t>(Rows - 1)], EFF_JUMP, 0, S.Channel(Used[0]).EffectColumns);
	else
		AddEffect(Channels[static_cast<size_t>(Used[0])][static_cast<size_t>(Rows - 1)], EFF_HALT, 0, S.Channel(Used[0]).EffectColumns);

	// A row a tick: speed 1 at the tempo that makes a row of the speed's ticks
	const int Tempo = std::max(std::max(32, S.Song.SplitPoint), std::min(255, DefaultTempo(S.Song.FrameRate)));
	std::string Error;
	const int Track = S.Host().AddTrack(Name, Channels, 1, Tempo, Error);
	if (Track < 0)
		throw Problem("The sound effect couldn't be added: " + (Error.empty() ? std::string("FamiTracker refused.") : Error));
	return Track;
}

// A WSL path to a Windows drive as Windows writes it: /mnt/c/Users/... is C:\Users\...
std::string WindowsPath(const std::string &Path)
{
	if (Path.size() >= 7 && Path.compare(0, 5, "/mnt/") == 0 && std::isalpha(static_cast<unsigned char>(Path[5])) && Path[6] == '/') {
		std::string Out = std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(Path[5])))) + ":" + Path.substr(6);
		std::replace(Out.begin(), Out.end(), '/', '\\');
		return Out;
	}
	return Path;
}

std::string SoundChanges(const SoundEffectChanges &Changes)
{
	if (Changes.Semitones == 0 && Changes.LengthPercent == 100 && Changes.VolumePercent == 100)
		return std::string();
	return Format(" Changed: %d semitones, %d%% of the length, %d%% of the volume.", Changes.Semitones, Changes.LengthPercent,
		Changes.VolumePercent);
}

// Why a sound came out shorter than asked, for the reply
std::string ShortenedText(const Session &S, bool Loop)
{
	return Format(" A sound can last at most %.1f s%s, so it was made shorter to fit.", MaxSoundTicks(Loop) /
		static_cast<double>(std::max(1, S.Song.FrameRate)), Loop ? " as a loop" : "");
}

// A sound effect from a recording of the user's voice ("pew", "boing", "kssh")
std::string VoiceSoundEffect(Session &S, const Json &Args, const std::string &Path)
{
	transcribe::Audio Audio;
	std::string Error;
	const std::string File = WindowsPath(Path);
	if (!transcribe::ReadWav(S.Host().LocalPath(File), Audio, &Error))
		throw Problem("The recording " + File + " couldn't be read: " + Error + " Give the path of a WAV file on the user's "
			"computer as Windows writes it, like C:\\Users\\Name\\Documents\\pew.wav.");
	transcribe::VoiceEffectOptions Options;
	Options.pal = S.Song.FrameRate == 50;
	if (Arg(Args, "octaves") != 0) {
		Options.fitOctave = false;
		Options.octaves = IntArg(Args, "octaves", 0, -4, 4);
	}
	Options.noise = BoolArg(Args, "noise", true);
	transcribe::SoundEffect Effect;
	transcribe::VoiceEffectInfo Info;
	if (!transcribe::VoiceToEffect(Audio, Options, Effect, &Info, &Error))
		throw Problem(Error);
	SoundEffectChanges Changes;
	Changes.Semitones = IntArg(Args, "semitones", 0, -24, 24);
	Changes.LengthPercent = IntArg(Args, "length_percent", 100, 25, 400);
	Changes.VolumePercent = IntArg(Args, "volume_percent", 100, 10, 200);
	const bool Shortened = FitSoundEffect(Effect, MaxSoundTicks(false), Changes);
	Effect = ChangeSoundEffect(Effect, Changes);

	// Named after the file: pew.wav makes "SFX Pew", and a take FamiTracker
	// kept ("Voice 2026-10-21 18-30-00.wav") "SFX Voice"
	std::string Base = File.substr(File.find_last_of("/\\") == std::string::npos ? 0 : File.find_last_of("/\\") + 1);
	if (Base.find('.') != std::string::npos)
		Base = Base.substr(0, Base.rfind('.'));
	if (Base.size() > 20) {
		static const char TIME[] = " 0000-00-00 00-00-00";
		bool Taken = true;
		for (size_t i = 0; i < 20; ++i) {
			const char c = Base[Base.size() - 20 + i];
			Taken = Taken && (TIME[i] == '0' ? std::isdigit(static_cast<unsigned char>(c)) != 0 : c == TIME[i]);
		}
		if (Taken)
			Base.resize(Base.size() - 20);
	}
	if (Base.empty())
		Base = "Voice";
	Base[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(Base[0])));
	const std::string Name = Arg(Args, "name") != 0 ? Trim(StringArg(Args, "name")) : FreeSoundName(S.Song, "SFX " + Base);
	const std::string Taken = SoundNameProblem(S.Song, Name);
	if (!Taken.empty())
		throw Problem(Taken);

	std::string Parts;
	const int Track = WriteSoundEffect(S, Effect, Name, Parts);
	std::string How;
	if (Info.pitched)
		How = Info.octaves == 0 ? "the voice's pitch and loudness on a pulse, as sung" : Format("the voice's pitch and loudness on a pulse, %s "
			"%s", Plural(std::abs(Info.octaves), "octave", "octaves").c_str(), Info.octaves > 0 ? "higher" : "lower");
	if (Info.noisy)
		How += std::string(How.empty() ? "" : ", and ") + "its breathy and hissy part on the noise channel";
	return WithRemarks(S, Format("Made track %d \"%s\" from the recording (%s): %s. It's %.2f s long, on %s.%s%s It plays at speed 1 "
		"(a row a tick) and halts at its end (C00), so export_for_game saves it as a sound that plays once (in GameMaker: "
		"audio_play_sound(%s, 0, false)). Its instruments' sequences hold the shape, so the user can smooth it in FamiTracker's "
		"instrument editor. FamiTracker shows the new track now: play it for the user. octaves moves the pitch by whole octaves, "
		"and noise: false leaves the noise out. FamiTracker can't undo adding a track.", Track + 1, Name.c_str(), File.c_str(),
		How.c_str(), Effect.length() / static_cast<double>(std::max(1, S.Song.FrameRate)), Parts.c_str(),
		Info.cut ? Format(" The recording was longer than %s, so it ends there.", Format("%.1f s", transcribe::SFX_MAX_TICKS /
		static_cast<double>(std::max(1, S.Song.FrameRate))).c_str()).c_str() : "", (SoundChanges(Changes) + (Shortened ?
		ShortenedText(S, false) : std::string())).c_str(),
		GameMakerName(Name + ".ogg").c_str()));
}

// How a sound effect's track plays, for the reply
std::string SoundPlayText(const std::string &Name, bool Loop)
{
	if (Loop)
		return Format("It plays at speed 1 (a row a tick) and loops: B00 on its last row takes it back to its start, so "
			"export_for_game saves an OGG that loops seamlessly, for ambience, with the GameMaker code that plays %s "
			"(audio_stop_sound ends it).", GameMakerName(Name + ".ogg").c_str());
	return Format("It plays at speed 1 (a row a tick) and halts at its end (C00), so export_for_game saves it as a sound that "
		"plays once (in GameMaker: audio_play_sound(%s, 0, false)).", GameMakerName(Name + ".ogg").c_str());
}

// The 2A03's pulse channels in the song, the MMC5's too (they play its instruments)
int SoundPulses(Session &S)
{
	int Pulses = 0;
	for (int c = 0; c < S.ChannelCount(); ++c)
		if (InstrumentChip(S.Channel(c)) == "2A03" && S.Channel(c).Kind == KIND_PULSE)
			++Pulses;
	return Pulses;
}


// A number, or a word that stands for one
double WordNumberArg(const Json &Args, const char *Name, double Default, double Min, double Max,
	const std::vector<std::pair<std::string, double> > &Words)
{
	const Json *pValue = Arg(Args, Name);
	if (pValue != 0 && pValue->IsString()) {
		const std::string Word = Squeezed(pValue->String());
		for (size_t i = 0; i < Words.size(); ++i)
			if (Squeezed(Words[i].first) == Word)
				return Words[i].second;
		char *pEnd = 0;
		std::strtod(pValue->String().c_str(), &pEnd);
		if (pEnd == pValue->String().c_str() || *pEnd != 0) {
			std::string Known;
			for (size_t i = 0; i < Words.size(); ++i)
				Known += (i == 0 ? "" : ", ") + Words[i].first;
			throw Problem(Format("%s should be a number from %s to %s%s.", Name, FormatNumber(Min).c_str(), FormatNumber(Max).c_str(),
				Known.empty() ? "" : (" or one of " + Known).c_str()));
		}
	}
	return NumberArg(Args, Name, Default, Min, Max);
}

// A layer's pitch: a note (C-6) on a pulse or the triangle, 0-15 on the
// noise (15 the brightest), or a word for one
double SoundPitchArg(const Json &Args, const char *Name, double Default, bool Noise)
{
	const Json *pValue = Arg(Args, Name);
	if (pValue == 0)
		return Default;
	static const struct { const char *Word; double Tone, Noise; } WORDS[] = {
		{ "lowest", 12, 0 }, { "very low", 18, 1 }, { "deep", 18, 1 }, { "low", 24, 3 }, { "dark", 24, 3 }, { "mid", 48, 7 },
		{ "middle", 48, 7 }, { "medium", 48, 7 }, { "high", 60, 11 }, { "bright", 64, 12 }, { "very high", 72, 14 },
		{ "shrill", 79, 15 }, { "highest", 84, 15 } };
	if (pValue->IsString()) {
		const std::string Text = Trim(pValue->String());
		for (size_t i = 0; i < sizeof(WORDS) / sizeof(WORDS[0]); ++i)
			if (Squeezed(WORDS[i].Word) == Squeezed(Text))
				return Noise ? WORDS[i].Noise : WORDS[i].Tone;
		int Pitch = 0;
		std::string Drum;
		if (!Noise && ParsePitch(Text, Pitch))
			return Pitch;
		if (Noise && Text.find_first_not_of("0123456789. ") != std::string::npos && ParseNoisePitch(Text, Pitch, Drum))
			return Pitch;
	}
	try {
		return Noise ? NumberArg(Args, Name, Default, 0.0, 15.0) : NumberArg(Args, Name, Default, 9.0, 95.0);
	}
	catch (const Problem &) {
		throw Problem(Noise ? Format("%s on the noise: 0-15 (15 the brightest), or dark, mid, bright.", Name) :
			Format("%s: a note like \"C-6\" (C-4 is 48, A-0 to B-7), or low, mid, high, very high.", Name));
	}
}

// A layer of a sound described in words, from make_sound_effect's layers
SfxLayerDesign SoundLayerArg(const Json &Item)
{
	if (!Item.IsObject())
		throw Problem("should be an object like {\"channel\": \"noise\", \"seconds\": 1}.");
	SfxLayerDesign D;
	const std::string Channel = Squeezed(StringArg(Item, "channel"));
	if (Channel == "pulse" || Channel == "square")
		D.Channel = transcribe::SfxChannel::Pulse;
	else if (Channel == "triangle" || Channel == "tri")
		D.Channel = transcribe::SfxChannel::Triangle;
	else if (Channel == "noise")
		D.Channel = transcribe::SfxChannel::Noise;
	else
		throw Problem("channel should be pulse, triangle or noise.");
	const bool Noise = D.Channel == transcribe::SfxChannel::Noise;
	D.Start = NumberArg(Item, "start", 0.0, 0.0, 10.0);
	D.Seconds = NumberArg(Item, "seconds", 0.5, 0.02, 10.0);
	D.Pitch = SoundPitchArg(Item, "pitch", Noise ? 8.0 : 60.0, Noise);
	D.PitchTo = Arg(Item, "pitch_to") != 0 ? SoundPitchArg(Item, "pitch_to", D.Pitch, Noise) : -1.0;
	const std::string Glide = Squeezed(StringArg(Item, "glide", "even"));
	if (Glide == "even" || Glide == "steady" || Glide == "linear")
		D.Glide = SFX_GLIDE_EVEN;
	else if (Glide == "fast" || Glide == "quick" || Glide == "snap")
		D.Glide = SFX_GLIDE_FAST;
	else if (Glide == "slow" || Glide == "late" || Glide == "lazy")
		D.Glide = SFX_GLIDE_SLOW;
	else
		throw Problem("glide should be even, fast or slow.");
	const std::vector<std::pair<std::string, double> > Loudness = { { "silent", 0 }, { "soft", 5 }, { "quiet", 5 }, { "medium", 9 },
		{ "loud", 13 }, { "full", 15 }, { "loudest", 15 } };
	D.Volume = static_cast<int>(std::floor(WordNumberArg(Item, "volume", 12.0, 1.0, 15.0, Loudness) + 0.5));
	D.Volume = std::max(1, D.Volume);
	D.VolumeTo = Arg(Item, "volume_to") != 0 ? static_cast<int>(std::floor(WordNumberArg(Item, "volume_to", 0.0, 0.0, 15.0, Loudness) +
		0.5)) : -1;
	static const char *const ENVELOPES[][4] = { { "steady", "hold", "flat", "constant" }, { "hit", "pluck", "pop", "strike" },
		{ "fade", "decay", "fadeout", "die" }, { "swell", "rise", "fadein", "grow" }, { "swellfade", "swellandfade", "riseandfall",
		"bloom" }, { "drops", "drip", "drips", "plinks" } };
	const std::string Envelope = Squeezed(StringArg(Item, "envelope", "steady"));
	D.Envelope = -1;
	for (int e = 0; e < 6 && D.Envelope < 0; ++e)
		for (int w = 0; w < 4; ++w)
			if (Envelope == ENVELOPES[e][w])
				D.Envelope = e;
	if (D.Envelope < 0)
		throw Problem("envelope should be steady, hit, fade, swell, swell_fade or drops.");
	D.Repeats = IntArg(Item, "repeats", 1, 1, 64);
	D.Gap = Arg(Item, "gap") != 0 ? NumberArg(Item, "gap", 0.25, 0.0, 0.9) : -1.0;
	D.Step = NumberArg(Item, "step", 0.0, -24.0, 24.0);
	D.Gusts = IntArg(Item, "gusts", 0, 0, 16);
	D.Flutter = WordNumberArg(Item, "flutter", 0.0, 0.0, 30.0, { { "none", 0 }, { "slow", 4 }, { "medium", 8 }, { "fast", 14 } });
	D.Vibrato = WordNumberArg(Item, "vibrato", 0.0, 0.0, 12.0, { { "none", 0 }, { "gentle", 0.3 }, { "medium", 0.8 }, { "strong", 2 },
		{ "wild", 5 } });
	D.VibratoHz = NumberArg(Item, "vibrato_hz", 6.0, 0.5, 20.0);
	D.Crackle = WordNumberArg(Item, "crackle", 0.0, 0.0, 1.0, { { "none", 0 }, { "few", 0.25 }, { "some", 0.5 }, { "many", 1 } });
	D.Rough = BoolArg(Item, "rough", false);
	D.Echo = BoolArg(Item, "echo", false);
	if (D.Echo && D.Channel != transcribe::SfxChannel::Pulse)
		throw Problem("echo is for a pulse (it plays on the other pulse).");
	if (Arg(Item, "duty") != 0) {
		const std::string Duty = Squeezed(StringArg(Item, "duty"));
		if (Noise) {
			if (Duty == "metallic" || Duty == "buzz" || Duty == "buzzy" || Duty == "tonal" || Duty == "1")
				D.Duty = 1;
			else if (Duty == "plain" || Duty == "hiss" || Duty == "normal" || Duty == "0")
				D.Duty = 0;
			else
				throw Problem("duty on the noise should be metallic (buzzy) or plain.");
		}
		else if (Duty == "125" || Duty == "thin" || Duty == "nasal")
			D.Duty = 0;
		else if (Duty == "25" || Duty == "bright")
			D.Duty = 1;
		else if (Duty == "50" || Duty == "round" || Duty == "hollow" || Duty == "square")
			D.Duty = 2;
		else if (Duty == "75")
			D.Duty = 3;
		else
			throw Problem("duty should be 12.5%, 25%, 50% or 75% (thin, bright, round).");
	}
	return D;
}

// A sound described in words, built from layers
std::string DesignedSoundEffect(Session &S, const Json &Args)
{
	const Json *pLayers = Arg(Args, "layers");
	if (!pLayers->IsArray() || pLayers->Size() == 0)
		throw Problem("layers should be a list of layers, like [{\"channel\": \"noise\", \"seconds\": 1, \"envelope\": \"fade\"}].");
	if (pLayers->Size() > 16)
		throw Problem("A sound can have at most 16 layers.");
	std::vector<SfxLayerDesign> Layers;
	for (size_t i = 0; i < pLayers->Size(); ++i) {
		try {
			Layers.push_back(SoundLayerArg(pLayers->At(i)));
		}
		catch (const Problem &P) {
			throw Problem(Format("Layer %d: %s", static_cast<int>(i) + 1, P.Text.c_str()));
		}
	}
	const bool Loop = BoolArg(Args, "loop", false);
	SoundEffectChanges Changes;
	Changes.Variant = IntArg(Args, "variant", 1, 1, 9);
	Changes.Semitones = IntArg(Args, "semitones", 0, -24, 24);
	Changes.LengthPercent = IntArg(Args, "length_percent", 100, 25, 400);
	Changes.VolumePercent = IntArg(Args, "volume_percent", 100, 10, 200);
	const double Seconds = NumberArg(Args, "seconds", 0.0, 0.0, 10.0);
	transcribe::SoundEffect Designed;
	std::string Error;
	if (!DesignSoundEffect(Layers, Seconds, std::max(1, S.Song.FrameRate), Changes.Variant, Loop, SoundPulses(S), Designed, Error))
		throw Problem(Error);
	const bool Shortened = FitSoundEffect(Designed, MaxSoundTicks(Loop), Changes);
	const transcribe::SoundEffect Effect = ChangeSoundEffect(Designed, Changes);

	const std::string Name = Arg(Args, "name") != 0 ? Trim(StringArg(Args, "name")) : FreeSoundName(S.Song, "SFX Sound");
	const std::string Taken = SoundNameProblem(S.Song, Name);
	if (!Taken.empty())
		throw Problem(Taken);
	std::string Parts;
	const int Track = WriteSoundEffect(S, Effect, Name, Parts, Loop);
	std::string Described;
	for (size_t i = 0; i < Layers.size(); ++i)
		Described += Format("\n%d. %s", static_cast<int>(i) + 1, DescribeLayerDesign(Layers[i]).c_str());
	std::string Changed;
	if (Changes.Variant > 1)
		Changed += Format(" Variant %d: %s, and another draw of its random parts.", Changes.Variant, VariantText(Changes.Variant).c_str());
	Changed += SoundChanges(Changes);
	if (Shortened)
		Changed += ShortenedText(S, Loop);
	return WithRemarks(S, Format("Made track %d \"%s\": a sound in %s, %.2f s long, on %s.%s The layers:%s\n%s Its instruments' "
		"sequences shape it, so the user can change it in FamiTracker's instrument editor. FamiTracker shows the new track now: play it "
		"for the user and change what's off (a layer's pitch, loudness, length or repeats), or try variant 2, 3... for other takes "
		"(other draws of gusts, crackle and drops). FamiTracker can't undo adding a track: the user removes one in Module > Module "
		"properties.", Track + 1, Name.c_str(), Plural(static_cast<int>(Layers.size()), "layer", "layers").c_str(),
		Effect.length() / static_cast<double>(std::max(1, S.Song.FrameRate)), Parts.c_str(), Changed.c_str(), Described.c_str(),
		SoundPlayText(Name, Loop).c_str()));
}

std::string MakeSoundEffectTool(Session &S, const Json &Args)
{
	const int Given = (Arg(Args, "kind") != 0 ? 1 : 0) + (Arg(Args, "recording") != 0 ? 1 : 0) + (Arg(Args, "layers") != 0 ? 1 : 0);
	if (Given > 1)
		throw Problem("Give one of kind, recording and layers.");
	if (Arg(Args, "recording") != 0) {
		// "latest": the newest take recorded in FamiTracker
		std::string Recording = StringArg(Args, "recording");
		if (Lowercase(Trim(Recording)) == "latest") {
			std::string Error;
			if (!S.Host().LatestRecording(Recording, Error))
				throw Problem(Error);
		}
		return VoiceSoundEffect(S, Args, Recording);
	}
	if (Arg(Args, "layers") != 0)
		return DesignedSoundEffect(S, Args);
	const std::string Kind = StringArg(Args, "kind");
	if (Kind.empty())
		throw Problem("Say which sound with kind (" + SoundEffectNames() + "), describe one with layers, or give a recording of the "
			"user's voice.");
	const SoundEffectKind *pKind = FindSoundEffect(Kind);
	if (pKind == 0)
		throw Problem(Format("There's no \"%s\" sound. The kinds: %s. For something else, describe it with layers, or start from "
			"the nearest kind and change it with semitones, length_percent or a variant.", Kind.c_str(), SoundEffectNames().c_str()));
	SoundEffectChanges Changes;
	Changes.Variant = IntArg(Args, "variant", 1, 1, 9);
	Changes.Semitones = IntArg(Args, "semitones", 0, -24, 24);
	Changes.LengthPercent = IntArg(Args, "length_percent", 100, 25, 400);
	Changes.VolumePercent = IntArg(Args, "volume_percent", 100, 10, 200);
	const bool Loop = BoolArg(Args, "loop", pKind->Loops);
	const transcribe::SoundEffect Original = pKind->Make();
	const bool Shortened = FitSoundEffect(Original, MaxSoundTicks(Loop), Changes);
	const transcribe::SoundEffect Effect = ChangeSoundEffect(Original, Changes);
	std::string Title = pKind->Name;
	Title[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(Title[0])));
	const std::string Name = Arg(Args, "name") != 0 ? Trim(StringArg(Args, "name")) : FreeSoundName(S.Song, "SFX " + Title);
	const std::string Taken = SoundNameProblem(S.Song, Name);
	if (!Taken.empty())
		throw Problem(Taken);

	std::string Parts;
	const int Track = WriteSoundEffect(S, Effect, Name, Parts, Loop);
	std::string Changed;
	if (Changes.Variant > 1)
		Changed += Format(" Variant %d: %s.", Changes.Variant, VariantText(Changes.Variant).c_str());
	Changed += SoundChanges(Changes);
	if (Shortened)
		Changed += ShortenedText(S, Loop);
	return WithRemarks(S, Format("Made track %d \"%s\": the %s sound (%s), %s long, on %s.%s %s Its "
		"instruments' sequences shape it, so the user can change it in FamiTracker's instrument editor. FamiTracker shows the new "
		"track now: play it for the user. For other takes, make another with variant 2, 3..., semitones, length_percent or "
		"volume_percent. FamiTracker can't undo adding a track: the user removes one in Module > Module properties.",
		Track + 1, Name.c_str(), pKind->Name.c_str(), pKind->Use.c_str(), Format("%.2f s", Effect.length() / static_cast<double>(
		std::max(1, S.Song.FrameRate))).c_str(), Parts.c_str(), Changed.c_str(), SoundPlayText(Name, Loop).c_str()));
}

// Tracks by number (from 1) or name
int TrackArg(Session &S, const Json &Args, const char *Name)
{
	const Json *pValue = Arg(Args, Name);
	if (pValue == 0)
		throw Problem(Format("Say which track with %s (a number from 1, or its name).", Name));
	const std::vector<std::string> &Names = S.Song.TrackNames;
	std::string List;
	for (size_t t = 0; t < Names.size(); ++t)
		List += Format("%s%d \"%s\"", t > 0 ? ", " : "", static_cast<int>(t) + 1, Names[t].c_str());
	if (pValue->IsNumber() || (pValue->IsString() && !Trim(pValue->String()).empty() &&
		Trim(pValue->String()).find_first_not_of("0123456789") == std::string::npos)) {
		const int Number = pValue->IsNumber() ? static_cast<int>(pValue->Number()) : std::atoi(Trim(pValue->String()).c_str());
		if (Number < 1 || Number > S.Song.Tracks)
			throw Problem(Format("There's no track %d. The tracks: %s.", Number, List.c_str()));
		return Number - 1;
	}
	const std::string Wanted = Lowercase(StringArg(Args, Name));
	int Found = -1, Matches = 0;
	for (size_t t = 0; t < Names.size(); ++t) {
		const std::string Track = Lowercase(Names[t]);
		if (Track == Wanted)
			return static_cast<int>(t);
		if (Track.find(Wanted) != std::string::npos) {
			Found = static_cast<int>(t);
			++Matches;
		}
	}
	if (Matches == 1)
		return Found;
	throw Problem(Format("%s track \"%s\". The tracks: %s.", Matches > 1 ? "More than one" : "No", StringArg(Args, Name).c_str(),
		List.c_str()));
}

std::string SelectTrackTool(Session &S, const Json &Args)
{
	const int Track = TrackArg(S, Args, "track");
	std::string Error;
	if (Track != S.Song.Track && !S.Host().SelectTrack(Track, Error))
		throw Problem(Error.empty() ? std::string("FamiTracker didn't switch tracks.") : Error);
	return Format("Track %d \"%s\" is selected; the tools work on it now.", Track + 1,
		static_cast<size_t>(Track) < S.Song.TrackNames.size() ? S.Song.TrackNames[static_cast<size_t>(Track)].c_str() : "");
}

// The expansion chip

// "the VRC7", "no expansion chip"
std::string ChipText(const std::string &Chip)
{
	return Chip.empty() ? std::string("no expansion chip") : "the " + Chip;
}

std::string SetExpansionTool(Session &S, const Json &Args)
{
	std::string Chip;
	const std::string Asked = StringArg(Args, "chip");
	for (size_t i = 0; i < Asked.size(); ++i)
		if (std::isalnum(static_cast<unsigned char>(Asked[i])))
			Chip += static_cast<char>(std::toupper(static_cast<unsigned char>(Asked[i])));
	if (Chip == "NONE" || Chip == "2A03" || Chip == "NES" || Chip == "OFF")
		Chip.clear();
	else if (Chip == "NAMCO" || Chip == "NAMCO163" || Chip == "N106")
		Chip = "N163";
	else if (Chip == "5B" || Chip == "S5B" || Chip == "SUNSOFT" || Chip == "SUNSOFT5B")
		throw Problem("This FamiTracker doesn't play the Sunsoft 5B in songs.");
	if (Asked.empty())
		throw Problem("Say which chip with chip: none, VRC6, VRC7, FDS, MMC5 or N163.");
	if (!Chip.empty() && Chip != "VRC6" && Chip != "VRC7" && Chip != "FDS" && Chip != "MMC5" && Chip != "N163")
		throw Problem("chip should be none, VRC6, VRC7, FDS, MMC5 or N163 (FamiTracker plays one expansion chip at a time).");
	const int Channels = IntArg(Args, "n163_channels", 4, 1, 8);
	int Namco = 0;
	for (int c = 0; c < S.ChannelCount(); ++c)
		Namco += S.Channel(c).Chip == "N163" ? 1 : 0;
	if (S.Song.Expansion == Chip && (Chip != "N163" || Namco == Channels))
		return "The song uses " + ChipText(Chip) + " already.";
	std::string Why;
	if (!S.Host().CanChange(Why))
		throw Problem(Why);

	// Notes on the channels that go need the user's word
	const bool Fewer = Chip == "N163" && S.Song.Expansion == Chip;	// only the N163's channel count changes
	std::vector<int> Tracks;
	const int Notes = S.Host().ExpansionNotes(Chip, Channels, Tracks);
	if (Notes > 0 && !BoolArg(Args, "confirm", false)) {
		std::string Where;
		for (size_t i = 0; i < Tracks.size(); ++i) {
			const int t = Tracks[i];
			Where += Format("%s%d \"%s\"", i == 0 ? "" : ", ", t + 1,
				static_cast<size_t>(t) < S.Song.TrackNames.size() ? S.Song.TrackNames[static_cast<size_t>(t)].c_str() : "");
		}
		const std::string Going = Fewer ? Format("the N163 channels after Namco %d", Channels) :
			ChipText(S.Song.Expansion) + "'s channels";
		const std::string Switching = Fewer ? Format("%d N163 channels removes those channels", Channels) :
			ChipText(Chip) + " clears those channels";
		throw Problem(Format("The song has %s on %s (track%s %s). Switching to %s in every track%s, and FamiTracker can't undo "
			"it. Ask the user first; if they agree, call set_expansion again with confirm: true.", Plural(Notes, "note", "notes").c_str(),
			Going.c_str(), Tracks.size() == 1 ? "" : "s", Where.c_str(), Switching.c_str(),
			Fewer ? "" : ", since the new chip's channels take their place"));
	}
	const bool Pal = S.Song.FrameRate == 50;
	std::string Error;
	int Moved = 0, Lost = 0;
	if (!S.Host().SetExpansion(Chip, Channels, Moved, Lost, Error))
		throw Problem(Error.empty() ? std::string("FamiTracker didn't switch the chip.") : Error);
	S.Load();

	std::string Added;
	int Count = 0;
	for (int c = 0; c < S.ChannelCount(); ++c) {
		if (!Chip.empty() && S.Channel(c).Chip == Chip) {
			Added += (Count == 0 ? "" : ", ") + S.Channel(c).FullName();
			++Count;
		}
	}
	std::string Text = "The song uses " + ChipText(Chip) + " now";
	Text += Count > 0 ? Format(": %s (%s) besides the 2A03's five.", Plural(Count, "more channel", "more channels").c_str(), Added.c_str()) :
		": just the 2A03's five channels.";
	if (Chip == "VRC6")
		Text += " Two pulses and a sawtooth, a strong bass; the presets work on them.";
	else if (Chip == "VRC7")
		Text += " FM channels with the chip's built-in sounds: " + Vrc7PatchList() + ". Name one as an instrument in write_notes, "
			"set_instrument or add_instrument (a preset's name picks the one most like it).";
	else if (Chip == "MMC5")
		Text += " Two more pulses; the 2A03 presets work on them.";
	else if (Chip == "N163")
		Text += " Wavetable channels that play N163 sounds: " + WaveSoundList() + ". Their waves are drawn for them and can change "
			"as a note plays (a shimmer, brass opening up, a bell mellowing); add_instrument, write_notes and set_instrument take "
			"their names. Up to 6 channels keep the chip's switching whine above hearing (about 20 kHz); 7 or 8 bring it down to "
			"17 or 15 kHz.";
	else if (Chip == "FDS")
		Text += " A wavetable channel: it needs an FDS instrument, which you can't make yet; the user makes one in FamiTracker's "
			"instrument editor.";
	if (Notes > 0)
		Text += Format(" Cleared %s on the channels that went.", Plural(Notes, "note", "notes").c_str());
	if (Moved > 0 && S.ChannelCount() > 0)
		Text += Format(" Moved %s that steer%s the song (speed, loops, ends) from the channels that went to %s.",
			Plural(Moved, "effect", "effects").c_str(), Moved == 1 ? "s" : "", S.Channel(S.ChannelCount() - 1).FullName().c_str());
	if (Lost > 0)
		Text += Format(" %s that steered the song found no free effect column there and %s gone: check the loop with check_loop.",
			Plural(Lost, "effect", "effects").c_str(), Lost == 1 ? "is" : "are");
	if (Pal && !Chip.empty())
		Text += " Expansion chips play on NTSC machines, so the song is NTSC now.";
	Text += " FamiTracker can't undo switching chips: Module > Module properties switches back.";
	return WithRemarks(S, Text);
}

// Loops

SongFlow Flow(Session &S);

// The chord the song sounds in rows [First, End): the triad whose notes sound
// longest there (every melodic channel, with arpeggios), the bass note at the
// start counting extra for its root. False when nothing sounds.
bool SoundingChord(Session &S, int First, int End, int &Root, bool &Minor)
{
	double Weight[12] = { 0 };
	int Bass = -1, BassPitch = 1000;
	for (int c = 0; c < S.ChannelCount(); ++c) {
		if (!IsMelodic(S.Channel(c)))
			continue;
		const std::vector<NoteInfo> &Notes = S.Notes(c);
		int Arpeggio = 0;
		for (size_t i = 0; i < Notes.size(); ++i) {
			const NoteInfo &N = Notes[i];
			for (size_t e = 0; e < N.Effects.size(); ++e)
				if (N.Effects[e].first == 0 && N.Effects[e].second.first == EFF_ARPEGGIO)
					Arpeggio = N.Effects[e].second.second;
			const int From = std::max(First, N.Row), To = std::min(End, N.Row + N.Rows);
			if (To <= From)
				continue;
			const double Rows = To - From;
			Weight[N.Pitch % 12] += Rows;
			const std::vector<int> Above = ChordAbove(S, N, Arpeggio);
			for (size_t a = 0; a < Above.size(); ++a)
				Weight[(N.Pitch + Above[a]) % 12] += Rows;
			if (N.Row <= First + S.Time.RowsPerBeat && N.Pitch < BassPitch) {
				BassPitch = N.Pitch;
				Bass = N.Pitch % 12;
			}
		}
	}
	double Best = 0.0;
	Root = -1;
	for (int r = 0; r < 12; ++r)
		for (int m = 0; m < 2; ++m) {
			const int Third = (r + (m ? 3 : 4)) % 12, Fifth = (r + 7) % 12;
			double Score = Weight[r] + Weight[Third] + 0.8 * Weight[Fifth];
			for (int p = 0; p < 12; ++p)
				if (p != r && p != Third && p != Fifth)
					Score -= 0.5 * Weight[p];
			if (r == Bass)
				Score += 0.5 * (End - First);
			if (Score > Best) {
				Best = Score;
				Root = r;
				Minor = m != 0;
			}
		}
	return Root >= 0;
}

// "G (V)", "Am (vi)", "F#" when it isn't in the key
std::string ChordLabel(int Root, bool Minor, const Key &K)
{
	static const char *const MAJOR[7] = { "I", "ii", "iii", "IV", "V", "vi", "vii" };
	static const char *const MINOR[7] = { "i", "ii", "III", "iv", "v", "VI", "VII" };
	std::string Name = NoteClassName(Root, K) + (Minor ? "m" : "");
	const int *pSteps = ModeSteps(K.Mode);
	const bool KeyMinor = K.Mode == MODE_MINOR;
	for (int d = 0; d < 7; ++d)
		if ((K.Tonic + pSteps[d]) % 12 == Root) {
			std::string Numeral = KeyMinor ? MINOR[d] : MAJOR[d];
			// The numeral's case says major or minor
			const bool NumeralMinor = Numeral[0] == 'i' || Numeral[0] == 'v';
			if (KeyMinor && d == 4 && !Minor)
				Numeral = "V";
			else if (NumeralMinor != Minor)
				return Name;
			return Name + " (" + Numeral + ")";
		}
	return Name;
}

std::string SecondsText(double Seconds)
{
	return FormatNumber(std::floor(Seconds * 10 + 0.5) / 10) + " s";
}

std::string EffectName(int Effect)
{
	switch (Effect) {
	case EFF_ARPEGGIO: return "arpeggio";
	case EFF_VIBRATO: return "vibrato";
	case EFF_TREMOLO: return "tremolo";
	case EFF_VOLUME_SLIDE: return "volume slide";
	case EFF_PORTAMENTO: return "portamento";
	case EFF_PORTA_UP: return "pitch slide up";
	case EFF_PORTA_DOWN: return "pitch slide down";
	default: return std::string("effect ") + EffectLetter(Effect);
	}
}

// What differs between the loop's start the first time (from the intro, or
// the song's start) and on repeat (from the loop's end): notes hanging over,
// instruments, volumes, effects still on, speed and tempo. Rows: the song's
// rows in playing order with the loop twice; First and Second: where the loop
// starts in them.
std::vector<std::string> SeamProblems(Session &S, const std::vector<std::vector<Cell> > &Cells, const std::vector<int> &Columns,
	const std::vector<TimedRow> &Rows, size_t First, size_t Second, int LoopRow)
{
	std::vector<std::string> Seams;
	for (int c = 0; c < S.ChannelCount(); ++c) {
		const ChannelInfo &Info = S.Channel(c);
		std::vector<Cell> Played;
		for (size_t i = 0; i < Rows.size(); ++i)
			Played.push_back(Cells[static_cast<size_t>(c)][static_cast<size_t>(Rows[i].Row)]);
		const int Cols = Columns[static_cast<size_t>(c)];
		const ChannelState Before = StateAt(Played, static_cast<int>(First), Cols);
		const ChannelState Again = StateAt(Played, static_cast<int>(Second), Cols);
		const Cell &Start = Played[Second];
		const std::vector<NoteInfo> Notes = ReadNotes(Played, Cols);
		auto SoundingAt = [&](int Index) -> const NoteInfo* {
			for (size_t i = 0; i < Notes.size(); ++i)
				if (Notes[i].Row < Index && Notes[i].Row + Notes[i].Rows > Index)
					return &Notes[i];
			return 0;
		};
		if (!Start.HasNote() && !Start.IsEnd()) {
			const NoteInfo *pFirst = SoundingAt(static_cast<int>(First)), *pAgain = SoundingAt(static_cast<int>(Second));
			const int PitchFirst = pFirst != 0 ? pFirst->Pitch : -1, PitchAgain = pAgain != 0 ? pAgain->Pitch : -1;
			if (PitchFirst != PitchAgain && pAgain != 0) {
				// When the loop's own first note comes
				std::string Until = "for the whole loop";
				for (size_t i = 0; i < Notes.size(); ++i)
					if (Notes[i].Row > static_cast<int>(Second)) {
						Until = "until " + S.Time.Position(Rows[static_cast<size_t>(Notes[i].Row)].Row);
						break;
					}
				Seams.push_back(Format("%s: %s from the loop's end keeps sounding into %s on repeat (%s), where the first time %s. "
					"Put a note, release (===) or halt (---) there.", Info.FullName().c_str(), PitchText(Info, PitchAgain).c_str(),
					S.Time.Position(LoopRow).c_str(), Until.c_str(),
					PitchFirst < 0 ? "nothing sounds" : (PitchText(Info, PitchFirst) + " from the intro does").c_str()));
			}
			else if (PitchFirst != PitchAgain && pFirst != 0)
				Seams.push_back(Format("%s: the first time, %s from the intro sounds into %s; on repeat, nothing does.",
					Info.FullName().c_str(), PitchText(Info, PitchFirst).c_str(), S.Time.Position(LoopRow).c_str()));
		}
		if (!IsMelodic(Info) && Info.Kind != KIND_NOISE)
			continue;
		if (Start.Instrument == NO_INSTRUMENT && Before.Instrument != Again.Instrument && Before.Instrument != NO_INSTRUMENT &&
			Again.Instrument != NO_INSTRUMENT && !Start.IsEnd())
			Seams.push_back(Format("%s: the loop starts with %s the first time and %s on repeat; set the instrument on its first note.",
				Info.FullName().c_str(), InstrumentLabel(S.Song, Before.Instrument).c_str(), InstrumentLabel(S.Song, Again.Instrument).c_str()));
		if (Start.Volume == NO_VOLUME && Before.Volume != Again.Volume && Info.Kind != KIND_TRIANGLE)
			Seams.push_back(Format("%s: the loop starts at volume %d the first time and %d on repeat; set the volume on its first note.",
				Info.FullName().c_str(), Before.Volume, Again.Volume));
		for (int e = 1; e < EFF_COUNT; ++e) {
			if (!IsStickyEffect(e) || Before.Sticky[e] == Again.Sticky[e] || HasEffect(Start, e, Cols))
				continue;
			const bool StillOn = Again.Sticky[e] != StickyOff(e);
			const int On = StillOn ? Again.Sticky[e] : Before.Sticky[e];
			Seams.push_back(Format("%s: %s %s is %s at the loop's end, so the loop's start %s it on repeat but not the first time; "
				"set it at %s (%c%02X turns it off).", Info.FullName().c_str(), EffectName(e).c_str(), EffectText(e, On).c_str(),
				StillOn ? "still on" : "off", StillOn ? "gets" : "lacks", S.Time.Position(LoopRow).c_str(), EffectLetter(e), StickyOff(e)));
		}
	}

	// Speed and tempo as the loop starts, both times
	int SpeedFirst = S.Song.Speed, TempoFirst = S.Song.Tempo, Speed = S.Song.Speed, Tempo = S.Song.Tempo;
	bool StartSets = false;
	for (size_t i = 0; i <= Second; ++i) {
		if (i == First) {
			SpeedFirst = Speed;
			TempoFirst = Tempo;
		}
		for (int c = 0; c < S.ChannelCount(); ++c) {
			const Cell &C = Cells[static_cast<size_t>(c)][static_cast<size_t>(Rows[i].Row)];
			for (int k = 0; k < Columns[static_cast<size_t>(c)]; ++k)
				if (C.Effect[k] == EFF_SPEED) {
					if (i == Second)
						StartSets = true;
					else if (C.Param[k] >= S.Song.SplitPoint)
						Tempo = C.Param[k];
					else
						Speed = std::max(1, static_cast<int>(C.Param[k]));
				}
		}
	}
	if (!StartSets && (Speed != SpeedFirst || Tempo != TempoFirst))
		Seams.push_back(Format("The loop starts at speed %d, tempo %d the first time and speed %d, tempo %d on repeat; put the "
			"speed and tempo (Fxx) on its first row.", SpeedFirst, TempoFirst, Speed, Tempo));

	return Seams;
}

// "bar 8" when the row ends its bar, else "bar 8 beat 3"
std::string EndPosition(const Session &S, int LastRow)
{
	const int Bar = S.Time.BarOf(LastRow);
	if (S.Time.BarEnd(Bar) == LastRow + 1)
		return Format("bar %d", Bar);
	return S.Time.Position(LastRow);
}

// "bar 5" when the row starts its bar, else "bar 5 beat 3"
std::string StartPosition(const Session &S, int Row)
{
	const int Bar = S.Time.BarOf(Row);
	if (S.Time.BarStart(Bar) == Row)
		return Format("bar %d", Bar);
	return S.Time.Position(Row);
}

std::string CheckLoopTool(Session &S, const Json &)
{
	const int PL = S.Time.PatternLength;
	const SongFlow F = Flow(S);
	std::vector<std::vector<Cell> > Cells;
	std::vector<int> Columns;
	for (int c = 0; c < S.ChannelCount(); ++c) {
		Cells.push_back(S.Cells(c));
		Columns.push_back(S.Channel(c).EffectColumns);
	}
	int EndTick = 0;
	const std::vector<TimedRow> Rows = TimeRows(Cells, Columns, F, PL, S.Song.Speed, S.Song.Tempo, S.Song.SplitPoint,
		S.Song.FrameRate, 2, EndTick);
	if (Rows.empty())
		throw Problem("The song has no rows.");
	const double Rate = std::max(1, S.Song.FrameRate);
	const int LastRow = F.EndRow(PL) - 1;

	if (F.Halts())
		return Format("The song plays once and stops after %s (a halt, Cxx): %s. That suits a jingle or an ending; music that "
			"repeats loops instead (set_loop sets where).", EndPosition(S, LastRow).c_str(), SecondsText(EndTick / Rate).c_str());

	// Where the loop starts the first time and the second
	size_t First = Rows.size(), Second = Rows.size();
	for (size_t i = 0; i < Rows.size(); ++i)
		if (Rows[i].LoopStart) {
			if (First == Rows.size())
				First = i;
			else if (Second == Rows.size())
				Second = i;
		}
	if (First == Rows.size() || Second == Rows.size())
		throw Problem("The song's loop couldn't be followed.");
	const int LoopRow = F.LoopRow(PL);
	std::string Text;
	const double LoopSeconds = (Rows[Second].Tick - Rows[First].Tick) / Rate;
	if (First == 0)
		Text = Format("The whole song loops: %s, %s, then back to bar 1.", RangeText(1, S.Time.BarOf(LastRow)).c_str(),
			SecondsText(LoopSeconds).c_str());
	else
		Text = Format("Bars 1-%d play once as an intro (%s), then %s loop (%s): after %s it goes back to %s.",
			S.Time.BarOf(LoopRow) - 1, SecondsText(Rows[First].Tick / Rate).c_str(),
			RangeText(S.Time.BarOf(LoopRow), S.Time.BarOf(LastRow)).c_str(), SecondsText(LoopSeconds).c_str(),
			EndPosition(S, LastRow).c_str(), StartPosition(S, LoopRow).c_str());
	const int LoopRows = F.LoopRows();
	if (LoopRows % std::max(1, S.Time.RowsPerBar) != 0)
		Text += Format(" The loop is %s long, not whole bars; whole bars repeat more steadily.", S.Time.Length(LoopRows).c_str());

	const std::vector<std::string> Seams = SeamProblems(S, Cells, Columns, Rows, First, Second, LoopRow);

	if (Seams.empty())
		Text += "\nThe seam is clean: every channel starts the loop the same way both times.";
	else {
		Text += "\nAt the seam (the loop's end into its start):";
		for (size_t i = 0; i < Seams.size(); ++i)
			Text += "\n- " + Seams[i];
	}

	// How it sounds going round: the chords, the melody's step
	Key K;
	if (FindKey(S, 0, S.Time.TotalRows(), K)) {
		const int EndBar = S.Time.BarOf(LastRow), StartBar = S.Time.BarOf(LoopRow);
		int EndRoot, StartRoot;
		bool EndMinor = false, StartMinor = false;
		if (SoundingChord(S, S.Time.BarStart(EndBar), LastRow + 1, EndRoot, EndMinor) &&
			SoundingChord(S, LoopRow, std::min(S.Time.BarEnd(StartBar), S.Time.TotalRows()), StartRoot, StartMinor)) {
			const std::string EndChord = ChordLabel(EndRoot, EndMinor, K), StartChord = ChordLabel(StartRoot, StartMinor, K);
			Text += Format("\nIn %s, the loop ends on %s and starts on %s", KeyName(K).c_str(), EndChord.c_str(), StartChord.c_str());
			const int Step = ((StartRoot - EndRoot) % 12 + 12) % 12;
			if (EndRoot == StartRoot && EndMinor == StartMinor)
				Text += ": the same chord; a V or IV in the last bar leads back more smoothly.";
			else if (Step == 5 || Step == 7 || (EndRoot - K.Tonic + 12) % 12 == 7)
				Text += ": the end leads back into the start.";
			else
				Text += ".";
		}
		const int Melody = GuessMelodyChannel(S, 0, S.Time.TotalRows());
		if (Melody >= 0) {
			int Last = -1, Next = -1;
			for (size_t i = 0; i < Rows.size(); ++i) {
				const Cell &C = Cells[static_cast<size_t>(Melody)][static_cast<size_t>(Rows[i].Row)];
				if (!C.HasNote())
					continue;
				if (i < Second)
					Last = C.Pitch();
				else {
					Next = C.Pitch();
					break;
				}
			}
			if (Last >= 0 && Next >= 0) {
				const int Leap = std::abs(Next - Last);
				Text += Format(" The melody (%s) goes from %s at the end to %s at the start: %s.", S.Channel(Melody).FullName().c_str(),
					PitchName(Last).c_str(), PitchName(Next).c_str(),
					Leap == 0 ? "the same note" : Leap <= 2 ? "a step" : Leap <= 7 ? Format("a leap of %d semitones", Leap).c_str() :
					Format("a big leap of %d semitones; a smaller one sounds smoother", Leap).c_str());
			}
		}
	}
	return Text;
}

// Sets where the song loops: a jump (Bxx) after to_bar back to from_bar,
// which starts a frame; jumps and halts that would end it earlier go
std::string SetLoopTool(Session &S, const Json &Args)
{
	const int PL = S.Time.PatternLength, Bars = S.Time.BarCount();
	const int From = IntArg(Args, "from_bar", 1, 1, Bars);
	const int To = IntArg(Args, "to_bar", Bars, 1, Bars);
	const int LoopRow = S.Time.BarStart(From);
	if (LoopRow % PL != 0) {
		std::string Starts;
		for (int Bar = 1; Bar <= Bars; Bar += S.Time.BarsPerFrame())
			Starts += (Starts.empty() ? "" : ", ") + FormatNumber(Bar);
		throw Problem(Format("A loop starts where a frame starts: bar %s (a frame is %s).", Starts.c_str(),
			Plural(S.Time.BarsPerFrame(), "bar", "bars").c_str()));
	}
	if (To < From)
		throw Problem("to_bar comes before from_bar.");
	if (!S.Song.DirectChanges)
		throw Problem(Format("Setting the loop changes the song, which needs direct changes. The user can do it: a B%02X effect on "
			"the last row of bar %d (frame %02X, row %02X) jumps back to bar %d.", LoopRow / PL, To, (S.Time.BarEnd(To) - 1) / PL,
			(S.Time.BarEnd(To) - 1) % PL, From));
	const int EndRow = S.Time.BarEnd(To) - 1;

	// The song's jumps and halts go (in the effect columns that play), but
	// for the one the loop needs
	const bool Natural = LoopRow == 0 && EndRow == S.Time.TotalRows() - 1;	// the song goes back to its start by itself
	Draft D(S);
	int Removed = 0;
	for (int c = 0; c < S.ChannelCount(); ++c) {
		std::vector<Cell> &Cells = D.Cells(c);
		const int Cols = S.Channel(c).EffectColumns;
		for (size_t r = 0; r < Cells.size(); ++r)
			for (int k = 0; k < Cols; ++k)
				if (Cells[r].Effect[k] == EFF_JUMP || Cells[r].Effect[k] == EFF_HALT) {
					if (Natural || static_cast<int>(r) != EndRow || Cells[r].Effect[k] != EFF_JUMP || Cells[r].Param[k] != LoopRow / PL)
						++Removed;
					Cells[r].Effect[k] = EFF_NONE;
					Cells[r].Param[k] = 0;
				}
	}
	if (!Natural) {
		bool Placed = false;
		for (int c = 0; c < S.ChannelCount() && !Placed; ++c)
			Placed = AddEffect(D.Cells(c)[static_cast<size_t>(EndRow)], EFF_JUMP, LoopRow / PL, S.Channel(c).EffectColumns);
		if (!Placed)
			throw Problem(Format("No channel has a free effect column on the last row of bar %d for the jump; show another effect "
				"column in FamiTracker.", To));
	}
	std::vector<Change> Changes;
	std::vector<std::string> Warnings;
	int Start = 0;
	if (!D.Collect(0, true, Changes, Start, Warnings))
		return Format("The song already loops from bar %d after bar %d.", From, To);
	std::string What = From == 1 ? Format("the song loops from bar 1 after bar %d", To) :
		Format("bars 1-%d play once as an intro, then bars %d-%d loop", From - 1, From, To);
	if (!Natural)
		What += Format(" (B%02X on the last row of bar %d)", LoopRow / PL, To);
	if (Removed > 0)
		What += Format("; %s that ended it elsewhere went", Plural(Removed, "jump or halt", "jumps and halts").c_str());
	if (EndRow + 1 < S.Time.TotalRows())
		Warnings.push_back(Format("Bars after %d don't play now.", To));
	const std::string Done = Commit(S, Changes, Start, false, "", What);

	// What the song does now, as FamiTracker has it
	S.Load();
	const SongFlow After = Flow(S);
	if (After.Halts())
		Warnings.push_back(Format("FamiTracker's song still stops after %s (a halt, Cxx); check_loop shows how it plays.",
			EndPosition(S, After.EndRow(PL) - 1).c_str()));
	else if (After.LoopRow(PL) != LoopRow || After.EndRow(PL) - 1 != EndRow) {
		std::string Text = Format("FamiTracker's song doesn't loop that way: after %s it goes back to %s.",
			EndPosition(S, After.EndRow(PL) - 1).c_str(), StartPosition(S, After.LoopRow(PL)).c_str());
		// A skip in the loop's last frame, before the jump, ends the frame first
		int Skip = -1;
		for (int c = 0; c < S.ChannelCount(); ++c) {
			const std::vector<Cell> &Cells = S.Cells(c);
			for (int r = EndRow / PL * PL; r < EndRow && r < static_cast<int>(Cells.size()); ++r)
				if (HasEffect(Cells[static_cast<size_t>(r)], EFF_SKIP, S.Channel(c).EffectColumns) && (Skip < 0 || r < Skip))
					Skip = r;
		}
		if (Skip >= 0)
			Text += Format(" A skip (Dxx) at %s ends that frame before the jump; set_loop leaves skips alone.",
				S.Time.Position(Skip).c_str());
		Warnings.push_back(Text + " check_loop shows how it plays.");
	}
	return WithRemarks(S, Done, Warnings);
}

// The song's form: bars put in and taken out

// A channel's rows with Shift empty rows put in at First (Shift > 0), or the
// -Shift rows from First on taken out (Shift < 0), the rest moving along;
// Rows of them
std::vector<Cell> ShiftedRows(const std::vector<Cell> &Cells, int First, int Shift, int Rows)
{
	const size_t Head = std::min(Cells.size(), static_cast<size_t>(std::max(0, First)));
	std::vector<Cell> Out(Cells.begin(), Cells.begin() + static_cast<std::ptrdiff_t>(Head));
	Out.resize(static_cast<size_t>(std::max(0, First + std::max(0, Shift))));
	const size_t From = static_cast<size_t>(std::max(0, First - std::min(0, Shift)));
	if (From < Cells.size())
		Out.insert(Out.end(), Cells.begin() + static_cast<std::ptrdiff_t>(From), Cells.end());
	Out.resize(static_cast<size_t>(std::max(0, Rows)));
	return Out;
}

// How the song plays, in words: "bars 1-4 play once, then bars 5-16 loop"
std::string FlowText(const Session &S, const SongFlow &F)
{
	const int PL = S.Time.PatternLength;
	const int Last = F.EndRow(PL) - 1;
	if (F.Halts())
		return Format("it plays once and stops after %s", EndPosition(S, Last).c_str());
	if (F.LoopRow(PL) <= 0)
		return Format("the whole song loops (%s)", RangeText(1, S.Time.BarOf(Last)).c_str());
	const int LoopBar = S.Time.BarOf(F.LoopRow(PL)), LastBar = S.Time.BarOf(Last);
	return Format("%s once, then %s %s", LoopBar == 2 ? "bar 1 plays" : Format("bars 1-%d play", LoopBar - 1).c_str(),
		RangeText(LoopBar, LastBar).c_str(), LoopBar == LastBar ? "loops" : "loop");
}

int LinkedCopies(const Session &S)
{
	int Count = 0;
	for (size_t i = 0; i < S.Song.Clips.size(); ++i)
		Count += S.Song.Clips[i].Links;
	return Count;
}

// Moves the song's rows from First on by Shift in every channel (empty bars
// put in, or bars taken out) as one undoable change that keeps how the song
// plays: jumps go to the bars they went to, the loop starts where it did and
// the song ends after what played last (bars put in right after that play
// too). Whole frames move in FamiTracker's frame list, so the frames after
// keep their patterns and linked copies; other moves rewrite the frames after.
std::string ShiftSong(Session &S, int First, int Shift, const std::string &What, std::vector<std::string> &Warnings)
{
	const int PL = S.Time.PatternLength;
	const int OldFrames = S.Time.Frames, OldTotal = S.Time.TotalRows();
	int NewFrames = std::max(1, (OldTotal + Shift + PL - 1) / PL), Rows = NewFrames * PL;
	const bool Whole = Shift % PL == 0;
	const std::string FrameBars = Plural(S.Time.BarsPerFrame(), "bar", "bars");
	const SongFlow Before = Flow(S);
	const int OldLast = Before.EndRow(PL) - 1, OldLoop = Before.LoopRow(PL);
	auto Moved = [&](int Row) {
		if (Row < First)
			return Row;
		if (Shift < 0 && Row < First - Shift)
			return -1;	// taken out
		return Row + Shift;
	};

	// Skips (Dxx) end a frame early and go on in the next, so they keep their meaning only when whole frames move
	for (int c = 0; c < S.ChannelCount() && !Whole; ++c) {
		const std::vector<Cell> &Cells = S.Cells(c);
		for (int r = 0; r < static_cast<int>(Cells.size()); ++r)
			for (int k = 0; k < S.Channel(c).EffectColumns && k < EFFECT_COLUMNS; ++k) {
				const Cell &C = Cells[static_cast<size_t>(r)];
				if (C.Effect[k] == EFF_SKIP && (r >= First || (r / PL + 1) * PL + C.Param[k] >= First))
					throw Problem(Format("The song skips ahead (D%02X) at %s, which ends that frame early, so bars go in or out "
						"there only a whole frame (%s) at a time.", C.Param[k], S.Time.Position(r).c_str(), FrameBars.c_str()));
			}
	}

	// The loop starts where it did, moved along (with what comes after its first bars when they go)
	int NewLoop = -1;
	bool LoopTakesNew = false;	// the bars put in begin the loop
	if (!Before.Halts()) {
		NewLoop = Moved(OldLoop);
		if (NewLoop < 0)
			NewLoop = First;
		if (NewLoop % PL != OldLoop % PL) {
			if (Shift > 0 && OldLoop == First) {
				NewLoop = OldLoop;	// FamiTracker jumps back only to a frame's start
				LoopTakesNew = true;
			}
			else if (Shift > 0)
				throw Problem(Format("The song loops back to %s, and bars put in before it would move that inside a frame: "
					"FamiTracker jumps back only to a frame's start. Put in a whole frame (%s), or put the bars in at %s, where "
					"they begin the loop.", StartPosition(S, OldLoop).c_str(), FrameBars.c_str(), StartPosition(S, OldLoop).c_str()));
			else
				throw Problem(Format("The song loops back to %s, and taking out those bars would move that inside a frame: "
					"FamiTracker jumps back only to a frame's start. Take out a whole frame (%s) instead.",
					StartPosition(S, OldLoop).c_str(), FrameBars.c_str()));
		}
	}

	// What plays last: the row that did, moved along (or the one before the bars taken out); bars put in right after it play too
	const bool AtEnd = Shift > 0 && First == OldLast + 1;
	int NewLast = AtEnd ? OldLast + Shift : Moved(OldLast);
	if (NewLast < 0)
		NewLast = First - 1;
	if (NewLast < 0)
		throw Problem("That takes out every bar the song plays.");
	if (NewLoop > NewLast)
		throw Problem(Format("That takes out the whole loop (%s); take out fewer bars, or set another loop first with set_loop.",
			RangeText(S.Time.BarOf(OldLoop), S.Time.BarOf(OldLast)).c_str()));

	// Every channel moved along, its jumps going to the bars they went to
	std::vector<std::vector<Cell> > Final;
	std::vector<int> Columns;
	for (int c = 0; c < S.ChannelCount(); ++c) {
		const int Cols = std::max(1, std::min(EFFECT_COLUMNS, S.Channel(c).EffectColumns));
		std::vector<Cell> Cells = ShiftedRows(S.Cells(c), First, Shift, Rows);
		for (int r = 0; r < Rows; ++r)
			for (int k = 0; k < Cols; ++k) {
				Cell &C = Cells[static_cast<size_t>(r)];
				if (C.Effect[k] != EFF_JUMP)
					continue;
				const int Old = std::min(static_cast<int>(C.Param[k]), OldFrames - 1) * PL;
				int Target = Moved(Old);
				if (Target < 0)
					Target = First;
				if (Target % PL != 0 && Shift > 0 && Old == First)
					Target = First;	// the bars put in begin where it jumps
				if (Target % PL != 0)
					throw Problem(Format("A jump (B%02X) at %s goes to %s, which would move inside a frame, where FamiTracker "
						"can't jump. Put in or take out a whole frame (%s) instead.", C.Param[k],
						S.Time.Position(r < First ? r : r - Shift).c_str(), StartPosition(S, Old).c_str(), FrameBars.c_str()));
				C.Param[k] = static_cast<unsigned char>(Target / PL);
			}
		// Bars put in right after the end play before the song goes back (or stops)
		if (AtEnd) {
			Cell &End = Cells[static_cast<size_t>(OldLast)];
			for (int k = 0; k < Cols; ++k)
				if (End.Effect[k] == EFF_JUMP || End.Effect[k] == EFF_HALT) {
					AddEffect(Cells[static_cast<size_t>(NewLast)], End.Effect[k], End.Param[k], Cols);
					End.Effect[k] = EFF_NONE;
					End.Param[k] = 0;
				}
		}
		Final.push_back(Cells);
		Columns.push_back(Cols);
	}

	// Frames the move leaves at the end holding nothing, after all that plays (and the bars put in), go
	if (!Whole) {
		auto Padding = [&](const std::vector<std::vector<Cell> > &Channels, int Frames, int Last) {
			int Pad = 0;
			for (int f = Frames - 1; f > 0 && f * PL > Last; --f, ++Pad)
				for (size_t c = 0; c < Channels.size(); ++c)
					for (int r = f * PL; r < (f + 1) * PL && r < static_cast<int>(Channels[c].size()); ++r)
						if (!Channels[c][static_cast<size_t>(r)].IsEmpty())
							return Pad;
			return Pad;
		};
		std::vector<std::vector<Cell> > Old;
		for (int c = 0; c < S.ChannelCount(); ++c)
			Old.push_back(S.Cells(c));
		const int Extra = Padding(Final, NewFrames, Shift > 0 ? std::max(NewLast, First + Shift - 1) : NewLast) -
			Padding(Old, OldFrames, OldLast);
		if (Extra > 0) {
			NewFrames -= Extra;
			Rows = NewFrames * PL;
			for (size_t c = 0; c < Final.size(); ++c)
				Final[c].resize(static_cast<size_t>(Rows));
		}
	}

	// The song ends where it should: its jump or halt written there when it went, or comes too late
	auto Right = [&](const SongFlow &F) {
		if (Before.Halts())
			return F.Halts() && F.EndRow(PL) - 1 == NewLast;
		return !F.Halts() && F.LoopRow(PL) == NewLoop && F.EndRow(PL) - 1 == NewLast;
	};
	if (!Right(FollowSong(Final, Columns, PL, NewFrames)) && (Before.Halts() || NewLoop % PL == 0)) {
		bool Placed = false;
		for (int c = 0; c < S.ChannelCount() && !Placed; ++c)
			Placed = AddEffect(Final[static_cast<size_t>(c)][static_cast<size_t>(NewLast)], Before.Halts() ? EFF_HALT : EFF_JUMP,
				Before.Halts() ? 0 : NewLoop / PL, Columns[static_cast<size_t>(c)]);
		if (!Placed)
			throw Problem(Format("No channel has a free effect column on the song's new last row (%s) for its %s (all four are "
				"full in every channel).", S.Time.Position(NewLast).c_str(), Before.Halts() ? "halt (C00)" : "jump back (Bxx)"));
	}

	// Whole frames move in the frame list, so the frames after keep their patterns (and linked
	// copies); otherwise the rows after are rewritten, frames coming or going at the end
	SongWrite Write;
	if (Whole) {
		Write.MoveFrame = (First + PL - 1) / PL;
		Write.MoveBy = Shift / PL;
	}
	else if (NewFrames != OldFrames) {
		Write.MoveFrame = std::min(OldFrames, NewFrames);
		Write.MoveBy = NewFrames - OldFrames;
	}
	Write.Cursor = std::min(First, Rows - 1);
	// What the frame list leaves to write: the rows that differ
	std::vector<std::vector<Cell> > Base;
	int Low = -1, High = -1;
	for (int c = 0; c < S.ChannelCount(); ++c) {
		Base.push_back(ShiftedRows(S.Cells(c), Write.MoveFrame * PL, Write.MoveBy * PL, Rows));
		for (int r = 0; r < Rows; ++r)
			if (Final[static_cast<size_t>(c)][static_cast<size_t>(r)] != Base.back()[static_cast<size_t>(r)]) {
				Low = Low < 0 ? r : std::min(Low, r);
				High = std::max(High, r);
			}
	}
	std::vector<ChannelCells> Parts;
	for (int c = 0; c < S.ChannelCount() && High >= 0; ++c) {
		const std::vector<Cell> &New = Final[static_cast<size_t>(c)], &Old = Base[static_cast<size_t>(c)];
		if (std::equal(New.begin() + Low, New.begin() + High + 1, Old.begin() + Low))
			continue;
		ChannelCells Part;
		Part.Channel = c;
		Part.Exact = true;
		Part.Cells.assign(New.begin() + Low, New.begin() + High + 1);
		Parts.push_back(Part);
	}
	if (Parts.empty() && Write.MoveBy == 0)
		throw Problem("Nothing would change: those bars are empty, and FamiTracker's frames keep their length.");
	Write.First = std::max(0, Low);
	const int LinksBefore = LinkedCopies(S);
	std::string Why, Error;
	if (!S.Host().CanChange(Why))
		throw Problem(Why);
	if (!S.Host().WriteSong(Parts, Write, Error))
		throw Problem(Error.empty() ? "The song couldn't be changed." : Error);

	// How it plays now, as FamiTracker has it
	S.Load();
	const SongFlow Now = Flow(S);
	std::string Text = "Changed the song: " + What + ". One Ctrl+Z in FamiTracker undoes it. Now " + FlowText(S, Now) + ".";
	if (LoopTakesNew)
		Text += " The new bars begin the loop: FamiTracker jumps back only to a frame's start, and the bar the loop began with "
			"is inside a frame now.";
	else if (Shift > 0 && OldLoop == First)
		Text += Format(" The new bars play once, before the loop; set_loop with from_bar %d takes them into it.", S.Time.BarOf(First));
	if (Shift > 0) {
		// Bars the song never gets to: after its loop's jump back, or its halt
		const std::vector<bool> Played = Now.Played(S.Time.TotalRows(), PL);
		int From = -1, To = -1;
		for (int r = First; r < First + Shift && r < static_cast<int>(Played.size()); ++r)
			if (!Played[static_cast<size_t>(r)]) {
				From = From < 0 ? r : From;
				To = r;
			}
		if (From >= 0) {
			std::string Bars = RangeText(S.Time.BarOf(From), S.Time.BarOf(To));
			Bars[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(Bars[0])));
			const bool One = S.Time.BarOf(From) == S.Time.BarOf(To);
			Warnings.push_back(Format("%s %s play: the song %s after %s, before %s. set_loop with to_bar %d takes it on through %s.",
				Bars.c_str(), One ? "doesn't" : "don't", Now.Halts() ? "stops" : "goes back", EndPosition(S, Now.EndRow(PL) - 1).c_str(),
				One ? "it" : "them", S.Time.BarOf(To), One ? "it" : "them"));
		}
	}
	const int Unlinked = LinksBefore - LinkedCopies(S);
	if (Unlinked > 0)
		Warnings.push_back(Format("%s of clips became plain notes: their notes moved inside frames.",
			Plural(Unlinked, "linked copy", "linked copies").c_str()));
	if (!Right(Now))
		Warnings.push_back("FamiTracker's song doesn't end where it should now; check_loop shows how it plays, and set_loop sets "
			"the loop.");
	return Text;
}

std::string InsertBarsTool(Session &S, const Json &Args)
{
	if (!S.Song.DirectChanges)
		throw Problem("Putting bars in moves the rest of the song, which needs direct changes: the user can allow them in File > "
			"Connect Claude Code, or insert frames in FamiTracker's frame list.");
	const int PL = S.Time.PatternLength, RPB = S.Time.RowsPerBar, BPF = S.Time.BarsPerFrame(), Bars = S.Time.BarCount();
	if (Arg(Args, "at_bar") == 0)
		throw Problem(Format("Say where with at_bar: the new bars go before that bar (%d adds them at the end).", Bars + 1));
	const int At = IntArg(Args, "at_bar", 1, 1, Bars + 1);
	const int Count = IntArg(Args, "bars", 1, 1, S.MaxBars());
	if (PL % RPB != 0 && ((At - 1) % BPF != 0 || Count % BPF != 0))
		throw Problem(Format("A frame here is %d rows, not whole bars of %d, so bars go in a frame (%s) at a time, before a "
			"frame's first bar (bar 1, %d, %d...).", PL, RPB, Plural(BPF, "bar", "bars").c_str(), BPF + 1, 2 * BPF + 1));
	const int Shift = PL % RPB == 0 ? Count * RPB : Count / BPF * PL;
	if (S.Time.TotalRows() + Shift > S.MaxRows())
		throw Problem(Format("That's more than a song can hold (%d bars; it has %d).", S.MaxBars(), Bars));
	const std::string New = Plural(Count, "empty bar", "empty bars");
	const std::string What = At > Bars ? Format("%s added at the end (%s)", New.c_str(), RangeText(At, At + Count - 1).c_str()) :
		Format("%s put in at bar %d; what was bar %d on is bar %d on now", New.c_str(), At, At, At + Count);
	std::vector<std::string> Warnings;
	std::string Text = ShiftSong(S, S.Time.BarStart(At), Shift, What, Warnings);
	Text += Format(" Fill %s with write_notes, copy_bars or the arranging tools.", RangeText(At, At + Count - 1).c_str());
	return WithRemarks(S, Text, Warnings);
}

std::string RemoveBarsTool(Session &S, const Json &Args)
{
	if (!S.Song.DirectChanges)
		throw Problem("Taking bars out moves the rest of the song, which needs direct changes: the user can allow them in File > "
			"Connect Claude Code, or delete frames in FamiTracker's frame list.");
	const int PL = S.Time.PatternLength, RPB = S.Time.RowsPerBar, BPF = S.Time.BarsPerFrame(), Bars = S.Time.BarCount();
	if (Arg(Args, "from_bar") == 0)
		throw Problem("Say which bars with from_bar (and to_bar).");
	const int From = IntArg(Args, "from_bar", 1, 1, Bars);
	const int To = IntArg(Args, "to_bar", From, From, Bars);
	if (From == 1 && To == Bars)
		throw Problem("That's every bar of the song.");
	if (PL % RPB != 0 && ((From - 1) % BPF != 0 || (To - From + 1) % BPF != 0))
		throw Problem(Format("A frame here is %d rows, not whole bars of %d, so bars come out a frame (%s) at a time, from a "
			"frame's first bar (bar 1, %d, %d...).", PL, RPB, Plural(BPF, "bar", "bars").c_str(), BPF + 1, 2 * BPF + 1));
	const int First = S.Time.BarStart(From), End = std::min(S.Time.BarEnd(To), S.Time.TotalRows());
	const std::string What = To < Bars ? Format("%s taken out; what came after starts at bar %d now", RangeText(From, To).c_str(), From) :
		Format("%s taken out at the end", RangeText(From, To).c_str());
	std::vector<std::string> Warnings;
	const std::string Text = ShiftSong(S, First, First - End, What, Warnings);
	return WithRemarks(S, Text, Warnings);
}

// Versions to compare: the track as it is now, kept as a track of its own

std::string KeepVersionTool(Session &S, const Json &Args)
{
	const std::string Base = S.Song.TrackName, Label = StringArg(Args, "name");
	const std::vector<std::string> &Names = S.Song.TrackNames;
	auto Taken = [&](const std::string &Name) { return std::find(Names.begin(), Names.end(), Name) != Names.end(); };
	std::string Name = Label.empty() ? std::string() : Base + " (" + Label + ")";
	for (int n = 1; Name.empty() || Taken(Name); ++n)
		Name = Label.empty() ? Format("%s (version %d)", Base.c_str(), n) : Format("%s (%s %d)", Base.c_str(), Label.c_str(), n + 1);
	std::vector<std::vector<Cell> > Channels;
	for (int c = 0; c < S.ChannelCount(); ++c)
		Channels.push_back(S.Cells(c));
	const int Here = S.Song.Track;
	std::string Error;
	const int Track = S.Host().AddTrack(Name, Channels, S.Song.Speed, S.Song.Tempo, Error);
	if (Track < 0)
		throw Problem("The version couldn't be kept: " + (Error.empty() ? std::string("FamiTracker refused.") : Error));
	if (!S.Host().SelectTrack(Here, Error))
		throw Problem(Format("Kept it as track %d \"%s\", but FamiTracker shows that one now: %s", Track + 1, Name.c_str(),
			Error.c_str()));
	return Format("Kept track %d \"%s\" as it is now, as track %d \"%s\". You're still on \"%s\", so change it as you like: "
		"compare_versions then plays the same bars of both for the user, one after the other, and select_track goes to the "
		"kept one to carry on from it instead. FamiTracker can't undo adding a track: the user removes one in Module > Module "
		"properties.", Here + 1, Base.c_str(), Track + 1, Name.c_str(), Base.c_str());
}

std::string CompareVersionsTool(Session &S, const Json &Args)
{
	const int Here = S.Song.Track;
	const std::string HereName = S.Song.TrackName;
	std::vector<int> Tracks;
	const Json *pVersions = Arg(Args, "versions");
	if (pVersions != 0) {
		const size_t Count = pVersions->IsArray() ? pVersions->Size() : 1;
		for (size_t i = 0; i < Count; ++i) {
			Json One = Json::MakeObject();
			One.Set("track", pVersions->IsArray() ? pVersions->At(i) : *pVersions);
			const int Track = TrackArg(S, One, "track");
			if (std::find(Tracks.begin(), Tracks.end(), Track) == Tracks.end())
				Tracks.push_back(Track);
		}
		if (Tracks.size() == 1 && Tracks[0] != Here)
			Tracks.push_back(Here);	// one named: with the track worked on
	}
	else {
		// The track worked on and its newest kept version
		const std::string Prefix = HereName + " (";
		int Kept = -1;
		for (size_t t = 0; t < S.Song.TrackNames.size(); ++t)
			if (static_cast<int>(t) != Here && S.Song.TrackNames[t].compare(0, Prefix.size(), Prefix) == 0)
				Kept = static_cast<int>(t);
		if (Kept < 0)
			throw Problem(Format("\"%s\" has no kept version to compare with: keep_version keeps one before you change it, or "
				"name the tracks with versions.", HereName.c_str()));
		Tracks.push_back(Kept);
		Tracks.push_back(Here);
	}
	if (Tracks.size() < 2)
		throw Problem("Name two tracks or more to compare.");
	if (Tracks.size() > 8)
		throw Problem("Compare up to 8 versions at a time.");
	const int From = IntArg(Args, "from_bar", 1, 1, S.MaxBars());
	const int Bars = IntArg(Args, "bars", 8, 1, 64);

	// The same bars of each version, read from its track
	SongHost &Host = S.Host();
	int Selected = Here, Last = From + Bars - 1;
	std::string Error, Names;
	PlayRequest Request;
	try {
		for (size_t i = 0; i < Tracks.size(); ++i) {
			if (Tracks[i] != Selected && !Host.SelectTrack(Tracks[i], Error))
				throw Problem(Error.empty() ? std::string("FamiTracker didn't switch tracks.") : Error);
			Selected = Tracks[i];
			SongInfo Info;
			if (!Host.GetSong(Info, Error))
				throw Problem(Error);
			const Timing Time(Info.PatternLength, Info.Frames, Info.FirstHighlight, Info.SecondHighlight);
			const std::string Label = Format("track %d \"%s\"", Tracks[i] + 1, Info.TrackName.c_str());
			if (From > Time.BarCount())
				throw Problem(Format("%s has only %d bars.", Label.c_str(), Time.BarCount()));
			Last = std::min(Last, Time.BarCount());
			PlayPart Part;
			Part.Track = Tracks[i];
			Part.First = Time.BarStart(From);
			Part.Rows = Time.BarEnd(std::min(From + Bars - 1, Time.BarCount())) - Part.First;
			if (i == 0) {
				Request.Track = Part.Track;
				Request.First = Part.First;
				Request.Rows = Part.Rows;
			}
			else
				Request.Then.push_back(Part);
			Names += (i == 0 ? "" : ", then of ") + Label;
		}
		Request.Loop = BoolArg(Args, "loop", false);
		Request.ReturnTrack = Here;
		if (!Host.Play(Request, Error))
			throw Problem(Error.empty() ? std::string("FamiTracker couldn't play.") : Error);
	}
	catch (...) {
		std::string Ignored;
		if (Selected != Here)
			Host.SelectTrack(Here, Ignored);
		throw;
	}
	return Format("Playing %s of %s for the user%s; afterwards FamiTracker shows \"%s\" again. You can't hear it yourself: ask "
		"the user which they like better, and what they hear in it. select_track goes to a version to carry on from it.",
		RangeText(From, Last).c_str(), Names.c_str(), Request.Loop ? ", over and over until you call stop" : "", HereName.c_str());
}

// Music for games: each track as a looping OGG

// The track's file name: its own name, or the song's for one called "New song"
std::string TrackFileName(const Session &S, int Track, std::vector<std::string> &Taken)
{
	std::string Name = static_cast<size_t>(Track) < S.Song.TrackNames.size() ? Trim(S.Song.TrackNames[static_cast<size_t>(Track)]) : "";
	if (Name.empty() || Lowercase(Name) == "new song") {
		Name = Trim(S.Song.Title);
		if (Name.empty()) {
			Name = S.Song.FileName;
			const size_t Dot = Name.rfind('.');
			if (Dot != std::string::npos)
				Name = Name.substr(0, Dot);
		}
		if (S.Song.Tracks > 1)
			Name += Format(" %d", Track + 1);
	}
	const std::string Base = SafeFileName(Name);
	std::string File = Base + ".ogg";
	for (int n = 2; std::find(Taken.begin(), Taken.end(), Lowercase(File)) != Taken.end(); ++n)
		File = Format("%s %d.ogg", Base.c_str(), n);
	Taken.push_back(Lowercase(File));
	return File;
}

// One track rendered, cut where it loops, brought up so its loudest peak is at
// -1 dBFS (when Normalize; never more than 12 dB), encoded and saved next to the song
std::string ExportTrack(Session &S, int Track, float Quality, bool Normalize, std::vector<std::string> &Taken, std::string &Code)
{
	const int PL = S.Time.PatternLength;
	const SongFlow F = Flow(S);
	std::vector<std::vector<Cell> > Cells;
	std::vector<int> Columns;
	for (int c = 0; c < S.ChannelCount(); ++c) {
		Cells.push_back(S.Cells(c));
		Columns.push_back(S.Channel(c).EffectColumns);
	}
	int EndTick = 0;
	const std::vector<TimedRow> Rows = TimeRows(Cells, Columns, F, PL, S.Song.Speed, S.Song.Tempo, S.Song.SplitPoint,
		S.Song.FrameRate, 2, EndTick);
	if (Rows.empty())
		throw Problem("The track has no rows.");
	const double Rate = std::max(1, S.Song.FrameRate);
	bool Clean = true;
	if (!F.Halts()) {
		size_t First = Rows.size(), Second = Rows.size();
		for (size_t i = 0; i < Rows.size(); ++i)
			if (Rows[i].LoopStart) {
				if (First == Rows.size())
					First = i;
				else if (Second == Rows.size())
					Second = i;
			}
		if (Second < Rows.size())
			Clean = SeamProblems(S, Cells, Columns, Rows, First, Second, F.LoopRow(PL)).empty();
	}

	// A little more than the loop twice, so the third time it starts is there too
	std::vector<float> Samples;
	std::vector<std::pair<int, long long> > Marks;
	int SampleRate = 0;
	std::string Error;
	if (!S.Host().RenderTrack(Track, EndTick / Rate + 1.0, Samples, SampleRate, Marks, Error))
		throw Problem("The track couldn't be rendered: " + Error);
	GameCut Cut;
	if (!CutForGame(Samples, SampleRate, Rows, Marks, F.Halts(), Clean, Cut, Error))
		throw Problem("The track couldn't be cut for a game: " + Error);

	const std::string File = TrackFileName(S, Track, Taken);
	OggComments Comments;
	Comments.push_back(std::make_pair(std::string("TITLE"), File.substr(0, File.size() - 4)));
	if (!S.Song.Title.empty())
		Comments.push_back(std::make_pair(std::string("ALBUM"), S.Song.Title));
	if (!S.Song.Author.empty())
		Comments.push_back(std::make_pair(std::string("ARTIST"), S.Song.Author));
	if (Cut.Loops) {
		// The loop in samples, as RPG Maker and other engines read it
		Comments.push_back(std::make_pair(std::string("LOOPSTART"), Format("%lld", Cut.LoopStart)));
		Comments.push_back(std::make_pair(std::string("LOOPLENGTH"), Format("%lld", Cut.Length() - Cut.LoopStart)));
		Comments.push_back(std::make_pair(std::string("LOOPEND"), Format("%lld", Cut.Length())));
	}
	Comments.push_back(std::make_pair(std::string("ENCODER"), std::string("FamiTracker (beginner-friendly)")));
	std::vector<float> Kept(Samples.begin() + static_cast<std::ptrdiff_t>(Cut.Begin), Samples.begin() + static_cast<std::ptrdiff_t>(Cut.End));
	double Raised = 0.0;	// dB
	if (Normalize) {
		// FamiTracker renders at the NES's level, well under full scale: the loudest peak to -1 dBFS
		float Peak = 0.0f;
		for (size_t i = 0; i < Kept.size(); ++i)
			Peak = std::max(Peak, std::fabs(Kept[i]));
		if (Peak > 0.0001f) {
			const double Gain = std::min(std::pow(10.0, 12.0 / 20.0), std::pow(10.0, -1.0 / 20.0) / Peak);
			if (Gain > 1.0) {
				for (size_t i = 0; i < Kept.size(); ++i)
					Kept[i] = static_cast<float>(Kept[i] * Gain);
				Raised = 20.0 * std::log10(Gain);
			}
		}
	}
	if (!Cut.Loops) {
		// No click where it stops: the last 10 ms fade out
		const size_t Fade = std::min(Kept.size(), static_cast<size_t>(SampleRate / 100));
		for (size_t i = 0; i < Fade; ++i)
			Kept[Kept.size() - 1 - i] *= static_cast<float>(i) / static_cast<float>(Fade);
	}
	std::string Bytes;
	if (!EncodeOgg(Kept, SampleRate, Quality, Comments, Bytes, Error))
		throw Problem(Error);
	std::string Path;
	if (!S.Host().SaveFile(File, Bytes, Path, Error))
		throw Problem(Error);

	const double Seconds = static_cast<double>(Cut.Length()) / SampleRate;
	std::string Text = Format("%s: %s, %d KB", File.c_str(), SecondsText(Seconds).c_str(), static_cast<int>((Bytes.size() + 1023) / 1024));
	if (Raised >= 0.1)
		Text += Format(", %.1f dB louder than FamiTracker plays it (its loudest peak at -1 dBFS)", Raised);
	if (!Cut.Loops)
		Text += ", plays once (the song halts).";
	else if (Cut.LoopStart < SampleRate / 100)	// moved a moment to where the waves meet
		Text += ", loops as a whole.";
	else
		Text += Format(", the first %s once, then the loop from %s to the end.%s", SecondsText(Cut.LoopStart / static_cast<double>(SampleRate)).c_str(),
			SecondsText(Cut.LoopStart / static_cast<double>(SampleRate)).c_str(),
			Cut.SecondPass ? " The loop is the song's second time through, since the first starts differently (check_loop says why)." : "");
	Code += (Code.empty() ? "" : "\n") + GameMakerCode(GameMakerName(File), Cut, SampleRate);
	return Text;
}

std::string ExportTool(Session &S, const Json &Args)
{
	const float Quality = static_cast<float>(NumberArg(Args, "quality", 0.6, 0.0, 1.0));
	const bool Normalize = BoolArg(Args, "normalize", true);
	std::vector<int> Tracks;
	const Json *pTracks = Arg(Args, "tracks");
	if (pTracks == 0 || (pTracks->IsString() && Lowercase(Trim(pTracks->String())) == "this"))
		Tracks.push_back(S.Song.Track);
	else if (pTracks->IsString() && Lowercase(Trim(pTracks->String())) == "all") {
		for (int t = 0; t < S.Song.Tracks; ++t)
			Tracks.push_back(t);
	}
	else {
		std::vector<Json> Items;
		if (pTracks->IsArray())
			Items = pTracks->Items();
		else
			Items.push_back(*pTracks);
		for (size_t i = 0; i < Items.size(); ++i) {
			Json One = Json::MakeObject();
			One.Set("track", Items[i]);
			Tracks.push_back(TrackArg(S, One, "track"));
		}
	}
	if (S.Song.FileName.empty())
		throw Problem("The song hasn't been saved yet, and the files go next to it. Ask the user to save it (File > Save As).");
	std::string Why;
	if (!S.Host().CanChange(Why))
		throw Problem(Why);

	const int Selected = S.Song.Track;
	int Current = Selected;
	std::vector<std::string> Taken, Lines;
	std::string Code;
	std::string Error;
	for (size_t i = 0; i < Tracks.size(); ++i) {
		if (Tracks[i] != Current) {
			if (!S.Host().SelectTrack(Tracks[i], Error)) {
				Lines.push_back(Format("Track %d: %s", Tracks[i] + 1, Error.c_str()));
				continue;
			}
			Current = Tracks[i];
		}
		Session Track(S.Host());
		try {
			Lines.push_back(ExportTrack(Track, Tracks[i], Quality, Normalize, Taken, Code));
		}
		catch (const Problem &P) {
			Lines.push_back(Format("Track %d: %s", Tracks[i] + 1, P.Text.c_str()));
		}
	}
	if (Current != Selected)
		S.Host().SelectTrack(Selected, Error);

	std::string Text = Format("Saved next to %s:", S.Song.FileName.c_str());
	for (size_t i = 0; i < Lines.size(); ++i)
		Text += "\n- " + Lines[i];
	const std::string Muted = MutedChannels(S);
	if (!Muted.empty())
		Text += "\nEvery channel is in the files, the muted " + Muted + " too.";
	Text += "\n\nIn GameMaker (2023.2 or newer): add each file as a sound with the name below, then play it like this; the loop "
		"points are in seconds.\n" + Code;
	return Text;
}

// Checks

std::string CheckTool(Session &S, const Json &Args)
{
	int From, To;
	BarRange(S, Args, From, To);
	const int First = S.Time.BarStart(From), End = std::min(S.Time.BarEnd(To), S.Time.TotalRows());
	bool Found;
	const Key K = KeyArg(S, Args, "key", First, End, Found);
	std::vector<std::string> Findings;
	int Hidden = 0;
	auto Report = [&](const std::string &Text) {
		if (static_cast<int>(Findings.size()) < MAX_LISTED * 3)
			Findings.push_back(Text);
		else
			++Hidden;
	};

	// What sounds on each row, per melodic channel
	std::vector<std::vector<int> > Sound(S.ChannelCount());
	int SongFirst = -1, SongLast = -1;
	for (int c = 0; c < S.ChannelCount(); ++c) {
		const std::vector<NoteInfo> &Notes = S.Notes(c);
		for (size_t i = 0; i < Notes.size(); ++i) {
			if (SongFirst < 0 || Notes[i].Row < SongFirst)
				SongFirst = Notes[i].Row;
			SongLast = std::max(SongLast, Notes[i].Row + Notes[i].Rows);
		}
		if (!IsMelodic(S.Channel(c)))
			continue;
		Sound[c].assign(static_cast<size_t>(std::max(0, End - First)), -1);
		for (size_t i = 0; i < Notes.size(); ++i)
			for (int r = std::max(First, Notes[i].Row); r < std::min(End, Notes[i].Row + Notes[i].Rows); ++r)
				Sound[c][r - First] = Notes[i].Pitch;
	}

	// Outside the key, and outside the channel's range
	int OutOfKey = 0, OutOfRange = 0;
	for (int c = 0; c < S.ChannelCount(); ++c) {
		const ChannelInfo &Info = S.Channel(c);
		if (!IsMelodic(Info))
			continue;
		const std::vector<NoteInfo> &Notes = S.Notes(c);
		for (size_t i = 0; i < Notes.size(); ++i) {
			const NoteInfo &N = Notes[i];
			if (N.Row < First || N.Row >= End)
				continue;
			const std::string Where = Format("%s %s", Info.FullName().c_str(), S.Time.Position(N.Row).c_str());
			if (Found && !InKey(N.Pitch, K)) {
				++OutOfKey;
				Report(Format("Outside %s: %s at %s (%s).", KeyName(K).c_str(), PitchName(N.Pitch).c_str(), Where.c_str(),
					S.Time.Length(N.Rows).c_str()));
			}
			std::string Range;
			if (Info.Kind == KIND_PULSE && (Info.Chip == "2A03" || Info.Chip == "MMC5") && N.Pitch < 9)
				Range = "below A-0, the lowest note its pulse can play";
			else if (Info.Kind == KIND_TRIANGLE && N.Pitch > 72)
				Range = "above C-6, thin and shrill on the triangle";
			else if (N.Pitch > 84)
				Range = "above C-7, very high";
			if (!Range.empty()) {
				++OutOfRange;
				Report(Format("Range: %s at %s is %s.", PitchName(N.Pitch).c_str(), Where.c_str(), Range.c_str()));
			}
		}
	}

	// Clashes: two channels a semitone (or a semitone plus octaves) apart for a while
	int Clashes = 0;
	const int MinClash = std::max(1, S.Time.RowsPerBeat / 2);
	for (int a = 0; a < S.ChannelCount(); ++a) {
		for (int b = a + 1; b < S.ChannelCount(); ++b) {
			if (Sound[a].empty() || Sound[b].empty())
				continue;
			int Run = 0;
			for (int r = 0; r <= End - First; ++r) {
				const bool Clash = r < End - First && Sound[a][r] >= 0 && Sound[b][r] >= 0 && std::abs(Sound[a][r] - Sound[b][r]) % 12 == 1;
				const bool Same = Clash && Run > 0 && Sound[a][r] == Sound[a][r - 1] && Sound[b][r] == Sound[b][r - 1];
				if (Clash && (Run == 0 || Same)) {
					++Run;
					continue;
				}
				if (Run >= MinClash) {
					const int Row = First + r - Run;
					++Clashes;
					Report(Format("Clash: %s on %s against %s on %s at %s for %s.", PitchName(Sound[a][r - Run]).c_str(),
						S.Channel(a).FullName().c_str(), PitchName(Sound[b][r - Run]).c_str(), S.Channel(b).FullName().c_str(),
						S.Time.Position(Row).c_str(), S.Time.Length(Run).c_str()));
				}
				Run = Clash ? 1 : 0;
			}
		}
	}

	// Busy and empty bars
	int Busy = 0, Silent = 0;
	for (int Bar = From; Bar <= To; ++Bar) {
		const int BarFirst = S.Time.BarStart(Bar), BarEnd = std::min(S.Time.BarEnd(Bar), S.Time.TotalRows());
		if (BarFirst >= S.Time.TotalRows())
			break;
		bool Anything = false;
		for (int c = 0; c < S.ChannelCount(); ++c) {
			const std::vector<NoteInfo> &Notes = S.Notes(c);
			int Starts = 0;
			for (size_t i = 0; i < Notes.size(); ++i) {
				if (Notes[i].Row < BarEnd && Notes[i].Row + Notes[i].Rows > BarFirst)
					Anything = true;
				if (Notes[i].Row >= BarFirst && Notes[i].Row < BarEnd)
					++Starts;
			}
			if (IsMelodic(S.Channel(c)) && BarEnd - BarFirst >= 8 && Starts * 4 >= (BarEnd - BarFirst) * 3) {
				++Busy;
				Report(Format("Busy: %s plays %s in bar %d.", S.Channel(c).FullName().c_str(), Plural(Starts, "note", "notes").c_str(), Bar));
			}
		}
		if (!Anything && SongFirst >= 0 && BarFirst > SongFirst && BarFirst < SongLast) {
			++Silent;
			Report(Format("Silent: nothing plays in bar %d.", Bar));
		}
	}
	std::string Unused;
	for (int c = 0; c < S.ChannelCount(); ++c) {
		if (IsMelodic(S.Channel(c)) && S.Notes(c).empty())
			Unused += (Unused.empty() ? "" : ", ") + S.Channel(c).FullName();
	}

	std::string Text = Format("Checked %s", RangeText(From, To).c_str());
	Text += Found ? " in " + KeyName(K) + (Arg(Args, "key") != 0 ? "" : " (found from the notes)") : " (no key: no notes)";
	Text += Format(": %d outside the key, %d clashes, %d out of range, %d busy, %d silent.", OutOfKey, Clashes, OutOfRange, Busy, Silent);
	if (!Unused.empty())
		Text += " Unused channels (free for harmony, an echo or a counter-melody): " + Unused + ".";
	for (size_t i = 0; i < Findings.size(); ++i)
		Text += "\n" + Findings[i];
	if (Hidden > 0)
		Text += Format("\n... and %d more.", Hidden);
	if (Findings.empty())
		Text += "\nNothing stands out.";
	return Text;
}

// A picture of the notes

// How the song plays (jumps, skips, halts) from its cells
SongFlow Flow(Session &S)
{
	std::vector<std::vector<Cell> > Cells;
	std::vector<int> Columns;
	for (int c = 0; c < S.ChannelCount(); ++c) {
		Cells.push_back(S.Cells(c));
		Columns.push_back(S.Channel(c).EffectColumns);
	}
	return FollowSong(Cells, Columns, S.Time.PatternLength, S.Song.Frames);
}

// Each channel's color in pictures, the same in every one: melodic channels
// take turns with the first eight, the noise and DPCM have their own
int PictureColor(const Session &S, int Channel)
{
	if (S.Channel(Channel).Kind == KIND_NOISE)
		return 8;
	if (S.Channel(Channel).Kind == KIND_DPCM)
		return 9;
	int Melodic = 0;
	for (int c = 0; c < Channel; ++c)
		if (IsMelodic(S.Channel(c)))
			++Melodic;
	return Melodic % 8;
}

std::string LookTool(Session &S, const Json &Args, std::string &Image)
{
	const int Bars = S.Time.BarCount();
	const int From = IntArg(Args, "from_bar", 1, 1, Bars);
	const int To = IntArg(Args, "to_bar", std::min(Bars, From + 7), 1, Bars);
	if (To < From)
		throw Problem("to_bar comes before from_bar.");
	if (To - From + 1 > MAX_PICTURE_BARS)
		throw Problem(Format("Up to %d bars in one picture (8 to 16 are easiest to read).", MAX_PICTURE_BARS));
	const int First = S.Time.BarStart(From), End = std::min(S.Time.BarEnd(To), S.Time.TotalRows());

	std::vector<int> Wanted;
	const Json *pChannels = Arg(Args, "channels");
	if (pChannels != 0 && !(pChannels->IsString() && Lowercase(pChannels->String()) == "all")) {
		if (pChannels->IsArray()) {
			for (size_t i = 0; i < pChannels->Size(); ++i)
				Wanted.push_back(ParseChannel(S, pChannels->At(i), "channels"));
		}
		else
			Wanted.push_back(ParseChannel(S, *pChannels, "channels"));
	}
	else
		for (int c = 0; c < S.ChannelCount(); ++c)
			Wanted.push_back(c);
	std::sort(Wanted.begin(), Wanted.end());
	Wanted.erase(std::unique(Wanted.begin(), Wanted.end()), Wanted.end());

	std::vector<PictureChannel> Channels;
	std::vector<std::string> Shown, Silent;
	bool Chords = false;
	std::string Strips;
	for (size_t w = 0; w < Wanted.size(); ++w) {
		const int c = Wanted[w];
		const ChannelInfo &Info = S.Channel(c);
		PictureChannel Channel;
		Channel.Color = PictureColor(S, c);
		Channel.Kind = Info.Kind;
		Channel.Notes = S.Notes(c);
		bool Sounds = false;
		for (size_t i = 0; i < Channel.Notes.size(); ++i) {
			const NoteInfo &Note = Channel.Notes[i];
			if (Note.Row < End && Note.Row + Note.Rows > First) {
				Sounds = true;
				Chords = Chords || SoundsChord(S, Note);
			}
		}
		if (!Sounds) {
			Silent.push_back(Info.FullName());
			continue;
		}
		if (Info.Kind == KIND_NOISE)
			Strips += Format(" The DRUMS strip below is %s (%s): a solid block is a hit, higher is brighter noise "
				"(kick low, hi-hat high), the faint bar after it lasts until the next hit.", Info.FullName().c_str(),
				PictureColorName(Channel.Color));
		else if (Info.Kind == KIND_DPCM)
			Strips += Format(" The DPCM strip is %s: each block is a sample starting.", Info.FullName().c_str());
		else
			Shown.push_back(Format("%s = %s", PictureColorName(Channel.Color), Info.FullName().c_str()));
		Channels.push_back(Channel);
	}

	PictureSpec Spec(S.Time);
	Spec.First = First;
	Spec.End = End;
	const SongFlow Played = Flow(S);
	Spec.Played = Played.Played(S.Time.TotalRows(), S.Time.PatternLength);
	Spec.LoopRow = Played.LoopRow(S.Time.PatternLength);
	Spec.EndRow = Played.EndRow(S.Time.PatternLength);
	const Picture P = DrawNotes(Channels, Spec);
	Image = P.Png();

	std::string Text = Format("Piano roll of %s: time runs left to right, bars numbered on top (thin lines are beats, "
		"dark double lines start a frame), pitch runs bottom to top with each C labeled (%s to %s shown; C-3 is middle C).",
		RangeText(From, To).c_str(), PitchName(P.LowPitch).c_str(), PitchName(P.HighPitch).c_str());
	if (!Shown.empty()) {
		Text += " Colors: ";
		for (size_t i = 0; i < Shown.size(); ++i)
			Text += (i > 0 ? ", " : "") + Shown[i];
		Text += ".";
	}
	Text += Strips;
	Text += " A note lasts until the next one, a release or a halt in its channel; paler notes are quieter.";
	if (Chords)
		Text += " Thin faint bars above a note are the other notes of its arpeggio chord (0xy).";
	const int LoopRow = Spec.LoopRow, EndRow = Spec.EndRow;
	if (Played.Halts())
		Text += Format(" The song stops after %s (red line).", S.Time.Position(EndRow - 1).c_str());
	else if (LoopRow > 0 || EndRow < S.Time.TotalRows())
		Text += Format(" Green line: where the song loops back to (%s); red line: where it jumps back, after %s.",
			S.Time.Position(LoopRow).c_str(), S.Time.Position(EndRow - 1).c_str());
	else
		Text += " The song loops from its end back to bar 1 (green and red lines).";
	bool Unplayed = false;
	for (int r = First; r < End && !Unplayed; ++r)
		Unplayed = !Spec.Played[static_cast<size_t>(r)];
	if (Unplayed)
		Text += " Hatched rows never play (skipped by a jump, skip or halt).";
	if (!Silent.empty()) {
		Text += " Nothing here on: ";
		for (size_t i = 0; i < Silent.size(); ++i)
			Text += (i > 0 ? ", " : "") + Silent[i];
		Text += ".";
	}
	Key K;
	if (FindKey(S, First, End, K)) {
		const std::vector<ChordAt> Heard = SongChords(S, First, End, K);
		if (!Heard.empty())
			Text += " Chords heard (" + KeyName(K) + "): " + ProgressionText(Heard, SongBarSpans(S, First, End), K) + ".";
	}
	if (To < Bars)
		Text += Format(" The song has %d bars.", Bars);
	return Text;
}

// "Pulse 1, Triangle and Noise"
std::string ChannelNames(const Session &S, const std::vector<int> &Channels)
{
	std::string Text;
	for (size_t i = 0; i < Channels.size(); ++i)
		Text += (i == 0 ? "" : i + 1 == Channels.size() ? " and " : ", ") + S.Channel(Channels[i]).FullName();
	return Text;
}

std::string PlayTool(Session &S, const Json &Args)
{
	const int From = IntArg(Args, "from_bar", 1, 1, std::max(1, S.Time.BarCount()));
	const int Bars = IntArg(Args, "bars", 0, 0, S.MaxBars());
	PlayRequest Request;
	Request.First = S.Time.BarStart(From);
	Request.Loop = BoolArg(Args, "loop", false);
	Request.Rows = Bars > 0 || Request.Loop ? std::min(S.Time.TotalRows(), Bars > 0 ? S.Time.BarEnd(From + Bars - 1) : S.Time.TotalRows()) -
		Request.First : 0;
	// Channels on their own, or left out, this time
	std::vector<int> Playing;
	if (Arg(Args, "channels") != 0 || Arg(Args, "mute") != 0) {
		Request.Muted.assign(static_cast<size_t>(S.ChannelCount()), Arg(Args, "channels") != 0);
		if (Arg(Args, "channels") != 0) {
			const std::vector<int> Solo = ChannelsArg(S, Args, "channels");
			for (size_t i = 0; i < Solo.size(); ++i)
				Request.Muted[static_cast<size_t>(Solo[i])] = false;
		}
		if (Arg(Args, "mute") != 0) {
			const std::vector<int> Mute = ChannelsArg(S, Args, "mute");
			for (size_t i = 0; i < Mute.size(); ++i)
				Request.Muted[static_cast<size_t>(Mute[i])] = true;
		}
		for (int c = 0; c < S.ChannelCount(); ++c)
			if (!Request.Muted[static_cast<size_t>(c)])
				Playing.push_back(c);
		if (Playing.empty())
			throw Problem("That leaves no channel playing.");
	}
	std::string Error;
	if (!S.Host().Play(Request, Error))
		throw Problem(Error.empty() ? "FamiTracker couldn't play." : Error);
	const std::string What = Bars > 0 ? RangeText(From, From + Bars - 1) : Format("bar %d to the end", From);
	std::string Text = Request.Loop ? Format("Playing %s for the user over and over, until you call stop or the user stops it.",
		What.c_str()) : Format("Playing for the user from bar %d%s.", From,
		Bars > 0 ? Format(" for %s", Plural(Bars, "bar", "bars").c_str()).c_str() : " to the end");
	if (!Request.Muted.empty())
		Text += Format(" Only %s %s; the user's own mutes come back when it stops.", ChannelNames(S, Playing).c_str(),
			Playing.size() == 1 ? "plays" : "play");
	Text += " You can't hear it yourself; ask the user how it sounds.";
	const std::string Muted = MutedChannels(S);
	if (!Muted.empty() && Request.Muted.empty())
		Text += " The user has muted " + Muted + " in FamiTracker, so that doesn't play; clicking a channel's name "
			"above its column switches it back on.";
	return Text;
}

std::string UndoTool(Session &S, const Json &)
{
	if (!S.Song.DirectChanges)
		throw Problem("Undo changes the song, and the user hasn't allowed direct changes; your ideas are clips the user places "
			"themselves (and takes out with Ctrl+Z).");
	std::string Message;
	if (!S.Host().Undo(Message))
		throw Problem(Message.empty() ? "There's nothing to undo." : Message);
	return Message.empty() ? "Undid the last change." : Message;
}

std::string SaveTool(Session &S, const Json &Args)
{
	const std::string Copy = StringArg(Args, "copy_name");
	if (!Copy.empty() && Copy.find_first_of("\\/:*?\"<>|") != std::string::npos)
		throw Problem("copy_name should be a plain file name, without folders.");
	std::string Message;
	if (!S.Host().Save(Copy, Message))
		throw Problem(Message.empty() ? "The song couldn't be saved." : Message);
	return Message;
}

// The reference library: tracks studied from the user's NSF files, to lean on

const int MAX_STUDIED = 24;			// tracks a study_nsf call studies; each takes a moment
const char *const REFERENCE_EXTENSION = ".json";

// Today, for when a reference was studied: "2026-10-21"
std::string Today()
{
	const std::time_t Now = std::time(0);
	std::tm Local;
#ifdef _MSC_VER
	if (localtime_s(&Local, &Now) != 0)
		return std::string();
#else
	if (localtime_r(&Now, &Local) == 0)
		return std::string();
#endif
	char Buffer[16];
	std::strftime(Buffer, sizeof(Buffer), "%Y-%m-%d", &Local);
	return Buffer;
}

// An NSF named in a tool's arguments, read with its playlist
nsf::NsfFile ReadNsfArg(Session &S, const Json &Args, std::string &Path)
{
	Path = WindowsPath(StringArg(Args, "path"));
	if (Path.empty())
		throw Problem("Give the path of the NSF file (\"path\"), as Windows writes it: C:\\Users\\Name\\Music\\game.nsf.");
	nsf::NsfFile File;
	std::string Error;
	if (!nsf::ReadNsfFile(S.Host().LocalPath(Path), File, Error))
		throw Problem("The NSF " + Path + " couldn't be read: " + Error + ". Give the path of an NSF or NSFe file on the user's "
			"computer as Windows writes it, like C:\\Users\\Name\\Music\\game.nsf.");
	return File;
}

// The tracks a tool's "tracks" asks for (from 0): "music" (the default), "all", or "1,3-5"
std::vector<int> NsfTracksArg(const nsf::NsfFile &File, const Json &Args, std::vector<nsf::TrackSurvey> *pSurvey)
{
	const std::string Asked = Lowercase(StringArg(Args, "tracks", "music"));
	std::vector<int> Tracks;
	if (Asked.empty() || Asked == "music") {
		const std::vector<nsf::TrackSurvey> Survey = nsf::SurveyTracks(File);
		for (size_t i = 0; i < Survey.size(); ++i)
			if (Survey[i].Kind == "music")
				Tracks.push_back(Survey[i].Track);
		if (pSurvey != 0)
			*pSurvey = Survey;
		if (Tracks.empty())
			throw Problem("None of the NSF's tracks sounds like music (they're short jingles or sound effects). Name the tracks to "
				"use with \"tracks\", like \"1,3-5\".");
		return Tracks;
	}
	std::string Error;
	if (!nsf::ParseTrackList(Asked, File.Songs, Tracks, Error))
		throw Problem("tracks: " + Error + ". Give \"music\", \"all\" or numbers from 1, like \"1,3-5\".");
	return Tracks;
}

// The library's references, read from their files; Broken counts the files that aren't references
std::vector<Reference> ReadLibrary(Session &S, int *pBroken = 0)
{
	std::vector<std::string> Names;
	std::string Error;
	if (!S.Host().ListLibrary(Names, Error))
		throw Problem(Error.empty() ? "The reference library couldn't be read." : Error);
	std::sort(Names.begin(), Names.end());
	std::vector<Reference> Out;
	int Broken = 0;
	for (size_t i = 0; i < Names.size(); ++i) {
		const std::string &Name = Names[i];
		if (Name.size() <= std::strlen(REFERENCE_EXTENSION) ||
			Lowercase(Name.substr(Name.size() - std::strlen(REFERENCE_EXTENSION))) != REFERENCE_EXTENSION)
			continue;
		std::string Bytes, Problem2;
		Reference R;
		if (!S.Host().ReadLibrary(Name, Bytes, Problem2) || !ReadReferenceFile(Bytes, R, Problem2)) {
			++Broken;
			continue;
		}
		R.Id = Name.substr(0, Name.size() - std::strlen(REFERENCE_EXTENSION));	// the file's name is what finds it
		Out.push_back(R);
	}
	if (pBroken != 0)
		*pBroken = Broken;
	return Out;
}

Reference FindReference(Session &S, const std::string &Id)
{
	if (Id.empty())
		throw Problem("Give the reference's id (\"reference\"), as references lists them.");
	const std::vector<Reference> All = ReadLibrary(S);
	for (size_t i = 0; i < All.size(); ++i)
		if (Lowercase(All[i].Id) == Lowercase(Id))
			return All[i];
	// Close enough: a unique one whose id or title holds the words
	std::vector<size_t> Near;
	for (size_t i = 0; i < All.size(); ++i)
		if (Lowercase(All[i].Id).find(Lowercase(Id)) != std::string::npos || Lowercase(All[i].Title) == Lowercase(Id))
			Near.push_back(i);
	if (Near.size() == 1)
		return All[Near[0]];
	throw Problem("There's no reference \"" + Id + "\" in the library" + (All.empty() ? std::string(" (it's empty: study_nsf adds "
		"references from NSF files)") : ". references lists them") + ".");
}

void SaveReference(Session &S, const Reference &R, std::string &Path)
{
	std::string Error;
	if (!S.Host().SaveLibrary(R.Id + REFERENCE_EXTENSION, ReferenceFile(R), Path, Error))
		throw Problem("The reference couldn't be saved: " + (Error.empty() ? std::string("FamiTracker refused.") : Error));
}

std::vector<std::string> TagsArg(const Json &Args, const char *Name)
{
	std::vector<std::string> Tags;
	const Json *pValue = Arg(Args, Name);
	if (pValue == 0)
		return Tags;
	std::vector<std::string> Raw;
	if (pValue->IsArray()) {
		for (size_t i = 0; i < pValue->Size(); ++i)
			if (pValue->At(i).IsString())
				Raw.push_back(pValue->At(i).String());
	}
	else if (pValue->IsString()) {
		std::string Part;
		const std::string &Text = pValue->String();
		for (size_t i = 0; i <= Text.size(); ++i) {
			if (i == Text.size() || Text[i] == ',') {
				Raw.push_back(Part);
				Part.clear();
			}
			else
				Part += Text[i];
		}
	}
	else
		throw Problem(Format("%s should be a list of words.", Name));
	for (size_t i = 0; i < Raw.size(); ++i) {
		const std::string Tag = Lowercase(Trim(Raw[i]));
		if (!Tag.empty() && std::find(Tags.begin(), Tags.end(), Tag) == Tags.end())
			Tags.push_back(Tag);
	}
	return Tags;
}

std::string StudyNsfTool(Session &S, const Json &Args)
{
	std::string Path;
	const nsf::NsfFile File = ReadNsfArg(S, Args, Path);
	if (Lowercase(StringArg(Args, "tracks")) == "list") {
		const std::vector<nsf::TrackSurvey> Survey = nsf::SurveyTracks(File);
		std::string Out = nsf::DescribeNsf(File) + "\nWhat the tracks are:\n";
		for (size_t i = 0; i < Survey.size(); ++i) {
			const nsf::TrackSurvey &t = Survey[i];
			Out += Format("  %d. %s: %s", t.Track + 1, ValidUtf8(t.Title).c_str(), t.Kind.c_str());
			if (t.Seconds > 0)
				Out += Format(", %s s", FormatNumber(static_cast<int>(t.Seconds * 10 + 0.5) / 10.0).c_str());
			if (t.LoopSeconds > 0)
				Out += Format(" (a loop of %s s)", FormatNumber(static_cast<int>(t.LoopSeconds * 10 + 0.5) / 10.0).c_str());
			Out += "\n";
		}
		return ValidUtf8(Out) + "Study the music with tracks \"music\", or name the tracks.";
	}
	std::vector<int> Tracks = NsfTracksArg(File, Args, 0);
	std::string Left;
	if (static_cast<int>(Tracks.size()) > MAX_STUDIED) {
		Left = Format(" %d more music tracks weren't studied this time: name them with tracks (\"%d-%d\").",
			static_cast<int>(Tracks.size()) - MAX_STUDIED, Tracks[MAX_STUDIED] + 1, Tracks.back() + 1);
		Tracks.resize(MAX_STUDIED);
	}
	nsf::StudyOptions Options;
	std::vector<nsf::StudiedTrack> Studied;
	std::string Error;
	if (!nsf::StudyTracks(File, Tracks, Options, Studied, Error))
		throw Problem("The study stopped: " + Error);

	const bool Keep = BoolArg(Args, "save", true);
	const std::vector<std::string> Tags = TagsArg(Args, "tags");
	const std::string Notes = StringArg(Args, "notes");
	const std::string Date = Today();
	std::vector<Reference> Made;
	std::vector<std::string> Problems;
	std::string Folder;
	for (size_t i = 0; i < Studied.size(); ++i) {
		if (!Studied[i].Problem.empty()) {
			Problems.push_back(ValidUtf8(Studied[i].Problem));
			continue;
		}
		Reference R = MakeReference(File, Studied[i].Track, Studied[i].Study, Path, Date);
		if (Keep) {
			// Studied again: what the user tagged and noted stays
			std::string Old, Why;
			Reference Before;
			if (S.Host().ReadLibrary(R.Id + REFERENCE_EXTENSION, Old, Why) && ReadReferenceFile(Old, Before, Why)) {
				for (size_t t = 0; t < Before.Tags.size(); ++t)
					if (std::find(R.Tags.begin(), R.Tags.end(), Before.Tags[t]) == R.Tags.end())
						R.Tags.push_back(Before.Tags[t]);
				R.Notes = Before.Notes;
			}
			for (size_t t = 0; t < Tags.size(); ++t)
				if (std::find(R.Tags.begin(), R.Tags.end(), Tags[t]) == R.Tags.end())
					R.Tags.push_back(Tags[t]);
			if (!Notes.empty())
				R.Notes = R.Notes.empty() ? Notes : R.Notes + " " + Notes;
			std::string Written;
			SaveReference(S, R, Written);
			if (Folder.empty()) {
				const size_t Slash = Written.find_last_of("\\/");
				Folder = Slash == std::string::npos ? Written : Written.substr(0, Slash);
			}
		}
		Made.push_back(R);
	}
	if (Made.empty())
		throw Problem("No track could be studied: " + (Problems.empty() ? std::string("they make no sound.") : Problems[0]));

	std::string Out;
	if (Made.size() <= 3) {
		for (size_t i = 0; i < Made.size(); ++i)
			Out += (i ? "\n" : "") + ReferenceText(Made[i]);
	}
	else {
		Out = Format("Studied %d tracks of %s:\n", static_cast<int>(Made.size()), File.Title.empty() ? "the NSF" :
			("\"" + ValidUtf8(File.Title) + "\"").c_str());
		for (size_t i = 0; i < Made.size(); ++i)
			Out += "- " + ReferenceLine(Made[i]) + "\n";
		Out += "references with action \"show\" and an id gives a whole study, with its sounds.";
	}
	if (!Problems.empty())
	{
		Out += "\nNot studied:";
		for (size_t i = 0; i < Problems.size(); ++i)
			Out += (i ? "; " : " ") + Problems[i];
		Out += ".";
	}
	if (Keep)
		Out += "\nKept in the reference library" + (Folder.empty() ? std::string() : " (" + Folder + ")") + ", for every song: "
			"references finds them again.";
	else
		Out += "\nNot kept (save was false).";
	return Out + Left;
}

// A FamiTracker module (.ftm) studied a track at a time from its own
// patterns and instruments, each kept in the library with its sounds
std::string StudyFtm(Session &S, const Json &Args, const std::string &Path, const nsf::Module &M, const nsf::FtmInfo &Info)
{
	std::vector<int> Tracks;
	std::string Error;
	if (!nsf::ParseTrackList(StringArg(Args, "tracks"), static_cast<int>(M.Tracks.size()), Tracks, Error))
		throw Problem("tracks: " + Error);
	std::string Left;
	if (static_cast<int>(Tracks.size()) > MAX_STUDIED) {
		Left = Format(" %d more tracks weren't studied this time: name them with tracks (\"%d-%d\").",
			static_cast<int>(Tracks.size()) - MAX_STUDIED, Tracks[MAX_STUDIED] + 1, Tracks.back() + 1);
		Tracks.resize(MAX_STUDIED);
	}
	const bool Keep = BoolArg(Args, "save", true);
	const std::vector<std::string> Tags = TagsArg(Args, "tags");
	const std::string Notes = StringArg(Args, "notes");
	const std::string Date = Today();
	std::vector<Reference> Made;
	std::string Folder;
	for (size_t i = 0; i < Tracks.size(); ++i) {
		const nsf::TrackStudy Study = nsf::StudyTrack(M, Tracks[i], nsf::FtmTrackReport(M, Info, Tracks[i]));
		Reference R = MakeFtmReference(M, Tracks[i], Study, Path, Date);
		if (Keep) {
			// Studied again: what the user tagged and noted stays
			std::string Old, Why;
			Reference Before;
			if (S.Host().ReadLibrary(R.Id + REFERENCE_EXTENSION, Old, Why) && ReadReferenceFile(Old, Before, Why)) {
				for (size_t t = 0; t < Before.Tags.size(); ++t)
					if (std::find(R.Tags.begin(), R.Tags.end(), Before.Tags[t]) == R.Tags.end())
						R.Tags.push_back(Before.Tags[t]);
				R.Notes = Before.Notes;
			}
			for (size_t t = 0; t < Tags.size(); ++t)
				if (std::find(R.Tags.begin(), R.Tags.end(), Tags[t]) == R.Tags.end())
					R.Tags.push_back(Tags[t]);
			if (!Notes.empty())
				R.Notes = R.Notes.empty() ? Notes : R.Notes + " " + Notes;
			std::string Written;
			SaveReference(S, R, Written);
			if (Folder.empty()) {
				const size_t Slash = Written.find_last_of("\\/");
				Folder = Slash == std::string::npos ? Written : Written.substr(0, Slash);
			}
		}
		else if (!Notes.empty())
			R.Notes = Notes;
		Made.push_back(R);
	}
	if (Made.empty())
		throw Problem("The module has no tracks to study.");
	std::string Out = Format("FamiTracker module %s%s, %s:\n", M.Title.empty() ? "" : ("\"" + ValidUtf8(M.Title) + "\"").c_str(),
		M.Author.empty() ? "" : (" by " + ValidUtf8(M.Author)).c_str(), Plural(static_cast<int>(M.Tracks.size()), "track", "tracks").c_str());
	if (Made.size() <= 3) {
		for (size_t i = 0; i < Made.size(); ++i)
			Out += (i ? "\n" : "") + ReferenceText(Made[i]);
	}
	else {
		for (size_t i = 0; i < Made.size(); ++i)
			Out += "- " + ReferenceLine(Made[i]) + "\n";
		Out += "references with action \"show\" and an id gives a whole study, with its sounds.";
	}
	if (!Info.Problems.empty()) {
		Out += "\nRead with remarks:";
		for (size_t i = 0; i < Info.Problems.size(); ++i)
			Out += (i ? "; " : " ") + ValidUtf8(Info.Problems[i]);
		Out += ".";
	}
	Out += Keep ? "\nKept in the reference library" + (Folder.empty() ? std::string() : " (" + Folder + ")") + ": references finds "
		"them again, and reference_sound gives a song their sounds." : std::string("\nNot kept (save was false).");
	Out += "\nTo write in its style: take its craft (the tempo and swing, how the channels share the parts, the echoes, the chords "
		"as arpeggio instruments, the mix's volumes and the sounds' shapes), not its melodies or its exact progressions.";
	return Out + Left;
}

// A tracker module (MOD, S3M, XM, IT) studied from its pattern data, a MIDI
// file from its notes, or a FamiTracker module a track at a time, kept in the library
std::string StudyModuleTool(Session &S, const Json &Args)
{
	const std::string Path = WindowsPath(StringArg(Args, "path"));
	if (Path.empty())
		throw Problem("Give the path of the module (\"path\"): a MOD, S3M, XM or IT file, a MIDI file, or a FamiTracker module "
			"(.ftm), on the user's computer, as Windows writes it, like C:\\Users\\Name\\Music\\song.xm or song.ftm.");
	std::string Error;
	{
		// A FamiTracker module of its own: by its name, else by what it starts with
		const std::string Local = S.Host().LocalPath(Path);
		nsf::Module M;
		nsf::FtmInfo Info;
		const bool Named = Lowercase(Path).size() > 4 && Lowercase(Path).substr(Lowercase(Path).size() - 4) == ".ftm";
		if (nsf::ReadFtmFile(Local, M, Info, Error))
			return StudyFtm(S, Args, Path, M, Info);
		if (Named)
			throw Problem("The FamiTracker module " + Path + " couldn't be read: " + Error + ".");
	}
	nsf::TrackerSong Song;
	if (!nsf::ReadTrackerFile(S.Host().LocalPath(Path), Song, Error))
		throw Problem("The file " + Path + " couldn't be read: " + Error + ". Give the path of a MOD, S3M, XM or IT file, or a "
			"MIDI file, as Windows writes it.");
	const std::string What = Song.Format == "MIDI" ? "MIDI file" : "module";
	if (Song.Title.empty()) {
		// Untitled: named after its file
		Song.Title = Path.substr(Path.find_last_of("\\/") == std::string::npos ? 0 : Path.find_last_of("\\/") + 1);
		if (Song.Title.find('.') != std::string::npos)
			Song.Title = Song.Title.substr(0, Song.Title.find_last_of('.'));
	}
	nsf::Module M;
	nsf::TrackReport Report;
	if (!nsf::TrackerToModule(Song, M, Report, Error))
		throw Problem("The " + What + " " + Path + " couldn't be studied: " + Error + ".");
	const nsf::TrackStudy Study = nsf::StudyTrack(M, 0, Report);
	Reference R = MakeModuleReference(Song, Study, Path, Today());
	const bool Keep = BoolArg(Args, "save", true);
	const std::vector<std::string> Tags = TagsArg(Args, "tags");
	const std::string Notes = StringArg(Args, "notes");
	std::string Written;
	if (Keep) {
		// Studied again: what the user tagged and noted stays
		std::string Old, Why;
		Reference Before;
		if (S.Host().ReadLibrary(R.Id + REFERENCE_EXTENSION, Old, Why) && ReadReferenceFile(Old, Before, Why)) {
			for (size_t t = 0; t < Before.Tags.size(); ++t)
				if (std::find(R.Tags.begin(), R.Tags.end(), Before.Tags[t]) == R.Tags.end())
					R.Tags.push_back(Before.Tags[t]);
			R.Notes = Before.Notes;
		}
		for (size_t t = 0; t < Tags.size(); ++t)
			if (std::find(R.Tags.begin(), R.Tags.end(), Tags[t]) == R.Tags.end())
				R.Tags.push_back(Tags[t]);
		if (!Notes.empty())
			R.Notes = R.Notes.empty() ? Notes : R.Notes + " " + Notes;
		SaveReference(S, R, Written);
	}
	else if (!Notes.empty())
		R.Notes = Notes;
	std::string Out = ValidUtf8(nsf::DescribeTracker(Song)) + "\n" + ReferenceText(R);
	Out += Keep ? "Kept in the reference library" + (Written.empty() ? std::string() : " (" + Written + ")") + ": references finds "
		"it again." : std::string("Not kept (save was false).");
	Out += "\nTo write in its style for the NES: keep its tempo and feel, form, chord movement, rhythms and groove, and boil its "
		"channels down to the NES's: its lead on a pulse (or the VRC6 sawtooth for a big lead), its harmony, pads or chords on "
		"the other pulse (held, or as 0xy arpeggios), its bass on the triangle, its drums on the noise channel with a sampled "
		"kick and snare on the DPCM (add_drums with kit samples). Write melodies and progressions of your own: the " + What +
		" is for its craft, not its tunes.";
	return Out;
}

// effects: FamiTracker 0.4.6's effects and the techniques made with them
std::string EffectsTool(Session &S, const Json &Args)
{
	const ChannelInfo *pChannel = 0;
	if (Arg(Args, "channel") != 0)
		pChannel = &S.Channel(ChannelArg(S, Args, "channel"));
	std::string Text;
	if (!EffectsText(StringArg(Args, "effect"), StringArg(Args, "technique"), pChannel, Text))
		throw Problem(Text);
	return Text;
}

std::string ReferencesTool(Session &S, const Json &Args)
{
	const std::string Action = Lowercase(StringArg(Args, "action", "list"));
	if (Action == "guide")
		return StyleGuide(StringArg(Args, "topic"));
	if (Action == "list") {
		int Broken = 0;
		const std::vector<Reference> All = ReadLibrary(S, &Broken);
		std::string Out;
		if (All.empty())
			Out = "The reference library is empty. study_nsf studies tracks of NSF files the user has and keeps them here; "
				"study_module studies their MOD, S3M, XM and IT modules and their MIDI files.";
		else {
			Out = Format("%s in the library:\n", Plural(static_cast<int>(All.size()), "reference", "references").c_str());
			std::string Game = "\x01";
			for (size_t i = 0; i < All.size(); ++i) {
				if (All[i].Game != Game) {
					Game = All[i].Game;
					Out += (Game.empty() ? std::string("Modules and others") : Game) + (All[i].Composer.empty() ? std::string() : " (" +
					All[i].Composer + ")") + ":\n";
				}
				Out += "- " + ReferenceLine(All[i]) + "\n";
			}
			Out += "show gives one's whole study; search finds them by words (a game, a technique, a mood, a tempo).";
		}
		if (Broken > 0)
			Out += Format("\n%s in the library folder couldn't be read.", Plural(Broken, "file", "files").c_str());
		return Out + "\nThe built-in guide to the classic NES styles: action \"guide\".";
	}
	if (Action == "show")
		return ReferenceText(FindReference(S, StringArg(Args, "reference")));
	if (Action == "search") {
		const std::string Query = StringArg(Args, "query");
		if (Query.empty())
			throw Problem("Give the words to look for (\"query\"): a game, a composer, a technique (echo, gallop, duty click, "
				"triangle thump), a mood or a tempo.");
		const std::vector<Reference> All = ReadLibrary(S);
		std::vector<std::pair<int, size_t> > Found;
		for (size_t i = 0; i < All.size(); ++i) {
			const int Score = ReferenceMatch(All[i], Query);
			if (Score > 0)
				Found.push_back(std::make_pair(-Score, i));
		}
		std::sort(Found.begin(), Found.end());
		std::string Out;
		if (Found.empty())
			Out = All.empty() ? "The reference library is empty (study_nsf and study_module add to it)." :
				"No reference matches \"" + Query + "\".";
		else {
			Out = Format("References for \"%s\", best first:\n", Query.c_str());
			for (size_t i = 0; i < Found.size() && i < static_cast<size_t>(MAX_LISTED); ++i)
				Out += "- " + ReferenceLine(All[Found[i].second]) + "\n";
			if (Found.size() > static_cast<size_t>(MAX_LISTED))
				Out += Format("and %d more.\n", static_cast<int>(Found.size()) - MAX_LISTED);
		}
		const std::string Guide = StyleGuide(Query);
		if (Guide.find("isn't in the guide") == std::string::npos)
			Out += "\nFrom the built-in guide:\n" + Guide;
		return Out;
	}
	if (Action == "tag") {
		Reference R = FindReference(S, StringArg(Args, "reference"));
		const std::vector<std::string> Add = TagsArg(Args, "tags"), Remove = TagsArg(Args, "remove_tags");
		for (size_t i = 0; i < Add.size(); ++i)
			if (std::find(R.Tags.begin(), R.Tags.end(), Add[i]) == R.Tags.end())
				R.Tags.push_back(Add[i]);
		for (size_t i = 0; i < Remove.size(); ++i)
			R.Tags.erase(std::remove(R.Tags.begin(), R.Tags.end(), Remove[i]), R.Tags.end());
		if (Arg(Args, "notes") != 0)
			R.Notes = StringArg(Args, "notes");
		std::string Path;
		SaveReference(S, R, Path);
		return "Updated " + ReferenceLine(R) + (R.Notes.empty() ? std::string() : "\nNotes: " + R.Notes);
	}
	if (Action == "delete") {
		const Reference R = FindReference(S, StringArg(Args, "reference"));
		std::string Error;
		if (!S.Host().DeleteLibrary(R.Id + REFERENCE_EXTENSION, Error))
			throw Problem("The reference couldn't be deleted: " + (Error.empty() ? std::string("FamiTracker refused.") : Error));
		return "Deleted the reference " + R.Id + " (" + R.Title + ").";
	}
	throw Problem("action: list, show, search, guide, tag or delete.");
}

std::string ReferenceSoundTool(Session &S, const Json &Args)
{
	const Reference R = FindReference(S, StringArg(Args, "reference"));
	if (R.Sounds.empty() && std::find(R.Tags.begin(), R.Tags.end(), "tracker-module") != R.Tags.end())
		throw Problem("The reference " + R.Id + " is a tracker module's: its sounds are samples, which the NES's chips can't play. "
			"design_instrument makes sounds in their spirit (its study names its instruments and what they play).");
	if (R.Sounds.empty() && std::find(R.Tags.begin(), R.Tags.end(), "midi") != R.Tags.end())
		throw Problem("The reference " + R.Id + " is a MIDI file's: its sounds are General MIDI's instruments, which the NES's "
			"chips can't play. design_instrument makes sounds in their spirit (its study names its instruments and what they "
			"play).");
	if (R.Sounds.empty())
		throw Problem("The reference " + R.Id + " has no sounds to use.");
	const int Number = IntArg(Args, "sound", 1, 1, static_cast<int>(R.Sounds.size()));
	const ReferenceSound &Sound = R.Sounds[static_cast<size_t>(Number - 1)];
	const Json *pChannel = Arg(Args, "channel");
	std::string Chip = "2A03";
	bool Sawtooth = false;
	std::string For;
	if (pChannel != 0) {
		const int Channel = ParseChannel(S, *pChannel, "channel");
		const ChannelInfo &Info = S.Channel(Channel);
		Chip = InstrumentChip(Info);
		Sawtooth = Info.Kind == KIND_SAWTOOTH;
		For = " for " + Info.FullName();
		if (Chip != "2A03" && Chip != "VRC6")
			throw Problem("A reference's sound can be an instrument for 2A03, MMC5 and VRC6 channels, not " + Info.FullName() + ".");
		if (Sound.Noise != (Info.Kind == KIND_NOISE))
			throw Problem(Sound.Noise ? "That sound is a drum: give the noise channel." : "That sound is melodic: give a channel "
				"other than the noise.");
	}
	else if (Sound.Chip == "VRC6" && S.Song.Expansion == "VRC6")
		Chip = "VRC6";
	std::string Name = StringArg(Args, "name");
	if (Name.empty())
		Name = ReferenceSoundName(R, Sound);
	const Preset Made = ReferencePreset(Sound, Name);
	std::string Error;
	const int Index = S.Host().AddInstrument(Made, Chip, Sawtooth, Error);
	if (Index < 0)
		throw Problem("The instrument couldn't be added: " + (Error.empty() ? std::string("FamiTracker refused.") : Error));
	S.Refresh();
	std::string Out = Format("Added %s, a %s instrument%s: sound %d of %s (%s, %s): %s.", InstrumentLabel(S.Song, Index).c_str(),
		Chip.c_str(), For.c_str(), Number, R.Id.c_str(), Sound.Channel.c_str(), Sound.Role.c_str(), Sound.Words.c_str());
	if (Sound.Chip != Chip && !Sound.Noise && !Sound.Sequences[SEQUENCE_DUTY].Items.empty())
		Out += " Its duty was translated from the " + Sound.Chip + "'s.";
	if (Sound.Triangle && !Sawtooth && pChannel != 0 && S.Channel(ParseChannel(S, *pChannel, "channel")).Kind != KIND_TRIANGLE)
		Out += " It was a triangle sound: on a pulse it plays with the pulse's duty.";
	return Out + " Use it with write_notes or set_instrument.";
}

std::string AddSampleDrumsTool(Session &S, const Json &)
{
	const DpcmKit &Kit = SampleDrums();
	const size_t Before = S.Song.Instruments.size();
	const int Index = SampleDrumsInstrument(S);
	S.Remarks.clear();
	int Bytes = 0;
	for (size_t i = 0; i < Kit.Samples.size(); ++i)
		Bytes += static_cast<int>(Kit.Samples[i].Data.size());
	std::string Keys;
	for (size_t i = 0; i < Kit.Keys.size(); ++i) {
		Cell At;
		At.SetPitch(Kit.Keys[i].Pitch);
		Keys += (i == 0 ? "" : (i + 1 == Kit.Keys.size() ? " and " : ", ")) + Kit.Keys[i].Drum + " on " + PitchName(At.Pitch());
	}
	int Dpcm = -1;
	for (int c = 0; c < S.ChannelCount() && Dpcm < 0; ++c)
		if (S.Channel(c).Kind == KIND_DPCM)
			Dpcm = c;
	return Format("%s %s: %s (%s of samples). Play them with write_notes on %s, by name: {\"pitch\": \"kick\"}, "
		"\"snare\", \"low tom\", \"high tom\"; each hit rings until the next. Keep the hi-hats on the noise channel, and "
		"keep the samples to the kick and snare (and toms in fills): that's the punch of the classic soundtracks.",
		S.Song.Instruments.size() > Before ? "Added" : "The song already has", InstrumentLabel(S.Song, Index).c_str(), Keys.c_str(),
		Bytes >= 1024 ? Format("%.1f KB", Bytes / 1024.0).c_str() : Format("%d bytes", Bytes).c_str(),
		Dpcm >= 0 ? S.Channel(Dpcm).FullName().c_str() : "the DPCM channel");
}

std::string ImportNsfTool(Session &S, const Json &Args)
{
	if (S.Song.Modified)
		throw Problem("The open song has changes that aren't saved, and importing replaces it. Save it first (save_song), or "
			"ask the user.");
	std::string Path;
	const nsf::NsfFile File = ReadNsfArg(S, Args, Path);
	std::vector<int> Tracks = NsfTracksArg(File, Args, 0);
	if (Tracks.size() > 64)
		Tracks.resize(64);
	nsf::ImportOptions Options;
	Options.Tracks = Tracks;
	nsf::ImportResult Result;
	std::string Error;
	if (!nsf::ImportNsf(File, Options, Result, Error))
		throw Problem("The NSF couldn't be imported: " + Error + ".");
	if (!S.Host().OpenText(Result.Text, Error))
		throw Problem("FamiTracker couldn't open the imported module: " + (Error.empty() ? std::string("it refused.") : Error));
	return ValidUtf8(Result.Summary) + "It's open in FamiTracker as a new module, not saved yet (save_song keeps it). Its tracks "
		"are the NSF's: select_track switches between them, and read_notes and look_at_song show how they're made.";
}

}	// namespace

// The tool set

ToolSet::ToolSet(SongHost &Host) : m_Host(Host)
{
	const std::string Presets = PresetList();
	const std::vector<std::string> Modes = ModeNames();
	std::vector<std::string> Chips;
	Chips.push_back("2A03");
	Chips.push_back("VRC6");
	Chips.push_back("VRC7");
	Chips.push_back("N163");
	std::vector<std::string> Sounds = PresetNames();	// and the VRC7's and the N163's
	for (size_t i = 0; i < Vrc7Patches().size(); ++i)
		if (std::find(Sounds.begin(), Sounds.end(), Vrc7Patches()[i].Name) == Sounds.end())
			Sounds.push_back(Vrc7Patches()[i].Name);
	for (size_t i = 0; i < WaveSounds().size(); ++i)
		if (std::find(Sounds.begin(), Sounds.end(), WaveSounds()[i].Name) == Sounds.end())
			Sounds.push_back(WaveSounds()[i].Name);

	Json NoteItem = Json::MakeObject();
	{
		Schema N;
		N.Add("bar", BarProp("The bar the note starts in (from 1)."), true);
		N.Add("beat", Prop("number", "Beat in the bar where it starts, from 1; fractions for offbeats (1.5 is the \"and\" of beat 1). Default 1."));
		N.Add("pitch", Prop("string", "Note name with FamiTracker's octave: C-4, C#4, Db4 (C-3 is middle C, A-3 is 440 Hz; C-0 to B-7). "
			"On the noise channel: kick, snare, hihat, open hihat, or a noise pitch 0-# to F-#."), true);
		N.Add("beats", Prop("number", "How long it plays, in beats (0.25, 0.5, 1, 1.5, 2...). A gap before the next note is a rest."), true);
		N.Add("volume", Prop("integer", "1-15 (15 is full). Stays for the following notes until another volume is given; the part starts at 15."));
		N.Add("instrument", Prop("string", "Instrument for this note, if not the part's."));
		N.Add("effects", Prop("array", "FamiTracker effects on the note's first row, e.g. [\"Q31\"] (slide up 1 semitone), [\"047\"] (a major chord as an arpeggio), [\"445\"] (vibrato)."));
		NoteItem = N.Get();
	}
	Json NotesProp = Prop("array", "The notes, each {bar, beat, pitch, beats}.");
	NotesProp.Set("items", NoteItem);

	struct Add {
		std::vector<ToolInfo> &Tools;
		void operator()(const char *Name, const char *Title, const std::string &Description, const Schema &Input, bool ReadOnly,
			bool Destructive = false, bool Idempotent = false) const
		{
			ToolInfo Tool;
			Tool.Name = Name;
			Tool.Title = Title;
			Tool.Description = Description;
			Tool.InputSchema = Input.Get();
			Tool.ReadOnly = ReadOnly;
			Tool.Destructive = Destructive;
			Tool.Idempotent = Idempotent;
			Tools.push_back(Tool);
		}
	} AddTool = { m_Tools };

	AddTool("get_song", "Song overview",
		"Overview of the song open in FamiTracker: title, tempo in BPM, bars and beats, each channel and what it plays, "
		"instruments, clips, the key found from the notes with chords that fit the melody bar by bar, whether your changes go "
		"into the song or arrive as clips, and the instrument presets. Start here.",
		Schema().Add("melody_channel", ChannelProp("Channel with the melody, for the chords. Default: the busiest one.")), true, false, true);

	AddTool("read_notes", "Read a channel's notes",
		"The notes of one channel in musical terms: bar, beat, note, length in beats, instrument, volume and effects. "
		"Up to 32 bars per call.",
		Schema().Add("channel", ChannelProp("Channel name (\"VRC6 Pulse 1\", \"Triangle\") or number from get_song."), true).Range(),
		true, false, true);

	AddTool("write_notes", "Write a part",
		"Writes a part (melody, counter-melody, bass line, drums...) on one channel, replacing that channel's notes in the bars "
		"it covers. Each note lasts its beats; a gap is a rest. Unless the user allowed direct changes, the part becomes a clip "
		"for the user to listen to and place. Name an instrument (one of the song's or a preset, which is added: " + Presets +
		"; on VRC7 channels also the VRC7's sounds: " + Vrc7PatchList() + "; on N163 channels the N163 sounds: " + WaveSoundList() +
		"). Drums on the noise channel by name (kick, "
		"snare, hihat); on the DPCM channel the sampled drums of add_sample_drums (kick, snare, low tom, high tom).",
		Schema()
			.Add("channel", ChannelProp("Channel name or number."), true)
			.Add("notes", NotesProp, true)
			.Add("instrument", Prop("string", "Instrument for the notes: a name from get_song, or a preset. Default: what the channel played with."))
			.Add("from_bar", BarProp("First bar to replace. Default: the first note's bar."))
			.Add("to_bar", BarProp("Last bar to replace. Default: the last note's bar. Give from_bar and to_bar with no notes to clear bars."))
			.ClipChoice(),
		false, true);

	AddTool("place_clip", "Place a clip",
		"Places one of the song's clips at a bar: its first column on the given channel, further columns on the channels to "
		"its right. As a copy, or linked (linked copies change together). Needs direct changes; otherwise the user drags "
		"clips into the song from the clips panel.",
		Schema()
			.Add("clip", ChannelProp("The clip's name or number from get_song."), true)
			.Add("channel", ChannelProp("Channel for its first column."), true)
			.Add("bar", BarProp("Bar to place it at."), true)
			.Add("beat", Prop("number", "Beat in the bar, default 1."))
			.Add("linked", Prop("boolean", "Place a linked copy instead of a plain one.")),
		false, true);

	AddTool("add_instrument", "Add an instrument",
		"Adds an instrument from a preset: " + Presets + ". 2A03, VRC6 and VRC7 channels need their own: give the channel it's "
		"for. VRC7 channels play the chip's built-in sounds: " + Vrc7PatchList() + " (a preset's name picks the one most like "
		"it). N163 channels play wave sounds drawn for them, whose timbre can move as a note plays: " + WaveSoundList() + ". An "
		"instrument of that name for that chip is reused. write_notes and set_instrument also take these names directly.",
		Schema()
			.Add("preset", EnumProp("The sound.", Sounds), true)
			.Add("channel", ChannelProp("The channel it's for (picks the chip)."))
			.Add("chip", EnumProp("The chip, when no channel is given. Default 2A03.", Chips)),
		false, false, true);

	AddTool("design_instrument", "Design an instrument",
		"Makes a new instrument from a description in words, for when no preset fits: its volume envelope (attack, decay to "
		"a sustain level, release after ===), its pulse width and how that moves (a tone that opens up, shimmers or thins), "
		"a vibrato built in that comes in after a moment (so long notes sing without effects), a blip, scoop or fall on "
		"the attack, or a chord arpeggio; the articulations of the classic NES soundtracks: an attack that blooms, a duty "
		"click, a decay in steps, held notes struck again, an echo after the note, a pitch flick, a bass that thumps; on the "
		"noise channel a kick, snare, hi-hat, crash or tom, darker or brighter, shorter or longer. Start from a kind (" +
		DesignKinds() + ") and change what should differ. Envelope times are in frames, 60 a second.",
		Schema()
			.Add("name", Prop("string", "The new instrument's name, like \"Hero lead\"."), true)
			.Add("kind", Prop("string", "What it starts from: lead (default), pad, pluck, bass, keys, bell, brass, strings, organ, "
				"chiptune; the classic articulations: bloom lead, stepped lead, click pluck, echo pluck, thump bass; kick, snare, "
				"hihat, open hihat, crash, tom."))
			.Add("channel", ChannelProp("The channel it's for (picks the chip; the noise channel makes a drum)."))
			.Add("chip", EnumProp("The chip, when no channel is given. Default 2A03.", std::vector<std::string>{ "2A03", "VRC6" }))
			.Add("attack", LevelProp("instant, fast, medium, slow, swell, bloom (from about two thirds of the peak to it in two "
				"frames, so notes open up), or frames (0-60)."))
			.Add("attack_from", Prop("integer", "The level the attack starts at, 1-15 (0: from silence); with attack in frames, a "
				"bloom of your own."))
			.Add("decay", LevelProp("none (holds at its peak), short, medium, long, or frames (0-120): how long it takes to fall "
				"to the sustain level."))
			.Add("decay_style", EnumProp("smooth (default), or steps: holds, then drops a few levels at a time, like the leads "
				"of fast action tracks.", std::vector<std::string>{ "smooth", "steps" }))
			.Add("sustain", LevelProp("The level it holds while the note lasts: none (dies away by itself, like a pluck), low, half, "
				"high, full, or 0-15."))
			.Add("release", LevelProp("short, medium, long, or frames (0-60): how it fades after a release (===)."))
			.Add("restrike_beats", Prop("number", "Strike a held note again every so many beats (0.5: eighths), a dip and a rise, "
				"so long notes keep the rhythm. 0 for none."))
			.Add("echo", Prop("boolean", "A soft echo of the note after it dies away or is released."))
			.Add("click", Prop("boolean", "A duty click: each note's first frame at another pulse width, so it speaks."))
			.Add("volume", Prop("integer", "Its loudest, 1-15."))
			.Add("duty", LevelProp("Pulse width: 12.5% (thin, nasal), 25% (bright), 50% (round, hollow), 75%."))
			.Add("duty_motion", EnumProp("How the tone moves: still, sweep (opens from thin on the attack), wobble (shimmers to "
				"and fro), fade (thins as it fades).", std::vector<std::string>{ "still", "sweep", "wobble", "fade" }))
			.Add("vibrato", EnumProp("A vibrato built into the instrument.", std::vector<std::string>{ "none", "gentle", "medium", "strong" }))
			.Add("vibrato_delay_beats", Prop("number", "When the vibrato comes in, in beats at the song's tempo (default: a third of "
				"a second)."))
			.Add("attack_pitch", EnumProp("The attack's pitch: none, blip (an octave up for two frames, like a bell), scoop (up into "
				"the note), drop (falls, like a laser or a tom), flick (a quick flick up and back), thump (from a fourth above, "
				"dropping into the note: a bass that thumps).", std::vector<std::string>{ "none", "blip", "scoop", "drop", "flick",
				"thump" }))
			.Add("arpeggio", EnumProp("A chord played as a fast arpeggio from each note.", std::vector<std::string>{ "none", "octave",
				"major", "minor", "fifth", "power", "sus4", "major7", "minor7" }))
			.Add("arpeggio_frames", Prop("integer", "Frames for each note of the arpeggio, 1-8 (default 1, the fastest)."))
			.Add("tone", EnumProp("Drums: darker or brighter noise.", std::vector<std::string>{ "dark", "normal", "bright" }))
			.Add("length", EnumProp("Drums: shorter or longer.", std::vector<std::string>{ "short", "normal", "long" })),
		false);

	AddTool("set_instrument", "Change the instrument",
		"Gives one channel's notes in some bars another instrument: one of the song's or a preset (added). Good for changing "
		"the sound of a part (softer, brighter, rounder) for a mood.",
		Schema()
			.Add("channel", ChannelProp("Channel name or number."), true)
			.Add("instrument", Prop("string", "Instrument name or number from get_song, or a preset."), true)
			.Range().ClipChoice(),
		false, true);

	AddTool("transpose", "Transpose or change key",
		"Moves notes by semitones, or into another key or mode by scale degree (C major to C minor sounds darker, to lydian "
		"dreamier, to dorian bittersweet; to_key also moves the tonic). Arpeggiated chords follow. Noise and DPCM are left alone.",
		Schema()
			.Add("channels", ChannelListProp("A channel, a list of channels, or \"all\" (default: every melodic channel)."))
			.Add("semitones", Prop("integer", "Up (positive) or down (negative) by this many semitones; 12 is an octave."))
			.Add("to_mode", EnumProp("The mode to change into, keeping the tonic.", Modes))
			.Add("to_key", Prop("string", "The key to change into, like \"A minor\" or \"F dorian\"."))
			.Add("key", Prop("string", "The key the notes are in now. Default: found from the notes."))
			.Range().ClipChoice(),
		false, true);

	AddTool("set_tempo", "Set the speed and tempo",
		"Sets the track's speed and tempo, as FamiTracker's toolbar boxes do. Give bpm and it picks them, rows of even length "
		"first, mostly by changing the speed at tempo 150 as FamiTracker users do (at 4 rows a beat: speed 5 is 180 BPM, 6 is 150, "
		"7 about 129, 8 112.5, 9 100). Or give FamiTracker's own speed (ticks a row, lower is faster) and tempo (150 usually; "
		"higher is faster) when the user names them; with bpm too, the one given stays. Slower feels calmer or sadder, faster "
		"brighter or more urgent. Needs direct changes; undoable.",
		Schema()
			.Add("bpm", Prop("number", "Beats per minute."))
			.Add("speed", Prop("integer", "FamiTracker's speed: ticks a row, 1-31 (lower is faster)."))
			.Add("tempo", Prop("integer", "FamiTracker's tempo, 32-255 (150 is the usual).")),
		false, false, true);

	{
		Json Levels = Prop("object", "Each channel's loudest volume, 1-15, like {\"VRC6 Pulse 1\": 9, \"Noise\": 10}; the "
			"channel's other volumes scale with it. Overrides the style for those channels.");
		AddTool("mix", "Balance the mix",
			"Sets how loud each part plays, keeping each part's own dynamics: its volume column scales so its loudest note plays "
			"at a level. style soft (the default) mixes the way atmospheric NES tracks do: the melody's loudest at 9, an echo of "
			"it at 4, chords at 6, a second line at 7, a bass on a pulse or the sawtooth at 10, drums at 10, and the DPCM level "
			"at Z30 so the triangle and the noise soften to about 70% too; the pulses stop burying the triangle, and the sound "
			"gets warmer. style full puts them near full volume again. levels sets channels yourself, dac the DPCM level alone "
			"(full Z00, warm Z30, soft Z50, softest Z7F, or 0-127), set where the song starts and where its loop starts. The "
			"reply says what each channel plays (melody, echo, chords...) and what changed. Needs direct changes; undoable.",
			Schema()
				.Add("style", EnumProp("soft (default) or full.", std::vector<std::string>{ "soft", "full" }))
				.Add("levels", Levels)
				.Add("dac", Prop("string", "The DPCM level that softens the triangle and the noise: full, warm, soft, softest, keep, or "
					"0-127 (default warm for soft, full for full)."))
				.Add("melody_channel", ChannelProp("The melody. Default: the busiest channel."))
				.Add("from_bar", BarProp("First bar (default 1)."))
				.Add("to_bar", BarProp("Last bar (default the song's last).")),
			false, false, true);
	}

	AddTool("set_swing", "Swing the rhythm",
		"Makes the rhythm swing the way FamiTracker 0.4.6 can: the speed changes every half beat (Fxx), longer then shorter, "
		"so every channel's off-beat notes come late together, and notes written later swing too. The two speeds average the "
		"speed the song had, so the tempo stays. light is about 58%, medium 62%, hard a triplet feel (67%); the speed's ticks "
		"decide which fit (speed 6: 58% or 67%; the reply says). none takes it off. unit sixteenths swings quarter beats "
		"instead (hip-hop, lo-fi). The speed changes go where the song's are, else on the last channel. Needs direct changes; "
		"undoable. set_tempo with another speed afterwards needs set_swing again.",
		Schema()
			.Add("amount", Prop("string", "none, light, medium, hard (triplet), or a percentage, 50 (straight) to 75."), true)
			.Add("unit", EnumProp("What swings: eighths (half beats, default) or sixteenths.",
				std::vector<std::string>{ "eighths", "sixteenths" }))
			.Add("by", EnumProp("speed (default): every channel swings together, by speed changes. delays: only the channels "
				"named swing, each note on the second unit of a pair late by a few frames (Gxx), as a shuffled hi-hat or "
				"arpeggio over a straight part; amount none with the same unit takes those delays off.",
				std::vector<std::string>{ "speed", "delays" }))
			.Add("channels", ChannelListProp("With by delays: the channels that swing (default: every one that plays there)."))
			.Add("from_bar", BarProp("First bar (default 1)."))
			.Add("to_bar", BarProp("Last bar (default the song's last).")),
		false, false, true);

	{
		Json Chords = Prop("string", "");
		Json Types = Json::MakeArray();
		Types.Push("string");
		Types.Push("array");
		Chords.Set("type", Types);
		Json Item = Json::MakeObject();
		Item.Set("type", "string");
		Chords.Set("items", Item);
		Chords.Set("description", "The chords to play instead of fitting them to the melody: a progression with bar lines, "
			"\"| Am7 | Fmaj7 | C | G7sus4 G7 |\", chords a bar each without them, \"I vi IV V\", or a list of bars, "
			"[\"Am7\", \"F G\"]. Names (C, F#m7, Bbmaj7, Dsus4, Cadd9, Em7b5, C/E) or degrees of the key (I, vi, IV, V7, "
			"bVII, V7/V). Chords in a bar share it; \".\" holds the chord before for a share (\"C . . G\": three beats of C, "
			"one of G); \"%\" repeats a bar. The progression repeats to fill the bars.");
		AddTool("add_chords_and_bass", "Add chords and a bass line",
			"Writes chords on a pulse channel and a bass line on the triangle or sawtooth: chords fitted to a melody bar by bar "
			"in its key, or the progression you give (no melody needed then). Choose how they're played; the defaults are the "
			"plainest (triads held as an arpeggio, root and fifth in the bass), and most songs want more. chord_style: " +
			StyleList(ChordStyles()) + ". rhythm (arpeggio, stabs and lush): " + StyleList(ChordRhythms()) + ". pattern (broken, "
			"and the order of a lush chord's arpeggio, where alberti leaps and rolling spreads it over two octaves): " +
			StyleList(BrokenPatterns()) + ". bass_style: " + StyleList(BassStyles()) + ". color adds sevenths (\"sevenths\") "
			"or richer tones (\"rich\": add9, maj7, m9, a V7sus4 resolving) where the melody allows. Lush chords are "
			"instruments made for each chord's notes (\"Lush maj7\", \"Lush m9\"), added when first needed. Chords and a bass "
			"line that play in those bars already are replaced (other chords for a section); else free channels are picked unless "
			"named. The reply lists the chords by name and as degrees.",
			Schema()
				.Add("chords", Chords)
				.Add("melody_channel", ChannelProp("Channel with the melody. Default: the busiest one."))
				.Add("chord_channel", ChannelProp("Pulse channel for the chords, or \"none\". Default: the one playing chords there "
					"already, else a free one."))
				.Add("chord_channels", ChannelListProp("Voiced chords: 2-4 channels, a voice each (VRC7 FM, N163 or pulse channels). "
					"Default: free FM channels, else N163 ones, else pulses."))
				.Add("bass_channel", ChannelProp("Triangle or sawtooth for the bass, or \"none\". Default: the one playing the bass "
					"there already, else a free one."))
				.Add("key", Prop("string", "Key, like \"C major\" or \"A minor\". Default: found from the melody."))
				.Add("color", EnumProp("plain (triads; the default but for lush and voices), sevenths, or rich (the default for lush "
					"and voices).",
					std::vector<std::string>{ "plain", "sevenths", "rich" }))
				.Add("chord_style", EnumProp("How the chords are played (default arpeggio).", StyleNames(ChordStyles())))
				.Add("rhythm", EnumProp("When arpeggio, stabs or lush chords are struck (arpeggio and lush: bars, stabs: offbeats by "
					"default).", StyleNames(ChordRhythms())))
				.Add("pattern", EnumProp("The order of a broken chord's notes, or of a lush chord's arpeggio (default up).",
					StyleNames(BrokenPatterns())))
				.Add("notes_per_beat", Prop("integer", "Broken chords: notes a beat, 1-4 (default 2)."))
				.Add("arp_frames", Prop("integer", "Lush chords: frames each note of a chord's arpeggio lasts, 1-6 (default 2; 3 or 4 "
					"slower and dreamier, 1 a buzz)."))
				.Add("strum", Prop("integer", "Voiced chords: frames between one voice and the next above it (Gxx), 0-3 (default 1: "
					"rolled a little, as a hand plays them; 0 struck together; 2 or 3 a slow harp-like roll)."))
				.Add("bass_style", EnumProp("How the bass line moves (default root_fifth).", StyleNames(BassStyles())))
				.Add("half_bars", Prop("boolean", "Fitted chords may change halfway through a bar (default true)."))
				.Add("chord_instrument", Prop("string", "Instrument or preset for the chords (default: soft chords for arpeggio, "
					"pluck for stabs and broken, warm pad for guide). Lush chords are made from a preset's sound (default lush chords; "
					"pluck, bell or soft chords work too). Voiced chords: a sound for every voice's channel (default electric piano on "
					"VRC7 and N163 channels, soft chords on pulses)."))
				.Add("chord_volume", Prop("integer", "The chords' volume, 1-15 (default 15 for arpeggio and guide, 11 for stabs, "
					"10 for broken and voices, 6 for lush); beats a step softer than a bar's first note."))
				.Add("bass_instrument", Prop("string", "Instrument or preset for the bass (default \"bass\")."))
				.Range().ClipChoice(),
			false, true);

		Json Intensity = LevelProp("light, normal (default) or busy.");
		AddTool("add_drums", "Add drums",
			"Writes a drum part on the noise channel: a groove with small changes every other bar (an open hi-hat into the "
			"next bar), a fill at the end of every phrase (toms, a snare roll, or kick and snare, in turn) and a crash where "
			"the drums come in and after each fill. Grooves: " + StyleList(Grooves()) + ". Kick, snare, hi-hat, crash and tom "
			"instruments are added from presets.",
			Schema()
				.Add("groove", EnumProp("The beat (default basic; waltz in bars of three beats).", StyleNames(Grooves())))
				.Add("intensity", Intensity)
				.Add("fill_every", Prop("integer", "A fill in the last bar of every so many bars, counted from bar 1 (default 4; "
					"0 for none)."))
				.Add("crash", Prop("boolean", "A crash where the drums start and after each fill (default true)."))
				.Add("volume", Prop("integer", "The loudest hit's volume, 1-15 (default 15); the others follow."))
				.Add("kit", EnumProp("noise (default): every drum on the noise channel, one at a time. samples: a sampled kick "
					"and snare (and toms in fills) on the DPCM channel, from add_sample_drums's kit, so the noise keeps the hi-hats "
					"where they'd give way: punchier, as many classic NES soundtracks do.",
					std::vector<std::string>{ "noise", "samples" }))
				.Add("channel", ChannelProp("The noise channel (default: the song's)."))
				.Add("from_bar", BarProp("First bar. Default: where the song's parts start."))
				.Add("to_bar", BarProp("Last bar. Default: where they end."))
				.ClipChoice(),
			false, true);

		AddTool("add_harmony", "Add a harmony voice",
			"Writes a second voice that follows the melody note for note, a third under it by default, in the key; where the "
			"melody holds or lands on a beat it takes the chord's note nearest that interval, so it doesn't rub. Intervals: " +
			StyleList(HarmonyIntervals()) + ". It follows the chords given, the ones the song's other parts play, or chords "
			"fitted to the melody. Best on a channel like the melody's (a free one is picked), quieter.",
			Schema()
				.Add("melody_channel", ChannelProp("The melody. Default: the busiest channel."))
				.Add("channel", ChannelProp("Channel for the harmony. Default: a free one like the melody's."))
				.Add("interval", EnumProp("How far from the melody (default third_below).", StyleNames(HarmonyIntervals())))
				.Add("long_notes_only", Prop("boolean", "Only under the melody's notes of a beat or more, so quick runs stay single "
					"(default false)."))
				.Add("volume", Prop("integer", "Its volume, 1-15. Default: 3 softer than the melody."))
				.Add("instrument", Prop("string", "Instrument or preset. Default: the melody's own (a soft lead on another chip)."))
				.Add("chords", Chords)
				.Add("key", Prop("string", "Key, like \"C major\". Default: found from the notes."))
				.Range().ClipChoice(),
			false, true);

		AddTool("add_counter_melody", "Add a counter-melody",
			"Writes a counter-melody: a second line of its own, made from the chords' thirds and sevenths, that holds while the "
			"melody moves and moves by step (a pickup into the next chord) while the melody holds or rests, never a semitone "
			"from it. Below the melody (a tenor line) or above it (a descant). Gives a song depth; a starting point you can "
			"shape further with write_notes.",
			Schema()
				.Add("melody_channel", ChannelProp("The melody. Default: the busiest channel."))
				.Add("channel", ChannelProp("Channel for it. Default: a free one like the melody's."))
				.Add("position", EnumProp("below (default) or above the melody.", std::vector<std::string>{ "below", "above" }))
				.Add("volume", Prop("integer", "Its volume, 1-15. Default: 4 softer than the melody."))
				.Add("instrument", Prop("string", "Instrument or preset (default \"soft lead\", a contrast to most leads)."))
				.Add("chords", Chords)
				.Add("key", Prop("string", "Key, like \"C major\". Default: found from the notes."))
				.Range().ClipChoice(),
			false, true);

		Json Touches = Prop("array", "Which touches: accents (a bar's first note loudest, beats a step softer, the notes between "
			"them softer still), vibrato (a gentle vibrato that comes in half a beat into the melody's notes of 1.5 beats or "
			"more), separate (a short gap between repeated triangle notes, which otherwise sound as one); and when asked, fades "
			"(held notes of two beats or more fade from halfway through with A0y, the next note at its own volume again) and "
			"scoops (held melody notes after a leap up slide into place from a semitone or two below, at most one every two "
			"bars), falls (the melody's last note before a rest of a beat or more drops two semitones as it ends, a sigh, at "
			"most one every two bars) and staccato (the bass's notes of a beat or more cut after half a beat, so it bounces "
			"rather than booms). Default: accents, vibrato and separate.");
		Json TouchItem = Json::MakeObject();
		TouchItem.Set("type", "string");
		Json TouchNames = Json::MakeArray();
		TouchNames.Push("accents");
		TouchNames.Push("vibrato");
		TouchNames.Push("separate");
		TouchNames.Push("fades");
		TouchNames.Push("scoops");
		TouchNames.Push("falls");
		TouchNames.Push("staccato");
		TouchItem.Set("enum", TouchNames);
		Touches.Set("items", TouchItem);
		AddTool("polish", "Finishing touches",
			"Gives parts the small things that make them sound played rather than typed: accents, a delayed vibrato on the "
			"melody's long notes, and gaps between repeated triangle notes; fades on held notes and scoops into notes after "
			"leaps when asked. Channels that already shape their volumes keep them. Run it after the parts are written.",
			Schema()
				.Add("touches", Touches)
				.Add("channels", ChannelListProp("A channel, a list, or \"all\" (default: every melodic channel)."))
				.Add("melody_channel", ChannelProp("The melody, for the vibrato. Default: the busiest channel."))
				.Range().ClipChoice(),
			false, true);
	}

	AddTool("add_echo", "Add an echo",
		"Copies a channel's notes onto a free channel, a little later and quieter: the classic NES echo. Makes a part sound "
		"spacious, dreamy or underwater. The echo follows the original's dynamics (volume 6 is about 40% of each note's "
		"loudness, as atmospheric chiptune mixes it), plays the same instrument and octave (on another chip, the same preset "
		"there), and can be detuned a step or two so it shimmers against the melody where they overlap.",
		Schema()
			.Add("channel", ChannelProp("Channel to echo."), true)
			.Add("to_channel", ChannelProp("Channel for the echo. Default: a free one of the same kind, else another free one."))
			.Add("delay_beats", Prop("number", "How much later, in beats (default 3 rows; a beat is the other common choice)."))
			.Add("volume", Prop("integer", "Echo volume 1-15 at the original's full loudness (default 6, about 40%), scaled with "
				"each note's own."))
			.Add("detune", Prop("integer", "Fine pitch steps, -8 to 8 (default 0): 1 or -1 (P81, P7F) give a subtle chorus with "
				"the melody, more sounds out of tune. Back in tune (P80) after the echo."))
			.Add("instrument", Prop("string", "Instrument for the echo. Default: the original's."))
			.Range().ClipChoice(),
		false, true);

	AddTool("add_vibrato", "Add vibrato",
		"Adds vibrato (FamiTracker's 4xy) to a channel's longer notes, starting just after each note begins and stopping at its "
		"end. Gentle sounds expressive; slow and strong sounds wobbly, dreamy or underwater. depth \"none\" takes vibrato off.",
		Schema()
			.Add("channel", ChannelProp("Channel name or number."), true)
			.Add("speed", LevelProp("slow, medium, fast, or 1-15 (default medium)."))
			.Add("depth", LevelProp("none, gentle, medium, strong, or 0-15 (default gentle)."))
			.Add("min_beats", Prop("number", "Only notes at least this long, in beats (default 1)."))
			.Add("delay_beats", Prop("number", "Start this long after the note begins (default 0.25 beat)."))
			.Range().ClipChoice(),
		false, true);

	{
		Schema Move;
		Move.Add("channel", ChannelProp("Channel name or number."), true)
			.Add("semitones", Prop("integer", "Up (positive) or down (negative); -12 is an octave lower."), true);
		Json Moves = Prop("array", "Parts moved up or down, like [{\"channel\": \"Triangle\", \"semitones\": -12}].");
		Moves.Set("items", Move.Get());

		Schema Sound;
		Sound.Add("channel", ChannelProp("Channel name or number."), true)
			.Add("instrument", Prop("string", "Instrument name or number from get_song, or a preset (added)."), true);
		Json Sounds = Prop("array", "New sounds for parts, like [{\"channel\": \"VRC6 Pulse 1\", \"instrument\": \"warm pad\"}].");
		Sounds.Set("items", Sound.Get());

		Schema Vibrato;
		Vibrato.Add("channel", ChannelProp("Channel name or number."), true)
			.Add("speed", LevelProp("slow, medium, fast, or 1-15 (default medium)."))
			.Add("depth", LevelProp("none, gentle, medium, strong, or 0-15 (default gentle)."))
			.Add("min_beats", Prop("number", "Only notes at least this long, in beats (default 1)."))
			.Add("delay_beats", Prop("number", "Start this long after the note begins (default 0.25 beat)."));
		Json VibratoProp = Vibrato.Get();
		VibratoProp.Set("description", "Vibrato on one part's longer notes, as add_vibrato.");

		Schema Echo;
		Echo.Add("channel", ChannelProp("Channel to echo."), true)
			.Add("to_channel", ChannelProp("Channel for the echo. Default: a free one of the same kind."))
			.Add("delay_beats", Prop("number", "How much later, in beats (default 3 rows)."))
			.Add("volume", Prop("integer", "Echo volume 1-15 (default 5), scaled with the original's loudness."))
			.Add("instrument", Prop("string", "Instrument for the echo. Default: the original's."));
		Json EchoProp = Echo.Get();
		EchoProp.Set("description", "An echo of one part (as changed by the steps before it), as add_echo.");

		AddTool("change_mood", "Change the mood",
			"Changes the mood of some bars in one go: another key or mode for every melodic channel, parts moved up or down, new "
			"sounds, vibrato on a part, an echo of a part (in that order), and the tempo. All of it arrives together: one clip "
			"to try, or with direct changes one Ctrl+Z step (the tempo is a second one). Use it rather than several separate "
			"tools whenever a mood needs more than one change, since separate clips each start from the original song. Darker, "
			"for example: to_mode minor, the bass an octave lower, a softer lead sound, tempo_percent 85.",
			Schema()
				.Add("mood", Prop("string", "The mood in a few words (\"darker\", \"underwater\"), for the clip's name."))
				.Add("to_mode", EnumProp("The mode to change into, keeping the tonic.", Modes))
				.Add("to_key", Prop("string", "The key to change into, like \"A minor\" or \"F dorian\"."))
				.Add("key", Prop("string", "The key the notes are in now. Default: found from the notes."))
				.Add("moves", Moves)
				.Add("sounds", Sounds)
				.Add("vibrato", VibratoProp)
				.Add("echo", EchoProp)
				.Add("bpm", Prop("number", "New tempo in beats per minute."))
				.Add("tempo_percent", Prop("number", "New tempo as a percentage of the current one (85 is 15% slower)."))
				.Range().ClipChoice(),
			false, true);
	}

	{
		Json Moves = Prop("array", "Parts moved up or down, e.g. [{\"channel\": \"Triangle\", \"semitones\": -12}].");
		Json Sounds = Prop("array", "New sounds, e.g. [{\"channel\": \"VRC6 Pulse 1\", \"instrument\": \"soft lead\"}]; false keeps every instrument.");
		std::vector<std::string> KeepOrModes(1, "keep");
		KeepOrModes.insert(KeepOrModes.end(), Modes.begin(), Modes.end());
		AddTool("make_version", "Make a version",
			"Makes another version of the selected track as a new track in the module, leaving the original as it was: a night "
			"version, calm or tense music for a game state, an area's variant. FamiTracker then shows the new track and the other "
			"tools work on it (select_track switches back). version \"night\" (or \"calm\") is much more than slower, by itself: "
			"the parts that sit high an octave lower (register), the melody's fast runs as their outline (melody simpler), notes "
			"held into the next with even dynamics (legato), chords held as a warm pad that swells in (chords held), the bass in "
			"long notes (bass long), the drums a soft pulse on each bar's first beat (drums pulse), a soft lead that swells in, "
			"slower and wider vibrato, a longer and softer echo, tempo_percent 80 (a fast song slows to 120 BPM, down to 60%) and "
			"volume_percent 75. version \"tense\" (or "
			"\"battle\", \"danger\", \"boss\", \"chase\") is faster and "
			"darker by itself: tempo_percent 115, to_mode minor when the song is in major, a bright lead on the melody, bass "
			"driving and drums busier. Give any of those to change it (false turns vibrato, echo or sounds off; to_mode \"keep\" "
			"keeps the mode). Other "
			"versions change only what you give, with change_mood's options. Adding a track can't be undone in FamiTracker.",
			Schema()
				.Add("version", Prop("string", "What the version is: \"night\", \"calm\", \"tense\", \"rain\"... Its name is the track's "
					"plus this in brackets, like \"Town (night)\"."), true)
				.Add("name", Prop("string", "The new track's name, if not that."))
				.Add("melody_channel", ChannelProp("The melody's channel, for the night version's lead sound, vibrato and echo. Default: the busiest one."))
				.Add("tempo_percent", Prop("number", "Tempo as a percentage of the original's (80 is 20% slower). Tempo commands in the patterns follow."))
				.Add("bpm", Prop("number", "Tempo in beats per minute instead."))
				.Add("volume_percent", Prop("integer", "Every channel's volumes at this percentage (10-100); the triangle has no volume."))
				.Add("drums", EnumProp("The drums: keep, softer, sparser (only the hits on beats, softer), half (beats 1 and 3), "
					"pulse (each bar's first beat, very soft), off, or busier (soft hi-hats added between the beats). Sampled drums on "
					"the DPCM channel thin out the same way.",
					std::vector<std::string>{ "keep", "softer", "sparser", "half", "pulse", "off", "busier" }))
				.Add("bass", EnumProp("The triangle and sawtooth: keep, driving (each note struck again on every eighth note, "
					"cut short before the next), or long (only the notes on the beats, a repeated one held instead).",
					std::vector<std::string>{ "keep", "driving", "long" }))
				.Add("melody", EnumProp("The melody: keep, or simpler (its fast runs as their outline: the short notes between the "
					"eighths left out, the notes before ringing on).", std::vector<std::string>{ "keep", "simpler" }))
				.Add("chords", EnumProp("Channels that play chords as arpeggios: keep, held (a chord struck again in its bar held "
					"instead, a pad), or richer (held, and major and minor triads as sevenths).",
					std::vector<std::string>{ "keep", "held", "richer" }))
				.Add("register", EnumProp("keep, or lower: the parts that sit high (from C-4 up, not the bass) an octave down.",
					std::vector<std::string>{ "keep", "lower" }))
				.Add("legato", Prop("boolean", "Notes held into the next (no note cuts, halts just before a note taken out, the others "
					"softened to releases) and even dynamics without accents. Default: true for a calm version."))
				.Add("to_mode", EnumProp("A mode to change into, keeping the tonic; \"keep\" keeps the mode.", KeepOrModes))
				.Add("to_key", Prop("string", "A key to change into, like \"A minor\"."))
				.Add("key", Prop("string", "The key the notes are in now. Default: found from the notes."))
				.Add("moves", Moves)
				.Add("sounds", Sounds)
				.Add("vibrato", Prop("object", "Vibrato like add_vibrato's arguments, e.g. {\"channel\": \"VRC6 Pulse 1\", \"speed\": \"slow\", \"depth\": \"gentle\"}; false for none."))
				.Add("echo", Prop("object", "An echo like add_echo's arguments, e.g. {\"channel\": \"VRC6 Pulse 1\"}; false for none.")),
			false);
	}

	{
		std::vector<std::string> Kinds;
		std::string Uses;
		for (size_t i = 0; i < Jingles().size(); ++i) {
			Kinds.push_back(Jingles()[i].Name);
			Uses += Format("%s%s (%s)", i == 0 ? "" : "; ", Jingles()[i].Name.c_str(), Jingles()[i].Use.c_str());
		}
		AddTool("make_jingle", "Make a jingle",
			"Makes a jingle, a short tune a game plays once, as a new track in the module: a lead with a harmony line, a bass and "
			"drums, in the song's key note, around its melody's register and with its sounds, at the jingle's own tempo. It ends "
			"with a halt, so export_for_game saves it as a sound that doesn't loop. FamiTracker then shows the new track and the "
			"other tools work on it (select_track switches back): play it for the user, and change notes with write_notes. "
			"Kinds: " + Uses + ". Other words for them work too (\"quest complete\", \"game over\", \"inn\"). Adding a track "
			"can't be undone in FamiTracker.",
			Schema()
				.Add("kind", EnumProp("What it's for.", Kinds), true)
				.Add("key", Prop("string", "Its key note, like \"D\" (each kind keeps its mode: defeat and danger are minor, the "
					"others major). Default: the song's key note."))
				.Add("bpm", Prop("number", "Its tempo. Default: the kind's own (72 for rest to 150 for level up)."))
				.Add("name", Prop("string", "The new track's name. Default: the kind's, like \"Level up\"."))
				.Add("lead_channel", ChannelProp("The lead's channel. Default: the song's melody channel."))
				.Add("harmony_channel", ChannelProp("The harmony line's channel, or \"none\". Default: another pulse channel."))
				.Add("bass_channel", ChannelProp("The bass's channel, or \"none\". Default: the song's triangle or sawtooth."))
				.Add("drums", Prop("boolean", "Drums on the noise channel, for the kinds that have them (default true)."))
				.Add("lead_instrument", Prop("string", "The lead's instrument or preset. Default: the song's lead sound (a bell for "
					"item and new quest, a soft lead for rest)."))
				.Add("harmony_instrument", Prop("string", "The harmony's instrument or preset. Default: the channel's sound in the song."))
				.Add("bass_instrument", Prop("string", "The bass's instrument or preset. Default: the song's bass sound.")),
			false);
	}

	{
		std::vector<std::string> Kinds;
		std::string Uses;
		for (size_t i = 0; i < SoundEffectKinds().size(); ++i) {
			Kinds.push_back(SoundEffectKinds()[i].Name);
			Uses += Format("%s%s (%s)", i == 0 ? "" : "; ", SoundEffectKinds()[i].Name.c_str(), SoundEffectKinds()[i].Use.c_str());
		}
		Json SoundLayer = Json::MakeObject();
		{
			Json Pitch = ChannelProp("Its pitch: a note like \"C-6\" on a pulse or the triangle (C-4 is 48; the triangle sounds an "
				"octave lower than written), 0-15 on the noise (15 the brightest), or a word: low, mid, high, very high (noise: dark, "
				"mid, bright).");
			Json PitchTo = ChannelProp("The pitch it slides to over its length, or over each repeat (a chirp). Default: it stays.");
			Schema L;
			L.Add("channel", EnumProp("The 2A03 channel that plays it: a pulse (tones, beeps, chirps), the triangle (low booms, "
				"thumps, pure whistles; no volume of its own) or the noise (wind, rain, fire, hits, flaps).",
				std::vector<std::string>{ "pulse", "triangle", "noise" }), true);
			L.Add("start", Prop("number", "When it starts, in seconds (default 0)."));
			L.Add("seconds", Prop("number", "How long it lasts (default 0.5; at most 4.2)."));
			L.Add("pitch", Pitch);
			L.Add("pitch_to", PitchTo);
			L.Add("glide", EnumProp("How it slides there: even (default), fast (most of the way at once, like a chirp or a zap), "
				"slow (late, like a siren winding up).", std::vector<std::string>{ "even", "fast", "slow" }));
			L.Add("volume", LevelProp("Its loudest, 1-15, or soft, medium, loud (default 12)."));
			L.Add("volume_to", LevelProp("Its loudness at its end, 0-15: fading away (or growing) over its length."));
			L.Add("envelope", EnumProp("Each time it sounds: steady (default), hit (loud at once, dying quickly), fade (dying "
				"evenly), swell (growing), swell_fade (growing, then fading), drops (silent but for short random blips: rain, "
				"drips).", std::vector<std::string>{ "steady", "hit", "fade", "swell", "swell_fade", "drops" }));
			L.Add("repeats", Prop("integer", "It sounds this many times in its length, each with the envelope and the slide: chirps, "
				"flaps, footfalls, beeps (1-64)."));
			L.Add("gap", Prop("number", "With repeats: the share of each that's silent, 0-0.9 (default 0.25)."));
			L.Add("step", Prop("number", "With repeats: semitones each one moves from the one before (on the noise, steps), like -2 "
				"for beeps going down."));
			L.Add("gusts", Prop("integer", "Slow, uneven swells of its loudness and pitch, like wind: how many over its length (1-16)."));
			L.Add("flutter", LevelProp("Its loudness wobbling, times a second: wings 8-12, a motor 15; or slow, medium, fast."));
			L.Add("vibrato", LevelProp("Its pitch wavering, in semitones (0.3 gentle to 5 wild), or gentle, medium, strong, wild."));
			L.Add("vibrato_hz", Prop("number", "How fast the vibrato wavers, times a second (default 6)."));
			L.Add("crackle", LevelProp("Random pops on top of it, like fire or rain on a roof: 0-1, or few, some, many. With envelope "
				"drops, how many drops."));
			L.Add("rough", Prop("boolean", "A harsh, gritty edge: the pitch jitters (thunder, a hurt buzz)."));
			L.Add("duty", LevelProp("A pulse's tone: 12.5% (thin, nasal), 25% (bright), 50% (round; the default), 75%. The noise's: "
				"metallic (buzzy) or plain."));
			L.Add("echo", Prop("boolean", "A pulse's softer echo a tenth of a second later, on the other pulse."));
			SoundLayer = L.Get();
		}
		Json SoundLayers = Prop("array", "Instead of a kind or a recording: a sound described in words, as layers on the 2A03's "
			"channels (two pulses, the triangle, the noise; layers that don't sound at once can share one). Think of what makes the "
			"sound: a bird flying off is wing flaps (noise, 7 hits fading away, pitch 7 to 5) and a chirp (a pulse sliding quickly "
			"from D-6 up to A-6); heavy wind is noise with gusts; a car passing is a low pulse rising and falling in pitch with a "
			"flutter.");
		SoundLayers.Set("items", SoundLayer);
		AddTool("make_sound_effect", "Make a sound effect",
			"Makes a sound effect for a game as a new track in the module, the NES's way: a pulse, the triangle and the noise "
			"channel moving their volume, pitch and duty 60 times a second, through instruments whose sequences shape the sound. "
			"It halts at its end, so export_for_game saves it as a sound that plays once; ambience (wind, rain, fire, an engine) "
			"loops instead. FamiTracker then shows the new track: play it for the user, and offer other takes (variant 2, 3..., "
			"semitones, length_percent, volume_percent) to choose from. Besides the kinds, it builds a sound described in words "
			"from layers (layers), or turns a recording of the user's voice into one. Suggest keeping sound effects in a module of "
			"their own, apart from the music. Kinds: " + Uses + ". Other "
			"words for them work too (\"pew\", \"boing\", \"kssh\", \"menu\", \"bird flying away\"). Adding a track can't be "
			"undone in FamiTracker.",
			Schema()
				.Add("kind", EnumProp("What it's for.", Kinds))
				.Add("layers", SoundLayers)
				.Add("seconds", Prop("number", "With layers: how long the whole sound lasts (default: until its last layer ends); a "
					"loop starts over after it."))
				.Add("loop", Prop("boolean", "The track loops instead of halting, for ambience a game plays over and over (the kinds "
					"marked \"loops\" do by default). A loop lasts at most 4.2 s."))
				.Add("recording", Prop("string", "Instead of a kind: a WAV file of the user's voice (\"pew\", \"boing\", \"kssh\") "
					"on their computer, like C:\\Users\\Name\\Documents\\pew.wav, or \"latest\" for the newest take recorded in "
					"FamiTracker (Record from microphone, Sound effect from your voice: they're kept in the Recordings folder next "
					"to FamiTracker.exe). Its pitch and loudness play on a pulse, its breathy and hissy part on the noise channel."))
				.Add("octaves", Prop("integer", "With a recording: whole octaves to move its pitch, -4 to 4. Default: so its "
					"middle lands near E-4."))
				.Add("noise", Prop("boolean", "With a recording: its unpitched part on the noise channel (default true)."))
				.Add("name", Prop("string", "The new track's name, which the exported file and the GameMaker sound are named "
					"after. Default: \"SFX\" and the kind or the recording's file name, like \"SFX Jump\"."))
				.Add("variant", Prop("integer", "1 as designed; 2-9 other takes: higher or lower, shorter or longer, every other "
					"one with another pulse duty."))
				.Add("semitones", Prop("integer", "Higher (positive) or lower, -24 to 24 (noise moves a step for 3)."))
				.Add("length_percent", Prop("integer", "Its length, 25-400 (100 as designed)."))
				.Add("volume_percent", Prop("integer", "Its volume, 10-200 (100 as designed; the loudest is 15).")),
			false);
	}

	AddTool("check_loop", "Check the loop",
		"How the song loops in a game: the intro that plays once, the part that repeats and how long each lasts, and the seam "
		"where the end goes back to the loop's start: notes hanging over it, effects, instruments, volumes or tempo that differ "
		"between the first time and the repeats, the chords and the melody's step across it. Run it before exporting music "
		"for a game.",
		Schema(), true, false, true);

	AddTool("set_loop", "Set the loop",
		"Sets where the song loops: after to_bar it jumps back to from_bar (a Bxx effect), which has to start a frame; bars "
		"before from_bar then play once as an intro. Jumps and halts elsewhere go. Needs direct changes; undoable.",
		Schema().Add("from_bar", BarProp("Where the loop starts; it has to start a frame. Default 1 (no intro)."))
			.Add("to_bar", BarProp("The loop's last bar. Default: the song's last bar.")),
		false, true);

	AddTool("insert_bars", "Put bars in",
		"Puts empty bars into the song at at_bar, in every channel: everything from there on moves later, and the loop, its "
		"jump back (Bxx) and the song's end move with it. For a new section between others (an intro, a bridge, a break); "
		"fill the new bars afterwards. Whole frames go into FamiTracker's frame list, so the frames after keep their patterns "
		"and linked copies. Needs direct changes; one Ctrl+Z undoes it.",
		Schema().Add("at_bar", BarProp("The new bars go before this bar; the bar after the last adds them at the end."), true)
			.Add("bars", Prop("integer", "How many bars (default 1).")),
		false);

	AddTool("remove_bars", "Take bars out",
		"Takes bars out of the song, in every channel: what comes after moves up to close the gap, and the loop, its jump "
		"back (Bxx) and the song's end move with it. Whole frames go from FamiTracker's frame list, so the frames after keep "
		"their patterns and linked copies. Needs direct changes; one Ctrl+Z undoes it.",
		Schema().Add("from_bar", BarProp("First bar to take out."), true)
			.Add("to_bar", BarProp("Last bar to take out (default from_bar).")),
		false, true);

	AddTool("export_for_game", "Export music for a game",
		"Saves tracks as OGG files for a game, next to the song: each rendered from FamiTracker, with the intro that plays once "
		"and the loop cut so it repeats without a seam (the loop points are in the file and in the GameMaker code the reply "
		"gives). A song that halts plays once. Each is brought up so its loudest peak is at -1 dBFS (FamiTracker renders at "
		"the NES's level, well under full scale); normalize false keeps that level. Run check_loop first. Takes a while for "
		"long songs.",
		Schema()
			.Add("tracks", ChannelListProp("\"this\" (default: the selected track), \"all\", or tracks by number or name."))
			.Add("quality", Prop("number", "OGG quality from 0 (small) to 1 (best); default 0.6, which suits chiptunes."))
			.Add("normalize", Prop("boolean", "Bring the loudest peak up to -1 dBFS, by at most 12 dB (default true); false keeps "
				"FamiTracker's own level, as it plays.")),
		false);

	AddTool("select_track", "Switch tracks",
		"Selects another track (song) of the module in FamiTracker, by number or name; the other tools work on the selected "
		"one. get_song lists the tracks.",
		Schema().Add("track", ChannelProp("Track number (from 1) or name, like \"Town (night)\"."), true), false, false, true);

	AddTool("keep_version", "Keep a version",
		"Keeps the selected track as it is now as a track of its own, named after it (\"Town (version 1)\", or with name: "
		"\"Town (calm bridge)\"), and stays on the selected track: keep one before trying something, so the user can "
		"compare (compare_versions) or go back (select_track).",
		Schema().Add("name", Prop("string", "What sets this version apart, like \"before the bridge\". Default: version 1, 2...")),
		false);

	AddTool("compare_versions", "Compare versions",
		"Plays the same bars of two versions or more for the user, one after the other: by default the newest version kept "
		"of the selected track (keep_version), then the track as it is now. FamiTracker shows the selected track again "
		"afterwards. You can't hear it; ask the user which they like better.",
		Schema().Add("versions", ChannelListProp("Tracks by number or name, in the order to play; one: it, then the selected track."))
			.Add("from_bar", BarProp("Bar to start at (default 1)."))
			.Add("bars", Prop("integer", "How many bars of each (default 8)."))
			.Add("loop", Prop("boolean", "Play them over and over until stop (default false).")),
		true);

	AddTool("set_expansion", "Switch the expansion chip",
		"Switches the song's expansion sound chip, as Module > Module properties does: none (the 2A03's five channels only), "
		"VRC6 (two more pulses and a sawtooth), VRC7 (six FM channels with built-in sounds like piano, flute and brass), MMC5 "
		"(two more pulses), FDS or N163 (wavetables). Use it when the user asks for a VRC6 or VRC7 song, before writing. "
		"FamiTracker plays one expansion chip at a time, for every track of the module: the old chip's channels are cleared, so "
		"when they have notes this asks for confirm: true, which needs the user's OK. It can't be undone in FamiTracker.",
		Schema()
			.Add("chip", EnumProp("The chip.", std::vector<std::string>{ "none", "VRC6", "VRC7", "FDS", "MMC5", "N163" }), true)
			.Add("n163_channels", Prop("integer", "N163's channels, 1-8 (default 4). Up to 6 keep the chip's switching whine above "
				"hearing; each takes 16 samples of its wave memory, leaving room for 16 minus that many sounds."))
			.Add("confirm", Prop("boolean", "True once the user agreed to lose the notes on the old chip's channels.")),
		false, true);

	{
		std::vector<std::string> Moods;
		std::string Uses;
		for (size_t i = 0; i < sizeof(MOODS) / sizeof(MOODS[0]); ++i) {
			Moods.push_back(MOODS[i].Name);
			Uses += std::string(i == 0 ? "" : "; ") + MOODS[i].Name + " (" + MOODS[i].Use + ")";
		}
		AddTool("make_groove", "Start from a groove",
			"Writes a backing to start a track from: chords, a bass line and drums for a mood, each played the way that mood "
			"is usually arranged, in one go (one clip, or one Ctrl+Z). Then write the melody over it with write_notes. Moods: " +
			Uses + ". Other words for them work too (\"village\", \"fight\", \"spooky\", \"jazzy\"). The reply gives the "
			"chords and the tempo the mood suits.",
			Schema()
				.Add("mood", EnumProp("The mood.", Moods), true)
				.Add("key", Prop("string", "Key, like \"D minor\". Default: the song's key note (C or A when there are no notes) "
					"in the mood's mode."))
				.Add("from_bar", BarProp("Where it starts (default 1)."))
				.Add("bars", Prop("integer", "How many bars, 1-64 (default 8; the four-bar progression repeats)."))
				.Add("chord_channel", ChannelProp("Pulse channel for the chords, or \"none\". Default: a free one."))
				.Add("bass_channel", ChannelProp("Triangle or sawtooth for the bass, or \"none\". Default: a free one."))
				.Add("drum_channel", ChannelProp("The noise channel, or \"none\" for no drums. Default: it, when free."))
				.Add("kit", EnumProp("The drums: noise (default) or samples (a sampled kick and snare on the DPCM channel, the "
					"hi-hats on the noise), as add_drums.", std::vector<std::string>{ "noise", "samples" }))
				.ClipChoice(),
			false, true);
	}

	AddTool("copy_bars", "Repeat bars",
		"Copies bars to another place in the song, for a song's form (A A B A, a verse coming back): every channel by "
		"default, a number of times, moved up or down if asked. The copy plays with the instruments and volumes the "
		"original did. The song's own speed changes, jumps and ends stay where they are, so copies past the end of its loop "
		"don't play until set_loop moves the loop (the reply says so). Vary the repeat afterwards (vary_part, a "
		"counter-melody, busier drums) so it doesn't sound pasted.",
		Schema()
			.Add("from_bar", BarProp("First bar to copy."), true)
			.Add("to_bar", BarProp("Last bar to copy (default from_bar)."))
			.Add("at_bar", BarProp("Where the copy starts."), true)
			.Add("times", Prop("integer", "How many copies, one after another (default 1)."))
			.Add("channels", ChannelListProp("A channel, a list, or \"all\" (default)."))
			.Add("semitones", Prop("integer", "Move the copy's notes up or down (noise and samples stay), -24 to 24."))
			.ClipChoice(),
		false, true);

	AddTool("vary_part", "Vary a part",
		"Changes a melodic part a little, for a repeat that shouldn't sound pasted: embellish (passing notes between notes "
		"a third apart and a neighbour note on held ones, at most two a bar), simplify (only the notes on the beats), "
		"syncopate (notes on the middle of a bar, and every other bar's first beat, come half a beat early), octave_up, "
		"octave_down, ending (the last note goes to the key's tonic and holds to the end).",
		Schema()
			.Add("channel", ChannelProp("Channel name or number."), true)
			.Add("variation", EnumProp("The variation.", std::vector<std::string>{ "embellish", "simplify", "syncopate", "octave_up",
				"octave_down", "ending" }), true)
			.Add("from_bar", BarProp("First bar."), true)
			.Add("to_bar", BarProp("Last bar (default: the song's end)."))
			.Add("key", Prop("string", "Key, like \"C major\". Default: found from the notes."))
			.ClipChoice(),
		false, true);

	AddTool("soundtrack_notes", "Soundtrack notes",
		std::string("Reads or adds to the soundtrack's notes: a text file next to the song (\"") + SOUNDTRACK_NOTES + "\"), shared "
		"by every module in its folder, that keeps the user's taste and what you decided together between sessions: sounds "
		"and styles they liked or didn't, keys and tempos of places, motifs for characters. Read it before writing for a "
		"song that has one (get_song says so); add a line whenever the user tells you what they like, dislike or want.",
		Schema()
			.Add("action", EnumProp("read (default), add (a line), or replace (all of it).", std::vector<std::string>{ "read", "add", "replace" }))
			.Add("text", Prop("string", "What to add, like \"likes bright leads with vibrato; no echo on the melody\", or the "
				"whole new text.")),
		false);

	AddTool("study_nsf", "Study an NSF's tracks",
		"Studies tracks of an NSF file the user has (game music, ripped as .nsf or .nsfe; track names come from a .m3u "
		"playlist of the same name next to it) and keeps each as a reference in the user's library, for every song: how it's "
		"made, in words, from its notes: tempo and form, key and the chords bar by bar, each channel's role, register, "
		"rhythm (its commonest bar as a grid) and intervals, the bass's figure, the drums' grooves, the tricks between "
		"channels (an echo a row behind, harmony in thirds, doubling, notes traded), the techniques in the sounds (duty "
		"clicks, attacks that bloom, echoes in the envelope, stepped decays, scoops, pitch flicks, vibrato with its delay, "
		"triangle thumps) and the main sounds with their sequences, which reference_sound turns into instruments. Melodies "
		"aren't kept. tracks \"list\" says which tracks are music, jingles or sound effects.",
		Schema()
			.Add("path", Prop("string", "The NSF file, as Windows writes it: C:\\Users\\Name\\Music\\game.nsf."), true)
			.Add("tracks", Prop("string", "Which: \"music\" (default: every track that's music, not a jingle or sound effect), "
				"\"all\", numbers from 1 like \"1,3-5\", or \"list\" to see what the tracks are first."))
			.Add("tags", WordListProp("Words to find these references by later, like [\"action\", \"boss\"]."))
			.Add("notes", Prop("string", "A note to keep with them, like \"the user loves the bass in this one\"."))
			.Add("save", Prop("boolean", "Keep them in the library (default true).")),
		false);

	AddTool("study_module", "Study a tracker module or MIDI file",
		"Studies a music module from a PC tracker (MOD, S3M, XM or IT) or a MIDI file (.mid, .midi, .rmi) that the user has, "
		"for how it's made, from its notes alone (a module's samples aren't read), and keeps it as a reference in the user's "
		"library, as study_nsf does for NSF tracks: tempo and form, key and the chords bar by bar, each channel's role, "
		"register, rhythm (its commonest bar as a grid), intervals and instruments by name, the bass's figure, the drums' "
		"grooves (drums told by their instruments' names, or General MIDI's drum keys), the tricks between channels and the "
		"techniques written into the patterns (arpeggios, vibrato, portamento, volume slides). A MIDI file's parts become "
		"channels of a note at a time (a piano's chords spread over several), named after its tracks or its General MIDI "
		"instruments, and its bars follow its meter (3/4, 6/8...). A FamiTracker module (.ftm, 0.2 to 0.4.6) is studied a "
		"track at a time from its own patterns and instruments, as written: its speed and tempo (and a swing or lilt from "
		"speeds that alternate), its echoes (also an echo in places), chords as arpeggio instruments, delays (Gxx), duty "
		"changes (Vxx), detune (Pxx), the DPCM level (Zxx), each channel's volumes, and its sounds with their sequences, which "
		"reference_sound brings into a song. Use it when the user wants music like a module or MIDI file of theirs: the study "
		"is for writing something of your own in its style, never a copy of its notes, and a module's many channels boil "
		"down to the NES's few.",
		Schema()
			.Add("path", Prop("string", "The module or MIDI file, as Windows writes it: C:\\Users\\Name\\Music\\song.xm, "
				"song.mid or song.ftm."), true)
			.Add("tracks", Prop("string", "A FamiTracker module's tracks: numbers from 1 like \"1,3-5\" (default all, at most 24)."))
			.Add("tags", WordListProp("Words to find it by later, like [\"chiptune\", \"upbeat\"]."))
			.Add("notes", Prop("string", "A note to keep with it, like \"the user wants a NES track that feels like this\"."))
			.Add("save", Prop("boolean", "Keep it in the library (default true).")),
		false);

	AddTool("references", "The reference library",
		"The user's library of studied tracks (study_nsf and study_module add to it) and the built-in guide to the classic NES styles "
		"(Konami, Capcom, Nintendo: grooves, bass figures, sounds, tricks). When the user wants music like a game, a composer "
		"or a reference, or in a style, search here first and lean on what you find: its tempo and feel, form, harmonic color, "
		"how the channels share the parts, rhythm grids, drum grooves, tricks and sounds. Write your own melodies and chord "
		"progressions; never copy a reference's notes. Actions: list (default), show (a reference's whole study and its "
		"sounds), search (by words: a game, composer, technique, mood or tempo), guide (the built-in guide, or a topic of "
		"it), tag (add or remove tags, set notes), delete.",
		Schema()
			.Add("action", EnumProp("What to do.", std::vector<std::string>{ "list", "show", "search", "guide", "tag", "delete" }))
			.Add("reference", Prop("string", "The reference's id, for show, tag and delete, like \"bucky-o-hare-08\"."))
			.Add("query", Prop("string", "For search: words like \"konami echo\", \"gallop bass\", \"fast minor\", \"mega man\"."))
			.Add("topic", Prop("string", "For guide: a topic, like \"konami\", \"drums\" or \"lead sounds\" (default: the contents)."))
			.Add("tags", WordListProp("For tag: tags to add."))
			.Add("remove_tags", WordListProp("For tag: tags to take off."))
			.Add("notes", Prop("string", "For tag: the reference's notes (replaces them).")),
		false);

	AddTool("effects", "FamiTracker's effects",
		"FamiTracker 0.4.6's pattern effects: what each does, which channels play it, how long it lasts and what it's good "
		"for, and the classic techniques made with them (echo, chorus with Pxx, delayed vibrato, swing with Gxx, staccato with "
		"Sxx, fades and swells, tremolo, sweeps, duty changes, slides and grace notes, arpeggio chords, a triangle kick, DPCM "
		"tricks, noise drums). Only 0.4.6's effects play here: Lxx, Mxy, Oxx and Txy come from later FamiTrackers, and "
		"write_notes refuses them. With nothing, the overview.",
		Schema()
			.Add("effect", Prop("string", "One effect, like \"Q\", \"Qxy\" or \"4\"."))
			.Add("technique", Prop("string", "A technique, like \"swing\" or \"triangle kick\"."))
			.Add("channel", ChannelProp("A channel: the effects it plays (with effect: H, I and J on the FDS are its modulator).")),
		true);

	AddTool("reference_sound", "Use a reference's sound",
		"Adds one of a reference's sounds to the song as an instrument, with the original's exact envelopes (volume, arpeggio, "
		"pitch, duty): a lead with its duty click and echo, a bass, a drum. references show lists a reference's sounds by "
		"number. The duty is translated between the 2A03 and the VRC6.",
		Schema()
			.Add("reference", Prop("string", "The reference's id."), true)
			.Add("sound", Prop("integer", "The sound's number in the reference (default 1)."))
			.Add("channel", ChannelProp("The channel it's for (picks the chip: 2A03, MMC5 or VRC6)."))
			.Add("name", Prop("string", "The instrument's name (default: the track's and the sound's role, like \"Magma Tanker lead\").")),
		false, false, true);

	AddTool("add_sample_drums", "Add sampled drums",
		"Adds a drum kit of samples for the DPCM channel, made for FamiTracker: a punchy kick, a snare and two toms, as one "
		"instrument (\"Sample drums\"). Then write_notes on the DPCM channel plays them by name (kick, snare, low tom, high "
		"tom). A sampled kick and snare under the noise channel's hi-hats is how many classic NES soundtracks get their punch "
		"(Konami's, Nintendo's); add_drums and make_groove with kit samples write a whole drum part that way. The samples "
		"start from silence, so the triangle stays loud (mix keeps a softer DPCM level on each of them).",
		Schema(), false, false, true);

	AddTool("import_nsf", "Open an NSF's tracks",
		"Opens tracks of an NSF file in FamiTracker as a new module, a track each, with the patterns, instruments and drums "
		"worked out from the music, to look at closely or play. It replaces the open song, so that must be saved first.",
		Schema()
			.Add("path", Prop("string", "The NSF file, as Windows writes it."), true)
			.Add("tracks", Prop("string", "Which: \"music\" (default), \"all\", or numbers from 1 like \"1,3-5\" (at most 64).")),
		false, true);

	AddTool("review_arrangement", "Review the arrangement's depth",
		"What makes the song sound plain, as a musician would hear it: which parts play (melody, chords, bass, drums, a "
		"second line) and which are missing, bars where only one or two play, the chords (how many, plain triads or "
		"colored, the same loop all the way), a bass that only holds roots, drums without fills, a melody with one note "
		"length or no rests, phrases and whole sections that repeat unchanged, flat dynamics, parts crowding the "
		"melody's register, a bass in octaves with the melody, and the length. It scores the depth out of 10 and lists "
		"what would help most, with the tool for each. Run it after a draft and before telling the user it's done. A calm "
		"track (a night version, ambient music) is meant to float: style \"calm\" asks no drums, fills or accents of it.",
		Schema()
			.Add("key", Prop("string", "Key, like \"C major\". Default: found from the notes."))
			.Add("style", EnumProp("any, or calm (also \"ambient\"): no tips for drums, fills or accents. Default: calm when the "
				"track's name says so, like \"Town (night)\".", std::vector<std::string>{ "any", "calm", "ambient" })).Range(),
		true, false, true);

	AddTool("check_song", "Check the song",
		"Checks what you can't hear: notes outside the key, two channels clashing a semitone apart, notes outside a channel's "
		"range, very busy bars, silent bars and unused channels. Run it after writing.",
		Schema().Add("key", Prop("string", "Key to check against. Default: found from the notes.")).Range(), true, false, true);

	AddTool("look_at_song", "Look at the notes",
		"A picture of the song's notes for you to look at: a piano roll with time left to right (bars numbered on top) and "
		"pitch bottom to top (each C labeled), each channel in its own color (the reply names them), drums and samples in "
		"strips below, arpeggio chords as faint bars, the loop marked. Use it to see a melody's shape, gaps, crowded or empty "
		"spots and how the parts fit together, and to check what you wrote. Up to 32 bars at once; 8 to 16 read best.",
		Schema()
			.Add("from_bar", BarProp("First bar (from 1). Default 1."))
			.Add("to_bar", BarProp("Last bar, included. Default: 8 bars from from_bar."))
			.Add("channels", ChannelListProp("A channel, a list of channels, or \"all\" (default: every channel).")),
		true, false, true);

	AddTool("play", "Play for the user",
		"Plays the song in FamiTracker for the user, from a bar, for some bars or to the end; over and over (loop) for listening "
		"closely, and with only some channels (channels) or without some (mute), to hear a part on its own or the rest without "
		"it. The user's own mutes come back when it stops. You can't hear it; ask the user.",
		Schema().Add("from_bar", BarProp("Bar to start at (default 1).")).Add("bars", Prop("integer", "How many bars (default: to the end)."))
			.Add("loop", Prop("boolean", "Play them over and over until stop (default false)."))
			.Add("channels", ChannelListProp("Only these channels play this time (the bass and drums, a lead alone)."))
			.Add("mute", ChannelListProp("These channels don't play this time.")),
		true);

	AddTool("stop", "Stop playing", "Stops playback in FamiTracker.", Schema(), true, false, true);

	AddTool("undo", "Undo",
		"Undoes the last change in the song, like Ctrl+Z in FamiTracker, whoever made it. Needs direct changes.",
		Schema(), false, true);

	AddTool("save_song", "Save the song",
		"Saves the song to its file, or a copy next to it (copy_name) so the user can compare versions.",
		Schema().Add("copy_name", Prop("string", "File name for a copy, like \"Town theme darker\". Default: save the song itself.")),
		false);
}

bool ToolSet::Has(const std::string &Name) const
{
	for (size_t i = 0; i < m_Tools.size(); ++i)
		if (m_Tools[i].Name == Name)
			return true;
	return false;
}

ToolResult ToolSet::Call(const std::string &Name, const Json &Arguments)
{
	ToolResult Result;
	const Json Args = Arguments.IsObject() ? Arguments : Json::MakeObject();
	try {
		Session S(m_Host);
		if (Name == "get_song") Result.Text = SongOverview(S, Args);
		else if (Name == "read_notes") Result.Text = ReadChannel(S, Args);
		else if (Name == "write_notes") Result.Text = WriteNotes(S, Args);
		else if (Name == "place_clip") Result.Text = PlaceClipTool(S, Args);
		else if (Name == "add_instrument") Result.Text = AddInstrumentTool(S, Args);
		else if (Name == "design_instrument") Result.Text = DesignInstrumentTool(S, Args);
		else if (Name == "set_instrument") Result.Text = SetInstrumentTool(S, Args);
		else if (Name == "transpose") Result.Text = TransposeTool(S, Args);
		else if (Name == "set_tempo") Result.Text = SetTempoTool(S, Args);
		else if (Name == "set_swing") Result.Text = SetSwingTool(S, Args);
		else if (Name == "mix") Result.Text = MixTool(S, Args);
		else if (Name == "add_chords_and_bass") Result.Text = AddBackingTool(S, Args);
		else if (Name == "add_drums") Result.Text = AddDrumsTool(S, Args);
		else if (Name == "add_harmony") Result.Text = AddHarmonyTool(S, Args);
		else if (Name == "add_counter_melody") Result.Text = AddCounterMelodyTool(S, Args);
		else if (Name == "polish") Result.Text = PolishTool(S, Args);
		else if (Name == "review_arrangement") Result.Text = ReviewTool(S, Args);
		else if (Name == "make_groove") Result.Text = MakeGrooveTool(S, Args);
		else if (Name == "copy_bars") Result.Text = CopyBarsTool(S, Args);
		else if (Name == "vary_part") Result.Text = VaryPartTool(S, Args);
		else if (Name == "soundtrack_notes") Result.Text = SoundtrackNotesTool(S, Args);
		else if (Name == "study_nsf") Result.Text = StudyNsfTool(S, Args);
		else if (Name == "study_module") Result.Text = StudyModuleTool(S, Args);
		else if (Name == "references") Result.Text = ReferencesTool(S, Args);
		else if (Name == "effects") Result.Text = EffectsTool(S, Args);
		else if (Name == "reference_sound") Result.Text = ReferenceSoundTool(S, Args);
		else if (Name == "import_nsf") Result.Text = ImportNsfTool(S, Args);
		else if (Name == "add_sample_drums") Result.Text = AddSampleDrumsTool(S, Args);
		else if (Name == "add_echo") Result.Text = AddEchoTool(S, Args);
		else if (Name == "add_vibrato") Result.Text = AddVibratoTool(S, Args);
		else if (Name == "change_mood") Result.Text = ChangeMoodTool(S, Args);
		else if (Name == "check_song") Result.Text = CheckTool(S, Args);
		else if (Name == "look_at_song") Result.Text = LookTool(S, Args, Result.Image);
		else if (Name == "make_version") Result.Text = MakeVersionTool(S, Args);
		else if (Name == "make_jingle") Result.Text = MakeJingleTool(S, Args);
		else if (Name == "make_sound_effect") Result.Text = MakeSoundEffectTool(S, Args);
		else if (Name == "select_track") Result.Text = SelectTrackTool(S, Args);
		else if (Name == "set_expansion") Result.Text = SetExpansionTool(S, Args);
		else if (Name == "check_loop") Result.Text = CheckLoopTool(S, Args);
		else if (Name == "set_loop") Result.Text = SetLoopTool(S, Args);
		else if (Name == "insert_bars") Result.Text = InsertBarsTool(S, Args);
		else if (Name == "keep_version") Result.Text = KeepVersionTool(S, Args);
		else if (Name == "compare_versions") Result.Text = CompareVersionsTool(S, Args);
		else if (Name == "remove_bars") Result.Text = RemoveBarsTool(S, Args);
		else if (Name == "export_for_game") Result.Text = ExportTool(S, Args);
		else if (Name == "play") Result.Text = PlayTool(S, Args);
		else if (Name == "stop") {
			S.Host().Stop();
			Result.Text = "Stopped.";
		}
		else if (Name == "undo") Result.Text = UndoTool(S, Args);
		else if (Name == "save_song") Result.Text = SaveTool(S, Args);
		else {
			Result.Text = "Unknown tool: " + Name;
			Result.IsError = true;
		}
	}
	catch (const Problem &P) {
		Result.Text = P.Text;
		Result.IsError = true;
	}
	return Result;
}

std::string ToolSet::Instructions()
{
	return
		"These tools work on the song open in the user's FamiTracker (NES music: 2A03 channels, often with the VRC6 expansion). "
		"Think in bars and beats (both count from 1) and note names with FamiTracker's octaves: C-3 is middle C, A-3 is 440 Hz. "
		"Start with get_song, then read_notes for the parts you change; look_at_song shows the notes as a picture. "
		"By default your changes arrive as clips the user listens to and places from the clips panel; if the user allowed direct "
		"changes, they go into the song and each one is undone with Ctrl+Z. You can't hear: run check_song after writing, "
		"look at the result with look_at_song, review_arrangement for depth, and use play so the user can listen, then ask them.\n"
		"Write music that sounds composed, not generated. Users notice plain tracks: one chord a bar held as an arpeggio, a bass "
		"on roots, no drums, one note length, sections pasted unchanged.\n"
		"- Plan first: the mood gives the key and mode (major or lydian bright; minor, dorian sad or mysterious; phrygian or "
		"harmonic minor for danger), the tempo and a form of 16 to 32 bars or more with an A part and a contrasting B (A A B A, "
		"intro A B A'). Game music is heard for minutes, so it needs somewhere to go. Read soundtrack_notes when get_song "
		"mentions them, and keep to the user's taste.\n"
		"- Layers: an NES arrangement has four or five: melody (a pulse), chords (the other pulse), bass (triangle), drums "
		"(noise), and a second line (a counter-melody, a harmony or an echo) where the melody leaves room. make_groove starts a "
		"backing for a mood; add_chords_and_bass plays chords you choose (\"| Am7 | Fmaj7 | C | G7sus4 G7 |\" or \"i VI III VII\") "
		"in a style; choose its chord_style, rhythm or pattern, color and bass_style for the mood, since the defaults are the "
		"plainest. add_drums adds a groove with fills; add_counter_melody and add_harmony add the second line.\n"
		"- Melody: a motif of two to four notes with a rhythm you'd recognize, repeated and developed (moved up a step, "
		"inverted, extended); phrases of two or four bars that breathe (a held note or a rest at the end); chord tones on "
		"strong beats and steps between them; one high point in each section; long and short notes mixed (dotted rhythms, "
		"syncopation). Not a scale run, not eighth notes without a rest. For warmth, a pulse lead lives mostly in octave 3 "
		"(C-3 to C-4) and reaches into octave 4 at its high points: higher, the pulses turn thin and the warm middle of the "
		"sound is left empty.\n"
		"- Harmony with direction: tonic, subdominant, dominant, home; a B part on other chords (IV, vi, the relative key); "
		"sevenths, added ninths and suspensions resolving into V (color sevenths or rich); chords changing about once a bar, "
		"not every beat. Bass: walking for towns and jazz, octaves for action, syncopated for pop, arpeggio for adventure. "
		"Drums: lighter in calm parts, busier toward a climax, a fill at each phrase end.\n"
		"- Expression: polish adds accents and a vibrato that comes in on long notes; add_echo gives space; change a sound or "
		"the duty between sections; instruments with life (the singing lead, strings and brass presets, or design_instrument "
		"for a sound described in words). The effects tool explains FamiTracker 0.4.6's effects and the techniques made with "
		"them (swing, a chorus with Pxx, staccato with Sxx, a kick on the triangle, lush chords, quiet mixing); only 0.4.6's "
		"effects play, so none of the Lxx, Mxy, Oxx or Txy of later FamiTrackers. With the N163 (set_expansion), up to 8 more "
		"channels play wave sounds whose timbre moves (an organ's rotating shimmer, strings and a choir that swell, brass "
		"opening up, an electric piano and a bell that mellow, a pulse lead that sweeps, a bass that growls): rich chords "
		"spread over several channels, each voice its own note.\n"
		"- Depth and atmosphere, the warm and nostalgic sound of modern FamiTracker music: a soft mix (mix: the melody's "
		"loudest at 9, echoes at 4, the triangle softened by the DPCM level), an echo under nearly every melodic line (add_echo "
		"a beat later, volume 6, detune 1), chords as lush arpeggio instruments with sevenths and ninths (add_chords_and_bass "
		"chord_style lush; make_groove nostalgic; on the VRC7, chord_style voices with its electric piano across FM "
		"channels), a swing from alternating speeds (set_swing), a vibrato that waits on long notes, fades on held notes, "
		"scoops after leaps and falls where phrases end (polish touches fades, scoops and falls), a bouncing bass (touch "
		"staccato) or a punchy one (bass_instrument "
		"\"thump bass\": an octave blip into each triangle note), a sampled kick and snare under the noise channel's hi-hats "
		"(add_drums or make_groove with kit samples), and VRC6 bells and pianos or VRC7 electric pianos. references action "
		"guide topic atmospheric has the numbers.\n"
		"- Repeats: copy_bars, then make the repeat different (vary_part, a counter-melody or harmony only the second time, "
		"busier drums, another chord style). insert_bars makes room for a new section (an intro, a bridge, a break) and "
		"remove_bars cuts bars; the rest of the song and its loop move along.\n"
		"- Let it breathe: well-arranged tracks swing 8-12 dB in loudness between sections. Open with an intro of one or two "
		"quiet parts (the chords alone, the bass joining later; insert_bars at bar 1, copy_bars with channels, set_loop from "
		"the bar after so it plays once), drop parts for a break before a repeat, and keep the full sound for where it matters. "
		"The triangle is loud, so bars with it stay loud. review_arrangement estimates how far the song swings. On the VRC7 a "
		"volume step is 3 dB (9 is 18 dB under 15): let mix set the levels.\n"
		"- Check before you call it done: review_arrangement scores the depth and lists what's missing; aim for 8 out of 10. "
		"When you're unsure between two ideas, offer both as clips (as_clip with a clip_name) and let the user pick. Keep what "
		"the user tells you they like or don't in soundtrack_notes.\n"
		"For a mood, use change_mood, which makes all of its changes together (separate tools would make separate clips that "
		"each start from the original): darker = to_mode minor or phrygian, about 10-20% slower, parts an octave lower, rounder "
		"or softer sounds (warm pad, soft lead); happier = to_mode major or lydian, a bit faster, brighter sounds (bright lead, "
		"bell, pluck); underwater = slower, a slow and strong vibrato on the lead, an echo, soft round sounds, a lower register. "
		"A night version (or another version of a song for a game state, like a tense one for a battle) is make_version, which "
		"adds it as a new track. Night is much more than slower: the parts that sat high move down an octave, fast runs thin "
		"to their outline, notes hold into each other without accents, chords become a held pad that swells in, the bass plays "
		"long notes, the drums fade to a soft pulse or stop, the lead swells in with a slower and wider vibrato, and a longer, "
		"softer echo adds space; make_version \"night\" does all of that by itself, and chords \"richer\" adds sevenths. A jingle (level up, quest complete, item found...) is make_jingle, and a sound effect (jump, "
		"coin, menu blip, \"pew\", wind, birds...) make_sound_effect, which also builds a sound described in words from layers "
		"(a bird flying off: wing flaps as noise hits fading away, and a pulse chirping up) and loops ambience. For a VRC6, VRC7 or MMC5 song, switch the chip with set_expansion "
		"first (it asks before clearing notes on the old chip's channels); VRC7 channels play the chip's own sounds by name "
		"(piano, flute, brass...). set_tempo takes a BPM, or FamiTracker's speed and tempo as the user names them. Tell the "
		"user what you changed and why.\n"
		"References: the user keeps a library of studied tracks (study_nsf studies NSF files they have, study_module their "
		"MOD, S3M, XM and IT modules and MIDI files; references lists, shows and searches them), and references action guide "
		"is a built-in "
		"guide to the classic NES styles. When the user "
		"wants music like a game, a composer or a track, or in a style (Konami, Capcom, Mega Man...), or says to use the "
		"references, search the library and the guide first and plan from what you find: the tempo and feel, the form, the "
		"harmonic color (modes, borrowed chords, how often chords change), how the channels share the parts (lead, echo, "
		"harmony, bass, drums), the rhythm grids and bass figures, the drum grooves, the tricks and the sounds. Take the craft, "
		"not the tunes: write your own melodies, motifs and chord progressions, and never copy a reference's notes or its exact "
		"progression. reference_sound gives a song a reference's instrument as it was (its duty click, bloom, echo, vibrato); "
		"design_instrument makes your own in the same spirit; add_drums with kit samples (or add_sample_drums and write_notes) "
		"gives the DPCM channel a sampled kick, snare and toms for the punch of those soundtracks. Tell the user which "
		"references you leaned on and how.";
}

const std::vector<PromptInfo> &ToolSet::Prompts()
{
	static std::vector<PromptInfo> PROMPTS;
	if (PROMPTS.empty()) {
		PromptInfo P;
		P.Name = "compose_track";
		P.Title = "Compose a track";
		P.Description = "Compose a track for a game scene or a mood, step by step, with the depth of a real arrangement.";
		PromptArgument Idea = { "idea", "What it's for: a place, a scene or a mood, like \"a snowy mountain village at night\".", true };
		PromptArgument Bars = { "bars", "How long, in bars (default 32).", false };
		P.Arguments.push_back(Idea);
		P.Arguments.push_back(Bars);
		PROMPTS.push_back(P);

		P = PromptInfo();
		P.Name = "deepen_track";
		P.Title = "Give the track more depth";
		P.Description = "Review the song as an arranger would and work through what makes it sound plain.";
		PromptArgument Where = { "bars", "Which bars, like \"9-16\" (default: the whole song).", false };
		P.Arguments.push_back(Where);
		PROMPTS.push_back(P);

		P = PromptInfo();
		P.Name = "make_versions";
		P.Title = "Offer versions to choose from";
		P.Description = "Make two or three versions of a part as clips, so the user can listen and pick.";
		PromptArgument Part = { "part", "What to try versions of: the chords, the bass, the drums, the melody's sound, a section...", true };
		PromptArgument Count = { "count", "How many (default 3).", false };
		P.Arguments.push_back(Part);
		P.Arguments.push_back(Count);
		PROMPTS.push_back(P);

		P = PromptInfo();
		P.Name = "study_track";
		P.Title = "Study a track";
		P.Description = "Explain what makes the open track work (form, harmony, melody, bass, drums, sounds), to reuse its ideas.";
		PromptArgument Track = { "track", "The track's number or name (default: the selected one).", false };
		P.Arguments.push_back(Track);
		PROMPTS.push_back(P);
	}
	return PROMPTS;
}

bool ToolSet::PromptText(const std::string &Name, const Json &Arguments, std::string &Text, std::string &Error)
{
	auto Value = [&](const char *Key, const std::string &Default) {
		const Json *pValue = Arguments.IsObject() ? Arguments.Get(Key) : 0;
		if (pValue == 0 || !pValue->IsString() || Trim(pValue->String()).empty())
			return Default;
		return Trim(pValue->String());
	};
	if (Name == "compose_track") {
		const std::string Idea = Value("idea", "");
		if (Idea.empty()) {
			Error = "compose_track needs the idea: what the track is for.";
			return false;
		}
		Text = "Compose a track in FamiTracker for this: " + Idea + ". Make it sound composed by a person, with depth. "
			"Work through these steps with the FamiTracker tools:\n"
			"1. get_song; if it mentions soundtrack notes, read them with soundtrack_notes and keep to them.\n"
			"2. Decide, and tell me in a few lines: the key and mode, the tempo, and the form for " + Value("bars", "32") + " bars "
			"(an A part and a contrasting B, like A A B A, or intro A B A'), and what each channel plays.\n"
			"3. Start the backing: make_groove with the nearest mood, or add_chords_and_bass with chords you choose and a chord_style, "
			"color and bass_style that fit (not the defaults). Set the tempo with set_tempo.\n"
			"4. Write the melody with write_notes, phrase by phrase: a short motif with a rhythm you'd recognize, repeated and "
			"developed; phrases of two or four bars that end on a held note or a rest; chord tones on strong beats; one high point "
			"in each section; long and short notes mixed.\n"
			"5. Build the form: copy_bars for repeats and make each repeat different (vary_part, a counter-melody or harmony only "
			"the second time, busier drums); give the B part other chords (add_chords_and_bass with chords and from_bar). Let "
			"it breathe, as well-arranged tracks swing 8-12 dB in loudness between sections: an intro of 2-4 bars with one or "
			"two quiet parts, like the chords alone with the bass joining for the last bar or two (insert_bars at bar 1, "
			"copy_bars with channels to fill it, set_loop from the bar after so it plays once; the triangle is loud, so an "
			"intro with it is only a few dB quieter), a few bars without the drums or the melody before a repeat, and the "
			"full sound where it matters.\n"
			"6. Add depth: add_counter_melody or add_harmony where the melody leaves room, add_drums with fills if the scene has a "
			"beat (kit samples for a punchy sampled kick and snare), polish (fades, scoops and a staccato or thump bass where they "
			"fit); give the lead a sound with life (the singing lead, strings or brass presets, or design_instrument), and an echo "
			"(add_echo, detune 1) where it leaves room.\n"
			"7. Finish the sound: set_swing if the feel should swing, then mix (soft) last, so the triangle and the drums come "
			"through and the pulses don't harshen.\n"
			"8. review_arrangement, and fix what it lists until it scores 8 or more; check_song for clashes; look_at_song to see it.\n"
			"9. check_loop and set_loop so it loops cleanly, then play it for me and ask what I'd change. Keep what I like in "
			"soundtrack_notes.";
		return true;
	}
	if (Name == "deepen_track") {
		const std::string Bars = Value("bars", "");
		Text = "Give the FamiTracker song more depth" + (Bars.empty() ? std::string() : " in bars " + Bars) + ", the way an "
			"arranger would:\n"
			"1. get_song and review_arrangement" + (Bars.empty() ? std::string() : " (from_bar and to_bar for those bars)") + ".\n"
			"2. Work through its list in order, one change at a time: missing layers first (chords, bass, drums, a second line), "
			"then variety (chord colors and styles, a bass that moves, fills, repeats that change), then breathing (an intro on "
			"the chords and bass, a break with fewer parts), then expression (polish, an echo, a sound that changes between "
			"sections, sampled drums), and mix last. Keep my melody unless the review points at it.\n"
			"3. Run review_arrangement again after each round until it scores 8 or more.\n"
			"4. Play the changed bars for me, say what you changed and why, and ask what I think.";
		return true;
	}
	if (Name == "make_versions") {
		const std::string Part = Value("part", "");
		if (Part.empty()) {
			Error = "make_versions needs the part to try versions of.";
			return false;
		}
		Text = "Make " + Value("count", "3") + " versions of " + Part + " in the FamiTracker song for me to choose from. Make each "
			"one a clip (as_clip true, with a clip_name that says what's different, like \"Chords: broken, rolling\"), changing "
			"one thing at a time: a chord style or rhythm, a bass style, a groove or how busy it is, a color, a sound. Then tell "
			"me how they differ and that each clip card's play button plays it, ask which I like, and put my choice in the song "
			"(place_clip when direct changes are allowed). Note what I chose with soundtrack_notes.";
		return true;
	}
	if (Name == "study_track") {
		const std::string Track = Value("track", "");
		Text = std::string("Study ") + (Track.empty() ? "the selected track" : "track " + Track + " (select_track first)") +
			" in FamiTracker as an arranger would, and explain what makes it work so its ideas can go into new music:\n"
			"1. get_song, review_arrangement, and look_at_song for a few sections.\n"
			"2. read_notes for each part over a phrase or two.\n"
			"3. Describe the form (sections, repeats and how they change), the harmony (progressions, colors, how often chords "
			"change), the melody (motifs, phrase lengths, range, rhythm), the bass and drums (patterns, fills), how the parts share "
			"the channels (who plays what, registers, call and response), the effects and sounds, and the tempo and feel.\n"
			"4. End with a short list of techniques to reuse, and offer to keep them in soundtrack_notes.";
		return true;
	}
	Error = "No prompt called \"" + Name + "\".";
	return false;
}

}	// namespace ftapi

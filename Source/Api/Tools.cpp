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
#include "GameAudio.h"
#include "Jingles.h"
#include "Music.h"
#include "Ogg.h"
#include "Picture.h"
#include "Presets.h"
#include "Song.h"
#include "SoundEffects.h"
#include "../Transcribe/Transcribe.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>

namespace ftapi {

namespace {

const int MAX_READ_BARS = 32;
const int MAX_PICTURE_BARS = 32;
const int MAX_LISTED = 15;

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
		m_Cache.clear();
	}

	// After instruments were added; the cells stay as they are
	void Refresh()
	{
		std::string Error;
		SongInfo Info;
		if (m_Host.GetSong(Info, Error))
			Song = Info;
	}

	SongHost &Host() { return m_Host; }

	const std::vector<Cell> &Cells(int Channel)
	{
		std::map<int, std::vector<Cell> >::iterator it = m_Cache.find(Channel);
		if (it == m_Cache.end()) {
			std::vector<Cell> Read;
			m_Host.ReadCells(Channel, 0, Time.TotalRows(), Read);
			Read.resize(Time.TotalRows());
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
	std::map<int, std::vector<Cell> > m_Cache;
	std::map<int, std::vector<NoteInfo> > m_Notes;
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
	// VRC7 channels play the chip's own patches (a preset's name picks the one most like it)
	if (Chip == "VRC7") {
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
			"name, like \"soft lead\").", Wanted.c_str(), Names.empty() ? "none" : Names.c_str(), Vrc7PatchList().c_str()));
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
			const std::vector<transcribe::MelodyNote> Part = MelodyOf(S.Notes(c), Pass == 0 ? First : 0,
				Pass == 0 ? End : S.Time.TotalRows());
			All.insert(All.end(), Part.begin(), Part.end());
		}
		if (All.size() >= 4 || (Pass == 1 && !All.empty())) {
			std::sort(All.begin(), All.end(), [](const transcribe::MelodyNote &a, const transcribe::MelodyNote &b) { return a.row < b.row; });
			const transcribe::Key K = transcribe::FindKey(All);
			Out = Key(K.tonic, K.minor ? MODE_MINOR : MODE_MAJOR);
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
int GuessMelodyChannel(Session &S, int First, int End)
{
	for (int Pass = 0; Pass < 3; ++Pass) {
		std::vector<int> Channels, Counts, Middles;
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
				for (size_t e = 0; e < Notes[i].Effects.size(); ++e)
					if (Notes[i].Effects[e].second.first == EFF_ARPEGGIO && Notes[i].Effects[e].second.second != 0) {
						++Chords;
						break;
					}
			if (Pass == 0 && Chords * 3 >= static_cast<int>(Notes.size()))
				continue;
			const int Middle = MiddleOf(Notes, 48) - (Info.Kind == KIND_TRIANGLE ? 12 : 0);
			Channels.push_back(c);
			Counts.push_back(static_cast<int>(Notes.size()));
			Middles.push_back(Middle);
			Lowest = std::min(Lowest, Middle);
		}
		int Best = -1;
		double BestScore = 0.0;
		for (size_t i = 0; i < Channels.size(); ++i) {
			const double Score = Counts[i] * std::pow(2.0, (Middles[i] - Lowest) / 8.0);
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
			Parts.push_back(Part);
		}
		std::string Error;
		if (!S.Host().WriteSong(Parts, First, -1, Error))
			throw Problem(Error.empty() ? "The song couldn't be changed." : Error);
		Result = "Changed the song: " + What + ". One Ctrl+Z in FamiTracker undoes it.";
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
	if (Info.Kind == KIND_DPCM)
		throw Problem("The DPCM channel plays samples; write notes on another channel.");
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
			throw Problem(Which + " needs a pitch, like \"C-4\" or \"F#3\"" + (Noise ? " (on the noise channel: kick, snare, hihat or 0-# to F-#)." : "."));
		std::string Drum;
		if (Noise) {
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
		else if (!Drum.empty())
			Note.Instrument = ResolveInstrument(S, Drum, Channel);
		if (Arg(Item, "volume") != 0)
			Note.Volume = IntArg(Item, "volume", 15, 0, 15);
		const Json *pEffects = Arg(Item, "effects");
		if (pEffects != 0) {
			if (!pEffects->IsArray())
				throw Problem(Which + ": effects should be a list like [\"Q31\"].");
			for (size_t e = 0; e < pEffects->Size(); ++e) {
				int Effect, Param;
				if (!pEffects->At(e).IsString() || !ParseEffect(pEffects->At(e).String(), Effect, Param) || IsSongEffect(Effect))
					throw Problem(Format("%s: effects are written as in FamiTracker, like \"Q31\" or \"421\" (no speed, jump, skip or halt).",
						Which.c_str()));
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
	if (!S.Host().WriteSong(Parts, Row, Linked ? pInfo->Id : -1, Error))
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

	// What changed: the rows from the first changed one (or RangeFirst, when
	// that's earlier) to the last, in every channel that changed. Direct
	// changes also keep the notes after it playing as before. False when
	// nothing changed.
	bool Collect(int RangeFirst, bool DirectChange, std::vector<Change> &Changes, int &First, std::vector<std::string> &Warnings)
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
		const int End = High + 1;
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
			Warnings.push_back("Some effects didn't fit in the channels' visible effect columns and were left out.");
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
	for (size_t i = 0; i < Melodic.size(); ++i) {
		const int Columns = S.Channel(Melodic[i]).EffectColumns;
		if (ByKey)
			MapPitches(D.Cells(Melodic[i]), First, End, Columns, [&](int Pitch) { return MapToKey(Pitch, FromKey, ToKey); }, true, Count,
				Skipped, &Moves);
		else
			MapPitches(D.Cells(Melodic[i]), First, End, Columns, [&](int Pitch) { return Pitch + Semitones; }, false, Count, Skipped, 0);
	}
	std::vector<std::string> Warnings;
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

bool IsBassChannel(const ChannelInfo &Channel)
{
	return IsBass(Channel);
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
		for (size_t t = 0; t < Classes.size(); ++t) {
			if (MelodyRub(Melody, A.Row, A.Row + A.Rows, Classes[t], S.Time.RowsPerBeat) < 1.0)
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
// the chords their 0xy arpeggios sound
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
			if (Arpeggio != 0) {
				H.Bass = false;
				H.PitchClass = (N.Pitch + (Arpeggio >> 4)) % 12;
				Out.push_back(H);
				H.PitchClass = (N.Pitch + (Arpeggio & 15)) % 12;
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
	else if (Given && Range)
		Melody = GuessMelodyChannel(S, S.Time.BarStart(From), S.Time.BarEnd(To));
	else
		Melody = GuessMelodyChannel(S, 0, S.Time.TotalRows());
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

	// The channels: as asked ("none" leaves one out), or free ones
	auto Pick = [&](const char *Name, bool (*Fits)(const ChannelInfo &), const char *Kind) -> int {
		const Json *pValue = Arg(Args, Name);
		if (pValue != 0 && pValue->IsString() && Lowercase(pValue->String()) == "none")
			return -1;
		if (pValue != 0) {
			const int Channel = ParseChannel(S, *pValue, Name);
			if (!Fits(S.Channel(Channel)) || Channel == Melody)
				throw Problem(Format("%s needs %s other than the melody's.", Name, Kind));
			return Channel;
		}
		const int Free = FreeChannel(S, First, End, Melody, Fits);
		if (Free >= 0)
			return Free;
		if (Free == -1000)
			return -1;	// the song has none
		throw Problem(Format("Every %s already plays in those bars. Name one with %s (its notes there are replaced), or \"none\".",
			Kind, Name));
	};
	const int ChordChannel = Pick("chord_channel", IsChordChannel, "a pulse channel");
	const int BassChannel = Pick("bass_channel", IsBassChannel, "the triangle or the sawtooth");
	if (ChordChannel < 0 && BassChannel < 0)
		throw Problem("There's no channel for chords or a bass line.");

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
	std::vector<std::string> Warnings;
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
	if (!ParseColor(StringArg(Args, "color", "plain"), Color))
		throw Problem("color: plain (triads), sevenths or rich.");
	ColorChords(Chords, MelodyNotes, K, R, Color);
	if (Given && !MelodyNotes.empty())
		RubWarnings(S, Chords, MelodyNotes, K, Warnings);

	std::vector<Change> Changes;
	std::string Where;
	const int Style = StyleArg(Args, "chord_style", ChordStyles(), CHORDS_ARPEGGIO);
	const int BassStyle = StyleArg(Args, "bass_style", BassStyles(), BASS_ROOT_FIFTH);
	if (ChordChannel >= 0) {
		const ChannelInfo &Info = S.Channel(ChordChannel);
		ChordPartOptions O;
		O.Style = Style;
		O.Rhythm = StyleArg(Args, "rhythm", ChordRhythms(), -1);
		O.Pattern = StyleArg(Args, "pattern", BrokenPatterns(), BROKEN_UP);
		O.StepRows = std::max(1, R / IntArg(Args, "notes_per_beat", 2, 1, 4));
		O.RowsPerBeat = R;
		// Under the melody: the chords' lowest notes from about an octave and a half below it
		int Top = MelodyNotes.empty() ? 43 : MiddleOf(MelodyNotes, 52) - 7;
		Top = std::max(36, std::min(48, Top));
		if (Style == CHORDS_BROKEN && O.Pattern == BROKEN_ROLLING)
			Top -= 5;	// it spreads up over two octaves
		O.High = Written(Info, Top);
		O.Low = O.High - 12;
		O.RootInBass = BassChannel >= 0;
		for (int c = 0; c < S.ChannelCount() && !O.RootInBass; ++c)
			O.RootInBass = c != ChordChannel && IsBass(S.Channel(c)) && HasNotesIn(S, c, First, End);
		const int DefaultVolume = Style == CHORDS_STABS ? 11 : Style == CHORDS_BROKEN ? 10 : 15;
		O.Volume = IntArg(Args, "chord_volume", DefaultVolume, 1, 15);
		const char *Sound = Style == CHORDS_GUIDE ? "warm pad" : Style == CHORDS_ARPEGGIO ? "soft chords" : "pluck";
		const int Instrument = PartInstrument(S, Args, "chord_instrument", ChordChannel, Sound);
		std::vector<PartNote> Notes = ChordPart(Chords, Spans, O);
		for (size_t i = 0; i < Notes.size(); ++i)
			Notes[i].Instrument = Instrument;
		Change C;
		C.Channel = ChordChannel;
		BuildPart(S.Cells(ChordChannel), First, End, Notes, OptionsFor(S, ChordChannel, DirectChange), C.Cells, Warnings);
		Changes.push_back(C);
		std::string Kind = ChordStyles()[static_cast<size_t>(Style)].Name;
		if (Style == CHORDS_BROKEN)
			Kind += std::string(" (") + BrokenPatterns()[static_cast<size_t>(O.Pattern)].Name + ")";
		else if (O.Rhythm >= 0)
			Kind += std::string(" (") + ChordRhythms()[static_cast<size_t>(O.Rhythm)].Name + ")";
		Where = Kind + " chords on " + Info.FullName();
	}
	if (BassChannel >= 0) {
		const ChannelInfo &Info = S.Channel(BassChannel);
		BassOptions O;
		O.Style = BassStyle;
		O.RowsPerBeat = R;
		O.K = K;
		O.Low = Written(Info, 12);		// C-1 to G-2 as a pulse or the sawtooth plays them
		O.High = Written(Info, 31);
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
	const std::string What = Where + ", " + RangeText(S.Time.BarOf(First), S.Time.BarOf(End - 1)) + ", in " + KeyName(K);
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
};

EchoPlan EchoArgs(Session &S, const Json &Args, int First, int End)
{
	EchoPlan Plan;
	Plan.Source = ChannelArg(S, Args, "channel");
	if (!IsMelodic(S.Channel(Plan.Source)))
		throw Problem("The echo copies a melodic channel's notes.");
	bool Exact;
	Plan.Delay = S.Time.BeatsToRows(NumberArg(Args, "delay_beats", 3.0 / S.Time.RowsPerBeat, 0.01, 16.0), Exact);
	Plan.Volume = IntArg(Args, "volume", 5, 1, 15);
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
	if (Plan.Instrument == NO_INSTRUMENT && InstrumentChip(S.Channel(Plan.Target)) != InstrumentChip(S.Channel(Plan.Source)))
		throw Problem(Format("%s needs %s instruments; give the echo an instrument (a preset works).",
			S.Channel(Plan.Target).FullName().c_str(), InstrumentChip(S.Channel(Plan.Target)).c_str()));
	return Plan;
}

// The echo of the source's notes that start in [First, End) in the draft:
// Delay rows later on the target, quieter (Volume at full loudness, following
// the original's), replacing what the target plays there. It stops at the
// song's end rather than making the song longer (which would change where it
// loops). How many notes.
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
		E.Volume = std::max(1, (N.Volume * Plan.Volume + 7) / 15);
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
	const std::string What = Format("an echo of %s on %s, %s later at volume %d, %s", S.Channel(Plan.Source).FullName().c_str(),
		S.Channel(Plan.Target).FullName().c_str(), S.Time.Length(Plan.Delay).c_str(), Plan.Volume, RangeText(From, To).c_str());
	return WithRemarks(S, Commit(S, Changes, Start, AsClip, StringArg(Args, "clip_name"), What), Warnings);
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
int MakeVibrato(Session &S, Draft &D, const VibratoPlan &Plan, int First, int End, bool &Lost)
{
	const int Columns = S.Channel(Plan.Channel).EffectColumns;
	std::vector<Cell> &Cells = D.Cells(Plan.Channel);
	const int Count = ApplyVibrato(Cells, D.Notes(Plan.Channel), First, End, Plan.Speed, Plan.Depth, Plan.MinRows, Plan.Delay,
		Columns, Lost);
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
		Warnings.push_back("Some notes had no free effect column for the vibrato (show more effect columns in FamiTracker).");
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
	O.RowsPerBeat = R;
	const std::vector<DrumHit> Hits = DrumPart(SongBarSpans(S, First, End), O);
	if (Hits.empty())
		throw Problem("There are no beats in those bars.");
	const int Loudest = IntArg(Args, "volume", 15, 1, 15);

	std::map<std::string, int> Instruments;
	std::vector<PartNote> Notes;
	for (size_t i = 0; i < Hits.size(); ++i) {
		const DrumHit &H = Hits[i];
		if (Instruments.find(H.Sound) == Instruments.end())
			Instruments[H.Sound] = ResolveInstrument(S, H.Sound, Channel);
		PartNote N;
		N.Row = H.Row;
		N.Rows = (i + 1 < Hits.size() ? Hits[i + 1].Row : End) - H.Row;	// each rings until the next
		N.Pitch = H.Pitch;
		N.Instrument = Instruments[H.Sound];
		N.Volume = std::max(1, (H.Volume * Loudest + 7) / 15);
		Notes.push_back(N);
	}
	TidyPart(Notes, First, End);
	std::vector<std::string> Warnings;
	if (HasNotesIn(S, Channel, First, End))
		Warnings.push_back(S.Channel(Channel).FullName() + "'s drums in those bars were replaced.");
	Change C;
	C.Channel = Channel;
	BuildPart(S.Cells(Channel), First, End, Notes, OptionsFor(S, Channel, Direct(S, AsClip)), C.Cells, Warnings);
	std::vector<Change> Changes(1, C);
	static const char *const INTENSITY[] = { "", "light", "", "busy" };
	std::string What = std::string(INTENSITY[O.Intensity]) + (O.Intensity == 2 ? "" : " ") + Grooves()[static_cast<size_t>(O.Groove)].Name +
		" drums on " + S.Channel(Channel).FullName();
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
		if (Instrument != NO_INSTRUMENT)
			Notes[i].Instrument = Instrument;
		else
			for (size_t m = 0; m < Line.size(); ++m)
				if (Line[m].Row == Notes[i].Row)
					Notes[i].Instrument = Line[m].Instrument;
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
	O.Volume = IntArg(Args, "volume", std::max(4, Loudest - 4), 1, 15);
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
		const int Softer = InBar == 0 ? 0 : InBar % R == 0 ? 1 : (2 * InBar) % R == 0 ? 2 : 3;
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
			if (Word != "accents" && Word != "vibrato" && Word != "separate")
				throw Problem("touches: accents, vibrato or separate.");
			What.insert(Word);
		}
	}
	const std::vector<int> Channels = ChannelsArg(S, Args, "channels");
	int Melody = Arg(Args, "melody_channel") != 0 ? ChannelArg(S, Args, "melody_channel") : GuessMelodyChannel(S, First, End);

	Draft D(S);
	std::vector<std::string> Done, Warnings;
	if (What.count("accents")) {
		std::string List;
		for (size_t i = 0; i < Channels.size(); ++i) {
			const int c = Channels[i];
			if (S.Channel(c).Kind == KIND_TRIANGLE || !IsMelodic(S.Channel(c)))
				continue;	// the triangle has no volume
			const int Count = Accents(S, D, c, First, End);
			if (Count > 0)
				List += (List.empty() ? "" : ", ") + S.Channel(c).FullName();
			else if (Count < 0)
				Warnings.push_back(S.Channel(c).FullName() + " already shapes its volumes, so it kept them.");
		}
		if (!List.empty())
			Done.push_back("accents (beats a step softer than a bar's first note, the notes between them softer still) on " + List);
	}
	if (What.count("vibrato") && Melody >= 0 && std::find(Channels.begin(), Channels.end(), Melody) != Channels.end()) {
		VibratoPlan Plan;
		Plan.Channel = Melody;
		Plan.Speed = 5;
		Plan.Depth = 2;
		Plan.MinRows = S.Time.RowsPerBeat * 3 / 2;
		Plan.Delay = std::max(1, S.Time.RowsPerBeat / 2);
		bool Lost = false;
		const int Count = MakeVibrato(S, D, Plan, First, End, Lost);
		if (Lost)
			Warnings.push_back("Some notes had no free effect column for the vibrato (show more effect columns in FamiTracker).");
		if (Count > 0)
			Done.push_back(Format("a gentle vibrato that comes in after half a beat on %s of %s", Plural(Count, "long note", "long notes").c_str(),
				S.Channel(Melody).FullName().c_str()));
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

int EffectCount(const std::vector<NoteInfo> &Notes, int Effect)
{
	int Count = 0;
	for (size_t i = 0; i < Notes.size(); ++i)
		for (size_t e = 0; e < Notes[i].Effects.size(); ++e)
			if (Notes[i].Effects[e].second.first == Effect && Notes[i].Effects[e].second.second != 0) {
				++Count;
				break;
			}
	return Count;
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
	int Bass = -1, Drums = -1;
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
		if (Info.Kind == KIND_DPCM)
			continue;
		if (IsBass(Info) && Bass < 0) {
			Bass = c;
			continue;
		}
		const int Arpeggios = EffectCount(Notes, EFF_ARPEGGIO);
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
		Parts += "; drums on " + S.Channel(Drums).FullName();
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
	if (Drums < 0)
		Tip(1.0, "No drums: add_drums (groove calm for a quiet track, driving for action), unless the track should float without a beat.");
	if (Voices.empty() && Echoes.empty())
		Tip(1.0, "The melody has no company: add_counter_melody (a line of its own) or add_harmony (thirds under it) on " +
			(Free.empty() ? std::string("a channel") : Free[0]) + " adds depth; add_echo makes it spacious.");

	// Thin bars: two parts or fewer
	std::vector<int> Thin;
	for (size_t b = 0; b < Bars.size(); ++b) {
		int Sounding = 0;
		for (size_t p = 0; p < Playing.size(); ++p)
			if (HasNotesIn(S, Playing[p], Bars[b].Row, Bars[b].Row + Bars[b].Rows))
				++Sounding;
		if (Sounding <= 2 && Layers > 2)
			Thin.push_back(S.Time.BarOf(Bars[b].Row));
	}
	if (!Thin.empty() && static_cast<int>(Thin.size()) < BarCount)
		Lines.push_back("Thin: only two parts or fewer play in bars " + BarList(Thin) + ".");

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
		if (Middle < 12)
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
		if (Fills == 0 && BarCount >= 8)
			Tip(1.0, "The drums never change where a phrase ends: add_drums with fill_every 4 (or 8) adds fills and crashes.");
		else if (Most * 10 >= BarCount * 9 && BarCount >= 8)
			Tip(1.0, "The drums repeat one bar almost all the way: add_drums varies every other bar and fills phrase ends.");
	}

	// The melody
	{
		int Low = 1000, High = -1, Rests = 0, LongBare = 0;
		std::set<int> Lengths;
		for (size_t i = 0; i < Tune.size(); ++i) {
			const NoteInfo &N = Tune[i];
			Low = std::min(Low, N.Pitch);
			High = std::max(High, N.Pitch);
			Lengths.insert(N.Rows);
			if (i + 1 < Tune.size()) {
				if (Tune[i + 1].Row - (N.Row + N.Rows) >= R || (N.EndNote != NOTE_NONE && Tune[i + 1].Row - N.Row - N.Rows >= R / 2))
					++Rests;
			}
			bool Expression = false;
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
		if (!Repeats.empty())
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
		Tip(1.0, "Flat dynamics sound typed: polish adds accents (the bar's first note loudest, the notes between beats "
			"softer).");
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
		Lines.push_back("Bars " + BarList(Copies) + " repeat the four bars before them in every part.");
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
		for (int c = 0; c < S.ChannelCount(); ++c) {
			if (IsMelodic(S.Channel(c)) && NoteStartsIn(S, c, First, End) &&
				MapPitches(D.Cells(c), First, End, S.Channel(c).EffectColumns, [&](int Pitch) { return MapToKey(Pitch, FromKey, ToKey); },
					true, Count, Skipped, &Moved))
				Changed = true;
		}
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
			Warnings.push_back("Some notes had no free effect column for the vibrato (show more effect columns in FamiTracker).");
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

enum drum_change { DRUMS_KEEP, DRUMS_SOFTER, DRUMS_SPARSER, DRUMS_HALF, DRUMS_OFF, DRUMS_BUSIER };

// The noise channel calmer: quieter, without the hits between beats
// (sparser), on beats 1 and 3 only (half), or none (off). How many hits went.
int ChangeDrums(Session &S, std::vector<Cell> &Cells, int Columns, int Change)
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
	if (Change != DRUMS_OFF)
		ScaleVolumes(Cells, 60);
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
		for (size_t e = 0; e < Notes[i].Effects.size(); ++e)
			if (Notes[i].Effects[e].second.first == EFF_ARPEGGIO && Notes[i].Effects[e].second.second != 0)
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

	// What's asked for, and for a calm version its own changes where nothing is said
	Json Plan = Args;
	std::vector<std::string> Remarks;
	if (Calm) {
		if (Arg(Args, "tempo_percent") == 0 && Arg(Args, "bpm") == 0)
			Plan.Set("tempo_percent", 80);
		if (Melody >= 0 && Arg(Args, "vibrato") == 0) {
			Json Vibrato = Json::MakeObject();
			Vibrato.Set("channel", Melody + 1);
			Vibrato.Set("speed", "slow");
			Vibrato.Set("depth", "gentle");
			Plan.Set("vibrato", Vibrato);
		}
		if (Arg(Args, "sounds") == 0) {
			Json Sounds = Json::MakeArray();
			for (int c = 0; c < S.ChannelCount(); ++c) {
				if (!HasPresets(S.Channel(c)) || IsBass(S.Channel(c)))
					continue;
				Json Sound = Json::MakeObject();
				Sound.Set("channel", c + 1);
				if (c == Melody)
					Sound.Set("instrument", "soft lead");
				else if (PlaysChords(S, c))
					Sound.Set("instrument", "soft chords");
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
				Json Echo = Json::MakeObject();
				Echo.Set("channel", Melody + 1);
				Echo.Set("to_channel", Target + 1);
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
	static const char *const DRUM_WORDS[] = { "keep", "softer", "sparser", "half", "off", "busier" };
	const std::string DrumWord = Lowercase(StringArg(Args, "drums", Calm ? "sparser" : (Tense ? "busier" : "keep")));
	int Drums = -1;
	for (int i = 0; i < 6; ++i)
		if (DrumWord == DRUM_WORDS[i])
			Drums = i;
	if (Drums < 0)
		throw Problem("drums should be keep, softer, sparser, half, off or busier.");
	const std::string BassWord = Lowercase(StringArg(Args, "bass", Tense ? "driving" : "keep"));
	if (BassWord != "keep" && BassWord != "driving")
		throw Problem("bass should be keep or driving.");
	const int VolumePercent = IntArg(Args, "volume_percent", Calm ? 75 : 100, 10, 100);
	const MoodPlan Changes = ReadMoodPlan(S, Plan, 0, Total);

	std::vector<std::string> Done, Warnings(Remarks);
	Draft D(S);
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
// layer starts, a halt where it ends and C00 after the last. Its number from
// 0; Parts says which channels play what.
int WriteSoundEffect(Session &S, const transcribe::SoundEffect &Effect, const std::string &Name, std::string &Parts)
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
	const int Rows = Effect.length() + 1;
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
	Effect = ChangeSoundEffect(Effect, Changes);

	// Named after the file: pew.wav makes "SFX Pew"
	std::string Base = File.substr(File.find_last_of("/\\") == std::string::npos ? 0 : File.find_last_of("/\\") + 1);
	if (Base.find('.') != std::string::npos)
		Base = Base.substr(0, Base.rfind('.'));
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
		static_cast<double>(std::max(1, S.Song.FrameRate))).c_str()).c_str() : "", SoundChanges(Changes).c_str(),
		GameMakerName(Name + ".ogg").c_str()));
}

std::string MakeSoundEffectTool(Session &S, const Json &Args)
{
	if (Arg(Args, "recording") != 0) {
		if (Arg(Args, "kind") != 0)
			throw Problem("Give kind or recording, not both.");
		return VoiceSoundEffect(S, Args, StringArg(Args, "recording"));
	}
	const std::string Kind = StringArg(Args, "kind");
	if (Kind.empty())
		throw Problem("Say which sound with kind (" + SoundEffectNames() + "), or give a recording of the user's voice.");
	const SoundEffectKind *pKind = FindSoundEffect(Kind);
	if (pKind == 0)
		throw Problem(Format("There's no \"%s\" sound. The kinds: %s. For something else, start from the nearest one and change "
			"it with semitones, length_percent or a variant.", Kind.c_str(), SoundEffectNames().c_str()));
	SoundEffectChanges Changes;
	Changes.Variant = IntArg(Args, "variant", 1, 1, 9);
	Changes.Semitones = IntArg(Args, "semitones", 0, -24, 24);
	Changes.LengthPercent = IntArg(Args, "length_percent", 100, 25, 400);
	Changes.VolumePercent = IntArg(Args, "volume_percent", 100, 10, 200);
	const transcribe::SoundEffect Effect = ChangeSoundEffect(pKind->Make(), Changes);
	std::string Title = pKind->Name;
	Title[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(Title[0])));
	const std::string Name = Arg(Args, "name") != 0 ? Trim(StringArg(Args, "name")) : FreeSoundName(S.Song, "SFX " + Title);
	const std::string Taken = SoundNameProblem(S.Song, Name);
	if (!Taken.empty())
		throw Problem(Taken);

	std::string Parts;
	const int Track = WriteSoundEffect(S, Effect, Name, Parts);
	std::string Changed;
	if (Changes.Variant > 1)
		Changed += Format(" Variant %d: %s.", Changes.Variant, VariantText(Changes.Variant).c_str());
	Changed += SoundChanges(Changes);
	return WithRemarks(S, Format("Made track %d \"%s\": the %s sound (%s), %s long, on %s.%s It plays at speed 1 (a row a tick) and halts at its "
		"end (C00), so export_for_game saves it as a sound that plays once (in GameMaker: audio_play_sound(%s, 0, false)). Its "
		"instruments' sequences shape it, so the user can change it in FamiTracker's instrument editor. FamiTracker shows the new "
		"track now: play it for the user. For other takes, make another with variant 2, 3..., semitones, length_percent or "
		"volume_percent. FamiTracker can't undo adding a track: the user removes one in Module > Module properties.",
		Track + 1, Name.c_str(), pKind->Name.c_str(), pKind->Use.c_str(), Format("%.2f s", Effect.length() / static_cast<double>(
		std::max(1, S.Song.FrameRate))).c_str(), Parts.c_str(), Changed.c_str(), GameMakerName(Name + ".ogg").c_str()));
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
	else if (Chip == "FDS" || Chip == "N163")
		Text += " Wavetable channels: they need " + Chip + " instruments, which you can't make yet; the user makes one in "
			"FamiTracker's instrument editor.";
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
			if (Arpeggio != 0) {
				Weight[(N.Pitch + (Arpeggio >> 4)) % 12] += Rows;
				Weight[(N.Pitch + (Arpeggio & 15)) % 12] += Rows;
			}
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
			const int On = Again.Sticky[e] != 0 ? Again.Sticky[e] : Before.Sticky[e];
			Seams.push_back(Format("%s: %s %s is %s at the loop's end, so the loop's start %s it on repeat but not the first time; "
				"set it at %s (%c00 turns it off).", Info.FullName().c_str(), EffectName(e).c_str(), EffectText(e, On).c_str(),
				Again.Sticky[e] != 0 ? "still on" : "off", Again.Sticky[e] != 0 ? "gets" : "lacks", S.Time.Position(LoopRow).c_str(),
				EffectLetter(e)));
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
			EndPosition(S, LastRow).c_str(), S.Time.Position(LoopRow).c_str());
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

	Draft D(S);
	int Removed = 0;
	for (int c = 0; c < S.ChannelCount(); ++c) {
		std::vector<Cell> &Cells = D.Cells(c);
		const int Cols = S.Channel(c).EffectColumns;
		for (size_t r = 0; r < Cells.size(); ++r)
			for (int k = 0; k < Cols; ++k)
				if (Cells[r].Effect[k] == EFF_JUMP || Cells[r].Effect[k] == EFF_HALT) {
					Cells[r].Effect[k] = EFF_NONE;
					Cells[r].Param[k] = 0;
					++Removed;
				}
	}
	// The jump, unless the song goes back to its start by itself
	const bool Natural = LoopRow == 0 && EndRow == S.Time.TotalRows() - 1;
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
	return WithRemarks(S, Commit(S, Changes, Start, false, "", What), Warnings);
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

// One track rendered, cut where it loops, encoded and saved next to the song
std::string ExportTrack(Session &S, int Track, float Quality, std::vector<std::string> &Taken, std::string &Code)
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
	if (!Cut.Loops)
		Text += ", plays once (the song halts).";
	else if (Cut.LoopStart == 0)
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
			Lines.push_back(ExportTrack(Track, Tracks[i], Quality, Taken, Code));
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
				for (size_t e = 0; e < Note.Effects.size(); ++e)
					if (Note.Effects[e].second.first == EFF_ARPEGGIO && Note.Effects[e].second.second != 0)
						Chords = true;
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

std::string PlayTool(Session &S, const Json &Args)
{
	const int From = IntArg(Args, "from_bar", 1, 1, std::max(1, S.Time.BarCount()));
	const int Bars = IntArg(Args, "bars", 0, 0, S.MaxBars());
	const int First = S.Time.BarStart(From);
	const int Rows = Bars > 0 ? std::min(S.Time.TotalRows(), S.Time.BarEnd(From + Bars - 1)) - First : 0;
	std::string Error;
	if (!S.Host().Play(First, Rows, Error))
		throw Problem(Error.empty() ? "FamiTracker couldn't play." : Error);
	std::string Text = Format("Playing for the user from bar %d%s. You can't hear it yourself; ask the user how it sounds.", From,
		Bars > 0 ? Format(" for %s", Plural(Bars, "bar", "bars").c_str()).c_str() : " to the end");
	const std::string Muted = MutedChannels(S);
	if (!Muted.empty())
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
	std::vector<std::string> Sounds = PresetNames();	// and the VRC7's
	for (size_t i = 0; i < Vrc7Patches().size(); ++i)
		if (std::find(Sounds.begin(), Sounds.end(), Vrc7Patches()[i].Name) == Sounds.end())
			Sounds.push_back(Vrc7Patches()[i].Name);

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
		N.Add("effects", Prop("array", "FamiTracker effects on the note's first row, e.g. [\"Q31\"] (slide up), [\"447\"] (arpeggio)."));
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
		"; on VRC7 channels also the VRC7's sounds: " + Vrc7PatchList() + ").",
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
		"it). An instrument of that name for that chip is reused. write_notes and set_instrument also take these names directly.",
		Schema()
			.Add("preset", EnumProp("The sound.", Sounds), true)
			.Add("channel", ChannelProp("The channel it's for (picks the chip)."))
			.Add("chip", EnumProp("The chip, when no channel is given. Default 2A03.", Chips)),
		false, false, true);

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
			StyleList(ChordStyles()) + ". rhythm (arpeggio and stabs): " + StyleList(ChordRhythms()) + ". pattern (broken): " +
			StyleList(BrokenPatterns()) + ". bass_style: " + StyleList(BassStyles()) + ". color adds sevenths (\"sevenths\") "
			"or richer tones (\"rich\": add9, maj7, m9, a V7sus4 resolving) where the melody allows. Free channels are "
			"picked unless named. The reply lists the chords by name and as degrees.",
			Schema()
				.Add("chords", Chords)
				.Add("melody_channel", ChannelProp("Channel with the melody. Default: the busiest one."))
				.Add("chord_channel", ChannelProp("Pulse channel for the chords, or \"none\". Default: a free one."))
				.Add("bass_channel", ChannelProp("Triangle or sawtooth for the bass, or \"none\". Default: a free one."))
				.Add("key", Prop("string", "Key, like \"C major\" or \"A minor\". Default: found from the melody."))
				.Add("color", EnumProp("plain (triads, default), sevenths, or rich.", std::vector<std::string>{ "plain", "sevenths", "rich" }))
				.Add("chord_style", EnumProp("How the chords are played (default arpeggio).", StyleNames(ChordStyles())))
				.Add("rhythm", EnumProp("When arpeggio or stabs chords are struck (arpeggio: bars, stabs: offbeats by default).",
					StyleNames(ChordRhythms())))
				.Add("pattern", EnumProp("The order of a broken chord's notes (default up).", StyleNames(BrokenPatterns())))
				.Add("notes_per_beat", Prop("integer", "Broken chords: notes a beat, 1-4 (default 2)."))
				.Add("bass_style", EnumProp("How the bass line moves (default root_fifth).", StyleNames(BassStyles())))
				.Add("half_bars", Prop("boolean", "Fitted chords may change halfway through a bar (default true)."))
				.Add("chord_instrument", Prop("string", "Instrument or preset for the chords (default: soft chords for arpeggio, "
					"pluck for stabs and broken, warm pad for guide)."))
				.Add("chord_volume", Prop("integer", "The chords' volume, 1-15 (default 15 for arpeggio and guide, 11 for stabs, "
					"10 for broken); beats a step softer than a bar's first note."))
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
			"more), separate (a short gap between repeated triangle notes, which otherwise sound as one). Default: all three.");
		Json TouchItem = Json::MakeObject();
		TouchItem.Set("type", "string");
		Json TouchNames = Json::MakeArray();
		TouchNames.Push("accents");
		TouchNames.Push("vibrato");
		TouchNames.Push("separate");
		TouchItem.Set("enum", TouchNames);
		Touches.Set("items", TouchItem);
		AddTool("polish", "Finishing touches",
			"Gives parts the small things that make them sound played rather than typed: accents, a delayed vibrato on the "
			"melody's long notes, and gaps between repeated triangle notes. Channels that already shape their volumes keep them. "
			"Run it after the parts are written.",
			Schema()
				.Add("touches", Touches)
				.Add("channels", ChannelListProp("A channel, a list, or \"all\" (default: every melodic channel)."))
				.Add("melody_channel", ChannelProp("The melody, for the vibrato. Default: the busiest channel."))
				.Range().ClipChoice(),
			false, true);
	}

	AddTool("add_echo", "Add an echo",
		"Copies a channel's notes onto a free channel, a little later and quieter: the classic NES echo. Makes a part sound "
		"spacious, dreamy or underwater.",
		Schema()
			.Add("channel", ChannelProp("Channel to echo."), true)
			.Add("to_channel", ChannelProp("Channel for the echo. Default: a free one of the same kind."))
			.Add("delay_beats", Prop("number", "How much later, in beats (default 3 rows)."))
			.Add("volume", Prop("integer", "Echo volume 1-15 (default 5), scaled with the original's loudness."))
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
			"tools work on it (select_track switches back). version \"night\" (or \"calm\") is slower, quieter and more "
			"atmospheric by itself: tempo_percent 80, volume_percent 75, drums sparser, a soft lead and soft chords, slow gentle "
			"vibrato and an echo on the melody. version \"tense\" (or \"battle\", \"danger\", \"boss\", \"chase\") is faster and "
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
				.Add("drums", EnumProp("The noise channel: keep, softer, sparser (only the hits on beats, softer), half (beats 1 and 3), "
					"off, or busier (soft hi-hats added between the beats).",
					std::vector<std::string>{ "keep", "softer", "sparser", "half", "off", "busier" }))
				.Add("bass", EnumProp("The triangle and sawtooth: keep, or driving (each note struck again on every eighth note, "
					"cut short before the next).", std::vector<std::string>{ "keep", "driving" }))
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
		AddTool("make_sound_effect", "Make a sound effect",
			"Makes a sound effect for a game as a new track in the module, the NES's way: a pulse, the triangle and the noise "
			"channel moving their volume, pitch and duty 60 times a second, through instruments whose sequences shape the sound. "
			"It halts at its end, so export_for_game saves it as a sound that plays once. FamiTracker then shows the new track: "
			"play it for the user, and offer other takes (variant 2, 3..., semitones, length_percent, volume_percent) to choose "
			"from. It can also turn a recording of the user's voice into one. Suggest keeping sound effects in a module of their "
			"own, apart from the music. Kinds: " + Uses + ". Other "
			"words for them work too (\"pew\", \"boing\", \"kssh\", \"menu\"). Adding a track can't be undone in FamiTracker.",
			Schema()
				.Add("kind", EnumProp("What it's for.", Kinds))
				.Add("recording", Prop("string", "Instead of a kind: a WAV file of the user's voice (\"pew\", \"boing\", \"kssh\") "
					"on their computer, like C:\\Users\\Name\\Documents\\pew.wav. Its pitch and loudness play on a pulse, its "
					"breathy and hissy part on the noise channel."))
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

	AddTool("export_for_game", "Export music for a game",
		"Saves tracks as OGG files for a game, next to the song: each rendered from FamiTracker, with the intro that plays once "
		"and the loop cut so it repeats without a seam (the loop points are in the file and in the GameMaker code the reply "
		"gives). A song that halts plays once. Run check_loop first. Takes a while for long songs.",
		Schema()
			.Add("tracks", ChannelListProp("\"this\" (default: the selected track), \"all\", or tracks by number or name."))
			.Add("quality", Prop("number", "OGG quality from 0 (small) to 1 (best); default 0.6, which suits chiptunes.")),
		false);

	AddTool("select_track", "Switch tracks",
		"Selects another track (song) of the module in FamiTracker, by number or name; the other tools work on the selected "
		"one. get_song lists the tracks.",
		Schema().Add("track", ChannelProp("Track number (from 1) or name, like \"Town (night)\"."), true), false, false, true);

	AddTool("set_expansion", "Switch the expansion chip",
		"Switches the song's expansion sound chip, as Module > Module properties does: none (the 2A03's five channels only), "
		"VRC6 (two more pulses and a sawtooth), VRC7 (six FM channels with built-in sounds like piano, flute and brass), MMC5 "
		"(two more pulses), FDS or N163 (wavetables). Use it when the user asks for a VRC6 or VRC7 song, before writing. "
		"FamiTracker plays one expansion chip at a time, for every track of the module: the old chip's channels are cleared, so "
		"when they have notes this asks for confirm: true, which needs the user's OK. It can't be undone in FamiTracker.",
		Schema()
			.Add("chip", EnumProp("The chip.", std::vector<std::string>{ "none", "VRC6", "VRC7", "FDS", "MMC5", "N163" }), true)
			.Add("n163_channels", Prop("integer", "N163's channels, 1-8 (default 4; the more, the noisier on the hardware)."))
			.Add("confirm", Prop("boolean", "True once the user agreed to lose the notes on the old chip's channels.")),
		false, true);

	AddTool("review_arrangement", "Review the arrangement's depth",
		"What makes the song sound plain, as a musician would hear it: which parts play (melody, chords, bass, drums, a "
		"second line) and which are missing, bars where only one or two play, the chords (how many, plain triads or "
		"colored, the same loop all the way), a bass that only holds roots, drums without fills, a melody with one note "
		"length or no rests, phrases and whole sections that repeat unchanged, flat dynamics, parts crowding the "
		"melody's register, a bass in octaves with the melody, and the length. It scores the depth out of 10 and lists "
		"what would help most, with the tool for each. Run it after a draft and before telling the user it's done.",
		Schema().Add("key", Prop("string", "Key, like \"C major\". Default: found from the notes.")).Range(), true, false, true);

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
		"Plays the song in FamiTracker for the user, from a bar, for some bars or to the end. You can't hear it; ask the user.",
		Schema().Add("from_bar", BarProp("Bar to start at (default 1).")).Add("bars", Prop("integer", "How many bars (default: to the end).")),
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
		else if (Name == "set_instrument") Result.Text = SetInstrumentTool(S, Args);
		else if (Name == "transpose") Result.Text = TransposeTool(S, Args);
		else if (Name == "set_tempo") Result.Text = SetTempoTool(S, Args);
		else if (Name == "add_chords_and_bass") Result.Text = AddBackingTool(S, Args);
		else if (Name == "add_drums") Result.Text = AddDrumsTool(S, Args);
		else if (Name == "add_harmony") Result.Text = AddHarmonyTool(S, Args);
		else if (Name == "add_counter_melody") Result.Text = AddCounterMelodyTool(S, Args);
		else if (Name == "polish") Result.Text = PolishTool(S, Args);
		else if (Name == "review_arrangement") Result.Text = ReviewTool(S, Args);
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
		"look at the result with look_at_song, and use play so the user can listen, then ask them. "
		"For a mood, use change_mood, which makes all of its changes together (separate tools would make separate clips that "
		"each start from the original): darker = to_mode minor or phrygian, about 10-20% slower, parts an octave lower, rounder "
		"or softer sounds (warm pad, soft lead); happier = to_mode major or lydian, a bit faster, brighter sounds (bright lead, "
		"bell, pluck), and add_chords_and_bass if there are no chords; underwater = slower, a slow and strong vibrato on the "
		"lead, an echo, soft round sounds (soft lead, warm pad), a lower register. Fewer notes also darken; rewrite a part "
		"with write_notes for that. A night version (or another version of a song for a game state, like a tense one for a "
		"battle) is make_version, which adds it as a new track and leaves the original as it was; a jingle (level up, quest "
		"complete, item found...) is make_jingle, and a sound effect (jump, coin, menu blip, \"pew\"...) make_sound_effect. "
		"For a VRC6, VRC7 or MMC5 song, switch the chip with set_expansion first (it asks before clearing notes on the old "
		"chip's channels); VRC7 channels play the chip's own sounds by name (piano, flute, "
		"brass...). set_tempo takes a BPM, or FamiTracker's speed and tempo as the user names them. Tell the user what you "
		"changed and why.";
}

}	// namespace ftapi

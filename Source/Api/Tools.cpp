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
#include "Music.h"
#include "Presets.h"
#include "Song.h"
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
	const Preset *pPreset = FindPreset(Wanted);
	if (pPreset != 0 && pWrongChip != 0 && Squeezed(pPreset->Name) != Squeezed(Wanted))
		pPreset = 0;	// the song's instrument of that name was meant, not a preset it's a nickname of
	if (pPreset != 0) {
		if (pPreset->Drum && Info.Kind != KIND_NOISE)
			throw Problem(Format("The preset \"%s\" is a drum for the noise channel.", pPreset->Name.c_str()));
		if (!pPreset->Drum && Info.Kind == KIND_NOISE)
			throw Problem(Format("The noise channel needs a drum preset (kick, snare, hi-hat, open hi-hat), not \"%s\".", pPreset->Name.c_str()));
		if (Chip != "2A03" && Chip != "VRC6")
			throw Problem(Format("Presets are for 2A03, MMC5 and VRC6 channels; %s needs one of the song's %s instruments.",
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

// The busiest melodic channel that isn't a bass channel (where a melody usually is)
int GuessMelodyChannel(Session &S, int First, int End)
{
	int Best = -1;
	size_t BestCount = 0;
	for (int Pass = 0; Pass < 2 && Best < 0; ++Pass) {
		for (int c = 0; c < S.ChannelCount(); ++c) {
			if (!IsMelodic(S.Channel(c)) || (Pass == 0 && IsBass(S.Channel(c))))
				continue;
			const size_t Count = MelodyOf(S.Notes(c), First, End).size();
			if (Count > BestCount) {
				Best = c;
				BestCount = Count;
			}
		}
	}
	return Best;
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
		Result = Format("Saved as the clip \"%s\" (%s). For the user to hear it: Edit > Clips, select it, put the pattern "
			"cursor on %s at %s and press \"Place a copy\"; Ctrl+Z takes it out again.", Name.c_str(), What.c_str(),
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
		Text += Format("  %d %s: ", c + 1, Channel.FullName().c_str());
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
		"Changes: as clips. Your edits are saved as clips for the user to listen to and place (Edit > Clips); "
		"they can allow direct changes in File > Connect Claude Code.\n";
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
		throw Problem("Placing clips changes the song, and the user hasn't allowed direct changes. Ask them to place it with "
			"Edit > Clips, or to allow direct changes in File > Connect Claude Code.");
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
	const Preset *pPreset = FindPreset(Name);
	if (pPreset == 0)
		throw Problem(Format("No preset called \"%s\". Presets: %s.", Name.c_str(), PresetList().c_str()));
	std::string Chip = "2A03";
	bool Sawtooth = false;
	if (Arg(Args, "channel") != 0) {
		const ChannelInfo &Info = S.Channel(ChannelArg(S, Args, "channel"));
		Chip = InstrumentChip(Info);
		Sawtooth = Info.Kind == KIND_SAWTOOTH;
		if (pPreset->Drum && Info.Kind != KIND_NOISE)
			throw Problem(Format("\"%s\" is a drum for the noise channel.", pPreset->Name.c_str()));
	}
	else if (Arg(Args, "chip") != 0) {
		Chip = StringArg(Args, "chip");
		for (size_t i = 0; i < Chip.size(); ++i)
			Chip[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(Chip[i])));
		if (Chip == "MMC5")
			Chip = "2A03";
	}
	if (Chip != "2A03" && Chip != "VRC6")
		throw Problem("Presets are made for the 2A03 (also used by MMC5 channels) and the VRC6.");
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

// Each channel's cells over [First, End), changed by Edit(channel, cells,
// row of the first), then committed together
template <class EditFunction>
std::string ChangeCells(Session &S, const std::vector<int> &Channels, int First, int End, bool AsClip, const std::string &ClipName,
	const std::string &What, std::vector<std::string> &Warnings, EditFunction Edit)
{
	const bool DirectChange = Direct(S, AsClip);
	std::vector<Change> Changes;
	bool EffectsLost = false;
	for (size_t i = 0; i < Channels.size(); ++i) {
		const int Channel = Channels[i];
		const std::vector<Cell> &Old = S.Cells(Channel);
		Change C;
		C.Channel = Channel;
		for (int r = First; r < End; ++r)
			C.Cells.push_back(r < static_cast<int>(Old.size()) ? Old[r] : Cell());
		if (!Edit(Channel, C.Cells))
			continue;	// nothing changed in this one
		if (DirectChange && !KeepFollowing(Old, First, End, C.Cells, S.Channel(Channel).EffectColumns))
			EffectsLost = true;
		Changes.push_back(C);
	}
	if (EffectsLost)
		Warnings.push_back("Some effects didn't fit in the channels' visible effect columns and were left out.");
	if (Changes.empty())
		throw Problem("Nothing to change there: no notes in those bars.");
	return Commit(S, Changes, First, AsClip, ClipName, What);
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
	int Count = 0;
	std::vector<std::string> Warnings;
	const std::string What = Format("%s on %s, %s", InstrumentLabel(S.Song, Instrument).c_str(), S.Channel(Channel).FullName().c_str(),
		RangeText(From, To).c_str());
	const std::string Result = ChangeCells(S, std::vector<int>(1, Channel), First, End, BoolArg(Args, "as_clip", false),
		StringArg(Args, "clip_name"), What, Warnings,
		[&](int, std::vector<Cell> &Cells) {
			for (size_t r = 0; r < Cells.size(); ++r) {
				if (Cells[r].HasNote()) {
					Cells[r].Instrument = static_cast<unsigned char>(Instrument);
					++Count;
				}
			}
			return Count > 0;
		});
	return WithRemarks(S, Result + Format(" (%s)", Plural(Count, "note", "notes").c_str()), Warnings);
}

std::string TransposeTool(Session &S, const Json &Args)
{
	std::vector<int> Channels = ChannelsArg(S, Args, "channels");
	int From, To;
	BarRange(S, Args, From, To);
	const int First = S.Time.BarStart(From), End = S.Time.BarEnd(To);
	const bool BySemitones = Arg(Args, "semitones") != 0;
	const bool ByKey = Arg(Args, "to_key") != 0 || Arg(Args, "to_mode") != 0;
	if (BySemitones == ByKey)
		throw Problem("Give either semitones, or to_key or to_mode.");
	const int Semitones = IntArg(Args, "semitones", 0, -48, 48);

	Key FromKey, ToKey;
	std::string What;
	if (ByKey) {
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
				throw Problem(Format("to_mode: \"%s\" isn't a mode. Modes: major, dorian, phrygian, lydian, mixolydian, minor, locrian, harmonic minor.",
					Text.c_str()));
		}
		if (ToKey.Tonic == FromKey.Tonic && ToKey.Mode == FromKey.Mode)
			throw Problem("The notes are already in " + KeyName(FromKey) + (Arg(Args, "key") != 0 ? "" : " (found from the notes; give key if "
				"that's wrong)") + ". Pick another to_mode or to_key.");
		What = "from " + KeyName(FromKey) + " to " + KeyName(ToKey);
	}
	else
		What = Format("%s %s", Semitones >= 0 ? "up" : "down", Plural(std::abs(Semitones), "semitone", "semitones").c_str());

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

	int Count = 0, Skipped = 0;
	std::vector<std::string> Warnings;
	std::map<int, int> Moves;	// pitch class -> semitones moved, for the summary
	auto Map = [&](int Pitch) { return BySemitones ? Pitch + Semitones : MapToKey(Pitch, FromKey, ToKey); };
	const std::string Result = ChangeCells(S, Melodic, First, End, BoolArg(Args, "as_clip", false), StringArg(Args, "clip_name"),
		Format("%s, %s, %s", What.c_str(), Plural(static_cast<int>(Melodic.size()), "channel", "channels").c_str(), RangeText(From, To).c_str()),
		Warnings,
		[&](int Channel, std::vector<Cell> &Cells) {
			const int Columns = S.Channel(Channel).EffectColumns;
			int Original = -1;	// the playing note's pitch before
			bool Changed = false;
			for (size_t r = 0; r < Cells.size(); ++r) {
				Cell &C = Cells[r];
				if (C.HasNote()) {
					Original = C.Pitch();
					const int Moved = Map(Original);
					if (Moved < 0 || Moved >= PITCHES) {
						++Skipped;
						continue;
					}
					if (!BySemitones)
						Moves[Original % 12] = Moved - Original;
					Changed = Changed || Moved != Original;
					C.SetPitch(Moved);
					++Count;
				}
				else if (C.IsEnd())
					Original = -1;
				// An arpeggiated chord keeps fitting the key: its notes move too
				if (!BySemitones && Original >= 0) {
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
			}
			return Changed;
		});
	std::string Summary;
	if (!BySemitones) {
		for (std::map<int, int>::const_iterator it = Moves.begin(); it != Moves.end(); ++it) {
			if (it->second == 0)
				continue;
			if (!Summary.empty())
				Summary += ", ";
			Summary += NoteClassName(it->first, FromKey) + " to " + NoteClassName(it->first + it->second, ToKey);
		}
		if (!Summary.empty())
			Summary = " Notes moved: " + Summary + ".";
	}
	if (Skipped > 0)
		Warnings.push_back(Format("%s would have left FamiTracker's range (C-0 to B-7) and stayed as they were.",
			Plural(Skipped, "note", "notes").c_str()));
	return WithRemarks(S, Result + Format(" (%s)", Plural(Count, "note", "notes").c_str()) + Summary, Warnings);
}

std::string SetTempoTool(Session &S, const Json &Args)
{
	if (!S.Song.DirectChanges)
		throw Problem(Format("Changing the tempo changes the song, and the user hasn't allowed direct changes. Suggest the user sets "
			"it with the speed and tempo boxes in FamiTracker's toolbar (now speed %d, tempo %d), or allows direct changes in "
			"File > Connect Claude Code.", S.Song.Speed, S.Song.Tempo));
	if (Arg(Args, "bpm") == 0)
		throw Problem("Give the tempo as bpm, like {\"bpm\": 120}.");
	const double Bpm = NumberArg(Args, "bpm", 0.0, 10.0, 1000.0);
	int Speed, Tempo;
	ChooseSpeedTempo(Bpm, S.Time.RowsPerBeat, S.Song.FrameRate, S.Song.Speed, S.Song.SplitPoint, Speed, Tempo);
	const double Before = BeatsPerMinute(S.Song.Speed, S.Song.Tempo, S.Song.FrameRate, S.Time.RowsPerBeat);
	const double After = BeatsPerMinute(Speed, Tempo, S.Song.FrameRate, S.Time.RowsPerBeat);
	std::string Why, Error;
	if (!S.Host().CanChange(Why))
		throw Problem(Why);
	if (!S.Host().SetSpeedTempo(Speed, Tempo, Error))
		throw Problem(Error.empty() ? "The tempo couldn't be set." : Error);
	std::string Text = Format("Tempo set to %s BPM (speed %d, tempo %d); it was %s BPM. One Ctrl+Z undoes it.",
		FormatNumber(std::floor(After * 10 + 0.5) / 10).c_str(), Speed, Tempo, FormatNumber(std::floor(Before * 10 + 0.5) / 10).c_str());
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

// Melody cells from the harmony code to notes to write
std::vector<PartNote> NotesFromCells(const std::vector<transcribe::MelodyCell> &Line, int End, int Instrument)
{
	std::vector<PartNote> Notes;
	for (size_t i = 0; i < Line.size(); ++i) {
		const transcribe::MelodyCell &C = Line[i];
		int Effect = EFF_NONE;
		switch (C.effect) {
		case '0': Effect = EFF_ARPEGGIO; break;
		case '4': Effect = EFF_VIBRATO; break;
		case 'Q': Effect = EFF_SLIDE_UP; break;
		case 'R': Effect = EFF_SLIDE_DOWN; break;
		}
		if (C.kind == transcribe::MelodyCell::Note) {
			if (!Notes.empty() && Notes.back().Row + Notes.back().Rows > C.row)
				Notes.back().Rows = std::max(1, C.row - Notes.back().Row);
			PartNote N;
			N.Row = C.row;
			N.Rows = std::max(1, End - C.row);
			N.Pitch = std::max(0, std::min(PITCHES - 1, C.midi - 24));
			N.Instrument = Instrument;
			N.Volume = C.volume;
			if (Effect != EFF_NONE)
				N.Effects.push_back(std::make_pair(Effect, C.effectParam));
			Notes.push_back(N);
		}
		else if (!Notes.empty()) {
			PartNote &Last = Notes.back();
			if (C.kind == transcribe::MelodyCell::Release) {
				Last.Rows = std::max(1, C.row - Last.Row);
				if (Effect != EFF_NONE)
					Last.EndEffects.push_back(std::make_pair(Effect, C.effectParam));
			}
			else if (Effect != EFF_NONE && C.row > Last.Row)
				Last.LaterEffects.push_back(std::make_pair(C.row - Last.Row, std::make_pair(Effect, C.effectParam)));
		}
	}
	return Notes;
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
	return Channel.Kind == KIND_PULSE && (Channel.Chip == "2A03" || Channel.Chip == "VRC6" || Channel.Chip == "MMC5");
}

bool IsBassChannel(const ChannelInfo &Channel)
{
	return IsBass(Channel);
}

std::string AddBackingTool(Session &S, const Json &Args)
{
	const bool AsClip = BoolArg(Args, "as_clip", false);
	const bool DirectChange = Direct(S, AsClip);
	int Melody = Arg(Args, "melody_channel") != 0 ? ChannelArg(S, Args, "melody_channel") : GuessMelodyChannel(S, 0, S.Time.TotalRows());
	if (Melody < 0)
		throw Problem("There's no melody to fit chords to yet.");
	if (!IsMelodic(S.Channel(Melody)))
		throw Problem("The melody channel needs notes, not drums or samples.");

	// The bars: as asked, or the ones the melody covers
	std::vector<transcribe::MelodyNote> Line = MelodyOf(S.Notes(Melody), 0, S.Time.TotalRows());
	std::vector<transcribe::Bar> Bars;
	if (Arg(Args, "from_bar") != 0 || Arg(Args, "to_bar") != 0) {
		int From, To;
		BarRange(S, Args, From, To);
		Bars = transcribe::SongBars(S.Time.BarStart(From), S.Time.BarEnd(To), S.Time.PatternLength, S.Time.RowsPerBar);
		Line = MelodyOf(S.Notes(Melody), S.Time.BarStart(From), S.Time.BarEnd(To));
	}
	else
		Bars = transcribe::MelodyBars(Line, S.Time.PatternLength, S.Time.RowsPerBar);
	if (Line.empty() || Bars.empty())
		throw Problem(Format("%s has no notes there to fit chords to.", S.Channel(Melody).FullName().c_str()));
	const int First = Bars.front().row, End = Bars.back().row + Bars.back().length;

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

	bool Found;
	const Key K = KeyArg(S, Args, "key", First, End, Found);
	const transcribe::Key HK = HarmonyKey(K);
	transcribe::HarmonyOptions Options;
	Options.rowsPerBeat = S.Time.RowsPerBeat;
	Options.halfBars = BoolArg(Args, "half_bars", true);
	std::sort(Line.begin(), Line.end(), [](const transcribe::MelodyNote &a, const transcribe::MelodyNote &b) { return a.row < b.row; });
	const std::vector<transcribe::ChordSpan> Chords = transcribe::ChooseChords(Line, Bars, HK, Options);
	if (Chords.empty())
		throw Problem("The melody is too short to fit chords to.");

	std::vector<Change> Changes;
	std::vector<std::string> Warnings;
	std::string Where;
	if (ChordChannel >= 0) {
		const int Instrument = InstrumentArg(S, Args, "chord_instrument", ChordChannel, ResolveInstrument(S, "soft chords", ChordChannel));
		std::vector<PartNote> Notes = NotesFromCells(transcribe::ArpeggioCells(Chords, Bars), End, Instrument);
		if (!Notes.empty() && Notes.front().Volume < 0)
			Notes.front().Volume = 15;
		Change C;
		C.Channel = ChordChannel;
		BuildPart(S.Cells(ChordChannel), First, End, Notes, OptionsFor(S, ChordChannel, DirectChange), C.Cells, Warnings);
		Changes.push_back(C);
		Where = "chords on " + S.Channel(ChordChannel).FullName();
	}
	if (BassChannel >= 0) {
		const int Instrument = InstrumentArg(S, Args, "bass_instrument", BassChannel, ResolveInstrument(S, "bass", BassChannel));
		std::vector<PartNote> Notes = NotesFromCells(transcribe::BassCells(Chords, Bars, Options), End, Instrument);
		if (!Notes.empty() && Notes.front().Volume < 0)
			Notes.front().Volume = 15;
		Change C;
		C.Channel = BassChannel;
		BuildPart(S.Cells(BassChannel), First, End, Notes, OptionsFor(S, BassChannel, DirectChange), C.Cells, Warnings);
		Changes.push_back(C);
		Where += (Where.empty() ? "a bass line on " : " and a bass line on ") + S.Channel(BassChannel).FullName();
	}
	const std::string What = Format("%s, %s, in %s", Where.c_str(), RangeText(S.Time.BarOf(First), S.Time.BarOf(End - 1)).c_str(),
		KeyName(K).c_str());
	std::sort(Changes.begin(), Changes.end(), [](const Change &a, const Change &b) { return a.Channel < b.Channel; });
	const std::string Result = Commit(S, Changes, First, AsClip, StringArg(Args, "clip_name"), What);
	return WithRemarks(S, Result + " Chords: " + ChordsText(Chords, Bars, HK), Warnings);
}

bool SameKind(const ChannelInfo &a, const ChannelInfo &b)
{
	return a.Kind == b.Kind && InstrumentChip(a) == InstrumentChip(b);
}

std::string AddEchoTool(Session &S, const Json &Args)
{
	const int Source = ChannelArg(S, Args, "channel");
	if (!IsMelodic(S.Channel(Source)))
		throw Problem("The echo copies a melodic channel's notes.");
	int From, To;
	BarRange(S, Args, From, To);
	const int First = S.Time.BarStart(From), End = S.Time.BarEnd(To);
	bool Exact;
	const int Delay = S.Time.BeatsToRows(NumberArg(Args, "delay_beats", 3.0 / S.Time.RowsPerBeat, 0.01, 16.0), Exact);
	const int Volume = IntArg(Args, "volume", 5, 1, 15);
	const bool AsClip = BoolArg(Args, "as_clip", false);

	int Target;
	if (Arg(Args, "to_channel") != 0) {
		Target = ChannelArg(S, Args, "to_channel");
		if (Target == Source || !IsMelodic(S.Channel(Target)))
			throw Problem("to_channel needs another melodic channel.");
	}
	else {
		Target = -1;
		for (int Pass = 0; Pass < 2 && Target < 0; ++Pass) {
			for (int c = 0; c < S.ChannelCount() && Target < 0; ++c) {
				if (c == Source || !IsMelodic(S.Channel(c)) || IsBass(S.Channel(c)) != IsBass(S.Channel(Source)))
					continue;
				if (Pass == 0 && !SameKind(S.Channel(c), S.Channel(Source)))
					continue;
				if (!HasNotesIn(S, c, First + Delay, End + Delay))
					Target = c;
			}
		}
		if (Target < 0)
			throw Problem("No free channel for the echo in those bars; name one with to_channel (its notes there are replaced).");
	}
	const int Explicit = Arg(Args, "instrument") != 0 ? InstrumentArg(S, Args, "instrument", Target, NO_INSTRUMENT) : NO_INSTRUMENT;
	const bool Fits = InstrumentChip(S.Channel(Target)) == InstrumentChip(S.Channel(Source));
	if (Explicit == NO_INSTRUMENT && !Fits)
		throw Problem(Format("%s needs %s instruments; give one with instrument (a preset works).", S.Channel(Target).FullName().c_str(),
			InstrumentChip(S.Channel(Target)).c_str()));

	std::vector<PartNote> Notes;
	const std::vector<NoteInfo> &Played = S.Notes(Source);
	for (size_t i = 0; i < Played.size(); ++i) {
		const NoteInfo &N = Played[i];
		if (N.Row < First || N.Row >= End)
			continue;
		PartNote E;
		E.Row = N.Row + Delay;
		E.Rows = std::min(N.Rows, End + Delay - E.Row);
		if (E.Rows < 1 || E.Row + E.Rows > S.MaxRows())
			continue;
		E.Pitch = N.Pitch;
		E.Instrument = Explicit != NO_INSTRUMENT ? Explicit : N.Instrument;
		E.Volume = std::max(1, (N.Volume * Volume + 7) / 15);	// quieter, following the loudness
		for (size_t e = 0; e < N.Effects.size(); ++e) {
			if (N.Effects[e].first == 0)
				E.Effects.push_back(N.Effects[e].second);
			else if (N.Effects[e].first < E.Rows)
				E.LaterEffects.push_back(N.Effects[e]);
		}
		Notes.push_back(E);
	}
	if (Notes.empty())
		throw Problem(Format("%s has no notes in %s to echo.", S.Channel(Source).FullName().c_str(), RangeText(From, To).c_str()));
	for (size_t i = 0; i + 1 < Notes.size(); ++i)
		Notes[i].Rows = std::min(Notes[i].Rows, Notes[i + 1].Row - Notes[i].Row);
	std::vector<std::string> Warnings;
	if (HasNotesIn(S, Target, First + Delay, End + Delay))
		Warnings.push_back(Format("%s's own notes in those bars were replaced by the echo.", S.Channel(Target).FullName().c_str()));
	const int TargetEnd = std::min(End + Delay, S.MaxRows());
	Change C;
	C.Channel = Target;
	BuildPart(S.Cells(Target), First + Delay, TargetEnd, Notes, OptionsFor(S, Target, Direct(S, AsClip)), C.Cells, Warnings);
	std::vector<Change> Changes(1, C);
	const std::string What = Format("an echo of %s on %s, %s later at volume %d, %s", S.Channel(Source).FullName().c_str(),
		S.Channel(Target).FullName().c_str(), S.Time.Length(Delay).c_str(), Volume, RangeText(From, To).c_str());
	return WithRemarks(S, Commit(S, Changes, First + Delay, AsClip, StringArg(Args, "clip_name"), What), Warnings);
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

std::string AddVibratoTool(Session &S, const Json &Args)
{
	const int Channel = ChannelArg(S, Args, "channel");
	if (!IsMelodic(S.Channel(Channel)))
		throw Problem("Vibrato is for melodic channels.");
	int From, To;
	BarRange(S, Args, From, To);
	const int First = S.Time.BarStart(From);
	int End = S.Time.BarEnd(To);
	static const char *const SPEEDS[] = { "slow", "medium", "fast" };
	static const int SPEED_VALUES[] = { 3, 5, 8 };
	static const char *const DEPTHS[] = { "none", "gentle", "medium", "strong" };
	static const int DEPTH_VALUES[] = { 0, 2, 4, 7 };
	const int Speed = std::max(1, LevelArg(Args, "speed", 5, SPEEDS, SPEED_VALUES, 3));
	const int Depth = LevelArg(Args, "depth", 2, DEPTHS, DEPTH_VALUES, 4);
	bool Exact;
	const int MinRows = S.Time.BeatsToRows(NumberArg(Args, "min_beats", 1.0, 0.01, 64.0), Exact);
	const int Delay = S.Time.BeatsToRows(NumberArg(Args, "delay_beats", 0.25, 0.01, 16.0), Exact);
	const int Columns = S.Channel(Channel).EffectColumns;

	// Notes that start in the bars, to their ends
	const std::vector<NoteInfo> &Notes = S.Notes(Channel);
	for (size_t i = 0; i < Notes.size(); ++i)
		if (Notes[i].Row >= First && Notes[i].Row < End)
			End = std::max(End, Notes[i].Row + Notes[i].Rows);
	End = std::min(End, S.Time.TotalRows());
	int Count = 0;
	bool Lost = false;
	std::vector<std::string> Warnings;
	const std::string What = Depth == 0 ? Format("vibrato taken off %s, %s", S.Channel(Channel).FullName().c_str(), RangeText(From, To).c_str()) :
		Format("vibrato 4%X%X on the longer notes of %s, %s", Speed, Depth, S.Channel(Channel).FullName().c_str(), RangeText(From, To).c_str());
	const std::string Result = ChangeCells(S, std::vector<int>(1, Channel), First, End, BoolArg(Args, "as_clip", false),
		StringArg(Args, "clip_name"), What, Warnings,
		[&](int, std::vector<Cell> &Cells) {
			for (size_t i = 0; i < Notes.size(); ++i) {
				const NoteInfo &N = Notes[i];
				if (N.Row < First || N.Row >= S.Time.BarEnd(To))
					continue;
				const int Last = std::min(N.Row + N.Rows, End) - 1;
				// The note's vibrato as it was goes
				for (int r = N.Row; r <= Last; ++r) {
					Cell &C = Cells[r - First];
					for (int k = 0; k < EFFECT_COLUMNS; ++k) {
						if (C.Effect[k] == EFF_VIBRATO) {
							C.Effect[k] = EFF_NONE;
							C.Param[k] = 0;
						}
					}
				}
				if (Depth == 0) {
					++Count;
					continue;
				}
				if (N.Rows < MinRows || N.Row + Delay >= Last)
					continue;
				if (!AddEffect(Cells[N.Row + Delay - First], EFF_VIBRATO, (Speed << 4) | Depth, Columns) ||
					!AddEffect(Cells[Last - First], EFF_VIBRATO, 0, Columns))
					Lost = true;
				++Count;
			}
			if (Depth == 0) {
				// A vibrato still on from before the bars stops too
				const ChannelState State = StateAt(S.Cells(Channel), First, Columns);
				if (State.Sticky[EFF_VIBRATO] != 0 && !AddEffect(Cells[0], EFF_VIBRATO, 0, Columns))
					Lost = true;
			}
			return Count > 0;
		});
	if (Lost)
		Warnings.push_back("Some notes had no free effect column for the vibrato (show more effect columns in FamiTracker).");
	return WithRemarks(S, Result + Format(" (%s)", Plural(Count, "note", "notes").c_str()), Warnings);
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
				Range = "below A-0, the lowest note a 2A03 pulse can play";
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

std::string PlayTool(Session &S, const Json &Args)
{
	const int From = IntArg(Args, "from_bar", 1, 1, std::max(1, S.Time.BarCount()));
	const int Bars = IntArg(Args, "bars", 0, 0, S.MaxBars());
	const int First = S.Time.BarStart(From);
	const int Rows = Bars > 0 ? std::min(S.Time.TotalRows(), S.Time.BarEnd(From + Bars - 1)) - First : 0;
	std::string Error;
	if (!S.Host().Play(First, Rows, Error))
		throw Problem(Error.empty() ? "FamiTracker couldn't play." : Error);
	return Format("Playing for the user from bar %d%s. You can't hear it yourself; ask the user how it sounds.", From,
		Bars > 0 ? Format(" for %s", Plural(Bars, "bar", "bars").c_str()).c_str() : " to the end");
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
		"for the user to listen to and place. Name an instrument (one of the song's or a preset, which is added: " + Presets + ").",
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
		"its right. As a copy, or linked (linked copies change together). Needs direct changes; otherwise the user places "
		"clips with Edit > Clips.",
		Schema()
			.Add("clip", ChannelProp("The clip's name or number from get_song."), true)
			.Add("channel", ChannelProp("Channel for its first column."), true)
			.Add("bar", BarProp("Bar to place it at."), true)
			.Add("beat", Prop("number", "Beat in the bar, default 1."))
			.Add("linked", Prop("boolean", "Place a linked copy instead of a plain one.")),
		false, true);

	AddTool("add_instrument", "Add an instrument",
		"Adds an instrument from a preset: " + Presets + ". 2A03 and VRC6 channels need their own: give the channel it's for. "
		"An instrument of that name for that chip is reused. write_notes and set_instrument also take preset names directly.",
		Schema()
			.Add("preset", EnumProp("The sound.", PresetNames()), true)
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
			.Add("channels", ChannelProp("A channel, a list of channels, or \"all\" (default: every melodic channel)."))
			.Add("semitones", Prop("integer", "Up (positive) or down (negative) by this many semitones; 12 is an octave."))
			.Add("to_mode", EnumProp("The mode to change into, keeping the tonic.", Modes))
			.Add("to_key", Prop("string", "The key to change into, like \"A minor\" or \"F dorian\"."))
			.Add("key", Prop("string", "The key the notes are in now. Default: found from the notes."))
			.Range().ClipChoice(),
		false, true);

	AddTool("set_tempo", "Set the tempo",
		"Sets the song's tempo in BPM (picks FamiTracker's speed and tempo). Slower feels calmer or sadder, faster brighter or "
		"more urgent. Needs direct changes; undoable.",
		Schema().Add("bpm", Prop("number", "Beats per minute."), true), false, false, true);

	AddTool("add_chords_and_bass", "Add chords and a bass line",
		"Fits chords to a melody bar by bar in its key and writes them as an arpeggio on a pulse channel, plus a bass line on "
		"the triangle or sawtooth. Free channels are picked unless named.",
		Schema()
			.Add("melody_channel", ChannelProp("Channel with the melody. Default: the busiest one."))
			.Add("chord_channel", ChannelProp("Pulse channel for the chords, or \"none\". Default: a free one."))
			.Add("bass_channel", ChannelProp("Triangle or sawtooth for the bass, or \"none\". Default: a free one."))
			.Add("key", Prop("string", "Key, like \"C major\" or \"A minor\". Default: found from the melody."))
			.Add("half_bars", Prop("boolean", "Chords may change halfway through a bar (default true)."))
			.Add("chord_instrument", Prop("string", "Instrument or preset for the chords (default \"soft chords\")."))
			.Add("bass_instrument", Prop("string", "Instrument or preset for the bass (default \"bass\")."))
			.Range().ClipChoice(),
		false, true);

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
			.Add("speed", ChannelProp("slow, medium, fast, or 1-15 (default medium)."))
			.Add("depth", ChannelProp("none, gentle, medium, strong, or 0-15 (default gentle)."))
			.Add("min_beats", Prop("number", "Only notes at least this long, in beats (default 1)."))
			.Add("delay_beats", Prop("number", "Start this long after the note begins (default 0.25 beat)."))
			.Range().ClipChoice(),
		false, true);

	AddTool("check_song", "Check the song",
		"Checks what you can't hear: notes outside the key, two channels clashing a semitone apart, notes outside a channel's "
		"range, very busy bars, silent bars and unused channels. Run it after writing.",
		Schema().Add("key", Prop("string", "Key to check against. Default: found from the notes.")).Range(), true, false, true);

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
		else if (Name == "add_echo") Result.Text = AddEchoTool(S, Args);
		else if (Name == "add_vibrato") Result.Text = AddVibratoTool(S, Args);
		else if (Name == "check_song") Result.Text = CheckTool(S, Args);
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
		"Start with get_song, then read_notes for the parts you change. "
		"By default your changes arrive as clips the user listens to and places (Edit > Clips); if the user allowed direct "
		"changes, they go into the song and each one is undone with Ctrl+Z. You can't hear: run check_song after writing and "
		"use play so the user can listen, then ask them. "
		"Moods combine the tools: darker = transpose to_mode minor or phrygian, about 10-20% slower (set_tempo), lower parts, "
		"rounder or softer sounds (warm pad, soft lead), fewer notes; happier = to_mode major or lydian, a bit faster, brighter "
		"sounds (bright lead, bell, pluck), arpeggiated chords; underwater = slower, add_vibrato slow and strong on the lead, "
		"add_echo, soft round sounds (soft lead, warm pad), a lower register. Tell the user what you changed and why.";
}

}	// namespace ftapi

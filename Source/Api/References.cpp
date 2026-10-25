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

#include "References.h"
#include "FdsSounds.h"
#include "Json.h"
#include "Music.h"
#include "Wavetables.h"
#include "../Nsf/NsfFile.h"
#include "../Nsf/Study.h"
#include "../Nsf/Tracker.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <map>
#include <set>

namespace ftapi {

namespace {

const int REFERENCE_FORMAT = 1;
const int MAX_ITEMS = 252;		// FamiTracker's sequences hold 253 values; one spare
const char *const SEQUENCE_NAMES[5] = { "volume", "arpeggio", "pitch", "hi_pitch", "duty" };

std::string Format(const char *pFormat, ...)
{
	char Buffer[1024];
	va_list Args;
	va_start(Args, pFormat);
	std::vsnprintf(Buffer, sizeof(Buffer), pFormat, Args);
	va_end(Args);
	return Buffer;
}

std::string Join(const std::vector<std::string> &Items, const std::string &Between)
{
	std::string Out;
	for (size_t i = 0; i < Items.size(); ++i)
		Out += (i ? Between : "") + Items[i];
	return Out;
}

// Lower case words of letters and digits ('#' kept, for keys like "C#")
std::vector<std::string> Words(const std::string &Text)
{
	std::vector<std::string> Out;
	std::string Word;
	for (size_t i = 0; i <= Text.size(); ++i) {
		const unsigned char c = i < Text.size() ? static_cast<unsigned char>(Text[i]) : ' ';
		if (std::isalnum(c) || c == '#' || c >= 0x80)
			Word += static_cast<char>(std::tolower(c));
		else if (!Word.empty()) {
			Out.push_back(Word);
			Word.clear();
		}
	}
	return Out;
}

std::vector<std::string> Lines(const std::string &Text)
{
	std::vector<std::string> Out;
	size_t Start = 0;
	while (Start < Text.size()) {
		size_t End = Text.find('\n', Start);
		if (End == std::string::npos)
			End = Text.size();
		Out.push_back(Text.substr(Start, End - Start));
		Start = End + 1;
	}
	return Out;
}

bool Contains(const std::string &Text, const char *Part)
{
	return Text.find(Part) != std::string::npos;
}

void AddTag(std::vector<std::string> &Tags, const std::string &Tag)
{
	if (!Tag.empty() && std::find(Tags.begin(), Tags.end(), Tag) == Tags.end())
		Tags.push_back(Tag);
}

// The sequence's numbers as FamiTracker shows them: "15 14 [13 12 | 6 0"
std::string SequenceText(const ReferenceSequence &S)
{
	std::string Out;
	for (size_t i = 0; i < S.Items.size(); ++i) {
		if (static_cast<int>(i) == S.Loop)
			Out += Out.empty() ? "[" : " [";
		else if (!Out.empty())
			Out += " ";
		Out += std::to_string(S.Items[i]);
		if (static_cast<int>(i) == S.Release && i + 1 < S.Items.size())
			Out += " |";
	}
	return Out;
}

// 2A03 duty (12.5%, 25%, 50%, 75%) and the VRC6's (n + 1 sixteenths)
int Vrc6Duty(int Duty2A03)
{
	static const int MAP[4] = { 1, 3, 7, 3 };
	return MAP[Duty2A03 & 3];
}

int Duty2A03(int DutyVrc6)
{
	const int d = DutyVrc6 & 7;
	return d <= 1 ? 0 : (d <= 4 ? 1 : 2);
}

// --- JSON, indented so the files read well ---

void Pretty(const Json &J, int Indent, std::string &Out)
{
	const std::string Tabs(static_cast<size_t>(Indent), '\t');
	if (J.IsArray()) {
		bool Flat = true;
		for (size_t i = 0; i < J.Size(); ++i)
			Flat = Flat && !J.At(i).IsArray() && !J.At(i).IsObject() && !J.At(i).IsString();
		if (Flat || J.Size() == 0) {
			Out += WriteJson(J);
			return;
		}
		Out += "[\n";
		for (size_t i = 0; i < J.Size(); ++i) {
			Out += Tabs + "\t";
			Pretty(J.At(i), Indent + 1, Out);
			Out += i + 1 < J.Size() ? ",\n" : "\n";
		}
		Out += Tabs + "]";
	}
	else if (J.IsObject()) {
		if (J.Size() == 0) {
			Out += "{}";
			return;
		}
		Out += "{\n";
		const std::vector<std::pair<std::string, Json> > &Members = J.Members();
		for (size_t i = 0; i < Members.size(); ++i) {
			Out += Tabs + "\t" + WriteJson(Json(Members[i].first)) + ": ";
			Pretty(Members[i].second, Indent + 1, Out);
			Out += i + 1 < Members.size() ? ",\n" : "\n";
		}
		Out += Tabs + "}";
	}
	else
		Out += WriteJson(J);
}

std::string TextOf(const Json &J, const char *Key)
{
	const Json *p = J.Get(Key);
	return p != 0 && p->IsString() ? p->String() : std::string();
}

int IntOf(const Json &J, const char *Key, int Default)
{
	const Json *p = J.Get(Key);
	return p != 0 && p->IsNumber() ? static_cast<int>(p->Integer()) : Default;
}

bool BoolOf(const Json &J, const char *Key)
{
	const Json *p = J.Get(Key);
	return p != 0 && p->IsBool() && p->Bool();
}

std::vector<std::string> TextsOf(const Json &J, const char *Key)
{
	std::vector<std::string> Out;
	const Json *p = J.Get(Key);
	if (p != 0 && p->IsArray())
		for (size_t i = 0; i < p->Size(); ++i)
			if (p->At(i).IsString())
				Out.push_back(p->At(i).String());
	return Out;
}

Json SequenceJson(const ReferenceSequence &S)
{
	Json Out = Json::MakeObject();
	Json Items = Json::MakeArray();
	for (size_t i = 0; i < S.Items.size(); ++i)
		Items.Push(S.Items[i]);
	Out.Set("values", Items);
	if (S.Loop >= 0)
		Out.Set("loop", S.Loop);
	if (S.Release >= 0)
		Out.Set("release", S.Release);
	if (S.Fixed)
		Out.Set("fixed", true);
	return Out;
}

// An N163 sound's waves as FamiTracker's instruments hold them: 1-16 of them, each of the same 4-32 samples
bool WholeN163Waves(const std::vector<std::vector<int> > &Waves)
{
	if (Waves.empty() || Waves.size() > 16)
		return false;
	for (size_t w = 0; w < Waves.size(); ++w)
		if (Waves[w].size() < 4 || Waves[w].size() > 32 || Waves[w].size() != Waves[0].size())
			return false;
	return true;
}

ReferenceSequence ReadSequence(const Json &J)
{
	ReferenceSequence S;
	const Json *pValues = J.Get("values");
	if (pValues != 0 && pValues->IsArray())
		for (size_t i = 0; i < pValues->Size() && i < static_cast<size_t>(MAX_ITEMS); ++i)
			if (pValues->At(i).IsNumber())
				S.Items.push_back(std::max(-128, std::min(127, static_cast<int>(pValues->At(i).Integer()))));
	S.Loop = IntOf(J, "loop", -1);
	S.Release = IntOf(J, "release", -1);
	if (S.Loop >= static_cast<int>(S.Items.size()))
		S.Loop = -1;
	if (S.Release >= static_cast<int>(S.Items.size()))
		S.Release = -1;
	S.Fixed = BoolOf(J, "fixed");
	return S;
}

// The words of a reference to search, with their weights
struct Indexed {
	std::map<std::string, int> Strong;	// tags, title, game, composer
	std::string Study;					// lower case
};

Indexed IndexOf(const Reference &R)
{
	Indexed Ix;
	for (size_t i = 0; i < R.Tags.size(); ++i) {
		Ix.Strong[Lowercase(R.Tags[i])] = std::max(Ix.Strong[Lowercase(R.Tags[i])], 8);
		const std::vector<std::string> Parts = Words(R.Tags[i]);
		for (size_t k = 0; k < Parts.size(); ++k)
			Ix.Strong[Parts[k]] = std::max(Ix.Strong[Parts[k]], 6);
	}
	const std::string Named[] = { R.Title, R.Game, R.Composer, R.Copyright, R.Key, R.Id, R.Notes };
	for (size_t n = 0; n < sizeof(Named) / sizeof(Named[0]); ++n) {
		const std::vector<std::string> Parts = Words(Named[n]);
		for (size_t k = 0; k < Parts.size(); ++k)
			Ix.Strong[Parts[k]] = std::max(Ix.Strong[Parts[k]], 6);
	}
	Ix.Study = Lowercase(R.Study);
	for (size_t s = 0; s < R.Sounds.size(); ++s)
		Ix.Study += "\n" + Lowercase(R.Sounds[s].Words);
	return Ix;
}

// Words of a search that say nothing on their own
bool Filler(const std::string &Word)
{
	static const char *const FILLERS[] = { "a", "an", "the", "and", "or", "of", "in", "on", "with", "like", "for", "to", "style",
		"track", "tracks", "music", "song", "songs", "some", "that", "has", "have", "uses", "using", "nes", "is" };
	for (size_t i = 0; i < sizeof(FILLERS) / sizeof(FILLERS[0]); ++i)
		if (Word == FILLERS[i])
			return true;
	return false;
}

size_t CountOf(const std::string &Text, const std::string &Word)
{
	size_t Count = 0;
	for (size_t Pos = Text.find(Word); Pos != std::string::npos; Pos = Text.find(Word, Pos + Word.size()))
		++Count;
	return Count;
}

}	// namespace

// "Bucky O'Hare" -> "bucky-o-hare": letters and digits, at most 32
std::string Slug(const std::string &Text, const char *Otherwise)
{
	std::string Out;
	for (size_t i = 0; i < Text.size() && Out.size() < 32; ++i) {
		const unsigned char c = static_cast<unsigned char>(Text[i]);
		if (c < 0x80 && std::isalnum(c))
			Out += static_cast<char>(std::tolower(c));
		else if (!Out.empty() && Out[Out.size() - 1] != '-')
			Out += '-';
	}
	while (!Out.empty() && Out[Out.size() - 1] == '-')
		Out.erase(Out.size() - 1);
	return Out.empty() ? std::string(Otherwise) : Out;
}

std::string ReferenceId(const std::string &Game, int Track)
{
	return Slug(Game, "nsf") + Format("-%02d", Track);
}

std::string ModuleReferenceId(const std::string &Title, const std::string &Format)
{
	return Slug(Title, "module") + "-" + Lowercase(Format);
}

namespace {
void AddTechniqueTags(Reference &R);

// A study's sounds as the reference keeps them (the VRC7's patches aren't sequences: left out)
void AddStudySounds(Reference &R, const nsf::TrackStudy &Study)
{
	for (size_t i = 0; i < Study.Sounds.size(); ++i) {
		const nsf::StudySound &s = Study.Sounds[i];
		if (s.Kind == nsf::INST_VRC7)
			continue;
		ReferenceSound Sd;
		Sd.Channel = s.Channel;
		Sd.Role = s.Role;
		Sd.Words = s.Words;
		Sd.Chip = s.Kind == nsf::INST_VRC6 ? "VRC6" : (s.Kind == nsf::INST_N163 ? "N163" : (s.Kind == nsf::INST_FDS ? "FDS" : "2A03"));
		Sd.Noise = s.Noise;
		Sd.Triangle = s.Triangle;
		Sd.Sawtooth = s.Sawtooth;
		Sd.Share = s.Share;
		for (int t = 0; t < 5 && t < nsf::SEQ_TYPES; ++t) {
			const nsf::Macro &m = s.Macros[t];
			ReferenceSequence &q = Sd.Sequences[t];
			for (size_t k = 0; k < m.Items.size() && k < static_cast<size_t>(MAX_ITEMS); ++k)
				q.Items.push_back(std::max(-128, std::min(127, m.Items[k])));
			q.Loop = m.Loop < static_cast<int>(q.Items.size()) ? m.Loop : -1;
			q.Release = m.Release < static_cast<int>(q.Items.size()) ? m.Release : -1;
			q.Fixed = t == nsf::SEQ_ARPEGGIO && m.Setting == nsf::ARP_FIXED;
		}
		if (s.Kind == nsf::INST_FDS && s.FdsWave.size() == static_cast<size_t>(FDS_WAVE_SIZE)) {
			Sd.FdsWave = s.FdsWave;
			if (s.FdsMod.size() == static_cast<size_t>(FDS_MOD_SIZE))
				Sd.FdsMod = s.FdsMod;
			Sd.ModSpeed = s.ModRate;
			Sd.ModDepth = s.ModDepth;
			Sd.ModDelay = s.ModDelay;
		}
		if (s.Kind == nsf::INST_N163 && WholeN163Waves(s.N163Waves)) {
			Sd.N163Waves = s.N163Waves;
			for (size_t w = 0; w < Sd.N163Waves.size(); ++w)
				for (size_t k = 0; k < Sd.N163Waves[w].size(); ++k)
					Sd.N163Waves[w][k] = std::max(0, std::min(15, Sd.N163Waves[w][k]));
			const nsf::Macro &m = s.N163WaveSteps;
			ReferenceSequence &q = Sd.N163WaveSteps;
			for (size_t k = 0; k < m.Items.size() && k < static_cast<size_t>(MAX_ITEMS) && Sd.N163Waves.size() > 1; ++k)
				q.Items.push_back(std::max(0, std::min(static_cast<int>(Sd.N163Waves.size()) - 1, m.Items[k])));
			q.Loop = m.Loop < static_cast<int>(q.Items.size()) ? m.Loop : -1;
			q.Release = m.Release < static_cast<int>(q.Items.size()) ? m.Release : -1;
		}
		R.Sounds.push_back(Sd);
	}
}

// The mode's tag: none for a key in both modes ("C (major and minor)"), whose
// words find it either way, or for no clear key
void AddModeTag(Reference &R)
{
	if (Contains(R.Key, "major and minor"))
		return;
	if (Contains(R.Key, "minor"))
		AddTag(R.Tags, "minor");
	else if (Contains(R.Key, "major"))
		AddTag(R.Tags, "major");
}

// Tags for the tempo, the mode and the chips
void AddMusicTags(Reference &R)
{
	if (R.Bpm > 0)
		AddTag(R.Tags, R.Bpm < 100 ? "slow" : (R.Bpm < 140 ? "moderate" : "fast"));
	AddModeTag(R);
	const std::vector<std::string> Chips = Words(R.Chips);
	for (size_t i = 0; i < Chips.size(); ++i)
		AddTag(R.Tags, Chips[i]);
}

// The tags an NSF's study gives its reference: the maker, the tempo, the
// mode, the chips and the techniques
void AddNsfStudyTags(Reference &R)
{
	const std::string Maker = Lowercase(R.Copyright + " " + R.Composer);
	static const char *const MAKERS[] = { "konami", "capcom", "nintendo", "sunsoft", "namco", "square", "enix", "taito", "tecmo",
		"hudson", "jaleco", "natsume", "irem", "rare", "technos", "bandai", "data east", "sega", "ultra", "hal" };
	for (size_t i = 0; i < sizeof(MAKERS) / sizeof(MAKERS[0]); ++i) {
		const std::vector<std::string> In = Words(Maker);
		const std::string Want = MAKERS[i];
		if (Want.find(' ') != std::string::npos ? Contains(Maker, MAKERS[i]) : std::find(In.begin(), In.end(), Want) != In.end())
			AddTag(R.Tags, Want);
	}
	AddMusicTags(R);
	AddTechniqueTags(R);
}
}

Reference MakeReference(const nsf::NsfFile &File, int Track, const nsf::TrackStudy &Study, const std::string &Source,
	const std::string &Date)
{
	Reference R;
	R.Game = ValidUtf8(File.Title);
	R.Composer = ValidUtf8(File.Artist);
	R.Copyright = ValidUtf8(File.Copyright);
	R.Track = Track + 1;
	R.Id = ReferenceId(File.Title, R.Track);
	R.Title = ValidUtf8(Study.Name.empty() ? Format("Track %d", R.Track) : Study.Name);
	R.Source = Source;
	R.Added = Date;
	R.Chips = File.ChipNames();
	R.Bpm = Study.Bpm;
	R.Key = Study.Key;
	R.LoopBars = Study.LoopBars;
	R.Study = ValidUtf8(nsf::DescribeStudy(Study, false));
	AddStudySounds(R, Study);

	// Tags to find it by: the maker, the tempo, the mode, the chips and the techniques
	AddNsfStudyTags(R);
	return R;
}

void KeepOwnTags(Reference &R, const Reference &Before)
{
	Reference Old = Before;
	Old.Tags.clear();
	AddNsfStudyTags(Old);
	for (size_t t = 0; t < Before.Tags.size(); ++t) {
		const std::string &Tag = Before.Tags[t];
		if (std::find(Old.Tags.begin(), Old.Tags.end(), Tag) == Old.Tags.end() && std::find(R.Tags.begin(), R.Tags.end(), Tag) == R.Tags.end())
			R.Tags.push_back(Tag);
	}
	R.Notes = Before.Notes;
}

Reference MakeFtmReference(const nsf::Module &M, int Track, const nsf::TrackStudy &Study, const std::string &Source,
	const std::string &Date)
{
	Reference R;
	std::string Name = Source.substr(Source.find_last_of("\\/") == std::string::npos ? 0 : Source.find_last_of("\\/") + 1);
	if (Name.find('.') != std::string::npos)
		Name = Name.substr(0, Name.find_last_of('.'));
	R.Game = ValidUtf8(M.Title.empty() ? Name : M.Title);
	R.Composer = ValidUtf8(M.Author);
	R.Copyright = ValidUtf8(M.Copyright);
	R.Track = Track + 1;
	// A track keeps FamiTracker's name for a new one ("New song") when the module has one track: the module's title then
	const std::string TrackName = Track >= 0 && Track < static_cast<int>(M.Tracks.size()) ? M.Tracks[static_cast<size_t>(Track)].Name : "";
	R.Title = ValidUtf8(TrackName.empty() || (Lowercase(TrackName) == "new song" && M.Tracks.size() == 1) ? R.Game : TrackName);
	R.Id = Slug(R.Game, "module") + "-ftm" + (M.Tracks.size() > 1 ? Format("-%02d", R.Track) : std::string());
	R.Source = Source;
	R.Added = Date;
	static const char *const NAMES[] = { "VRC6", "VRC7", "FDS", "MMC5", "N163", "5B" };
	for (int i = 0; i < 6; ++i)
		if (M.Expansion & (1 << i))
			R.Chips += (R.Chips.empty() ? "" : ", ") + std::string(NAMES[i]);
	R.Bpm = Study.Bpm;
	R.Key = Study.Key;
	R.LoopBars = Study.LoopBars;
	R.Study = ValidUtf8(nsf::DescribeStudy(Study, false));
	AddStudySounds(R, Study);
	AddTag(R.Tags, "famitracker");
	AddMusicTags(R);
	AddTechniqueTags(R);
	return R;
}

namespace {

// Tags for the techniques its study found
void AddTechniqueTags(Reference &R)
{
	const std::string Text = Lowercase(R.Study);
	struct { const char *Found, *Tag; } const TECHNIQUES[] = {
		{ "duty click", "duty-click" }, { "bloom", "bloom" }, { "swell in slowly", "swells" },
		{ "echo built into", "envelope-echo" }, { "an echo:", "echo-channel" }, { "struck again", "restrike" },
		{ "a tremolo", "tremolo" }, { "decays in steps", "stepped-decay" }, { "slide into their pitch", "scoops" },
		{ "pitch flick", "pitch-flick" }, { "fall or turn", "falls" }, { "vibrato built", "vibrato" },
		{ "chords played as fast arpeggios", "arpeggio-chords" }, { "triangle thump", "triangle-thump" },
		{ "staccato:", "staccato" }, { "harmony in thirds", "thirds" }, { "harmony in sixths", "sixths" },
		{ "doubles", "doubling" }, { "notes traded", "hocket" }, { "play in rhythm together", "pulse-chords" },
		{ "syncopated", "syncopated" }, { "octave jumps", "octave-jumps" }, { "x.xx x.xx", "gallop" },
		{ "snare-like (on beats 2 and 4)", "backbeat" }, { "on the off-beats", "offbeat-snare" }, { "dpcm:", "dpcm-drums" },
		{ "dpcm drums:", "dpcm-drums" }, { "hardware sweep", "sweep" }, { "a sampled bass on the dpcm", "dpcm-bass" }, { "a sting, not music to loop", "sting" },
		{ "a sound effect rather than music", "sound-effect" },
		// Tracker modules'
		{ "written into the patterns (4xy)", "vibrato" }, { "glide into the next", "portamento" }, { "volume slides", "volume-slides" },
		{ "held notes (a pad)", "pads" }, { "(drums):", "drums" }, { " drums:", "drums" },
	};
	for (size_t i = 0; i < sizeof(TECHNIQUES) / sizeof(TECHNIQUES[0]); ++i)
		if (Contains(Text, TECHNIQUES[i].Found))
			AddTag(R.Tags, TECHNIQUES[i].Tag);
}

}	// namespace

Reference MakeModuleReference(const nsf::TrackerSong &Song, const nsf::TrackStudy &Study, const std::string &Source,
	const std::string &Date)
{
	Reference R;
	// The title, or else the file's name
	std::string Name = Source.substr(Source.find_last_of("\\/") == std::string::npos ? 0 : Source.find_last_of("\\/") + 1);
	if (Name.find('.') != std::string::npos)
		Name = Name.substr(0, Name.find_last_of('.'));
	R.Title = ValidUtf8(Song.Title.empty() ? Name : Song.Title);
	R.Id = ModuleReferenceId(R.Title, Song.Format);
	R.Track = 0;
	R.Source = Source;
	R.Added = Date;
	R.Bpm = Study.Bpm;
	R.Key = Study.Key;
	R.LoopBars = Study.LoopBars;
	R.Study = ValidUtf8(nsf::DescribeStudy(Study, false));
	if (Song.Format == "MIDI") {
		AddTag(R.Tags, "midi");
		R.Copyright = ValidUtf8(Song.Copyright);
	}
	else {
		AddTag(R.Tags, Lowercase(Song.Format));
		AddTag(R.Tags, "tracker-module");
	}
	if (!Study.Meter.empty() && Study.Meter != "4/4")
		AddTag(R.Tags, Study.Meter);
	if (R.Bpm > 0)
		AddTag(R.Tags, R.Bpm < 100 ? "slow" : (R.Bpm < 140 ? "moderate" : "fast"));
	AddModeTag(R);
	AddTechniqueTags(R);
	return R;
}

std::string ReferenceFile(const Reference &R)
{
	Json J = Json::MakeObject();
	J.Set("reference", REFERENCE_FORMAT);
	J.Set("id", R.Id);
	J.Set("title", R.Title);
	J.Set("game", R.Game);
	J.Set("composer", R.Composer);
	J.Set("copyright", R.Copyright);
	J.Set("track", R.Track);
	J.Set("source", ValidUtf8(R.Source));
	J.Set("added", R.Added);
	J.Set("chips", R.Chips);
	Json Tags = Json::MakeArray();
	for (size_t i = 0; i < R.Tags.size(); ++i)
		Tags.Push(R.Tags[i]);
	J.Set("tags", Tags);
	J.Set("notes", R.Notes);
	J.Set("bpm", Json::NumberText(FormatNumber(R.Bpm)));
	J.Set("key", R.Key);
	J.Set("loop_bars", R.LoopBars);
	Json Study = Json::MakeArray();
	const std::vector<std::string> StudyLines = Lines(R.Study);
	for (size_t i = 0; i < StudyLines.size(); ++i)
		Study.Push(StudyLines[i]);
	J.Set("study", Study);
	Json Sounds = Json::MakeArray();
	for (size_t i = 0; i < R.Sounds.size(); ++i) {
		const ReferenceSound &s = R.Sounds[i];
		Json Sd = Json::MakeObject();
		Sd.Set("channel", s.Channel);
		Sd.Set("role", s.Role);
		Sd.Set("chip", s.Chip);
		Sd.Set("share", s.Share);
		Sd.Set("words", s.Words);
		if (s.Noise)
			Sd.Set("noise", true);
		if (s.Triangle)
			Sd.Set("triangle", true);
		if (s.Sawtooth)
			Sd.Set("sawtooth", true);
		for (int t = 0; t < 5; ++t)
			if (!s.Sequences[t].Items.empty())
				Sd.Set(SEQUENCE_NAMES[t], SequenceJson(s.Sequences[t]));
		if (!s.FdsWave.empty()) {
			Json Wave = Json::MakeArray(), Mod = Json::MakeArray();
			for (size_t k = 0; k < s.FdsWave.size(); ++k)
				Wave.Push(s.FdsWave[k]);
			for (size_t k = 0; k < s.FdsMod.size(); ++k)
				Mod.Push(s.FdsMod[k]);
			Sd.Set("fds_wave", Wave);
			Sd.Set("fds_modulation", Mod);
			Sd.Set("modulation_speed", s.ModSpeed);
			Sd.Set("modulation_depth", s.ModDepth);
			Sd.Set("modulation_delay", s.ModDelay);
		}
		if (!s.N163Waves.empty()) {
			Json Waves = Json::MakeArray();
			for (size_t w = 0; w < s.N163Waves.size(); ++w) {
				Json Wave = Json::MakeArray();
				for (size_t k = 0; k < s.N163Waves[w].size(); ++k)
					Wave.Push(s.N163Waves[w][k]);
				Waves.Push(Wave);
			}
			Sd.Set("n163_waves", Waves);
			if (!s.N163WaveSteps.Items.empty())
				Sd.Set("n163_wave_sequence", SequenceJson(s.N163WaveSteps));
		}
		Sounds.Push(Sd);
	}
	J.Set("sounds", Sounds);
	std::string Out;
	Pretty(J, 0, Out);
	return Out + "\n";
}

bool ReadReferenceFile(const std::string &Text, Reference &Out, std::string &Error)
{
	Json J;
	if (!ParseJson(Text, J, &Error))
		return false;
	if (!J.IsObject() || J.Get("reference") == 0 || TextOf(J, "id").empty()) {
		Error = "it isn't a reference";
		return false;
	}
	if (IntOf(J, "reference", 0) > REFERENCE_FORMAT) {
		Error = "it was written by a newer FamiTracker";
		return false;
	}
	Out = Reference();
	Out.Id = TextOf(J, "id");
	Out.Title = TextOf(J, "title");
	Out.Game = TextOf(J, "game");
	Out.Composer = TextOf(J, "composer");
	Out.Copyright = TextOf(J, "copyright");
	Out.Track = IntOf(J, "track", 0);
	Out.Source = TextOf(J, "source");
	Out.Added = TextOf(J, "added");
	Out.Chips = TextOf(J, "chips");
	Out.Tags = TextsOf(J, "tags");
	Out.Notes = TextOf(J, "notes");
	const Json *pBpm = J.Get("bpm");
	Out.Bpm = pBpm != 0 && pBpm->IsNumber() ? pBpm->Number() : 0;
	Out.Key = TextOf(J, "key");
	Out.LoopBars = IntOf(J, "loop_bars", 0);
	Out.Study = Join(TextsOf(J, "study"), "\n");
	const Json *pSounds = J.Get("sounds");
	if (pSounds != 0 && pSounds->IsArray())
		for (size_t i = 0; i < pSounds->Size(); ++i) {
			const Json &Sd = pSounds->At(i);
			if (!Sd.IsObject())
				continue;
			ReferenceSound s;
			s.Channel = TextOf(Sd, "channel");
			s.Role = TextOf(Sd, "role");
			s.Chip = TextOf(Sd, "chip");
			if (s.Chip.empty())
				s.Chip = "2A03";
			s.Share = IntOf(Sd, "share", 0);
			s.Words = TextOf(Sd, "words");
			s.Noise = BoolOf(Sd, "noise");
			s.Triangle = BoolOf(Sd, "triangle");
			s.Sawtooth = BoolOf(Sd, "sawtooth");
			for (int t = 0; t < 5; ++t) {
				const Json *pSeq = Sd.Get(SEQUENCE_NAMES[t]);
				if (pSeq != 0 && pSeq->IsObject())
					s.Sequences[t] = ReadSequence(*pSeq);
			}
			// An FDS sound's wave and modulator, when they're whole
			const Json *pWave = Sd.Get("fds_wave"), *pMod = Sd.Get("fds_modulation");
			if (pWave != 0 && pWave->IsArray() && pWave->Size() == static_cast<size_t>(FDS_WAVE_SIZE)) {
				for (size_t k = 0; k < pWave->Size(); ++k)
					s.FdsWave.push_back(pWave->At(k).IsNumber() ? std::max(0, std::min(63, static_cast<int>(pWave->At(k).Number()))) : 32);
				if (pMod != 0 && pMod->IsArray() && pMod->Size() == static_cast<size_t>(FDS_MOD_SIZE))
					for (size_t k = 0; k < pMod->Size(); ++k)
						s.FdsMod.push_back(pMod->At(k).IsNumber() ? std::max(0, std::min(7, static_cast<int>(pMod->At(k).Number()))) : 0);
				s.ModSpeed = std::max(0, std::min(4095, IntOf(Sd, "modulation_speed", 0)));
				s.ModDepth = std::max(0, std::min(63, IntOf(Sd, "modulation_depth", 0)));
				s.ModDelay = std::max(0, std::min(255, IntOf(Sd, "modulation_delay", 0)));
			}
			// An N163 sound's waves and wave sequence, when they're whole
			const Json *pN163 = Sd.Get("n163_waves");
			if (pN163 != 0 && pN163->IsArray()) {
				std::vector<std::vector<int> > Waves;
				for (size_t w = 0; w < pN163->Size() && w <= 16; ++w) {
					const Json &Wave = pN163->At(w);
					std::vector<int> Samples;
					for (size_t k = 0; Wave.IsArray() && k < Wave.Size() && k <= 32; ++k)
						Samples.push_back(Wave.At(k).IsNumber() ? std::max(0, std::min(15, static_cast<int>(Wave.At(k).Number()))) : 8);
					Waves.push_back(Samples);
				}
				if (WholeN163Waves(Waves)) {
					s.N163Waves = Waves;
					const Json *pSteps = Sd.Get("n163_wave_sequence");
					if (pSteps != 0 && pSteps->IsObject() && Waves.size() > 1) {
						s.N163WaveSteps = ReadSequence(*pSteps);
						for (size_t k = 0; k < s.N163WaveSteps.Items.size(); ++k)
							s.N163WaveSteps.Items[k] = std::max(0, std::min(static_cast<int>(Waves.size()) - 1, s.N163WaveSteps.Items[k]));
					}
				}
			}
			Out.Sounds.push_back(s);
		}
	return true;
}

std::string ReferenceLine(const Reference &R)
{
	std::string Out = R.Id + ": " + R.Title;
	std::vector<std::string> Where;
	if (!R.Game.empty())
		Where.push_back(R.Game);
	if (R.Track > 0)
		Where.push_back(Format("track %d", R.Track));
	if (!Where.empty())
		Out += " (" + Join(Where, ", ") + ")";
	std::vector<std::string> What;
	if (R.Bpm > 0)
		What.push_back(FormatNumber(static_cast<double>(static_cast<int>(R.Bpm + 0.5))) + " BPM");
	if (!R.Key.empty())
		What.push_back(R.Key);
	if (R.LoopBars > 0)
		What.push_back(Format("%s %d-bar loop", R.LoopBars == 8 || R.LoopBars == 11 || R.LoopBars == 18 || (R.LoopBars >= 80 && R.LoopBars <= 89) ? "an" : "a",
			R.LoopBars));
	if (!What.empty())
		Out += ", " + Join(What, ", ");
	if (!R.Tags.empty())
		Out += " [" + Join(R.Tags, ", ") + "]";
	return Out;
}

std::string ReferenceText(const Reference &R)
{
	std::string Out = R.Title;
	if (!R.Game.empty())
		Out += " from " + R.Game + (R.Track > 0 ? Format(" (track %d)", R.Track) : std::string());
	if (!R.Composer.empty())
		Out += ", by " + R.Composer;
	if (!R.Copyright.empty())
		Out += " (" + R.Copyright + ")";
	Out += ". Reference \"" + R.Id + "\"";
	if (!R.Added.empty())
		Out += ", studied " + R.Added;
	if (!R.Source.empty())
		Out += " from " + R.Source;
	Out += ".\n";
	if (!R.Tags.empty())
		Out += "Tags: " + Join(R.Tags, ", ") + "\n";
	if (!R.Notes.empty())
		Out += "Notes: " + R.Notes + "\n";
	// The study without its first line, the title
	const std::vector<std::string> StudyLines = Lines(R.Study);
	for (size_t i = 0; i < StudyLines.size(); ++i) {
		if (i == 0 && StudyLines[i] == R.Title)
			continue;
		if (StudyLines[i].find("  Sounds:") == 0)
			break;		// listed below, numbered, with their sequences
		Out += StudyLines[i] + "\n";
	}
	if (!R.Sounds.empty()) {
		Out += "Sounds (reference_sound adds one to the song as an instrument, by its number):\n";
		for (size_t i = 0; i < R.Sounds.size(); ++i) {
			const ReferenceSound &s = R.Sounds[i];
			Out += Format("  %d. %s, %s (%d%% of its notes, %s %s instrument): ", static_cast<int>(i + 1), s.Channel.c_str(), s.Role.c_str(),
				s.Share, s.Chip == "FDS" || s.Chip == "N163" || s.Chip == "MMC5" ? "an" : "a", s.Chip.c_str()) + s.Words + "\n";
			std::vector<std::string> Seqs;
			for (int t = 0; t < 5; ++t)
				if (!s.Sequences[t].Items.empty())
					Seqs.push_back(std::string(t == SEQUENCE_HIPITCH ? "hi-pitch" : SEQUENCE_NAMES[t]) +
						(t == SEQUENCE_ARPEGGIO && s.Sequences[t].Fixed ? " (fixed)" : "") + " " + SequenceText(s.Sequences[t]));
			if (!Seqs.empty())
				Out += "     " + Join(Seqs, "; ") + "\n";
			if (!s.FdsWave.empty()) {
				std::string Wave;
				for (size_t k = 0; k < s.FdsWave.size(); ++k)
					Wave += (k == 0 ? "" : " ") + std::to_string(s.FdsWave[k]);
				Out += "     FDS wave " + Wave + Format("; modulator speed %d, depth %d, delay %d", s.ModSpeed, s.ModDepth,
					s.ModDelay) + "\n";
			}
			for (size_t w = 0; w < s.N163Waves.size(); ++w) {
				std::string Wave;
				for (size_t k = 0; k < s.N163Waves[w].size(); ++k)
					Wave += (k == 0 ? "" : " ") + std::to_string(s.N163Waves[w][k]);
				Out += (s.N163Waves.size() == 1 ? std::string("     N163 wave ") : Format("     N163 wave %d: ", static_cast<int>(w))) + Wave +
					(w + 1 == s.N163Waves.size() && !s.N163WaveSteps.Items.empty() ? "; wave sequence " + SequenceText(s.N163WaveSteps) :
					std::string()) + "\n";
			}
		}
	}
	return Out;
}

int ReferenceMatch(const Reference &R, const std::string &Query)
{
	std::vector<std::string> Asked;
	const std::vector<std::string> All = Words(Query);
	for (size_t i = 0; i < All.size(); ++i)
		if (!Filler(All[i]))
			Asked.push_back(All[i]);
	if (Asked.empty())
		return 0;
	const Indexed Ix = IndexOf(R);
	int Score = 0, Matched = 0;
	for (size_t i = 0; i < Asked.size(); ++i) {
		int Best = 0;
		std::map<std::string, int>::const_iterator it = Ix.Strong.find(Asked[i]);
		if (it != Ix.Strong.end())
			Best = it->second;
		// Words in the study: "echo", "gallop", "thirds"; a plural finds the singular
		std::string Stem = Asked[i];
		if (Stem.size() > 3 && Stem[Stem.size() - 1] == 's')
			Stem.erase(Stem.size() - 1);
		const int InStudy = static_cast<int>(std::min<size_t>(3, CountOf(Ix.Study, Stem)));
		Best = std::max(Best, InStudy > 0 ? 2 + InStudy : 0);
		if (Best > 0)
			++Matched;
		Score += Best;
	}
	return Matched == 0 ? 0 : Score * Matched / static_cast<int>(Asked.size());
}

namespace {

// A pitch sequence's steps (each frame's added to the period or register) in another chip's units, Factor of them to
// one: the running total scaled and rounded, so a loop that sums to nothing still does, and small steps add up
std::vector<int> ScaledSteps(const std::vector<int> &Items, double Factor)
{
	std::vector<int> Out;
	double Total = 0.0;
	int Before = 0;
	for (size_t k = 0; k < Items.size(); ++k) {
		Total += Items[k] * Factor;
		const int Now = static_cast<int>(std::floor(Total + 0.5));
		Out.push_back(std::max(-127, std::min(127, Now - Before)));
		Before = Now;
	}
	return Out;
}

// A studied volume whose release ends above 0, as the instruments made from it play it (ReleaseFadeFrames): the fade
// from its last level, halving a frame, after the items an instrument holds (MAX_ITEMS), or in place of its last items
// when that's full (never the release point's own); *pFaded is how many frames it takes. The rest as it was
ReferenceSequence FadedRelease(const ReferenceSequence &Volume, int *pFaded = 0)
{
	ReferenceSequence Out = Volume;
	if (pFaded != 0)
		*pFaded = 0;
	if (Out.Items.size() > static_cast<size_t>(MAX_ITEMS))
		Out.Items.resize(static_cast<size_t>(MAX_ITEMS));
	if (Out.Release < 0 || Out.Release >= static_cast<int>(Out.Items.size()) || Out.Items.back() <= 0)
		return Out;
	std::vector<int> Fade;
	for (int Level = Out.Items.back() / 2; ; Level /= 2) {
		Fade.push_back(Level);
		if (Level == 0)
			break;
	}
	const size_t Keep = std::max(static_cast<size_t>(Out.Release + 1), std::min(Out.Items.size(), static_cast<size_t>(MAX_ITEMS) -
		Fade.size()));
	Out.Items.resize(Keep);
	Out.Items.insert(Out.Items.end(), Fade.begin(), Fade.end());
	if (Out.Items.size() > static_cast<size_t>(MAX_ITEMS)) {
		Out.Items.resize(static_cast<size_t>(MAX_ITEMS));
		Out.Items.back() = 0;
	}
	if (pFaded != 0)
		*pFaded = static_cast<int>(Fade.size());
	return Out;
}

}	// namespace

int ReleaseFadeFrames(const ReferenceSound &Sound)
{
	int Faded = 0;
	FadedRelease(Sound.Sequences[SEQUENCE_VOLUME], &Faded);
	return Faded;
}

Preset ReferencePreset(const ReferenceSound &Sound, const std::string &Name)
{
	Preset P;
	P.Name = Name;
	P.Description = Sound.Words;
	P.Drum = Sound.Noise;
	P.Vrc7Patch = 0;
	// An FDS sound's volume goes 0-32 and its pitch sequence moves a frequency register, up being sharper, about four of
	// its steps to one of the 2A03's period where they sound alike; it has no duty: one as bright as its wave
	const bool Fds = Sound.Chip == "FDS" && !Sound.Noise;
	if (Fds && Sound.Sequences[SEQUENCE_DUTY].Items.empty()) {
		const int Duty = !Sound.FdsWave.empty() && FdsWaveBrightness(Sound.FdsWave) > 0.3 ? 1 : 2;
		PresetSequence S;
		S.Type = SEQUENCE_DUTY;
		S.Items.push_back(Duty);
		S.Loop = S.Release = -1;
		S.Setting = SEQUENCE_ABSOLUTE;
		P.Sequences2A03.push_back(S);
		S.Items[0] = Vrc6Duty(Duty);
		P.SequencesVRC6.push_back(S);
	}
	for (int t = 0; t < 5; ++t) {
		// a release that ends above 0 fades out (it would drone on)
		const ReferenceSequence q = t == SEQUENCE_VOLUME ? FadedRelease(Sound.Sequences[t]) : Sound.Sequences[t];
		if (q.Items.empty())
			continue;
		PresetSequence S;
		S.Type = t;
		std::vector<int> Items(q.Items.begin(), q.Items.begin() + static_cast<std::ptrdiff_t>(std::min(q.Items.size(),
			static_cast<size_t>(MAX_ITEMS))));
		if (Fds && t == SEQUENCE_VOLUME)
			for (size_t k = 0; k < Items.size(); ++k)
				Items[k] = FdsVolumeAs15(Items[k]);
		else if (Fds && t == SEQUENCE_PITCH)
			Items = ScaledSteps(Items, -0.25);
		for (size_t k = 0; k < Items.size(); ++k)
			S.Items.push_back(std::max(-128, std::min(127, Items[k])));
		S.Loop = q.Loop < static_cast<int>(S.Items.size()) ? q.Loop : -1;
		S.Release = q.Release < static_cast<int>(S.Items.size()) ? q.Release : -1;
		S.Setting = t == SEQUENCE_ARPEGGIO && q.Fixed ? SEQUENCE_FIXED : SEQUENCE_ABSOLUTE;
		if (Sound.Noise) {
			P.Sequences2A03.push_back(S);
			continue;
		}
		PresetSequence On2A03 = S, OnVrc6 = S;
		if (t == SEQUENCE_DUTY) {
			for (size_t k = 0; k < S.Items.size(); ++k) {
				if (Sound.Chip == "VRC6")
					On2A03.Items[k] = Duty2A03(S.Items[k]);
				else
					OnVrc6.Items[k] = Vrc6Duty(S.Items[k]);
			}
		}
		P.Sequences2A03.push_back(On2A03);
		P.SequencesVRC6.push_back(OnVrc6);
	}
	return P;
}

FdsSound ReferenceFdsSound(const ReferenceSound &Sound, const std::string &Name, std::string &How)
{
	FdsSound F;
	F.Name = Name;
	F.Description = Sound.Words;
	F.Modulation.assign(FDS_MOD_SIZE, 0);
	const bool Fds = Sound.Chip == "FDS";
	if (Fds && Sound.FdsWave.size() == static_cast<size_t>(FDS_WAVE_SIZE)) {
		for (size_t k = 0; k < Sound.FdsWave.size(); ++k)
			F.Wave.push_back(std::max(0, std::min(63, Sound.FdsWave[k])));
		for (size_t k = 0; k < Sound.FdsMod.size() && k < static_cast<size_t>(FDS_MOD_SIZE); ++k)
			F.Modulation[k] = std::max(0, std::min(7, Sound.FdsMod[k]));
		F.ModSpeed = std::max(0, std::min(4095, Sound.ModSpeed));
		F.ModDepth = std::max(0, std::min(63, Sound.ModDepth));
		F.ModDelay = std::max(0, std::min(255, Sound.ModDelay));
		How = F.ModDepth > 0 && F.ModSpeed > 0 ? Format("its own wave and modulator (speed %d, depth %d%s)", F.ModSpeed, F.ModDepth,
			F.ModDelay > 0 ? Format(", from %d frames in", F.ModDelay).c_str() : "") : "its own wave";
	}
	else if (Fds) {
		F.Wave = FdsWaveOfWord("round");
		How = "a round wave: the reference was studied before FDS waves were kept, so study its NSF again (study_nsf) for its "
			"own wave and modulator";
	}
	else if (Sound.Sawtooth) {
		F.Wave = FdsWaveOfWord("bright");
		How = "a sawtooth wave, as its channel played it";
	}
	else if (Sound.Triangle) {
		F.Wave = FdsWaveOfWord("triangle");
		How = "a triangle wave, as its channel played it";
	}
	else if (Sound.Chip == "N163" && !Sound.N163Waves.empty()) {
		// Its first wave, each sample held over the FDS's 64 (0-15 counted 0-63)
		const std::vector<int> &Wave = Sound.N163Waves[0];
		for (int k = 0; k < FDS_WAVE_SIZE; ++k)
			F.Wave.push_back((std::max(0, std::min(15, Wave[static_cast<size_t>(k) * Wave.size() / FDS_WAVE_SIZE])) * 63 + 7) / 15);
		How = Sound.N163Waves.size() > 1 ? "its first N163 wave (the FDS plays one)" : "its N163 wave";
	}
	else if (Sound.Chip == "N163") {
		F.Wave = FdsWaveOfWord("round");
		How = "a round wave (the reference keeps no N163 wave)";
	}
	else {
		// A pulse: the width of its first duty
		const std::vector<int> &Duty = Sound.Sequences[SEQUENCE_DUTY].Items;
		int Width = 32;
		if (!Duty.empty())
			Width = Sound.Chip == "VRC6" ? 4 * (std::max(0, std::min(7, Duty[0])) + 1) :
				(Duty[0] == 0 ? 8 : Duty[0] == 1 ? 16 : Duty[0] == 3 ? 48 : 32);
		F.Wave = FdsPulseWave(Width);
		How = Format("a %s pulse wave, its duty's", Width == 8 ? "12.5%" : Format("%d%%", Width * 100 / 64).c_str());
	}
	static const int TYPES[] = { SEQUENCE_VOLUME, SEQUENCE_ARPEGGIO, SEQUENCE_PITCH };
	for (int i = 0; i < 3; ++i) {
		// a release that ends above 0 fades out (it would drone on)
		const ReferenceSequence q = TYPES[i] == SEQUENCE_VOLUME ? FadedRelease(Sound.Sequences[TYPES[i]]) : Sound.Sequences[TYPES[i]];
		if (q.Items.empty())
			continue;
		PresetSequence S;
		S.Type = TYPES[i];
		S.Items.assign(q.Items.begin(), q.Items.begin() + static_cast<std::ptrdiff_t>(std::min(q.Items.size(), static_cast<size_t>(MAX_ITEMS))));
		if (S.Type == SEQUENCE_VOLUME)
			for (size_t k = 0; k < S.Items.size(); ++k)
				S.Items[k] = Fds ? std::max(0, std::min(32, S.Items[k])) : (S.Items[k] <= 0 ? 0 :
					std::max(1, (std::min(15, S.Items[k]) * FDS_VOLUME_MAX + 7) / 15));
		else if (S.Type == SEQUENCE_PITCH && !Fds)
			S.Items = ScaledSteps(S.Items, -4.0);
		S.Loop = q.Loop < static_cast<int>(S.Items.size()) ? q.Loop : -1;
		S.Release = q.Release < static_cast<int>(S.Items.size()) ? q.Release : -1;
		S.Setting = S.Type == SEQUENCE_ARPEGGIO && q.Fixed ? SEQUENCE_FIXED : SEQUENCE_ABSOLUTE;
		F.Sequences.push_back(S);
	}
	if (!Fds && !Sound.Sequences[SEQUENCE_HIPITCH].Items.empty())
		How += " (its hi-pitch sequence left out)";
	return F;
}

namespace {

// A pulse's width in sixteenths: a 2A03 duty (12.5%, 25%, 50%, 75%), a VRC6 one (n + 1 sixteenths); a value neither
// knows (a 5B sound's mode) as a square
int PulseSixteenths(int Duty, bool Vrc6)
{
	if (Vrc6)
		return (Duty & 7) + 1;
	static const int WIDTHS[4] = { 2, 4, 8, 12 };
	return Duty >= 0 && Duty < 4 ? WIDTHS[Duty] : 8;
}

std::string WidthText(int Sixteenths)
{
	return Format("%g%%", Sixteenths * 100.0 / 16.0);
}

}	// namespace

WaveSound ReferenceWaveSound(const ReferenceSound &Sound, const std::string &Name, std::string &How)
{
	WaveSound W;
	W.Name = Name;
	W.Description = Sound.Words;
	const bool Fds = Sound.Chip == "FDS", N163 = Sound.Chip == "N163", Vrc6 = Sound.Chip == "VRC6";
	// The waves, and the sequence that picks among them (an N163 instrument's "duty" sequence)
	ReferenceSequence Steps;
	if (N163 && !Sound.N163Waves.empty()) {
		for (size_t w = 0; w < Sound.N163Waves.size() && w < static_cast<size_t>(N163_MAX_WAVES); ++w)
			W.Waves.push_back(N163WaveOf(Sound.N163Waves[w], 15));
		if (W.Waves.size() > 1)
			Steps = Sound.N163WaveSteps;
		const int Size = static_cast<int>(Sound.N163Waves[0].size());
		How = W.Waves.size() == 1 ? std::string("its own wave") : !Steps.Items.empty() ? Format("its own %d waves, in turn as its wave "
			"sequence played them", static_cast<int>(W.Waves.size())) : Format("its own %d waves (the first, or the one Vxx picks)",
			static_cast<int>(W.Waves.size()));
		if (Size != N163_WAVE_SIZE)
			How += Format(", resampled from %d samples to the 16 that play in tune", Size);
	}
	else if (N163) {
		W.Waves.push_back(N163WaveOf(FdsWaveOfWord("round"), 63));
		How = "a round wave: the reference was studied before N163 waves were kept, so study its NSF or module again (study_nsf, "
			"study_module) for its own";
	}
	else if (Fds && Sound.FdsWave.size() == static_cast<size_t>(FDS_WAVE_SIZE)) {
		W.Waves.push_back(N163WaveOf(Sound.FdsWave, 63));
		How = "its FDS wave, resampled to the N163's 16 samples";
		if (Sound.ModDepth > 0 && Sound.ModSpeed > 0)
			How += " (without its modulator, which the N163 hasn't)";
	}
	else if (Fds) {
		W.Waves.push_back(N163WaveOf(FdsWaveOfWord("round"), 63));
		How = "a round wave: the reference was studied before FDS waves were kept, so study its NSF again (study_nsf) for its own";
	}
	else if (Sound.Sawtooth) {
		W.Waves.push_back(N163SawtoothWave());
		How = "a sawtooth wave, as its channel played it";
	}
	else if (Sound.Triangle) {
		W.Waves.push_back(N163TriangleWave());
		How = "a triangle wave, as its channel played it";
	}
	else {
		// A pulse: a wave for each width its duty sequence steps through (as many as the 2A03 has 4 and the VRC6 8), the
		// sequence picking them in turn; 50% without one
		const ReferenceSequence &Duty = Sound.Sequences[SEQUENCE_DUTY];
		std::vector<int> Widths;
		for (size_t k = 0; k < Duty.Items.size(); ++k) {
			const int Width = PulseSixteenths(Duty.Items[k], Vrc6);
			std::vector<int>::const_iterator Found = std::find(Widths.begin(), Widths.end(), Width);
			if (Found == Widths.end() && Widths.size() < static_cast<size_t>(N163_MAX_WAVES)) {
				Widths.push_back(Width);
				Found = Widths.end() - 1;
			}
			Steps.Items.push_back(Found == Widths.end() ? 0 : static_cast<int>(Found - Widths.begin()));
		}
		if (Widths.empty())
			Widths.push_back(8);
		for (size_t i = 0; i < Widths.size(); ++i)
			W.Waves.push_back(N163PulseWave(Widths[i]));
		if (Widths.size() > 1) {
			Steps.Loop = Duty.Loop;
			Steps.Release = Duty.Release;
			std::string Named;
			for (size_t i = 0; i < Widths.size(); ++i)
				Named += (i == 0 ? "" : i + 1 == Widths.size() ? " and " : ", ") + WidthText(Widths[i]);
			How = "pulse waves of its duties (" + Named + "), in turn as its duty sequence played them";
		}
		else {
			Steps = ReferenceSequence();
			How = Duty.Items.empty() ? std::string("a 50% pulse wave") : "a " + WidthText(Widths[0]) + " pulse wave, its duty's";
		}
	}

	// Its volume, arpeggio, pitch and hi-pitch: the N163's steps of pitch move a frequency (up is sharper), as the FDS's
	// do, where the 2A03's and the VRC6's move a period: theirs turned around, about as big at A-3 (440 Hz) with four
	// channels (a period of 253, a frequency of 241); the FDS's four times finer
	static const int TYPES[] = { SEQUENCE_VOLUME, SEQUENCE_ARPEGGIO, SEQUENCE_PITCH, SEQUENCE_HIPITCH };
	for (int i = 0; i < 4; ++i) {
		// a release that ends above 0 fades out (it would drone on)
		const ReferenceSequence q = TYPES[i] == SEQUENCE_VOLUME ? FadedRelease(Sound.Sequences[TYPES[i]]) : Sound.Sequences[TYPES[i]];
		if (q.Items.empty())
			continue;
		PresetSequence S;
		S.Type = TYPES[i];
		S.Items.assign(q.Items.begin(), q.Items.begin() + static_cast<std::ptrdiff_t>(std::min(q.Items.size(), static_cast<size_t>(MAX_ITEMS))));
		if (S.Type == SEQUENCE_VOLUME)
			for (size_t k = 0; k < S.Items.size(); ++k)
				S.Items[k] = Fds ? FdsVolumeAs15(S.Items[k]) : Sound.Triangle ? (S.Items[k] > 0 ? 15 : 0) : std::max(0, std::min(15, S.Items[k]));
		else if ((S.Type == SEQUENCE_PITCH || S.Type == SEQUENCE_HIPITCH) && !N163)
			S.Items = ScaledSteps(S.Items, Fds ? 0.25 : -1.0);
		S.Loop = q.Loop < static_cast<int>(S.Items.size()) ? q.Loop : -1;
		S.Release = q.Release < static_cast<int>(S.Items.size()) ? q.Release : -1;
		S.Setting = S.Type == SEQUENCE_ARPEGGIO && q.Fixed ? SEQUENCE_FIXED : SEQUENCE_ABSOLUTE;
		W.Sequences.push_back(S);
	}
	if (W.Waves.size() > 1 && !Steps.Items.empty()) {
		PresetSequence S;
		S.Type = SEQUENCE_DUTY;
		for (size_t k = 0; k < Steps.Items.size() && k < static_cast<size_t>(MAX_ITEMS); ++k)
			S.Items.push_back(std::max(0, std::min(static_cast<int>(W.Waves.size()) - 1, Steps.Items[k])));
		S.Loop = Steps.Loop < static_cast<int>(S.Items.size()) ? Steps.Loop : -1;
		S.Release = Steps.Release < static_cast<int>(S.Items.size()) ? Steps.Release : -1;
		S.Setting = 0;
		W.Sequences.push_back(S);
	}
	return W;
}

namespace {

// The most letters a title keeps in a sound's name (a number that ended it may come after them)
const size_t TITLE_LETTERS = 20;

std::string TrimTitle(const std::string &Text)
{
	const size_t First = Text.find_first_not_of(" \t");
	if (First == std::string::npos)
		return std::string();
	return Text.substr(First, Text.find_last_not_of(" \t") - First + 1);
}

// The number that ends a title, as a name keeps it ("1", "3" for "-3-", "II"); "" for none
std::string EndingNumber(const std::string &Title)
{
	const std::string Trimmed = TrimTitle(Title);
	const size_t Space = Trimmed.find_last_of(" \t");
	std::string Token = Space == std::string::npos ? Trimmed : Trimmed.substr(Space + 1);
	const std::string Marks = "-()[]#.~";
	while (!Token.empty() && Marks.find(Token[0]) != std::string::npos)
		Token.erase(0, 1);
	while (!Token.empty() && Marks.find(Token[Token.size() - 1]) != std::string::npos)
		Token.erase(Token.size() - 1);
	if (Token.empty())
		return std::string();
	bool Digits = Token.size() <= 3;
	for (size_t i = 0; i < Token.size() && Digits; ++i)
		Digits = std::isdigit(static_cast<unsigned char>(Token[i])) != 0;
	static const char *const ROMAN[] = { "II", "III", "IV", "VI", "VII", "VIII", "IX" };
	for (size_t i = 0; i < sizeof(ROMAN) / sizeof(ROMAN[0]) && !Digits; ++i)
		Digits = Token == ROMAN[i];
	return Digits ? Token : std::string();
}

// A word a name doesn't end with: it only leads on to the next ("Deep Under the")
bool LeadsOn(const std::string &Word)
{
	static const char *const WORDS[] = { "the", "a", "an", "of", "and", "or", "to", "in", "on", "at", "for", "with", "from", "by",
		"&", "-" };
	std::string Lower;
	for (size_t i = 0; i < Word.size(); ++i)
		Lower += static_cast<char>(std::tolower(static_cast<unsigned char>(Word[i])));
	for (size_t i = 0; i < sizeof(WORDS) / sizeof(WORDS[0]); ++i)
		if (Lower == WORDS[i])
			return true;
	return false;
}

}	// namespace

std::string ShortTitle(const std::string &Title)
{
	// The title before its parenthesis, bracket or subtitle in tildes: "Vampire Killer (Courtyard)" by "Vampire Killer"
	std::string Main = TrimTitle(Title);
	const size_t Mark = Main.find_first_of("([~");
	if (Mark != std::string::npos && !TrimTitle(Main.substr(0, Mark)).empty())
		Main = TrimTitle(Main.substr(0, Mark));
	// A long one up to where its first clause ends ("Stage 4 - Mechanic Factory" by "Stage 4")...
	if (Main.size() > TITLE_LETTERS) {
		static const char *const CLAUSES[] = { " - ", ", ", ": ", " / " };
		size_t Cut = std::string::npos;
		for (size_t c = 0; c < sizeof(CLAUSES) / sizeof(CLAUSES[0]); ++c) {
			const size_t At = Main.find(CLAUSES[c]);
			if (At != std::string::npos && At >= 3)
				Cut = std::min(Cut, At);
		}
		if (Cut != std::string::npos)
			Main = TrimTitle(Main.substr(0, Cut));
	}
	// ...and the words from its start that fit, without a word at their end that only leads on to the next
	if (Main.size() > TITLE_LETTERS) {
		std::vector<std::string> Kept;
		size_t Letters = 0;
		size_t Start = 0;
		while (Start < Main.size()) {
			const size_t Space = Main.find(' ', Start);
			const std::string Word = Main.substr(Start, Space == std::string::npos ? std::string::npos : Space - Start);
			Start = Space == std::string::npos ? Main.size() : Space + 1;
			if (Word.empty())
				continue;
			if (Kept.empty() && Word.size() > TITLE_LETTERS) {
				Kept.push_back(Word.substr(0, TITLE_LETTERS));
				break;
			}
			if (!Kept.empty() && Letters + 1 + Word.size() > TITLE_LETTERS)
				break;
			Letters += (Kept.empty() ? 0 : 1) + Word.size();
			Kept.push_back(Word);
		}
		while (Kept.size() > 1 && LeadsOn(Kept.back()))
			Kept.pop_back();
		Main.clear();
		for (size_t k = 0; k < Kept.size(); ++k)
			Main += (k == 0 ? "" : " ") + Kept[k];
	}
	while (!Main.empty() && std::string(" -,:;/&").find(Main[Main.size() - 1]) != std::string::npos)
		Main.erase(Main.size() - 1);
	if (Main.empty())
		Main = TrimTitle(Title).substr(0, TITLE_LETTERS);
	// A number that ended the title stays, when the name lost it ("Hazardous Short 1", "The Everlasting 2")
	const std::string Number = EndingNumber(Title);
	if (!Number.empty() && EndingNumber(Main).empty())
		Main += " " + Number;
	return Main;
}

std::string ReferenceSoundName(const Reference &R, const ReferenceSound &Sound)
{
	// The title, or the start of it (ShortTitle); a numbered track by its game
	std::string Title = R.Title;
	const std::vector<std::string> TitleWords = Words(Title);
	if (TitleWords.size() == 2 && TitleWords[0] == "track") {
		std::vector<std::string> Game = Words(R.Game);
		while (Game.size() > 1 && (Game[0] == "the" || Game[0] == "a" || Game[0] == "an"))
			Game.erase(Game.begin());	// "The Legend of Zelda": by "legend", not "the"
		Title = (Game.empty() ? std::string("Track") : Game[0]) + " " + TitleWords[1];
		Title[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(Title[0])));
	}
	else
		Title = ShortTitle(Title);
	std::string Role = Sound.Role;
	if (Role == "the lead")
		Role = "lead";
	else if (Role.compare(0, 10, "an echo of") == 0)
		Role = "echo";
	else if (Role == "chords as arpeggios")
		Role = "arp";
	else if (Role == "a second part")
		Role = "second";
	else if (Role == "a low part")
		Role = "low";
	else if (Role == "kick or low hit")
		Role = "kick";
	else if (Role == "cymbal or open hi-hat" || Role == "open hi-hat or cymbal")
		Role = "cymbal";
	return Title + " " + Role;
}

}	// namespace ftapi

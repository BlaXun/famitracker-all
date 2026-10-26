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

#include "SoundIndex.h"
#include "DpcmKit.h"
#include "FdsSounds.h"
#include "GameSounds.h"
#include "Music.h"
#include "PulseClicks.h"
#include "SoundLibrary.h"
#include "Vrc6Plus.h"
#include "Wavetables.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <map>
#include <set>

namespace ftapi {

namespace {

const int HEARD_LIMIT = 60;		// frames the vanish check listens for (a second)

// Words in lower case: runs of letters and digits
std::vector<std::string> WordsOf(const std::string &Text)
{
	std::vector<std::string> Out;
	std::string Word;
	for (size_t i = 0; i <= Text.size(); ++i) {
		const unsigned char c = i < Text.size() ? static_cast<unsigned char>(Text[i]) : 0;
		if (std::isalnum(c))
			Word += static_cast<char>(std::tolower(c));
		else if (!Word.empty()) {
			Out.push_back(Word);
			Word.clear();
		}
	}
	return Out;
}

// A word asked for found in a text's words: one of them starts with it ("pluck" in "plucky"), its plural too
// ("bells" finds "bell")
bool HasWord(const std::vector<std::string> &Words, const std::string &Asked)
{
	std::vector<std::string> Forms(1, Asked);
	if (Asked.size() > 3 && Asked[Asked.size() - 1] == 's')
		Forms.push_back(Asked.substr(0, Asked.size() - 1));
	if (Asked.size() > 4 && Asked.compare(Asked.size() - 2, 2, "es") == 0)
		Forms.push_back(Asked.substr(0, Asked.size() - 2));
	for (size_t w = 0; w < Words.size(); ++w)
		for (size_t f = 0; f < Forms.size(); ++f)
			if (Words[w] == Forms[f] || (Forms[f].size() >= 2 && Words[w].compare(0, Forms[f].size(), Forms[f]) == 0))
				return true;
	return false;
}

// Words that say nothing about a sound
bool IsFiller(const std::string &Word)
{
	static const char *const FILLER[] = { "a", "an", "the", "and", "or", "of", "for", "with", "in", "on", "to", "sound",
		"sounds", "like", "that", "some", "one" };
	for (size_t i = 0; i < sizeof(FILLER) / sizeof(FILLER[0]); ++i)
		if (Word == FILLER[i])
			return true;
	return false;
}

const SoundKind *FindKind(const std::string &Id)
{
	const std::vector<SoundKind> &All = SoundKinds();
	for (size_t i = 0; i < All.size(); ++i)
		if (All[i].Id == Id)
			return &All[i];
	return 0;
}

int KindOrder(const std::string &Id)
{
	const std::vector<SoundKind> &All = SoundKinds();
	for (size_t i = 0; i < All.size(); ++i)
		if (All[i].Id == Id)
			return static_cast<int>(i);
	return static_cast<int>(All.size());
}

std::string Hex2(int Value)
{
	char Text[8];
	std::snprintf(Text, sizeof(Text), "%02X", Value & 0xFF);
	return Text;
}

// What a song's channel is called in the list: a 2A03 channel by its name ("Pulse 1"), another chip's with
// the chip ("VRC6 Sawtooth")
std::string ChannelLabel(const ChannelInfo &Channel)
{
	return Channel.Chip == "2A03" ? Channel.Name : Channel.FullName();
}

// The pitches a vibrato clicks on, worked out once for each set of sequences
const std::vector<int> &ClickPitchesOf(const InstrumentInfo &Info)
{
	static std::map<std::string, std::vector<int> > Known;
	static const std::vector<int> NONE;
	if (Info.Pitch.Items.empty() && Info.HiPitch.Items.empty())
		return NONE;
	std::string Key;
	const SequenceInfo *const SEQUENCES[] = { &Info.Pitch, &Info.HiPitch, &Info.Steps };
	for (size_t s = 0; s < 3; ++s) {
		const SequenceInfo &Seq = *SEQUENCES[s];
		Key += "|" + std::to_string(Seq.Loop) + "," + std::to_string(Seq.Release) + "," + std::to_string(Seq.Setting) + ":";
		for (size_t i = 0; i < Seq.Items.size(); ++i)
			Key += std::to_string(Seq.Items[i]) + " ";
	}
	Key += "|";
	for (size_t i = 0; i < Info.Volume.size(); ++i)
		Key += std::to_string(Info.Volume[i]) + " ";
	Key += std::to_string(Info.VolumeLoop) + "," + std::to_string(Info.VolumeRelease);
	std::map<std::string, std::vector<int> >::iterator it = Known.find(Key);
	if (it == Known.end()) {
		if (Known.size() > 4096)
			Known.clear();
		it = Known.insert(std::make_pair(Key, VibratoClickPitches(Info))).first;
	}
	return it->second;
}

// The frames a note of a sound is heard for (SoundHeardFrames), worked out once for each envelope
int HeardFramesOf(const InstrumentInfo &Info)
{
	static std::map<std::string, int> Known;
	std::string Key = Info.Chip + ":" + std::to_string(Info.VolumeLoop) + "," + std::to_string(Info.VolumeRelease) + ":";
	for (size_t i = 0; i < Info.Volume.size(); ++i)
		Key += std::to_string(Info.Volume[i]) + " ";
	std::map<std::string, int>::iterator it = Known.find(Key);
	if (it == Known.end()) {
		if (Known.size() > 4096)
			Known.clear();
		it = Known.insert(std::make_pair(Key, SoundHeardFrames(Info, HEARD_LIMIT))).first;
	}
	return it->second;
}

std::string PitchList(const std::vector<int> &Pitches)
{
	std::string Out;
	const size_t Shown = std::min<size_t>(Pitches.size(), 4);
	for (size_t i = 0; i < Shown; ++i)
		Out += (i == 0 ? "" : (i + 1 == Shown && Shown == Pitches.size() ? " and " : ", ")) + PitchName(Pitches[i]);
	if (Pitches.size() > Shown)
		Out += " and " + std::to_string(Pitches.size() - Shown) + " more";
	return Out;
}

// The kinds of drum a preview tells apart, by their words
enum drum_kind { DRUM_KICK, DRUM_SNARE, DRUM_OPEN, DRUM_CYMBAL, DRUM_TOM, DRUM_HAT, DRUM_OTHER };

int DrumKind(const std::string &Words)
{
	const std::vector<std::string> W = WordsOf(Words);
	if (HasWord(W, "kick"))
		return DRUM_KICK;
	if (HasWord(W, "snare"))
		return DRUM_SNARE;
	if (HasWord(W, "open"))
		return DRUM_OPEN;
	if (HasWord(W, "crash") || HasWord(W, "cymbal"))
		return DRUM_CYMBAL;
	if (HasWord(W, "tom"))
		return DRUM_TOM;
	if (HasWord(W, "hat") || HasWord(W, "hihat") || HasWord(W, "hi"))
		return DRUM_HAT;
	return DRUM_OTHER;
}

// The noise pitch a drum sounds best at, by its kind (as the library played them)
int DrumPitch(int Kind)
{
	static const int PITCHES[] = { 0x4, 0xA, 0xD, 0xC, 0x7, 0xE, 0xE };
	return PITCHES[Kind];
}

// Where a kind of channel's sounds would play on a chip the song doesn't have: its first channel's name
std::string WouldPlayOn(int Target)
{
	switch (Target) {
	case TARGET_VRC6_PULSE: return "VRC6 Pulse 1";
	case TARGET_SAWTOOTH: return "VRC6 Sawtooth";
	case TARGET_MMC5: return "MMC5 Pulse 1";
	case TARGET_N163: return "N163 Namco 1";
	case TARGET_SOFT: return "VRC6 Plus Soft 1";
	case TARGET_FDS: return "FDS";
	case TARGET_VRC7: return "VRC7 FM Channel 1";
	case TARGET_S5B: return "5B Square 1";
	default: return std::string();
	}
}

// A studied sound to list (a built-in game's or the library's): how it's named, made and found
struct Studied {
	std::shared_ptr<const ReferenceSound> pSound;
	std::string Short;		// the instrument's name
	std::string Key;		// the study's id, for reference_sound
	int Number;				// its sound there, from 1
	std::string Game, Origin, About, Extra;
	bool Builtin;
	Studied() : Number(0), Builtin(false) {}
};

class Lister {
public:
	Lister(const SongInfo &Song, int Cursor) :
		m_Song(Song), m_First(TARGET_COUNT, -1), m_Cursor(-1)
	{
		for (size_t c = 0; c < Song.Channels.size(); ++c) {
			const int Target = TargetOf(Song.Channels[c]);
			if (Target >= 0 && m_First[static_cast<size_t>(Target)] < 0)
				m_First[static_cast<size_t>(Target)] = static_cast<int>(c);
		}
		if (Cursor >= 0 && static_cast<size_t>(Cursor) < Song.Channels.size())
			m_Cursor = TargetOf(Song.Channels[static_cast<size_t>(Cursor)]);
	}

	bool Has(int Target) const { return Target >= 0 && Target < TARGET_COUNT && m_First[static_cast<size_t>(Target)] >= 0; }

	// Where the ready-made sounds that play on many kinds of channel are at home: the cursor's kind, when it's one
	int PresetHome() const
	{
		switch (m_Cursor) {
		case TARGET_PULSE: case TARGET_TRIANGLE: case TARGET_VRC6_PULSE: case TARGET_SAWTOOTH: case TARGET_MMC5: case TARGET_S5B:
			return m_Cursor;
		default:
			return TARGET_PULSE;
		}
	}

	// The ready-made sounds of the song's chips
	void ReadyMade()
	{
		static const int MELODIC[] = { TARGET_PULSE, TARGET_TRIANGLE, TARGET_MMC5, TARGET_VRC6_PULSE, TARGET_SAWTOOTH };
		for (size_t t = 0; t < sizeof(MELODIC) / sizeof(MELODIC[0]); ++t)
			if (Has(MELODIC[t]))
				Melodic(MELODIC[t], false);
		if (Has(TARGET_NOISE))
			Drums();
		if (Has(TARGET_DPCM))
			Kits();
		if (Has(TARGET_S5B))
			Sounds5B(true);
		if (Has(TARGET_VRC7))
			Vrc7();
		for (const int Target : { static_cast<int>(TARGET_N163), static_cast<int>(TARGET_SOFT) })
			if (Has(Target))
				Waves(Target, false);
		if (Has(TARGET_FDS))
			Fds();
	}

	// The ready-made sounds of the chips the song doesn't have, each at home where it would play there
	void OtherChips()
	{
		const std::vector<std::string> &Chips = OtherChipNames();
		for (size_t c = 0; c < Chips.size(); ++c) {
			const std::string &Chip = Chips[c];
			if (ChipsWith(m_Song, Chip).empty())
				continue;
			m_Adding = Chip;
			if (Chip == "VRC6") {
				Library(TARGET_VRC6_PULSE, HOME_VRC6_PULSE);
				Library(TARGET_SAWTOOTH, HOME_SAWTOOTH);
			}
			else if (Chip == VRC6_PLUS)
				Waves(TARGET_SOFT, true);
			else if (Chip == "MMC5")
				Melodic(TARGET_MMC5, true);
			else if (Chip == "N163")
				Waves(TARGET_N163, false);
			else if (Chip == "FDS")
				Fds();
			else if (Chip == "VRC7")
				Vrc7();
			else if (Chip == "5B")
				Sounds5B(false);
			m_Adding.clear();
		}
	}

	// The studied sounds FamiTracker carries (GameSounds.h), by game, named and found as the library's are
	void Games()
	{
		const std::vector<GameSound> &All = BuiltinGameSounds();
		const std::vector<GameSoundGame> &Names = GameSoundGames();
		for (size_t g = 0; g < All.size(); ++g) {
			const GameSound &G = All[g];
			if (G.Uses.empty())
				continue;
			const GameSoundUse &First = G.Uses[0];
			Studied S;
			S.pSound = G.Sound;
			S.Short = G.Name;
			m_Taken.insert(Lowercase(G.Name));
			S.Key = First.Id;
			S.Number = First.Number;
			S.Game = Names[G.Game].Name;
			S.Builtin = true;
			const std::string Track = "track " + std::to_string(First.Track);
			const bool Named = !First.Title.empty() && Lowercase(First.Title) != Track;
			S.About = (Named ? First.Title + " (" + Track + ")" : Track) + (First.Role.empty() ? std::string() : ", " + First.Role);
			S.Origin = (Named ? First.Title + " (" + S.Game + ", " + Track + ")" : S.Game + ", " + Track) + ", sound " +
				std::to_string(First.Number) + ": " + First.Channel + ", " + First.Role;
			std::string Also;
			for (size_t u = 1; u < G.Uses.size(); ++u) {
				if (G.Uses[u].Track == G.Uses[u - 1].Track)
					continue;
				Also += (Also.empty() ? "" : ", ") + std::to_string(G.Uses[u].Track);
				S.Extra += " " + G.Uses[u].Title;
			}
			if (!Also.empty()) {
				const size_t Comma = Also.rfind(", ");
				const std::string List = Comma == std::string::npos ? Also : Also.substr(0, Comma) + " and " + Also.substr(Comma + 2);
				S.About += "; also " + std::string(Comma == std::string::npos ? "track " : "tracks ") + List;
				S.Origin += "; the same sound plays in " + std::string(Comma == std::string::npos ? "track " : "tracks ") + List;
			}
			S.Extra = G.Uses[0].Id + S.Extra;
			AddStudied(S);
		}
	}

	void References(const std::vector<Reference> &Library)
	{
		for (size_t r = 0; r < Library.size(); ++r) {
			const Reference &R = Library[r];
			// Its sounds' names, numbered where another has the same, of this study or another (Unique)
			std::vector<std::string> Names;
			for (size_t s = 0; s < R.Sounds.size(); ++s)
				Names.push_back(ReferenceSoundName(R, R.Sounds[s]));
			const bool Public = GameOfReference(R) >= 0;
			for (size_t s = 0; s < R.Sounds.size(); ++s) {
				if (Public && m_Games && SameGameSound(R, R.Sounds[s]) != 0)
					continue;	// one of the built-in games' sounds: listed once, as theirs
				Studied S;
				S.pSound = std::make_shared<ReferenceSound>(R.Sounds[s]);
				const ReferenceSound &Sound = *S.pSound;
				S.Short = Unique(Names[s]);
				const std::string Track = R.Game.empty() ? R.Id : R.Game + (R.Track > 0 ? ", track " + std::to_string(R.Track) :
					std::string());
				S.Origin = (R.Title.empty() ? Track : R.Title + " (" + Track + ")") + ", sound " + std::to_string(s + 1) + ": " +
					Sound.Channel + ", " + Sound.Role;
				S.Extra = R.Id + " " + R.Composer;
				for (size_t t = 0; t < R.Tags.size(); ++t)
					S.Extra += " " + R.Tags[t];
				S.Key = R.Id;
				S.Number = static_cast<int>(s) + 1;
				S.Game = R.Game;
				// Under its game's heading: the track and the part ("Needle Man (track 2), the lead"; "track 2, the lead" for a
				// track the NSF names no better)
				const std::string TrackWords = R.Track > 0 ? "track " + std::to_string(R.Track) : std::string();
				const bool Named = !R.Title.empty() && Lowercase(R.Title) != TrackWords;
				S.About = (!Named ? (TrackWords.empty() ? (R.Title.empty() ? R.Id : R.Title) : TrackWords) : R.Title + (TrackWords.empty() ?
					std::string() : " (" + TrackWords + ")")) + (Sound.Role.empty() ? std::string() : ", " + Sound.Role);
				AddStudied(S);
			}
		}
	}

	void Own(const std::vector<SoundUse> &Uses)
	{
		for (size_t i = 0; i < m_Song.Instruments.size(); ++i) {
			const InstrumentInfo &I = m_Song.Instruments[i];
			const SoundUse Use = I.Index >= 0 && static_cast<size_t>(I.Index) < Uses.size() ? Uses[static_cast<size_t>(I.Index)] :
				SoundUse();
			int Home = Use.Channel >= 0 && static_cast<size_t>(Use.Channel) < m_Song.Channels.size() ?
				TargetOf(m_Song.Channels[static_cast<size_t>(Use.Channel)]) : -1;
			std::string Kind = KindOfWords(I.Name);
			if (Home < 0 || TargetChip(Home) != I.Chip) {
				// Not played yet: where its chip's sounds go
				Home = I.Chip == "VRC6" ? (Lowercase(I.Name).find("(saw)") != std::string::npos ? TARGET_SAWTOOTH :
					TARGET_VRC6_PULSE) : I.Chip == "N163" ? (Has(TARGET_SOFT) ? TARGET_SOFT : TARGET_N163) : I.Chip == "FDS" ? TARGET_FDS : I.Chip == "VRC7" ?
					TARGET_VRC7 : I.Chip == "5B" ? TARGET_S5B : Kind == "drums" ? TARGET_NOISE : TARGET_PULSE;
			}
			if (Kind.empty())
				Kind = Home == TARGET_NOISE || Home == TARGET_DPCM ? "drums" : Home == TARGET_TRIANGLE ? "bass" : "other";
			for (int Target = 0; Target < TARGET_COUNT; ++Target) {
				if (!Has(Target) || TargetChip(Target) != I.Chip)
					continue;
				const bool Played = PlaysOn(Use, Target);
				// A drum where drums go, a melodic sound where melodies go, and anywhere it's played
				const bool Drummy = Target == TARGET_NOISE || Target == TARGET_DPCM;
				if (Target != Home && !Played && (Drummy || Kind == "drums" || (Target == TARGET_SAWTOOTH && I.OddDuty)))
					continue;
				SoundEntry E = Base(I.Name, std::string(), Target, Target == Home);
				E.Kind = Kind;
				E.Drum = Kind == "drums";
				E.Source = SOURCE_SONG;
				E.Make = MAKE_NONE;
				E.Chip = I.Chip;
				E.Number = I.Index;
				E.InSong = I.Index;
				E.Origin = "this song's instrument " + Hex2(I.Index);
				if (Target == Home && Use.Channel >= 0) {
					E.Channel = Use.Channel;
					E.ChannelName = ChannelLabel(m_Song.Channels[static_cast<size_t>(Use.Channel)]);
					E.Pitches = Use.Pitches;
					E.Words = Use.Notes == 1 ? "1 note on " + E.ChannelName + " in this track" :
						std::to_string(Use.Notes) + " notes on " + E.ChannelName + " in this track";
				}
				else
					E.Words = Use.Notes > 0 ? "in this track on other channels" : "not played in this track";
				Finish(E, &I);
			}
		}
	}

	std::vector<SoundEntry> &Out() { return m_Out; }
	bool m_Games = true;		// the built-in games' sounds are listed: the library's that are theirs are left out

private:
	// A studied sound's name, numbered when a sound listed before has it ("Green Planet lead 2"): a name is a title's
	// start (ReferenceSoundName), which tracks share ("Boss" and "Boss (Simple)", "Green Fields ~Intro~"), and the song's
	// instrument of a name is taken for the sound of that name
	std::string Unique(const std::string &Name)
	{
		std::string Out = Name;
		for (int n = 2; m_Taken.count(Lowercase(Out)) != 0; ++n)
			Out = Name + " " + std::to_string(n);
		m_Taken.insert(Lowercase(Out));
		return Out;
	}

	// The presets and the library's 2A03 pulse sounds on a melodic kind of channel (Backing: another chip's, only those
	// that make a backing: pads, plucks and keys), and the library's sounds made for that channel
	void Melodic(int Target, bool Backing)
	{
		const std::vector<Preset> &All = Presets();
		const int Home = PresetHome();
		const bool VRC6 = Target == TARGET_VRC6_PULSE || Target == TARGET_SAWTOOTH, Saw = Target == TARGET_SAWTOOTH;
		for (size_t i = 0; i < All.size(); ++i) {
			if (All[i].Drum)
				continue;
			const std::string Kind = KindOfPreset(All[i].Name, All[i].Description);
			if (Backing && Kind != "pad" && Kind != "pluck" && Kind != "keys")
				continue;
			SoundEntry E = Base(All[i].InstrumentName(Saw), All[i].Description, Target, Backing || Target == Home);
			E.Kind = Kind;
			MakesPreset(E, All[i], VRC6, Saw, All[i].Name);
		}
		const std::vector<LibraryPreset> &Own = LibraryPresets();
		for (size_t i = 0; i < Own.size(); ++i) {
			if (Own[i].Home != HOME_PULSE || (Backing && Own[i].Kind != "pad" && Own[i].Kind != "pluck" && Own[i].Kind != "keys"))
				continue;
			SoundEntry E = Base(Own[i].Sound.InstrumentName(Saw), Own[i].Sound.Description, Target, Backing || Target == Home);
			E.Kind = Own[i].Kind;
			E.Extra = Own[i].Words;
			E.Brief = Own[i].Short;
			MakesPreset(E, Own[i].Sound, VRC6, Saw, Own[i].Sound.Name);
		}
		if (Target == TARGET_TRIANGLE)
			Library(Target, HOME_TRIANGLE);
		else if (Target == TARGET_VRC6_PULSE)
			Library(Target, HOME_VRC6_PULSE);
		else if (Target == TARGET_SAWTOOTH)
			Library(Target, HOME_SAWTOOTH);
	}

	// The library's presets made for one channel (Home), at home there
	void Library(int Target, int Home)
	{
		const std::vector<LibraryPreset> &Own = LibraryPresets();
		const bool VRC6 = Target == TARGET_VRC6_PULSE || Target == TARGET_SAWTOOTH, Saw = Target == TARGET_SAWTOOTH;
		for (size_t i = 0; i < Own.size(); ++i) {
			if (Own[i].Home != Home)
				continue;
			SoundEntry E = Base(Own[i].Sound.InstrumentName(Saw), Own[i].Sound.Description, Target, true);
			E.Kind = Own[i].Kind;
			E.Extra = Own[i].Words;
			E.Brief = Own[i].Short;
			if (Home == HOME_NOISE) {
				E.Drum = true;
				E.Make = MAKE_PRESET;
				E.Chip = "2A03";
				E.Key = Own[i].Sound.Name;
				E.Origin = "ready-made";
				Finish(E, 0);
			}
			else if (Home == HOME_5B) {
				E.Drum = E.Kind == "drums";
				E.Make = MAKE_5B;
				E.Chip = "5B";
				E.Key = Own[i].Sound.Name;
				E.Origin = "ready-made for the 5B";
				const InstrumentInfo Info = SequencesInfo(Own[i].Sound.Sequences5B, "5B");
				Finish(E, &Info);
			}
			else {
				E.Drum = E.Kind == "drums";
				MakesPreset(E, Own[i].Sound, VRC6, Saw, Own[i].Sound.Name);
			}
		}
	}

	// A preset (the presets', the library's) made as add_instrument makes it
	void MakesPreset(SoundEntry &E, const Preset &Sound, bool VRC6, bool Saw, const std::string &Key)
	{
		E.Make = MAKE_PRESET;
		E.Chip = VRC6 ? "VRC6" : "2A03";
		E.Sawtooth = Saw;
		E.Key = Key;
		E.Origin = "ready-made";
		std::vector<PresetSequence> Sequences = VRC6 ? Sound.SequencesVRC6 : Sound.Sequences2A03;
		if (Saw)	// as InstrumentLibrary.cpp makes it: the duty's low bit would add to the sawtooth's volume
			Sequences.erase(std::remove_if(Sequences.begin(), Sequences.end(),
				[](const PresetSequence &S) { return S.Type == SEQUENCE_DUTY; }), Sequences.end());
		const InstrumentInfo Info = SequencesInfo(Sequences, E.Chip);
		Finish(E, &Info);
	}

	void Drums()
	{
		const std::vector<Preset> &All = Presets();
		for (size_t i = 0; i < All.size(); ++i) {
			if (!All[i].Drum)
				continue;
			SoundEntry E = Base(All[i].InstrumentName(false), All[i].Description, TARGET_NOISE, true);
			E.Kind = "drums";
			E.Drum = true;
			E.Make = MAKE_PRESET;
			E.Chip = "2A03";
			E.Key = All[i].Name;
			E.Origin = "ready-made";
			Finish(E, 0);
		}
		Library(TARGET_NOISE, HOME_NOISE);
	}

	// The DPCM's kits of samples (DpcmKit.h)
	void Kits()
	{
		const std::vector<DpcmKit> &All = DrumKits();
		for (size_t i = 0; i < All.size(); ++i) {
			SoundEntry E = Base(All[i].Name, All[i].Description, TARGET_DPCM, true);
			E.Kind = "drums";
			E.Drum = true;
			E.Make = MAKE_DPCM_KIT;
			E.Chip = "2A03";
			E.Key = All[i].Name;
			E.Origin = "ready-made samples";
			Finish(E, 0);
		}
	}

	// The 5B's own sounds, the library's for it and (Presets: on the song's 5B) the presets as it plays them
	void Sounds5B(bool WithPresets)
	{
		const std::vector<Preset> &Own = Sunsoft5BSounds();
		for (size_t i = 0; i < Own.size(); ++i) {
			SoundEntry E = Base(Own[i].InstrumentName(false), Own[i].Description, TARGET_S5B, true);
			E.Kind = Own[i].Drum ? "drums" : KindOfPreset(Own[i].Name, Own[i].Description);
			E.Drum = E.Kind == "drums";
			E.Make = MAKE_5B;
			E.Chip = "5B";
			E.Key = Own[i].Name;
			E.Origin = "ready-made for the 5B";
			const InstrumentInfo Info = SequencesInfo(Own[i].Sequences5B, "5B");
			Finish(E, &Info);
		}
		Library(TARGET_S5B, HOME_5B);
		if (!WithPresets)
			return;
		const std::vector<Preset> &All = Presets();
		const int Home = PresetHome();
		for (size_t i = 0; i < All.size(); ++i) {
			if (All[i].Drum || FindSoundFor5B(All[i].Name) != &All[i])
				continue;	// the presets' drums are the noise channel's; and a 5B sound of the same name comes first
			SoundEntry E = Base(All[i].InstrumentName(false), All[i].Description, TARGET_S5B, Home == TARGET_S5B);
			E.Kind = KindOfPreset(All[i].Name, All[i].Description);
			E.Make = MAKE_PRESET;
			E.Chip = "5B";
			E.Key = All[i].Name;
			E.Origin = "ready-made";
			const InstrumentInfo Info = SequencesInfo(Preset5BSequences(All[i]), "5B");
			Finish(E, &Info);
		}
	}

	void Vrc7()
	{
		const std::vector<Vrc7Patch> &Patches = Vrc7Patches();
		for (size_t i = 0; i < Patches.size(); ++i) {
			SoundEntry E = Base(Patches[i].InstrumentName(), Patches[i].Description, TARGET_VRC7, true);
			E.Kind = KindOfWords(Patches[i].Name);
			if (E.Kind.empty())
				E.Kind = KindOfWords(Patches[i].Description);
			if (E.Kind.empty())
				E.Kind = "lead";
			E.Make = MAKE_VRC7;
			E.Chip = "VRC7";
			E.Number = Patches[i].Number;
			E.Key = Patches[i].Name;
			E.Origin = "the VRC7's patch " + std::to_string(Patches[i].Number);
			Finish(E, 0);
		}
		const std::vector<Vrc7Custom> &Customs = Vrc7Customs();
		const std::vector<LibraryVrc7> &More = LibraryVrc7Sounds();
		for (size_t i = 0; i < Customs.size() + More.size(); ++i) {
			const Vrc7Custom &C = i < Customs.size() ? Customs[i] : More[i - Customs.size()].Sound;
			SoundEntry E = Base(C.InstrumentName(), C.Description, TARGET_VRC7, true);
			if (i >= Customs.size()) {
				E.Kind = More[i - Customs.size()].Kind;
				E.Extra = More[i - Customs.size()].Words;
			}
			else {
				E.Kind = KindOfWords(C.Name);
				if (E.Kind.empty())
					E.Kind = KindOfWords(C.Description);
				if (E.Kind.empty())
					E.Kind = "other";
			}
			E.Make = MAKE_VRC7_CUSTOM;
			E.Chip = "VRC7";
			E.Key = C.Name;
			E.Origin = "a VRC7 custom patch (the chip holds one at a time)";
			Finish(E, 0);
		}
	}

	// The N163's wave sounds, the library's after them; the VRC6 Plus's soft layer's starter sounds first, then its calm
	// shapes, then the others (SoftOnly: the soft layer's own)
	void Waves(int Target, bool SoftOnly)
	{
		const std::vector<WaveSound> &All = WaveSounds();
		std::vector<const WaveSound *> List;
		for (int Group = 0; Group < (Target == TARGET_SOFT ? 3 : 1); ++Group)
			for (size_t i = 0; i < All.size(); ++i)
				if (Target != TARGET_SOFT || Group == (IsSoftLayerSound(All[i].Name) ? 0 : IsSoftLayerCalmSound(All[i].Name) ? 1 : 2))
					if (!SoftOnly || Group < 2)
						List.push_back(&All[i]);
		const std::vector<WaveSound> &More = LibraryWaveSounds();
		for (size_t i = 0; i < More.size() && !SoftOnly; ++i)
			List.push_back(&More[i]);
		for (size_t w = 0; w < List.size(); ++w) {
			const WaveSound &Wave = *List[w];
			SoundEntry E = Base(Wave.InstrumentName(), Wave.Description, Target, true);
			E.Kind = !Wave.Kind.empty() ? Wave.Kind : KindOf(Wave.Name, Wave.Words, Wave.Description);
			E.Make = MAKE_N163;
			E.Chip = "N163";
			E.Key = Wave.Name;
			E.Origin = Target == TARGET_SOFT && IsSoftLayerSound(Wave.Name) ? "a starter sound of the VRC6 Plus's soft layer" :
				Target == TARGET_SOFT && IsSoftLayerCalmSound(Wave.Name) ? "a calm sound made for the VRC6 Plus's soft layer" :
				"ready-made for the N163";
			E.Extra = Wave.Words;
			const InstrumentInfo Info = SequencesInfo(Wave.Sequences, "N163");
			Finish(E, &Info);
		}
	}

	void Fds()
	{
		const std::vector<FdsSound> &All = FdsSounds(), &More = LibraryFdsSounds();
		for (size_t i = 0; i < All.size() + More.size(); ++i) {
			const FdsSound &Sound = i < All.size() ? All[i] : More[i - All.size()];
			SoundEntry E = Base(Sound.InstrumentName(), Sound.Description, TARGET_FDS, true);
			E.Kind = !Sound.Kind.empty() ? Sound.Kind : Sound.Low ? "bass" : KindOf(Sound.Name, Sound.Words, Sound.Description);
			E.Low = Sound.Low;
			E.Make = MAKE_FDS;
			E.Chip = "FDS";
			E.Key = Sound.Name;
			E.Origin = "ready-made for the FDS";
			E.Extra = Sound.Words;
			const InstrumentInfo Info = SequencesInfo(Sound.Sequences, "FDS");
			Finish(E, &Info);
		}
	}

	bool PlaysOn(const SoundUse &Use, int Target) const
	{
		for (size_t c = 0; c < Use.Channels.size(); ++c)
			if (Use.Channels[c] >= 0 && static_cast<size_t>(Use.Channels[c]) < m_Song.Channels.size() &&
				TargetOf(m_Song.Channels[static_cast<size_t>(Use.Channels[c])]) == Target)
				return true;
		return false;
	}

	SoundEntry Base(const std::string &Name, const std::string &Words, int Target, bool Home) const
	{
		SoundEntry E;
		E.Name = Name;
		E.Words = Words;
		E.Target = Target;
		E.Home = Home;
		E.Channel = m_First[static_cast<size_t>(Target)];
		E.ChannelName = E.Channel >= 0 ? ChannelLabel(m_Song.Channels[static_cast<size_t>(E.Channel)]) : std::string();
		if (!m_Adding.empty()) {
			// A chip the song doesn't have: where it would play
			E.Channel = -1;
			E.ChannelName = WouldPlayOn(Target);
			E.AddsChip = m_Adding;
		}
		return E;
	}

	static std::string KindOfPreset(const std::string &Name, const std::string &Description)
	{
		std::string Kind = KindOfWords(Name);
		if (Kind.empty() && Name.find("buzz") != std::string::npos)
			Kind = "bass";	// the 5B's sync buzz: a bass
		if (Kind.empty())
			Kind = KindOfWords(Description);
		return Kind.empty() ? "lead" : Kind;
	}

	static std::string KindOf(const std::string &Name, const std::string &Words, const std::string &Description)
	{
		std::string Kind = KindOfWords(Name);
		if (Kind.empty())
			Kind = KindOfWords(Words);
		if (Kind.empty())
			Kind = KindOfWords(Description);
		return Kind.empty() ? "lead" : Kind;
	}

	// Where reference_sound puts a sound when it's given no channel: a VRC6 sound on the VRC6 (its sawtooth's on
	// the sawtooth), an FDS sound on the FDS and an N163 sound on the N163 (the VRC6 Plus's soft layer) when the song
	// has them, the others on the 2A03's channel of their kind
	int ReferenceHome(const ReferenceSound &Sound) const
	{
		if (Sound.Noise)
			return TARGET_NOISE;
		if (Sound.Chip == "VRC6" && Has(TARGET_VRC6_PULSE))
			return Sound.Sawtooth && Has(TARGET_SAWTOOTH) ? TARGET_SAWTOOTH : TARGET_VRC6_PULSE;
		if (Sound.Chip == "FDS" && Has(TARGET_FDS))
			return TARGET_FDS;
		if (Sound.Chip == "N163" && (Has(TARGET_SOFT) || Has(TARGET_N163)))
			return Has(TARGET_SOFT) ? TARGET_SOFT : TARGET_N163;
		return Sound.Triangle ? TARGET_TRIANGLE : TARGET_PULSE;
	}

	// A studied sound on every kind of channel it can play on, as reference_sound makes it there: a preset for the
	// 2A03, the VRC6 and the 5B, an FDS sound for the FDS, a wave sound for the N163 (the VRC6 Plus's soft layer)
	void AddStudied(const Studied &S)
	{
		static const int TARGETS[] = { TARGET_PULSE, TARGET_TRIANGLE, TARGET_NOISE, TARGET_MMC5, TARGET_VRC6_PULSE,
			TARGET_SAWTOOTH, TARGET_FDS, TARGET_S5B, TARGET_N163, TARGET_SOFT };
		const ReferenceSound &Sound = *S.pSound;
		const int Home = ReferenceHome(Sound);
		const Preset Made = ReferencePreset(Sound, S.Short);
		const std::string Kind = ReferenceKind(Sound);
		InstrumentInfo Info[TARGET_COUNT];
		bool Made2A03 = false, MadeVrc6 = false, MadeSaw = false, MadeWave = false;
		std::string WaveName;
		for (size_t t = 0; t < sizeof(TARGETS) / sizeof(TARGETS[0]); ++t) {
			const int Target = TARGETS[t];
			if (!Has(Target) || Sound.Noise != (Target == TARGET_NOISE))
				continue;
			std::string Name = Made.InstrumentName(Target == TARGET_SAWTOOTH);
			InstrumentInfo &I = Info[Target];
			if (Sound.Noise) {
				// a drum: no checks
			}
			else if (Target == TARGET_FDS) {
				std::string How;
				const FdsSound F = ReferenceFdsSound(Sound, S.Short, How);
				Name = F.InstrumentName();
				I = SequencesInfo(F.Sequences, "FDS");
			}
			else if (Target == TARGET_S5B)
				I = SequencesInfo(Preset5BSequences(Made), "5B");
			else if (Target == TARGET_N163 || Target == TARGET_SOFT) {
				if (!MadeWave) {
					std::string How;
					const WaveSound W = ReferenceWaveSound(Sound, S.Short, How);
					WaveName = W.InstrumentName();
					Info[TARGET_N163] = SequencesInfo(W.Sequences, "N163");
					MadeWave = true;
				}
				Name = WaveName;
				I = Info[TARGET_N163];
			}
			else if (Target == TARGET_SAWTOOTH) {
				if (!MadeSaw) {
					std::vector<PresetSequence> Sequences = Made.SequencesVRC6;
					Sequences.erase(std::remove_if(Sequences.begin(), Sequences.end(),
						[](const PresetSequence &Seq) { return Seq.Type == SEQUENCE_DUTY; }), Sequences.end());
					Info[TARGET_SAWTOOTH] = SequencesInfo(Sequences, "VRC6");
					MadeSaw = true;
				}
			}
			else if (Target == TARGET_VRC6_PULSE) {
				if (!MadeVrc6)
					Info[TARGET_VRC6_PULSE] = SequencesInfo(Made.SequencesVRC6, "VRC6");
				MadeVrc6 = true;
			}
			else {
				// the 2A03's pulses, triangle and the MMC5's play the same instrument
				if (!Made2A03)
					Info[TARGET_PULSE] = SequencesInfo(Made.Sequences2A03, "2A03");
				Made2A03 = true;
				if (Target != TARGET_PULSE)
					I = Info[TARGET_PULSE];
			}
			SoundEntry E = Base(Name, Sound.Words, Target, Target == Home);
			E.Source = SOURCE_REFERENCE;
			E.Builtin = S.Builtin;
			E.Game = S.Game;
			E.About = S.About;
			E.Kind = Kind;
			E.Drum = Sound.Noise;
			E.Low = E.Kind == "bass";
			E.Make = MAKE_REFERENCE;
			E.Chip = TargetChip(Target);
			E.Sawtooth = Target == TARGET_SAWTOOTH;
			E.Key = S.Key;
			E.Number = S.Number;
			E.Reference = S.pSound;
			E.Short = S.Short;
			E.Origin = S.Origin;
			E.Extra = S.Extra;
			Finish(E, Sound.Noise ? 0 : &I);
		}
	}

	static std::string ReferenceKind(const ReferenceSound &Sound)
	{
		if (Sound.Noise)
			return "drums";
		const std::vector<std::string> Role = WordsOf(Sound.Role);
		if (HasWord(Role, "bass") || HasWord(Role, "low"))
			return "bass";
		if (HasWord(Role, "chords") || HasWord(Role, "pad") || HasWord(Role, "arpeggios"))
			return "pad";
		// A lead, an echo or a second part: a pluck or a bell when its words say so
		const std::string Kind = KindOfWords(Sound.Words);
		if (Kind == "pluck" || Kind == "bell")
			return Kind;
		return Sound.Triangle ? "bass" : "lead";
	}

	// The rest of an entry: whether the song has it, its warnings and the words searched
	void Finish(SoundEntry &E, const InstrumentInfo *pInfo)
	{
		if (E.Channel < 0 && E.AddsChip.empty())
			return;
		E.Low = E.Low || E.Kind == "bass";
		if (E.Source != SOURCE_SONG)
			for (size_t i = 0; i < m_Song.Instruments.size() && E.InSong < 0; ++i)
				if (m_Song.Instruments[i].Chip == E.Chip && Lowercase(m_Song.Instruments[i].Name) == Lowercase(E.Name))
					E.InSong = m_Song.Instruments[i].Index;
		const bool Melodic = E.Target != TARGET_TRIANGLE && E.Target != TARGET_NOISE && E.Target != TARGET_DPCM &&
			E.Target != TARGET_VRC7 && E.Kind != "drums";
		if (pInfo != 0 && Melodic) {
			E.Heard = HeardFramesOf(*pInfo);
			if (SoundVanishes(E.Heard))
				E.Warning += VanishWarning(E.Heard, false);
		}
		if (pInfo != 0 && (E.Target == TARGET_PULSE || E.Target == TARGET_MMC5) && E.Kind != "drums") {
			E.ClickPitches = ClickPitchesOf(*pInfo);
			if (!E.ClickPitches.empty())
				E.Warning += " Held on " + std::string(E.Target == TARGET_MMC5 ? "an MMC5" : "a 2A03") + " pulse, its vibrato "
					"clicks on " + PitchList(E.ClickPitches) + " (a tick several times a second): View > Problems finds such "
					"notes in the song, and its Nudge stops them.";
		}
		if (!E.Warning.empty() && E.Warning[0] == ' ')
			E.Warning.erase(0, 1);
		if (E.About.empty())
			E.About = E.Words;
		IndexWords(E);
		m_Out.push_back(E);
	}

	const SongInfo &m_Song;
	std::vector<int> m_First;	// the song's first channel of each kind, -1 for none
	int m_Cursor;				// the kind of channel the user is on, -1 for none
	std::string m_Adding;		// listing the sounds of a chip the song doesn't have: that chip
	std::set<std::string> m_Taken;	// the studied sounds' names listed, in lower case (Unique)
	std::vector<SoundEntry> m_Out;
};

}	// namespace

const std::vector<SoundKind> &SoundKinds()
{
	static const std::vector<SoundKind> KINDS = {
		{ "lead", "Leads", "lead leads melody tune flute whistle brass trumpet horn clarinet oboe saxophone violin" },
		{ "bass", "Basses", "bass basses low" },
		{ "pad", "Pads and chords", "pad pads chord chords strings organ choir" },
		{ "pluck", "Plucks", "pluck plucks plucked harp guitar pizzicato koto banjo" },
		{ "bell", "Bells", "bell bells chime chimes glass glockenspiel celesta marimba xylophone vibraphone" },
		{ "keys", "Keys", "keys piano harpsichord clavinet rhodes" },
		{ "drums", "Drums", "drum drums kick snare hat hihat cymbal crash tom toms percussion" },
		{ "effect", "Effects", "effect effects wobble drone wind water atmosphere ambience" },
		{ "other", "Other", "" },
	};
	return KINDS;
}

std::string KindOfWords(const std::string &Text)
{
	const std::vector<std::string> Words = WordsOf(Text);
	// In this order: a plucky bass is a bass, a glass bell a bell, a bell-like piano keys
	static const char *const ORDER[] = { "drums", "bass", "bell", "keys", "pluck", "pad", "effect", "lead" };
	static std::vector<std::vector<std::string> > Means;
	if (Means.empty())
		for (size_t k = 0; k < sizeof(ORDER) / sizeof(ORDER[0]); ++k)
			Means.push_back(WordsOf(FindKind(ORDER[k])->Words));
	for (size_t k = 0; k < Means.size(); ++k)
		for (size_t m = 0; m < Means[k].size(); ++m)
			for (size_t w = 0; w < Words.size(); ++w)
				if (Words[w] == Means[k][m] || (Means[k][m].size() >= 5 && Words[w].compare(0, Means[k][m].size(), Means[k][m]) == 0))
					return ORDER[k];
	return std::string();
}

std::string KindId(const std::string &Asked)
{
	const std::string Lower = Lowercase(Asked);
	const std::vector<SoundKind> &All = SoundKinds();
	for (size_t i = 0; i < All.size(); ++i)
		if (Lower == All[i].Id || Lower == Lowercase(All[i].Label))
			return All[i].Id;
	return KindOfWords(Asked);
}

std::string TargetName(int Target)
{
	switch (Target) {
	case TARGET_PULSE: return "Pulse channels (2A03)";
	case TARGET_TRIANGLE: return "Triangle (2A03)";
	case TARGET_NOISE: return "Noise (2A03)";
	case TARGET_DPCM: return "DPCM (2A03)";
	case TARGET_VRC6_PULSE: return "VRC6 pulses";
	case TARGET_SAWTOOTH: return "VRC6 sawtooth";
	case TARGET_MMC5: return "MMC5 pulses";
	case TARGET_N163: return "N163 channels";
	case TARGET_FDS: return "FDS";
	case TARGET_VRC7: return "VRC7 channels";
	case TARGET_S5B: return "Sunsoft 5B channels";
	case TARGET_SOFT: return "VRC6 Plus soft layer";
	default: return std::string();
	}
}

int TargetOf(const ChannelInfo &Channel)
{
	if (Channel.Chip == "2A03")
		return Channel.Kind == KIND_TRIANGLE ? TARGET_TRIANGLE : Channel.Kind == KIND_NOISE ? TARGET_NOISE :
			Channel.Kind == KIND_DPCM ? TARGET_DPCM : TARGET_PULSE;
	if (Channel.Chip == "VRC6")
		return Channel.Kind == KIND_SAWTOOTH ? TARGET_SAWTOOTH : TARGET_VRC6_PULSE;
	if (Channel.Chip == "MMC5")
		return Channel.Kind == KIND_PULSE ? TARGET_MMC5 : -1;
	if (Channel.Chip == "N163")
		return Channel.Group == VRC6_PLUS ? TARGET_SOFT : TARGET_N163;
	if (Channel.Chip == "FDS")
		return TARGET_FDS;
	if (Channel.Chip == "VRC7")
		return TARGET_VRC7;
	if (Channel.Chip == "5B")
		return TARGET_S5B;
	return -1;
}

std::string TargetChip(int Target)
{
	switch (Target) {
	case TARGET_VRC6_PULSE: case TARGET_SAWTOOTH: return "VRC6";
	case TARGET_N163: case TARGET_SOFT: return "N163";
	case TARGET_FDS: return "FDS";
	case TARGET_VRC7: return "VRC7";
	case TARGET_S5B: return "5B";
	default: return "2A03";
	}
}

std::string SourceName(int Source)
{
	return Source == SOURCE_REFERENCE ? "reference library" : Source == SOURCE_SONG ? "this song" : "ready-made";
}

const std::vector<std::string> &OtherChipNames()
{
	static const std::vector<std::string> NAMES = { "VRC6", VRC6_PLUS, "MMC5", "N163", "FDS", "VRC7", "5B" };
	return NAMES;
}

namespace {

// The expansion chips a song has, by name, as SongInfo::Expansion lists them ("VRC6, N163, 5B")
std::vector<std::string> SongChipList(const SongInfo &Song)
{
	std::vector<std::string> Out;
	std::string Word;
	for (size_t i = 0; i <= Song.Expansion.size(); ++i) {
		const char c = i < Song.Expansion.size() ? Song.Expansion[i] : ',';
		if (c == ',' || c == ' ') {
			if (!Word.empty())
				Out.push_back(Word);
			Word.clear();
		}
		else
			Word += c;
	}
	return Out;
}

bool HasChipNamed(const std::vector<std::string> &Chips, const std::string &Chip)
{
	return std::find(Chips.begin(), Chips.end(), Chip) != Chips.end();
}

}	// namespace

std::vector<std::string> ChipsWith(const SongInfo &Song, const std::string &Chip)
{
	const std::vector<std::string> Have = SongChipList(Song);
	std::vector<std::string> None;
	const bool Plus = Chip == VRC6_PLUS;
	if (Plus ? (Song.Vrc6Plus || HasChipNamed(Have, "N163")) : HasChipNamed(Have, Chip))
		return None;	// it has it (the VRC6 Plus's N163 would change its channels: never offered then)
	if (!Plus && Chip != "VRC6" && Chip != "MMC5" && Chip != "N163" && Chip != "FDS" && Chip != "VRC7" && Chip != "5B")
		return None;
	// The chips as the user knows them (the VRC6 Plus in place of its two), with this one
	std::vector<std::string> Out;
	if (Song.Vrc6Plus || Plus)
		Out.push_back(VRC6_PLUS);
	static const char *const ORDER[] = { "VRC6", "VRC7", "FDS", "MMC5", "N163", "5B" };
	for (size_t i = 0; i < sizeof(ORDER) / sizeof(ORDER[0]); ++i) {
		const std::string Name = ORDER[i];
		if ((Song.Vrc6Plus || Plus) && (Name == "VRC6" || Name == "N163"))
			continue;
		if (HasChipNamed(Have, Name) || Name == Chip)
			Out.push_back(Name);
	}
	return Out;
}

std::string AddChipQuestion(const SongInfo &Song, const SoundEntry &Sound)
{
	const std::string &Chip = Sound.AddsChip;
	const std::vector<std::string> Have = SongChipList(Song), After = ChipsWith(Song, Chip);
	std::string Brings;
	if (Chip == "VRC6")
		Brings = "three more channels: two pulses with eight widths and a sawtooth";
	else if (Chip == VRC6_PLUS)
		Brings = !HasChipNamed(Have, "VRC6") ? "the VRC6's three channels (two pulses and a sawtooth) and under them a soft layer of "
			"four wave channels, Soft 1 to Soft 4, with its four starter sounds" : "a soft layer of four wave channels under the "
			"VRC6's, Soft 1 to Soft 4, with its four starter sounds";
	else if (Chip == "MMC5")
		Brings = "two more pulse channels, which play the 2A03's sounds";
	else if (Chip == "N163")
		Brings = "four wave channels, which play drawn waves";
	else if (Chip == "FDS")
		Brings = "one wave channel with a modulator that bends its pitch";
	else if (Chip == "VRC7")
		Brings = "six FM channels with the chip's own sounds";
	else if (Chip == "5B")
		Brings = "three square channels that can also play noise and a buzzing envelope";
	std::string Text = "\"" + Sound.Name + "\" is a sound for the " + (Chip == VRC6_PLUS ? std::string("VRC6 Plus's soft layer") : Chip) +
		", which this song doesn't have yet.\n\nAdd the " + Chip + " to the song? It brings " + Brings + ", after the song's own "
		"channels. Every note you've written stays where it is.";
	// NSF export plays one expansion chip, never the 5B
	int Real = 0;
	bool Has5B = false;
	for (size_t i = 0; i < After.size(); ++i) {
		Real += After[i] == VRC6_PLUS ? 2 : 1;
		Has5B = Has5B || After[i] == "5B";
	}
	if (Real > 1 || Has5B)
		Text += std::string(" FamiTracker's NSF export plays only one expansion chip") + (Has5B ? " and not the 5B" : "") + ", so the song "
			"will export to OGG and WAV files only (File > Export for games and File > Create WAV).";
	if (Song.FrameRate == 50)
		Text += " Expansion chips play at NTSC speed, so the song will play at 60 frames a second instead of 50.";
	// Adding a chip clears the undo history (FamiTracker's views reset it on UPDATE_PROPERTIES), as Module properties does
	Text += "\n\nThis can't be undone: adding a chip also clears the undo history, so Ctrl+Z won't take back this or any change "
		"you made before it. Save the song first if you'd like a copy to go back to (Module > Module properties takes the chip "
		"off again).";
	return Text;
}

InstrumentInfo SequencesInfo(const std::vector<PresetSequence> &Sequences, const std::string &Chip)
{
	InstrumentInfo Info;
	Info.Chip = Chip;
	for (size_t s = 0; s < Sequences.size(); ++s) {
		const PresetSequence &Seq = Sequences[s];
		if (Seq.Items.empty())
			continue;
		if (Seq.Type == SEQUENCE_VOLUME) {
			Info.Volume.clear();
			for (size_t k = 0; k < Seq.Items.size(); ++k)
				Info.Volume.push_back(Chip == "FDS" ? FdsVolumeAs15(Seq.Items[k]) : Seq.Items[k]);
			Info.VolumeLoop = Seq.Loop;
			Info.VolumeRelease = Seq.Release;
			Info.Releases = Seq.Release >= 0;
		}
		SequenceInfo *pInto = Seq.Type == SEQUENCE_ARPEGGIO ? &Info.Steps : Seq.Type == SEQUENCE_PITCH ? &Info.Pitch :
			Seq.Type == SEQUENCE_HIPITCH ? &Info.HiPitch : Seq.Type == SEQUENCE_DUTY && Chip == "5B" ? &Info.Mode : 0;
		if (pInto != 0) {
			pInto->Items = Seq.Items;
			pInto->Loop = Seq.Loop;
			pInto->Release = Seq.Release;
			pInto->Setting = Seq.Setting;
		}
	}
	return Info;
}

std::vector<SoundEntry> ListSounds(const SongInfo &Song, const std::vector<Reference> &Library, const std::vector<SoundUse> &Uses,
	int Cursor, const SoundListing &Listing)
{
	Lister L(Song, Cursor);
	L.m_Games = Listing.Games;
	L.ReadyMade();
	if (Listing.Games)
		L.Games();
	L.References(Library);
	L.Own(Uses);
	if (Listing.OtherChips) {
		L.OtherChips();
		std::vector<SoundEntry> &Out = L.Out();
		for (size_t i = 0; i < Out.size(); ++i)
			if (!Out[i].AddsChip.empty())
				Out[i].Question = AddChipQuestion(Song, Out[i]);
	}
	return L.Out();
}

namespace {
std::vector<size_t> FindMatching(const std::vector<SoundEntry> &All, const SoundQuery &Query, bool Every);
}

// The fields of SoundEntry::Search, in the order they count: the name, the kind, where it's from, its words, where it plays
const char FIELD_MARKS[] = "\x01\x02\x03\x04\x05";
const int FIELD_SCORES[] = { 10, 6, 4, 3, 2 };

void IndexWords(SoundEntry &Sound)
{
	const SoundKind *pKind = FindKind(Sound.Kind);
	Sound.Search = std::string(1, FIELD_MARKS[0]) + Lowercase(Sound.Name) + FIELD_MARKS[1] +
		Lowercase(pKind != 0 ? pKind->Id + " " + pKind->Label + " " + pKind->Words : Sound.Kind) + FIELD_MARKS[2] +
		Lowercase(Sound.Origin + " " + Sound.Extra) + FIELD_MARKS[3] + Lowercase(Sound.Words) + FIELD_MARKS[4] +
		Lowercase(Sound.Chip + " " + TargetName(Sound.Target) + " " + Sound.ChannelName + " " + SourceName(Sound.Source));
}

namespace {

// The best a word asked for scores in a sound's words: found at the start of one of them, in the field that counts most
int WordScore(const std::string &Search, const std::string &Asked)
{
	std::vector<std::string> Forms(1, Asked);
	if (Asked.size() > 3 && Asked[Asked.size() - 1] == 's')
		Forms.push_back(Asked.substr(0, Asked.size() - 1));
	if (Asked.size() > 4 && Asked.compare(Asked.size() - 2, 2, "es") == 0)
		Forms.push_back(Asked.substr(0, Asked.size() - 2));
	int Best = 0;
	for (size_t f = 0; f < Forms.size(); ++f) {
		for (size_t At = Search.find(Forms[f]); At != std::string::npos; At = Search.find(Forms[f], At + 1)) {
			if (At > 0 && std::isalnum(static_cast<unsigned char>(Search[At - 1])))
				continue;	// inside a word
			if (Forms[f].size() < 2 && At + 1 < Search.size() && std::isalnum(static_cast<unsigned char>(Search[At + 1])))
				continue;	// a single letter or digit matches only itself
			int Field = 0;
			for (size_t k = 0; k < 5; ++k)
				if (Search.rfind(FIELD_MARKS[k], At) != std::string::npos)
					Field = static_cast<int>(k);
			Best = std::max(Best, FIELD_SCORES[Field]);
			if (Best == FIELD_SCORES[0])
				return Best;
		}
	}
	return Best;
}

}	// namespace

int SoundMatch(const SoundEntry &Sound, const std::string &Words, bool Every)
{
	const std::vector<std::string> Asked = WordsOf(Words);
	std::string Indexed;
	if (Sound.Search.empty()) {
		SoundEntry Copy = Sound;
		IndexWords(Copy);
		Indexed = Copy.Search;
	}
	const std::string &Search = Sound.Search.empty() ? Indexed : Sound.Search;
	int Score = 0, Counted = 0;
	for (size_t a = 0; a < Asked.size(); ++a) {
		if (IsFiller(Asked[a]) && Asked.size() > 1)
			continue;
		++Counted;
		const int Best = WordScore(Search, Asked[a]);
		if (Best == 0 && Every)
			return 0;
		Score += Best;
	}
	return Counted > 0 ? Score : 1;
}

std::vector<size_t> FindSounds(const std::vector<SoundEntry> &All, const SoundQuery &Query, bool *pSome)
{
	if (pSome != 0)
		*pSome = false;
	std::vector<size_t> Out = FindMatching(All, Query, true);
	if (Out.empty() && WordsOf(Query.Words).size() > 1) {
		// None has every word: the ones with some of them
		Out = FindMatching(All, Query, false);
		if (pSome != 0)
			*pSome = !Out.empty();
	}
	return Out;
}

namespace {

std::vector<size_t> FindMatching(const std::vector<SoundEntry> &All, const SoundQuery &Query, bool Every)
{
	std::vector<std::pair<std::pair<int, int>, size_t> > Found;		// (-score, order), index
	const bool Words = !WordsOf(Query.Words).empty();
	for (size_t i = 0; i < All.size(); ++i) {
		const SoundEntry &E = All[i];
		if ((!Query.Kind.empty() && E.Kind != Query.Kind) || (Query.Source >= 0 && E.Source != Query.Source) ||
			(Query.Target >= 0 ? E.Target != Query.Target : !E.Home))
			continue;
		const int Score = Words ? SoundMatch(E, Query.Words, Every) : 1;
		if (Score <= 0)
			continue;
		const int Order = ((KindOrder(E.Kind) * 2 + (E.AddsChip.empty() ? 0 : 1)) * SOURCE_COUNT + E.Source) * TARGET_COUNT + E.Target;
		Found.push_back(std::make_pair(std::make_pair(-Score, Order), i));
	}
	std::stable_sort(Found.begin(), Found.end(), [](const std::pair<std::pair<int, int>, size_t> &A,
		const std::pair<std::pair<int, int>, size_t> &B) { return A.first < B.first; });
	std::vector<size_t> Out;
	for (size_t i = 0; i < Found.size(); ++i)
		Out.push_back(Found[i].second);
	return Out;
}

}	// namespace

// The sound browser's groups and rows ---------------------------------------------------------------------------

namespace {

// A game's place among the others: A to Z, "the" left out ("The Legend of Zelda" under L), the studies without one last
std::string GameOrder(const std::string &Game)
{
	if (Game.empty())
		return std::string(1, '\x7F');
	std::string Lower = Lowercase(Game);
	if (Lower.compare(0, 4, "the ") == 0 && Lower.size() > 4)
		Lower.erase(0, 4);
	return Lower;
}

std::string GroupLabel(int Source, const std::string &Game, const std::string &Chip)
{
	if (!Chip.empty())
		return (Chip == VRC6_PLUS ? std::string("VRC6 Plus soft layer") : Chip) + " (not in the song yet)";
	if (Source == SOURCE_PRESET)
		return "Ready-made sounds";
	if (Source == SOURCE_SONG)
		return "This song's instruments";
	return Game.empty() ? "Other references" : Game;
}

// A chip's place among the others the song doesn't have
int OtherChipOrder(const std::string &Chip)
{
	const std::vector<std::string> &Names = OtherChipNames();
	return static_cast<int>(std::find(Names.begin(), Names.end(), Chip) - Names.begin());
}

}	// namespace

std::vector<SoundGroup> GroupSounds(const std::vector<SoundEntry> &All, const std::vector<size_t> &Found)
{
	// The kinds in the order they're first found: SoundKinds()'s without words, the best match's first with them
	std::vector<std::string> Kinds;
	std::map<std::string, std::vector<size_t> > OfKind;
	for (size_t f = 0; f < Found.size(); ++f) {
		const std::string &Kind = All[Found[f]].Kind;
		std::vector<size_t> &Sounds = OfKind[Kind];
		if (Sounds.empty())
			Kinds.push_back(Kind);
		Sounds.push_back(Found[f]);
	}
	std::vector<SoundGroup> Out;
	for (size_t k = 0; k < Kinds.size(); ++k) {
		const std::vector<size_t> &Sounds = OfKind[Kinds[k]];
		// The ready-made sounds, the reference library's by game, the song's own
		std::map<std::pair<int, std::string>, SoundGroup> Groups;		// (place, game's order)
		for (size_t s = 0; s < Sounds.size(); ++s) {
			const SoundEntry &E = All[Sounds[s]];
			const int Source = E.Source == SOURCE_REFERENCE || E.Source == SOURCE_SONG ? E.Source : SOURCE_PRESET;
			const std::string Game = Source == SOURCE_REFERENCE ? E.Game : std::string();
			// The chips the song doesn't have come after its own sounds, each in a group of its own
			const int Place = E.AddsChip.empty() ? Source : SOURCE_COUNT + OtherChipOrder(E.AddsChip);
			SoundGroup &G = Groups[std::make_pair(Place, Source == SOURCE_REFERENCE ? GameOrder(Game) : std::string())];
			if (G.Sounds.empty()) {
				G.Kind = Kinds[k];
				G.Source = Source;
				G.Game = Game;
				G.Chip = E.AddsChip;
				G.Label = GroupLabel(Source, Game, E.AddsChip);
				G.Key = E.AddsChip.empty() ? Kinds[k] + "/" + std::to_string(Source) + "/" + Lowercase(Game) : Kinds[k] + "/chip/" +
					Lowercase(E.AddsChip);
				G.Builtin = true;
			}
			G.Builtin = G.Builtin && E.Builtin;
			G.Sounds.push_back(Sounds[s]);
		}
		for (std::map<std::pair<int, std::string>, SoundGroup>::iterator it = Groups.begin(); it != Groups.end(); ++it)
			Out.push_back(it->second);
	}
	return Out;
}

std::vector<int> KindCounts(const std::vector<SoundEntry> &All, const std::vector<size_t> &Found)
{
	std::vector<int> Counts(SoundKinds().size(), 0);
	for (size_t f = 0; f < Found.size(); ++f) {
		const size_t k = static_cast<size_t>(KindOrder(All[Found[f]].Kind));
		if (k < Counts.size())
			++Counts[k];
	}
	return Counts;
}

std::vector<int> KindsListed(const std::vector<int> &Counts, int Picked)
{
	std::vector<int> Out;
	for (size_t k = 0; k < Counts.size(); ++k)
		if (Counts[k] > 0 || static_cast<int>(k) == Picked)
			Out.push_back(static_cast<int>(k));
	return Out;
}

std::string KindKey(const std::string &Kind)
{
	return "kind/" + Kind;
}

bool GroupFolded(const std::vector<SoundGroup> &Groups, size_t Group, const SoundView &View)
{
	const SoundGroup &G = Groups[Group];
	const std::map<std::string, bool>::const_iterator Asked = View.Folds.find(G.Key);
	if (Asked != View.Folds.end())
		return Asked->second;
	if (View.Words)
		return false;
	if (!G.Chip.empty())
		return true;	// another chip's: there to discover, not in the way
	if (G.Source != SOURCE_REFERENCE)
		return false;
	int References = 0;
	for (size_t g = 0; g < Groups.size(); ++g)
		if (Groups[g].Kind == G.Kind && Groups[g].Source == SOURCE_REFERENCE)
			References += static_cast<int>(Groups[g].Sounds.size());
	return References > FOLD_GAMES_OVER;
}

std::vector<SoundRow> SoundRows(const std::vector<SoundGroup> &Groups, const SoundView &View)
{
	std::vector<SoundRow> Rows;
	for (size_t g = 0; g < Groups.size(); ) {
		// The kind's groups follow each other
		size_t End = g + 1;
		while (End < Groups.size() && Groups[End].Kind == Groups[g].Kind)
			++End;
		if (View.EveryKind) {
			SoundRow Kind;
			Kind.Type = ROW_KIND;
			Kind.Group = g;
			for (size_t k = g; k < End; ++k)
				Kind.Count += static_cast<int>(Groups[k].Sounds.size());
			const std::map<std::string, bool>::const_iterator Asked = View.Folds.find(KindKey(Groups[g].Kind));
			Kind.Folded = Asked != View.Folds.end() && Asked->second;
			Rows.push_back(Kind);
			if (Kind.Folded) {
				g = End;
				continue;
			}
		}
		for (; g < End; ++g) {
			SoundRow Heading;
			Heading.Type = ROW_GROUP;
			Heading.Group = g;
			Heading.Count = static_cast<int>(Groups[g].Sounds.size());
			Heading.Folded = GroupFolded(Groups, g, View);
			Rows.push_back(Heading);
			if (Heading.Folded)
				continue;
			for (size_t s = 0; s < Groups[g].Sounds.size(); ++s) {
				SoundRow Sound;
				Sound.Type = ROW_SOUND;
				Sound.Group = g;
				Sound.Sound = Groups[g].Sounds[s];
				Rows.push_back(Sound);
			}
		}
	}
	return Rows;
}

std::string RowKey(const std::vector<SoundGroup> &Groups, const SoundRow &Row)
{
	if (Row.Group >= Groups.size() || Row.Type == ROW_SOUND)
		return std::string();
	return Row.Type == ROW_KIND ? KindKey(Groups[Row.Group].Kind) : Groups[Row.Group].Key;
}

int RowHeading(const std::vector<SoundRow> &Rows, int Row)
{
	if (Row < 0 || static_cast<size_t>(Row) >= Rows.size() || Rows[static_cast<size_t>(Row)].Type == ROW_KIND)
		return -1;
	const int Above = Rows[static_cast<size_t>(Row)].Type == ROW_SOUND ? ROW_GROUP : ROW_KIND;
	for (int r = Row - 1; r >= 0; --r) {
		if (Rows[static_cast<size_t>(r)].Type == Above)
			return r;
		if (Rows[static_cast<size_t>(r)].Type == ROW_KIND)
			return -1;		// (a group's sounds never pass their kind's heading)
	}
	return -1;
}

int SoundRowOf(const std::vector<SoundRow> &Rows, size_t Sound)
{
	for (size_t r = 0; r < Rows.size(); ++r)
		if (Rows[r].Type == ROW_SOUND && Rows[r].Sound == Sound)
			return static_cast<int>(r);
	return -1;
}

int BestSound(const std::vector<SoundEntry> &All, const std::vector<SoundGroup> &Groups, const std::string &Words)
{
	// The groups hold the sounds shown (of the kind picked), in the list's order: kind by kind and source by source, so
	// the best match can be anywhere in it (the song's own choir pad under the ready-made pads)
	const bool Asked = !WordsOf(Words).empty();
	int Best = -1, BestScore = 0;
	bool BestPlays = false;
	for (size_t g = 0; g < Groups.size(); ++g)
		for (size_t s = 0; s < Groups[g].Sounds.size(); ++s) {
			const size_t Sound = Groups[g].Sounds[s];
			if (Sound >= All.size())
				continue;
			const int Score = Asked ? SoundMatch(All[Sound], Words, false) : 1;
			const bool Plays = All[Sound].AddsChip.empty();
			if (Best < 0 || Score > BestScore || (Score == BestScore && Plays && !BestPlays)) {
				Best = static_cast<int>(Sound);
				BestScore = Score;
				BestPlays = Plays;
			}
		}
	return Best;
}

bool OpenTo(const std::vector<SoundGroup> &Groups, SoundView &View, size_t Sound)
{
	for (size_t g = 0; g < Groups.size(); ++g) {
		if (std::find(Groups[g].Sounds.begin(), Groups[g].Sounds.end(), Sound) == Groups[g].Sounds.end())
			continue;
		bool Opened = false;
		const std::map<std::string, bool>::iterator Kind = View.Folds.find(KindKey(Groups[g].Kind));
		if (View.EveryKind && Kind != View.Folds.end() && Kind->second) {
			Kind->second = false;
			Opened = true;
		}
		if (GroupFolded(Groups, g, View)) {
			View.Folds[Groups[g].Key] = false;
			Opened = true;
		}
		return Opened;
	}
	return false;
}

// Previews --------------------------------------------------------------------------------------------------------

namespace {

// A phrase as a 2A03 pulse plays it (C-4 is 48): its notes (ms from the start, pitch), the last held until the
// release, and the end, where it's cut
struct PhraseNote {
	int At;
	int Pitch;
};
struct Phrase {
	const char *Kind;		// the kind it's for
	const PhraseNote *Notes;
	size_t Count;
	int Release;
	int End;
};

// A lead: a short tune that ends on a long note, to hear its vibrato and how it holds (E G A G E D C)
const PhraseNote LEAD_TUNE[] = { { 0, 52 }, { 180, 55 }, { 360, 57 }, { 540, 55 }, { 720, 52 }, { 900, 50 }, { 1080, 48 } };
// A bass: octaves that bounce and walk down (C-2 C-3 G-2 C-3 A#1 A#2 G-1 G-2 C-2)
const PhraseNote BASS_FIGURE[] = { { 0, 24 }, { 200, 36 }, { 400, 31 }, { 600, 36 }, { 800, 22 }, { 1000, 34 }, { 1200, 19 },
	{ 1400, 31 }, { 1600, 24 } };
// A pad: two held notes, to hear it swell and hold (C-4, then A-3)
const PhraseNote PAD_NOTES[] = { { 0, 48 }, { 1000, 45 } };
// A pluck: a quick broken chord, up and back (C E G C, G E C)
const PhraseNote PLUCK_CHORD[] = { { 0, 48 }, { 150, 52 }, { 300, 55 }, { 450, 60 }, { 600, 55 }, { 750, 52 }, { 900, 48 } };
// A bell: a few notes, far apart, each left to ring (C-5 G-4 E-4 C-4)
const PhraseNote BELL_NOTES[] = { { 0, 60 }, { 500, 55 }, { 1000, 52 }, { 1500, 48 } };
// Keys: a broken chord that turns into a tune (C E G C B G C)
const PhraseNote KEYS_TUNE[] = { { 0, 48 }, { 200, 52 }, { 400, 55 }, { 600, 60 }, { 800, 59 }, { 1000, 55 }, { 1200, 60 } };
// An effect: one long note
const PhraseNote EFFECT_NOTE[] = { { 0, 48 } };

#define PHRASE(Kind, Notes, Release, End) { Kind, Notes, sizeof(Notes) / sizeof(Notes[0]), Release, End }
const Phrase PHRASES[] = {
	PHRASE("lead", LEAD_TUNE, 2100, 2800),
	PHRASE("bass", BASS_FIGURE, 2300, 2900),
	PHRASE("pad", PAD_NOTES, 2000, 2800),
	PHRASE("pluck", PLUCK_CHORD, 1700, 2300),
	PHRASE("bell", BELL_NOTES, 2400, 3000),
	PHRASE("keys", KEYS_TUNE, 2000, 2600),
	PHRASE("effect", EFFECT_NOTE, 1800, 2600),
};
#undef PHRASE

const Phrase &PhraseFor(const std::string &Kind)
{
	for (size_t i = 0; i < sizeof(PHRASES) / sizeof(PHRASES[0]); ++i)
		if (Kind == PHRASES[i].Kind)
			return PHRASES[i];
	return PHRASES[0];		// "other": a lead's tune
}

// A beat: hits at their ms, a pitch each (into the pitches, in turn), and the end
std::vector<PreviewStep> Beat(const std::vector<int> &At, const std::vector<int> &Pitches, int End)
{
	std::vector<PreviewStep> Out;
	for (size_t i = 0; i < At.size(); ++i) {
		const PreviewStep Hit = { At[i], Pitches[i % Pitches.size()] };
		Out.push_back(Hit);
	}
	const PreviewStep Last = { End, PREVIEW_HALT };
	Out.push_back(Last);
	return Out;
}

std::vector<PreviewStep> DrumSteps(const SoundEntry &Sound)
{
	if (!Sound.Pitches.empty())		// the song's own: a beat at the pitches the song plays it at
		return Beat(std::vector<int>{ 0, 300, 600, 750, 900, 1200 }, Sound.Pitches, 1700);
	if (Sound.Target == TARGET_DPCM)	// a sample kit: kick, snare, kick, kick, snare, low tom, high tom
		return Beat(std::vector<int>{ 0, 300, 600, 750, 900, 1200, 1350 }, std::vector<int>{ 36, 38, 36, 36, 38, 41, 45 }, 1900);
	if (Sound.Target != TARGET_NOISE)	// a drum on another channel (the 5B's noise and envelope drums): at C-4 on
		// the 5B, C-5 on the FDS, C-3 elsewhere, which sound alike
		return Beat(std::vector<int>{ 0, 300, 600, 750, 900 }, std::vector<int>(1, Sound.Target == TARGET_S5B ? 48 :
			Sound.Target == TARGET_FDS ? 60 : 36), 1500);
	// On the noise, a rhythm for its kind of drum, where it sounds best
	const int Kind = DrumKind(Sound.Name + " " + Sound.Words);
	const std::vector<int> Pitch(1, DrumPitch(Kind));
	switch (Kind) {
	case DRUM_KICK:
		return Beat(std::vector<int>{ 0, 450, 600, 900 }, Pitch, 1500);
	case DRUM_SNARE:
		return Beat(std::vector<int>{ 0, 600, 900, 1050 }, Pitch, 1600);
	case DRUM_HAT:
		return Beat(std::vector<int>{ 0, 150, 300, 450, 600, 750, 900, 1050 }, Pitch, 1400);
	case DRUM_OPEN:
		return Beat(std::vector<int>{ 0, 600 }, Pitch, 1600);
	case DRUM_CYMBAL:
		return Beat(std::vector<int>{ 0 }, Pitch, 1800);
	case DRUM_TOM:	// a fill, falling
		return Beat(std::vector<int>{ 0, 200, 400, 600 }, std::vector<int>{ 0x9, 0x8, 0x7, 0x6 }, 1400);
	default:
		return Beat(std::vector<int>{ 0, 300, 600, 750, 900 }, Pitch, 1500);
	}
}

}	// namespace

std::vector<PreviewStep> PreviewSteps(const SoundEntry &Sound)
{
	if (Sound.Target == TARGET_NOISE || Sound.Target == TARGET_DPCM || Sound.Drum || Sound.Kind == "drums")
		return DrumSteps(Sound);
	// A bass's figure for a sound heard low, whatever its words
	const std::string Kind = Sound.Low ? std::string("bass") : Sound.Kind;
	const Phrase &P = PhraseFor(Kind);
	// The FDS sounds two octaves under the note written and the 5B one: written up, where a pulse would play them
	int Lift = Sound.Target == TARGET_FDS ? 24 : Sound.Target == TARGET_S5B ? 12 : 0;
	if (Sound.Source == SOURCE_SONG && !Sound.Pitches.empty()) {
		// The song's own: by octaves (at most one) toward the lowest note the song plays it at
		int Lowest = P.Notes[0].Pitch;
		for (size_t i = 1; i < P.Count; ++i)
			Lowest = std::min(Lowest, P.Notes[i].Pitch);
		const int Apart = Sound.Pitches[0] - (Lowest + Lift);
		const int Octaves = Apart >= 0 ? (Apart + 6) / 12 : -((-Apart + 5) / 12);
		Lift += 12 * std::max(-1, std::min(1, Octaves));
	}
	std::vector<PreviewStep> Out;
	for (size_t i = 0; i < P.Count; ++i) {
		const PreviewStep Note = { P.Notes[i].At, std::max(0, std::min(95, P.Notes[i].Pitch + Lift)) };
		Out.push_back(Note);
	}
	const PreviewStep Release = { P.Release, PREVIEW_RELEASE };
	Out.push_back(Release);
	const PreviewStep End = { P.End, PREVIEW_HALT };
	Out.push_back(End);
	return Out;
}

}	// namespace ftapi

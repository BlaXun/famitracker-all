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

#include "GameSounds.h"
#include "Music.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <map>

namespace ftapi {

namespace {

// The rows of GameSoundsData.h: the games, their tracks, the sounds (a game's sound once) and where each plays
struct GameRow {
	const char *Slug;
	const char *Name;
};
struct TrackRow {
	int Game;			// into GAME_ROWS
	int Track;			// in the NSF
	const char *Id;		// its study's
	const char *Title;
};
// A sound's data: its sequences as V (volume), A (arpeggio), P (pitch), H (hi-pitch) and D (duty), each its values with
// /L its loop, /R its release and /F for fixed notes; an FDS sound's wave (W), modulation table (M) and modulator's speed,
// depth and delay (S); an N163 sound's waves (N, apart by |) and the sequence that picks them (Q); apart by ;
struct SoundRow {
	int Game;
	const char *Chip;	// "2A03", "VRC6", "FDS", "N163"
	const char *Flags;	// n: the noise's, t: the triangle's, s: the sawtooth's
	const char *Words;
	const char *Data;
};
struct UseRow {
	int Sound;			// into SOUND_ROWS
	int Track;			// into TRACK_ROWS
	int Number;			// its number in that study, from 1
	const char *Channel;
	const char *Role;
};

#include "GameSoundsData.h"

std::vector<int> Numbers(const std::string &Text)
{
	std::vector<int> Out;
	size_t At = 0;
	while (At < Text.size()) {
		size_t End = Text.find(',', At);
		if (End == std::string::npos)
			End = Text.size();
		if (End > At)
			Out.push_back(std::atoi(Text.substr(At, End - At).c_str()));
		At = End + 1;
	}
	return Out;
}

// "15,8,8/L2/R0/F"
ReferenceSequence ParseSequence(const std::string &Text)
{
	ReferenceSequence Seq;
	size_t Slash = Text.find('/');
	Seq.Items = Numbers(Text.substr(0, Slash));
	while (Slash != std::string::npos) {
		const size_t Next = Text.find('/', Slash + 1);
		const std::string Part = Text.substr(Slash + 1, Next == std::string::npos ? std::string::npos : Next - Slash - 1);
		if (!Part.empty() && Part[0] == 'L')
			Seq.Loop = std::atoi(Part.c_str() + 1);
		else if (!Part.empty() && Part[0] == 'R')
			Seq.Release = std::atoi(Part.c_str() + 1);
		else if (Part == "F")
			Seq.Fixed = true;
		Slash = Next;
	}
	return Seq;
}

ReferenceSound ParseSound(const SoundRow &Row)
{
	ReferenceSound S;
	S.Chip = Row.Chip;
	S.Words = Row.Words;
	const std::string Flags = Row.Flags;
	S.Noise = Flags.find('n') != std::string::npos;
	S.Triangle = Flags.find('t') != std::string::npos;
	S.Sawtooth = Flags.find('s') != std::string::npos;
	const std::string Data = Row.Data;
	size_t At = 0;
	while (At < Data.size()) {
		size_t End = Data.find(';', At);
		if (End == std::string::npos)
			End = Data.size();
		const std::string Part = Data.substr(At, End - At);
		At = End + 1;
		if (Part.empty())
			continue;
		const std::string Rest = Part.substr(1);
		switch (Part[0]) {
		case 'V': S.Sequences[SEQUENCE_VOLUME] = ParseSequence(Rest); break;
		case 'A': S.Sequences[SEQUENCE_ARPEGGIO] = ParseSequence(Rest); break;
		case 'P': S.Sequences[SEQUENCE_PITCH] = ParseSequence(Rest); break;
		case 'H': S.Sequences[SEQUENCE_HIPITCH] = ParseSequence(Rest); break;
		case 'D': S.Sequences[SEQUENCE_DUTY] = ParseSequence(Rest); break;
		case 'W': S.FdsWave = Numbers(Rest); break;
		case 'M': S.FdsMod = Numbers(Rest); break;
		case 'S': {
			const std::vector<int> Mod = Numbers(Rest);
			if (Mod.size() == 3) {
				S.ModSpeed = Mod[0];
				S.ModDepth = Mod[1];
				S.ModDelay = Mod[2];
			}
			break;
		}
		case 'N': {
			size_t From = 0;
			while (From <= Rest.size()) {
				size_t Bar = Rest.find('|', From);
				if (Bar == std::string::npos)
					Bar = Rest.size();
				S.N163Waves.push_back(Numbers(Rest.substr(From, Bar - From)));
				From = Bar + 1;
			}
			break;
		}
		case 'Q': S.N163WaveSteps = ParseSequence(Rest); break;
		default: break;
		}
	}
	return S;
}

struct Built {
	std::vector<GameSoundGame> Games;
	std::vector<GameSound> Sounds;
	std::map<std::pair<size_t, std::string>, size_t> ByKey;	// (game, key) -> sound
};

const Built &All()
{
	static const Built B = [] {
		Built Out;
		for (size_t g = 0; g < sizeof(GAME_ROWS) / sizeof(GAME_ROWS[0]); ++g) {
			GameSoundGame G;
			G.Slug = GAME_ROWS[g].Slug;
			G.Name = GAME_ROWS[g].Name;
			Out.Games.push_back(G);
		}
		for (size_t s = 0; s < sizeof(SOUND_ROWS) / sizeof(SOUND_ROWS[0]); ++s) {
			GameSound G;
			G.Game = static_cast<size_t>(SOUND_ROWS[s].Game);
			G.Sound = std::make_shared<ReferenceSound>(ParseSound(SOUND_ROWS[s]));
			Out.Sounds.push_back(G);
		}
		for (size_t u = 0; u < sizeof(USE_ROWS) / sizeof(USE_ROWS[0]); ++u) {
			const UseRow &Row = USE_ROWS[u];
			const TrackRow &Track = TRACK_ROWS[Row.Track];
			GameSoundUse Use;
			Use.Id = Track.Id;
			Use.Track = Track.Track;
			Use.Title = Track.Title;
			Use.Number = Row.Number;
			Use.Channel = Row.Channel;
			Use.Role = Row.Role;
			Out.Sounds[static_cast<size_t>(Row.Sound)].Uses.push_back(Use);
		}
		// Named as the library names a study's sounds, after the first track each plays in; every name another
		std::map<std::string, int> Named;
		for (size_t s = 0; s < Out.Sounds.size(); ++s) {
			GameSound &G = Out.Sounds[s];
			if (G.Uses.empty())
				continue;
			std::shared_ptr<ReferenceSound> pSound = std::make_shared<ReferenceSound>(*G.Sound);
			pSound->Channel = G.Uses[0].Channel;
			pSound->Role = G.Uses[0].Role;
			G.Sound = pSound;
			Reference R;
			R.Id = G.Uses[0].Id;
			R.Title = G.Uses[0].Title;
			R.Game = Out.Games[G.Game].Name;
			R.Track = G.Uses[0].Track;
			const std::string Name = ReferenceSoundName(R, *pSound);
			const int Before = Named[Lowercase(Name)]++;
			G.Name = Before == 0 ? Name : Name + " " + std::to_string(Before + 1);
			G.Key = GameSoundKey(*pSound);
			Out.ByKey[std::make_pair(G.Game, G.Key)] = s;
		}
		return Out;
	}();
	return B;
}

void AddSequence(std::string &Out, char Letter, const ReferenceSequence &Seq)
{
	if (Seq.Items.empty())
		return;
	Out += Letter;
	for (size_t i = 0; i < Seq.Items.size(); ++i)
		Out += (i == 0 ? "" : ",") + std::to_string(Seq.Items[i]);
	if (Seq.Loop >= 0)
		Out += "/L" + std::to_string(Seq.Loop);
	if (Seq.Release >= 0)
		Out += "/R" + std::to_string(Seq.Release);
	if (Seq.Fixed)
		Out += "/F";
	Out += ';';
}

void AddNumbers(std::string &Out, char Letter, const std::vector<int> &Values)
{
	if (Values.empty())
		return;
	Out += Letter;
	for (size_t i = 0; i < Values.size(); ++i)
		Out += (i == 0 ? "" : ",") + std::to_string(Values[i]);
	Out += ';';
}

}	// namespace

const std::vector<GameSoundGame> &GameSoundGames()
{
	return All().Games;
}

const std::vector<GameSound> &BuiltinGameSounds()
{
	return All().Sounds;
}

std::string GameSoundKey(const ReferenceSound &Sound)
{
	std::string Out = Sound.Chip + ":" + (Sound.Noise ? "n" : "") + (Sound.Triangle ? "t" : "") + (Sound.Sawtooth ? "s" : "") + ":";
	static const char LETTERS[5] = { 'V', 'A', 'P', 'H', 'D' };
	for (int t = 0; t < 5; ++t)
		AddSequence(Out, LETTERS[t], Sound.Sequences[t]);
	if (!Sound.FdsWave.empty()) {
		AddNumbers(Out, 'W', Sound.FdsWave);
		AddNumbers(Out, 'M', Sound.FdsMod);
		Out += "S" + std::to_string(Sound.ModSpeed) + "," + std::to_string(Sound.ModDepth) + "," + std::to_string(Sound.ModDelay) + ";";
	}
	for (size_t w = 0; w < Sound.N163Waves.size(); ++w)
		AddNumbers(Out, 'N', Sound.N163Waves[w]);
	AddSequence(Out, 'Q', Sound.N163WaveSteps);
	return Out;
}

int GameOfReference(const Reference &R)
{
	const std::vector<GameSoundGame> &Games = GameSoundGames();
	// "castlevania-3-26": the game castlevania-3
	std::string Slug = Lowercase(R.Id);
	const size_t Dash = Slug.rfind('-');
	if (Dash != std::string::npos && Dash + 1 < Slug.size() && Slug.find_first_not_of("0123456789", Dash + 1) == std::string::npos)
		Slug.erase(Dash);
	for (size_t g = 0; g < Games.size(); ++g)
		if (Slug == Games[g].Slug)
			return static_cast<int>(g);
	const std::string Game = Lowercase(R.Game);
	for (size_t g = 0; g < Games.size() && !Game.empty(); ++g)
		if (Game == Lowercase(Games[g].Name))
			return static_cast<int>(g);
	return -1;
}

const GameSound *SameGameSound(const Reference &R, const ReferenceSound &Sound)
{
	const int Game = GameOfReference(R);
	if (Game < 0)
		return 0;
	const Built &B = All();
	const std::map<std::pair<size_t, std::string>, size_t>::const_iterator it =
		B.ByKey.find(std::make_pair(static_cast<size_t>(Game), GameSoundKey(Sound)));
	return it != B.ByKey.end() ? &B.Sounds[it->second] : 0;
}

bool BuiltinReference(const std::string &Id, Reference &Out)
{
	const std::string Wanted = Lowercase(Id);
	for (size_t t = 0; t < sizeof(TRACK_ROWS) / sizeof(TRACK_ROWS[0]); ++t) {
		if (Lowercase(TRACK_ROWS[t].Id) != Wanted)
			continue;
		Out = Reference();
		Out.Id = TRACK_ROWS[t].Id;
		Out.Title = TRACK_ROWS[t].Title;
		Out.Game = GAME_ROWS[TRACK_ROWS[t].Game].Name;
		Out.Track = TRACK_ROWS[t].Track;
		// Its sounds in the order its study numbered them
		std::vector<std::pair<int, ReferenceSound> > Sounds;
		const std::vector<GameSound> &Known = BuiltinGameSounds();
		for (size_t u = 0; u < sizeof(USE_ROWS) / sizeof(USE_ROWS[0]); ++u) {
			if (USE_ROWS[u].Track != static_cast<int>(t))
				continue;
			ReferenceSound S = *Known[static_cast<size_t>(USE_ROWS[u].Sound)].Sound;
			S.Channel = USE_ROWS[u].Channel;
			S.Role = USE_ROWS[u].Role;
			Sounds.push_back(std::make_pair(USE_ROWS[u].Number, S));
		}
		std::stable_sort(Sounds.begin(), Sounds.end(), [](const std::pair<int, ReferenceSound> &A,
			const std::pair<int, ReferenceSound> &B) { return A.first < B.first; });
		for (size_t s = 0; s < Sounds.size(); ++s)
			Out.Sounds.push_back(Sounds[s].second);
		return true;
	}
	return false;
}

}	// namespace ftapi

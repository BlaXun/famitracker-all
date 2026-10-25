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

// The sounds FamiTracker carries from the studies of the soundtracks that may
// be public: each sound as a study keeps it (References.h's ReferenceSound:
// its chip, sequences, duty, waves and words) and the tracks it plays in,
// which name it. GameSoundsData.h holds them, written by tools/game_sounds.py
// from a folder of studies (only those games, only their sounds, a sound that
// several tracks of a game play kept once). The sound browser lists them by
// game (SoundIndex.h) whether or not the reference library is installed, and
// once when the library has them too; reference_sound adds them by a track's
// id. Plain C++14, tested on Linux.

#include <memory>
#include <string>
#include <vector>
#include "References.h"

namespace ftapi {

struct GameSoundGame {
	std::string Slug;		// "castlevania-3": the start of its studies' ids
	std::string Name;		// "Castlevania 3", as its studies name it
};

// A track a sound plays in
struct GameSoundUse {
	std::string Id;			// the track's study's id: "castlevania-3-26"
	int Track;				// in the NSF, from 1
	std::string Title;		// "Flashback (Staff Roll)"
	int Number;				// the sound's number in that study, from 1
	std::string Channel;	// "Pulse 1"
	std::string Role;		// "the lead"
};

struct GameSound {
	size_t Game;			// into GameSoundGames()
	std::shared_ptr<const ReferenceSound> Sound;	// as the study keeps it, Channel and Role its first track's
	std::vector<GameSoundUse> Uses;	// the tracks it plays in, in their order
	std::string Name;		// as ReferenceSoundName names it in its first track ("Vampire Killer lead"), numbered where
							// another of them has that name ("Vampire Killer lead 2"): every name is another
	std::string Key;		// GameSoundKey(*Sound)
	GameSound() : Game(0) {}
};

const std::vector<GameSoundGame> &GameSoundGames();
const std::vector<GameSound> &BuiltinGameSounds();

// What makes two sounds the same: its chip, its kind of channel, its sequences and its waves, as text
std::string GameSoundKey(const ReferenceSound &Sound);
// The game of a reference among them (by the start of its id, or its game's name), -1 for none
int GameOfReference(const Reference &R);
// The built-in sound a reference's sound is: the same game, the same sound; 0 for none
const GameSound *SameGameSound(const Reference &R, const ReferenceSound &Sound);
// One of their tracks as a reference, by its id ("castlevania-3-26"): its title, game and track, and its sounds
// numbered as its study numbered them (no study, no tags); false when none of them has that track
bool BuiltinReference(const std::string &Id, Reference &Out);

}	// namespace ftapi

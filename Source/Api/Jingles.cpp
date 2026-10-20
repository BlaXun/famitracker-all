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

#include "Jingles.h"

namespace ftapi {

namespace {

const int K = JINGLE_KICK, S = JINGLE_SNARE, O = JINGLE_OPEN_HIHAT;

// Lowercase letters and digits only: "Quest-complete" and "quest complete" match
std::string Squeezed(const std::string &Text)
{
	std::string Out;
	for (size_t i = 0; i < Text.size(); ++i) {
		const char c = Text[i];
		if (c >= 'A' && c <= 'Z')
			Out += static_cast<char>(c - 'A' + 'a');
		else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
			Out += c;
	}
	return Out;
}

std::vector<Jingle> MakeJingles()
{
	std::vector<Jingle> All;

	// I, IV and V as quick arpeggios climbing, the dominant seventh coming
	// down to the key note; the harmony climbs in fourths to the seventh,
	// which falls to the third
	Jingle J;
	J.Name = "level up";
	J.Use = "a level up, a new skill or a power-up";
	J.Words = { "levelup", "level", "upgrade", "power up", "new skill", "skill up", "ding" };
	J.Minor = false;
	J.Bpm = 150.0;
	J.Steps = 32;
	J.LeadSound = 0;
	J.LeadVolume = 15;
	J.HarmonyVolume = 10;
	J.Lead = { { 0, 1, 0 }, { 1, 1, 4 }, { 2, 1, 7 }, { 3, 1, 12 }, { 4, 1, 5 }, { 5, 1, 9 }, { 6, 1, 12 }, { 7, 1, 17 },
		{ 8, 1, 7 }, { 9, 1, 11 }, { 10, 1, 14 }, { 11, 1, 19 }, { 12, 2, 17 }, { 14, 2, 14 }, { 16, 12, 12, true } };
	J.Harmony = { { 0, 4, -8 }, { 4, 4, -3 }, { 8, 4, 2 }, { 12, 4, 5 }, { 16, 12, 4 } };
	J.Bass = { { 0, 4, 0 }, { 4, 4, 5 }, { 8, 8, 7 }, { 16, 12, 0 } };
	J.Drums = { { 0, K, 15 }, { 8, K, 13 }, { 12, S, 9 }, { 13, S, 11 }, { 14, S, 13 }, { 15, S, 15 }, { 16, O, 14 } };
	All.push_back(J);

	// A rising call, then the flat sixth and seventh lifting into the key
	// note (bVI-bVII-I), in sixths
	J = Jingle();
	J.Name = "victory";
	J.Use = "a quest completed, a battle won or a stage cleared";
	J.Words = { "quest complete", "quest completed", "complete", "win", "won", "fanfare", "stage clear", "clear", "success",
		"triumph", "boss defeated" };
	J.Minor = false;
	J.Bpm = 132.0;
	J.Steps = 32;
	J.LeadSound = 0;
	J.LeadVolume = 15;
	J.HarmonyVolume = 10;
	J.Lead = { { 0, 2, 0 }, { 2, 2, 4 }, { 4, 2, 7 }, { 6, 4, 12 }, { 10, 2, 7 }, { 12, 2, 8 }, { 14, 2, 10 }, { 16, 12, 12, true } };
	J.Harmony = { { 6, 6, 4 }, { 12, 2, 0 }, { 14, 2, 2 }, { 16, 12, 4 } };
	J.Bass = { { 0, 12, 0 }, { 12, 2, -4 }, { 14, 2, -2 }, { 16, 12, 0 } };
	J.Drums = { { 0, K, 15 }, { 6, S, 12 }, { 10, K, 11 }, { 12, S, 12 }, { 14, S, 14 }, { 16, O, 15 } };
	All.push_back(J);

	// A sparkle: a major ninth chord in sixteenths, settling on the key note
	J = Jingle();
	J.Name = "item";
	J.Use = "an item, a treasure or a key found";
	J.Words = { "item get", "get item", "pickup", "pick up", "treasure", "chest", "collect", "key", "reward", "loot", "found item" };
	J.Minor = false;
	J.Bpm = 140.0;
	J.Steps = 16;
	J.LeadSound = "bell";
	J.LeadVolume = 15;
	J.HarmonyVolume = 9;
	J.Lead = { { 0, 1, 0 }, { 1, 1, 4 }, { 2, 1, 7 }, { 3, 1, 11 }, { 4, 1, 14 }, { 5, 11, 12 } };	// a bell rings out by itself
	J.Harmony = { { 5, 7, 4 } };
	J.Bass = { { 0, 12, 0 } };
	J.Drums = { { 5, O, 7 } };
	All.push_back(J);

	// Two chimes, a fourth up
	J = Jingle();
	J.Name = "new quest";
	J.Use = "a new quest, a message or a notification";
	J.Words = { "quest", "notification", "message", "quest accepted", "quest start", "ping", "mail", "letter", "chat", "invite" };
	J.Minor = false;
	J.Bpm = 120.0;
	J.Steps = 12;
	J.LeadSound = "bell";
	J.LeadVolume = 13;
	J.HarmonyVolume = 8;
	J.Lead = { { 0, 2, 7 }, { 2, 10, 12 } };	// a bell rings out by itself
	J.Harmony = { { 0, 10, 4 } };
	J.Bass = { { 0, 10, 0 } };
	All.push_back(J);

	// The key's major seventh chord, then the major chord a step up (the
	// lydian sharp fourth), opening onto the third
	J = Jingle();
	J.Name = "discovery";
	J.Use = "a secret, a new area or a solved puzzle";
	J.Words = { "secret", "discover", "discovered", "new area", "area", "puzzle", "puzzle solved", "solved", "mystery", "wonder",
		"magic", "found" };
	J.Minor = false;
	J.Bpm = 112.0;
	J.Steps = 24;
	J.LeadSound = 0;
	J.LeadVolume = 14;
	J.HarmonyVolume = 9;
	J.Lead = { { 0, 1, 0 }, { 1, 1, 4 }, { 2, 1, 7 }, { 3, 1, 11 }, { 4, 1, 2 }, { 5, 1, 6 }, { 6, 1, 9 }, { 7, 1, 14 },
		{ 8, 12, 16, true } };
	J.Harmony = { { 0, 4, -5 }, { 4, 4, -6 }, { 8, 12, -5 } };
	J.Bass = { { 0, 4, 0 }, { 4, 4, 2 }, { 8, 12, 0 } };
	J.Drums = { { 8, O, 8 } };
	All.push_back(J);

	// A lullaby's cadence: I, IV, V, I, slow and soft
	J = Jingle();
	J.Name = "rest";
	J.Use = "a rest at an inn, a save or healing";
	J.Words = { "inn", "save", "saved", "sleep", "heal", "healing", "campfire", "bed", "home", "cozy", "lullaby", "recover" };
	J.Minor = false;
	J.Bpm = 72.0;
	J.Steps = 32;
	J.LeadSound = "soft lead";
	J.LeadVolume = 12;
	J.HarmonyVolume = 7;
	J.Lead = { { 0, 4, 7 }, { 4, 4, 4 }, { 8, 4, 5 }, { 12, 4, 2 }, { 16, 2, 4 }, { 18, 2, 2 }, { 20, 8, 0, true } };
	J.Harmony = { { 0, 4, 4 }, { 4, 4, 0 }, { 8, 4, -3 }, { 12, 4, -1 }, { 16, 4, -5 }, { 20, 8, -8 } };
	J.Bass = { { 0, 8, 0 }, { 8, 4, 5 }, { 12, 8, 7 }, { 20, 8, 0 } };
	All.push_back(J);

	// Sinking by semitones from the fifth, through the leading tone home
	J = Jingle();
	J.Name = "defeat";
	J.Use = "a lost battle or a game over";
	J.Words = { "game over", "gameover", "lose", "lost", "death", "die", "died", "dead", "fail", "failed", "failure",
		"knocked out", "ko", "faint", "fainted" };
	J.Minor = true;
	J.Bpm = 76.0;
	J.Steps = 32;
	J.LeadSound = 0;
	J.LeadVolume = 13;
	J.HarmonyVolume = 8;
	J.Lead = { { 0, 2, 7 }, { 2, 2, 6 }, { 4, 2, 5 }, { 6, 2, 3 }, { 8, 4, 2 }, { 12, 4, -1 }, { 16, 12, 0, true } };
	J.Harmony = { { 0, 4, 3 }, { 4, 4, 0 }, { 8, 4, -1 }, { 12, 4, -5 }, { 16, 12, -9 } };
	J.Bass = { { 0, 4, 0 }, { 4, 4, -7 }, { 8, 8, -5 }, { 16, 12, -12 } };
	All.push_back(J);

	// Tritones climbing by semitones over a pulsing key note, in minor
	// thirds: diminished and unresolved
	J = Jingle();
	J.Name = "danger";
	J.Use = "an alarm, a trap or a boss appearing";
	J.Words = { "alarm", "warning", "alert", "boss", "boss appears", "ambush", "trap", "enemy", "threat", "panic" };
	J.Minor = true;
	J.Bpm = 144.0;
	J.Steps = 28;
	J.LeadSound = 0;
	J.LeadVolume = 15;
	J.HarmonyVolume = 10;
	J.Lead = { { 0, 2, 0 }, { 2, 2, 6 }, { 4, 2, 1 }, { 6, 2, 7 }, { 8, 2, 2 }, { 10, 2, 8 }, { 12, 12, 3, true } };
	J.Harmony = { { 0, 2, -3 }, { 2, 2, 3 }, { 4, 2, -2 }, { 6, 2, 4 }, { 8, 2, -1 }, { 10, 2, 5 }, { 12, 12, 0 } };
	for (int Step = 0; Step < 24; Step += 2)
		J.Bass.push_back(JingleNote(Step, 1, 0));
	J.Drums = { { 0, K, 15 }, { 4, K, 13 }, { 8, K, 14 }, { 10, S, 11 }, { 11, S, 13 }, { 12, O, 15 }, { 16, K, 12 }, { 20, K, 12 } };
	All.push_back(J);

	return All;
}

}	// namespace

const std::vector<Jingle> &Jingles()
{
	static const std::vector<Jingle> All = MakeJingles();
	return All;
}

const Jingle *FindJingle(const std::string &Name)
{
	const std::string Wanted = Squeezed(Name);
	if (Wanted.empty())
		return 0;
	const std::vector<Jingle> &All = Jingles();
	for (size_t i = 0; i < All.size(); ++i)
		if (Squeezed(All[i].Name) == Wanted)
			return &All[i];
	for (size_t i = 0; i < All.size(); ++i)
		for (size_t w = 0; w < All[i].Words.size(); ++w)
			if (Squeezed(All[i].Words[w]) == Wanted)
				return &All[i];
	return 0;
}

std::string JingleNames()
{
	std::string Names;
	const std::vector<Jingle> &All = Jingles();
	for (size_t i = 0; i < All.size(); ++i)
		Names += (i == 0 ? "" : ", ") + All[i].Name;
	return Names;
}

}	// namespace ftapi

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

// Jingles for games: short tunes that play once, like a level up, a quest
// completed or an item found. Each is a little score in semitones from its
// key note, with a lead, a harmony line below it, a bass and drums;
// make_jingle (Tools.cpp) writes one into a track of its own in the song's
// key, register and sounds, ending with a halt. Plain C++14.

#include <string>
#include <vector>

namespace ftapi {

struct JingleNote {
	int Step;		// when it starts, in sixteenth notes from the start
	int Steps;		// how long it sounds
	int Degree;		// semitones from the key note (negative is lower)
	bool Vibrato;	// a gentle vibrato after its first beat

	JingleNote(int Step, int Steps, int Degree, bool Vibrato = false) : Step(Step), Steps(Steps), Degree(Degree), Vibrato(Vibrato) {}
};

enum jingle_drum { JINGLE_KICK, JINGLE_SNARE, JINGLE_HIHAT, JINGLE_OPEN_HIHAT };

struct JingleHit {
	int Step;
	int Drum;		// jingle_drum
	int Volume;		// 1-15

	JingleHit(int Step, int Drum, int Volume) : Step(Step), Drum(Drum), Volume(Volume) {}
};

struct Jingle {
	std::string Name;			// "level up"
	std::string Use;			// what it's for, in a few words
	std::vector<std::string> Words;	// other names it's found by ("quest complete")
	bool Minor;					// in the key note's minor (harmonic minor), else its major
	double Bpm;					// four sixteenths to a beat
	int Steps;					// how long it plays; it halts then
	const char *LeadSound;		// the preset its lead suits ("bell"), 0 for the song's own lead sound
	int LeadVolume, HarmonyVolume;
	std::vector<JingleNote> Lead, Harmony, Bass;	// each in order, one note at a time
	std::vector<JingleHit> Drums;
};

const std::vector<Jingle> &Jingles();

// By its name or another word for it, ignoring case, spaces and dashes
// ("Quest complete", "levelup"); 0 if none
const Jingle *FindJingle(const std::string &Name);

// "level up, victory, item, ..."
std::string JingleNames();

}	// namespace ftapi

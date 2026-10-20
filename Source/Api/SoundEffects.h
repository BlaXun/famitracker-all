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

// Sound effects for games, made the NES's way: a pulse, the triangle and the
// noise channel, each moving its volume, pitch and duty a tick (1/60 s) at a
// time. make_sound_effect (Tools.cpp) writes one into a track of its own that
// halts, so the export for games saves it as a sound that plays once. The
// kinds suit a cozy platformer: jumps, coins, menus, chat pings. Plain C++14.

#include <string>
#include <vector>
#include "../Transcribe/SoundEffect.h"

namespace ftapi {

struct SongInfo;

struct SoundEffectKind {
	std::string Name;			// "jump"
	std::string Use;			// what it's for, in a few words
	std::vector<std::string> Words;	// other names it's found by ("pew", "boing")
	transcribe::SoundEffect (*Make)();
};

const std::vector<SoundEffectKind> &SoundEffectKinds();
const SoundEffectKind *FindSoundEffect(const std::string &Name);	// by name or another word for it
std::string SoundEffectNames();									// "jump, land, coin, ..."

struct SoundEffectChanges {
	int Variant;		// 1: as designed; 2 and up: other takes on it (VariantText says how)
	int Semitones;		// higher (positive) or lower
	int LengthPercent;	// 100: as designed
	int VolumePercent;	// 100: as designed

	SoundEffectChanges() : Variant(1), Semitones(0), LengthPercent(100), VolumePercent(100) {}
};

// A sound with the changes made: the pitch moved, stretched in time, louder
// or softer. A variant moves the pitch and the length its own way, the same
// each time, and every other one changes a pulse's duty.
transcribe::SoundEffect ChangeSoundEffect(const transcribe::SoundEffect &Effect, const SoundEffectChanges &Changes);
std::string VariantText(int Variant);	// "3 semitones higher, 15% shorter, another duty"; "" for 1

// A sound effect's track and instruments share its name: "SFX Jump", with
// "SFX Jump (pulse 2)" and so on for its other layers. FamiTracker reuses an
// instrument when one has the name already, so a name is free when no track
// and no instrument has it (or one of its layers' names), whatever the case.
// FreeSoundName: Base, or Base with a number ("SFX Jump 2") when it's taken.
// SoundNameProblem: why Name can't be used, or "" when it can.
std::string FreeSoundName(const SongInfo &Song, const std::string &Base);
std::string SoundNameProblem(const SongInfo &Song, const std::string &Name);

}	// namespace ftapi

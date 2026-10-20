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

// Instrument presets for the Claude Code connection: sounds asked for by
// name ("soft lead", "plucky bass", "snare"), built from sequences for the
// 2A03 (also used by MMC5 channels) and for the VRC6, and the VRC7's sounds:
// the 15 patches built into the chip ("piano", "brass"), which the presets
// also have an equivalent among.

#include <string>
#include <vector>

namespace ftapi {

// Sequence types as sequence_t: volume, arpeggio, pitch, hi-pitch, duty
const int SEQUENCE_VOLUME = 0;
const int SEQUENCE_ARPEGGIO = 1;
const int SEQUENCE_PITCH = 2;
const int SEQUENCE_HIPITCH = 3;
const int SEQUENCE_DUTY = 4;

struct PresetSequence {
	int Type;
	std::vector<int> Items;
	int Loop;		// -1: none
	int Release;	// -1: none. Held there while the note lasts, the rest plays on release (===)
};

struct Preset {
	std::string Name;			// "soft lead"
	std::string Description;
	bool Drum;					// for the noise channel
	std::vector<PresetSequence> Sequences2A03;
	std::vector<PresetSequence> SequencesVRC6;	// the sawtooth leaves out the duty sequence
	int Vrc7Patch;				// the VRC7 patch that sounds most like it, 0 for none (drums)

	bool Releases() const;		// notes end with a release (===) rather than a halt (---)
	std::string InstrumentName(bool Sawtooth) const;	// "Soft lead", "Bass (saw)"
};

const std::vector<Preset> &Presets();
const Preset *FindPreset(const std::string &Name);	// by name or a common word for it ("flute", "pad")
std::string PresetList();							// "soft lead, bright lead, ..."

// A VRC7 patch, as FamiTracker's VRC7 instrument editor names it
struct Vrc7Patch {
	int Number;					// 1-15
	std::string Name;			// "piano"
	std::string Description;

	std::string InstrumentName() const;	// "Piano"
};

const std::vector<Vrc7Patch> &Vrc7Patches();
// A VRC7 sound asked for by name: a patch's ("piano", "brass"), a common word for one ("organ",
// "choir"), or a preset's, which has an equivalent ("soft lead" is the flute); 0 if none
const Vrc7Patch *FindVrc7Patch(const std::string &Name);
std::string Vrc7PatchList();						// "bell, guitar, piano, ..."

}	// namespace ftapi

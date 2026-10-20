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

#include "Presets.h"
#include "Music.h"

#include <cctype>
#include <initializer_list>

namespace ftapi {

namespace {

PresetSequence Sequence(int Type, const std::vector<int> &Items, int Loop = -1, int Release = -1)
{
	PresetSequence S;
	S.Type = Type;
	S.Items = Items;
	S.Loop = Loop;
	S.Release = Release;
	S.Setting = SEQUENCE_ABSOLUTE;
	return S;
}

// A melodic preset: one volume envelope for both chips, a duty cycle for each
// (2A03: 0 = 12.5%, 1 = 25%, 2 = 50%; VRC6: n = (n + 1) / 16)
Preset Melodic(const char *Name, const char *Description, const std::vector<int> &Volume, int Release, int Duty2A03,
	int DutyVRC6, const std::vector<int> &Arpeggio = std::vector<int>())
{
	Preset P;
	P.Name = Name;
	P.Description = Description;
	P.Drum = false;
	P.Vrc7Patch = 0;
	P.Sequences2A03.push_back(Sequence(SEQUENCE_VOLUME, Volume, -1, Release));
	P.SequencesVRC6.push_back(Sequence(SEQUENCE_VOLUME, Volume, -1, Release));
	if (!Arpeggio.empty()) {
		P.Sequences2A03.push_back(Sequence(SEQUENCE_ARPEGGIO, Arpeggio));
		P.SequencesVRC6.push_back(Sequence(SEQUENCE_ARPEGGIO, Arpeggio));
	}
	P.Sequences2A03.push_back(Sequence(SEQUENCE_DUTY, std::vector<int>(1, Duty2A03)));
	P.SequencesVRC6.push_back(Sequence(SEQUENCE_DUTY, std::vector<int>(1, DutyVRC6)));
	return P;
}

Preset Drum(const char *Name, const char *Description, const std::vector<int> &Volume)
{
	Preset P;
	P.Name = Name;
	P.Description = Description;
	P.Drum = true;
	P.Vrc7Patch = 0;
	P.Sequences2A03.push_back(Sequence(SEQUENCE_VOLUME, Volume));
	return P;
}

std::vector<int> List(std::initializer_list<int> Items)
{
	return std::vector<int>(Items);
}

std::vector<Preset> MakePresets()
{
	std::vector<Preset> All;
	All.push_back(Melodic("soft lead", "soft, round, flute-like lead that swells in (50% duty)",
		List({ 5, 8, 10, 11, 12, 12, 11, 10, 8, 6, 4, 3, 2, 1, 0 }), 7, 2, 7));
	All.push_back(Melodic("bright lead", "punchy, bright lead (25% duty)",
		List({ 15, 14, 13, 12, 12, 11, 9, 7, 5, 3, 2, 1, 0 }), 5, 1, 3));
	All.push_back(Melodic("thin lead", "thin, nasal retro lead (12.5% duty)",
		List({ 13, 12, 11, 11, 10, 8, 6, 4, 2, 1, 0 }), 4, 0, 1));
	All.push_back(Melodic("pluck", "plucked string or harp: a quick attack that decays by itself",
		List({ 15, 12, 10, 9, 8, 7, 6, 5, 5, 4, 4, 3, 3, 2, 2, 1, 1, 0 }), -1, 1, 3));
	All.push_back(Melodic("bell", "bell or chime: an octave blip on the attack, then a long ring",
		List({ 15, 13, 11, 10, 9, 8, 8, 7, 7, 6, 6, 5, 5, 4, 4, 3, 3, 2, 2, 2, 1, 1, 1, 0 }), -1, 1, 2, List({ 12, 0 })));
	All.push_back(Melodic("soft chords", "quiet, thin pad for chords and arpeggios under a melody (12.5% duty)",
		List({ 4, 6, 7, 8, 8, 7, 5, 4, 3, 2, 1, 0 }), 5, 0, 1));
	All.push_back(Melodic("warm pad", "slow, round and dark pad (50% duty); good for calm, sad or underwater moods",
		List({ 2, 3, 4, 5, 6, 7, 7, 8, 8, 7, 6, 5, 4, 3, 2, 1, 0 }), 8, 2, 7));
	All.push_back(Melodic("bass", "sustained bass for the triangle, the VRC6 sawtooth or a pulse",
		List({ 15, 15, 14, 13, 12, 12, 10, 8, 6, 4, 2, 0 }), 5, 2, 7));
	All.push_back(Melodic("plucky bass", "short, bouncy bass notes that decay by themselves",
		List({ 15, 15, 14, 12, 10, 8, 7, 6, 5, 4, 3, 2, 1, 0 }), -1, 2, 7));
	All.push_back(Drum("kick", "noise kick drum (play it as \"kick\" on the noise channel)", List({ 15, 12, 8, 5, 3, 1, 0 })));
	All.push_back(Drum("snare", "noise snare drum (\"snare\")", List({ 14, 11, 9, 7, 5, 3, 2, 1, 0 })));
	All.push_back(Drum("hi-hat", "short closed hi-hat (\"hihat\")", List({ 10, 6, 3, 1, 0 })));
	All.push_back(Drum("open hi-hat", "longer open hi-hat or crash (\"open hi-hat\")", List({ 11, 9, 8, 7, 6, 5, 4, 3, 2, 1, 1, 0 })));

	// What sounds most like each on the VRC7
	static const struct { const char *Name; int Patch; } ON_VRC7[] = {
		{ "soft lead", 4 }, { "bright lead", 14 }, { "thin lead", 5 }, { "pluck", 2 }, { "bell", 1 }, { "soft chords", 8 },
		{ "warm pad", 15 }, { "bass", 13 }, { "plucky bass", 13 },
	};
	for (size_t i = 0; i < All.size(); ++i)
		for (size_t k = 0; k < sizeof(ON_VRC7) / sizeof(ON_VRC7[0]); ++k)
			if (All[i].Name == ON_VRC7[k].Name)
				All[i].Vrc7Patch = ON_VRC7[k].Patch;
	return All;
}

std::vector<Vrc7Patch> MakeVrc7Patches()
{
	static const char *const PATCHES[][2] = {
		{ "bell", "bright bell" },
		{ "guitar", "plucked electric guitar" },
		{ "piano", "piano" },
		{ "flute", "soft flute; a gentle lead" },
		{ "clarinet", "reedy clarinet; a woody lead" },
		{ "rattling bell", "metallic, rattling bell" },
		{ "trumpet", "trumpet; a bold lead" },
		{ "reed organ", "reed organ; sustained chords" },
		{ "soft bell", "soft bell, like a music box" },
		{ "xylophone", "xylophone: short and woody" },
		{ "vibraphone", "vibraphone" },
		{ "brass", "brass section; bold chords or a lead" },
		{ "bass guitar", "electric bass" },
		{ "synthesizer", "synth lead" },
		{ "chorus", "airy, choir-like pad" },
	};
	std::vector<Vrc7Patch> All;
	for (size_t i = 0; i < sizeof(PATCHES) / sizeof(PATCHES[0]); ++i) {
		Vrc7Patch P;
		P.Number = static_cast<int>(i) + 1;
		P.Name = PATCHES[i][0];
		P.Description = PATCHES[i][1];
		All.push_back(P);
	}
	return All;
}

std::string Simplified(const std::string &Text)
{
	std::string Out;
	for (size_t i = 0; i < Text.size(); ++i) {
		const unsigned char c = static_cast<unsigned char>(Text[i]);
		if (std::isalnum(c))
			Out += static_cast<char>(std::tolower(c));
	}
	return Out;
}

}	// namespace

bool Preset::Releases() const
{
	for (size_t i = 0; i < Sequences2A03.size(); ++i)
		if (Sequences2A03[i].Type == SEQUENCE_VOLUME)
			return Sequences2A03[i].Release >= 0;
	return false;
}

std::string Preset::InstrumentName(bool Sawtooth) const
{
	std::string Out = Name;
	if (!Out.empty())
		Out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(Out[0])));
	if (Sawtooth)
		Out += " (saw)";
	return Out;
}

const std::vector<Preset> &Presets()
{
	static const std::vector<Preset> All = MakePresets();
	return All;
}

const Preset *FindPreset(const std::string &Name)
{
	static const char *const ALIASES[][2] = {
		{ "flute", "soft lead" }, { "softlead", "soft lead" }, { "lead", "bright lead" }, { "brightlead", "bright lead" },
		{ "square", "bright lead" }, { "thinlead", "thin lead" }, { "nasal", "thin lead" }, { "harp", "pluck" },
		{ "guitar", "pluck" }, { "pluckylead", "pluck" }, { "chime", "bell" }, { "glockenspiel", "bell" },
		{ "chords", "soft chords" }, { "softchords", "soft chords" }, { "arpeggio", "soft chords" }, { "pad", "warm pad" },
		{ "warmpad", "warm pad" }, { "strings", "warm pad" }, { "bassline", "bass" }, { "softbass", "bass" },
		{ "pluckybass", "plucky bass" }, { "slapbass", "plucky bass" }, { "kickdrum", "kick" }, { "bassdrum", "kick" },
		{ "snaredrum", "snare" }, { "clap", "snare" }, { "hihat", "hi-hat" }, { "hat", "hi-hat" },
		{ "closedhihat", "hi-hat" }, { "openhihat", "open hi-hat" }, { "openhat", "open hi-hat" }, { "crash", "open hi-hat" },
		{ "cymbal", "open hi-hat" },
	};
	const std::string Wanted = Simplified(Name);
	if (Wanted.empty())
		return 0;
	const std::vector<Preset> &All = Presets();
	for (size_t i = 0; i < All.size(); ++i)
		if (Simplified(All[i].Name) == Wanted)
			return &All[i];
	for (size_t a = 0; a < sizeof(ALIASES) / sizeof(ALIASES[0]); ++a) {
		if (Wanted == ALIASES[a][0]) {
			for (size_t i = 0; i < All.size(); ++i)
				if (All[i].Name == ALIASES[a][1])
					return &All[i];
		}
	}
	return 0;
}

std::string PresetList()
{
	std::string Out;
	const std::vector<Preset> &All = Presets();
	for (size_t i = 0; i < All.size(); ++i) {
		if (i > 0)
			Out += ", ";
		Out += All[i].Name;
	}
	return Out;
}

std::string Vrc7Patch::InstrumentName() const
{
	std::string Out = Name;
	if (!Out.empty())
		Out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(Out[0])));
	return Out;
}

const std::vector<Vrc7Patch> &Vrc7Patches()
{
	static const std::vector<Vrc7Patch> All = MakeVrc7Patches();
	return All;
}

const Vrc7Patch *FindVrc7Patch(const std::string &Name)
{
	static const char *const WORDS[][2] = {
		{ "chime", "bell" }, { "glockenspiel", "bell" }, { "electricguitar", "guitar" }, { "keys", "piano" }, { "keyboard", "piano" },
		{ "sax", "clarinet" }, { "saxophone", "clarinet" }, { "oboe", "clarinet" }, { "reed", "clarinet" }, { "horn", "brass" },
		{ "trombone", "brass" }, { "organ", "reed organ" }, { "musicbox", "soft bell" }, { "celesta", "soft bell" },
		{ "marimba", "xylophone" }, { "mallet", "xylophone" }, { "vibes", "vibraphone" }, { "bass", "bass guitar" },
		{ "electricbass", "bass guitar" }, { "synth", "synthesizer" }, { "synthlead", "synthesizer" }, { "choir", "chorus" },
		{ "voices", "chorus" }, { "strings", "chorus" }, { "pad", "chorus" },
	};
	const std::string Wanted = Simplified(Name);
	if (Wanted.empty())
		return 0;
	const std::vector<Vrc7Patch> &All = Vrc7Patches();
	for (size_t i = 0; i < All.size(); ++i)
		if (Simplified(All[i].Name) == Wanted)
			return &All[i];
	for (size_t w = 0; w < sizeof(WORDS) / sizeof(WORDS[0]); ++w) {
		if (Wanted == WORDS[w][0]) {
			for (size_t i = 0; i < All.size(); ++i)
				if (All[i].Name == WORDS[w][1])
					return &All[i];
		}
	}
	// A preset's equivalent
	const Preset *pPreset = FindPreset(Name);
	if (pPreset != 0 && pPreset->Vrc7Patch >= 1 && pPreset->Vrc7Patch <= static_cast<int>(All.size()))
		return &All[static_cast<size_t>(pPreset->Vrc7Patch - 1)];
	return 0;
}

std::string Vrc7PatchList()
{
	std::string Out;
	const std::vector<Vrc7Patch> &All = Vrc7Patches();
	for (size_t i = 0; i < All.size(); ++i)
		Out += (i == 0 ? "" : ", ") + All[i].Name;
	return Out;
}

}	// namespace ftapi

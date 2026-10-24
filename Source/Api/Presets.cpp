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

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
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

std::vector<int> List(std::initializer_list<int> Items)
{
	return std::vector<int>(Items);
}

// A drum: a volume envelope, and noise pitches that move: Fixed ones it plays
// whatever its note is (a kick's falling thump), or ones relative to its note
Preset Drum(const char *Name, const char *Description, const std::vector<int> &Volume,
	const std::vector<int> &Pitches = std::vector<int>(), bool Fixed = true)
{
	Preset P;
	P.Name = Name;
	P.Description = Description;
	P.Drum = true;
	P.Vrc7Patch = 0;
	P.Sequences2A03.push_back(Sequence(SEQUENCE_VOLUME, Volume));
	if (!Pitches.empty()) {
		PresetSequence Arpeggio = Sequence(SEQUENCE_ARPEGGIO, Pitches);
		Arpeggio.Setting = Fixed ? SEQUENCE_FIXED : SEQUENCE_ABSOLUTE;
		P.Sequences2A03.push_back(Arpeggio);
	}
	return P;
}

// A vibrato for a pitch sequence: Delay frames of nothing, then a looping
// triangle wave Depth units a frame each way that sums to nothing (pitch
// sequences add to the pitch every frame), Rate frames a cycle
std::vector<int> VibratoItems(int Delay, int Depth, int Rate, int &Loop)
{
	std::vector<int> Items(static_cast<size_t>(std::max(0, Delay)), 0);
	Loop = static_cast<int>(Items.size());
	const int Quarter = std::max(1, Rate / 4);
	for (int i = 0; i < Quarter; ++i)
		Items.push_back(Depth);
	for (int i = 0; i < 2 * Quarter; ++i)
		Items.push_back(-Depth);
	for (int i = 0; i < Quarter; ++i)
		Items.push_back(Depth);
	return Items;
}

// The faint vibrato (VIBRATO_FAINT): Delay frames of nothing, then the period one step longer for half of each cycle of
// Rate frames and back for the other half, looping
std::vector<int> FaintVibratoItems(int Delay, int Rate, int &Loop)
{
	std::vector<int> Items(static_cast<size_t>(std::max(0, Delay)), 0);
	Loop = static_cast<int>(Items.size());
	const int Half = std::max(2, Rate / 2);
	Items.push_back(1);
	Items.insert(Items.end(), static_cast<size_t>(Half - 1), 0);
	Items.push_back(-1);
	Items.insert(Items.end(), static_cast<size_t>(Half - 1), 0);
	return Items;
}

// A melodic preset with a delayed vibrato built in
Preset Singing(const char *Name, const char *Description, const std::vector<int> &Volume, int Release, int Duty2A03,
	int DutyVRC6, int Delay, int Depth)
{
	Preset P = Melodic(Name, Description, Volume, Release, Duty2A03, DutyVRC6);
	int Loop;
	PresetSequence Pitch = Sequence(SEQUENCE_PITCH, VibratoItems(Delay, Depth, 12, Loop));
	Pitch.Loop = Loop;
	P.Sequences2A03.push_back(Pitch);
	P.SequencesVRC6.push_back(Pitch);
	return P;
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
	All.push_back(Melodic("lush chords", "steady 25% tone that settles a little and holds, for lush chords (their arpeggios "
		"are added to it); full in the instrument, so the volume column sets how soft it plays",
		List({ 15, 14, 14, 13, 13, 12, 12, 12, 11, 11, 11, 10, 9, 7, 5, 3, 2, 1, 0 }), 11, 1, 3));
	All.push_back(Melodic("warm pad", "slow, round and dark pad (50% duty); good for calm, sad or underwater moods",
		List({ 2, 3, 4, 5, 6, 7, 7, 8, 8, 7, 6, 5, 4, 3, 2, 1, 0 }), 8, 2, 7));
	All.push_back(Melodic("bass", "sustained bass for the triangle, the VRC6 sawtooth or a pulse",
		List({ 15, 15, 14, 13, 12, 12, 10, 8, 6, 4, 2, 0 }), 5, 2, 7));
	All.push_back(Melodic("thump bass", "punchy bass: each note starts an octave up for a frame and drops into place, a thump "
		"that gives the triangle (which has no volume to accent with) an attack; sharper on a pulse or the sawtooth",
		List({ 15, 15, 14, 13, 12, 12, 10, 8, 6, 4, 2, 0 }), 5, 2, 7, List({ 12, 0 })));
	All.push_back(Melodic("plucky bass", "short, bouncy bass notes that decay by themselves",
		List({ 15, 15, 14, 12, 10, 8, 7, 6, 5, 4, 3, 2, 1, 0 }), -1, 2, 7));
	All.push_back(Singing("singing lead", "expressive lead (25% duty) with a vibrato that comes in after a third of a second, "
		"so long notes sing without effects", List({ 9, 12, 13, 13, 12, 12, 11, 11, 9, 7, 5, 3, 2, 1, 0 }), 7, 1, 3, 20, 1));
	All.push_back(Singing("strings", "string section: swells in slowly, sustains with a gentle late vibrato (50% duty)",
		List({ 2, 3, 4, 5, 6, 7, 8, 9, 10, 10, 11, 11, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0 }), 12, 2, 7, 24, 1));
	{
		// A quick swell, from thin to round, scooping up into the note
		Preset Brass = Melodic("brass", "brass or horn: a quick swell from a thin to a round tone, scooping up into the note",
			List({ 8, 12, 14, 15, 15, 14, 13, 13, 13, 12, 10, 8, 6, 4, 2, 0 }), 8, 2, 7, List({ -2, -1, 0 }));
		Brass.Sequences2A03.back().Items = List({ 0, 0, 1, 1, 2 });
		Brass.SequencesVRC6.back().Items = List({ 1, 1, 3, 3, 5, 5, 7 });
		All.push_back(Brass);
	}
	{
		// Held at full volume; the duty moves to and fro like an organ's chorus
		Preset Organ = Melodic("organ", "organ: full and steady with a slow shimmer between two tones, stops at once",
			List({ 13, 13, 13, 12, 9, 5, 2, 0 }), 2, 2, 7);
		Organ.Sequences2A03.back().Items = List({ 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1 });
		Organ.Sequences2A03.back().Loop = 0;
		Organ.SequencesVRC6.back().Items = List({ 7, 7, 7, 7, 7, 7, 5, 5, 5, 5, 5, 5 });
		Organ.SequencesVRC6.back().Loop = 0;
		All.push_back(Organ);
	}
	{
		// The calm leads, as design_instrument's kinds of those names make them at 75 BPM (CALM_BEAT_FRAMES): none holds a
		// high level from attack to release, which presses however soft the parts around it
		static const char *const CALM[][2] = {
			{ "music box", "calm lead struck at 15 that fades to silence by itself, half gone in 16 frames and silent in 43 (under "
				"a beat at 64-82 BPM), like a music box (50% duty); design_instrument kind music box fits it to the tempo" },
			{ "breath", "calm lead that breathes in from 4 to 15 over 12 frames, sinks to 9 by frame 22 and holds it, with a faint "
				"vibrato after 24 frames, and fades over 24 frames after a release (50% duty)" },
			{ "falling strike", "calm lead struck at 15 that falls to 6 over 14 frames and holds it there, with a faint vibrato "
				"after 24 frames, fading quickly after a release (25% duty), as Final Fantasy III's Time Remains" },
		};
		for (size_t i = 0; i < sizeof(CALM) / sizeof(CALM[0]); ++i) {
			SoundDesign D;
			DesignOfKind(CALM[i][0], D);
			Preset P = DesignPreset(D);
			P.Name = CALM[i][0];
			P.Description = CALM[i][1];
			All.push_back(P);
		}
	}
	All.push_back(Drum("kick", "punchy noise kick that falls from a click to a thump (play it as \"kick\" on the noise channel)",
		List({ 15, 13, 10, 7, 4, 2, 1, 0 }), List({ 12, 8, 5, 3, 2, 1 })));
	All.push_back(Drum("snare", "noise snare drum with a bright snap (\"snare\")", List({ 15, 13, 11, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0 }),
		List({ 14, 12, 11, 11, 10 })));
	All.push_back(Drum("hi-hat", "short closed hi-hat (\"hihat\")", List({ 10, 6, 3, 1, 0 })));
	All.push_back(Drum("open hi-hat", "longer open hi-hat (\"open hi-hat\")", List({ 11, 9, 8, 7, 6, 5, 4, 3, 2, 1, 1, 0 })));
	All.push_back(Drum("crash", "crash cymbal that rings out, for the first beat of a new part (\"crash\")",
		List({ 15, 14, 13, 12, 11, 10, 9, 9, 8, 8, 7, 7, 6, 6, 5, 5, 4, 4, 3, 3, 3, 2, 2, 2, 1, 1, 1, 0 })));
	All.push_back(Drum("tom", "tom for fills that falls as it rings, pitched by its note (\"high tom\", \"mid tom\", \"low tom\")",
		List({ 15, 13, 11, 10, 8, 7, 6, 5, 4, 3, 2, 1, 0 }), List({ 0, 0, -1, -1, -2, -2, -3 }), false));

	// What sounds most like each on the VRC7
	static const struct { const char *Name; int Patch; } ON_VRC7[] = {
		{ "soft lead", 4 }, { "bright lead", 14 }, { "thin lead", 5 }, { "pluck", 2 }, { "bell", 1 }, { "soft chords", 8 },
		{ "lush chords", 11 }, { "warm pad", 15 }, { "bass", 13 }, { "thump bass", 13 }, { "plucky bass", 13 }, { "singing lead", 5 },
		{ "strings", 15 }, { "brass", 12 },
		{ "organ", 8 }, { "music box", 9 }, { "breath", 4 }, { "falling strike", 3 },
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

// Measured with emu2413 as FamiTracker plays it (a C-3 struck, held, released)
std::vector<Vrc7Custom> MakeVrc7Customs()
{
	struct Design { const char *Name; const char *Description; int Regs[8]; };
	static const Design DESIGNS[] = {
		{ "electric piano", "a bright, bell-like attack that mellows within a fifth of a second into a warm tone, fading over two "
			"seconds: comping chords and soft leads", { 0x01, 0x01, 0x48, 0x01, 0xF7, 0xF2, 0x95, 0x23 } },
		{ "soft electric piano", "the electric piano with a gentler attack: rounder, for quiet chords under a melody",
			{ 0x01, 0x01, 0x50, 0x00, 0xF6, 0xF2, 0x85, 0x23 } },
		{ "glass bell", "a glassy bell whose high partials ring and fade slowly into a pure tone: chimes, music boxes, night",
			{ 0x05, 0x01, 0x56, 0x00, 0xF4, 0xF2, 0xF3, 0x21 } },
		{ "fm pad", "a warm pad that swells in over a fifth of a second and holds, with a gentle vibrato: long chords",
			{ 0x21, 0x61, 0x64, 0x01, 0x52, 0x42, 0x12, 0x13 } },
		{ "fm bass", "a bass with a plucked, bright start that settles into a round, held body", { 0x00, 0x21, 0x50, 0x05, 0xF6,
			0xF4, 0xF6, 0x26 } },
		{ "wooden mallet", "a soft, woody mallet that dies away within a second: marimba-like riffs and arpeggios",
			{ 0x03, 0x01, 0x5A, 0x00, 0xF9, 0xF5, 0xF7, 0x34 } },
	};
	std::vector<Vrc7Custom> All;
	for (size_t i = 0; i < sizeof(DESIGNS) / sizeof(DESIGNS[0]); ++i) {
		Vrc7Custom C;
		C.Name = DESIGNS[i].Name;
		C.Description = DESIGNS[i].Description;
		for (int r = 0; r < 8; ++r)
			C.Regs[r] = DESIGNS[i].Regs[r];
		All.push_back(C);
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

Preset WithArpeggio(const Preset &Base, const std::vector<int> &Notes, int Frames, const std::string &Name)
{
	Preset P = Base;
	P.Name = Name;
	PresetSequence Arpeggio = Sequence(SEQUENCE_ARPEGGIO, std::vector<int>(), 0);
	for (size_t i = 0; i < Notes.size(); ++i)
		Arpeggio.Items.insert(Arpeggio.Items.end(), static_cast<size_t>(std::max(1, Frames)), Notes[i]);
	std::vector<PresetSequence> *Lists[3] = { &P.Sequences2A03, &P.SequencesVRC6, &P.Sequences5B };
	for (int l = 0; l < 3; ++l) {
		std::vector<PresetSequence> &List = *Lists[l];
		if (l == 2 && List.empty())
			continue;	// not a 5B sound: on the 5B it plays the 2A03's, arpeggio and all
		for (size_t s = List.size(); s-- > 0; )
			if (List[s].Type == SEQUENCE_ARPEGGIO)
				List.erase(List.begin() + static_cast<std::ptrdiff_t>(s));
		List.push_back(Arpeggio);
	}
	return P;
}

bool Preset::Releases() const
{
	const std::vector<PresetSequence> &List = Sequences2A03.empty() ? Sequences5B : Sequences2A03;
	for (size_t i = 0; i < List.size(); ++i)
		if (List[i].Type == SEQUENCE_VOLUME)
			return List[i].Release >= 0;
	return false;
}

int Volume5B(int Volume)
{
	if (Volume <= 0)
		return 0;
	const double Steps = 15.0 + (20.0 / 3.0) * std::log10(std::min(15, Volume) / 15.0);
	return std::max(1, std::min(15, static_cast<int>(std::floor(Steps + 0.5))));
}

std::vector<PresetSequence> Preset5BSequences(const Preset &Sound)
{
	if (!Sound.Sequences5B.empty())
		return Sound.Sequences5B;
	std::vector<PresetSequence> Out;
	for (size_t i = 0; i < Sound.Sequences2A03.size(); ++i) {
		PresetSequence S = Sound.Sequences2A03[i];
		if (S.Type == SEQUENCE_DUTY)
			continue;
		if (S.Type == SEQUENCE_VOLUME)
			for (size_t k = 0; k < S.Items.size(); ++k)
				S.Items[k] = Volume5B(S.Items[k]);
		Out.push_back(S);
	}
	return Out;
}

namespace {

// A 5B sound: its volume envelope (3 dB a step), its noise / mode a frame at a
// time (none: the square alone), an arpeggio or a pitch sequence
Preset Sound5B(const char *Name, const char *Description, const std::vector<int> &Volume, int Release,
	const std::vector<int> &Mode = std::vector<int>(), const std::vector<int> &Arpeggio = std::vector<int>())
{
	Preset P;
	P.Name = Name;
	P.Description = Description;
	P.Drum = false;
	P.Vrc7Patch = 0;
	P.Sequences5B.push_back(Sequence(SEQUENCE_VOLUME, Volume, -1, Release));
	if (!Arpeggio.empty())
		P.Sequences5B.push_back(Sequence(SEQUENCE_ARPEGGIO, Arpeggio));
	if (!Mode.empty())
		P.Sequences5B.push_back(Sequence(SEQUENCE_DUTY, Mode));
	return P;
}

std::vector<Preset> MakeSunsoft5BSounds()
{
	std::vector<Preset> All;
	All.push_back(Sound5B("square lead", "plain, full square lead: strikes at full and holds 6 dB under it",
		List({ 15, 15, 14, 14, 13, 11, 9, 7, 4, 0 }), 4));
	Preset Singing = Sound5B("singing square", "the square lead with a gentle vibrato that comes in after a third of a second",
		List({ 15, 15, 14, 14, 13, 11, 9, 7, 4, 0 }), 4);
	int Loop;
	PresetSequence Vibrato = Sequence(SEQUENCE_PITCH, VibratoItems(18, 1, 12, Loop));
	Vibrato.Loop = Loop;
	Singing.Sequences5B.push_back(Vibrato);
	All.push_back(Singing);
	Preset Pad = Sound5B("square pad", "soft square pad that swells in over a fifth of a second and holds, with a slow release",
		List({ 8, 9, 10, 10, 11, 11, 12, 12, 12, 13, 12, 11, 10, 9, 8, 7, 5, 3, 0 }), 9);
	PresetSequence Slow = Sequence(SEQUENCE_PITCH, VibratoItems(24, 1, 16, Loop));
	Slow.Loop = Loop;
	Pad.Sequences5B.push_back(Slow);
	All.push_back(Pad);
	All.push_back(Sound5B("square pluck", "plucked square that falls quickly to a level it holds (its notes don't vanish)",
		List({ 15, 14, 13, 12, 12, 11, 11, 10, 8, 6, 3, 0 }), 7));
	All.push_back(Sound5B("square bell", "bell: an octave blip on the strike, then a ring that settles low and holds",
		List({ 15, 14, 13, 12, 12, 11, 11, 11, 10, 10, 10, 10, 9, 8, 6, 4, 0 }), 12, std::vector<int>(), List({ 12, 0 })));
	All.push_back(Sound5B("buzz bass", "the 5B's buzz: its envelope as a falling saw at the note's pitch, no square; a strong, "
		"bright bass. The envelope sets the level (the volume column doesn't change it; 0 silences it), and the chip has one: "
		"play it on one channel at a time", List({ 15, 0 }), 0, List({ S5B_ENVELOPE })));
	All.push_back(Sound5B("sync buzz", "the square and the envelope together: a hard, nasal buzz for a bass or a lead (the "
		"envelope sets the level; one channel at a time)", List({ 15, 0 }), 0, List({ S5B_SQUARE | S5B_ENVELOPE })));
	All.push_back(Sound5B("noise hat", "closed hi-hat: the noise at its highest pitch, gone in a few frames (any note plays it)",
		List({ 13, 10, 7, 4, 1, 0 }), -1, List({ S5B_NOISE | 31 })));
	All.push_back(Sound5B("noise crash", "crash: bright noise that rings out for most of a second (any note plays it)",
		List({ 14, 13, 13, 12, 12, 11, 11, 11, 10, 10, 10, 9, 9, 9, 8, 8, 8, 7, 7, 6, 6, 5, 5, 4, 3, 2, 1, 0 }), -1,
		List({ S5B_NOISE | 28 })));
	All.push_back(Sound5B("noise snare", "snare: a square snap at the note's pitch, then noise",
		List({ 15, 14, 13, 12, 11, 10, 9, 7, 5, 3, 0 }), -1,
		List({ S5B_NOISE | S5B_SQUARE | 26, S5B_NOISE | 24, S5B_NOISE | 22, S5B_NOISE | 20 })));
	All.push_back(Sound5B("square kick", "kick: a click, then a square that drops two octaves in a tenth of a second",
		List({ 15, 15, 14, 13, 12, 10, 8, 5, 0 }), -1, List({ S5B_NOISE | S5B_SQUARE | 20, S5B_SQUARE }),
		List({ 0, -4, -9, -14, -19, -24 })));
	All.push_back(Sound5B("square tom", "tom: a square that drops a fifth as it fades (its pitch is the note's)",
		List({ 15, 14, 13, 12, 11, 10, 8, 6, 3, 0 }), -1, std::vector<int>(), List({ 0, -1, -2, -3, -4, -5, -6, -7 })));
	return All;
}

}	// namespace

const std::vector<Preset> &Sunsoft5BSounds()
{
	static const std::vector<Preset> All = MakeSunsoft5BSounds();
	return All;
}

const Preset *Find5BSound(const std::string &Name)
{
	static const char *const ALIASES[][2] = {
		{ "lead", "square lead" }, { "square", "square lead" }, { "vibratosquare", "singing square" },
		{ "pad", "square pad" }, { "softpad", "square pad" }, { "softsquarepad", "square pad" }, { "pluck", "square pluck" },
		{ "bell", "square bell" }, { "chime", "square bell" }, { "buzz", "buzz bass" }, { "envelopebass", "buzz bass" },
		{ "envelopebuzz", "buzz bass" }, { "buzzer", "buzz bass" }, { "sawbass", "buzz bass" }, { "sync", "sync buzz" },
		{ "syncbass", "sync buzz" }, { "hat", "noise hat" }, { "hihat", "noise hat" }, { "closedhihat", "noise hat" },
		{ "openhihat", "noise crash" }, { "crash", "noise crash" }, { "cymbal", "noise crash" }, { "snare", "noise snare" },
		{ "snaredrum", "noise snare" }, { "clap", "noise snare" }, { "noise", "noise hat" }, { "kick", "square kick" },
		{ "kickdrum", "square kick" }, { "bassdrum", "square kick" },
		{ "tom", "square tom" }, { "toms", "square tom" },
	};
	const std::string Wanted = Simplified(Name);
	if (Wanted.empty())
		return 0;
	const std::vector<Preset> &All = Sunsoft5BSounds();
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

std::string Sound5BList()
{
	std::string Out;
	const std::vector<Preset> &All = Sunsoft5BSounds();
	for (size_t i = 0; i < All.size(); ++i)
		Out += (i > 0 ? ", " : "") + All[i].Name;
	return Out;
}

const Preset *FindSoundFor5B(const std::string &Name)
{
	const std::string Wanted = Simplified(Name);
	const Preset *pOwn = Find5BSound(Name), *pPreset = FindPreset(Name);
	if (pPreset != 0 && pPreset->Drum)
		pPreset = 0;
	if (pOwn != 0 && Simplified(pOwn->Name) == Wanted)
		return pOwn;
	if (pPreset != 0 && Simplified(pPreset->Name) == Wanted)
		return pPreset;
	return pOwn != 0 ? pOwn : pPreset;
}

std::string Preset::InstrumentName(bool Sawtooth) const
{
	std::string Out = Name;
	if (!Out.empty())
		Out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(Out[0])));
	const std::string Mark = " (saw)";
	if (Sawtooth && !(Out.size() >= Mark.size() && Out.compare(Out.size() - Mark.size(), Mark.size(), Mark) == 0))
		Out += Mark;	// once, when the name has it already
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
		{ "lush", "lush chords" }, { "jazzchords", "lush chords" },
		{ "warmpad", "warm pad" }, { "violin", "strings" }, { "violins", "strings" }, { "bassline", "bass" }, { "softbass", "bass" },
		{ "vibratolead", "singing lead" }, { "singinglead", "singing lead" }, { "vocal", "singing lead" }, { "voice", "singing lead" },
		{ "horn", "brass" }, { "horns", "brass" }, { "trumpet", "brass" }, { "keys", "organ" },
		{ "pluckybass", "plucky bass" }, { "slapbass", "plucky bass" }, { "punchybass", "thump bass" }, { "blipbass", "thump bass" },
		{ "thumpbass", "thump bass" }, { "kickdrum", "kick" }, { "bassdrum", "kick" },
		{ "snaredrum", "snare" }, { "clap", "snare" }, { "hihat", "hi-hat" }, { "hat", "hi-hat" },
		{ "closedhihat", "hi-hat" }, { "openhihat", "open hi-hat" }, { "openhat", "open hi-hat" }, { "cymbal", "crash" },
		{ "crashcymbal", "crash" }, { "toms", "tom" }, { "hightom", "tom" }, { "midtom", "tom" }, { "lowtom", "tom" },
		{ "floortom", "tom" }, { "musicboxlead", "music box" }, { "breathlead", "breath" }, { "breathylead", "breath" },
		{ "fallinglead", "falling strike" },
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

std::string Vrc7Custom::InstrumentName() const
{
	std::string Out = Name;
	if (!Out.empty())
		Out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(Out[0])));
	return Out;
}

const std::vector<Vrc7Custom> &Vrc7Customs()
{
	static const std::vector<Vrc7Custom> All = MakeVrc7Customs();
	return All;
}

const Vrc7Custom *FindVrc7Custom(const std::string &Name)
{
	static const char *const WORDS[][2] = {
		{ "epiano", "electric piano" }, { "rhodes", "electric piano" }, { "ep", "electric piano" }, { "fmpiano", "electric piano" },
		{ "softepiano", "soft electric piano" }, { "softrhodes", "soft electric piano" }, { "softep", "soft electric piano" },
		{ "glassbells", "glass bell" }, { "fmbell", "glass bell" }, { "fmsynthpad", "fm pad" }, { "fmbassguitar", "fm bass" },
		{ "woodenmallets", "wooden mallet" }, { "woodmallet", "wooden mallet" },
	};
	const std::string Wanted = Simplified(Name);
	if (Wanted.empty())
		return 0;
	const std::vector<Vrc7Custom> &All = Vrc7Customs();
	for (size_t i = 0; i < All.size(); ++i)
		if (Simplified(All[i].Name) == Wanted)
			return &All[i];
	for (size_t w = 0; w < sizeof(WORDS) / sizeof(WORDS[0]); ++w)
		if (Wanted == WORDS[w][0])
			for (size_t i = 0; i < All.size(); ++i)
				if (All[i].Name == WORDS[w][1])
					return &All[i];
	return 0;
}

std::string Vrc7CustomList()
{
	std::string Out;
	const std::vector<Vrc7Custom> &All = Vrc7Customs();
	for (size_t i = 0; i < All.size(); ++i)
		Out += (i == 0 ? "" : ", ") + All[i].Name;
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

// Sounds in words

SoundDesign::SoundDesign() : Drum(false), Peak(13), Attack(1), AttackFrom(0), Decay(8), DecayStyle(DECAY_SMOOTH), Sustain(10),
	Release(6), Restrike(0), Echo(false), Duty(1), Motion(DUTY_STILL), Click(false), Vibrato(0), VibratoDelay(20), VibratoRate(12),
	AttackPitch(PITCH_PLAIN), ArpeggioFrames(1), Kind(DRUM_KICK), Tone(0), Length(0)
{
}

namespace {

const int MAX_ITEMS = 252;	// a FamiTracker sequence's most

int Clamp(int Value, int Low, int High)
{
	return std::max(Low, std::min(High, Value));
}

// The Frames levels after the peak as it decays to Sustain, faster at first (a
// quadratic, as a struck string or a music box dies away), reaching Sustain on
// the last: stretched so its rounding gets there then, not a quarter sooner
std::vector<int> DecayItems(int Peak, int Sustain, int Frames)
{
	std::vector<int> Out;
	const int Fall = Peak - Sustain;
	for (int i = 1; i <= Frames; ++i) {
		if (Fall <= 0 || i == Frames) {
			Out.push_back(Sustain);
			continue;
		}
		// Sustain + Fall * Left^2 rounds to Sustain once Left^2 < 0.5 / Fall: on frame Frames, half a frame past Length * (1 - Edge)
		const double Edge = std::sqrt(0.5 / Fall), Length = (Frames - 0.5) / (1.0 - Edge);
		const double Left = 1.0 - i / Length;
		Out.push_back(static_cast<int>(std::floor(Sustain + Fall * Left * Left + 0.5)));
	}
	return Out;
}

// Attack, decay to the sustain level (held at the release point, struck again
// in a loop when it restrikes), release, and an echo after it
std::vector<int> Envelope(const SoundDesign &D, int &ReleasePoint, int &LoopPoint)
{
	const int Peak = Clamp(D.Peak, 1, 15);
	const int Sustain = Clamp(D.Sustain, 0, Peak);
	const int Attack = Clamp(D.Attack, 0, 60), Release = Clamp(D.Release, 0, 60);
	const int Decay = Sustain == 0 ? Clamp(D.Decay, 4, 120) : Clamp(D.Decay, 0, 120);
	const int From = Clamp(D.AttackFrom, 0, Peak);
	std::vector<int> Out;
	if (From > 0)		// a bloom: from its level to the peak
		for (int i = 0; i < Attack; ++i)
			Out.push_back(From + (Peak - From) * i / std::max(1, Attack));
	else
		for (int i = 1; i <= Attack; ++i)
			Out.push_back(std::max(1, (Peak * i + Attack / 2) / (Attack + 1)));
	Out.push_back(Peak);
	if (D.DecayStyle == DECAY_STEPS && Decay > 0) {
		// Plateaus a few levels apart, each held a while
		const int Steps = std::max(1, (Peak - Sustain + 2) / 3);
		const int Hold = std::max(2, Decay / Steps);
		for (int s = 0; s < Steps; ++s) {
			const int Level = s == 0 ? Peak : std::max(Sustain, Peak - 3 * s);
			for (int i = s == 0 ? 1 : 0; i < Hold; ++i)
				Out.push_back(Level);
		}
	}
	else {
		const std::vector<int> Fall = DecayItems(Peak, Sustain, Decay);
		Out.insert(Out.end(), Fall.begin(), Fall.end());
	}
	if (Out.back() != Sustain)
		Out.push_back(Sustain);
	ReleasePoint = LoopPoint = -1;
	if (Sustain == 0 && D.Restrike >= 4) {
		// Struck again every Restrike frames while the note holds: the strike and as much of its fade as fits (or silence
		// after it), looped; a release lets the last of it fade out
		const int Cycle = std::max(Clamp(D.Restrike, 4, 120), Attack + 1);
		Out.resize(static_cast<size_t>(Cycle), 0);
		LoopPoint = 0;
		ReleasePoint = Cycle - 1;
		const int Last = Out.back();
		for (int i = 1; i <= Release && Out.back() > 0; ++i)
			Out.push_back(static_cast<int>(std::floor(Last * (1.0 - i / static_cast<double>(Release + 1)) + 0.5)));
		if (Out.back() != 0)
			Out.push_back(0);
	}
	if (Sustain > 0) {
		const int Restrike = Clamp(D.Restrike, 0, 120);
		if (Restrike >= 4) {
			// Held: struck again every Restrike frames, a dip and a rise back
			LoopPoint = static_cast<int>(Out.size());
			for (int i = 0; i < Restrike - 2; ++i)
				Out.push_back(Sustain);
			Out.push_back(std::max(1, Sustain - 5));
			Out.push_back(std::max(1, Sustain - 2));
		}
		ReleasePoint = static_cast<int>(Out.size()) - 1;
		for (int i = 1; i <= Release && Out.back() > 0; ++i)
			Out.push_back(static_cast<int>(std::floor(Sustain * (1.0 - i / static_cast<double>(Release + 1)) + 0.5)));
		if (Out.back() != 0)
			Out.push_back(0);
	}
	if (D.Echo) {
		// Quiet for a moment, then a soft repeat that fades
		if (Out.back() != 0)
			Out.push_back(0);
		Out.push_back(0);
		Out.push_back(0);
		const int Echo = std::max(2, (Peak + 2) / 3);
		for (int Level = Echo; Level >= 1; --Level) {
			Out.push_back(Level);
			Out.push_back(Level);
		}
		Out.push_back(0);
	}
	if (Out.size() > MAX_ITEMS) {
		Out.resize(MAX_ITEMS);
		Out.back() = 0;
		ReleasePoint = std::min(ReleasePoint, MAX_ITEMS - 2);
		if (LoopPoint > ReleasePoint)
			LoopPoint = -1;
	}
	return Out;
}

// The 2A03's duty (0-3) as the VRC6's nearest (0-7)
int Vrc6Duty(int Duty)
{
	static const int NEAREST[4] = { 1, 3, 7, 7 };
	return NEAREST[Clamp(Duty, 0, 3)];
}

std::vector<int> DutyItems(int Duty, int Motion, bool Click, bool Vrc6, int &Loop)
{
	Loop = -1;
	const int d = Vrc6 ? Vrc6Duty(Duty) : Clamp(Duty, 0, 3);
	std::vector<int> Items;
	if (Click)		// the first frame thin, or round when the note is thin itself; a 75% note clicks at 50%, the
					// thump of a pulse bass
		Items.push_back(Vrc6 ? (d >= 3 ? 1 : 7) : (d == 3 ? 2 : (d >= 1 ? 0 : 2)));
	switch (Motion) {
	case DUTY_SWEEP:	// thin to the duty on the attack
		for (int Step = Vrc6 ? 1 : 0; Step < d; Step += Vrc6 ? 2 : 1) {
			Items.push_back(Step);
			Items.push_back(Step);
		}
		Items.push_back(d);
		break;
	case DUTY_WOBBLE: {	// to and fro: a shimmer like a chorus
		const int Other = Vrc6 ? (d >= 3 ? d - 2 : d + 2) : (d >= 2 ? d - 1 : d + 1);
		Loop = static_cast<int>(Items.size());
		for (int i = 0; i < 12; ++i)
			Items.push_back(i < 6 ? d : Other);
		break;
	}
	case DUTY_FADE:		// thinner as it fades
		for (int Step = d; Step >= 0; Step -= Vrc6 ? 2 : 1)
			for (int i = 0; i < 6; ++i)
				Items.push_back(Step);
		break;
	default:
		Items.push_back(d);
		break;
	}
	return Items;
}

}	// namespace

Preset DesignPreset(const SoundDesign &D)
{
	Preset P;
	P.Name = D.Name;
	P.Description = "designed with design_instrument";
	P.Drum = D.Drum;
	P.Vrc7Patch = 0;
	if (D.Drum) {
		// Noise: a volume envelope, and pitches that fall
		std::vector<int> Volume, Pitches;
		bool Fixed = true;
		switch (D.Kind) {
		case DRUM_SNARE:
			Volume = List({ 15, 13, 11, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0 });
			Pitches = List({ 14, 12, 11, 11, 10 });
			break;
		case DRUM_HIHAT:
			Volume = List({ 10, 6, 3, 1, 0 });
			Pitches = List({ 13 });	// D-#: at E-# it's about 4 dB quieter, most of it above hearing
			break;
		case DRUM_OPEN_HIHAT:
			Volume = List({ 11, 9, 8, 7, 6, 5, 4, 3, 2, 1, 1, 0 });
			Pitches = List({ 13 });
			break;
		case DRUM_CRASH:
			Volume = List({ 15, 14, 13, 12, 11, 10, 9, 9, 8, 8, 7, 7, 6, 6, 5, 5, 4, 4, 3, 3, 3, 2, 2, 2, 1, 1, 1, 0 });
			Pitches = List({ 12 });
			break;
		case DRUM_TOM:
			Volume = List({ 15, 13, 11, 10, 8, 7, 6, 5, 4, 3, 2, 1, 0 });
			Pitches = List({ 0, 0, -1, -1, -2, -2, -3 });
			Fixed = false;
			break;
		default:	// kick
			Volume = List({ 15, 13, 10, 7, 4, 2, 1, 0 });
			Pitches = List({ 12, 8, 5, 3, 2, 1 });
			break;
		}
		const int Peak = Clamp(D.Peak, 1, 15);
		for (size_t i = 0; i < Volume.size(); ++i)
			Volume[i] = Volume[i] == 0 ? 0 : std::max(1, (Volume[i] * Peak + 7) / 15);
		if (D.Length > 0) {			// longer: each step twice
			std::vector<int> Longer;
			for (size_t i = 0; i + 1 < Volume.size(); ++i) {
				Longer.push_back(Volume[i]);
				Longer.push_back(Volume[i]);
			}
			Longer.push_back(0);
			Volume = Longer;
		}
		else if (D.Length < 0 && Volume.size() > 4) {	// shorter: every other step
			std::vector<int> Shorter;
			for (size_t i = 0; i + 1 < Volume.size(); i += 2)
				Shorter.push_back(Volume[i]);
			Shorter.push_back(0);
			Volume = Shorter;
		}
		if (Fixed)
			for (size_t i = 0; i < Pitches.size(); ++i)
				Pitches[i] = Clamp(Pitches[i] + 2 * D.Tone, 0, 15);
		P.Sequences2A03.push_back(Sequence(SEQUENCE_VOLUME, Volume));
		PresetSequence Arpeggio = Sequence(SEQUENCE_ARPEGGIO, Pitches);
		Arpeggio.Setting = Fixed ? SEQUENCE_FIXED : SEQUENCE_ABSOLUTE;
		P.Sequences2A03.push_back(Arpeggio);
		return P;
	}

	int Release, VolumeLoop;
	const std::vector<int> Volume = Envelope(D, Release, VolumeLoop);
	P.Sequences2A03.push_back(Sequence(SEQUENCE_VOLUME, Volume, VolumeLoop, Release));
	P.SequencesVRC6.push_back(Sequence(SEQUENCE_VOLUME, Volume, VolumeLoop, Release));

	// Arpeggio: a chord looped, or the attack's pitch
	std::vector<int> Arpeggio;
	int ArpeggioLoop = -1;
	if (!D.Arpeggio.empty()) {
		for (size_t i = 0; i < D.Arpeggio.size(); ++i)
			for (int f = 0; f < Clamp(D.ArpeggioFrames, 1, 8); ++f)
				Arpeggio.push_back(Clamp(D.Arpeggio[i], -24, 24));
		ArpeggioLoop = 0;
	}
	else if (D.AttackPitch == PITCH_BLIP)
		Arpeggio = List({ 12, 12, 0 });
	else if (D.AttackPitch == PITCH_SCOOP)
		Arpeggio = List({ -2, -1, 0 });	// a whole step, as a horn bends up into a note (a third smears quick notes)
	else if (D.AttackPitch == PITCH_THUMP)
		Arpeggio = List({ 5, 3, 1, 0 });
	if (!Arpeggio.empty()) {
		P.Sequences2A03.push_back(Sequence(SEQUENCE_ARPEGGIO, Arpeggio, ArpeggioLoop));
		P.SequencesVRC6.push_back(Sequence(SEQUENCE_ARPEGGIO, Arpeggio, ArpeggioLoop));
	}

	// Pitch: a fall, then the vibrato
	std::vector<int> Pitch;
	int PitchLoop = -1;
	if (D.AttackPitch == PITCH_DROP)
		Pitch = List({ 0, 2, 3, 4, 5, 6, 7, 8 });
	else if (D.AttackPitch == PITCH_FLICK)
		Pitch = List({ -4, 3, 1 });		// up and back: the period's steps add up to nothing
	if (D.Vibrato > 0) {
		int Loop;
		const int Delay = std::max(0, D.VibratoDelay - static_cast<int>(Pitch.size())), Rate = std::max(4, D.VibratoRate / 4 * 4);
		const std::vector<int> Wave = D.Vibrato == VIBRATO_FAINT ? FaintVibratoItems(Delay, Rate, Loop) :
			VibratoItems(Delay, Clamp(D.Vibrato, 1, 3), Rate, Loop);
		PitchLoop = static_cast<int>(Pitch.size()) + Loop;
		Pitch.insert(Pitch.end(), Wave.begin(), Wave.end());
	}
	if (!Pitch.empty() && Pitch.size() <= MAX_ITEMS) {
		P.Sequences2A03.push_back(Sequence(SEQUENCE_PITCH, Pitch, PitchLoop));
		P.SequencesVRC6.push_back(Sequence(SEQUENCE_PITCH, Pitch, PitchLoop));
	}

	int Loop;
	std::vector<int> Duty = DutyItems(D.Duty, D.Motion, D.Click, false, Loop);
	P.Sequences2A03.push_back(Sequence(SEQUENCE_DUTY, Duty, Loop));
	Duty = DutyItems(D.Duty, D.Motion, D.Click, true, Loop);
	P.SequencesVRC6.push_back(Sequence(SEQUENCE_DUTY, Duty, Loop));
	return P;
}

std::string DescribeDesign(const SoundDesign &D, int FrameRate, const std::string &Wave)
{
	char Buffer[512];
	const double Rate = FrameRate > 0 ? FrameRate : 60;
	if (D.Drum) {
		static const char *const KINDS[] = { "kick", "snare", "closed hi-hat", "open hi-hat", "crash", "tom" };
		std::snprintf(Buffer, sizeof(Buffer), "a %s%s%s at volume %d%s", D.Tone < 0 ? "dark " : D.Tone > 0 ? "bright " : "",
			D.Length < 0 ? "short " : D.Length > 0 ? "long " : "", KINDS[Clamp(D.Kind, 0, 5)], Clamp(D.Peak, 1, 15),
			D.Kind == DRUM_KICK || D.Kind == DRUM_SNARE ? ", its noise falling from bright to dark" :
			D.Kind == DRUM_TOM ? ", falling as it rings (pitched by its note)" : "");
		return Buffer;
	}
	static const char *const DUTIES[] = { "12.5%", "25%", "50%", "75%" };
	const int Sustain = Clamp(D.Sustain, 0, Clamp(D.Peak, 1, 15));
	const bool Pulse = Wave != "sawtooth" && Wave != "triangle" && Wave != "square";	// "square": the 5B's
	std::string Out;
	if (Wave == "triangle") {
		// No volume: the envelope only starts the note and stops it at silence
		if (Sustain == 0) {
			std::snprintf(Buffer, sizeof(Buffer), "a triangle that sounds for about %d frames, then stops", std::max(4, D.Attack + D.Decay));
			Out = Buffer;
		}
		else
			Out = "a triangle at its one loudness, held until the next note or a release (===)";
	}
	else {
		std::snprintf(Buffer, sizeof(Buffer), "a %s that %s %d %s", Wave == "noise" ? "noise envelope" : Pulse ?
			(std::string(DUTIES[Clamp(D.Duty, 0, 3)]) + " pulse").c_str() : Wave == "square" ? "5B square (its levels as on a pulse)" :
			"sawtooth", D.AttackFrom > 0 && D.Attack > 0 ? ("blooms from " + std::to_string(Clamp(D.AttackFrom, 1, 15)) + " to").c_str() :
			"reaches", Clamp(D.Peak, 1, 15), D.Attack <= 0 ? "at once" :
			(std::to_string(D.Attack) + (D.Attack == 1 ? " frame in" : " frames in")).c_str());
		Out = Buffer;
	}
	if (D.Click && Pulse)
		Out += std::string(" after a ") + (D.Duty >= 1 && D.Duty != 3 ? "12.5%" : "50%") + " duty click on the first frame";
	Buffer[0] = 0;
	if (Wave == "triangle")
		;	// said above
	else if (Sustain == 0)
		std::snprintf(Buffer, sizeof(Buffer), ", then dies away over %d frames by itself", std::max(4, D.Decay));
	else if (Sustain == Clamp(D.Peak, 1, 15))
		std::snprintf(Buffer, sizeof(Buffer), ", holds, and fades over %d frames after a release (===)", D.Release);
	else
		std::snprintf(Buffer, sizeof(Buffer), ", settles to %d over %d frames, and fades over %d frames after a release (===)", Sustain,
			D.Decay, D.Release);
	Out += Buffer;
	if (D.DecayStyle == DECAY_STEPS && D.Decay > 0 && Sustain < Clamp(D.Peak, 1, 15) && Wave != "triangle")
		Out += " (stepping down a few levels at a time)";
	if (D.Restrike >= 4 && Wave != "triangle") {
		std::snprintf(Buffer, sizeof(Buffer), "; struck again every %d frames while it holds%s", Sustain == 0 ?
			std::max(D.Restrike, D.Attack + 1) : D.Restrike, Sustain == 0 ? " (until a release, ===)" : "");
		Out += Buffer;
	}
	if (D.Echo && Wave != "triangle")
		Out += "; a soft echo after it";
	static const char *const MOTIONS[] = { "", "; its tone opens from thin on the attack", "; its tone shimmers to and fro",
		"; its tone thins as it fades" };
	if (Pulse)
		Out += MOTIONS[Clamp(D.Motion, 0, 3)];
	if (D.AttackPitch == PITCH_BLIP)
		Out += "; an octave blip on the attack";
	else if (D.AttackPitch == PITCH_SCOOP)
		Out += "; it scoops up into the note from a whole step below";
	else if (D.AttackPitch == PITCH_DROP)
		Out += "; its pitch falls";
	else if (D.AttackPitch == PITCH_FLICK)
		Out += "; a quick pitch flick up and back on the attack";
	else if (D.AttackPitch == PITCH_THUMP)
		Out += "; it thumps: starts a fourth up and drops into the note";
	if (!D.Arpeggio.empty()) {
		Out += "; it arpeggiates";
		for (size_t i = 0; i < D.Arpeggio.size(); ++i)
			Out += (i == 0 ? " " : ", ") + std::to_string(D.Arpeggio[i]);
		Out += " semitones";
	}
	if (D.Vibrato > 0) {
		// How far each takes A-3 (period 253, about 6.8 cents a step) either way: the faint one a step for half the time
		static const char *const DEPTHS[] = { "", "gentle", "medium", "strong", "faint" };
		static const int CENTS[] = { 0, 20, 40, 60, 3 };
		const int Depth = D.Vibrato == VIBRATO_FAINT ? VIBRATO_FAINT : Clamp(D.Vibrato, 1, 3);
		std::snprintf(Buffer, sizeof(Buffer), "; a %s vibrato from %d frames (%.2g s) into each note, about %d cents either way at A-3",
			DEPTHS[Depth], D.VibratoDelay, D.VibratoDelay / Rate, CENTS[Depth]);
		Out += Buffer;
	}
	if (Wave == "noise")
		Out += "; its note picks the noise's pitch, from 0-# (dark, a deep wash) to F-# (bright, a hiss)";
	return Out;
}

bool DesignOfKind(const std::string &Kind, SoundDesign &D)
{
	const std::string K = Simplified(Kind);
	D = SoundDesign();
	if (K == "lead" || K == "melody") {
		D.Attack = 1; D.Peak = 13; D.Decay = 8; D.Sustain = 11; D.Release = 6; D.Duty = 1; D.Vibrato = 1;
	}
	else if (K == "pad") {
		D.Attack = 10; D.Peak = 11; D.Decay = 0; D.Sustain = 11; D.Release = 16; D.Duty = 2; D.Vibrato = 1; D.VibratoDelay = 30;
	}
	else if (K == "pluck" || K == "harp" || K == "guitar") {
		D.Attack = 0; D.Peak = 15; D.Decay = 24; D.Sustain = 0; D.Duty = 1;
	}
	else if (K == "bass") {
		D.Attack = 0; D.Peak = 15; D.Decay = 6; D.Sustain = 12; D.Release = 3; D.Duty = 2;
	}
	else if (K == "keys" || K == "piano") {
		D.Attack = 0; D.Peak = 14; D.Decay = 40; D.Sustain = 3; D.Release = 6; D.Duty = 2; D.Motion = DUTY_FADE;
	}
	else if (K == "bell" || K == "chime") {
		D.Attack = 0; D.Peak = 15; D.Decay = 40; D.Sustain = 0; D.Duty = 1; D.AttackPitch = PITCH_BLIP;
	}
	else if (K == "brass" || K == "horn") {
		D.Attack = 3; D.Peak = 15; D.Decay = 6; D.Sustain = 13; D.Release = 6; D.Duty = 2; D.Motion = DUTY_SWEEP; D.AttackPitch = PITCH_SCOOP;
	}
	else if (K == "strings" || K == "violin") {
		D.Attack = 14; D.Peak = 12; D.Decay = 0; D.Sustain = 12; D.Release = 12; D.Duty = 2; D.Vibrato = 1; D.VibratoDelay = 24;
	}
	else if (K == "organ") {
		D.Attack = 0; D.Peak = 13; D.Decay = 0; D.Sustain = 13; D.Release = 2; D.Duty = 2; D.Motion = DUTY_WOBBLE;
	}
	else if (K == "chiptune" || K == "arp" || K == "arpeggio") {
		D.Attack = 0; D.Peak = 13; D.Decay = 10; D.Sustain = 9; D.Release = 4; D.Duty = 1;
		D.Arpeggio = List({ 0, 4, 7 });
	}
	// The calm leads of the quiet references, none holding a high level (their times as at 75 BPM: FitCalmTempo)
	else if (K == "musicbox") {		// struck, then fading to silence in 0.9 of a beat, like a music box (track 49's lead)
		D.Attack = 0; D.Peak = 15; D.Decay = 43; D.Sustain = 0; D.Release = 6; D.Duty = 2;
	}
	else if (K == "breath") {		// breathes in from 4 over 12 frames, sinks to 60% and holds it, a faint vibrato after half a
									// beat, a slow fade after a release (track 50's lead)
		D.Attack = 12; D.AttackFrom = 4; D.Peak = 15; D.Decay = 10; D.Sustain = 9; D.Release = 24; D.Duty = 2;
		D.Vibrato = VIBRATO_FAINT; D.VibratoDelay = CALM_BEAT_FRAMES / 2;
	}
	else if (K == "fallingstrike") {	// struck at once, falling to a low hold, a faint vibrato after half a beat, a short fade
									// after a release (Final Fantasy III's Time Remains)
		D.Attack = 0; D.Peak = 15; D.Decay = 14; D.Sustain = 6; D.Release = 6; D.Duty = 1;
		D.Vibrato = VIBRATO_FAINT; D.VibratoDelay = CALM_BEAT_FRAMES / 2;
	}
	else if (K == "swell" || K == "wave" || K == "waves" || K == "wind" || K == "surf") {
		// Rises slowly and washes out after a release: a wave or a gust on the noise channel, a swell on a pulse
		D.Attack = 30; D.Peak = 12; D.Decay = 0; D.Sustain = 12; D.Release = 40; D.Duty = 0;
	}
	// The articulations of the classic NES soundtracks (references action guide)
	else if (K == "bloomlead") {		// blooms in, a duty click, struck again while held, a vibrato that waits
		D.Attack = 2; D.AttackFrom = 10; D.Peak = 15; D.Decay = 6; D.Sustain = 12; D.Release = 6; D.Duty = 1; D.Click = true;
		D.Restrike = 12; D.Vibrato = 1; D.VibratoDelay = 24;
	}
	else if (K == "steppedlead") {	// holds, then steps down; the lead of fast action tracks
		D.Attack = 0; D.Peak = 15; D.Decay = 36; D.DecayStyle = DECAY_STEPS; D.Sustain = 9; D.Release = 7; D.Duty = 2;
	}
	else if (K == "clickpluck") {	// a short, bright note that speaks with a click and holds low
		D.Attack = 0; D.Peak = 15; D.Decay = 4; D.Sustain = 3; D.Release = 2; D.Duty = 1; D.Click = true;
	}
	else if (K == "echopluck") {		// dies away, then a soft echo of itself
		D.Attack = 0; D.Peak = 14; D.Decay = 12; D.Sustain = 0; D.Duty = 1; D.Echo = true;
	}
	else if (K == "thumpbass") {		// a bass that thumps in from a fourth above
		D.Attack = 0; D.Peak = 15; D.Decay = 0; D.Sustain = 15; D.Release = 2; D.Duty = 2; D.AttackPitch = PITCH_THUMP;
	}
	else {
		static const struct { const char *Word; int Kind; } DRUMS[] = {
			{ "kick", DRUM_KICK }, { "bassdrum", DRUM_KICK }, { "snare", DRUM_SNARE }, { "clap", DRUM_SNARE }, { "hihat", DRUM_HIHAT },
			{ "hat", DRUM_HIHAT }, { "closedhihat", DRUM_HIHAT }, { "openhihat", DRUM_OPEN_HIHAT }, { "openhat", DRUM_OPEN_HIHAT },
			{ "crash", DRUM_CRASH }, { "cymbal", DRUM_CRASH }, { "tom", DRUM_TOM },
		};
		for (size_t i = 0; i < sizeof(DRUMS) / sizeof(DRUMS[0]); ++i) {
			if (K == DRUMS[i].Word) {
				D.Drum = true;
				D.Kind = DRUMS[i].Kind;
				D.Peak = 15;
				return true;
			}
		}
		return false;
	}
	return true;
}

std::string DesignKinds()
{
	return "lead, pad, pluck, bass, keys, bell, brass, strings, organ, chiptune (a chord arpeggio), swell (rises and washes out: "
		"waves and wind on the noise channel); the calm leads: music box (struck, gone within a beat), breath (breathes in, "
		"sinks to 60% and holds), falling strike (struck, falls to a low hold); the classic articulations: bloom lead, stepped "
		"lead, click pluck, echo pluck, thump bass; drums: kick, snare, hihat, open hihat, crash, tom";
}

bool IsCalmKind(const std::string &Kind)
{
	const std::string K = Simplified(Kind);
	return K == "musicbox" || K == "breath" || K == "fallingstrike";
}

void FitCalmTempo(const std::string &Kind, double FramesPerBeat, SoundDesign &D)
{
	if (!IsCalmKind(Kind) || FramesPerBeat <= 0.0)
		return;
	if (Simplified(Kind) == "musicbox")
		D.Decay = Clamp(static_cast<int>(std::floor(0.9 * FramesPerBeat + 0.5)), 12, 120);
	else
		D.VibratoDelay = Clamp(static_cast<int>(std::floor(0.5 * FramesPerBeat + 0.5)), 12, 60);
}

void FitCalmPeak(const std::string &Kind, SoundDesign &D)
{
	const std::string K = Simplified(Kind);
	const int Peak = Clamp(D.Peak, 1, 15);
	if (K == "breath") {
		D.AttackFrom = std::max(1, (Peak * 4 + 7) / 15);	// from 4 of 15
		D.Sustain = std::max(1, (Peak * 9 + 7) / 15);		// 9 of 15
	}
	else if (K == "fallingstrike")
		D.Sustain = std::max(1, (Peak * 6 + 7) / 15);		// 6 of 15
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

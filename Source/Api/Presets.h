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
// Arpeggio settings as arp_setting_t: semitones from the note, or notes of their own
const int SEQUENCE_ABSOLUTE = 0;
const int SEQUENCE_FIXED = 1;

struct PresetSequence {
	int Type;
	std::vector<int> Items;
	int Loop;		// -1: none
	int Release;	// -1: none. Held there while the note lasts, the rest plays on release (===)
	int Setting;	// arpeggio: SEQUENCE_ABSOLUTE or SEQUENCE_FIXED
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

// A sound described in words (design_instrument), made into a preset
enum duty_motion { DUTY_STILL, DUTY_SWEEP, DUTY_WOBBLE, DUTY_FADE };
enum attack_pitch { PITCH_PLAIN, PITCH_BLIP, PITCH_SCOOP, PITCH_DROP, PITCH_FLICK, PITCH_THUMP };
enum decay_style { DECAY_SMOOTH, DECAY_STEPS };
enum drum_kind { DRUM_KICK, DRUM_SNARE, DRUM_HIHAT, DRUM_OPEN_HIHAT, DRUM_CRASH, DRUM_TOM };

struct SoundDesign {
	std::string Name;
	bool Drum;
	// A melodic sound: its volume envelope in frames (60 a second on NTSC)
	int Peak;					// 1-15
	int Attack;					// frames to reach the peak; 0 at once
	int AttackFrom;				// the level the attack starts at: 0 from silence; a bloom starts near two thirds of the peak
	int Decay;					// frames from the peak to the sustain level
	int DecayStyle;				// decay_style: smooth, or in steps that hold and then drop a few levels at a time
	int Sustain;				// 0-15, held while the note lasts; 0 dies away by itself
	int Release;				// frames to fade after a release (===)
	int Restrike;				// frames between restrikes while a note holds (a dip and a rise back), 0 for none
	bool Echo;					// a soft echo after the sound dies away or is released
	int Duty;					// 0-3: 12.5%, 25%, 50%, 75% (the VRC6's nearest)
	int Motion;					// duty_motion: still, thin to the duty on the attack, to and fro (a chorus), thinner as it fades
	bool Click;					// a duty click: the first frame at another duty, so each note speaks
	int Vibrato;				// 0 none, 1 gentle, 2 medium, 3 strong
	int VibratoDelay;			// frames before it comes in
	int VibratoRate;			// frames a cycle, a multiple of 4
	int AttackPitch;			// attack_pitch: plain, an octave up for two frames, up into the note from below, falling,
								// a quick flick up and back, a thump (from a fourth above, dropping into the note)
	std::vector<int> Arpeggio;	// semitones looped ({0, 4, 7}); empty for none
	int ArpeggioFrames;			// frames each
	// A drum, on the noise channel
	int Kind;					// drum_kind
	int Tone;					// -1 dark, 0, 1 bright
	int Length;					// -1 short, 0, 1 long
	SoundDesign();
};

// The design's sequences: volume, arpeggio (the attack's pitch, a chord, or a
// drum's noise pitches), pitch (vibrato that sums to nothing each cycle, or a
// fall) and duty
Preset DesignPreset(const SoundDesign &Design);
// The design in words: "a 25% pulse that reaches 13 in 2 frames, ..." (Wave
// "sawtooth" or "triangle" for those channels, which have no duty, and the
// triangle no volume)
std::string DescribeDesign(const SoundDesign &Design, int FrameRate, const std::string &Wave = "pulse");
// A starting point by kind: lead, pad, pluck, bass, keys, bell, brass, strings, organ,
// chiptune; kick, snare, hihat, open hihat, crash, tom. False if Kind isn't one.
bool DesignOfKind(const std::string &Kind, SoundDesign &Out);
std::string DesignKinds();

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

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
// 2A03 (also used by MMC5 channels) and for the VRC6, the Sunsoft 5B's own
// sounds ("buzz bass", "noise hat"; the other presets play on it too), and
// the VRC7's sounds: the 15 patches built into the chip ("piano", "brass"),
// which the presets also have an equivalent among.

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
	std::vector<PresetSequence> Sequences5B;	// a 5B sound's (Sunsoft5BSounds), its duty sequence the 5B's noise / mode
												// (S5B_* below); the others play theirs there (Preset5BSequences)
	int Vrc7Patch;				// the VRC7 patch that sounds most like it, 0 for none (drums)

	bool Releases() const;		// notes end with a release (===) rather than a halt (---)
	std::string InstrumentName(bool Sawtooth) const;	// "Soft lead", "Bass (saw)"
};

const std::vector<Preset> &Presets();
const Preset *FindPreset(const std::string &Name);	// by name or a common word for it ("flute", "pad")
std::string PresetList();							// "soft lead, bright lead, ..."

// The Sunsoft 5B. Its noise / mode sequence (in the duty sequence's place) turns
// on, a frame at a time, its square, its noise and its envelope, whose level
// replaces the volume's (a falling saw at the note's pitch, until a song sets
// it with Hxy), and sets the noise's pitch (0-31, bits 0-4) while the noise is on.
const int S5B_ENVELOPE = 32, S5B_SQUARE = 64, S5B_NOISE = 128;
// The 5B's volume (0-15, 3 dB a step) that plays as loud as Volume does on a pulse (in proportion to it)
int Volume5B(int Volume);
// The sequences a preset plays as a 5B instrument: a 5B sound's own, or the 2A03's with each frame's volume as loud
// on the 5B (Volume5B) and no duty (the 5B's square is always 50%)
std::vector<PresetSequence> Preset5BSequences(const Preset &Sound);
// The 5B's own sounds: square lead, singing square, square pad, square pluck, square bell, buzz bass, sync buzz,
// noise hat, noise snare and square kick (Sequences5B only)
const std::vector<Preset> &Sunsoft5BSounds();
const Preset *Find5BSound(const std::string &Name);	// by name or a common word for it ("buzz": the buzz bass, "hat")
std::string Sound5BList();							// "square lead, singing square, ..."
// A sound for a 5B channel by name: one of the 5B's own or a preset (played as Preset5BSequences makes it), each
// by its own name before the words for it ("lead" is the square lead, "flute" the soft lead); not the presets'
// drums, which are the noise channel's (the 5B has its own: "snare" is the noise snare). 0 when none is
const Preset *FindSoundFor5B(const std::string &Name);

// Base with Notes (semitones from the note played) looping as its arpeggio,
// Frames frames each, in place of an arpeggio it had; named Name
Preset WithArpeggio(const Preset &Base, const std::vector<int> &Notes, int Frames, const std::string &Name);

// A sound described in words (design_instrument), made into a preset
enum duty_motion { DUTY_STILL, DUTY_SWEEP, DUTY_WOBBLE, DUTY_FADE };
enum attack_pitch { PITCH_PLAIN, PITCH_BLIP, PITCH_SCOOP, PITCH_DROP, PITCH_FLICK, PITCH_THUMP };
enum decay_style { DECAY_SMOOTH, DECAY_STEPS };
enum drum_kind { DRUM_KICK, DRUM_SNARE, DRUM_HIHAT, DRUM_OPEN_HIHAT, DRUM_CRASH, DRUM_TOM };
// SoundDesign::Vibrato's faintest: the calm references' (Final Fantasy III's Time Remains), the period one step longer
// for half of each cycle, about 3 cents either way at A-3 (the gentle one swings 3 steps, about 20 cents). Going
// only flatter, it takes no note of the 2A03's pulses across a multiple of 256, so it clicks on none.
const int VIBRATO_FAINT = 4;

struct SoundDesign {
	std::string Name;
	bool Drum;
	// A melodic sound: its volume envelope in frames (60 a second on NTSC)
	int Peak;					// 1-15
	int Attack;					// frames to reach the peak; 0 at once
	int AttackFrom;				// the level the attack starts at: 0 from silence; a bloom starts near two thirds of the peak
	int Decay;					// frames from the peak to the sustain level (or to silence): it gets there on that frame
	int DecayStyle;				// decay_style: smooth, or in steps that hold and then drop a few levels at a time
	int Sustain;				// 0-15, held while the note lasts; 0 dies away by itself
	int Release;				// frames to fade after a release (===)
	int Restrike;				// frames between restrikes while a note holds (a dip and a rise back; a sound that dies away
								// is struck again from its peak), 0 for none
	bool Echo;					// a soft echo after the sound dies away or is released
	int Duty;					// 0-3: 12.5%, 25%, 50%, 75% (the VRC6's nearest)
	int Vrc6Duty;				// the VRC6's own duty, 0-7 ((n + 1) / 16: 6.25% to 50%), -1 for the nearest to Duty
	int Motion;					// duty_motion: still, thin to the duty on the attack, to and fro (a chorus), thinner as it fades
	bool Click;					// a duty click: the first frame at another duty, so each note speaks
	int Vibrato;				// 0 none, 1 gentle, 2 medium, 3 strong, VIBRATO_FAINT
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
// chiptune; the calm leads music box, breath and falling strike (CALM_BEAT_FRAMES: as at 75 BPM,
// which design_instrument fits to the song's tempo); kick, snare, hihat, open hihat, crash, tom.
// False if Kind isn't one.
bool DesignOfKind(const std::string &Kind, SoundDesign &Out);
std::string DesignKinds();
// The calm kinds (music box, breath, falling strike): their times go by the beat, and their levels by their peak
bool IsCalmKind(const std::string &Kind);
const int CALM_BEAT_FRAMES = 48;	// a beat at 75 BPM, the beat the calm kinds' times are given in
// A calm kind's times at a tempo of FramesPerBeat: the music box dies away in 0.9 of a beat (43 frames at 75 BPM), the
// vibratos come in after half a beat
void FitCalmTempo(const std::string &Kind, double FramesPerBeat, SoundDesign &D);
// A calm kind's levels for another peak (D.Peak): where its swell starts and what it holds, in proportion
void FitCalmPeak(const std::string &Kind, SoundDesign &D);

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

// A VRC7 sound of this connection's own design, for the chip's custom patch
// (patch 0): its 8 registers (the modulator's and carrier's multipliers and
// envelope types, the modulation level, feedback, attack, decay, sustain and
// release). The chip holds one custom patch at a time, so channels playing
// two different ones at once both sound like the one struck last.
struct Vrc7Custom {
	std::string Name;			// "electric piano"
	std::string Description;
	int Regs[8];

	std::string InstrumentName() const;	// "Electric piano"
};

const std::vector<Vrc7Custom> &Vrc7Customs();
const Vrc7Custom *FindVrc7Custom(const std::string &Name);	// by name or a common word for it ("rhodes", "epiano")
std::string Vrc7CustomList();						// "electric piano, soft electric piano, ..."

}	// namespace ftapi

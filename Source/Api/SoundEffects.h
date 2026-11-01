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
// halts, so the export for games saves it as a sound that plays once, or
// loops, for ambience (wind, rain). The kinds suit a cozy platformer: jumps,
// coins, menus, chat pings, and the sounds of nature around them. Sounds
// described in words are built from layers (SfxLayerDesign). Plain C++14.

#include <string>
#include <vector>
#include "../Transcribe/SoundEffect.h"

namespace ftapi {

struct SongInfo;

// How a kind is made (make_sound_effect's surface, count and variant): what its
// steps fall on, how many birds call or bubbles rise, and the draw of its
// random parts. The kinds that take none ignore them.
struct SoundKindOptions {
	std::string Surface;	// footsteps: dirt (the kind's own), wood, grass, stone, snow, sand, water, metal
	int Count;				// birds in a scene, steps, bubbles, drips; 0 for the kind's own
	int Seed;				// another draw of the random parts (a variant's)
	SoundKindOptions() : Count(0), Seed(1) {}
};

// A sound's style (make_sound_effect's style): game, as the kinds are made;
// realistic (grounded): pitch sweeps of more than half an octave narrowed by
// half and attacks softened, for sounds of nature that shouldn't sound like a
// cartoon; cartoon: sweeps widened half again, bouncier
enum sfx_style { SFX_STYLE_GAME, SFX_STYLE_REALISTIC, SFX_STYLE_CARTOON };
int FindSoundStyle(const std::string &Word);	// sfx_style, or -1 when it's no style's word
void StyleSoundEffect(transcribe::SoundEffect &Effect, int Style);

struct SoundEffectKind {
	std::string Name;			// "jump"
	std::string Use;			// what it's for, in a few words
	std::vector<std::string> Words;	// other names it's found by ("pew", "boing")
	transcribe::SoundEffect (*Make)();
	bool Loops;					// ambience a game plays over and over (wind, rain): its track loops, and its end
								// goes on into its start
	transcribe::SoundEffect (*MakeLong)(double Seconds, const SoundKindOptions &Options);	// a loop whose random parts
								// (gusts, drops, crackle) are drawn for as long as asked (LoopOfLength); 0 for one that plays its
								// own loop over and over
	transcribe::SoundEffect (*MakeWith)(const SoundKindOptions &Options);	// a kind that takes options; 0 for Make alone
	std::vector<std::string> Surfaces;	// the surfaces it takes (footsteps), the first its own
	std::string Counts;			// what its count counts ("birds"), empty when it takes none
};

const std::vector<SoundEffectKind> &SoundEffectKinds();
const SoundEffectKind *FindSoundEffect(const std::string &Name);	// by name or another word for it
// A kind named in a few words, as people ask for one: "footsteps on grass", "realistic wind", "a squeaking chain with a
// lantern", "jumping out of water". The longest run of the words that names a kind, then a surface and a style among the
// others (Surface, Style; -1 when none is said). 0 when no run names one
const SoundEffectKind *FindSoundEffectIn(const std::string &Text, std::string &Surface, int &Style);
std::string SoundEffectNames();									// "jump, land, coin, ..."
// The kind made with its options (MakeWith, else Make)
transcribe::SoundEffect MakeSoundKind(const SoundEffectKind &Kind, const SoundKindOptions &Options);
// A surface word as its kind takes it ("wooden" is wood), or "" when it's none of Kind's
std::string FindSurface(const SoundEffectKind &Kind, const std::string &Word);

struct SoundEffectChanges {
	int Variant;		// 1: as designed; 2 and up: other takes on it (VariantText says how)
	int Semitones;		// higher (positive) or lower
	int LengthPercent;	// 100: as designed
	int VolumePercent;	// 100: as designed

	SoundEffectChanges() : Variant(1), Semitones(0), LengthPercent(100), VolumePercent(100) {}
};

// A sound with the changes made: the pitch moved, stretched in time, louder
// or softer. A variant moves the pitch and the length its own way, the same
// each time, and every other one changes a pulse's duty. A layer longer than
// Most ticks is cut there (an instrument's; a loop's layers play as pieces,
// SoundPieces, so a loop passes its own most).
transcribe::SoundEffect ChangeSoundEffect(const transcribe::SoundEffect &Effect, const SoundEffectChanges &Changes,
	int Most = transcribe::SFX_MAX_TICKS);
// Changes.LengthPercent made smaller when the changed sound would last more
// than Most ticks, so it fits whole; true if it had to be
bool FitSoundEffect(const transcribe::SoundEffect &Effect, int Most, SoundEffectChanges &Changes);
// "3 semitones higher, 15% shorter, another pulse duty"; "" for 1. Pulse: the sound has a pulse (a noise-only sound's
// variants keep their duty)
std::string VariantText(int Variant, bool Pulse = true);
bool HasPulse(const transcribe::SoundEffect &Effect);

// A sound that plays once rings out to silence: a layer that stops above volume 1 dies away first, halving a
// tick (its length counts the fade; the triangle, which has no volume, stops where it ends, as the 2A03's
// does), and its track halts SFX_SILENT_TICKS rows after the last tick that sounds
// (SoundTrackRows: the track's rows, the halt on the last), so the sound is never cut while it sounds, and a
// game's file of it ends quiet: the step of its last tick fades through the mixer's low cut in a few ms, and
// its last 50 ms are silent. A sound that loops (Loop) runs on into its start instead: its rows are its
// ticks, with the jump back on the last.
// Most: the longest a layer may grow to as it rings out (a one-shot's layers play as pieces past an instrument's).
const int SFX_SILENT_TICKS = 5;
void RingOut(transcribe::SoundEffect &Effect, int Most = transcribe::SFX_MAX_TICKS);
int SoundTrackRows(const transcribe::SoundEffect &Effect, bool Loop);

// A sound effect's track and instruments share its name: "SFX Jump", with
// "SFX Jump (pulse 2)" and so on for its other layers. FamiTracker reuses an
// instrument when one has the name already, so a name is free when no track
// and no instrument has it (or one of its layers' names), whatever the case.
// FreeSoundName: Base, or Base with a number ("SFX Jump 2") when it's taken.
// SoundNameProblem: why Name can't be used, or "" when it can.
std::string FreeSoundName(const SongInfo &Song, const std::string &Base);
std::string SoundNameProblem(const SongInfo &Song, const std::string &Name);

// A sound described in words (make_sound_effect's layers): each layer a part
// on one of the 2A03's channels, with when it sounds, its pitch and how that
// moves, its loudness and how that moves, and the textures of natural sounds:
// gusts (wind), a flutter (wings, motors), crackle (fire, rain), drops.

enum sfx_envelope {
	SFX_STEADY,			// holds
	SFX_HIT,			// loud at once, dying away quickly
	SFX_FADE,			// loud at once, fading evenly
	SFX_SWELL,			// growing to its loudest
	SFX_SWELL_FADE,		// growing, then fading
	SFX_DROPS,			// silent but for short random blips (rain on a roof, dripping)
	SFX_HELD			// holds, without the short fade a steady sound ends with (layers that run on into each other)
};
enum sfx_glide { SFX_GLIDE_EVEN, SFX_GLIDE_FAST, SFX_GLIDE_SLOW };	// fast: most of the way at once; slow: late

struct SfxLayerDesign {
	transcribe::SfxChannel Channel;
	double Start, Seconds;		// when it starts and how long it lasts
	double Pitch, PitchTo;		// note numbers as written (pulse, triangle; C-4 is 48) or noise 0-15 (15 the
								// brightest); PitchTo < 0: it stays
	int Glide;					// sfx_glide: how the pitch gets to PitchTo
	int Volume, VolumeTo;		// 1-15: its loudest at the start, and at its end (-1: the same)
	int Envelope;				// sfx_envelope, each time it sounds
	int Repeats;				// it sounds this many times in its length (chirps, flaps, beeps), 1-64
	double Gap;					// the share of each repeat that's silent, 0-0.9 (-1: a quarter when it repeats)
	double Step;				// semitones (noise: steps) each repeat moves from the one before
	int Gusts;					// slow, uneven swells of its loudness and pitch (wind): how many, 0 for none
	double Flutter;				// its loudness wobbling this many times a second (wings, motors), 0 for none
	double Vibrato, VibratoHz;	// semitones up and down, and how many times a second
	double Crackle;				// 0-1: random pops on top of it (fire, rain); with SFX_DROPS, how many drops
	bool Rough;					// a harsh edge: the pitch jitters every tick
	int Duty;					// pulse 0-3 (12.5%, 25%, 50%, 75%); noise 1 for its metallic mode
	bool Echo;					// a pulse's softer copy a little later, on another pulse
	int Voice;					// a pulse layer's own pulse: 0 Pulse 1, 1 Pulse 2; -1 the first free one
	double Pan;					// -1 left to 1 right; 0 the middle (as FamiTracker starts every channel)

	SfxLayerDesign();
};

// The sound the layers make, at FrameRate ticks a second, lasting Seconds
// (0: until its last layer ends). Seed draws the random parts (gusts,
// crackle, drops): another seed, another take. Layers on the same kind of
// channel share one when they don't sound at once, at the same pan (a pulse
// layer with a Voice only that pulse's; its echo goes to the other).
// Crackle's pops follow the layer's loudness (a fading wash pops softer) and
// stay at noise pitches a speaker plays (11 and under: the brighter ones carry
// most of their power above 20 kHz, which the export filters out). Loop: the sound repeats,
// so what reaches its end goes on into its start, as gusts and wobbles do.
// False with the reason when the layers don't fit the 2A03 (it has Pulses
// pulse channels here, one triangle and one noise) or run too long: longer
// than Most ticks (0: MaxSoundTicks(Loop); a loop in a song, MaxLoopTicks).
bool DesignSoundEffect(const std::vector<SfxLayerDesign> &Layers, double Seconds, int FrameRate, int Seed, bool Loop, int Pulses,
	transcribe::SoundEffect &Out, std::string &Problem, int Most = 0);

// A layer in words, as the tool reports it: "noise from 0.2 s for 1 s:
// 6 hits, pitch 6 to 4, volume 9 fading to 2, a flutter at 8 Hz"
std::string DescribeLayerDesign(const SfxLayerDesign &Layer);

// The longest a sound effect can be: a layer's ticks (FamiTracker's longest
// sequence), one fewer for one that loops (its end mustn't fall silent)
int MaxSoundTicks(bool Loop);

// Loops as long as asked: ambience (rain, wind, a crowd) wants 30 seconds or
// more, where an instrument's sequences hold 4.2 s. A layer longer than that
// plays as pieces (SoundPieces), an instrument each, with a note where each
// starts; FamiTracker's pulse writes its period's high byte only when it
// changes and the noise its registers every tick, so the sound runs on across
// the pieces. The longest loop a song holds: its frames' rows, a row a tick
// (MaxFrames frames of PatternLength rows); its instruments limit it too
// (make_sound_effect says so).
int MaxLoopTicks(int MaxFrames, int PatternLength);
// The longest a sound that plays once can be: SFX_ONE_SHOT_TICKS (12 s; its layers play as pieces past an instrument's
// 4.2 s, as a loop's do), or what the song's frames hold, with its silence and halt after it
const int SFX_ONE_SHOT_TICKS = 720;
int MaxOneShotTicks(int MaxFrames, int PatternLength);
// A layer as the pieces it plays in, each at most MaxSoundTicks(true) ticks
// (its instrument's volume holds it and the 0 it ends with, which the next
// piece's note replaces): the layer itself when it fits, else equal pieces,
// or whole turns of a loop Period ticks long played over and over, as many
// as divide its length, so its pieces are alike and share an instrument. A
// piece silent throughout is left out: the one before it ends in silence.
std::vector<transcribe::SfxLayer> SoundPieces(const transcribe::SfxLayer &Layer, int Period = 0);
// A kind that loops, Seconds long (at 60 ticks a second, as the kinds are
// made) with the changes made (ChangeSoundEffect; its length the one asked,
// a variant's own stretch counted in): one with random parts (MakeLong:
// wind's gusts, rain's drops, a fire's crackle) drawn for the whole length,
// the others (an engine, an alarm, a heartbeat) their own loop played over and
// over, as many whole times as come nearest. At most Most ticks. Period: that
// loop's ticks, 0 for the drawn ones.
transcribe::SoundEffect LoopOfLength(const SoundEffectKind &Kind, double Seconds, const SoundEffectChanges &Changes, int Most,
	int &Period, const SoundKindOptions &Options = SoundKindOptions());

}	// namespace ftapi

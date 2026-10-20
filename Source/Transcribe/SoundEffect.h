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

// Sound effects (a jump, a coin, a "pew" into the microphone) as the NES
// plays them: layers on its channels, each a value per tick, the 60 (PAL: 50)
// steps a second FamiTracker's engine runs at. In FamiTracker a layer becomes
// an instrument whose sequences step through those values, played by one
// note, so the sound stays editable in the instrument editor.
//
// Plain C++14 like the rest of the engine, tested on Linux.

#include <string>
#include <vector>
#include "Transcribe.h"

namespace transcribe {

enum class SfxChannel { Pulse, Triangle, Noise };

struct SfxLayer {
	SfxChannel channel = SfxChannel::Pulse;
	int start = 0;					// the tick it begins at
	// A value per tick from start on. The volume sets the length; pitch and
	// duty keep their last value when they're shorter.
	std::vector<int> volume;		// 0-15; the triangle sounds at any volume above 0
	std::vector<double> pitch;		// FamiTracker's note numbers as written (C-0 = 0, A-3 = 45 = 440 Hz on a
									// pulse; the triangle sounds an octave lower), with fractions between
									// notes. Noise: 0-15, 15 the brightest.
	std::vector<int> duty;			// pulse: 0-3 (12.5%, 25%, 50%, 75%); noise: 1 for its metallic mode

	int length() const { return static_cast<int>(volume.size()); }
	double pitchAt(int tick) const;	// the last one past the end
	int dutyAt(int tick) const;
};

struct SoundEffect {
	std::vector<SfxLayer> layers;

	int length() const;				// ticks until the last layer ends
};

// FamiTracker's longest sequence, so a layer's longest run
const int SFX_MAX_TICKS = 252;

// The instrument that plays a layer: FamiTracker's sequences, which step
// through the layer's values a tick at a time, and the note that plays it
struct SfxInstrument {
	int note = 0;					// FamiTracker note number (noise: 0-15)
	std::vector<int> volume;		// ends with 0
	std::vector<int> arpeggio;		// pulse, triangle: semitones from note (absolute); noise: its pitch (fixed)
	bool fixedArpeggio = false;
	std::vector<int> pitch;			// period offsets for the steps between notes, added after the arpeggio
	std::vector<int> duty;			// pulse duty or noise mode; one item when it doesn't change
};

// pal: the PAL machine's periods (the 2A07's clock)
SfxInstrument ToInstrument(const SfxLayer &layer, bool pal);

// The 2A03 playing a sound effect through its instruments, tick by tick as
// FamiTracker runs the sequences, for tests and checks. Mono, roughly the
// NES's mix.
Audio RenderEffect(const SoundEffect &effect, int sampleRate, bool pal);

// ---------------------------------------------------------------------------
// From a voice: "pew", "boing", "kssh" into the microphone
// ---------------------------------------------------------------------------

struct VoiceEffectOptions {
	bool fitOctave = true;		// move the pitch by whole octaves so its middle lands near targetNote...
	double targetNote = 52.0;	// (E-4, where NES sound effects often sit)
	int octaves = 0;			// ...or by this many octaves
	bool noise = true;			// the noisy parts (breath, "k", "sh") on the noise channel
	bool pal = false;			// PAL's 50 ticks a second
};

struct VoiceEffectInfo {
	int octaves = 0;			// how far the pitch was moved
	double seconds = 0.0;		// how long the sound is
	bool cut = false;			// it was longer than a layer can be (SFX_MAX_TICKS), so it ends early
	bool pitched = false;		// it has a pulse layer: the voice's pitch
	bool noisy = false;			// it has a noise layer
};

// A sound effect from a recording. Each tick, the recording's loudness sets
// the volume; while the voice has a pitch, a pulse follows it (sliding
// between notes, moved by octaves), and the rest of its loudness, the
// unpitched part, plays on the noise channel, brighter for hissy sounds
// ("sss") than for dull ones (a breath), as its zero crossings tell. Silence
// before and after is left out. False with the reason when there's no sound.
bool VoiceToEffect(const Audio &audio, const VoiceEffectOptions &options, SoundEffect &effect, VoiceEffectInfo *info,
	std::string *problem);

}	// namespace transcribe

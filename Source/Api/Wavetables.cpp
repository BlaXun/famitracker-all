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

#include "Wavetables.h"
#include <algorithm>
#include <cctype>
#include <cmath>

namespace ftapi {

namespace {

const double PI = 3.14159265358979323846;

// A wave from harmonics: Amplitudes[k] for harmonic k + 1, each a sine
// shifted by Phases[k] turns, stretched to fill 0-15
std::vector<int> HarmonicWave(const std::vector<double> &Amplitudes, const std::vector<double> &Phases = std::vector<double>())
{
	double Values[N163_WAVE_SIZE];
	double Low = 1e9, High = -1e9;
	for (int i = 0; i < N163_WAVE_SIZE; ++i) {
		const double T = double(i) / N163_WAVE_SIZE;
		double Sum = 0.0;
		for (size_t k = 0; k < Amplitudes.size(); ++k)
			Sum += Amplitudes[k] * std::sin(2.0 * PI * (double(k + 1) * T + (k < Phases.size() ? Phases[k] : 0.0)));
		Values[i] = Sum;
		Low = std::min(Low, Sum);
		High = std::max(High, Sum);
	}
	std::vector<int> Wave;
	for (int i = 0; i < N163_WAVE_SIZE; ++i) {
		const double Part = High > Low ? (Values[i] - Low) / (High - Low) : 0.5;
		Wave.push_back(std::max(0, std::min(15, int(std::floor(Part * 15.0 + 0.5)))));
	}
	return Wave;
}

// A pulse: High of the 16 samples up, the rest down
std::vector<int> PulseWave(int High)
{
	std::vector<int> Wave;
	for (int i = 0; i < N163_WAVE_SIZE; ++i)
		Wave.push_back(i < High ? 15 : 0);
	return Wave;
}

// Part of the way (0-1) from one wave to another
std::vector<int> Blend(const std::vector<int> &From, const std::vector<int> &To, double Part)
{
	std::vector<int> Wave;
	for (size_t i = 0; i < From.size() && i < To.size(); ++i)
		Wave.push_back(int(std::floor(From[i] * (1.0 - Part) + To[i] * Part + 0.5)));
	return Wave;
}

// A sawtooth's harmonics (1/k), Count of them, rounder the faster they fall (Roll in harmonics, 0 for none)
std::vector<double> SawHarmonics(int Count, double Roll)
{
	std::vector<double> Amplitudes;
	for (int k = 1; k <= Count; ++k)
		Amplitudes.push_back((1.0 / k) * (Roll > 0.0 ? std::exp(-double(k - 1) / Roll) : 1.0));
	return Amplitudes;
}

// The waves one after the other: each index held for its frames; Loop: the index in Steps it goes back to (-1 holds the last)
PresetSequence WaveSteps(const std::vector<std::pair<int, int> > &Steps, int Loop)
{
	PresetSequence Seq;
	Seq.Type = SEQUENCE_DUTY;
	Seq.Loop = -1;
	Seq.Release = -1;
	Seq.Setting = 0;
	for (size_t s = 0; s < Steps.size(); ++s) {
		if (static_cast<int>(s) == Loop)
			Seq.Loop = static_cast<int>(Seq.Items.size());
		for (int f = 0; f < Steps[s].second; ++f)
			Seq.Items.push_back(Steps[s].first);
	}
	return Seq;
}

// A volume envelope (frames at 60 a second), with a gentle vibrato that waits Delay frames when Vibrato
SoundDesign Envelope(int Attack, int AttackFrom, int Peak, int Decay, int Sustain, int Release, bool Vibrato = false, int Delay = 24)
{
	SoundDesign Design;
	Design.Attack = Attack;
	Design.AttackFrom = AttackFrom;
	Design.Peak = Peak;
	Design.Decay = Decay;
	Design.Sustain = Sustain;
	Design.Release = Release;
	Design.Vibrato = Vibrato ? 1 : 0;
	Design.VibratoDelay = Delay;
	return Design;
}

// A sound: its waves, how they follow each other, and a design's volume, arpeggio and vibrato. (FamiTracker
// moves an N163 note's pitch in steps 16 times its table's, which with its table's larger numbers makes a
// vibrato about as deep as on the 2A03, a little shallower the more channels are in use.)
WaveSound Sound(const char *Name, const char *Words, const char *Description, const std::vector<std::vector<int> > &Waves,
	const PresetSequence &Steps, const SoundDesign &Design)
{
	const Preset Made = DesignPreset(Design);
	WaveSound S;
	S.Name = Name;
	S.Words = Words;
	S.Description = Description;
	S.Waves = Waves;
	for (size_t i = 0; i < Made.Sequences2A03.size(); ++i)
		if (Made.Sequences2A03[i].Type == SEQUENCE_VOLUME || Made.Sequences2A03[i].Type == SEQUENCE_ARPEGGIO ||
			Made.Sequences2A03[i].Type == SEQUENCE_PITCH)
			S.Sequences.push_back(Made.Sequences2A03[i]);
	if (Waves.size() > 1)
		S.Sequences.push_back(Steps);
	return S;
}

std::vector<std::pair<int, int> > Steps(std::initializer_list<std::pair<int, int> > List)
{
	return std::vector<std::pair<int, int> >(List);
}

// A design with the faint vibrato (VIBRATO_FAINT) after Delay frames
SoundDesign Faint(SoundDesign Design, int Delay)
{
	Design.Vibrato = VIBRATO_FAINT;
	Design.VibratoDelay = Delay;
	return Design;
}

// A sound's volume sequence replaced by one of its own (its loop and release point as a sequence keeps them, -1 for none)
WaveSound WithVolume(WaveSound Sound, const std::vector<int> &Items, int Loop, int Release)
{
	for (size_t i = 0; i < Sound.Sequences.size(); ++i) {
		if (Sound.Sequences[i].Type != SEQUENCE_VOLUME)
			continue;
		Sound.Sequences[i].Items = Items;
		Sound.Sequences[i].Loop = Loop;
		Sound.Sequences[i].Release = Release;
	}
	return Sound;
}

// Waves whose upper harmonics drift in phase, a quarter turn each: a timbre that shimmers like several players
std::vector<std::vector<int> > Shimmer(const std::vector<double> &Amplitudes, size_t From)
{
	std::vector<std::vector<int> > Waves;
	for (int j = 0; j < 4; ++j) {
		std::vector<double> Phases(Amplitudes.size(), 0.0);
		for (size_t k = From; k < Amplitudes.size(); ++k)
			Phases[k] = 0.25 * j * double(k - From + 1);
		Waves.push_back(HarmonicWave(Amplitudes, Phases));
	}
	return Waves;
}

std::vector<WaveSound> MakeSounds()
{
	std::vector<WaveSound> Out;
	const PresetSequence None = WaveSteps(Steps({ { 0, 1 } }), -1);

	Out.push_back(Sound("soft flute", "flute, soft lead, sine, whistle, ocarina",
		"a round, breathy flute: almost a sine, swelling in gently", std::vector<std::vector<int> >{ HarmonicWave({ 1.0, 0.12 }) },
		None, Envelope(3, 0, 12, 0, 12, 8, true, 30)));
	Out.push_back(Sound("drawbar organ", "organ, church organ, tonewheel",
		"an organ of four drawbars whose upper ones turn slowly, like a rotating speaker",
		Shimmer({ 1.0, 0.7, 0.5, 0.45, 0.0, 0.25 }, 1), WaveSteps(Steps({ { 0, 4 }, { 1, 4 }, { 2, 4 }, { 3, 4 } }), 0), Envelope(0, 0, 13, 0, 13, 3)));
	Out.push_back(Sound("string ensemble", "strings, violin, pad, ensemble, orchestra",
		"a section of strings swelling in, its upper harmonics shimmering like several players",
		Shimmer(SawHarmonics(8, 6.0), 2), WaveSteps(Steps({ { 0, 6 }, { 1, 6 }, { 2, 6 }, { 3, 6 } }), 0), Envelope(14, 0, 12, 0, 12, 14, true, 28)));
	Out.push_back(Sound("choir", "voice, vox, aah, ooh, singers",
		"voices singing \"aah\", the vowel moving a little as it holds",
		std::vector<std::vector<int> >{ HarmonicWave({ 0.9, 0.5, 1.0, 0.6, 0.25, 0.15, 0.1 }),
			HarmonicWave({ 0.9, 0.6, 0.8, 0.75, 0.3, 0.12, 0.08 }, { 0.0, 0.1, 0.0, 0.2 }) },
		WaveSteps(Steps({ { 0, 10 }, { 1, 10 } }), 0), Envelope(12, 0, 11, 0, 11, 16, true, 24)));
	Out.push_back(Sound("brass section", "brass, horn, trumpet, fanfare",
		"brass that opens up from mellow to bright as each note starts",
		std::vector<std::vector<int> >{ HarmonicWave({ 1.0, 0.3 }), HarmonicWave({ 1.0, 0.5, 0.3 }),
			HarmonicWave(SawHarmonics(5, 0.0)), HarmonicWave(SawHarmonics(8, 0.0)) },
		WaveSteps(Steps({ { 0, 2 }, { 1, 2 }, { 2, 2 }, { 3, 1 } }), -1), Envelope(3, 6, 14, 6, 12, 6)));
	Out.push_back(Sound("electric piano", "keys, piano, rhodes, ep, e-piano",
		"an electric piano: a bright strike that mellows to a round tone as it fades",
		std::vector<std::vector<int> >{ HarmonicWave({ 1.0, 0.9, 0.5, 0.35, 0.2 }), HarmonicWave({ 1.0, 0.6, 0.3, 0.15 }),
			HarmonicWave({ 1.0, 0.35, 0.12 }), HarmonicWave({ 1.0, 0.15 }) },
		WaveSteps(Steps({ { 0, 3 }, { 1, 5 }, { 2, 8 }, { 3, 1 } }), -1), Envelope(0, 0, 14, 48, 4, 8)));
	Out.push_back(Sound("glass bell", "bell, chime, glass, celesta, music box",
		"a glassy bell: a hollow, metallic ring that mellows as it dies away",
		std::vector<std::vector<int> >{ HarmonicWave({ 1.0, 0.0, 0.5, 0.0, 0.6, 0.0, 0.4 }), HarmonicWave({ 1.0, 0.0, 0.3 }),
			HarmonicWave({ 1.0 }) },
		WaveSteps(Steps({ { 0, 4 }, { 1, 10 }, { 2, 1 } }), -1), Envelope(0, 0, 15, 70, 0, 12)));
	Out.push_back(Sound("harp", "pluck, guitar, pizzicato, koto",
		"a plucked string: bright at the pluck, rounder as it rings",
		std::vector<std::vector<int> >{ HarmonicWave(SawHarmonics(6, 0.0)), HarmonicWave({ 1.0, 0.3, 0.1 }), HarmonicWave({ 1.0 }) },
		WaveSteps(Steps({ { 0, 2 }, { 1, 4 }, { 2, 1 } }), -1), Envelope(0, 0, 15, 30, 0, 6)));
	Out.push_back(Sound("marimba", "mallet, xylophone, vibraphone, wood",
		"a wooden mallet: a knock with a high overtone, then a round, short tone",
		std::vector<std::vector<int> >{ HarmonicWave({ 1.0, 0.0, 0.0, 0.6 }), HarmonicWave({ 1.0 }) },
		WaveSteps(Steps({ { 0, 2 }, { 1, 1 } }), -1), Envelope(0, 0, 15, 18, 0, 4)));
	{
		std::vector<std::vector<int> > Widths;
		for (int High = 8; High >= 3; --High)
			Widths.push_back(PulseWave(High));
		Out.push_back(Sound("pulse sweep lead", "lead, pwm, pulse lead, bright lead, synth lead",
			"a lead whose pulse narrows and widens smoothly as it holds (the 2A03 only steps between four widths)", Widths,
			WaveSteps(Steps({ { 0, 2 }, { 1, 2 }, { 2, 2 }, { 3, 2 }, { 4, 2 }, { 5, 2 }, { 4, 2 }, { 3, 2 }, { 2, 2 }, { 1, 2 } }), 0),
			Envelope(1, 0, 13, 8, 11, 6, true, 24)));
	}
	{
		const std::vector<int> Saw = HarmonicWave(SawHarmonics(8, 0.0)), Square = PulseWave(8);
		Out.push_back(Sound("growl bass", "wah bass, synth bass, acid, funk bass",
			"a bass that growls: its wave turns from sawtooth to square and back, like a wah",
			std::vector<std::vector<int> >{ Saw, Blend(Saw, Square, 1.0 / 3.0), Blend(Saw, Square, 2.0 / 3.0), Square },
			WaveSteps(Steps({ { 0, 2 }, { 1, 2 }, { 2, 2 }, { 3, 2 }, { 2, 2 }, { 1, 2 } }), 0), Envelope(0, 0, 15, 6, 13, 3)));
	}
	Out.push_back(Sound("round bass", "bass, soft bass, deep bass",
		"a warm, round bass, rounder than the triangle and with a volume of its own",
		std::vector<std::vector<int> >{ HarmonicWave({ 1.0, 0.35, 0.1 }) }, None, Envelope(0, 0, 15, 8, 13, 3)));
	// The VRC6 Plus's soft layer (Vrc6Plus.h): the choir and the strings softened into pads that swell in under the VRC6,
	// and a sub bass for an octave under its sawtooth. Their waves are about as loud as the others' (Tools.cpp's N163 level).
	Out.push_back(Sound("choir pad", "soft choir, vocal pad, voices pad, aah pad",
		"a soft choir that swells in and holds under a lead: the choir's \"aah\" with its edge taken off",
		std::vector<std::vector<int> >{ HarmonicWave({ 1.0, 0.45, 0.6, 0.3 }), HarmonicWave({ 1.0, 0.55, 0.5, 0.4, 0.08 }, { 0.0, 0.1, 0.0, 0.2 }) },
		WaveSteps(Steps({ { 0, 12 }, { 1, 12 } }), 0), Envelope(20, 0, 11, 0, 11, 20, true, 30)));
	Out.push_back(Sound("string pad", "warm pad, soft strings, strings pad, slow strings, warm strings",
		"strings swelling in slowly behind the melody, softer and rounder than the string ensemble",
		Shimmer(SawHarmonics(6, 3.0), 2), WaveSteps(Steps({ { 0, 8 }, { 1, 8 }, { 2, 8 }, { 3, 8 } }), 0),
		Envelope(24, 0, 11, 0, 11, 22, true, 34)));
	Out.push_back(Sound("sub bass", "sub, sine bass, low bass, 808",
		"a round sub bass, almost a sine, for an octave under the sawtooth: felt more than heard",
		std::vector<std::vector<int> >{ HarmonicWave({ 1.0, 0.18 }) }, None, Envelope(1, 8, 15, 6, 13, 4)));
	// The soft layer's calm shapes (Vrc6Plus.h, SoftLayerCalmSounds), which its pads don't have: a lead that breathes in and
	// sinks, a pad quick enough for a chord every beat, a pad struck again and again, a music box
	Out.push_back(Sound("breath", "breathy lead, breath lead, calm lead, airy flute, night lead",
		"a breathy lead that breathes in from 4 over 11 frames, sinks to 10 by frame 21 and holds it, with a faint vibrato "
		"after 24 frames: a calm lead, or a soft answer to the VRC6's",
		std::vector<std::vector<int> >{ HarmonicWave({ 1.0, 0.24, 0.1 }), HarmonicWave({ 1.0, 0.18, 0.15 }, { 0.0, 0.12, 0.3 }) },
		WaveSteps(Steps({ { 0, 8 }, { 1, 8 } }), 0), Faint(Envelope(11, 4, 14, 10, 10, 16), 24)));
	Out.push_back(Sound("quick pad", "fast pad, short pad, beat pad, chord a beat, quick chords",
		"a soft pad that's in within 7 frames and holds, for a chord every beat (the other pads take 20-24 frames to swell)",
		std::vector<std::vector<int> >{ HarmonicWave({ 1.0, 0.5, 0.3, 0.15 }), HarmonicWave({ 1.0, 0.45, 0.35, 0.12, 0.05 },
			{ 0.0, 0.08, 0.0, 0.15 }) }, WaveSteps(Steps({ { 0, 10 }, { 1, 10 } }), 0), Envelope(7, 0, 11, 0, 11, 10)));
	{
		// Struck every 12 frames, each strike a little softer down to a level it keeps striking, and after a release a fade,
		// a moment's silence and a soft echo (Fearofdark's slow pads, snow-burn-ftm-01)
		const WaveSound Pulsing = Sound("pulsing pad", "struck pad, restruck pad, restrike pad, echo pad, throb pad, tremolo pad",
			"a pad struck again every 12 frames, each strike softer (13, 11, then 10 again and again), with a soft echo after "
			"a release: slow chords that keep moving",
			std::vector<std::vector<int> >{ HarmonicWave({ 1.0, 0.4, 0.25, 0.1 }), HarmonicWave({ 1.0, 0.3, 0.3, 0.15 }, { 0.0, 0.1, 0.2 }) },
			WaveSteps(Steps({ { 0, 6 }, { 1, 6 } }), 0), Envelope(0, 0, 13, 0, 13, 6));
		const std::vector<int> Volume = { 13, 13, 12, 12, 11, 10, 10, 9, 9, 9, 8, 8,	// the first strike
			11, 11, 10, 10, 9, 9, 8, 8, 8, 7, 7, 7,		// the second, softer
			10, 10, 9, 9, 8, 8, 7, 7, 7, 6, 6, 6,		// then this one again and again while the note holds
			5, 4, 3, 3, 2, 1, 0, 0, 0, 0, 2, 2, 3, 3, 2, 2, 1, 1, 0 };	// after a release: a fade, then the echo
		Out.push_back(WithVolume(Pulsing, Volume, 24, 35));
	}
	Out.push_back(Sound("music box", "celesta, toy piano, lullaby, tinkle, struck lead",
		"a soft music box: a bright tine struck at 15 that mellows to a pure tone and dies away by itself in 40 frames",
		std::vector<std::vector<int> >{ HarmonicWave({ 1.0, 0.0, 0.0, 0.4, 0.0, 0.15 }), HarmonicWave({ 1.0, 0.0, 0.0, 0.18 }),
			HarmonicWave({ 1.0 }) }, WaveSteps(Steps({ { 0, 3 }, { 1, 6 }, { 2, 1 } }), -1), Envelope(0, 0, 15, 40, 0, 6)));
	return Out;
}

std::string Simple(const std::string &Text)
{
	std::string Out;
	for (size_t i = 0; i < Text.size(); ++i)
		if (std::isalnum(static_cast<unsigned char>(Text[i])))
			Out += static_cast<char>(std::tolower(static_cast<unsigned char>(Text[i])));
	return Out;
}

}	// namespace

bool WaveSound::Releases() const
{
	for (size_t i = 0; i < Sequences.size(); ++i)
		if (Sequences[i].Type == SEQUENCE_VOLUME && Sequences[i].Release >= 0)
			return true;
	return false;
}

std::string WaveSound::InstrumentName() const
{
	std::string Out = Name;
	if (!Out.empty())
		Out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(Out[0])));
	return Out;
}

const std::vector<WaveSound> &WaveSounds()
{
	static const std::vector<WaveSound> SOUNDS = MakeSounds();
	return SOUNDS;
}

const WaveSound *FindWaveSound(const std::string &Name)
{
	const std::string Wanted = Simple(Name);
	if (Wanted.empty())
		return 0;
	const std::vector<WaveSound> &All = WaveSounds();
	for (size_t i = 0; i < All.size(); ++i)
		if (Simple(All[i].Name) == Wanted)
			return &All[i];
	for (size_t i = 0; i < All.size(); ++i) {
		const std::string &Words = All[i].Words;
		size_t Start = 0;
		while (Start <= Words.size()) {
			const size_t End = std::min(Words.find(',', Start), Words.size());
			if (Simple(Words.substr(Start, End - Start)) == Wanted)
				return &All[i];
			Start = End + 1;
		}
	}
	return 0;
}

std::string WaveSoundList()
{
	std::string Out;
	const std::vector<WaveSound> &All = WaveSounds();
	for (size_t i = 0; i < All.size(); ++i)
		Out += (i == 0 ? "" : ", ") + All[i].Name;
	return Out;
}

const WaveSound *WaveSoundForKind(const std::string &Kind)
{
	static const char *const KINDS[][2] = {
		{ "breath", "breath" }, { "musicbox", "music box" }, { "fallingstrike", "electric piano" }, { "pad", "string pad" },
		{ "strings", "string pad" }, { "violin", "string pad" }, { "swell", "choir pad" }, { "wind", "choir pad" },
		{ "pluck", "harp" }, { "harp", "harp" }, { "guitar", "harp" }, { "clickpluck", "harp" }, { "echopluck", "harp" },
		{ "bass", "sub bass" }, { "thumpbass", "sub bass" }, { "keys", "electric piano" }, { "piano", "electric piano" },
		{ "bell", "glass bell" }, { "chime", "glass bell" }, { "brass", "brass section" }, { "horn", "brass section" },
		{ "organ", "drawbar organ" }, { "chiptune", "pulse sweep lead" }, { "arp", "pulse sweep lead" },
		{ "arpeggio", "pulse sweep lead" },
	};
	const std::string Wanted = Simple(Kind);
	for (size_t k = 0; k < sizeof(KINDS) / sizeof(KINDS[0]); ++k)
		if (Wanted == KINDS[k][0] && FindWaveSound(KINDS[k][1]) != 0)
			return FindWaveSound(KINDS[k][1]);
	return FindWaveSound("soft flute");	// a lead
}

WaveSound DesignedWaveSound(const WaveSound &Base, const SoundDesign &Design, const std::string &Name)
{
	WaveSound Out;
	Out.Name = Name;
	Out.Words = Base.Words;
	Out.Description = "designed with design_instrument from the " + Base.Name + "'s waves";
	Out.Waves = Base.Waves;
	// The design's volume, arpeggio and pitch (its duty would pick waves), then Base's waves in their order
	const Preset Made = DesignPreset(Design);
	for (size_t i = 0; i < Made.Sequences2A03.size(); ++i)
		if (Made.Sequences2A03[i].Type == SEQUENCE_VOLUME || Made.Sequences2A03[i].Type == SEQUENCE_ARPEGGIO ||
			Made.Sequences2A03[i].Type == SEQUENCE_PITCH)
			Out.Sequences.push_back(Made.Sequences2A03[i]);
	for (size_t i = 0; i < Base.Sequences.size(); ++i)
		if (Base.Sequences[i].Type == SEQUENCE_DUTY)
			Out.Sequences.push_back(Base.Sequences[i]);
	return Out;
}

int N163WaveMemory(int Channels)
{
	return 256 - 16 * std::max(1, std::min(8, Channels));
}

int FindWavePlace(int Size, int Memory, const std::vector<std::pair<int, int> > &Used)
{
	if (Size < 1)
		return -1;
	for (int Place = 0; Place + Size <= Memory; Place += Size) {
		bool Free = true;
		for (size_t i = 0; i < Used.size() && Free; ++i)
			if (Place < Used[i].first + Used[i].second && Used[i].first < Place + Size)
				Free = false;
		if (Free)
			return Place;
	}
	return -1;
}

}	// namespace ftapi

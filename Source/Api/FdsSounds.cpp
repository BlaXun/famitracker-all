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

#include "FdsSounds.h"
#include <algorithm>
#include <cctype>
#include <cmath>

namespace ftapi {

namespace {

const double PI = 3.14159265358979323846;

// A wave from harmonics: Amplitudes[k] for harmonic k + 1, each a sine shifted
// by Phases[k] turns, centred on 32 (the chip plays a sample as itself less
// 32, so the wave carries nothing a volume change would thump with) and as
// large as fits 0-63
std::vector<int> HarmonicWave(const std::vector<double> &Amplitudes, const std::vector<double> &Phases = std::vector<double>())
{
	double Values[FDS_WAVE_SIZE];
	double Mean = 0.0;
	for (int i = 0; i < FDS_WAVE_SIZE; ++i) {
		const double T = double(i) / FDS_WAVE_SIZE;
		double Sum = 0.0;
		for (size_t k = 0; k < Amplitudes.size(); ++k)
			Sum += Amplitudes[k] * std::sin(2.0 * PI * (double(k + 1) * T + (k < Phases.size() ? Phases[k] : 0.0)));
		Values[i] = Sum;
		Mean += Sum / FDS_WAVE_SIZE;
	}
	double High = 1e-9, Low = 1e-9;
	for (int i = 0; i < FDS_WAVE_SIZE; ++i) {
		High = std::max(High, Values[i] - Mean);
		Low = std::max(Low, Mean - Values[i]);
	}
	const double Scale = std::min(31.0 / High, 32.0 / Low);
	std::vector<int> Wave;
	for (int i = 0; i < FDS_WAVE_SIZE; ++i)
		Wave.push_back(std::max(0, std::min(63, int(std::floor(32.0 + (Values[i] - Mean) * Scale + 0.5)))));
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

PresetSequence Sequence(int Type, const std::vector<int> &Items, int Loop = -1, int Release = -1)
{
	PresetSequence Seq;
	Seq.Type = Type;
	Seq.Items = Items;
	Seq.Loop = Loop;
	Seq.Release = Release;
	Seq.Setting = SEQUENCE_ABSOLUTE;
	return Seq;
}

// A volume envelope, 0-31 a frame (60 a second): Attack frames from From up to
// Peak along a curve that starts and lands gently (a smooth wave starting at
// full volume would click), Decay frames falling to Sustain, held while the
// note lasts (the release point), then Release frames to silence. With
// Sustain 0 the sound dies away by itself over Decay frames, holding its last
// whisper until the note ends.
PresetSequence Volume(int Attack, int From, int Peak, int Decay, int Sustain, int Release)
{
	std::vector<int> Out;
	for (int i = 1; i < Attack; ++i)
		Out.push_back(std::max(1, int(std::floor(From + (Peak - From) * (1.0 - std::cos(PI * i / Attack)) / 2.0 + 0.5))));
	Out.push_back(Peak);
	const int Floor = Sustain > 0 ? Sustain : 1;
	for (int i = 1; i <= Decay; ++i) {
		// Falling faster at first, as a struck or plucked sound does
		const double Left = std::exp(-3.0 * i / std::max(1, Decay));
		Out.push_back(int(std::floor(Floor + (Peak - Floor) * (Left - std::exp(-3.0)) / (1.0 - std::exp(-3.0)) + 0.5)));
	}
	if (Out.back() != Floor)
		Out.push_back(Floor);
	const int ReleasePoint = static_cast<int>(Out.size()) - 1;
	for (int i = 1; i <= Release; ++i)
		Out.push_back(int(std::floor(Floor * std::exp(-3.0 * i / std::max(1, Release)) + 0.5)));
	if (Out.back() != 0)
		Out.push_back(0);
	return Sequence(SEQUENCE_VOLUME, Out, -1, ReleasePoint);
}

// A volume envelope that breathes while the note holds: up to Low over
// Attack frames, then slowly up to Peak and back to Low over Period frames,
// again and again, Release frames to silence after a release. A release
// starts from Low (FamiTracker jumps to the release point, the loop's end),
// so wherever the breath is, it falls a little and fades, never swells.
PresetSequence BreathingVolume(int Attack, int Peak, int Low, int Period, int Release)
{
	std::vector<int> Out;
	for (int i = 1; i <= Attack; ++i)
		Out.push_back(std::max(1, int(std::floor(Low * (1.0 - std::cos(PI * i / Attack)) / 2.0 + 0.5))));
	const int Loop = static_cast<int>(Out.size());
	for (int i = 1; i <= Period; ++i)
		Out.push_back(int(std::floor(Low + (Peak - Low) * (1.0 - std::cos(2.0 * PI * i / Period)) / 2.0 + 0.5)));
	const int ReleasePoint = static_cast<int>(Out.size()) - 1;
	for (int i = 1; i <= Release; ++i)
		Out.push_back(int(std::floor(Low * std::exp(-3.0 * i / std::max(1, Release)) + 0.5)));
	if (Out.back() != 0)
		Out.push_back(0);
	return Sequence(SEQUENCE_VOLUME, Out, Loop, ReleasePoint);
}

// A vibrato as the pitch sequence plays it on the FDS: each frame's value is
// added to the note's frequency register (higher is sharper there), so the
// items are the steps of a sine Depth units deep, Period frames a cycle,
// summing to nothing each cycle, after Delay frames of none. A unit is 1731
// cents over the register: at A-5 (sounding 440 Hz, register 1031) 1.7
// cents, an octave down twice as much.
PresetSequence Vibrato(int Delay, int Depth, int Period)
{
	std::vector<int> Items(static_cast<size_t>(std::max(0, Delay)), 0);
	const int Loop = static_cast<int>(Items.size());
	int Last = 0;
	for (int k = 1; k <= Period; ++k) {
		const int Now = int(std::floor(Depth * std::sin(2.0 * PI * k / Period) + 0.5));
		Items.push_back(Now - Last);
		Last = Now;
	}
	return Sequence(SEQUENCE_PITCH, Items, Loop);
}

// An arpeggio that plays once as the note starts (a strike above the note),
// then the note
PresetSequence Strike(const std::vector<int> &Notes)
{
	std::vector<int> Items(Notes);
	Items.push_back(0);
	return Sequence(SEQUENCE_ARPEGGIO, Items);
}

// A modulation table from its 32 steps written as digits
std::vector<int> Table(const char *Steps)
{
	std::vector<int> Out;
	for (const char *p = Steps; *p != 0 && Out.size() < static_cast<size_t>(FDS_MOD_SIZE); ++p)
		Out.push_back(std::max(0, std::min(7, *p - '0')));
	Out.resize(FDS_MOD_SIZE, 0);
	return Out;
}

FdsSound Sound(const char *Name, const char *Words, const char *Description, const std::vector<int> &Wave,
	const std::vector<PresetSequence> &Sequences, bool Low = false)
{
	FdsSound S;
	S.Name = Name;
	S.Words = Words;
	S.Description = Description;
	S.Wave = Wave;
	S.Modulation.assign(FDS_MOD_SIZE, 0);
	S.Sequences = Sequences;
	S.Low = Low;
	return S;
}

FdsSound Modulated(FdsSound S, const char *Steps, int Speed, int Depth, int Delay)
{
	S.Modulation = Table(Steps);
	S.ModSpeed = Speed;
	S.ModDepth = Depth;
	S.ModDelay = Delay;
	return S;
}

// A wobble of the modulator's smallest bends: about 79 cents up for a third of
// each turn and 55 down for half, at the note between, which stays in tune on
// average (the bend's mean is under a cent from the note)
const char *const WOBBLE = "00330000000000555000000000000004";

std::vector<FdsSound> MakeSounds()
{
	std::vector<FdsSound> Out;
	typedef std::vector<PresetSequence> Seqs;

	Out.push_back(Sound("soft flute", "flute, soft lead, singing lead, recorder, ocarina, pan flute",
		"a round, breathy flute that swells in gently, its vibrato coming in after a moment",
		HarmonicWave({ 1.0, 0.16, 0.05 }),
		Seqs{ Volume(5, 0, 28, 0, 28, 10), Vibrato(24, 8, 11) }));
	Out.push_back(Sound("whistle", "thin lead, sine, sine lead, pure lead, piccolo, fife",
		"a pure, high whistle with a quick, wide vibrato like a person whistling",
		HarmonicWave({ 1.0 }),
		Seqs{ Volume(3, 0, 28, 0, 27, 6), Vibrato(14, 14, 8) }));
	Out.push_back(Modulated(Sound("eerie lead", "ghost, ghostly, haunted, spooky, theremin, hollow lead, eerie",
		"a hollow lead whose pitch starts to wobble slowly and widely as it holds, like a theremin in the dark",
		HarmonicWave({ 1.0, 0.0, 0.33, 0.0, 0.12, 0.0, 0.05 }),
		Seqs{ Volume(4, 0, 27, 0, 26, 14) }), WOBBLE, 12, 1, 30));
	Out.push_back(Sound("glass bell", "bell, chime, glass, celesta, music box, glockenspiel, pluck",
		"a glassy bell: a bright strike above the note that rings into a round, hollow tone and dies away slowly to a soft hum",
		HarmonicWave({ 1.0, 0.0, 0.15, 0.0, 0.0, 0.05 }),
		Seqs{ Volume(2, 0, 30, 90, 4, 10), Strike({ 24, 12 }) }));
	Out.push_back(Sound("warm organ", "organ, church organ, pipe organ, drawbar organ, soft chords, harmonium",
		"a warm organ of five drawbars, steady as a pipe organ with a faint chorus",
		HarmonicWave({ 1.0, 0.55, 0.35, 0.25, 0.0, 0.12, 0.0, 0.08 }),
		Seqs{ Volume(2, 0, 28, 0, 28, 3), Vibrato(0, 2, 7) }));
	Out.push_back(Sound("nasal reed", "reed, oboe, bassoon, clarinet, shawm, bagpipe, bright lead, brass",
		"a nasal double reed like an oboe, its sound in its middle harmonics, with a vibrato that waits",
		HarmonicWave({ 0.45, 0.8, 1.0, 0.75, 0.5, 0.3, 0.2, 0.12, 0.08 }),
		Seqs{ Volume(3, 0, 27, 0, 25, 6), Vibrato(20, 8, 10) }));
	Out.push_back(Sound("swelling strings", "strings, string pad, violin, viola, cello, ensemble, orchestra",
		"bowed strings that swell in slowly and sing with a vibrato as they hold",
		HarmonicWave(SawHarmonics(16, 8.0)),
		Seqs{ Volume(24, 6, 28, 0, 26, 18), Vibrato(18, 8, 10) }));
	Out.push_back(Sound("choir pad", "choir, voices, vox, aah, ooh, singers, pad, warm pad, lush chords",
		"voices singing \"aah\" that rise in softly and hold, with a slow vibrato",
		HarmonicWave({ 0.8, 0.5, 1.0, 0.65, 0.35, 0.18, 0.12, 0.08 }),
		Seqs{ Volume(16, 0, 26, 0, 25, 22), Vibrato(20, 6, 13) }));
	Out.push_back(Sound("round bass", "bass, deep bass, sub bass, soft bass, plucky bass, double bass",
		"a deep, round bass: almost a sine, warmer than the triangle and with a volume of its own",
		HarmonicWave({ 1.0, 0.4, 0.15 }),
		Seqs{ Volume(3, 0, 30, 10, 26, 4) }, true));
	Out.push_back(Modulated(Sound("growl bass", "growl, growling bass, synth bass, buzz bass, dirty bass, thump bass, fuzz bass",
		"a bright bass whose pitch the modulator shakes 35 times a second, so it growls",
		HarmonicWave({ 1.0, 0.6, 0.45, 0.35, 0.28, 0.22, 0.18, 0.15, 0.12, 0.1 }),
		Seqs{ Volume(2, 0, 30, 6, 27, 3) }, true), WOBBLE, 82, 1, 0));
	Out.push_back(Modulated(Sound("underwater wobble", "underwater, water, wobble, warble, bubbles, wah, aquatic",
		"a round tone that wobbles up and down a few times a second, like a sound heard under water",
		HarmonicWave({ 1.0, 0.0, 0.2 }),
		Seqs{ Volume(6, 0, 26, 0, 25, 12) }), WOBBLE, 7, 1, 4));
	Out.push_back(Modulated(Sound("cave drone", "drone, cave, dark pad, rumble, ominous, dungeon, dark",
		"a dark, hollow drone that breathes slowly in and out, a faint rumble in it, for caves and the night",
		HarmonicWave({ 1.0, 0.25, 0.4, 0.1, 0.15 }),
		Seqs{ BreathingVolume(30, 27, 20, 150, 30) }), WOBBLE, 60, 1, 0));
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

bool FdsSound::Releases() const
{
	for (size_t i = 0; i < Sequences.size(); ++i)
		if (Sequences[i].Type == SEQUENCE_VOLUME && Sequences[i].Release >= 0)
			return true;
	return false;
}

std::string FdsSound::InstrumentName() const
{
	std::string Out = Name;
	if (!Out.empty())
		Out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(Out[0])));
	return Out;
}

const std::vector<FdsSound> &FdsSounds()
{
	static const std::vector<FdsSound> SOUNDS = MakeSounds();
	return SOUNDS;
}

const FdsSound *FindFdsSound(const std::string &Name)
{
	const std::string Wanted = Simple(Name);
	if (Wanted.empty())
		return 0;
	const std::vector<FdsSound> &All = FdsSounds();
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

std::string FdsSoundList()
{
	std::string Out;
	const std::vector<FdsSound> &All = FdsSounds();
	for (size_t i = 0; i < All.size(); ++i)
		Out += (i == 0 ? "" : ", ") + All[i].Name;
	return Out;
}

int FdsVolumeAs15(int Volume)
{
	if (Volume <= 0)
		return 0;
	return std::max(1, std::min(15, (Volume * 15 + FDS_VOLUME_MAX / 2) / FDS_VOLUME_MAX));
}

bool FdsModulatorWobbles(int Speed, int Depth, const std::vector<int> &Modulation)
{
	if (Speed <= 0 || Depth <= 0)
		return false;
	bool Bends = false;
	for (size_t i = 0; i < Modulation.size(); ++i)
		Bends = Bends || (Modulation[i] != 0 && Modulation[i] != 4);
	// The table goes round 1789773 * Speed / 2^22 times a second
	return Bends && 1789773.0 * Speed / 4194304.0 < 16.0;
}

}	// namespace ftapi

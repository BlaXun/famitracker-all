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

// Sound effects for games: see SoundEffects.h. Pitches are FamiTracker's
// note numbers as written (C-3 = 36 is middle C, A-3 = 45 is 440 Hz; the
// triangle sounds an octave below what's written), noise pitches 0-15 (15
// the brightest), and every value lasts a tick, 1/60 s.

#include "SoundEffects.h"

#include <algorithm>
#include <cmath>

namespace ftapi {

using transcribe::SfxChannel;
using transcribe::SfxLayer;
using transcribe::SoundEffect;

namespace {

const double PI = 3.14159265358979323846;
const SfxChannel PULSE = SfxChannel::Pulse;
const SfxChannel TRIANGLE = SfxChannel::Triangle;
const SfxChannel NOISE = SfxChannel::Noise;

int Round(double Value)
{
	return static_cast<int>(std::floor(Value + 0.5));
}

std::string Squeezed(const std::string &Text)
{
	std::string Out;
	for (size_t i = 0; i < Text.size(); ++i) {
		const char c = Text[i];
		if (c >= 'A' && c <= 'Z')
			Out += static_cast<char>(c - 'A' + 'a');
		else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
			Out += c;
	}
	return Out;
}

// Builds a layer a stretch at a time
class Maker {
public:
	explicit Maker(SfxChannel Channel, int Start = 0) : m_iDuty(Channel == NOISE ? 0 : 2)
	{
		m_Layer.channel = Channel;
		m_Layer.start = Start;
	}

	// Ticks ticks with the volume going from VolumeFrom to VolumeTo and the
	// pitch from PitchFrom to PitchTo. Curve shapes the pitch's way there (1
	// even, below 1 quick at first, above 1 slow at first), VolumeCurve the
	// volume's.
	Maker &Go(int Ticks, double VolumeFrom, double VolumeTo, double PitchFrom, double PitchTo, double Curve = 1.0,
		double VolumeCurve = 1.0)
	{
		for (int i = 0; i < Ticks; ++i) {
			const double x = Ticks > 1 ? static_cast<double>(i) / (Ticks - 1) : 1.0;
			m_Layer.volume.push_back(Round(VolumeFrom + (VolumeTo - VolumeFrom) * std::pow(x, VolumeCurve)));
			m_Layer.pitch.push_back(PitchFrom + (PitchTo - PitchFrom) * std::pow(x, Curve));
			m_Layer.duty.push_back(m_iDuty);
		}
		return *this;
	}

	Maker &Hold(int Ticks, double Volume, double Pitch)
	{
		return Go(Ticks, Volume, Volume, Pitch, Pitch);
	}

	Maker &Rest(int Ticks)
	{
		const double Pitch = m_Layer.pitch.empty() ? 0.0 : m_Layer.pitch.back();
		return Go(Ticks, 0.0, 0.0, Pitch, Pitch);
	}

	Maker &Duty(int Duty)	// from here on
	{
		m_iDuty = Duty;
		return *this;
	}

	// A vibrato from tick From for Ticks ticks: Depth semitones up and down,
	// becoming DepthEnd by its end, Hz times a second
	Maker &Vibrato(int From, int Ticks, double Depth, double Hz, double DepthEnd)
	{
		for (int i = 0; i < Ticks && static_cast<size_t>(From + i) < m_Layer.pitch.size(); ++i) {
			const double x = Ticks > 1 ? static_cast<double>(i) / (Ticks - 1) : 0.0;
			m_Layer.pitch[static_cast<size_t>(From + i)] += (Depth + (DepthEnd - Depth) * x) * std::sin(2.0 * PI * Hz * i / 60.0);
		}
		return *this;
	}

	// Up and down by Amount every other tick, for a harsh or crackling sound
	Maker &Rough(double Amount)
	{
		for (size_t t = 0; t < m_Layer.pitch.size(); ++t)
			m_Layer.pitch[t] += (t % 2 == 0) ? Amount : -Amount;
		return *this;
	}

	// The duty cycling through Duties, Each ticks each
	Maker &DutyCycle(const std::vector<int> &Duties, int Each)
	{
		for (size_t t = 0; t < m_Layer.duty.size(); ++t)
			m_Layer.duty[t] = Duties[(t / static_cast<size_t>(Each)) % Duties.size()];
		return *this;
	}

	// A copy that starts later and softer, for an echo
	Maker Echo(int Delay, double Volume, int Duty) const
	{
		Maker Out(*this);
		Out.m_Layer.start += Delay;
		for (size_t t = 0; t < Out.m_Layer.volume.size(); ++t)
			Out.m_Layer.volume[t] = std::max(Out.m_Layer.volume[t] > 0 ? 1 : 0, Round(Out.m_Layer.volume[t] * Volume));
		for (size_t t = 0; t < Out.m_Layer.duty.size(); ++t)
			Out.m_Layer.duty[t] = Duty;
		return Out;
	}

	SfxLayer Make() const { return m_Layer; }

private:
	SfxLayer m_Layer;
	int m_iDuty;
};

SoundEffect Effect(const Maker &A)
{
	SoundEffect Out;
	Out.layers.push_back(A.Make());
	return Out;
}

SoundEffect Effect(const Maker &A, const Maker &B)
{
	SoundEffect Out;
	Out.layers.push_back(A.Make());
	Out.layers.push_back(B.Make());
	return Out;
}

// The kinds

SoundEffect Jump()		// a quick rise, still climbing a little as it fades
{
	return Effect(Maker(PULSE).Duty(2).Go(8, 12, 12, 38, 50, 0.6).Go(8, 11, 1, 50, 52));
}

SoundEffect Land()		// a low drop on the triangle and a dull puff of noise
{
	return Effect(Maker(TRIANGLE).Go(5, 15, 15, 36, 28, 0.7), Maker(NOISE).Go(7, 8, 1, 3, 2));
}

SoundEffect Coin()		// B, then the E above ringing out
{
	return Effect(Maker(PULSE).Duty(1).Hold(4, 12, 59).Go(22, 13, 1, 64, 64, 1.0, 0.8));
}

SoundEffect Pickup()	// up a C major chord quickly
{
	return Effect(Maker(PULSE).Duty(2).Hold(3, 12, 48).Hold(3, 12, 52).Hold(3, 12, 55).Go(12, 12, 1, 60, 60));
}

SoundEffect PowerUp()	// three runs up a chord, each a fourth higher, then a shimmering top
{
	Maker M(PULSE);
	M.Duty(1);
	const int Starts[] = { 48, 53, 58 };
	const int Steps[] = { 0, 4, 7, 12 };
	for (int r = 0; r < 3; ++r)
		for (int s = 0; s < 4; ++s)
			M.Hold(2, 12, Starts[r] + Steps[s]);
	M.Go(14, 12, 1, 70, 70).Vibrato(24, 14, 0.4, 10.0, 0.2);
	return Effect(M);
}

SoundEffect Hurt()		// a harsh falling buzz with a hit of noise
{
	return Effect(Maker(PULSE).Duty(0).Go(15, 14, 1, 55, 40).Rough(0.5), Maker(NOISE).Go(6, 12, 2, 7, 5));
}

SoundEffect Laser()		// "pew": falling fast from high, then slower
{
	return Effect(Maker(PULSE).Duty(1).Go(13, 12, 1, 74, 50, 0.45));
}

SoundEffect Explosion()	// noise rumbling down, crackling, with a low thump
{
	return Effect(Maker(NOISE).Go(4, 15, 15, 8, 7).Go(44, 15, 1, 7, 1, 0.7, 0.6).Rough(1.0),
		Maker(TRIANGLE).Go(10, 15, 15, 30, 18, 0.6));
}

SoundEffect Bounce()	// "boing": up quickly, then a wobble that dies down
{
	return Effect(Maker(PULSE).Duty(2).Go(4, 12, 12, 40, 52, 0.7).Go(26, 12, 1, 52, 52).Vibrato(4, 26, 2.5, 8.0, 0.0));
}

SoundEffect Select()	// a tiny high blip
{
	return Effect(Maker(PULSE).Duty(1).Go(4, 10, 3, 67, 67));
}

SoundEffect Confirm()	// C, then the G above
{
	return Effect(Maker(PULSE).Duty(2).Hold(3, 11, 60).Go(10, 12, 1, 67, 67));
}

SoundEffect Cancel()	// E, then the A below
{
	return Effect(Maker(PULSE).Duty(2).Hold(3, 11, 64).Go(10, 11, 1, 57, 57));
}

SoundEffect Error()		// two low buzzes
{
	return Effect(Maker(PULSE).Duty(0).Hold(6, 12, 31).Rest(3).Hold(5, 12, 30).Go(3, 9, 2, 30, 30));
}

SoundEffect Door()		// a wooden knock: a short low thump and a puff of noise
{
	return Effect(Maker(TRIANGLE).Go(6, 15, 15, 31, 26, 0.7), Maker(NOISE).Go(9, 12, 1, 4, 3));
}

SoundEffect Chest()		// a sparkling run up a G major chord, ringing at the top, with an echo
{
	Maker M(PULSE);
	M.Duty(1);
	const int Notes[] = { 55, 59, 62, 67, 71, 74, 79 };
	for (int i = 0; i < 7; ++i)
		M.Hold(2, 11, Notes[i]);
	M.Go(18, 11, 1, 79, 79).Vibrato(14, 18, 0.3, 12.0, 0.1);
	return Effect(M, M.Echo(3, 0.5, 2));
}

SoundEffect Splash()	// a bright burst of noise falling away, with a rising "bloop"
{
	return Effect(Maker(NOISE).Go(3, 13, 13, 12, 12).Go(28, 12, 1, 12, 5, 0.8, 0.6).Rough(1.0),
		Maker(PULSE, 1).Duty(2).Go(6, 9, 2, 45, 57, 0.6));
}

SoundEffect Footstep()	// a tiny soft tap
{
	return Effect(Maker(NOISE).Go(4, 7, 1, 9, 8));
}

SoundEffect Warp()		// rising and wobbling, the duty flickering
{
	return Effect(Maker(PULSE).Go(36, 11, 11, 40, 76, 1.3).Go(8, 11, 1, 76, 77).Vibrato(0, 44, 0.6, 12.0, 0.6).DutyCycle({ 1, 2 }, 2));
}

SoundEffect Message()	// a soft chime: E, then the B above
{
	return Effect(Maker(PULSE).Duty(1).Go(5, 10, 7, 64, 64).Go(16, 10, 1, 71, 71));
}

SoundEffect Heal()		// a gentle rise up a C major chord, softly, with a slow vibrato at the top
{
	Maker M(PULSE);
	M.Duty(2);
	const int Notes[] = { 48, 52, 55, 60, 64, 67, 72 };
	for (int i = 0; i < 7; ++i)
		M.Go(4, 9, 8, Notes[i], Notes[i]);
	M.Go(14, 9, 1, 72, 72).Vibrato(28, 14, 0.3, 6.0, 0.3);
	return Effect(M);
}

SoundEffect Text()		// a very short blip, for each letter
{
	return Effect(Maker(PULSE).Duty(2).Go(2, 7, 4, 55, 55));
}

SoundEffect Stomp()		// a quick drop with a hit
{
	return Effect(Maker(PULSE).Duty(1).Go(7, 13, 2, 60, 43, 0.6), Maker(NOISE).Go(4, 11, 2, 8, 8));
}

SoundEffect Whoosh()	// air rushing past: noise swelling up and away
{
	return Effect(Maker(NOISE).Go(10, 2, 11, 5, 12).Go(12, 11, 1, 12, 7));
}

SoundEffect Break()		// something crumbling: bursts of noise
{
	return Effect(Maker(NOISE).Go(3, 14, 7, 10, 10).Go(3, 12, 6, 8, 8).Go(3, 10, 5, 11, 11).Go(4, 7, 1, 9, 9));
}

SoundEffect Fall()		// a long falling whistle
{
	return Effect(Maker(PULSE).Duty(2).Go(48, 11, 2, 64, 36));
}

SoundEffectKind Kind(const char *Name, const char *Use, std::vector<std::string> Words, SoundEffect (*Make)())
{
	SoundEffectKind K;
	K.Name = Name;
	K.Use = Use;
	K.Words = Words;
	K.Make = Make;
	return K;
}

std::vector<SoundEffectKind> MakeKinds()
{
	std::vector<SoundEffectKind> All;
	All.push_back(Kind("jump", "a quick rising blip for jumping", { "hop", "jumping" }, Jump));
	All.push_back(Kind("land", "a soft thud for landing", { "landing", "thud" }, Land));
	All.push_back(Kind("coin", "two bright notes, for coins and gems", { "gem", "ring", "money" }, Coin));
	All.push_back(Kind("pickup", "a short rising arpeggio, for picking up an item", { "item", "pick up", "collect", "get item" }, Pickup));
	All.push_back(Kind("power-up", "three rising runs and a shimmer, for power-ups and upgrades", { "powerup", "upgrade", "power" }, PowerUp));
	All.push_back(Kind("hurt", "a harsh falling buzz with a hit, for taking damage", { "damage", "hit", "ouch", "pain" }, Hurt));
	All.push_back(Kind("laser", "a falling zap (\"pew\"), for shooting", { "pew", "shoot", "shot", "zap", "blaster", "fire" }, Laser));
	All.push_back(Kind("explosion", "a rumbling, crackling burst of noise", { "boom", "kssh", "blast", "explode", "bomb" }, Explosion));
	All.push_back(Kind("bounce", "a springy \"boing\"", { "boing", "spring", "trampoline" }, Bounce));
	All.push_back(Kind("select", "a tiny blip, for moving through menus", { "menu", "cursor", "move", "blip", "click" }, Select));
	All.push_back(Kind("confirm", "two rising notes, for OK and accept", { "ok", "accept", "yes", "start" }, Confirm));
	All.push_back(Kind("cancel", "two falling notes, for back and cancel", { "back", "no", "close" }, Cancel));
	All.push_back(Kind("error", "a low double buzz, for something that can't be done", { "buzz", "wrong", "denied", "invalid" }, Error));
	All.push_back(Kind("door", "a short wooden knock, for doors", { "knock", "open door" }, Door));
	All.push_back(Kind("chest", "a sparkling rise with an echo, for opening a chest", { "treasure", "open chest", "loot" }, Chest));
	All.push_back(Kind("splash", "water: a burst of noise and a rising bloop", { "water", "swim", "drop" }, Splash));
	All.push_back(Kind("footstep", "a tiny soft tap, for footsteps", { "step", "walk", "footsteps" }, Footstep));
	All.push_back(Kind("warp", "a wobbling rise, for teleporting", { "teleport", "portal" }, Warp));
	All.push_back(Kind("message", "a soft two-note chime, for chat messages and notifications", { "chat", "ping", "notification", "notify",
		"whisper" }, Message));
	All.push_back(Kind("heal", "a gentle rising arpeggio, for healing", { "healing", "restore", "cure" }, Heal));
	All.push_back(Kind("text", "a very short blip per letter, for dialogue", { "dialogue", "dialog", "letter", "typing", "talk" }, Text));
	All.push_back(Kind("stomp", "a quick drop with a hit, for landing on an enemy", { "squash", "bop" }, Stomp));
	All.push_back(Kind("whoosh", "a rush of air, for dashing", { "dash", "swoosh", "swing", "woosh" }, Whoosh));
	All.push_back(Kind("break", "bursts of noise, for breaking blocks or pots", { "crumble", "smash", "shatter", "crack" }, Break));
	All.push_back(Kind("fall", "a long falling whistle, for falling into a pit", { "falling", "pit" }, Fall));
	return All;
}

// A layer stretched in time (Factor 2: twice as long)
SfxLayer Stretched(const SfxLayer &Layer, double Factor)
{
	if (std::fabs(Factor - 1.0) < 1e-9 || Layer.volume.empty())
		return Layer;
	SfxLayer Out = Layer;
	Out.start = Round(Layer.start * Factor);
	const int Length = std::max(1, Round(Layer.length() * Factor));
	Out.volume.clear();
	Out.pitch.clear();
	Out.duty.clear();
	for (int t = 0; t < Length; ++t) {
		const double From = std::min(static_cast<double>(Layer.length() - 1), t / Factor);
		const int Before = static_cast<int>(std::floor(From));
		const double Part = From - Before;
		Out.volume.push_back(Layer.volume[static_cast<size_t>(Before)]);
		Out.pitch.push_back(Layer.pitchAt(Before) + (Layer.pitchAt(Before + 1) - Layer.pitchAt(Before)) * Part);
		Out.duty.push_back(Layer.dutyAt(Before));
	}
	if (Out.volume.size() > 1 && Layer.volume.back() == 0)
		Out.volume.back() = 0;	// it still ends in silence
	return Out;
}

}	// namespace

const std::vector<SoundEffectKind> &SoundEffectKinds()
{
	static const std::vector<SoundEffectKind> All = MakeKinds();
	return All;
}

const SoundEffectKind *FindSoundEffect(const std::string &Name)
{
	const std::string Wanted = Squeezed(Name);
	if (Wanted.empty())
		return 0;
	const std::vector<SoundEffectKind> &All = SoundEffectKinds();
	for (size_t i = 0; i < All.size(); ++i)
		if (Squeezed(All[i].Name) == Wanted)
			return &All[i];
	for (size_t i = 0; i < All.size(); ++i)
		for (size_t w = 0; w < All[i].Words.size(); ++w)
			if (Squeezed(All[i].Words[w]) == Wanted)
				return &All[i];
	return 0;
}

std::string SoundEffectNames()
{
	std::string Names;
	const std::vector<SoundEffectKind> &All = SoundEffectKinds();
	for (size_t i = 0; i < All.size(); ++i)
		Names += (i == 0 ? "" : ", ") + All[i].Name;
	return Names;
}

namespace {

// How each variant from 2 on differs: semitones, length in percent, and
// every other one a pulse's duty
const int VARIANT_PITCH[] = { 3, -3, 5, -2, 7, -5, 2, -4 };
const int VARIANT_LENGTH[] = { 85, 120, 75, 110, 90, 130, 80, 115 };

}	// namespace

std::string VariantText(int Variant)
{
	if (Variant <= 1)
		return std::string();
	const size_t i = static_cast<size_t>(Variant - 2) % 8;
	const int Pitch = VARIANT_PITCH[i], Length = VARIANT_LENGTH[i];
	std::string Text = std::to_string(std::abs(Pitch)) + (std::abs(Pitch) == 1 ? " semitone " : " semitones ") +
		(Pitch > 0 ? "higher" : "lower") + ", " + std::to_string(std::abs(Length - 100)) + "% " + (Length < 100 ? "shorter" : "longer");
	if (Variant % 2 == 0)
		Text += ", another pulse duty";
	return Text;
}

SoundEffect ChangeSoundEffect(const SoundEffect &Effect, const SoundEffectChanges &Changes)
{
	int Semitones = Changes.Semitones;
	double Stretch = Changes.LengthPercent / 100.0;
	bool OtherDuty = false;
	if (Changes.Variant > 1) {
		const size_t i = static_cast<size_t>(Changes.Variant - 2) % 8;
		Semitones += VARIANT_PITCH[i];
		Stretch *= VARIANT_LENGTH[i] / 100.0;
		OtherDuty = Changes.Variant % 2 == 0;
	}
	SoundEffect Out;
	for (size_t l = 0; l < Effect.layers.size(); ++l) {
		SfxLayer Layer = Stretched(Effect.layers[l], Stretch);
		const bool Noise = Layer.channel == NOISE;
		for (size_t t = 0; t < Layer.pitch.size(); ++t) {
			if (Noise)
				Layer.pitch[t] = std::max(0.0, std::min(15.0, Layer.pitch[t] + Round(Semitones / 3.0)));
			else	// A-0 is a pulse's lowest note
				Layer.pitch[t] = std::max(9.0, std::min(95.0, Layer.pitch[t] + Semitones));
		}
		if (OtherDuty && Layer.channel == PULSE) {
			static const int OTHER[] = { 1, 2, 1, 2 };	// thin buzz and 75% become 25%, 25% and 50% swap
			for (size_t t = 0; t < Layer.duty.size(); ++t)
				Layer.duty[t] = OTHER[std::max(0, std::min(3, Layer.duty[t]))];
		}
		for (size_t t = 0; t < Layer.volume.size(); ++t) {
			const int Was = Layer.volume[t];
			Layer.volume[t] = Was == 0 ? 0 : std::max(1, std::min(15, Round(Was * Changes.VolumePercent / 100.0)));
		}
		if (static_cast<int>(Layer.volume.size()) > transcribe::SFX_MAX_TICKS)
			Layer.volume.resize(static_cast<size_t>(transcribe::SFX_MAX_TICKS));
		Out.layers.push_back(Layer);
	}
	return Out;
}

}	// namespace ftapi

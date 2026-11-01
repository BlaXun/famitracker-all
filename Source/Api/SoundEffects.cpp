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
#include <cstdio>
#include "Music.h"
#include "Song.h"

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

// A steady random draw, so a sound comes out the same each time it's made
class Random {
public:
	explicit Random(unsigned int Seed) : m_State(Seed * 2654435761u + 0x9E3779B9u)
	{
		if (m_State == 0)
			m_State = 1;
		Next();
	}
	unsigned int Next()
	{
		m_State ^= m_State << 13;
		m_State ^= m_State >> 17;
		m_State ^= m_State << 5;
		return m_State;
	}
	double Unit()    // 0 to just under 1
	{
		return (Next() >> 8) / 16777216.0;
	}

private:
	unsigned int m_State;
};

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

SoundEffect Coin()		// a glint of our own: A, then the F# a sixth above ringing out, shimmering
{
	// A bright 25% pulse; while the F# rings, its duty flickers to 12.5% and back every two ticks, a metallic shimmer.
	// (Not a famous game's coin, whose two notes are a fourth apart, B to E.)
	SfxLayer Glint = Maker(PULSE).Duty(1).Hold(3, 12, 57).Go(24, 13, 0, 66, 66, 1.0, 0.7).Make();
	for (size_t t = 3; t < Glint.duty.size(); ++t)
		Glint.duty[t] = ((t - 3) / 2) % 2 == 0 ? 1 : 0;
	SoundEffect Out;
	Out.layers.push_back(Glint);
	return Out;
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

// Nature and machines, built from layers the way make_sound_effect's
// described sounds are (DesignSoundEffect), at 60 ticks a second

SfxLayerDesign Layer(SfxChannel Channel, double Start, double Seconds, double Pitch, double PitchTo, int Volume, int Envelope)
{
	SfxLayerDesign D;
	D.Channel = Channel;
	D.Start = Start;
	D.Seconds = Seconds;
	D.Pitch = Pitch;
	D.PitchTo = PitchTo;
	D.Volume = Volume;
	D.Envelope = Envelope;
	D.Duty = Channel == NOISE ? 0 : 2;
	return D;
}

SoundEffect Designed(const std::vector<SfxLayerDesign> &Layers, bool Loop, double Seconds = 0.0, int Seed = 1)
{
	SoundEffect Out;
	std::string Problem;
	DesignSoundEffect(Layers, Seconds, 60, Seed, Loop, 2, Out, Problem, Loop ? 1 << 20 : SFX_ONE_SHOT_TICKS);	// as long as it's made
	return Out;
}

// The loops of ambience, Seconds long: their random parts drawn for the whole of it

// A heavy wind: dark noise in uneven gusts, louder and brighter as each one blows. At volume 6 (at 10 it played 5-7 dB over
// the other loops in FamiTracker, and the user found it "too loud probably")
SoundEffect WindFor(double Seconds, const SoundKindOptions &)
{
	SfxLayerDesign Air = Layer(NOISE, 0.0, Seconds, 6, -1, 6, SFX_STEADY);
	Air.Gusts = std::max(1, Round(0.75 * Seconds));	// three in four seconds
	return Designed({ Air }, true);
}

SoundEffect RainFor(double Seconds, const SoundKindOptions &)	// a soft, bright hiss with drops pattering on it, and now and then a drop plinking
{
	SfxLayerDesign Hiss = Layer(NOISE, 0.0, Seconds, 13, -1, 3, SFX_STEADY);
	Hiss.Crackle = 0.4;
	SfxLayerDesign Plinks = Layer(PULSE, 0.0, Seconds, 84, -1, 4, SFX_DROPS);
	Plinks.Crackle = 0.25;
	Plinks.Duty = 0;
	return Designed({ Hiss, Plinks }, true);
}

SoundEffect FireFor(double Seconds, const SoundKindOptions &)	// a dark, flickering roar with crackles popping out of it
{
	SfxLayerDesign Roar = Layer(NOISE, 0.0, Seconds, 3, -1, 5, SFX_STEADY);
	Roar.Gusts = std::max(1, Round(1.5 * Seconds));	// six in four seconds
	Roar.Crackle = 0.6;
	return Designed({ Roar }, true);
}

SoundEffect Wind()
{
	return WindFor(4.0, SoundKindOptions());
}

SoundEffect Rain()
{
	return RainFor(4.0, SoundKindOptions());
}

SoundEffect Fire()
{
	return FireFor(4.0, SoundKindOptions());
}

SoundEffect Thunder()	// a bright crack, then a rough rumble rolling away in waves, over a low boom
{
	SfxLayerDesign Crack = Layer(NOISE, 0.0, 0.2, 14, 10, 15, SFX_HIT);
	Crack.Glide = SFX_GLIDE_FAST;
	SfxLayerDesign Rumble = Layer(NOISE, 0.2, 3.2, 6, 2, 15, SFX_STEADY);
	Rumble.VolumeTo = 2;
	Rumble.Gusts = 4;
	Rumble.Rough = true;
	SfxLayerDesign Boom = Layer(TRIANGLE, 0.05, 0.45, 31, 19, 15, SFX_HIT);
	Boom.Glide = SFX_GLIDE_FAST;
	return Designed({ Crack, Rumble, Boom }, false);
}

SoundEffect Wings()		// wings flapping away, fading, with a chirp as the bird takes off and a faint one far off
{
	SfxLayerDesign Flaps = Layer(NOISE, 0.0, 1.1, 7, 5, 10, SFX_HIT);
	Flaps.Repeats = 7;
	Flaps.Gap = 0.3;
	Flaps.VolumeTo = 2;
	SfxLayerDesign Chirp = Layer(PULSE, 0.0, 0.12, 74, 81, 9, SFX_HIT);
	Chirp.Glide = SFX_GLIDE_FAST;
	SfxLayerDesign Far = Layer(PULSE, 0.8, 0.12, 76, 83, 3, SFX_HIT);
	Far.Glide = SFX_GLIDE_FAST;
	return Designed({ Flaps, Chirp, Far }, false);
}

SoundEffect Engine()	// an engine idling: a low, buzzing pulse and dark noise, throbbing together
{
	SfxLayerDesign Hum = Layer(PULSE, 0.0, 1.0, 21, -1, 8, SFX_STEADY);
	Hum.Duty = 0;
	Hum.Flutter = 15.0;
	SfxLayerDesign Rumble = Layer(NOISE, 0.0, 1.0, 2, -1, 6, SFX_STEADY);
	Rumble.Flutter = 15.0;
	return Designed({ Hum, Rumble }, true);
}

SoundEffect Alarm()		// a siren whooping up from A-4 to A-5, over and over
{
	SfxLayerDesign Whoop = Layer(PULSE, 0.0, 2.0, 57, 69, 11, SFX_STEADY);
	Whoop.Repeats = 4;
	Whoop.Gap = 0.1;
	Whoop.Glide = SFX_GLIDE_SLOW;
	Whoop.Duty = 1;
	return Designed({ Whoop }, true);
}

SoundEffect Heartbeat()	// lub-dub: two low thumps on the triangle with a dull knock, once a second
{
	SfxLayerDesign Lub = Layer(TRIANGLE, 0.0, 0.1, 33, 26, 15, SFX_HIT);
	Lub.Glide = SFX_GLIDE_FAST;
	SfxLayerDesign Dub = Layer(TRIANGLE, 0.22, 0.08, 31, 25, 15, SFX_HIT);
	Dub.Glide = SFX_GLIDE_FAST;
	SfxLayerDesign Knock = Layer(NOISE, 0.0, 0.08, 3, 2, 7, SFX_HIT);
	SfxLayerDesign Knock2 = Layer(NOISE, 0.22, 0.06, 3, 2, 5, SFX_HIT);
	return Designed({ Lub, Dub, Knock, Knock2 }, true, 1.0);
}

// The kinds added for nature and places (feat-sfx-kinds), made in the style and at the level of the kinds above (the user
// preferred them to designs from the layers that were more elaborate), except the reward, which is the designed item get the
// user preferred

SoundEffect Chirp()		// one bird's "chir-rup": a quick slide up, then a little fall
{
	SfxLayerDesign Up = Layer(PULSE, 0.0, 0.12, 74, 81, 9, SFX_HIT);
	Up.Glide = SFX_GLIDE_FAST;
	SfxLayerDesign Down = Layer(PULSE, 0.13, 0.08, 80, 76, 6, SFX_HIT);
	Down.Glide = SFX_GLIDE_FAST;
	return Designed({ Up, Down }, false);
}

// Birds as a scene: one bird (Count 1), two answering each other (the kind's own), and more calling farther off
SoundEffect BirdsWith(const SoundKindOptions &Options)
{
	const int Count = Options.Count <= 0 ? 2 : std::max(1, std::min(4, Options.Count));
	SfxLayerDesign Tweet = Layer(PULSE, 0.0, 0.25, 74, 81, 9, SFX_HIT);
	Tweet.Repeats = 2;
	Tweet.Glide = SFX_GLIDE_FAST;
	SfxLayerDesign Trill = Layer(PULSE, 0.35, 0.35, 83, 78, 8, SFX_HIT);
	Trill.Repeats = 5;
	SfxLayerDesign Again = Layer(PULSE, 1.1, 0.12, 74, 81, 7, SFX_HIT);
	Again.Glide = SFX_GLIDE_FAST;
	std::vector<SfxLayerDesign> Layers = { Tweet, Trill, Again };
	if (Count >= 2) {
		SfxLayerDesign Answer = Layer(PULSE, 0.55, 0.4, 71, 76, 6, SFX_SWELL_FADE);
		Answer.Repeats = 2;
		Answer.Duty = 1;
		Layers.push_back(Answer);
	}
	if (Count >= 3) {	// a third farther off, higher and fainter, after the others
		SfxLayerDesign Far = Layer(PULSE, 1.3, 0.3, 79, 84, 4, SFX_HIT);
		Far.Repeats = 3;
		Far.Glide = SFX_GLIDE_FAST;
		Far.Duty = 1;
		Layers.push_back(Far);
	}
	if (Count >= 4) {	// and a fourth answering it, a little lower
		SfxLayerDesign Last = Layer(PULSE, 1.7, 0.24, 76, 72, 4, SFX_HIT);
		Last.Repeats = 2;
		Last.Glide = SFX_GLIDE_FAST;
		Layers.push_back(Last);
	}
	return Designed(Layers, false, 0.0, Options.Seed);
}

SoundEffect Birds()
{
	return BirdsWith(SoundKindOptions());
}

// Birdsong as ambience, Seconds long: calls here and there, Count birds (the kind's two), on the two pulses in turn
SoundEffect BirdsFor(double Seconds, const SoundKindOptions &Options)
{
	const int Count = Options.Count <= 0 ? 2 : std::max(1, std::min(6, Options.Count));
	Random R(static_cast<unsigned int>(Options.Seed) * 7919u + 17u);
	std::vector<SfxLayerDesign> Layers;
	double End[2] = { 0.0, 0.0 };
	const double Every = 2.4 / Count;	// a call every this long on average
	for (double t = 0.3 * R.Unit(); t < Seconds - 0.5; t += Every * (0.6 + 0.8 * R.Unit())) {
		const int Lane = End[0] <= End[1] ? 0 : 1;
		const double At = std::max(t, End[Lane] + 0.05);
		if (At > Seconds - 0.5)
			break;
		const int Bird = static_cast<int>(R.Unit() * Count);	// each bird its own pitch and voice
		const double Pitch = 72 + (Bird * 5) % 11;
		SfxLayerDesign Call = Layer(PULSE, At, 0.12 + 0.2 * R.Unit(), Pitch, Pitch + 5 + std::floor(3 * R.Unit()),
			std::max(3, 9 - Bird - static_cast<int>(3 * R.Unit())), SFX_HIT);
		Call.Glide = SFX_GLIDE_FAST;
		Call.Repeats = 1 + static_cast<int>(3 * R.Unit());
		Call.Duty = Bird % 2 == 0 ? 2 : 1;
		Call.Voice = Lane;
		End[Lane] = At + Call.Seconds;
		Layers.push_back(Call);
	}
	if (Layers.empty())
		Layers.push_back(Layer(PULSE, 0.0, 0.12, 74, 81, 9, SFX_HIT));
	return Designed(Layers, true, Seconds, Options.Seed);
}

// Waves as add_ambience draws them (Ambience.cpp, Bobber on the Bay's technique, which the user found fine in Night
// Harbor, where the earlier surf of gliding noise sounded "a bit weird"): each wave swells in at one noise pitch and,
// at its top, sinks away two steps darker, a bigger and a smaller in turn (the smaller the bigger's shape under a lower
// volume column, so all the waves share two instruments), each a step brighter or darker than the one before. Seconds
// long, in whole waves. Small: water lapping, quicker, brighter and softer
const int SURF_IN_TICKS = 72, SURF_WAVE_TICKS = 225, LAP_IN_TICKS = 26, LAP_WAVE_TICKS = 80;
SoundEffect Surf(double Seconds, int Seed, bool Small)
{
	Random R(static_cast<unsigned int>(Seed) * 104729u + (Small ? 5u : 3u));
	const int Wave = Small ? LAP_WAVE_TICKS : SURF_WAVE_TICKS, InTicks = Small ? LAP_IN_TICKS : SURF_IN_TICKS;
	const int Ticks = std::max(12, Round(Seconds * 60.0));
	const int Count = std::max(1, Round(static_cast<double>(Ticks) / Wave)), Each = Ticks / Count;
	const int In = std::min(InTicks, Each / 3), Away = Each - In;
	const int Top = Small ? 8 : 11;
	std::vector<int> Swell, Sink;
	for (int t = 0; t < In; ++t)
		Swell.push_back(std::max(1, std::min(Top, Round(1.0 + (Top - 1.0) * std::pow(t / std::max(1.0, In - 1.0), 1.4)))));
	for (int t = 0; t < Away; ++t)
		Sink.push_back(std::max(1, Round(Top * std::pow(1.0 - t / std::max(1.0, Away - 1.0), 1.6))));	// never quite silent
	static const int WAVE_PITCHES[] = { 8, 7, 9, 8, 7, 8 }, LAP_PITCHES[] = { 10, 9, 11, 10, 11, 9 };
	static const int WAVE_COLUMNS[] = { 15, 11 }, LAP_COLUMNS[] = { 15, 11, 13, 10 };
	SfxLayer Layer;
	Layer.channel = NOISE;
	for (int i = 0; i < Count; ++i) {
		const int Pitch = (Small ? LAP_PITCHES : WAVE_PITCHES)[(i + static_cast<int>(R.Unit() * 2.0)) % 6];
		const int Column = Small ? LAP_COLUMNS[i % 4] : WAVE_COLUMNS[i % 2];
		for (int t = 0; t < In; ++t) {
			Layer.volume.push_back(SfxColumnVolume(Swell[static_cast<size_t>(t)], Column));
			Layer.pitch.push_back(Pitch);
			Layer.duty.push_back(0);
		}
		for (int t = 0; t < Away; ++t) {
			Layer.volume.push_back(SfxColumnVolume(Sink[static_cast<size_t>(t)], Column));
			Layer.pitch.push_back(Pitch - 2);
			Layer.duty.push_back(0);
		}
	}
	while (Layer.length() < Ticks) {	// the ticks a wave's whole count leaves over: silent
		Layer.volume.push_back(0);
		Layer.pitch.push_back(Layer.pitch.back());
		Layer.duty.push_back(0);
	}
	SoundEffect Sound;
	Sound.layers.push_back(Layer);
	return Sound;
}

SoundEffect WavesFor(double Seconds, const SoundKindOptions &Options)
{
	return Surf(Seconds, Options.Seed, false);
}

SoundEffect LappingFor(double Seconds, const SoundKindOptions &Options)
{
	return Surf(Seconds, Options.Seed, true);
}

SoundEffect Waves()		// a bigger wave and a smaller one
{
	return Surf(2 * SURF_WAVE_TICKS / 60.0, 1, false);
}

SoundEffect Lapping()	// four little waves
{
	return Surf(4 * LAP_WAVE_TICKS / 60.0, 1, true);
}

// A door creaking open, slowly: a dry wooden groan low on a thin pulse, the hinge catching and slipping in uneven grains
// (its volume, not its pitch: the earlier creak's climb of seven semitones and its vibrato sounded "too goofy"), its pitch
// drifting a semitone or two, then easing to rest, and a dull stop
SoundEffect Creak()
{
	Random R(3u);
	Maker Groan(PULSE);
	Groan.Duty(0);
	const int Ticks = 84;	// 1.4 s
	for (int t = 0; t < Ticks; ++t) {
		const double x = t / (Ticks - 1.0);
		// Swelling in over a fifth of it and easing out over the last third
		const double Level = std::min(1.0, x / 0.2) * std::min(1.0, (1.0 - x) / 0.35 + 0.15);
		// A grain every two to four ticks: the hinge catching (loud) and slipping (soft)
		const double Grain = (t % 3 == 0 || R.Unit() < 0.25) ? 1.0 : 0.45 + 0.2 * R.Unit();
		const double Pitch = 34.0 + 1.5 * std::sin(PI * std::min(1.0, x * 1.3)) - 0.5 * x;	// A#2 up to about C-3 and back
		Groan.Go(1, std::max(1.0, 9.0 * Level * Grain), std::max(1.0, 9.0 * Level * Grain), Pitch, Pitch);
	}
	Maker Stop(NOISE, Ticks + 2);
	Stop.Go(2, 8, 6, 3, 3).Go(4, 5, 1, 2, 2);
	return Effect(Groan, Stop);
}

// One step on a surface: the kind's own soft tap on dirt, a hollow tok on wood, a brush on grass...
SoundEffect StepOn(const std::string &Surface, int Start)
{
	if (Surface == "wood")	// a hollow tok: a short falling thump on the triangle under a click, and a small rattle
		return Effect(Maker(TRIANGLE, Start).Go(3, 15, 15, 57, 50, 0.6), Maker(NOISE, Start).Go(2, 8, 5, 9, 9).Rest(1).Go(3, 4, 1, 6, 6));
	if (Surface == "grass")	// a soft, high brush and a small crunch, no thump
		return Effect(Maker(NOISE, Start).Go(2, 4, 7, 12, 12).Go(3, 7, 2, 12, 11).Rest(2).Go(3, 4, 1, 11, 11));
	if (Surface == "stone")	// a hard heel's click: sharp and bright at once, then a little grit (the softer click was weaker
		// than the other surfaces)
		return Effect(Maker(NOISE, Start).Go(1, 13, 13, 13, 13).Go(1, 10, 10, 12, 12).Go(3, 5, 1, 10, 9));
	if (Surface == "snow")	// a soft, muffled crunch, dark and short (the brighter, longer one sounded too loud beside the others)
		return Effect(Maker(NOISE, Start).Go(2, 2, 4, 8, 8).Go(4, 4, 1, 8, 7).Rough(1.0));
	if (Surface == "sand")	// a duller, softer crunch
		return Effect(Maker(NOISE, Start).Go(3, 2, 5, 8, 8).Go(6, 5, 1, 8, 7).Rough(1.0));
	if (Surface == "water")	// a splashy step: wet noise falling away and a small bloop
		return Effect(Maker(NOISE, Start).Go(2, 8, 8, 11, 11).Go(8, 7, 1, 11, 8, 0.8, 0.6).Rough(1.0),
			Maker(PULSE, Start + 1).Duty(2).Go(4, 4, 1, 50, 57, 0.6));
	if (Surface == "metal")	// a bright "bling": a tick of bright noise for the heel and a high, thin ring dying away quickly (the
		// noise's metallic mode buzzed, as if the player were shocked on every step)
		return Effect(Maker(PULSE, Start).Duty(1).Go(1, 9, 9, 76, 76).Go(10, 7, 1, 76, 76, 1.0, 0.6),
			Maker(NOISE, Start).Go(1, 6, 6, 14, 14).Go(1, 2, 2, 13, 13));
	return Effect(Maker(NOISE, Start).Go(4, 7, 1, 9, 8));	// dirt: a tiny soft tap
}

// Count steps (one: the kind's own), a third of a second apart, every other one a little softer
SoundEffect FootstepWith(const SoundKindOptions &Options)
{
	const int Count = std::max(1, std::min(8, Options.Count));
	SoundEffect Out;
	for (int i = 0; i < Count; ++i) {
		SoundEffect Step = StepOn(Options.Surface, i * 20);
		for (size_t l = 0; l < Step.layers.size(); ++l) {
			transcribe::SfxLayer &Layer = Step.layers[l];
			if (i % 2 == 1)
				for (size_t t = 0; t < Layer.volume.size(); ++t)
					Layer.volume[t] = Layer.volume[t] > 0 ? std::max(1, Layer.volume[t] - 1) : 0;
			// Each kind of channel one layer, its steps one after another
			transcribe::SfxLayer *pLane = 0;
			for (size_t k = 0; k < Out.layers.size() && pLane == 0; ++k)
				if (Out.layers[k].channel == Layer.channel)
					pLane = &Out.layers[k];
			if (pLane == 0) {
				Out.layers.push_back(Layer);
				continue;
			}
			while (pLane->start + pLane->length() < Layer.start) {
				pLane->volume.push_back(0);
				pLane->pitch.push_back(pLane->pitch.empty() ? Layer.pitchAt(0) : pLane->pitch.back());
				pLane->duty.push_back(pLane->dutyAt(pLane->length()));
			}
			for (int t = 0; t < Layer.length(); ++t) {
				pLane->volume.push_back(Layer.volume[static_cast<size_t>(t)]);
				pLane->pitch.push_back(Layer.pitchAt(t));
				pLane->duty.push_back(Layer.dutyAt(t));
			}
		}
	}
	return Out;
}

SoundEffect Footstep()
{
	return FootstepWith(SoundKindOptions());
}

SoundEffect Gust()		// one gust: wind swelling in and brightening, then falling away and darkening, a little unevenly
{
	SfxLayerDesign Rise = Layer(NOISE, 0.0, 0.6, 3, 8, 1, SFX_HELD);
	Rise.VolumeTo = 11;
	Rise.Glide = SFX_GLIDE_SLOW;
	SfxLayerDesign Fall = Layer(NOISE, 0.6, 1.0, 8, 3, 11, SFX_FADE);
	Fall.Glide = SFX_GLIDE_FAST;
	Fall.Gusts = 2;
	return Designed({ Rise, Fall }, false);
}

// A howling wind, Seconds long: wind in long, slow gusts, its noise rising brighter as each one howls and sinking dark
// again, all breath (the quiet pulse that moaned over it played a "goofy note", a tune where the user wanted air)
SoundEffect HowlFor(double Seconds, const SoundKindOptions &Options)
{
	SfxLayerDesign Air = Layer(NOISE, 0.0, Seconds, 5, -1, 9, SFX_STEADY);
	Air.Gusts = std::max(1, Round(0.4 * Seconds));
	SoundEffect Out = Designed({ Air }, true, 0.0, Options.Seed);
	// Wider than wind's: the pitch swings another three steps with the gust (its volume says where the gust is)
	for (size_t l = 0; l < Out.layers.size(); ++l) {
		SfxLayer &L = Out.layers[l];
		for (size_t t = 0; t < L.pitch.size() && t < L.volume.size(); ++t)
			L.pitch[t] = std::max(1.0, std::min(10.0, L.pitch[t] + 3.0 * (L.volume[t] / 9.0 - 0.5)));
	}
	return Out;
}

SoundEffect Howl()
{
	return HowlFor(6.0, SoundKindOptions());
}

SoundEffect Owl()		// "hoo... hoo-hoo, hoooo": soft swelling hoots on a round pulse, the last one falling away
{
	std::vector<SfxLayerDesign> Hoots;
	const double Starts[] = { 0.0, 0.75, 0.95, 1.2 }, Lengths[] = { 0.42, 0.16, 0.18, 0.62 };
	for (int i = 0; i < 4; ++i) {
		SfxLayerDesign Hoot = Layer(PULSE, Starts[i], Lengths[i], 43, i == 3 ? 41 : 42.5, i == 1 || i == 2 ? 8 : 10, SFX_SWELL_FADE);
		Hoot.Glide = SFX_GLIDE_SLOW;
		Hoot.Vibrato = 0.15;
		Hoot.VibratoHz = 5.0;
		Hoots.push_back(Hoot);
	}
	return Designed(Hoots, false);
}

// A water drop: a pure plip rising quickly on the triangle, its echo faint on a pulse as in a cave
SoundEffect DripAt(int Start, double Pitch, int Volume)
{
	return Effect(Maker(TRIANGLE, Start).Go(4, 15, 15, Pitch, Pitch + 7, 0.5),
		Maker(PULSE, Start + 8).Duty(1).Go(3, Volume, 1, Pitch + 7, Pitch + 12, 0.5).Rest(5).Go(3, std::max(1, Volume / 2), 1,
			Pitch + 7, Pitch + 12, 0.5));
}

SoundEffect Drip()
{
	return DripAt(0, 81, 4);
}

// Dripping, Seconds long: Count drops in four seconds (the kind's three) here and there, each a little higher or lower
SoundEffect DripsFor(double Seconds, const SoundKindOptions &Options)
{
	const double PerSecond = (Options.Count > 0 ? std::min(16, Options.Count) : 3) / 4.0;
	Random R(static_cast<unsigned int>(Options.Seed) * 15485863u + 11u);
	const int Ticks = std::max(30, Round(Seconds * 60.0));
	SoundEffect Out;
	transcribe::SfxLayer Tri, Echo;
	Tri.channel = TRIANGLE;
	Echo.channel = PULSE;
	for (int t = Round(std::min(30, Ticks - 24) * R.Unit()); t + 24 <= Ticks; t += std::max(24, Round(60.0 / PerSecond * (0.5 + R.Unit())))) {
		const SoundEffect Drop = DripAt(t, 78 + std::floor(5 * R.Unit()), 3 + static_cast<int>(2 * R.Unit()));
		for (size_t l = 0; l < Drop.layers.size(); ++l) {
			transcribe::SfxLayer &Lane = Drop.layers[l].channel == TRIANGLE ? Tri : Echo;
			const transcribe::SfxLayer &D = Drop.layers[l];
			while (Lane.length() < D.start) {
				Lane.volume.push_back(0);
				Lane.pitch.push_back(D.pitchAt(0));
				Lane.duty.push_back(D.dutyAt(0));
			}
			for (int k = 0; k < D.length(); ++k) {
				Lane.volume.push_back(D.volume[static_cast<size_t>(k)]);
				Lane.pitch.push_back(D.pitchAt(k));
				Lane.duty.push_back(D.dutyAt(k));
			}
		}
	}
	transcribe::SfxLayer *const Lanes[2] = { &Tri, &Echo };
	for (int l = 0; l < 2; ++l) {
		transcribe::SfxLayer &Lane = *Lanes[l];
		if (Lane.volume.empty())
			continue;
		while (Lane.length() < Ticks) {
			Lane.volume.push_back(0);
			Lane.pitch.push_back(Lane.pitch.back());
			Lane.duty.push_back(Lane.duty.back());
		}
		Lane.volume.resize(static_cast<size_t>(Ticks));
		Lane.pitch.resize(static_cast<size_t>(Ticks));
		Lane.duty.resize(static_cast<size_t>(Ticks));
		Out.layers.push_back(Lane);
	}
	if (Out.layers.empty())
		return DripAt(0, 81, 4);
	return Out;
}

SoundEffect Emerge()	// out of the water: a rising rush as the body comes up, water streaming off, and a few drops after
{
	SfxLayerDesign Rush = Layer(NOISE, 0.0, 0.15, 7, 10, 3, SFX_HELD);
	Rush.VolumeTo = 10;
	SfxLayerDesign Stream = Layer(NOISE, 0.15, 0.85, 10, 6, 10, SFX_FADE);
	Stream.VolumeTo = 1;
	Stream.Crackle = 0.25;
	SfxLayerDesign Drop1 = Layer(TRIANGLE, 0.45, 0.07, 79, 86, 15, SFX_HIT);
	Drop1.Glide = SFX_GLIDE_FAST;
	SfxLayerDesign Drop2 = Layer(TRIANGLE, 0.7, 0.06, 82, 89, 15, SFX_HIT);
	Drop2.Glide = SFX_GLIDE_FAST;
	return Designed({ Rush, Stream, Drop1, Drop2 }, false);
}

// A bubble: a quick bloop up the scale in whole semitones (a glide between them would give each pitch an instrument of its
// own), at Column's share of its level, so every bubble of a sound plays one instrument
void Bloop(SfxLayer &Lane, int At, int Pitch, int Column)
{
	static const int STEPS[] = { 0, 2, 4, 5, 7 }, LEVELS[] = { 6, 8, 7, 5, 3 };
	while (Lane.length() < At) {
		Lane.volume.push_back(0);
		Lane.pitch.push_back(Lane.pitch.empty() ? Pitch : Lane.pitch.back());
		Lane.duty.push_back(2);
	}
	for (int i = 0; i < 5; ++i) {
		Lane.volume.push_back(SfxColumnVolume(LEVELS[i], Column));
		Lane.pitch.push_back(Pitch + STEPS[i]);
		Lane.duty.push_back(2);
	}
}

// Bubbles rising: Count bloops (the kind's four; six were "too many, too active") at uneven times, now and then two close
// together, each at a pitch and a loudness of its own
const int BUBBLE_COUNT = 4;
SoundEffect BubblesWith(const SoundKindOptions &Options)
{
	const int Count = Options.Count > 0 ? std::min(24, Options.Count) : BUBBLE_COUNT;
	Random R(static_cast<unsigned int>(Options.Seed) * 32452843u + 7u);
	static const int COLUMNS[] = { 15, 11, 13, 9 };
	SfxLayer Lane;
	Lane.channel = PULSE;
	int At = 0;
	for (int i = 0; i < Count; ++i) {
		Bloop(Lane, At, 50 + static_cast<int>(12 * R.Unit()), COLUMNS[i % 4]);
		At += R.Unit() < 0.3 ? 7 + static_cast<int>(3 * R.Unit()) : 12 + static_cast<int>(14 * R.Unit());
	}
	Lane.volume.push_back(0);
	Lane.pitch.push_back(Lane.pitch.back());
	Lane.duty.push_back(2);
	SoundEffect Out;
	Out.layers.push_back(Lane);
	return Out;
}

SoundEffect Bubbles()
{
	return BubblesWith(SoundKindOptions());
}

// Underwater, Seconds long: a bubble now and then, at uneven times (a second to two apart, now and then two close
// together), Count of them in four seconds (the kind's three); no murmur of noise under them (the user heard it as "noise in
// the background which is weird")
const int BUBBLES_IN_FOUR_SECONDS = 3;
SoundEffect BubblesFor(double Seconds, const SoundKindOptions &Options)
{
	const int Ticks = std::max(30, Round(Seconds * 60.0));
	const double Every = 240.0 / (Options.Count > 0 ? std::min(16, Options.Count) : BUBBLES_IN_FOUR_SECONDS);
	Random R(static_cast<unsigned int>(Options.Seed) * 32452843u + 13u);
	static const int COLUMNS[] = { 15, 10, 13, 8, 12 };
	SfxLayer Lane;
	Lane.channel = PULSE;
	int i = 0;
	for (int At = Round(20 * R.Unit()); At + 6 <= Ticks; ++i) {
		Bloop(Lane, At, 50 + static_cast<int>(12 * R.Unit()), COLUMNS[i % 5]);
		if (R.Unit() < 0.25 && At + 22 <= Ticks) {	// another close behind it
			At += 8 + static_cast<int>(4 * R.Unit());
			Bloop(Lane, At, 50 + static_cast<int>(12 * R.Unit()), COLUMNS[(i + 2) % 5]);
		}
		At += std::max(12, Round(Every * (0.45 + 0.7 * R.Unit())));
	}
	while (Lane.length() < Ticks) {
		Lane.volume.push_back(0);
		Lane.pitch.push_back(Lane.pitch.empty() ? 50.0 : Lane.pitch.back());
		Lane.duty.push_back(2);
	}
	Lane.volume.resize(static_cast<size_t>(Ticks));
	Lane.pitch.resize(static_cast<size_t>(Ticks));
	Lane.duty.resize(static_cast<size_t>(Ticks));
	SoundEffect Out;
	Out.layers.push_back(Lane);
	return Out;
}

// A lantern swinging on a chain, Seconds long: a thin squeak each way, the second lower; each swing a little unlike the one
// before (the clink where it turned, a tick of the noise's metallic mode, was heard as "one blip note" after each squeak)
SoundEffect ChainFor(double Seconds, const SoundKindOptions &Options)
{
	Random R(static_cast<unsigned int>(Options.Seed) * 49979687u + 3u);
	const int Swings = std::max(1, Round(Seconds / 1.6));
	const double Each = Seconds / Swings;
	std::vector<SfxLayerDesign> Layers;
	for (int i = 0; i < Swings; ++i) {
		const double At = i * Each, Shift = std::floor(3 * R.Unit()) - 1;
		SfxLayerDesign Out = Layer(PULSE, At + 0.1 * Each, 0.25 * Each, 67 + Shift, 69 + Shift, 7, SFX_SWELL_FADE);
		Out.Glide = SFX_GLIDE_SLOW;
		Out.Vibrato = 0.4;
		Out.VibratoHz = 9.0;
		Out.Rough = true;
		Out.Duty = 0;
		SfxLayerDesign Back = Out;
		Back.Start = At + 0.6 * Each;
		Back.Pitch = 64 + Shift;
		Back.PitchTo = 62 + Shift;
		Back.Volume = 6;
		Layers.push_back(Out);
		Layers.push_back(Back);
	}
	return Designed(Layers, true, Seconds, Options.Seed);
}

SoundEffect Chain()
{
	return ChainFor(3.2, SoundKindOptions());
}

// Entering a cave: the open air outside (a brighter breeze) fading as it darkens into the cave's low, hollow hush, which
// swells and dies away, and one drop falling far off with its echo coming back twice, fainter (the earlier deep moan on a
// pulse left the user wondering what it was)
SoundEffect Cave()
{
	Maker Air(NOISE);
	Air.Go(40, 6, 3, 9, 4, 0.7).Go(50, 3, 7, 3, 2, 1.0).Hold(20, 7, 2).Go(70, 7, 0, 2, 2, 1.0, 0.7);
	SfxLayerDesign Drop = Layer(TRIANGLE, 1.45, 0.07, 81, 88, 15, SFX_HIT);
	Drop.Glide = SFX_GLIDE_FAST;
	SfxLayerDesign Echo = Layer(PULSE, 1.75, 0.1, 86, 88, 4, SFX_HIT);
	Echo.Repeats = 1;
	Echo.Glide = SFX_GLIDE_FAST;
	Echo.Duty = 1;
	SfxLayerDesign Again = Echo;
	Again.Start = 2.1;
	Again.Volume = 2;
	SoundEffect Out = Designed({ Drop, Echo, Again }, false);
	Out.layers.insert(Out.layers.begin(), Air.Make());
	return Out;
}

SoundEffect Outfit()	// changing an outfit: a soft cloth swish twice, then a little sparkle rising and ringing
{
	SfxLayerDesign Swish = Layer(NOISE, 0.0, 0.36, 9, 11, 9, SFX_SWELL_FADE);
	Swish.Repeats = 2;
	Swish.Gap = 0.1;
	std::vector<SfxLayerDesign> Layers = { Swish };
	const int Sparkle[] = { 76, 80, 83, 88 };
	for (int i = 0; i < 4; ++i) {
		SfxLayerDesign Note = Layer(PULSE, 0.38 + i * 0.05, i == 3 ? 0.35 : 0.05, Sparkle[i], -1, 7, i == 3 ? SFX_FADE : SFX_HELD);
		Note.Duty = 1;
		Note.Echo = i == 3;
		Layers.push_back(Note);
	}
	return Designed(Layers, false);
}

// "You got something awesome": a quick run up a C major chord landing on a high C that rings with a light vibrato, a
// harmony a sixth under it, and a short drop on the triangle under the landing (the designed item get the user chose)
SoundEffect Reward()
{
	std::vector<SfxLayerDesign> Layers;
	const int Run[] = { 55, 60, 64, 67 }, Loud[] = { 10, 10, 11, 11 };
	for (int i = 0; i < 4; ++i) {
		SfxLayerDesign Note = Layer(PULSE, i * 0.08, 0.08, Run[i], -1, Loud[i], SFX_STEADY);
		Note.Duty = 1;
		Layers.push_back(Note);
	}
	SfxLayerDesign Top = Layer(PULSE, 0.32, 0.7, 72, -1, 12, SFX_STEADY);
	Top.VolumeTo = 2;
	Top.Vibrato = 0.25;
	Top.VibratoHz = 6.0;
	Top.Duty = 1;
	SfxLayerDesign Under = Layer(PULSE, 0.32, 0.6, 64, -1, 7, SFX_STEADY);
	Under.VolumeTo = 1;
	SfxLayerDesign Bom = Layer(TRIANGLE, 0.32, 0.1, 48, 36, 15, SFX_STEADY);
	Bom.Glide = SFX_GLIDE_SLOW;
	Layers.push_back(Top);
	Layers.push_back(Under);
	Layers.push_back(Bom);
	return Designed(Layers, false);
}


SoundEffectKind Kind(const char *Name, const char *Use, std::vector<std::string> Words, SoundEffect (*Make)(), bool Loops = false)
{
	SoundEffectKind K;
	K.Name = Name;
	K.Use = Use;
	K.Words = Words;
	K.Make = Make;
	K.Loops = Loops;
	K.MakeLong = 0;
	K.MakeWith = 0;
	return K;
}

std::vector<SoundEffectKind> MakeKinds()
{
	std::vector<SoundEffectKind> All;
	All.push_back(Kind("jump", "a quick rising blip for jumping", { "hop", "jumping" }, Jump));
	All.push_back(Kind("land", "a soft thud for landing", { "landing", "thud" }, Land));
	All.push_back(Kind("coin", "a bright glint of two notes, for coins and gems", { "gem", "ring", "money" }, Coin));
	All.push_back(Kind("pickup", "a short rising arpeggio, for picking up an item", { "item", "pick up", "collect" }, Pickup));
	All.push_back(Kind("power-up", "three rising runs and a shimmer, for power-ups and upgrades", { "powerup", "upgrade", "power" }, PowerUp));
	All.push_back(Kind("hurt", "a harsh falling buzz with a hit, for taking damage", { "damage", "hit", "ouch", "pain" }, Hurt));
	All.push_back(Kind("laser", "a falling zap (\"pew\"), for shooting", { "pew", "shoot", "shot", "zap", "blaster" }, Laser));
	All.push_back(Kind("explosion", "a rumbling, crackling burst of noise", { "boom", "kssh", "blast", "explode", "bomb" }, Explosion));
	All.push_back(Kind("bounce", "a springy \"boing\"", { "boing", "spring", "trampoline" }, Bounce));
	All.push_back(Kind("select", "a tiny blip, for moving through menus", { "menu", "cursor", "move", "blip", "click" }, Select));
	All.push_back(Kind("confirm", "two rising notes, for OK and accept", { "ok", "accept", "yes", "start" }, Confirm));
	All.push_back(Kind("cancel", "two falling notes, for back and cancel", { "back", "no", "close" }, Cancel));
	All.push_back(Kind("error", "a low double buzz, for something that can't be done", { "buzz", "wrong", "denied", "invalid" }, Error));
	All.push_back(Kind("door", "a short wooden knock, for doors (a door creaking open: creak)", { "knock", "door knock", "knocking",
		"close door", "door shut", "shut door" }, Door));
	All.push_back(Kind("chest", "a sparkling rise with an echo, for opening a chest", { "treasure", "open chest", "loot" }, Chest));
	All.push_back(Kind("splash", "water: a burst of noise and a rising bloop, for jumping into water", { "water", "swim",
		"jump into water", "jumping into water", "into water", "into the water", "dive", "dive in", "diving", "plunge",
		"fall into water", "splash in" }, Splash));
	All.push_back(Kind("footstep", "a step: a tiny soft tap on dirt, or on another surface (wood, grass, stone, snow, sand, water, "
		"metal), count steps", { "step", "walk", "footsteps", "steps", "walking", "footfall" }, Footstep));
	All.back().MakeWith = FootstepWith;
	All.back().Surfaces = { "dirt", "wood", "grass", "stone", "snow", "sand", "water", "metal" };
	All.back().Counts = "steps";
	All.push_back(Kind("warp", "a wobbling rise, for teleporting", { "teleport", "portal" }, Warp));
	All.push_back(Kind("message", "a soft two-note chime, for chat messages and notifications", { "chat", "ping", "notification", "notify",
		"whisper" }, Message));
	All.push_back(Kind("heal", "a gentle rising arpeggio, for healing", { "healing", "restore", "cure" }, Heal));
	All.push_back(Kind("text", "a very short blip per letter, for dialogue", { "dialogue", "dialog", "letter", "typing", "talk" }, Text));
	All.push_back(Kind("stomp", "a quick drop with a hit, for landing on an enemy", { "squash", "bop" }, Stomp));
	All.push_back(Kind("whoosh", "a rush of air, for dashing", { "dash", "swoosh", "swing", "woosh" }, Whoosh));
	All.push_back(Kind("break", "bursts of noise, for breaking blocks or pots", { "crumble", "smash", "shatter", "crack" }, Break));
	All.push_back(Kind("fall", "a long falling whistle, for falling into a pit", { "falling", "pit" }, Fall));
	All.push_back(Kind("wind", "a heavy wind blowing in gusts (loops, for ambience; one gust: gust; a howl: howl)", { "breeze",
		"gale", "heavy wind", "windy", "wind blowing", "blowing wind", "strong wind" }, Wind, true));
	All.back().MakeLong = WindFor;
	All.push_back(Kind("rain", "rain: a soft hiss with drops pattering (loops, for ambience)", { "rainfall", "drizzle", "downpour",
		"raining", "rainy", "shower" }, Rain, true));
	All.back().MakeLong = RainFor;
	All.push_back(Kind("fire", "a fire flickering and crackling (loops, for ambience)", { "flames", "campfire", "fireplace", "burning",
		"crackle", "crackling", "torch", "bonfire" }, Fire, true));
	All.back().MakeLong = FireFor;
	All.push_back(Kind("thunder", "a crack of thunder rumbling away", { "thunderclap", "lightning", "thunderstorm", "rumble",
		"storm" }, Thunder));
	All.push_back(Kind("birds", "birds chirping, one answering the other (count: 1-4 birds; loop for birdsong as ambience)",
		{ "bird", "birdsong", "songbird", "chirping", "birds chirping", "chirping birds", "bird song", "bird call", "chirps" }, Birds));
	All.back().MakeWith = BirdsWith;
	All.back().MakeLong = BirdsFor;
	All.back().Counts = "birds";
	All.push_back(Kind("chirp", "one bird's single chirp, a quick \"chir-rup\"", { "bird chirp", "tweet", "single chirp", "peep",
		"cheep", "one chirp" }, Chirp));
	All.push_back(Kind("wings", "wings flapping away with a chirp, for a bird flying off", { "bird flying away", "flying away", "fly away",
		"flap", "flaps", "flapping", "take off", "takeoff", "flutter", "bat" }, Wings));
	All.push_back(Kind("engine", "an engine idling, a low throbbing hum (loops)", { "motor", "car", "machine", "engine hum", "vehicle",
		"idle", "truck", "machinery" }, Engine, true));
	All.push_back(Kind("alarm", "a siren whooping up again and again (loops)", { "siren", "alert", "klaxon", "emergency", "whoop" },
		Alarm, true));
	All.push_back(Kind("heartbeat", "a heartbeat, lub-dub once a second (loops), for low health or tension", { "heart", "heart beat",
		"heartbeats", "lub dub", "low health", "low hp" }, Heartbeat, true));
	All.push_back(Kind("waves", "waves on a shore, drawing in, breaking and washing back (loops, for ambience)", { "wave", "sea",
		"ocean", "surf", "shore", "beach", "tide", "seaside", "harbor", "harbour", "coast", "ocean waves", "sea waves" }, Waves, true));
	All.back().MakeLong = WavesFor;
	All.push_back(Kind("lapping", "small waves lapping at a dock or a lake's edge, softer and quicker (loops)", { "water lapping",
		"lapping water", "lake", "pond", "dock", "pier", "ripples", "gentle waves", "small waves" }, Lapping, true));
	All.back().MakeLong = LappingFor;
	All.push_back(Kind("creak", "a door creaking open slowly: a dry, low wooden groan and a dull stop", { "door creak",
		"creaking", "creaking door", "creaky door", "creaky", "open door", "door opening", "opening door", "hinge", "creaky hinge",
		"squeaky door", "floorboard", "creaking floor" }, Creak));
	All.push_back(Kind("gust", "one gust of wind swelling in and falling away (plays once; wind loops)", { "gust of wind",
		"wind gust", "gusts", "a gust", "puff of wind", "blast of wind" }, Gust));
	All.push_back(Kind("howl", "a howling wind: long, slow gusts of air rising brighter as each one howls (loops)", { "howling wind",
		"wind howling", "wind howl", "howling", "moaning wind", "blizzard", "cave wind", "wind in a cave", "wind in cave",
		"haunted wind", "ghostly wind", "storm wind" }, Howl, true));
	All.back().MakeLong = HowlFor;
	All.push_back(Kind("owl", "an owl hooting softly, hoo... hoo-hoo, hoooo", { "owl hoot", "hoot", "hooting", "owls", "night bird",
		"hoo" }, Owl));
	All.push_back(Kind("drip", "a water drop: a pure plip with a faint echo, as in a cave (loop: dripping, count drops in 4 s)",
		{ "drop", "water drop", "drops", "droplet", "dripping", "drip drop", "plip", "plink", "water drip", "leak", "dripping water",
		"cave drip" }, Drip));
	All.back().MakeLong = DripsFor;
	All.back().Counts = "drops in four seconds (with loop)";
	All.push_back(Kind("emerge", "out of the water: a rising rush, water streaming off, and a few drops", { "out of water",
		"out of the water", "jump out of water", "jumping out of water", "climb out of water", "leave water", "leaving water",
		"surface", "surfacing", "get out of water", "splash out" }, Emerge));
	All.push_back(Kind("bubbles", "bubbles rising underwater, uneven bloops (count: how many; loop for underwater ambience)",
		{ "bubble", "underwater", "bubbling", "blub", "gurgle", "air bubbles", "underwater bubbles", "bubbles underwater" }, Bubbles));
	All.back().MakeWith = BubblesWith;
	All.back().MakeLong = BubblesFor;
	All.back().Counts = "bubbles";
	All.push_back(Kind("chain", "a lantern swinging on a chain: a thin squeak each way (loops)", { "chains",
		"chain creak", "chain squeak", "squeaking chain", "squeaky chain", "creaking chain", "lantern", "swinging lantern",
		"hanging lantern", "rusty chain", "squeak", "squeaking" }, Chain, true));
	All.back().MakeLong = ChainFor;
	All.push_back(Kind("cave", "entering a cave: the open air outside fading into a low, hollow hush, and one drop falling far off, "
		"its echo coming back", {
		"cave entrance", "enter cave", "entering cave", "entering a cave", "cavern", "into the cave", "underground", "mine entrance",
		"dungeon entrance", "enter the cave" }, Cave));
	All.push_back(Kind("outfit", "changing an outfit: a soft cloth swish twice and a little sparkle", { "outfit change",
		"change outfit", "changing outfit", "costume", "costume change", "change clothes", "changing clothes", "dress up",
		"wardrobe", "equip", "clothes", "new clothes", "swish", "change costume" }, Outfit));
	All.push_back(Kind("reward", "\"you got something awesome\": a bright run up landing on a high note that rings, with a "
		"harmony and a low drop (a lucky box, an item get)", { "lucky box", "loot box", "mystery box", "gift box", "open lucky box",
		"opening lucky box", "item get", "get item", "got item", "you got", "you got something", "fanfare", "prize", "jackpot",
		"achievement", "unlock", "unlocked", "rare item", "treasure get", "item fanfare", "present" }, Reward));
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

namespace {

// The words of a text, lowercase, without punctuation
std::vector<std::string> WordsOf(const std::string &Text)
{
	std::vector<std::string> Out;
	std::string Word;
	for (size_t i = 0; i <= Text.size(); ++i) {
		const char c = i < Text.size() ? Text[i] : ' ';
		if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '\'')
			Word += c;
		else if (c >= 'A' && c <= 'Z')
			Word += static_cast<char>(c - 'A' + 'a');
		else if (!Word.empty()) {
			Out.push_back(Word);
			Word.clear();
		}
	}
	return Out;
}

// Other words for the surfaces, as people say them
const char *const SURFACE_WORDS[][2] = { { "wooden", "wood" }, { "planks", "wood" }, { "plank", "wood" }, { "floorboards", "wood" },
	{ "boards", "wood" }, { "bridge", "wood" }, { "deck", "wood" }, { "grassy", "grass" }, { "leaves", "grass" }, { "meadow", "grass" },
	{ "lawn", "grass" }, { "rock", "stone" }, { "rocks", "stone" }, { "rocky", "stone" }, { "tile", "stone" }, { "tiles", "stone" },
	{ "marble", "stone" }, { "cobble", "stone" }, { "cobblestone", "stone" }, { "cobblestones", "stone" }, { "brick", "stone" },
	{ "concrete", "stone" }, { "snowy", "snow" }, { "ice", "snow" }, { "sandy", "sand" }, { "gravel", "sand" }, { "beach", "sand" },
	{ "puddle", "water" }, { "puddles", "water" }, { "wet", "water" }, { "shallow", "water" }, { "mud", "water" },
	{ "muddy", "water" }, { "iron", "metal" }, { "steel", "metal" }, { "grate", "metal" }, { "metallic", "metal" },
	{ "earth", "dirt" }, { "ground", "dirt" }, { "path", "dirt" }, { "soil", "dirt" } };

}	// namespace

std::string FindSurface(const SoundEffectKind &Kind, const std::string &Word)
{
	std::string Wanted = Squeezed(Word);
	for (size_t i = 0; i < sizeof(SURFACE_WORDS) / sizeof(SURFACE_WORDS[0]); ++i)
		if (Wanted == SURFACE_WORDS[i][0])
			Wanted = SURFACE_WORDS[i][1];
	for (size_t i = 0; i < Kind.Surfaces.size(); ++i)
		if (Kind.Surfaces[i] == Wanted)
			return Wanted;
	return std::string();
}

int FindSoundStyle(const std::string &Word)
{
	static const struct { const char *Word; int Style; } STYLES[] = { { "realistic", SFX_STYLE_REALISTIC },
		{ "grounded", SFX_STYLE_REALISTIC }, { "natural", SFX_STYLE_REALISTIC }, { "real", SFX_STYLE_REALISTIC },
		{ "subtle", SFX_STYLE_REALISTIC }, { "lifelike", SFX_STYLE_REALISTIC }, { "cartoon", SFX_STYLE_CARTOON },
		{ "cartoony", SFX_STYLE_CARTOON }, { "cartoonish", SFX_STYLE_CARTOON }, { "silly", SFX_STYLE_CARTOON },
		{ "exaggerated", SFX_STYLE_CARTOON }, { "bouncy", SFX_STYLE_CARTOON }, { "game", SFX_STYLE_GAME },
		{ "retro", SFX_STYLE_GAME }, { "default", SFX_STYLE_GAME }, { "classic", SFX_STYLE_GAME } };
	const std::string Wanted = Squeezed(Word);
	for (size_t i = 0; i < sizeof(STYLES) / sizeof(STYLES[0]); ++i)
		if (Wanted == STYLES[i].Word)
			return STYLES[i].Style;
	return -1;
}

const SoundEffectKind *FindSoundEffectIn(const std::string &Text, std::string &Surface, int &Style)
{
	Surface.clear();
	Style = -1;
	const SoundEffectKind *pWhole = FindSoundEffect(Text);
	if (pWhole != 0)
		return pWhole;
	const std::vector<std::string> Words = WordsOf(Text);
	static const char *const FILLERS[] = { "a", "an", "the", "of", "on", "in", "into", "with", "and", "sound", "sounds", "sfx",
		"effect", "some", "inside", "for", "my", "to" };
	auto Filler = [&](const std::string &W) {
		for (size_t f = 0; f < sizeof(FILLERS) / sizeof(FILLERS[0]); ++f)
			if (W == FILLERS[f])
				return true;
		return false;
	};
	for (size_t Length = Words.size(); Length > 0; --Length)
		for (size_t From = 0; From + Length <= Words.size(); ++From) {
			if (Filler(Words[From]) || Filler(Words[From + Length - 1]))
				continue;
			std::string Run;
			for (size_t w = From; w < From + Length; ++w)
				Run += (w == From ? "" : " ") + Words[w];
			const SoundEffectKind *pKind = FindSoundEffect(Run);
			if (pKind == 0)
				continue;
			for (size_t w = 0; w < Words.size(); ++w) {
				if (w >= From && w < From + Length)
					continue;
				if (Surface.empty())
					Surface = FindSurface(*pKind, Words[w]);
				if (Style < 0)
					Style = FindSoundStyle(Words[w]);
			}
			return pKind;
		}
	return 0;
}

SoundEffect MakeSoundKind(const SoundEffectKind &Kind, const SoundKindOptions &Options)
{
	return Kind.MakeWith != 0 ? Kind.MakeWith(Options) : Kind.Make();
}

bool HasPulse(const SoundEffect &Effect)
{
	for (size_t l = 0; l < Effect.layers.size(); ++l)
		if (Effect.layers[l].channel == PULSE)
			return true;
	return false;
}

void StyleSoundEffect(SoundEffect &Effect, int Style)
{
	if (Style != SFX_STYLE_REALISTIC && Style != SFX_STYLE_CARTOON)
		return;
	const double Sweep = Style == SFX_STYLE_REALISTIC ? 0.5 : 1.5;
	for (size_t l = 0; l < Effect.layers.size(); ++l) {
		SfxLayer &Layer = Effect.layers[l];
		const bool Noise = Layer.channel == NOISE;
		// Each stretch it sounds: its pitch around where it ends up, a sweep of more than half an octave narrowed (or widened)
		for (int a = 0; a < Layer.length(); ) {
			if (Layer.volume[static_cast<size_t>(a)] <= 0) {
				++a;
				continue;
			}
			int b = a;
			while (b < Layer.length() && Layer.volume[static_cast<size_t>(b)] > 0)
				++b;
			while (static_cast<int>(Layer.pitch.size()) < b)
				Layer.pitch.push_back(Layer.pitch.empty() ? 0.0 : Layer.pitch.back());
			if (!Noise) {
				double Low = 1e9, High = -1e9;
				for (int t = a; t < b; ++t) {
					Low = std::min(Low, Layer.pitch[static_cast<size_t>(t)]);
					High = std::max(High, Layer.pitch[static_cast<size_t>(t)]);
				}
				const double Rest = Layer.pitch[static_cast<size_t>(b - 1)];
				if (High - Low > 7.0)
					for (int t = a; t < b; ++t) {
						double &P = Layer.pitch[static_cast<size_t>(t)];
						P = std::max(9.0, std::min(95.0, Rest + (P - Rest) * Sweep));
					}
			}
			// A softer attack: a sound of four ticks or more that starts loud comes in over two
			if (Style == SFX_STYLE_REALISTIC && Layer.channel != TRIANGLE && b - a >= 4 && Layer.volume[static_cast<size_t>(a)] >= 6) {
				const int Full = Layer.volume[static_cast<size_t>(a)];
				Layer.volume[static_cast<size_t>(a)] = std::max(1, Round(Full * 0.55));
				Layer.volume[static_cast<size_t>(a + 1)] = std::max(1, std::min(Layer.volume[static_cast<size_t>(a + 1)],
					Round(Full * 0.85)));
			}
			a = b;
		}
	}
}

double SoundLevelOffset(const SoundEffectKind &Kind, const SoundKindOptions &Options)
{
	if (Kind.Name == "wind" || Kind.Name == "howl")	// a bed of air: the user heard the realistic wind at -24 LUFS as "too loud"
		return -4.0;
	if (Kind.Name == "footstep" && Options.Surface == "snow")	// at -18 LUFS beside the other steps "too loud"
		return -3.0;
	return 0.0;
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

std::string VariantText(int Variant, bool Pulse)
{
	if (Variant <= 1)
		return std::string();
	const size_t i = static_cast<size_t>(Variant - 2) % 8;
	const int Pitch = VARIANT_PITCH[i], Length = VARIANT_LENGTH[i];
	std::string Text = std::to_string(std::abs(Pitch)) + (std::abs(Pitch) == 1 ? " semitone " : " semitones ") +
		(Pitch > 0 ? "higher" : "lower") + ", " + std::to_string(std::abs(Length - 100)) + "% " + (Length < 100 ? "shorter" : "longer");
	if (Variant % 2 == 0 && Pulse)
		Text += ", another pulse duty";
	return Text;
}

namespace {

// How much longer the changes make a sound (2: twice as long)
double StretchOf(const SoundEffectChanges &Changes)
{
	double Stretch = Changes.LengthPercent / 100.0;
	if (Changes.Variant > 1)
		Stretch *= VARIANT_LENGTH[static_cast<size_t>(Changes.Variant - 2) % 8] / 100.0;
	return Stretch;
}

// Its ticks when stretched, as Stretched makes them
int StretchedLength(const SoundEffect &Effect, double Stretch)
{
	int Length = 0;
	const bool Same = std::fabs(Stretch - 1.0) < 1e-9;
	for (size_t l = 0; l < Effect.layers.size(); ++l) {
		const SfxLayer &Layer = Effect.layers[l];
		if (Layer.volume.empty())
			continue;
		Length = std::max(Length, Same ? Layer.start + Layer.length() :
			Round(Layer.start * Stretch) + std::max(1, Round(Layer.length() * Stretch)));
	}
	return Length;
}

}	// namespace

bool FitSoundEffect(const SoundEffect &Effect, int Most, SoundEffectChanges &Changes)
{
	bool Shortened = false;
	while (Changes.LengthPercent > 1 && StretchedLength(Effect, StretchOf(Changes)) > Most) {
		--Changes.LengthPercent;
		Shortened = true;
	}
	return Shortened;
}

void RingOut(SoundEffect &Effect, int Most)
{
	for (size_t l = 0; l < Effect.layers.size(); ++l) {
		SfxLayer &Layer = Effect.layers[l];
		// The triangle has no volume (0.4.6 plays it whole at any volume above 0): a fade would only hold its last pitch
		// longer at full level. It stops where it ends, its wave holding the step it stopped on, which makes no sound
		if (Layer.channel == transcribe::SfxChannel::Triangle)
			continue;
		// From its last tick that sounds, halving a tick (15, 7, 3, 1), over the silence after it and on, then the 0 its
		// instrument ends with (its sequence holds SFX_MAX_TICKS)
		int Last = Layer.length() - 1;
		while (Last >= 0 && Layer.volume[static_cast<size_t>(Last)] <= 0)
			--Last;
		int Volume = Last >= 0 ? Layer.volume[static_cast<size_t>(Last)] : 0;
		for (int t = Last + 1; Volume > 1 && t < std::max(transcribe::SFX_MAX_TICKS, Most) - 1; ++t) {
			Volume /= 2;
			if (t < Layer.length())
				Layer.volume[static_cast<size_t>(t)] = Volume;
			else
				Layer.volume.push_back(Volume);
			Last = t;
		}
		// One as long as a layer can be fades within its last ticks instead: at most 1, 3, 7 from where it stops
		for (int k = 0, Most = 1; Volume > 1 && Last - k >= 0 && Most < 15; ++k, Most = Most * 2 + 1) {
			int &Tick = Layer.volume[static_cast<size_t>(Last - k)];
			Tick = std::min(Tick, Most);
		}
	}
}

int SoundTrackRows(const SoundEffect &Effect, bool Loop)
{
	if (Loop)
		return Effect.length();
	int Sounding = 0;	// the tick after the last that sounds
	for (size_t l = 0; l < Effect.layers.size(); ++l) {
		const SfxLayer &Layer = Effect.layers[l];
		for (int t = Layer.length() - 1; t >= 0; --t)
			if (Layer.volume[static_cast<size_t>(t)] > 0) {
				Sounding = std::max(Sounding, Layer.start + t + 1);
				break;
			}
	}
	return std::max(Effect.length(), Sounding + SFX_SILENT_TICKS) + 1;
}

SoundEffect ChangeSoundEffect(const SoundEffect &Effect, const SoundEffectChanges &Changes, int Most)
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
		if (static_cast<int>(Layer.volume.size()) > Most)
			Layer.volume.resize(static_cast<size_t>(std::max(1, Most)));
		Out.layers.push_back(Layer);
	}
	return Out;
}

// Sounds described in words

SfxLayerDesign::SfxLayerDesign() : Channel(transcribe::SfxChannel::Pulse), Start(0.0), Seconds(0.5), Pitch(60.0), PitchTo(-1.0),
	Glide(SFX_GLIDE_EVEN), Volume(12), VolumeTo(-1), Envelope(SFX_STEADY), Repeats(1), Gap(-1.0), Step(0.0), Gusts(0),
	Flutter(0.0), Vibrato(0.0), VibratoHz(6.0), Crackle(0.0), Rough(false), Duty(2), Echo(false), Voice(-1), Pan(0.0)
{
}

int MaxSoundTicks(bool Loop)
{
	return transcribe::SFX_MAX_TICKS - (Loop ? 1 : 0);
}

int MaxLoopTicks(int MaxFrames, int PatternLength)
{
	return std::max(MaxSoundTicks(true), MaxFrames * PatternLength);
}

int MaxOneShotTicks(int MaxFrames, int PatternLength)
{
	return std::max(MaxSoundTicks(false), std::min(SFX_ONE_SHOT_TICKS, MaxFrames * PatternLength - SFX_SILENT_TICKS - 2));
}

std::vector<SfxLayer> SoundPieces(const SfxLayer &Layer, int Period)
{
	std::vector<SfxLayer> Out;
	const int Length = Layer.length(), Most = MaxSoundTicks(true);
	if (Length <= Most || (Length == transcribe::SFX_MAX_TICKS && Layer.volume.back() == 0)) {
		Out.push_back(Layer);
		return Out;
	}
	// Equal pieces; for a loop played over and over, whole turns of it that divide its length, so every piece is alike
	const int Count = (Length + Most - 1) / Most;
	int Piece = (Length + Count - 1) / Count;
	if (Period > 0 && Period <= Most) {
		const int Turns = (Length + Period - 1) / Period;
		int Each = Most / Period;
		while (Each > 1 && Turns % Each != 0)
			--Each;
		Piece = Period * Each;
	}
	for (int From = 0; From < Length; From += Piece) {
		SfxLayer P;
		P.channel = Layer.channel;
		P.start = Layer.start + From;
		bool Sounds = false;
		for (int t = From; t < std::min(Length, From + Piece); ++t) {
			P.volume.push_back(Layer.volume[static_cast<size_t>(t)]);
			P.pitch.push_back(Layer.pitchAt(t));
			P.duty.push_back(Layer.dutyAt(t));
			Sounds = Sounds || Layer.volume[static_cast<size_t>(t)] > 0;
		}
		if (Sounds)
			Out.push_back(P);
	}
	return Out;
}

int SfxColumnVolume(int Volume, int Column)
{
	return Volume <= 0 || Column <= 0 ? 0 : std::max(1, Volume * Column / 15);
}

std::vector<SfxLayer> SoundEvents(const SfxLayer &Layer)
{
	std::vector<SfxLayer> Out;
	const int N = Layer.length(), Most = MaxSoundTicks(true);
	auto HeldFrom = [&](int From, int End) {	// ticks the pitch stays as it is at From
		int k = From;
		while (k < End && Layer.pitchAt(k) == Layer.pitchAt(From))
			++k;
		return k - From;
	};
	auto Add = [&](int a, int b) {
		const int Count = (b - a + Most - 1) / Most, Piece = (b - a + Count - 1) / Count;
		for (int From = a; From < b; From += Piece) {
			SfxLayer P;
			P.channel = Layer.channel;
			P.start = Layer.start + From;
			for (int t = From; t < std::min(b, From + Piece); ++t) {
				P.volume.push_back(Layer.volume[static_cast<size_t>(t)]);
				P.pitch.push_back(Layer.pitchAt(t));
				P.duty.push_back(Layer.dutyAt(t));
			}
			Out.push_back(P);
		}
	};
	for (int t = 0; t < N; ) {
		if (Layer.volume[static_cast<size_t>(t)] <= 0) {
			++t;
			continue;
		}
		int End = t;
		while (End < N && Layer.volume[static_cast<size_t>(End)] > 0)
			++End;
		int From = t, Run = t;
		for (int k = t + 1; k < End; ++k) {
			if (Layer.pitchAt(k) == Layer.pitchAt(k - 1))
				continue;
			if (k - Run >= SFX_HELD_PITCH_TICKS && HeldFrom(k, End) >= SFX_HELD_PITCH_TICKS) {
				Add(From, k);
				From = k;
			}
			Run = k;
		}
		Add(From, End);
		t = End;
	}
	return Out;
}

namespace {

// A loop played Times times over, each layer one layer still, silent between its turns
SoundEffect Repeated(const SoundEffect &Base, int Times)
{
	const int Length = Base.length();
	SoundEffect Out;
	for (size_t l = 0; l < Base.layers.size(); ++l) {
		const SfxLayer &Layer = Base.layers[l];
		SfxLayer Long;
		Long.channel = Layer.channel;
		Long.start = Layer.start;
		for (int t = 0; t < Times * Length - Layer.start; ++t) {
			const int In = (Layer.start + t) % Length - Layer.start;	// in its own time, below 0 before it starts again
			const int At = std::max(0, std::min(Layer.length() - 1, In));
			Long.volume.push_back(In >= 0 && In < Layer.length() ? Layer.volume[static_cast<size_t>(In)] : 0);
			Long.pitch.push_back(Layer.pitchAt(At));
			Long.duty.push_back(Layer.dutyAt(At));
		}
		Out.layers.push_back(Long);
	}
	return Out;
}

}	// namespace

SoundEffect LoopOfLength(const SoundEffectKind &Kind, double Seconds, const SoundEffectChanges &Asked, int Most, int &Period,
	const SoundKindOptions &Options)
{
	SoundEffectChanges Changes = Asked;
	Changes.LengthPercent = 100;		// the length is the one asked
	const double Stretch = StretchOf(Changes);	// a variant's own
	Period = 0;
	if (Kind.MakeLong != 0) {
		const double Made = std::min(Seconds, Most / 60.0) / Stretch;
		SoundEffect Out = ChangeSoundEffect(Kind.MakeLong(Made, Options), Changes, 1 << 20);
		while (Out.length() > Most && Changes.Variant > 1)	// rounding: a tick or two over
			Out = ChangeSoundEffect(Kind.MakeLong(Made - (Out.length() - Most + 1) / (60.0 * Stretch), Options), Changes, 1 << 20);
		return Out;
	}
	const SoundEffect Base = ChangeSoundEffect(MakeSoundKind(Kind, Options), Changes, 1 << 20);
	const int Length = Base.length();
	if (Length <= 0)
		return Base;
	const int Times = std::max(1, std::min(std::max(1, Most / Length), Round(Seconds * 60.0 / Length)));
	Period = Length;
	return Repeated(Base, Times);
}

namespace {


// Slow, uneven swells: a value from 0 to 1 a tick, Count of them in Ticks.
// Whole cycles, so its end leads back into its start.
std::vector<double> GustCurve(int Ticks, int Count, Random &R)
{
	std::vector<double> Out(static_cast<size_t>(std::max(0, Ticks)), 0.5);
	if (Ticks < 2 || Count < 1)
		return Out;
	// Cycles beside the gusts' own make them uneven: some stronger, some longer
	const int Cycles[4] = { Count, Count + 1, 2 * Count + 1, 3 * Count + 2 };
	const double Weights[4] = { 1.0, 0.6, 0.4, 0.25 };
	double Phase[4];
	for (int k = 0; k < 4; ++k)
		Phase[k] = 2.0 * PI * R.Unit();
	double Low = 1e9, High = -1e9;
	for (int t = 0; t < Ticks; ++t) {
		double Value = 0.0;
		for (int k = 0; k < 4; ++k)
			Value += Weights[k] * std::sin(2.0 * PI * Cycles[k] * t / Ticks + Phase[k]);
		Out[static_cast<size_t>(t)] = Value;
		Low = std::min(Low, Value);
		High = std::max(High, Value);
	}
	for (int t = 0; t < Ticks; ++t)
		Out[static_cast<size_t>(t)] = High > Low ? (Out[static_cast<size_t>(t)] - Low) / (High - Low) : 0.5;
	return Out;
}

double Glided(double x, int Glide)
{
	if (Glide == SFX_GLIDE_FAST)
		return 1.0 - (1.0 - x) * (1.0 - x);
	if (Glide == SFX_GLIDE_SLOW)
		return x * x;
	return x;
}

// A sounding stretch's loudness at tick t of Ticks, 0 to 1
double EnvelopeAt(int Envelope, int t, int Ticks)
{
	const double x = (t + 0.5) / std::max(1, Ticks);
	switch (Envelope) {
	case SFX_HIT: return std::pow(1.0 - x, 1.8);
	case SFX_FADE: return 1.0 - x;
	case SFX_SWELL: return x;
	case SFX_SWELL_FADE: return std::sin(PI * x);
	case SFX_DROPS: return 0.0;
	}
	return 1.0;
}

// A layer while it's worked out: values a tick, before they're rounded
struct Shape {
	SfxChannel Channel;
	int Start;
	std::vector<double> Volume, Pitch;
	std::vector<int> Duty;
	int Voice;		// a pulse's own (0, 1), -1 any
	int Pan;		// 8xx, -1 the middle
	Shape() : Channel(PULSE), Start(0), Voice(-1), Pan(-1) {}
	int End() const { return Start + static_cast<int>(Volume.size()); }
};

std::string SecondsText(double Seconds)
{
	char Text[32];
	std::snprintf(Text, sizeof(Text), "%.2f", Seconds);
	std::string Out = Text;
	while (!Out.empty() && Out[Out.size() - 1] == '0')
		Out.erase(Out.size() - 1);
	if (!Out.empty() && Out[Out.size() - 1] == '.')
		Out.erase(Out.size() - 1);
	return Out + " s";
}

const char *ChannelWord(SfxChannel Channel)
{
	return Channel == NOISE ? "noise" : (Channel == TRIANGLE ? "triangle" : "pulse");
}

// The layer's ticks from its start. Wraps: it reaches the end of a sound
// that loops, so its end goes on into its start.
Shape MakeShape(const SfxLayerDesign &D, int FrameRate, bool Wraps, Random &R)
{
	const bool Noise = D.Channel == NOISE;
	const int N = std::max(1, Round(D.Seconds * FrameRate));
	const int Repeats = std::max(1, std::min(256, D.Repeats));
	const double Gap = D.Gap < 0.0 ? (Repeats > 1 ? 0.25 : 0.0) : std::max(0.0, std::min(0.9, D.Gap));
	const double Loudest = std::max(1, std::min(15, D.Volume));
	const double Last = D.VolumeTo < 0 ? Loudest : std::max(0, std::min(15, D.VolumeTo));
	const double From = D.Pitch, To = D.PitchTo < 0.0 ? D.Pitch : D.PitchTo;
	Shape S;
	S.Channel = D.Channel;
	S.Start = std::max(0, Round(D.Start * FrameRate));
	S.Voice = D.Channel == PULSE && D.Voice >= 0 ? std::min(1, D.Voice) : -1;
	const double Pan = std::max(-1.0, std::min(1.0, D.Pan));
	S.Pan = std::fabs(Pan) < 1e-6 ? -1 : std::max(0, std::min(255, Round(128.0 + Pan * (Pan < 0.0 ? 128.0 : 127.0))));
	S.Volume.assign(static_cast<size_t>(N), 0.0);
	S.Pitch.assign(static_cast<size_t>(N), From);
	S.Duty.assign(static_cast<size_t>(N), Noise ? (D.Duty == 1 ? 1 : 0) : std::max(0, std::min(3, D.Duty)));	// noise: 1 is metallic

	// Each time it sounds: the pitch on its way, the envelope, then a gap
	for (int i = 0; i < Repeats; ++i) {
		const int a = Round(static_cast<double>(i) * N / Repeats), b = Round(static_cast<double>(i + 1) * N / Repeats);
		const int Length = b - a;
		if (Length <= 0)
			continue;
		const int On = Gap > 0.0 ? std::max(1, Round(Length * (1.0 - Gap))) : Length;
		const double Base = From + i * D.Step, Target = To + i * D.Step;
		for (int t = 0; t < Length; ++t) {
			const size_t Tick = static_cast<size_t>(a + t);
			if (t < On) {
				const double x = On > 1 ? static_cast<double>(t) / (On - 1) : 1.0;
				S.Pitch[Tick] = Base + (Target - Base) * Glided(x, D.Glide);
				S.Volume[Tick] = EnvelopeAt(D.Envelope, t, On);
				// A steady sound ends with a short fade, unless it goes on into the start
				const bool GoesOn = Wraps && b == N && On == Length;
				if (D.Envelope == SFX_STEADY && !GoesOn && On >= 4)
					S.Volume[Tick] *= t == On - 1 ? 0.4 : (t == On - 2 ? 0.7 : 1.0);
			}
			else
				S.Pitch[Tick] = Tick > 0 ? S.Pitch[Tick - 1] : Base;
		}
	}

	// Its loudness from start to end
	for (int t = 0; t < N; ++t)
		S.Volume[static_cast<size_t>(t)] *= Loudest + (Last - Loudest) * (N > 1 ? static_cast<double>(t) / (N - 1) : 0.0);

	// Gusts: louder and higher, softer and lower, unevenly
	if (D.Gusts > 0) {
		const std::vector<double> G = GustCurve(N, std::min(D.Gusts, 128), R);
		for (int t = 0; t < N; ++t) {
			const double g = G[static_cast<size_t>(t)];
			S.Volume[static_cast<size_t>(t)] *= 0.3 + 0.7 * g;
			S.Pitch[static_cast<size_t>(t)] += (g - 0.5) * (Noise ? 3.0 : 2.0);
		}
	}
	// A flutter and a vibrato, in whole cycles when the sound loops on
	const double Seconds = static_cast<double>(N) / FrameRate;
	if (D.Flutter > 0.0) {
		double Cycles = D.Flutter * Seconds;
		if (Wraps)
			Cycles = std::max(1.0, std::floor(Cycles + 0.5));
		for (int t = 0; t < N; ++t)
			S.Volume[static_cast<size_t>(t)] *= 1.0 - 0.6 * (0.5 - 0.5 * std::cos(2.0 * PI * Cycles * t / N));
	}
	if (D.Vibrato > 0.0) {
		double Cycles = std::max(0.1, D.VibratoHz) * Seconds;
		if (Wraps)
			Cycles = std::max(1.0, std::floor(Cycles + 0.5));
		for (int t = 0; t < N; ++t)
			S.Pitch[static_cast<size_t>(t)] += D.Vibrato * std::sin(2.0 * PI * Cycles * t / N);
	}
	// Drops: short blips here and there, the rest silent
	const double Level = std::max(1.0, Loudest);
	if (D.Envelope == SFX_DROPS) {
		const double Chance = 0.02 + 0.1 * (D.Crackle > 0.0 ? std::min(1.0, D.Crackle) : 0.5);
		for (int t = 0; t + 1 < N; ++t) {
			if (R.Unit() >= Chance)
				continue;
			const double Here = Level + (Last - Level) * (N > 1 ? static_cast<double>(t) / (N - 1) : 0.0);
			const double Offset = Noise ? std::floor(R.Unit() * 5.0) - 2.0 : std::floor(R.Unit() * 11.0) - 5.0;
			S.Volume[static_cast<size_t>(t)] = Here;
			S.Volume[static_cast<size_t>(t + 1)] = Here * 0.5;
			S.Pitch[static_cast<size_t>(t)] += Offset;
			S.Pitch[static_cast<size_t>(t + 1)] += Offset;
			t += 2;
		}
	}
	// Crackle: pops louder than the sound under them, as loud as it is there (a fading wash pops softer: a pop at the
	// layer's peak stood 6 dB over a wave's faded end). On the noise a pop moves a step or two either way and never above
	// CRACKLE_TOP: brighter, the 2A03's noise carries most of its power above 20 kHz, which the export filters out, and a
	// louder pop at noise pitch 13 came out no louder than the hiss under it (rain's drops were nearly inaudible)
	else if (D.Crackle > 0.0) {
		const double CRACKLE_TOP = 11.0;
		const double Chance = 0.03 + 0.15 * std::min(1.0, D.Crackle);
		for (int t = 0; t < N; ++t) {
			if (R.Unit() >= Chance)
				continue;
			const double Here = S.Volume[static_cast<size_t>(t)];
			const double Pop = std::min(15.0, 2.0 * std::max(1.0, Here) + 2.0);
			S.Volume[static_cast<size_t>(t)] = std::max(Here, Pop * (0.7 + 0.3 * R.Unit()));
			double &Pitch = S.Pitch[static_cast<size_t>(t)];
			if (Noise)
				Pitch = std::min(CRACKLE_TOP, Pitch + std::floor(R.Unit() * 5.0) - 2.0);
			else
				Pitch += 12.0;
		}
	}
	if (D.Rough)
		for (int t = 0; t < N; ++t)
			S.Pitch[static_cast<size_t>(t)] += (t % 2 == 0 ? 1.0 : -1.0) * (Noise ? 1.0 : 0.5);
	return S;
}

// Puts a layer into a lane of its channel after what's there, with silence between
void Append(Shape &Lane, const Shape &S)
{
	const double Pitch = S.Pitch.empty() ? (Lane.Pitch.empty() ? 0.0 : Lane.Pitch.back()) : S.Pitch.front();
	const int Duty = S.Duty.empty() ? 0 : S.Duty.front();
	while (Lane.End() < S.Start) {
		Lane.Volume.push_back(0.0);
		Lane.Pitch.push_back(Pitch);
		Lane.Duty.push_back(Duty);
	}
	Lane.Volume.insert(Lane.Volume.end(), S.Volume.begin(), S.Volume.end());
	Lane.Pitch.insert(Lane.Pitch.end(), S.Pitch.begin(), S.Pitch.end());
	Lane.Duty.insert(Lane.Duty.end(), S.Duty.begin(), S.Duty.end());
}

SfxLayer ToLayer(const Shape &S)
{
	SfxLayer L;
	L.channel = S.Channel;
	L.start = S.Start;
	const bool Noise = S.Channel == NOISE;
	for (size_t t = 0; t < S.Volume.size(); ++t) {
		L.volume.push_back(std::max(0, std::min(15, Round(S.Volume[t]))));
		L.pitch.push_back(Noise ? std::max(0.0, std::min(15.0, std::floor(S.Pitch[t] + 0.5))) : std::max(9.0, std::min(95.0, S.Pitch[t])));
		L.duty.push_back(S.Duty[t]);
	}
	L.voice = S.Voice;
	L.pan = S.Pan;
	return L;
}

}    // namespace

bool DesignSoundEffect(const std::vector<SfxLayerDesign> &Layers, double Seconds, int FrameRate, int Seed, bool Loop, int Pulses,
	SoundEffect &Out, std::string &Problem, int Longest)
{
	Out = SoundEffect();
	FrameRate = std::max(1, FrameRate);
	const int Most = Longest > 0 ? Longest : MaxSoundTicks(Loop);
	const double MostSeconds = static_cast<double>(Most) / FrameRate;
	if (Layers.empty()) {
		Problem = "The sound has no layers.";
		return false;
	}

	// How long it is: until its last layer ends, or as asked
	int Total = 0;
	for (size_t i = 0; i < Layers.size(); ++i) {
		const int N = std::max(1, Round(Layers[i].Seconds * FrameRate));
		if (N > Most) {
			Problem = "A layer can last at most " + SecondsText(MostSeconds) + (Loop ? " in a sound that loops" : "") + "; layer " +
				std::to_string(i + 1) + " lasts " + SecondsText(Layers[i].Seconds) + ".";
			return false;
		}
		Total = std::max(Total, std::max(0, Round(Layers[i].Start * FrameRate)) + N);
	}
	if (Seconds > 0.0)
		Total = std::max(Total, Round(Seconds * FrameRate));
	if (Total > Most) {
		Problem = "A sound can last at most " + SecondsText(MostSeconds) + (Loop ? " when it loops" : "") + "; these layers run to " +
			SecondsText(static_cast<double>(Total) / FrameRate) + ".";
		return false;
	}

	// Each layer, and an echo of a pulse's, a tenth of a second later and softer
	std::vector<Shape> Shapes;
	for (size_t i = 0; i < Layers.size(); ++i) {
		const SfxLayerDesign &D = Layers[i];
		Random R(static_cast<unsigned int>(Seed) * 1000u + static_cast<unsigned int>(i) + 1u);
		const bool Wraps = Loop && std::max(0, Round(D.Start * FrameRate)) + std::max(1, Round(D.Seconds * FrameRate)) >= Total;
		Shapes.push_back(MakeShape(D, FrameRate, Wraps, R));
		if (D.Echo && D.Channel == PULSE) {
			Shape Echo = Shapes.back();
			Echo.Start += std::max(1, Round(0.1 * FrameRate));
			for (size_t t = 0; t < Echo.Volume.size(); ++t)
				Echo.Volume[t] *= 0.45;
			for (size_t t = 0; t < Echo.Duty.size(); ++t)
				Echo.Duty[t] = Echo.Duty[t] == 2 ? 1 : 2;
			if (Echo.Voice >= 0)
				Echo.Voice = 1 - Echo.Voice;
			if (Echo.End() > Total) {
				if (Loop) {
					const size_t Keep = static_cast<size_t>(std::max(0, Total - Echo.Start));
					Echo.Volume.resize(Keep);
					Echo.Pitch.resize(Keep);
					Echo.Duty.resize(Keep);
				}
				else if (Echo.End() > Most) {
					Problem = "Layer " + std::to_string(i + 1) + "'s echo would run past the " + SecondsText(MostSeconds) +
						" a sound can last; end it a little sooner.";
					return false;
				}
			}
			if (!Echo.Volume.empty())
				Shapes.push_back(Echo);
		}
	}

	// Layers on the same kind of channel share one when they don't sound at once
	std::vector<size_t> Order(Shapes.size());
	for (size_t i = 0; i < Order.size(); ++i)
		Order[i] = i;
	std::stable_sort(Order.begin(), Order.end(), [&](size_t a, size_t b) { return Shapes[a].Start < Shapes[b].Start; });
	std::vector<Shape> Lanes;
	for (size_t o = 0; o < Order.size(); ++o) {
		const Shape &S = Shapes[Order[o]];
		int Found = -1, Same = 0;
		for (size_t l = 0; l < Lanes.size() && Found < 0; ++l) {
			if (Lanes[l].Channel != S.Channel)
				continue;
			++Same;
			// A pulse of its own only shares with that pulse's lane (or one free of any), and layers share at one pan
			const bool Voices = S.Voice < 0 || Lanes[l].Voice < 0 || S.Voice == Lanes[l].Voice;
			if (Voices && Lanes[l].Pan == S.Pan && Lanes[l].End() <= S.Start && S.End() - Lanes[l].Start <= Most)
				Found = static_cast<int>(l);
		}
		if (Found >= 0 && Lanes[static_cast<size_t>(Found)].Voice < 0)
			Lanes[static_cast<size_t>(Found)].Voice = S.Voice;
		if (Found < 0 && S.Voice >= 0)
			for (size_t l = 0; l < Lanes.size(); ++l)
				if (Lanes[l].Channel == PULSE && Lanes[l].Voice == S.Voice) {
					Problem = "A layer at " + SecondsText(static_cast<double>(S.Start) / FrameRate) + " asks for Pulse " +
						std::to_string(S.Voice + 1) + ", which another layer plays then; let one end before the next starts, or give "
						"it the other pulse.";
					return false;
				}
		if (Found >= 0) {
			Append(Lanes[static_cast<size_t>(Found)], S);
			continue;
		}
		const int Room = S.Channel == PULSE ? std::max(1, Pulses) : 1;
		if (Same >= Room) {
			const std::string At = SecondsText(static_cast<double>(S.Start) / FrameRate);
			if (S.Channel == PULSE)
				Problem = "More pulse layers sound at once (at " + At + ") than the song's " + std::to_string(Room) + " pulse channels; "
					"let one end before the next starts, or move one to the triangle.";
			else
				Problem = std::string("Two ") + ChannelWord(S.Channel) + " layers sound at once (at " + At + "); the 2A03 has one " +
					ChannelWord(S.Channel) + " channel, so let one end before the next starts.";
			return false;
		}
		Lanes.push_back(S);
	}
	// The pulses' lanes: those of a voice keep it, the others take the voices left, in order
	{
		bool Taken[2] = { false, false };
		for (size_t l = 0; l < Lanes.size(); ++l)
			if (Lanes[l].Channel == PULSE && Lanes[l].Voice >= 0)
				Taken[Lanes[l].Voice] = true;
		for (size_t l = 0; l < Lanes.size(); ++l)
			if (Lanes[l].Channel == PULSE && Lanes[l].Voice < 0 && (Taken[0] || Taken[1]))
				for (int v = 0; v < 2; ++v)
					if (!Taken[v]) {
						Lanes[l].Voice = v;
						Taken[v] = true;
						break;
					}
	}

	// The sound lasts its whole length: silence after the layers when it's longer
	size_t First = 0;
	for (size_t l = 1; l < Lanes.size(); ++l)
		if (Lanes[l].Start < Lanes[First].Start)
			First = l;
	Shape &Pad = Lanes[First];
	while (Pad.End() < Total) {
		Pad.Volume.push_back(0.0);
		Pad.Pitch.push_back(Pad.Pitch.empty() ? 0.0 : Pad.Pitch.back());
		Pad.Duty.push_back(Pad.Duty.empty() ? 0 : Pad.Duty.back());
	}
	for (size_t l = 0; l < Lanes.size(); ++l)
		Out.layers.push_back(ToLayer(Lanes[l]));
	return true;
}

std::string DescribeLayerDesign(const SfxLayerDesign &D)
{
	const bool Noise = D.Channel == NOISE;
	static const char *const ONCE[] = { "steady", "a hit", "fading", "swelling", "swelling and fading", "drops", "held" };
	static const char *const MANY[] = { "steady sounds", "hits", "fading sounds", "swells", "swells", "bursts of drops",
		"held sounds" };
	const int Envelope = std::max(0, std::min(6, D.Envelope));
	const bool Triangle = D.Channel == TRIANGLE;
	std::string Text = std::string(ChannelWord(D.Channel)) + (D.Channel == PULSE && D.Voice >= 0 ? " " + std::to_string(D.Voice + 1) :
		std::string()) + " from " + SecondsText(D.Start) + " for " + SecondsText(D.Seconds) + ": ";
	Text += D.Repeats > 1 ? std::to_string(D.Repeats) + " " + MANY[Envelope] : ONCE[Envelope];
	auto PitchText = [&](double Pitch) {
		return Noise ? "noise pitch " + std::to_string(Round(Pitch)) : PitchName(Round(Pitch));
	};
	Text += ", " + PitchText(D.Pitch);
	if (D.PitchTo >= 0.0 && Round(D.PitchTo) != Round(D.Pitch)) {
		Text += (Noise ? " to " + std::to_string(Round(D.PitchTo)) : " to " + PitchName(Round(D.PitchTo)));
		if (D.Glide == SFX_GLIDE_FAST)
			Text += " (quickly)";
		else if (D.Glide == SFX_GLIDE_SLOW)
			Text += " (late)";
	}
	if (D.Repeats > 1 && std::fabs(D.Step) > 1e-9) {
		char Step[48];
		std::snprintf(Step, sizeof(Step), ", each %g %s %s", std::fabs(D.Step), Noise ? (std::fabs(D.Step) == 1.0 ? "step" : "steps") :
			(std::fabs(D.Step) == 1.0 ? "semitone" : "semitones"), D.Step > 0 ? "higher" : "lower");
		Text += Step;
	}
	// The triangle has no volume: it sounds whole while it sounds, and stops at once
	if (Triangle)
		Text += ", at the triangle's one level";
	else {
		Text += ", volume " + std::to_string(std::max(1, std::min(15, D.Volume)));
		if (D.VolumeTo >= 0 && D.VolumeTo != D.Volume)
			Text += (D.VolumeTo < D.Volume ? " fading to " : " growing to ") + std::to_string(D.VolumeTo);
	}
	if (D.Gusts > 0)
		Text += ", " + std::to_string(D.Gusts) + (D.Gusts == 1 ? " gust" : " gusts");
	char Buffer[64];
	if (D.Flutter > 0.0) {
		std::snprintf(Buffer, sizeof(Buffer), ", a flutter %g times a second", D.Flutter);
		Text += Buffer;
	}
	if (D.Vibrato > 0.0) {
		std::snprintf(Buffer, sizeof(Buffer), ", vibrato of %g semitones %g times a second", D.Vibrato, D.VibratoHz);
		Text += Buffer;
	}
	if (D.Crackle > 0.0 && D.Envelope != SFX_DROPS)
		Text += ", crackling";
	if (D.Rough)
		Text += ", rough";
	if (D.Channel == PULSE) {
		static const char *const DUTIES[] = { "12.5%", "25%", "50%", "75%" };
		Text += std::string(", ") + DUTIES[std::max(0, std::min(3, D.Duty))] + " duty";
		if (D.Echo)
			Text += ", with an echo";
	}
	else if (Noise && D.Duty == 1)
		Text += ", metallic";
	if (std::fabs(D.Pan) >= 1e-6) {
		char Pan[64];
		std::snprintf(Pan, sizeof(Pan), ", panned %s%s", std::fabs(D.Pan) < 0.5 ? "a little " : (std::fabs(D.Pan) >= 0.99 ? "all the way " :
			""), D.Pan < 0 ? "left" : "right");
		Text += Pan;
	}
	return Text;
}

// Names

namespace {

// What a sound effect called Name would clash with: a track of that name, or
// an instrument of that name or of one of its layers ("SFX Jump (noise)")
std::string NameClash(const SongInfo &Song, const std::string &Name, bool &Track)
{
	const std::string Lower = Lowercase(Name);
	for (size_t t = 0; t < Song.TrackNames.size(); ++t) {
		if (Lowercase(Song.TrackNames[t]) == Lower) {
			Track = true;
			return Song.TrackNames[t];
		}
	}
	for (size_t i = 0; i < Song.Instruments.size(); ++i) {
		const std::string Instrument = Lowercase(Song.Instruments[i].Name);
		if (Instrument == Lower || Instrument.compare(0, Lower.size() + 2, Lower + " (") == 0) {
			Track = false;
			return Song.Instruments[i].Name;
		}
	}
	return std::string();
}

}	// namespace

std::string FreeSoundName(const SongInfo &Song, const std::string &Base)
{
	for (int n = 1; ; ++n) {
		const std::string Candidate = n == 1 ? Base : Base + " " + std::to_string(n);
		bool Track;
		if (NameClash(Song, Candidate, Track).empty())
			return Candidate;
	}
}

std::string SoundNameProblem(const SongInfo &Song, const std::string &Name)
{
	if (Name.find_first_not_of(" \t") == std::string::npos)
		return "The sound effect's name can't be empty.";
	bool Track = false;
	const std::string Clash = NameClash(Song, Name, Track);
	if (Clash.empty())
		return std::string();
	if (Track)
		return "There's a track called \"" + Clash + "\" already; give the sound another name.";
	return "There's an instrument called \"" + Clash + "\" already (from an earlier sound, perhaps), and FamiTracker would play "
		"the new sound with it; give the sound another name, or remove that instrument first.";
}

}	// namespace ftapi

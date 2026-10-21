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

SoundEffect Designed(const std::vector<SfxLayerDesign> &Layers, bool Loop, double Seconds = 0.0)
{
	SoundEffect Out;
	std::string Problem;
	DesignSoundEffect(Layers, Seconds, 60, 1, Loop, 2, Out, Problem);
	return Out;
}

SoundEffect Wind()		// a heavy wind: dark noise in uneven gusts, louder and brighter as each one blows
{
	SfxLayerDesign Air = Layer(NOISE, 0.0, 4.0, 6, -1, 10, SFX_STEADY);
	Air.Gusts = 3;
	return Designed({ Air }, true);
}

SoundEffect Rain()		// a soft, bright hiss with drops pattering on it, and now and then a drop plinking
{
	SfxLayerDesign Hiss = Layer(NOISE, 0.0, 4.0, 13, -1, 3, SFX_STEADY);
	Hiss.Crackle = 0.4;
	SfxLayerDesign Plinks = Layer(PULSE, 0.0, 4.0, 84, -1, 4, SFX_DROPS);
	Plinks.Crackle = 0.25;
	Plinks.Duty = 0;
	return Designed({ Hiss, Plinks }, true);
}

SoundEffect Fire()		// a dark, flickering roar with crackles popping out of it
{
	SfxLayerDesign Roar = Layer(NOISE, 0.0, 4.0, 3, -1, 5, SFX_STEADY);
	Roar.Gusts = 6;
	Roar.Crackle = 0.6;
	return Designed({ Roar }, true);
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

SoundEffect Birds()		// one bird tweets twice and trills, another answers lower and softer
{
	SfxLayerDesign Tweet = Layer(PULSE, 0.0, 0.25, 74, 81, 9, SFX_HIT);
	Tweet.Repeats = 2;
	Tweet.Glide = SFX_GLIDE_FAST;
	SfxLayerDesign Trill = Layer(PULSE, 0.35, 0.35, 83, 78, 8, SFX_HIT);
	Trill.Repeats = 5;
	SfxLayerDesign Again = Layer(PULSE, 1.1, 0.12, 74, 81, 7, SFX_HIT);
	Again.Glide = SFX_GLIDE_FAST;
	SfxLayerDesign Answer = Layer(PULSE, 0.55, 0.4, 71, 76, 6, SFX_SWELL_FADE);
	Answer.Repeats = 2;
	Answer.Duty = 1;
	return Designed({ Tweet, Trill, Again, Answer }, false);
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


SoundEffectKind Kind(const char *Name, const char *Use, std::vector<std::string> Words, SoundEffect (*Make)(), bool Loops = false)
{
	SoundEffectKind K;
	K.Name = Name;
	K.Use = Use;
	K.Words = Words;
	K.Make = Make;
	K.Loops = Loops;
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
	All.push_back(Kind("laser", "a falling zap (\"pew\"), for shooting", { "pew", "shoot", "shot", "zap", "blaster" }, Laser));
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
	All.push_back(Kind("wind", "a heavy wind blowing in gusts (loops, for ambience)", { "breeze", "gale", "gust", "gusts", "heavy wind",
		"howling wind", "windy", "blizzard" }, Wind, true));
	All.push_back(Kind("rain", "rain: a soft hiss with drops pattering (loops, for ambience)", { "rainfall", "drizzle", "downpour",
		"raining", "rainy", "shower" }, Rain, true));
	All.push_back(Kind("fire", "a fire flickering and crackling (loops, for ambience)", { "flames", "campfire", "fireplace", "burning",
		"crackle", "crackling", "torch", "bonfire" }, Fire, true));
	All.push_back(Kind("thunder", "a crack of thunder rumbling away", { "thunderclap", "lightning", "thunderstorm", "rumble",
		"storm" }, Thunder));
	All.push_back(Kind("birds", "two birds chirping, one answering the other", { "bird", "chirp", "chirps", "tweet", "birdsong",
		"songbird", "chirping" }, Birds));
	All.push_back(Kind("wings", "wings flapping away with a chirp, for a bird flying off", { "bird flying away", "flying away", "fly away",
		"flap", "flaps", "flapping", "take off", "takeoff", "flutter", "bat" }, Wings));
	All.push_back(Kind("engine", "an engine idling, a low throbbing hum (loops)", { "motor", "car", "machine", "engine hum", "vehicle",
		"idle", "truck", "machinery" }, Engine, true));
	All.push_back(Kind("alarm", "a siren whooping up again and again (loops)", { "siren", "alert", "klaxon", "emergency", "whoop" },
		Alarm, true));
	All.push_back(Kind("heartbeat", "a heartbeat, lub-dub once a second (loops), for low health or tension", { "heart", "heart beat",
		"heartbeats", "lub dub", "low health", "low hp" }, Heartbeat, true));
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

// Sounds described in words

SfxLayerDesign::SfxLayerDesign() : Channel(transcribe::SfxChannel::Pulse), Start(0.0), Seconds(0.5), Pitch(60.0), PitchTo(-1.0),
	Glide(SFX_GLIDE_EVEN), Volume(12), VolumeTo(-1), Envelope(SFX_STEADY), Repeats(1), Gap(-1.0), Step(0.0), Gusts(0),
	Flutter(0.0), Vibrato(0.0), VibratoHz(6.0), Crackle(0.0), Rough(false), Duty(2), Echo(false)
{
}

int MaxSoundTicks(bool Loop)
{
	return transcribe::SFX_MAX_TICKS - (Loop ? 1 : 0);
}

namespace {

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
	const int Repeats = std::max(1, std::min(64, D.Repeats));
	const double Gap = D.Gap < 0.0 ? (Repeats > 1 ? 0.25 : 0.0) : std::max(0.0, std::min(0.9, D.Gap));
	const double Loudest = std::max(1, std::min(15, D.Volume));
	const double Last = D.VolumeTo < 0 ? Loudest : std::max(0, std::min(15, D.VolumeTo));
	const double From = D.Pitch, To = D.PitchTo < 0.0 ? D.Pitch : D.PitchTo;
	Shape S;
	S.Channel = D.Channel;
	S.Start = std::max(0, Round(D.Start * FrameRate));
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
		const std::vector<double> G = GustCurve(N, std::min(D.Gusts, 32), R);
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
	// Crackle: pops louder and brighter than the sound under them
	else if (D.Crackle > 0.0) {
		const double Chance = 0.03 + 0.15 * std::min(1.0, D.Crackle);
		const double Pop = std::min(15.0, 2.0 * Loudest + 2.0);
		for (int t = 0; t < N; ++t) {
			if (R.Unit() >= Chance)
				continue;
			S.Volume[static_cast<size_t>(t)] = std::max(S.Volume[static_cast<size_t>(t)], Pop * (0.7 + 0.3 * R.Unit()));
			S.Pitch[static_cast<size_t>(t)] += Noise ? 3.0 + std::floor(R.Unit() * 4.0) : 12.0;
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
	return L;
}

}    // namespace

bool DesignSoundEffect(const std::vector<SfxLayerDesign> &Layers, double Seconds, int FrameRate, int Seed, bool Loop, int Pulses,
	SoundEffect &Out, std::string &Problem)
{
	Out = SoundEffect();
	FrameRate = std::max(1, FrameRate);
	const int Most = MaxSoundTicks(Loop);
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
			if (Lanes[l].End() <= S.Start && S.End() - Lanes[l].Start <= Most)
				Found = static_cast<int>(l);
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
	static const char *const ONCE[] = { "steady", "a hit", "fading", "swelling", "swelling and fading", "drops" };
	static const char *const MANY[] = { "steady sounds", "hits", "fading sounds", "swells", "swells", "bursts of drops" };
	const int Envelope = std::max(0, std::min(5, D.Envelope));
	std::string Text = std::string(ChannelWord(D.Channel)) + " from " + SecondsText(D.Start) + " for " + SecondsText(D.Seconds) + ": ";
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
	Text += ", volume " + std::to_string(std::max(1, std::min(15, D.Volume)));
	if (D.VolumeTo >= 0 && D.VolumeTo != D.Volume)
		Text += (D.VolumeTo < D.Volume ? " fading to " : " growing to ") + std::to_string(D.VolumeTo);
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

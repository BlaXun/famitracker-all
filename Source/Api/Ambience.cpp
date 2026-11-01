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

#include "Ambience.h"
#include <algorithm>
#include <cctype>
#include <cmath>

namespace ftapi {

namespace {

const AmbienceKind KINDS[AMBIENCE_KINDS] = {
	{ "waves", "the sea's waves: noise swelling in and sinking away, a little darker, every few seconds",
		"waves wave sea seas ocean oceans harbor harbour harbors bay shore shores beach beaches coast coastal surf tide tides "
		"dock docks pier piers boat boats seaside lighthouse lagoon wharf seashore" },
	{ "rain", "rain: a soft, bright hiss with drops ticking on it",
		"rain rainy raining rainfall drizzle drip drips dripping drops downpour shower showers puddle puddles" },
	{ "wind", "wind: dark noise blowing in slow gusts, a little brighter as each one blows",
		"wind winds windy breeze breezy gust gusts gale blizzard howling windswept" },
	{ "stream", "a stream: a light, bright hiss rippling quickly",
		"stream streams river rivers brook brooks creek creeks waterfall waterfalls rapids fountain fountains canal" },
	{ "fire", "a fire: a dark, low roar flickering, with crackles popping out of it",
		"fire fires fireplace campfire campfires hearth bonfire flames torch torches crackling crackle embers forge" },
};

std::string Lower(const std::string &Text)
{
	std::string Out = Text;
	for (size_t i = 0; i < Out.size(); ++i)
		Out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(Out[i])));
	return Out;
}

std::vector<std::string> WordsOf(const std::string &Text)
{
	std::vector<std::string> Out;
	std::string Word;
	for (size_t i = 0; i <= Text.size(); ++i) {
		const char c = i < Text.size() ? static_cast<char>(std::tolower(static_cast<unsigned char>(Text[i]))) : ' ';
		if (std::isalpha(static_cast<unsigned char>(c)))
			Word += c;
		else if (!Word.empty()) {
			Out.push_back(Word);
			Word.clear();
		}
	}
	return Out;
}

bool HasWord(const char *List, const std::string &Word)
{
	const std::vector<std::string> Words = WordsOf(List);
	return std::find(Words.begin(), Words.end(), Word) != Words.end();
}

// The same numbers for the same song (xorshift)
struct Dice {
	unsigned int State;
	explicit Dice(unsigned int Seed) : State(Seed == 0 ? 0x9E3779B9u : Seed) {}
	unsigned int Next()
	{
		State ^= State << 13;
		State ^= State >> 17;
		State ^= State << 5;
		return State;
	}
	double Unit() { return (Next() >> 8) / 16777216.0; }		// [0, 1)
	int Below(int n) { return n <= 1 ? 0 : static_cast<int>(Next() % static_cast<unsigned int>(n)); }
};

PresetSequence Sequence(int Type, const std::vector<int> &Items, int Loop = -1, int Setting = SEQUENCE_ABSOLUTE)
{
	PresetSequence S;
	S.Type = Type;
	S.Items = Items;
	S.Loop = Loop;
	S.Release = -1;
	S.Setting = Setting;
	return S;
}

Preset NoiseSound(const std::string &Name, const std::string &Description, const std::vector<int> &Volume, int Loop,
	const std::vector<int> &Arpeggio = std::vector<int>())
{
	Preset P;
	P.Name = Name;
	P.Description = Description;
	P.Drum = true;
	P.Vrc7Patch = 0;
	P.Sequences2A03.push_back(Sequence(SEQUENCE_VOLUME, Volume, Loop));
	if (!Arpeggio.empty())
		P.Sequences2A03.push_back(Sequence(SEQUENCE_ARPEGGIO, Arpeggio, Loop));
	P.Sequences2A03.push_back(Sequence(SEQUENCE_DUTY, std::vector<int>(1, 0)));	// the plain noise, never the metallic
	return P;
}

int Clamp(int Value, int Low, int High)
{
	return std::max(Low, std::min(High, Value));
}

// The frames of the waves' two sounds: swelling in (to its top, held until the next note) and sinking away to silence
const int WAVE_IN_FRAMES = 72, WAVE_OUT_FRAMES = 150;
// A wave every so many seconds at least (Bobber's came every 5.3 s, swelling 1.3 s and sinking 2 s): at a slow tempo a
// bar of 3.5 s holds one, a bigger and a smaller one in turn
const double WAVE_SECONDS = 3.2;
// The loops' sequences: a turn of the rain, the stream and the fire (their textures alike all through, so a note
// starting again isn't heard), and the most a wind's gust curve takes (FamiTracker's sequences hold 252 items)
const int TEXTURE_FRAMES = 240;

Preset WaveIn()
{
	std::vector<int> V;
	for (int t = 0; t < WAVE_IN_FRAMES; ++t)
		V.push_back(Clamp(static_cast<int>(std::floor(1.0 + 14.0 * std::pow(t / (WAVE_IN_FRAMES - 1.0), 1.4) + 0.5)), 1, 15));
	return NoiseSound("ambience waves in", "a wave swelling in over 1.2 s (ambience: waves)", V, -1);
}

Preset WaveOut()
{
	std::vector<int> V;
	for (int t = 0; t < WAVE_OUT_FRAMES; ++t)
		V.push_back(Clamp(static_cast<int>(std::floor(15.0 * std::pow(1.0 - t / (WAVE_OUT_FRAMES - 1.0), 1.6) + 0.5)), 0, 15));
	V.push_back(0);
	return NoiseSound("ambience waves out", "a wave sinking away over 2.5 s (ambience: waves)", V, -1);
}

Preset Rain(Dice &D)
{
	// A hiss at 6 of 15, with a drop about every third of a second: a tick of 13-15, falling away in two frames, a little
	// brighter (the sounds' sequences reach 15, so the volume column sets how loud the layer is)
	std::vector<int> V(static_cast<size_t>(TEXTURE_FRAMES), 6), A(static_cast<size_t>(TEXTURE_FRAMES), 0);
	for (int t = 2; t < TEXTURE_FRAMES - 3; t += 12 + D.Below(16)) {
		const int Top = 13 + D.Below(3);
		V[static_cast<size_t>(t)] = Top;
		V[static_cast<size_t>(t + 1)] = Top / 2 + 1;
		A[static_cast<size_t>(t)] = 2;
		A[static_cast<size_t>(t + 1)] = 1;
	}
	return NoiseSound("ambience rain", "rain: a soft hiss with drops ticking on it, looping (ambience)", V, 0, A);
}

Preset Stream(Dice &D)
{
	// A ripple: the level wandering between 8 and 15 a few frames at a time, the pitch a step either way
	std::vector<int> V, A;
	int Level = 11;
	for (int t = 0; t < TEXTURE_FRAMES; ++t) {
		if (t % 3 == 0)
			Level = Clamp(Level + 2 * (D.Below(3) - 1), 8, 15);
		V.push_back(Level);
		A.push_back(t % 5 == 0 ? D.Below(3) - 1 : (A.empty() ? 0 : A.back()));
	}
	V.back() = V.front();
	A.back() = A.front();
	return NoiseSound("ambience stream", "a stream rippling: a light hiss, looping (ambience)", V, 0, A);
}

Preset Fire(Dice &D)
{
	// A low roar flickering between 5 and 9, and a crackle about twice a second: a pop of 15, much brighter, gone at once
	std::vector<int> V, A;
	int Level = 7;
	for (int t = 0; t < TEXTURE_FRAMES; ++t) {
		if (t % 2 == 0)
			Level = 5 + 2 * D.Below(3);
		V.push_back(Level);
		A.push_back(0);
	}
	for (int t = 5; t < TEXTURE_FRAMES - 2; t += 18 + D.Below(24)) {
		V[static_cast<size_t>(t)] = 14 + D.Below(2);
		V[static_cast<size_t>(t + 1)] = 8;
		A[static_cast<size_t>(t)] = 9;
		A[static_cast<size_t>(t + 1)] = 5;
	}
	V.back() = V.front();
	return NoiseSound("ambience fire", "a fire's low roar flickering, with crackles, looping (ambience)", V, 0, A);
}

Preset Wind(int Frames, Dice &D)
{
	// Two or three slow gusts over the cycle, rising from 4 to about 15 and back, a little brighter at their top; it starts
	// and ends at its calm, so a note starting again on the next cycle doesn't jump
	const int Gusts = Frames >= 180 ? 3 : 2;
	std::vector<double> Curve(static_cast<size_t>(Frames), 0.0);
	for (int g = 0; g < Gusts; ++g) {
		const double Middle = (g + 0.5 + (D.Unit() - 0.5) * 0.4) / Gusts, Width = 0.55 / Gusts + D.Unit() * 0.25 / Gusts;
		const double Height = 0.6 + 0.4 * D.Unit();
		for (int t = 0; t < Frames; ++t) {
			const double x = (t / static_cast<double>(Frames) - Middle) / Width;
			if (std::fabs(x) < 1.0)
				Curve[static_cast<size_t>(t)] = std::max(Curve[static_cast<size_t>(t)], Height * std::pow(std::cos(x * 1.5707963), 2.0));
		}
	}
	std::vector<int> V, A;
	for (int t = 0; t < Frames; ++t) {
		const double c = Curve[static_cast<size_t>(t)];
		V.push_back(Clamp(static_cast<int>(std::floor(4.0 + 11.0 * c + 0.5)), 1, 15));
		A.push_back(static_cast<int>(std::floor(2.0 * c + 0.5)));
	}
	const std::string Name = Frames == TEXTURE_FRAMES ? std::string("ambience wind") : "ambience wind, " + std::to_string(Frames) +
		" frames";
	return NoiseSound(Name, "wind blowing in slow gusts (ambience)", V, Frames == TEXTURE_FRAMES ? 0 : -1, A);
}

}	// namespace

const AmbienceKind &AmbienceKindInfo(int Kind)
{
	return KINDS[Clamp(Kind, 0, AMBIENCE_KINDS - 1)];
}

std::string AmbienceKindNames()
{
	std::string Out;
	for (int k = 0; k < AMBIENCE_KINDS; ++k)
		Out += std::string(k > 0 ? ", " : "") + KINDS[k].Name;
	return Out;
}

bool ParseAmbienceKind(const std::string &Text, int &Kind)
{
	const std::string Asked = Lower(Text);
	for (int k = 0; k < AMBIENCE_KINDS; ++k)
		if (Asked == KINDS[k].Name) {
			Kind = k;
			return true;
		}
	const int Found = AmbienceOfWords(Text);
	if (Found < 0)
		return false;
	Kind = Found;
	return true;
}

int AmbienceOfWords(const std::string &Text, std::string *pWord)
{
	const std::vector<std::string> Words = WordsOf(Text);
	for (size_t w = 0; w < Words.size(); ++w)
		for (int k = 0; k < AMBIENCE_KINDS; ++k)
			if (HasWord(KINDS[k].Words, Words[w])) {
				if (pWord != 0)
					*pWord = Words[w];
				return k;
			}
	return -1;
}

bool IsAmbienceName(const std::string &InstrumentName)
{
	return Lower(InstrumentName).compare(0, 9, "ambience ") == 0;
}

AmbiencePlan PlanAmbience(int Kind, const std::vector<std::pair<int, int> > &Stretches, int RowsPerBar, double FramesPerRow,
	int FrameRate, unsigned int Seed)
{
	AmbiencePlan Plan;
	Plan.Kind = Clamp(Kind, 0, AMBIENCE_KINDS - 1);
	const int Bar = std::max(1, RowsPerBar);
	const double RowFrames = std::max(0.25, FramesPerRow), BarFrames = Bar * RowFrames;
	const double Rate = FrameRate > 0 ? FrameRate : 60;
	Dice D(Seed ^ (0x51ED27u * static_cast<unsigned int>(Plan.Kind + 1)));
	// Whole bars a cycle: the waves' at least WAVE_SECONDS, in 1, 2 or 4 bars (so a loop of 8 bars holds them whole);
	// the wind's as many as fit one sequence, at least one; the textures' a bar
	int Bars = 1;
	if (Plan.Kind == AMBIENCE_WAVES)
		while (Bars < 4 && Bars * BarFrames / Rate < WAVE_SECONDS)
			Bars *= 2;
	else if (Plan.Kind == AMBIENCE_WIND)
		while (Bars < 4 && (Bars * 2) * BarFrames <= TEXTURE_FRAMES)
			Bars *= 2;
	Plan.CycleBars = Bars;
	Plan.PerCycle = 1;
	Plan.CycleRows = Bars * Bar;
	int Each = Plan.CycleRows, Count = 1;	// rows from one note of a cycle to the next of its kind, and how many a cycle
	switch (Plan.Kind) {
	case AMBIENCE_WAVES: {
		Plan.Sounds.push_back(WaveIn());
		Plan.Sounds.push_back(WaveOut());
		Plan.SoundFrames = WAVE_IN_FRAMES + WAVE_OUT_FRAMES;
		// Two waves a cycle where the cycle holds two whole ones (a long bar at a slow tempo), each on whole rows
		while (Count < 4 && Plan.CycleRows % (Count * 2) == 0 && Plan.CycleRows / (Count * 2) * RowFrames / Rate >= WAVE_SECONDS)
			Count *= 2;
		Each = Plan.CycleRows / Count;
		Plan.PerCycle = Count;
		break;
	}
	case AMBIENCE_RAIN:
		Plan.Sounds.push_back(Rain(D));
		Plan.SoundFrames = TEXTURE_FRAMES;
		break;
	case AMBIENCE_WIND: {
		const int Frames = std::min(TEXTURE_FRAMES, std::max(24, static_cast<int>(std::floor(Plan.CycleRows * RowFrames + 0.5))));
		Plan.Sounds.push_back(Wind(Frames, D));
		Plan.SoundFrames = Frames;
		break;
	}
	case AMBIENCE_STREAM:
		Plan.Sounds.push_back(Stream(D));
		Plan.SoundFrames = TEXTURE_FRAMES;
		break;
	default:
		Plan.Sounds.push_back(Fire(D));
		Plan.SoundFrames = TEXTURE_FRAMES;
		break;
	}
	// The waves: each in at a pitch of 7-9 (a bigger one, then a smaller one), out two steps darker once it has swelled
	static const int WAVE_PITCHES[] = { 8, 7, 9, 8, 7, 8 };
	const int OutAfter = std::max(1, static_cast<int>(std::floor(WAVE_IN_FRAMES / RowFrames + 0.5)));
	static const int PITCHES[AMBIENCE_KINDS] = { 8, 12, 5, 10, 3 };
	int Wave = 0;
	for (size_t s = 0; s < Stretches.size(); ++s) {
		const int First = Stretches[s].first, End = Stretches[s].second;
		for (int Row = First; Row < End; Row += Each) {
			if (Plan.Kind == AMBIENCE_WAVES) {
				const int Pitch = WAVE_PITCHES[(Wave + D.Below(2)) % 6];
				const double Share = Wave % 2 == 0 ? 1.0 : 0.75;
				++Wave;
				const int Out = Row + std::min(OutAfter, Each - 1);
				AmbienceNote In = { Row, std::max(1, std::min(Out, End) - Row), Pitch, 0, Share };
				Plan.Notes.push_back(In);
				if (Out < End && Out > Row) {
					AmbienceNote Away = { Out, std::min(Row + Each, End) - Out, Pitch - 2, 1, Share };
					Plan.Notes.push_back(Away);
				}
			}
			else {
				AmbienceNote N = { Row, std::min(Row + Each, End) - Row, PITCHES[Plan.Kind], 0, 1.0 };
				Plan.Notes.push_back(N);
			}
		}
	}
	return Plan;
}

}	// namespace ftapi

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

#include "DpcmKit.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace ftapi {

namespace {

const double RATE = 33143.9;		// the DMC at rate 15 (NTSC)
const double PI = 3.14159265358979323846;

// A drum hit as the levels the DMC should follow: a body that rises from
// silence, rings at Freq (falling from FreqFrom) and decays, with noise on
// top, and settles back to silence at its end
struct Hit {
	double Seconds;
	double FreqFrom, Freq, FreqFall;	// Hz, and the time constant of the fall in seconds
	double Tone, ToneDecay;				// the body's amplitude in levels, and its decay in seconds
	double Noise, NoiseDecay;			// the noise's
	double Center;						// the level it swings around
};

std::vector<double> Levels(const Hit &h, unsigned int Seed)
{
	const int Count = static_cast<int>(h.Seconds * RATE);
	std::vector<double> Out(static_cast<size_t>(Count));
	double Phase = 0;
	unsigned int Random = Seed;
	double Held = 0;
	for (int i = 0; i < Count; ++i) {
		const double t = i / RATE;
		const double Left = (h.Seconds - t) / 0.03;		// the last 30 ms settle to silence
		const double Rise = std::min(1.0, t / 0.003);
		const double Center = h.Center * Rise * std::min(1.0, Left);
		const double Freq = h.Freq + (h.FreqFrom - h.Freq) * std::exp(-t / h.FreqFall);
		Phase += 2 * PI * Freq / RATE;
		double Level = Center + h.Tone * Rise * std::exp(-t / h.ToneDecay) * std::sin(Phase);
		if (h.Noise > 0) {
			if (i % 2 == 0) {		// noise held for two samples, so the steps can follow it
				Random = Random * 1103515245u + 12345u;
				Held = ((Random >> 16) & 0x7FFF) / 16383.5 - 1.0;
			}
			Level += h.Noise * Rise * std::exp(-t / h.NoiseDecay) * Held;
		}
		Out[static_cast<size_t>(i)] = std::max(0.0, std::min(127.0, Level));
	}
	return Out;
}

DpcmKit MakeKit()
{
	DpcmKit Kit;
	Kit.Name = "Sample drums";
	struct Drum { const char *Name, *SampleName; int Pitch; Hit Sound; };
	const Drum DRUMS[] = {
		{ "kick", "Kit kick", 36, { 0.14, 170, 48, 0.022, 34, 0.07, 0, 1, 36 } },
		{ "snare", "Kit snare", 38, { 0.16, 230, 185, 0.02, 16, 0.035, 34, 0.055, 40 } },
		{ "low tom", "Kit low tom", 41, { 0.18, 140, 82, 0.04, 30, 0.09, 4, 0.03, 34 } },
		{ "high tom", "Kit high tom", 45, { 0.15, 210, 125, 0.035, 28, 0.08, 4, 0.03, 34 } },
	};
	for (size_t d = 0; d < sizeof(DRUMS) / sizeof(DRUMS[0]); ++d) {
		DpcmSample Sample;
		Sample.Name = DRUMS[d].SampleName;
		Sample.Data = EncodeDpcm(Levels(DRUMS[d].Sound, 0x5EED + static_cast<unsigned int>(d) * 977u), 0);
		DpcmKey Key;
		Key.Drum = DRUMS[d].Name;
		Key.Pitch = DRUMS[d].Pitch;
		Key.Sample = static_cast<int>(Kit.Samples.size());
		Key.Rate = 15;
		Key.Delta = 0;		// from silence, which keeps the triangle and noise loud (the DMC's level quiets them)
		Kit.Samples.push_back(Sample);
		Kit.Keys.push_back(Key);
	}
	return Kit;
}

std::string Simple(const std::string &Text)
{
	std::string Out;
	for (size_t i = 0; i < Text.size(); ++i) {
		const unsigned char c = static_cast<unsigned char>(Text[i]);
		if (std::isalnum(c))
			Out += static_cast<char>(std::tolower(c));
	}
	return Out;
}

}	// namespace

std::vector<unsigned char> EncodeDpcm(const std::vector<double> &Wave, int Start)
{
	std::vector<unsigned char> Out;
	int Level = std::max(0, std::min(127, Start));
	unsigned char Byte = 0;
	int Bit = 0;
	for (size_t i = 0; i < Wave.size(); ++i) {
		// A step the counter can't take here (down from 0 or 1, up from 126 or 127) is a hold instead,
		// a step the other way that the next one takes back: a step lost here would be taken when the
		// DMC starts the sample from another level (a Zxx on its row), and the sample wouldn't end where
		// it started (the toms slid to silence, which clicked)
		bool Up = Wave[i] > Level;
		if (!Up && Level < 2)
			Up = true;
		else if (Up && Level > 125)
			Up = false;
		if (Up) {
			Byte = static_cast<unsigned char>(Byte | (1 << Bit));
			Level += 2;
		}
		else
			Level -= 2;
		if (++Bit == 8) {
			Out.push_back(Byte);
			Byte = 0;
			Bit = 0;
		}
	}
	// Back to the level it started from (a decaying drum still swings a little at its end), so the
	// level after the sound is the one before it and the next sound starts without a step
	const int Home = std::max(0, std::min(127, Start));
	while (Level != Home && Level != Home + 1 && Level != Home - 1) {
		if (Level < Home) {
			Byte = static_cast<unsigned char>(Byte | (1 << Bit));
			Level += 2;
		}
		else
			Level -= 2;
		if (++Bit == 8) {
			Out.push_back(Byte);
			Byte = 0;
			Bit = 0;
		}
	}
	if (Bit > 0) {		// the rest of a byte, holding the level
		for (; Bit < 8; ++Bit)
			if (Bit % 2 == 0)
				Byte = static_cast<unsigned char>(Byte | (1 << Bit));
		Out.push_back(Byte);
	}
	while (Out.empty() || Out.size() % 16 != 1)
		Out.push_back(0x55);	// up, down: the level holds
	return Out;
}

std::vector<int> DecodeDpcm(const std::vector<unsigned char> &Data, int Start)
{
	std::vector<int> Out;
	int Level = std::max(0, std::min(127, Start));
	for (size_t i = 0; i < Data.size(); ++i)
		for (int Bit = 0; Bit < 8; ++Bit) {
			if ((Data[i] >> Bit) & 1) {
				if (Level <= 125)
					Level += 2;
			}
			else if (Level >= 2)
				Level -= 2;
			Out.push_back(Level);
		}
	return Out;
}

const DpcmKit &SampleDrums()
{
	static const DpcmKit Kit = MakeKit();
	return Kit;
}

const DpcmKey *FindSampleDrum(const std::string &Name)
{
	static const char *const ALIASES[][2] = {
		{ "kick", "kick" }, { "bassdrum", "kick" }, { "bd", "kick" }, { "kickdrum", "kick" },
		{ "snare", "snare" }, { "sd", "snare" }, { "snaredrum", "snare" }, { "clap", "snare" },
		{ "lowtom", "low tom" }, { "tom", "low tom" }, { "floortom", "low tom" }, { "tom1", "low tom" },
		{ "hightom", "high tom" }, { "hitom", "high tom" }, { "tom2", "high tom" },
	};
	const std::string Wanted = Simple(Name);
	for (size_t i = 0; i < sizeof(ALIASES) / sizeof(ALIASES[0]); ++i) {
		if (Wanted != ALIASES[i][0])
			continue;
		const DpcmKit &Kit = SampleDrums();
		for (size_t k = 0; k < Kit.Keys.size(); ++k)
			if (Kit.Keys[k].Drum == ALIASES[i][1])
				return &Kit.Keys[k];
	}
	return 0;
}

}	// namespace ftapi

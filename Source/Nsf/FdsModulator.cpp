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

#include "FdsModulator.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace nsf {

namespace {

// A warble is faster than this and wider than WARBLE_CENTS either way (38's lead, speed 34 and depth 5: 14.5 times a
// second, -170 to +180 cents); a vibrato wider than DEEP_CENTS either way sounds out of tune; past AUDIO_RATE the modulator
// colours the sound
const double WARBLE_RATE = 8.0, WARBLE_CENTS = 40.0, DEEP_CENTS = 60.0, AUDIO_RATE = 30.0;
const int ENTRIES = 64;		// the table's 32 steps, each played twice

std::string Number(double Value)
{
	char Buffer[32];
	std::snprintf(Buffer, sizeof(Buffer), "%.*f", Value < 20.0 ? 1 : 0, Value);
	std::string Out = Buffer;
	if (Out.size() > 2 && Out.compare(Out.size() - 2, 2, ".0") == 0)
		Out.erase(Out.size() - 2);
	return Out;
}

// The bend of an entry with this bias, in 64ths of the pitch, as the emulator works it out: the bias times the depth, shifted
// down 4 bits, then rounded away from the note when the shifted value (rather than the product) has bits in its low
// nibble, then wrapped as the chip's 8 bits do
int Bend(int Bias, int Depth)
{
	const int Product = Bias * Depth;
	int Mod = Product >= 0 ? Product / 16 : -((-Product + 15) / 16);	// an arithmetic shift
	if ((Mod & 0x0F) != 0)
		Mod += Bias < 0 ? -1 : 2;
	if (Mod > 193)
		Mod -= 258;
	if (Mod < -64)
		Mod += 256;
	return Mod;
}

}	// namespace

FdsModMotion FdsModulatorMotion(int Speed, int Depth, const std::vector<int> &Table)
{
	static const int ADJUST[8] = { 0, 1, 2, 4, 0, -4, -2, -1 };
	FdsModMotion Out;
	if (Speed <= 0 || Depth <= 0 || Table.empty())
		return Out;
	// The bias entry by entry, from a note's start (the bias set to 0), its second turn kept: a table whose steps don't
	// add up to none drifts, and wraps as the chip's 7 bits do
	std::vector<int> Biases;
	int Bias = 0;
	for (int Turn = 0; Turn < 2; ++Turn)
		for (int e = 0; e < ENTRIES; ++e) {
			const int Value = Table[static_cast<size_t>(e / 2) % Table.size()] & 7;
			Bias = Value == 4 ? 0 : Bias + ADJUST[Value];
			while (Bias > 63)
				Bias -= 128;
			while (Bias < -64)
				Bias += 128;
			if (Turn == 1)
				Biases.push_back(Bias);
		}
	double Sum = 0.0;
	Out.Low = 1e9;
	Out.High = -1e9;
	for (size_t e = 0; e < Biases.size(); ++e) {
		const double Cents = 1200.0 * std::log2(std::max(0.01, 1.0 + Bend(Biases[e], Depth) / 64.0));
		Out.Low = std::min(Out.Low, Cents);
		Out.High = std::max(Out.High, Cents);
		Sum += Cents;
	}
	Out.Mean = Sum / static_cast<double>(Biases.size());
	Out.Moves = Out.High - Out.Low > 0.5;
	if (!Out.Moves) {
		Out.Low = Out.High = Out.Mean;
		return Out;
	}
	// How often it goes round: the table's turn, or the shortest stretch its bias repeats in (a table of two swings
	// wobbles twice a turn)
	int Period = ENTRIES;
	for (int p = 1; p < ENTRIES; ++p) {
		if (ENTRIES % p != 0)
			continue;
		bool Repeats = true;
		for (int e = 0; e < ENTRIES && Repeats; ++e)
			Repeats = Biases[static_cast<size_t>(e)] == Biases[static_cast<size_t>((e + p) % ENTRIES)];
		if (Repeats) {
			Period = p;
			break;
		}
	}
	Out.Rate = 1789773.0 * Speed / 4194304.0 * (static_cast<double>(ENTRIES) / Period);
	const double Half = (Out.High - Out.Low) / 2.0;
	Out.Audio = Out.Rate > AUDIO_RATE;
	Out.Warble = !Out.Audio && Out.Rate > WARBLE_RATE && Half > WARBLE_CENTS;
	Out.Deep = !Out.Audio && !Out.Warble && Half > DEEP_CENTS;
	return Out;
}

std::string CentsInWords(double Cents)
{
	Cents = std::fabs(Cents);
	if (Cents < 12.0)
		return "a few cents";
	if (Cents < 37.5)
		return "about a quarter of a semitone";
	if (Cents < 62.5)
		return "about half a semitone";
	if (Cents < 87.5)
		return "about three quarters of a semitone";
	if (Cents < 125.0)
		return "about a semitone";
	if (Cents < 175.0)
		return "about a semitone and a half";
	if (Cents < 250.0)
		return "about two semitones";
	char Buffer[48];
	std::snprintf(Buffer, sizeof(Buffer), "about %d semitones", static_cast<int>(std::floor(Cents / 100.0 + 0.5)));
	return Buffer;
}

std::string TimesInWords(double PerSecond)
{
	if (PerSecond <= 0.0)
		return "unevenly";
	if (PerSecond < 0.75) {
		char Buffer[64];
		std::snprintf(Buffer, sizeof(Buffer), "about once every %d seconds", static_cast<int>(std::floor(1.0 / PerSecond + 0.5)));
		return Buffer;
	}
	if (PerSecond < 1.5)
		return "about once a second";
	return Number(PerSecond) + " times a second";
}

std::string FdsModulatorWords(int Speed, int Depth, const std::vector<int> &Table, int Delay, int FrameRate)
{
	const FdsModMotion M = FdsModulatorMotion(Speed, Depth, Table);
	if (!M.Moves)
		return "off (it doesn't move the pitch)";
	char Buffer[256];
	std::snprintf(Buffer, sizeof(Buffer), "%s %s, from %d cents %s to %d %s (%s either way)", M.Audio ? "it moves the pitch" :
		M.Warble ? "it shakes the pitch" : "it swings the pitch", TimesInWords(M.Rate).c_str(),
		static_cast<int>(std::floor(std::fabs(M.Low) + 0.5)), M.Low < 0.0 ? "down" : "up", static_cast<int>(std::floor(std::fabs(M.High) + 0.5)),
		M.High < 0.0 ? "down" : "up", CentsInWords((M.High - M.Low) / 2.0).c_str());
	std::string Out = M.Audio ? std::string("a growl or shimmer in the sound, not a vibrato: ") + Buffer + ", too fast to hear as a pitch" :
		M.Warble ? std::string("a warble, not a vibrato: ") + Buffer + ", so each note sounds like a fluttering echo" :
		M.Deep ? std::string("a deep wobble, wider than a vibrato: ") + Buffer :
		M.Rate < 2.0 ? std::string("a slow bend: ") + Buffer : std::string("a vibrato: ") + Buffer;
	if (std::fabs(M.Mean) >= 15.0 && !M.Audio) {
		std::snprintf(Buffer, sizeof(Buffer), "; on average %d cents %s", static_cast<int>(std::floor(std::fabs(M.Mean) + 0.5)),
			M.Mean < 0.0 ? "flat" : "sharp");
		Out += Buffer;
	}
	if (Delay > 0 && FrameRate > 0) {
		std::snprintf(Buffer, sizeof(Buffer), "; from %d frames (%s s) into each note", Delay, Number(static_cast<double>(Delay) / FrameRate).c_str());
		Out += Buffer;
	}
	return Out;
}

}	// namespace nsf

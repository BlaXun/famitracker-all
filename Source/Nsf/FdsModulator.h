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

// What the FDS's modulator does to a note's pitch, in plain words: how many
// times a second it moves and how far it bends, as FamiTracker 0.4.6's
// emulator (APU/FDSSound.cpp) bends it. Its table of 32 steps (each played
// twice, every entry moving the bias by 0, 1, 2 or 4 either way, or back to
// none) goes round 1789773 x speed / 2^22 times a second, and each entry
// bends the note by the bias times the depth over 16, rounded away from the
// note after the shift, in 64ths of its pitch. So speed 34 with depth 5, on
// a table that swings the bias between -16 and 16, shakes the pitch from 170
// cents down to 180 up 14.5 times a second: a warble, which a composer took
// for a slow vibrato. Shared by the NSF importer's studies and the Claude
// Code tools; plain C++14.

#include <string>
#include <vector>

namespace nsf {

struct FdsModMotion {
	bool Moves;				// it bends the pitch at all (speed and depth over 0, a table that moves the bias)
	double Rate;			// times a second the bend goes round (the table's turn, or more when it repeats inside it)
	double Low, High;		// the bend's lowest and highest, in cents
	double Mean;			// on average, in cents
	bool Warble;			// faster than 8 times a second and wider than 40 cents either way: a warble, not a vibrato
	bool Deep;				// slower, but wider than 60 cents either way: too deep for a vibrato
	bool Audio;				// over 30 times a second: it colours the sound (a growl, a shimmer) rather than moving its pitch
	FdsModMotion() : Moves(false), Rate(0.0), Low(0.0), High(0.0), Mean(0.0), Warble(false), Deep(false), Audio(false) {}
};

// What a modulator does with this speed (0-4095), depth (0-63) and table (32 steps, 0-7)
FdsModMotion FdsModulatorMotion(int Speed, int Depth, const std::vector<int> &Table);

// The same in plain words, for the studies and the tools: "a warble, not a vibrato: it shakes the pitch 14.5 times a
// second, from 170 cents down to 180 up (about a semitone and a half either way)", with the frames it waits (Delay) as
// time at FrameRate ticks a second; "off" when it doesn't move the pitch
std::string FdsModulatorWords(int Speed, int Depth, const std::vector<int> &Table, int Delay, int FrameRate);

// Pitch distances and rates in words, as the listen check says them too (ListenCentsWords): "about a semitone and a
// half", "14 times a second"
std::string CentsInWords(double Cents);
std::string TimesInWords(double PerSecond);

}	// namespace nsf

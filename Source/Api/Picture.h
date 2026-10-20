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

// The song's notes as a picture for Claude to look at (the look_at_song
// tool): a piano roll with time from left to right and pitch from bottom to
// top, each channel in its own color, the drums (noise) and samples (DPCM)
// in strips below. Bars are numbered on top, each C is labeled on the left.
// Plain C++14.

#include <string>
#include <vector>
#include "Music.h"
#include "Png.h"

namespace ftapi {

struct PictureChannel {
	int Color;						// 0 to PictureColors() - 1
	int Kind;						// channel_kind
	std::vector<NoteInfo> Notes;	// from the song's first row
};

struct PictureSpec {
	int First, End;					// song rows shown, End excluded
	Timing Time;
	std::vector<bool> Played;		// per song row: does it ever play (empty: all do)
	int LoopRow;					// where the song loops back to, -1 for no mark
	int EndRow;						// where it jumps back or halts (after the last row played), -1 for no mark

	PictureSpec(const Timing &T) : First(0), End(0), Time(T), LoopRow(-1), EndRow(-1) {}
};

struct Picture {
	int Width, Height;
	std::vector<unsigned char> Pixels;	// palette indices, rows from the top
	std::vector<Rgb> Palette;
	int LowPitch, HighPitch;			// the pitches the roll shows
	bool DrumStrip, SampleStrip;

	Picture() : Width(0), Height(0), LowPitch(0), HighPitch(0), DrumStrip(false), SampleStrip(false) {}
	std::string Png() const { return EncodePng(Width, Height, Pixels, Palette); }
};

Picture DrawNotes(const std::vector<PictureChannel> &Channels, const PictureSpec &Spec);

int PictureColors();
const char *PictureColorName(int Color);	// "red", "blue", ...

}	// namespace ftapi

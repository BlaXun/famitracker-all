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

// A track of a module played the way FamiTracker 0.4.6 plays it, for the
// parts the NSF importer writes: speed and tempo, frames with Bxx, Dxx and
// Cxx, notes, halts and releases, the volume column, instruments' macros
// with their loops and release points, and the effects 0xy, Gxx, Sxx, Hxy,
// Ixy, Vxx, Pxx, 4xy and Fxx, on the 2A03, VRC6 and MMC5 channels. Its
// register writes go through the same chip model as an NSF's, so a
// converted track can be compared with the NSF it came from, tick by tick.
// Follows CSoundGen and CChannelHandler's code; used by the tests.

#include "Module.h"
#include "Player.h"

#include <string>

namespace nsf {

bool SimulateTrack(const Module &M, int Track, int Ticks, Recording &Out, std::string &Error);

// How closely two recordings agree over Ticks ticks (the second shifted by
// Offset): the share of ticks, over all channels, where both sound or both
// are silent, where sounding ones have the same note, and the volume's
// average difference where both sound
struct Agreement {
	double Sound, Pitch, Volume;
	int Ticks;
	Agreement() : Sound(0), Pitch(0), Volume(0), Ticks(0) {}
};
Agreement Compare(const Recording &A, const Recording &B, int Ticks, int Offset, int Channel = -1);
// The offset (within ±Range ticks) at which B lines up best with A
int BestOffset(const Recording &A, const Recording &B, int Ticks, int Range);

}	// namespace nsf

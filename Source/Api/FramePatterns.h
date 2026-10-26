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

// The pattern a frame put into a track's frame list plays in a channel, as
// CFamiTrackerDoc::InsertFrame picks it: the first that no other frame plays
// and that holds nothing. Frames: the patterns the channel's frames play, the
// new one's place (Frame) among them, which doesn't count: it may still name
// the pattern a frame played before (one taken out, or a change undone), and
// 0.4.6 counted it as in use, so frames put in at the end one after another
// got their patterns in swapped pairs (frame 1 on 02, frame 2 on 01, then 04
// and 03). Empty(p): whether pattern p holds nothing. -1 when none is free.
// Plain C++14, shared by FamiTracker and its tests.

#include <cstddef>
#include <vector>

namespace ftapi {

template <class EmptyTest>
int FreePatternForFrame(const std::vector<int> &Frames, int Frame, int Patterns, EmptyTest Empty)
{
	for (int p = 0; p < Patterns; ++p) {
		bool Taken = false;
		for (size_t f = 0; f < Frames.size() && !Taken; ++f)
			Taken = static_cast<int>(f) != Frame && Frames[f] == p;
		if (!Taken && Empty(p))
			return p;
	}
	return -1;
}

}	// namespace ftapi

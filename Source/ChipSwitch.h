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

#include <vector>	// the files including this one include it before stdafx.h (see ChipSwitch.cpp)

class CFamiTrackerDoc;

// Switching the expansion chip for Claude Code's set_expansion tool
// (McpConnection.cpp). The new chip's channels take the old one's place, so
// the channels that go are emptied in every track first, and the effects on
// them that steer the whole song (speed and tempo, jumps, skips, halts) move
// to a channel that stays, so the song still loops and ends as it did.
// Channels are column indices in the module, as the document's pattern
// functions take them. Source/Tests checks this against a stand-in document.

// An effect that steers the song, and where it was
struct song_effect_t {
	unsigned int Track, Frame, Row;
	unsigned char Number, Param;
};

// The notes (not releases or halts) in the channels in every track, counted
// frame by frame as they play; Tracks gets the tracks that have some (from 0)
int CountChannelNotes(const CFamiTrackerDoc *pDoc, const std::vector<unsigned int> &Channels, std::vector<int> &Tracks);

// The effects that steer the song in the channels' effect columns that play
void GatherSongEffects(const CFamiTrackerDoc *pDoc, const std::vector<unsigned int> &Channels, std::vector<song_effect_t> &Effects);

// Empties the channels in every track: linked clip copies in them become
// plain notes, every pattern is cleared and every frame plays pattern 00
void EmptyChannels(CFamiTrackerDoc *pDoc, const std::vector<unsigned int> &Channels);

// Writes the effects into Channel, each in a free effect column of its row
// (more columns show when needed, emptied first, since hidden columns may hold
// old effects). A frame whose pattern other frames play with other cells gets
// a pattern of its own. Returns how many effects found no place: every column
// of the row was taken, or no pattern was free.
int PlaceSongEffects(CFamiTrackerDoc *pDoc, unsigned int Channel, const std::vector<song_effect_t> &Effects);

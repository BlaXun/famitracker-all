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

// Switching the expansion chips, for Module > Module properties and Claude
// Code's set_expansion tool (McpConnection.cpp). A module can have any of the
// expansion chips on (SNDCHIP_* bits); its channels are the 2A03's and then
// every chip's, in chan_id_t order. When the chips change, the channels of the
// chips that stay keep their notes and move to their new columns
// (CFamiTrackerDoc::SelectExpansionChip, with ChannelSources), and the
// channels of chips switched off go: their notes are counted first so the user
// can be asked, and the effects on them that steer the whole song (speed and
// tempo, jumps, skips, halts) move to a channel that stays, so the song still
// loops and ends as it did. Channels are column indices in the module, as the
// document's pattern functions take them. Source/Tests checks this against a
// stand-in document.

// The channel types (chan_id_t) of a module with the expansion chips Chips
// and NamcoChannels N163 channels (1-8), in the order of its columns: the
// 2A03's, then those of every chip that's on, in chan_id_t order (the MMC5's
// third, CHANID_MMC5_VOICE, has no channel). CSoundGen::RegisterChannels
// registers these, then the N163 channels past the count.
std::vector<int> ChipChannels(unsigned char Chips, unsigned int NamcoChannels);

// For each of the new channels (NewTypes, in column order), the column of the
// old ones (OldTypes) that has its type, -1 when none does
std::vector<int> ChannelSources(const std::vector<int> &OldTypes, const std::vector<int> &NewTypes);

// The module's channels (column indices) that a switch to Chips and
// NamcoChannels takes away: those whose type the new channels don't have
std::vector<unsigned int> LeavingChannels(const CFamiTrackerDoc *pDoc, unsigned char Chips, unsigned int NamcoChannels);

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

// Switches the module to the expansion chips Chips with NamcoChannels N163
// channels (when the N163 is on): the effects that steer the song move from
// the channels that go (LeavingChannels) to the module's last channel after
// the switch, those channels are emptied, and the others keep their notes in
// their new columns. Moved gets how many effects moved; returns how many found
// no place. The caller asks the user first (CountChannelNotes) and stops the
// player; FamiTracker can't undo it.
int SwitchChips(CFamiTrackerDoc *pDoc, unsigned char Chips, unsigned int NamcoChannels, int &Moved);

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

// NSF files: the music of an NES game as its own player code, with a
// subroutine to start each track (INIT) and one to call every frame (PLAY).
// Reads NSF (versions 1 and 2, with NSF2's metadata) and NSFe, which adds
// track names and lengths.

#include <cstdint>
#include <string>
#include <vector>

namespace nsf {

// The expansion sound chips an NSF uses (byte $7B of the header)
enum chip_flag {
	CHIP_VRC6 = 0x01,
	CHIP_VRC7 = 0x02,
	CHIP_FDS = 0x04,
	CHIP_MMC5 = 0x08,
	CHIP_N163 = 0x10,
	CHIP_5B = 0x20,
};

struct NsfFile {
	bool Nsfe;					// read from an NSFe file
	int Version;				// 1 or 2 for NSF
	std::string Title, Artist, Copyright, Ripper;
	int Songs;					// how many tracks
	int StartSong;				// the first to play, from 0
	uint16_t LoadAddress, InitAddress, PlayAddress;
	int SpeedNtsc, SpeedPal;	// microseconds between PLAY calls
	bool Pal;					// made for PAL machines
	bool Dual;					// plays on both
	int Chips;					// chip_flag
	bool Bankswitched;
	uint8_t Banks[8];			// the banks at $8000-$FFFF to start with
	std::vector<uint8_t> Data;	// program and music, loaded at LoadAddress

	// From NSFe, or NSF2's metadata; empty or shorter than Songs when missing
	std::vector<std::string> TrackNames;
	std::vector<int> TrackTimes;	// milliseconds, -1 when not known
	std::vector<int> TrackFades;
	std::vector<int> Playlist;		// tracks in the order to play them

	NsfFile();
	std::string TrackName(int Track) const;	// "" when it has none
	int TrackTime(int Track) const;			// milliseconds, -1 when not known
	double PlayRate() const;				// PLAY calls a second
	std::string ChipNames() const;			// "VRC6, N163", "" for none
};

// Parses an NSF or NSFe file; false with the reason when it isn't one
bool ParseNsf(const std::vector<uint8_t> &Bytes, NsfFile &Out, std::string &Error);
// Reads one, with the track names of a playlist of the same name next to it
// (music.nsf and music.m3u), when the file has none of its own
bool ReadNsfFile(const std::string &Path, NsfFile &Out, std::string &Error);

// Track names and lengths from a playlist made for the NSF, as NSF players
// read them: lines like "music.nsf::NSF,12,Intro - Part 1,0:00:10.753,,0:00:04.778"
// (the track, its name, its length, the loop and the fade). Tracks count from
// 1, unless one of them is 0. Names the file already has stay. Returns how
// many tracks the playlist named.
int ApplyPlaylist(const std::string &Text, NsfFile &File);

}	// namespace nsf

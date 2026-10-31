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

// A song's seed (feat-cozy-kind): a number made once when the song is
// created (File > New, new_song, or what_next on an empty song without one),
// saved with it in a BF SEED block, so what_next's picks from a kind's pools,
// song_shape's variant and grow_melody's first version differ between songs
// whose names and arguments are alike, and stay the same for one song across
// sessions. 0 is no seed: the kinds' own picks, as before seeds. Plain C++14.

#include <cstddef>
#include <string>
#include <vector>

namespace ftapi {

class SongSeedMark {
public:
	static const char *const BLOCK_ID;		// "BF SEED"
	static const int BLOCK_VERSION = 1;

	// The block's data: the seed as an int32 (write it only for a seed other than 0)
	static std::vector<unsigned char> Write(unsigned int Seed);
	// The seed in a block: exactly its int32, of version 1, other than 0; a damaged block, another version or 0
	// gives 0 (no seed: the song opens with the kinds' own picks)
	static unsigned int Read(const unsigned char *pData, size_t Size, int Version);
};

// A new seed, never 0, from what changes from moment to moment (Entropy: the clock, a counter, an address)
unsigned int NewSongSeed(unsigned long long Entropy);
// A seed mixed with a roll asked for (what_next's and song_shape's seed: 1 or none is the song's own, 2 and on others)
unsigned int RolledSeed(unsigned int SongSeed, int Roll);
// FNV-1a over a salt, a name's words (lowercase letters and digits) and a seed, mixed at the end: the same song gets
// the same pick on every computer, and each salt its own
unsigned int SeededHash(const char *Salt, const std::string &Name, unsigned int Seed);

}	// namespace ftapi

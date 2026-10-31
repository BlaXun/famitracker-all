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

#include "SongSeed.h"
#include <cctype>

namespace ftapi {

const char *const SongSeedMark::BLOCK_ID = "BF SEED";

std::vector<unsigned char> SongSeedMark::Write(unsigned int Seed)
{
	std::vector<unsigned char> Out;
	for (int i = 0; i < 4; ++i)
		Out.push_back(static_cast<unsigned char>((Seed >> (8 * i)) & 0xFF));
	return Out;
}

unsigned int SongSeedMark::Read(const unsigned char *pData, size_t Size, int Version)
{
	if (Version != BLOCK_VERSION || pData == 0 || Size != 4)
		return 0;
	unsigned int Value = 0;
	for (int i = 0; i < 4; ++i)
		Value |= static_cast<unsigned int>(pData[i]) << (8 * i);
	return Value;
}

namespace {

unsigned int Mix(unsigned int Hash)
{
	Hash ^= Hash >> 15;
	Hash *= 2246822519u;
	Hash ^= Hash >> 13;
	Hash *= 3266489917u;
	Hash ^= Hash >> 16;
	return Hash;
}

}	// namespace

unsigned int NewSongSeed(unsigned long long Entropy)
{
	static unsigned int Count = 0;
	const unsigned int Seed = Mix(static_cast<unsigned int>(Entropy) ^ Mix(static_cast<unsigned int>(Entropy >> 32) + 0x9E3779B9u *
		++Count));
	return Seed != 0 ? Seed : 1;
}

unsigned int RolledSeed(unsigned int SongSeed, int Roll)
{
	if (Roll <= 1)
		return SongSeed;
	const unsigned int Seed = Mix(SongSeed ^ Mix(0x85EBCA6Bu * static_cast<unsigned int>(Roll)));
	return Seed != 0 ? Seed : 1;
}

unsigned int SeededHash(const char *Salt, const std::string &Name, unsigned int Seed)
{
	unsigned int Hash = 2166136261u;
	auto Add = [&](unsigned char c) {
		Hash ^= c;
		Hash *= 16777619u;
	};
	for (const char *p = Salt; *p; ++p)
		Add(static_cast<unsigned char>(*p));
	Add(':');
	for (size_t i = 0; i < Name.size(); ++i)
		if (std::isalnum(static_cast<unsigned char>(Name[i])))
			Add(static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(Name[i]))));
	for (int b = 0; b < 4; ++b)
		Add(static_cast<unsigned char>(Seed >> (8 * b)));
	return Mix(Hash);
}

}	// namespace ftapi

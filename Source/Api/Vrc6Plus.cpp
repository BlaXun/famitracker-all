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

#include "Vrc6Plus.h"
#include "Song.h"
#include "Wavetables.h"

#include <algorithm>
#include <cctype>

namespace ftapi {

namespace {

std::string Lower(const std::string &Text)
{
	std::string Out(Text);
	for (size_t i = 0; i < Out.size(); ++i)
		Out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(Out[i])));
	return Out;
}

}	// namespace

const char *const Vrc6PlusMark::BLOCK_ID = "BF VRC6PLUS";

std::string SoftChannelName(int Index)
{
	return "Soft " + std::to_string(Index + 1);
}

std::string SoftChannelOldName(int Index)
{
	return "Namco " + std::to_string(Index + 1);
}

std::vector<unsigned char> Vrc6PlusMark::Write()
{
	std::vector<unsigned char> Out;
	const unsigned int Value = static_cast<unsigned int>(VRC6_PLUS_SOFT_CHANNELS);
	for (int i = 0; i < 4; ++i)
		Out.push_back(static_cast<unsigned char>((Value >> (8 * i)) & 0xFF));
	return Out;
}

bool Vrc6PlusMark::Read(const unsigned char *pData, size_t Size, int Version)
{
	if (Version != BLOCK_VERSION || pData == 0 || Size != 4)
		return false;
	unsigned int Value = 0;
	for (int i = 0; i < 4; ++i)
		Value |= static_cast<unsigned int>(pData[i]) << (8 * i);
	return Value == static_cast<unsigned int>(VRC6_PLUS_SOFT_CHANNELS);
}

const std::vector<std::string> &SoftLayerSounds()
{
	static const std::vector<std::string> SOUNDS = { "choir pad", "string pad", "soft flute", "sub bass" };
	return SOUNDS;
}

bool IsSoftLayerSound(const std::string &Name)
{
	const std::string Wanted = Lower(Name);
	const std::vector<std::string> &All = SoftLayerSounds();
	return std::find(All.begin(), All.end(), Wanted) != All.end();
}

std::vector<std::pair<const WaveSound *, int> > MissingSoftSounds(const std::vector<InstrumentInfo> &Instruments)
{
	std::vector<std::pair<int, int> > Used;
	std::vector<std::string> Have;
	for (size_t i = 0; i < Instruments.size(); ++i) {
		if (Instruments[i].Chip != "N163")
			continue;
		Have.push_back(Lower(Instruments[i].Name));
		if (Instruments[i].WaveSize > 0)
			Used.push_back(std::make_pair(Instruments[i].WavePos, Instruments[i].WaveSize));
	}
	// FamiTracker 0.4.6 loads an N163 wave only from under sample 128, and the soft layer's 4 channels leave the chip 192
	const int Memory = std::min(N163WaveMemory(VRC6_PLUS_SOFT_CHANNELS), N163_LOADABLE);
	std::vector<std::pair<const WaveSound *, int> > Out;
	const std::vector<std::string> &All = SoftLayerSounds();
	for (size_t s = 0; s < All.size(); ++s) {
		const WaveSound *pSound = FindWaveSound(All[s]);
		if (pSound == 0 || std::find(Have.begin(), Have.end(), Lower(pSound->InstrumentName())) != Have.end())
			continue;
		const int Place = FindWavePlace(N163_WAVE_SIZE, Memory, Used);
		if (Place < 0)
			continue;
		Used.push_back(std::make_pair(Place, N163_WAVE_SIZE));
		Out.push_back(std::make_pair(pSound, Place));
	}
	return Out;
}

}	// namespace ftapi

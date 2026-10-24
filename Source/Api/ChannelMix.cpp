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

#include "ChannelMix.h"

#include <algorithm>
#include <cmath>

namespace ftapi {

namespace {

// Reading the block without running past its end
class MixReader {
public:
	MixReader(const unsigned char *pData, size_t Size) : m_pData(pData), m_Size(pData != 0 ? Size : 0), m_Pos(0), m_OK(true) {}

	bool OK() const { return m_OK; }
	bool AtEnd() const { return m_Pos == m_Size; }

	int Int(int Min, int Max)
	{
		if (!m_OK || m_Size - m_Pos < 4) {
			m_OK = false;
			return Min;
		}
		unsigned int Value = 0;
		for (int i = 0; i < 4; ++i)
			Value |= static_cast<unsigned int>(m_pData[m_Pos + static_cast<size_t>(i)]) << (8 * i);
		m_Pos += 4;
		const int Signed = static_cast<int>(Value);
		if (Signed < Min || Signed > Max)
			m_OK = false;
		return Signed;
	}

private:
	const unsigned char *m_pData;
	size_t m_Size, m_Pos;
	bool m_OK;
};

void PutInt(std::vector<unsigned char> &Out, int Value)
{
	const unsigned int Bits = static_cast<unsigned int>(Value);
	for (int i = 0; i < 4; ++i)
		Out.push_back(static_cast<unsigned char>((Bits >> (8 * i)) & 0xFF));
}

bool InRange(int ChanId)
{
	return ChanId >= 0 && ChanId < MIX_CHANNELS;
}

}	// namespace

const char *const MixStore::BLOCK_ID = "BF MIXER";

MixStore::MixStore()
{
	Clear();
}

bool MixStore::Empty() const
{
	for (int c = 0; c < MIX_CHANNELS; ++c)
		if (m_Level[c] != 0 || m_Pan[c] != MIX_PAN_CENTER)
			return false;
	return true;
}

void MixStore::Clear()
{
	for (int c = 0; c < MIX_CHANNELS; ++c) {
		m_Level[c] = 0;
		m_Pan[c] = MIX_PAN_CENTER;
	}
}

int MixStore::Level(int ChanId) const
{
	return InRange(ChanId) ? m_Level[ChanId] : 0;
}

int MixStore::Pan(int ChanId) const
{
	return InRange(ChanId) ? m_Pan[ChanId] : MIX_PAN_CENTER;
}

bool MixStore::Set(int ChanId, int Level, int Pan)
{
	if (!InRange(ChanId))
		return false;
	Level = std::max(MIX_LEVEL_LOWEST, std::min(MIX_LEVEL_HIGHEST, Level));
	Pan = std::max(0, std::min(0xFF, Pan));
	if (m_Level[ChanId] == Level && m_Pan[ChanId] == Pan)
		return false;
	m_Level[ChanId] = Level;
	m_Pan[ChanId] = Pan;
	return true;
}

void MixStore::KeepOnly(const std::vector<int> &ChanIds)
{
	for (int c = 0; c < MIX_CHANNELS; ++c)
		if (std::find(ChanIds.begin(), ChanIds.end(), c) == ChanIds.end()) {
			m_Level[c] = 0;
			m_Pan[c] = MIX_PAN_CENTER;
		}
}

bool MixStore::operator==(const MixStore &Other) const
{
	for (int c = 0; c < MIX_CHANNELS; ++c)
		if (m_Level[c] != Other.m_Level[c] || m_Pan[c] != Other.m_Pan[c])
			return false;
	return true;
}

double MixStore::Gain(int Level)
{
	return Level == 0 ? 1.0 : std::pow(10.0, Level / 200.0);
}

std::vector<unsigned char> MixStore::Write() const
{
	std::vector<unsigned char> Out;
	int Count = 0;
	for (int c = 0; c < MIX_CHANNELS; ++c)
		Count += (m_Level[c] != 0 || m_Pan[c] != MIX_PAN_CENTER) ? 1 : 0;
	PutInt(Out, Count);
	for (int c = 0; c < MIX_CHANNELS; ++c) {
		if (m_Level[c] == 0 && m_Pan[c] == MIX_PAN_CENTER)
			continue;
		PutInt(Out, c);
		PutInt(Out, m_Level[c]);
		PutInt(Out, m_Pan[c]);
	}
	return Out;
}

bool MixStore::Read(const unsigned char *pData, size_t Size, int Version)
{
	Clear();
	if (Version != BLOCK_VERSION)
		return true;	// another version's: left out
	MixReader In(pData, Size);
	const int Count = In.Int(0, MIX_CHANNELS);
	MixStore Read;
	int Previous = -1;
	for (int i = 0; i < Count && In.OK(); ++i) {
		const int ChanId = In.Int(Previous + 1, MIX_CHANNELS - 1);	// each channel once, in order
		const int Level = In.Int(MIX_LEVEL_LOWEST, MIX_LEVEL_HIGHEST);
		const int Pan = In.Int(0, 0xFF);
		if (!In.OK())
			return false;
		Read.m_Level[ChanId] = Level;
		Read.m_Pan[ChanId] = Pan;
		Previous = ChanId;
	}
	if (!In.OK() || !In.AtEnd())
		return false;
	*this = Read;
	return true;
}

}	// namespace ftapi

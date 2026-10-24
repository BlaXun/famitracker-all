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
const char *const SPACE_ROOM_NAMES[SPACE_ROOMS] = { "small room", "hall", "cathedral", "cave", "none" };

MixStore::MixStore()
{
	Clear();
}

bool MixStore::ChannelDefault(int ChanId) const
{
	return m_Level[ChanId] == 0 && m_Pan[ChanId] == MIX_PAN_CENTER && m_Soft[ChanId] == 0 && m_Space[ChanId] == 0;
}

bool MixStore::Empty() const
{
	for (int c = 0; c < MIX_CHANNELS; ++c)
		if (!ChannelDefault(c))
			return false;
	return m_Room == SPACE_ROOM_DEFAULT && m_Echo == 0 && m_Repeats == ECHO_REPEATS_DEFAULT;
}

bool MixStore::FadersDefault() const
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
		m_Soft[c] = 0;
		m_Space[c] = 0;
	}
	m_Room = SPACE_ROOM_DEFAULT;
	m_Echo = 0;
	m_Repeats = ECHO_REPEATS_DEFAULT;
}

int MixStore::Level(int ChanId) const
{
	return InRange(ChanId) ? m_Level[ChanId] : 0;
}

int MixStore::Pan(int ChanId) const
{
	return InRange(ChanId) ? m_Pan[ChanId] : MIX_PAN_CENTER;
}

int MixStore::Soft(int ChanId) const
{
	return InRange(ChanId) ? m_Soft[ChanId] : 0;
}

int MixStore::Space(int ChanId) const
{
	return InRange(ChanId) ? m_Space[ChanId] : 0;
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

bool MixStore::SetDepth(int ChanId, int Soft, int Space)
{
	if (!InRange(ChanId))
		return false;
	Soft = std::max(0, std::min(MIX_DEPTH_MOST, Soft));
	Space = std::max(0, std::min(MIX_DEPTH_MOST, Space));
	if (m_Soft[ChanId] == Soft && m_Space[ChanId] == Space)
		return false;
	m_Soft[ChanId] = Soft;
	m_Space[ChanId] = Space;
	return true;
}

bool MixStore::SetSpace(int Room, int Echo, int Repeats)
{
	Room = std::max(0, std::min(SPACE_ROOMS - 1, Room));
	Echo = std::max(0, std::min(ECHO_LONGEST, Echo));
	Repeats = std::max(1, std::min(ECHO_REPEATS_MOST, Repeats));
	if (m_Room == Room && m_Echo == Echo && m_Repeats == Repeats)
		return false;
	m_Room = Room;
	m_Echo = Echo;
	m_Repeats = Repeats;
	return true;
}

bool MixStore::SpaceHeard() const
{
	if (m_Room == SPACE_ROOM_NONE && m_Echo == 0)
		return false;
	for (int c = 0; c < MIX_CHANNELS; ++c)
		if (m_Space[c] > 0)
			return true;
	return false;
}

void MixStore::KeepOnly(const std::vector<int> &ChanIds)
{
	for (int c = 0; c < MIX_CHANNELS; ++c)
		if (std::find(ChanIds.begin(), ChanIds.end(), c) == ChanIds.end()) {
			m_Level[c] = 0;
			m_Pan[c] = MIX_PAN_CENTER;
			m_Soft[c] = 0;
			m_Space[c] = 0;
		}
}

bool MixStore::operator==(const MixStore &Other) const
{
	for (int c = 0; c < MIX_CHANNELS; ++c)
		if (m_Level[c] != Other.m_Level[c] || m_Pan[c] != Other.m_Pan[c] || m_Soft[c] != Other.m_Soft[c] || m_Space[c] != Other.m_Space[c])
			return false;
	return m_Room == Other.m_Room && m_Echo == Other.m_Echo && m_Repeats == Other.m_Repeats;
}

double MixStore::Gain(int Level)
{
	return Level == 0 ? 1.0 : std::pow(10.0, Level / 200.0);
}

int MixStore::WriteVersion() const
{
	for (int c = 0; c < MIX_CHANNELS; ++c)
		if (m_Soft[c] != 0 || m_Space[c] != 0)
			return 2;
	return m_Room == SPACE_ROOM_DEFAULT && m_Echo == 0 && m_Repeats == ECHO_REPEATS_DEFAULT ? 1 : 2;
}

std::vector<unsigned char> MixStore::Write() const
{
	const bool Depth = WriteVersion() >= 2;
	std::vector<unsigned char> Out;
	int Count = 0;
	for (int c = 0; c < MIX_CHANNELS; ++c)
		Count += ChannelDefault(c) ? 0 : 1;
	PutInt(Out, Count);
	for (int c = 0; c < MIX_CHANNELS; ++c) {
		if (ChannelDefault(c))
			continue;
		PutInt(Out, c);
		PutInt(Out, m_Level[c]);
		PutInt(Out, m_Pan[c]);
		if (Depth) {
			PutInt(Out, m_Soft[c]);
			PutInt(Out, m_Space[c]);
		}
	}
	if (Depth) {
		PutInt(Out, m_Room);
		PutInt(Out, m_Echo);
		PutInt(Out, m_Repeats);
	}
	return Out;
}

bool MixStore::Read(const unsigned char *pData, size_t Size, int Version)
{
	Clear();
	if (Version != 1 && Version != 2)
		return true;	// another version's: left out
	const bool Depth = Version >= 2;
	MixReader In(pData, Size);
	const int Count = In.Int(0, MIX_CHANNELS);
	MixStore Read;
	int Previous = -1;
	for (int i = 0; i < Count && In.OK(); ++i) {
		const int ChanId = In.Int(Previous + 1, MIX_CHANNELS - 1);	// each channel once, in order
		const int Level = In.Int(MIX_LEVEL_LOWEST, MIX_LEVEL_HIGHEST);
		const int Pan = In.Int(0, 0xFF);
		const int Soft = Depth ? In.Int(0, MIX_DEPTH_MOST) : 0;
		const int Space = Depth ? In.Int(0, MIX_DEPTH_MOST) : 0;
		if (!In.OK())
			return false;
		Read.m_Level[ChanId] = Level;
		Read.m_Pan[ChanId] = Pan;
		Read.m_Soft[ChanId] = Soft;
		Read.m_Space[ChanId] = Space;
		Previous = ChanId;
	}
	if (Depth) {
		Read.m_Room = In.Int(0, SPACE_ROOMS - 1);
		Read.m_Echo = In.Int(0, ECHO_LONGEST);
		Read.m_Repeats = In.Int(1, ECHO_REPEATS_MOST);
	}
	if (!In.OK() || !In.AtEnd()) {
		Clear();
		return false;
	}
	*this = Read;
	return true;
}

}	// namespace ftapi

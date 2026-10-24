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

#include "Keys.h"

#include <algorithm>
#include <cmath>

namespace ftapi {

namespace {

const int MAX_TRACK_COUNT = 64;		// FamiTracker's MAX_TRACKS
const int FLAG_PENTATONIC = 1;
const char PENTATONIC[] = "pentatonic";

int Mod12(int Value)
{
	return ((Value % 12) + 12) % 12;
}

// Pitches far enough around the song's for stepping through the key
const int LOWEST_STEP = -24, HIGHEST_STEP = PITCHES + 24;

// Reading the block without running past its end
class Reader {
public:
	Reader(const unsigned char *pData, size_t Size) : m_pData(pData), m_Size(pData != 0 ? Size : 0), m_Pos(0), m_OK(true) {}

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
		if (Signed < Min || Signed > Max) {
			m_OK = false;
			return Min;
		}
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
		Out.push_back(static_cast<unsigned char>(Bits >> (8 * i)));
}

std::string Lower(const std::string &Text)
{
	std::string Out(Text);
	for (size_t i = 0; i < Out.size(); ++i)
		if (Out[i] >= 'A' && Out[i] <= 'Z')
			Out[i] = static_cast<char>(Out[i] - 'A' + 'a');
	return Out;
}

std::string Trimmed(const std::string &Text)
{
	size_t First = 0, Last = Text.size();
	while (First < Last && (Text[First] == ' ' || Text[First] == '\t'))
		++First;
	while (Last > First && (Text[Last - 1] == ' ' || Text[Last - 1] == '\t'))
		--Last;
	return Text.substr(First, Last - First);
}

}	// namespace

bool CanBePentatonic(const Key &K)
{
	return K.Mode == MODE_MAJOR || K.Mode == MODE_MINOR;
}

std::string KeyChoiceName(const KeyChoice &C)
{
	return KeyName(C.K) + (C.Pentatonic && CanBePentatonic(C.K) ? std::string(" ") + PENTATONIC : std::string());
}

bool ParseKeyChoice(const std::string &Input, KeyChoice &Out)
{
	std::string Text = Trimmed(Input);
	const std::string Lowered = Lower(Text);
	const size_t Length = sizeof(PENTATONIC) - 1;
	bool Five = false;
	if (Lowered.size() >= Length && Lowered.compare(Lowered.size() - Length, Length, PENTATONIC) == 0) {
		Text = Trimmed(Text.substr(0, Text.size() - Length));
		Five = true;
	}
	Key K;
	if (!ParseKey(Text, K) || (Five && !CanBePentatonic(K)))
		return false;
	Out = KeyChoice(K, Five);
	return true;
}

bool KeyHasPitch(const KeyChoice &C, int Pitch)
{
	if (!C.Pentatonic || !CanBePentatonic(C.K))
		return InKey(Pitch, C.K);
	static const int MAJOR[] = { 0, 2, 4, 7, 9 }, MINOR[] = { 0, 3, 5, 7, 10 };
	const int *pSteps = C.K.Mode == MODE_MINOR ? MINOR : MAJOR;
	const int Relative = Mod12(Pitch - C.K.Tonic);
	for (int i = 0; i < 5; ++i)
		if (pSteps[i] == Relative)
			return true;
	return false;
}

bool IsTonic(const KeyChoice &C, int Pitch)
{
	return Mod12(Pitch - C.K.Tonic) == 0;
}

int SnapToKey(const KeyChoice &C, double Pitch, int Low, int High)
{
	if (High < Low)
		std::swap(Low, High);
	const int Nearest = static_cast<int>(std::floor(Pitch + 0.5));
	int Best = std::max(Low, std::min(High, Nearest));
	double BestDistance = 1e9;
	for (int Candidate = std::max(Low, Nearest - 7); Candidate <= std::min(High, Nearest + 7); ++Candidate) {
		if (!KeyHasPitch(C, Candidate))
			continue;
		const double Distance = std::fabs(Candidate - Pitch);
		if (Distance < BestDistance - 1e-9) {	// of two as near, the lower (it came first)
			BestDistance = Distance;
			Best = Candidate;
		}
	}
	return Best;
}

int StepInKey(const KeyChoice &C, int Pitch, int Steps)
{
	const int Way = Steps > 0 ? 1 : -1;
	for (int k = 0; k != Steps; k += Way) {
		do
			Pitch += Way;
		while (Pitch > LOWEST_STEP && Pitch < HIGHEST_STEP && !KeyHasPitch(C, Pitch));
	}
	return Pitch;
}

int KeyStepsBetween(const KeyChoice &C, int From, int To)
{
	int Steps = 0;
	for (int p = From + 1; p <= To; ++p)
		Steps += KeyHasPitch(C, p) ? 1 : 0;
	for (int p = From - 1; p >= To; --p)
		Steps -= KeyHasPitch(C, p) ? 1 : 0;
	return Steps;
}

// The module's keys

const char *const KeyStore::BLOCK_ID = "BF KEYS";

bool KeyStore::Empty() const
{
	for (size_t t = 0; t < m_Tracks.size(); ++t)
		if (m_Tracks[t].Chosen)
			return false;
	return true;
}

void KeyStore::Clear()
{
	m_Tracks.clear();
}

bool KeyStore::Get(int Track, KeyChoice &Out) const
{
	if (Track < 0 || Track >= static_cast<int>(m_Tracks.size()) || !m_Tracks[static_cast<size_t>(Track)].Chosen)
		return false;
	Out = m_Tracks[static_cast<size_t>(Track)].Choice;
	return true;
}

void KeyStore::Set(int Track, const KeyChoice &Choice)
{
	if (Track < 0 || Track >= MAX_TRACK_COUNT)
		return;
	if (Track >= static_cast<int>(m_Tracks.size()))
		m_Tracks.resize(static_cast<size_t>(Track) + 1);
	Stored &Into = m_Tracks[static_cast<size_t>(Track)];
	Into.Chosen = true;
	Into.Choice = Choice;
	Into.Choice.K.Tonic = Mod12(Choice.K.Tonic);
	if (Into.Choice.K.Mode < 0 || Into.Choice.K.Mode >= ModeCount())
		Into.Choice.K.Mode = MODE_MAJOR;
	Into.Choice.Pentatonic = Choice.Pentatonic && CanBePentatonic(Into.Choice.K);
}

void KeyStore::SetAuto(int Track)
{
	if (Track >= 0 && Track < static_cast<int>(m_Tracks.size()))
		m_Tracks[static_cast<size_t>(Track)] = Stored();
}

void KeyStore::TrackRemoved(int Track)
{
	if (Track >= 0 && Track < static_cast<int>(m_Tracks.size()))
		m_Tracks.erase(m_Tracks.begin() + Track);
}

void KeyStore::TracksSwapped(int Track1, int Track2)
{
	if (Track1 < 0 || Track2 < 0 || Track1 >= MAX_TRACK_COUNT || Track2 >= MAX_TRACK_COUNT)
		return;
	const size_t Needed = static_cast<size_t>(std::max(Track1, Track2)) + 1;
	if (m_Tracks.size() < Needed)
		m_Tracks.resize(Needed);
	std::swap(m_Tracks[static_cast<size_t>(Track1)], m_Tracks[static_cast<size_t>(Track2)]);
}

std::vector<unsigned char> KeyStore::Write() const
{
	std::vector<unsigned char> Out;
	int Tracks = 0;
	for (size_t t = 0; t < m_Tracks.size(); ++t)
		Tracks += m_Tracks[t].Chosen ? 1 : 0;
	PutInt(Out, Tracks);
	for (size_t t = 0; t < m_Tracks.size(); ++t) {
		const Stored &One = m_Tracks[t];
		if (!One.Chosen)
			continue;
		PutInt(Out, static_cast<int>(t));
		PutInt(Out, One.Choice.K.Tonic);
		PutInt(Out, One.Choice.K.Mode);
		PutInt(Out, One.Choice.Pentatonic ? FLAG_PENTATONIC : 0);
	}
	return Out;
}

bool KeyStore::Read(const unsigned char *pData, size_t Size, int Version)
{
	Clear();
	if (Version != BLOCK_VERSION)
		return true;	// from a newer version: left out
	Reader In(pData, Size);
	const int Tracks = In.Int(0, MAX_TRACK_COUNT);
	std::vector<Stored> Read;
	int Previous = -1;
	for (int t = 0; t < Tracks && In.OK(); ++t) {
		const int Track = In.Int(Previous + 1, MAX_TRACK_COUNT - 1);	// each track once, in order
		Stored One;
		One.Chosen = true;
		One.Choice.K.Tonic = In.Int(0, 11);
		One.Choice.K.Mode = In.Int(0, ModeCount() - 1);
		const int Flags = In.Int(0, FLAG_PENTATONIC);
		One.Choice.Pentatonic = (Flags & FLAG_PENTATONIC) != 0;
		if (!In.OK() || (One.Choice.Pentatonic && !CanBePentatonic(One.Choice.K)))
			return false;
		if (Track >= static_cast<int>(Read.size()))
			Read.resize(static_cast<size_t>(Track) + 1);
		Read[static_cast<size_t>(Track)] = One;
		Previous = Track;
	}
	if (!In.OK() || !In.AtEnd())
		return false;
	m_Tracks.swap(Read);
	return true;
}

}	// namespace ftapi

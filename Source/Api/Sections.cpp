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

#include "Sections.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace ftapi {

namespace {

const int MAX_TRACK_COUNT = 64;		// FamiTracker's MAX_TRACKS
const int MAX_FRAME_COUNT = 128;	// FamiTracker's MAX_FRAMES

char Lower(char c)
{
	return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool ByFirst(const Section &a, const Section &b)
{
	return a.First < b.First;
}

// Sections that lost all their frames go; the rest in frame order
void Tidy(SectionList &List)
{
	SectionList Kept;
	for (size_t i = 0; i < List.size(); ++i)
		if (List[i].Last >= List[i].First)
			Kept.push_back(List[i]);
	std::stable_sort(Kept.begin(), Kept.end(), ByFirst);
	List.swap(Kept);
}

// "Chorus 2" -> "Chorus" and 2; a name without a number at its end -> itself and 1
std::string NumberedBase(const std::string &Name, int &Number)
{
	Number = 1;
	size_t Digits = Name.size();
	while (Digits > 0 && Name[Digits - 1] >= '0' && Name[Digits - 1] <= '9')
		--Digits;
	if (Digits == Name.size() || Digits < 2 || Name[Digits - 1] != ' ' || Name.size() - Digits > 3)
		return Name;
	Number = std::atoi(Name.c_str() + Digits);
	return Name.substr(0, Digits - 1);
}

// Reading the block without running past its end
class Reader {
public:
	Reader(const unsigned char *pData, size_t Size) : m_pData(pData), m_Size(Size), m_Pos(0), m_OK(true) {}

	bool OK() const { return m_OK; }

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

	std::string Text()
	{
		std::string Out;
		while (m_OK) {
			if (m_Pos >= m_Size || static_cast<int>(Out.size()) > MAX_SECTION_NAME) {
				m_OK = false;	// no end, or too long
				break;
			}
			const char c = static_cast<char>(m_pData[m_Pos++]);
			if (c == 0)
				break;
			Out += c;
		}
		return Out;
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

const unsigned char COLORS[SECTION_COLORS][3] = {
	{ 214, 120, 64 }, { 70, 140, 220 }, { 90, 175, 85 }, { 190, 95, 185 },
	{ 215, 180, 50 }, { 60, 170, 170 }, { 220, 90, 110 }, { 140, 125, 220 },
};

}	// namespace

// Lookups

int SectionAt(const SectionList &List, int Frame)
{
	for (size_t i = 0; i < List.size(); ++i)
		if (Frame >= List[i].First && Frame <= List[i].Last)
			return static_cast<int>(i);
	return -1;
}

bool SameSectionName(const std::string &a, const std::string &b)
{
	const std::string x = TrimSectionName(a), y = TrimSectionName(b);
	if (x.size() != y.size())
		return false;
	for (size_t i = 0; i < x.size(); ++i)
		if (Lower(x[i]) != Lower(y[i]))
			return false;
	return true;
}

int FindSection(const SectionList &List, const std::string &Name)
{
	for (size_t i = 0; i < List.size(); ++i)
		if (SameSectionName(List[i].Name, Name))
			return static_cast<int>(i);
	return -1;
}

std::string TrimSectionName(const std::string &Name)
{
	size_t First = 0, Last = Name.size();
	while (First < Last && (Name[First] == ' ' || Name[First] == '\t'))
		++First;
	while (Last > First && (Name[Last - 1] == ' ' || Name[Last - 1] == '\t'))
		--Last;
	return Name.substr(First, Last - First);
}

std::string SectionNameProblem(const std::string &Name)
{
	const std::string Text = TrimSectionName(Name);
	if (Text.empty())
		return "A section's name can't be empty.";
	if (static_cast<int>(Text.size()) > MAX_SECTION_NAME) {
		char Buffer[96];
		std::snprintf(Buffer, sizeof(Buffer), "A section's name has at most %d characters.", MAX_SECTION_NAME);
		return Buffer;
	}
	for (size_t i = 0; i < Text.size(); ++i)
		if (static_cast<unsigned char>(Text[i]) < 0x20)
			return "A section's name is one line of text.";
	return std::string();
}

// A name and the number at its end, with or without a space before it: "A2" and "A 2" are both A's second (Spaced
// says which way it's written); a name without a number at its end is itself and 1
static std::string NumberedRoot(const std::string &Name, int &Number, bool &Spaced)
{
	Number = 1;
	Spaced = false;
	size_t Digits = Name.size();
	while (Digits > 0 && Name[Digits - 1] >= '0' && Name[Digits - 1] <= '9')
		--Digits;
	if (Digits == Name.size() || Digits == 0 || Name.size() - Digits > 3)
		return Name;
	size_t Root = Digits;
	if (Name[Root - 1] == ' ') {
		if (Root < 2)
			return Name;
		Spaced = true;
		--Root;
	}
	Number = std::atoi(Name.c_str() + Digits);
	return Name.substr(0, Root);
}

// Whether a section's name is Name or reads as it: "A 2" beside a section "A2" would look like the same name
static bool SectionNameTaken(const SectionList &List, const std::string &Name)
{
	if (FindSection(List, Name) >= 0)
		return true;
	int Number = 1;
	bool Spaced = false;
	const std::string Root = NumberedRoot(Name, Number, Spaced);
	if (Root == Name)
		return false;
	for (size_t i = 0; i < List.size(); ++i) {
		int Other = 1;
		const std::string Its = NumberedRoot(TrimSectionName(List[i].Name), Other, Spaced);
		if (Its != TrimSectionName(List[i].Name) && Other == Number && SameSectionName(Its, Root))
			return true;
	}
	return false;
}

std::string FreeSectionName(const SectionList &List, const std::string &Base)
{
	const std::string Name = TrimSectionName(Base);
	if (!SectionNameTaken(List, Name))
		return Name;
	int Number = 1;
	bool Spaced = false;
	const std::string Root = NumberedRoot(Name, Number, Spaced);
	if (Root == Name) {
		// A name without a number: numbered the way the song numbers that name already ("A2", or "A 2")
		bool Plain = false, WithSpace = false;
		for (size_t i = 0; i < List.size(); ++i) {
			int Its = 1;
			bool Space = false;
			const std::string Other = TrimSectionName(List[i].Name);
			const std::string OtherRoot = NumberedRoot(Other, Its, Space);
			if (OtherRoot != Other && SameSectionName(OtherRoot, Root))
				(Space ? WithSpace : Plain) = true;
		}
		Spaced = WithSpace || !Plain;
	}
	for (int n = std::max(2, Number + 1); n < 1000; ++n) {
		char Suffix[16];
		std::snprintf(Suffix, sizeof(Suffix), Spaced ? " %d" : "%d", n);
		std::string Candidate = Root.substr(0, static_cast<size_t>(MAX_SECTION_NAME) - std::string(Suffix).size()) + Suffix;
		if (!SectionNameTaken(List, Candidate))
			return Candidate;
	}
	return Name;
}

bool ValidSections(const SectionList &List, int Frames)
{
	for (size_t i = 0; i < List.size(); ++i) {
		const Section &S = List[i];
		if (S.First < 0 || S.Last < S.First || S.Last >= MAX_FRAME_COUNT || (Frames > 0 && S.Last >= Frames))
			return false;
		if (i > 0 && S.First <= List[i - 1].Last)
			return false;
		if (!SectionNameProblem(S.Name).empty() || S.Name != TrimSectionName(S.Name))
			return false;
		for (size_t k = 0; k < i; ++k)
			if (SameSectionName(List[k].Name, S.Name))
				return false;
	}
	return true;
}

// Naming

void UnnameFrames(SectionList &List, int First, int Last)
{
	for (size_t i = 0; i < List.size(); ++i) {
		Section &S = List[i];
		if (S.Last < First || S.First > Last)
			continue;
		if (S.First < First)
			S.Last = First - 1;			// keeps its frames before them
		else if (S.Last > Last)
			S.First = Last + 1;			// keeps its frames after them
		else
			S.Last = S.First - 1;		// all of it: it goes
	}
	Tidy(List);
}

void NameFrames(SectionList &List, int First, int Last, const std::string &Name)
{
	if (Last < First)
		return;
	const int Same = FindSection(List, Name);
	if (Same >= 0)
		List.erase(List.begin() + Same);
	UnnameFrames(List, First, Last);
	List.push_back(Section(TrimSectionName(Name), First, Last));
	Tidy(List);
}

bool RenameSection(SectionList &List, int Index, const std::string &Name)
{
	if (Index < 0 || Index >= static_cast<int>(List.size()))
		return false;
	const int Other = FindSection(List, Name);
	if (Other >= 0 && Other != Index)
		return false;
	List[static_cast<size_t>(Index)].Name = TrimSectionName(Name);
	return true;
}

void RemoveSection(SectionList &List, int Index)
{
	if (Index >= 0 && Index < static_cast<int>(List.size()))
		List.erase(List.begin() + Index);
}

// The frame list changing

void SectionsInsertFrames(SectionList &List, int Frame, int Count)
{
	if (Count <= 0)
		return;
	for (size_t i = 0; i < List.size(); ++i) {
		Section &S = List[i];
		if (S.First >= Frame) {			// after them, or starting where they go in
			S.First += Count;
			S.Last += Count;
		}
		else if (S.Last >= Frame)		// between two of its frames: they're part of it
			S.Last += Count;
	}
}

void SectionsJoinBefore(SectionList &List, int Frame)
{
	if (Frame <= 0 || SectionAt(List, Frame) >= 0)
		return;
	const int Before = SectionAt(List, Frame - 1);
	if (Before >= 0 && List[static_cast<size_t>(Before)].Last == Frame - 1)
		List[static_cast<size_t>(Before)].Last = Frame;
}

void SectionsCopyFrame(SectionList &List, int Frame)
{
	SectionsInsertFrames(List, Frame + 1, 1);
	SectionsJoinBefore(List, Frame + 1);
}

void SectionsRemoveFrames(SectionList &List, int Frame, int Count)
{
	if (Count <= 0)
		return;
	const int End = Frame + Count;	// the first frame after them
	for (size_t i = 0; i < List.size(); ++i) {
		Section &S = List[i];
		const int First = S.First < Frame ? S.First : (S.First >= End ? S.First - Count : Frame);
		const int Stop = S.Last + 1 <= Frame ? S.Last + 1 : (S.Last + 1 >= End ? S.Last + 1 - Count : Frame);
		S.First = First;
		S.Last = Stop - 1;
	}
	Tidy(List);
}

void SectionsMoveFrames(SectionList &List, int First, int Last, int To)
{
	if (Last < First)
		return;
	const int Count = Last - First + 1;
	// The sections wholly among the frames go along; the others give them up
	SectionList Carried, Rest;
	for (size_t i = 0; i < List.size(); ++i) {
		const Section &S = List[i];
		if (S.First >= First && S.Last <= Last)
			Carried.push_back(Section(S.Name, S.First - First, S.Last - First));
		else
			Rest.push_back(S);
	}
	SectionsRemoveFrames(Rest, First, Count);
	// Where they land: between two frames of a section they join it, unless
	// they bring sections of their own, which that section then ends before
	for (size_t i = 0; i < Rest.size(); ++i) {
		Section &S = Rest[i];
		if (S.First >= To) {
			S.First += Count;
			S.Last += Count;
		}
		else if (S.Last >= To) {
			if (Carried.empty())
				S.Last += Count;
			else
				S.Last = To - 1;
		}
	}
	for (size_t i = 0; i < Carried.size(); ++i)
		Rest.push_back(Section(Carried[i].Name, Carried[i].First + To, Carried[i].Last + To));
	Tidy(Rest);
	List.swap(Rest);
}

void SectionsFrameCount(SectionList &List, int Frames)
{
	for (size_t i = 0; i < List.size(); ++i)
		List[i].Last = std::min(List[i].Last, Frames - 1);
	Tidy(List);
}

int SectionMoveTarget(const SectionList &List, int Index, bool Up, int Frames)
{
	if (Index < 0 || Index >= static_cast<int>(List.size()))
		return -1;
	const Section &S = List[static_cast<size_t>(Index)];
	if (Up) {
		if (S.First <= 0)
			return -1;
		const int Before = SectionAt(List, S.First - 1);
		return Before >= 0 ? List[static_cast<size_t>(Before)].First : S.First - 1;
	}
	if (S.Last + 1 >= Frames)
		return -1;
	const int After = SectionAt(List, S.Last + 1);
	return S.First + (After >= 0 ? List[static_cast<size_t>(After)].Frames() : 1);
}

SectionList SectionsShiftRows(const SectionList &List, int PatternLength, int First, int Shift, int Frames, bool &Exact)
{
	Exact = true;
	const int PL = std::max(1, PatternLength);
	// Where a row boundary goes (rows taken out close up on First)
	auto Moved = [&](int Row, bool Start) {
		if (Shift >= 0)
			return Row < First || (!Start && Row == First) ? Row : Row + Shift;
		const int End = First - Shift;
		return Row <= First ? Row : (Row >= End ? Row + Shift : First);
	};
	SectionList Out;
	for (size_t i = 0; i < List.size(); ++i) {
		const Section &S = List[i];
		const int From = Moved(S.First * PL, true), To = Moved((S.Last + 1) * PL, false);
		if (To <= From)
			continue;					// all its rows were taken out
		if (From % PL != 0 || To % PL != 0)
			Exact = false;
		const int FirstFrame = (From + PL / 2) / PL, EndFrame = std::min(Frames, (To + PL / 2) / PL);
		if (EndFrame > FirstFrame)
			Out.push_back(Section(S.Name, FirstFrame, EndFrame - 1));
		else
			Exact = false;				// less than half a frame left
	}
	// Rounding can make neighbours meet: the earlier one keeps the frame
	for (size_t i = 1; i < Out.size(); ++i)
		if (Out[i].First <= Out[i - 1].Last)
			Out[i].First = Out[i - 1].Last + 1;
	Tidy(Out);
	return Out;
}

int SectionColor(const std::string &Name)
{
	int Number = 1;
	std::string Root = NumberedBase(TrimSectionName(Name), Number);
	while (Root.size() > 1 && Root[Root.size() - 1] == '\'')
		Root.erase(Root.size() - 1);	// "A'" and "A''" are A's repeats too (SongShapes.h)
	unsigned int Hash = 2166136261u;
	for (size_t i = 0; i < Root.size(); ++i) {
		Hash ^= static_cast<unsigned char>(Lower(Root[i]));
		Hash *= 16777619u;
	}
	return static_cast<int>(Hash % SECTION_COLORS);
}

void SectionRgb(int Color, int &Red, int &Green, int &Blue)
{
	const unsigned char *pColor = COLORS[((Color % SECTION_COLORS) + SECTION_COLORS) % SECTION_COLORS];
	Red = pColor[0];
	Green = pColor[1];
	Blue = pColor[2];
}

// Every track's

const char *const SectionStore::BLOCK_ID = "BF SECTIONS";

bool SectionStore::Empty() const
{
	for (size_t t = 0; t < m_Tracks.size(); ++t)
		if (!m_Tracks[t].empty())
			return false;
	return true;
}

void SectionStore::Clear()
{
	m_Tracks.clear();
}

const SectionList &SectionStore::Get(int Track) const
{
	static const SectionList NONE;
	return (Track >= 0 && Track < static_cast<int>(m_Tracks.size())) ? m_Tracks[static_cast<size_t>(Track)] : NONE;
}

SectionList &SectionStore::Edit(int Track)
{
	static SectionList Nowhere;
	if (Track < 0 || Track >= MAX_TRACK_COUNT) {
		Nowhere.clear();
		return Nowhere;
	}
	if (Track >= static_cast<int>(m_Tracks.size()))
		m_Tracks.resize(static_cast<size_t>(Track) + 1);
	return m_Tracks[static_cast<size_t>(Track)];
}

void SectionStore::Set(int Track, const SectionList &List)
{
	if (Track >= 0 && Track < MAX_TRACK_COUNT)
		Edit(Track) = List;
}

void SectionStore::TrackRemoved(int Track)
{
	if (Track >= 0 && Track < static_cast<int>(m_Tracks.size()))
		m_Tracks.erase(m_Tracks.begin() + Track);
}

void SectionStore::TracksSwapped(int Track1, int Track2)
{
	if (Track1 < 0 || Track2 < 0 || Track1 >= MAX_TRACK_COUNT || Track2 >= MAX_TRACK_COUNT)
		return;
	Edit(std::max(Track1, Track2));
	m_Tracks[static_cast<size_t>(Track1)].swap(m_Tracks[static_cast<size_t>(Track2)]);
}

void SectionStore::FitFrames(int Track, int Frames)
{
	if (Track >= 0 && Track < static_cast<int>(m_Tracks.size()))
		SectionsFrameCount(m_Tracks[static_cast<size_t>(Track)], Frames);
}

std::vector<unsigned char> SectionStore::Write() const
{
	std::vector<unsigned char> Out;
	int Tracks = 0;
	for (size_t t = 0; t < m_Tracks.size(); ++t)
		Tracks += m_Tracks[t].empty() ? 0 : 1;
	PutInt(Out, Tracks);
	for (size_t t = 0; t < m_Tracks.size(); ++t) {
		const SectionList &List = m_Tracks[t];
		if (List.empty())
			continue;
		PutInt(Out, static_cast<int>(t));
		PutInt(Out, static_cast<int>(List.size()));
		for (size_t i = 0; i < List.size(); ++i) {
			PutInt(Out, List[i].First);
			PutInt(Out, List[i].Last);
			Out.insert(Out.end(), List[i].Name.begin(), List[i].Name.end());
			Out.push_back(0);
		}
	}
	return Out;
}

bool SectionStore::Read(const unsigned char *pData, size_t Size, int Version)
{
	Clear();
	if (Version != BLOCK_VERSION)
		return true;	// from a newer version: left out
	Reader In(pData, pData != 0 ? Size : 0);
	const int Tracks = In.Int(0, MAX_TRACK_COUNT);
	std::vector<SectionList> Read;
	int Previous = -1;
	for (int t = 0; t < Tracks && In.OK(); ++t) {
		const int Track = In.Int(Previous + 1, MAX_TRACK_COUNT - 1);	// each track once, in order
		const int Count = In.Int(1, MAX_FRAME_COUNT);
		SectionList List;
		for (int i = 0; i < Count && In.OK(); ++i) {
			Section S;
			S.First = In.Int(0, MAX_FRAME_COUNT - 1);
			S.Last = In.Int(S.First, MAX_FRAME_COUNT - 1);
			S.Name = In.Text();
			List.push_back(S);
		}
		if (!In.OK() || !ValidSections(List, 0))
			return false;
		if (Track >= static_cast<int>(Read.size()))
			Read.resize(static_cast<size_t>(Track) + 1);
		Read[static_cast<size_t>(Track)] = List;
		Previous = Track;
	}
	if (!In.OK())
		return false;
	m_Tracks.swap(Read);
	return true;
}

}	// namespace ftapi

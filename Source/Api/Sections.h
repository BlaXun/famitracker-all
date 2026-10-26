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

// A song's sections: named runs of a track's frames (Intro, A, B, Bridge,
// Outro...). The frame editor shows them beside the frames, the piano roll
// above its bars, and Claude Code's tools take them in place of bar numbers.
// Each track has its own, and frames outside every section are fine. A list
// is kept in frame order, its sections never overlap, and a name is used
// once in a track (letter case aside), so a name says which section.
//
// When the frame list changes, the sections follow:
//  - frames put in between two frames of a section become part of it; put
//    in where a section starts, or after it ends, they don't;
//  - a frame's copy (Duplicate frame) goes into its original's section;
//  - frames taken out leave their section, which goes when they were all
//    of it;
//  - frames moved as a block (Move up and down, a drag, Move section) take
//    the sections that are wholly among them along and leave the ones they
//    were only part of. Landing between two frames of a section, they join
//    it; bringing sections of their own, that section ends where they land.
//
// FamiTracker keeps them in the module's "BF SECTIONS" block, which other
// FamiTracker versions skip; a module without sections has no such block.
// Plain C++14, shared by FamiTracker and the tools.

#include <cstddef>
#include <string>
#include <vector>

namespace ftapi {

struct Section {
	std::string Name;	// in the module FamiTracker's code page, in the tools UTF-8
	int First, Last;	// frames, from 0; Last included

	Section() : First(0), Last(0) {}
	Section(const std::string &SectionName, int FirstFrame, int LastFrame) : Name(SectionName), First(FirstFrame), Last(LastFrame) {}
	int Frames() const { return Last - First + 1; }
};

inline bool operator==(const Section &a, const Section &b)
{
	return a.Name == b.Name && a.First == b.First && a.Last == b.Last;
}

inline bool operator!=(const Section &a, const Section &b)
{
	return !(a == b);
}

typedef std::vector<Section> SectionList;

const int MAX_SECTION_NAME = 32;	// bytes
const int SECTION_COLORS = 8;

// Lookups
int SectionAt(const SectionList &List, int Frame);					// the section that frame is in, -1 for none
int FindSection(const SectionList &List, const std::string &Name);	// letter case aside, spaces around it too; -1 for none
bool SameSectionName(const std::string &a, const std::string &b);
std::string TrimSectionName(const std::string &Name);
// What keeps a name from being a section's, "" when nothing does (spaces around it don't count)
std::string SectionNameProblem(const std::string &Name);
// A name no section of the list has: Base, else Base numbered on ("Chorus 2", "Chorus 3"; "A 2" goes on to "A 3")
std::string FreeSectionName(const SectionList &List, const std::string &Base);
// Sorted, not overlapping, inside Frames frames (0: any), every name a good one and used once
bool ValidSections(const SectionList &List, int Frames);

// Naming. Frames First..Last become the section Name: a section of that name
// elsewhere goes, and the others there give those frames up (one that had
// frames before and after them keeps those before).
void NameFrames(SectionList &List, int First, int Last, const std::string &Name);
// Frames First..Last leave their sections, the same way
void UnnameFrames(SectionList &List, int First, int Last);
// False when another section has that name
bool RenameSection(SectionList &List, int Index, const std::string &Name);
void RemoveSection(SectionList &List, int Index);

// The frame list changing (see above)
void SectionsInsertFrames(SectionList &List, int Frame, int Count);		// Count frames put in before Frame
void SectionsCopyFrame(SectionList &List, int Frame);					// a copy of Frame put in after it
void SectionsJoinBefore(SectionList &List, int Frame);					// a frame just put in as a copy of the one before it
void SectionsRemoveFrames(SectionList &List, int Frame, int Count);
void SectionsMoveFrames(SectionList &List, int First, int Last, int To);	// To: where First goes, counted without them
void SectionsFrameCount(SectionList &List, int Frames);					// the frames from Frames on are gone

// Moving the whole section at Index up past what's before it (or down past
// what's after it): the section there, or the one frame without a section.
// Where its first frame goes, counted without its frames (for
// SectionsMoveFrames); -1 at the song's start (end).
int SectionMoveTarget(const SectionList &List, int Index, bool Up, int Frames);

// The song's rows from First on moving by Shift (rows put in, or -Shift rows
// taken out), as insert_bars and remove_bars move them: the sections where
// their rows go, on whole frames of Frames. Rows put in where a section
// starts aren't part of it. Exact is false when an end fell inside a frame and
// went to the nearest frame's start.
SectionList SectionsShiftRows(const SectionList &List, int PatternLength, int First, int Shift, int Frames, bool &Exact);

// A section's color, 0 to SECTION_COLORS - 1: the same for "Chorus" and
// "Chorus 2" (and "A", "A'" and "A''"), so repeats look alike
int SectionColor(const std::string &Name);
void SectionRgb(int Color, int &Red, int &Green, int &Blue);

// Every track's sections, as the module keeps them
class SectionStore {
public:
	bool Empty() const;
	void Clear();
	const SectionList &Get(int Track) const;	// empty for a track without
	void Set(int Track, const SectionList &List);
	SectionList &Edit(int Track);
	void TrackRemoved(int Track);				// the tracks after it move up
	void TracksSwapped(int Track1, int Track2);
	void FitFrames(int Track, int Frames);		// sections past a track's frames go (after loading)

	// The "BF SECTIONS" block: little-endian 32-bit numbers and names ending
	// in a zero byte. The count of tracks with sections, then for each its
	// number from 0, its count of sections, and for each its first and last
	// frame and its name.
	static const char *const BLOCK_ID;
	static const int BLOCK_VERSION = 1;
	std::vector<unsigned char> Write() const;
	// From a block's data: false when it's damaged (and then the store is
	// empty). A newer block version is left out.
	bool Read(const unsigned char *pData, size_t Size, int Version);

private:
	std::vector<SectionList> m_Tracks;
};

}	// namespace ftapi

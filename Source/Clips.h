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

class CDocumentFile;
class CFamiTrackerDoc;
struct stChanNote;

// A saved copy of the song's linked copies, for undo (CClipLibrary::SaveLinks)
class CClipLinkState
{
public:
	CClipLinkState();
	~CClipLinkState();

private:
	friend class CClipLibrary;
	struct Impl;
	Impl *m_pImpl;

	CClipLinkState(const CClipLinkState &);
	CClipLinkState &operator=(const CClipLinkState &);
};

// Clips: pieces of pattern data kept with the song (a recorded take, or a
// selection) to place again, as plain copies or linked. Editing a linked copy
// in the song, or the clip in the clip editor, changes the clip and every
// linked copy.
//
// A clip is a block of cells, columns (channels) by rows. A linked copy
// remembers where its cells are by pattern rather than by frame: for each
// channel (by channel ID) the patterns and rows they fill. It keeps working
// when frames move around, and a link that no longer leads anywhere (its
// channel, track or rows gone) is dropped.
//
// Saved in the module's "BF CLIPS" block, which other FamiTracker versions
// skip; there the linked copies are plain notes.
class CClipLibrary
{
public:
	CClipLibrary();
	~CClipLibrary();

	void Clear();
	bool IsEmpty() const;

	// Clips, by index from 0 to GetCount() - 1
	int GetCount() const;
	CString NewName(LPCTSTR Base, bool Numbered) const;	// "Base 1" (or "Base"), "Base 2"...: one no clip has yet
	int Add(LPCTSTR Name, int Columns, int Rows);	// empty cells; returns the new index, -1 if there are too many
	int Duplicate(int Index, LPCTSTR Name);			// the cells, without the links; returns the new index
	void Remove(int Index);							// linked copies stay in the song as plain notes
	CString GetName(int Index) const;
	void SetName(int Index, LPCTSTR Name);
	int GetColumns(int Index) const;
	int GetRows(int Index) const;
	int GetSource(int Index, int Column) const;		// ID of the channel the column came from, -1 if not known
	void SetSource(int Index, int Column, int ChanID);
	void GetCell(int Index, int Column, int Row, stChanNote &Cell) const;
	void SetCell(int Index, int Column, int Row, const stChanNote &Cell);
	int GetId(int Index) const;						// stays the same while the clip exists
	int FindIndex(int Id) const;					// -1 when that clip is gone

	// Linked copies. AddLink notes the copy of clip Index whose first cell is
	// at Row of Frame in Channel, and returns the link's ID, -1 if nothing of
	// it is in the song. The copy's patterns must already be its own.
	int AddLink(const CFamiTrackerDoc *pDoc, int Index, unsigned int Track, unsigned int Frame, unsigned int Row,
		int Channel);
	void RemoveLink(int LinkId);
	int GetLinkCount(int Index) const;
	void Unlink(int Index);							// its linked copies become plain notes

	// Something else is written over Rows rows from Row of Frame in Channel:
	// linked copies with cells there become plain notes. Call it once the
	// patterns to write are the frames' own.
	void UnlinkArea(const CFamiTrackerDoc *pDoc, unsigned int Track, unsigned int Frame, unsigned int Row, int Channel,
		int Rows);

	// Undo: the linked copies as they are, and back to that
	void SaveLinks(CClipLinkState &State) const;
	void RestoreLinks(const CClipLinkState &State);

	// The patterns a clip's linked copies are written in, for undo: Number from 0
	// to GetLinkSegmentCount(Index) - 1. False when that one leads nowhere now.
	int GetLinkSegmentCount(int Index) const;
	bool GetLinkSegment(const CFamiTrackerDoc *pDoc, int Index, int Number, unsigned int &Track, int &Channel,
		unsigned int &Pattern) const;

	// Writes the clip into all of its linked copies. Returns true if the song changed.
	bool PushToLinks(CFamiTrackerDoc *pDoc, int Index);

	// After an edit: drops links that lead nowhere, and when a linked copy no
	// longer matches its clip (it was edited), it becomes the clip and the
	// other copies follow. A copy whose cells are all empty now is unlinked
	// rather than followed. Returns true if the song changed.
	bool Sync(CFamiTrackerDoc *pDoc);

	// The song's tracks changed
	void TrackRemoved(unsigned int Track);
	void TracksSwapped(unsigned int Track1, unsigned int Track2);
	void DropLinks(unsigned int Track);

	// The module file
	bool Write(CDocumentFile *pFile) const;		// the whole block
	bool Read(CDocumentFile *pFile);	// false if the block is damaged; the library is then empty

private:
	struct Impl;
	Impl *m_pImpl;

	CClipLibrary(const CClipLibrary &);
	CClipLibrary &operator=(const CClipLibrary &);
};

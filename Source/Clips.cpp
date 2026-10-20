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

// The song's clips and their linked copies (see Clips.h).

// Standard headers come before stdafx.h, whose DEBUG_NEW macro would
// otherwise rewrite the placement new inside the standard library.
#include <algorithm>
#include <vector>
#include "stdafx.h"
#include "FamiTracker.h"
#include "FamiTrackerDoc.h"
#include "DocumentFile.h"
#include "Clips.h"

namespace {

const char *const BLOCK_ID = "BF CLIPS";	// beginner-friendly clips
const int BLOCK_VERSION = 1;
const int MAX_CLIPS = 256;
const int MAX_CLIP_ROWS = MAX_FRAMES * MAX_PATTERN_LENGTH;
const int MAX_LINKS = 4096;
const int MAX_NAME = 255;

stChanNote EmptyCell()
{
	stChanNote Cell;
	Cell.Note = NONE;
	Cell.Octave = 0;
	Cell.Vol = MAX_VOLUME;
	Cell.Instrument = MAX_INSTRUMENTS;
	for (int i = 0; i < MAX_EFFECT_COLUMNS; ++i) {
		Cell.EffNumber[i] = EF_NONE;
		Cell.EffParam[i] = 0;
	}
	return Cell;
}

// The same as far as playing goes: an octave without a note, or a parameter
// without an effect, makes no difference
bool SameCell(const stChanNote &a, const stChanNote &b)
{
	if (a.Note != b.Note || a.Vol != b.Vol || a.Instrument != b.Instrument)
		return false;
	if (a.Note >= C && a.Note <= B && a.Octave != b.Octave)
		return false;
	for (int i = 0; i < MAX_EFFECT_COLUMNS; ++i) {
		if (a.EffNumber[i] != b.EffNumber[i])
			return false;
		if (a.EffNumber[i] != EF_NONE && a.EffParam[i] != b.EffParam[i])
			return false;
	}
	return true;
}

bool IsBlankCell(const stChanNote &Cell)
{
	return SameCell(Cell, EmptyCell());
}

struct Clip {
	int Id;
	CString Name;
	int Columns;
	int Rows;
	std::vector<int> Sources;		// channel ID per column
	std::vector<stChanNote> Cells;	// Columns * Rows, a column at a time

	stChanNote &At(int Column, int Row) { return Cells[size_t(Column) * size_t(Rows) + size_t(Row)]; }
	const stChanNote &At(int Column, int Row) const { return Cells[size_t(Column) * size_t(Rows) + size_t(Row)]; }
};

// Rows ClipRow.. of a clip column, as rows PatternRow.. of a pattern
struct Segment {
	int ChanID;
	unsigned int Pattern;
	int PatternRow;
	int Column;
	int ClipRow;
	int Rows;
};

struct Link {
	int Id;
	int ClipId;
	unsigned int Track;
	std::vector<Segment> Segments;
};

// The segment's channel index in the song, -1 if that channel is gone
int ChannelOf(const CFamiTrackerDoc *pDoc, const Segment &s)
{
	const int Index = pDoc->GetChannelIndex(s.ChanID);
	return (Index >= 0 && Index < static_cast<int>(pDoc->GetAvailableChannels())) ? Index : -1;
}

// Does the segment still lead to rows of a pattern, in a clip that has those rows?
// (A pattern no frame plays at the moment still counts: undoing a removed frame
// brings it back.)
bool IsLive(const CFamiTrackerDoc *pDoc, const Link &l, const Segment &s, const Clip &c)
{
	if (l.Track >= pDoc->GetTrackCount())
		return false;
	const int Channel = ChannelOf(pDoc, s);
	if (Channel < 0 || s.Pattern >= static_cast<unsigned int>(MAX_PATTERN) || s.Rows <= 0 || s.PatternRow < 0 ||
		s.PatternRow + s.Rows > static_cast<int>(pDoc->GetPatternLength(l.Track)))
		return false;
	return s.Column >= 0 && s.Column < c.Columns && s.ClipRow >= 0 && s.ClipRow + s.Rows <= c.Rows;
}

bool Matches(const CFamiTrackerDoc *pDoc, const Link &l, const Clip &c)
{
	for (size_t i = 0; i < l.Segments.size(); ++i) {
		const Segment &s = l.Segments[i];
		const unsigned int Channel = static_cast<unsigned int>(ChannelOf(pDoc, s));
		for (int r = 0; r < s.Rows; ++r) {
			stChanNote Cell;
			pDoc->GetDataAtPattern(l.Track, s.Pattern, Channel, static_cast<unsigned int>(s.PatternRow + r), &Cell);
			if (!SameCell(Cell, c.At(s.Column, s.ClipRow + r)))
				return false;
		}
	}
	return true;
}

bool IsBlank(const CFamiTrackerDoc *pDoc, const Link &l)
{
	for (size_t i = 0; i < l.Segments.size(); ++i) {
		const Segment &s = l.Segments[i];
		const unsigned int Channel = static_cast<unsigned int>(ChannelOf(pDoc, s));
		for (int r = 0; r < s.Rows; ++r) {
			stChanNote Cell;
			pDoc->GetDataAtPattern(l.Track, s.Pattern, Channel, static_cast<unsigned int>(s.PatternRow + r), &Cell);
			if (!IsBlankCell(Cell))
				return false;
		}
	}
	return true;
}

// The clip takes the linked copy's cells
void TakeFrom(const CFamiTrackerDoc *pDoc, const Link &l, Clip &c)
{
	for (size_t i = 0; i < l.Segments.size(); ++i) {
		const Segment &s = l.Segments[i];
		const unsigned int Channel = static_cast<unsigned int>(ChannelOf(pDoc, s));
		for (int r = 0; r < s.Rows; ++r)
			pDoc->GetDataAtPattern(l.Track, s.Pattern, Channel, static_cast<unsigned int>(s.PatternRow + r), &c.At(s.Column, s.ClipRow + r));
	}
}

// The linked copy takes the clip's cells. Returns true if any changed.
bool GiveTo(CFamiTrackerDoc *pDoc, const Link &l, const Clip &c)
{
	bool Changed = false;
	for (size_t i = 0; i < l.Segments.size(); ++i) {
		const Segment &s = l.Segments[i];
		const unsigned int Channel = static_cast<unsigned int>(ChannelOf(pDoc, s));
		for (int r = 0; r < s.Rows; ++r) {
			const unsigned int Row = static_cast<unsigned int>(s.PatternRow + r);
			stChanNote Cell;
			pDoc->GetDataAtPattern(l.Track, s.Pattern, Channel, Row, &Cell);
			const stChanNote &Wanted = c.At(s.Column, s.ClipRow + r);
			if (!SameCell(Cell, Wanted)) {
				pDoc->SetDataAtPattern(l.Track, s.Pattern, Channel, Row, &Wanted);
				Changed = true;
			}
		}
	}
	return Changed;
}

// Reading a block without running past its end
class CBlockReader
{
public:
	explicit CBlockReader(CDocumentFile *pFile) : m_pFile(pFile), m_bOK(true) {}

	bool OK() const { return m_bOK; }
	bool Has(int Bytes) const { return m_bOK && m_pFile->GetBlockPos() + Bytes <= m_pFile->GetBlockSize(); }
	int Int(int Min, int Max)
	{
		if (!Has(4)) {
			m_bOK = false;
			return Min;
		}
		const int Value = m_pFile->GetBlockInt();
		if (Value < Min || Value > Max) {
			m_bOK = false;
			return Min;
		}
		return Value;
	}
	unsigned char Byte()
	{
		if (!Has(1)) {
			m_bOK = false;
			return 0;
		}
		return static_cast<unsigned char>(m_pFile->GetBlockChar());
	}
	CString String()
	{
		CString Text;
		for (int i = 0; i <= MAX_NAME; ++i) {
			const unsigned char c = Byte();
			if (!m_bOK || c == 0)
				return Text;
			Text += static_cast<TCHAR>(c);
		}
		m_bOK = false;	// no end
		return Text;
	}

private:
	CDocumentFile *m_pFile;
	bool m_bOK;
};

}	// namespace

struct CClipLibrary::Impl {
	std::vector<Clip> Clips;
	std::vector<Link> Links;
	int NextId;

	Impl() : NextId(1) {}

	Clip *FindClip(int Id)
	{
		for (size_t i = 0; i < Clips.size(); ++i)
			if (Clips[i].Id == Id)
				return &Clips[i];
		return NULL;
	}

	const Clip *FindClip(int Id) const
	{
		for (size_t i = 0; i < Clips.size(); ++i)
			if (Clips[i].Id == Id)
				return &Clips[i];
		return NULL;
	}

	void EraseLinks(int ClipId)
	{
		std::vector<Link> Kept;
		for (size_t i = 0; i < Links.size(); ++i)
			if (Links[i].ClipId != ClipId)
				Kept.push_back(Links[i]);
		Links.swap(Kept);
	}

	// Drops segments, and then links, that no longer lead anywhere
	void DropDead(const CFamiTrackerDoc *pDoc)
	{
		std::vector<Link> Kept;
		for (size_t i = 0; i < Links.size(); ++i) {
			const Clip *pClip = FindClip(Links[i].ClipId);
			if (pClip == NULL)
				continue;
			Link l = Links[i];
			std::vector<Segment> Live;
			for (size_t s = 0; s < l.Segments.size(); ++s)
				if (IsLive(pDoc, l, l.Segments[s], *pClip))
					Live.push_back(l.Segments[s]);
			if (Live.empty())
				continue;
			l.Segments.swap(Live);
			Kept.push_back(l);
		}
		Links.swap(Kept);
	}
};

struct CClipLinkState::Impl {
	std::vector<Link> Links;
};

CClipLinkState::CClipLinkState() : m_pImpl(new Impl())
{
}

CClipLinkState::~CClipLinkState()
{
	delete m_pImpl;
}

CClipLibrary::CClipLibrary() : m_pImpl(new Impl())
{
}

CClipLibrary::~CClipLibrary()
{
	delete m_pImpl;
}

void CClipLibrary::Clear()
{
	m_pImpl->Clips.clear();
	m_pImpl->Links.clear();
	m_pImpl->NextId = 1;
}

bool CClipLibrary::IsEmpty() const
{
	return m_pImpl->Clips.empty();
}

int CClipLibrary::GetCount() const
{
	return static_cast<int>(m_pImpl->Clips.size());
}

CString CClipLibrary::NewName(LPCTSTR Base, bool Numbered) const
{
	for (int Number = 1; ; ++Number) {
		CString Name(Base);
		if (Numbered || Number > 1) {
			CString Suffix;
			Suffix.Format(_T(" %d"), Number);
			Name += Suffix;
		}
		bool Taken = false;
		for (size_t i = 0; i < m_pImpl->Clips.size() && !Taken; ++i)
			Taken = m_pImpl->Clips[i].Name.CompareNoCase(Name) == 0;
		if (!Taken)
			return Name.Left(MAX_NAME);
	}
}

int CClipLibrary::Add(LPCTSTR Name, int Columns, int Rows)
{
	if (GetCount() >= MAX_CLIPS || Columns < 1 || Columns > MAX_CHANNELS || Rows < 1 || Rows > MAX_CLIP_ROWS)
		return -1;
	Clip c;
	c.Id = m_pImpl->NextId++;
	c.Name = CString(Name).Left(MAX_NAME);
	c.Columns = Columns;
	c.Rows = Rows;
	c.Sources.assign(size_t(Columns), -1);
	c.Cells.assign(size_t(Columns) * size_t(Rows), EmptyCell());
	m_pImpl->Clips.push_back(c);
	return GetCount() - 1;
}

int CClipLibrary::Duplicate(int Index, LPCTSTR Name)
{
	if (Index < 0 || Index >= GetCount() || GetCount() >= MAX_CLIPS)
		return -1;
	Clip c = m_pImpl->Clips[size_t(Index)];
	c.Id = m_pImpl->NextId++;
	c.Name = CString(Name).Left(MAX_NAME);
	m_pImpl->Clips.push_back(c);
	return GetCount() - 1;
}

void CClipLibrary::Remove(int Index)
{
	if (Index < 0 || Index >= GetCount())
		return;
	m_pImpl->EraseLinks(m_pImpl->Clips[size_t(Index)].Id);
	m_pImpl->Clips.erase(m_pImpl->Clips.begin() + Index);
}

CString CClipLibrary::GetName(int Index) const
{
	return (Index >= 0 && Index < GetCount()) ? m_pImpl->Clips[size_t(Index)].Name : CString();
}

void CClipLibrary::SetName(int Index, LPCTSTR Name)
{
	if (Index >= 0 && Index < GetCount())
		m_pImpl->Clips[size_t(Index)].Name = CString(Name).Left(MAX_NAME);
}

int CClipLibrary::GetColumns(int Index) const
{
	return (Index >= 0 && Index < GetCount()) ? m_pImpl->Clips[size_t(Index)].Columns : 0;
}

int CClipLibrary::GetRows(int Index) const
{
	return (Index >= 0 && Index < GetCount()) ? m_pImpl->Clips[size_t(Index)].Rows : 0;
}

int CClipLibrary::GetSource(int Index, int Column) const
{
	if (Index < 0 || Index >= GetCount() || Column < 0 || Column >= GetColumns(Index))
		return -1;
	return m_pImpl->Clips[size_t(Index)].Sources[size_t(Column)];
}

void CClipLibrary::SetSource(int Index, int Column, int ChanID)
{
	if (Index >= 0 && Index < GetCount() && Column >= 0 && Column < GetColumns(Index))
		m_pImpl->Clips[size_t(Index)].Sources[size_t(Column)] = ChanID;
}

void CClipLibrary::GetCell(int Index, int Column, int Row, stChanNote &Cell) const
{
	if (Index < 0 || Index >= GetCount() || Column < 0 || Column >= GetColumns(Index) || Row < 0 || Row >= GetRows(Index)) {
		Cell = EmptyCell();
		return;
	}
	Cell = m_pImpl->Clips[size_t(Index)].At(Column, Row);
}

void CClipLibrary::SetCell(int Index, int Column, int Row, const stChanNote &Cell)
{
	if (Index < 0 || Index >= GetCount() || Column < 0 || Column >= GetColumns(Index) || Row < 0 || Row >= GetRows(Index))
		return;
	stChanNote &Stored = m_pImpl->Clips[size_t(Index)].At(Column, Row);
	Stored = Cell;
	// Tidy: no octave without a note, no parameter without an effect
	if (!(Stored.Note >= C && Stored.Note <= B))
		Stored.Octave = 0;
	for (int i = 0; i < MAX_EFFECT_COLUMNS; ++i)
		if (Stored.EffNumber[i] == EF_NONE)
			Stored.EffParam[i] = 0;
}

int CClipLibrary::GetId(int Index) const
{
	return (Index >= 0 && Index < GetCount()) ? m_pImpl->Clips[size_t(Index)].Id : -1;
}

int CClipLibrary::FindIndex(int Id) const
{
	for (int i = 0; i < GetCount(); ++i)
		if (m_pImpl->Clips[size_t(i)].Id == Id)
			return i;
	return -1;
}

// Linked copies

int CClipLibrary::AddLink(const CFamiTrackerDoc *pDoc, int Index, unsigned int Track, unsigned int Frame, unsigned int Row,
	int Channel)
{
	if (Index < 0 || Index >= GetCount() || Track >= pDoc->GetTrackCount() || static_cast<int>(m_pImpl->Links.size()) >= MAX_LINKS)
		return -1;
	const Clip &c = m_pImpl->Clips[size_t(Index)];
	const unsigned int Length = pDoc->GetPatternLength(Track);
	const unsigned int Frames = pDoc->GetFrameCount(Track);
	const int Channels = static_cast<int>(pDoc->GetAvailableChannels());

	Link l;
	l.Id = m_pImpl->NextId++;
	l.ClipId = c.Id;
	l.Track = Track;
	for (int Column = 0; Column < c.Columns && Channel + Column < Channels; ++Column) {
		const unsigned int ChannelIndex = static_cast<unsigned int>(Channel + Column);
		int r = 0;
		while (r < c.Rows) {
			const unsigned int Position = Row + static_cast<unsigned int>(r);
			const unsigned int f = Frame + Position / Length;
			if (f >= Frames)
				break;
			const unsigned int PatternRow = Position % Length;
			Segment s;
			s.ChanID = pDoc->GetChannelType(static_cast<int>(ChannelIndex));
			s.Pattern = pDoc->GetPatternAtFrame(Track, f, ChannelIndex);
			s.PatternRow = static_cast<int>(PatternRow);
			s.Column = Column;
			s.ClipRow = r;
			s.Rows = std::min(c.Rows - r, static_cast<int>(Length - PatternRow));
			l.Segments.push_back(s);
			r += s.Rows;
		}
	}
	if (l.Segments.empty())
		return -1;
	m_pImpl->Links.push_back(l);
	return l.Id;
}

void CClipLibrary::RemoveLink(int LinkId)
{
	for (size_t i = 0; i < m_pImpl->Links.size(); ++i) {
		if (m_pImpl->Links[i].Id == LinkId) {
			m_pImpl->Links.erase(m_pImpl->Links.begin() + std::ptrdiff_t(i));
			return;
		}
	}
}

int CClipLibrary::GetLinkCount(int Index) const
{
	const int Id = GetId(Index);
	int Count = 0;
	for (size_t i = 0; i < m_pImpl->Links.size(); ++i)
		if (m_pImpl->Links[i].ClipId == Id)
			++Count;
	return Count;
}

void CClipLibrary::Unlink(int Index)
{
	if (Index >= 0 && Index < GetCount())
		m_pImpl->EraseLinks(m_pImpl->Clips[size_t(Index)].Id);
}

void CClipLibrary::UnlinkArea(const CFamiTrackerDoc *pDoc, unsigned int Track, unsigned int Frame, unsigned int Row,
	int Channel, int Rows)
{
	if (m_pImpl->Links.empty() || Channel < 0 || Channel >= static_cast<int>(pDoc->GetAvailableChannels()) ||
		Track >= pDoc->GetTrackCount())
		return;
	const unsigned int Length = pDoc->GetPatternLength(Track);
	const unsigned int Frames = pDoc->GetFrameCount(Track);
	const int ChanID = pDoc->GetChannelType(Channel);

	// The pattern rows written, a frame at a time
	std::vector<Link> Kept;
	for (size_t i = 0; i < m_pImpl->Links.size(); ++i) {
		const Link &l = m_pImpl->Links[i];
		bool Hit = false;
		for (size_t k = 0; k < l.Segments.size() && !Hit && l.Track == Track; ++k) {
			const Segment &s = l.Segments[k];
			if (s.ChanID != ChanID)
				continue;
			int r = 0;
			while (r < Rows && !Hit) {
				const unsigned int Position = Row + static_cast<unsigned int>(r);
				const unsigned int f = Frame + Position / Length;
				if (f >= Frames)
					break;
				const int PatternRow = static_cast<int>(Position % Length);
				const int Count = std::min(Rows - r, static_cast<int>(Length) - PatternRow);
				Hit = pDoc->GetPatternAtFrame(Track, f, static_cast<unsigned int>(Channel)) == s.Pattern &&
					PatternRow < s.PatternRow + s.Rows && s.PatternRow < PatternRow + Count;
				r += Count;
			}
		}
		if (!Hit)
			Kept.push_back(l);
	}
	m_pImpl->Links.swap(Kept);
}

void CClipLibrary::SaveLinks(CClipLinkState &State) const
{
	State.m_pImpl->Links = m_pImpl->Links;
}

void CClipLibrary::RestoreLinks(const CClipLinkState &State)
{
	// Links to clips deleted since then stay gone
	m_pImpl->Links.clear();
	for (size_t i = 0; i < State.m_pImpl->Links.size(); ++i)
		if (m_pImpl->FindClip(State.m_pImpl->Links[i].ClipId) != NULL)
			m_pImpl->Links.push_back(State.m_pImpl->Links[i]);
}

int CClipLibrary::GetLinkSegmentCount(int Index) const
{
	const int Id = GetId(Index);
	int Count = 0;
	for (size_t i = 0; i < m_pImpl->Links.size(); ++i)
		if (m_pImpl->Links[i].ClipId == Id)
			Count += static_cast<int>(m_pImpl->Links[i].Segments.size());
	return Count;
}

bool CClipLibrary::GetLinkSegment(const CFamiTrackerDoc *pDoc, int Index, int Number, unsigned int &Track, int &Channel,
	unsigned int &Pattern) const
{
	const int Id = GetId(Index);
	if (Id < 0 || Number < 0)
		return false;
	const Clip &c = m_pImpl->Clips[size_t(Index)];
	for (size_t i = 0; i < m_pImpl->Links.size(); ++i) {
		const Link &l = m_pImpl->Links[i];
		if (l.ClipId != Id)
			continue;
		if (Number >= static_cast<int>(l.Segments.size())) {
			Number -= static_cast<int>(l.Segments.size());
			continue;
		}
		const Segment &s = l.Segments[size_t(Number)];
		if (!IsLive(pDoc, l, s, c))
			return false;
		Track = l.Track;
		Channel = ChannelOf(pDoc, s);
		Pattern = s.Pattern;
		return true;
	}
	return false;
}

bool CClipLibrary::PushToLinks(CFamiTrackerDoc *pDoc, int Index)
{
	if (Index < 0 || Index >= GetCount())
		return false;
	m_pImpl->DropDead(pDoc);
	const Clip &c = m_pImpl->Clips[size_t(Index)];
	bool Changed = false;
	pDoc->LockDocument();
	for (size_t i = 0; i < m_pImpl->Links.size(); ++i)
		if (m_pImpl->Links[i].ClipId == c.Id && GiveTo(pDoc, m_pImpl->Links[i], c))
			Changed = true;
	pDoc->UnlockDocument();
	return Changed;
}

bool CClipLibrary::Sync(CFamiTrackerDoc *pDoc)
{
	if (m_pImpl->Links.empty())
		return false;
	m_pImpl->DropDead(pDoc);

	bool Changed = false;
	std::vector<int> Unlinked;	// copies whose notes were all deleted
	pDoc->LockDocument();
	for (size_t i = 0; i < m_pImpl->Clips.size(); ++i) {
		Clip &c = m_pImpl->Clips[i];
		int Edited = -1;
		for (size_t k = 0; k < m_pImpl->Links.size(); ++k) {
			const Link &l = m_pImpl->Links[k];
			if (l.ClipId != c.Id || Matches(pDoc, l, c))
				continue;
			if (IsBlank(pDoc, l))
				Unlinked.push_back(l.Id);
			else if (Edited < 0)
				Edited = static_cast<int>(k);
		}
		if (Edited < 0)
			continue;
		TakeFrom(pDoc, m_pImpl->Links[size_t(Edited)], c);
		for (size_t k = 0; k < m_pImpl->Links.size(); ++k) {
			const Link &l = m_pImpl->Links[k];
			if (l.ClipId != c.Id || static_cast<int>(k) == Edited ||
				std::find(Unlinked.begin(), Unlinked.end(), l.Id) != Unlinked.end())
				continue;
			if (GiveTo(pDoc, l, c))
				Changed = true;
		}
	}
	pDoc->UnlockDocument();
	for (size_t i = 0; i < Unlinked.size(); ++i)
		RemoveLink(Unlinked[i]);
	return Changed;
}

// The song's tracks

void CClipLibrary::TrackRemoved(unsigned int Track)
{
	std::vector<Link> Kept;
	for (size_t i = 0; i < m_pImpl->Links.size(); ++i) {
		Link l = m_pImpl->Links[i];
		if (l.Track == Track)
			continue;
		if (l.Track > Track)
			--l.Track;
		Kept.push_back(l);
	}
	m_pImpl->Links.swap(Kept);
}

void CClipLibrary::TracksSwapped(unsigned int Track1, unsigned int Track2)
{
	for (size_t i = 0; i < m_pImpl->Links.size(); ++i) {
		Link &l = m_pImpl->Links[i];
		if (l.Track == Track1)
			l.Track = Track2;
		else if (l.Track == Track2)
			l.Track = Track1;
	}
}

void CClipLibrary::DropLinks(unsigned int Track)
{
	std::vector<Link> Kept;
	for (size_t i = 0; i < m_pImpl->Links.size(); ++i)
		if (m_pImpl->Links[i].Track != Track)
			Kept.push_back(m_pImpl->Links[i]);
	m_pImpl->Links.swap(Kept);
}

// The module file

bool CClipLibrary::Write(CDocumentFile *pFile) const
{
	pFile->CreateBlock(BLOCK_ID, BLOCK_VERSION);
	pFile->WriteBlockInt(GetCount());
	for (size_t i = 0; i < m_pImpl->Clips.size(); ++i) {
		const Clip &c = m_pImpl->Clips[i];
		pFile->WriteBlockInt(c.Id);
		pFile->WriteString(c.Name);
		pFile->WriteBlockInt(c.Columns);
		pFile->WriteBlockInt(c.Rows);
		for (int Column = 0; Column < c.Columns; ++Column)
			pFile->WriteBlockInt(c.Sources[size_t(Column)]);
		for (size_t k = 0; k < c.Cells.size(); ++k) {
			const stChanNote &Cell = c.Cells[k];
			pFile->WriteBlockChar(static_cast<char>(Cell.Note));
			pFile->WriteBlockChar(static_cast<char>(Cell.Octave));
			pFile->WriteBlockChar(static_cast<char>(Cell.Vol));
			pFile->WriteBlockChar(static_cast<char>(Cell.Instrument));
			for (int e = 0; e < MAX_EFFECT_COLUMNS; ++e) {
				pFile->WriteBlockChar(static_cast<char>(Cell.EffNumber[e]));
				pFile->WriteBlockChar(static_cast<char>(Cell.EffParam[e]));
			}
		}
	}
	pFile->WriteBlockInt(m_pImpl->NextId);
	pFile->WriteBlockInt(static_cast<int>(m_pImpl->Links.size()));
	for (size_t i = 0; i < m_pImpl->Links.size(); ++i) {
		const Link &l = m_pImpl->Links[i];
		pFile->WriteBlockInt(l.Id);
		pFile->WriteBlockInt(l.ClipId);
		pFile->WriteBlockInt(static_cast<int>(l.Track));
		pFile->WriteBlockInt(static_cast<int>(l.Segments.size()));
		for (size_t s = 0; s < l.Segments.size(); ++s) {
			const Segment &Seg = l.Segments[s];
			pFile->WriteBlockInt(Seg.ChanID);
			pFile->WriteBlockInt(static_cast<int>(Seg.Pattern));
			pFile->WriteBlockInt(Seg.PatternRow);
			pFile->WriteBlockInt(Seg.Column);
			pFile->WriteBlockInt(Seg.ClipRow);
			pFile->WriteBlockInt(Seg.Rows);
		}
	}
	return pFile->FlushBlock();
}

bool CClipLibrary::Read(CDocumentFile *pFile)
{
	Clear();
	if (pFile->GetBlockVersion() != BLOCK_VERSION)
		return true;	// from a newer version: left out

	CBlockReader Reader(pFile);
	const int Count = Reader.Int(0, MAX_CLIPS);
	for (int i = 0; i < Count && Reader.OK(); ++i) {
		Clip c;
		c.Id = Reader.Int(1, 0x7FFFFFFF);
		c.Name = Reader.String();
		c.Columns = Reader.Int(1, MAX_CHANNELS);
		c.Rows = Reader.Int(1, MAX_CLIP_ROWS);
		if (!Reader.OK() || !Reader.Has(c.Columns * 4 + c.Columns * c.Rows * (4 + 2 * MAX_EFFECT_COLUMNS)) ||
			m_pImpl->FindClip(c.Id) != NULL) {
			Clear();	// damaged: not that much data left, or the same clip twice
			return false;
		}
		for (int Column = 0; Column < c.Columns; ++Column)
			c.Sources.push_back(Reader.Int(-1, 0xFFFF));
		c.Cells.resize(size_t(c.Columns) * size_t(c.Rows));
		for (size_t k = 0; k < c.Cells.size() && Reader.OK(); ++k) {
			// Only values a pattern can hold
			stChanNote &Cell = c.Cells[k];
			Cell.Note = Reader.Byte();
			Cell.Octave = Reader.Byte();
			Cell.Vol = Reader.Byte();
			Cell.Instrument = Reader.Byte();
			bool Valid = Cell.Note <= HALT && Cell.Octave < OCTAVE_RANGE && Cell.Vol <= MAX_VOLUME &&
				Cell.Instrument <= MAX_INSTRUMENTS;
			for (int e = 0; e < MAX_EFFECT_COLUMNS; ++e) {
				Cell.EffNumber[e] = Reader.Byte();
				Cell.EffParam[e] = Reader.Byte();
				Valid = Valid && Cell.EffNumber[e] < EF_COUNT;
			}
			if (!Valid) {
				Clear();
				return false;
			}
		}
		m_pImpl->Clips.push_back(c);
	}
	m_pImpl->NextId = Reader.Int(1, 0x7FFFFFFF);
	const int Links = Reader.Int(0, MAX_LINKS);
	for (int i = 0; i < Links && Reader.OK(); ++i) {
		Link l;
		l.Id = Reader.Int(1, 0x7FFFFFFF);
		l.ClipId = Reader.Int(1, 0x7FFFFFFF);
		l.Track = static_cast<unsigned int>(Reader.Int(0, MAX_TRACKS - 1));
		const int Segments = Reader.Int(1, MAX_CHANNELS * MAX_FRAMES);
		for (int s = 0; s < Segments && Reader.OK(); ++s) {
			Segment Seg;
			Seg.ChanID = Reader.Int(0, 0xFFFF);
			Seg.Pattern = static_cast<unsigned int>(Reader.Int(0, MAX_PATTERN - 1));
			Seg.PatternRow = Reader.Int(0, MAX_PATTERN_LENGTH - 1);
			Seg.Column = Reader.Int(0, MAX_CHANNELS - 1);
			Seg.ClipRow = Reader.Int(0, MAX_CLIP_ROWS - 1);
			Seg.Rows = Reader.Int(1, MAX_PATTERN_LENGTH);
			l.Segments.push_back(Seg);
		}
		m_pImpl->Links.push_back(l);
	}
	if (!Reader.OK()) {
		Clear();
		return false;
	}

	// IDs stay unique
	int Highest = 0;
	for (size_t i = 0; i < m_pImpl->Clips.size(); ++i)
		Highest = std::max(Highest, m_pImpl->Clips[i].Id);
	for (size_t i = 0; i < m_pImpl->Links.size(); ++i)
		Highest = std::max(Highest, m_pImpl->Links[i].Id);
	m_pImpl->NextId = std::max(m_pImpl->NextId, Highest + 1);
	return true;
}

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

// Edit > Insert recording, the Record dialog and Edit > Add chords and bass
// line. The transcription engine in Transcribe/ finds the notes (and the
// chords); this file turns them into pattern cells and writes them into the
// open song as one undoable action.

// Standard and engine headers come before stdafx.h, whose DEBUG_NEW macro
// would otherwise rewrite the placement new inside the standard library.
#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "Transcribe/Transcribe.h"
#include "Api/Sections.h"
#include "stdafx.h"
#include "FamiTracker.h"
#include "FamiTrackerDoc.h"
#include "FamiTrackerView.h"
#include "MainFrm.h"
#include "PatternEditor.h"
#include "TrackerChannel.h"
#include "Action.h"
#include "Clips.h"
#include "InsertRecording.h"

namespace {

// Action IDs, well away from the CPatternAction and CFrameAction values
const int ACT_INSERT_RECORDING = 0x1000;
const int ACT_EDIT_CLIP = 0x1001;

// Volume envelopes, the same as wav2ftm writes (Transcribe/TextModule.cpp).
// Both hold at 10 on their release point for as long as the note lasts, then
// fade out on the note release. The lead swells in like a whistle; the bass
// starts with a small accent.
const signed char LEAD_VOLUME[] = { 11, 14, 15, 15, 14, 13, 12, 11, 10, 7, 4, 2, 0 };
const int LEAD_RELEASE = 8;
const signed char BASS_VOLUME[] = { 15, 14, 13, 12, 11, 10, 7, 4, 2, 0 };
const int BASS_RELEASE = 5;
// Lead duty cycle: 25% on the 2A03; on the VRC6 a slow shimmer between 4/16 and 3/16
const signed char LEAD_DUTY_2A03[] = { 1 };
const signed char LEAD_DUTY_VRC6[] = { 3, 3, 3, 3, 3, 3, 2, 2, 2, 2, 2, 2 };
// Chords: soft and thin under the melody, holding at 6 (12.5% duty)
const signed char CHORD_VOLUME[] = { 10, 9, 8, 8, 7, 7, 6, 6, 4, 2, 1, 0 };
const int CHORD_RELEASE = 7;
const signed char CHORD_DUTY_2A03[] = { 0 };
const signed char CHORD_DUTY_VRC6[] = { 1 };
const signed char KICK_VOLUME[] = { 15, 12, 8, 5, 3, 1, 0 };
const signed char SNARE_VOLUME[] = { 14, 11, 9, 7, 5, 3, 2, 1, 0 };
const signed char HIHAT_VOLUME[] = { 10, 6, 3, 1, 0 };

struct RecordedCell {
	int Row;					// rows after the start row
	unsigned char Note;			// note_t
	unsigned char Octave;
	unsigned char Instrument;
	unsigned char Vol;
	unsigned char Effect[MAX_EFFECT_COLUMNS];		// effect_t, EF_NONE for none
	unsigned char EffectParam[MAX_EFFECT_COLUMNS];
};

RecordedCell MakeCell(int Row, int Note, int Octave, int Instrument, int Vol, int Effect = EF_NONE, int EffectParam = 0)
{
	RecordedCell Cell;
	Cell.Row = Row;
	Cell.Note = static_cast<unsigned char>(Note);
	Cell.Octave = static_cast<unsigned char>(Octave);
	Cell.Instrument = static_cast<unsigned char>(Instrument);
	Cell.Vol = static_cast<unsigned char>(Vol);
	for (int c = 0; c < MAX_EFFECT_COLUMNS; ++c) {
		Cell.Effect[c] = EF_NONE;
		Cell.EffectParam[c] = 0;
	}
	Cell.Effect[0] = static_cast<unsigned char>(Effect);
	Cell.EffectParam[0] = static_cast<unsigned char>(EffectParam);
	return Cell;
}

// A pattern cell as it is, at Row
RecordedCell CellOf(int Row, const stChanNote &Note)
{
	RecordedCell Cell = MakeCell(Row, Note.Note, Note.Octave, Note.Instrument, Note.Vol);
	for (int c = 0; c < MAX_EFFECT_COLUMNS; ++c) {
		Cell.Effect[c] = Note.EffNumber[c];
		Cell.EffectParam[c] = Note.EffParam[c];
	}
	return Cell;
}

// The effects the recorder writes: slides, vibrato and portamento
bool IsExpressionEffect(unsigned char Effect)
{
	return Effect == EF_SLIDE_UP || Effect == EF_SLIDE_DOWN || Effect == EF_VIBRATO || Effect == EF_PORTAMENTO;
}

// Effects that steer the whole song: speed and tempo, jumps, skips and halts
bool IsSongEffect(unsigned char Effect)
{
	return Effect == EF_SPEED || Effect == EF_JUMP || Effect == EF_SKIP || Effect == EF_HALT;
}

bool HoldsEffect(const stChanNote &Note, unsigned char Effect, unsigned char Param)
{
	for (int c = 0; c < MAX_EFFECT_COLUMNS; ++c) {
		if (Note.EffNumber[c] == Effect && Note.EffParam[c] == Param)
			return true;
	}
	return false;
}

// Does any frame of the track other than Frame play Pattern in Channel?
bool UsedElsewhere(const CFamiTrackerDoc *pDoc, unsigned int Track, unsigned int Channel, unsigned int Frame, unsigned int Pattern)
{
	const unsigned int Frames = pDoc->GetFrameCount(Track);
	for (unsigned int f = 0; f < Frames; ++f) {
		if (f != Frame && pDoc->GetPatternAtFrame(Track, f, Channel) == Pattern)
			return true;
	}
	return false;
}

bool UsedAnywhere(const CFamiTrackerDoc *pDoc, unsigned int Track, unsigned int Channel, unsigned int Pattern)
{
	return UsedElsewhere(pDoc, Track, Channel, MAX_FRAMES, Pattern);
}

// A pattern that no frame plays and that holds nothing, or -1
int FindFreePattern(const CFamiTrackerDoc *pDoc, unsigned int Track, unsigned int Channel)
{
	for (unsigned int p = 0; p < MAX_PATTERN; ++p) {
		if (!UsedAnywhere(pDoc, Track, Channel, p) && pDoc->IsPatternEmpty(Track, Channel, p))
			return static_cast<int>(p);
	}
	return -1;
}

void EnableSequence(CInstrument *pInstrument, int Type, int Index)
{
	if (pInstrument->GetType() == INST_VRC6) {
		CInstrumentVRC6 *pVRC6 = static_cast<CInstrumentVRC6*>(pInstrument);
		pVRC6->SetSeqEnable(Type, 1);
		pVRC6->SetSeqIndex(Type, Index);
	}
	else {
		CInstrument2A03 *p2A03 = static_cast<CInstrument2A03*>(pInstrument);
		p2A03->SetSeqEnable(Type, 1);
		p2A03->SetSeqIndex(Type, Index);
	}
}

// Fills a free sequence slot and returns its index, or -1 when all are taken
int AddSequence(CFamiTrackerDoc *pDoc, inst_type_t Type, int SeqType, const signed char *pItems, int Count, int Release)
{
	const int Index = (Type == INST_VRC6) ? pDoc->GetFreeSequenceVRC6(SeqType) : pDoc->GetFreeSequence(SeqType);
	if (Index < 0)
		return -1;
	CSequence *pSeq = pDoc->GetSequence((Type == INST_VRC6) ? SNDCHIP_VRC6 : SNDCHIP_NONE, Index, SeqType);
	pSeq->Clear();
	for (int i = 0; i < Count; ++i)
		pSeq->SetItem(i, pItems[i]);
	pSeq->SetItemCount(static_cast<unsigned int>(Count));
	if (Release >= 0)
		pSeq->SetReleasePoint(static_cast<unsigned int>(Release));
	return Index;
}

// The instrument called pName for this chip, added if the song doesn't have
// it yet. Inserting more recordings reuses it, so they all sound alike and can
// be changed in one place. The duty sequence loops when it has several items.
int FindOrAddInstrument(CFamiTrackerDoc *pDoc, const char *pName, int Chip, const signed char *pVolume, int VolumeCount,
	int Release, const signed char *pDuty, int DutyCount)
{
	const inst_type_t Type = (Chip == SNDCHIP_VRC6) ? INST_VRC6 : INST_2A03;	// MMC5 uses 2A03 instruments
	for (int i = 0; i < MAX_INSTRUMENTS; ++i) {
		if (!pDoc->IsInstrumentUsed(i) || pDoc->GetInstrumentType(i) != Type)
			continue;
		char Name[CInstrument::INST_NAME_MAX];
		pDoc->GetInstrumentName(i, Name);
		if (std::strcmp(Name, pName) == 0)
			return i;
	}

	const int Index = pDoc->AddInstrument(pName, Chip);
	if (Index == INVALID_INSTRUMENT)
		return INVALID_INSTRUMENT;

	CInstrument *pInstrument = pDoc->GetInstrument(Index);	// retained
	const int Volume = AddSequence(pDoc, Type, SEQ_VOLUME, pVolume, VolumeCount, Release);
	if (Volume >= 0)
		EnableSequence(pInstrument, SEQ_VOLUME, Volume);
	if (DutyCount > 0) {
		const int Cycle = AddSequence(pDoc, Type, SEQ_DUTYCYCLE, pDuty, DutyCount, -1);
		if (Cycle >= 0) {
			if (DutyCount > 1)
				pDoc->GetSequence((Type == INST_VRC6) ? SNDCHIP_VRC6 : SNDCHIP_NONE, Cycle, SEQ_DUTYCYCLE)->SetLoopPoint(0);
			EnableSequence(pInstrument, SEQ_DUTYCYCLE, Cycle);
		}
	}
	pInstrument->Release();
	return Index;
}

// One channel's part of an insert
struct RecordedPart {
	unsigned int Channel;
	std::vector<RecordedCell> Cells;	// sorted by row
	int Rows;							// rows written, counted from the start row
	bool Replace;						// clear the effect columns too, not just earlier slides and vibrato
	bool Exact = false;					// a cell for every row, written as it is (the piano roll)
};

bool SameCell(const stChanNote &Note, const RecordedCell &Cell)
{
	if (Note.Note != Cell.Note || Note.Octave != Cell.Octave || Note.Instrument != Cell.Instrument || Note.Vol != Cell.Vol)
		return false;
	for (int c = 0; c < MAX_EFFECT_COLUMNS; ++c) {
		if (Note.EffNumber[c] != Cell.Effect[c] || Note.EffParam[c] != Cell.EffectParam[c])
			return false;
	}
	return true;
}

// Does an exact part, written from Row of Frame on, change anything in frame f (one the song has)?
bool ChangesFrame(const CFamiTrackerDoc *pDoc, unsigned int Track, const RecordedPart &Part, unsigned int Frame,
	unsigned int Row, unsigned int f)
{
	const int Length = static_cast<int>(pDoc->GetPatternLength(Track));
	for (int r = 0; r < Length; ++r) {
		const int Index = static_cast<int>(f - Frame) * Length + r - static_cast<int>(Row);
		if (Index < 0 || Index >= Part.Rows || static_cast<size_t>(Index) >= Part.Cells.size())
			continue;
		stChanNote Note;
		pDoc->GetNoteData(Track, f, Part.Channel, static_cast<unsigned int>(r), &Note);
		if (!SameCell(Note, Part.Cells[static_cast<size_t>(Index)]))
			return true;
	}
	return false;
}

// Writes channels' cells from a frame and row on. Every frame a channel's part
// touches first gets a pattern of its own in that channel, so frames that
// played the same pattern stay as they were. Frames past the end of the song
// are added. Linked copies of clips that get written over become plain notes.
class CInsertRecordingAction : public CAction
{
public:
	CInsertRecordingAction(const std::vector<RecordedPart> &Parts, int Frame, int Row) :
		CAction(ACT_INSERT_RECORDING), m_Parts(Parts),
		m_iTrack(0), m_iFrame(static_cast<unsigned int>(Frame)), m_iRow(static_cast<unsigned int>(Row)),
		m_iOldFrameCount(0), m_iLinkClip(-1), m_bKeepLinks(false),
		m_iCursorFrame(static_cast<unsigned int>(Frame)), m_iCursorRow(static_cast<unsigned int>(Row)),
		m_iMoveFrame(0), m_iMoveBy(0), m_bSetSections(false)
	{
	}

	// The track's sections afterwards (Api/Sections.h), set once the frames
	// moved and the cells are written; otherwise they follow the frames moved
	void SetSections(const ftapi::SectionList &Sections)
	{
		m_bSetSections = true;
		m_SectionsAfter = Sections;
	}

	// Before the cells are written, By frames put in at Frame (each with new
	// empty patterns) or -By frames there taken out, as the frame list does
	// it; the parts count with the frames moved
	void MoveFrames(int Frame, int By)
	{
		m_iMoveFrame = Frame;
		m_iMoveBy = By;
	}

	// What's written is a linked copy of this clip (by ID)
	void LinkTo(int ClipId) { m_iLinkClip = ClipId; }

	// An edit of the notes there rather than something new: linked copies of clips written over
	// stay linked, and the clip follows the edit (CMainFrame::SyncClips)
	void KeepLinks() { m_bKeepLinks = true; }

	// Where the cursor goes afterwards, instead of the start
	void PlaceCursor(int Frame, int Row)
	{
		m_iCursorFrame = static_cast<unsigned int>(Frame);
		m_iCursorRow = static_cast<unsigned int>(Row);
	}

	bool SaveState(CMainFrame *pMainFrm);
	void Undo(CMainFrame *pMainFrm);
	void Redo(CMainFrame *pMainFrm);

private:
	struct Reassigned {
		unsigned int Frame;
		unsigned int Channel;
		unsigned int OldPattern;
		unsigned int NewPattern;
	};
	typedef std::pair<unsigned int, unsigned int> PatternKey;	// channel, pattern

	unsigned int LastFrame(const RecordedPart &Part, unsigned int Length) const;
	void Backup(const CFamiTrackerDoc *pDoc, unsigned int Channel, unsigned int Pattern);
	void Finish(CMainFrame *pMainFrm, CFamiTrackerDoc *pDoc, CPatternEditor *pEditor) const;

	std::vector<RecordedPart> m_Parts;
	unsigned int m_iTrack;
	unsigned int m_iFrame;
	unsigned int m_iRow;
	unsigned int m_iOldFrameCount;
	std::vector<Reassigned> m_Reassigned;
	std::map<PatternKey, std::vector<stChanNote> > m_Backup;	// a pattern's rows before the insert
	int m_iLinkClip;	// clip ID, -1 for no link
	CClipLinkState m_LinksBefore;
	bool m_bKeepLinks;
	unsigned int m_iCursorFrame;
	unsigned int m_iCursorRow;
	int m_iMoveFrame;
	int m_iMoveBy;
	std::vector<unsigned int> m_FrameList;	// the patterns of the frames before frames moved, a frame at a time
	ftapi::SectionList m_SectionsBefore;	// the track's sections, which undo brings back
	bool m_bSetSections;
	ftapi::SectionList m_SectionsAfter;
	std::vector<std::pair<unsigned int, unsigned int> > m_Widened;	// channels that show more effect columns, and what they showed
};

bool CInsertRecordingAction::SaveState(CMainFrame *pMainFrm)
{
	m_iTrack = static_cast<unsigned int>(pMainFrm->GetSelectedTrack());
	Redo(pMainFrm);
	return true;
}

unsigned int CInsertRecordingAction::LastFrame(const RecordedPart &Part, unsigned int Length) const
{
	return m_iFrame + (m_iRow + static_cast<unsigned int>(std::max(1, Part.Rows)) - 1) / Length;
}

void CInsertRecordingAction::Backup(const CFamiTrackerDoc *pDoc, unsigned int Channel, unsigned int Pattern)
{
	const PatternKey Key(Channel, Pattern);
	if (m_Backup.count(Key) != 0)
		return;
	const unsigned int Length = pDoc->GetPatternLength(m_iTrack);
	std::vector<stChanNote> &Rows = m_Backup[Key];
	Rows.resize(Length);
	for (unsigned int r = 0; r < Length; ++r)
		pDoc->GetDataAtPattern(m_iTrack, Pattern, Channel, r, &Rows[r]);
}

void CInsertRecordingAction::Redo(CMainFrame *pMainFrm)
{
	CFamiTrackerView *pView = static_cast<CFamiTrackerView*>(pMainFrm->GetActiveView());
	CFamiTrackerDoc *pDoc = pView->GetDocument();
	CPatternEditor *pEditor = pView->GetPatternEditor();
	const unsigned int Length = pDoc->GetPatternLength(m_iTrack);

	pDoc->LockDocument();
	m_iOldFrameCount = pDoc->GetFrameCount(m_iTrack);
	m_SectionsBefore = pDoc->GetSections()->Get(static_cast<int>(m_iTrack));

	// Frames put in or taken out first, the frame list kept for Undo (the sections follow them)
	if (m_iMoveBy != 0) {
		const unsigned int Channels = pDoc->GetAvailableChannels();
		m_FrameList.clear();
		for (unsigned int f = 0; f < m_iOldFrameCount; ++f)
			for (unsigned int c = 0; c < Channels; ++c)
				m_FrameList.push_back(pDoc->GetPatternAtFrame(m_iTrack, f, c));
		for (int i = 0; i < m_iMoveBy; ++i)
			pDoc->InsertFrame(m_iTrack, static_cast<unsigned int>(m_iMoveFrame + i));	// new free patterns
		for (int i = 0; i < -m_iMoveBy; ++i)
			pDoc->RemoveFrame(m_iTrack, static_cast<unsigned int>(m_iMoveFrame));
	}

	// Add frames at the end of the song when the insert runs past it
	unsigned int Last = m_iFrame;
	for (size_t p = 0; p < m_Parts.size(); ++p)
		Last = std::max(Last, LastFrame(m_Parts[p], Length));
	while (!m_Parts.empty() && pDoc->GetFrameCount(m_iTrack) <= Last) {
		if (!pDoc->InsertFrame(m_iTrack, pDoc->GetFrameCount(m_iTrack)))
			break;	// MAX_FRAMES
	}
	const unsigned int Frames = pDoc->GetFrameCount(m_iTrack);

	// One pattern per touched frame in each channel (for an exact part, per frame it changes)
	m_Reassigned.clear();
	m_Backup.clear();
	for (size_t p = 0; p < m_Parts.size(); ++p) {
		const unsigned int Channel = m_Parts[p].Channel;
		const unsigned int PartLast = LastFrame(m_Parts[p], Length);
		for (unsigned int f = m_iFrame; f <= PartLast && f < Frames; ++f) {
			if (m_Parts[p].Exact && !ChangesFrame(pDoc, m_iTrack, m_Parts[p], m_iFrame, m_iRow, f))
				continue;	// (frames added hold new empty patterns)
			unsigned int Pattern = pDoc->GetPatternAtFrame(m_iTrack, f, Channel);
			if (UsedElsewhere(pDoc, m_iTrack, Channel, f, Pattern)) {
				const int Free = FindFreePattern(pDoc, m_iTrack, Channel);
				if (Free >= 0) {
					const unsigned int NewPattern = static_cast<unsigned int>(Free);
					Backup(pDoc, Channel, NewPattern);
					pDoc->CopyPattern(m_iTrack, Free, Pattern, Channel);
					pDoc->SetPatternAtFrame(m_iTrack, f, Channel, NewPattern);
					Reassigned Entry = { f, Channel, Pattern, NewPattern };
					m_Reassigned.push_back(Entry);
					Pattern = NewPattern;
				}
			}
			Backup(pDoc, Channel, Pattern);
		}
	}

	// Linked copies written over aren't linked any more (unless the notes there are being edited);
	// an exact part writes over only the rows it changes
	CClipLibrary *pClips = pDoc->GetClips();
	pClips->SaveLinks(m_LinksBefore);
	for (size_t p = 0; p < m_Parts.size() && !m_bKeepLinks; ++p) {
		const RecordedPart &Part = m_Parts[p];
		if (!Part.Exact) {
			pClips->UnlinkArea(pDoc, m_iTrack, m_iFrame, m_iRow, static_cast<int>(Part.Channel), Part.Rows);
			continue;
		}
		int Run = -1;	// the first row of changed rows in a row
		for (int r = 0; r <= Part.Rows; ++r) {
			bool Changed = false;
			const unsigned int Position = m_iRow + static_cast<unsigned int>(r);
			if (r < Part.Rows && static_cast<size_t>(r) < Part.Cells.size() && m_iFrame + Position / Length < Frames) {
				stChanNote Note;
				pDoc->GetNoteData(m_iTrack, m_iFrame + Position / Length, Part.Channel, Position % Length, &Note);
				Changed = !SameCell(Note, Part.Cells[static_cast<size_t>(r)]);
			}
			if (Changed && Run < 0)
				Run = r;
			else if (!Changed && Run >= 0) {
				pClips->UnlinkArea(pDoc, m_iTrack, m_iFrame, m_iRow + static_cast<unsigned int>(Run), static_cast<int>(Part.Channel), r - Run);
				Run = -1;
			}
		}
	}

	// Write the cells. Rows between them are cleared in the channel (note,
	// instrument and volume). Effects stay, except earlier slides and vibrato
	// in the first effect column, which would play over the new notes; a
	// replacing part clears the effect columns too, except effects that steer
	// the song (speed, jumps, skips and halts), so its loop keeps working. A
	// new effect whose column holds one of those goes in a free column, and
	// one of those the row keeps already isn't written twice.
	for (size_t p = 0; p < m_Parts.size(); ++p) {
		const RecordedPart &Part = m_Parts[p];
		const int Shown = static_cast<int>(pDoc->GetEffColumns(m_iTrack, Part.Channel)) + 1;	// the columns that play
		size_t Next = 0;
		for (int r = 0; r < Part.Rows; ++r) {
			const unsigned int Position = m_iRow + static_cast<unsigned int>(r);
			const unsigned int Frame = m_iFrame + Position / Length;
			if (Frame >= Frames)
				break;
			const unsigned int Row = Position % Length;
			stChanNote Note;
			pDoc->GetNoteData(m_iTrack, Frame, Part.Channel, Row, &Note);
			if (Part.Exact) {
				if (static_cast<size_t>(r) < Part.Cells.size() && !SameCell(Note, Part.Cells[static_cast<size_t>(r)])) {
					const RecordedCell &Cell = Part.Cells[static_cast<size_t>(r)];
					Note.Note = Cell.Note;
					Note.Octave = Cell.Octave;
					Note.Instrument = Cell.Instrument;
					Note.Vol = Cell.Vol;
					for (int c = 0; c < MAX_EFFECT_COLUMNS; ++c) {
						Note.EffNumber[c] = Cell.Effect[c];
						Note.EffParam[c] = Cell.EffectParam[c];
					}
					pDoc->SetNoteData(m_iTrack, Frame, Part.Channel, Row, &Note);
				}
				continue;
			}
			if (Part.Replace) {
				for (int c = 0; c < MAX_EFFECT_COLUMNS; ++c) {
					if (IsSongEffect(Note.EffNumber[c]))
						continue;
					Note.EffNumber[c] = EF_NONE;
					Note.EffParam[c] = 0;
				}
			}
			else if (IsExpressionEffect(Note.EffNumber[0])) {
				Note.EffNumber[0] = EF_NONE;
				Note.EffParam[0] = 0;
			}
			if (Next < Part.Cells.size() && Part.Cells[Next].Row == r) {
				const RecordedCell &Cell = Part.Cells[Next++];
				Note.Note = Cell.Note;
				Note.Octave = Cell.Octave;
				Note.Instrument = Cell.Instrument;
				Note.Vol = Cell.Vol;
				for (int c = 0; c < MAX_EFFECT_COLUMNS; ++c) {
					if (Cell.Effect[c] == EF_NONE || (IsSongEffect(Cell.Effect[c]) && HoldsEffect(Note, Cell.Effect[c], Cell.EffectParam[c])))
						continue;
					int Column = c;
					if (IsSongEffect(Note.EffNumber[Column])) {
						Column = -1;
						for (int k = 0; k < Shown && k < MAX_EFFECT_COLUMNS && Column < 0; ++k) {
							if (Note.EffNumber[k] == EF_NONE)
								Column = k;
						}
						if (Column < 0)
							continue;	// no room: the song's effect wins
					}
					Note.EffNumber[Column] = Cell.Effect[c];
					Note.EffParam[Column] = Cell.EffectParam[c];
				}
			}
			else {
				Note.Note = NONE;
				Note.Octave = 0;
				Note.Instrument = static_cast<unsigned char>(MAX_INSTRUMENTS);
				Note.Vol = static_cast<unsigned char>(MAX_VOLUME);
			}
			pDoc->SetNoteData(m_iTrack, Frame, Part.Channel, Row, &Note);
		}
	}

	// Effects in columns a channel doesn't show don't play: it shows more
	m_Widened.clear();
	for (size_t p = 0; p < m_Parts.size(); ++p) {
		const RecordedPart &Part = m_Parts[p];
		int Needed = 0;
		for (size_t i = 0; i < Part.Cells.size(); ++i)
			for (int c = MAX_EFFECT_COLUMNS - 1; c >= Needed; --c)
				if (Part.Cells[i].Effect[c] != EF_NONE) {
					Needed = c + 1;
					break;
				}
		const unsigned int Shown = pDoc->GetEffColumns(m_iTrack, Part.Channel);	// one less than it shows
		if (Needed > static_cast<int>(Shown) + 1) {
			m_Widened.push_back(std::make_pair(Part.Channel, Shown));
			pDoc->SetEffColumns(m_iTrack, Part.Channel, static_cast<unsigned int>(Needed - 1));
		}
	}

	// The sections as asked, inside the frames there are
	if (m_bSetSections) {
		pDoc->GetSections()->Set(static_cast<int>(m_iTrack), m_SectionsAfter);
		pDoc->GetSections()->FitFrames(static_cast<int>(m_iTrack), static_cast<int>(pDoc->GetFrameCount(m_iTrack)));
	}

	pDoc->UnlockDocument();
	if (!m_Widened.empty())
		pDoc->UpdateAllViews(NULL, UPDATE_COLUMNS);

	// A linked copy: its patterns are its own now
	if (m_iLinkClip >= 0 && !m_Parts.empty())
		pClips->AddLink(pDoc, pClips->FindIndex(m_iLinkClip), m_iTrack, m_iFrame, m_iRow, static_cast<int>(m_Parts.front().Channel));
	Finish(pMainFrm, pDoc, pEditor);
}

void CInsertRecordingAction::Undo(CMainFrame *pMainFrm)
{
	CFamiTrackerView *pView = static_cast<CFamiTrackerView*>(pMainFrm->GetActiveView());
	CFamiTrackerDoc *pDoc = pView->GetDocument();
	CPatternEditor *pEditor = pView->GetPatternEditor();
	const unsigned int Length = pDoc->GetPatternLength(m_iTrack);

	pDoc->GetClips()->RestoreLinks(m_LinksBefore);

	pDoc->LockDocument();
	for (std::map<PatternKey, std::vector<stChanNote> >::const_iterator it = m_Backup.begin(); it != m_Backup.end(); ++it) {
		for (unsigned int r = 0; r < Length && r < it->second.size(); ++r)
			pDoc->SetDataAtPattern(m_iTrack, it->first.second, it->first.first, r, &it->second[r]);
	}
	for (size_t i = m_Reassigned.size(); i-- > 0; )
		pDoc->SetPatternAtFrame(m_iTrack, m_Reassigned[i].Frame, m_Reassigned[i].Channel, m_Reassigned[i].OldPattern);
	pDoc->SetFrameCount(m_iTrack, m_iOldFrameCount);
	if (m_iMoveBy != 0) {
		// The frames as they were before frames moved
		const unsigned int Channels = pDoc->GetAvailableChannels();
		for (unsigned int f = 0; f < m_iOldFrameCount; ++f)
			for (unsigned int c = 0; c < Channels && f * Channels + c < m_FrameList.size(); ++c)
				pDoc->SetPatternAtFrame(m_iTrack, f, c, m_FrameList[f * Channels + c]);
	}
	for (size_t i = m_Widened.size(); i-- > 0; )
		pDoc->SetEffColumns(m_iTrack, m_Widened[i].first, m_Widened[i].second);
	pDoc->GetSections()->Set(static_cast<int>(m_iTrack), m_SectionsBefore);
	pDoc->UnlockDocument();
	if (!m_Widened.empty())
		pDoc->UpdateAllViews(NULL, UPDATE_COLUMNS);

	Finish(pMainFrm, pDoc, pEditor);
}

// A clip edited in the clip editor, written into its linked copies. Undo puts
// the copies' patterns back, and the clip follows them (CMainFrame::SyncClips).
class CEditClipAction : public CAction
{
public:
	CEditClipAction(int ClipId, const stChanNote *pCells, int Count) :
		CAction(ACT_EDIT_CLIP), m_iClipId(ClipId), m_Cells(pCells, pCells + Count)
	{
	}

	bool SaveState(CMainFrame *pMainFrm)
	{
		// The patterns of every linked copy, as they are
		CFamiTrackerDoc *pDoc = static_cast<CFamiTrackerDoc*>(pMainFrm->GetActiveDocument());
		const CClipLibrary *pClips = pDoc->GetClips();
		const int Index = pClips->FindIndex(m_iClipId);
		for (int i = 0; i < pClips->GetLinkSegmentCount(Index); ++i) {
			Place Where;
			if (!pClips->GetLinkSegment(pDoc, Index, i, Where.Track, Where.Channel, Where.Pattern) || m_Backup.count(Where) != 0)
				continue;
			const unsigned int Length = pDoc->GetPatternLength(Where.Track);
			std::vector<stChanNote> &Rows = m_Backup[Where];
			Rows.resize(Length);
			for (unsigned int r = 0; r < Length; ++r)
				pDoc->GetDataAtPattern(Where.Track, Where.Pattern, static_cast<unsigned int>(Where.Channel), r, &Rows[r]);
		}
		Redo(pMainFrm);
		return true;
	}

	void Redo(CMainFrame *pMainFrm)
	{
		CFamiTrackerDoc *pDoc = static_cast<CFamiTrackerDoc*>(pMainFrm->GetActiveDocument());
		CClipLibrary *pClips = pDoc->GetClips();
		const int Index = pClips->FindIndex(m_iClipId);
		if (Index < 0)
			return;	// deleted since
		const int Columns = pClips->GetColumns(Index), Rows = pClips->GetRows(Index);
		if (static_cast<size_t>(Columns) * static_cast<size_t>(Rows) == m_Cells.size()) {
			for (int c = 0; c < Columns; ++c)
				for (int r = 0; r < Rows; ++r)
					pClips->SetCell(Index, c, r, m_Cells[static_cast<size_t>(c) * static_cast<size_t>(Rows) + static_cast<size_t>(r)]);
		}
		pClips->PushToLinks(pDoc, Index);
		pDoc->SetModifiedFlag();
		pDoc->UpdateAllViews(NULL, UPDATE_PATTERN);
	}

	void Undo(CMainFrame *pMainFrm)
	{
		CFamiTrackerDoc *pDoc = static_cast<CFamiTrackerDoc*>(pMainFrm->GetActiveDocument());
		pDoc->LockDocument();
		for (std::map<Place, std::vector<stChanNote> >::const_iterator it = m_Backup.begin(); it != m_Backup.end(); ++it) {
			const Place &Where = it->first;
			if (Where.Track >= pDoc->GetTrackCount() || Where.Channel >= static_cast<int>(pDoc->GetAvailableChannels()))
				continue;
			const unsigned int Length = std::min(pDoc->GetPatternLength(Where.Track), static_cast<unsigned int>(it->second.size()));
			for (unsigned int r = 0; r < Length; ++r)
				pDoc->SetDataAtPattern(Where.Track, Where.Pattern, static_cast<unsigned int>(Where.Channel), r, &it->second[r]);
		}
		pDoc->UnlockDocument();
		pDoc->SetModifiedFlag();
		pDoc->UpdateAllViews(NULL, UPDATE_PATTERN);
	}

private:
	struct Place {
		unsigned int Track;
		int Channel;
		unsigned int Pattern;
		bool operator<(const Place &Other) const
		{
			if (Track != Other.Track)
				return Track < Other.Track;
			if (Channel != Other.Channel)
				return Channel < Other.Channel;
			return Pattern < Other.Pattern;
		}
	};

	int m_iClipId;
	std::vector<stChanNote> m_Cells;	// the edited clip
	std::map<Place, std::vector<stChanNote> > m_Backup;	// the linked copies' patterns before
};

void CInsertRecordingAction::Finish(CMainFrame *pMainFrm, CFamiTrackerDoc *pDoc, CPatternEditor *pEditor) const
{
	pEditor->MoveToFrame(static_cast<int>(std::min(m_iCursorFrame, pDoc->GetFrameCount(m_iTrack) - 1)));
	if (!m_Parts.empty())
		pEditor->MoveToChannel(static_cast<int>(m_Parts.front().Channel));
	pEditor->MoveToRow(static_cast<int>(m_iCursorRow));
	pEditor->MoveToColumn(0);	// the note column
	pEditor->InvalidateCursor();

	pDoc->SetModifiedFlag();
	pDoc->UpdateAllViews(NULL, UPDATE_FRAME);	// redraws the frame list and the patterns
	pMainFrm->UpdateControls();
}

// The patterns a channel's frames play once By frames are put in at frame
// Frame (By > 0), each with the next free pattern as InsertFrame picks it
// (-1 when there is none), or the -By frames from Frame on are taken out
std::vector<int> FramesAfterMove(const CFamiTrackerDoc *pDoc, unsigned int Track, unsigned int Channel, int Frame, int By)
{
	const int Count = static_cast<int>(pDoc->GetFrameCount(Track));
	std::vector<int> Layout;
	for (int f = 0; f <= Count; ++f) {
		if (f == Frame && By > 0)
			Layout.insert(Layout.end(), static_cast<size_t>(By), -2);	// filled in below
		if (f == Count)
			break;
		if (By < 0 && f >= Frame && f < Frame - By)
			continue;
		Layout.push_back(static_cast<int>(pDoc->GetPatternAtFrame(Track, static_cast<unsigned int>(f), Channel)));
	}
	for (size_t i = 0; i < Layout.size(); ++i) {
		if (Layout[i] != -2)
			continue;
		Layout[i] = -1;
		for (unsigned int p = 0; p < MAX_PATTERN && Layout[i] < 0; ++p) {
			if (std::find(Layout.begin(), Layout.end(), static_cast<int>(p)) == Layout.end() &&
				!UsedAnywhere(pDoc, Track, Channel, p) && pDoc->IsPatternEmpty(Track, Channel, p))
				Layout[i] = static_cast<int>(p);
		}
	}
	return Layout;
}

// Does an exact part, written from Row of Frame on, change anything in frame
// f, which plays Pattern?
bool ChangesPattern(const CFamiTrackerDoc *pDoc, unsigned int Track, const RecordedPart &Part, unsigned int Frame,
	unsigned int Row, unsigned int f, unsigned int Pattern)
{
	const int Length = static_cast<int>(pDoc->GetPatternLength(Track));
	for (int r = 0; r < Length; ++r) {
		const int Index = static_cast<int>(f - Frame) * Length + r - static_cast<int>(Row);
		if (Index < 0 || Index >= Part.Rows || static_cast<size_t>(Index) >= Part.Cells.size())
			continue;
		stChanNote Note;
		pDoc->GetDataAtPattern(Track, Pattern, Part.Channel, static_cast<unsigned int>(r), &Note);
		if (!SameCell(Note, Part.Cells[static_cast<size_t>(Index)]))
			return true;
	}
	return false;
}

// Free patterns for a part written in its channel from Row of Frame on, with
// the frames laid out as Layout says (FramesAfterMove): one for each frame
// added at the end of the song, and one for each frame it touches (an exact
// part: changes) whose pattern another frame plays too
bool EnoughPatternsAfterMove(const CFamiTrackerDoc *pDoc, unsigned int Track, const RecordedPart &Part, unsigned int Frame,
	unsigned int Row, std::vector<int> Layout)
{
	const unsigned int Length = pDoc->GetPatternLength(Track);
	const unsigned int LastFrame = Frame + (Row + static_cast<unsigned int>(std::max(1, Part.Rows)) - 1) / Length;
	const unsigned int Count = static_cast<unsigned int>(Layout.size());
	unsigned int Needed = LastFrame >= Count ? LastFrame + 1 - Count : 0;
	for (unsigned int f = Frame; f <= LastFrame && f < Count; ++f) {
		if (Layout[f] < 0 || (Part.Exact && !ChangesPattern(pDoc, Track, Part, Frame, Row, f, static_cast<unsigned int>(Layout[f]))))
			continue;
		for (unsigned int g = 0; g < Count; ++g) {
			if (g != f && Layout[g] == Layout[f]) {
				++Needed;
				Layout[f] = -1;	// from now on its own
				break;
			}
		}
	}
	unsigned int Free = 0;
	for (unsigned int p = 0; p < MAX_PATTERN; ++p) {
		if (std::find(Layout.begin(), Layout.end(), static_cast<int>(p)) == Layout.end() &&
			!UsedAnywhere(pDoc, Track, Part.Channel, p) && pDoc->IsPatternEmpty(Track, Part.Channel, p))
			++Free;
	}
	return Needed <= Free;
}

// Free patterns for a part written in Channel over frames Frame-LastFrame:
// one for each frame added at the end of the song, and one for each touched
// frame whose pattern another frame plays too. With pChanges, only the frames
// it marks (from Frame on) are touched.
bool EnoughPatterns(const CFamiTrackerDoc *pDoc, unsigned int Track, unsigned int Channel, unsigned int Frame,
	unsigned int LastFrame, const std::vector<bool> *pChanges = NULL)
{
	const unsigned int FrameCount = pDoc->GetFrameCount(Track);
	std::vector<unsigned int> Playing(FrameCount);
	for (unsigned int f = 0; f < FrameCount; ++f)
		Playing[f] = pDoc->GetPatternAtFrame(Track, f, Channel);
	unsigned int Needed = (LastFrame >= FrameCount) ? LastFrame + 1 - FrameCount : 0;
	for (unsigned int f = Frame; f <= LastFrame && f < FrameCount; ++f) {
		if (pChanges != NULL && (f - Frame >= pChanges->size() || !(*pChanges)[f - Frame]))
			continue;
		for (unsigned int g = 0; g < FrameCount; ++g) {
			if (g != f && Playing[g] == Playing[f]) {
				++Needed;
				Playing[f] = MAX_PATTERN + f;	// from now on its own
				break;
			}
		}
	}
	unsigned int Free = 0;
	for (unsigned int p = 0; p < MAX_PATTERN; ++p) {
		if (!UsedAnywhere(pDoc, Track, Channel, p) && pDoc->IsPatternEmpty(Track, Channel, p))
			++Free;
	}
	return Needed <= Free;
}

// A pattern cell from a cell of the engine: notes, releases ending in Release
// (=== or ---), effects in the first effect column
RecordedCell ToCell(const transcribe::MelodyCell &Cell, int Instrument, int Release, int Volume)
{
	int Effect = EF_NONE;
	switch (Cell.effect) {
	case 'Q': Effect = EF_SLIDE_UP; break;
	case 'R': Effect = EF_SLIDE_DOWN; break;
	case '4': Effect = EF_VIBRATO; break;
	case '0': Effect = EF_ARPEGGIO; break;
	}
	if (Cell.kind == transcribe::MelodyCell::Note) {
		int Index = Cell.midi - 24;	// FamiTracker's C-0 is MIDI note 24
		Index = std::max(0, std::min(95, Index));
		const int Vol = Cell.volume >= 0 ? Cell.volume : Volume;
		return MakeCell(Cell.row, Index % 12 + 1, Index / 12, Instrument, Vol, Effect, Cell.effectParam);
	}
	if (Cell.kind == transcribe::MelodyCell::Release)
		return MakeCell(Cell.row, Release, 0, MAX_INSTRUMENTS, MAX_VOLUME, Effect, Cell.effectParam);
	return MakeCell(Cell.row, NONE, 0, MAX_INSTRUMENTS, MAX_VOLUME, Effect, Cell.effectParam);
}

// The melody in a channel: each note lasts until the next note, release or
// cut. A slide (Qxy, Rxy) counts at the pitch it lands on, and the volume
// column (which carries on to later notes) sets how much a note counts.
std::vector<transcribe::MelodyNote> ReadMelody(const CFamiTrackerDoc *pDoc, unsigned int Track, int Channel)
{
	std::vector<transcribe::MelodyNote> Melody;
	const unsigned int Length = pDoc->GetPatternLength(Track);
	const unsigned int Frames = pDoc->GetFrameCount(Track);
	int Volume = 15;
	bool Sounding = false;
	for (unsigned int f = 0; f < Frames; ++f) {
		for (unsigned int r = 0; r < Length; ++r) {
			stChanNote Note;
			pDoc->GetNoteData(Track, f, static_cast<unsigned int>(Channel), r, &Note);
			const int Row = static_cast<int>(f * Length + r);
			if (Note.Vol < MAX_VOLUME)
				Volume = Note.Vol;
			if (Note.Note >= C && Note.Note <= B) {
				if (Sounding)
					Melody.back().length = std::max(1, Row - Melody.back().row);
				transcribe::MelodyNote Played;
				Played.row = Row;
				Played.midi = Note.Octave * 12 + (Note.Note - C) + 24;	// C-0 is MIDI note 24
				for (int c = 0; c < MAX_EFFECT_COLUMNS; ++c) {
					if (Note.EffNumber[c] == EF_SLIDE_UP)
						Played.midi += Note.EffParam[c] & 0x0F;
					else if (Note.EffNumber[c] == EF_SLIDE_DOWN)
						Played.midi -= Note.EffParam[c] & 0x0F;
				}
				Played.weight = std::max(0.3, Volume / 15.0);
				Melody.push_back(Played);
				Sounding = true;
			}
			else if (Note.Note == RELEASE || Note.Note == HALT) {
				if (Sounding)
					Melody.back().length = std::max(1, Row - Melody.back().row);
				Sounding = false;
			}
		}
	}
	if (Sounding)
		Melody.back().length = std::max(1, static_cast<int>(Frames * Length) - Melody.back().row);
	return Melody;
}

// Chords for the melody in a channel
struct BackingPlan {
	transcribe::Key Key;
	std::vector<transcribe::Bar> Bars;
	std::vector<transcribe::ChordSpan> Chords;
	transcribe::HarmonyOptions Options;
};

bool PlanBacking(const CFamiTrackerDoc *pDoc, unsigned int Track, const backing_choice_t &Choice, BackingPlan &Plan,
	CString &Message)
{
	const int Channels = static_cast<int>(pDoc->GetAvailableChannels());
	if (Choice.MelodyChannel < 0 || Choice.MelodyChannel >= Channels) {
		Message = _T("Pick the channel with the melody.");
		return false;
	}
	const std::vector<transcribe::MelodyNote> Melody = ReadMelody(pDoc, Track, Choice.MelodyChannel);
	if (Melody.empty()) {
		Message.Format(_T("%s has no notes to fit chords to."), static_cast<LPCTSTR>(RecordingChannelName(pDoc, Choice.MelodyChannel)));
		return false;
	}
	if (Choice.KeyTonic >= 0) {
		Plan.Key.tonic = Choice.KeyTonic % 12;
		Plan.Key.minor = Choice.KeyMinor;
	}
	else
		Plan.Key = transcribe::FindKey(Melody);

	// Beats and bars as the row highlight shows them
	int Bar = pDoc->GetSecondHighlight();
	if (Bar <= 0 || Bar > 256)
		Bar = 16;
	int Beat = pDoc->GetFirstHighlight();
	if (Beat <= 0 || Beat > Bar)
		Beat = std::max(1, Bar / 4);
	Plan.Options.rowsPerBeat = Beat;
	Plan.Options.halfBars = Choice.HalfBars;
	Plan.Bars = transcribe::MelodyBars(Melody, static_cast<int>(pDoc->GetPatternLength(Track)), Bar);
	Plan.Chords = transcribe::ChooseChords(Melody, Plan.Bars, Plan.Key, Plan.Options);
	if (Plan.Bars.empty() || Plan.Chords.empty()) {
		Message = _T("The melody is too short to fit chords to.");
		return false;
	}
	return true;
}

// "| C | F C | G | C |", up to 32 bars
CString ChordsText(const BackingPlan &Plan)
{
	CString Text;
	for (size_t b = 0; b < Plan.Bars.size(); ++b) {
		if (b == 32) {
			Text += _T("| ...");
			return Text;
		}
		Text += _T("|");
		const int BarEnd = Plan.Bars[b].row + Plan.Bars[b].length;
		for (size_t c = 0; c < Plan.Chords.size(); ++c) {
			const transcribe::ChordSpan &Span = Plan.Chords[c];
			const int From = std::max(Span.row, Plan.Bars[b].row);
			if (From < std::min(Span.row + Span.length, BarEnd) && (From == Plan.Bars[b].row || From == Span.row)) {
				Text += _T(" ");
				Text += transcribe::ChordName(Span.chord, Plan.Key).c_str();
			}
		}
		Text += _T(" ");
	}
	Text += _T("|");
	return Text;
}

bool IsEmptyCell(const stChanNote &Note)
{
	if (Note.Note != NONE || Note.Instrument != MAX_INSTRUMENTS || Note.Vol != MAX_VOLUME)
		return false;
	for (int c = 0; c < MAX_EFFECT_COLUMNS; ++c)
		if (Note.EffNumber[c] != EF_NONE)
			return false;
	return true;
}

// A backing channel's part: cells relative to Start, over Rows rows. The
// release where the chords end goes on the row after the last bar when that
// row is in the song and empty, otherwise on the last bar's last row.
RecordedPart BackingPart(const CFamiTrackerDoc *pDoc, unsigned int Track, int Channel,
	const std::vector<transcribe::MelodyCell> &Line, int Instrument, int Start, int Rows)
{
	RecordedPart Part;
	Part.Channel = static_cast<unsigned int>(Channel);
	Part.Rows = Rows;
	Part.Replace = true;
	const unsigned int Length = pDoc->GetPatternLength(Track);
	const int SongRows = static_cast<int>(pDoc->GetFrameCount(Track) * Length);
	bool First = true;
	for (size_t i = 0; i < Line.size(); ++i) {
		RecordedCell Cell = ToCell(Line[i], Instrument, RELEASE, First ? 15 : MAX_VOLUME);	// the first note sets full volume
		if (Line[i].kind == transcribe::MelodyCell::Note)
			First = false;
		Cell.Row -= Start;
		if (Cell.Row >= Rows) {
			// The release at the end
			const int After = Start + Cell.Row;
			stChanNote Next;
			bool Free = After < SongRows;
			if (Free) {
				pDoc->GetNoteData(Track, static_cast<unsigned int>(After) / Length, Part.Channel, static_cast<unsigned int>(After) % Length, &Next);
				Free = IsEmptyCell(Next);
			}
			if (Free) {
				Cell.Row = Rows;
				Part.Rows = Rows + 1;
			}
			else {
				Cell.Row = Rows - 1;
				if (!Part.Cells.empty() && Part.Cells.back().Row >= Cell.Row)
					continue;	// a note of ours is there already
			}
		}
		Part.Cells.push_back(Cell);
	}
	return Part;
}

bool ReadRecording(LPCTSTR Path, transcribe::Audio &Audio, CString &Message)
{
	std::string Error;
	if (!transcribe::ReadWav(std::string(static_cast<LPCSTR>(CStringA(Path))), Audio, &Error)) {
		Message = _T("The recording couldn't be read: ");
		Message += CString(Error.c_str());
		return false;
	}
	return true;
}

}	// namespace

CString RecordingChannelName(const CFamiTrackerDoc *pDoc, int Channel)
{
	CString Chip;
	switch (pDoc->GetChipType(Channel)) {
	case SNDCHIP_NONE: Chip = _T("2A03"); break;
	case SNDCHIP_VRC6: Chip = _T("VRC6"); break;
	case SNDCHIP_VRC7: Chip = _T("VRC7"); break;
	case SNDCHIP_FDS: Chip = _T("FDS"); break;
	case SNDCHIP_MMC5: Chip = _T("MMC5"); break;
	case SNDCHIP_N163: Chip = _T("N163"); break;
	case SNDCHIP_S5B: Chip = _T("5B"); break;
	}
	CString Name = Chip;
	if (!Name.IsEmpty())
		Name += _T(" ");
	Name += pDoc->GetChannel(Channel)->GetChannelName();
	return Name;
}

bool IsMelodyChannel(const CFamiTrackerDoc *pDoc, int Channel)
{
	const int ChanID = pDoc->GetChannelType(Channel);
	return ChanID != CHANID_NOISE && ChanID != CHANID_DPCM;
}

bool IsBassChannel(const CFamiTrackerDoc *pDoc, int Channel)
{
	const int ChanID = pDoc->GetChannelType(Channel);
	return ChanID == CHANID_TRIANGLE || ChanID == CHANID_VRC6_SAWTOOTH;
}

void GuessRecording(const transcribe::Audio &Audio, recording_kind_t &Kind, CString &Description)
{
	const transcribe::SourceGuess Guess = transcribe::GuessSource(Audio);
	switch (Guess.source) {
	case transcribe::Source::Whistle: Kind = RECORDING_WHISTLE; break;
	case transcribe::Source::Voice: Kind = RECORDING_VOICE; break;
	case transcribe::Source::Taps: Kind = RECORDING_DRUMS; break;
	}
	Description = CString(transcribe::SourceName(Guess.source));
}

bool GuessRecording(LPCTSTR Path, recording_kind_t &Kind, CString &Description, CString &Message)
{
	transcribe::Audio Audio;
	if (!ReadRecording(Path, Audio, Message))
		return false;
	GuessRecording(Audio, Kind, Description);
	return true;
}

bool InsertRecording(CMainFrame *pMainFrm, LPCTSTR Path, recording_kind_t Kind, int Channel, bool Expression,
	CString &Message, LPCTSTR ClipName)
{
	transcribe::Audio Audio;
	if (!ReadRecording(Path, Audio, Message))
		return false;
	CFamiTrackerView *pView = static_cast<CFamiTrackerView*>(pMainFrm->GetActiveView());
	CPatternEditor *pEditor = pView->GetPatternEditor();
	const transcribe::TakeTiming Timing;	// the first note lands on the cursor row
	return InsertTake(pMainFrm, Audio, Timing, Kind, Channel, Expression, pEditor->GetFrame(), pEditor->GetRow(), Message,
		ClipName);
}

bool InsertTake(CMainFrame *pMainFrm, const transcribe::Audio &Audio, const transcribe::TakeTiming &Timing,
	recording_kind_t Kind, int Channel, bool Expression, int StartFrame, int StartRow, CString &Message, LPCTSTR ClipName)
{
	CFamiTrackerView *pView = static_cast<CFamiTrackerView*>(pMainFrm->GetActiveView());
	CFamiTrackerDoc *pDoc = pView->GetDocument();
	const unsigned int Track = static_cast<unsigned int>(pMainFrm->GetSelectedTrack());
	if (StartFrame < 0 || StartFrame >= static_cast<int>(pDoc->GetFrameCount(Track)) ||
		StartRow < 0 || StartRow >= static_cast<int>(pDoc->GetPatternLength(Track))) {
		Message = _T("The place to insert at is outside the song.");
		return false;
	}

	// Drums always go on the noise channel
	if (Kind == RECORDING_DRUMS) {
		Channel = -1;
		for (int i = 0; i < static_cast<int>(pDoc->GetAvailableChannels()); ++i) {
			if (pDoc->GetChannelType(i) == CHANID_NOISE)
				Channel = i;
		}
	}
	if (Channel < 0 || Channel >= static_cast<int>(pDoc->GetAvailableChannels())) {
		Message = _T("This song has no channel for that recording.");
		return false;
	}
	const int ChanID = pDoc->GetChannelType(Channel);
	if (ChanID == CHANID_DPCM || (Kind != RECORDING_DRUMS && ChanID == CHANID_NOISE)) {
		Message = _T("A melody can't go on the noise or DPCM channel. Pick another channel.");
		return false;
	}

	// Rows at the song's speed and tempo, as CSoundGen plays them
	transcribe::Grid Grid;
	Grid.frameRate = static_cast<int>(pDoc->GetFrameRate());
	Grid.speed = static_cast<int>(pDoc->GetSongSpeed(Track));
	Grid.tempo = static_cast<int>(pDoc->GetSongTempo(Track));
	if (Grid.tempo == 0)
		Grid.tempo = Grid.frameRate * 5 / 2;	// no tempo: one row every <speed> frames

	std::vector<RecordedCell> Cells;
	int Rows = 0;
	CString What;

	if (Kind == RECORDING_DRUMS) {
		// Taps and beatboxing become drums
		std::vector<transcribe::Hit> Hits = transcribe::DetectHits(Audio, transcribe::HitOptions());
		if (Hits.empty()) {
			Message = _T("No taps or beats were found in the recording. Is it loud enough?");
			return false;
		}
		if (!Timing.lineUpFirstNote) {
			// A take recorded against the click: the click itself is no drum
			transcribe::RemoveClickHits(Hits, Audio, Timing.clicks, Timing.leak);
			transcribe::PlaceHits(Hits, Timing.start, Grid, true);
			if (Hits.empty()) {
				Message = _T("No taps or beats were found after the count-in.");
				return false;
			}
		}
		transcribe::ClassifyHits(Hits);
		const std::vector<transcribe::GridHit> Drums = transcribe::QuantizeHits(Hits, Grid, Timing.lineUpFirstNote);

		const int Kick = FindOrAddInstrument(pDoc, "Kick", SNDCHIP_NONE, KICK_VOLUME, int(sizeof(KICK_VOLUME)), -1, NULL, 0);
		const int Snare = FindOrAddInstrument(pDoc, "Snare", SNDCHIP_NONE, SNARE_VOLUME, int(sizeof(SNARE_VOLUME)), -1, NULL, 0);
		const int HiHat = FindOrAddInstrument(pDoc, "Hi-hat", SNDCHIP_NONE, HIHAT_VOLUME, int(sizeof(HIHAT_VOLUME)), -1, NULL, 0);
		if (Kick == INVALID_INSTRUMENT || Snare == INVALID_INSTRUMENT || HiHat == INVALID_INSTRUMENT) {
			Message = _T("There are no free instrument slots left for the drum sounds.");
			return false;
		}

		for (size_t i = 0; i < Drums.size(); ++i) {
			if (!Cells.empty() && Cells.back().Row >= Drums[i].row)
				continue;	// one hit per row
			int Pitch = 0xE, Instrument = HiHat;	// noise pitch, as in the text format's "E-#"
			if (Drums[i].drum == transcribe::Drum::Kick) {
				Pitch = 0x4;
				Instrument = Kick;
			}
			else if (Drums[i].drum == transcribe::Drum::Snare) {
				Pitch = 0xA;
				Instrument = Snare;
			}
			Cells.push_back(MakeCell(Drums[i].row, Pitch % 12 + 1, Pitch / 12, Instrument, Drums[i].volume));
		}
		Rows = Cells.back().Row + 1;
		What.Format(_T("%d drum hits"), static_cast<int>(Cells.size()));
	}
	else {
		// Whistling, humming and singing become a melody; sung syllables ("la la la") become notes of their own
		const transcribe::Source Heard = Kind == RECORDING_WHISTLE ? transcribe::Source::Whistle : transcribe::Source::Voice;
		const std::vector<transcribe::PitchFrame> Frames = transcribe::TrackPitch(Audio, transcribe::PitchOptionsFor(Heard));
		std::vector<transcribe::Note> Notes = transcribe::SegmentNotes(Frames, transcribe::NoteOptionsFor(Heard));
		if (Notes.empty()) {
			Message = _T("No notes were found in the recording. Is it loud enough?");
			return false;
		}
		const bool Low = IsBassChannel(pDoc, Channel);

		// How it was played: loudness, slides into notes, vibrato (no added vibrato on a bass line)
		transcribe::ExpressionOptions Options;
		if (!Expression)
			Options.dynamics = Options.slides = Options.vibrato = Options.gentleVibrato = false;
		if (Low)
			Options.gentleVibrato = false;
		transcribe::DescribeExpression(Notes, Frames, Options);

		if (!Timing.lineUpFirstNote) {
			// A take recorded against the click: the downbeat after the count-in is the start row
			transcribe::PlaceNotes(Notes, Timing.start, Grid, true);
			if (Notes.empty()) {
				Message = _T("No notes were found after the count-in.");
				return false;
			}
		}

		transcribe::Transpose(Notes, transcribe::OctaveShiftFor(Notes, Low ? 48 : 67));
		transcribe::QuantizeOptions Rules;
		Rules.alignToStart = Timing.lineUpFirstNote;
		const std::vector<transcribe::GridNote> Melody = transcribe::Quantize(Notes, Grid, Rules);

		int Instrument = INVALID_INSTRUMENT;
		const int Chip = pDoc->GetChipType(Channel);
		if (Chip == SNDCHIP_NONE || Chip == SNDCHIP_VRC6 || Chip == SNDCHIP_MMC5) {
			if (Low)
				Instrument = FindOrAddInstrument(pDoc, "Bass", Chip, BASS_VOLUME, int(sizeof(BASS_VOLUME)), BASS_RELEASE, NULL, 0);
			else if (Chip == SNDCHIP_VRC6)
				Instrument = FindOrAddInstrument(pDoc, "Lead", Chip, LEAD_VOLUME, int(sizeof(LEAD_VOLUME)), LEAD_RELEASE,
					LEAD_DUTY_VRC6, int(sizeof(LEAD_DUTY_VRC6)));
			else
				Instrument = FindOrAddInstrument(pDoc, "Lead", Chip, LEAD_VOLUME, int(sizeof(LEAD_VOLUME)), LEAD_RELEASE,
					LEAD_DUTY_2A03, int(sizeof(LEAD_DUTY_2A03)));
			if (Instrument == INVALID_INSTRUMENT) {
				Message = _T("There are no free instrument slots left for the recording.");
				return false;
			}
		}
		else {
			// Other chips need instruments of their own kind: use the selected one
			Instrument = pMainFrm->GetSelectedInstrument();
			if (Instrument < 0 || Instrument >= MAX_INSTRUMENTS || !pDoc->IsInstrumentUsed(Instrument)) {
				Message = _T("Select an instrument for this channel's sound chip first.");
				return false;
			}
		}

		// End notes with a release (===) when the instrument fades out on one,
		// otherwise with a cut (---)
		bool Release = false;
		CInstrument *pInstrument = pDoc->GetInstrument(Instrument);	// retained
		if (pInstrument != NULL) {
			Release = pInstrument->CanRelease();
			pInstrument->Release();
		}

		const transcribe::ChannelVoice Voice = ChanID == CHANID_TRIANGLE ? transcribe::ChannelVoice::Triangle :
			(ChanID == CHANID_VRC6_SAWTOOTH ? transcribe::ChannelVoice::Sawtooth : transcribe::ChannelVoice::Pulse);
		const std::vector<transcribe::MelodyCell> Line = transcribe::MelodyCells(Melody, Voice, Grid.frameRate);
		for (size_t i = 0; i < Line.size(); ++i)
			Cells.push_back(ToCell(Line[i], Instrument, Release ? RELEASE : HALT, MAX_VOLUME));
		Rows = Cells.back().Row + 1;
		What.Format(_T("%d notes"), static_cast<int>(Melody.size()));
	}

	if (ClipName != NULL) {
		// Kept as a clip instead of going into the song
		CClipLibrary *pClips = pDoc->GetClips();
		const int Index = pClips->Add(ClipName, 1, Rows);
		if (Index < 0) {
			Message = _T("There are too many clips already. Delete some first: right-click them in the clips panel.");
			return false;
		}
		pClips->SetSource(Index, 0, ChanID);
		for (size_t i = 0; i < Cells.size(); ++i) {
			const RecordedCell &Cell = Cells[i];
			stChanNote Note;
			Note.Note = Cell.Note;
			Note.Octave = Cell.Octave;
			Note.Vol = Cell.Vol;
			Note.Instrument = Cell.Instrument;
			for (int c = 0; c < MAX_EFFECT_COLUMNS; ++c) {
				Note.EffNumber[c] = Cell.Effect[c];
				Note.EffParam[c] = Cell.EffectParam[c];
			}
			pClips->SetCell(Index, 0, Cell.Row, Note);
		}
		pDoc->SetModifiedFlag();
		pDoc->UpdateAllViews(NULL, UPDATE_INSTRUMENT);	// new instruments, if any
		Message.Format(_T("Saved %s as the clip \"%s\" (%d rows). Drag it from the clips panel into the song."), static_cast<LPCTSTR>(What),
			ClipName, Rows);
		return true;
	}

	// Room: the song has at most MAX_FRAMES frames
	const unsigned int Length = pDoc->GetPatternLength(Track);
	const unsigned int Frame = static_cast<unsigned int>(StartFrame);
	const unsigned int Row = static_cast<unsigned int>(StartRow);
	unsigned int LastFrame = Frame + (Row + static_cast<unsigned int>(Rows) - 1) / Length;
	bool Cut = false;
	if (LastFrame >= MAX_FRAMES) {
		Rows = static_cast<int>((MAX_FRAMES - Frame) * Length - Row);
		while (!Cells.empty() && Cells.back().Row >= Rows)
			Cells.pop_back();
		LastFrame = MAX_FRAMES - 1;
		Cut = true;
		if (Cells.empty()) {
			Message = _T("There's no room left in the song after the cursor.");
			return false;
		}
	}

	if (!EnoughPatterns(pDoc, Track, static_cast<unsigned int>(Channel), Frame, LastFrame)) {
		Message = _T("This channel has run out of free patterns. Edit > Clean up > Remove unused patterns may make room.");
		return false;
	}

	RecordedPart Part;
	Part.Channel = static_cast<unsigned int>(Channel);
	Part.Cells = Cells;
	Part.Rows = Rows;
	Part.Replace = false;
	CInsertRecordingAction *pAction = new CInsertRecordingAction(std::vector<RecordedPart>(1, Part), StartFrame, StartRow);
	if (!pMainFrm->AddAction(pAction)) {
		Message = _T("The recording couldn't be inserted.");
		return false;
	}
	pDoc->UpdateAllViews(NULL, UPDATE_INSTRUMENT);	// new instruments, if any

	const CString ChannelName = RecordingChannelName(pDoc, Channel);
	Message.Format(_T("Inserted %s on %s over %d rows. Ctrl+Z undoes it."), static_cast<LPCTSTR>(What),
		static_cast<LPCTSTR>(ChannelName), Rows);
	if (Cut)
		Message += _T(" The end didn't fit: a song has at most 128 frames.");
	return true;
}

// Backing

bool HasNotes(const CFamiTrackerDoc *pDoc, unsigned int Track, int Channel)
{
	const unsigned int Length = pDoc->GetPatternLength(Track);
	const unsigned int Frames = pDoc->GetFrameCount(Track);
	for (unsigned int f = 0; f < Frames; ++f) {
		for (unsigned int r = 0; r < Length; ++r) {
			stChanNote Note;
			pDoc->GetNoteData(Track, f, static_cast<unsigned int>(Channel), r, &Note);
			if (Note.Note >= C && Note.Note <= B)
				return true;
		}
	}
	return false;
}

CString KeyName(int Tonic, bool Minor)
{
	transcribe::Key Key;
	Key.tonic = ((Tonic % 12) + 12) % 12;
	Key.minor = Minor;
	return CString(transcribe::KeyName(Key).c_str());
}

bool IsChordChannel(const CFamiTrackerDoc *pDoc, int Channel)
{
	const int ChanID = pDoc->GetChannelType(Channel);
	return ChanID == CHANID_SQUARE1 || ChanID == CHANID_SQUARE2 || ChanID == CHANID_VRC6_PULSE1 ||
		ChanID == CHANID_VRC6_PULSE2 || ChanID == CHANID_MMC5_SQUARE1 || ChanID == CHANID_MMC5_SQUARE2;
}

bool DescribeBacking(const CFamiTrackerDoc *pDoc, unsigned int Track, const backing_choice_t &Choice, CString &Key,
	CString &Chords, int &FirstFrame, int &LastFrame, CString &Message)
{
	BackingPlan Plan;
	if (!PlanBacking(pDoc, Track, Choice, Plan, Message))
		return false;
	const int Length = static_cast<int>(pDoc->GetPatternLength(Track));
	Key = transcribe::KeyName(Plan.Key).c_str();
	Chords = ChordsText(Plan);
	FirstFrame = Plan.Bars.front().row / Length;
	LastFrame = (Plan.Bars.back().row + Plan.Bars.back().length - 1) / Length;
	return true;
}

bool AddBacking(CMainFrame *pMainFrm, const backing_choice_t &Choice, CString &Message)
{
	CFamiTrackerView *pView = static_cast<CFamiTrackerView*>(pMainFrm->GetActiveView());
	CFamiTrackerDoc *pDoc = pView->GetDocument();
	const unsigned int Track = static_cast<unsigned int>(pMainFrm->GetSelectedTrack());
	const int Channels = static_cast<int>(pDoc->GetAvailableChannels());

	if (Choice.ChordChannel < 0 && Choice.BassChannel < 0) {
		Message = _T("Pick a channel for the chords or the bass line.");
		return false;
	}
	if (Choice.ChordChannel >= Channels || (Choice.ChordChannel >= 0 && !IsChordChannel(pDoc, Choice.ChordChannel)) ||
		Choice.ChordChannel == Choice.MelodyChannel) {
		Message = _T("The chords need a pulse channel other than the melody's.");
		return false;
	}
	if (Choice.BassChannel >= Channels || (Choice.BassChannel >= 0 && !IsBassChannel(pDoc, Choice.BassChannel)) ||
		Choice.BassChannel == Choice.MelodyChannel) {
		Message = _T("The bass line needs the triangle or the sawtooth, other than the melody's channel.");
		return false;
	}

	BackingPlan Plan;
	if (!PlanBacking(pDoc, Track, Choice, Plan, Message))
		return false;

	const unsigned int Length = pDoc->GetPatternLength(Track);
	const int Start = Plan.Bars.front().row;
	const int Rows = Plan.Bars.back().row + Plan.Bars.back().length - Start;
	const unsigned int Frame = static_cast<unsigned int>(Start) / Length;
	const unsigned int Row = static_cast<unsigned int>(Start) % Length;

	std::vector<RecordedPart> Parts;
	if (Choice.ChordChannel >= 0) {
		const int Chip = pDoc->GetChipType(Choice.ChordChannel);
		const int Instrument = (Chip == SNDCHIP_VRC6) ?
			FindOrAddInstrument(pDoc, "Chords", Chip, CHORD_VOLUME, int(sizeof(CHORD_VOLUME)), CHORD_RELEASE, CHORD_DUTY_VRC6, 1) :
			FindOrAddInstrument(pDoc, "Chords", Chip, CHORD_VOLUME, int(sizeof(CHORD_VOLUME)), CHORD_RELEASE, CHORD_DUTY_2A03, 1);
		if (Instrument == INVALID_INSTRUMENT) {
			Message = _T("There are no free instrument slots left for the chords.");
			return false;
		}
		Parts.push_back(BackingPart(pDoc, Track, Choice.ChordChannel, transcribe::ArpeggioCells(Plan.Chords, Plan.Bars),
			Instrument, Start, Rows));
	}
	if (Choice.BassChannel >= 0) {
		const int Chip = pDoc->GetChipType(Choice.BassChannel);
		const int Instrument = FindOrAddInstrument(pDoc, "Bass", Chip, BASS_VOLUME, int(sizeof(BASS_VOLUME)), BASS_RELEASE, NULL, 0);
		if (Instrument == INVALID_INSTRUMENT) {
			Message = _T("There are no free instrument slots left for the bass line.");
			return false;
		}
		Parts.push_back(BackingPart(pDoc, Track, Choice.BassChannel, transcribe::BassCells(Plan.Chords, Plan.Bars, Plan.Options),
			Instrument, Start, Rows));
	}

	for (size_t p = 0; p < Parts.size(); ++p) {
		const unsigned int LastFrame = Frame + (Row + static_cast<unsigned int>(Parts[p].Rows) - 1) / Length;
		if (!EnoughPatterns(pDoc, Track, Parts[p].Channel, Frame, LastFrame)) {
			Message.Format(_T("%s has run out of free patterns. Edit > Clean up > Remove unused patterns may make room."),
				static_cast<LPCTSTR>(RecordingChannelName(pDoc, static_cast<int>(Parts[p].Channel))));
			return false;
		}
	}

	CInsertRecordingAction *pAction = new CInsertRecordingAction(Parts, static_cast<int>(Frame), static_cast<int>(Row));
	if (!pMainFrm->AddAction(pAction)) {
		Message = _T("The chords couldn't be added.");
		return false;
	}
	pDoc->UpdateAllViews(NULL, UPDATE_INSTRUMENT);	// new instruments, if any

	CString Where;
	if (Choice.ChordChannel >= 0 && Choice.BassChannel >= 0)
		Where.Format(_T("chords on %s and a bass line on %s"), static_cast<LPCTSTR>(RecordingChannelName(pDoc, Choice.ChordChannel)),
			static_cast<LPCTSTR>(RecordingChannelName(pDoc, Choice.BassChannel)));
	else if (Choice.ChordChannel >= 0)
		Where.Format(_T("chords on %s"), static_cast<LPCTSTR>(RecordingChannelName(pDoc, Choice.ChordChannel)));
	else
		Where.Format(_T("a bass line on %s"), static_cast<LPCTSTR>(RecordingChannelName(pDoc, Choice.BassChannel)));
	const CString Key(transcribe::KeyName(Plan.Key).c_str());
	Message.Format(_T("Added %s, in %s: %s Ctrl+Z undoes it."), static_cast<LPCTSTR>(Where), static_cast<LPCTSTR>(Key),
		static_cast<LPCTSTR>(ChordsText(Plan)));
	return true;
}

// Clips

bool SaveSelectionAsClip(CMainFrame *pMainFrm, LPCTSTR Name, CString &Message)
{
	CFamiTrackerView *pView = static_cast<CFamiTrackerView*>(pMainFrm->GetActiveView());
	CFamiTrackerDoc *pDoc = pView->GetDocument();
	CPatternEditor *pEditor = pView->GetPatternEditor();
	const unsigned int Track = static_cast<unsigned int>(pMainFrm->GetSelectedTrack());
	if (!pEditor->IsSelecting()) {
		Message = _T("Select the rows to keep in the pattern editor first.");
		return false;
	}
	const CSelection Selection = pEditor->GetSelection();
	const int Channels = static_cast<int>(pDoc->GetAvailableChannels());
	const int Length = static_cast<int>(pDoc->GetPatternLength(Track));
	const int First = std::max(0, Selection.GetChanStart());
	const int Last = std::min(Channels - 1, Selection.GetChanEnd());
	const int Top = std::max(0, Selection.GetRowStart());
	const int Bottom = std::min(Length - 1, Selection.GetRowEnd());
	if (Last < First || Bottom < Top) {
		Message = _T("Select the rows to keep in the pattern editor first.");
		return false;
	}

	CClipLibrary *pClips = pDoc->GetClips();
	const int Index = pClips->Add(Name, Last - First + 1, Bottom - Top + 1);
	if (Index < 0) {
		Message = _T("There are too many clips already. Delete some first: right-click them in the clips panel.");
		return false;
	}
	const unsigned int Frame = static_cast<unsigned int>(pEditor->GetFrame());
	for (int c = First; c <= Last; ++c) {
		pClips->SetSource(Index, c - First, pDoc->GetChannelType(c));
		for (int r = Top; r <= Bottom; ++r) {
			stChanNote Note;
			pDoc->GetNoteData(Track, Frame, static_cast<unsigned int>(c), static_cast<unsigned int>(r), &Note);
			pClips->SetCell(Index, c - First, r - Top, Note);
		}
	}
	pDoc->SetModifiedFlag();

	CString Which;
	if (Last == First)
		Which = RecordingChannelName(pDoc, First);
	else
		Which.Format(_T("%d channels"), Last - First + 1);
	Message.Format(_T("Saved %d %s of %s as the clip \"%s\". Drag it from the clips panel to where it goes, or double-click it."),
		Bottom - Top + 1, Bottom == Top ? _T("row") : _T("rows"), static_cast<LPCTSTR>(Which), Name);
	return true;
}

bool PlaceClip(CMainFrame *pMainFrm, int Index, bool Linked, CString &Message)
{
	CFamiTrackerView *pView = static_cast<CFamiTrackerView*>(pMainFrm->GetActiveView());
	CFamiTrackerDoc *pDoc = pView->GetDocument();
	CPatternEditor *pEditor = pView->GetPatternEditor();
	CClipLibrary *pClips = pDoc->GetClips();
	const unsigned int Track = static_cast<unsigned int>(pMainFrm->GetSelectedTrack());
	if (Index < 0 || Index >= pClips->GetCount()) {
		Message = _T("Pick a clip first.");
		return false;
	}

	const unsigned int Length = pDoc->GetPatternLength(Track);
	const unsigned int Frame = static_cast<unsigned int>(pEditor->GetFrame());
	const unsigned int Row = static_cast<unsigned int>(pEditor->GetRow());
	const int Channel = pEditor->GetChannel();
	const int Channels = static_cast<int>(pDoc->GetAvailableChannels());

	// Room: the song has at most MAX_FRAMES frames
	int Rows = pClips->GetRows(Index);
	unsigned int LastFrame = Frame + (Row + static_cast<unsigned int>(Rows) - 1) / Length;
	bool Cut = false;
	if (LastFrame >= MAX_FRAMES) {
		Rows = static_cast<int>((MAX_FRAMES - Frame) * Length - Row);
		LastFrame = MAX_FRAMES - 1;
		Cut = true;
	}

	// One part per column, replacing what the channel had over those rows
	std::vector<RecordedPart> Parts;
	for (int c = 0; c < pClips->GetColumns(Index) && Channel + c < Channels; ++c) {
		RecordedPart Part;
		Part.Channel = static_cast<unsigned int>(Channel + c);
		Part.Rows = Rows;
		Part.Replace = true;
		for (int r = 0; r < Rows; ++r) {
			stChanNote Note;
			pClips->GetCell(Index, c, r, Note);
			if (!IsEmptyCell(Note))
				Part.Cells.push_back(CellOf(r, Note));
		}
		Parts.push_back(Part);
	}
	if (Parts.empty() || Rows < 1) {
		Message = _T("The clip doesn't fit there.");
		return false;
	}
	for (size_t p = 0; p < Parts.size(); ++p) {
		if (!EnoughPatterns(pDoc, Track, Parts[p].Channel, Frame, LastFrame)) {
			Message.Format(_T("%s has run out of free patterns. Edit > Clean up > Remove unused patterns may make room."),
				static_cast<LPCTSTR>(RecordingChannelName(pDoc, static_cast<int>(Parts[p].Channel))));
			return false;
		}
	}

	CInsertRecordingAction *pAction = new CInsertRecordingAction(Parts, static_cast<int>(Frame), static_cast<int>(Row));
	if (Linked)
		pAction->LinkTo(pClips->GetId(Index));
	if (!pMainFrm->AddAction(pAction)) {
		Message = _T("The clip couldn't be placed.");
		return false;
	}

	Message.Format(_T("Placed the clip \"%s\" %s at frame %02X, row %02X. Ctrl+Z undoes it."),
		static_cast<LPCTSTR>(pClips->GetName(Index)), Linked ? _T("linked") : _T("as a copy"), Frame, Row);
	if (static_cast<int>(Parts.size()) < pClips->GetColumns(Index))
		Message += _T(" Its last channels didn't fit to the right of the cursor.");
	if (Cut)
		Message += _T(" Its end didn't fit: a song has at most 128 frames.");
	return true;
}

void ApplyClipEdit(CMainFrame *pMainFrm, int Index, const stChanNote *pCells, int Count)
{
	CFamiTrackerDoc *pDoc = static_cast<CFamiTrackerDoc*>(pMainFrm->GetActiveDocument());
	CClipLibrary *pClips = pDoc->GetClips();
	if (Index < 0 || Index >= pClips->GetCount() || Count != pClips->GetColumns(Index) * pClips->GetRows(Index))
		return;
	if (pClips->GetLinkCount(Index) > 0) {
		// The song changes too: undoable
		pMainFrm->AddAction(new CEditClipAction(pClips->GetId(Index), pCells, Count));
		return;
	}
	const int Rows = pClips->GetRows(Index);
	for (int c = 0; c < pClips->GetColumns(Index); ++c)
		for (int r = 0; r < Rows; ++r)
			pClips->SetCell(Index, c, r, pCells[c * Rows + r]);
	pDoc->SetModifiedFlag();
}

// The piano roll

bool WriteChannelRows(CMainFrame *pMainFrm, int Channel, int Row, const stChanNote *pCells, int Count, int CursorRow,
	CString &Message)
{
	CFamiTrackerView *pView = static_cast<CFamiTrackerView*>(pMainFrm->GetActiveView());
	CFamiTrackerDoc *pDoc = pView->GetDocument();
	const unsigned int Track = static_cast<unsigned int>(pMainFrm->GetSelectedTrack());
	const int Length = static_cast<int>(pDoc->GetPatternLength(Track));
	if (Channel < 0 || Channel >= static_cast<int>(pDoc->GetAvailableChannels()) || Row < 0 || Count < 1 || Length < 1) {
		Message = _T("There's nothing to change there.");
		return false;
	}
	const unsigned int Frame = static_cast<unsigned int>(Row / Length);
	const unsigned int LastFrame = static_cast<unsigned int>((Row + Count - 1) / Length);
	if (LastFrame >= MAX_FRAMES) {
		Message = _T("That's past the end: a song has at most 128 frames.");
		return false;
	}

	RecordedPart Part;
	Part.Channel = static_cast<unsigned int>(Channel);
	Part.Rows = Count;
	Part.Replace = true;
	Part.Exact = true;
	for (int i = 0; i < Count; ++i)
		Part.Cells.push_back(CellOf(i, pCells[i]));

	// Free patterns for the frames that change, if they share their pattern with another frame
	std::vector<bool> Changes;
	for (unsigned int f = Frame; f <= LastFrame; ++f)
		Changes.push_back(f >= pDoc->GetFrameCount(Track) ||
			ChangesFrame(pDoc, Track, Part, Frame, static_cast<unsigned int>(Row % Length), f));
	if (!EnoughPatterns(pDoc, Track, Part.Channel, Frame, LastFrame, &Changes)) {
		Message.Format(_T("%s has run out of free patterns. Edit > Clean up > Remove unused patterns may make room."),
			static_cast<LPCTSTR>(RecordingChannelName(pDoc, Channel)));
		return false;
	}

	CInsertRecordingAction *pAction = new CInsertRecordingAction(std::vector<RecordedPart>(1, Part), static_cast<int>(Frame), Row % Length);
	pAction->KeepLinks();
	if (CursorRow >= 0)
		pAction->PlaceCursor(CursorRow / Length, CursorRow % Length);
	if (!pMainFrm->AddAction(pAction)) {
		Message = _T("The notes couldn't be changed.");
		return false;
	}
	return true;
}

// The Claude Code connection

bool WriteSongParts(CMainFrame *pMainFrm, const song_part_t *pParts, int Count, int Frame, int Row, int LinkClipId,
	int MoveFrame, int MoveBy, int Cursor, CString &Message, const ftapi::SectionStore *pSections)
{
	CFamiTrackerView *pView = static_cast<CFamiTrackerView*>(pMainFrm->GetActiveView());
	CFamiTrackerDoc *pDoc = pView->GetDocument();
	const unsigned int Track = static_cast<unsigned int>(pMainFrm->GetSelectedTrack());
	const int Length = static_cast<int>(pDoc->GetPatternLength(Track));
	if (Count < 0 || (Count == 0 && MoveBy == 0) || (Count > 0 && pParts == NULL) || Frame < 0 || Frame >= MAX_FRAMES || Row < 0 ||
		Row >= Length) {
		Message = _T("There's nothing to write there.");
		return false;
	}
	// Frames to move: ones the song has, leaving at least one, at most MAX_FRAMES
	const int FrameCount = static_cast<int>(pDoc->GetFrameCount(Track));
	if (MoveBy != 0 && (MoveFrame < 0 || MoveFrame > FrameCount || (MoveBy < 0 && MoveFrame - MoveBy > FrameCount) ||
		FrameCount + MoveBy < 1 || FrameCount + MoveBy > MAX_FRAMES)) {
		Message = _T("Those frames can't move.");
		return false;
	}
	// Frames put in need a new empty pattern in every channel
	for (int c = 0; MoveBy > 0 && c < static_cast<int>(pDoc->GetAvailableChannels()); ++c) {
		const std::vector<int> Layout = FramesAfterMove(pDoc, Track, static_cast<unsigned int>(c), MoveFrame, MoveBy);
		if (std::find(Layout.begin(), Layout.end(), -1) != Layout.end()) {
			Message.Format(_T("%s has run out of free patterns. Edit > Clean up > Remove unused patterns may make room."),
				static_cast<LPCTSTR>(RecordingChannelName(pDoc, c)));
			return false;
		}
	}
	std::vector<RecordedPart> Parts;
	for (int p = 0; p < Count; ++p) {
		const song_part_t &In = pParts[p];
		if (In.Channel < 0 || In.Channel >= static_cast<int>(pDoc->GetAvailableChannels()) || In.Rows < 1 || In.pCells == NULL)
			continue;
		// At most MAX_FRAMES frames
		const int Rows = std::min(In.Rows, (MAX_FRAMES - Frame) * Length - Row);
		RecordedPart Part;
		Part.Channel = static_cast<unsigned int>(In.Channel);
		Part.Rows = Rows;
		Part.Replace = true;
		Part.Exact = In.Exact;
		for (int r = 0; r < Rows; ++r)
			if (Part.Exact || !IsEmptyCell(In.pCells[r]))
				Part.Cells.push_back(CellOf(r, In.pCells[r]));
		// Patterns of its own in the frames it touches once frames moved (for an exact part, the ones it changes)
		if (!EnoughPatternsAfterMove(pDoc, Track, Part, static_cast<unsigned int>(Frame), static_cast<unsigned int>(Row),
			FramesAfterMove(pDoc, Track, Part.Channel, MoveFrame, MoveBy))) {
			Message.Format(_T("%s has run out of free patterns. Edit > Clean up > Remove unused patterns may make room."),
				static_cast<LPCTSTR>(RecordingChannelName(pDoc, In.Channel)));
			return false;
		}
		Parts.push_back(Part);
	}
	if (Parts.empty() && MoveBy == 0) {
		Message = _T("There's nothing to write there.");
		return false;
	}
	CInsertRecordingAction *pAction = new CInsertRecordingAction(Parts, Frame, Row);
	if (LinkClipId >= 0)
		pAction->LinkTo(LinkClipId);
	if (MoveBy != 0)
		pAction->MoveFrames(MoveFrame, MoveBy);
	if (pSections != NULL)
		pAction->SetSections(pSections->Get(0));
	if (Cursor >= 0)
		pAction->PlaceCursor(Cursor / Length, Cursor % Length);
	if (!pMainFrm->AddAction(pAction)) {
		Message = _T("The song couldn't be changed.");
		return false;
	}
	return true;
}

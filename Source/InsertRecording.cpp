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

// Edit > Insert recording. The transcription engine in Transcribe/ finds the
// notes; this file turns them into pattern cells and writes them into the
// open song as one undoable action.

// Standard and engine headers come before stdafx.h, whose DEBUG_NEW macro
// would otherwise rewrite the placement new inside the standard library.
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "Transcribe/Transcribe.h"
#include "stdafx.h"
#include "FamiTracker.h"
#include "FamiTrackerDoc.h"
#include "FamiTrackerView.h"
#include "MainFrm.h"
#include "PatternEditor.h"
#include "TrackerChannel.h"
#include "Action.h"
#include "InsertRecording.h"

namespace {

// Action ID, well away from the CPatternAction and CFrameAction values
const int ACT_INSERT_RECORDING = 0x1000;

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
const signed char KICK_VOLUME[] = { 15, 12, 8, 5, 3, 1, 0 };
const signed char SNARE_VOLUME[] = { 14, 11, 9, 7, 5, 3, 2, 1, 0 };
const signed char HIHAT_VOLUME[] = { 10, 6, 3, 1, 0 };

struct RecordedCell {
	int Row;					// rows after the start row
	unsigned char Note;			// note_t
	unsigned char Octave;
	unsigned char Instrument;
	unsigned char Vol;
	unsigned char Effect;		// effect_t in the first effect column, EF_NONE for none
	unsigned char EffectParam;
};

RecordedCell MakeCell(int Row, int Note, int Octave, int Instrument, int Vol, int Effect = EF_NONE, int EffectParam = 0)
{
	RecordedCell Cell;
	Cell.Row = Row;
	Cell.Note = static_cast<unsigned char>(Note);
	Cell.Octave = static_cast<unsigned char>(Octave);
	Cell.Instrument = static_cast<unsigned char>(Instrument);
	Cell.Vol = static_cast<unsigned char>(Vol);
	Cell.Effect = static_cast<unsigned char>(Effect);
	Cell.EffectParam = static_cast<unsigned char>(EffectParam);
	return Cell;
}

// The effects the recorder writes: slides, vibrato and portamento
bool IsExpressionEffect(unsigned char Effect)
{
	return Effect == EF_SLIDE_UP || Effect == EF_SLIDE_DOWN || Effect == EF_VIBRATO || Effect == EF_PORTAMENTO;
}

int EffectFromLetter(char Letter)
{
	switch (Letter) {
	case 'Q': return EF_SLIDE_UP;
	case 'R': return EF_SLIDE_DOWN;
	case '4': return EF_VIBRATO;
	}
	return EF_NONE;
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

// Writes one channel's cells from the cursor on. Every frame it touches first
// gets a pattern of its own in that channel, so frames that played the same
// pattern stay as they were. Frames past the end of the song are added.
class CInsertRecordingAction : public CAction
{
public:
	CInsertRecordingAction(const std::vector<RecordedCell> &Cells, int Rows, int Channel) :
		CAction(ACT_INSERT_RECORDING), m_Cells(Cells), m_iRows(Rows),
		m_iTrack(0), m_iFrame(0), m_iChannel(static_cast<unsigned int>(Channel)), m_iRow(0), m_iColumn(0),
		m_iOldFrameCount(0)
	{
	}

	bool SaveState(CMainFrame *pMainFrm);
	void Undo(CMainFrame *pMainFrm);
	void Redo(CMainFrame *pMainFrm);

private:
	struct Reassigned {
		unsigned int Frame;
		unsigned int OldPattern;
		unsigned int NewPattern;
	};

	void Backup(const CFamiTrackerDoc *pDoc, unsigned int Pattern);
	void Finish(CMainFrame *pMainFrm, CFamiTrackerDoc *pDoc, CPatternEditor *pEditor) const;

	std::vector<RecordedCell> m_Cells;	// sorted by row
	int m_iRows;						// rows covered, counted from the start row

	unsigned int m_iTrack;
	unsigned int m_iFrame;
	unsigned int m_iChannel;
	unsigned int m_iRow;
	int m_iColumn;
	unsigned int m_iOldFrameCount;
	std::vector<Reassigned> m_Reassigned;
	std::map<unsigned int, std::vector<stChanNote> > m_Backup;	// pattern -> its rows before the insert
};

bool CInsertRecordingAction::SaveState(CMainFrame *pMainFrm)
{
	CFamiTrackerView *pView = static_cast<CFamiTrackerView*>(pMainFrm->GetActiveView());
	CPatternEditor *pEditor = pView->GetPatternEditor();

	m_iTrack = static_cast<unsigned int>(pMainFrm->GetSelectedTrack());
	m_iFrame = static_cast<unsigned int>(pEditor->GetFrame());
	m_iRow = static_cast<unsigned int>(pEditor->GetRow());
	m_iColumn = 0;	// the note column of the channel written to

	Redo(pMainFrm);
	return true;
}

void CInsertRecordingAction::Backup(const CFamiTrackerDoc *pDoc, unsigned int Pattern)
{
	if (m_Backup.count(Pattern) != 0)
		return;
	const unsigned int Length = pDoc->GetPatternLength(m_iTrack);
	std::vector<stChanNote> &Rows = m_Backup[Pattern];
	Rows.resize(Length);
	for (unsigned int r = 0; r < Length; ++r)
		pDoc->GetDataAtPattern(m_iTrack, Pattern, m_iChannel, r, &Rows[r]);
}

void CInsertRecordingAction::Redo(CMainFrame *pMainFrm)
{
	CFamiTrackerView *pView = static_cast<CFamiTrackerView*>(pMainFrm->GetActiveView());
	CFamiTrackerDoc *pDoc = pView->GetDocument();
	CPatternEditor *pEditor = pView->GetPatternEditor();
	const unsigned int Length = pDoc->GetPatternLength(m_iTrack);
	const unsigned int LastFrame = m_iFrame + (m_iRow + static_cast<unsigned int>(m_iRows) - 1) / Length;

	pDoc->LockDocument();

	// Add frames at the end of the song when the recording runs past it
	m_iOldFrameCount = pDoc->GetFrameCount(m_iTrack);
	while (pDoc->GetFrameCount(m_iTrack) <= LastFrame) {
		if (!pDoc->InsertFrame(m_iTrack, pDoc->GetFrameCount(m_iTrack)))
			break;	// MAX_FRAMES
	}
	const unsigned int Frames = pDoc->GetFrameCount(m_iTrack);

	// One pattern per touched frame
	m_Reassigned.clear();
	m_Backup.clear();
	for (unsigned int f = m_iFrame; f <= LastFrame && f < Frames; ++f) {
		unsigned int Pattern = pDoc->GetPatternAtFrame(m_iTrack, f, m_iChannel);
		if (UsedElsewhere(pDoc, m_iTrack, m_iChannel, f, Pattern)) {
			const int Free = FindFreePattern(pDoc, m_iTrack, m_iChannel);
			if (Free >= 0) {
				const unsigned int NewPattern = static_cast<unsigned int>(Free);
				Backup(pDoc, NewPattern);
				pDoc->CopyPattern(m_iTrack, Free, Pattern, m_iChannel);
				pDoc->SetPatternAtFrame(m_iTrack, f, m_iChannel, NewPattern);
				Reassigned Entry = { f, Pattern, NewPattern };
				m_Reassigned.push_back(Entry);
				Pattern = NewPattern;
			}
		}
		Backup(pDoc, Pattern);
	}

	// Write the cells. Rows between them are cleared in this channel (note,
	// instrument and volume). Effects stay, except earlier slides and vibrato
	// in the first effect column, which would play over the new notes.
	size_t Next = 0;
	for (int r = 0; r < m_iRows; ++r) {
		const unsigned int Position = m_iRow + static_cast<unsigned int>(r);
		const unsigned int Frame = m_iFrame + Position / Length;
		if (Frame >= Frames)
			break;
		const unsigned int Row = Position % Length;
		stChanNote Note;
		pDoc->GetNoteData(m_iTrack, Frame, m_iChannel, Row, &Note);
		if (IsExpressionEffect(Note.EffNumber[0])) {
			Note.EffNumber[0] = EF_NONE;
			Note.EffParam[0] = 0;
		}
		if (Next < m_Cells.size() && m_Cells[Next].Row == r) {
			const RecordedCell &Cell = m_Cells[Next++];
			Note.Note = Cell.Note;
			Note.Octave = Cell.Octave;
			Note.Instrument = Cell.Instrument;
			Note.Vol = Cell.Vol;
			if (Cell.Effect != EF_NONE) {
				Note.EffNumber[0] = Cell.Effect;
				Note.EffParam[0] = Cell.EffectParam;
			}
		}
		else {
			Note.Note = NONE;
			Note.Octave = 0;
			Note.Instrument = static_cast<unsigned char>(MAX_INSTRUMENTS);
			Note.Vol = static_cast<unsigned char>(MAX_VOLUME);
		}
		pDoc->SetNoteData(m_iTrack, Frame, m_iChannel, Row, &Note);
	}

	pDoc->UnlockDocument();
	Finish(pMainFrm, pDoc, pEditor);
}

void CInsertRecordingAction::Undo(CMainFrame *pMainFrm)
{
	CFamiTrackerView *pView = static_cast<CFamiTrackerView*>(pMainFrm->GetActiveView());
	CFamiTrackerDoc *pDoc = pView->GetDocument();
	CPatternEditor *pEditor = pView->GetPatternEditor();
	const unsigned int Length = pDoc->GetPatternLength(m_iTrack);

	pDoc->LockDocument();
	for (std::map<unsigned int, std::vector<stChanNote> >::const_iterator it = m_Backup.begin(); it != m_Backup.end(); ++it) {
		for (unsigned int r = 0; r < Length && r < it->second.size(); ++r)
			pDoc->SetDataAtPattern(m_iTrack, it->first, m_iChannel, r, &it->second[r]);
	}
	for (size_t i = m_Reassigned.size(); i-- > 0; )
		pDoc->SetPatternAtFrame(m_iTrack, m_Reassigned[i].Frame, m_iChannel, m_Reassigned[i].OldPattern);
	pDoc->SetFrameCount(m_iTrack, m_iOldFrameCount);
	pDoc->UnlockDocument();

	Finish(pMainFrm, pDoc, pEditor);
}

void CInsertRecordingAction::Finish(CMainFrame *pMainFrm, CFamiTrackerDoc *pDoc, CPatternEditor *pEditor) const
{
	pEditor->MoveToFrame(static_cast<int>(m_iFrame));
	pEditor->MoveToChannel(static_cast<int>(m_iChannel));
	pEditor->MoveToRow(static_cast<int>(m_iRow));
	pEditor->MoveToColumn(m_iColumn);
	pEditor->InvalidateCursor();

	pDoc->SetModifiedFlag();
	pDoc->UpdateAllViews(NULL, UPDATE_FRAME);	// redraws the frame list and the patterns
	pMainFrm->UpdateControls();
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

bool GuessRecording(LPCTSTR Path, recording_kind_t &Kind, CString &Description, CString &Message)
{
	transcribe::Audio Audio;
	if (!ReadRecording(Path, Audio, Message))
		return false;
	const transcribe::SourceGuess Guess = transcribe::GuessSource(Audio);
	switch (Guess.source) {
	case transcribe::Source::Whistle: Kind = RECORDING_WHISTLE; break;
	case transcribe::Source::Voice: Kind = RECORDING_VOICE; break;
	case transcribe::Source::Taps: Kind = RECORDING_DRUMS; break;
	}
	Description = CString(transcribe::SourceName(Guess.source));
	return true;
}

bool InsertRecording(CMainFrame *pMainFrm, LPCTSTR Path, recording_kind_t Kind, int Channel, bool Expression,
	CString &Message)
{
	CFamiTrackerView *pView = static_cast<CFamiTrackerView*>(pMainFrm->GetActiveView());
	CFamiTrackerDoc *pDoc = pView->GetDocument();
	CPatternEditor *pEditor = pView->GetPatternEditor();
	const unsigned int Track = static_cast<unsigned int>(pMainFrm->GetSelectedTrack());

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

	transcribe::Audio Audio;
	if (!ReadRecording(Path, Audio, Message))
		return false;

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
		transcribe::ClassifyHits(Hits);
		const std::vector<transcribe::GridHit> Drums = transcribe::QuantizeHits(Hits, Grid, true);

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
		// Whistling, humming and singing become a melody
		const transcribe::PitchOptions Pitch =
			transcribe::PitchOptionsFor(Kind == RECORDING_WHISTLE ? transcribe::Source::Whistle : transcribe::Source::Voice);
		const std::vector<transcribe::PitchFrame> Frames = transcribe::TrackPitch(Audio, Pitch);
		std::vector<transcribe::Note> Notes = transcribe::SegmentNotes(Frames, transcribe::NoteOptions());
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

		transcribe::Transpose(Notes, transcribe::OctaveShiftFor(Notes, Low ? 48 : 67));
		const std::vector<transcribe::GridNote> Melody = transcribe::Quantize(Notes, Grid, transcribe::QuantizeOptions());

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
		for (size_t i = 0; i < Line.size(); ++i) {
			const transcribe::MelodyCell &Cell = Line[i];
			const int Effect = EffectFromLetter(Cell.effect);
			if (Cell.kind == transcribe::MelodyCell::Note) {
				int Index = Cell.midi - 24;	// FamiTracker's C-0 is MIDI note 24
				if (Index < 0)
					Index = 0;
				if (Index > 95)
					Index = 95;
				Cells.push_back(MakeCell(Cell.row, Index % 12 + 1, Index / 12, Instrument,
					Cell.volume >= 0 ? Cell.volume : MAX_VOLUME, Effect, Cell.effectParam));
			}
			else if (Cell.kind == transcribe::MelodyCell::Release)
				Cells.push_back(MakeCell(Cell.row, Release ? RELEASE : HALT, 0, MAX_INSTRUMENTS, MAX_VOLUME));
			else
				Cells.push_back(MakeCell(Cell.row, NONE, 0, MAX_INSTRUMENTS, MAX_VOLUME, Effect, Cell.effectParam));
		}
		Rows = Cells.back().Row + 1;
		What.Format(_T("%d notes"), static_cast<int>(Melody.size()));
	}

	// Room: the song has at most MAX_FRAMES frames
	const unsigned int Length = pDoc->GetPatternLength(Track);
	const unsigned int Frame = static_cast<unsigned int>(pEditor->GetFrame());
	const unsigned int Row = static_cast<unsigned int>(pEditor->GetRow());
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

	// Free patterns: one for each frame added at the end of the song, and one
	// for each touched frame whose pattern another frame plays too
	const unsigned int FrameCount = pDoc->GetFrameCount(Track);
	std::vector<unsigned int> Playing(FrameCount);
	for (unsigned int f = 0; f < FrameCount; ++f)
		Playing[f] = pDoc->GetPatternAtFrame(Track, f, Channel);
	unsigned int Needed = (LastFrame >= FrameCount) ? LastFrame + 1 - FrameCount : 0;
	for (unsigned int f = Frame; f <= LastFrame && f < FrameCount; ++f) {
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
	if (Needed > Free) {
		Message = _T("This channel has run out of free patterns. Edit > Clean up > Remove unused patterns may make room.");
		return false;
	}

	CInsertRecordingAction *pAction = new CInsertRecordingAction(Cells, Rows, Channel);
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

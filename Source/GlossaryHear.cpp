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

// Hear it (see GlossaryHear.h)

#include "stdafx.h"
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
#include "Api/Glossary.h"
#include "Api/Music.h"
#include "Api/Song.h"
#include "FamiTracker.h"
#include "FamiTrackerDoc.h"
#include "FamiTrackerView.h"
#include "PatternEditor.h"
#include "MainFrm.h"
#include "SoundGen.h"
#include "TrackerChannel.h"
#include "InstrumentLibrary.h"
#include "SoundPreview.h"
#include "GlossaryTips.h"
#include "GlossaryHear.h"

// The examples' effects are FamiTracker's own (Api/Music.h numbers them alike)
static_assert(EF_PAN == ftapi::EFF_PAN && EF_COUNT == ftapi::EFF_COUNT, "the effects are numbered alike");

namespace {

const UINT_PTR TMR_STEP = 1;
const UINT STEP_MS = 10;
const DWORD AFTER_MS = 150;		// after the last note: then the channels are reset
const DWORD VOICE_GAP_MS = 18;	// a channel takes one queued note a frame: its notes go this far apart at least
const char TRIAL_NAME[] = "Glossary example";
const unsigned int VRC7_PIANO = 3;	// the VRC7's built-in sound its examples play (Api/Presets.h's Vrc7Patches)

// The channel (chan_id_t) a voice plays on
int VoiceChannelId(int Voice)
{
	switch (Voice) {
	case ftapi::HEAR_PULSE1: return CHANID_SQUARE1;
	case ftapi::HEAR_PULSE2: return CHANID_SQUARE2;
	case ftapi::HEAR_TRIANGLE: return CHANID_TRIANGLE;
	case ftapi::HEAR_NOISE: return CHANID_NOISE;
	case ftapi::HEAR_VRC6_PULSE: return CHANID_VRC6_PULSE1;
	case ftapi::HEAR_SAWTOOTH: return CHANID_VRC6_SAWTOOTH;
	case ftapi::HEAR_MMC5: return CHANID_MMC5_SQUARE1;
	case ftapi::HEAR_N163: return CHANID_N163_CHAN1;
	case ftapi::HEAR_FDS: return CHANID_FDS;
	case ftapi::HEAR_VRC7: return CHANID_VRC7_CH1;
	case ftapi::HEAR_5B: return CHANID_S5B_CH1;
	default: return -1;
	}
}

// The chip whose instruments a channel plays (the MMC5 plays the 2A03's)
int InstrumentChip(int Chip)
{
	return Chip == SNDCHIP_MMC5 ? SNDCHIP_NONE : Chip;
}

inst_type_t ChipInstrumentType(int Chip)
{
	switch (Chip) {
	case SNDCHIP_VRC6: return INST_VRC6;
	case SNDCHIP_VRC7: return INST_VRC7;
	case SNDCHIP_FDS: return INST_FDS;
	case SNDCHIP_N163: return INST_N163;
	case SNDCHIP_S5B: return INST_S5B;
	default: return INST_2A03;
	}
}

// An instrument of the song that plays a note as it is: no sequences on (the 2A03's, the VRC6's and the 5B's)
bool IsPlain(CFamiTrackerDoc *pDoc, int Index, inst_type_t Type)
{
	if (!pDoc->IsInstrumentUsed(static_cast<unsigned int>(Index)) || pDoc->GetInstrumentType(static_cast<unsigned int>(Index)) != Type)
		return false;
	CInstrument *pInstrument = pDoc->GetInstrument(static_cast<unsigned int>(Index));	// retained
	if (pInstrument == NULL)
		return false;
	bool Plain = Type == INST_2A03 || Type == INST_VRC6 || Type == INST_S5B;
	for (int i = 0; Plain && i < CInstrument2A03::SEQUENCE_COUNT; ++i) {
		if (Type == INST_2A03)
			Plain = static_cast<CInstrument2A03*>(pInstrument)->GetSeqEnable(i) == 0;
		else if (Type == INST_VRC6)
			Plain = static_cast<CInstrumentVRC6*>(pInstrument)->GetSeqEnable(i) == 0;
		else
			Plain = static_cast<CInstrumentS5B*>(pInstrument)->GetSeqEnable(i) == 0;
	}
	pInstrument->Release();
	return Plain;
}

class CGlossaryPlayer : public CWnd
{
public:
	CGlossaryPlayer() : m_pMainFrame(NULL), m_iNext(0), m_dwStart(0), m_iSongSerial(0), m_bPlaying(false), m_bSongStopped(true)
	{
		Clear();
	}
	virtual ~CGlossaryPlayer()
	{
		if (m_hWnd != NULL)
			DestroyWindow();
	}

	// Plays the example on the song's channels (HEAR_HERE on Here with HereInstrument, or a plain one for its chip
	// when that's -1): false with why it can't
	bool Play(CMainFrame *pMainFrame, const ftapi::HearExample &Example, int Here, int HereInstrument, CString &Why);
	void Stop()
	{
		if (m_bPlaying)
			Finish(true);
	}
	bool IsPlaying() const { return m_bPlaying; }
	CString Says() const { return m_bPlaying ? m_strSays : CString(); }
	void Close()
	{
		Stop();
		if (m_hWnd != NULL)
			DestroyWindow();
	}

protected:
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	DECLARE_MESSAGE_MAP()

private:
	void Clear()
	{
		for (int v = 0; v < ftapi::HEAR_VOICES; ++v) {
			m_Channel[v] = -1;
			m_Instrument[v] = -1;
			m_Queued[v] = 0;
			m_bQueued[v] = false;
		}
	}
	int InstrumentFor(CFamiTrackerDoc *pDoc, int Chip, CString &Why);
	void Queue(const ftapi::HearEvent &Event);
	void Finish(bool Silence);
	void DropTrials(CFamiTrackerDoc *pDoc);

	CMainFrame *m_pMainFrame;
	std::vector<ftapi::HearEvent> m_Events;
	size_t m_iNext;
	DWORD m_dwStart;
	int m_Channel[ftapi::HEAR_VOICES];		// by voice: the song's channel (a column), -1 for none
	int m_Instrument[ftapi::HEAR_VOICES];	// and the instrument it plays
	DWORD m_Queued[ftapi::HEAR_VOICES];		// when the voice took its last note
	bool m_bQueued[ftapi::HEAR_VOICES];
	std::vector<int> m_Trials;				// the instruments made for this example, taken out again after it
	unsigned int m_iSongSerial;				// the song they're in (CFamiTrackerDoc::GetSongSerial)
	bool m_bPlaying;
	bool m_bSongStopped;					// the song was seen stopped since this began: a song that starts now ends it
	CString m_strSays;
};

BEGIN_MESSAGE_MAP(CGlossaryPlayer, CWnd)
	ON_WM_TIMER()
END_MESSAGE_MAP()

CGlossaryPlayer *g_pPlayer = NULL;

CGlossaryPlayer &Player()
{
	if (g_pPlayer == NULL)
		g_pPlayer = new CGlossaryPlayer();
	return *g_pPlayer;
}

// The song's own plain instrument of the chip, else one made for the example
int CGlossaryPlayer::InstrumentFor(CFamiTrackerDoc *pDoc, int Chip, CString &Why)
{
	Chip = InstrumentChip(Chip);
	const inst_type_t Type = ChipInstrumentType(Chip);
	for (int i = 0; i < MAX_INSTRUMENTS; ++i)
		if (IsPlain(pDoc, i, Type))
			return i;
	for (size_t i = 0; i < m_Trials.size(); ++i)
		if (pDoc->GetInstrumentType(static_cast<unsigned int>(m_Trials[i])) == Type)
			return m_Trials[i];
	if (pDoc->FindFreeInstrumentSlot() < 0) {
		Why = _T("The song has 64 instruments, so there's no room for the example's own plain one. Remove one it doesn't use ")
			_T("(Edit > Clean up) to hear the examples.");
		return -1;
	}
	// Made quietly: the instrument list isn't told (RemoveInstrumentAndSequences tells it once the example is over), and
	// the song stays as changed or unchanged as it was
	const BOOL Modified = pDoc->IsModified();
	const int Index = pDoc->AddInstrument(TRIAL_NAME, Chip);
	pDoc->SetModifiedFlag(Modified);
	if (Index < 0 || Index >= MAX_INSTRUMENTS) {
		Why = _T("The example's instrument couldn't be made.");
		return -1;
	}
	if (Type == INST_VRC7) {
		CInstrumentContainer<CInstrumentVRC7> Vrc7(pDoc, Index);
		if (Vrc7() != NULL)
			Vrc7()->SetPatch(VRC7_PIANO);
	}
	m_Trials.push_back(Index);
	return Index;
}

bool CGlossaryPlayer::Play(CMainFrame *pMainFrame, const ftapi::HearExample &Example, int Here, int HereInstrument, CString &Why)
{
	Stop();
	Clear();
	m_pMainFrame = pMainFrame;
	CFamiTrackerDoc *pDoc = static_cast<CFamiTrackerDoc*>(pMainFrame->GetActiveDocument());
	if (pDoc == NULL || !pDoc->IsFileLoaded()) {
		Why = _T("Open a song first: the examples play on its channels.");
		return false;
	}
	if (Example.Events.empty()) {
		Why = _T("There's nothing to hear.");
		return false;
	}
	if (m_hWnd == NULL && !CreateEx(0, AfxRegisterWndClass(0), _T("Glossary example"), 0, 0, 0, 0, 0, HWND_MESSAGE, NULL)) {
		Why = _T("The example couldn't be played.");
		return false;
	}
	// Each voice's channel in the song
	bool Used[ftapi::HEAR_VOICES] = { false };
	for (size_t i = 0; i < Example.Events.size(); ++i)
		if (Example.Events[i].Voice >= 0 && Example.Events[i].Voice < ftapi::HEAR_VOICES)
			Used[Example.Events[i].Voice] = true;
	const int Available = static_cast<int>(pDoc->GetAvailableChannels());
	if (Example.Chip == "VRC6 Plus" && !pDoc->IsVrc6Plus()) {
		Why = CString(ftapi::HearChipHow(Example.Chip).c_str());
		return false;
	}
	for (int v = 0; v < ftapi::HEAR_VOICES; ++v) {
		if (!Used[v])
			continue;
		if (v == ftapi::HEAR_HERE) {
			if (Here < 0 || Here >= Available) {
				Why = _T("There's no such channel.");
				return false;
			}
			m_Channel[v] = Here;
			continue;
		}
		const int Index = pDoc->GetChannelIndex(VoiceChannelId(v));
		if (Index < 0 || Index >= Available) {
			Why = Example.Chip.empty() ? CString(_T("This song has no channel for the example.")) :
				CString(ftapi::HearChipHow(Example.Chip).c_str());
			return false;
		}
		m_Channel[v] = Index;
	}
	// And the instrument it plays
	m_iSongSerial = pDoc->GetSongSerial();
	for (int v = 0; v < ftapi::HEAR_VOICES; ++v) {
		if (!Used[v])
			continue;
		if (v == ftapi::HEAR_HERE && HereInstrument >= 0 && HereInstrument < MAX_INSTRUMENTS &&
			pDoc->IsInstrumentUsed(static_cast<unsigned int>(HereInstrument))) {
			m_Instrument[v] = HereInstrument;
			continue;
		}
		m_Instrument[v] = InstrumentFor(pDoc, pDoc->GetChipType(m_Channel[v]), Why);
		if (m_Instrument[v] < 0) {
			DropTrials(pDoc);
			return false;
		}
	}
	// The song and the sound browser's sound stop, as for a preview
	if (theApp.IsPlaying())
		theApp.StopPlayer();
	CSoundPreview *pPreview = pMainFrame->GetSoundPreview();
	if (pPreview != NULL)
		pPreview->Stop();
	m_bSongStopped = !theApp.IsPlaying();
	m_Events = Example.Events;
	m_iNext = 0;
	m_strSays = CString(Example.Says.c_str());
	m_dwStart = ::GetTickCount();
	m_bPlaying = true;
	SetTimer(TMR_STEP, STEP_MS, NULL);
	OnTimer(TMR_STEP);
	return true;
}

// A note, a halt or a change of volume or effect to its voice's channel
void CGlossaryPlayer::Queue(const ftapi::HearEvent &Event)
{
	const int Channel = m_Channel[Event.Voice];
	if (Channel < 0)
		return;
	stChanNote Cell;
	memset(&Cell, 0, sizeof(Cell));
	Cell.Instrument = MAX_INSTRUMENTS;
	Cell.Vol = MAX_VOLUME;
	if (Event.Note == ftapi::HEAR_HALT)
		Cell.Note = HALT;
	else if (Event.Note >= 0) {
		Cell.Note = static_cast<unsigned char>(Event.Note % NOTE_RANGE + 1);
		Cell.Octave = static_cast<unsigned char>(Event.Note / NOTE_RANGE);
		Cell.Instrument = static_cast<unsigned char>(m_Instrument[Event.Voice]);
	}
	if (Event.Volume >= 0)
		Cell.Vol = static_cast<unsigned char>(std::min(Event.Volume, MAX_VOLUME - 1));
	if (Event.Effect > ftapi::EFF_NONE && Event.Effect < ftapi::EFF_COUNT) {
		Cell.EffNumber[0] = static_cast<unsigned char>(Event.Effect);
		Cell.EffParam[0] = static_cast<unsigned char>(Event.Param);
	}
	theApp.GetSoundGenerator()->QueueNote(Channel, Cell, NOTE_PRIO_2);
}

void CGlossaryPlayer::DropTrials(CFamiTrackerDoc *pDoc)
{
	if (m_Trials.empty())
		return;
	// The same song only: another one may have an instrument of its own there now
	if (pDoc != NULL && pDoc->GetSongSerial() == m_iSongSerial) {
		const BOOL Modified = pDoc->IsModified();
		for (size_t i = 0; i < m_Trials.size(); ++i)
			if (pDoc->IsInstrumentUsed(static_cast<unsigned int>(m_Trials[i])))
				RemoveInstrumentAndSequences(pDoc, static_cast<unsigned int>(m_Trials[i]));
		pDoc->SetModifiedFlag(Modified);
	}
	m_Trials.clear();
}

void CGlossaryPlayer::Finish(bool Silence)
{
	if (m_hWnd != NULL)
		KillTimer(TMR_STEP);
	m_bPlaying = false;
	m_Events.clear();
	m_iNext = 0;
	// The channels as a song leaves them, so the example's effects (a vibrato, a pan) don't stay on them
	if (Silence && !theApp.IsPlaying())
		theApp.GetSoundGenerator()->SilentAll();
	DropTrials(CFamiTrackerDoc::GetDoc());
}

void CGlossaryPlayer::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent != TMR_STEP) {
		CWnd::OnTimer(nIDEvent);
		return;
	}
	if (!m_bPlaying) {
		KillTimer(TMR_STEP);
		return;
	}
	// The song started after the example, or another song came: the channels are the song's again
	CFamiTrackerDoc *pDoc = CFamiTrackerDoc::GetDoc();
	if (!m_bSongStopped)
		m_bSongStopped = !theApp.IsPlaying();
	else if (theApp.IsPlaying()) {
		Finish(false);
		return;
	}
	if (pDoc == NULL || !pDoc->IsFileLoaded() || pDoc->GetSongSerial() != m_iSongSerial) {
		Finish(false);
		return;
	}
	const DWORD Now = ::GetTickCount();
	const DWORD Elapsed = Now - m_dwStart;
	bool Took[ftapi::HEAR_VOICES] = { false };
	while (m_iNext < m_Events.size() && static_cast<DWORD>(m_Events[m_iNext].At) <= Elapsed) {
		const ftapi::HearEvent &Event = m_Events[m_iNext];
		const int Voice = Event.Voice;
		// One note a frame per channel: a later one waits (and those after it, in their order)
		if (Took[Voice] || (m_bQueued[Voice] && Now - m_Queued[Voice] < VOICE_GAP_MS))
			break;
		Queue(Event);
		Took[Voice] = true;
		m_bQueued[Voice] = true;
		m_Queued[Voice] = Now;
		++m_iNext;
	}
	if (m_iNext >= m_Events.size() && Elapsed > static_cast<DWORD>(m_Events.back().At) + AFTER_MS)
		Finish(true);
}

// The note an effect plays on: the row's own, else the last one before it on the channel (this frame and the ones
// before); the instrument it plays, and the volume written last at or before the effect's row
void NoteAtEffect(CFamiTrackerDoc *pDoc, int Track, int Frame, int Row, int Channel, int &Pitch, int &Instrument, int &Volume)
{
	Pitch = Instrument = Volume = -1;
	const int Length = static_cast<int>(pDoc->GetPatternLength(Track));
	int Looked = 0;
	for (int f = Frame; f >= 0 && Looked < 2048; --f)
		for (int r = f == Frame ? Row : Length - 1; r >= 0 && Looked < 2048; --r, ++Looked) {
			stChanNote Cell;
			pDoc->GetNoteData(Track, f, Channel, r, &Cell);
			if (Volume < 0 && Cell.Vol < MAX_VOLUME)
				Volume = Cell.Vol;
			if (Pitch < 0 && Cell.Note >= C && Cell.Note <= B)
				Pitch = Cell.Octave * NOTE_RANGE + Cell.Note - 1;
			if (Pitch >= 0 && Instrument < 0 && Cell.Instrument < MAX_INSTRUMENTS)
				Instrument = Cell.Instrument;
			if (Pitch >= 0 && Instrument >= 0 && Volume >= 0)
				return;
		}
}

}	// namespace

ftapi::ChannelInfo GlossaryChannelInfo(CFamiTrackerDoc *pDoc, int Channel)
{
	ftapi::ChannelInfo Info;
	if (pDoc == NULL || Channel < 0 || Channel >= pDoc->GetChannelCount())
		return Info;
	const int Id = pDoc->GetChannelType(Channel);
	Info.ChanId = Id;
	Info.Name = static_cast<LPCSTR>(CStringA(pDoc->GetChannel(Channel)->GetChannelName()));
	switch (Id) {
	case CHANID_SQUARE1: case CHANID_SQUARE2:
		Info.Kind = ftapi::KIND_PULSE; Info.Chip = "2A03"; break;
	case CHANID_TRIANGLE:
		Info.Kind = ftapi::KIND_TRIANGLE; Info.Chip = "2A03"; break;
	case CHANID_NOISE:
		Info.Kind = ftapi::KIND_NOISE; Info.Chip = "2A03"; break;
	case CHANID_DPCM:
		Info.Kind = ftapi::KIND_DPCM; Info.Chip = "2A03"; break;
	case CHANID_VRC6_PULSE1: case CHANID_VRC6_PULSE2:
		Info.Kind = ftapi::KIND_PULSE; Info.Chip = "VRC6"; break;
	case CHANID_VRC6_SAWTOOTH:
		Info.Kind = ftapi::KIND_SAWTOOTH; Info.Chip = "VRC6"; break;
	case CHANID_MMC5_SQUARE1: case CHANID_MMC5_SQUARE2:
		Info.Kind = ftapi::KIND_PULSE; Info.Chip = "MMC5"; break;
	case CHANID_FDS:
		Info.Kind = ftapi::KIND_OTHER; Info.Chip = "FDS"; break;
	case CHANID_S5B_CH1: case CHANID_S5B_CH2: case CHANID_S5B_CH3:
		Info.Kind = ftapi::KIND_SQUARE; Info.Chip = "5B"; break;
	default:
		if (Id >= CHANID_N163_CHAN1 && Id <= CHANID_N163_CHAN8) {
			Info.Kind = ftapi::KIND_OTHER;
			Info.Chip = "N163";
		}
		else if (Id >= CHANID_VRC7_CH1 && Id <= CHANID_VRC7_CH6) {
			Info.Kind = ftapi::KIND_FM;
			Info.Chip = "VRC7";
		}
		break;
	}
	return Info;
}

CString EffectOverriddenBy(CFamiTrackerDoc *pDoc, int Track, int Frame, int Row, int Channel, int Effect)
{
	if (pDoc == NULL || Effect != EF_DUTY_CYCLE)
		return CString();
	int Pitch = -1, Instrument = -1, Volume = -1;
	NoteAtEffect(pDoc, Track, Frame, Row, Channel, Pitch, Instrument, Volume);
	if (Instrument < 0 || Instrument >= MAX_INSTRUMENTS || !pDoc->IsInstrumentUsed(static_cast<unsigned int>(Instrument)))
		return CString();
	CInstrument *pInstrument = pDoc->GetInstrument(static_cast<unsigned int>(Instrument));	// retained
	if (pInstrument == NULL)
		return CString();
	LPCTSTR Sequence = NULL;
	switch (pInstrument->GetType()) {
	case INST_2A03:
		if (static_cast<CInstrument2A03*>(pInstrument)->GetSeqEnable(SEQ_DUTYCYCLE) != 0)
			Sequence = pDoc->GetChannelType(Channel) == CHANID_NOISE ? _T("a noise-mode sequence") : _T("a duty sequence");
		break;
	case INST_VRC6:
		if (static_cast<CInstrumentVRC6*>(pInstrument)->GetSeqEnable(SEQ_DUTYCYCLE) != 0)
			Sequence = _T("a duty sequence");
		break;
	case INST_N163:
		if (static_cast<CInstrumentN163*>(pInstrument)->GetSeqEnable(SEQ_DUTYCYCLE) != 0)
			Sequence = _T("a wave sequence");
		break;
	case INST_S5B:
		if (static_cast<CInstrumentS5B*>(pInstrument)->GetSeqEnable(SEQ_DUTYCYCLE) != 0)
			Sequence = _T("a noise and mode sequence");
		break;
	default:
		break;
	}
	CString Why;
	if (Sequence != NULL)
		Why.Format(_T("its instrument, %s, sets it with %s"), pInstrument->GetName(), Sequence);
	pInstrument->Release();
	return Why;
}

bool PlayGlossaryExample(CMainFrame *pMainFrame, const std::string &Id, CString &Why)
{
	ftapi::HearExample Example;
	if (!ftapi::GlossaryExample(Id, Example)) {
		Why = _T("There's no example to hear for this one.");
		return false;
	}
	return Player().Play(pMainFrame, Example, -1, -1, Why);
}

void HearEffectAtCursor(CMainFrame *pMainFrame)
{
	CFamiTrackerDoc *pDoc = static_cast<CFamiTrackerDoc*>(pMainFrame->GetActiveDocument());
	CFamiTrackerView *pView = CFamiTrackerView::GetView();
	if (pDoc == NULL || !pDoc->IsFileLoaded() || pView == NULL || pView->GetPatternEditor() == NULL)
		return;
	// The effect the tooltip shows, else the one at the cursor
	int Frame = 0, Row = 0, Channel = 0, Column = 0;
	if (!GlossaryTipCell(Frame, Row, Channel, Column)) {
		const CPatternEditor *pEditor = pView->GetPatternEditor();
		Frame = pEditor->GetFrame();
		Row = pEditor->GetRow();
		Channel = pEditor->GetChannel();
		Column = pEditor->GetColumn();
	}
	const int Track = pMainFrame->GetSelectedTrack();
	CString Line;
	std::string Entry;
	if (!GlossaryEffectLine(Track, Frame, Row, Channel, Column, Line, Entry)) {
		pMainFrame->SetMessageText(_T("Put the cursor on an effect (the columns after a note's volume) to hear it, plain and ")
			_T("then with its own numbers."));
		return;
	}
	const int Index = CPatternEditor::GetSelectColumn(Column) - COLUMN_EFF1;
	stChanNote Cell;
	pDoc->GetNoteData(Track, Frame, Channel, Row, &Cell);
	int Pitch = -1, Instrument = -1, Volume = -1;
	NoteAtEffect(pDoc, Track, Frame, Row, Channel, Pitch, Instrument, Volume);
	const ftapi::ChannelInfo Info = GlossaryChannelInfo(pDoc, Channel);
	if (Info.Kind == ftapi::KIND_DPCM && Instrument < 0) {
		pMainFrame->SetMessageText(_T("The DPCM channel plays samples: write a note with its instrument at or above the effect ")
			_T("to hear it."));
		return;
	}
	ftapi::HearExample Example;
	std::string Why;
	if (!ftapi::EffectHearExample(Cell.EffNumber[Index], Cell.EffParam[Index], Info, Pitch, Volume, Example, Why)) {
		pMainFrame->SetMessageText(CString(Why.c_str()));
		return;
	}
	// An effect the note's instrument overrides is heard on a plain sound, and the line says why it does nothing in the song
	const CString Overridden = EffectOverriddenBy(pDoc, Track, Frame, Row, Channel, Cell.EffNumber[Index]);
	CString Error;
	if (!Player().Play(pMainFrame, Example, Channel, Overridden.IsEmpty() ? Instrument : -1, Error)) {
		pMainFrame->SetMessageText(Error);
		return;
	}
	CString Text;
	Text.Format(_T("Hearing on %s%s: %s"), pDoc->GetChannel(Channel)->GetChannelName(),
		Overridden.IsEmpty() ? _T("") : _T(" with a plain sound"), static_cast<LPCTSTR>(CString(Example.Says.c_str())));
	if (!Overridden.IsEmpty())
		Text += _T(" In the song it changes nothing: ") + Overridden + _T(".");
	pMainFrame->SetMessageText(Text);
}

void StopGlossaryExample()
{
	if (g_pPlayer != NULL)
		g_pPlayer->Stop();
}

bool IsGlossaryExamplePlaying()
{
	return g_pPlayer != NULL && g_pPlayer->IsPlaying();
}

CString GlossaryExamplePlaying()
{
	return g_pPlayer != NULL ? g_pPlayer->Says() : CString();
}

void CloseGlossaryPlayer()
{
	if (g_pPlayer != NULL) {
		g_pPlayer->Close();
		delete g_pPlayer;
		g_pPlayer = NULL;
	}
}

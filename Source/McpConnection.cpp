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

// File > Connect Claude Code (see McpConnection.h): the song as the tools see
// it (Api/Song.h) over the open document, the window on the UI thread that
// answers requests, the settings and the dialog.

// rand_s, for the access token, needs this before stdlib.h
#define _CRT_RAND_S
#include <cstdlib>
// Standard and API headers come before stdafx.h, whose DEBUG_NEW macro would
// otherwise rewrite the placement new inside the standard library.
#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>
#include "Api/ChannelMix.h"
#include "Api/DpcmKit.h"
#include "Api/FdsSounds.h"
#include "Api/Http.h"
#include "Api/Json.h"
#include "Api/Mcp.h"
#include "Api/Music.h"
#include "Api/Network.h"
#include "Api/Presets.h"
#include "Api/Sections.h"
#include "Api/Song.h"
#include "Api/SoundEffects.h"
#include "Api/SoundIndex.h"
#include "Api/Tools.h"
#include "Transcribe/Transcribe.h"
#include "stdafx.h"
#include <shlobj.h>
#include "FamiTracker.h"
#include "FamiTrackerDoc.h"
#include "FamiTrackerView.h"
#include "MainFrm.h"
#include "PatternEditor.h"
#include "SoundGen.h"
#include "TrackerChannel.h"
#include "ChannelMap.h"
#include "Action.h"
#include "FrameAction.h"
#include "ChipSwitch.h"
#include "Clips.h"
#include "InsertRecording.h"
#include "InstrumentLibrary.h"
#include "AppFolders.h"
#include "ImportNsfDlg.h"
#include "McpSocket.h"
#include "McpConnection.h"
#include "VoiceEffectDlg.h"
#include "version.h"

// The API's copies of FamiTracker's values must match
static_assert(ftapi::NOTE_RELEASE == RELEASE && ftapi::NOTE_HALT == HALT && ftapi::NOTE_NONE == NONE, "notes");
static_assert(ftapi::NO_INSTRUMENT == MAX_INSTRUMENTS && ftapi::NO_VOLUME == MAX_VOLUME, "empty columns");
static_assert(ftapi::EFFECT_COLUMNS == MAX_EFFECT_COLUMNS && ftapi::PITCHES == OCTAVE_RANGE * 12, "sizes");
static_assert(ftapi::EFF_COUNT == EF_COUNT && ftapi::EFF_ARPEGGIO == EF_ARPEGGIO && ftapi::EFF_VIBRATO == EF_VIBRATO &&
	ftapi::EFF_SLIDE_UP == EF_SLIDE_UP && ftapi::EFF_SLIDE_DOWN == EF_SLIDE_DOWN && ftapi::EFF_SPEED == EF_SPEED &&
	ftapi::EFF_JUMP == EF_JUMP && ftapi::EFF_SKIP == EF_SKIP && ftapi::EFF_HALT == EF_HALT && ftapi::EFF_NOTE_CUT == EF_NOTE_CUT &&
	ftapi::EFF_VOLUME_SLIDE == EF_VOLUME_SLIDE && ftapi::EFF_PORTAMENTO == EF_PORTAMENTO && ftapi::EFF_TREMOLO == EF_TREMOLO &&
	ftapi::EFF_SUNSOFT_ENV_TYPE == EF_SUNSOFT_ENV_TYPE && ftapi::EFF_SUNSOFT_ENV_LO == EF_SUNSOFT_ENV_LO && ftapi::EFF_PAN == EF_PAN && ftapi::PAN_CENTER == PAN_CENTER, "effects");
static_assert(ftapi::SEQUENCE_VOLUME == SEQ_VOLUME && ftapi::SEQUENCE_ARPEGGIO == SEQ_ARPEGGIO && ftapi::SEQUENCE_PITCH == SEQ_PITCH &&
	ftapi::SEQUENCE_HIPITCH == SEQ_HIPITCH && ftapi::SEQUENCE_DUTY == SEQ_DUTYCYCLE, "sequences");
static_assert(ftapi::SEQUENCE_ABSOLUTE == ARP_SETTING_ABSOLUTE && ftapi::SEQUENCE_FIXED == ARP_SETTING_FIXED, "arpeggio settings");

namespace {

const TCHAR SECTION[] = _T("Claude Code");
const int DEFAULT_PORT = 46600;
const int ACT_MCP_TEMPO = 0x1100;		// action ID, away from the others
const int ACT_MCP_METER = 0x1101;
const UINT WM_MCP_REQUEST = WM_APP + 0x3C1;
const UINT_PTR PLAY_TIMER = 1;
const TCHAR WINDOW_CLASS[] = _T("FamiTrackerClaudeCode");
const TCHAR WSL_HELPER[] = _T("famitracker-mcp.exe");	// for Claude Code in WSL (Api/Bridge.h)
const char SECTIONS_REFUSED[] = "FamiTracker can't keep those sections: each is a run of the track's frames, apart from the "
	"others, with a name of its own of at most 32 characters (in FamiTracker's character set).";

// Text between the API (UTF-8) and FamiTracker (the ANSI code page)
std::string Utf8(LPCSTR pText)
{
	if (pText == NULL || *pText == 0)
		return std::string();
	const int Wide = MultiByteToWideChar(CP_ACP, 0, pText, -1, NULL, 0);
	if (Wide <= 0)
		return std::string();
	std::vector<wchar_t> Buffer(static_cast<size_t>(Wide));
	MultiByteToWideChar(CP_ACP, 0, pText, -1, &Buffer[0], Wide);
	const int Bytes = WideCharToMultiByte(CP_UTF8, 0, &Buffer[0], -1, NULL, 0, NULL, NULL);
	if (Bytes <= 0)
		return std::string();
	std::vector<char> Out(static_cast<size_t>(Bytes));
	WideCharToMultiByte(CP_UTF8, 0, &Buffer[0], -1, &Out[0], Bytes, NULL, NULL);
	return std::string(&Out[0]);
}

CString Ansi(const std::string &Text)
{
	if (Text.empty())
		return CString();
	const int Wide = MultiByteToWideChar(CP_UTF8, 0, Text.c_str(), -1, NULL, 0);
	if (Wide <= 0)
		return CString();
	std::vector<wchar_t> Buffer(static_cast<size_t>(Wide));
	MultiByteToWideChar(CP_UTF8, 0, Text.c_str(), -1, &Buffer[0], Wide);
	const int Bytes = WideCharToMultiByte(CP_ACP, 0, &Buffer[0], -1, NULL, 0, "?", NULL);
	if (Bytes <= 0)
		return CString();
	std::vector<char> Out(static_cast<size_t>(Bytes));
	WideCharToMultiByte(CP_ACP, 0, &Buffer[0], -1, &Out[0], Bytes, "?", NULL);
	return CString(&Out[0]);
}

// Sections' names between the API (UTF-8) and the module (the ANSI code page)
ftapi::SectionList Utf8Sections(const ftapi::SectionList &In)
{
	ftapi::SectionList Out(In);
	for (size_t i = 0; i < Out.size(); ++i)
		Out[i].Name = Utf8(In[i].Name.c_str());
	return Out;
}

ftapi::SectionList AnsiSections(const ftapi::SectionList &In)
{
	ftapi::SectionList Out(In);
	for (size_t i = 0; i < Out.size(); ++i)
		Out[i].Name = static_cast<LPCSTR>(Ansi(In[i].Name));
	return Out;
}

ftapi::Cell ToCell(const stChanNote &Note)
{
	ftapi::Cell Cell;
	Cell.Note = Note.Note;
	Cell.Octave = Note.Octave;
	Cell.Instrument = Note.Instrument;
	Cell.Volume = Note.Vol;
	for (int k = 0; k < MAX_EFFECT_COLUMNS; ++k) {
		Cell.Effect[k] = Note.EffNumber[k];
		Cell.Param[k] = Note.EffParam[k];
	}
	return Cell;
}

stChanNote ToNote(const ftapi::Cell &Cell)
{
	stChanNote Note;
	memset(&Note, 0, sizeof(Note));
	Note.Note = Cell.Note;
	Note.Octave = Cell.Octave;
	Note.Instrument = Cell.Instrument;
	Note.Vol = Cell.Volume;
	for (int k = 0; k < MAX_EFFECT_COLUMNS; ++k) {
		Note.EffNumber[k] = Cell.Effect[k];
		Note.EffParam[k] = Cell.Param[k];
	}
	return Note;
}

std::string ChipName(int Chip)
{
	switch (Chip) {
	case SNDCHIP_NONE: return "2A03";
	case SNDCHIP_VRC6: return "VRC6";
	case SNDCHIP_VRC7: return "VRC7";
	case SNDCHIP_FDS: return "FDS";
	case SNDCHIP_MMC5: return "MMC5";
	case SNDCHIP_N163: return "N163";
	case SNDCHIP_S5B: return "5B";
	}
	return "?";
}

// An FDS instrument's modulation table, as the tools read one
std::vector<int> FdsModulation(const CInstrumentFDS *pFds)
{
	std::vector<int> Table;
	for (int i = 0; i < CInstrumentFDS::MOD_SIZE; ++i)
		Table.push_back(pFds->GetModulation(i));
	return Table;
}

// A pitch sequence that goes up and down in its loop
bool LoopsUpAndDown(const CSequence *pSeq)
{
	if (pSeq == NULL)
		return false;
	const int Loop = static_cast<int>(pSeq->GetLoopPoint()), Count = static_cast<int>(pSeq->GetItemCount());
	bool Up = false, Down = false;
	for (int i = Loop; i >= 0 && i < Count; ++i) {
		Up = Up || pSeq->GetItem(i) > 0;
		Down = Down || pSeq->GetItem(i) < 0;
	}
	return Up && Down;
}

// An instrument with vibrato built in: a pitch sequence that goes up and
// down in its loop (2A03, VRC6, N163, FDS and 5B instruments), or an FDS
// modulator that wobbles the pitch slowly enough to hear as one
bool HasVibrato(CFamiTrackerDoc *pDoc, CInstrument *pInstrument)
{
	if (pInstrument->GetType() == INST_FDS) {
		const CInstrumentFDS *pFds = static_cast<const CInstrumentFDS *>(pInstrument);
		return LoopsUpAndDown(pFds->GetPitchSeq()) || ftapi::FdsModulatorWobbles(pFds->GetModulationSpeed(),
			pFds->GetModulationDepth(), FdsModulation(pFds));
	}
	int Chip = SNDCHIP_NONE, Enabled = 0, Index = 0;
	switch (pInstrument->GetType()) {
	case INST_2A03: {
		const CInstrument2A03 *p = static_cast<const CInstrument2A03 *>(pInstrument);
		Enabled = p->GetSeqEnable(SEQ_PITCH);
		Index = p->GetSeqIndex(SEQ_PITCH);
		break;
	}
	case INST_VRC6: {
		const CInstrumentVRC6 *p = static_cast<const CInstrumentVRC6 *>(pInstrument);
		Chip = SNDCHIP_VRC6;
		Enabled = p->GetSeqEnable(SEQ_PITCH);
		Index = p->GetSeqIndex(SEQ_PITCH);
		break;
	}
	case INST_N163: {
		const CInstrumentN163 *p = static_cast<const CInstrumentN163 *>(pInstrument);
		Chip = SNDCHIP_N163;
		Enabled = p->GetSeqEnable(SEQ_PITCH);
		Index = p->GetSeqIndex(SEQ_PITCH);
		break;
	}
	case INST_S5B: {
		const CInstrumentS5B *p = static_cast<const CInstrumentS5B *>(pInstrument);
		Chip = SNDCHIP_S5B;
		Enabled = p->GetSeqEnable(SEQ_PITCH);
		Index = p->GetSeqIndex(SEQ_PITCH);
		break;
	}
	default:
		return false;
	}
	if (Enabled == 0)
		return false;
	return LoopsUpAndDown(pDoc->GetSequence(Chip, static_cast<unsigned int>(Index), SEQ_PITCH));
}

// An FDS instrument's own sequence of a type (volume, arpeggio or pitch;
// CInstrumentFDS keeps them itself), NULL for the others
const CSequence *FdsSequence(const CInstrument *pInstrument, int Type)
{
	const CInstrumentFDS *pFds = static_cast<const CInstrumentFDS *>(pInstrument);
	switch (Type) {
	case SEQ_VOLUME: return pFds->GetVolumeSeq();
	case SEQ_ARPEGGIO: return pFds->GetArpSeq();
	case SEQ_PITCH: return pFds->GetPitchSeq();
	}
	return NULL;
}

// One of an instrument's sequences (2A03, VRC6, N163, FDS and 5B instruments):
// its values, where it loops back to, where it waits for a release, its
// setting; false when it has none
bool ReadSequence(CFamiTrackerDoc *pDoc, CInstrument *pInstrument, int Type, std::vector<int> &Items, int &Loop, int &Release,
	int &Setting)
{
	int Chip = SNDCHIP_NONE, Enabled = 0, Index = 0;
	const CSequence *pSeq = NULL;
	switch (pInstrument->GetType()) {
	case INST_2A03: {
		const CInstrument2A03 *p = static_cast<const CInstrument2A03 *>(pInstrument);
		Enabled = p->GetSeqEnable(Type);
		Index = p->GetSeqIndex(Type);
		break;
	}
	case INST_VRC6: {
		const CInstrumentVRC6 *p = static_cast<const CInstrumentVRC6 *>(pInstrument);
		Chip = SNDCHIP_VRC6;
		Enabled = p->GetSeqEnable(Type);
		Index = p->GetSeqIndex(Type);
		break;
	}
	case INST_N163: {
		const CInstrumentN163 *p = static_cast<const CInstrumentN163 *>(pInstrument);
		Chip = SNDCHIP_N163;
		Enabled = p->GetSeqEnable(Type);
		Index = p->GetSeqIndex(Type);
		break;
	}
	case INST_S5B: {
		const CInstrumentS5B *p = static_cast<const CInstrumentS5B *>(pInstrument);
		Chip = SNDCHIP_S5B;
		Enabled = p->GetSeqEnable(Type);
		Index = p->GetSeqIndex(Type);
		break;
	}
	case INST_FDS:
		pSeq = FdsSequence(pInstrument, Type);
		Enabled = pSeq != NULL ? 1 : 0;
		break;
	default:
		return false;
	}
	if (Enabled == 0)
		return false;
	if (pSeq == NULL)
		pSeq = pDoc->GetSequence(Chip, static_cast<unsigned int>(Index), Type);
	if (pSeq == NULL || pSeq->GetItemCount() == 0)
		return false;
	Items.clear();
	for (unsigned int i = 0; i < pSeq->GetItemCount(); ++i)
		Items.push_back(pSeq->GetItem(static_cast<int>(i)));
	Loop = static_cast<int>(pSeq->GetLoopPoint());
	Release = static_cast<int>(pSeq->GetReleasePoint());
	Setting = static_cast<int>(pSeq->GetSetting());
	return true;
}

// An instrument's volume, arpeggio, pitch and hi-pitch sequences; an FDS
// instrument's volume (0-31) counted as the others' (0-15)
void ReadSequences(CFamiTrackerDoc *pDoc, CInstrument *pInstrument, ftapi::InstrumentInfo &Info)
{
	int Setting = 0;
	ReadSequence(pDoc, pInstrument, SEQ_VOLUME, Info.Volume, Info.VolumeLoop, Info.VolumeRelease, Setting);
	if (pInstrument->GetType() == INST_FDS)
		for (size_t i = 0; i < Info.Volume.size(); ++i)
			Info.Volume[i] = ftapi::FdsVolumeAs15(Info.Volume[i]);
	ftapi::SequenceInfo *const INTO[] = { &Info.Steps, &Info.Pitch, &Info.HiPitch };
	const int TYPES[] = { SEQ_ARPEGGIO, SEQ_PITCH, SEQ_HIPITCH };
	for (int i = 0; i < 3; ++i)
		ReadSequence(pDoc, pInstrument, TYPES[i], INTO[i]->Items, INTO[i]->Loop, INTO[i]->Release, INTO[i]->Setting);
	if (pInstrument->GetType() == INST_S5B)	// the 5B's noise / mode sequence, in the duty sequence's place
		ReadSequence(pDoc, pInstrument, SEQ_DUTYCYCLE, Info.Mode.Items, Info.Mode.Loop, Info.Mode.Release, Info.Mode.Setting);
}

// A VRC6 instrument whose duty sequence plays odd values: on the sawtooth the
// duty's low bit adds 32 to the volume (CVRC6Sawtooth::RefreshChannel), so a
// pulse sound played there overdrives
bool HasOddDuty(CFamiTrackerDoc *pDoc, CInstrument *pInstrument)
{
	if (pInstrument->GetType() != INST_VRC6)
		return false;
	const CInstrumentVRC6 *p = static_cast<const CInstrumentVRC6 *>(pInstrument);
	if (p->GetSeqEnable(SEQ_DUTYCYCLE) == 0)
		return false;
	const CSequence *pSeq = pDoc->GetSequence(SNDCHIP_VRC6, static_cast<unsigned int>(p->GetSeqIndex(SEQ_DUTYCYCLE)), SEQ_DUTYCYCLE);
	if (pSeq == NULL)
		return false;
	for (unsigned int i = 0; i < pSeq->GetItemCount(); ++i)
		if (pSeq->GetItem(static_cast<int>(i)) & 1)
			return true;
	return false;
}

// The notes an instrument's looping arpeggio sequence plays besides the note
// itself, in semitones ({4, 7, 11}): the chord it plays from each note; empty
// when it has none, or one that doesn't loop (an attack's octave blip)
std::vector<int> LoopingArpeggio(CFamiTrackerDoc *pDoc, CInstrument *pInstrument)
{
	std::vector<int> Out;
	int Chip = SNDCHIP_NONE, Enabled = 0, Index = 0;
	const CSequence *pSeq = NULL;
	switch (pInstrument->GetType()) {
	case INST_2A03: {
		const CInstrument2A03 *p = static_cast<const CInstrument2A03 *>(pInstrument);
		Enabled = p->GetSeqEnable(SEQ_ARPEGGIO);
		Index = p->GetSeqIndex(SEQ_ARPEGGIO);
		break;
	}
	case INST_VRC6: {
		const CInstrumentVRC6 *p = static_cast<const CInstrumentVRC6 *>(pInstrument);
		Chip = SNDCHIP_VRC6;
		Enabled = p->GetSeqEnable(SEQ_ARPEGGIO);
		Index = p->GetSeqIndex(SEQ_ARPEGGIO);
		break;
	}
	case INST_N163: {
		const CInstrumentN163 *p = static_cast<const CInstrumentN163 *>(pInstrument);
		Chip = SNDCHIP_N163;
		Enabled = p->GetSeqEnable(SEQ_ARPEGGIO);
		Index = p->GetSeqIndex(SEQ_ARPEGGIO);
		break;
	}
	case INST_S5B: {
		const CInstrumentS5B *p = static_cast<const CInstrumentS5B *>(pInstrument);
		Chip = SNDCHIP_S5B;
		Enabled = p->GetSeqEnable(SEQ_ARPEGGIO);
		Index = p->GetSeqIndex(SEQ_ARPEGGIO);
		break;
	}
	case INST_FDS:
		pSeq = FdsSequence(pInstrument, SEQ_ARPEGGIO);
		Enabled = pSeq != NULL && pSeq->GetItemCount() > 0 ? 1 : 0;
		break;
	default:
		return Out;
	}
	if (Enabled == 0)
		return Out;
	if (pSeq == NULL)
		pSeq = pDoc->GetSequence(Chip, static_cast<unsigned int>(Index), SEQ_ARPEGGIO);
	if (pSeq == NULL || pSeq->GetSetting() != ARP_SETTING_ABSOLUTE)
		return Out;
	const int Loop = static_cast<int>(pSeq->GetLoopPoint()), Count = static_cast<int>(pSeq->GetItemCount());
	if (Loop < 0 || Loop >= Count)
		return Out;
	bool Root = false;
	for (int i = Loop; i < Count; ++i) {
		const int Value = pSeq->GetItem(i);
		if (Value == 0)
			Root = true;
		else if (std::find(Out.begin(), Out.end(), Value) == Out.end())
			Out.push_back(Value);
	}
	if (Out.empty() || (Out.size() == 1 && !Root))
		Out.clear();	// one note over and over
	std::sort(Out.begin(), Out.end());
	return Out;
}

std::string InstrumentChip(inst_type_t Type)
{
	switch (Type) {
	case INST_2A03: return "2A03";
	case INST_VRC6: return "VRC6";
	case INST_VRC7: return "VRC7";
	case INST_FDS: return "FDS";
	case INST_N163: return "N163";
	case INST_S5B: return "5B";
	default: return "?";
	}
}

int KindOf(int ChanID)
{
	switch (ChanID) {
	case CHANID_SQUARE1: case CHANID_SQUARE2: case CHANID_VRC6_PULSE1: case CHANID_VRC6_PULSE2:
	case CHANID_MMC5_SQUARE1: case CHANID_MMC5_SQUARE2:
		return ftapi::KIND_PULSE;
	case CHANID_TRIANGLE: return ftapi::KIND_TRIANGLE;
	case CHANID_NOISE: return ftapi::KIND_NOISE;
	case CHANID_DPCM: return ftapi::KIND_DPCM;
	case CHANID_VRC6_SAWTOOTH: return ftapi::KIND_SAWTOOTH;
	case CHANID_VRC7_CH1: case CHANID_VRC7_CH2: case CHANID_VRC7_CH3: case CHANID_VRC7_CH4: case CHANID_VRC7_CH5:
	case CHANID_VRC7_CH6:
		return ftapi::KIND_FM;
	case CHANID_S5B_CH1: case CHANID_S5B_CH2: case CHANID_S5B_CH3:
		return ftapi::KIND_SQUARE;
	}
	return ftapi::KIND_OTHER;
}

// An expansion chip by the API's name ("" for none); false if it isn't one FamiTracker has
bool ChipFlag(const std::string &Chip, unsigned char &Flag)
{
	const struct { const char *pName; unsigned char Flag; } CHIPS[] = {
		{ "", SNDCHIP_NONE }, { "VRC6", SNDCHIP_VRC6 }, { "VRC7", SNDCHIP_VRC7 }, { "FDS", SNDCHIP_FDS },
		{ "MMC5", SNDCHIP_MMC5 }, { "N163", SNDCHIP_N163 }, { "5B", SNDCHIP_S5B },
	};
	for (size_t i = 0; i < sizeof(CHIPS) / sizeof(CHIPS[0]); ++i) {
		if (Chip == CHIPS[i].pName) {
			Flag = CHIPS[i].Flag;
			return true;
		}
	}
	return false;
}

// The expansion chips by the API's names, together (SNDCHIP_* bits); false if one isn't a chip FamiTracker has
bool ChipFlags(const std::vector<std::string> &Chips, unsigned char &Flags)
{
	Flags = SNDCHIP_NONE;
	for (size_t i = 0; i < Chips.size(); ++i) {
		unsigned char Flag = SNDCHIP_NONE;
		if (!ChipFlag(Chips[i], Flag))
			return false;
		Flags |= Flag;
	}
	return true;
}

// 32 hex digits from the system's random numbers
CString NewToken()
{
	CString Token;
	for (int i = 0; i < 4; ++i) {
		unsigned int Value = 0;
		if (rand_s(&Value) != 0)
			Value = static_cast<unsigned int>(GetTickCount()) * 2654435761u + static_cast<unsigned int>(i);
		CString Part;
		Part.Format(_T("%08x"), Value);
		Token += Part;
	}
	return Token;
}

// The tempo, undoably (module settings aren't undoable in FamiTracker 0.4.6)
class CMcpTempoAction : public CAction
{
public:
	CMcpTempoAction(int Speed, int Tempo) :
		CAction(ACT_MCP_TEMPO), m_iTrack(0), m_iSpeed(Speed), m_iTempo(Tempo), m_iOldSpeed(0), m_iOldTempo(0)
	{
	}

	bool SaveState(CMainFrame *pMainFrm)
	{
		CFamiTrackerDoc *pDoc = static_cast<CFamiTrackerDoc*>(pMainFrm->GetActiveDocument());
		m_iTrack = static_cast<unsigned int>(pMainFrm->GetSelectedTrack());
		m_iOldSpeed = static_cast<int>(pDoc->GetSongSpeed(m_iTrack));
		m_iOldTempo = static_cast<int>(pDoc->GetSongTempo(m_iTrack));
		Redo(pMainFrm);
		return true;
	}

	void Undo(CMainFrame *pMainFrm) { Set(pMainFrm, m_iOldSpeed, m_iOldTempo); }
	void Redo(CMainFrame *pMainFrm) { Set(pMainFrm, m_iSpeed, m_iTempo); }

private:
	void Set(CMainFrame *pMainFrm, int Speed, int Tempo)
	{
		CFamiTrackerDoc *pDoc = static_cast<CFamiTrackerDoc*>(pMainFrm->GetActiveDocument());
		if (m_iTrack >= pDoc->GetTrackCount())
			return;
		pDoc->SetSongSpeed(m_iTrack, static_cast<unsigned int>(Speed));
		pDoc->SetSongTempo(m_iTrack, static_cast<unsigned int>(Tempo));
		theApp.GetSoundGenerator()->ResetTempo();
		pDoc->SetModifiedFlag();
		pMainFrm->UpdateControls();
	}

	unsigned int m_iTrack;
	int m_iSpeed, m_iTempo, m_iOldSpeed, m_iOldTempo;
};

// The meter: the row highlights (the module's) and a track's pattern length
class CMcpMeterAction : public CAction
{
public:
	CMcpMeterAction(int First, int Second, int Length) :
		CAction(ACT_MCP_METER), m_iTrack(0), m_iFirst(First), m_iSecond(Second), m_iLength(Length), m_iOldFirst(0), m_iOldSecond(0),
		m_iOldLength(0)
	{
	}

	bool SaveState(CMainFrame *pMainFrm)
	{
		CFamiTrackerDoc *pDoc = static_cast<CFamiTrackerDoc*>(pMainFrm->GetActiveDocument());
		m_iTrack = static_cast<unsigned int>(pMainFrm->GetSelectedTrack());
		m_iOldFirst = pDoc->GetFirstHighlight();
		m_iOldSecond = pDoc->GetSecondHighlight();
		m_iOldLength = static_cast<int>(pDoc->GetPatternLength(m_iTrack));
		if (m_iLength <= 0)
			m_iLength = m_iOldLength;
		Redo(pMainFrm);
		return true;
	}

	void Undo(CMainFrame *pMainFrm) { Set(pMainFrm, m_iOldFirst, m_iOldSecond, m_iOldLength); }
	void Redo(CMainFrame *pMainFrm) { Set(pMainFrm, m_iFirst, m_iSecond, m_iLength); }

private:
	void Set(CMainFrame *pMainFrm, int First, int Second, int Length)
	{
		CFamiTrackerDoc *pDoc = static_cast<CFamiTrackerDoc*>(pMainFrm->GetActiveDocument());
		if (m_iTrack >= pDoc->GetTrackCount())
			return;
		if (Length > 0 && Length <= MAX_PATTERN_LENGTH)
			pDoc->SetPatternLength(m_iTrack, static_cast<unsigned int>(Length));
		pDoc->SetHighlight(First, Second);
		pMainFrm->SetFirstHighlightRow(First);	// the toolbar's boxes, which would set them back
		pMainFrm->SetSecondHighlightRow(Second);
		pDoc->SetModifiedFlag();
		pDoc->UpdateAllViews(NULL, UPDATE_HIGHLIGHT);
		pDoc->UpdateAllViews(NULL, UPDATE_PATTERN);
		pMainFrm->UpdateControls();
	}

	unsigned int m_iTrack;
	int m_iFirst, m_iSecond, m_iLength, m_iOldFirst, m_iOldSecond, m_iOldLength;
};

}	// namespace

struct mcp_settings_t {
	bool Enabled;
	int Port;
	CString Token;
	bool DirectChanges;
	bool Wsl;			// the dialog shows the setup for Claude Code in WSL
	bool Network;		// other computers on the user's network may connect too
	bool CoreTools;		// the dialog shows other computers the addresses of the small set of tools (/mcp/core)
};

// The connection: settings, listener, and the song for the tools (and for
// Edit > Sound effect from your voice, which uses one)
class CMcpLink : public ftapi::SongHost, public CVoiceEffectMaker
{
public:
	CMcpLink() : m_pMain(NULL), m_hWindow(NULL), m_Tools(*this), m_iAddressesTime(0), m_bAddressesRead(false), m_bBusy(false),
		m_bStopping(false), m_bOwnDialog(false), m_bByHand(false), m_pProgress(NULL), m_iPlayFirst(0),
		m_iPlayEnd(0), m_bPlayStarted(false), m_iPlayTicks(0), m_bPlayLoop(false), m_iPlayPart(0), m_iReturnTrack(-1)
	{
		m_Settings.Enabled = false;
		m_Settings.Port = DEFAULT_PORT;
		m_Settings.DirectChanges = false;
		m_Settings.Wsl = false;
		m_Settings.Network = false;
		m_Settings.CoreTools = false;
	}

	~CMcpLink()
	{
		Shutdown();
	}

	void Start(CMainFrame *pMainFrame)
	{
		m_pMain = pMainFrame;
		LoadSettings();
		CreateWindowOnce();
		if (m_Settings.Enabled) {
			CString Error;
			if (!Listen(Error))
				m_pMain->SetMessageText(_T("Claude Code connection: ") + Error);
		}
	}

	void Shutdown()
	{
		m_bStopping = true;
		m_Server.Stop();
		if (m_hWindow != NULL) {
			KillTimer(m_hWindow, PLAY_TIMER);
			DestroyWindow(m_hWindow);
			m_hWindow = NULL;
		}
	}

	const mcp_settings_t &GetSettings() const { return m_Settings; }

	CString Status() const
	{
		if (m_Server.IsRunning()) {
			CString Text;
			Text.Format(m_Server.IsNetwork() ? _T("On: Claude Code can connect at 127.0.0.1:%d, and other computers on your network too.") :
				_T("On: Claude Code can connect at 127.0.0.1:%d."), m_Server.GetPort());
			return Text;
		}
		if (!m_sLastError.IsEmpty())
			return m_sLastError;
		return _T("Off.");
	}

	CString Command(int Port, LPCTSTR Token) const
	{
		ftapi::McpConfig Config;
		Config.Port = Port;
		Config.Token = std::string(static_cast<LPCSTR>(CStringA(Token)));
		return CString(ftapi::ClaudeAddCommand(Config).c_str());
	}

	// Claude Code in WSL starts famitracker-mcp.exe from FamiTracker's
	// folder, which reads the port and the access code from the settings
	CString WslCommand() const
	{
		return CString(ftapi::WslAddCommand(std::string(static_cast<LPCSTR>(CStringA(HelperPath())))).c_str());
	}

	static CString HelperPath()
	{
		TCHAR Path[MAX_PATH];
		const DWORD Length = GetModuleFileName(NULL, Path, MAX_PATH);
		if (Length == 0 || Length >= MAX_PATH)
			return WSL_HELPER;
		const CString Program(Path);
		return Program.Left(Program.ReverseFind(_T('\\')) + 1) + WSL_HELPER;
	}

	static bool HelperFound()
	{
		const DWORD Attributes = GetFileAttributes(HelperPath());
		return Attributes != INVALID_FILE_ATTRIBUTES && (Attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
	}

	// Only the dialog's choice: nothing to start or stop
	void RememberWsl(bool Wsl)
	{
		m_Settings.Wsl = Wsl;
		theApp.WriteProfileInt(SECTION, _T("Claude Code in WSL"), Wsl ? 1 : 0);
	}

	// From the dialog: saves the settings and starts or stops listening
	bool Apply(const mcp_settings_t &Settings, CString &Error)
	{
		const bool Restart = Settings.Port != m_Settings.Port || Settings.Token != m_Settings.Token || !m_Server.IsRunning() ||
			Settings.Network != m_Server.IsNetwork();
		m_Settings = Settings;
		SaveSettings();
		if (!m_Settings.Enabled) {
			m_Server.Stop();
			m_sLastError.Empty();
			return true;
		}
		if (Restart)
			return Listen(Error);
		return true;
	}

	LRESULT Answer(mcp_exchange_t *pExchange)
	{
		if (m_bStopping || m_bBusy || pExchange == NULL || m_pMain == NULL)
			return 0;	// the client hears that FamiTracker is busy
		m_bBusy = true;
		try {
			ftapi::McpConfig Config;
			Config.Port = m_Server.GetPort();
			Config.Token = std::string(static_cast<LPCSTR>(CStringA(m_Settings.Token)));
			Config.Network = m_Server.IsNetwork();
			if (Config.Network) {
				Config.Addresses = OwnAddresses();
				Config.ComputerName = NetworkComputerName();
			}
			char Version[32];
			_snprintf_s(Version, sizeof(Version), _TRUNCATE, "%d.%d.%d-beginner-friendly", VERSION_MAJ, VERSION_MIN, VERSION_REV);
			Config.Version = Version;
			*pExchange->pResponse = ftapi::HandleMcp(*pExchange->pRequest, Config, m_Tools);
		}
		catch (...) {
			ftapi::HttpResponse Failed;
			Failed.Status = 500;
			Failed.ContentType = "text/plain; charset=utf-8";
			Failed.Body = "FamiTracker couldn't answer that request.\n";
			*pExchange->pResponse = Failed;
		}
		m_bBusy = false;
		return 1;
	}

	// Stops playback after the asked bars, or plays them again (a loop), and
	// gives the user's mutes back once it's over
	void CheckPlayback()
	{
		CSoundGen *pSound = theApp.GetSoundGenerator();
		CFamiTrackerDoc *pDoc = Doc();
		if (pSound == NULL || pDoc == NULL) {
			EndPlayback();
			return;
		}
		if (!theApp.IsPlaying()) {
			if (!m_bPlayStarted && ++m_iPlayTicks <= 10)
				return;	// the sound thread hasn't started it yet
			EndPlayback();
			return;
		}
		if (m_iPlayEnd <= m_iPlayFirst)
			return;	// to the end: waiting for it to stop
		const int Length = static_cast<int>(pDoc->GetPatternLength(static_cast<unsigned int>(pSound->GetPlayerTrack())));
		const int Row = pSound->GetPlayerFrame() * Length + pSound->GetPlayerRow();
		if (!m_bPlayStarted) {
			if (Row >= m_iPlayFirst && Row < m_iPlayEnd)
				m_bPlayStarted = true;
			else if (++m_iPlayTicks > 40) {	// it never got there: leave it be
				m_iPlayEnd = m_iPlayFirst;
				return;
			}
		}
		if (m_bPlayStarted && (Row >= m_iPlayEnd || Row < m_iPlayFirst)) {
			if (m_iPlayPart + 1 < m_PlayParts.size())
				BeginPart(m_iPlayPart + 1);	// the next version
			else if (m_bPlayLoop)
				BeginPart(0);
			else {
				theApp.StopPlayer();
				EndPlayback();
			}
		}
		else if (m_bPlayStarted && m_bPlayLoop && m_PlayParts.size() == 1 && m_iPlayFirst % Length == 0 && m_iPlayEnd % Length == 0 &&
			Row / Length == (m_iPlayEnd - 1) / Length && pSound->GetQueueFrame() == -1)
			pSound->SetQueueFrame(m_iPlayFirst / Length);	// whole frames: back to the first without a gap
	}

	// Plays a part of what Claude asked for: its track, from its first row
	void BeginPart(size_t Part)
	{
		if (Part >= m_PlayParts.size())
			return;
		m_iPlayPart = Part;
		const ftapi::PlayPart &p = m_PlayParts[Part];
		if (p.Track != static_cast<int>(Track())) {
			if (theApp.IsPlaying())
				theApp.StopPlayerAndWait();
			m_pMain->SelectTrack(static_cast<unsigned int>(p.Track));
		}
		m_iPlayFirst = p.First;
		m_iPlayEnd = p.Rows > 0 ? p.First + p.Rows : p.First;
		StartAt(p.First);
	}

	// Plays from a song row (stopping what plays)
	void StartAt(int First)
	{
		CFamiTrackerDoc *pDoc = Doc();
		CFamiTrackerView *pView = CFamiTrackerView::GetView();
		if (pDoc == NULL || pView == NULL)
			return;
		const int Length = static_cast<int>(pDoc->GetPatternLength(Track()));
		if (theApp.IsPlaying())
			theApp.StopPlayerAndWait();	// the cursor doesn't move while playing in follow mode
		CSoundGen *pSound = theApp.GetSoundGenerator();
		if (pSound != NULL)
			pSound->SetQueueFrame(-1);
		pView->SelectFrame(static_cast<unsigned int>(First / Length));
		pView->GetPatternEditor()->MoveToRow(First % Length);
		pView->GetPatternEditor()->InvalidateCursor();
		theApp.StartPlayer(MODE_PLAY_CURSOR);
		m_bPlayStarted = false;
		m_iPlayTicks = 0;
	}

	// Playback for Claude is over: no loop, and the user's own mutes back
	void EndPlayback()
	{
		if (m_hWindow != NULL)
			KillTimer(m_hWindow, PLAY_TIMER);
		CSoundGen *pSound = theApp.GetSoundGenerator();
		if (pSound != NULL && m_bPlayLoop)
			pSound->SetQueueFrame(-1);
		m_bPlayLoop = false;
		m_PlayParts.clear();
		CFamiTrackerDoc *pDoc = Doc();
		if (m_iReturnTrack >= 0 && pDoc != NULL && m_iReturnTrack < static_cast<int>(pDoc->GetTrackCount()) &&
			m_iReturnTrack != static_cast<int>(Track())) {
			if (theApp.IsPlaying())
				theApp.StopPlayerAndWait();
			m_pMain->SelectTrack(static_cast<unsigned int>(m_iReturnTrack));	// the track Claude works on
		}
		m_iReturnTrack = -1;
		CFamiTrackerView *pView = CFamiTrackerView::GetView();
		if (!m_UserMutes.empty() && pDoc != NULL && pView != NULL) {
			for (unsigned int c = 0; c < m_UserMutes.size() && c < pDoc->GetAvailableChannels(); ++c)
				pView->SetChannelMute(static_cast<int>(c), m_UserMutes[c]);
			pView->GetPatternEditor()->InvalidateHeader();
			pView->RedrawWindow(pView->GetPatternEditor()->GetHeaderRect(), NULL, RDW_INVALIDATE);
		}
		m_UserMutes.clear();
	}

	// ftapi::SongHost

	bool GetSong(ftapi::SongInfo &Info, std::string &Error)
	{
		CFamiTrackerDoc *pDoc = Doc();
		if (pDoc == NULL || !pDoc->IsFileLoaded()) {
			Error = "No song is open in FamiTracker.";
			return false;
		}
		const unsigned int Track = this->Track();
		Info = ftapi::SongInfo();
		Info.Title = Utf8(pDoc->GetSongName());
		Info.Author = Utf8(pDoc->GetSongArtist());
		Info.Copyright = Utf8(pDoc->GetSongCopyright());
		const CString Path = pDoc->GetPathName();
		Info.FileName = Utf8(Path.Mid(Path.ReverseFind(_T('\\')) + 1));
		Info.Modified = pDoc->IsModified() != FALSE;
		Info.Track = static_cast<int>(Track);
		Info.Tracks = static_cast<int>(pDoc->GetTrackCount());
		Info.TrackName = Utf8(pDoc->GetTrackTitle(Track));
		for (unsigned int t = 0; t < pDoc->GetTrackCount(); ++t)
			Info.TrackNames.push_back(Utf8(pDoc->GetTrackTitle(t)));
		Info.FrameRate = static_cast<int>(pDoc->GetFrameRate());
		Info.Pal = pDoc->GetMachine() == PAL;
		Info.Speed = static_cast<int>(pDoc->GetSongSpeed(Track));
		Info.Tempo = static_cast<int>(pDoc->GetSongTempo(Track));
		Info.SplitPoint = pDoc->GetSpeedSplitPoint();
		Info.PatternLength = static_cast<int>(pDoc->GetPatternLength(Track));
		Info.Frames = static_cast<int>(pDoc->GetFrameCount(Track));
		Info.MaxFrames = MAX_FRAMES;
		Info.FirstHighlight = pDoc->GetFirstHighlight();
		Info.SecondHighlight = pDoc->GetSecondHighlight();
		const unsigned char Chips = pDoc->GetExpansionChip();
		const unsigned char ALL[] = { SNDCHIP_VRC6, SNDCHIP_VRC7, SNDCHIP_FDS, SNDCHIP_MMC5, SNDCHIP_N163, SNDCHIP_S5B };
		for (size_t i = 0; i < sizeof(ALL); ++i) {
			if (Chips & ALL[i])
				Info.Expansion += (Info.Expansion.empty() ? "" : ", ") + ChipName(ALL[i]);
		}

		const int Channels = static_cast<int>(pDoc->GetAvailableChannels());
		const CFamiTrackerView *pView = CFamiTrackerView::GetView();
		for (int c = 0; c < Channels; ++c) {
			ftapi::ChannelInfo Channel;
			Channel.Name = Utf8(pDoc->GetChannel(c)->GetChannelName());
			Channel.Chip = ChipName(pDoc->GetChipType(c));
			Channel.ChanId = pDoc->GetChannelType(c);
			Channel.Kind = KindOf(Channel.ChanId);
			Channel.EffectColumns = static_cast<int>(pDoc->GetEffColumns(Track, static_cast<unsigned int>(c))) + 1;
			Channel.Muted = pView != NULL && pView->IsChannelMuted(static_cast<unsigned int>(c));
			Channel.MixLevel = pDoc->GetChannelMix()->Level(Channel.ChanId);
			Channel.MixPan = pDoc->GetChannelMix()->Pan(Channel.ChanId);
			Channel.MixSoft = pDoc->GetChannelMix()->Soft(Channel.ChanId);
			Channel.MixSpace = pDoc->GetChannelMix()->Space(Channel.ChanId);
			Info.Channels.push_back(Channel);
		}
		Info.SpaceRoom = pDoc->GetChannelMix()->Room();
		Info.SpaceEcho = pDoc->GetChannelMix()->Echo();
		Info.SpaceRepeats = pDoc->GetChannelMix()->Repeats();
		for (int i = 0; i < MAX_INSTRUMENTS; ++i) {
			if (!pDoc->IsInstrumentUsed(static_cast<unsigned int>(i)))
				continue;
			ftapi::InstrumentInfo Instrument;
			Instrument.Index = i;
			char Name[CInstrument::INST_NAME_MAX];
			pDoc->GetInstrumentName(static_cast<unsigned int>(i), Name);
			Name[CInstrument::INST_NAME_MAX - 1] = 0;
			Instrument.Name = Utf8(Name);
			Instrument.Chip = InstrumentChip(pDoc->GetInstrumentType(static_cast<unsigned int>(i)));
			CInstrument *pInstrument = pDoc->GetInstrument(static_cast<unsigned int>(i));	// retained
			if (pInstrument != NULL) {
				Instrument.Releases = pInstrument->CanRelease();
				Instrument.Vibrato = HasVibrato(pDoc, pInstrument);
				Instrument.Arpeggio = LoopingArpeggio(pDoc, pInstrument);
				Instrument.OddDuty = HasOddDuty(pDoc, pInstrument);
				ReadSequences(pDoc, pInstrument, Instrument);
				if (pInstrument->GetType() == INST_N163) {
					Instrument.WavePos = static_cast<CInstrumentN163*>(pInstrument)->GetWavePos();
					Instrument.WaveSize = static_cast<CInstrumentN163*>(pInstrument)->GetWaveSize();
				}
				pInstrument->Release();
			}
			Info.Instruments.push_back(Instrument);
		}
		const CClipLibrary *pClips = pDoc->GetClips();
		for (int i = 0; pClips != NULL && i < pClips->GetCount(); ++i) {
			ftapi::ClipInfo Clip;
			Clip.Index = i;
			Clip.Id = pClips->GetId(i);
			Clip.Name = Utf8(pClips->GetName(i));
			Clip.Columns = pClips->GetColumns(i);
			Clip.Rows = pClips->GetRows(i);
			Clip.Links = pClips->GetLinkCount(i);
			for (int c = 0; c < Clip.Columns; ++c)
				Clip.Sources.push_back(pClips->GetSource(i, c));
			Info.Clips.push_back(Clip);
		}
		Info.Sections = Utf8Sections(pDoc->GetSections()->Get(static_cast<int>(Track)));
		Info.KeyChosen = pDoc->GetKeys()->Get(static_cast<int>(Track), Info.ChosenKey);
		Info.DirectChanges = m_Settings.DirectChanges || m_bByHand;
		Info.Playing = theApp.IsPlaying();
		CSoundGen *pSound = theApp.GetSoundGenerator();
		if (Info.Playing && pSound != NULL && static_cast<unsigned int>(pSound->GetPlayerTrack()) == Track)
			Info.PlayRow = pSound->GetPlayerFrame() * Info.PatternLength + pSound->GetPlayerRow();
		return true;
	}

	void ReadCells(int Channel, int First, int Rows, std::vector<ftapi::Cell> &Out)
	{
		Out.clear();
		CFamiTrackerDoc *pDoc = Doc();
		if (pDoc == NULL || Rows <= 0)
			return;
		const unsigned int Track = this->Track();
		const int Length = static_cast<int>(pDoc->GetPatternLength(Track));
		const int Total = Length * static_cast<int>(pDoc->GetFrameCount(Track));
		const bool Valid = Channel >= 0 && Channel < static_cast<int>(pDoc->GetAvailableChannels());
		Out.reserve(static_cast<size_t>(Rows));
		for (int r = First; r < First + Rows; ++r) {
			if (!Valid || r < 0 || r >= Total) {
				Out.push_back(ftapi::Cell());
				continue;
			}
			stChanNote Note;
			pDoc->GetNoteData(Track, static_cast<unsigned int>(r / Length), static_cast<unsigned int>(Channel),
				static_cast<unsigned int>(r % Length), &Note);
			Out.push_back(ToCell(Note));
		}
	}

	bool ReadClip(int Index, std::vector<std::vector<ftapi::Cell> > &Columns)
	{
		CFamiTrackerDoc *pDoc = Doc();
		const CClipLibrary *pClips = pDoc != NULL ? pDoc->GetClips() : NULL;
		if (pClips == NULL || Index < 0 || Index >= pClips->GetCount())
			return false;
		Columns.assign(static_cast<size_t>(pClips->GetColumns(Index)), std::vector<ftapi::Cell>());
		for (int c = 0; c < pClips->GetColumns(Index); ++c) {
			for (int r = 0; r < pClips->GetRows(Index); ++r) {
				stChanNote Note;
				pClips->GetCell(Index, c, r, Note);
				Columns[static_cast<size_t>(c)].push_back(ToCell(Note));
			}
		}
		return true;
	}

	bool SelectTrack(int Track, std::string &Error)
	{
		CFamiTrackerDoc *pDoc = Doc();
		if (!CanChange(Error))
			return false;
		if (Track < 0 || Track >= static_cast<int>(pDoc->GetTrackCount())) {
			Error = "There's no such track.";
			return false;
		}
		m_pMain->SelectTrack(static_cast<unsigned int>(Track));
		return true;
	}

	// The new track's patterns: one per different content in each channel,
	// so frames that play the same share it
	int AddTrack(const std::string &Name, const std::vector<std::vector<ftapi::Cell> > &Channels, int Speed, int Tempo, std::string &Error)
	{
		if (!CanChange(Error))
			return -1;
		CFamiTrackerDoc *pDoc = Doc();
		if (pDoc->GetTrackCount() >= MAX_TRACKS) {
			Error = "The module already has 64 tracks, FamiTracker's most.";
			return -1;
		}
		const unsigned int Source = Track();
		const unsigned int Length = pDoc->GetPatternLength(Source);
		size_t Rows = Length;
		for (size_t c = 0; c < Channels.size(); ++c)
			Rows = std::max(Rows, Channels[c].size());
		const unsigned int Frames = static_cast<unsigned int>(std::min<size_t>(MAX_FRAMES, (Rows + Length - 1) / Length));
		const unsigned int ChannelCount = pDoc->GetAvailableChannels();

		pDoc->LockDocument();
		const int New = pDoc->AddTrack();
		if (New >= 0) {
			const unsigned int Added = static_cast<unsigned int>(New);
			pDoc->SetTrackTitle(Added, Ansi(Name));
			pDoc->SetPatternLength(Added, Length);
			pDoc->SetFrameCount(Added, Frames);
			pDoc->SetSongSpeed(Added, static_cast<unsigned int>(Speed));
			pDoc->SetSongTempo(Added, static_cast<unsigned int>(Tempo));
			for (unsigned int c = 0; c < ChannelCount; ++c) {
				pDoc->SetEffColumns(Added, c, pDoc->GetEffColumns(Source, c));
				std::map<std::string, unsigned int> Patterns;
				std::vector<stChanNote> Notes(Length);
				for (unsigned int f = 0; f < Frames; ++f) {
					for (unsigned int r = 0; r < Length; ++r) {
						const size_t Row = static_cast<size_t>(f) * Length + r;
						Notes[r] = ToNote(c < Channels.size() && Row < Channels[c].size() ? Channels[c][Row] : ftapi::Cell());
					}
					const std::string Content(reinterpret_cast<const char*>(&Notes[0]), Notes.size() * sizeof(stChanNote));
					std::map<std::string, unsigned int>::const_iterator Found = Patterns.find(Content);
					unsigned int Pattern;
					if (Found != Patterns.end())
						Pattern = Found->second;
					else {
						Pattern = static_cast<unsigned int>(Patterns.size());
						Patterns[Content] = Pattern;
						for (unsigned int r = 0; r < Length; ++r)
							pDoc->SetDataAtPattern(Added, Pattern, c, r, &Notes[r]);
					}
					pDoc->SetPatternAtFrame(Added, f, c, Pattern);
				}
			}
		}
		pDoc->UnlockDocument();
		if (New < 0) {
			Error = "FamiTracker couldn't add a track.";
			return -1;
		}
		pDoc->SetModifiedFlag();
		pDoc->UpdateAllViews(NULL, UPDATE_TRACK);	// the track box lists it
		m_pMain->SelectTrack(static_cast<unsigned int>(New));
		return New;
	}

	// Waits a little, keeping the windows going
	static void Pump(DWORD Milliseconds)
	{
		MsgWaitForMultipleObjects(0, NULL, FALSE, Milliseconds, QS_ALLINPUT);
		MSG Message;
		while (PeekMessage(&Message, NULL, 0, 0, PM_REMOVE)) {
			if (Message.message == WM_QUIT) {
				PostQuitMessage(static_cast<int>(Message.wParam));
				return;
			}
			TranslateMessage(&Message);
			DispatchMessage(&Message);
		}
	}

	// FamiTracker's own WAV rendering (stereo) into a temporary file, read
	// back with the sample where each row began
	bool RenderTrack(int Track, double Seconds, std::vector<float> &Left, std::vector<float> &Right, int &SampleRate,
		std::vector<std::pair<int, long long> > &Marks, std::string &Error)
	{
		CFamiTrackerDoc *pDoc = Doc();
		CSoundGen *pSound = theApp.GetSoundGenerator();
		if (pDoc == NULL || pSound == NULL) {
			Error = "FamiTracker isn't ready.";
			return false;
		}
		if (Track < 0 || Track >= static_cast<int>(pDoc->GetTrackCount())) {
			Error = "There's no such track.";
			return false;
		}
		if (pSound->IsRendering()) {
			Error = "FamiTracker is rendering something else.";
			return false;
		}
		TCHAR Folder[MAX_PATH], File[MAX_PATH];
		if (GetTempPath(MAX_PATH, Folder) == 0 || GetTempFileName(Folder, _T("ftg"), 0, File) == 0) {
			Error = "No temporary file could be made for the rendering.";
			return false;
		}
		const int Limit = static_cast<int>(std::ceil(Seconds));
		CWaitCursor Wait;
		const BOOL Enabled = m_pMain->IsWindowEnabled();
		m_pMain->EnableWindow(FALSE);	// nothing changes the song while it renders

		// A game gets the whole song: muted channels play too, and are muted
		// again afterwards (the sound thread reads the mutes in PlayerGetNote)
		CFamiTrackerView *pView = CFamiTrackerView::GetView();
		std::vector<bool> Muted;
		for (unsigned int c = 0; pView != NULL && c < pDoc->GetAvailableChannels(); ++c) {
			Muted.push_back(pView->IsChannelMuted(c));
			pView->SetChannelMute(static_cast<int>(c), false);
		}

		const long Before = pSound->GetRenderCount();
		bool Started = pSound->RenderToFile(File, SONG_TIME_LIMIT, Limit, Track) != false;
		if (Started) {
			// It starts on the sound thread a moment later and runs to its end;
			// a short one can be over before it's seen running
			Started = false;
			const DWORD Since = GetTickCount();
			for (;;) {
				if (pSound->GetRenderCount() != Before) {	// over, its file closed
					Started = true;
					break;
				}
				if (pSound->IsRendering())
					Started = true;
				else if (!Started && GetTickCount() - Since > 5000)
					break;
				int Frame = 0, Time = 0, Frames = 0, Row = 0, Rows = 0;
				bool Done = false;
				pSound->GetRenderStat(Frame, Time, Done, Frames, Row, Rows);
				CString Text;
				Text.Format(_T("Rendering \"%s\" for the game: %d of %d seconds..."), pDoc->GetTrackTitle(static_cast<unsigned int>(Track)).GetString(),
					std::min(Time, Limit), Limit);
				if (m_pProgress != NULL)
					m_pProgress->SetWindowText(Text);
				m_pMain->SetMessageText(Text);
				Pump(50);
			}
		}
		if (std::find(Muted.begin(), Muted.end(), true) != Muted.end()) {
			for (size_t c = 0; c < Muted.size(); ++c)
				pView->SetChannelMute(static_cast<int>(c), Muted[c]);
			pView->GetPatternEditor()->InvalidateHeader();
			pView->RedrawWindow(pView->GetPatternEditor()->GetHeaderRect(), NULL, RDW_INVALIDATE);
		}
		m_pMain->EnableWindow(Enabled);
		m_pMain->SetMessageText(_T(""));

		CArray<CSoundGen::render_mark_t> Raw;
		pSound->GetRenderMarks(Raw);
		transcribe::Audio LeftSide, RightSide;
		std::string Problem;
		const bool Read = Started && transcribe::ReadWavSides(std::string(static_cast<LPCSTR>(CStringA(File))), LeftSide, RightSide,
			&Problem);
		DeleteFile(File);
		if (!Read) {
			Error = Started ? "The rendering couldn't be read: " + Problem : "FamiTracker didn't start rendering.";
			return false;
		}
		Left.swap(LeftSide.samples);
		Right.swap(RightSide.samples);
		SampleRate = LeftSide.sampleRate;
		const int Length = static_cast<int>(pDoc->GetPatternLength(static_cast<unsigned int>(Track)));
		Marks.clear();
		for (INT_PTR i = 0; i < Raw.GetCount(); ++i)
			Marks.push_back(std::make_pair(Raw[i].Frame * Length + Raw[i].Row, static_cast<long long>(Raw[i].Sample)));
		return true;
	}

	bool SaveFile(const std::string &FileName, const std::string &Bytes, std::string &Path, std::string &Error)
	{
		CFamiTrackerDoc *pDoc = Doc();
		const CString Song = pDoc != NULL ? pDoc->GetPathName() : CString();
		if (Song.IsEmpty()) {
			Error = "The song hasn't been saved yet, so there's no folder for the files.";
			return false;
		}
		const CString Full = Song.Left(Song.ReverseFind(_T('\\')) + 1) + Ansi(FileName);
		FILE *pFile = NULL;
		if (_tfopen_s(&pFile, Full, _T("wb")) != 0 || pFile == NULL) {
			Error = "Couldn't write " + Utf8(Full) + " (is it open somewhere else?).";
			return false;
		}
		const size_t Written = fwrite(Bytes.data(), 1, Bytes.size(), pFile);
		const bool Closed = fclose(pFile) == 0;
		if (Written != Bytes.size() || !Closed) {
			DeleteFile(Full);
			Error = "Couldn't write all of " + Utf8(Full) + " (is the disk full?).";
			return false;
		}
		Path = Utf8(Full);
		return true;
	}

	bool ReadFile(const std::string &FileName, std::string &Bytes, std::string &Error)
	{
		Bytes.clear();
		CFamiTrackerDoc *pDoc = Doc();
		const CString Song = pDoc != NULL ? pDoc->GetPathName() : CString();
		if (Song.IsEmpty())
			return false;	// no folder yet, so no file
		const CString Full = Song.Left(Song.ReverseFind(_T('\\')) + 1) + Ansi(FileName);
		if (GetFileAttributes(Full) == INVALID_FILE_ATTRIBUTES)
			return false;
		FILE *pFile = NULL;
		if (_tfopen_s(&pFile, Full, _T("rb")) != 0 || pFile == NULL) {
			Error = "Couldn't read " + Utf8(Full) + " (is it open somewhere else?).";
			return false;
		}
		char Buffer[4096];
		size_t Read;
		while ((Read = fread(Buffer, 1, sizeof(Buffer), pFile)) > 0 && Bytes.size() < 1024 * 1024)
			Bytes.append(Buffer, Read);
		fclose(pFile);
		return true;
	}

	static bool PlainName(const std::string &Name)
	{
		return !Name.empty() && Name.find_first_of("\\/:*?\"<>|") == std::string::npos && Name != "." && Name != "..";
	}

	// send_file and PUT /inbox: a file from another computer into the Inbox folder next to FamiTracker.exe (the
	// tools checked its name)
	bool SaveInbox(const std::string &Name, const std::string &Bytes, std::string &Path, std::string &Error)
	{
		const CString File = Ansi(Name);
		if (!PlainName(Name) || Utf8(File) != Name) {
			Error = "FamiTracker can't make a file called " + Name + " on this computer; give it a name of plain letters.";
			return false;
		}
		const CString Folder = InboxFolder(true);
		if (Folder.IsEmpty()) {
			Error = "FamiTracker found no folder for its Inbox.";
			return false;
		}
		const CString Full = Folder + _T("\\") + File;
		FILE *pFile = NULL;
		if (_tfopen_s(&pFile, Full, _T("wb")) != 0 || pFile == NULL) {
			Error = "Couldn't write " + Utf8(Full) + " (is it open somewhere else?).";
			return false;
		}
		const size_t Written = fwrite(Bytes.data(), 1, Bytes.size(), pFile);
		const bool Closed = fclose(pFile) == 0;
		if (Written != Bytes.size() || !Closed) {
			DeleteFile(Full);
			Error = "Couldn't write all of " + Utf8(Full) + " (is the disk full?).";
			return false;
		}
		Path = Utf8(Full);
		return true;
	}

	// GET /files: a file next to the song, all of it (the OGG files export_for_game saves)
	bool ReadExport(const std::string &Name, std::string &Bytes, std::string &Error)
	{
		Bytes.clear();
		CFamiTrackerDoc *pDoc = Doc();
		const CString Song = pDoc != NULL ? pDoc->GetPathName() : CString();
		const CString File = Ansi(Name);
		if (Song.IsEmpty() || !PlainName(Name) || Utf8(File) != Name)
			return false;	// no such file can be there
		const CString Full = Song.Left(Song.ReverseFind(_T('\\')) + 1) + File;
		const DWORD Attributes = GetFileAttributes(Full);
		if (Attributes == INVALID_FILE_ATTRIBUTES || (Attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
			return false;
		FILE *pFile = NULL;
		if (_tfopen_s(&pFile, Full, _T("rb")) != 0 || pFile == NULL) {
			Error = "Couldn't read " + Utf8(Full) + " (is it open somewhere else?).";
			return false;
		}
		char Buffer[65536];
		size_t Read;
		while ((Read = fread(Buffer, 1, sizeof(Buffer), pFile)) > 0 && Bytes.size() < (256u << 20))
			Bytes.append(Buffer, Read);
		const bool Failed = ferror(pFile) != 0, Whole = feof(pFile) != 0;
		fclose(pFile);
		if (Failed || !Whole) {
			Error = Failed ? "Couldn't read all of " + Utf8(Full) + "." : Utf8(Full) + " is too big to send (over 256 MB).";
			Bytes.clear();
			return false;
		}
		return true;
	}

	bool ListLibrary(std::vector<std::string> &Names, std::string &Error)
	{
		Names.clear();
		const CString Folder = ReferencesFolder(false);
		if (Folder.IsEmpty()) {
			Error = "FamiTracker couldn't find a folder for the reference library.";
			return false;
		}
		WIN32_FIND_DATA Found;
		HANDLE hFind = FindFirstFile(Folder + _T("\\*.json"), &Found);
		if (hFind == INVALID_HANDLE_VALUE)
			return true;	// no folder or no references yet
		do {
			if ((Found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
				Names.push_back(Utf8(Found.cFileName));
		} while (FindNextFile(hFind, &Found));
		FindClose(hFind);
		return true;
	}

	bool ReadLibrary(const std::string &Name, std::string &Bytes, std::string &Error)
	{
		Bytes.clear();
		const CString Folder = ReferencesFolder(false);
		if (Folder.IsEmpty() || !PlainName(Name))
			return false;
		const CString Full = Folder + _T("\\") + Ansi(Name);
		if (GetFileAttributes(Full) == INVALID_FILE_ATTRIBUTES)
			return false;
		FILE *pFile = NULL;
		if (_tfopen_s(&pFile, Full, _T("rb")) != 0 || pFile == NULL) {
			Error = "Couldn't read " + Utf8(Full) + " (is it open somewhere else?).";
			return false;
		}
		char Buffer[4096];
		size_t Read;
		while ((Read = fread(Buffer, 1, sizeof(Buffer), pFile)) > 0 && Bytes.size() < 4 * 1024 * 1024)
			Bytes.append(Buffer, Read);
		fclose(pFile);
		return true;
	}

	bool SaveLibrary(const std::string &Name, const std::string &Bytes, std::string &Path, std::string &Error)
	{
		if (!PlainName(Name)) {
			Error = "A reference's file name has no folders.";
			return false;
		}
		const CString Folder = ReferencesFolder(true);
		if (Folder.IsEmpty()) {
			Error = "FamiTracker couldn't find a folder for the reference library.";
			return false;
		}
		const CString Full = Folder + _T("\\") + Ansi(Name);
		FILE *pFile = NULL;
		if (_tfopen_s(&pFile, Full, _T("wb")) != 0 || pFile == NULL) {
			Error = "Couldn't write " + Utf8(Full) + " (is it open somewhere else?).";
			return false;
		}
		const size_t Written = fwrite(Bytes.data(), 1, Bytes.size(), pFile);
		const bool Closed = fclose(pFile) == 0;
		if (Written != Bytes.size() || !Closed) {
			DeleteFile(Full);
			Error = "Couldn't write all of " + Utf8(Full) + " (is the disk full?).";
			return false;
		}
		Path = Utf8(Full);
		return true;
	}

	bool DeleteLibrary(const std::string &Name, std::string &Error)
	{
		const CString Folder = ReferencesFolder(false);
		if (Folder.IsEmpty() || !PlainName(Name)) {
			Error = "There's no such reference.";
			return false;
		}
		const CString Full = Folder + _T("\\") + Ansi(Name);
		if (!DeleteFile(Full)) {
			Error = "Couldn't delete " + Utf8(Full) + ".";
			return false;
		}
		return true;
	}

	// make_sound_effect's "latest": the newest WAV file in the Recordings folder
	bool LatestRecording(std::string &Path, std::string &Error)
	{
		const CString Folder = RecordingsFolder(false);
		CString Newest;
		FILETIME Time = { 0, 0 };
		WIN32_FIND_DATA Found;
		HANDLE hFind = Folder.IsEmpty() ? INVALID_HANDLE_VALUE : FindFirstFile(Folder + _T("\\*.wav"), &Found);
		if (hFind != INVALID_HANDLE_VALUE) {
			do {
				if ((Found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 && CompareFileTime(&Found.ftLastWriteTime, &Time) > 0) {
					Time = Found.ftLastWriteTime;
					Newest = Found.cFileName;
				}
			} while (FindNextFile(hFind, &Found));
			FindClose(hFind);
		}
		if (Newest.IsEmpty()) {
			Error = "There are no recordings yet" + (Folder.IsEmpty() ? std::string() : " in " + Utf8(Folder)) + ": Edit > Record "
				"from microphone and Edit > Sound effect from your voice keep their takes there. Give the path of a WAV file "
				"instead.";
			return false;
		}
		Path = Utf8(Folder + _T("\\") + Newest);
		return true;
	}

	// import_nsf: the module replaces the song, as File > Import NSF does
	bool OpenText(const std::string &Text, std::string &Error)
	{
		CFamiTrackerDoc *pDoc = Doc();
		if (pDoc == NULL) {
			Error = "No song is open in FamiTracker.";
			return false;
		}
		if (!m_bOwnDialog && !m_pMain->IsWindowEnabled()) {
			Error = "A dialog is open in FamiTracker; ask the user to close it, then try again.";
			return false;
		}
		Stop();
		const CString Problem = OpenModuleText(m_pMain, pDoc, CString(Text.c_str()), CString());
		if (!Problem.IsEmpty()) {
			Error = Utf8(Problem);
			return false;
		}
		return true;
	}

	// Edit > Sound effect from your voice: make_sound_effect with the take
	CString FreeSoundName(const CString &Base)
	{
		ftapi::SongInfo Info;
		std::string Error;
		if (!GetSong(Info, Error))
			return Base;
		return Ansi(ftapi::FreeSoundName(Info, Utf8(Base)));
	}

	int MakeSoundEffect(const CString &Path, CString &Name, bool FitOctave, int Octaves, bool Noise, CString &Error)
	{
		if (m_bBusy) {
			Error = _T("FamiTracker is busy with Claude Code; try again in a moment.");
			return -1;
		}
		m_bBusy = true;
		m_bOwnDialog = true;
		ftapi::Json Args = ftapi::Json::MakeObject();
		Args.Set("recording", Utf8(Path));
		if (!Name.IsEmpty())
			Args.Set("name", Utf8(Name));
		if (!FitOctave)
			Args.Set("octaves", Octaves);
		Args.Set("noise", Noise);
		ftapi::ToolResult Result;
		try {
			Result = m_Tools.Call("make_sound_effect", Args);
		}
		catch (...) {
			Result.Text = "The sound effect couldn't be made.";
			Result.IsError = true;
		}
		m_bOwnDialog = false;
		m_bBusy = false;
		CFamiTrackerDoc *pDoc = Doc();
		if (Result.IsError || pDoc == NULL) {
			Error = Ansi(Result.Text);
			return -1;
		}
		const unsigned int Made = Track();	// the new track is the selected one
		Name = pDoc->GetTrackTitle(Made);
		return static_cast<int>(Made);
	}

	void PlaySelectedTrack()
	{
		std::string Error;
		Play(ftapi::PlayRequest(), Error);	// from the start
	}

	std::string LocalPath(const std::string &Path)
	{
		return std::string(static_cast<LPCSTR>(CStringA(Ansi(Path))));
	}

	// File > Export for games: the export_for_game tool, for the dialog
	// Loudness: the loudness in LUFS to bring every track to, or 0 for its loudest peak at -1 dBFS
	CString ExportForGames(bool All, double Quality, double Loudness, CWnd *pProgress)
	{
		if (m_bBusy)
			return _T("FamiTracker is busy with Claude Code; try again in a moment.");
		m_bBusy = true;
		m_bOwnDialog = true;
		m_pProgress = pProgress;
		ftapi::Json Args = ftapi::Json::MakeObject();
		Args.Set("tracks", All ? "all" : "this");
		Args.Set("quality", Quality);
		if (Loudness < 0.0)
			Args.Set("loudness", Loudness);
		ftapi::ToolResult Result;
		try {
			Result = m_Tools.Call("export_for_game", Args);
		}
		catch (...) {
			Result.Text = "The export failed.";
		}
		m_pProgress = NULL;
		m_bOwnDialog = false;
		m_bBusy = false;
		CString Text = Ansi(Result.Text);
		Text.Replace(_T("\n"), _T("\r\n"));
		return Text;
	}

	bool IsBusy() const { return m_bBusy; }

	// The piano roll's key and chords (ToolSet::Harmony); false when busy with Claude Code or without a song
	bool Harmony(ftapi::TrackHarmony &Out)
	{
		if (m_bBusy || m_pMain == NULL)
			return false;
		m_bBusy = true;
		bool Done = false;
		try {
			std::string Error;
			Done = m_Tools.Harmony(Out, Error);
		}
		catch (...) {
			Done = false;
		}
		m_bBusy = false;
		return Done;
	}

	// Instrument > Sound browser: every sound the song can get (ToolSet::Sounds), and one made into an instrument as
	// add_instrument or reference_sound makes it (ToolSet::MakeSound), while the browser's dialog is open
	bool Sounds(int Cursor, std::vector<ftapi::SoundEntry> &Out, std::string &Error)
	{
		if (m_bBusy || m_pMain == NULL) {
			Error = "FamiTracker is busy with Claude Code; try again in a moment.";
			return false;
		}
		m_bBusy = true;
		m_bOwnDialog = true;
		bool Done = false;
		try {
			Done = m_Tools.Sounds(Cursor, Out, Error);
		}
		catch (...) {
			Error = "The sounds couldn't be listed.";
			Done = false;
		}
		m_bOwnDialog = false;
		m_bBusy = false;
		return Done;
	}

	int MakeSound(const ftapi::SoundEntry &Sound, std::string &Error)
	{
		if (m_bBusy || m_pMain == NULL) {
			Error = "FamiTracker is busy with Claude Code; try again in a moment.";
			return -1;
		}
		m_bBusy = true;
		m_bOwnDialog = true;
		int Index = -1;
		try {
			Index = m_Tools.MakeSound(Sound, Error);
		}
		catch (...) {
			Error = "The instrument couldn't be added.";
			Index = -1;
		}
		m_bOwnDialog = false;
		m_bBusy = false;
		return Index;
	}

	// A tool for a window of FamiTracker's (the problems panel runs check_song, and a finding's fix): what it changes
	// goes into the song, one Ctrl+Z, whatever the Claude Code setting says, since the user asked for it there
	bool RunForWindow(const std::string &Name, const std::string &Arguments, std::string &Reply)
	{
		if (m_bBusy) {
			Reply = "FamiTracker is busy with Claude Code; try again in a moment.";
			return false;
		}
		ftapi::Json Args;
		if (!ftapi::ParseJson(Arguments, Args) || !Args.IsObject()) {
			Reply = "The tool's arguments aren't JSON.";
			return false;
		}
		m_bBusy = true;
		m_bByHand = true;
		ftapi::ToolResult Result;
		try {
			Result = m_Tools.Call(Name, Args);
		}
		catch (...) {
			Result.Text = "FamiTracker couldn't do that.";
			Result.IsError = true;
		}
		m_bByHand = false;
		m_bBusy = false;
		Reply = Result.Text;
		return !Result.IsError;
	}

	bool CanChange(std::string &Why)
	{
		CFamiTrackerDoc *pDoc = Doc();
		if (pDoc == NULL || !pDoc->IsFileLoaded()) {
			Why = "No song is open in FamiTracker.";
			return false;
		}
		if (!m_bOwnDialog && !m_pMain->IsWindowEnabled()) {
			Why = "A dialog is open in FamiTracker; ask the user to close it, then try again.";
			return false;
		}
		return true;
	}

	bool WriteSong(const std::vector<ftapi::ChannelCells> &Parts, const ftapi::SongWrite &Write, std::string &Error)
	{
		if (!CanChange(Error))
			return false;
		CFamiTrackerDoc *pDoc = Doc();
		const int Length = static_cast<int>(pDoc->GetPatternLength(Track()));
		std::vector<std::vector<stChanNote> > Cells(Parts.size());
		std::vector<song_part_t> Raw;
		for (size_t p = 0; p < Parts.size(); ++p) {
			for (size_t r = 0; r < Parts[p].Cells.size(); ++r)
				Cells[p].push_back(ToNote(Parts[p].Cells[r]));
			if (Cells[p].empty())
				continue;
			song_part_t Part;
			Part.Channel = Parts[p].Channel;
			Part.pCells = &Cells[p][0];
			Part.Rows = static_cast<int>(Cells[p].size());
			Part.Exact = Parts[p].Exact;
			Raw.push_back(Part);
		}
		if (Raw.empty() && Write.MoveBy == 0) {
			Error = "There was nothing to write.";
			return false;
		}
		ftapi::SectionStore Sections;
		if (Write.SetSections) {
			Sections.Set(0, AnsiSections(Write.Sections));
			if (!ftapi::ValidSections(Sections.Get(0), 0)) {
				Error = SECTIONS_REFUSED;
				return false;
			}
		}
		ftapi::KeyStore Keys;	// track 0's key: the track's afterwards (none: back to auto)
		if (Write.SetKey && Write.KeyChosen)
			Keys.Set(0, Write.Key);
		CString Message;
		if (!WriteSongParts(m_pMain, Raw.empty() ? NULL : &Raw[0], static_cast<int>(Raw.size()), Write.First / Length,
			Write.First % Length, Write.LinkClipId, Write.MoveFrame, Write.MoveBy, Write.Cursor, Message,
			Write.SetSections ? &Sections : NULL, Write.SetKey ? &Keys : NULL)) {
			Error = Utf8(Message);
			return false;
		}
		return true;
	}

	bool SetSections(const ftapi::SectionList &Sections, std::string &Error)
	{
		if (!CanChange(Error))
			return false;
		CFamiTrackerDoc *pDoc = Doc();
		const int Track = static_cast<int>(this->Track());
		const ftapi::SectionList List = AnsiSections(Sections);
		if (!ftapi::ValidSections(List, static_cast<int>(pDoc->GetFrameCount(static_cast<unsigned int>(Track))))) {
			Error = SECTIONS_REFUSED;
			return false;
		}
		if (List == pDoc->GetSections()->Get(Track))
			return true;
		ftapi::SectionStore *pStore = new ftapi::SectionStore();
		pStore->Set(0, List);
		CFrameAction *pAction = new CFrameAction(CFrameAction::ACT_SECTIONS);
		pAction->SetSections(pStore);
		if (!m_pMain->AddAction(pAction)) {
			Error = "The sections couldn't be changed.";
			return false;
		}
		return true;
	}

	bool SetKey(bool Chosen, const ftapi::KeyChoice &Choice, std::string &Error)
	{
		if (!CanChange(Error))
			return false;
		ftapi::KeyChoice Now;
		const bool WasChosen = Doc()->GetKeys()->Get(static_cast<int>(Track()), Now);
		if (WasChosen == Chosen && (!Chosen || Now == Choice))
			return true;
		if (!SetTrackKey(m_pMain, Chosen, Choice)) {
			Error = "The track's key couldn't be changed.";
			return false;
		}
		return true;
	}

	// The mixer window's faders, pans, Soft and Space and its space, as one undoable step (CChannelMixAction)
	bool SetMixer(const ftapi::MixerSettings &Mixer, std::string &Error)
	{
		if (!CanChange(Error))
			return false;
		CFamiTrackerDoc *pDoc = Doc();
		ftapi::MixStore Mix = *pDoc->GetChannelMix();
		const int Channels = static_cast<int>(pDoc->GetAvailableChannels());
		for (int c = 0; c < Channels && c < static_cast<int>(Mixer.Levels.size()) && c < static_cast<int>(Mixer.Pans.size()); ++c) {
			const size_t i = static_cast<size_t>(c);
			Mix.Set(pDoc->GetChannelType(c), Mixer.Levels[i], Mixer.Pans[i]);
			if (i < Mixer.Softs.size() && i < Mixer.Spaces.size())
				Mix.SetDepth(pDoc->GetChannelType(c), Mixer.Softs[i], Mixer.Spaces[i]);
		}
		Mix.SetSpace(Mixer.Room, Mixer.Echo, Mixer.Repeats);
		if (Mix == *pDoc->GetChannelMix())
			return true;
		if (!SetChannelMix(m_pMain, Mix)) {
			Error = "The mixer couldn't be changed.";
			return false;
		}
		return true;
	}

	// The changes until EndGroup are one Ctrl+Z (CMainFrame::BeginActionGroup)
	void BeginGroup()
	{
		m_pMain->BeginActionGroup();
	}

	void EndGroup()
	{
		m_pMain->EndActionGroup();
	}

	bool AddClip(std::string &Name, const std::vector<int> &Sources, const std::vector<std::vector<ftapi::Cell> > &Columns,
		std::string &Error)
	{
		if (!CanChange(Error))
			return false;
		CFamiTrackerDoc *pDoc = Doc();
		CClipLibrary *pClips = pDoc != NULL ? pDoc->GetClips() : NULL;
		if (pClips == NULL || Columns.empty() || Columns[0].empty()) {
			Error = "There was nothing to keep as a clip.";
			return false;
		}
		CString Base = Ansi(Name);
		Base.Replace(_T('\r'), _T(' '));
		Base.Replace(_T('\n'), _T(' '));
		const CString Unique = pClips->NewName(Base, false);
		const int Rows = static_cast<int>(Columns[0].size());
		const int Index = pClips->Add(Unique, static_cast<int>(Columns.size()), Rows);
		if (Index < 0) {
			Error = "The song has too many clips already; the user can delete some in the clips panel.";
			return false;
		}
		for (size_t c = 0; c < Columns.size(); ++c) {
			pClips->SetSource(Index, static_cast<int>(c), c < Sources.size() ? Sources[c] : -1);
			for (int r = 0; r < Rows && r < static_cast<int>(Columns[c].size()); ++r)
				pClips->SetCell(Index, static_cast<int>(c), r, ToNote(Columns[c][static_cast<size_t>(r)]));
		}
		pDoc->SetModifiedFlag();
		Name = Utf8(Unique);
		return true;
	}

	int AddInstrument(const ftapi::Preset &Sound, const std::string &Chip, bool Sawtooth, std::string &Error)
	{
		if (!CanChange(Error))
			return -1;
		CString Problem;
		const int Index = Chip == "5B" ? AddPreset5BInstrument(Doc(), Sound, Problem) :
			AddPresetInstrument(Doc(), Sound, Chip == "VRC6", Sawtooth, Problem);
		if (Index < 0)
			Error = Utf8(Problem);
		return Index;
	}

	int AddN163Instrument(const ftapi::WaveSound &Sound, int WavePos, std::string &Error)
	{
		if (!CanChange(Error))
			return -1;
		CString Problem;
		const int Index = AddWaveSoundInstrument(Doc(), Sound, WavePos, Problem);
		if (Index < 0)
			Error = Utf8(Problem);
		return Index;
	}

	int AddFdsInstrument(const ftapi::FdsSound &Sound, std::string &Error)
	{
		if (!CanChange(Error))
			return -1;
		CString Problem;
		const int Index = AddFdsSoundInstrument(Doc(), Sound, Problem);
		if (Index < 0)
			Error = Utf8(Problem);
		return Index;
	}

	int AddVrc7Instrument(int Patch, const std::string &Name, std::string &Error)
	{
		if (!CanChange(Error))
			return -1;
		CString Problem;
		const int Index = AddVrc7PatchInstrument(Doc(), Patch, Ansi(Name), Problem);
		if (Index < 0)
			Error = Utf8(Problem);
		return Index;
	}

	int AddVrc7CustomInstrument(const std::vector<int> &Regs, const std::string &Name, std::string &Error)
	{
		if (!CanChange(Error))
			return -1;
		if (Regs.size() != 8) {
			Error = "A VRC7 custom patch has 8 registers.";
			return -1;
		}
		CString Problem;
		const int Index = ::AddVrc7CustomInstrument(Doc(), &Regs[0], Ansi(Name), Problem);
		if (Index < 0)
			Error = Utf8(Problem);
		return Index;
	}

	int AddDpcmKit(const ftapi::DpcmKit &Kit, std::string &Error)
	{
		if (!CanChange(Error))
			return -1;
		CFamiTrackerDoc *pDoc = Doc();
		const CString Name = Ansi(Kit.Name);
		const int Existing = FindNamedInstrument(pDoc, INST_2A03, Name);
		if (Existing >= 0)
			return Existing;
		// The samples: ones of the same name and size are reused
		std::vector<int> Slots;
		std::vector<int> Taken;
		unsigned int Bytes = 0;
		for (size_t s = 0; s < Kit.Samples.size(); ++s) {
			const ftapi::DpcmSample &Sample = Kit.Samples[s];
			int Slot = -1;
			for (int i = 0; i < MAX_DSAMPLES && Slot < 0; ++i) {
				const CDSample *pSample = pDoc->GetSample(static_cast<unsigned int>(i));
				if (pSample->GetSize() == Sample.Data.size() && CString(pSample->GetName()) == Ansi(Sample.Name))
					Slot = i;
			}
			if (Slot < 0) {
				Bytes += static_cast<unsigned int>(Sample.Data.size());
				for (int i = 0; i < MAX_DSAMPLES && Slot < 0; ++i)
					if (!pDoc->IsSampleUsed(static_cast<unsigned int>(i)) && pDoc->GetSample(static_cast<unsigned int>(i))->GetSize() == 0 &&
						std::find(Taken.begin(), Taken.end(), i) == Taken.end())
						Slot = i;
				if (Slot < 0) {
					Error = "There's no room for more DPCM samples (a song has at most 64).";
					return -1;
				}
				Taken.push_back(Slot);
			}
			Slots.push_back(Slot);
		}
		if (pDoc->GetTotalSampleSize() + Bytes > static_cast<unsigned int>(MAX_SAMPLE_SPACE)) {
			Error = "The song's DPCM samples would take more room than FamiTracker has.";
			return -1;
		}
		CInstrument *pNew = theApp.GetChannelMap()->GetChipInstrument(SNDCHIP_NONE);
		if (pNew == NULL) {
			Error = "FamiTracker has no 2A03 instruments.";
			return -1;
		}
		pNew->SetName(Name);
		const int Index = pDoc->AddInstrument(pNew);
		if (Index == INVALID_INSTRUMENT) {
			pNew->Release();
			Error = "There are no free instrument slots left (a song has at most 64).";
			return -1;
		}
		for (size_t s = 0; s < Kit.Samples.size(); ++s) {
			CDSample *pSample = pDoc->GetSample(static_cast<unsigned int>(Slots[s]));
			if (std::find(Taken.begin(), Taken.end(), Slots[s]) == Taken.end())
				continue;	// reused
			const std::vector<unsigned char> &Data = Kit.Samples[s].Data;
			pSample->Allocate(static_cast<unsigned int>(Data.size()), reinterpret_cast<const char*>(&Data[0]));
			pSample->SetName(static_cast<LPCSTR>(CStringA(Ansi(Kit.Samples[s].Name))));
		}
		CInstrument2A03 *pInstrument = static_cast<CInstrument2A03*>(pDoc->GetInstrument(static_cast<unsigned int>(Index)));	// retained
		if (pInstrument != NULL) {
			for (size_t k = 0; k < Kit.Keys.size(); ++k) {
				const ftapi::DpcmKey &Key = Kit.Keys[k];
				const int Octave = Key.Pitch / 12, Note = Key.Pitch % 12;
				pInstrument->SetSample(Octave, Note, static_cast<char>(Slots[static_cast<size_t>(Key.Sample)] + 1));
				pInstrument->SetSamplePitch(Octave, Note, static_cast<char>(Key.Rate & 15));
				pInstrument->SetSampleLoop(Octave, Note, false);
				pInstrument->SetSampleDeltaValue(Octave, Note, static_cast<char>(Key.Delta));
			}
			pInstrument->Release();
		}
		pDoc->SetModifiedFlag();
		pDoc->UpdateAllViews(NULL, UPDATE_INSTRUMENT);
		return Index;
	}

	int RefreshDpcmKit(const ftapi::DpcmKit &Kit, std::string &Error)
	{
		if (!CanChange(Error))
			return -1;
		CFamiTrackerDoc *pDoc = Doc();
		const int Index = FindNamedInstrument(pDoc, INST_2A03, Ansi(Kit.Name));
		if (Index < 0)
			return 0;
		CInstrument2A03 *pInstrument = static_cast<CInstrument2A03*>(pDoc->GetInstrument(static_cast<unsigned int>(Index)));	// retained
		if (pInstrument == NULL)
			return 0;
		int Refreshed = 0;
		for (size_t k = 0; k < Kit.Keys.size(); ++k) {
			const ftapi::DpcmKey &Key = Kit.Keys[k];
			if (Key.Sample < 0 || static_cast<size_t>(Key.Sample) >= Kit.Samples.size())
				continue;
			const int Slot = pInstrument->GetSample(Key.Pitch / 12, Key.Pitch % 12) - 1;
			if (Slot < 0 || Slot >= MAX_DSAMPLES)
				continue;
			const ftapi::DpcmSample &Sample = Kit.Samples[static_cast<size_t>(Key.Sample)];
			CDSample *pSample = pDoc->GetSample(static_cast<unsigned int>(Slot));
			// Only the kit's own samples: one of the user's under its key stays
			if (pSample == NULL || Sample.Data.empty() || CString(pSample->GetName()) != Ansi(Sample.Name))
				continue;
			const unsigned char *pOld = reinterpret_cast<const unsigned char*>(pSample->GetData());
			bool Same = pOld != NULL && pSample->GetSize() == Sample.Data.size();
			for (size_t b = 0; b < Sample.Data.size() && Same; ++b)
				Same = pOld[b] == Sample.Data[b];
			if (Same)
				continue;
			if (pDoc->GetTotalSampleSize() - pSample->GetSize() + Sample.Data.size() > static_cast<unsigned int>(MAX_SAMPLE_SPACE))
				continue;
			pSample->Allocate(static_cast<unsigned int>(Sample.Data.size()), reinterpret_cast<const char*>(&Sample.Data[0]));
			++Refreshed;
		}
		pInstrument->Release();
		if (Refreshed > 0) {
			pDoc->SetModifiedFlag();
			pDoc->UpdateAllViews(NULL, UPDATE_INSTRUMENT);
		}
		return Refreshed;
	}

	void CountInstrumentUses(std::vector<int> &InTracks, std::vector<int> &InClips)
	{
		InTracks.assign(MAX_INSTRUMENTS, 0);
		InClips.assign(MAX_INSTRUMENTS, 0);
		CFamiTrackerDoc *pDoc = Doc();
		if (pDoc == NULL)
			return;
		const unsigned int Channels = pDoc->GetAvailableChannels();
		for (unsigned int t = 0; t < pDoc->GetTrackCount(); ++t) {
			const unsigned int Length = pDoc->GetPatternLength(t);
			for (unsigned int f = 0; f < pDoc->GetFrameCount(t); ++f)
				for (unsigned int c = 0; c < Channels; ++c)
					for (unsigned int r = 0; r < Length; ++r) {
						stChanNote Note;
						pDoc->GetNoteData(t, f, c, r, &Note);
						if (Note.Instrument < MAX_INSTRUMENTS)
							++InTracks[Note.Instrument];
					}
		}
		const CClipLibrary *pClips = pDoc->GetClips();
		for (int i = 0; pClips != NULL && i < pClips->GetCount(); ++i)
			for (int c = 0; c < pClips->GetColumns(i); ++c)
				for (int r = 0; r < pClips->GetRows(i); ++r) {
					stChanNote Note;
					pClips->GetCell(i, c, r, Note);
					if (Note.Instrument < MAX_INSTRUMENTS)
						++InClips[Note.Instrument];
				}
	}

	bool RemoveInstruments(const std::vector<int> &Indices, std::string &Error)
	{
		if (!CanChange(Error))
			return false;
		CFamiTrackerDoc *pDoc = Doc();
		m_pMain->CloseInstrumentEditor();	// it may show one of them
		for (size_t i = 0; i < Indices.size(); ++i)
			if (Indices[i] >= 0 && Indices[i] < MAX_INSTRUMENTS && pDoc->IsInstrumentUsed(static_cast<unsigned int>(Indices[i])))
				pDoc->RemoveInstrument(static_cast<unsigned int>(Indices[i]));
		pDoc->RemoveUnusedSequences();
		pDoc->SetModifiedFlag();
		pDoc->UpdateAllViews(NULL, UPDATE_INSTRUMENT);
		return true;
	}

	int ExpansionNotes(const std::vector<std::string> &Chips, int Channels, std::vector<int> &Tracks)
	{
		Tracks.clear();
		CFamiTrackerDoc *pDoc = Doc();
		unsigned char Flags = SNDCHIP_NONE;
		if (pDoc == NULL || !pDoc->IsFileLoaded() || !ChipFlags(Chips, Flags))
			return 0;
		const unsigned int Namco = static_cast<unsigned int>(std::max(1, std::min(8, Channels)));
		return CountChannelNotes(pDoc, LeavingChannels(pDoc, Flags, Namco), Tracks);
	}

	// As Module > Module properties does (ChipSwitch.h): the channels of the chips that stay keep
	// their notes in their new columns, those of chips switched off are emptied, and channels that
	// come in start empty with one effect column
	bool SetExpansion(const std::vector<std::string> &Chips, int Channels, int &Moved, int &Lost, std::string &Error)
	{
		Moved = Lost = 0;
		if (!CanChange(Error))
			return false;
		unsigned char Flags = SNDCHIP_NONE;
		if (!ChipFlags(Chips, Flags)) {
			Error = "FamiTracker can't switch to those chips.";
			return false;
		}
		CFamiTrackerDoc *pDoc = Doc();
		Stop();
		if (theApp.IsPlaying())
			theApp.StopPlayerAndWait();
		Lost = SwitchChips(pDoc, Flags, static_cast<unsigned int>(std::max(1, std::min(8, Channels))), Moved);
		pDoc->SetModifiedFlag();
		pDoc->UpdateAllViews(NULL, UPDATE_PROPERTIES);
		m_pMain->UpdateControls();
		theApp.GetSoundGenerator()->DocumentPropertiesChanged(pDoc);
		return true;
	}

	bool SetNames(const ftapi::SongNames &Names, std::string &Error)
	{
		if (!CanChange(Error))
			return false;
		CFamiTrackerDoc *pDoc = Doc();
		const CString Title = Ansi(Names.Title), Author = Ansi(Names.Author), Copyright = Ansi(Names.Copyright);
		if ((Names.HasTitle && Title.GetLength() > 31) || (Names.HasAuthor && Author.GetLength() > 31) ||
			(Names.HasCopyright && Copyright.GetLength() > 31)) {
			Error = "FamiTracker keeps at most 31 characters for the title, the author and the copyright.";
			return false;
		}
		if (Names.HasTitle)
			pDoc->SetSongName(Title);
		if (Names.HasAuthor)
			pDoc->SetSongArtist(Author);
		if (Names.HasCopyright)
			pDoc->SetSongCopyright(Copyright);
		if (Names.HasTrack)
			pDoc->SetTrackTitle(Track(), Ansi(Names.Track));
		m_pMain->SetSongInfo(pDoc->GetSongName(), pDoc->GetSongArtist(), pDoc->GetSongCopyright());
		if (Names.HasTrack) {
			m_pMain->UpdateTrackBox();
			static_cast<CFrameWnd*>(m_pMain)->OnUpdateFrameTitle(TRUE);	// the caption shows the track's name
		}
		pDoc->UpdateAllViews(NULL, UPDATE_PROPERTIES);
		return true;
	}

	bool SetSpeedTempo(int Speed, int Tempo, std::string &Error)
	{
		if (!CanChange(Error))
			return false;
		if (!m_pMain->AddAction(new CMcpTempoAction(Speed, Tempo))) {
			Error = "The tempo couldn't be changed.";
			return false;
		}
		return true;
	}

	bool SetMeter(int RowsPerBeat, int RowsPerBar, int PatternLength, std::string &Error)
	{
		if (!CanChange(Error))
			return false;
		if (RowsPerBeat < 1 || RowsPerBar < RowsPerBeat || RowsPerBar > MAX_PATTERN_LENGTH || PatternLength > MAX_PATTERN_LENGTH) {
			Error = "FamiTracker can't show that meter.";
			return false;
		}
		if (!m_pMain->AddAction(new CMcpMeterAction(RowsPerBeat, RowsPerBar, PatternLength))) {
			Error = "The meter couldn't be changed.";
			return false;
		}
		return true;
	}

	bool Play(const ftapi::PlayRequest &Request, std::string &Error)
	{
		CFamiTrackerDoc *pDoc = Doc();
		CFamiTrackerView *pView = CFamiTrackerView::GetView();
		if (pDoc == NULL || pView == NULL) {
			Error = "No song is open in FamiTracker.";
			return false;
		}
		// The parts: the request's rows, then the other versions'
		std::vector<ftapi::PlayPart> Parts(1);
		Parts[0].Track = Request.Track >= 0 ? Request.Track : static_cast<int>(Track());
		Parts[0].First = Request.First;
		Parts[0].Rows = Request.Rows;
		Parts.insert(Parts.end(), Request.Then.begin(), Request.Then.end());
		for (size_t i = 0; i < Parts.size(); ++i) {
			const ftapi::PlayPart &p = Parts[i];
			if (p.Track < 0 || p.Track >= static_cast<int>(pDoc->GetTrackCount())) {
				Error = "There's no such track.";
				return false;
			}
			const unsigned int Track = static_cast<unsigned int>(p.Track);
			const int Length = static_cast<int>(pDoc->GetPatternLength(Track));
			if (p.First < 0 || p.First >= Length * static_cast<int>(pDoc->GetFrameCount(Track)) || (i > 0 && p.Rows < 1)) {
				Error = "That bar is past the end of the song.";
				return false;
			}
		}
		if (theApp.IsPlaying())
			theApp.StopPlayerAndWait();
		EndPlayback();	// what an earlier call left: its loop, the mutes it set
		// Channels silent this time; the user's own mutes are kept for afterwards
		if (!Request.Muted.empty()) {
			for (unsigned int c = 0; c < pDoc->GetAvailableChannels(); ++c) {
				m_UserMutes.push_back(pView->IsChannelMuted(c));
				pView->SetChannelMute(static_cast<int>(c), c < Request.Muted.size() && Request.Muted[c]);
			}
			pView->GetPatternEditor()->InvalidateHeader();
			pView->RedrawWindow(pView->GetPatternEditor()->GetHeaderRect(), NULL, RDW_INVALIDATE);
		}
		m_PlayParts = Parts;
		m_bPlayLoop = Request.Loop && Request.Rows > 0;
		m_iReturnTrack = Request.ReturnTrack;
		BeginPart(0);
		if (Request.Rows > 0 || !m_UserMutes.empty() || m_iReturnTrack >= 0)
			SetTimer(m_hWindow, PLAY_TIMER, m_bPlayLoop || Parts.size() > 1 ? 20 : 50, NULL);
		return true;
	}

	void Stop()
	{
		theApp.StopPlayer();
		EndPlayback();
	}

	bool Undo(std::string &Message)
	{
		if (!CanChange(Message))
			return false;
		m_pMain->SendMessage(WM_COMMAND, ID_EDIT_UNDO);
		Message = "Undid the last change in the song (like Ctrl+Z); Ctrl+Y in FamiTracker brings it back.";
		return true;
	}

	bool Save(const std::string &CopyName, std::string &Message)
	{
		if (!CanChange(Message))
			return false;
		CFamiTrackerDoc *pDoc = Doc();
		const CString Path = pDoc->GetPathName();
		if (Path.IsEmpty()) {
			Message = "The song hasn't been saved yet. Ask the user to save it (File > Save As) so it has a place.";
			return false;
		}
		if (CopyName.empty()) {
			m_pMain->SendMessage(WM_COMMAND, ID_FILE_SAVE);
			if (pDoc->IsModified()) {
				Message = "The song couldn't be saved.";
				return false;
			}
			Message = "Saved the song (" + Utf8(Path.Mid(Path.ReverseFind(_T('\\')) + 1)) + ").";
			return true;
		}
		CString File = Ansi(CopyName);
		if (File.Right(4).CompareNoCase(_T(".ftm")) != 0)
			File += _T(".ftm");
		const CString Copy = Path.Left(Path.ReverseFind(_T('\\')) + 1) + File;
		if (GetFileAttributes(Copy) != INVALID_FILE_ATTRIBUTES) {
			Message = "There's already a file called " + Utf8(File) + " next to the song; pick another copy_name.";
			return false;
		}
		const BOOL Modified = pDoc->IsModified();
		if (!pDoc->OnSaveDocument(Copy)) {
			Message = "The copy couldn't be saved.";
			return false;
		}
		pDoc->SetModifiedFlag(Modified);	// the song itself still has what it had unsaved
		Message = "Saved a copy as " + Utf8(File) + " next to the song; the song itself wasn't saved.";
		return true;
	}

private:
	CFamiTrackerDoc *Doc() const
	{
		return m_pMain != NULL ? static_cast<CFamiTrackerDoc*>(m_pMain->GetActiveDocument()) : NULL;
	}

	unsigned int Track() const
	{
		return static_cast<unsigned int>(std::max(0, m_pMain->GetSelectedTrack()));
	}


	void LoadSettings()
	{
		m_Settings.Enabled = theApp.GetProfileInt(SECTION, _T("Enabled"), 0) != 0;
		m_Settings.Port = static_cast<int>(theApp.GetProfileInt(SECTION, _T("Port"), DEFAULT_PORT));
		if (m_Settings.Port < 1024 || m_Settings.Port > 65535)
			m_Settings.Port = DEFAULT_PORT;
		m_Settings.Token = theApp.GetProfileString(SECTION, _T("Token"), _T(""));
		if (m_Settings.Token.GetLength() < 16) {
			m_Settings.Token = NewToken();
			theApp.WriteProfileString(SECTION, _T("Token"), m_Settings.Token);
		}
		m_Settings.DirectChanges = theApp.GetProfileInt(SECTION, _T("Direct changes"), 0) != 0;
		m_Settings.Wsl = theApp.GetProfileInt(SECTION, _T("Claude Code in WSL"), 0) != 0;
		m_Settings.Network = theApp.GetProfileInt(SECTION, _T("Allow network"), 0) != 0;
		m_Settings.CoreTools = theApp.GetProfileInt(SECTION, _T("Core tools"), 0) != 0;
	}

	void SaveSettings()
	{
		theApp.WriteProfileInt(SECTION, _T("Enabled"), m_Settings.Enabled ? 1 : 0);
		theApp.WriteProfileInt(SECTION, _T("Port"), m_Settings.Port);
		theApp.WriteProfileString(SECTION, _T("Token"), m_Settings.Token);
		theApp.WriteProfileInt(SECTION, _T("Direct changes"), m_Settings.DirectChanges ? 1 : 0);
		theApp.WriteProfileInt(SECTION, _T("Claude Code in WSL"), m_Settings.Wsl ? 1 : 0);
		theApp.WriteProfileInt(SECTION, _T("Allow network"), m_Settings.Network ? 1 : 0);
		theApp.WriteProfileInt(SECTION, _T("Core tools"), m_Settings.CoreTools ? 1 : 0);
	}

	// This computer's addresses for the Host check, looked up again every few seconds (Tailscale may come up later)
	std::vector<std::string> OwnAddresses()
	{
		const DWORD Now = GetTickCount();	// the difference stays right when it wraps
		if (!m_bAddressesRead || Now - m_iAddressesTime > 10000) {
			const std::vector<mcp_address_t> Found = ListNetworkAddresses();
			m_Addresses.clear();
			for (size_t i = 0; i < Found.size(); ++i)
				m_Addresses.push_back(Found[i].Address);
			m_iAddressesTime = Now;
			m_bAddressesRead = true;
		}
		return m_Addresses;
	}

	static LRESULT CALLBACK WindowProc(HWND hWnd, UINT Message, WPARAM wParam, LPARAM lParam)
	{
		CMcpLink *pLink = reinterpret_cast<CMcpLink*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
		if (Message == WM_MCP_REQUEST) {
			// The listener may have stopped waiting; the exchange lives until both let go
			mcp_exchange_t *pExchange = reinterpret_cast<mcp_exchange_t*>(lParam);
			LRESULT Result = 0;
			if (pLink != NULL && BeginMcpExchange(pExchange))
				Result = pLink->Answer(pExchange);
			EndMcpExchange(pExchange);
			return Result;
		}
		if (pLink != NULL && Message == WM_TIMER && wParam == PLAY_TIMER) {
			pLink->CheckPlayback();
			return 0;
		}
		return DefWindowProc(hWnd, Message, wParam, lParam);
	}

	// A message-only window on the UI thread, where requests are answered
	void CreateWindowOnce()
	{
		if (m_hWindow != NULL)
			return;
		WNDCLASS Class;
		memset(&Class, 0, sizeof(Class));
		Class.lpfnWndProc = &CMcpLink::WindowProc;
		Class.hInstance = AfxGetInstanceHandle();
		Class.lpszClassName = WINDOW_CLASS;
		RegisterClass(&Class);	// fails harmlessly when already registered
		m_hWindow = CreateWindowEx(0, WINDOW_CLASS, _T(""), 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, AfxGetInstanceHandle(), NULL);
		if (m_hWindow != NULL)
			SetWindowLongPtr(m_hWindow, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
	}

	bool Listen(CString &Error)
	{
		m_Server.Stop();
		if (m_hWindow == NULL) {
			Error = _T("FamiTracker couldn't set up the connection.");
			m_sLastError = Error;
			return false;
		}
		char Reason[256] = "";
		if (!m_Server.Start(m_Settings.Port, m_Settings.Network, m_hWindow, WM_MCP_REQUEST, Reason, sizeof(Reason))) {
			Error = Reason;
			m_sLastError = Error;
			return false;
		}
		m_sLastError.Empty();
		return true;
	}

	CMainFrame *m_pMain;
	HWND m_hWindow;
	CMcpSocketServer m_Server;
	ftapi::ToolSet m_Tools;
	mcp_settings_t m_Settings;
	CString m_sLastError;
	std::vector<std::string> m_Addresses;	// this computer's, for other computers' requests
	DWORD m_iAddressesTime;					// when they were looked up
	bool m_bAddressesRead;
	bool m_bBusy;		// answering a request (a message box inside one could let another in)
	bool m_bStopping;
	bool m_bOwnDialog;	// working for a dialog of FamiTracker's (Export for games, Sound effect from your voice),
						// which may change the song
	bool m_bByHand;		// working for a window of FamiTracker's (the problems panel): what the user asks of it goes
						// into the song, whatever the Claude Code setting says
	CWnd *m_pProgress;	// where a rendering's progress shows, or NULL
	int m_iPlayFirst, m_iPlayEnd;	// the rows played for Claude; End at First: to the end
	bool m_bPlayStarted;
	int m_iPlayTicks;
	bool m_bPlayLoop;				// those rows over and over
	std::vector<bool> m_UserMutes;	// the user's own mutes while Claude's playback mutes others, else empty
	std::vector<ftapi::PlayPart> m_PlayParts;	// what Claude asked to hear, one after the other
	size_t m_iPlayPart;				// the one playing
	int m_iReturnTrack;				// selected again when it's over, or -1
};

// The dialog

// This computer's addresses that other computers at home can use: home networks' and Tailscale's (the others'
// own addresses would be public too, and FamiTracker doesn't answer those)
static std::vector<mcp_address_t> HomeAddresses()
{
	const std::vector<mcp_address_t> Found = ListNetworkAddresses();
	std::vector<mcp_address_t> Home;
	for (size_t i = 0; i < Found.size(); ++i) {
		unsigned long Address = 0;
		if (ftapi::ParseIpv4(Found[i].Address, Address) && ftapi::IsPrivateIpv4(Address))
			Home.push_back(Found[i]);
	}
	return Home;
}

class CConnectClaudeDlg : public CDialog
{
public:
	CConnectClaudeDlg(CMcpLink *pLink, CWnd *pParent) : CDialog(IDD_CONNECT_CLAUDE, pParent), m_pLink(pLink) {}

protected:
	virtual BOOL OnInitDialog();
	virtual void OnOK();
	afx_msg void OnChange();
	afx_msg void OnCopy();
	afx_msg void OnNewToken();
	afx_msg void OnNetwork();
	afx_msg void OnCopyAddress(UINT nID);
	afx_msg void OnCopyCode();

	DECLARE_MESSAGE_MAP()

private:
	enum { ADDRESS_ROWS = 4 };	// IDC_MCP_NET_NAME1..4, URL1..4, COPY1..4

	int Port() const;
	bool Wsl() const;
	bool Network() const;
	CString CurrentCommand() const;		// empty while the port isn't valid
	CString AddressUrl(int Row) const;	// what the other computer connects to, by the address on that row
	void ShowCommand();
	void ShowNetwork();					// the other computers' side: their addresses and the access code, when allowed
	bool CopyText(const CString &Text);

	CMcpLink *m_pLink;
	CString m_sToken;
	std::vector<mcp_address_t> m_Addresses;	// this computer's, as the rows show them
};

BEGIN_MESSAGE_MAP(CConnectClaudeDlg, CDialog)
	ON_EN_CHANGE(IDC_MCP_PORT, OnChange)
	ON_BN_CLICKED(IDC_MCP_ENABLE, OnChange)
	ON_BN_CLICKED(IDC_MCP_WINDOWS, OnChange)
	ON_BN_CLICKED(IDC_MCP_WSL, OnChange)
	ON_BN_CLICKED(IDC_MCP_COPY, OnCopy)
	ON_BN_CLICKED(IDC_MCP_NEWTOKEN, OnNewToken)
	ON_BN_CLICKED(IDC_MCP_NETWORK, OnNetwork)
	ON_BN_CLICKED(IDC_MCP_NET_CORE, OnChange)
	ON_CONTROL_RANGE(BN_CLICKED, IDC_MCP_NET_COPY1, IDC_MCP_NET_COPY4, OnCopyAddress)
	ON_BN_CLICKED(IDC_MCP_NET_COPYCODE, OnCopyCode)
END_MESSAGE_MAP()

BOOL CConnectClaudeDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	const mcp_settings_t &Settings = m_pLink->GetSettings();
	m_sToken = Settings.Token;
	m_Addresses = HomeAddresses();
	CheckDlgButton(IDC_MCP_ENABLE, Settings.Enabled ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_MCP_DIRECT, Settings.DirectChanges ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_MCP_NETWORK, Settings.Network ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_MCP_NET_CORE, Settings.CoreTools ? BST_CHECKED : BST_UNCHECKED);
	CheckRadioButton(IDC_MCP_WINDOWS, IDC_MCP_WSL, Settings.Wsl ? IDC_MCP_WSL : IDC_MCP_WINDOWS);
	SetDlgItemInt(IDC_MCP_PORT, static_cast<UINT>(Settings.Port), FALSE);
	static_cast<CEdit*>(GetDlgItem(IDC_MCP_PORT))->SetLimitText(5);
	SetDlgItemText(IDC_MCP_STATUS, m_pLink->Status());
	ShowCommand();
	ShowNetwork();
	return TRUE;
}

int CConnectClaudeDlg::Port() const
{
	BOOL Valid = FALSE;
	const UINT Value = GetDlgItemInt(IDC_MCP_PORT, &Valid, FALSE);
	return (Valid && Value >= 1024 && Value <= 65535) ? static_cast<int>(Value) : 0;
}

bool CConnectClaudeDlg::Wsl() const
{
	return IsDlgButtonChecked(IDC_MCP_WSL) == BST_CHECKED;
}

bool CConnectClaudeDlg::Network() const
{
	return IsDlgButtonChecked(IDC_MCP_NETWORK) == BST_CHECKED;
}

CString CConnectClaudeDlg::AddressUrl(int Row) const
{
	if (Row < 0 || Row >= static_cast<int>(m_Addresses.size()) || Port() == 0)
		return CString();
	return CString(ftapi::NetworkUrl(m_Addresses[static_cast<size_t>(Row)].Address, Port(),
		IsDlgButtonChecked(IDC_MCP_NET_CORE) == BST_CHECKED).c_str());
}

void CConnectClaudeDlg::ShowNetwork()
{
	const bool On = Network();
	const int Rows = On ? std::min<int>(ADDRESS_ROWS, static_cast<int>(m_Addresses.size())) : 0;
	for (int i = 0; i < ADDRESS_ROWS; ++i) {
		const int Show = i < Rows ? SW_SHOW : SW_HIDE;
		GetDlgItem(IDC_MCP_NET_NAME1 + i)->ShowWindow(Show);
		GetDlgItem(IDC_MCP_NET_URL1 + i)->ShowWindow(Show);
		GetDlgItem(IDC_MCP_NET_COPY1 + i)->ShowWindow(Show);
		if (i < Rows) {
			const CString Adapter(m_Addresses[static_cast<size_t>(i)].Adapter.c_str());
			SetDlgItemText(IDC_MCP_NET_NAME1 + i, (Adapter.IsEmpty() ? CString(_T("Network")) : Adapter) + _T(":"));
			const CString Url = AddressUrl(i);
			SetDlgItemText(IDC_MCP_NET_URL1 + i, Url.IsEmpty() ? CString(_T("(Pick a port from 1024 to 65535.)")) : Url);
		}
	}
	const int Code = On ? SW_SHOW : SW_HIDE;
	GetDlgItem(IDC_MCP_NET_CODELABEL)->ShowWindow(Code);
	GetDlgItem(IDC_MCP_NET_CODE)->ShowWindow(Code);
	GetDlgItem(IDC_MCP_NET_COPYCODE)->ShowWindow(Code);
	GetDlgItem(IDC_MCP_NET_CORE)->ShowWindow(Code);
	SetDlgItemText(IDC_MCP_NET_CODE, m_sToken);
	if (!On)
		SetDlgItemText(IDC_MCP_NET_INFO, _T("Off: only programs on this computer can connect. Tick it to use FamiTracker's tools from ")
			_T("another computer at home, like an AI assistant on a Linux PC."));
	else if (Rows == 0)
		SetDlgItemText(IDC_MCP_NET_INFO, _T("This computer has no home network or Tailscale address right now. Connect it to your ")
			_T("network (or start Tailscale), then open this dialog again."));
	else
		SetDlgItemText(IDC_MCP_NET_INFO, _T("On the other computer, the MCP client connects to one of these addresses, with the access ")
			_T("code in the header \"Authorization: Bearer <code>\":"));
	SetDlgItemText(IDC_MCP_NET_HINT, !On ? CString() : CString(_T("Windows asks once whether FamiTracker may use the network: allow it ")
		_T("on private networks. For your home network or Tailscale only: never forward the port to the internet.\r\n\r\n")
		_T("A client that starts its servers over stdio uses famitracker-mcp-linux from the zip instead: ")
		_T("famitracker-mcp-linux --host <address> --token <code> --core. MCP.md shows the setup for Hermes Agent, and how files ")
		_T("travel: send_file puts one into FamiTracker's Inbox, and export_for_game links to the files it saves.")));
}

CString CConnectClaudeDlg::CurrentCommand() const
{
	if (Wsl())
		return m_pLink->WslCommand();	// the helper finds the port by itself
	const int Value = Port();
	return Value > 0 ? m_pLink->Command(Value, m_sToken) : CString();
}

void CConnectClaudeDlg::ShowCommand()
{
	const CString Command = CurrentCommand();
	SetDlgItemText(IDC_MCP_COMMAND, Command.IsEmpty() ? CString(_T("(Pick a port from 1024 to 65535.)")) : Command);
	if (!Wsl()) {
		SetDlgItemText(IDC_MCP_WHERE, _T("Run this in a terminal (PowerShell or Command Prompt):"));
		SetDlgItemText(IDC_MCP_HINT, _T("It needs Claude Code installed. The access code in the command keeps other programs and web pages out. ")
			_T("Then start claude in any folder and ask, for example: \"make the atmosphere darker\" or \"add a bass line\"."));
	}
	else {
		SetDlgItemText(IDC_MCP_WHERE, _T("Run this in your WSL terminal, where you start claude:"));
		if (CMcpLink::HelperFound())
			SetDlgItemText(IDC_MCP_HINT, _T("Claude Code in WSL then starts famitracker-mcp.exe from FamiTracker's folder, which passes its requests ")
				_T("on to FamiTracker here: no network setup, and new access codes work without connecting again. ")
				_T("Then start claude and ask, for example: \"add a bass line\"."));
		else
			SetDlgItemText(IDC_MCP_HINT, _T("famitracker-mcp.exe is missing from FamiTracker's folder. It comes in the same zip as FamiTracker.exe: ")
				_T("put it next to FamiTracker.exe, then run the command."));
	}
}

void CConnectClaudeDlg::OnChange()
{
	ShowCommand();
	ShowNetwork();
}

void CConnectClaudeDlg::OnNetwork()
{
	m_Addresses = HomeAddresses();	// as they are now
	ShowNetwork();
}

bool CConnectClaudeDlg::CopyText(const CString &Text)
{
	const CStringA Bytes(Text);
	if (Bytes.IsEmpty() || !OpenClipboard())
		return false;
	EmptyClipboard();
	bool Copied = false;
	HGLOBAL hMemory = GlobalAlloc(GMEM_MOVEABLE, static_cast<SIZE_T>(Bytes.GetLength() + 1));
	if (hMemory != NULL) {
		char *pTarget = static_cast<char*>(GlobalLock(hMemory));
		if (pTarget != NULL) {
			memcpy(pTarget, static_cast<LPCSTR>(Bytes), static_cast<size_t>(Bytes.GetLength() + 1));
			GlobalUnlock(hMemory);
			Copied = SetClipboardData(CF_TEXT, hMemory) != NULL;
			if (!Copied)
				GlobalFree(hMemory);
		}
		else
			GlobalFree(hMemory);
	}
	CloseClipboard();
	return Copied;
}

void CConnectClaudeDlg::OnCopy()
{
	if (!CopyText(CurrentCommand()))
		return;
	SetDlgItemText(IDC_MCP_STATUS, Wsl() ? _T("Copied. Paste it into your WSL terminal and press Enter.") :
		_T("Copied. Paste it into a terminal and press Enter."));
	m_pLink->RememberWsl(Wsl());
}

void CConnectClaudeDlg::OnCopyAddress(UINT nID)
{
	if (CopyText(AddressUrl(static_cast<int>(nID) - IDC_MCP_NET_COPY1)))
		SetDlgItemText(IDC_MCP_STATUS, _T("Copied the address: put it into the other computer's MCP settings, with the access code."));
}

void CConnectClaudeDlg::OnCopyCode()
{
	if (CopyText(m_sToken))
		SetDlgItemText(IDC_MCP_STATUS, _T("Copied the access code."));
}

void CConnectClaudeDlg::OnNewToken()
{
	m_sToken = NewToken();
	ShowCommand();
	ShowNetwork();
	SetDlgItemText(IDC_MCP_STATUS, Wsl() ? _T("New access code: Claude Code in WSL uses it after OK, without connecting again.") :
		_T("New access code: after OK, connect again with the new command (claude mcp remove famitracker first)."));
}

void CConnectClaudeDlg::OnOK()
{
	mcp_settings_t Settings;
	Settings.Enabled = IsDlgButtonChecked(IDC_MCP_ENABLE) == BST_CHECKED;
	Settings.DirectChanges = IsDlgButtonChecked(IDC_MCP_DIRECT) == BST_CHECKED;
	Settings.Wsl = Wsl();
	Settings.Network = Network();
	Settings.CoreTools = IsDlgButtonChecked(IDC_MCP_NET_CORE) == BST_CHECKED;
	Settings.Port = Port();
	Settings.Token = m_sToken;
	if (Settings.Port == 0) {
		AfxMessageBox(_T("Pick a port from 1024 to 65535."), MB_ICONWARNING);
		GetDlgItem(IDC_MCP_PORT)->SetFocus();
		return;
	}
	CString Error;
	if (!m_pLink->Apply(Settings, Error)) {
		SetDlgItemText(IDC_MCP_STATUS, Error);
		AfxMessageBox(Error, MB_ICONWARNING);
		return;
	}
	CDialog::OnOK();
}

// File > Export for games

class CGameExportDlg : public CDialog
{
public:
	CGameExportDlg(CMcpLink *pLink, CWnd *pParent) : CDialog(IDD_GAME_EXPORT, pParent), m_pLink(pLink), m_bWorking(false) {}

protected:
	virtual BOOL OnInitDialog();
	virtual void OnCancel();
	afx_msg void OnExport();
	afx_msg void OnCopy();
	afx_msg void OnLoudnessChange();

	DECLARE_MESSAGE_MAP()

private:
	CMcpLink *m_pLink;
	bool m_bWorking;
};

BEGIN_MESSAGE_MAP(CGameExportDlg, CDialog)
	ON_BN_CLICKED(IDC_GAME_EXPORT, OnExport)
	ON_BN_CLICKED(IDC_GAME_COPY, OnCopy)
	ON_CBN_SELCHANGE(IDC_GAME_LOUDNESS, OnLoudnessChange)
END_MESSAGE_MAP()

namespace {
const TCHAR GAME_SECTION[] = _T("Game export");
const double QUALITIES[] = { 0.3, 0.6, 0.9 };
const LPCTSTR QUALITY_NAMES[] = { _T("Smaller files"), _T("Normal (for chiptunes)"), _T("Best") };
// The loudness every track is brought to (LUFS, as export_for_game's loudness), or 0 for each one's loudest
// peak at -1 dBFS; the last choice is a number of the user's (kept in tenths of a LUFS)
const int LOUDNESS_CHOICES = 5, LOUDNESS_OTHER = 4;
const double LOUDNESSES[] = { 0.0, -22.0, -20.0, -19.0 };
const LPCTSTR LOUDNESS_NAMES[] = { _T("As loud as it goes (peak at -1 dB)"), _T("Calm place (-22 LUFS)"), _T("Most tracks (-20 LUFS)"),
	_T("Action and bosses (-19 LUFS)"), _T("Other...") };
const double LOUDEST = -6.0, QUIETEST = -40.0;	// the tool's range
}

BOOL CGameExportDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	const bool All = theApp.GetProfileInt(GAME_SECTION, _T("All tracks"), 1) != 0;
	CheckRadioButton(IDC_GAME_THIS, IDC_GAME_ALL, All ? IDC_GAME_ALL : IDC_GAME_THIS);
	CComboBox *pQuality = static_cast<CComboBox*>(GetDlgItem(IDC_GAME_QUALITY));
	for (int i = 0; i < 3; ++i)
		pQuality->AddString(QUALITY_NAMES[i]);
	pQuality->SetCurSel(std::max(0, std::min(2, static_cast<int>(theApp.GetProfileInt(GAME_SECTION, _T("Quality"), 1)))));
	CComboBox *pLoudness = static_cast<CComboBox*>(GetDlgItem(IDC_GAME_LOUDNESS));
	for (int i = 0; i < LOUDNESS_CHOICES; ++i)
		pLoudness->AddString(LOUDNESS_NAMES[i]);
	pLoudness->SetCurSel(std::max(0, std::min(LOUDNESS_CHOICES - 1, static_cast<int>(theApp.GetProfileInt(GAME_SECTION, _T("Loudness"), 0)))));
	const int Tenths = std::max(static_cast<int>(QUIETEST * 10), std::min(static_cast<int>(LOUDEST * 10),
		static_cast<int>(theApp.GetProfileInt(GAME_SECTION, _T("Other loudness"), -180))));
	CString Other;
	Other.Format(Tenths % 10 == 0 ? _T("%.0f") : _T("%.1f"), Tenths / 10.0);
	SetDlgItemText(IDC_GAME_LOUDNESS_OTHER, Other);
	OnLoudnessChange();
	CFamiTrackerDoc *pDoc = static_cast<CFamiTrackerDoc*>(static_cast<CFrameWnd*>(AfxGetMainWnd())->GetActiveDocument());
	CString Text;
	if (pDoc == NULL || pDoc->GetPathName().IsEmpty())
		Text = _T("Save the song first: the OGG files go next to it.");
	else
		Text.Format(_T("The files go next to the song, in %s, named after the tracks (\"%s.ogg\"). Files of those names are replaced.\r\n\r\n")
			_T("Run Claude Code's check_loop first, or listen to where the song jumps back: a note or effect that hangs over the seam ")
			_T("makes the exported loop longer (it then repeats the second time through)."), pDoc->GetPathName().Left(pDoc->GetPathName().ReverseFind(_T('\\'))).GetString(),
			pDoc->GetTrackTitle(0).GetString());
	SetDlgItemText(IDC_GAME_RESULT, Text);
	return TRUE;
}

// The number box beside the loudness, for Other... only
void CGameExportDlg::OnLoudnessChange()
{
	const bool Other = static_cast<CComboBox*>(GetDlgItem(IDC_GAME_LOUDNESS))->GetCurSel() == LOUDNESS_OTHER;
	GetDlgItem(IDC_GAME_LOUDNESS_OTHER)->EnableWindow(Other ? TRUE : FALSE);
	GetDlgItem(IDC_GAME_LOUDNESS_UNIT)->EnableWindow(Other ? TRUE : FALSE);
}

void CGameExportDlg::OnExport()
{
	if (m_bWorking)
		return;
	const bool All = IsDlgButtonChecked(IDC_GAME_ALL) == BST_CHECKED;
	const int Quality = std::max(0, static_cast<CComboBox*>(GetDlgItem(IDC_GAME_QUALITY))->GetCurSel());
	const int Choice = std::max(0, std::min(LOUDNESS_CHOICES - 1, static_cast<CComboBox*>(GetDlgItem(IDC_GAME_LOUDNESS))->GetCurSel()));
	double Loudness = Choice < LOUDNESS_OTHER ? LOUDNESSES[Choice] : 0.0;
	if (Choice == LOUDNESS_OTHER) {
		CString Typed;
		GetDlgItemText(IDC_GAME_LOUDNESS_OTHER, Typed);
		Typed.Trim();
		Typed.Replace(_T(','), _T('.'));
		LPTSTR pEnd = NULL;
		Loudness = _tcstod(Typed, &pEnd);
		if (Typed.IsEmpty() || pEnd == NULL || *pEnd != _T('\0') || !(Loudness >= QUIETEST && Loudness <= LOUDEST)) {
			AfxMessageBox(_T("Type a loudness from -40 to -6 LUFS, like -18 (the higher, the louder)."), MB_ICONWARNING);
			GetDlgItem(IDC_GAME_LOUDNESS_OTHER)->SetFocus();
			return;
		}
		theApp.WriteProfileInt(GAME_SECTION, _T("Other loudness"), static_cast<int>(std::floor(Loudness * 10.0 + 0.5)));
	}
	m_bWorking = true;
	theApp.WriteProfileInt(GAME_SECTION, _T("All tracks"), All ? 1 : 0);
	theApp.WriteProfileInt(GAME_SECTION, _T("Quality"), Quality);
	theApp.WriteProfileInt(GAME_SECTION, _T("Loudness"), Choice);
	const int Buttons[] = { IDC_GAME_EXPORT, IDC_GAME_COPY, IDCANCEL, IDC_GAME_THIS, IDC_GAME_ALL, IDC_GAME_QUALITY, IDC_GAME_LOUDNESS,
		IDC_GAME_LOUDNESS_OTHER, IDC_GAME_LOUDNESS_UNIT };
	const int Count = static_cast<int>(sizeof(Buttons) / sizeof(Buttons[0]));
	for (int i = 0; i < Count; ++i)
		GetDlgItem(Buttons[i])->EnableWindow(FALSE);
	const CString Result = m_pLink->ExportForGames(All, QUALITIES[std::min(Quality, 2)], Loudness, GetDlgItem(IDC_GAME_RESULT));
	for (int i = 0; i < Count; ++i)
		GetDlgItem(Buttons[i])->EnableWindow(TRUE);
	OnLoudnessChange();
	SetDlgItemText(IDC_GAME_RESULT, Result);
	m_bWorking = false;
}

void CGameExportDlg::OnCopy()
{
	CString Text;
	GetDlgItemText(IDC_GAME_RESULT, Text);
	if (Text.IsEmpty() || !OpenClipboard())
		return;
	EmptyClipboard();
	const CStringA Bytes(Text);
	HGLOBAL hMemory = GlobalAlloc(GMEM_MOVEABLE, static_cast<SIZE_T>(Bytes.GetLength() + 1));
	if (hMemory != NULL) {
		char *pTarget = static_cast<char*>(GlobalLock(hMemory));
		if (pTarget != NULL) {
			memcpy(pTarget, static_cast<LPCSTR>(Bytes), static_cast<size_t>(Bytes.GetLength() + 1));
			GlobalUnlock(hMemory);
			if (SetClipboardData(CF_TEXT, hMemory) == NULL)
				GlobalFree(hMemory);
		}
		else
			GlobalFree(hMemory);
	}
	CloseClipboard();
}

void CGameExportDlg::OnCancel()
{
	if (!m_bWorking)
		CDialog::OnCancel();
}

// CMcpConnection

struct CMcpConnection::Impl {
	CMcpLink Link;
};

CMcpConnection::CMcpConnection() : m_pImpl(new Impl())
{
}

CMcpConnection::~CMcpConnection()
{
	delete m_pImpl;
}

void CMcpConnection::Start(CMainFrame *pMainFrame)
{
	m_pImpl->Link.Start(pMainFrame);
}

void CMcpConnection::Stop()
{
	m_pImpl->Link.Shutdown();
}

void CMcpConnection::ShowDialog()
{
	CConnectClaudeDlg Dialog(&m_pImpl->Link, AfxGetMainWnd());
	Dialog.DoModal();
}

void CMcpConnection::ShowGameExport()
{
	CGameExportDlg Dialog(&m_pImpl->Link, AfxGetMainWnd());
	Dialog.DoModal();
}

void CMcpConnection::ShowVoiceEffect()
{
	ShowVoiceEffectDialog(&m_pImpl->Link, AfxGetMainWnd());
}

bool CMcpConnection::RunTool(const char *pName, const std::string &Arguments, std::string &Reply)
{
	return m_pImpl->Link.RunForWindow(pName, Arguments, Reply);
}

bool CMcpConnection::Harmony(ftapi::TrackHarmony &Out)
{
	return m_pImpl->Link.Harmony(Out);
}

bool CMcpConnection::Sounds(int Cursor, std::vector<ftapi::SoundEntry> &Out, std::string &Error)
{
	return m_pImpl->Link.Sounds(Cursor, Out, Error);
}

int CMcpConnection::MakeSound(const ftapi::SoundEntry &Sound, std::string &Error)
{
	return m_pImpl->Link.MakeSound(Sound, Error);
}

bool CMcpConnection::IsBusy() const
{
	return m_pImpl->Link.IsBusy();
}

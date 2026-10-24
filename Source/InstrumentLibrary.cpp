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

// Instruments from the sound presets, and Instrument > Instrument library
// (see InstrumentLibrary.h). The library plays a sound with FamiTracker's own
// engine: the sound goes into the song as an instrument while it's tried
// (one at a time), and comes out again, sequences and all, unless it's kept.

// Standard and API headers come before stdafx.h, whose DEBUG_NEW macro would
// otherwise rewrite the placement new inside the standard library.
#include <algorithm>
#include <string>
#include <vector>
#include "Api/Presets.h"
#include "Api/Wavetables.h"
#include "Api/FdsSounds.h"
#include "stdafx.h"
#include "FamiTracker.h"
#include "FamiTrackerDoc.h"
#include "FamiTrackerView.h"
#include "MainFrm.h"
#include "SoundGen.h"
#include "TrackerChannel.h"
#include "ChannelMap.h"
#include "InstrumentLibrary.h"

int FindNamedInstrument(const CFamiTrackerDoc *pDoc, inst_type_t Type, LPCTSTR Name)
{
	for (int i = 0; i < MAX_INSTRUMENTS; ++i) {
		if (!pDoc->IsInstrumentUsed(static_cast<unsigned int>(i)) || pDoc->GetInstrumentType(static_cast<unsigned int>(i)) != Type)
			continue;
		char Existing[CInstrument::INST_NAME_MAX];
		pDoc->GetInstrumentName(static_cast<unsigned int>(i), Existing);
		Existing[CInstrument::INST_NAME_MAX - 1] = 0;
		if (CString(Name) == Existing)
			return i;
	}
	return -1;
}

int AddPresetInstrument(CFamiTrackerDoc *pDoc, const ftapi::Preset &Sound, bool VRC6, bool Sawtooth, CString &Error, bool Notify)
{
	const inst_type_t Type = VRC6 ? INST_VRC6 : INST_2A03;
	const int ChipType = VRC6 ? SNDCHIP_VRC6 : SNDCHIP_NONE;
	if (VRC6 && !pDoc->ExpansionEnabled(SNDCHIP_VRC6)) {
		Error = _T("This song has no VRC6 channels.");
		return -1;
	}
	const CString Name(Sound.InstrumentName(Sawtooth).c_str());
	const int Existing = FindNamedInstrument(pDoc, Type, Name);
	if (Existing >= 0)
		return Existing;
	// Not CFamiTrackerDoc::AddInstrument(Name, Chip): its instruments' Setup()
	// finds free sequences in the active document, and a new song gets its
	// template's instruments at startup, before there is one. The preset sets
	// the sequences it uses below; the others stay off.
	CInstrument *pNew = theApp.GetChannelMap()->GetChipInstrument(ChipType);
	if (pNew == NULL) {
		Error = _T("FamiTracker has no instruments for that chip.");
		return -1;
	}
	pNew->SetName(Name);
	const int Index = pDoc->AddInstrument(pNew);
	if (Index == INVALID_INSTRUMENT) {
		pNew->Release();
		Error = _T("There are no free instrument slots left (a song has at most 64).");
		return -1;
	}
	CInstrument *pInstrument = pDoc->GetInstrument(static_cast<unsigned int>(Index));	// retained
	const std::vector<ftapi::PresetSequence> &Sequences = VRC6 ? Sound.SequencesVRC6 : Sound.Sequences2A03;
	for (size_t s = 0; s < Sequences.size() && pInstrument != NULL; ++s) {
		const ftapi::PresetSequence &Seq = Sequences[s];
		if (Sawtooth && Seq.Type == ftapi::SEQUENCE_DUTY)
			continue;	// on the sawtooth the duty's low bit adds to the volume, which would never fade out
		const int Slot = VRC6 ? pDoc->GetFreeSequenceVRC6(Seq.Type) : pDoc->GetFreeSequence(Seq.Type);
		if (Slot < 0)
			continue;	// all taken: the sound is plainer
		CSequence *pSequence = pDoc->GetSequence(ChipType, static_cast<unsigned int>(Slot), Seq.Type);
		pSequence->Clear();
		for (size_t k = 0; k < Seq.Items.size(); ++k)
			pSequence->SetItem(static_cast<int>(k), static_cast<signed char>(Seq.Items[k]));
		pSequence->SetItemCount(static_cast<unsigned int>(Seq.Items.size()));
		if (Seq.Loop >= 0)
			pSequence->SetLoopPoint(static_cast<unsigned int>(Seq.Loop));
		if (Seq.Release >= 0)
			pSequence->SetReleasePoint(static_cast<unsigned int>(Seq.Release));
		if (Seq.Type == ftapi::SEQUENCE_ARPEGGIO)
			pSequence->SetSetting(static_cast<unsigned int>(Seq.Setting));
		if (VRC6) {
			static_cast<CInstrumentVRC6*>(pInstrument)->SetSeqEnable(Seq.Type, 1);
			static_cast<CInstrumentVRC6*>(pInstrument)->SetSeqIndex(Seq.Type, Slot);
		}
		else {
			static_cast<CInstrument2A03*>(pInstrument)->SetSeqEnable(Seq.Type, 1);
			static_cast<CInstrument2A03*>(pInstrument)->SetSeqIndex(Seq.Type, Slot);
		}
	}
	if (pInstrument != NULL)
		pInstrument->Release();
	pDoc->SetModifiedFlag();
	if (Notify)
		pDoc->UpdateAllViews(NULL, UPDATE_INSTRUMENT);
	return Index;
}

int AddWaveSoundInstrument(CFamiTrackerDoc *pDoc, const ftapi::WaveSound &Sound, int WavePos, CString &Error)
{
	if (!pDoc->ExpansionEnabled(SNDCHIP_N163)) {
		Error = _T("This song has no N163 channels.");
		return -1;
	}
	const CString Name(Sound.InstrumentName().c_str());
	const int Existing = FindNamedInstrument(pDoc, INST_N163, Name);
	if (Existing >= 0)
		return Existing;
	if (Sound.Waves.empty() || Sound.Waves.size() > static_cast<size_t>(CInstrumentN163::MAX_WAVE_COUNT) || WavePos < 0) {
		Error = _T("That N163 sound can't be made.");
		return -1;
	}
	CInstrument *pNew = theApp.GetChannelMap()->GetChipInstrument(SNDCHIP_N163);	// as for presets: no Setup()
	if (pNew == NULL) {
		Error = _T("FamiTracker has no N163 instruments.");
		return -1;
	}
	pNew->SetName(Name);
	const int Index = pDoc->AddInstrument(pNew);
	if (Index == INVALID_INSTRUMENT) {
		pNew->Release();
		Error = _T("There are no free instrument slots left (a song has at most 64).");
		return -1;
	}
	CInstrumentN163 *pInstrument = static_cast<CInstrumentN163*>(pDoc->GetInstrument(static_cast<unsigned int>(Index)));	// retained
	if (pInstrument != NULL) {
		// The waves, and where they go in the chip's memory
		pInstrument->SetWaveSize(ftapi::N163_WAVE_SIZE);
		pInstrument->SetWaveCount(static_cast<int>(Sound.Waves.size()));
		pInstrument->SetWavePos(WavePos);
		for (size_t w = 0; w < Sound.Waves.size(); ++w)
			for (size_t i = 0; i < Sound.Waves[w].size() && i < static_cast<size_t>(ftapi::N163_WAVE_SIZE); ++i)
				pInstrument->SetSample(static_cast<int>(w), static_cast<int>(i), std::max(0, std::min(15, Sound.Waves[w][i])));
		// Its volume, arpeggio, pitch and which wave plays (the "duty" sequence)
		for (size_t s = 0; s < Sound.Sequences.size(); ++s) {
			const ftapi::PresetSequence &Seq = Sound.Sequences[s];
			const int Slot = pDoc->GetFreeSequenceN163(Seq.Type);
			if (Slot < 0)
				continue;	// all taken: the sound is plainer
			CSequence *pSequence = pDoc->GetSequenceN163(static_cast<unsigned int>(Slot), Seq.Type);
			pSequence->Clear();
			for (size_t k = 0; k < Seq.Items.size(); ++k)
				pSequence->SetItem(static_cast<int>(k), static_cast<signed char>(Seq.Items[k]));
			pSequence->SetItemCount(static_cast<unsigned int>(Seq.Items.size()));
			if (Seq.Loop >= 0)
				pSequence->SetLoopPoint(static_cast<unsigned int>(Seq.Loop));
			if (Seq.Release >= 0)
				pSequence->SetReleasePoint(static_cast<unsigned int>(Seq.Release));
			if (Seq.Type == ftapi::SEQUENCE_ARPEGGIO)
				pSequence->SetSetting(static_cast<unsigned int>(Seq.Setting));
			pInstrument->SetSeqEnable(Seq.Type, 1);
			pInstrument->SetSeqIndex(Seq.Type, Slot);
		}
		pInstrument->Release();
	}
	pDoc->SetModifiedFlag();
	pDoc->UpdateAllViews(NULL, UPDATE_INSTRUMENT);
	return Index;
}

namespace {

// An FDS sequence (kept inside CInstrumentFDS) set to a sound's
void SetFdsSequence(CSequence *pSequence, const ftapi::PresetSequence &Seq)
{
	pSequence->Clear();
	const size_t Count = std::min(Seq.Items.size(), static_cast<size_t>(MAX_SEQUENCE_ITEMS));
	for (size_t k = 0; k < Count; ++k)
		pSequence->SetItem(static_cast<int>(k), static_cast<signed char>(Seq.Items[k]));
	pSequence->SetItemCount(static_cast<unsigned int>(Count));
	if (Seq.Loop >= 0 && static_cast<size_t>(Seq.Loop) < Count)
		pSequence->SetLoopPoint(static_cast<unsigned int>(Seq.Loop));
	if (Seq.Release >= 0 && static_cast<size_t>(Seq.Release) < Count)
		pSequence->SetReleasePoint(static_cast<unsigned int>(Seq.Release));
	if (Seq.Type == ftapi::SEQUENCE_ARPEGGIO)
		pSequence->SetSetting(static_cast<unsigned int>(Seq.Setting));
}

}	// namespace

int AddFdsSoundInstrument(CFamiTrackerDoc *pDoc, const ftapi::FdsSound &Sound, CString &Error, bool Notify)
{
	if (!pDoc->ExpansionEnabled(SNDCHIP_FDS)) {
		Error = _T("This song has no FDS channel.");
		return -1;
	}
	const CString Name(Sound.InstrumentName().c_str());
	const int Existing = FindNamedInstrument(pDoc, INST_FDS, Name);
	if (Existing >= 0)
		return Existing;
	if (Sound.Wave.size() != static_cast<size_t>(CInstrumentFDS::WAVE_SIZE) ||
		Sound.Modulation.size() != static_cast<size_t>(CInstrumentFDS::MOD_SIZE)) {
		Error = _T("That FDS sound can't be made.");
		return -1;
	}
	CInstrument *pNew = theApp.GetChannelMap()->GetChipInstrument(SNDCHIP_FDS);	// as for presets: no Setup()
	if (pNew == NULL) {
		Error = _T("FamiTracker has no FDS instruments.");
		return -1;
	}
	pNew->SetName(Name);
	const int Index = pDoc->AddInstrument(pNew);
	if (Index == INVALID_INSTRUMENT) {
		pNew->Release();
		Error = _T("There are no free instrument slots left (a song has at most 64).");
		return -1;
	}
	CInstrumentFDS *pInstrument = static_cast<CInstrumentFDS*>(pDoc->GetInstrument(static_cast<unsigned int>(Index)));	// retained
	if (pInstrument != NULL) {
		// The wave and the modulator
		for (int i = 0; i < CInstrumentFDS::WAVE_SIZE; ++i)
			pInstrument->SetSample(i, std::max(0, std::min(63, Sound.Wave[static_cast<size_t>(i)])));
		for (int i = 0; i < CInstrumentFDS::MOD_SIZE; ++i)
			pInstrument->SetModulation(i, std::max(0, std::min(7, Sound.Modulation[static_cast<size_t>(i)])));
		pInstrument->SetModulationSpeed(std::max(0, std::min(4095, Sound.ModSpeed)));
		pInstrument->SetModulationDepth(std::max(0, std::min(63, Sound.ModDepth)));
		pInstrument->SetModulationDelay(std::max(0, std::min(255, Sound.ModDelay)));
		// Its volume (0-31), arpeggio and pitch, the sequences an FDS instrument keeps itself
		for (size_t s = 0; s < Sound.Sequences.size(); ++s) {
			const ftapi::PresetSequence &Seq = Sound.Sequences[s];
			CSequence *pSequence = Seq.Type == ftapi::SEQUENCE_VOLUME ? pInstrument->GetVolumeSeq() :
				Seq.Type == ftapi::SEQUENCE_ARPEGGIO ? pInstrument->GetArpSeq() :
				Seq.Type == ftapi::SEQUENCE_PITCH ? pInstrument->GetPitchSeq() : NULL;
			if (pSequence != NULL)
				SetFdsSequence(pSequence, Seq);
		}
		pInstrument->Release();
	}
	pDoc->SetModifiedFlag();
	if (Notify)
		pDoc->UpdateAllViews(NULL, UPDATE_INSTRUMENT);
	return Index;
}

int AddVrc7PatchInstrument(CFamiTrackerDoc *pDoc, int Patch, LPCTSTR Name, CString &Error)
{
	if (!pDoc->ExpansionEnabled(SNDCHIP_VRC7)) {
		Error = _T("This song has no VRC7 channels.");
		return -1;
	}
	if (Patch < 1 || Patch > 15) {
		Error = _T("The VRC7's patches are 1-15.");
		return -1;
	}
	const int Existing = FindNamedInstrument(pDoc, INST_VRC7, Name);
	if (Existing >= 0)
		return Existing;
	const int Index = pDoc->AddInstrument(Name, SNDCHIP_VRC7);
	if (Index == INVALID_INSTRUMENT) {
		Error = _T("There are no free instrument slots left (a song has at most 64).");
		return -1;
	}
	CInstrument *pInstrument = pDoc->GetInstrument(static_cast<unsigned int>(Index));	// retained
	if (pInstrument != NULL) {
		if (pInstrument->GetType() == INST_VRC7)
			static_cast<CInstrumentVRC7*>(pInstrument)->SetPatch(static_cast<unsigned int>(Patch));
		pInstrument->Release();
	}
	pDoc->SetModifiedFlag();
	pDoc->UpdateAllViews(NULL, UPDATE_INSTRUMENT);
	return Index;
}

int AddVrc7CustomInstrument(CFamiTrackerDoc *pDoc, const int *Regs, LPCTSTR Name, CString &Error)
{
	if (!pDoc->ExpansionEnabled(SNDCHIP_VRC7)) {
		Error = _T("This song has no VRC7 channels.");
		return -1;
	}
	const int Existing = FindNamedInstrument(pDoc, INST_VRC7, Name);
	if (Existing >= 0)
		return Existing;
	const int Index = pDoc->AddInstrument(Name, SNDCHIP_VRC7);
	if (Index == INVALID_INSTRUMENT) {
		Error = _T("There are no free instrument slots left (a song has at most 64).");
		return -1;
	}
	CInstrument *pInstrument = pDoc->GetInstrument(static_cast<unsigned int>(Index));	// retained
	if (pInstrument != NULL) {
		if (pInstrument->GetType() == INST_VRC7) {
			CInstrumentVRC7 *pVrc7 = static_cast<CInstrumentVRC7*>(pInstrument);
			pVrc7->SetPatch(0);
			for (int i = 0; i < 8; ++i)
				pVrc7->SetCustomReg(i, static_cast<unsigned int>(Regs[i] & 0xFF));
		}
		pInstrument->Release();
	}
	pDoc->SetModifiedFlag();
	pDoc->UpdateAllViews(NULL, UPDATE_INSTRUMENT);
	return Index;
}

namespace {

// The sequence an instrument plays for a type, -1 if it plays none
int SequenceOf(CFamiTrackerDoc *pDoc, unsigned int Index, int Type)
{
	const inst_type_t Kind = pDoc->GetInstrumentType(Index);
	if (Kind != INST_2A03 && Kind != INST_VRC6)
		return -1;
	CInstrument *pInstrument = pDoc->GetInstrument(Index);	// retained
	if (pInstrument == NULL)
		return -1;
	int Sequence = -1;
	if (Kind == INST_2A03) {
		const CInstrument2A03 *p2A03 = static_cast<CInstrument2A03*>(pInstrument);
		if (p2A03->GetSeqEnable(Type))
			Sequence = p2A03->GetSeqIndex(Type);
	}
	else {
		const CInstrumentVRC6 *pVRC6 = static_cast<CInstrumentVRC6*>(pInstrument);
		if (pVRC6->GetSeqEnable(Type))
			Sequence = pVRC6->GetSeqIndex(Type);
	}
	pInstrument->Release();
	return Sequence;
}

}	// namespace

void RemoveInstrumentAndSequences(CFamiTrackerDoc *pDoc, unsigned int Index)
{
	if (Index >= MAX_INSTRUMENTS || !pDoc->IsInstrumentUsed(Index))
		return;
	const inst_type_t Type = pDoc->GetInstrumentType(Index);
	int Used[SEQ_COUNT];
	for (int s = 0; s < SEQ_COUNT; ++s)
		Used[s] = SequenceOf(pDoc, Index, s);
	pDoc->RemoveInstrument(Index);

	// Its sequences go too, unless another instrument of the chip plays them
	for (int s = 0; s < SEQ_COUNT; ++s) {
		if (Used[s] < 0)
			continue;
		bool Shared = false;
		for (unsigned int i = 0; i < MAX_INSTRUMENTS && !Shared; ++i)
			Shared = pDoc->IsInstrumentUsed(i) && pDoc->GetInstrumentType(i) == Type && SequenceOf(pDoc, i, s) == Used[s];
		if (Shared)
			continue;
		CSequence *pSequence = pDoc->GetSequence(Type == INST_VRC6 ? SNDCHIP_VRC6 : SNDCHIP_NONE, static_cast<unsigned int>(Used[s]), s);
		if (pSequence != NULL)
			pSequence->Clear();
	}
	pDoc->UpdateAllViews(NULL, UPDATE_INSTRUMENT);
}

// ---------------------------------------------------------------------------
// The library
// ---------------------------------------------------------------------------

namespace {

LPCTSTR SECTION = _T("Instrument library");	// registry section
const UINT_PTR TMR_PLAY = 1;
const UINT PLAY_TIMER_MS = 15;

// What the library lists sounds for: the kinds of channel the song has
enum lib_kind_t {
	LIB_PULSE,			// the 2A03's pulses
	LIB_TRIANGLE,
	LIB_NOISE,
	LIB_VRC6_PULSE,
	LIB_SAWTOOTH,
	LIB_MMC5,			// its pulses play 2A03 instruments
	LIB_VRC7,
	LIB_FDS,			// the FDS's wavetable sounds (kept as a number in the registry: new kinds go last)
};

struct lib_target_t {
	lib_kind_t Kind;
	CString Name;		// "Pulse channels (2A03)"
	int Channel;		// the song's first channel of the kind, where it plays to be heard
};

struct lib_sound_t {
	CString Name;		// "Soft lead"; for the VRC7 "Piano"
	CString Group;		// "Lead", "Bass", ...
	CString Description;
	const ftapi::Preset *pPreset;	// NULL for a VRC7 or FDS sound
	const ftapi::FdsSound *pFds;	// an FDS sound, or NULL
	int Patch;			// the VRC7's sound, 1-15
	bool Low;			// a bass: heard low
};

LPCTSTR PresetGroup(const std::string &Name)
{
	if (Name == "soft lead" || Name == "bright lead" || Name == "thin lead" || Name == "singing lead" || Name == "brass")
		return _T("Lead");
	if (Name == "pluck" || Name == "bell")
		return _T("Pluck or bell");
	if (Name == "soft chords" || Name == "warm pad" || Name == "strings" || Name == "organ")
		return _T("Chords or pad");
	if (Name == "bass" || Name == "plucky bass")
		return _T("Bass");
	return _T("Drum");
}

LPCTSTR FdsGroup(const ftapi::FdsSound &Sound)
{
	if (Sound.Low)
		return _T("Bass");
	if (Sound.Name == "glass bell")
		return _T("Pluck or bell");
	if (Sound.Name == "warm organ" || Sound.Name == "swelling strings" || Sound.Name == "choir pad")
		return _T("Chords or pad");
	if (Sound.Name == "underwater wobble" || Sound.Name == "cave drone")
		return _T("Atmosphere");
	return _T("Lead");
}

LPCTSTR PatchGroup(int Patch)
{
	switch (Patch) {
	case 1: case 6: case 9: case 11:
		return _T("Bell");
	case 2: case 10:
		return _T("Pluck or bell");
	case 3:
		return _T("Keys");
	case 8: case 12: case 15:
		return _T("Chords or pad");
	case 13:
		return _T("Bass");
	default:
		return _T("Lead");
	}
}

// The noise pitch a drum sounds best at (as Claude Code and the recorder write them)
int DrumPitch(const std::string &Name)
{
	if (Name == "kick")
		return 0x4;
	if (Name == "snare")
		return 0xA;
	if (Name == "open hi-hat")
		return 0xD;
	if (Name == "crash")
		return 0xC;
	if (Name == "tom")
		return 0x7;
	return 0xE;
}

lib_kind_t KindOfChannel(int ChanID)
{
	switch (ChanID) {
	case CHANID_TRIANGLE:
		return LIB_TRIANGLE;
	case CHANID_NOISE:
		return LIB_NOISE;
	case CHANID_VRC6_PULSE1: case CHANID_VRC6_PULSE2:
		return LIB_VRC6_PULSE;
	case CHANID_VRC6_SAWTOOTH:
		return LIB_SAWTOOTH;
	case CHANID_MMC5_SQUARE1: case CHANID_MMC5_SQUARE2:
		return LIB_MMC5;
	case CHANID_VRC7_CH1: case CHANID_VRC7_CH2: case CHANID_VRC7_CH3:
	case CHANID_VRC7_CH4: case CHANID_VRC7_CH5: case CHANID_VRC7_CH6:
		return LIB_VRC7;
	case CHANID_FDS:
		return LIB_FDS;
	default:
		return LIB_PULSE;
	}
}

// A note for a channel: FamiTracker's note number (C-0 = 0), or a noise pitch
stChanNote MakeNote(int Note, int Instrument)
{
	stChanNote Out;
	memset(&Out, 0, sizeof(Out));
	if (Note == RELEASE || Note == HALT)
		Out.Note = static_cast<unsigned char>(Note);
	else {
		Out.Note = static_cast<unsigned char>(Note % 12 + 1);
		Out.Octave = static_cast<unsigned char>(Note / 12);
	}
	Out.Vol = MAX_VOLUME;
	Out.Instrument = static_cast<unsigned char>(Instrument);
	return Out;
}

struct play_event_t {
	DWORD At;			// ms after Listen
	int Note;			// note number, RELEASE or HALT
};

}	// namespace

class CInstrumentLibraryDlg : public CDialog
{
public:
	CInstrumentLibraryDlg(CMainFrame *pMainFrame);

protected:
	virtual BOOL OnInitDialog();
	virtual void OnOK();
	virtual void OnCancel();
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnDestroy();
	afx_msg void OnTargetChanged();
	afx_msg void OnListen();
	afx_msg void OnAdd();
	afx_msg void OnItemChanged(NMHDR *pNMHDR, LRESULT *pResult);
	afx_msg void OnItemActivate(NMHDR *pNMHDR, LRESULT *pResult);

	DECLARE_MESSAGE_MAP()

private:
	CFamiTrackerDoc *Doc() const;
	const lib_target_t *Target() const;
	const lib_sound_t *Selected() const;
	void FillTargets();
	void FillSounds();
	void ShowSelected();
	int ExistingInstrument(const lib_sound_t &Sound) const;
	int MakeInstrument(const lib_sound_t &Sound, CString &Error);
	void DropTrial();
	void Play(const lib_sound_t &Sound, int Instrument);
	void StopPlaying();
	void SetStatus(LPCTSTR Text);

	CMainFrame *m_pMainFrame;
	CComboBox m_cTarget;
	CListCtrl m_cList;
	std::vector<lib_target_t> m_Targets;
	std::vector<lib_sound_t> m_Sounds;		// for the target picked
	int m_iTrial;							// the instrument a sound is tried in, -1 for none
	int m_iKept;							// instruments added to the song
	bool m_bWasModified;
	bool m_bFilling;

	std::vector<play_event_t> m_Events;		// what Listen plays
	size_t m_iNextEvent;
	DWORD m_dwStart;
	int m_iPlayChannel;						// -1 while nothing plays
	int m_iPlayInstrument;
};

BEGIN_MESSAGE_MAP(CInstrumentLibraryDlg, CDialog)
	ON_WM_TIMER()
	ON_WM_DESTROY()
	ON_CBN_SELCHANGE(IDC_LIB_FOR, OnTargetChanged)
	ON_BN_CLICKED(IDC_LIB_LISTEN, OnListen)
	ON_BN_CLICKED(IDC_LIB_ADD, OnAdd)
	ON_NOTIFY(LVN_ITEMCHANGED, IDC_LIB_LIST, OnItemChanged)
	ON_NOTIFY(NM_DBLCLK, IDC_LIB_LIST, OnItemActivate)
END_MESSAGE_MAP()

CInstrumentLibraryDlg::CInstrumentLibraryDlg(CMainFrame *pMainFrame) :
	CDialog(IDD_INSTRUMENT_LIBRARY, pMainFrame),
	m_pMainFrame(pMainFrame),
	m_iTrial(-1),
	m_iKept(0),
	m_bWasModified(false),
	m_bFilling(false),
	m_iNextEvent(0),
	m_dwStart(0),
	m_iPlayChannel(-1),
	m_iPlayInstrument(MAX_INSTRUMENTS)
{
}

CFamiTrackerDoc *CInstrumentLibraryDlg::Doc() const
{
	return static_cast<CFamiTrackerDoc*>(m_pMainFrame->GetActiveDocument());
}

BOOL CInstrumentLibraryDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	m_cTarget.SubclassDlgItem(IDC_LIB_FOR, this);
	m_cList.SubclassDlgItem(IDC_LIB_LIST, this);
	m_cList.SetExtendedStyle(m_cList.GetExtendedStyle() | LVS_EX_FULLROWSELECT);
	CRect Rect;
	m_cList.GetClientRect(&Rect);
	const int Width = Rect.Width() - ::GetSystemMetrics(SM_CXVSCROLL);
	m_cList.InsertColumn(0, _T("Sound"), LVCFMT_LEFT, Width * 40 / 100);
	m_cList.InsertColumn(1, _T("Kind"), LVCFMT_LEFT, Width * 30 / 100);
	m_cList.InsertColumn(2, _T("In this song"), LVCFMT_LEFT, Width - Width * 40 / 100 - Width * 30 / 100);

	m_bWasModified = Doc()->IsModified() != FALSE;
	FillTargets();
	FillSounds();
	SetStatus(_T("Click a sound to hear it. Add to the song keeps it as an instrument; you can change it later in the instrument editor."));
	SetTimer(TMR_PLAY, PLAY_TIMER_MS, NULL);
	return TRUE;
}

void CInstrumentLibraryDlg::FillTargets()
{
	CFamiTrackerDoc *pDoc = Doc();
	CFamiTrackerView *pView = CFamiTrackerView::GetView();
	static const struct { lib_kind_t Kind; LPCTSTR Name; } KINDS[] = {
		{ LIB_PULSE, _T("Pulse channels (2A03)") },
		{ LIB_TRIANGLE, _T("Triangle (2A03): bass") },
		{ LIB_NOISE, _T("Noise (2A03): drums") },
		{ LIB_VRC6_PULSE, _T("VRC6 pulses") },
		{ LIB_SAWTOOTH, _T("VRC6 sawtooth: bass") },
		{ LIB_MMC5, _T("MMC5 pulses") },
		{ LIB_VRC7, _T("VRC7: FM sounds") },
		{ LIB_FDS, _T("FDS: wavetable sounds") },
	};
	m_Targets.clear();
	const int Channels = static_cast<int>(pDoc->GetAvailableChannels());
	for (size_t k = 0; k < sizeof(KINDS) / sizeof(KINDS[0]); ++k) {
		for (int c = 0; c < Channels; ++c) {
			const int ID = pDoc->GetChannelType(static_cast<unsigned int>(c));
			if (ID == CHANID_DPCM || (ID >= CHANID_N163_CHAN1 && ID <= CHANID_N163_CHAN8) ||
				ID == CHANID_MMC5_VOICE || ID >= CHANID_S5B_CH1 || KindOfChannel(ID) != KINDS[k].Kind)
				continue;
			lib_target_t Target;
			Target.Kind = KINDS[k].Kind;
			Target.Name = KINDS[k].Name;
			Target.Channel = c;
			m_Targets.push_back(Target);
			break;
		}
	}

	// The kind of channel the cursor is on, or the one picked last time
	int Pick = 0;
	const int Cursor = pView != NULL ? static_cast<int>(pView->GetSelectedChannel()) : -1;
	const lib_kind_t Wanted = (Cursor >= 0 && Cursor < Channels) ? KindOfChannel(pDoc->GetChannelType(static_cast<unsigned int>(Cursor))) :
		static_cast<lib_kind_t>(theApp.GetProfileInt(SECTION, _T("Kind"), LIB_PULSE));
	m_cTarget.ResetContent();
	for (size_t t = 0; t < m_Targets.size(); ++t) {
		m_cTarget.AddString(m_Targets[t].Name);
		if (m_Targets[t].Kind == Wanted)
			Pick = static_cast<int>(t);
	}
	m_cTarget.SetCurSel(Pick);
}

const lib_target_t *CInstrumentLibraryDlg::Target() const
{
	const int Item = m_cTarget.GetCurSel();
	return (Item >= 0 && Item < static_cast<int>(m_Targets.size())) ? &m_Targets[static_cast<size_t>(Item)] : NULL;
}

const lib_sound_t *CInstrumentLibraryDlg::Selected() const
{
	const int Item = m_cList.GetNextItem(-1, LVNI_SELECTED);
	return (Item >= 0 && Item < static_cast<int>(m_Sounds.size())) ? &m_Sounds[static_cast<size_t>(Item)] : NULL;
}

void CInstrumentLibraryDlg::FillSounds()
{
	m_bFilling = true;
	m_cList.DeleteAllItems();
	m_Sounds.clear();
	const lib_target_t *pTarget = Target();
	if (pTarget != NULL) {
		if (pTarget->Kind == LIB_VRC7) {
			const std::vector<ftapi::Vrc7Patch> &Patches = ftapi::Vrc7Patches();
			for (size_t i = 0; i < Patches.size(); ++i) {
				lib_sound_t Sound;
				Sound.Name = Patches[i].InstrumentName().c_str();
				Sound.Group = PatchGroup(Patches[i].Number);
				Sound.Description = Patches[i].Description.c_str();
				Sound.pPreset = NULL;
				Sound.pFds = NULL;
				Sound.Patch = Patches[i].Number;
				Sound.Low = Patches[i].Number == 13;
				m_Sounds.push_back(Sound);
			}
		}
		else if (pTarget->Kind == LIB_FDS) {
			const std::vector<ftapi::FdsSound> &Sounds = ftapi::FdsSounds();
			for (size_t i = 0; i < Sounds.size(); ++i) {
				lib_sound_t Sound;
				Sound.Name = Sounds[i].InstrumentName().c_str();
				Sound.Group = FdsGroup(Sounds[i]);
				Sound.Description = Sounds[i].Description.c_str();
				Sound.pPreset = NULL;
				Sound.pFds = &Sounds[i];
				Sound.Patch = 0;
				Sound.Low = Sounds[i].Low;
				m_Sounds.push_back(Sound);
			}
		}
		else {
			const std::vector<ftapi::Preset> &Presets = ftapi::Presets();
			for (size_t i = 0; i < Presets.size(); ++i) {
				if (Presets[i].Drum != (pTarget->Kind == LIB_NOISE))
					continue;
				lib_sound_t Sound;
				Sound.Name = Presets[i].InstrumentName(pTarget->Kind == LIB_SAWTOOTH).c_str();
				Sound.Group = PresetGroup(Presets[i].Name);
				Sound.Description = Presets[i].Description.c_str();
				Sound.pPreset = &Presets[i];
				Sound.pFds = NULL;
				Sound.Patch = 0;
				Sound.Low = Sound.Group == _T("Bass");
				m_Sounds.push_back(Sound);
			}
		}
	}
	for (size_t i = 0; i < m_Sounds.size(); ++i) {
		const int Item = m_cList.InsertItem(static_cast<int>(i), m_Sounds[i].Name);
		m_cList.SetItemText(Item, 1, m_Sounds[i].Group);
		const int Existing = ExistingInstrument(m_Sounds[i]);
		CString In;
		if (Existing >= 0 && Existing != m_iTrial)
			In.Format(_T("yes (%02X)"), Existing);
		m_cList.SetItemText(Item, 2, In);
	}
	m_bFilling = false;
	ShowSelected();
}

void CInstrumentLibraryDlg::ShowSelected()
{
	const lib_sound_t *pSound = Selected();
	const lib_target_t *pTarget = Target();
	CString Text;
	if (pSound != NULL && pTarget != NULL) {
		CString Where(Doc()->GetChannel(pTarget->Channel) != NULL ? Doc()->GetChannel(pTarget->Channel)->GetChannelName() : _T(""));
		Text.Format(_T("%s: %s. Listen plays it on %s."), static_cast<LPCTSTR>(pSound->Name), static_cast<LPCTSTR>(pSound->Description),
			static_cast<LPCTSTR>(Where));
	}
	else
		Text = _T("Pick a sound in the list.");
	SetDlgItemText(IDC_LIB_TEXT, Text);
	GetDlgItem(IDC_LIB_LISTEN)->EnableWindow(pSound != NULL ? TRUE : FALSE);
	GetDlgItem(IDC_LIB_ADD)->EnableWindow(pSound != NULL ? TRUE : FALSE);
}

int CInstrumentLibraryDlg::ExistingInstrument(const lib_sound_t &Sound) const
{
	const lib_target_t *pTarget = Target();
	if (pTarget == NULL)
		return -1;
	inst_type_t Type = INST_2A03;
	if (pTarget->Kind == LIB_VRC7)
		Type = INST_VRC7;
	else if (pTarget->Kind == LIB_FDS)
		Type = INST_FDS;
	else if (pTarget->Kind == LIB_VRC6_PULSE || pTarget->Kind == LIB_SAWTOOTH)
		Type = INST_VRC6;
	return FindNamedInstrument(Doc(), Type, Sound.Name);
}

int CInstrumentLibraryDlg::MakeInstrument(const lib_sound_t &Sound, CString &Error)
{
	const lib_target_t *pTarget = Target();
	if (pTarget == NULL)
		return -1;
	if (Sound.pFds != NULL)
		return AddFdsSoundInstrument(Doc(), *Sound.pFds, Error);
	if (Sound.pPreset == NULL)
		return AddVrc7PatchInstrument(Doc(), Sound.Patch, Sound.Name, Error);
	const bool VRC6 = pTarget->Kind == LIB_VRC6_PULSE || pTarget->Kind == LIB_SAWTOOTH;
	return AddPresetInstrument(Doc(), *Sound.pPreset, VRC6, pTarget->Kind == LIB_SAWTOOTH, Error);
}

// The sound tried last goes out of the song again
void CInstrumentLibraryDlg::DropTrial()
{
	if (m_iTrial < 0)
		return;
	StopPlaying();
	RemoveInstrumentAndSequences(Doc(), static_cast<unsigned int>(m_iTrial));
	m_iTrial = -1;
}

void CInstrumentLibraryDlg::OnListen()
{
	const lib_sound_t *pSound = Selected();
	if (pSound == NULL)
		return;
	int Instrument = ExistingInstrument(*pSound);
	if (Instrument < 0) {
		DropTrial();
		CString Error;
		Instrument = MakeInstrument(*pSound, Error);
		if (Instrument < 0) {
			SetStatus(Error.IsEmpty() ? CString(_T("The sound couldn't be tried.")) : Error);
			return;
		}
		m_iTrial = Instrument;
	}
	Play(*pSound, Instrument);
}

void CInstrumentLibraryDlg::Play(const lib_sound_t &Sound, int Instrument)
{
	const lib_target_t *pTarget = Target();
	if (pTarget == NULL)
		return;
	StopPlaying();
	if (theApp.IsPlaying())
		theApp.StopPlayer();	// the song would play over it

	// A few notes: up and back for a melody, low for a bass, a beat for a drum
	m_Events.clear();
	if (pTarget->Kind == LIB_NOISE) {
		const int Pitch = DrumPitch(Sound.pPreset != NULL ? Sound.pPreset->Name : std::string());
		static const DWORD HITS[] = { 0, 300, 600, 750, 900 };
		for (size_t i = 0; i < sizeof(HITS) / sizeof(HITS[0]); ++i) {
			play_event_t Hit = { HITS[i], Pitch };
			m_Events.push_back(Hit);
		}
		play_event_t End = { 1500, HALT };
		m_Events.push_back(End);
	}
	else {
		static const int MELODY[] = { 48, 52, 55, 60, 55, 60 };		// C-4 E-4 G-4 C-5 G-4 C-5
		static const int BASS[] = { 36, 43, 46, 48, 43, 36 };		// C-3 G-3 A#3 C-4 G-3 C-3
		static const DWORD STEP[] = { 0, 220, 440, 660, 880, 1100 };
		const bool Low = Sound.Low || pTarget->Kind == LIB_TRIANGLE || pTarget->Kind == LIB_SAWTOOTH;
		const int *pNotes = Low ? BASS : MELODY;
		// The FDS sounds two octaves under the note written: the melody goes up two, where a pulse would play
		// it, and a bass one, where the triangle plays it
		const int Lift = pTarget->Kind == LIB_FDS ? (Low ? 12 : 24) : 0;
		for (size_t i = 0; i < 6; ++i) {
			play_event_t Note = { STEP[i], pNotes[i] + Lift };
			m_Events.push_back(Note);
		}
		play_event_t Release = { 1700, RELEASE };
		m_Events.push_back(Release);
		play_event_t End = { 2600, HALT };
		m_Events.push_back(End);
	}
	m_iNextEvent = 0;
	m_dwStart = ::GetTickCount();
	m_iPlayChannel = pTarget->Channel;
	m_iPlayInstrument = Instrument;
	OnTimer(TMR_PLAY);
}

void CInstrumentLibraryDlg::StopPlaying()
{
	if (m_iPlayChannel < 0)
		return;
	stChanNote Note = MakeNote(HALT, MAX_INSTRUMENTS);
	theApp.GetSoundGenerator()->QueueNote(m_iPlayChannel, Note, NOTE_PRIO_2);
	m_iPlayChannel = -1;
	m_Events.clear();
	m_iNextEvent = 0;
}

void CInstrumentLibraryDlg::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent != TMR_PLAY) {
		CDialog::OnTimer(nIDEvent);
		return;
	}
	if (m_iPlayChannel < 0)
		return;
	const DWORD Now = ::GetTickCount() - m_dwStart;
	while (m_iNextEvent < m_Events.size() && m_Events[m_iNextEvent].At <= Now) {
		stChanNote Note = MakeNote(m_Events[m_iNextEvent].Note, m_iPlayInstrument);
		theApp.GetSoundGenerator()->QueueNote(m_iPlayChannel, Note, NOTE_PRIO_2);
		++m_iNextEvent;
	}
	if (m_iNextEvent >= m_Events.size()) {
		m_iPlayChannel = -1;
		m_Events.clear();
		m_iNextEvent = 0;
	}
}

void CInstrumentLibraryDlg::OnTargetChanged()
{
	DropTrial();
	const lib_target_t *pTarget = Target();
	if (pTarget != NULL)
		theApp.WriteProfileInt(SECTION, _T("Kind"), pTarget->Kind);
	FillSounds();
}

void CInstrumentLibraryDlg::OnItemChanged(NMHDR *pNMHDR, LRESULT *pResult)
{
	const NMLISTVIEW *pList = reinterpret_cast<NMLISTVIEW*>(pNMHDR);
	*pResult = 0;
	if (m_bFilling || (pList->uChanged & LVIF_STATE) == 0)
		return;
	const bool Now = (pList->uNewState & LVIS_SELECTED) != 0, Before = (pList->uOldState & LVIS_SELECTED) != 0;
	if (Now == Before)
		return;
	ShowSelected();
	if (Now)
		OnListen();		// a click plays it
}

void CInstrumentLibraryDlg::OnItemActivate(NMHDR *pNMHDR, LRESULT *pResult)
{
	*pResult = 0;
	OnListen();
}

// Enter
void CInstrumentLibraryDlg::OnOK()
{
	OnAdd();
}

void CInstrumentLibraryDlg::OnAdd()
{
	const lib_sound_t *pSound = Selected();
	if (pSound == NULL)
		return;
	CString Text;
	int Instrument = ExistingInstrument(*pSound);
	if (Instrument >= 0 && Instrument != m_iTrial)
		Text.Format(_T("\"%s\" is in the song already, as instrument %02X: it's picked in the instrument list now."),
			static_cast<LPCTSTR>(pSound->Name), Instrument);
	else {
		if (Instrument < 0) {
			CString Error;
			Instrument = MakeInstrument(*pSound, Error);
			if (Instrument < 0) {
				SetStatus(Error);
				return;
			}
		}
		if (Instrument == m_iTrial)
			m_iTrial = -1;	// kept
		++m_iKept;
		Text.Format(_T("Added \"%s\" as instrument %02X and picked it: type notes with it, or draw them in the piano roll."),
			static_cast<LPCTSTR>(pSound->Name), Instrument);
	}
	m_pMainFrame->SelectInstrument(Instrument);
	const int Item = m_cList.GetNextItem(-1, LVNI_SELECTED);
	CString In;
	In.Format(_T("yes (%02X)"), Instrument);
	if (Item >= 0)
		m_cList.SetItemText(Item, 2, In);
	SetStatus(Text);
}

void CInstrumentLibraryDlg::OnCancel()
{
	CDialog::OnCancel();
}

void CInstrumentLibraryDlg::OnDestroy()
{
	KillTimer(TMR_PLAY);
	StopPlaying();
	DropTrial();
	// Only trying sounds leaves the song as it was
	if (m_iKept == 0 && !m_bWasModified)
		Doc()->SetModifiedFlag(FALSE);
	CDialog::OnDestroy();
}

void CInstrumentLibraryDlg::SetStatus(LPCTSTR Text)
{
	SetDlgItemText(IDC_LIB_STATUS, Text);
}

void ShowInstrumentLibrary(CMainFrame *pMainFrame)
{
	CFamiTrackerDoc *pDoc = static_cast<CFamiTrackerDoc*>(pMainFrame->GetActiveDocument());
	if (pDoc == NULL || !pDoc->IsFileLoaded())
		return;
	CInstrumentLibraryDlg Dialog(pMainFrame);
	Dialog.DoModal();
}

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

// Instruments from the sound presets (see InstrumentLibrary.h), for Claude
// Code's tools and Instrument > Sound browser (SoundBrowserDlg.cpp), which tries
// a sound as an instrument and takes it out again with
// RemoveInstrumentAndSequences unless it's kept.

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

namespace {

// The chip whose sequences an instrument type plays (2A03, VRC6 and 5B instruments)
int SequenceChip(inst_type_t Type)
{
	return Type == INST_VRC6 ? SNDCHIP_VRC6 : (Type == INST_S5B ? SNDCHIP_S5B : SNDCHIP_NONE);
}

// Turns on an instrument's sequence of a type (2A03, VRC6 and 5B instruments)
void UseSequence(CInstrument *pInstrument, int Type, int Slot)
{
	switch (pInstrument->GetType()) {
	case INST_VRC6:
		static_cast<CInstrumentVRC6*>(pInstrument)->SetSeqEnable(Type, 1);
		static_cast<CInstrumentVRC6*>(pInstrument)->SetSeqIndex(Type, Slot);
		break;
	case INST_S5B:
		static_cast<CInstrumentS5B*>(pInstrument)->SetSeqEnable(Type, 1);
		static_cast<CInstrumentS5B*>(pInstrument)->SetSeqIndex(Type, Slot);
		break;
	case INST_2A03:
		static_cast<CInstrument2A03*>(pInstrument)->SetSeqEnable(Type, 1);
		static_cast<CInstrument2A03*>(pInstrument)->SetSeqIndex(Type, Slot);
		break;
	default:
		break;
	}
}

// An instrument of Type (2A03, VRC6 or 5B) named Name playing Sequences, or the
// one of that type and name already there
int AddSequenceInstrument(CFamiTrackerDoc *pDoc, inst_type_t Type, const CString &Name,
	const std::vector<ftapi::PresetSequence> &Sequences, bool Sawtooth, CString &Error, bool Notify)
{
	const int ChipType = SequenceChip(Type);
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
	for (size_t s = 0; s < Sequences.size() && pInstrument != NULL; ++s) {
		const ftapi::PresetSequence &Seq = Sequences[s];
		if (Sawtooth && Seq.Type == ftapi::SEQUENCE_DUTY)
			continue;	// on the sawtooth the duty's low bit adds to the volume, which would never fade out
		const int Slot = Type == INST_VRC6 ? pDoc->GetFreeSequenceVRC6(Seq.Type) :
			(Type == INST_S5B ? pDoc->GetFreeSequenceS5B(Seq.Type) : pDoc->GetFreeSequence(Seq.Type));
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
		UseSequence(pInstrument, Seq.Type, Slot);
	}
	if (pInstrument != NULL)
		pInstrument->Release();
	pDoc->SetModifiedFlag();
	if (Notify)
		pDoc->UpdateAllViews(NULL, UPDATE_INSTRUMENT);
	return Index;
}

}	// namespace

int AddPresetInstrument(CFamiTrackerDoc *pDoc, const ftapi::Preset &Sound, bool VRC6, bool Sawtooth, CString &Error, bool Notify)
{
	if (VRC6 && !pDoc->ExpansionEnabled(SNDCHIP_VRC6)) {
		Error = _T("This song has no VRC6 channels.");
		return -1;
	}
	return AddSequenceInstrument(pDoc, VRC6 ? INST_VRC6 : INST_2A03, CString(Sound.InstrumentName(Sawtooth).c_str()),
		VRC6 ? Sound.SequencesVRC6 : Sound.Sequences2A03, Sawtooth, Error, Notify);
}

int AddPreset5BInstrument(CFamiTrackerDoc *pDoc, const ftapi::Preset &Sound, CString &Error, bool Notify)
{
	if (!pDoc->ExpansionEnabled(SNDCHIP_S5B)) {
		Error = _T("This song has no 5B channels.");
		return -1;
	}
	return AddSequenceInstrument(pDoc, INST_S5B, CString(Sound.InstrumentName(false).c_str()), ftapi::Preset5BSequences(Sound),
		false, Error, Notify);
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
	if (Kind != INST_2A03 && Kind != INST_VRC6 && Kind != INST_S5B)
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
	else if (Kind == INST_S5B) {
		const CInstrumentS5B *p5B = static_cast<CInstrumentS5B*>(pInstrument);
		if (p5B->GetSeqEnable(Type))
			Sequence = p5B->GetSeqIndex(Type);
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
		CSequence *pSequence = pDoc->GetSequence(SequenceChip(Type), static_cast<unsigned int>(Used[s]), s);
		if (pSequence != NULL)
			pSequence->Clear();
	}
	pDoc->UpdateAllViews(NULL, UPDATE_INSTRUMENT);
}

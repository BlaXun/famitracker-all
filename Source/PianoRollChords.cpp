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

// The piano roll's chord helper on the document (see PianoRollChords.h)

// Standard and API headers come before stdafx.h, whose DEBUG_NEW macro would
// otherwise rewrite the placement new inside the standard library.
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
#include "Api/ChordHelper.h"
#include "Api/FdsSounds.h"
#include "Api/Presets.h"
#include "Api/Wavetables.h"
#include "stdafx.h"
#include "FamiTracker.h"
#include "FamiTrackerDoc.h"
#include "FamiTrackerView.h"
#include "MainFrm.h"
#include "InstrumentLibrary.h"
#include "InsertRecording.h"
#include "PianoRollChords.h"

namespace {

// The chip of a channel (SNDCHIP_*) by the helper's name for it
std::string ChipName(int Chip)
{
	switch (Chip) {
	case SNDCHIP_VRC6: return "VRC6";
	case SNDCHIP_VRC7: return "VRC7";
	case SNDCHIP_FDS: return "FDS";
	case SNDCHIP_MMC5: return "MMC5";
	case SNDCHIP_N163: return "N163";
	case SNDCHIP_S5B: return "5B";
	}
	return "2A03";
}

// What a channel plays, as the Claude Code tools say it (McpConnection.cpp's KindOf)
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

std::string InstrumentChip(inst_type_t Type)
{
	switch (Type) {
	case INST_VRC6: return "VRC6";
	case INST_VRC7: return "VRC7";
	case INST_FDS: return "FDS";
	case INST_N163: return "N163";
	case INST_S5B: return "5B";
	default: return "2A03";
	}
}

ftapi::Cell CellOf(const stChanNote &In)
{
	ftapi::Cell C;
	C.Note = In.Note;
	C.Octave = In.Octave;
	C.Instrument = In.Instrument;
	C.Volume = In.Vol;
	for (int k = 0; k < MAX_EFFECT_COLUMNS && k < ftapi::EFFECT_COLUMNS; ++k) {
		C.Effect[k] = In.EffNumber[k];
		C.Param[k] = In.EffParam[k];
	}
	return C;
}

stChanNote NoteOf(const ftapi::Cell &In)
{
	stChanNote Out;
	memset(&Out, 0, sizeof(Out));
	Out.Note = In.Note;
	Out.Octave = In.Octave;
	Out.Instrument = In.Instrument;
	Out.Vol = In.Volume;
	for (int k = 0; k < MAX_EFFECT_COLUMNS && k < ftapi::EFFECT_COLUMNS; ++k) {
		Out.EffNumber[k] = In.Effect[k];
		Out.EffParam[k] = In.Param[k];
	}
	return Out;
}

void ReadInstruments(const CFamiTrackerDoc *pDoc, std::vector<ftapi::HelperInstrument> &Out)
{
	Out.clear();
	for (unsigned int i = 0; i < MAX_INSTRUMENTS; ++i) {
		if (!pDoc->IsInstrumentUsed(i))
			continue;
		ftapi::HelperInstrument I;
		I.Index = static_cast<int>(i);
		char Name[CInstrument::INST_NAME_MAX] = "";
		pDoc->GetInstrumentName(i, Name);
		Name[CInstrument::INST_NAME_MAX - 1] = 0;
		I.Name = Name;
		I.Chip = InstrumentChip(pDoc->GetInstrumentType(i));
		CInstrument *pInstrument = pDoc->GetInstrument(i);	// retained
		if (pInstrument != NULL) {
			I.Releases = pInstrument->CanRelease();
			if (pInstrument->GetType() == INST_N163) {
				const CInstrumentN163 *pN163 = static_cast<const CInstrumentN163*>(pInstrument);
				I.WavePos = pN163->GetWavePos();
				I.WaveSize = pN163->GetWaveSize();
			}
			pInstrument->Release();
		}
		Out.push_back(I);
	}
}

// A voice's sound as an instrument: the song's, or one made for it
int MakeSound(CFamiTrackerDoc *pDoc, const ftapi::HelperChannel &Channel, const ftapi::VoiceSound &Sound, CString &Error)
{
	if (Sound.Instrument >= 0)
		return Sound.Instrument;
	if (Channel.Chip == "N163") {
		// Its own place in the wave memory, and its own name when it's another string pad
		const ftapi::WaveSound *pBase = ftapi::FindWaveSound(Sound.Sound);
		if (pBase == NULL || Sound.WavePos < 0) {
			Error = _T("There's no room in the N163's wave memory for another sound.");
			return -1;
		}
		ftapi::WaveSound Copy = *pBase;
		if (!Sound.Name.empty())
			Copy.Name = ftapi::Lowercase(Sound.Name);	// InstrumentName() writes it "String pad 2"
		return AddWaveSoundInstrument(pDoc, Copy, Sound.WavePos, Error);
	}
	if (Channel.Kind == ftapi::KIND_FM) {
		const ftapi::Vrc7Patch *pPatch = ftapi::FindVrc7Patch(Sound.Sound);
		if (pPatch != NULL)
			return AddVrc7PatchInstrument(pDoc, pPatch->Number, CString(pPatch->InstrumentName().c_str()), Error);
	}
	else if (Channel.Chip == "FDS") {
		const ftapi::FdsSound *pSound = ftapi::VoiceFdsSound(Sound.Sound);
		if (pSound != NULL)
			return AddFdsSoundInstrument(pDoc, *pSound, Error);
	}
	else if (Channel.Kind == ftapi::KIND_SQUARE) {
		const ftapi::Preset *pSound = ftapi::FindSoundFor5B(Sound.Sound);
		if (pSound != NULL)
			return AddPreset5BInstrument(pDoc, *pSound, Error);
	}
	else {
		const ftapi::Preset *pPreset = ftapi::FindPreset(Sound.Sound);
		if (pPreset != NULL)
			return AddPresetInstrument(pDoc, *pPreset, Channel.Chip == "VRC6", Channel.Kind == ftapi::KIND_SAWTOOTH, Error);
	}
	Error = _T("There's no sound for chords on ") + CString(Channel.Name.c_str()) + _T(".");
	return -1;
}

}	// namespace

void ReadChordSong(const CFamiTrackerDoc *pDoc, unsigned int Track, const ftapi::Key &K, bool HasKey, ftapi::HelperSong &Out)
{
	Out = ftapi::HelperSong();
	Out.PatternLength = static_cast<int>(std::max(1U, pDoc->GetPatternLength(Track)));
	Out.Frames = static_cast<int>(std::max(1U, pDoc->GetFrameCount(Track)));
	Out.FirstHighlight = pDoc->GetFirstHighlight();
	Out.SecondHighlight = pDoc->GetSecondHighlight();
	Out.K = K;
	Out.HasKey = HasKey;
	Out.N163Channels = pDoc->ExpansionEnabled(SNDCHIP_N163) ? static_cast<int>(pDoc->GetNamcoChannels()) : 0;
	const int Channels = static_cast<int>(pDoc->GetAvailableChannels());
	for (int c = 0; c < Channels; ++c) {
		ftapi::HelperChannel Channel;
		Channel.Name = static_cast<LPCSTR>(CStringA(RecordingChannelName(pDoc, c)));
		Channel.Chip = ChipName(pDoc->GetChipType(c));
		Channel.Kind = KindOf(pDoc->GetChannelType(c));
		Channel.Soft = Channel.Chip == "N163" && pDoc->IsVrc6PlusChannel(c);
		Channel.EffectColumns = static_cast<int>(pDoc->GetEffColumns(Track, static_cast<unsigned int>(c))) + 1;
		Channel.Cells.reserve(static_cast<size_t>(Out.Rows()));
		for (int f = 0; f < Out.Frames; ++f)
			for (int r = 0; r < Out.PatternLength; ++r) {
				stChanNote Cell;
				pDoc->GetNoteData(Track, static_cast<unsigned int>(f), static_cast<unsigned int>(c), static_cast<unsigned int>(r), &Cell);
				Channel.Cells.push_back(CellOf(Cell));
			}
		Out.Channels.push_back(Channel);
	}
	ReadInstruments(pDoc, Out.Instruments);
}

bool WriteChordPlan(CMainFrame *pMainFrm, ftapi::HelperSong &Song, const ftapi::ChordPlan &Plan, int CursorRow, CString &Sounds,
	CString &Message)
{
	CFamiTrackerView *pView = static_cast<CFamiTrackerView*>(pMainFrm->GetActiveView());
	CFamiTrackerDoc *pDoc = pView->GetDocument();
	if (Plan.Voices.empty()) {
		Message = _T("There's nothing to write.");
		return false;
	}

	// The voices' sounds, made where the song has none yet
	std::vector<int> Instruments;
	for (size_t v = 0; v < Plan.Voices.size(); ++v) {
		const ftapi::ChordVoice &Voice = Plan.Voices[v];
		if (Voice.Channel < 0 || Voice.Channel >= static_cast<int>(Song.Channels.size())) {
			Message = _T("The song's channels changed: try again.");
			return false;
		}
		CString Error;
		const int Index = MakeSound(pDoc, Song.Channels[static_cast<size_t>(Voice.Channel)], Voice.Sound, Error);
		if (Index < 0) {
			Message = Error.IsEmpty() ? CString(_T("The voices' instrument couldn't be made.")) : Error;
			return false;
		}
		Instruments.push_back(Index);
	}
	ReadInstruments(pDoc, Song.Instruments);	// the new ones' releases
	std::vector<std::string> Names;
	for (size_t v = 0; v < Instruments.size(); ++v) {
		char Name[CInstrument::INST_NAME_MAX] = "";
		pDoc->GetInstrumentName(static_cast<unsigned int>(Instruments[v]), Name);
		Name[CInstrument::INST_NAME_MAX - 1] = 0;
		if (std::find(Names.begin(), Names.end(), std::string(Name)) == Names.end())
			Names.push_back(Name);
	}
	Sounds.Empty();
	for (size_t i = 0; i < Names.size(); ++i) {
		if (i > 0)
			Sounds += i + 1 == Names.size() ? _T(" and ") : _T(", ");
		Sounds += Names[i].c_str();
	}

	// The voices, as one undoable action
	std::vector<std::string> Warnings;
	const std::vector<ftapi::ChannelCells> Parts = ftapi::PlanCells(Song, Plan, Instruments, Warnings);
	if (Parts.empty()) {
		Message = _T("There's nothing to write.");
		return false;
	}
	pMainFrm->BeginActionGroup();
	int Written = 0;
	CString Error;
	bool Failed = false;
	for (size_t p = 0; p < Parts.size() && !Failed; ++p) {
		std::vector<stChanNote> Cells;
		for (size_t r = 0; r < Parts[p].Cells.size(); ++r)
			Cells.push_back(NoteOf(Parts[p].Cells[r]));
		if (Cells.empty())
			continue;
		if (WriteChannelRows(pMainFrm, Parts[p].Channel, Parts[p].Row, &Cells[0], static_cast<int>(Cells.size()), CursorRow, Error, true))
			++Written;
		else
			Failed = true;
	}
	pMainFrm->EndActionGroup();
	if (Failed) {
		if (Written > 0)
			pMainFrm->SendMessage(WM_COMMAND, ID_EDIT_UNDO);	// all the voices or none
		Message = Error;
		return false;
	}
	Message.Empty();
	for (size_t i = 0; i < Warnings.size(); ++i)
		Message += CString(_T(" ")) + Warnings[i].c_str();
	return Written > 0;
}

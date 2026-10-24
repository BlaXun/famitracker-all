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

// Instruments made from FamiTracker's sound presets (Api/Presets.h: leads,
// plucks, pads, basses and drums for the 2A03, MMC5, VRC6 and 5B, the 5B's
// own sounds, and the VRC7's built-in sounds and custom patches), the N163's
// wave sounds (Api/Wavetables.h) and the FDS's sounds (Api/FdsSounds.h), for
// Claude Code's tools and for Instrument > Sound browser (SoundBrowserDlg.h),
// which makes them through the tools and plays each one before it goes into
// the song. Include after FamiTrackerDoc.h.

namespace ftapi { struct Preset; struct WaveSound; struct FdsSound; }

// The instrument of that type and name, -1 if there's none
int FindNamedInstrument(const CFamiTrackerDoc *pDoc, inst_type_t Type, LPCTSTR Name);

// A preset as a 2A03 (also for the MMC5) or VRC6 instrument named after it
// ("Soft lead"; on the sawtooth "Bass (saw)", without a duty sequence), or
// the one of that name already there: its index, or -1 with the reason
int AddPresetInstrument(CFamiTrackerDoc *pDoc, const ftapi::Preset &Sound, bool VRC6, bool Sawtooth, CString &Error,
	bool Notify = true);
// The same as a 5B instrument: one of the 5B's own sounds, or a preset as the
// 5B plays it (ftapi::Preset5BSequences)
int AddPreset5BInstrument(CFamiTrackerDoc *pDoc, const ftapi::Preset &Sound, CString &Error, bool Notify = true);

// A VRC7 instrument playing one of the chip's sounds (patch 1-15), named
// Name, or the one of that name already there
int AddVrc7PatchInstrument(CFamiTrackerDoc *pDoc, int Patch, LPCTSTR Name, CString &Error);
// The same with the chip's custom patch (patch 0) set to Regs, its 8 registers
int AddVrc7CustomInstrument(CFamiTrackerDoc *pDoc, const int *Regs, LPCTSTR Name, CString &Error);

// An N163 instrument playing a wave sound (Api/Wavetables.h), its waves at
// WavePos (samples) of the chip's memory, or the N163 one of that name
// already there: its index, or -1 with the reason
int AddWaveSoundInstrument(CFamiTrackerDoc *pDoc, const ftapi::WaveSound &Sound, int WavePos, CString &Error);

// What a VRC6 Plus song (Api/Vrc6Plus.h) is missing of its soft layer's starter
// sounds, each with a wave of its own (ftapi::MissingSoftSounds), as
// set_expansion adds them: how many it added
int AddSoftLayerSounds(CFamiTrackerDoc *pDoc);

// An FDS instrument playing one of the FDS's sounds (Api/FdsSounds.h): its
// wave, modulation table, modulator speed, depth and delay, and its volume,
// arpeggio and pitch sequences; or the FDS one of that name already there:
// its index, or -1 with the reason
int AddFdsSoundInstrument(CFamiTrackerDoc *pDoc, const ftapi::FdsSound &Sound, CString &Error, bool Notify = true);

// Removes an instrument, and the sequences it used that no other instrument uses
void RemoveInstrumentAndSequences(CFamiTrackerDoc *pDoc, unsigned int Index);

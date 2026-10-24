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

// Stand-in for FamiTrackerDoc.h: the limits, the pattern cell and a song in
// memory with the document functions Clips.cpp and ChipSwitch.cpp call
// (NoteEdit.cpp needs only the cell and the enums)
#pragma once
#include <vector>
#include <map>
const int MAX_INSTRUMENTS = 64; const int MAX_PATTERN = 128; const int MAX_FRAMES = 128; const int MAX_VOLUME = 0x10;
const int MAX_EFFECT_COLUMNS = 4; const int MAX_CHANNELS = 28; const int OCTAVE_RANGE = 8; const int MAX_TRACKS = 64; const int MAX_PATTERN_LENGTH = 256;
enum note_t { NONE = 0, C, Cs, D, Ds, E, F, Fs, G, Gs, A, As, B, RELEASE, HALT };
enum effect_t { EF_NONE = 0, EF_SPEED, EF_JUMP, EF_SKIP, EF_HALT, EF_VOLUME, EF_PORTAMENTO, EF_PORTAOFF, EF_SWEEPUP, EF_SWEEPDOWN, EF_ARPEGGIO,
	EF_VIBRATO, EF_TREMOLO, EF_PITCH, EF_DELAY, EF_DAC, EF_PORTA_UP, EF_PORTA_DOWN, EF_DUTY_CYCLE, EF_SAMPLE_OFFSET, EF_SLIDE_UP, EF_SLIDE_DOWN,
	EF_VOLUME_SLIDE, EF_NOTE_CUT, EF_RETRIGGER, EF_DELAYED_VOLUME, EF_FDS_MOD_DEPTH, EF_FDS_MOD_SPEED_HI, EF_FDS_MOD_SPEED_LO, EF_DPCM_PITCH,
	EF_SUNSOFT_ENV_TYPE, EF_SUNSOFT_ENV_HI, EF_SUNSOFT_ENV_LO, EF_PAN, EF_COUNT };
const int PAN_LEFT = 0x00; const int PAN_CENTER = 0x80; const int PAN_RIGHT = 0xFF;
// As APU/Types.h
const unsigned char SNDCHIP_NONE = 0, SNDCHIP_VRC6 = 1, SNDCHIP_VRC7 = 2, SNDCHIP_FDS = 4, SNDCHIP_MMC5 = 8, SNDCHIP_N163 = 16, SNDCHIP_S5B = 32;
enum chan_id_t { CHANID_SQUARE1, CHANID_SQUARE2, CHANID_TRIANGLE, CHANID_NOISE, CHANID_DPCM, CHANID_VRC6_PULSE1, CHANID_VRC6_PULSE2,
	CHANID_VRC6_SAWTOOTH, CHANID_MMC5_SQUARE1, CHANID_MMC5_SQUARE2, CHANID_MMC5_VOICE, CHANID_N163_CHAN1, CHANID_N163_CHAN2,
	CHANID_N163_CHAN3, CHANID_N163_CHAN4, CHANID_N163_CHAN5, CHANID_N163_CHAN6, CHANID_N163_CHAN7, CHANID_N163_CHAN8, CHANID_FDS,
	CHANID_VRC7_CH1, CHANID_VRC7_CH2, CHANID_VRC7_CH3, CHANID_VRC7_CH4, CHANID_VRC7_CH5, CHANID_VRC7_CH6, CHANID_S5B_CH1,
	CHANID_S5B_CH2, CHANID_S5B_CH3, CHANNELS };
struct stChanNote { unsigned char Note; unsigned char Octave; unsigned char Vol; unsigned char Instrument; unsigned char EffNumber[MAX_EFFECT_COLUMNS]; unsigned char EffParam[MAX_EFFECT_COLUMNS]; };
class CClipLibrary;
// A song in memory: tracks of frames, each frame a pattern per channel
class CFamiTrackerDoc {
public:
	struct Track { std::vector<std::vector<unsigned>> frames; std::map<std::pair<unsigned, unsigned>, std::vector<stChanNote>> patterns; std::map<unsigned, unsigned> columns; };
	std::vector<Track> tracks;
	std::vector<int> chanIDs;
	unsigned length = 16;
	CClipLibrary *clips = nullptr;
	static stChanNote Empty() { stChanNote n; n.Note = NONE; n.Octave = 0; n.Vol = MAX_VOLUME; n.Instrument = MAX_INSTRUMENTS; for (int i = 0; i < MAX_EFFECT_COLUMNS; ++i) { n.EffNumber[i] = EF_NONE; n.EffParam[i] = 0; } return n; }
	std::vector<stChanNote> &Pattern(unsigned t, unsigned c, unsigned p) { auto &v = tracks[t].patterns[{c, p}]; if (v.empty()) v.assign(length, Empty()); return v; }
	CClipLibrary *GetClips() const { return clips; }
	unsigned GetTrackCount() const { return unsigned(tracks.size()); }
	unsigned GetPatternLength(unsigned) const { return length; }
	unsigned GetFrameCount(unsigned t) const { return unsigned(tracks[t].frames.size()); }
	unsigned GetAvailableChannels() const { return unsigned(chanIDs.size()); }
	int GetChannelType(int i) const { return chanIDs[size_t(i)]; }
	int GetChannelIndex(int id) const { for (size_t i = 0; i < chanIDs.size(); ++i) if (chanIDs[i] == id) return int(i); return -1; }
	unsigned GetPatternAtFrame(unsigned t, unsigned f, unsigned c) const { return tracks[t].frames[f][c]; }
	void GetDataAtPattern(unsigned t, unsigned p, unsigned c, unsigned r, stChanNote *pData) const {
		auto it = tracks[t].patterns.find({c, p}); *pData = (it == tracks[t].patterns.end()) ? Empty() : it->second[r]; }
	void SetDataAtPattern(unsigned t, unsigned p, unsigned c, unsigned r, const stChanNote *pData) { ++writes; Pattern(t, c, p)[r] = *pData; }
	// For ChipSwitch.cpp
	void GetNoteData(unsigned t, unsigned f, unsigned c, unsigned r, stChanNote *pData) const { GetDataAtPattern(t, GetPatternAtFrame(t, f, c), c, r, pData); }
	void SetPatternAtFrame(unsigned t, unsigned f, unsigned c, unsigned p) { tracks[t].frames[f][c] = p; }
	void ClearPattern(unsigned t, unsigned f, unsigned c) { tracks[t].patterns.erase({c, tracks[t].frames[f][c]}); }
	bool IsPatternEmpty(unsigned t, unsigned c, unsigned p) const {
		auto it = tracks[t].patterns.find({c, p}); if (it == tracks[t].patterns.end()) return true;
		for (unsigned r = 0; r < length; ++r) { const stChanNote &n = it->second[r];
			if (n.Note != NONE || n.Vol != MAX_VOLUME || n.Instrument != MAX_INSTRUMENTS || n.EffNumber[0] || n.EffNumber[1] || n.EffNumber[2] || n.EffNumber[3]) return false; }
		return true; }
	unsigned GetEffColumns(unsigned t, unsigned c) const { auto it = tracks[t].columns.find(c); return it == tracks[t].columns.end() ? 0 : it->second; }
	void SetEffColumns(unsigned t, unsigned c, unsigned n) { tracks[t].columns[c] = n; }
	BOOL LockDocument() const { ++locks; return TRUE; }
	BOOL UnlockDocument() const { --locks; return TRUE; }
	mutable int locks = 0;
	int writes = 0;
	// The expansion chips (SwitchChips): ChipSwitchTests.cpp defines SelectExpansionChip as the document does it
	unsigned char chips = 0;
	unsigned namco = 1;
	unsigned char GetExpansionChip() const { return chips; }
	unsigned GetNamcoChannels() const { return namco; }
	void SetNamcoChannels(unsigned n) { namco = n; }
	void SelectExpansionChip(unsigned char Chips);
};

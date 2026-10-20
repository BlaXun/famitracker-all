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
// memory with the document functions Clips.cpp calls (NoteEdit.cpp needs only
// the cell and the enums)
#pragma once
#include <vector>
#include <map>
const int MAX_INSTRUMENTS = 64; const int MAX_PATTERN = 128; const int MAX_FRAMES = 128; const int MAX_VOLUME = 0x10;
const int MAX_EFFECT_COLUMNS = 4; const int MAX_CHANNELS = 28; const int OCTAVE_RANGE = 8; const int MAX_TRACKS = 64; const int MAX_PATTERN_LENGTH = 256;
enum note_t { NONE = 0, C, Cs, D, Ds, E, F, Fs, G, Gs, A, As, B, RELEASE, HALT };
enum effect_t { EF_NONE = 0, EF_SPEED, EF_JUMP, EF_SKIP, EF_HALT, EF_VOLUME, EF_PORTAMENTO, EF_PORTAOFF, EF_SWEEPUP, EF_SWEEPDOWN, EF_ARPEGGIO,
	EF_VIBRATO, EF_TREMOLO, EF_PITCH, EF_DELAY, EF_DAC, EF_PORTA_UP, EF_PORTA_DOWN, EF_DUTY_CYCLE, EF_SAMPLE_OFFSET, EF_SLIDE_UP, EF_SLIDE_DOWN,
	EF_VOLUME_SLIDE, EF_NOTE_CUT, EF_RETRIGGER, EF_DELAYED_VOLUME, EF_FDS_MOD_DEPTH, EF_FDS_MOD_SPEED_HI, EF_FDS_MOD_SPEED_LO, EF_DPCM_PITCH,
	EF_SUNSOFT_ENV_LO, EF_SUNSOFT_ENV_HI, EF_SUNSOFT_ENV_TYPE, EF_COUNT };
struct stChanNote { unsigned char Note; unsigned char Octave; unsigned char Vol; unsigned char Instrument; unsigned char EffNumber[MAX_EFFECT_COLUMNS]; unsigned char EffParam[MAX_EFFECT_COLUMNS]; };
class CClipLibrary;
// A song in memory: tracks of frames, each frame a pattern per channel
class CFamiTrackerDoc {
public:
	struct Track { std::vector<std::vector<unsigned>> frames; std::map<std::pair<unsigned, unsigned>, std::vector<stChanNote>> patterns; };
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
	BOOL LockDocument() const { ++locks; return TRUE; }
	BOOL UnlockDocument() const { --locks; return TRUE; }
	mutable int locks = 0;
	int writes = 0;
};

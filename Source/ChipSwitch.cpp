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

// Switching the expansion chip: see ChipSwitch.h

// Standard headers come before stdafx.h, whose DEBUG_NEW macro would
// otherwise rewrite the placement new inside the standard library
#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "stdafx.h"
#include "FamiTracker.h"
#include "FamiTrackerDoc.h"
#include "Clips.h"
#include "ChipSwitch.h"

namespace {

// Speed and tempo, jumps, skips and halts
bool SteersSong(unsigned char Effect)
{
	return Effect == EF_SPEED || Effect == EF_JUMP || Effect == EF_SKIP || Effect == EF_HALT;
}

// A frame's cells as a map key
std::string CellsKey(const std::vector<stChanNote> &Cells)
{
	return Cells.empty() ? std::string() :
		std::string(reinterpret_cast<const char*>(&Cells[0]), Cells.size() * sizeof(stChanNote));
}

// The effect columns that play in a channel
unsigned int ShownColumns(const CFamiTrackerDoc *pDoc, unsigned int Track, unsigned int Channel)
{
	return std::min(static_cast<unsigned int>(MAX_EFFECT_COLUMNS), pDoc->GetEffColumns(Track, Channel) + 1);
}

}	// namespace

int CountChannelNotes(const CFamiTrackerDoc *pDoc, const std::vector<unsigned int> &Channels, std::vector<int> &Tracks)
{
	Tracks.clear();
	int Total = 0;
	for (unsigned int t = 0; t < pDoc->GetTrackCount(); ++t) {
		const unsigned int Frames = pDoc->GetFrameCount(t), Length = pDoc->GetPatternLength(t);
		int Count = 0;
		for (size_t i = 0; i < Channels.size(); ++i) {
			for (unsigned int f = 0; f < Frames; ++f) {
				for (unsigned int r = 0; r < Length; ++r) {
					stChanNote Note;
					pDoc->GetNoteData(t, f, Channels[i], r, &Note);
					if (Note.Note != NONE && Note.Note != RELEASE && Note.Note != HALT)
						++Count;
				}
			}
		}
		if (Count > 0)
			Tracks.push_back(static_cast<int>(t));
		Total += Count;
	}
	return Total;
}

void GatherSongEffects(const CFamiTrackerDoc *pDoc, const std::vector<unsigned int> &Channels, std::vector<song_effect_t> &Effects)
{
	Effects.clear();
	for (unsigned int t = 0; t < pDoc->GetTrackCount(); ++t) {
		const unsigned int Frames = pDoc->GetFrameCount(t), Length = pDoc->GetPatternLength(t);
		for (size_t i = 0; i < Channels.size(); ++i) {
			const unsigned int Shown = ShownColumns(pDoc, t, Channels[i]);
			for (unsigned int f = 0; f < Frames; ++f) {
				for (unsigned int r = 0; r < Length; ++r) {
					stChanNote Note;
					pDoc->GetNoteData(t, f, Channels[i], r, &Note);
					for (unsigned int k = 0; k < Shown; ++k) {
						if (!SteersSong(Note.EffNumber[k]))
							continue;
						song_effect_t Effect = { t, f, r, Note.EffNumber[k], Note.EffParam[k] };
						Effects.push_back(Effect);
					}
				}
			}
		}
	}
}

void EmptyChannels(CFamiTrackerDoc *pDoc, const std::vector<unsigned int> &Channels)
{
	CClipLibrary *pClips = pDoc->GetClips();
	for (unsigned int t = 0; t < pDoc->GetTrackCount(); ++t) {
		const unsigned int Frames = pDoc->GetFrameCount(t), Length = pDoc->GetPatternLength(t);
		for (size_t i = 0; i < Channels.size(); ++i) {
			const unsigned int Channel = Channels[i];
			if (pClips != NULL)
				pClips->UnlinkArea(pDoc, t, 0, 0, static_cast<int>(Channel), static_cast<int>(Frames * Length));
			// The document clears the pattern a frame plays, so frame 0 points at each in turn
			for (unsigned int p = 0; p < MAX_PATTERN; ++p) {
				pDoc->SetPatternAtFrame(t, 0, Channel, p);
				pDoc->ClearPattern(t, 0, Channel);
			}
			for (unsigned int f = 0; f < Frames; ++f)
				pDoc->SetPatternAtFrame(t, f, Channel, 0);
		}
	}
}

int PlaceSongEffects(CFamiTrackerDoc *pDoc, unsigned int Channel, const std::vector<song_effect_t> &Effects)
{
	int Lost = 0;
	CClipLibrary *pClips = pDoc->GetClips();
	for (unsigned int t = 0; t < pDoc->GetTrackCount(); ++t) {
		bool Any = false;
		for (size_t e = 0; e < Effects.size() && !Any; ++e)
			Any = Effects[e].Track == t;
		if (!Any)
			continue;
		const unsigned int Frames = pDoc->GetFrameCount(t), Length = pDoc->GetPatternLength(t);
		const unsigned int Shown = ShownColumns(pDoc, t, Channel);

		// The channel's cells frame by frame, before and after. Hidden columns
		// don't play: what they hold is cleared if they show.
		std::vector<std::vector<stChanNote> > Before(Frames), After(Frames);
		for (unsigned int f = 0; f < Frames; ++f) {
			Before[f].resize(Length);
			for (unsigned int r = 0; r < Length; ++r)
				pDoc->GetNoteData(t, f, Channel, r, &Before[f][r]);
			After[f] = Before[f];
			for (unsigned int r = 0; r < Length; ++r) {
				for (unsigned int k = Shown; k < MAX_EFFECT_COLUMNS; ++k) {
					After[f][r].EffNumber[k] = EF_NONE;
					After[f][r].EffParam[k] = 0;
				}
			}
		}
		unsigned int Columns = Shown;
		std::vector<int> Placed(Frames, 0);
		for (size_t e = 0; e < Effects.size(); ++e) {
			const song_effect_t &Effect = Effects[e];
			if (Effect.Track != t)
				continue;
			if (Effect.Frame >= Frames || Effect.Row >= Length) {
				++Lost;
				continue;
			}
			stChanNote &Cell = After[Effect.Frame][Effect.Row];
			int Free = -1;
			bool There = false;
			for (int k = 0; k < MAX_EFFECT_COLUMNS; ++k) {
				if (Cell.EffNumber[k] == Effect.Number && Cell.EffParam[k] == Effect.Param)
					There = true;
				else if (Cell.EffNumber[k] == EF_NONE && Free < 0)
					Free = k;
			}
			if (There)
				continue;
			if (Free < 0) {
				++Lost;
				continue;
			}
			Cell.EffNumber[Free] = Effect.Number;
			Cell.EffParam[Free] = Effect.Param;
			Columns = std::max(Columns, static_cast<unsigned int>(Free) + 1);
			++Placed[Effect.Frame];
		}
		// Columns that stay hidden keep what they held
		for (unsigned int f = 0; f < Frames; ++f) {
			for (unsigned int r = 0; r < Length; ++r) {
				for (unsigned int k = Columns; k < MAX_EFFECT_COLUMNS; ++k) {
					After[f][r].EffNumber[k] = Before[f][r].EffNumber[k];
					After[f][r].EffParam[k] = Before[f][r].EffParam[k];
				}
			}
		}

		std::vector<bool> Changed(Frames, false);
		std::vector<unsigned int> Played(Frames);
		bool AnyChange = false;
		for (unsigned int f = 0; f < Frames; ++f) {
			Changed[f] = CellsKey(Before[f]) != CellsKey(After[f]);
			AnyChange = AnyChange || Changed[f];
			Played[f] = pDoc->GetPatternAtFrame(t, f, Channel);
		}
		if (!AnyChange)
			continue;
		for (unsigned int f = 0; f < Frames; ++f) {
			if (Changed[f] && pClips != NULL)
				pClips->UnlinkArea(pDoc, t, f, 0, static_cast<int>(Channel), static_cast<int>(Length));
		}

		// Frames that don't change keep their patterns. The others share a
		// pattern with frames of the same cells, take their own pattern when no
		// frame keeps it, or a free one: one no frame plays and that's empty.
		std::map<std::string, unsigned int> Holding;
		std::vector<bool> Taken(MAX_PATTERN, false);
		std::vector<unsigned int> Assigned(Played);
		for (unsigned int f = 0; f < Frames; ++f) {
			if (Changed[f])
				continue;
			Taken[Played[f]] = true;
			Holding[CellsKey(After[f])] = Played[f];
		}
		for (unsigned int f = 0; f < Frames; ++f) {
			if (!Changed[f])
				continue;
			const std::string Key = CellsKey(After[f]);
			std::map<std::string, unsigned int>::const_iterator Same = Holding.find(Key);
			if (Same != Holding.end()) {
				Assigned[f] = Same->second;
				continue;
			}
			int Pattern = Taken[Played[f]] ? -1 : static_cast<int>(Played[f]);
			for (unsigned int p = 0; p < MAX_PATTERN && Pattern < 0; ++p) {
				if (Taken[p] || std::find(Played.begin(), Played.end(), p) != Played.end())
					continue;
				if (pDoc->IsPatternEmpty(t, Channel, p))
					Pattern = static_cast<int>(p);
			}
			if (Pattern < 0) {
				// Every pattern is in use: this frame stays as it was
				Lost += Placed[f];
				Changed[f] = false;
				continue;
			}
			Assigned[f] = static_cast<unsigned int>(Pattern);
			Taken[Assigned[f]] = true;
			Holding[Key] = Assigned[f];
		}
		for (unsigned int f = 0; f < Frames; ++f) {
			if (!Changed[f])
				continue;
			for (unsigned int r = 0; r < Length; ++r)
				pDoc->SetDataAtPattern(t, Assigned[f], Channel, r, &After[f][r]);
			pDoc->SetPatternAtFrame(t, f, Channel, Assigned[f]);
		}
		if (Columns > Shown)
			pDoc->SetEffColumns(t, Channel, Columns - 1);
	}
	return Lost;
}

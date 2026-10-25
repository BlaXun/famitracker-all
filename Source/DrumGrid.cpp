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

// The drum grid's arithmetic (see DrumGrid.h).

// Standard headers come before stdafx.h, whose DEBUG_NEW macro would
// otherwise rewrite the placement new inside the standard library.
#include <vector>
#include "stdafx.h"
#include "FamiTracker.h"
#include "FamiTrackerDoc.h"
#include "DrumGrid.h"

namespace {

const int PITCHES[DRUM_LANES] = { 0x4, 0xA, 0xE, 0xD };
const char *const NAMES[DRUM_LANES] = { "Kick", "Snare", "Hi-hat", "Open hi-hat" };
const char *const PRESETS[DRUM_LANES] = { "kick", "snare", "hi-hat", "open hi-hat" };

bool IsPlayed(const stChanNote &Cell)
{
	return Cell.Note >= C && Cell.Note <= B;
}

int PitchOf(const stChanNote &Cell)
{
	return (Cell.Octave * 12 + Cell.Note - 1) & 0x0F;
}

}	// namespace

drum_kit_t::drum_kit_t()
{
	for (int i = 0; i < DRUM_LANES; ++i)
		Instruments[i] = -1;
}

int DrumPitch(int Lane)
{
	return (Lane >= 0 && Lane < DRUM_LANES) ? PITCHES[Lane] : 0;
}

const char *DrumName(int Lane)
{
	return (Lane >= 0 && Lane < DRUM_LANES) ? NAMES[Lane] : "Other";
}

const char *DrumPreset(int Lane)
{
	return (Lane >= 0 && Lane < DRUM_LANES) ? PRESETS[Lane] : "";
}

int DrumOf(const stChanNote &Cell, const drum_kit_t &Kit)
{
	if (!IsPlayed(Cell))
		return DRUM_NONE;
	if (Cell.Instrument < MAX_INSTRUMENTS) {
		for (int i = 0; i < DRUM_LANES; ++i)
			if (Kit.Instruments[i] == Cell.Instrument)
				return i;
		return DRUM_OTHER;
	}
	// No instrument on the note: the pitch tells
	for (int i = 0; i < DRUM_LANES; ++i)
		if (PITCHES[i] == PitchOf(Cell))
			return i;
	return DRUM_OTHER;
}

void SetDrum(stChanNote &Cell, int Lane, const drum_kit_t &Kit)
{
	if (Lane < 0 || Lane >= DRUM_LANES || Kit.Instruments[Lane] < 0) {
		if (IsPlayed(Cell)) {
			Cell.Note = NONE;
			Cell.Octave = 0;
			Cell.Instrument = MAX_INSTRUMENTS;
			Cell.Vol = MAX_VOLUME;
		}
		return;
	}
	Cell.Note = static_cast<unsigned char>(PITCHES[Lane] % 12 + 1);
	Cell.Octave = static_cast<unsigned char>(PITCHES[Lane] / 12);
	Cell.Instrument = static_cast<unsigned char>(Kit.Instruments[Lane]);
	Cell.Vol = MAX_VOLUME;
}

void ToggleDrum(stChanNote &Cell, int Lane, const drum_kit_t &Kit)
{
	SetDrum(Cell, DrumOf(Cell, Kit) == Lane ? DRUM_NONE : Lane, Kit);
}

const char *BeatName(int Beat)
{
	static const char *const BEATS[BEAT_COUNT] = { "Basic", "Four on the floor", "Half-time", "Busy", "Clear" };
	return (Beat >= 0 && Beat < BEAT_COUNT) ? BEATS[Beat] : "";
}

bool BeatUses(int Beat, int Lane)
{
	switch (Beat) {
	case BEAT_FOUR_ON_FLOOR:
		return Lane == DRUM_KICK || Lane == DRUM_HIHAT;
	case BEAT_BASIC: case BEAT_HALF_TIME: case BEAT_BUSY:
		return Lane == DRUM_KICK || Lane == DRUM_SNARE || Lane == DRUM_HIHAT;
	default:
		return false;
	}
}

void ApplyBeat(std::vector<stChanNote> &Bar, int Beat, int StepsPerBeat, const drum_kit_t &Kit)
{
	const int Steps = static_cast<int>(Bar.size());
	if (StepsPerBeat < 1)
		StepsPerBeat = 4;
	const int Beats = (Steps + StepsPerBeat - 1) / StepsPerBeat;
	std::vector<int> Plan(static_cast<size_t>(Steps), DRUM_NONE);
	const int Half = StepsPerBeat >= 2 ? StepsPerBeat / 2 : 0;		// the "and" between beats
	const int Quarter = StepsPerBeat >= 4 ? StepsPerBeat / 4 : 0;
	auto Put = [&](int Step, int Lane) {
		if (Step >= 0 && Step < Steps && Plan[static_cast<size_t>(Step)] == DRUM_NONE)
			Plan[static_cast<size_t>(Step)] = Lane;
	};

	switch (Beat) {
	case BEAT_BASIC:
		for (int b = 0; b < Beats; ++b) {
			// In three, the snare takes the second and third beats (a waltz)
			const bool Snare = Beats == 3 ? b > 0 : b % 2 == 1;
			Put(b * StepsPerBeat, Snare ? DRUM_SNARE : DRUM_KICK);
			if (Half > 0)
				Put(b * StepsPerBeat + Half, DRUM_HIHAT);
		}
		break;
	case BEAT_FOUR_ON_FLOOR:
		for (int b = 0; b < Beats; ++b) {
			Put(b * StepsPerBeat, DRUM_KICK);
			if (Half > 0)
				Put(b * StepsPerBeat + Half, DRUM_HIHAT);
		}
		break;
	case BEAT_HALF_TIME:
		Put(0, DRUM_KICK);
		Put((Beats / 2) * StepsPerBeat, DRUM_SNARE);
		for (int b = 0; b < Beats; ++b) {
			Put(b * StepsPerBeat, DRUM_HIHAT);
			if (Half > 0)
				Put(b * StepsPerBeat + Half, DRUM_HIHAT);
		}
		break;
	case BEAT_BUSY:
		for (int b = 0; b < Beats; ++b) {
			const bool Snare = Beats == 3 ? b > 0 : b % 2 == 1;
			Put(b * StepsPerBeat, Snare ? DRUM_SNARE : DRUM_KICK);
		}
		// A kick just before the middle beat, and hi-hats on every quarter of a beat left
		if (Beats >= 2 && Half > 0)
			Put((Beats / 2 - 1) * StepsPerBeat + Half + Quarter, DRUM_KICK);
		for (int s = 0; s < Steps; s += (Quarter > 0 ? Quarter : (Half > 0 ? Half : 1)))
			Put(s, DRUM_HIHAT);
		break;
	default:
		break;
	}

	for (int s = 0; s < Steps; ++s) {
		const int Lane = Plan[static_cast<size_t>(s)];
		SetDrum(Bar[static_cast<size_t>(s)], (Lane >= 0 && Kit.Instruments[Lane] >= 0) ? Lane : DRUM_NONE, Kit);
	}
}

bool FollowCursor(drum_follow_t &Follow, int CursorBar, int Bars)
{
	const int Last = Bars > 0 ? Bars - 1 : 0;
	const int Before = Follow.Bar;
	if (CursorBar != Follow.CursorBar) {
		Follow.CursorBar = CursorBar;
		if (CursorBar >= 0)
			Follow.Bar = CursorBar;
	}
	Follow.Bar = Follow.Bar < 0 ? 0 : (Follow.Bar > Last ? Last : Follow.Bar);
	return Follow.Bar != Before;
}

int FlowDrumControls(const std::vector<int> &Widths, const std::vector<int> &Groups, int Width, int Gap, int GroupGap,
	std::vector<drum_place_t> &Places)
{
	const size_t Count = Widths.size();
	Places.assign(Count, drum_place_t());
	// A control without a group number is a group of its own
	const auto GroupOf = [&Groups](size_t i) { return i < Groups.size() ? Groups[i] : -1 - static_cast<int>(i); };
	int Line = 0, X = 0;
	bool Empty = true;		// nothing on the line yet
	for (size_t First = 0; First < Count; ) {
		size_t End = First;
		int Long = 0;
		while (End < Count && GroupOf(End) == GroupOf(First)) {
			Long += (End > First ? Gap : 0) + Widths[End];
			++End;
		}
		if (!Empty && X + GroupGap + Long > Width) {
			++Line;
			X = 0;
			Empty = true;
		}
		for (size_t i = First; i < End; ++i) {
			const int Space = i == First ? GroupGap : Gap;
			if (!Empty && X + Space + Widths[i] > Width) {
				++Line;
				X = 0;
				Empty = true;
			}
			Places[i].Line = Line;
			Places[i].X = Empty ? 0 : X + Space;
			X = Places[i].X + Widths[i];
			Empty = false;
		}
		First = End;
	}
	return Line + 1;
}

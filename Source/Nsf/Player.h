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

// Plays a track of an NSF file the way an NSF player does (INIT once, then
// PLAY at the file's rate) and records what each channel sounds like at
// every PLAY call, until the track has looped twice, falls silent or runs
// too long. Where it loops is found from the recording.

#include "Chips.h"
#include "NsfFile.h"

#include <functional>
#include <string>
#include <vector>

namespace nsf {

struct Recording {
	double TickRate;		// PLAY calls a second
	bool Pal;
	int ChipFlags;
	std::vector<int> Channels;					// channel_t of the channels the chips have, in order
	std::vector<std::vector<ChannelTick> > Ticks;	// by channel_t (empty for absent ones), a tick each
	SoundData Sounds;
	int Length;				// ticks recorded
	int LoopStart;			// the tick the track loops back to, -1 when it doesn't loop
	int LoopLength;			// ticks in the loop
	bool EndsInSilence;		// stopped because it went quiet (a jingle, say)
	std::vector<std::string> Problems;	// what went wrong along the way, in words

	Recording() : TickRate(60), Pal(false), ChipFlags(0), Length(0), LoopStart(-1), LoopLength(0), EndsInSilence(false) {}
	const ChannelTick &At(int Channel, int Tick) const { return Ticks[Channel][Tick]; }
	bool Has(int Channel) const { return Channel >= 0 && Channel < int(Ticks.size()) && !Ticks[Channel].empty(); }
};

struct RecordOptions {
	double MaxSeconds;		// the longest a track is played
	double SilenceSeconds;	// this long without a sound ends a track
	double MinLoopSeconds;	// shorter repeats aren't taken for the loop
	// Called now and then with the seconds played; returning false stops
	std::function<bool(double)> Progress;
	RecordOptions() : MaxSeconds(600), SilenceSeconds(4), MinLoopSeconds(2) {}
};

// Plays Track (from 0); false with the reason when the NSF can't be played
bool RecordTrack(const NsfFile &File, int Track, const RecordOptions &Options, Recording &Out, std::string &Error);

// The loop in a sequence of per-tick fingerprints: the smallest period
// MinPeriod or longer that repeats for at least one whole period plus Margin
// ticks up to the end, and the earliest tick from which it repeats. False
// when there is none.
bool FindLoop(const std::vector<uint64_t> &Fingerprints, int MinPeriod, int Margin, int &Start, int &Period);

// The loop of a recording (its first Ticks ticks) that repeats only a tick
// either way, as music whose loop isn't a whole number of ticks long does:
// each channel's notes the same within a tick, for at least one whole
// period plus Margin ticks up to the end
bool FindLoopApprox(const Recording &R, int Ticks, int MinPeriod, int Margin, int &Start, int &Period);

}	// namespace nsf

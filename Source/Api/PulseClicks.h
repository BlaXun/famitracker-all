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

// Clicks in held notes on the 2A03's and MMC5's pulses. FamiTracker writes a
// pulse's period in two registers, the high one only when its value changes
// (CSquare1Chan::RefreshChannel), and writing it restarts the pulse's wave
// (CSquare::Write): a click. A note whose vibrato, its instrument's pitch
// sequence or 4xy, takes the period across a multiple of 256 clicks each
// time it crosses, several times a second; A-3 (period 253) does with the
// gentlest vibrato. A fine pitch (Pxx) a step or two of the period away, a
// few cents, keeps the vibrato on one side. The notes are played here as
// 0.4.6's channel handlers play them (ChannelHandler.cpp, Channels2A03.cpp).
// Plain C++14, tested on Linux.

#include <utility>
#include <vector>
#include "Music.h"
#include "Song.h"

namespace ftapi {

// The most a nudge moves a note, in cents: from its start, or from where its
// vibrato starts (the attack stays in tune, and the vibrato's swing hides
// more)
const double MAX_NUDGE_CENTS = 15.0;
const double MAX_LATE_NUDGE_CENTS = 30.0;

// A channel FamiTracker writes that way: the 2A03's and MMC5's pulses
bool ClicksOnPeriods(const ChannelInfo &Info);

// A pitch's period on those pulses, as CSoundGen::SetupPeriodTables works it
// out (NTSC, or PAL's 2A07)
int PulsePeriod(int Pitch, bool Pal);

// How far moving a period by Shift steps takes the pitch, in cents (a pulse
// plays its clock over 16 times the period plus one)
double NudgeCents(int Period, int Shift);

// The rows as the song plays them, from its start through its loop twice (a
// song that halts plays once), with the frames each lasts; LoopRow is the
// loop's first row, -1 when the song halts
std::vector<std::pair<int, int> > PlayedFrames(const SongInfo &Song, const std::vector<std::vector<Cell> > &Channels,
	const std::vector<int> &Columns, int &LoopRow);

// A pulse note as FamiTracker plays it, a frame at a time from its row to the
// next note or a halt
struct PulseNote {
	int Row;					// song row
	int Pitch;
	int Fine;					// the fine pitch (Pxx) it starts with, 0x80 for none
	std::vector<int> Periods;	// each frame's period, -1 while silent
	std::vector<char> Steady;	// 1 where nothing but a vibrato moved the period since the frame before
	std::vector<std::pair<int, int> > Rows;	// each row it plays on: the song row, and its first frame
};

// The notes of a pulse channel's Cells (Columns effect columns), every time
// they play: Played is from PlayedFrames
std::vector<PulseNote> PlayPulse(const SongInfo &Song, const std::vector<Cell> &Cells, int Columns,
	const std::vector<std::pair<int, int> > &Played);

// The clicks a note makes with its period moved Shift steps (negative is
// sharper) from frame From on: its period's high byte changing from one frame
// to the next where only a vibrato (or that move) moved it
int PulseClicks(const PulseNote &Note, int Shift, int From = 0);

// A note that clicks
struct ClickFix {
	int Row, Pitch;
	int Clicks;		// how many times as it plays (the most of the times it plays)
	int Shift;		// the period steps the nudge takes it (negative: sharper, P81 where it played P80); 0 when no nudge
					// small enough stops them
	int At;			// the row the nudge starts on: its own, or the one its vibrato starts in
	int Fine;		// the fine pitch (Pxx) that nudge gives it
	int Left;		// the clicks it still makes as it plays (after the nudge, when Fix)
};

// The notes of a pulse channel that sound in [First, End) and click (twice or
// more), with the least nudge that stops each: from the note's row (at most
// MAX_NUDGE_CENTS), or else from the row its vibrato starts in (at most
// MAX_LATE_NUDGE_CENTS). When Fix, each is nudged: a Pxx there, and on the
// next note the fine pitch it had (on the loop's first note too when that's
// the song's last note), so only it moves.
// Columns grows when a nudge goes past the columns the channel shows; Lost
// when a row had no free column at all.
std::vector<ClickFix> SmoothClicks(const SongInfo &Song, std::vector<Cell> &Cells, int &Columns, int First, int End,
	const std::vector<std::pair<int, int> > &Played, int LoopRow, bool Fix, bool &Lost);

}

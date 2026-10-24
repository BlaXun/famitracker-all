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

// FamiTracker 0.4.6's pattern effects for Claude, and this FamiTracker's own
// pan (8xx, stereo): what each one does, which channels play it, how long it
// lasts and what it's good for; the classic techniques made with them; and
// the checks on the effects Claude writes, which refuse letters 0.4.6 doesn't
// have (Lxx and the like, from later versions and other trackers) and effects
// a channel can't play, saying what to use instead. Checked against the
// channel handlers of 0.4.6 (Channels*.cpp, ChannelHandler.cpp) and
// hlp/effect_list.htm.

#include <string>
#include <vector>
#include "Song.h"

namespace ftapi {

struct EffectInfo {
	const char *Form;		// "Qxy"
	const char *Name;		// "note slide up"
	const char *Does;
	const char *Channels;	// which channels play it
	const char *Lasts;		// until turned off, or this row only
	const char *Uses;		// what it's good for, with values
};

// The effects a note can carry, in FamiTracker's order of letters
const std::vector<EffectInfo> &EffectReference();

struct Technique {
	const char *Name;		// "delayed vibrato"
	const char *Words;		// other names it's asked for by, with commas
	const char *Text;		// how, with the effects and their values
};

const std::vector<Technique> &Techniques();

// The reference in words: the overview (Effect and Technique empty), one
// effect ("Q", "Qxy" or "q31"), one technique (by name or another word for
// it), or what a channel can play (pChannel, with the others empty). False
// with the reason when nothing matches.
bool EffectsText(const std::string &Effect, const std::string &Technique, const ChannelInfo *pChannel, std::string &Text);

// An effect as written for a channel ("Q31"; "H10" on the FDS is its
// modulation depth): its effect_id and parameter, or false with why
// FamiTracker 0.4.6 can't play it there and what to use instead. Speed, the
// song's ticks a row, bounds Gxx.
bool ParseChannelEffect(const std::string &Text, const ChannelInfo &Channel, int Speed, int &Effect, int &Param,
	std::string &Problem);

// Whether a channel plays an effect (an effect_id with its parameter) as
// ParseChannelEffect would take it there, whatever the speed: its chip's own
// (the FDS's modulator Hxx-Jxx only on the FDS) or one it shares
bool ChannelPlaysEffect(const ChannelInfo &Channel, int Effect, int Param);

}	// namespace ftapi

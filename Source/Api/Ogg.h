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

// OGG Vorbis files for games (the looping export), with libvorbis and
// libogg (Source/libvorbis, Source/libogg; BSD licensed, see their COPYING).

#include <string>
#include <utility>
#include <vector>

namespace ftapi {

typedef std::vector<std::pair<std::string, std::string> > OggComments;	// "TITLE", "Town"

// Mono samples in [-1, 1] at SampleRate, encoded at Quality (0.0 small to
// 1.0 best; 0.6 suits chiptunes), with Vorbis comments. False with the reason.
bool EncodeOgg(const std::vector<float> &Samples, int SampleRate, float Quality, const OggComments &Comments, std::string &Out,
	std::string &Error);
// The same in stereo: the left and the right side, as long as each other
bool EncodeOgg(const std::vector<float> &Left, const std::vector<float> &Right, int SampleRate, float Quality,
	const OggComments &Comments, std::string &Out, std::string &Error);

// For checks and tests: an OGG Vorbis file's first channel, its rate and
// comments; with pSecond, its second channel there (the first again in a mono file)
bool DecodeOgg(const std::string &File, std::vector<float> &Samples, int &SampleRate, int &Channels, OggComments &Comments,
	std::string &Error, std::vector<float> *pSecond = 0);

}	// namespace ftapi

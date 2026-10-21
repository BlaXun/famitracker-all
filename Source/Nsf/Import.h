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

// Importing an NSF in one call: plays the tracks asked for, converts them
// and writes a FamiTracker text module, with a summary in words. Used by
// File > Import NSF, Claude Code's import_nsf and the nsf2ftm tool.

#include "Convert.h"
#include "Module.h"
#include "NsfFile.h"

#include <functional>
#include <string>
#include <vector>

namespace nsf {

struct ImportOptions {
	std::vector<int> Tracks;		// from 0, in the order they become the module's; empty for all
	double MaxSeconds;				// the longest a track is played to find its loop
	ConvertOptions Convert;
	// Called before each track is played; returning false stops the import
	std::function<bool(int Done, int Total, const std::string &Track)> Progress;
	ImportOptions() : MaxSeconds(600) {}
};

struct ImportResult {
	Module Song;
	std::string Text;		// FamiTracker's text format
	ConvertReport Report;
	std::string Summary;	// what was imported, track by track, and what couldn't be
};

// The NSF in a few lines: title, artist, chips, its tracks
std::string DescribeNsf(const NsfFile &File);
// A track's name: its own (NSFe), or "Track 3"
std::string TrackTitle(const NsfFile &File, int Track);
// "1,3-5" or "all" (tracks from 1) as tracks from 0; empty means all
bool ParseTrackList(const std::string &Text, int Songs, std::vector<int> &Out, std::string &Error);

bool ImportNsf(const NsfFile &File, const ImportOptions &Options, ImportResult &Out, std::string &Error);

}	// namespace nsf

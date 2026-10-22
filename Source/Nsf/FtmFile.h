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

// FamiTracker modules (.ftm) read as FamiTracker 0.4.6 reads them
// (CFamiTrackerDoc::OpenDocumentNew and its ReadBlock functions, the
// instruments' Load), every block version it knows, with the conversions it
// makes for old files: the sequences of 0.2.x put in order and turned into
// today's, old effects changed, the FDS channel's notes moved up two octaves
// for modules from before 0.3.7, and speeds told from tempos. The result is
// a Module (Module.h) that means what FamiTracker means: instruments, macros
// and DPCM samples keep their numbers (the gaps between instruments are
// marked Unused, empty macros and samples are free places in their lists),
// and WriteText writes it as FamiTracker's File > Export text does. Unlike
// 0.4.6, every read is checked against its block and the file: a damaged
// module fails with the reason rather than being read out of step, and the
// patterns are read into at most 8M cells.
//
// Files of version 1.x (FamiTracker 0.1, before modules were made of blocks)
// aren't read, nor ones newer than 0.4.6's (4.40). The module's text is read
// as Windows-1252 (FamiTracker writes it in the Windows code page) into
// UTF-8.

#include "Convert.h"
#include "Module.h"

#include <cstdint>
#include <string>
#include <vector>

namespace nsf {

// What a module has besides the Module
struct FtmInfo {
	unsigned Version;					// the file's version, 0x0440 for 0.4.6
	int FirstHighlight, SecondHighlight;	// row highlights: rows a beat and rows a bar as the user set them
	int SplitPoint;						// Fxx under it set the speed, at or above it the tempo (32 by default, 21
										// in modules from before 0.4.0)
	int VibratoStyle;					// as stored: 1 the new vibrato (4xy bends up and down), 0 the old (down
										// only, in modules from before 0.3.5)
	int EngineSpeed;					// 0 for the machine's default (Module::FrameRate too)
	std::vector<std::string> Problems;	// what couldn't be read: blocks skipped (unknown ones, from other
										// trackers), block versions newer than 0.4.6's, data left out
	FtmInfo() : Version(0), FirstHighlight(4), SecondHighlight(16), SplitPoint(32), VibratoStyle(0), EngineSpeed(0) {}
};

// A FamiTracker module, by its first bytes
bool IsFtm(const std::vector<uint8_t> &Bytes);

// The module; false with the reason when it isn't one, is newer than 0.4.6
// reads or is damaged
bool ParseFtm(const std::vector<uint8_t> &Bytes, Module &Out, FtmInfo &Info, std::string &Error);
bool ReadFtmFile(const std::string &Path, Module &Out, FtmInfo &Info, std::string &Error);

// Track Track (from 0) as the study takes it (Study.h): its name, its tempo
// from the speed and tempo it starts with (rows a minute are 24 * tempo /
// speed, at the module's frame rate), its beat and bar from the row
// highlights the composer set (4 and 16 unless they say otherwise)
TrackReport FtmTrackReport(const Module &M, const FtmInfo &Info, int Track);

}	// namespace nsf

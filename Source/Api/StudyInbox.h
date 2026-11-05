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

// The study folder ("To study" next to FamiTracker.exe, StudyWatcher.h): a file dropped in is studied as study_nsf and
// study_module study it (every music track of an NSF, a FamiTracker module's every track, a tracker module or a MIDI
// file as one piece), its studies kept in the reference library, then the file moves into "Done" (or "Failed" with a
// note why). Plain C++: the folder's watching and the file moves are FamiTracker's; what's here is tested on Linux.

#include <functional>
#include <string>
#include <vector>

namespace ftapi {

enum study_file_t {
	STUDY_FILE_NONE,		// not a kind FamiTracker studies: it goes to Failed
	STUDY_FILE_NSF,			// .nsf, .nsfe
	STUDY_FILE_MODULE,		// .mod, .s3m, .xm, .it, .mid, .midi
	STUDY_FILE_FTM,			// .ftm
};

study_file_t StudyFileKind(const std::string &Name);
// What the folder takes, for its notes: "NSF and NSFe files, FamiTracker modules (.ftm), ..."
std::string StudyFileKinds();

// A file's content as a key (FNV-1a, 64 bits, in hex): the same file under another name is known by it
std::string StudyContentKey(const std::string &Bytes);

// The study log ("Study log.txt" in the folder): a line a file, newest last, like
// "2026-11-05 19:40  Castlevania III.nsf: 24 tracks studied: castlevania-iii-01, ... [key 0123456789abcdef]"
std::string StudyLogLine(const std::string &When, const std::string &Name, const std::string &What, const std::string &Key);
// The keys of the files the log says were studied (not those that failed)
std::vector<std::string> StudiedKeys(const std::string &Log);

// Name, or "Name (2).ext", "Name (3).ext"... the first that Taken says is free
std::string FreeStudyName(const std::string &Name, const std::function<bool(const std::string &)> &Taken);

// A file is studied once it has stopped growing: listed twice, a poll apart, with the same size and time
struct StudyFolderEntry {
	std::string Name;
	unsigned long long Size;
	unsigned long long Time;
	StudyFolderEntry() : Size(0), Time(0) {}
	StudyFolderEntry(const std::string &N, unsigned long long S, unsigned long long T) : Name(N), Size(S), Time(T) {}
};
std::vector<std::string> StudyFilesReady(const std::vector<StudyFolderEntry> &Now, const std::vector<StudyFolderEntry> &Before);

// Where the studies go: the reference library's files, by name
struct StudyLibrary {
	std::function<bool(const std::string &Name, std::string &Bytes)> Read;		// false: not there
	std::function<bool(const std::string &Name, const std::string &Bytes, std::string &Error)> Save;
};

struct StudyFileResult {
	bool Ok;
	std::string Error;					// why nothing was studied
	std::vector<std::string> Ids;		// the references kept
	std::vector<std::string> Skipped;	// tracks that couldn't be studied, each with why
	std::string What;					// "24 tracks studied: a, b, ..." for the log and the status bar
	StudyFileResult() : Ok(false) {}
};

// Studies the file at LocalPath (as the C runtime opens it) of the kind its Name says, keeping the studies in Library.
// Source is where the file will be (in Done), kept in each reference so that "references restudy" finds it. An NSF's
// music tracks are studied (all its tracks when none sounds like music); a study kept before keeps the user's tags and
// notes. Progress tells what it's doing ("track 3 of 24"); returning false stops (Error "stopped").
StudyFileResult StudyFileIntoLibrary(const std::string &LocalPath, const std::string &Name, const std::string &Source,
	const std::string &Date, const StudyLibrary &Library,
	const std::function<bool(const std::string &Doing)> &Progress = std::function<bool(const std::string &)>());

}	// namespace ftapi

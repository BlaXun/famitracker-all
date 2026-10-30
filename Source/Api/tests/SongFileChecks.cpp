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

// check_song and what_next on real songs: a folder (the first argument, or the
// FAMITRACKER_SONG_CHECKS environment variable) with "keep" (songs the user
// likes, which must get no "too_busy" finding and no what_next step for it: a
// failure) and "flag" (songs the user called too busy for the NES, which should
// get one: listed when they don't, since not every busy song can be told from
// the kept ones; see CLAUDE.md, "Busy for NES"). The songs are the user's own and stay out of the repository: without
// the folder, nothing is checked and the test passes.
//   song_file_checks /path/to/songs [-v]

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#ifdef _MSC_VER
#include <io.h>
#else
#include <dirent.h>
#endif

#include "Json.h"
#include "Tools.h"
#include "SongFiles.h"

using namespace ftapi;

namespace {

int g_Failures = 0;

std::vector<std::string> FtmFiles(const std::string &Folder)
{
	std::vector<std::string> Out;
#ifdef _MSC_VER
	_finddata_t Found;
	const intptr_t Handle = _findfirst((Folder + "/*.ftm").c_str(), &Found);
	if (Handle == -1)
		return Out;
	do
		Out.push_back(Folder + "/" + Found.name);
	while (_findnext(Handle, &Found) == 0);
	_findclose(Handle);
#else
	DIR *pDir = opendir(Folder.c_str());
	if (pDir == 0)
		return Out;
	while (dirent *pEntry = readdir(pDir)) {
		const std::string Name = pEntry->d_name;
		if (Name.size() > 4 && Name.substr(Name.size() - 4) == ".ftm")
			Out.push_back(Folder + "/" + Name);
	}
	closedir(pDir);
#endif
	std::sort(Out.begin(), Out.end());
	return Out;
}

Json Parse(const char *pText)
{
	Json Out;
	ParseJson(pText, Out);
	return Out;
}

// The "too_busy" findings check_song gives every track of a song
int CheckSong(const std::string &Path, bool Flagged, bool Verbose)
{
	FileSong Probe;
	std::string Error;
	if (!Probe.Load(Path, 0, Error)) {
		std::printf("FAIL %s: %s\n", Path.c_str(), Error.c_str());
		++g_Failures;
		return 0;
	}
	int Found = 0;
	for (int t = 0; t < Probe.Info.Tracks; ++t) {
		FileSong Song;
		if (!Song.Load(Path, t, Error))
			continue;
		ToolSet Tools(Song);
		const ToolResult Checked = Tools.Call("check_song", Parse("{\"format\": \"json\"}"));
		Json Reply;
		if (!ParseJson(Checked.Text, Reply)) {
			std::printf("FAIL %s track %d: check_song's JSON doesn't read: %s\n", Path.c_str(), t + 1, Checked.Text.substr(0, 300).c_str());
			++g_Failures;
			continue;
		}
		const Json *pFindings = Reply.Get("findings");
		int Here = 0;
		for (size_t i = 0; pFindings != 0 && i < pFindings->Items().size(); ++i) {
			const Json &F = pFindings->Items()[i];
			const Json *pKind = F.Get("kind");
			if (pKind == 0 || !pKind->IsString() || pKind->String() != "too_busy")
				continue;
			++Here;
			const Json *pMessage = F.Get("message");
			std::printf("    %s track %d: %s\n", Path.substr(Path.find_last_of('/') + 1).c_str(), t + 1,
				pMessage != 0 && pMessage->IsString() ? pMessage->String().c_str() : "");
		}
		const std::string Next = Tools.Call("what_next", Parse("{\"all\": true}")).Text;
		const bool Step = Next.find("oo busy for the NES") != std::string::npos;
		if (Flagged && Here > 0 && !Step) {
			std::printf("FAIL %s track %d: what_next lists no step for its too_busy finding\n", Path.c_str(), t + 1);
			++g_Failures;
		}
		if (Verbose)
			std::printf("    what_next: %s\n", Next.c_str());
		if (!Flagged && (Here > 0 || Step)) {
			std::printf("FAIL %s track %d: %d too_busy findings%s on a song the user keeps\n", Path.c_str(), t + 1, Here,
				Step ? " and a what_next step" : "");
			++g_Failures;
		}
		Found += Here;
	}
	return Found;
}

}	// namespace

int main(int argc, char *argv[])
{
	std::string Folder = argc > 1 ? argv[1] : "";
	if (Folder.empty()) {
#ifdef _MSC_VER
		char *pValue = 0;
		size_t Size = 0;
		if (_dupenv_s(&pValue, &Size, "FAMITRACKER_SONG_CHECKS") == 0 && pValue != 0) {
			Folder = pValue;
			std::free(pValue);
		}
#else
		const char *pValue = std::getenv("FAMITRACKER_SONG_CHECKS");
		Folder = pValue != 0 ? pValue : "";
#endif
	}
	const bool Verbose = argc > 2 && std::string(argv[2]) == "-v";
	if (Folder.empty()) {
		std::printf("No songs to check (give a folder with keep/ and flag/, or set FAMITRACKER_SONG_CHECKS): skipped.\n");
		return 0;
	}
	const std::vector<std::string> Keep = FtmFiles(Folder + "/keep"), Flag = FtmFiles(Folder + "/flag");
	for (size_t i = 0; i < Keep.size(); ++i) {
		std::printf("keep %s\n", Keep[i].c_str());
		CheckSong(Keep[i], false, Verbose);
	}
	int Missed = 0;
	for (size_t i = 0; i < Flag.size(); ++i) {
		std::printf("flag %s\n", Flag[i].c_str());
		if (CheckSong(Flag[i], true, Verbose) == 0) {
			std::printf("MISSED %s: no too_busy finding on a song the user called too busy\n", Flag[i].c_str());
			++Missed;
		}
	}
	std::printf("%d kept and %d flagged songs, %d failed, %d flagged without a finding\n", static_cast<int>(Keep.size()),
		static_cast<int>(Flag.size()), g_Failures, Missed);
	return g_Failures == 0 ? 0 : 1;
}

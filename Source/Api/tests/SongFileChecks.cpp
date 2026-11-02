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
// likes, which must get no finding of the kinds below and no what_next step (nor the review's development tips)
// for one: a failure) and "flag" (songs the user found fault with: too busy for
// the NES, chords that drone on or a climax that repeats A, which should get one of them: listed when
// they don't, since not every such song can be told from the kept ones; see
// CLAUDE.md, "Busy for NES" and "Chords that drone on"). The songs are the
// user's own and stay out of the repository: without the folder, nothing is
// checked and the test passes.
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

// The kinds checked, words of the what_next step each finding gives, and whether a flagged song's finding must give
// one (a climax too high to lift an octave gets advice without a call, so no step)
struct CheckedKind { const char *Kind; const char *Step; bool StepNeeded; };
const CheckedKind KINDS[] = { { "too_busy", "oo busy for the NES", true }, { "chord_drone", "drone against the tune", true },
	{ "climax_repeats", "the song's high point, plays the melody", false },
	{ "chords_throughout", "A chord part that plays all through", true },
	{ "monotone", "doesn't catch: in ", false },
	{ "off_chord", "lands off the chord on strong beats", true } };
const size_t KIND_COUNT = sizeof(KINDS) / sizeof(KINDS[0]);

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

// The findings of the kinds checked that check_song gives every track of a song
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
		const std::string Next = Tools.Call("what_next", Parse("{\"all\": true}")).Text;
		if (Verbose)
			std::printf("    what_next: %s\n", Next.c_str());
		for (size_t k = 0; k < KIND_COUNT; ++k) {
			int Here = 0;
			for (size_t i = 0; pFindings != 0 && i < pFindings->Items().size(); ++i) {
				const Json &F = pFindings->Items()[i];
				const Json *pKind = F.Get("kind");
				if (pKind == 0 || !pKind->IsString() || pKind->String() != KINDS[k].Kind)
					continue;
				++Here;
				const Json *pMessage = F.Get("message");
				std::printf("    %s track %d, %s: %s\n", Path.substr(Path.find_last_of('/') + 1).c_str(), t + 1, KINDS[k].Kind,
					pMessage != 0 && pMessage->IsString() ? pMessage->String().c_str() : "");
			}
			const bool Step = Next.find(KINDS[k].Step) != std::string::npos;
			if (Flagged && Here > 0 && !Step && KINDS[k].StepNeeded) {
				std::printf("FAIL %s track %d: what_next lists no step for its %s finding\n", Path.c_str(), t + 1, KINDS[k].Kind);
				++g_Failures;
			}
			if (!Flagged && (Here > 0 || Step)) {
				std::printf("FAIL %s track %d: %d %s findings%s on a song the user keeps\n", Path.c_str(), t + 1, Here, KINDS[k].Kind,
					Step ? " and a what_next step" : "");
				++g_Failures;
			}
			Found += Here;
		}
		// How the song develops part by part (fix-r6-depth): a song the user keeps gets none of the review's development
		// tips and no what_next step for them
		if (!Flagged) {
			const std::string Review = Tools.Call("review_arrangement", Parse("{}")).Text;
			static const char *const DEVELOPMENT[] = { " did: the same parts with the same sounds", "should be the fullest part, the last "
				"time round", "Nothing carries from the intro into", "gives it something of its own", "its melody climbs over",
				"a line a third over its melody" };
			for (const char *Words : DEVELOPMENT)
				if (Review.find(Words) != std::string::npos || Next.find(Words) != std::string::npos) {
					std::printf("FAIL %s track %d: \"%s\" (development) on a song the user keeps\n", Path.c_str(), t + 1, Words);
					++g_Failures;
				}
		}
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
	// -balance [style]: each song's balance as the review hears it (style calm unless given), for calibrating the mix's
	// places: the folder's songs (or one .ftm file given), then keep/ and flag/
	if (argc > 2 && std::string(argv[2]) == "-balance") {
		const std::string Style = argc > 3 ? argv[3] : "calm";
		std::vector<std::string> Files;
		if (Folder.size() > 4 && Folder.substr(Folder.size() - 4) == ".ftm")
			Files.push_back(Folder);
		else {
			const char *const SUBS[] = { "", "/keep", "/flag" };
			for (size_t s = 0; s < 3; ++s) {
				const std::vector<std::string> More = FtmFiles(Folder + SUBS[s]);
				Files.insert(Files.end(), More.begin(), More.end());
			}
		}
		for (size_t i = 0; i < Files.size(); ++i) {
			FileSong Song;
			std::string Error;
			if (!Song.Load(Files[i], 0, Error))
				continue;
			ToolSet Tools(Song);
			Json Args = Json::MakeObject();
			Args.Set("style", Style);
			const std::string Review = Tools.Call("review_arrangement", Args).Text;
			const size_t At = Review.find("Balance as heard: ");
			std::printf("%s: %s\n", Files[i].c_str(), At == std::string::npos ? "(no balance)" : Review.substr(At + 18,
				Review.find('\n', At) - At - 18).c_str());
		}
		return 0;
	}
	// -tunes: each song's melody measured (TuneShape.h), for calibrating what makes a tune catchy; the folder's songs
	// themselves, then keep/ and flag/
	if (argc > 2 && std::string(argv[2]) == "-tunes") {
		const char *const SUBS[] = { "", "/keep", "/flag" };
		for (size_t s = 0; s < 3; ++s) {
			const std::vector<std::string> Files = FtmFiles(Folder + SUBS[s]);
			for (size_t i = 0; i < Files.size(); ++i) {
				FileSong Song;
				std::string Error;
				if (!Song.Load(Files[i], 0, Error))
					continue;
				ToolSet Tools(Song);
				std::printf("%s: %s\n", Files[i].c_str(), Tools.TuneShapes().c_str());
				// With -tunes -steps [N], the melody's first N bars (8) in steps too
				const std::string Shapes = Tools.TuneShapes();
				const size_t At = Shapes.find(" bars ");
				if (argc > 3 && std::string(argv[3]) == "-steps" && At != std::string::npos) {
					Json Args = Json::MakeObject();
					Args.Set("channel", Shapes.substr(0, At));
					Args.Set("format", "steps");
					const int First = std::atoi(Shapes.c_str() + At + 6);
					Args.Set("from_bar", First);
					Args.Set("to_bar", First + (argc > 4 ? std::max(1, std::atoi(argv[4])) : 8) - 1);
					std::printf("%s\n", Tools.Call("read_notes", Args).Text.c_str());
				}
			}
		}
		return 0;
	}
	// -beats: each song's melody notes on strong beats that are off the chord there (OffChordBeats), for calibrating
	// check_song's off_chord: the folder's songs (or one .ftm file given), then keep/ and flag/, every track
	if (argc > 2 && std::string(argv[2]) == "-beats") {
		std::vector<std::string> Files;
		if (Folder.size() > 4 && Folder.substr(Folder.size() - 4) == ".ftm")
			Files.push_back(Folder);
		else {
			const char *const SUBS[] = { "", "/keep", "/flag" };
			for (size_t s = 0; s < 3; ++s) {
				const std::vector<std::string> More = FtmFiles(Folder + SUBS[s]);
				Files.insert(Files.end(), More.begin(), More.end());
			}
		}
		for (size_t i = 0; i < Files.size(); ++i) {
			FileSong Probe;
			std::string Error;
			if (!Probe.Load(Files[i], 0, Error))
				continue;
			for (int t = 0; t < Probe.Info.Tracks; ++t) {
				FileSong Song;
				if (!Song.Load(Files[i], t, Error))
					continue;
				ToolSet Tools(Song);
				std::printf("%s track %d: %s\n", Files[i].c_str(), t + 1, Tools.StrongBeats().c_str());
			}
		}
		return 0;
	}
	// -repeats: each song's repeats as the review and what_next hear them (the review's lines and tips that name a repeat,
	// what_next's step when it varies one, and RepeatShapes' counts), for calibrating what a planned return is
	if (argc > 2 && std::string(argv[2]) == "-repeats") {
		const char *const SUBS[] = { "", "/keep", "/flag" };
		for (size_t s = 0; s < 3; ++s) {
			const std::vector<std::string> Files = FtmFiles(Folder + SUBS[s]);
			for (size_t i = 0; i < Files.size(); ++i) {
				FileSong Probe;
				std::string Error;
				if (!Probe.Load(Files[i], 0, Error))
					continue;
				for (int t = 0; t < Probe.Info.Tracks; ++t) {
					FileSong Song;
					if (!Song.Load(Files[i], t, Error))
						continue;
					ToolSet Tools(Song);
					std::printf("%s track %d: %s\n", Files[i].c_str(), t + 1, Tools.RepeatShapes().c_str());
					const std::string Review = Tools.Call("review_arrangement", Parse("{}")).Text;
					size_t At = 0;
					while (At < Review.size()) {
						size_t Stop = Review.find('\n', At);
						if (Stop == std::string::npos)
							Stop = Review.size();
						const std::string Line = Review.substr(At, Stop - At);
						if (Line.find("four before") != std::string::npos || Line.find("unchanged") != std::string::npos ||
							Line.find("nothing new") != std::string::npos)
							std::printf("  review: %s\n", Line.c_str());
						At = Stop + 1;
					}
					const std::string Next = Tools.Call("what_next", Parse("{}")).Text;
					if (Next.find("four before") != std::string::npos || Next.find("note for note") != std::string::npos ||
						Next.find("vary_part") != std::string::npos)
						std::printf("  what_next: %s\n", Next.substr(0, 400).c_str());
				}
			}
		}
		return 0;
	}
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
			std::printf("MISSED %s: no finding of the kinds checked on a song the user found fault with\n", Flag[i].c_str());
			++Missed;
		}
	}
	std::printf("%d kept and %d flagged songs, %d failed, %d flagged without a finding\n", static_cast<int>(Keep.size()),
		static_cast<int>(Flag.size()), g_Failures, Missed);
	return g_Failures == 0 ? 0 : 1;
}

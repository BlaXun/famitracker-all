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

// nsf2ftm: an NSF's tracks as a FamiTracker text module, for File > Import
// text. The same conversion FamiTracker's File > Import NSF does.
//
//   nsf2ftm music.nsf music.txt [-tracks 1,3-5] [-seconds 600] [-list]
//
// It also studies tracker modules (MOD, S3M, XM, IT) for how they're made,
// from their pattern data, MIDI files from their notes, and FamiTracker's own
// modules from theirs, a study a track:
// nsf2ftm song.xm -study
// nsf2ftm song.ftm -study [-tracks 1,3-5]

#include "../FtmFile.h"
#include "../Import.h"
#include "../Study.h"
#include "../Tracker.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

int main(int argc, char **argv)
{
	if (argc < 2) {
		std::printf("nsf2ftm: an NSF's tracks as a FamiTracker text module\n\n"
			"  nsf2ftm music.nsf music.txt [-tracks 1,3-5] [-seconds 600]\n"
			"  nsf2ftm music.nsf -study [-tracks 1,3-5] [-seconds 600]\n"
			"  nsf2ftm music.nsf -survey\n"
			"  nsf2ftm music.nsf -list\n"
			"  nsf2ftm song.xm -study\n"
			"  nsf2ftm song.mid -study\n"
			"  nsf2ftm song.ftm -study [-tracks 1,3-5]\n\n"
			"Tracks count from 1; all of them by default. Open the text in FamiTracker\n"
			"with File > Import text. -study says how each track is made instead,\n"
			"-survey which tracks are music. A tracker module (MOD, S3M, XM or IT), a\n"
			"MIDI file or a FamiTracker module can only be studied.\n");
		return 1;
	}
	const std::string In = argv[1];
	std::string OutPath, Tracks;
	double Seconds = 600;
	int Instruments = 64;
	bool List = false, Study = false, Survey = false;
	for (int i = 2; i < argc; ++i) {
		if (!std::strcmp(argv[i], "-list"))
			List = true;
		else if (!std::strcmp(argv[i], "-study"))
			Study = true;
		else if (!std::strcmp(argv[i], "-survey"))
			Survey = true;
		else if (!std::strcmp(argv[i], "-tracks") && i + 1 < argc)
			Tracks = argv[++i];
		else if (!std::strcmp(argv[i], "-seconds") && i + 1 < argc)
			Seconds = std::atof(argv[++i]);
		else if (!std::strcmp(argv[i], "-instruments") && i + 1 < argc)
			Instruments = std::atoi(argv[++i]);
		else if (OutPath.empty())
			OutPath = argv[i];
	}

	nsf::NsfFile File;
	std::string Error;
	if (!nsf::ReadNsfFile(In, File, Error)) {
		// A FamiTracker module, to study track by track
		nsf::Module Ftm;
		nsf::FtmInfo FtmInfo;
		std::string FtmError;
		if (nsf::ReadFtmFile(In, Ftm, FtmInfo, FtmError)) {
			std::printf("FamiTracker module \"%s\" by %s: %d track%s\n", Ftm.Title.c_str(), Ftm.Author.empty() ? "?" : Ftm.Author.c_str(),
				int(Ftm.Tracks.size()), Ftm.Tracks.size() == 1 ? "" : "s");
			for (size_t i = 0; i < FtmInfo.Problems.size(); ++i)
				std::printf("  (%s)\n", FtmInfo.Problems[i].c_str());
			if (!Study)
				return 0;
			std::vector<int> Which;
			if (!nsf::ParseTrackList(Tracks, int(Ftm.Tracks.size()), Which, FtmError)) {
				std::fprintf(stderr, "nsf2ftm: %s\n", FtmError.c_str());
				return 2;
			}
			for (size_t i = 0; i < Which.size(); ++i)
				std::printf("\n%d. %s", Which[i] + 1, nsf::DescribeStudy(nsf::StudyTrack(Ftm, Which[i],
					nsf::FtmTrackReport(Ftm, FtmInfo, Which[i])), true).c_str());
			return 0;
		}
		// A tracker module or a MIDI file, to study
		nsf::TrackerSong Song;
		std::string TrackerError;
		if (!nsf::ReadTrackerFile(In, Song, TrackerError)) {
			std::fprintf(stderr, "nsf2ftm: %s\n", Error.c_str());
			return 2;
		}
		std::printf("%s\n", nsf::DescribeTracker(Song).c_str());
		if (!Study)
			return 0;
		nsf::Module M;
		nsf::TrackReport Report;
		if (!nsf::TrackerToModule(Song, M, Report, TrackerError)) {
			std::fprintf(stderr, "nsf2ftm: %s\n", TrackerError.c_str());
			return 3;
		}
		std::printf("\n%s", nsf::DescribeStudy(nsf::StudyTrack(M, 0, Report)).c_str());
		return 0;
	}
	std::printf("%s\n", nsf::DescribeNsf(File).c_str());
	if (Survey) {
		const std::vector<nsf::TrackSurvey> Found = nsf::SurveyTracks(File);
		for (size_t i = 0; i < Found.size(); ++i) {
			const nsf::TrackSurvey &t = Found[i];
			std::printf("  %d. %s: %s, %.1f s", t.Track + 1, t.Title.c_str(), t.Kind.c_str(), t.Seconds);
			if (t.LoopSeconds > 0)
				std::printf(", a loop of %.1f s", t.LoopSeconds);
			std::printf("\n");
		}
		return 0;
	}
	if (List || (OutPath.empty() && !Study))
		return 0;

	std::vector<int> Chosen;
	if (!nsf::ParseTrackList(Tracks, File.Songs, Chosen, Error)) {
		std::fprintf(stderr, "nsf2ftm: %s\n", Error.c_str());
		return 2;
	}
	if (Study) {
		nsf::StudyOptions Options;
		Options.MaxSeconds = Seconds;
		std::vector<nsf::StudiedTrack> Studied;
		if (!nsf::StudyTracks(File, Chosen, Options, Studied, Error)) {
			std::fprintf(stderr, "nsf2ftm: %s\n", Error.c_str());
			return 3;
		}
		for (size_t i = 0; i < Studied.size(); ++i) {
			if (!Studied[i].Problem.empty())
				std::printf("\n%d. %s\n", Studied[i].Track + 1, Studied[i].Problem.c_str());
			else
				std::printf("\n%d. %s", Studied[i].Track + 1, nsf::DescribeStudy(Studied[i].Study, true).c_str());
		}
		return 0;
	}
	nsf::ImportOptions Options;
	Options.Tracks = Chosen;
	Options.MaxSeconds = Seconds;
	Options.Convert.MaxInstruments = Instruments;
	Options.Progress = [](int Done, int Total, const std::string &Name) {
		std::printf("  track %d of %d: %s\n", Done + 1, Total, Name.c_str());
		return true;
	};
	nsf::ImportResult Result;
	if (!nsf::ImportNsf(File, Options, Result, Error)) {
		std::fprintf(stderr, "nsf2ftm: %s\n", Error.c_str());
		return 3;
	}
#ifdef _MSC_VER
	FILE *f = nullptr;
	if (fopen_s(&f, OutPath.c_str(), "wb") != 0)
		f = nullptr;
#else
	FILE *f = std::fopen(OutPath.c_str(), "wb");
#endif
	if (!f) {
		std::fprintf(stderr, "nsf2ftm: can't write %s\n", OutPath.c_str());
		return 4;
	}
	std::fwrite(Result.Text.data(), 1, Result.Text.size(), f);
	std::fclose(f);
	std::printf("%s", Result.Summary.c_str());
	std::printf("Wrote %s\n", OutPath.c_str());
	return 0;
}

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

#include "Import.h"
#include "Player.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <memory>

namespace nsf {

namespace {

std::string Time(int Milliseconds)
{
	const int s = (Milliseconds + 500) / 1000;
	char Buf[16];
	std::snprintf(Buf, sizeof(Buf), "%d:%02d", s / 60, s % 60);
	return Buf;
}

std::string Number(double Value, int Decimals)
{
	char Buf[32];
	std::snprintf(Buf, sizeof(Buf), "%.*f", Decimals, Value);
	return Buf;
}

}	// namespace

std::string TrackTitle(const NsfFile &File, int Track)
{
	const std::string Name = File.TrackName(Track);
	return Name.empty() ? "Track " + std::to_string(Track + 1) : Name;
}

std::string DescribeNsf(const NsfFile &File)
{
	std::string s = File.Title.empty() ? "An NSF" : "\"" + File.Title + "\"";
	if (!File.Artist.empty())
		s += " by " + File.Artist;
	if (!File.Copyright.empty())
		s += " (" + File.Copyright + ")";
	s += "\n" + std::to_string(File.Songs) + (File.Songs == 1 ? " track" : " tracks");
	s += File.Chips ? ", with the " + File.ChipNames() + (File.ChipNames().find(',') != std::string::npos ? " chips" : " chip") : "";
	s += File.Pal && !File.Dual ? ", for PAL machines" : "";
	s += "\n";
	for (int t = 0; t < File.Songs; ++t) {
		const int Ms = File.TrackTime(t);
		s += "  " + std::to_string(t + 1) + ". " + TrackTitle(File, t) + (Ms > 0 ? " (" + Time(Ms) + ")" : "") + "\n";
	}
	return s;
}

bool ParseTrackList(const std::string &Text, int Songs, std::vector<int> &Out, std::string &Error)
{
	Out.clear();
	std::string t;
	for (size_t i = 0; i < Text.size(); ++i)
		if (!std::isspace(static_cast<unsigned char>(Text[i])))
			t += char(std::tolower(static_cast<unsigned char>(Text[i])));
	if (t.empty() || t == "all") {
		for (int i = 0; i < Songs; ++i)
			Out.push_back(i);
		return true;
	}
	size_t Pos = 0;
	while (Pos < t.size()) {
		size_t End = t.find(',', Pos);
		if (End == std::string::npos)
			End = t.size();
		const std::string Part = t.substr(Pos, End - Pos);
		Pos = End + 1;
		if (Part.empty())
			continue;
		int a = 0, b = 0;
		const size_t Dash = Part.find('-');
		char *e1 = nullptr, *e2 = nullptr;
		if (Dash == std::string::npos) {
			a = b = int(std::strtol(Part.c_str(), &e1, 10));
			if (*e1) {
				Error = "\"" + Part + "\" isn't a track number";
				return false;
			}
		}
		else {
			const std::string L = Part.substr(0, Dash), R = Part.substr(Dash + 1);
			a = int(std::strtol(L.c_str(), &e1, 10));
			b = int(std::strtol(R.c_str(), &e2, 10));
			if (*e1 || *e2 || L.empty() || R.empty() || b < a) {
				Error = "\"" + Part + "\" isn't a range of tracks";
				return false;
			}
		}
		if (a < 1 || b > Songs) {
			Error = "the NSF has tracks 1 to " + std::to_string(Songs) + ", not " + Part;
			return false;
		}
		for (int i = a; i <= b; ++i)
			Out.push_back(i - 1);
	}
	if (Out.empty()) {
		Error = "no tracks given";
		return false;
	}
	return true;
}

bool ImportNsf(const NsfFile &File, const ImportOptions &Options, ImportResult &Out, std::string &Error)
{
	Out = ImportResult();
	std::vector<int> Tracks = Options.Tracks;
	if (Tracks.empty())
		for (int i = 0; i < File.Songs; ++i)
			Tracks.push_back(i);
	if (Tracks.size() > 64) {
		Error = "FamiTracker has room for 64 tracks; pick up to 64";
		return false;
	}

	std::vector<std::unique_ptr<Recording> > Recordings;
	std::vector<TrackSource> Sources;
	std::vector<int> SourceTracks;		// the NSF's track of each source
	std::vector<std::string> Skipped;
	for (size_t i = 0; i < Tracks.size(); ++i) {
		const std::string Title = TrackTitle(File, Tracks[i]);
		if (Options.Progress && !Options.Progress(int(i), int(Tracks.size()), Title)) {
			Error = "stopped";
			return false;
		}
		std::unique_ptr<Recording> R(new Recording());
		RecordOptions Record;
		Record.MaxSeconds = Options.MaxSeconds;
		std::string Why;
		if (!RecordTrack(File, Tracks[i], Record, *R, Why)) {
			Skipped.push_back(Title + ": " + Why);
			continue;
		}
		bool Sound = false;
		for (size_t c = 0; c < R->Channels.size() && !Sound; ++c)
			for (int t = 0; t < R->Length && !Sound; ++t)
				Sound = R->Ticks[R->Channels[c]][t].Sounding();
		if (!Sound) {
			Skipped.push_back(Title + ": it makes no sound FamiTracker can play");
			continue;
		}
		TrackSource Source;
		Source.Name = Title;
		Source.Rec = R.get();
		Sources.push_back(Source);
		SourceTracks.push_back(Tracks[i]);
		Recordings.push_back(std::move(R));
	}
	if (Sources.empty()) {
		Error = Skipped.empty() ? "no tracks to import" : "none of the tracks could be imported (" + Skipped[0] + ")";
		return false;
	}

	if (!ConvertTracks(File, Sources, Options.Convert, Out.Song, Out.Report, Error))
		return false;
	// A track each source, in order
	for (size_t i = 0; i < Out.Report.Tracks.size() && i < SourceTracks.size(); ++i)
		Out.Report.Tracks[i].PlaylistSeconds = std::max(0, File.TrackTime(SourceTracks[i])) / 1000.0;
	Out.Text = WriteText(Out.Song);

	// In words
	std::string &s = Out.Summary;
	s = "Imported " + std::to_string(Out.Report.Tracks.size()) + (Out.Report.Tracks.size() == 1 ? " track" : " tracks");
	if (!File.Title.empty())
		s += " of \"" + File.Title + "\"";
	s += ":\n";
	for (size_t i = 0; i < Out.Report.Tracks.size(); ++i) {
		const TrackReport &t = Out.Report.Tracks[i];
		s += "  " + std::to_string(i + 1) + ". " + t.Name + ": " + Number(t.Bpm, 0) + " BPM, " + std::to_string(t.RowsPerBeat) +
			" rows a beat (speed " + std::to_string(t.Speed) + ", tempo " + std::to_string(t.Tempo) + "), " + std::to_string(t.Rows) +
			" rows";
		s += t.LoopRow >= 0 ? ", looping back to row " + std::to_string(t.LoopRow) : ", then it ends";
		s += ", " + std::to_string(t.Notes) + " notes";
		if (t.Delayed > 0)
			s += " (" + std::to_string(t.Delayed) + " between rows, delayed with Gxx)";
		s += "\n";
		for (size_t p = 0; p < t.Problems.size(); ++p)
			s += "     " + t.Problems[p] + "\n";
	}
	s += std::to_string(Out.Report.Instruments) + (Out.Report.Instruments == 1 ? " instrument" : " instruments") + ".\n";
	for (size_t p = 0; p < Out.Report.Problems.size(); ++p)
		s += Out.Report.Problems[p] + ".\n";
	for (size_t p = 0; p < Skipped.size(); ++p)
		s += "Not imported: " + Skipped[p] + ".\n";
	return true;
}

}	// namespace nsf

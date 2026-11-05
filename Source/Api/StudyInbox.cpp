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

#include "StudyInbox.h"
#include "References.h"
#include "../Nsf/FtmFile.h"
#include "../Nsf/NsfFile.h"
#include "../Nsf/Study.h"
#include "../Nsf/Tracker.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>

namespace ftapi {

namespace {

const char *const REFERENCE_FILE_EXTENSION = ".json";

std::string Lower(std::string Text)
{
	for (size_t i = 0; i < Text.size(); ++i)
		Text[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(Text[i])));
	return Text;
}

std::string ExtensionOf(const std::string &Name)
{
	const size_t Dot = Name.find_last_of('.');
	const size_t Slash = Name.find_last_of("\\/");
	if (Dot == std::string::npos || (Slash != std::string::npos && Dot < Slash))
		return std::string();
	return Lower(Name.substr(Dot));
}

std::string NumberText(int N)
{
	char Buffer[16];
	std::snprintf(Buffer, sizeof(Buffer), "%d", N);
	return Buffer;
}

// Studied again: what the user tagged and noted stays. An NSF's study drops the old study's own tags (KeepOwnTags);
// a module's keeps them all, as study_module does
void KeepBefore(Reference &R, const StudyLibrary &Library, bool Nsf)
{
	std::string Old, Why;
	Reference Before;
	if (!Library.Read || !Library.Read(R.Id + REFERENCE_FILE_EXTENSION, Old) || !ReadReferenceFile(Old, Before, Why))
		return;
	if (Nsf) {
		KeepOwnTags(R, Before);
		return;
	}
	for (size_t t = 0; t < Before.Tags.size(); ++t)
		if (std::find(R.Tags.begin(), R.Tags.end(), Before.Tags[t]) == R.Tags.end())
			R.Tags.push_back(Before.Tags[t]);
	R.Notes = Before.Notes;
}

bool Keep(Reference &R, const StudyLibrary &Library, bool Nsf, StudyFileResult &Out)
{
	KeepBefore(R, Library, Nsf);
	std::string Error;
	if (!Library.Save || !Library.Save(R.Id + REFERENCE_FILE_EXTENSION, ReferenceFile(R), Error)) {
		Out.Error = "The study couldn't be kept in the reference library: " + (Error.empty() ? std::string("no library.") : Error);
		return false;
	}
	Out.Ids.push_back(R.Id);
	return true;
}

std::string IdsText(const std::vector<std::string> &Ids)
{
	std::string Out;
	const size_t Shown = std::min<size_t>(Ids.size(), 6);
	for (size_t i = 0; i < Shown; ++i)
		Out += (i ? ", " : "") + Ids[i];
	if (Ids.size() > Shown)
		Out += ", and " + NumberText(static_cast<int>(Ids.size() - Shown)) + " more";
	return Out;
}

std::string BaseName(const std::string &Name)
{
	const size_t Slash = Name.find_last_of("\\/");
	std::string Base = Slash == std::string::npos ? Name : Name.substr(Slash + 1);
	const size_t Dot = Base.find_last_of('.');
	return Dot == std::string::npos || Dot == 0 ? Base : Base.substr(0, Dot);
}

StudyFileResult StudyNsf(const std::string &LocalPath, const std::string &Source, const std::string &Date,
	const StudyLibrary &Library, const std::function<bool(const std::string &)> &Progress)
{
	StudyFileResult Out;
	nsf::NsfFile File;
	std::string Error;
	if (!nsf::ReadNsfFile(LocalPath, File, Error)) {
		Out.Error = "Not an NSF FamiTracker can read: " + Error;
		return Out;
	}
	bool Stopped = false;
	const std::vector<nsf::TrackSurvey> Survey = nsf::SurveyTracks(File, 45, [&](int Done, int Total) {
		if (Progress && !Progress("looking at its tracks, " + NumberText(Done + 1) + " of " + NumberText(Total)))
			Stopped = true;
		return !Stopped;
	});
	if (Stopped) {
		Out.Error = "stopped";
		return Out;
	}
	std::vector<int> Tracks;
	for (size_t i = 0; i < Survey.size(); ++i)
		if (Survey[i].Kind == "music")
			Tracks.push_back(Survey[i].Track);
	if (Tracks.empty())		// only jingles and sounds: study them all
		for (size_t i = 0; i < Survey.size(); ++i)
			if (Survey[i].Kind != "silent")
				Tracks.push_back(Survey[i].Track);
	if (Tracks.empty()) {
		Out.Error = "None of its tracks makes a sound.";
		return Out;
	}
	nsf::StudyOptions Options;
	Options.Progress = [&](int Done, int Total, const std::string &Track) {
		return !Progress || Progress("studying track " + NumberText(Done + 1) + " of " + NumberText(Total) +
			(Track.empty() ? std::string() : " (" + Track + ")"));
	};
	std::vector<nsf::StudiedTrack> Studied;
	if (!nsf::StudyTracks(File, Tracks, Options, Studied, Error)) {
		Out.Error = Error.empty() ? std::string("stopped") : Error;
		return Out;
	}
	for (size_t i = 0; i < Studied.size(); ++i) {
		if (!Studied[i].Problem.empty()) {
			Out.Skipped.push_back("track " + NumberText(Studied[i].Track + 1) + ": " + Studied[i].Problem);
			continue;
		}
		Reference R = MakeReference(File, Studied[i].Track, Studied[i].Study, Source, Date);
		if (!Keep(R, Library, true, Out))
			return Out;
	}
	if (Out.Ids.empty()) {
		Out.Error = "No track could be studied" + (Out.Skipped.empty() ? std::string(".") : ": " + Out.Skipped[0]);
		return Out;
	}
	Out.Ok = true;
	Out.What = NumberText(static_cast<int>(Out.Ids.size())) + (Out.Ids.size() == 1 ? " track" : " tracks") + " studied: " +
		IdsText(Out.Ids);
	if (!Out.Skipped.empty())
		Out.What += "; " + NumberText(static_cast<int>(Out.Skipped.size())) + " couldn't be (" + Out.Skipped[0] +
			(Out.Skipped.size() > 1 ? ", ..." : "") + ")";
	return Out;
}

StudyFileResult StudyFtm(const std::string &LocalPath, const std::string &Source, const std::string &Date,
	const StudyLibrary &Library, const std::function<bool(const std::string &)> &Progress)
{
	StudyFileResult Out;
	nsf::Module M;
	nsf::FtmInfo Info;
	std::string Error;
	if (!nsf::ReadFtmFile(LocalPath, M, Info, Error)) {
		Out.Error = "Not a FamiTracker module FamiTracker can read: " + Error;
		return Out;
	}
	for (int t = 0; t < static_cast<int>(M.Tracks.size()); ++t) {
		if (Progress && !Progress("studying track " + NumberText(t + 1) + " of " + NumberText(static_cast<int>(M.Tracks.size())))) {
			Out.Error = "stopped";
			return Out;
		}
		const nsf::TrackStudy Study = nsf::StudyTrack(M, t, nsf::FtmTrackReport(M, Info, t));
		Reference R = MakeFtmReference(M, t, Study, Source, Date);
		if (!Keep(R, Library, false, Out))
			return Out;
	}
	if (Out.Ids.empty()) {
		Out.Error = "The module has no tracks.";
		return Out;
	}
	Out.Ok = true;
	Out.What = NumberText(static_cast<int>(Out.Ids.size())) + (Out.Ids.size() == 1 ? " track" : " tracks") + " studied: " +
		IdsText(Out.Ids);
	return Out;
}

StudyFileResult StudyModule(const std::string &LocalPath, const std::string &Name, const std::string &Source,
	const std::string &Date, const StudyLibrary &Library, const std::function<bool(const std::string &)> &Progress)
{
	StudyFileResult Out;
	nsf::TrackerSong Song;
	std::string Error;
	if (Progress && !Progress("reading it")) {
		Out.Error = "stopped";
		return Out;
	}
	if (!nsf::ReadTrackerFile(LocalPath, Song, Error)) {
		Out.Error = "Not a module FamiTracker can read: " + Error;
		return Out;
	}
	if (Song.Title.empty())		// untitled: named after its file
		Song.Title = BaseName(Name);
	nsf::Module M;
	nsf::TrackReport Report;
	if (!nsf::TrackerToModule(Song, M, Report, Error)) {
		Out.Error = "It couldn't be studied: " + Error;
		return Out;
	}
	if (Progress && !Progress("studying it")) {
		Out.Error = "stopped";
		return Out;
	}
	const nsf::TrackStudy Study = nsf::StudyTrack(M, 0, Report);
	Reference R = MakeModuleReference(Song, Study, Source, Date);
	if (!Keep(R, Library, false, Out))
		return Out;
	Out.Ok = true;
	Out.What = "studied: " + R.Id;
	return Out;
}

}	// namespace

study_file_t StudyFileKind(const std::string &Name)
{
	const std::string Ext = ExtensionOf(Name);
	if (Ext == ".nsf" || Ext == ".nsfe")
		return STUDY_FILE_NSF;
	if (Ext == ".ftm")
		return STUDY_FILE_FTM;
	if (Ext == ".mod" || Ext == ".s3m" || Ext == ".xm" || Ext == ".it" || Ext == ".mid" || Ext == ".midi")
		return STUDY_FILE_MODULE;
	return STUDY_FILE_NONE;
}

std::string StudyFileKinds()
{
	return "NSF and NSFe files, FamiTracker modules (.ftm), tracker modules (.mod, .s3m, .xm, .it) and MIDI files (.mid, .midi)";
}

std::string StudyContentKey(const std::string &Bytes)
{
	unsigned long long Hash = 14695981039346656037ULL;
	for (size_t i = 0; i < Bytes.size(); ++i) {
		Hash ^= static_cast<unsigned char>(Bytes[i]);
		Hash *= 1099511628211ULL;
	}
	char Buffer[24];
	std::snprintf(Buffer, sizeof(Buffer), "%016llx", Hash);
	return Buffer;
}

std::string StudyLogLine(const std::string &When, const std::string &Name, const std::string &What, const std::string &Key)
{
	std::string Line = When + "  " + Name + ": " + What;
	for (size_t i = 0; i < Line.size(); ++i)
		if (Line[i] == '\r' || Line[i] == '\n')
			Line[i] = ' ';
	return Line + (Key.empty() ? std::string() : " [key " + Key + "]");
}

std::vector<std::string> StudiedKeys(const std::string &Log)
{
	std::vector<std::string> Keys;
	size_t At = 0;
	while (At < Log.size()) {
		size_t End = Log.find('\n', At);
		if (End == std::string::npos)
			End = Log.size();
		const std::string Line = Log.substr(At, End - At);
		const size_t Mark = Line.rfind("[key ");
		// failures are logged as "failed: ..."
		if (Mark != std::string::npos && Line.find(": failed: ") == std::string::npos && Line.size() >= Mark + 5 + 16) {
			const std::string Key = Line.substr(Mark + 5, 16);
			if (std::find(Keys.begin(), Keys.end(), Key) == Keys.end())
				Keys.push_back(Key);
		}
		At = End + 1;
	}
	return Keys;
}

std::string FreeStudyName(const std::string &Name, const std::function<bool(const std::string &)> &Taken)
{
	if (!Taken(Name))
		return Name;
	const std::string Ext = ExtensionOf(Name);
	const std::string Base = Name.substr(0, Name.size() - Ext.size());
	for (int n = 2; n < 1000; ++n) {
		const std::string Try = Base + " (" + NumberText(n) + ")" + Name.substr(Name.size() - Ext.size());
		if (!Taken(Try))
			return Try;
	}
	return Base + " (more)" + Name.substr(Name.size() - Ext.size());
}

std::vector<std::string> StudyFilesReady(const std::vector<StudyFolderEntry> &Now, const std::vector<StudyFolderEntry> &Before)
{
	std::map<std::string, const StudyFolderEntry *> Seen;
	for (size_t i = 0; i < Before.size(); ++i)
		Seen[Before[i].Name] = &Before[i];
	std::vector<std::string> Out;
	for (size_t i = 0; i < Now.size(); ++i) {
		const std::map<std::string, const StudyFolderEntry *>::const_iterator it = Seen.find(Now[i].Name);
		if (it != Seen.end() && it->second->Size == Now[i].Size && it->second->Time == Now[i].Time)
			Out.push_back(Now[i].Name);
	}
	std::sort(Out.begin(), Out.end());
	return Out;
}

StudyFileResult StudyFileIntoLibrary(const std::string &LocalPath, const std::string &Name, const std::string &Source,
	const std::string &Date, const StudyLibrary &Library, const std::function<bool(const std::string &)> &Progress)
{
	switch (StudyFileKind(Name)) {
	case STUDY_FILE_NSF:
		return StudyNsf(LocalPath, Source, Date, Library, Progress);
	case STUDY_FILE_FTM:
		return StudyFtm(LocalPath, Source, Date, Library, Progress);
	case STUDY_FILE_MODULE:
		return StudyModule(LocalPath, Name, Source, Date, Library, Progress);
	default: {
		StudyFileResult Out;
		Out.Error = "FamiTracker doesn't study " + (ExtensionOf(Name).empty() ? std::string("files without an extension") :
			ExtensionOf(Name) + " files") + ": it studies " + StudyFileKinds() + ".";
		return Out;
	}
	}
}

}	// namespace ftapi

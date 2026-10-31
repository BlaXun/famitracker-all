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

#include "Backups.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <set>

namespace ftapi {

namespace {

std::string Lower(const std::string &Text)
{
	std::string Out = Text;
	for (size_t i = 0; i < Out.size(); ++i)
		Out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(Out[i])));
	return Out;
}

bool EndsWithNoCase(const std::string &Text, const std::string &End)
{
	return Text.size() >= End.size() && Lower(Text.substr(Text.size() - End.size())) == Lower(End);
}

// At most Most bytes of Text, not cutting a UTF-8 character
std::string CutUtf8(const std::string &Text, size_t Most)
{
	if (Text.size() <= Most)
		return Text;
	size_t End = Most;
	while (End > 0 && (static_cast<unsigned char>(Text[End]) & 0xC0) == 0x80)
		--End;
	return Text.substr(0, End);
}

std::string TrimSpaces(const std::string &Text)
{
	size_t A = 0, Z = Text.size();
	while (A < Z && Text[A] == ' ')
		++A;
	while (Z > A && Text[Z - 1] == ' ')
		--Z;
	return Text.substr(A, Z - A);
}

bool Digits(const std::string &Text, size_t At, size_t Count)
{
	if (At + Count > Text.size())
		return false;
	for (size_t i = At; i < At + Count; ++i)
		if (!std::isdigit(static_cast<unsigned char>(Text[i])))
			return false;
	return true;
}

int Number(const std::string &Text, size_t At, size_t Count)
{
	return std::atoi(Text.substr(At, Count).c_str());
}

// "2026-10-31 14:05" (Separator ':'), or as in a file's name "2026-10-31 14-05" (Separator '-'), at At
bool ReadTime(const std::string &Text, size_t At, char Separator, LogTime &Time)
{
	if (!Digits(Text, At, 4) || Text.size() < At + 16 || Text[At + 4] != '-' || !Digits(Text, At + 5, 2) || Text[At + 7] != '-' ||
		!Digits(Text, At + 8, 2) || Text[At + 10] != ' ' || !Digits(Text, At + 11, 2) || Text[At + 13] != Separator ||
		!Digits(Text, At + 14, 2))
		return false;
	Time = LogTime(Number(Text, At, 4), Number(Text, At + 5, 2), Number(Text, At + 8, 2), Number(Text, At + 11, 2),
		Number(Text, At + 14, 2));
	return Time.Month >= 1 && Time.Month <= 12 && Time.Day >= 1 && Time.Day <= 31 && Time.Hour < 24 && Time.Minute < 60;
}

std::vector<std::string> Split(const std::string &Line, char By)
{
	std::vector<std::string> Out;
	size_t From = 0;
	for (;;) {
		const size_t At = Line.find(By, From);
		Out.push_back(Line.substr(From, At == std::string::npos ? std::string::npos : At - From));
		if (At == std::string::npos)
			return Out;
		From = At + 1;
	}
}

bool LaterThan(const LogTime &A, const LogTime &B)
{
	const int a[5] = { A.Year, A.Month, A.Day, A.Hour, A.Minute }, b[5] = { B.Year, B.Month, B.Day, B.Hour, B.Minute };
	for (int i = 0; i < 5; ++i)
		if (a[i] != b[i])
			return a[i] > b[i];
	return false;
}

}	// namespace

std::string BackupSongName(const std::string &SongFile)
{
	std::string Name = SongFile.substr(SongFile.find_last_of("\\/") == std::string::npos ? 0 : SongFile.find_last_of("\\/") + 1);
	if (EndsWithNoCase(Name, ".ftm"))
		Name.erase(Name.size() - 4);
	return Name.empty() ? std::string("Song") : Name;
}

std::string BackupIndexName(const std::string &SongFile)
{
	return BackupSongName(SongFile) + " backups.txt";
}

std::string CleanBackupLabel(const std::string &Label)
{
	std::string Out;
	for (size_t i = 0; i < Label.size(); ++i) {
		const unsigned char c = static_cast<unsigned char>(Label[i]);
		const bool Space = c < 0x20 || c == 0x7F || c == ' ';
		if (Space) {
			if (!Out.empty() && Out[Out.size() - 1] != ' ')
				Out += ' ';
		}
		else
			Out += static_cast<char>(c);
	}
	return TrimSpaces(CutUtf8(TrimSpaces(Out), MAX_BACKUP_LABEL));
}

std::string BackupFileName(const std::string &SongFile, const LogTime &Time, const std::string &Label,
	const std::vector<std::string> &Taken)
{
	// The label's words as a file name takes them: letters, digits, - and _ (and UTF-8's other letters whole), the rest
	// a space
	std::string Words;
	for (size_t i = 0; i < Label.size(); ++i) {
		const unsigned char c = static_cast<unsigned char>(Label[i]);
		const bool Keep = c >= 0x80 || std::isalnum(c) || c == '-' || c == '_';
		if (Keep)
			Words += static_cast<char>(c);
		else if (!Words.empty() && Words[Words.size() - 1] != ' ')
			Words += ' ';
	}
	Words = TrimSpaces(CutUtf8(TrimSpaces(Words), MAX_BACKUP_NAME_LABEL));
	char Stamp[32];
	std::snprintf(Stamp, sizeof(Stamp), "%04d-%02d-%02d %02d-%02d", Time.Year, Time.Month, Time.Day, Time.Hour, Time.Minute);
	const std::string Base = BackupSongName(SongFile) + " " + Stamp + (Words.empty() ? std::string() : " " + Words);
	std::set<std::string> Names;
	for (size_t i = 0; i < Taken.size(); ++i)
		Names.insert(Lower(Taken[i]));
	for (int n = 1; n <= MAX_BACKUP_NUMBER; ++n) {
		char Count[16];
		std::snprintf(Count, sizeof(Count), " (%d)", n);
		const std::string Name = Base + (n == 1 ? std::string() : std::string(Count)) + ".ftm";
		if (Names.count(Lower(Name)) == 0)
			return Name;
	}
	return Base + ".ftm";
}

std::string WriteBackupIndex(const std::string &SongFile, const std::vector<BackupEntry> &List)
{
	std::string Text = "# FamiTracker's backups of " + BackupSongName(SongFile) + ".ftm: number, time, file, label\r\n";
	for (size_t i = 0; i < List.size(); ++i) {
		char Head[24];
		std::snprintf(Head, sizeof(Head), "%d\t", List[i].Number);
		Text += Head + LogTimeText(List[i].Time) + "\t" + List[i].File + "\t" + CleanBackupLabel(List[i].Label) + "\r\n";
	}
	return Text;
}

std::vector<BackupEntry> ReadBackupIndex(const std::string &Text)
{
	std::vector<BackupEntry> Out;
	std::set<int> Numbers;
	for (const std::string &Raw : Split(Text, '\n')) {
		std::string Line = Raw;
		if (!Line.empty() && Line[Line.size() - 1] == '\r')
			Line.erase(Line.size() - 1);
		if (Line.empty() || Line[0] == '#')
			continue;
		const std::vector<std::string> Fields = Split(Line, '\t');
		if (Fields.size() < 4 || Fields[0].empty() || !Digits(Fields[0], 0, Fields[0].size()) || Fields[0].size() > 5)
			continue;
		BackupEntry E;
		E.Number = std::atoi(Fields[0].c_str());
		E.File = Fields[2];
		E.Label = CleanBackupLabel(Fields[3]);
		if (E.Number < 1 || Fields[1].size() != 16 || !ReadTime(Fields[1], 0, ':', E.Time) || E.File.empty() ||
			E.File.find_first_of("\\/:") != std::string::npos || !EndsWithNoCase(E.File, ".ftm") || !Numbers.insert(E.Number).second)
			continue;
		Out.push_back(E);
	}
	return Out;
}

std::vector<BackupEntry> SongBackups(const std::string &SongFile, const std::vector<BackupEntry> &Indexed,
	const std::vector<std::string> &Files)
{
	std::set<std::string> There, Named;
	for (size_t i = 0; i < Files.size(); ++i)
		There.insert(Lower(Files[i]));
	std::vector<BackupEntry> Out;
	for (size_t i = 0; i < Indexed.size(); ++i) {
		Named.insert(Lower(Indexed[i].File));
		if (There.count(Lower(Indexed[i].File)) != 0)
			Out.push_back(Indexed[i]);
	}
	// The song's backup files the index doesn't name: "<song> 2026-10-31 14-05[ label][ (n)].ftm"
	const std::string Prefix = BackupSongName(SongFile) + " ";
	std::vector<BackupEntry> Found;
	for (size_t i = 0; i < Files.size(); ++i) {
		const std::string &File = Files[i];
		LogTime Time;
		if (Named.count(Lower(File)) != 0 || File.size() < Prefix.size() + 16 + 4 || Lower(File.substr(0, Prefix.size())) != Lower(Prefix) ||
			!EndsWithNoCase(File, ".ftm") || !ReadTime(File, Prefix.size(), '-', Time))
			continue;
		const size_t After = Prefix.size() + 16;
		if (After + 4 < File.size() && File[After] != ' ')
			continue;
		BackupEntry E;
		E.Time = Time;
		E.File = File;
		std::string Label = TrimSpaces(File.substr(After, File.size() - 4 - After));
		const size_t Open = Label.rfind(" (");
		if (Open != std::string::npos && Label[Label.size() - 1] == ')' && Digits(Label, Open + 2, Label.size() - Open - 3))
			Label.erase(Open);
		else if (!Label.empty() && Label[0] == '(' && Label[Label.size() - 1] == ')' && Digits(Label, 1, Label.size() - 2))
			Label.clear();
		E.Label = CleanBackupLabel(Label);
		Found.push_back(E);
	}
	std::stable_sort(Found.begin(), Found.end(), [](const BackupEntry &A, const BackupEntry &B) { return LaterThan(B.Time, A.Time); });
	int Next = NextBackupNumber(Indexed);
	for (size_t i = 0; i < Found.size(); ++i) {
		Found[i].Number = Next++;
		Out.push_back(Found[i]);
	}
	std::stable_sort(Out.begin(), Out.end(), [](const BackupEntry &A, const BackupEntry &B) { return A.Number < B.Number; });
	return Out;
}

int NextBackupNumber(const std::vector<BackupEntry> &List)
{
	int Most = 0;
	for (size_t i = 0; i < List.size(); ++i)
		Most = std::max(Most, List[i].Number);
	return Most + 1;
}

int FindBackup(const std::vector<BackupEntry> &List, const std::string &Asked, std::string &Why)
{
	Why.clear();
	std::string Text = TrimSpaces(Asked);
	if (!Text.empty() && Text[0] == '#')
		Text.erase(0, 1);
	if (List.empty()) {
		Why = "The song has no backups yet (create_backup keeps one).";
		return -1;
	}
	if (Text.empty()) {
		Why = "Name a backup by its number or its label (list_backups lists them).";
		return -1;
	}
	if (Digits(Text, 0, Text.size()) && Text.size() <= 5) {
		const int Wanted = std::atoi(Text.c_str());
		for (size_t i = 0; i < List.size(); ++i)
			if (List[i].Number == Wanted)
				return static_cast<int>(i);
		Why = "There's no backup " + Text + " (list_backups lists them).";
		return -1;
	}
	const std::string Label = Lower(CleanBackupLabel(Text));
	for (size_t i = List.size(); i-- > 0; )
		if (Lower(List[i].Label) == Label)
			return static_cast<int>(i);	// the newest of that label
	// The words of one label (the newest backups of it)
	int Found = -1;
	std::set<std::string> Labels;
	for (size_t i = List.size(); i-- > 0; )
		if (Lower(List[i].Label).find(Label) != std::string::npos) {
			if (Found < 0)
				Found = static_cast<int>(i);
			Labels.insert(Lower(List[i].Label));
		}
	if (Labels.size() == 1)
		return Found;
	Why = Labels.empty() ? "No backup's label says \"" + Text + "\" (list_backups lists them)." :
		"Several backups' labels say \"" + Text + "\": name one by its number (list_backups lists them).";
	return -1;
}

std::string BackupLine(const BackupEntry &Entry)
{
	char Head[24];
	std::snprintf(Head, sizeof(Head), "%d. ", Entry.Number);
	return Head + LogTimeText(Entry.Time) + (Entry.Label.empty() ? std::string(", no label") : ", \"" + Entry.Label + "\"");
}

}	// namespace ftapi

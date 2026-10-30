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

#include "AgentLog.h"
#include "Song.h"
#include <cctype>
#include <cstdio>
#include <ctime>

namespace ftapi {

namespace {

const char *const NAMES[LOG_CATEGORIES] = { "idea", "plan", "research", "fix", "check", "note", "error", "change", "you" };
const char *const LABELS[LOG_CATEGORIES] = { "Idea", "Plan", "Research", "Fix", "Check", "Note", "Error", "Change", "You" };
const char *const UNREAD = "You, unread";	// a note of the user's no agent has read, in the file
const size_t MAX_LOG_FILE = 900 * 1024;		// bytes of a log's file at most (the oldest entries left out): the host reads 1 MB

std::string Lower(std::string Text)
{
	for (size_t i = 0; i < Text.size(); ++i)
		Text[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(Text[i])));
	return Text;
}

std::string Trimmed(const std::string &Text)
{
	size_t First = 0, Last = Text.size();
	while (First < Last && std::isspace(static_cast<unsigned char>(Text[First])))
		++First;
	while (Last > First && std::isspace(static_cast<unsigned char>(Text[Last - 1])))
		--Last;
	return Text.substr(First, Last - First);
}

bool Digits(const std::string &Line, size_t At, size_t Count)
{
	for (size_t i = At; i < At + Count; ++i)
		if (i >= Line.size() || !std::isdigit(static_cast<unsigned char>(Line[i])))
			return false;
	return true;
}

int Number(const std::string &Line, size_t At, size_t Count)
{
	int Value = 0;
	for (size_t i = At; i < At + Count; ++i)
		Value = Value * 10 + (Line[i] - '0');
	return Value;
}

// "2026-10-30 22:31  [Fix]  text": the entry, false for a line that isn't one
bool ReadEntryLine(const std::string &Line, LogEntry &Entry)
{
	if (Line.size() < 19 || !Digits(Line, 0, 4) || Line[4] != '-' || !Digits(Line, 5, 2) || Line[7] != '-' || !Digits(Line, 8, 2) ||
		Line[10] != ' ' || !Digits(Line, 11, 2) || Line[13] != ':' || !Digits(Line, 14, 2))
		return false;
	const size_t Open = Line.find('[', 16), Close = Line.find(']', 16);
	if (Open == std::string::npos || Close == std::string::npos || Close < Open || Trimmed(Line.substr(16, Open - 16)) != "")
		return false;
	Entry = LogEntry();
	Entry.Time = LogTime(Number(Line, 0, 4), Number(Line, 5, 2), Number(Line, 8, 2), Number(Line, 11, 2), Number(Line, 14, 2));
	const std::string Label = Lower(Trimmed(Line.substr(Open + 1, Close - Open - 1)));
	Entry.Category = LOG_NOTE;	// a label it doesn't know: a note
	for (int c = 0; c < LOG_CATEGORIES; ++c)
		if (Label == NAMES[c])
			Entry.Category = c;
	if (Label == Lower(UNREAD)) {
		Entry.Category = LOG_USER;
		Entry.Read = false;
	}
	size_t Text = Close + 1;
	while (Text < Line.size() && Line[Text] == ' ')
		++Text;
	Entry.Text = Line.substr(Text);
	return true;
}

}	// namespace

const char *LogCategoryName(int Category)
{
	return Category >= 0 && Category < LOG_CATEGORIES ? NAMES[Category] : NAMES[LOG_NOTE];
}

const char *LogCategoryLabel(int Category)
{
	return Category >= 0 && Category < LOG_CATEGORIES ? LABELS[Category] : LABELS[LOG_NOTE];
}

bool ParseLogCategory(const std::string &Text, int &Category)
{
	const std::string Name = Lower(Trimmed(Text));
	for (int c = 0; c < LOG_WRITTEN; ++c)
		if (Name == NAMES[c]) {
			Category = c;
			return true;
		}
	return false;
}

std::string LogCategoryList()
{
	std::string Text;
	for (int c = 0; c < LOG_WRITTEN; ++c)
		Text += std::string(c == 0 ? "" : c + 1 == LOG_WRITTEN ? " or " : ", ") + NAMES[c];
	return Text;
}

bool LogTime::operator==(const LogTime &Other) const
{
	return Year == Other.Year && Month == Other.Month && Day == Other.Day && Hour == Other.Hour && Minute == Other.Minute;
}

LogTime LogTimeNow()
{
	const std::time_t Now = std::time(0);
	std::tm Local;
#ifdef _MSC_VER
	if (localtime_s(&Local, &Now) != 0)
		return LogTime();
#else
	if (localtime_r(&Now, &Local) == 0)
		return LogTime();
#endif
	return LogTime(Local.tm_year + 1900, Local.tm_mon + 1, Local.tm_mday, Local.tm_hour, Local.tm_min);
}

std::string LogTimeText(const LogTime &Time)
{
	char Buffer[40];
	std::snprintf(Buffer, sizeof(Buffer), "%04d-%02d-%02d %02d:%02d", Time.Year, Time.Month, Time.Day, Time.Hour, Time.Minute);
	return Buffer;
}

std::string LogClockText(const LogTime &Time)
{
	char Buffer[16];
	std::snprintf(Buffer, sizeof(Buffer), "%02d:%02d", Time.Hour, Time.Minute);
	return Buffer;
}

std::string CleanLogText(const std::string &Text, bool *pCut)
{
	std::string Out;
	Out.reserve(Text.size());
	for (size_t i = 0; i < Text.size(); ++i) {
		const unsigned char c = static_cast<unsigned char>(Text[i]);
		if (c == '\r') {
			Out += '\n';
			if (i + 1 < Text.size() && Text[i + 1] == '\n')
				++i;
		}
		else if (c == '\n')
			Out += '\n';
		else if (c < 0x20 || c == 0x7F)
			Out += ' ';
		else
			Out += static_cast<char>(c);
	}
	// Each line's end trimmed, and the text's ends
	std::string Lines;
	size_t Start = 0;
	while (Start <= Out.size()) {
		size_t End = Out.find('\n', Start);
		if (End == std::string::npos)
			End = Out.size();
		size_t Last = End;
		while (Last > Start && Out[Last - 1] == ' ')
			--Last;
		Lines += Out.substr(Start, Last - Start);
		if (End < Out.size())
			Lines += '\n';
		Start = End + 1;
	}
	Out = Trimmed(Lines);
	const bool Cut = Out.size() > MAX_LOG_TEXT;
	if (Cut) {
		size_t At = MAX_LOG_TEXT - 3;
		while (At > 0 && (static_cast<unsigned char>(Out[At]) & 0xC0) == 0x80)
			--At;	// not inside a character
		Out = Trimmed(Out.substr(0, At)) + "...";
	}
	if (pCut != 0)
		*pCut = Cut;
	return Out;
}

long long AgentLog::Add(int Category, const std::string &Text, const LogTime &Time)
{
	LogEntry Entry;
	Entry.Id = m_NextId;
	Entry.Time = Time;
	Entry.Category = Category >= 0 && Category < LOG_CATEGORIES ? Category : LOG_NOTE;
	Entry.Text = CleanLogText(Text);
	Entry.Read = Entry.Category != LOG_USER;
	if (Entry.Category == LOG_ERROR && !m_Entries.empty() && m_Entries.back().Category == LOG_ERROR &&
		m_Entries.back().Text == Entry.Text)
		return m_Entries.back().Id;
	++m_NextId;
	m_Entries.push_back(Entry);
	if (m_Entries.size() > MAX_LOG_ENTRIES)
		m_Entries.erase(m_Entries.begin(), m_Entries.begin() + static_cast<long>(m_Entries.size() - MAX_LOG_ENTRIES));
	++m_Version;
	m_Unsaved = true;
	return Entry.Id;
}

std::vector<LogEntry> AgentLog::TakeNotes(bool All)
{
	std::vector<LogEntry> Out;
	bool Marked = false;
	for (size_t i = 0; i < m_Entries.size(); ++i) {
		LogEntry &Entry = m_Entries[i];
		if (Entry.Category != LOG_USER || (!All && Entry.Read))
			continue;
		Out.push_back(Entry);
		if (!Entry.Read) {
			Entry.Read = true;
			Marked = true;
		}
	}
	if (Marked) {
		++m_Version;
		m_Unsaved = true;
	}
	return Out;
}

int AgentLog::UnreadNotes() const
{
	int Count = 0;
	for (size_t i = 0; i < m_Entries.size(); ++i)
		Count += m_Entries[i].Category == LOG_USER && !m_Entries[i].Read ? 1 : 0;
	return Count;
}

void AgentLog::Clear()
{
	m_Entries.clear();
	++m_Version;
	m_Unsaved = true;
}

std::string AgentLog::FileText(const std::string &SongName) const
{
	const std::string Head = "# Agent log: " + SongName + "\r\n\r\nWhat the agents working on this song did and why, and the notes "
		"written to them, as FamiTracker's View > Agent log shows them, kept by FamiTracker next to the song.\r\n\r\n";
	std::vector<std::string> Lines;
	for (size_t i = 0; i < m_Entries.size(); ++i) {
		const LogEntry &Entry = m_Entries[i];
		const std::string Label = Entry.Category == LOG_USER && !Entry.Read ? UNREAD : LogCategoryLabel(Entry.Category);
		std::string Line = LogTimeText(Entry.Time) + "  [" + Label + "]  ";
		for (size_t c = 0; c < Entry.Text.size(); ++c)
			Line += Entry.Text[c] == '\n' ? std::string("\r\n  ") : std::string(1, Entry.Text[c]);
		Lines.push_back(Line + "\r\n");
	}
	// The newest entries that fit in the file
	size_t Size = Head.size(), First = Lines.size();
	while (First > 0 && Size + Lines[First - 1].size() <= MAX_LOG_FILE)
		Size += Lines[--First].size();
	std::string Text = Head;
	for (size_t i = First; i < Lines.size(); ++i)
		Text += Lines[i];
	return Text;
}

void AgentLog::ReadFileText(const std::string &Text)
{
	m_Entries.clear();
	size_t Start = 0;
	LogEntry Entry;
	bool Open = false;
	while (Start < Text.size()) {
		size_t End = Text.find('\n', Start);
		if (End == std::string::npos)
			End = Text.size();
		std::string Line = Text.substr(Start, End - Start);
		if (!Line.empty() && Line[Line.size() - 1] == '\r')
			Line.erase(Line.size() - 1);
		Start = End + 1;
		LogEntry Next;
		if (ReadEntryLine(Line, Next)) {
			if (Open)
				m_Entries.push_back(Entry);
			Entry = Next;
			Open = true;
		}
		else if (Open && Line.compare(0, 2, "  ") == 0)
			Entry.Text += "\n" + Line.substr(2);
		else if (Open) {
			m_Entries.push_back(Entry);
			Open = false;
		}
	}
	if (Open)
		m_Entries.push_back(Entry);
	for (size_t i = 0; i < m_Entries.size(); ++i) {
		m_Entries[i].Text = CleanLogText(m_Entries[i].Text);
		m_Entries[i].Id = m_NextId++;
	}
	if (m_Entries.size() > MAX_LOG_ENTRIES)
		m_Entries.erase(m_Entries.begin(), m_Entries.begin() + static_cast<long>(m_Entries.size() - MAX_LOG_ENTRIES));
	++m_Version;
	m_Unsaved = false;
}

std::string AgentLogFileName(const std::string &SongFileName)
{
	std::string Name = SongFileName;
	const size_t Slash = Name.find_last_of("\\/");
	if (Slash != std::string::npos)
		Name = Name.substr(Slash + 1);
	if (Name.empty())
		return std::string();
	if (Name.size() > 4 && Lower(Name.substr(Name.size() - 4)) == ".ftm")
		Name.erase(Name.size() - 4);
	return Name + " agent log.md";
}

// The host's log of the song open now (Song.h)

AgentLog &SongHost::Log()
{
	long long Serial = 0;
	std::string File;
	SongIdentity(Serial, File);
	if (!m_LogFollowed || Serial != m_LogSerial) {
		// Another song: its log from the file next to it, or none
		m_LogFollowed = true;
		m_LogSerial = Serial;
		m_LogFile = File;
		m_Log.Clear();
		std::string Bytes, Error;
		const std::string Name = AgentLogFileName(File);
		if (!Name.empty() && ReadFile(Name, Bytes, Error))
			m_Log.ReadFileText(Bytes);
		m_Log.MarkSaved();
	}
	else if (File != m_LogFile) {
		// The same song saved under a name (its first save, or Save As): the whole log goes next to it
		m_LogFile = File;
		if (!File.empty() && !m_Log.Entries().empty())
			m_Log.MarkUnsaved();
	}
	return m_Log;
}

bool SongHost::SaveLog(std::string &Error)
{
	Error.clear();
	AgentLog &Kept = Log();
	const std::string Name = AgentLogFileName(m_LogFile);
	if (!Kept.Unsaved() || Name.empty())
		return true;	// nothing new, or no file yet: kept until the song's first save
	if (Kept.Entries().empty()) {
		// Cleared: the file says so, if there's one
		std::string Bytes, Ignored;
		if (!ReadFile(Name, Bytes, Ignored)) {
			Kept.MarkSaved();
			return true;
		}
	}
	std::string SongName = m_LogFile;
	const size_t Slash = SongName.find_last_of("\\/");
	if (Slash != std::string::npos)
		SongName = SongName.substr(Slash + 1);
	if (SongName.size() > 4 && Lower(SongName.substr(SongName.size() - 4)) == ".ftm")
		SongName.erase(SongName.size() - 4);
	std::string Path;
	if (!SaveFile(Name, Kept.FileText(SongName), Path, Error))
		return false;
	Kept.MarkSaved();
	return true;
}

}	// namespace ftapi

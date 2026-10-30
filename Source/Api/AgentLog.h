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

// The agent log: what the agents working on a song did and why, in their own
// words (write_log, by category), the changes the tools made (a quieter line
// each, written by ToolSet::AfterCall) and the calls that failed, with the
// notes the user writes to the agents (read_user_notes). FamiTracker shows it
// in View > Agent log (Source/AgentLogPanel.cpp) and keeps it in a text file
// next to the song ("<song> agent log.md"). Plain C++14, tested on Linux.

#include <string>
#include <vector>

namespace ftapi {

// An entry's kind. The first LOG_WRITTEN are write_log's categories; then the change lines the tools write themselves
// and the user's notes. In the file by their labels, so the order can change
enum log_category_t {
	LOG_IDEA, LOG_PLAN, LOG_RESEARCH, LOG_FIX, LOG_CHECK, LOG_NOTE, LOG_ERROR,
	LOG_CHANGE,		// a change a tool made (its undo label)
	LOG_USER,		// a note the user wrote to the agents
	LOG_CATEGORIES
};
const int LOG_WRITTEN = LOG_CHANGE;				// write_log's categories: idea to error
const log_category_t LOG_DEFAULT = LOG_NOTE;		// write_log's without one

const size_t MAX_LOG_TEXT = 2000;		// bytes of an entry's text kept (more is cut, and write_log says so)
const size_t MAX_LOG_ENTRIES = 3000;	// entries kept in a song's log; the oldest go first

// "idea"... "change", "you": write_log's names (and the file's, lower case)
const char *LogCategoryName(int Category);
// "Idea"... "Change", "You": the panel's labels and the file's
const char *LogCategoryLabel(int Category);
// One of write_log's categories by name, any case ("Fix", " research "); false for anything else
bool ParseLogCategory(const std::string &Text, int &Category);
// "idea, plan, research, fix, check, note or error"
std::string LogCategoryList();

// A moment in this computer's time, to the minute
struct LogTime {
	int Year, Month, Day, Hour, Minute;
	LogTime() : Year(0), Month(0), Day(0), Hour(0), Minute(0) {}
	LogTime(int y, int mo, int d, int h, int mi) : Year(y), Month(mo), Day(d), Hour(h), Minute(mi) {}
	bool operator==(const LogTime &Other) const;
};
LogTime LogTimeNow();
std::string LogTimeText(const LogTime &Time);	// "2026-10-30 22:31"
std::string LogClockText(const LogTime &Time);	// "22:31"

struct LogEntry {
	long long Id;		// from 1, growing, never reused in a log
	LogTime Time;
	int Category;		// log_category_t
	std::string Text;	// UTF-8, lines apart by '\n'
	bool Read;			// a note of the user's that an agent has read (read_user_notes); true for the others
	LogEntry() : Id(0), Category(LOG_NOTE), Read(true) {}
};

// An entry's text as kept: line ends made '\n', tabs and other control characters spaces, the ends trimmed, at most
// MAX_LOG_TEXT bytes (cut at a character's start, "..." added); Cut says whether it was cut
std::string CleanLogText(const std::string &Text, bool *pCut = 0);

// The log of one song
class AgentLog {
public:
	AgentLog() : m_NextId(1), m_Version(0), m_Unsaved(false) {}

	// A new entry at Time, the newest; its Id. The text is cleaned (CleanLogText); an error just like the newest entry
	// isn't added again (its Id comes back), so a window that asks again and again fills nothing
	long long Add(int Category, const std::string &Text, const LogTime &Time);
	const std::vector<LogEntry> &Entries() const { return m_Entries; }
	// The user's notes the agents haven't read (or every one, All), oldest first, all marked read
	std::vector<LogEntry> TakeNotes(bool All);
	int UnreadNotes() const;
	void Clear();
	// Counts every change (an entry, a note read, cleared, read from a file), so a window knows when to draw again
	long long Version() const { return m_Version; }

	// Whether it has changed since it was last written to its file (or read from it)
	bool Unsaved() const { return m_Unsaved; }
	void MarkSaved() { m_Unsaved = false; }
	void MarkUnsaved() { m_Unsaved = true; }

	// The file: a heading naming the song, a line on what it is, then an entry a line, "2026-10-30 22:31  [Fix]  text",
	// a note not read yet "[You, unread]", the text's other lines after it indented by two spaces
	std::string FileText(const std::string &SongName) const;
	// The entries of such a text in place of these (lines that aren't entries are left out); not unsaved
	void ReadFileText(const std::string &Text);

private:
	std::vector<LogEntry> m_Entries;
	long long m_NextId;
	long long m_Version;
	bool m_Unsaved;
};

// The file next to a song that keeps its log: "Town.ftm" -> "Town agent log.md"; "" for a song without a file
std::string AgentLogFileName(const std::string &SongFileName);

}	// namespace ftapi

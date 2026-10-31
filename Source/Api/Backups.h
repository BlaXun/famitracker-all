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

// Backups of a song (GitHub issue #4): copies of the song as it was at a moment, each with a label saying why it was kept
// ("before messing with the intro"), so a batch of changes an agent made can be taken back in one go. They live in the
// Backups folder beside the song's file (BACKUPS_FOLDER), shared by the songs in that folder: each song's files are named
// after it with the time and the label, and its index file keeps the labels exactly ("<song> backups.txt"). The tools
// create_backup, list_backups and restore_backup and FamiTracker's File > Backups... read and write them through SongHost.
// Plain C++14, tested on Linux.

#include <string>
#include <vector>

#include "AgentLog.h"	// LogTime

namespace ftapi {

const char *const BACKUPS_FOLDER = "Backups";
const size_t MAX_BACKUP_LABEL = 120;		// bytes of a label kept (whole UTF-8 characters)
const size_t MAX_BACKUP_NAME_LABEL = 40;	// of it in the file's name
const int MAX_BACKUP_NUMBER = 99999;

// A backup: its number (from 1, in the order they were made, never reused), when, its file's name in the Backups folder
// and its label
struct BackupEntry {
	int Number;
	LogTime Time;
	std::string File;
	std::string Label;
	BackupEntry() : Number(0) {}
};

// "Town.ftm" -> "Town"
std::string BackupSongName(const std::string &SongFile);
// The index of a song's backups in the Backups folder: "Town backups.txt"
std::string BackupIndexName(const std::string &SongFile);
// A label as kept: one line, spaces trimmed and joined, at most MAX_BACKUP_LABEL bytes; "" when nothing's left
std::string CleanBackupLabel(const std::string &Label);
// A backup's file name: "Town 2026-10-31 14-05 before messing with the intro.ftm" (the label's letters, digits, spaces,
// - and _, at most MAX_BACKUP_NAME_LABEL bytes of it; other bytes of UTF-8 kept whole), numbered " (2)", " (3)"... when
// the name is taken (Taken: the folder's names, any case)
std::string BackupFileName(const std::string &SongFile, const LogTime &Time, const std::string &Label,
	const std::vector<std::string> &Taken);

// The index's text: a heading line, then a line a backup, "number<TAB>2026-10-31 14:05<TAB>file<TAB>label"
std::string WriteBackupIndex(const std::string &SongFile, const std::vector<BackupEntry> &List);
// Read back, the oldest first; lines that don't read are left out
std::vector<BackupEntry> ReadBackupIndex(const std::string &Text);

// A song's backups as they are on disk: the index's entries whose files are there (Files: the Backups folder's names),
// then the song's backup files the index doesn't name (copied there by hand, or the index lost), each numbered after the
// others, its time and label read from its name; the oldest first
std::vector<BackupEntry> SongBackups(const std::string &SongFile, const std::vector<BackupEntry> &Indexed,
	const std::vector<std::string> &Files);
// The next number for a new backup
int NextBackupNumber(const std::vector<BackupEntry> &List);

// The backup Asked names: its number ("3", "#3"), else its label (any case; a label's words when only one label has them:
// the newest of several alike). Its index in List, or -1 with Why
int FindBackup(const std::vector<BackupEntry> &List, const std::string &Asked, std::string &Why);

// "3. 2026-10-31 14:05, \"before messing with the intro\""
std::string BackupLine(const BackupEntry &Entry);

}	// namespace ftapi

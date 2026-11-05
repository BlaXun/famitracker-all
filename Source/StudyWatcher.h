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

// The study folder: files dropped into "To study" next to FamiTracker.exe (AppFolders.h) are studied into the reference
// library, as Claude Code's study_nsf and study_module study them (Api/StudyInbox.h), on a thread of their own while
// FamiTracker runs, then moved into its Done folder, or into Failed with a note why ("<file>.txt"). Each file gets a
// line in "Study log.txt" there; a file whose content was studied before goes to Done without studying it again. The
// status bar says what's being studied (WM_STUDY_NEWS to the main window).

#include <string>

const UINT WM_STUDY_NEWS = WM_APP + 0x3D1;	// lParam: a CString* for the status bar, deleted by the receiver

// Started once the main window is there; the folders are made then
void StartStudyWatcher(HWND hMainWindow);
// Stops at the next track (waits a moment, then leaves the thread to end with FamiTracker)
void StopStudyWatcher();

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

// Edit > Insert recording: turns a WAV recording (whistle, hum, finger taps)
// into notes at the pattern cursor, in the song that is open. See RECORDER.md.

class CMainFrame;

// Transcribes the WAV file at Path and writes the notes into the cursor's
// channel, starting at the cursor row and running on into the following
// frames (frames are added at the end of the song when needed). The noise
// channel gets drums, any other channel a melody. One Ctrl+Z undoes it.
//
// Returns false, with the reason in Message, when nothing was inserted. On
// success Message says what was inserted.
bool InsertRecording(CMainFrame *pMainFrm, LPCTSTR Path, CString &Message);

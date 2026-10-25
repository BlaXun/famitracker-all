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

// Ideas from a phone (Edit > Record from your phone, PhoneDlg.h): the takes
// the phone page sends (Api/PhoneTakes.h) go into the Recordings folder next
// to FamiTracker.exe (AppFolders.h) and become clips of the open song, each
// transcribed as Edit > Record from microphone transcribes a take
// (GuessRecording and InsertRecording with a clip name, InsertRecording.h)
// on the lead's channel (DefaultRecordingChannel), and named after the idea.
// The page's requests come through the Claude Code connection's listener
// and are answered on the UI thread (McpConnection.cpp), which owns the
// song; a take becomes a clip a moment later, from a thread timer, once the
// answer has gone. While FamiTracker is busy (a dialog open, Claude Code
// being answered, something dragged) the take waits in the folder, and the
// page says so; it never changes the song's patterns.

class CMainFrame;
namespace ftapi { class PhoneTakes; }

// The phone page's takes, for the connection's requests (McpConfig::pPhone): made the first time
ftapi::PhoneTakes *GetPhoneTakes(CMainFrame *pMainFrame);
// Before the main window goes, once nothing answers requests any more
void ClosePhoneTakes();

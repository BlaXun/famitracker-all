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

// File > Backups... (GitHub issue #4): the open song's backups (Api/Backups.h), the newest first with their time and
// label, and Create (with a label typed in), Restore and Open folder. It runs the tools create_backup, list_backups and
// restore_backup through CMcpConnection::RunTool, so FamiTracker and an agent keep and restore backups the same way: a
// restore first keeps a backup of the song as it is, then opens the one picked in the song's place, the song keeping its
// own file.

class CMainFrame;

void ShowBackups(CMainFrame *pMainFrm);

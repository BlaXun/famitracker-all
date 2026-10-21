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

// File > Import NSF: the tracks of an NSF file (game music), with the music
// picked out from the jingles and sound effects, as a new module: a track
// each, with patterns, instruments and drums worked out from what plays
// (Source/Nsf/Import.h). Claude Code's import_nsf opens its modules the same
// way, through OpenModuleText.

class CMainFrame;
class CFamiTrackerDoc;

// Asks for an NSF file and which of its tracks to import, and opens them
void ImportNsfFile(CMainFrame *pMain);

// Replaces the document with a module in FamiTracker's text format, as File >
// Import text does, titled Title (or else by the module's own title). The new
// module isn't saved yet. Empty on success, else what went wrong.
CString OpenModuleText(CMainFrame *pMain, CFamiTrackerDoc *pDoc, const CString &Text, const CString &Title);

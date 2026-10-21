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

// The folders FamiTracker keeps the user's own files in, next to
// FamiTracker.exe so that they go wherever FamiTracker goes:
//
// - References: the reference library (Api/References.h). Studies kept in
//   Documents\FamiTracker references (where the library was before) or in a
//   "FamiTracker references" folder next to the exe (the zip of studies,
//   unpacked there) move into it.
// - Recordings: the takes of Edit > Record from microphone and Edit > Sound
//   effect from your voice. Takes kept in Documents\FamiTracker recordings
//   (where they were before) move into it.
//
// Files move to another drive too; one of the same name already there stays
// where it was. Where FamiTracker's folder can't be written (Program Files),
// the folder stays in Documents. With Create, the folder is made when it
// isn't there yet; without, a folder that isn't there may come back (it
// lists as empty). Empty when there's no folder for it at all.

// FamiTracker.exe's folder, without a backslash at the end; empty when unknown
CString ExeFolder();

CString ReferencesFolder(bool Create);
CString RecordingsFolder(bool Create);

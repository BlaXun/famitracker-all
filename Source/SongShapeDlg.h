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

// Module > Song shape (also the frame editor's right-click and the piano
// roll's ruler): the shapes of Api/SongShapes.h with their parts' bars and
// what each is for; the one picked is laid out on the selected track by the
// song_shape tool (CMcpConnection::RunTool), as one Ctrl+Z. A track with
// notes keeps them all, and gets the empty bars a longer shape needs only
// after asking.

class CMainFrame;
class CFamiTrackerDoc;

void ShowSongShapes(CMainFrame *pMainFrm);

// What a section of the track is for, where it's named (the frame editor's
// tooltip, the piano roll's status line): "A' (bars 13-20): The tune again,
// ...", from its name and the shape the track's sections are (SongShapes.h);
// empty for no such section
CString SectionHoverLine(const CFamiTrackerDoc *pDoc, unsigned int Track, int Section);

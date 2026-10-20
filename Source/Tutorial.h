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

// Help > Tutorials: tutorials (TutorialScript.h) played over the main
// window. The window dims except for the part a step is about, which gets a
// bright frame, and a note beside it says what to do; the step goes on when
// the user has done it (or presses Next). The dimming lets the mouse through,
// so FamiTracker works as always while a tutorial runs.
//
// The built-in tutorials are res/tutorials/*.txt, in FamiTracker.exe as
// RCDATA; more are read from a Tutorials folder next to it.

class CMainFrame;

void ShowTutorials(CMainFrame *pMainFrame);	// the list; the one picked starts
void TutorialCommand(UINT nID);				// CMainFrame::OnCommand: a menu item, button or shortcut was used
bool TutorialRunning();

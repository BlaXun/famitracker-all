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

// Edit > Drum grid: the noise channel a bar at a time, as a grid of steps
// (a row each) with a line for each drum (DrumGrid.h). Clicking a step, or
// dragging along a line, puts drums on or takes them off, and plays them;
// ready-made beats fill a bar, which can be copied to the next bar or to
// every bar. Each change is written into the song at once, as one undoable
// action (WriteChannelRows), and the drums' instruments are added from the
// sound presets the first time they're needed.

class CMainFrame;

void ShowDrumGrid(CMainFrame *pMainFrame);

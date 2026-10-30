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

#include "PanelBar.h"

// View > Drum grid (also in the Edit menu, Ctrl+Shift+D): the noise channel
// a bar at a time, as a grid of steps (a row each) with a line for each drum
// (DrumGrid.h), in a panel docked along the bottom of the main window or
// along its right. It shows the bar the pattern editor's cursor is in and
// goes with the cursor (in follow mode, with the song as it plays); its <
// and > buttons show the bar before or after until the cursor moves.
// Clicking a step, or dragging along a line, puts drums on or takes them
// off, and plays them; ready-made beats fill a bar, which can be copied to
// the next bar or to every bar. Each change is written into the song at
// once, as one undoable action (WriteChannelRows), and the drums'
// instruments are added from the sound presets the first time they're
// needed. The main frame tells it when the song changes (SongChanged).

class CMainFrame;
class CDrumGridPanelWnd;	// the grid and its buttons (DrumGridPanel.cpp)

// The panel in a tab along the bottom of the main window (under the note
// views) or along its right, or in a window of its own (PanelBar.h). Where
// it is, its sizes and whether it was open are remembered.
class CDrumGridBar : public CPanelBar
{
public:
	CDrumGridBar();
	virtual ~CDrumGridBar();

	bool Create(CMainFrame *pFrame);	// hidden at first
	void SongChanged();						// the bar is read again
	static bool WasOpen();					// it was open when FamiTracker closed (closed the first time)

protected:
	virtual int PanelLeast(int Width) const;
	virtual void PanelOpened(bool Open);

private:
	CDrumGridPanelWnd *m_pPanel;

	CDrumGridBar(const CDrumGridBar &);
	CDrumGridBar &operator=(const CDrumGridBar &);
};

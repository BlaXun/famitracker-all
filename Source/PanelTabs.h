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

#include <cstddef>
#include <vector>

// The panels (View > Problems, What next, Ask, Agent log, Drum grid) each lie
// in one of three places: in the tabs along the bottom of the main window, in
// the tabs along its right, or in a window of their own. The open panels of a
// place docked in the main window share it: only the selected tab shows, so
// one panel's room is all the place takes (CMainFrame::ApplyPanels does the
// windows, CPanelBar draws the tab strip).
//
// This is plain C++ that the tests build on Linux (Source/Tests).

enum panel_place_t {
	PANEL_BOTTOM,		// a tab along the bottom of the main window
	PANEL_RIGHT,		// a tab along its right
	PANEL_WINDOW,		// a window of its own, anywhere (a second monitor too)
	PANEL_PLACES
};

const int NO_PANEL = -1;

// The place as the registry keeps it ("Place"); without one, as the panels
// kept it before they had tabs ("On the right")
panel_place_t StoredPanelPlace(int Stored, bool HasStored, bool OldOnTheRight);

// Which panels are open, where each one is and which tab each docked place
// shows. Panels are numbered from 0; the tabs go in that order.
class panel_tabs_t {
public:
	explicit panel_tabs_t(int Count = 0);

	int Count() const { return static_cast<int>(m_Panels.size()); }
	bool IsOpen(int Panel) const;
	panel_place_t Place(int Panel) const;
	// The panel shows: open, in its own window or the selected tab of its place
	bool Shows(int Panel) const;
	// The selected tab of a docked place, NO_PANEL when none is open there
	int Selected(panel_place_t Place) const;
	// The open panels of a place, in order
	std::vector<int> Tabs(panel_place_t Place) const;

	// Opened (from the View menu): it becomes the selected tab of its place
	void Open(int Panel);
	// Closed: its place shows the tab after it, else the one before
	void Close(int Panel);
	// To another place: the selected tab there, when it's open; the place it
	// left shows another tab
	void Move(int Panel, panel_place_t Place);
	// Its tab picked (an open, docked panel)
	void Select(int Panel);
	// The place it's in, before it opens (as it was remembered)
	void SetPlace(int Panel, panel_place_t Place);

private:
	struct panel_t {
		bool Open;
		panel_place_t Place;
		panel_t() : Open(false), Place(PANEL_BOTTOM) {}
	};
	bool Valid(int Panel) const { return Panel >= 0 && Panel < Count(); }
	void Leave(int Panel);		// its place shows another tab, if it was the selected one
	std::vector<panel_t> m_Panels;
	int m_Selected[PANEL_PLACES];
};

// The tabs' widths in a strip Room wide: each as wide as it wants (its name)
// while they fit; else the widest ones narrowed alike, none under Least (so
// they may run past Room then)
std::vector<int> TabWidths(const std::vector<int> &Wanted, int Room, int Least);

// The tab at x, the first starting at Left with Gap between them; -1 for none
int TabAt(const std::vector<int> &Widths, int Left, int Gap, int x);

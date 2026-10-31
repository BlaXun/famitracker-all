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

// Tests for the panels' tabs and windows (PanelTabs.cpp): one tab shows in
// each docked place, a panel opened becomes the tab shown, closing or moving
// one shows its neighbour, and the tab strip's widths

#include <cstdio>
#include <vector>
#include "PanelTabs.h"

namespace {

int g_checks = 0, g_failures = 0;

#define CHECK(x) do { ++g_checks; if (!(x)) { ++g_failures; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

enum { PROBLEMS, WHAT_NEXT, ASK, AGENT_LOG, DRUM_GRID, COUNT };

std::vector<int> List(int a = -1, int b = -1, int c = -1)
{
	std::vector<int> v;
	const int all[] = { a, b, c };
	for (int i = 0; i < 3; ++i)
		if (all[i] >= 0)
			v.push_back(all[i]);
	return v;
}

// The places as the registry keeps them, and as they were kept before tabs
void TestStoredPlace()
{
	CHECK(StoredPanelPlace(0, false, false) == PANEL_BOTTOM);
	CHECK(StoredPanelPlace(0, false, true) == PANEL_RIGHT);		// "On the right" from before
	CHECK(StoredPanelPlace(2, true, true) == PANEL_WINDOW);		// "Place" wins
	CHECK(StoredPanelPlace(1, true, false) == PANEL_RIGHT);
	CHECK(StoredPanelPlace(0, true, true) == PANEL_BOTTOM);
	CHECK(StoredPanelPlace(7, true, false) == PANEL_BOTTOM);	// damaged: along the bottom
	CHECK(StoredPanelPlace(-1, true, true) == PANEL_BOTTOM);
}

// Opened from the View menu: the tab shown; one tab shows in each place
void TestOpening()
{
	panel_tabs_t Tabs(COUNT);
	CHECK(Tabs.Selected(PANEL_BOTTOM) == NO_PANEL);
	CHECK(Tabs.Tabs(PANEL_BOTTOM).empty());
	CHECK(!Tabs.Shows(PROBLEMS));
	Tabs.Open(PROBLEMS);
	CHECK(Tabs.Selected(PANEL_BOTTOM) == PROBLEMS && Tabs.Shows(PROBLEMS));
	Tabs.Open(AGENT_LOG);
	CHECK(Tabs.Selected(PANEL_BOTTOM) == AGENT_LOG);
	CHECK(Tabs.Shows(AGENT_LOG) && !Tabs.Shows(PROBLEMS) && Tabs.IsOpen(PROBLEMS));
	CHECK(Tabs.Tabs(PANEL_BOTTOM) == List(PROBLEMS, AGENT_LOG));	// in order, whichever opened first
	Tabs.Open(WHAT_NEXT);
	CHECK(Tabs.Tabs(PANEL_BOTTOM) == List(PROBLEMS, WHAT_NEXT, AGENT_LOG));
	CHECK(Tabs.Selected(PANEL_BOTTOM) == WHAT_NEXT);
	// Opened again (it was open, not shown): shown
	Tabs.Open(PROBLEMS);
	CHECK(Tabs.Selected(PANEL_BOTTOM) == PROBLEMS && !Tabs.Shows(WHAT_NEXT));
	Tabs.Select(AGENT_LOG);
	CHECK(Tabs.Selected(PANEL_BOTTOM) == AGENT_LOG);
	// A closed panel's tab can't be picked
	Tabs.Select(ASK);
	CHECK(Tabs.Selected(PANEL_BOTTOM) == AGENT_LOG);
	// The right is a place of its own
	Tabs.SetPlace(DRUM_GRID, PANEL_RIGHT);
	Tabs.Open(DRUM_GRID);
	CHECK(Tabs.Selected(PANEL_RIGHT) == DRUM_GRID && Tabs.Selected(PANEL_BOTTOM) == AGENT_LOG);
	CHECK(Tabs.Shows(DRUM_GRID) && Tabs.Shows(AGENT_LOG));
	// A window shows whatever the tabs do
	Tabs.SetPlace(ASK, PANEL_WINDOW);
	Tabs.Open(ASK);
	CHECK(Tabs.Shows(ASK) && Tabs.Selected(PANEL_BOTTOM) == AGENT_LOG && Tabs.Selected(PANEL_RIGHT) == DRUM_GRID);
	CHECK(Tabs.Selected(PANEL_WINDOW) == NO_PANEL);
	CHECK(Tabs.Tabs(PANEL_WINDOW) == List(ASK));
	// Out of range: nothing happens
	Tabs.Open(COUNT);
	Tabs.Open(-1);
	CHECK(!Tabs.IsOpen(COUNT) && !Tabs.Shows(-1));
}

// Closed: the tab after it shows, else the one before
void TestClosing()
{
	panel_tabs_t Tabs(COUNT);
	Tabs.Open(PROBLEMS);
	Tabs.Open(ASK);
	Tabs.Open(DRUM_GRID);
	Tabs.Select(ASK);
	Tabs.Close(ASK);
	CHECK(Tabs.Selected(PANEL_BOTTOM) == DRUM_GRID && !Tabs.IsOpen(ASK));
	Tabs.Close(DRUM_GRID);
	CHECK(Tabs.Selected(PANEL_BOTTOM) == PROBLEMS);
	// A tab not shown closed: the one shown stays
	Tabs.Open(AGENT_LOG);
	Tabs.Select(PROBLEMS);
	Tabs.Close(AGENT_LOG);
	CHECK(Tabs.Selected(PANEL_BOTTOM) == PROBLEMS);
	Tabs.Close(PROBLEMS);
	CHECK(Tabs.Selected(PANEL_BOTTOM) == NO_PANEL && Tabs.Tabs(PANEL_BOTTOM).empty());
	// Closed twice: nothing more happens
	Tabs.Close(PROBLEMS);
	CHECK(Tabs.Selected(PANEL_BOTTOM) == NO_PANEL);
	// It keeps its place closed, and opens there
	Tabs.Move(WHAT_NEXT, PANEL_RIGHT);
	CHECK(Tabs.Place(WHAT_NEXT) == PANEL_RIGHT && !Tabs.IsOpen(WHAT_NEXT));
	Tabs.Open(WHAT_NEXT);
	CHECK(Tabs.Selected(PANEL_RIGHT) == WHAT_NEXT && Tabs.Selected(PANEL_BOTTOM) == NO_PANEL);
}

// To another place: the tab shown there; the place it left shows its neighbour
void TestMoving()
{
	panel_tabs_t Tabs(COUNT);
	Tabs.Open(PROBLEMS);
	Tabs.Open(WHAT_NEXT);
	Tabs.Open(AGENT_LOG);
	Tabs.Select(WHAT_NEXT);
	Tabs.Move(WHAT_NEXT, PANEL_RIGHT);
	CHECK(Tabs.Selected(PANEL_BOTTOM) == AGENT_LOG && Tabs.Selected(PANEL_RIGHT) == WHAT_NEXT);
	CHECK(Tabs.Tabs(PANEL_BOTTOM) == List(PROBLEMS, AGENT_LOG) && Tabs.Tabs(PANEL_RIGHT) == List(WHAT_NEXT));
	// A tab not shown moved: the one shown stays, it shows where it went
	Tabs.Select(PROBLEMS);
	Tabs.Move(AGENT_LOG, PANEL_RIGHT);
	CHECK(Tabs.Selected(PANEL_BOTTOM) == PROBLEMS && Tabs.Selected(PANEL_RIGHT) == AGENT_LOG);
	CHECK(Tabs.Tabs(PANEL_RIGHT) == List(WHAT_NEXT, AGENT_LOG));
	// To its own window: shown, and the right shows the other tab
	Tabs.Move(AGENT_LOG, PANEL_WINDOW);
	CHECK(Tabs.Shows(AGENT_LOG) && Tabs.Selected(PANEL_RIGHT) == WHAT_NEXT);
	// Back from its window into the tabs: the tab shown
	Tabs.Move(AGENT_LOG, PANEL_BOTTOM);
	CHECK(Tabs.Selected(PANEL_BOTTOM) == AGENT_LOG && Tabs.Shows(AGENT_LOG) && !Tabs.Shows(PROBLEMS));
	// To where it is: nothing changes
	Tabs.Select(PROBLEMS);
	Tabs.Move(AGENT_LOG, PANEL_BOTTOM);
	CHECK(Tabs.Selected(PANEL_BOTTOM) == PROBLEMS);
	// The last one in a place moved out: none shown there
	Tabs.Move(WHAT_NEXT, PANEL_BOTTOM);
	CHECK(Tabs.Selected(PANEL_RIGHT) == NO_PANEL && Tabs.Selected(PANEL_BOTTOM) == WHAT_NEXT);
	// No such place
	Tabs.Move(WHAT_NEXT, PANEL_PLACES);
	CHECK(Tabs.Place(WHAT_NEXT) == PANEL_BOTTOM);
}

// The tab strip: names in full while they fit, else the widest narrowed alike
void TestTabWidths()
{
	std::vector<int> Wanted;
	Wanted.push_back(80);
	Wanted.push_back(60);
	Wanted.push_back(120);
	CHECK(TabWidths(Wanted, 400, 30) == Wanted);
	CHECK(TabWidths(Wanted, 260, 30) == Wanted);			// just fits
	std::vector<int> Narrow = TabWidths(Wanted, 230, 30);	// the widest gives way first
	CHECK(Narrow[0] == 80 && Narrow[1] == 60 && Narrow[2] == 90);
	Narrow = TabWidths(Wanted, 180, 30);					// then the next
	CHECK(Narrow[0] == 60 && Narrow[1] == 60 && Narrow[2] == 60);
	Narrow = TabWidths(Wanted, 150, 30);
	CHECK(Narrow[0] == 50 && Narrow[1] == 50 && Narrow[2] == 50);
	int Total = 0;
	Narrow = TabWidths(Wanted, 200, 30);
	for (size_t i = 0; i < Narrow.size(); ++i)
		Total += Narrow[i];
	CHECK(Total <= 200 && Total >= 197);
	Narrow = TabWidths(Wanted, 40, 30);						// none under the least: past the room
	CHECK(Narrow[0] == 30 && Narrow[1] == 30 && Narrow[2] == 30);
	std::vector<int> Short;
	Short.push_back(10);
	CHECK(TabWidths(Short, 100, 30)[0] == 30);				// the least for a short name
	CHECK(TabWidths(std::vector<int>(), 100, 30).empty());
	// Which tab x is on, with gaps between them
	std::vector<int> Widths;
	Widths.push_back(50);
	Widths.push_back(70);
	CHECK(TabAt(Widths, 4, 2, 3) == -1);
	CHECK(TabAt(Widths, 4, 2, 4) == 0);
	CHECK(TabAt(Widths, 4, 2, 53) == 0);
	CHECK(TabAt(Widths, 4, 2, 54) == -1);					// the gap
	CHECK(TabAt(Widths, 4, 2, 56) == 1);
	CHECK(TabAt(Widths, 4, 2, 125) == 1);
	CHECK(TabAt(Widths, 4, 2, 126) == -1);
}

}	// namespace

int main()
{
	TestStoredPlace();
	TestOpening();
	TestClosing();
	TestMoving();
	TestTabWidths();
	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}

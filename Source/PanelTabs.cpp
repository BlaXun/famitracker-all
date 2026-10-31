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

#include "PanelTabs.h"
#include <algorithm>

panel_place_t StoredPanelPlace(int Stored, bool HasStored, bool OldOnTheRight)
{
	if (!HasStored)
		return OldOnTheRight ? PANEL_RIGHT : PANEL_BOTTOM;
	switch (Stored) {
	case PANEL_RIGHT: return PANEL_RIGHT;
	case PANEL_WINDOW: return PANEL_WINDOW;
	default: return PANEL_BOTTOM;
	}
}

panel_tabs_t::panel_tabs_t(int Count) : m_Panels(static_cast<size_t>(std::max(0, Count)))
{
	for (int p = 0; p < PANEL_PLACES; ++p)
		m_Selected[p] = NO_PANEL;
}

bool panel_tabs_t::IsOpen(int Panel) const
{
	return Valid(Panel) && m_Panels[static_cast<size_t>(Panel)].Open;
}

panel_place_t panel_tabs_t::Place(int Panel) const
{
	return Valid(Panel) ? m_Panels[static_cast<size_t>(Panel)].Place : PANEL_BOTTOM;
}

bool panel_tabs_t::Shows(int Panel) const
{
	if (!IsOpen(Panel))
		return false;
	const panel_place_t Where = Place(Panel);
	return Where == PANEL_WINDOW || Selected(Where) == Panel;
}

int panel_tabs_t::Selected(panel_place_t Place) const
{
	if (Place != PANEL_BOTTOM && Place != PANEL_RIGHT)
		return NO_PANEL;
	const int Panel = m_Selected[Place];
	// Kept true by every change, checked again all the same
	return IsOpen(Panel) && this->Place(Panel) == Place ? Panel : NO_PANEL;
}

std::vector<int> panel_tabs_t::Tabs(panel_place_t Place) const
{
	std::vector<int> Open;
	for (int i = 0; i < Count(); ++i)
		if (IsOpen(i) && this->Place(i) == Place)
			Open.push_back(i);
	return Open;
}

void panel_tabs_t::Leave(int Panel)
{
	const panel_place_t Where = Place(Panel);
	if (Where == PANEL_WINDOW || m_Selected[Where] != Panel)
		return;
	// The tab after it, else the one before (it's still counted here, open or not)
	int After = NO_PANEL, Before = NO_PANEL;
	for (int i = 0; i < Count(); ++i) {
		if (i == Panel || !IsOpen(i) || Place(i) != Where)
			continue;
		if (i > Panel && After == NO_PANEL)
			After = i;
		if (i < Panel)
			Before = i;
	}
	m_Selected[Where] = After != NO_PANEL ? After : Before;
}

void panel_tabs_t::Open(int Panel)
{
	if (!Valid(Panel))
		return;
	m_Panels[static_cast<size_t>(Panel)].Open = true;
	Select(Panel);
}

void panel_tabs_t::Close(int Panel)
{
	if (!IsOpen(Panel))
		return;
	Leave(Panel);
	m_Panels[static_cast<size_t>(Panel)].Open = false;
}

void panel_tabs_t::Move(int Panel, panel_place_t Place)
{
	if (!Valid(Panel) || Place < PANEL_BOTTOM || Place >= PANEL_PLACES || this->Place(Panel) == Place)
		return;
	if (IsOpen(Panel))
		Leave(Panel);
	m_Panels[static_cast<size_t>(Panel)].Place = Place;
	if (IsOpen(Panel))
		Select(Panel);
}

void panel_tabs_t::Select(int Panel)
{
	const panel_place_t Where = Place(Panel);
	if (IsOpen(Panel) && Where != PANEL_WINDOW)
		m_Selected[Where] = Panel;
}

void panel_tabs_t::SetPlace(int Panel, panel_place_t Place)
{
	if (IsOpen(Panel))
		Move(Panel, Place);
	else if (Valid(Panel) && Place >= PANEL_BOTTOM && Place < PANEL_PLACES)
		m_Panels[static_cast<size_t>(Panel)].Place = Place;
}

std::vector<int> TabWidths(const std::vector<int> &Wanted, int Room, int Least)
{
	std::vector<int> Widths(Wanted.size());
	long long Total = 0;
	for (size_t i = 0; i < Wanted.size(); ++i) {
		Widths[i] = std::max(Least, Wanted[i]);
		Total += Widths[i];
	}
	if (Total <= Room || Wanted.empty())
		return Widths;
	// The widest narrowed to a common width, as wide as lets them all fit (none under Least)
	std::vector<int> Sorted(Widths);
	std::sort(Sorted.begin(), Sorted.end());
	const int Count = static_cast<int>(Sorted.size());
	long long Under = 0;		// the widths of those narrower than the cap
	int Cap = Least;
	for (int i = 0; i < Count; ++i) {
		// With the cap at or under Sorted[i], the ones from i on are all at the cap
		const long long Left = Room - Under;
		const int Even = static_cast<int>(Left / (Count - i));
		if (Even <= Sorted[static_cast<size_t>(i)]) {
			Cap = std::max(Least, Even);
			break;
		}
		Under += Sorted[static_cast<size_t>(i)];
		Cap = Sorted[static_cast<size_t>(i)];
	}
	for (size_t i = 0; i < Widths.size(); ++i)
		Widths[i] = std::max(Least, std::min(Widths[i], Cap));
	return Widths;
}

int TabAt(const std::vector<int> &Widths, int Left, int Gap, int x)
{
	int At = Left;
	for (size_t i = 0; i < Widths.size(); ++i) {
		if (x >= At && x < At + Widths[i])
			return static_cast<int>(i);
		At += Widths[i] + Gap;
	}
	return -1;
}

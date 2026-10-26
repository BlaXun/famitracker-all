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

// The views notes are placed in (see NoteViews.h)

#include "NoteViews.h"
#include <algorithm>

note_views_t ShowNoteView(const note_views_t &Now, note_view_t Which, bool Show)
{
	note_views_t Views = Now;
	if (Which == NOTE_VIEW_PATTERN)
		Views.Pattern = Show;
	else
		Views.Roll = Show;
	// Never neither: the other one takes over
	if (!Views.Pattern && !Views.Roll) {
		if (Which == NOTE_VIEW_PATTERN)
			Views.Roll = true;
		else
			Views.Pattern = true;
	}
	return Views;
}

note_views_t ToggleNoteView(const note_views_t &Now, note_view_t Which)
{
	const bool Shown = Which == NOTE_VIEW_PATTERN ? Now.Pattern : Now.Roll;
	return ShowNoteView(Now, Which, !Shown);
}

note_views_t StartingNoteViews(bool Pattern, bool Roll)
{
	return note_views_t(Pattern || !Roll, Roll);
}

bool RollFillsPattern(const note_views_t &Views)
{
	return Views.Roll && !Views.Pattern;
}

std::vector<int> FitPanelBands(const std::vector<panel_band_t> &Bands, int Room, int Keep, int KeepLeast)
{
	std::vector<int> Heights(Bands.size(), 0);
	int Left = std::max(0, Room);
	const auto Take = [&Left](int Want) {
		const int Got = std::max(0, std::min(Left, Want));
		Left -= Got;
		return Got;
	};
	const int Views = std::max(0, Keep), ViewsLeast = std::max(0, std::min(KeepLeast, Views));
	Take(ViewsLeast);
	for (size_t i = Bands.size(); i-- > 0; )
		Heights[i] = Take(std::min(Bands[i].Least, Bands[i].Wanted));
	Take(Views - ViewsLeast);
	for (size_t i = Bands.size(); i-- > 0; )
		Heights[i] += Take(Bands[i].Wanted - Heights[i]);
	return Heights;
}

int PanelBandMost(const std::vector<int> &Heights, std::size_t Which, int Room, int Keep)
{
	if (Which >= Heights.size())
		return 0;
	int Used = std::max(0, Keep);
	for (size_t i = 0; i < Heights.size(); ++i)
		Used += Heights[i];
	return Heights[Which] + std::max(0, Room - Used);
}

int NoteViewsKeep(int ClientHeight, int Least, int Room)
{
	return std::max(0, std::min(Room, std::max(ClientHeight / 3, Least)));
}

std::vector<int> ShareRow(const std::vector<int> &Weights, int Width)
{
	std::vector<int> Widths(Weights.size());
	long long Total = 0;
	for (size_t i = 0; i < Weights.size(); ++i)
		Total += std::max(0, Weights[i]);
	int Given = 0;
	for (size_t i = 0; i < Weights.size(); ++i) {
		Widths[i] = i + 1 == Weights.size() ? std::max(0, Width - Given) : Total > 0 ?
			static_cast<int>(std::max(0, Weights[i]) * static_cast<long long>(std::max(0, Width)) / Total) :
			std::max(0, Width) / static_cast<int>(Weights.size());
		Given += Widths[i];
	}
	return Widths;
}

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

std::vector<int> FitPanelBands(const std::vector<panel_band_t> &Bands, int Room, int Keep)
{
	const int Budget = std::max(0, Room - std::max(0, Keep));
	std::vector<int> Heights(Bands.size());
	int Total = 0, Least = 0;
	for (size_t i = 0; i < Bands.size(); ++i) {
		const int Low = std::max(0, Bands[i].Least);
		Heights[i] = std::max(Low, Bands[i].Wanted);
		Total += Heights[i];
		Least += Low;
	}
	// Short of room: the later bands give way first, each down to its least
	for (size_t i = Bands.size(); i-- > 0 && Total > Budget; ) {
		const int Give = std::min(Total - Budget, Heights[i] - std::max(0, Bands[i].Least));
		if (Give > 0) {
			Heights[i] -= Give;
			Total -= Give;
		}
	}
	// Still short: every band under its least, in proportion to it (what rounding leaves to the first)
	if (Total > Budget) {
		int Left = Budget;
		for (size_t i = 0; i < Bands.size(); ++i) {
			Heights[i] = Least > 0 ? static_cast<int>(static_cast<long long>(std::max(0, Bands[i].Least)) * Budget / Least) :
				Budget / static_cast<int>(Bands.size());
			Left -= Heights[i];
		}
		if (!Heights.empty())
			Heights[0] += std::max(0, Left);
	}
	return Heights;
}

int PanelBandMost(const std::vector<panel_band_t> &Bands, const std::vector<int> &Heights, std::size_t Which, int Room, int Keep)
{
	if (Which >= Bands.size() || Which >= Heights.size())
		return 0;
	const int Budget = std::max(0, Room - std::max(0, Keep));
	int Used = 0;
	for (size_t i = 0; i < Heights.size(); ++i)
		Used += Heights[i];
	int Most = Heights[Which] + std::max(0, Budget - Used);
	for (size_t i = Which + 1; i < Bands.size() && i < Heights.size(); ++i)
		Most += std::max(0, Heights[i] - std::max(0, Bands[i].Least));
	return std::max(Most, Heights[Which]);
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

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

// View > Pattern editor and View > Piano roll: the two views notes are typed
// and drawn in. One of them always shows, so there's always somewhere to put
// notes: hiding the one shown alone shows the other. While the pattern
// editor is hidden, the piano roll fills its place and takes the note keys
// (CMainFrame::ShowNoteViews does the windows).
//
// This is plain C++ that the tests build on Linux (Source/Tests).

struct note_views_t {
	bool Pattern;			// the pattern editor, the channel grid
	bool Roll;				// the piano roll

	note_views_t(bool ShowPattern = true, bool ShowRoll = false) : Pattern(ShowPattern), Roll(ShowRoll) {}
	bool operator==(const note_views_t &Other) const { return Pattern == Other.Pattern && Roll == Other.Roll; }
	bool operator!=(const note_views_t &Other) const { return !(*this == Other); }
};

enum note_view_t {
	NOTE_VIEW_PATTERN,
	NOTE_VIEW_ROLL,
};

// The views after Which is shown or hidden: hiding the only one shown shows
// the other, and the rest stays as it is
note_views_t ShowNoteView(const note_views_t &Now, note_view_t Which, bool Show);

// The views after Which's menu item (with its check mark) is chosen
note_views_t ToggleNoteView(const note_views_t &Now, note_view_t Which);

// The views as FamiTracker remembers them from last time, at least one of
// them shown: the pattern editor when neither was
note_views_t StartingNoteViews(bool Pattern, bool Roll);

// The piano roll in the pattern editor's place: all the room it leaves, and
// the note keys
bool RollFillsPattern(const note_views_t &Views);

// The panels along the bottom of the main window (CMainFrame::PlaceBottomPanel)
// lie in bands, outermost first: the mixer along the window's edge, then the
// problems panel, What next and the drum grid side by side in one row. Room,
// the height between the window's top bars and its status bar, goes first to
// the note views (the pattern editor, the piano roll) as far as KeepLeast,
// enough to play in; then to each band as far as the least it's usable at
// (the row before the mixer: the innermost first); then to the note views as
// far as Keep, their share of the window; then to each band as far as the
// height it was made (dragged to). What's left is the note views' too. A band
// made shorter than its least keeps that height.
struct panel_band_t {
	int Wanted;		// the height it was made (dragged to)
	int Least;		// the least it's usable at
	panel_band_t(int WantedHeight = 0, int LeastHeight = 0) : Wanted(WantedHeight), Least(LeastHeight) {}
};

std::vector<int> FitPanelBands(const std::vector<panel_band_t> &Bands, int Room, int Keep, int KeepLeast);

// How tall dragging band Which's line may make it, the bands at Heights (as
// FitPanelBands gave them): as tall as it is and the room the note views have
// past Keep
int PanelBandMost(const std::vector<int> &Heights, std::size_t Which, int Room, int Keep);

// The room the note views keep: about a third of the main window's client
// height, at least Least (enough of the piano roll's lanes to play in), at
// most Room
int NoteViewsKeep(int ClientHeight, int Least, int Room);

// Panels side by side in a row Width wide: their widths, in proportion to
// Weights, adding up to Width
std::vector<int> ShareRow(const std::vector<int> &Weights, int Width);

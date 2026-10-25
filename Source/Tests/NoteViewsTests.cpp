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

// Tests for View > Pattern editor and View > Piano roll (NoteViews.cpp):
// there's always one view to place notes in

#include <cstdio>
#include "NoteViews.h"

namespace {

int g_checks = 0, g_failures = 0;

#define CHECK(x) do { ++g_checks; if (!(x)) { ++g_failures; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

const note_views_t GRID_ONLY(true, false), BOTH(true, true), ROLL_ONLY(false, true);

// Each menu item from each layout
void TestToggles()
{
	// View > Pattern editor
	CHECK(ToggleNoteView(BOTH, NOTE_VIEW_PATTERN) == ROLL_ONLY);			// the piano roll fills its place
	CHECK(ToggleNoteView(GRID_ONLY, NOTE_VIEW_PATTERN) == ROLL_ONLY);		// with the piano roll hidden, it shows
	CHECK(ToggleNoteView(ROLL_ONLY, NOTE_VIEW_PATTERN) == BOTH);			// back, and the piano roll stays
	// View > Piano roll
	CHECK(ToggleNoteView(GRID_ONLY, NOTE_VIEW_ROLL) == BOTH);
	CHECK(ToggleNoteView(BOTH, NOTE_VIEW_ROLL) == GRID_ONLY);
	CHECK(ToggleNoteView(ROLL_ONLY, NOTE_VIEW_ROLL) == GRID_ONLY);			// the pattern editor comes back
	// One menu item switches between the two views alone
	CHECK(ToggleNoteView(ToggleNoteView(GRID_ONLY, NOTE_VIEW_PATTERN), NOTE_VIEW_ROLL) == GRID_ONLY);
	// From every layout, every item leaves one view at least
	const note_views_t ALL[] = { GRID_ONLY, BOTH, ROLL_ONLY };
	for (int i = 0; i < 3; ++i)
		for (int v = 0; v < 2; ++v) {
			const note_views_t After = ToggleNoteView(ALL[i], v == 0 ? NOTE_VIEW_PATTERN : NOTE_VIEW_ROLL);
			CHECK(After.Pattern || After.Roll);
			CHECK(After != ALL[i]);		// and changes something
		}
}

// Shown or hidden from elsewhere (a tutorial step, the problems panel, the piano roll's close button)
void TestShowing()
{
	CHECK(ShowNoteView(GRID_ONLY, NOTE_VIEW_ROLL, true) == BOTH);
	CHECK(ShowNoteView(BOTH, NOTE_VIEW_ROLL, true) == BOTH);				// already shown: nothing changes
	CHECK(ShowNoteView(ROLL_ONLY, NOTE_VIEW_PATTERN, true) == BOTH);
	CHECK(ShowNoteView(ROLL_ONLY, NOTE_VIEW_PATTERN, false) == ROLL_ONLY);
	CHECK(ShowNoteView(GRID_ONLY, NOTE_VIEW_ROLL, false) == GRID_ONLY);
	CHECK(ShowNoteView(GRID_ONLY, NOTE_VIEW_PATTERN, false) == ROLL_ONLY);	// hiding the only one shows the other
	CHECK(ShowNoteView(ROLL_ONLY, NOTE_VIEW_ROLL, false) == GRID_ONLY);
	CHECK(ShowNoteView(BOTH, NOTE_VIEW_PATTERN, false) == ROLL_ONLY);
	// Even from nothing shown
	CHECK(ShowNoteView(note_views_t(false, false), NOTE_VIEW_ROLL, false) == GRID_ONLY);
	CHECK(ShowNoteView(note_views_t(false, false), NOTE_VIEW_PATTERN, false) == ROLL_ONLY);
}

// As FamiTracker starts
void TestStarting()
{
	CHECK(note_views_t() == GRID_ONLY);										// the first time
	CHECK(StartingNoteViews(true, false) == GRID_ONLY);
	CHECK(StartingNoteViews(true, true) == BOTH);
	CHECK(StartingNoteViews(false, true) == ROLL_ONLY);
	CHECK(StartingNoteViews(false, false) == GRID_ONLY);					// never neither
	CHECK(RollFillsPattern(ROLL_ONLY) && !RollFillsPattern(BOTH) && !RollFillsPattern(GRID_ONLY));
}

}	// namespace

int main()
{
	TestToggles();
	TestShowing();
	TestStarting();
	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}

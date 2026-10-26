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
#include <vector>
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

// The panels along the bottom: the mixer, then the problems panel, What next and the drum grid in one row. On a window
// 1000 pixels high (697 between its top bars and the status bar, a client height of 947) and one 768 high (463 and 715),
// all of them open: the note views keep enough lanes to play in, then the row, then the mixer get their least, then the
// note views a third of the window and the panels the heights they were made
void TestPanelBands()
{
	const panel_band_t MIXER(290, 250), ROW(200, 175);
	std::vector<panel_band_t> Bands;
	Bands.push_back(MIXER);
	Bands.push_back(ROW);
	// Room for both
	const int Tall = NoteViewsKeep(1340, 240, 1100);
	CHECK(Tall == 446);
	std::vector<int> H = FitPanelBands(Bands, 1100, Tall, 240);
	CHECK(H.size() == 2 && H[0] == 290 && H[1] == 200);
	// 1000 pixels: both at their least, the note views the rest (more than enough to play in, less than a third)
	const int Keep = NoteViewsKeep(947, 240, 697);
	CHECK(Keep == 315);
	H = FitPanelBands(Bands, 697, Keep, 240);
	CHECK(H[0] == 250 && H[1] == 175);
	// Dragging: neither can grow into the note views' share; shorter, the note views get the room
	CHECK(PanelBandMost(H, 0, 697, Keep) == 250 && PanelBandMost(H, 1, 697, Keep) == 175);
	Bands[1].Wanted = 150;
	H = FitPanelBands(Bands, 697, Keep, 240);
	CHECK(H[0] == 250 && H[1] == 150);
	Bands[1].Wanted = 200;
	// 768 pixels: the note views enough to play in, the row its least, the mixer what's left
	const int Small = NoteViewsKeep(715, 240, 463);
	CHECK(Small == 240);
	H = FitPanelBands(Bands, 463, Small, 240);
	CHECK(H[0] == 48 && H[1] == 175);
	// A short window: the note views get their least, not a third, and never more than the room
	CHECK(NoteViewsKeep(450, 220, 300) == 220);
	CHECK(NoteViewsKeep(450, 220, 150) == 150);
	// One band: what it was made, as far as the room goes
	std::vector<panel_band_t> One(1, panel_band_t(150, 60));
	CHECK(FitPanelBands(One, 710, 316, 240)[0] == 150);
	CHECK(FitPanelBands(One, 300, 220, 220)[0] == 80);
	CHECK(FitPanelBands(One, 200, 200, 220)[0] == 0);
	// Made shorter than its least, it keeps that height
	CHECK(FitPanelBands(std::vector<panel_band_t>(1, panel_band_t(100, 250)), 900, 300, 240)[0] == 100);
	// Room to spare: a band can grow into it
	std::vector<panel_band_t> Two;
	Two.push_back(panel_band_t(150, 60));
	Two.push_back(panel_band_t(150, 70));
	H = FitPanelBands(Two, 710, 316, 240);
	CHECK(H[0] == 150 && H[1] == 150);
	CHECK(PanelBandMost(H, 0, 710, 316) == 244 && PanelBandMost(H, 1, 710, 316) == 244);
	CHECK(PanelBandMost(H, 2, 710, 316) == 0);
	// The row's widths: in proportion, adding up
	std::vector<int> Weights;
	Weights.push_back(340);
	Weights.push_back(340);
	Weights.push_back(430);
	const std::vector<int> W = ShareRow(Weights, 1280);
	CHECK(W.size() == 3 && W[0] == 392 && W[1] == 392 && W[0] + W[1] + W[2] == 1280);
	CHECK(ShareRow(std::vector<int>(1, 430), 999)[0] == 999);
	CHECK(ShareRow(std::vector<int>(), 999).empty());
}

int main()
{
	TestToggles();
	TestShowing();
	TestStarting();
	TestPanelBands();
	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}

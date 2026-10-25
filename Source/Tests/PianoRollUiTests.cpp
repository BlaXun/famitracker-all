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

// Tests for the piano roll's toolbar rows, status hints and menu clicks
// (PianoRollUi.cpp): every button has a place at any width, the hints name
// the keys only while they are the piano roll's, and a click that closed a
// menu is told from the next one

#include <cstdio>
#include <string>
#include "PianoRollUi.h"

namespace {

int g_checks = 0, g_failures = 0;

#define CHECK(x) do { ++g_checks; if (!(x)) { ++g_failures; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

// The piano roll's buttons as DEFAULT_GUI_FONT measures them (text, the margins and a menu's arrow): the channel; Draw,
// Paint melody, Select; Grid, Key, Snap to key; Instrument, Effects, Chords, Groove, Clips
const int WIDTHS[] = { 106, 44, 86, 52, 90, 146, 82, 168, 72, 72, 72, 60 };
const int GROUPS[] = { 0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 3, 3 };
const int GAP = 4, GROUP_GAP = 12, CLOSE = 32;	// the close button and its gap, at the first row's right end

std::vector<int> Widths() { return std::vector<int>(WIDTHS, WIDTHS + sizeof(WIDTHS) / sizeof(WIDTHS[0])); }
std::vector<int> Groups() { return std::vector<int>(GROUPS, GROUPS + sizeof(GROUPS) / sizeof(GROUPS[0])); }

int Total()
{
	int Sum = 0;
	for (size_t i = 0; i < sizeof(WIDTHS) / sizeof(WIDTHS[0]); ++i)
		Sum += WIDTHS[i] + (i == 0 ? 0 : GROUPS[i] != GROUPS[i - 1] ? GROUP_GAP : GAP);
	return Sum;
}

// Every button in a row, left to right without overlapping, inside its row (unless it's wider than a row and alone on
// its own), in their order
bool Tidy(const std::vector<int> &W, const std::vector<roll_place_t> &P, int Rows, int First, int Width)
{
	if (P.size() != W.size())
		return false;
	int MaxRow = 0;
	for (size_t i = 0; i < P.size(); ++i) {
		const int Room = P[i].Row == 0 ? First : Width;
		if (P[i].Row < 0 || P[i].X < 0)
			return false;
		const bool Alone = P[i].X == 0 && (i + 1 == P.size() || P[i + 1].Row != P[i].Row);
		if (P[i].X + W[i] > Room && !Alone)
			return false;
		if (i > 0) {
			if (P[i].Row < P[i - 1].Row)
				return false;
			if (P[i].Row == P[i - 1].Row && P[i].X < P[i - 1].X + W[i - 1] + GAP)
				return false;
		}
		MaxRow = P[i].Row > MaxRow ? P[i].Row : MaxRow;
	}
	return Rows == MaxRow + 1;
}

void TestFlow()
{
	const std::vector<int> W = Widths(), G = Groups();
	std::vector<roll_place_t> P;

	// Wide enough: one row, the groups apart
	CHECK(FlowRollButtons(W, G, Total(), Total(), GAP, GROUP_GAP, P) == 1);
	CHECK(Tidy(W, P, 1, Total(), Total()));
	CHECK(P[1].X == W[0] + GROUP_GAP && P[2].X == P[1].X + W[1] + GAP);

	// About 840 pixels of window: everything after the grid used to be cut off; now two rows, Chords and Groove on the second
	const int Narrow = 800;
	const int Rows = FlowRollButtons(W, G, Narrow - CLOSE, Narrow, GAP, GROUP_GAP, P);
	CHECK(Rows == 2);
	CHECK(Tidy(W, P, Rows, Narrow - CLOSE, Narrow));
	CHECK(P[9].Row == 1 && P[10].Row == 1);		// Chords, Groove
	CHECK(P[7].Row == 1 && P[7].X == 0);		// the group moves to the new row whole

	// Any width: every button has a place
	for (int Width = 20; Width <= 2000; Width += 3) {
		const int First = Width > CLOSE ? Width - CLOSE : Width;
		const int N = FlowRollButtons(W, G, First, Width, GAP, GROUP_GAP, P);
		CHECK(N >= 1 && N <= static_cast<int>(W.size()));
		CHECK(Tidy(W, P, N, First, Width));
	}

	// A group longer than a row breaks where it must; a button wider than a row gets one of its own
	CHECK(FlowRollButtons(W, G, 300, 300, GAP, GROUP_GAP, P) >= 3);
	CHECK(P[7].Row != P[11].Row);
	const std::vector<int> Wide(3, 50);
	std::vector<int> Big(Wide);
	Big[1] = 500;
	CHECK(FlowRollButtons(Big, std::vector<int>(3, 0), 200, 200, GAP, GROUP_GAP, P) == 3);
	CHECK(P[0].Row == 0 && P[1].Row == 1 && P[1].X == 0 && P[2].Row == 2);

	// The first row is the narrower one
	CHECK(FlowRollButtons(Wide, std::vector<int>(3, 0), 100, 400, GAP, GROUP_GAP, P) == 2);
	CHECK(P[0].Row == 0 && P[1].Row == 1 && P[2].Row == 1);

	// Nothing to place
	CHECK(FlowRollButtons(std::vector<int>(), std::vector<int>(), 100, 100, GAP, GROUP_GAP, P) == 1 && P.empty());
}

bool Has(const std::string &Text, const char *Part)
{
	return Text.find(Part) != std::string::npos;
}

// The tools' letters and the menus' keys: E (effects), H (chords), D, B and S (the tools)
bool NamesALetter(const std::string &Text)
{
	return Has(Text, "E:") || Has(Text, "press E") || Has(Text, "H:") || Has(Text, " H ") || Has(Text, "(D)") || Has(Text, "(B)") ||
		Has(Text, "(S)");
}

void TestHints()
{
	// Every case, both ways
	for (int Typing = 0; Typing < 2; ++Typing)
		for (int Tool = ROLL_TOOL_DRAW; Tool <= ROLL_TOOL_SELECT; ++Tool)
			for (int Selected = 0; Selected <= 3; Selected += 3)
				for (int Snaps = 0; Snaps < 2; ++Snaps)
					for (int Empty = 0; Empty < 2; ++Empty) {
						roll_hint_t S;
						S.TypesNotes = Typing != 0;
						S.Tool = Tool;
						S.Selected = Selected;
						S.Snaps = Snaps != 0;
						S.Empty = Empty != 0 && Selected == 0;
						const std::string Hint = RollHint(S);
						CHECK(!Hint.empty());
						if (S.TypesNotes)
							CHECK(!NamesALetter(Hint));		// E types a note there: no "E: effects"
						if (Selected > 0)
							CHECK(Has(Hint, "3 notes selected") && Has(Hint, "Groove"));
						if (S.Empty)
							CHECK(Has(Hint, "Groove") && Has(Hint, "mood"));
					}

	roll_hint_t S;
	// With the pattern editor showing, the letters are the piano roll's
	S.Tool = ROLL_TOOL_DRAW;
	CHECK(Has(RollHint(S), "press E") && Has(RollHint(S), "H: chords"));
	S.Snaps = true;
	CHECK(Has(RollHint(S), "E: effects"));
	S.Selected = 1;
	CHECK(Has(RollHint(S), "1 note selected") && Has(RollHint(S), "E: effects") && Has(RollHint(S), "H: chords"));
	S.Selected = 0;
	S.Tool = ROLL_TOOL_SELECT;
	CHECK(Has(RollHint(S), "H puts chords"));
	// In its place they're notes: the buttons and the double-click instead
	S.TypesNotes = true;
	CHECK(Has(RollHint(S), "Chords puts chords"));
	S.Tool = ROLL_TOOL_DRAW;
	S.Snaps = false;
	CHECK(Has(RollHint(S), "Double-click a note for effects") && Has(RollHint(S), "Chords: a chord"));
	S.Snaps = true;
	CHECK(Has(RollHint(S), "Double-click a note for effects"));
	S.Selected = 2;
	CHECK(Has(RollHint(S), "Effects: their effects") && Has(RollHint(S), "Chords: chords on their bars"));
	// An empty song points at the groove, and at what each tool does
	S.Selected = 0;
	S.Empty = true;
	CHECK(Has(RollHint(S), "Or click to add a note"));
	S.Tool = ROLL_TOOL_PAINT;
	CHECK(Has(RollHint(S), "paint a melody"));
}

void TestMenuClick()
{
	roll_menu_click_t Click;
	CHECK(!ClosedAMenu(Click, 0, 0, 0));				// no menu saw one
	Click.Seen = true;
	Click.Time = 5000;
	Click.X = 320;
	Click.Y = 240;
	CHECK(ClosedAMenu(Click, 5000, 320, 240));			// the very click
	CHECK(ClosedAMenu(Click, 5000 + MENU_CLICK_SLACK_MS, 320, 240));	// passed on a little later
	CHECK(!ClosedAMenu(Click, 5000 + MENU_CLICK_SLACK_MS + 1, 320, 240));	// a click of its own
	CHECK(!ClosedAMenu(Click, 5000, 321, 240));			// elsewhere
	CHECK(!ClosedAMenu(Click, 5000, 320, 239));
	CHECK(!ClosedAMenu(Click, 4999, 320, 240));			// before the menu saw it
	// The tick count wraps around after 49.7 days
	Click.Time = 0xFFFFFFF0u;
	CHECK(ClosedAMenu(Click, 0x00000010u, 320, 240));
	CHECK(!ClosedAMenu(Click, 0xFFFFFFE0u, 320, 240));
}

}	// namespace

int main()
{
	TestFlow();
	TestHints();
	TestMenuClick();
	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}

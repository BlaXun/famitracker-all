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

// The piano roll's toolbar, status line and mouse, worked out apart from the
// window (PianoRoll.cpp): where its buttons go at any width, what the status
// line says the mouse and the keys do, and which click only closed a menu.
//
// This is plain C++ that the tests build on Linux (Source/Tests).

#include <cstdint>
#include <string>
#include <vector>

// Where the buttons along the piano roll's top go, in rows: Widths are the
// buttons left to right, Gap apart, and Groups[i] numbers the run of buttons
// that button i belongs to (GroupGap apart). The first row is FirstWidth
// wide (the close button sits at its right end), the others Width. A group
// goes on the row it starts on when it fits there, else on a new row, and a
// group longer than a row breaks where it must; a button wider than a row
// gets a row of its own. So every button gets a place, however narrow the
// window. Places gets each button's row and x; returns the number of rows, 1
// at least.
//
// When the piano roll is too short for more than MaxRows rows (its notes
// need the room), the buttons that don't fit in them go into a menu: their
// Row is -1, and More is where the button that opens it goes, at the end of
// the last row, which keeps MoreWidth (and a Gap) for it. More's Row is -1
// when every button has a place of its own.
struct roll_place_t {
	int Row;
	int X;
	roll_place_t() : Row(0), X(0) {}
	roll_place_t(int R, int Left) : Row(R), X(Left) {}
};

int FlowRollButtons(const std::vector<int> &Widths, const std::vector<int> &Groups, int FirstWidth, int Width, int Gap,
	int GroupGap, std::vector<roll_place_t> &Places);
int FlowRollButtons(const std::vector<int> &Widths, const std::vector<int> &Groups, int FirstWidth, int Width, int Gap,
	int GroupGap, int MaxRows, int MoreWidth, std::vector<roll_place_t> &Places, roll_place_t &More);

// What the status line along the piano roll's bottom says while the mouse
// isn't over a note and nothing else is going on: what the mouse and the keys
// do now. While the pattern editor is hidden the piano roll takes its note
// keys (TypesNotes), so the letters that pick tools and open menus (D, B, S,
// E and H) type notes instead and the hints name the buttons.
enum roll_tool_t {
	ROLL_TOOL_DRAW,
	ROLL_TOOL_PAINT,
	ROLL_TOOL_SELECT,
};

struct roll_hint_t {
	int Selected;		// notes selected in the channel being edited
	int Tool;			// roll_tool_t
	bool Snaps;			// new notes land on the key's notes
	bool TypesNotes;	// in the pattern editor's place: the note keys type notes
	bool Empty;			// the song has no notes yet
	roll_hint_t() : Selected(0), Tool(ROLL_TOOL_DRAW), Snaps(false), TypesNotes(false), Empty(false) {}
};

std::string RollHint(const roll_hint_t &State);

// Strong notes on the beat: the row whose chord's notes the piano roll lights.
// Where a note is being placed (drawn, painted or moved, starting at
// PlaceRow), or where the mouse is over the notes while Draw or Paint is
// picked, ready to place one; else where the cursor is. While a clip is being
// placed, or something else is dragged (a box, the ruler, erasing), the
// cursor's. -1 for none.
enum roll_placing_t {
	ROLL_PLACING_NONE,
	ROLL_PLACING_NOTE,		// a note drawn, painted or moved
	ROLL_PLACING_OTHER,		// another drag
};

struct roll_strong_t {
	int Tool;			// roll_tool_t
	int Placing;		// roll_placing_t
	int PlaceRow;		// where the note being placed starts
	int MouseRow;		// the mouse's row over the notes, -1 when it isn't over them
	int CursorRow;		// from the song's start, -1 for none
	bool PlacingClip;
	roll_strong_t() : Tool(ROLL_TOOL_DRAW), Placing(ROLL_PLACING_NONE), PlaceRow(-1), MouseRow(-1), CursorRow(-1), PlacingClip(false) {}
};

int StrongNotesRow(const roll_strong_t &State);

// A click that only closed a menu. A mouse button pressed outside an open
// menu closes it, and Windows (Wine always) passes that click on to the
// window under the mouse, where it would draw a note or start a box. The
// menu looks at the click before it closes (the piano roll sees the messages
// a menu looks at through a WH_MSGFILTER hook, MSGF_MENU): a button message
// the piano roll gets later is that click when it comes from the same point
// at (or just after) the same time.
struct roll_menu_click_t {
	bool Seen;				// a button was pressed while a menu was open
	std::uint32_t Time;		// the message's time (GetMessageTime), in ms
	long X, Y;				// where, on the screen (GetMessagePos)
	roll_menu_click_t() : Seen(false), Time(0), X(0), Y(0) {}
};

const std::uint32_t MENU_CLICK_SLACK_MS = 100;	// a click passed on late, as a new message

bool ClosedAMenu(const roll_menu_click_t &MenuClick, std::uint32_t Time, long X, long Y);

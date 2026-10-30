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

// The piano roll's toolbar rows, status hints and menu clicks (see PianoRollUi.h).

#include "PianoRollUi.h"

namespace {

// The flow, with the row LastRow (if any) Shorter narrower
int Flow(const std::vector<int> &Widths, const std::vector<int> &Groups, int FirstWidth, int Width, int Gap, int GroupGap,
	int LastRow, int Shorter, std::vector<roll_place_t> &Places)
{
	const size_t Count = Widths.size();
	Places.assign(Count, roll_place_t());
	// A button without a group number is a group of its own
	const auto GroupOf = [&Groups](size_t i) { return i < Groups.size() ? Groups[i] : -1 - static_cast<int>(i); };
	int Row = 0, X = 0;
	bool Empty = true;		// nothing on the row yet
	const auto RowWidth = [&]() { return (Row == 0 ? FirstWidth : Width) - (Row == LastRow ? Shorter : 0); };
	for (size_t First = 0; First < Count; ) {
		size_t End = First;
		int Long = 0;
		while (End < Count && GroupOf(End) == GroupOf(First)) {
			Long += (End > First ? Gap : 0) + Widths[End];
			++End;
		}
		if (!Empty && X + GroupGap + Long > RowWidth()) {
			++Row;
			X = 0;
			Empty = true;
		}
		for (size_t i = First; i < End; ++i) {
			const int Space = i == First ? GroupGap : Gap;
			if (!Empty && X + Space + Widths[i] > RowWidth()) {
				++Row;
				X = 0;
				Empty = true;
			}
			Places[i].Row = Row;
			Places[i].X = Empty ? 0 : X + Space;
			X = Places[i].X + Widths[i];
			Empty = false;
		}
		First = End;
	}
	return Row + 1;
}

}	// namespace

int FlowRollButtons(const std::vector<int> &Widths, const std::vector<int> &Groups, int FirstWidth, int Width, int Gap,
	int GroupGap, std::vector<roll_place_t> &Places)
{
	return Flow(Widths, Groups, FirstWidth, Width, Gap, GroupGap, -1, 0, Places);
}

int FlowRollButtons(const std::vector<int> &Widths, const std::vector<int> &Groups, int FirstWidth, int Width, int Gap,
	int GroupGap, int MaxRows, int MoreWidth, std::vector<roll_place_t> &Places, roll_place_t &More)
{
	More = roll_place_t(-1, 0);
	MaxRows = MaxRows < 1 ? 1 : MaxRows;
	const int Rows = Flow(Widths, Groups, FirstWidth, Width, Gap, GroupGap, -1, 0, Places);
	if (Rows <= MaxRows)
		return Rows;
	// The last row keeps room for More; what doesn't fit by then goes into its menu
	const int Last = MaxRows - 1;
	Flow(Widths, Groups, FirstWidth, Width, Gap, GroupGap, Last, MoreWidth + Gap, Places);
	More = roll_place_t(Last, 0);
	for (size_t i = 0; i < Places.size(); ++i) {
		if (Places[i].Row > Last)
			Places[i].Row = -1;
		else if (Places[i].Row == Last)
			More.X = Places[i].X + Widths[i] + Gap;
	}
	return MaxRows;
}

namespace {

std::string Notes(int Count)
{
	return std::to_string(Count) + (Count == 1 ? " note" : " notes");
}

}	// namespace

std::string RollHint(const roll_hint_t &State)
{
	// The letters are the piano roll's keys only while the pattern editor shows; in its place they type notes, and the
	// buttons do the same
	const bool Keys = !State.TypesNotes;
	if (State.Selected > 0)
		return Notes(State.Selected) + " selected   |   Ctrl+Shift+C: save as a clip. Drag to move (Shift: copy), arrows to move, "
			"Shift+arrows: octave, Alt+Up / Alt+Down: louder / softer, Delete, Ctrl+C / Ctrl+V, " + std::string(Keys ? "E: effects, H: chords on their bars, " :
			"Effects: their effects, Chords: chords on their bars, ") + "Groove: drums, bass and chords under them, Esc: none";
	if (State.Empty) {
		std::string Text = "The song is empty. Groove (at the top) starts it: pick a mood, and drums, a bass line and chords "
			"come in. ";
		if (State.Tool == ROLL_TOOL_PAINT)
			Text += "Or drag to paint a melody.";
		else if (State.Tool == ROLL_TOOL_SELECT)
			Text += "Or pick Draw and click to add a note.";
		else
			Text += "Or click to add a note.";
		return Text;
	}
	if (State.Tool == ROLL_TOOL_PAINT)
		return "Paint: drag to paint a melody, a note per grid step; hold a pitch for a longer note. Right-click erases. "
			"Double-click a faint note to edit its channel.";
	if (State.Tool == ROLL_TOOL_SELECT)
		return std::string("Select: drag around notes, or along the ruler above for whole bars. Click a note to select it (Shift: "
			"more), double-click a faint one to edit its channel. Then Ctrl+Shift+C saves them as a clip, ") + (Keys ? "H" : "Chords") + " puts chords on their bars and Groove "
			"a groove under them.";
	if (State.Snaps)
		return std::string("Click to add a note: it lands on the key's notes (the lighter lanes; Alt: any note). Double-click a "
			"faint note to edit its channel. Drag a note to move it (Shift: copy), its end to change its length. Right-click "
			"erases. Ctrl+drag selects. ") +
			(Keys ? "E: effects." : "Double-click a note for effects.") + " Ctrl+Z undoes.";
	return std::string("Click to add a note, drag to make it longer. Double-click a faint note to edit its channel. Drag a "
		"note to move it (Shift: copy), its end to change its length. Right-click erases. Ctrl+drag selects. ") + (Keys ? "Double-click a note (or press E) for effects. "
		"H: chords. " : "Double-click a note for effects. Chords: a chord on the free channels. ") + "Ctrl+Z undoes.";
}

std::string VolumeStepText(int Steps, int Count, int Low, int High, bool Changed)
{
	const bool Louder = Steps > 0;
	const std::string Volume = (Low == High ? "volume " + std::to_string(Low) : "volumes " + std::to_string(Low) + " to " +
		std::to_string(High)) + " of 15" + (High == 0 ? ", silent" : "");
	if (!Changed)
		return std::string(Count == 1 ? "The note is" : "The notes are") + " as " + (Louder ? "loud" : "soft") + " as " +
			(Count == 1 ? "it goes" : "they go") + " already: " + Volume + ".";
	const int Size = Steps < 0 ? -Steps : Steps;
	std::string Text = std::string(Louder ? "Louder" : "Softer") + ": " + Volume + " (" + Notes(Count);
	if (Size > 1 || Low != High)
		Text += std::string(Count == 1 ? ", " : ", each ") + (Size == 1 ? "a step" : std::to_string(Size) + " steps") +
			(Louder ? " louder" : " softer");
	Text += "). Ctrl+Z undoes it";
	if (Size == 1)
		Text += std::string(", Shift+Alt+") + (Louder ? "Up" : "Down") + " goes 4 steps";
	return Text + ".";
}

std::string FaintNoteHint(int Tool, const std::string &Faint, const std::string &Bright)
{
	std::string Text = "Double-click to edit " + Faint;
	if (Tool == ROLL_TOOL_DRAW)
		Text += " (a click adds a note to " + Bright + " here)";
	else if (Tool == ROLL_TOOL_PAINT)
		Text += " (a drag paints " + Bright + "'s notes here)";
	return Text;
}

int StrongNotesRow(const roll_strong_t &State)
{
	if (State.Placing == ROLL_PLACING_NOTE)
		return State.PlaceRow;
	if (State.Placing == ROLL_PLACING_NONE && !State.PlacingClip && State.MouseRow >= 0 &&
		(State.Tool == ROLL_TOOL_DRAW || State.Tool == ROLL_TOOL_PAINT))
		return State.MouseRow;
	return State.CursorRow;
}

bool ClosedAMenu(const roll_menu_click_t &MenuClick, std::uint32_t Time, long X, long Y)
{
	if (!MenuClick.Seen || X != MenuClick.X || Y != MenuClick.Y)
		return false;
	// Its own time, or a little later when it's passed on as a new message (the tick count wraps around)
	const std::uint32_t Later = Time - MenuClick.Time;
	return Later <= MENU_CLICK_SLACK_MS;
}

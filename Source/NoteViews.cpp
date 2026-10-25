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

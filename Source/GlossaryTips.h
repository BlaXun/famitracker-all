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

// Explanations in place, from the glossary (Api/Glossary.h): a tooltip where
// the mouse rests on an effect in the pattern editor (what it does with its own
// numbers, Api/Effects.h's DescribeEffect), on a song setting's label (Speed,
// Tempo or BPM, Rows, Frames, Step, the row highlights, Octave: a click on the
// label opens the glossary there), on a sequence's name in the instrument
// editor, and on the parts of windows that draw their own (the mixer's Soft,
// Space and pan, the piano roll's Key, Snap to key, Chords and Groove, each
// registered with AddGlossaryTipArea). F1 while a tip shows opens the glossary
// at its entry. The status bar says the same about the effect under the
// pattern editor's cursor. One tracking tooltip, polled from a timer of a
// message-only window: nothing here needs the windows' own messages.

#include <functional>
#include <string>

class CMainFrame;

struct glossary_tip_t {
	CString Text;		// what the tooltip says
	std::string Id;		// the glossary entry F1 opens, "" for none
	int Key;			// tells the parts of one window apart (a knob, a button, a cell): another starts the tip again
	glossary_tip_t() : Key(0) {}
};

// A window that draws its own parts: the tip at a point of its client area, false for none
typedef std::function<bool(CPoint Point, glossary_tip_t &Tip)> glossary_hit_t;

void StartGlossaryHelp(CMainFrame *pMainFrame);		// the end of the main frame's OnCreate
void StopGlossaryHelp();							// its OnDestroy
void AddGlossaryTipArea(CWnd *pWnd, const glossary_hit_t &HitTest);

// A label's tip (where it is and its text as shown, Api/Glossary.h's GlossaryLabels): the entry's first sentence and
// how to read more; false when the label has no entry
bool GlossaryLabelTip(const char *Where, LPCTSTR Label, int Key, glossary_tip_t &Tip, bool Clickable = false);

// F1: the glossary at the entry the tooltip showing names, or at the effect under the pattern editor's cursor while it
// has the keys; false when there's neither (FamiTracker's own help then)
bool GlossaryHelpKey(CMainFrame *pMainFrame);

// The pattern editor's cell whose effect the tooltip shows (its frame, row, channel and cursor column); false when none
bool GlossaryTipCell(int &Frame, int &Row, int &Channel, int &Column);

// The effect line for a cell's effect ("447  Vibrato: the pitch wobbles, speed 4, depth 7") and its entry; false for no
// effect there
bool GlossaryEffectLine(int Track, int Frame, int Row, int Channel, int Column, CString &Line, std::string &Id);

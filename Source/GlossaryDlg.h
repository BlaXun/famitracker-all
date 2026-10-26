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

// Help > Glossary: a window of its own beside the song (modeless, owned by the
// main window, a tool window), shown and hidden by Help > Glossary (a check
// mark), its toolbar button and Ctrl+Shift+G. A search box (terms and their
// other names, then the texts), the categories, the terms, and the entry shown:
// its text, its other names, Hear it (GlossaryHear.h) and its see-also links.
// It shows without taking the keys, so the note keys go on typing notes in the
// pattern editor or the piano roll until it's clicked; Esc and its close box
// hide it. It remembers whether it was open, where it was and its size (the
// "Glossary" registry section). The entries are Api/Glossary.h's.

#include <string>

class CMainFrame;

void ToggleGlossary(CMainFrame *pMainFrame);
bool IsGlossaryOpen();
// Shown at an entry (its id or one of its names): the effects' hover's F1, the labels' clicks
void ShowGlossaryAt(CMainFrame *pMainFrame, const std::string &Id);
// Shown again when FamiTracker starts if it was open when it closed (StartGlossaryHelp)
void RestoreGlossary(CMainFrame *pMainFrame);
void CloseGlossary();		// the main window's OnDestroy

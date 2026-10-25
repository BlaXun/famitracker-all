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

// Instrument > Sound browser (Ctrl+Shift+L): every sound the song can get
// (Api/SoundIndex.h, the same index find_sound searches): the ready-made sounds
// for its chips, the sounds of the reference library and the song's own
// instruments, found by words and filtered by source and channel, and shown in
// groups: the kinds on the left with their counts, and in the list a heading
// for each kind and inside it for the ready-made sounds, each game of the
// reference library and the song's own, which fold (a kind's many games start
// folded). Every click on a sound plays it on its channel (the one picked too,
// and its play mark), as do the arrow keys (a key held down plays where it
// stops), Space and Listen; the note keys (or a MIDI keyboard) play it while
// the list has the focus. A sound not in the song is tried as an instrument of
// its own, which goes out of the song again unless Add to the song keeps it.
// The window can be resized; it remembers its place, kind and folds while
// FamiTracker runs.

class CMainFrame;

void ShowSoundBrowser(CMainFrame *pMainFrame);

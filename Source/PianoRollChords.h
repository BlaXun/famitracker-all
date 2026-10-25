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

// The piano roll's chord helper on the document (PianoRoll.cpp's Chords
// menu; Api/ChordHelper.h works the chords out): the selected track as the
// helper reads it, and a plan written into the song, the voices' sounds
// made into instruments first, as one undoable action. Include after
// FamiTrackerDoc.h.

class CMainFrame;
namespace ftapi { struct HelperSong; struct ChordPlan; struct Key; }

// The track's channels (their cells, the song's rows of them), its
// instruments, its rows a beat and a bar, and the key the piano roll shows
// (chosen, or found from the notes; HasKey false for none)
void ReadChordSong(const CFamiTrackerDoc *pDoc, unsigned int Track, const ftapi::Key &K, bool HasKey, ftapi::HelperSong &Out);

// Writes the plan: each voice's sound made into an instrument where the
// song has none yet (from the presets, the N163's wave sounds at the places
// the plan gives, the VRC7's patches, the FDS's sounds), then the voices'
// channels, as one undoable action (one Ctrl+Z), the cursor staying in its
// channel and going to CursorRow (a song row). Sounds names the instruments
// the voices play ("Soft chords"; "String pad, String pad 2 and String pad
// 3"). False with the reason in Message when nothing was written.
bool WriteChordPlan(CMainFrame *pMainFrm, ftapi::HelperSong &Song, const ftapi::ChordPlan &Plan, int CursorRow, CString &Sounds,
	CString &Message);

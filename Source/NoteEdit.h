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

struct stChanNote;

// The notes of one channel, and the edits the piano roll makes to them.
//
// The channel's cells are held a row at a time from the start of the song
// (frame * pattern length + row). A note starts on a row with a note and
// lasts until the next note, release or halt in the channel, or the end; a
// channel plays one note at a time.
//
// A note owns its rows: the note column of its first row; the instrument,
// volume and effect columns of every row it lasts; and its release or halt,
// if it ends on one. Edits move, cut or clear what notes own. Effects that
// steer the whole song (Fxx, Bxx, Dxx, Cxx) belong to their row and stay.
//
// Edits keep the notes they don't move sounding as before. A volume or
// instrument that a note took over from an earlier row is written into its
// own cell when the edit would change it. Effects that switch something off
// (000, 100, 200, 300, 400, 700, A00) go where a note now ends when it is
// cut short, moved or removed, so an arpeggio or vibrato doesn't run on into
// the notes after it.
//
// No MFC and no document: the logic is tested on Linux (Source/Tests).
class CChannelNotes
{
public:
	struct Note {
		int Start;			// rows from the start of the song
		int End;			// the row of the next note, release or halt, or GetRows()
		int Pitch;			// where it lands, 0 = C-0 ... 95 = B-7: a Qxy or Rxy slide counts at its target
		int Written;		// the pitch in the cell
		int Instrument;		// in effect, -1 if none was set yet
		int Volume;			// in effect, 0-15
		bool Released;		// ends on a release (===)
		bool Halted;		// ends on a halt (---)
	};

	CChannelNotes();
	CChannelNotes(const CChannelNotes &Other);
	CChannelNotes &operator=(const CChannelNotes &Other);
	~CChannelNotes();

	// The cells
	void Reset(int Rows);								// all empty
	int GetRows() const;
	void SetRows(int Rows);								// empty rows added at the end, or rows cut off
	void GetCell(int Row, stChanNote &Cell) const;
	void SetCell(int Row, const stChanNote &Cell);

	// How the channel plays
	void SetEffectColumns(int Columns);					// the effect columns that play, 1-4 (the rest are left alone)
	void SetCanRelease(int Instrument, bool Release);	// ends of new notes: === when it fades out on a release, --- otherwise
	void SetPitchRange(int Lowest, int Highest);		// 0-95; the noise channel's notes are 0-15

	// The notes, in order
	int GetNoteCount() const;
	const Note &GetNote(int Index) const;
	int FindNote(int Start) const;			// the note starting at Start, -1 if none
	int NoteAt(int Row) const;				// the note sounding at Row, -1 if none

	// Edits, naming notes by where they start
	void Add(int Start, int End, int Pitch, int Instrument);	// a new note; the notes it covers make way
	void Remove(int Start);
	void Resize(int Start, int End);		// it can grow up to the next note
	// Moves (or copies) notes by Rows rows, each to its new pitch (where it lands); they keep how
	// they sounded, and the notes they land on make way
	void Move(const int *pStarts, int Count, int Rows, const int *pPitches, bool Copy);
	void SetInstrument(const int *pStarts, int Count, int Instrument);	// the notes after them don't change

	// Copying: Copy() keeps just these notes, from the first one's start on (as row 0), and how they
	// sounded; Paste() puts Clip's notes in from Row on, Semitones higher. SelfContained() writes how
	// a copy's notes sounded into their cells where needed, so they sound the same anywhere.
	CChannelNotes Copy(const int *pStarts, int Count) const;
	void Paste(const CChannelNotes &Clip, int Row, int Semitones);
	void SelfContained();

	// The first and last rows that differ from Other; false if none do
	bool Compare(const CChannelNotes &Other, int &First, int &Last) const;

private:
	struct Impl;
	Impl *m_pImpl;
};

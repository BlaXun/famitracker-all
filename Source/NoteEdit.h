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

#include <vector>	// the files including this one include it before stdafx.h (see NoteEdit.cpp)

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
// Effects on notes: some last until another effect changes them, from note
// to note (an arpeggio, portamento, vibrato, tremolo, a volume slide, the
// duty and fine pitch: "lasting" effects), the rest work where they are.
// GetEffects tells what works while a note sounds, those taken over from
// earlier notes included. The effect edits put effects on notes or take
// them off; the notes after them sound as before, since a lasting effect
// that changes for them is set again (or switched off) on their first row.
//
// The pan (8xx, this FamiTracker's stereo) lasts from note to note too, and
// each note keeps the pan it plays at through the edits: a note moved, copied
// or pasted plays at its pan where it lands, the notes it leaves play at
// theirs, and a new note plays at the channel's pan where it goes.
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
		int Pan;			// in effect after its first row: 0x00 left, 0x80 the middle (PAN_CENTER), 0xFF right
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

	// An effect at work while a note sounds
	struct EffectSpan {
		unsigned char Number;	// effect_t
		unsigned char Param;
		int From, To;			// the rows it works on, within the note's: a lasting effect from where it
								// starts (or the note's start) to where it changes (or the note's end), any
								// other one on its own row
		bool Carried;			// a lasting effect taken over from an earlier note (not set on the note's first row)
	};
	// What works while a note sounds (Index into the notes), in time order: the lasting effects, taken
	// over from earlier notes too (each while it's on: arpeggio, portamento up, down or to the note,
	// vibrato, tremolo, volume slide), the slides (Qxy and Rxy, on their rows) and the other effects
	// on its rows (the duty and fine pitch where they're set). Song-wide effects are left out.
	void GetEffects(int Index, std::vector<EffectSpan> &Out) const;

	// Effects put on notes (by where they start), or taken off. Each returns false when an effect
	// didn't fit in the effect columns that play; the rest is done.
	//
	// A lasting effect: EF_ARPEGGIO, EF_VIBRATO, EF_TREMOLO, EF_VOLUME_SLIDE or EF_PORTAMENTO, with
	// a parameter per note (0 takes it off), working from Delay rows into the note (a delay per
	// note; portamento always from its first row) to its end. What the note had of it is replaced.
	// After a volume slide, the next note gets the volume it had written into its cell.
	bool SetLasting(const int *pStarts, int Count, unsigned char Number, const unsigned char *pParams, const int *pDelays);
	// A slide into the notes from Semitones below (negative: above, up to 15) at Speed (1-15), or
	// none with 0; they still land where they did. A slide replaces an arpeggio or portamento set on
	// the note's first row (they can't play together).
	bool SetSlideIn(const int *pStarts, int Count, int Semitones, int Speed);
	// A bend at the end of the notes, Before rows before it: Semitones up (negative: down, up to 15)
	// at Speed (1-15), or none with 0. Notes too short to bend are left as they are.
	bool SetBend(const int *pStarts, int Count, int Semitones, int Speed, int Before);
	// Takes every effect off the notes but the song-wide ones and the pan; one that came from an
	// earlier note is switched off. Notes that slid into their pitch now start there.
	bool ClearEffects(const int *pStarts, int Count);
	// The notes play at Pan (0x00 left, 0x80 the middle, 0xFF right) from their first row to their
	// end; the notes after them keep the pan they had. A pan that changed while one of them sounded
	// goes. When the channel's last note ends up panned otherwise than its first note starts, the
	// first note gets its pan written, so it plays at it again when the song loops.
	bool SetPan(const int *pStarts, int Count, unsigned char Pan);

	// The first and last rows that differ from Other; false if none do
	bool Compare(const CChannelNotes &Other, int &First, int &Last) const;

private:
	struct Impl;
	Impl *m_pImpl;
};

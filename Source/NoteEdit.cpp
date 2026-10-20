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

// The notes of one channel and the piano roll's edits to them (see NoteEdit.h).

// Standard headers come before stdafx.h, whose DEBUG_NEW macro would
// otherwise rewrite the placement new inside the standard library.
#include <algorithm>
#include <map>
#include <utility>
#include <vector>
#include "stdafx.h"
#include "FamiTracker.h"
#include "FamiTrackerDoc.h"
#include "NoteEdit.h"

namespace {

const int TOP_PITCH = OCTAVE_RANGE * 12 - 1;	// B-7
const int FULL_VOLUME = 0x0F;

// An effect taken out of a row, and the column it was in
struct Effect {
	unsigned char Number;
	int Column;
};

// What sounds at a note's start: the instrument (-1: none) and the volume in effect
struct Sound {
	int Instrument;
	int Volume;			// -1: whatever is in effect where it goes
};
typedef std::map<int, Sound> Sounds;	// by the row a note starts on

// What a note owns of its rows, taken out to be put back elsewhere
struct Block {
	int Length;							// rows from its start to its end
	std::vector<stChanNote> Rows;		// Length cells, song-wide effects left out
	bool HasEnd;						// it ends on a release or halt...
	stChanNote EndCell;					// ...this one
	Sound Sounded;						// how it sounded where it was
	int Shift;							// semitones to move its note by
};

stChanNote EmptyCell()
{
	stChanNote Cell;
	Cell.Note = NONE;
	Cell.Octave = 0;
	Cell.Vol = MAX_VOLUME;
	Cell.Instrument = MAX_INSTRUMENTS;
	for (int i = 0; i < MAX_EFFECT_COLUMNS; ++i) {
		Cell.EffNumber[i] = EF_NONE;
		Cell.EffParam[i] = 0;
	}
	return Cell;
}

bool SameCell(const stChanNote &First, const stChanNote &Second)
{
	if (First.Note != Second.Note || First.Octave != Second.Octave || First.Vol != Second.Vol || First.Instrument != Second.Instrument)
		return false;
	for (int i = 0; i < MAX_EFFECT_COLUMNS; ++i) {
		if (First.EffNumber[i] != Second.EffNumber[i] || First.EffParam[i] != Second.EffParam[i])
			return false;
	}
	return true;
}

bool IsNote(const stChanNote &Cell)
{
	return Cell.Note >= C && Cell.Note <= B;
}

bool IsEnd(const stChanNote &Cell)
{
	return Cell.Note == RELEASE || Cell.Note == HALT;
}

// Effects that steer the whole song rather than a note
bool IsSongEffect(unsigned char Number)
{
	return Number == EF_SPEED || Number == EF_JUMP || Number == EF_SKIP || Number == EF_HALT;
}

// Effects that switch off an arpeggio, a slide, portamento, vibrato, tremolo or a volume slide
bool IsSwitchOff(unsigned char Number, unsigned char Param)
{
	if (Param != 0)
		return false;
	switch (Number) {
	case EF_ARPEGGIO:
	case EF_PORTA_UP:
	case EF_PORTA_DOWN:
	case EF_PORTAMENTO:
	case EF_VIBRATO:
	case EF_TREMOLO:
	case EF_VOLUME_SLIDE:
		return true;
	}
	return false;
}

int Clamp(int Value, int Low, int High)
{
	return std::max(Low, std::min(High, Value));
}

}	// namespace

struct CChannelNotes::Impl {
	std::vector<stChanNote> Cells;
	int Columns;						// effect columns that play
	bool Release[MAX_INSTRUMENTS];		// instruments that fade out on a release
	int Lowest, Highest;				// pitches notes can have
	Sounds Hints;						// a copy's notes: how they sounded where they were copied from
	mutable std::vector<Note> Notes;	// read from the cells when asked for
	mutable bool Parsed;

	Impl() : Columns(1), Lowest(0), Highest(TOP_PITCH), Parsed(false)
	{
		std::fill(Release, Release + MAX_INSTRUMENTS, false);
	}

	int Rows() const
	{
		return static_cast<int>(Cells.size());
	}

	static int PitchOf(const stChanNote &Cell)
	{
		return Cell.Octave * 12 + Cell.Note - 1;
	}

	// How far from its note a slide in the cell (Qxy up, Rxy down) lands
	int SlideOf(const stChanNote &Cell) const
	{
		for (int i = 0; i < Columns; ++i) {
			if (Cell.EffNumber[i] == EF_SLIDE_UP)
				return Cell.EffParam[i] & 0x0F;
			if (Cell.EffNumber[i] == EF_SLIDE_DOWN)
				return -(Cell.EffParam[i] & 0x0F);
		}
		return 0;
	}

	void Parse() const
	{
		if (Parsed)
			return;
		Parsed = true;
		Notes.clear();
		int Instrument = -1, Volume = FULL_VOLUME;
		bool Open = false;
		for (int r = 0; r < Rows(); ++r) {
			const stChanNote &Cell = Cells[static_cast<size_t>(r)];
			if (IsNote(Cell)) {
				if (Open)
					Notes.back().End = r;
				Note Found;
				Found.Start = r;
				Found.End = Rows();
				Found.Written = Clamp(PitchOf(Cell), 0, TOP_PITCH);
				Found.Pitch = Clamp(Found.Written + SlideOf(Cell), 0, TOP_PITCH);
				Found.Instrument = Cell.Instrument < MAX_INSTRUMENTS ? Cell.Instrument : Instrument;
				Found.Volume = Cell.Vol < MAX_VOLUME ? Cell.Vol : Volume;
				Found.Released = Found.Halted = false;
				Notes.push_back(Found);
				Open = true;
			}
			else if (IsEnd(Cell) && Open) {
				Notes.back().End = r;
				Notes.back().Released = Cell.Note == RELEASE;
				Notes.back().Halted = Cell.Note == HALT;
				Open = false;
			}
			// A release or halt ignores its instrument column; a volume counts on any row
			if (Cell.Instrument < MAX_INSTRUMENTS && !IsEnd(Cell))
				Instrument = Cell.Instrument;
			if (Cell.Vol < MAX_VOLUME)
				Volume = Cell.Vol;
		}
	}

	// The note starting at Start, -1 if none
	int Find(int Start) const
	{
		Parse();
		size_t Low = 0, High = Notes.size();
		while (Low < High) {
			const size_t Mid = (Low + High) / 2;
			if (Notes[Mid].Start < Start)
				Low = Mid + 1;
			else
				High = Mid;
		}
		return (Low < Notes.size() && Notes[Low].Start == Start) ? static_cast<int>(Low) : -1;
	}

	Sounds SoundsNow() const
	{
		Parse();
		Sounds Out;
		for (size_t i = 0; i < Notes.size(); ++i) {
			Sound Now = { Notes[i].Instrument, Notes[i].Volume };
			Out[Notes[i].Start] = Now;
		}
		return Out;
	}

	stChanNote &CellAt(int Row)
	{
		return Cells[static_cast<size_t>(Row)];
	}

	// What a row's note owns of it: song-wide effects and the effect columns that don't play are left out
	stChanNote Owned(const stChanNote &Cell) const
	{
		stChanNote Out = Cell;
		for (int i = 0; i < MAX_EFFECT_COLUMNS; ++i) {
			if (i >= Columns || IsSongEffect(Out.EffNumber[i])) {
				Out.EffNumber[i] = EF_NONE;
				Out.EffParam[i] = 0;
			}
		}
		return Out;
	}

	void ClearOwned(int Row)
	{
		stChanNote &Cell = CellAt(Row);
		Cell.Note = NONE;
		Cell.Octave = 0;
		Cell.Instrument = MAX_INSTRUMENTS;
		Cell.Vol = MAX_VOLUME;
		for (int i = 0; i < Columns; ++i) {
			if (!IsSongEffect(Cell.EffNumber[i])) {
				Cell.EffNumber[i] = EF_NONE;
				Cell.EffParam[i] = 0;
			}
		}
		Parsed = false;
	}

	bool HasEffect(int Row, unsigned char Number) const
	{
		for (int i = 0; i < Columns; ++i) {
			if (Cells[static_cast<size_t>(Row)].EffNumber[i] == Number)
				return true;
		}
		return false;
	}

	// Adds an effect in column Prefer if it's free, otherwise in the first free one; false if none is
	bool AddEffect(int Row, unsigned char Number, unsigned char Param, int Prefer)
	{
		stChanNote &Cell = CellAt(Row);
		int Column = -1;
		if (Prefer >= 0 && Prefer < Columns && Cell.EffNumber[Prefer] == EF_NONE)
			Column = Prefer;
		for (int i = 0; i < Columns && Column < 0; ++i) {
			if (Cell.EffNumber[i] == EF_NONE)
				Column = i;
		}
		if (Column < 0)
			return false;
		Cell.EffNumber[Column] = Number;
		Cell.EffParam[Column] = Param;
		Parsed = false;
		return true;
	}

	// Puts what a note owns of a row there, in place of what a note owned there before
	void PutOwned(int Row, const stChanNote &Part)
	{
		ClearOwned(Row);
		stChanNote &Cell = CellAt(Row);
		Cell.Note = Part.Note;
		Cell.Octave = Part.Octave;
		Cell.Instrument = Part.Instrument;
		Cell.Vol = Part.Vol;
		for (int i = 0; i < Columns; ++i) {
			if (Part.EffNumber[i] != EF_NONE)
				AddEffect(Row, Part.EffNumber[i], Part.EffParam[i], i);
		}
	}

	// Takes the switch-offs out of a row
	void TakeSwitchOffs(int Row, std::vector<Effect> &Out)
	{
		stChanNote &Cell = CellAt(Row);
		for (int i = 0; i < Columns; ++i) {
			if (IsSwitchOff(Cell.EffNumber[i], Cell.EffParam[i])) {
				const Effect Off = { Cell.EffNumber[i], i };
				Out.push_back(Off);
				Cell.EffNumber[i] = EF_NONE;
				Cell.EffParam[i] = 0;
			}
		}
		Parsed = false;
	}

	// Switch-offs go on a row, except those it sets already
	void PutSwitchOffs(int Row, const std::vector<Effect> &Offs)
	{
		if (Row < 0 || Row >= Rows())
			return;
		for (size_t i = 0; i < Offs.size(); ++i) {
			if (!HasEffect(Row, Offs[i].Number))
				AddEffect(Row, Offs[i].Number, 0, Offs[i].Column);
		}
	}

	// The end of a new note: a release if the instrument fades out on one, otherwise a halt
	stChanNote NewEnd(int Instrument) const
	{
		stChanNote Cell = EmptyCell();
		Cell.Note = static_cast<unsigned char>((Instrument >= 0 && Instrument < MAX_INSTRUMENTS && Release[Instrument]) ? RELEASE : HALT);
		return Cell;
	}

	bool EndsOnCell(const Note &Which) const
	{
		return Which.End < Rows() && IsEnd(Cells[static_cast<size_t>(Which.End)]);
	}

	Block TakeBlock(const Note &Which) const
	{
		Block Out;
		Out.Length = Which.End - Which.Start;
		for (int r = Which.Start; r < Which.End; ++r)
			Out.Rows.push_back(Owned(Cells[static_cast<size_t>(r)]));
		Out.HasEnd = EndsOnCell(Which);
		Out.EndCell = Out.HasEnd ? Owned(Cells[static_cast<size_t>(Which.End)]) : EmptyCell();
		Out.Sounded.Instrument = Which.Instrument;
		Out.Sounded.Volume = Which.Volume;
		Out.Shift = 0;
		return Out;
	}

	// Clears all a note owns, its end included
	void ClearNote(const Note &Which)
	{
		const bool HasEnd = EndsOnCell(Which);
		for (int r = Which.Start; r < Which.End; ++r)
			ClearOwned(r);
		if (HasEnd)
			ClearOwned(Which.End);
	}

	// Cuts off a note's rows from Row on, its end included, and returns the switch-offs that were there
	std::vector<Effect> Cut(const Note &Which, int Row)
	{
		std::vector<Effect> Offs;
		const bool HasEnd = EndsOnCell(Which);
		for (int r = Row; r < Which.End; ++r) {
			TakeSwitchOffs(r, Offs);
			ClearOwned(r);
		}
		if (HasEnd) {
			TakeSwitchOffs(Which.End, Offs);
			ClearOwned(Which.End);
		}
		return Offs;
	}

	// Takes a note out. The note before it, if it ran on into this one, ends where it began, and the
	// switch-offs this one had stay there.
	void TakeOut(int Start)
	{
		const int Index = Find(Start);
		if (Index < 0)
			return;
		const Note Which = Notes[static_cast<size_t>(Index)];
		const bool RunsInto = Index > 0 && Notes[static_cast<size_t>(Index) - 1].End == Which.Start;
		const int Instrument = RunsInto ? Notes[static_cast<size_t>(Index) - 1].Instrument : -1;
		std::vector<Effect> Offs;
		for (int r = Which.Start; r < Which.End; ++r)
			TakeSwitchOffs(r, Offs);
		if (EndsOnCell(Which))
			TakeSwitchOffs(Which.End, Offs);
		ClearNote(Which);
		if (RunsInto)
			PutOwned(Which.Start, NewEnd(Instrument));
		PutSwitchOffs(Which.Start, Offs);
	}

	// Writes a block from Row on. Notes starting where it goes make way, and a note sounding on at
	// Row is cut there; what that note switched off there, the new one starts without.
	void Place(const Block &What, int Row)
	{
		if (Row < 0 || Row >= Rows() || What.Length < 1)
			return;
		const int Stop = std::min(Rows(), Row + What.Length);
		Parse();
		const std::vector<Note> Before(Notes);
		std::vector<Effect> Offs;
		for (size_t i = 0; i < Before.size(); ++i) {
			const Note &Other = Before[i];
			if (Other.Start >= Row && Other.Start < Stop)
				ClearNote(Other);
			else if (Other.Start < Row && Other.End > Row) {
				const std::vector<Effect> Cuts = Cut(Other, Row);
				Offs.insert(Offs.end(), Cuts.begin(), Cuts.end());
			}
			else if (Other.Start < Row && Other.End == Row && EndsOnCell(Other)) {
				TakeSwitchOffs(Row, Offs);	// its release or halt makes way: it runs on into the new note
				ClearOwned(Row);
			}
		}

		stChanNote First = What.Rows[0];
		if (IsNote(First) && What.Shift != 0) {
			const int Pitch = Clamp(PitchOf(First) + What.Shift, Lowest, Highest);
			First.Note = static_cast<unsigned char>(Pitch % 12 + 1);
			First.Octave = static_cast<unsigned char>(Pitch / 12);
		}
		PutOwned(Row, First);
		for (int r = Row + 1; r < Stop; ++r)
			PutOwned(r, What.Rows[static_cast<size_t>(r - Row)]);
		PutSwitchOffs(Row, Offs);

		// Its end: a release or halt, unless it runs on into the next note. Then the switch-offs of
		// its end go on its last row, so they don't change the next note.
		if (Stop < Rows()) {
			if (IsNote(Cells[static_cast<size_t>(Stop)])) {
				std::vector<Effect> EndOffs;
				for (int i = 0; i < Columns && What.HasEnd; ++i) {
					if (IsSwitchOff(What.EndCell.EffNumber[i], What.EndCell.EffParam[i])) {
						const Effect Off = { What.EndCell.EffNumber[i], i };
						EndOffs.push_back(Off);
					}
				}
				if (Stop - 1 > Row)
					PutSwitchOffs(Stop - 1, EndOffs);
			}
			else
				PutOwned(Stop, What.HasEnd ? What.EndCell : NewEnd(What.Sounded.Instrument));
		}
		Parsed = false;
	}

	// After an edit: the notes that were there before sound as they did, and the placed ones as
	// where they came from. A volume or instrument they took over from earlier rows and would now
	// take over differently is written into their own cell.
	void Keep(const Sounds &Before, const Sounds &Placed)
	{
		int Instrument = -1, Volume = FULL_VOLUME;
		for (int r = 0; r < Rows(); ++r) {
			stChanNote &Cell = CellAt(r);
			if (IsNote(Cell)) {
				const Sound *pWant = NULL;
				Sounds::const_iterator It = Placed.find(r);
				if (It != Placed.end())
					pWant = &It->second;
				else if ((It = Before.find(r)) != Before.end())
					pWant = &It->second;
				if (pWant != NULL) {
					if (Cell.Instrument >= MAX_INSTRUMENTS && pWant->Instrument >= 0 && pWant->Instrument != Instrument)
						Cell.Instrument = static_cast<unsigned char>(pWant->Instrument);
					if (Cell.Vol >= MAX_VOLUME && pWant->Volume >= 0 && pWant->Volume != Volume)
						Cell.Vol = static_cast<unsigned char>(pWant->Volume);
				}
			}
			if (Cell.Instrument < MAX_INSTRUMENTS && !IsEnd(Cell))
				Instrument = Cell.Instrument;
			if (Cell.Vol < MAX_VOLUME)
				Volume = Cell.Vol;
		}
		Parsed = false;
	}
};

CChannelNotes::CChannelNotes() : m_pImpl(new Impl())
{
}

CChannelNotes::CChannelNotes(const CChannelNotes &Other) : m_pImpl(new Impl(*Other.m_pImpl))
{
}

CChannelNotes &CChannelNotes::operator=(const CChannelNotes &Other)
{
	if (this != &Other)
		*m_pImpl = *Other.m_pImpl;
	return *this;
}

CChannelNotes::~CChannelNotes()
{
	delete m_pImpl;
}

void CChannelNotes::Reset(int Rows)
{
	m_pImpl->Cells.assign(static_cast<size_t>(std::max(0, Rows)), EmptyCell());
	m_pImpl->Hints.clear();
	m_pImpl->Parsed = false;
}

int CChannelNotes::GetRows() const
{
	return m_pImpl->Rows();
}

void CChannelNotes::SetRows(int Rows)
{
	m_pImpl->Cells.resize(static_cast<size_t>(std::max(0, Rows)), EmptyCell());
	m_pImpl->Parsed = false;
}

void CChannelNotes::GetCell(int Row, stChanNote &Cell) const
{
	Cell = (Row >= 0 && Row < m_pImpl->Rows()) ? m_pImpl->Cells[static_cast<size_t>(Row)] : EmptyCell();
}

void CChannelNotes::SetCell(int Row, const stChanNote &Cell)
{
	if (Row < 0 || Row >= m_pImpl->Rows())
		return;
	m_pImpl->CellAt(Row) = Cell;
	m_pImpl->Parsed = false;
}

void CChannelNotes::SetEffectColumns(int Columns)
{
	m_pImpl->Columns = Clamp(Columns, 1, MAX_EFFECT_COLUMNS);
	m_pImpl->Parsed = false;
}

void CChannelNotes::SetCanRelease(int Instrument, bool Release)
{
	if (Instrument >= 0 && Instrument < MAX_INSTRUMENTS)
		m_pImpl->Release[Instrument] = Release;
}

void CChannelNotes::SetPitchRange(int Lowest, int Highest)
{
	m_pImpl->Lowest = Clamp(Lowest, 0, TOP_PITCH);
	m_pImpl->Highest = Clamp(Highest, m_pImpl->Lowest, TOP_PITCH);
}

int CChannelNotes::GetNoteCount() const
{
	m_pImpl->Parse();
	return static_cast<int>(m_pImpl->Notes.size());
}

const CChannelNotes::Note &CChannelNotes::GetNote(int Index) const
{
	m_pImpl->Parse();
	return m_pImpl->Notes[static_cast<size_t>(Index)];
}

int CChannelNotes::FindNote(int Start) const
{
	return m_pImpl->Find(Start);
}

int CChannelNotes::NoteAt(int Row) const
{
	const Impl &d = *m_pImpl;
	d.Parse();
	size_t Low = 0, High = d.Notes.size();	// the first note starting after Row
	while (Low < High) {
		const size_t Mid = (Low + High) / 2;
		if (d.Notes[Mid].Start <= Row)
			Low = Mid + 1;
		else
			High = Mid;
	}
	return (Low > 0 && d.Notes[Low - 1].End > Row) ? static_cast<int>(Low) - 1 : -1;
}

void CChannelNotes::Add(int Start, int End, int Pitch, int Instrument)
{
	Impl &d = *m_pImpl;
	if (Start < 0 || Start >= d.Rows() || End <= Start)
		return;
	Block New;
	New.Length = End - Start;
	New.Rows.assign(static_cast<size_t>(New.Length), EmptyCell());
	const int Lands = Clamp(Pitch, d.Lowest, d.Highest);
	New.Rows[0].Note = static_cast<unsigned char>(Lands % 12 + 1);
	New.Rows[0].Octave = static_cast<unsigned char>(Lands / 12);
	const bool HasInstrument = Instrument >= 0 && Instrument < MAX_INSTRUMENTS;
	if (HasInstrument)
		New.Rows[0].Instrument = static_cast<unsigned char>(Instrument);
	New.HasEnd = false;
	New.EndCell = EmptyCell();
	New.Sounded.Instrument = HasInstrument ? Instrument : -1;
	New.Sounded.Volume = -1;	// as loud as the notes around it
	New.Shift = 0;

	const Sounds Before = d.SoundsNow();
	d.Place(New, Start);
	Sounds Placed;
	Placed[Start] = New.Sounded;
	d.Keep(Before, Placed);
}

void CChannelNotes::Remove(int Start)
{
	Impl &d = *m_pImpl;
	if (d.Find(Start) < 0)
		return;
	const Sounds Before = d.SoundsNow();
	d.TakeOut(Start);
	d.Keep(Before, Sounds());
}

void CChannelNotes::Resize(int Start, int End)
{
	Impl &d = *m_pImpl;
	const int Index = d.Find(Start);
	if (Index < 0)
		return;
	const Note Which = d.Notes[static_cast<size_t>(Index)];
	const int Limit = (static_cast<size_t>(Index) + 1 < d.Notes.size()) ? d.Notes[static_cast<size_t>(Index) + 1].Start : d.Rows();
	End = Clamp(End, Which.Start + 1, Limit);
	if (End == Which.End)
		return;

	const Sounds Before = d.SoundsNow();
	const stChanNote EndCell = d.EndsOnCell(Which) ? d.Owned(d.Cells[static_cast<size_t>(Which.End)]) : d.NewEnd(Which.Instrument);
	if (End < Which.End) {
		// Shorter: what was switched off in the rows cut off is switched off at its new end
		const std::vector<Effect> Offs = d.Cut(Which, End);
		d.PutOwned(End, EndCell);
		d.PutSwitchOffs(End, Offs);
	}
	else {
		// Longer, over empty rows: what it switched off on its last row is switched off at its new end
		std::vector<Effect> Offs;
		if (Which.End - 1 > Which.Start)
			d.TakeSwitchOffs(Which.End - 1, Offs);
		for (int r = Which.End; r < End; ++r)
			d.ClearOwned(r);
		if (End < d.Rows() && !IsNote(d.Cells[static_cast<size_t>(End)])) {
			d.PutOwned(End, EndCell);
			d.PutSwitchOffs(End, Offs);
		}
		else {
			// It runs on into the next note: what its end switched off is switched off on its last row,
			// so the next note doesn't change
			for (int i = 0; i < d.Columns; ++i) {
				if (IsSwitchOff(EndCell.EffNumber[i], EndCell.EffParam[i])) {
					const Effect Off = { EndCell.EffNumber[i], i };
					Offs.push_back(Off);
				}
			}
			d.PutSwitchOffs(End - 1, Offs);
		}
	}
	d.Keep(Before, Sounds());
}

void CChannelNotes::Move(const int *pStarts, int Count, int Rows, const int *pPitches, bool Copy)
{
	Impl &d = *m_pImpl;
	const Sounds Before = d.SoundsNow();

	// The notes, in order
	std::vector<std::pair<int, Block> > Moving;
	for (int k = 0; k < Count; ++k) {
		const int Index = d.Find(pStarts[k]);
		if (Index < 0)
			continue;
		bool Twice = false;
		for (size_t m = 0; m < Moving.size(); ++m)
			Twice = Twice || Moving[m].first == pStarts[k];
		if (Twice)
			continue;
		const Note &Which = d.Notes[static_cast<size_t>(Index)];
		Block What = d.TakeBlock(Which);
		What.Shift = pPitches[k] - Which.Pitch;
		Moving.push_back(std::make_pair(Which.Start, What));
	}
	std::sort(Moving.begin(), Moving.end(),
		[](const std::pair<int, Block> &First, const std::pair<int, Block> &Second) { return First.first < Second.first; });

	if (!Copy) {
		for (size_t m = 0; m < Moving.size(); ++m)
			d.TakeOut(Moving[m].first);
	}
	Sounds Placed;
	for (size_t m = 0; m < Moving.size(); ++m) {
		const int At = Moving[m].first + Rows;
		if (At < 0 || At >= d.Rows())
			continue;
		d.Place(Moving[m].second, At);
		Placed[At] = Moving[m].second.Sounded;
	}
	d.Keep(Before, Placed);
}

void CChannelNotes::SetInstrument(const int *pStarts, int Count, int Instrument)
{
	Impl &d = *m_pImpl;
	if (Instrument < 0 || Instrument >= MAX_INSTRUMENTS)
		return;
	const Sounds Before = d.SoundsNow();
	Sounds Placed;
	for (int k = 0; k < Count; ++k) {
		const int Index = d.Find(pStarts[k]);
		if (Index < 0)
			continue;
		d.CellAt(pStarts[k]).Instrument = static_cast<unsigned char>(Instrument);
		const Sound Now = { Instrument, -1 };
		Placed[pStarts[k]] = Now;
	}
	d.Parsed = false;
	d.Keep(Before, Placed);
}

void CChannelNotes::SelfContained()
{
	Impl &d = *m_pImpl;
	d.Keep(d.Hints, Sounds());
}

CChannelNotes CChannelNotes::Copy(const int *pStarts, int Count) const
{
	const Impl &d = *m_pImpl;
	std::vector<std::pair<int, Block> > Taken;
	for (int k = 0; k < Count; ++k) {
		const int Index = d.Find(pStarts[k]);
		bool Twice = false;
		for (size_t m = 0; m < Taken.size(); ++m)
			Twice = Twice || Taken[m].first == pStarts[k];
		if (Index >= 0 && !Twice)
			Taken.push_back(std::make_pair(pStarts[k], d.TakeBlock(d.Notes[static_cast<size_t>(Index)])));
	}
	std::sort(Taken.begin(), Taken.end(),
		[](const std::pair<int, Block> &First, const std::pair<int, Block> &Second) { return First.first < Second.first; });

	CChannelNotes Out;
	Impl &o = *Out.m_pImpl;
	o.Columns = d.Columns;
	std::copy(d.Release, d.Release + MAX_INSTRUMENTS, o.Release);
	o.Lowest = d.Lowest;
	o.Highest = d.Highest;
	if (Taken.empty())
		return Out;
	const int Base = Taken.front().first;
	int Span = 0;
	for (size_t m = 0; m < Taken.size(); ++m)
		Span = std::max(Span, Taken[m].first - Base + Taken[m].second.Length + 1);	// room for its end
	o.Cells.assign(static_cast<size_t>(Span), EmptyCell());
	for (size_t m = 0; m < Taken.size(); ++m) {
		const Block &What = Taken[m].second;
		const int At = Taken[m].first - Base;
		for (int r = 0; r < What.Length; ++r)
			o.PutOwned(At + r, What.Rows[static_cast<size_t>(r)]);
		const bool RunsInto = m + 1 < Taken.size() && Taken[m + 1].first == Taken[m].first + What.Length;
		if (!RunsInto)	// a note that ran into one not copied ends where it did
			o.PutOwned(At + What.Length, What.HasEnd ? What.EndCell : o.NewEnd(What.Sounded.Instrument));
		o.Hints[At] = What.Sounded;
	}
	o.Parsed = false;
	return Out;
}

void CChannelNotes::Paste(const CChannelNotes &Clip, int Row, int Semitones)
{
	Impl &d = *m_pImpl;
	const Impl &c = *Clip.m_pImpl;
	c.Parse();
	const std::vector<Note> Pasted(c.Notes);
	const Sounds Before = d.SoundsNow();
	Sounds Placed;
	for (size_t i = 0; i < Pasted.size(); ++i) {
		const int At = Row + Pasted[i].Start;
		if (At < 0)
			continue;
		if (At >= d.Rows())
			break;
		Block What = c.TakeBlock(Pasted[i]);
		What.Shift = Semitones;
		const Sounds::const_iterator Hint = c.Hints.find(Pasted[i].Start);
		if (Hint != c.Hints.end())
			What.Sounded = Hint->second;
		d.Place(What, At);
		Placed[At] = What.Sounded;
	}
	d.Keep(Before, Placed);
}

bool CChannelNotes::Compare(const CChannelNotes &Other, int &First, int &Last) const
{
	const Impl &d = *m_pImpl, &o = *Other.m_pImpl;
	const int Rows = std::max(d.Rows(), o.Rows());
	const stChanNote Empty = EmptyCell();
	First = Last = -1;
	for (int r = 0; r < Rows; ++r) {
		const stChanNote &Mine = r < d.Rows() ? d.Cells[static_cast<size_t>(r)] : Empty;
		const stChanNote &Theirs = r < o.Rows() ? o.Cells[static_cast<size_t>(r)] : Empty;
		if (!SameCell(Mine, Theirs)) {
			if (First < 0)
				First = r;
			Last = r;
		}
	}
	return First >= 0;
}

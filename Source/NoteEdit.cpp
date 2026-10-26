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
#include <cstdlib>
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

// What sounds at a note's start: the instrument (-1: none), the volume and the pan in effect
struct Sound {
	int Instrument;
	int Volume;			// -1: whatever is in effect where it goes
	int Pan;			// -1: whatever is in effect where it goes
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

// Effects that change the pitch through the channel's one pitch effect: the last of them on a row
// replaces the one before (an arpeggio stops when a slide starts)
bool IsPitchEffect(unsigned char Number)
{
	switch (Number) {
	case EF_ARPEGGIO:
	case EF_PORTAMENTO:
	case EF_PORTA_UP:
	case EF_PORTA_DOWN:
	case EF_SLIDE_UP:
	case EF_SLIDE_DOWN:
		return true;
	}
	return false;
}

bool IsSlide(unsigned char Number)
{
	return Number == EF_SLIDE_UP || Number == EF_SLIDE_DOWN;
}

// Effects that go on from row to row, until another one changes them
bool IsLasting(unsigned char Number)
{
	switch (Number) {
	case EF_ARPEGGIO:
	case EF_PORTAMENTO:
	case EF_PORTA_UP:
	case EF_PORTA_DOWN:
	case EF_VIBRATO:
	case EF_TREMOLO:
	case EF_VOLUME_SLIDE:
	case EF_DUTY_CYCLE:
	case EF_PITCH:
	case EF_PAN:
		return true;
	}
	return false;
}

// The pan a row leaves the channel at, from Pan before it: its last 8xx in the effect columns that play
int PanAfter(int Pan, const stChanNote &Cell, int Columns)
{
	for (int i = 0; i < Columns; ++i) {
		if (Cell.EffNumber[i] == EF_PAN)
			Pan = Cell.EffParam[i];
	}
	return Pan;
}

bool SetsPan(const stChanNote &Cell, int Columns)
{
	return PanAfter(-1, Cell, Columns) >= 0;
}

// The lasting effects in force, as the channel handlers keep them from row to row (rows and notes
// don't reset them, releases and halts neither)
struct Lasting {
	unsigned char Pitch;		// EF_ARPEGGIO, EF_PORTAMENTO, EF_PORTA_UP, EF_PORTA_DOWN, a slide, or EF_NONE
	unsigned char PitchParam;
	unsigned char Vibrato, Tremolo, VolumeSlide;
	unsigned char Duty, FinePitch;
	unsigned char Pan;

	Lasting() : Pitch(EF_NONE), PitchParam(0), Vibrato(0), Tremolo(0), VolumeSlide(0), Duty(0), FinePitch(0x80), Pan(PAN_CENTER)
	{
	}

	// The pitch effect at work, EF_NONE for none (an arpeggio or portamento with 00 does nothing)
	unsigned char PitchOn() const
	{
		return PitchParam != 0 ? Pitch : static_cast<unsigned char>(EF_NONE);
	}

	bool SamePitch(const Lasting &Other) const
	{
		return PitchOn() == Other.PitchOn() && (PitchOn() == EF_NONE || PitchParam == Other.PitchParam);
	}

	bool operator==(const Lasting &Other) const
	{
		return SamePitch(Other) && Vibrato == Other.Vibrato && Tremolo == Other.Tremolo && VolumeSlide == Other.VolumeSlide &&
			Duty == Other.Duty && FinePitch == Other.FinePitch && Pan == Other.Pan;
	}
	bool operator!=(const Lasting &Other) const { return !(*this == Other); }

	// The parameter of one lasting effect
	unsigned char Get(unsigned char Number) const
	{
		switch (Number) {
		case EF_VIBRATO: return Vibrato;
		case EF_TREMOLO: return Tremolo;
		case EF_VOLUME_SLIDE: return VolumeSlide;
		case EF_DUTY_CYCLE: return Duty;
		case EF_PITCH: return FinePitch;
		case EF_PAN: return Pan;
		}
		return PitchOn() == Number ? PitchParam : 0;
	}

	void Set(unsigned char Number, unsigned char Param)
	{
		switch (Number) {
		case EF_VIBRATO: Vibrato = Param; break;
		case EF_TREMOLO: Tremolo = Param; break;
		case EF_VOLUME_SLIDE: VolumeSlide = Param; break;
		case EF_DUTY_CYCLE: Duty = Param; break;
		case EF_PITCH: FinePitch = Param; break;
		case EF_PAN: Pan = Param; break;
		default:
			if (IsPitchEffect(Number)) {
				Pitch = Number;
				PitchParam = Param;
			}
			break;
		}
	}
};

}	// namespace

struct CChannelNotes::Impl {
	std::vector<stChanNote> Cells;
	int Columns;						// effect columns that play
	bool Release[MAX_INSTRUMENTS];		// instruments that fade out on a release
	int Lowest, Highest;				// pitches notes can have
	Sounds Hints;						// a copy's notes: how they sounded where they were copied from
	mutable std::vector<Note> Notes;	// read from the cells when asked for
	mutable bool Parsed;
	mutable std::vector<std::vector<EffectSpan> > Spans;	// per note, worked out when asked for
	mutable bool SpansParsed;

	Impl() : Columns(1), Lowest(0), Highest(TOP_PITCH), Parsed(false), SpansParsed(false)
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
		SpansParsed = false;
		Notes.clear();
		int Instrument = -1, Volume = FULL_VOLUME, Pan = PAN_CENTER;
		bool Open = false;
		for (int r = 0; r < Rows(); ++r) {
			const stChanNote &Cell = Cells[static_cast<size_t>(r)];
			Pan = PanAfter(Pan, Cell, Columns);
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
				Found.Pan = Pan;
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
			Sound Now = { Notes[i].Instrument, Notes[i].Volume, Notes[i].Pan };
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
		Out.Sounded.Pan = Which.Pan;
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
	// where they came from. A volume, instrument or pan they took over from earlier rows and would
	// now take over differently is written into their own cell.
	void Keep(const Sounds &Before, const Sounds &Placed)
	{
		int Instrument = -1, Volume = FULL_VOLUME, Pan = PAN_CENTER;
		for (int r = 0; r < Rows(); ++r) {
			stChanNote &Cell = CellAt(r);
			Pan = PanAfter(Pan, Cell, Columns);
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
					if (!SetsPan(Cell, Columns) && pWant->Pan >= 0 && pWant->Pan != Pan &&
						AddEffect(r, EF_PAN, static_cast<unsigned char>(pWant->Pan), -1))
						Pan = pWant->Pan;
				}
			}
			if (Cell.Instrument < MAX_INSTRUMENTS && !IsEnd(Cell))
				Instrument = Cell.Instrument;
			if (Cell.Vol < MAX_VOLUME)
				Volume = Cell.Vol;
		}
		Parsed = false;
	}

	// Effects on notes

	// The lasting effects after a row, from those before it: its effects in column order, then a new
	// note ends a slide that isn't its own (as the 2A03 and VRC6 channels do)
	void Step(Lasting &State, const stChanNote &Cell) const
	{
		bool Slides = false;
		for (int i = 0; i < Columns; ++i) {
			const unsigned char Number = Cell.EffNumber[i];
			if (IsPitchEffect(Number) || IsLasting(Number))
				State.Set(Number, Cell.EffParam[i]);
			Slides = Slides || IsSlide(Number);
		}
		if (IsNote(Cell) && !Slides && IsSlide(State.Pitch)) {
			State.Pitch = EF_NONE;
			State.PitchParam = 0;
		}
	}

	// The lasting effects in force where each note starts, after its first row (by where it starts),
	// and coming into it (before its first row)
	void LastingNow(std::map<int, Lasting> &After, std::map<int, Lasting> *pComing = NULL) const
	{
		Parse();
		After.clear();
		Lasting State;
		size_t n = 0;
		for (int r = 0; r < Rows() && n < Notes.size(); ++r) {
			if (Notes[n].Start == r && pComing != NULL)
				(*pComing)[r] = State;
			Step(State, Cells[static_cast<size_t>(r)]);
			if (Notes[n].Start == r)
				After[r] = State, ++n;
		}
	}

	// Sets an effect on a row: one of that kind there gets Param, or it goes in a free column
	bool SetEffect(int Row, unsigned char Number, unsigned char Param)
	{
		stChanNote &Cell = CellAt(Row);
		Parsed = false;
		for (int i = 0; i < Columns; ++i) {
			if (Cell.EffNumber[i] == Number) {
				Cell.EffParam[i] = Param;
				return true;
			}
		}
		return AddEffect(Row, Number, Param, -1);
	}

	// Takes the effects that Which picks out of a row's playing columns
	template <class Pick>
	void RemoveEffects(int Row, Pick Which)
	{
		stChanNote &Cell = CellAt(Row);
		for (int i = 0; i < Columns; ++i) {
			if (Cell.EffNumber[i] != EF_NONE && Which(Cell.EffNumber[i])) {
				Cell.EffNumber[i] = EF_NONE;
				Cell.EffParam[i] = 0;
			}
		}
		Parsed = false;
	}

	// Writes on a row what makes the lasting effects after it Want instead of Now: false if they didn't fit
	bool MakeLasting(int Row, const Lasting &Now, const Lasting &Want)
	{
		bool Fits = true;
		if (!Now.SamePitch(Want)) {
			if (Want.PitchOn() != EF_NONE)
				Fits = SetEffect(Row, Want.Pitch, Want.PitchParam) && Fits;
			else if (!IsSlide(Now.PitchOn()))	// a slide on the row is the note's own and stays
				Fits = SetEffect(Row, Now.PitchOn(), 0) && Fits;
		}
		const unsigned char OTHERS[] = { EF_VIBRATO, EF_TREMOLO, EF_VOLUME_SLIDE, EF_DUTY_CYCLE, EF_PITCH, EF_PAN };
		for (size_t i = 0; i < sizeof(OTHERS); ++i) {
			if (Now.Get(OTHERS[i]) != Want.Get(OTHERS[i]))
				Fits = SetEffect(Row, OTHERS[i], Want.Get(OTHERS[i])) && Fits;
		}
		return Fits;
	}

	// After an effect edit: where each note starts, the lasting effects are those in Want (by where it
	// starts), set on its first row where they aren't. False if some didn't fit.
	bool KeepLasting(const std::map<int, Lasting> &Want)
	{
		bool Fits = true;
		Lasting State;
		for (int r = 0; r < Rows(); ++r) {
			Lasting After = State;
			Step(After, Cells[static_cast<size_t>(r)]);
			const std::map<int, Lasting>::const_iterator It = Want.find(r);
			if (It != Want.end() && IsNote(Cells[static_cast<size_t>(r)]) && After != It->second) {
				Fits = MakeLasting(r, After, It->second) && Fits;
				After = State;
				Step(After, Cells[static_cast<size_t>(r)]);
			}
			State = After;
		}
		Parsed = false;
		return Fits;
	}

	// The notes named, each once, in time order
	std::vector<Note> Named(const int *pStarts, int Count) const
	{
		std::vector<Note> Out;
		for (int k = 0; k < Count; ++k) {
			const int Index = Find(pStarts[k]);
			bool Twice = false;
			for (size_t m = 0; m < Out.size(); ++m)
				Twice = Twice || Out[m].Start == pStarts[k];
			if (Index >= 0 && !Twice)
				Out.push_back(Notes[static_cast<size_t>(Index)]);
		}
		std::sort(Out.begin(), Out.end(), [](const Note &First, const Note &Second) { return First.Start < Second.Start; });
		return Out;
	}

	// A note's cell plays another pitch (it lands where its slide takes it)
	void SetWritten(int Row, int Pitch)
	{
		stChanNote &Cell = CellAt(Row);
		Pitch = Clamp(Pitch, Lowest, Highest);
		Cell.Note = static_cast<unsigned char>(Pitch % 12 + 1);
		Cell.Octave = static_cast<unsigned char>(Pitch / 12);
		Parsed = false;
	}

	// What works while each note sounds (see GetEffects)
	void ParseSpans() const
	{
		Parse();
		if (SpansParsed)
			return;
		SpansParsed = true;
		Spans.assign(Notes.size(), std::vector<EffectSpan>());
		const unsigned char KINDS[] = { EF_NONE, EF_VIBRATO, EF_TREMOLO, EF_VOLUME_SLIDE };	// EF_NONE: the pitch effect
		const size_t COUNT = sizeof(KINDS);
		Lasting State;
		size_t n = 0;
		int Open[COUNT];	// per kind, the span still going on in the note, -1 for none
		std::fill(Open, Open + COUNT, -1);
		for (int r = 0; r < Rows(); ++r) {
			const stChanNote &Cell = Cells[static_cast<size_t>(r)];
			Lasting After = State;
			Step(After, Cell);
			while (n < Notes.size() && Notes[n].End <= r)
				++n;
			if (n < Notes.size() && Notes[n].Start <= r) {
				std::vector<EffectSpan> &Out = Spans[n];
				const bool First = Notes[n].Start == r;
				for (size_t k = 0; k < COUNT; ++k) {
					// What this kind does now, and did on the row before
					unsigned char Number = KINDS[k], Param = 0, WasNumber = KINDS[k], WasParam = 0;
					if (Number == EF_NONE) {
						Number = IsSlide(After.PitchOn()) ? static_cast<unsigned char>(EF_NONE) : After.PitchOn();
						Param = After.PitchParam;
						WasNumber = IsSlide(State.PitchOn()) ? static_cast<unsigned char>(EF_NONE) : State.PitchOn();
						WasParam = State.PitchParam;
					}
					else {
						Param = After.Get(Number);
						WasParam = State.Get(Number);
						if (Param == 0)
							Number = EF_NONE;
						if (WasParam == 0)
							WasNumber = EF_NONE;
					}
					if (!First && Number == WasNumber && (Number == EF_NONE || Param == WasParam))
						continue;
					if (!First && Open[k] >= 0)
						Out[static_cast<size_t>(Open[k])].To = r;
					Open[k] = -1;
					if (Number != EF_NONE) {
						// Taken over when the note's first row doesn't set it
						bool Set = false;
						for (int i = 0; i < Columns; ++i)
							Set = Set || (KINDS[k] == EF_NONE ? IsPitchEffect(Cell.EffNumber[i]) : Cell.EffNumber[i] == KINDS[k]);
						const EffectSpan Span = { Number, Param, r, Notes[n].End, First && !Set };
						Open[k] = static_cast<int>(Out.size());
						Out.push_back(Span);
					}
				}
				// The other effects on the row: slides, the duty and fine pitch where they're set, and the rest
				// (not the pan, which a note shows on its own: Note::Pan)
				for (int i = 0; i < Columns; ++i) {
					const unsigned char Number = Cell.EffNumber[i];
					if (Number == EF_NONE || IsSongEffect(Number) || (IsLasting(Number) && Number != EF_DUTY_CYCLE && Number != EF_PITCH))
						continue;
					const EffectSpan Span = { Number, Cell.EffParam[i], r, r + 1, false };
					Out.push_back(Span);
				}
			}
			State = After;
		}
		for (size_t i = 0; i < Spans.size(); ++i)
			std::stable_sort(Spans[i].begin(), Spans[i].end(),
				[](const EffectSpan &First, const EffectSpan &Second) { return First.From < Second.From; });
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
	New.Sounded.Pan = -1;		// and where the channel is panned
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
		const Sound Now = { Instrument, -1, -1 };
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

void CChannelNotes::ReplaceRows(int First, int End, const CChannelNotes &Take)
{
	Impl &d = *m_pImpl;
	const Impl &t = *Take.m_pImpl;
	First = Clamp(First, 0, d.Rows());
	End = Clamp(End, First, d.Rows());
	if (End <= First)
		return;

	// How the channel played before
	d.Parse();
	const std::vector<Note> Before(d.Notes);
	const Sounds OldSounds = d.SoundsNow();
	std::map<int, Lasting> OldLasting;
	d.LastingNow(OldLasting);
	Lasting Coming;	// the lasting effects in force coming into First
	for (int r = 0; r < First; ++r)
		d.Step(Coming, d.Cells[static_cast<size_t>(r)]);
	const Note *pInto = NULL;	// a note sounding into the rows from before
	bool SoundedOn = false;		// a note sounded on at End into the rows after
	for (size_t i = 0; i < Before.size(); ++i) {
		if (Before[i].Start < First && Before[i].End > First)
			pInto = &Before[i];
		if (Before[i].Start < End && Before[i].End > End)
			SoundedOn = true;
	}

	// The take's cells in place of what the notes owned there
	Sounds Placed;
	for (int r = First; r < End; ++r) {
		if (r - First < t.Rows()) {
			const stChanNote Cell = t.Owned(t.Cells[static_cast<size_t>(r - First)]);
			d.PutOwned(r, Cell);
			if (IsNote(Cell)) {
				const Sound Own = { Cell.Instrument < MAX_INSTRUMENTS ? Cell.Instrument : -1, Cell.Vol < MAX_VOLUME ? Cell.Vol : -1, -1 };
				Placed[r] = Own;
			}
		}
		else
			d.ClearOwned(r);
	}
	// The note sounding into the rows ends where they start, unless the take starts a note there
	stChanNote &Start = d.CellAt(First);
	if (pInto != NULL && !IsNote(Start) && !IsEnd(Start))
		Start.Note = d.NewEnd(pInto->Instrument).Note;
	// The take's notes start as they were played: lasting effects coming in are switched off
	{
		Lasting Now = Coming, Want = Coming;
		Want.Pitch = EF_NONE;
		Want.PitchParam = 0;
		Want.Vibrato = Want.Tremolo = Want.VolumeSlide = 0;
		d.Step(Now, d.Cells[static_cast<size_t>(First)]);
		d.Step(Want, d.Cells[static_cast<size_t>(First)]);
		if (Now != Want)
			d.MakeLasting(First, Now, Want);
	}
	d.Parsed = false;

	// The take's last note, still sounding at End: it carries on where a note did before, or ends there
	if (End < d.Rows()) {
		stChanNote &After = d.CellAt(End);
		d.Parse();
		const int Last = NoteAt(End - 1);
		if (!IsNote(After) && !IsEnd(After) && Last >= 0 && d.Notes[static_cast<size_t>(Last)].End > End && !SoundedOn) {
			After.Note = d.NewEnd(d.Notes[static_cast<size_t>(Last)].Instrument).Note;
			After.Octave = 0;
			d.Parsed = false;
		}
	}

	// The notes after the rows sound as before: their instrument, volume and pan, and their lasting effects
	Sounds Kept;
	std::map<int, Lasting> KeptLasting;
	for (size_t i = 0; i < Before.size(); ++i) {
		if (Before[i].Start < First || Before[i].Start >= End) {
			const Sounds::const_iterator It = OldSounds.find(Before[i].Start);
			if (It != OldSounds.end())
				Kept[Before[i].Start] = It->second;
		}
		if (Before[i].Start >= End && OldLasting.count(Before[i].Start) != 0)
			KeptLasting[Before[i].Start] = OldLasting[Before[i].Start];
	}
	d.Keep(Kept, Placed);
	d.KeepLasting(KeptLasting);
}

CChannelNotes CChannelNotes::TakeRows(int From, int Rows, int Early) const
{
	const Impl &d = *m_pImpl;
	CChannelNotes Out(*this);	// how the channel plays
	Impl &o = *Out.m_pImpl;
	o.Cells.assign(static_cast<size_t>(std::max(0, Rows)), EmptyCell());
	o.Hints.clear();
	o.Parsed = false;
	for (int r = 0; r < Rows; ++r)
		if (From + r >= 0 && From + r < d.Rows())
			o.Cells[static_cast<size_t>(r)] = d.Cells[static_cast<size_t>(From + r)];
	// A note sung a little early starts where the rows do, with what its first row had
	const int Index = NoteAt(From);
	if (Rows > 0 && Index >= 0) {
		const Note &N = d.Notes[static_cast<size_t>(Index)];
		stChanNote &Cell = o.Cells[0];
		if (N.Start < From && N.Start >= From - std::max(0, Early) && !IsNote(Cell) && !IsEnd(Cell)) {
			const stChanNote &Start = d.Cells[static_cast<size_t>(N.Start)];
			Cell.Note = Start.Note;
			Cell.Octave = Start.Octave;
			Cell.Instrument = Start.Instrument < MAX_INSTRUMENTS ? Start.Instrument :
				(N.Instrument >= 0 ? static_cast<unsigned char>(N.Instrument) : Cell.Instrument);
			if (Start.Vol < MAX_VOLUME)
				Cell.Vol = Start.Vol;
			for (int c = 0; c < MAX_EFFECT_COLUMNS; ++c) {
				if (Start.EffNumber[c] == EF_NONE || IsSongEffect(Start.EffNumber[c]))
					continue;
				bool Has = false;
				for (int k = 0; k < MAX_EFFECT_COLUMNS; ++k)
					Has = Has || Cell.EffNumber[k] == Start.EffNumber[c];
				for (int k = 0; k < MAX_EFFECT_COLUMNS && !Has; ++k)
					if (Cell.EffNumber[k] == EF_NONE) {
						Cell.EffNumber[k] = Start.EffNumber[c];
						Cell.EffParam[k] = Start.EffParam[c];
						Has = true;
					}
			}
		}
	}
	return Out;
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

bool CChannelNotes::OnlyEnds(int First, int Last) const
{
	const Impl &d = *m_pImpl;
	for (int r = std::max(0, First); r <= Last && r < d.Rows(); ++r) {
		stChanNote Cell = d.Cells[static_cast<size_t>(r)];
		if (IsEnd(Cell)) {
			Cell.Note = NONE;
			Cell.Octave = 0;
		}
		if (!SameCell(Cell, EmptyCell()))
			return false;
	}
	return true;
}

void CChannelNotes::GetEffects(int Index, std::vector<EffectSpan> &Out) const
{
	const Impl &d = *m_pImpl;
	d.ParseSpans();
	Out.clear();
	if (Index >= 0 && Index < static_cast<int>(d.Spans.size()))
		Out = d.Spans[static_cast<size_t>(Index)];
}

bool CChannelNotes::SetLasting(const int *pStarts, int Count, unsigned char Number, const unsigned char *pParams, const int *pDelays)
{
	Impl &d = *m_pImpl;
	if (!IsLasting(Number) || Number == EF_DUTY_CYCLE || Number == EF_PITCH || Number == EF_PAN || Number == EF_PORTA_UP ||
		Number == EF_PORTA_DOWN)
		return true;
	d.Parse();
	std::map<int, Lasting> Want;
	d.LastingNow(Want);
	const std::vector<Note> Targets = d.Named(pStarts, Count);
	const bool Pitch = IsPitchEffect(Number);
	bool Fits = true;
	std::vector<int> Louder;	// notes after a volume slide, to get their volume written
	for (size_t t = 0; t < Targets.size(); ++t) {
		const Note &Which = Targets[t];
		unsigned char Param = 0;
		int Delay = 0;
		for (int k = 0; k < Count; ++k) {
			if (pStarts[k] == Which.Start) {
				Param = pParams[k];
				Delay = Number == EF_PORTAMENTO ? 0 : Clamp(pDelays[k], 0, Which.End - Which.Start - 1);
			}
		}
		// What it had of the kind goes; an arpeggio or portamento replaces any pitch effect on its first
		// row, a slide into it included (it plays where it landed)
		const bool HasEnd = d.EndsOnCell(Which);
		for (int r = Which.Start; r <= Which.End && r < d.Rows(); ++r) {
			if (r < Which.End || HasEnd)
				d.RemoveEffects(r, [Number](unsigned char Other) { return Other == Number; });
		}
		if (Pitch) {
			d.RemoveEffects(Which.Start, IsPitchEffect);
			if (Which.Written != Which.Pitch)
				d.SetWritten(Which.Start, Which.Pitch);
		}
		Lasting &Start = Want[Which.Start];
		Start.Set(Number, Delay == 0 ? Param : static_cast<unsigned char>(0));
		if (Pitch && Param == 0) {
			Start.Pitch = EF_NONE;
			Start.PitchParam = 0;
		}
		if (Param != 0)		// on its own row even when the note before has the same, so it goes where the note goes
			Fits = d.SetEffect(Which.Start + Delay, Number, Param) && Fits;
		if (Number == EF_VOLUME_SLIDE && Param != 0)
			Louder.push_back(Which.End);
	}
	Fits = d.KeepLasting(Want) && Fits;

	// The last note doesn't carry it on past the channel's end (into a loop's start); the note after a
	// volume slide gets the volume it plays at, which the slide changed
	d.Parse();
	for (size_t t = 0; t < Targets.size(); ++t) {
		const int Index = d.Find(Targets[t].Start);
		if (Index < 0 || static_cast<size_t>(Index) + 1 != d.Notes.size())
			continue;
		const Note &Last = d.Notes[static_cast<size_t>(Index)];
		unsigned char Param = 0;
		for (int k = 0; k < Count; ++k)
			if (pStarts[k] == Last.Start)
				Param = pParams[k];
		if (Param != 0 && d.EndsOnCell(Last))
			Fits = d.SetEffect(Last.End, Number, 0) && Fits;
	}
	for (size_t i = 0; i < Louder.size(); ++i) {
		for (size_t n = 0; n < d.Notes.size(); ++n) {	// the first note from where the faded one ended
			if (d.Notes[n].Start >= Louder[i]) {
				stChanNote &Cell = d.CellAt(d.Notes[n].Start);
				if (Cell.Vol >= MAX_VOLUME)
					Cell.Vol = static_cast<unsigned char>(d.Notes[n].Volume);
				break;
			}
		}
	}
	d.Parsed = false;
	return Fits;
}

bool CChannelNotes::SetSlideIn(const int *pStarts, int Count, int Semitones, int Speed)
{
	Impl &d = *m_pImpl;
	d.Parse();
	std::map<int, Lasting> Want;
	d.LastingNow(Want);
	const std::vector<Note> Targets = d.Named(pStarts, Count);
	const int Size = std::min(15, std::abs(Semitones));
	const int Fast = Clamp(Speed, 1, 15);
	bool Fits = true;
	for (size_t t = 0; t < Targets.size(); ++t) {
		const Note &Which = Targets[t];
		// From below goes up to it, from above down
		const int From = Clamp(Which.Pitch + (Semitones > 0 ? -Size : Size), d.Lowest, d.Highest);
		const int Slide = std::abs(Which.Pitch - From);
		d.RemoveEffects(Which.Start, Slide > 0 ? IsPitchEffect : IsSlide);
		d.SetWritten(Which.Start, Slide > 0 ? From : Which.Pitch);
		Lasting &Start = Want[Which.Start];
		if (Slide > 0) {
			const unsigned char Number = static_cast<unsigned char>(From < Which.Pitch ? EF_SLIDE_UP : EF_SLIDE_DOWN);
			Fits = d.AddEffect(Which.Start, Number, static_cast<unsigned char>((Fast << 4) | Slide), -1) && Fits;
			Start.Pitch = Number;
			Start.PitchParam = static_cast<unsigned char>((Fast << 4) | Slide);
		}
		else if (IsSlide(Start.PitchOn())) {
			Start.Pitch = EF_NONE;
			Start.PitchParam = 0;
		}
	}
	Fits = d.KeepLasting(Want) && Fits;
	return Fits;
}

bool CChannelNotes::SetBend(const int *pStarts, int Count, int Semitones, int Speed, int Before)
{
	Impl &d = *m_pImpl;
	d.Parse();
	std::map<int, Lasting> Want;
	d.LastingNow(Want);
	const std::vector<Note> Targets = d.Named(pStarts, Count);
	const int Size = std::min(15, std::abs(Semitones));
	bool Fits = true;
	for (size_t t = 0; t < Targets.size(); ++t) {
		const Note &Which = Targets[t];
		if (Which.End - Which.Start < 2)
			continue;	// no room after its first row
		for (int r = Which.Start + 1; r < Which.End; ++r)
			d.RemoveEffects(r, IsSlide);
		if (Size > 0) {
			const int Row = std::max(Which.Start + 1, Which.End - std::max(1, Before));
			Fits = d.SetEffect(Row, static_cast<unsigned char>(Semitones > 0 ? EF_SLIDE_UP : EF_SLIDE_DOWN),
				static_cast<unsigned char>((Clamp(Speed, 1, 15) << 4) | Size)) && Fits;
		}
	}
	Fits = d.KeepLasting(Want) && Fits;
	return Fits;
}

bool CChannelNotes::ClearEffects(const int *pStarts, int Count)
{
	Impl &d = *m_pImpl;
	d.Parse();
	std::map<int, Lasting> Want, Coming;
	d.LastingNow(Want, &Coming);
	const std::vector<Note> Targets = d.Named(pStarts, Count);
	for (size_t t = 0; t < Targets.size(); ++t) {
		const Note &Which = Targets[t];
		const bool HasEnd = d.EndsOnCell(Which);
		for (int r = Which.Start; r <= Which.End && r < d.Rows(); ++r) {
			if (r < Which.End || HasEnd)
				d.RemoveEffects(r, [](unsigned char Number) { return !IsSongEffect(Number); });
		}
		if (Which.Written != Which.Pitch)
			d.SetWritten(Which.Start, Which.Pitch);
		// Nothing lasting works on it any more, but the duty and fine pitch it came in with, and it
		// stays where it was panned
		Lasting None;
		None.Duty = Coming[Which.Start].Duty;
		None.FinePitch = Coming[Which.Start].FinePitch;
		None.Pan = Want[Which.Start].Pan;
		Want[Which.Start] = None;
	}
	return d.KeepLasting(Want);
}

bool CChannelNotes::SetPan(const int *pStarts, int Count, unsigned char Pan)
{
	Impl &d = *m_pImpl;
	d.Parse();
	std::map<int, Lasting> Want;
	d.LastingNow(Want);
	const std::vector<Note> Targets = d.Named(pStarts, Count);
	for (size_t t = 0; t < Targets.size(); ++t) {
		const Note &Which = Targets[t];
		for (int r = Which.Start; r < Which.End; ++r)
			d.RemoveEffects(r, [](unsigned char Number) { return Number == EF_PAN; });
		Want[Which.Start].Pan = Pan;
	}
	bool Fits = d.KeepLasting(Want);

	// When the song loops, the channel's first note comes after its last one: it gets its pan written
	// if the last one leaves the channel elsewhere
	d.Parse();
	if (!Targets.empty() && !d.Notes.empty()) {
		int End = PAN_CENTER;
		for (int r = 0; r < d.Rows(); ++r)
			End = PanAfter(End, d.Cells[static_cast<size_t>(r)], d.Columns);
		const Note &First = d.Notes.front();
		if (End != First.Pan && !SetsPan(d.Cells[static_cast<size_t>(First.Start)], d.Columns))
			Fits = d.SetEffect(First.Start, EF_PAN, static_cast<unsigned char>(First.Pan)) && Fits;
	}
	d.Parsed = false;
	return Fits;
}

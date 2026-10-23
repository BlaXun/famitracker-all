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

#include "PulseClicks.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace ftapi {

namespace {

const int MAX_PERIOD = 0x7FF;
const int FINE_CENTER = 0x80;	// P80: no change

int LimitPeriod(int Period)
{
	return std::max(0, std::min(MAX_PERIOD, Period));
}

// 4xy's change to the period at a phase (0-63) for a depth (0-15), as
// CChannelHandler::GetVibrato works it out from CSoundGen's table in the new
// vibrato style (FamiTracker's own since 0.3.0)
int VibratoAt(int Depth, int Phase)
{
	static const double DEPTHS[16] = { 1.0, 1.5, 2.5, 4.0, 5.0, 7.0, 10.0, 12.0, 14.0, 17.0, 22.0, 30.0, 44.0, 64.0, 96.0, 128.0 };
	const double Most = DEPTHS[Depth & 15];
	auto Table = [Most](int Step) { return static_cast<int>(std::sin(Step / 16.0 * (3.1415 / 2.0)) * Most); };
	Phase &= 63;
	if (Phase < 16)
		return Table(Phase);
	if (Phase < 32)
		return Table(15 - (Phase - 16));
	if (Phase < 48)
		return -Table(Phase - 32);
	return -Table(15 - (Phase - 48));
}

// An instrument's sequence as CSequenceHandler runs it
class SequencePlay {
public:
	SequencePlay() : m_pSeq(0), m_Pointer(0), m_State(OFF) {}

	void Start(const SequenceInfo *pSeq)
	{
		m_pSeq = pSeq != 0 && !pSeq->Items.empty() ? pSeq : 0;
		m_Pointer = 0;
		m_State = m_pSeq != 0 ? RUNNING : OFF;
	}

	// ReleaseSequence: on to the release point, when it has one
	void Release()
	{
		if (m_pSeq != 0 && (m_State == RUNNING || m_State == AT_END) && m_pSeq->Release >= 0) {
			m_Pointer = m_pSeq->Release;
			m_State = RUNNING;
		}
	}

	// This frame's value (UpdateSequenceRunning); false when it doesn't run.
	// Ended: this is the frame after its last value (UpdateSequenceEnd).
	bool Step(bool Releasing, int &Value, bool &Ended)
	{
		Ended = false;
		if (m_pSeq == 0)
			return false;
		if (m_State == AT_END) {
			m_State = HALTED;
			Ended = true;
			return false;
		}
		if (m_State != RUNNING)
			return false;
		const int Items = static_cast<int>(m_pSeq->Items.size());
		Value = m_pSeq->Items[static_cast<size_t>(std::max(0, std::min(m_Pointer, Items - 1)))];
		++m_Pointer;
		const int Release = m_pSeq->Release, Loop = m_pSeq->Loop;
		if (m_Pointer == Release + 1 || m_Pointer >= Items) {
			if (Loop != -1 && !(Releasing && Release != -1))
				m_Pointer = Loop;
			else if (m_Pointer >= Items)
				m_State = AT_END;
			else if (!Releasing)
				--m_Pointer;
		}
		return true;
	}

private:
	enum { OFF, RUNNING, AT_END, HALTED };
	const SequenceInfo *m_pSeq;
	int m_Pointer, m_State;
};

const InstrumentInfo *FindInfo(const SongInfo &Song, int Index)
{
	for (size_t i = 0; i < Song.Instruments.size(); ++i)
		if (Song.Instruments[i].Index == Index)
			return &Song.Instruments[i];
	return 0;
}

// A fine pitch on a row: the Pxx already there changes, or one goes in a free
// column, past the shown ones (Columns grows) when those are full
bool PutFine(Cell &C, int Value, int &Columns)
{
	if (AddEffect(C, EFF_PITCH, Value, Columns))
		return true;
	if (!AddEffect(C, EFF_PITCH, Value, EFFECT_COLUMNS))
		return false;
	for (int k = 0; k < EFFECT_COLUMNS; ++k)
		if (C.Effect[k] == EFF_PITCH)
			Columns = std::max(Columns, k + 1);
	return true;
}

// The fine pitch a row's own Pxx sets, -1 for none
int OwnFine(const Cell &C, int Columns)
{
	int Fine = -1;
	for (int k = 0; k < std::max(1, std::min(EFFECT_COLUMNS, Columns)); ++k)
		if (C.Effect[k] == EFF_PITCH)
			Fine = C.Param[k];
	return Fine;
}

}

bool ClicksOnPeriods(const ChannelInfo &Info)
{
	return Info.Kind == KIND_PULSE && (Info.Chip == "2A03" || Info.Chip == "MMC5");
}

double NudgeCents(int Period, int Shift)
{
	if (Period + 1 + Shift <= 0)
		return 1e9;
	return std::fabs(1200.0 * std::log2((Period + 1.0) / (Period + 1.0 + Shift)));
}

int PulsePeriod(int Pitch, bool Pal)
{
	const double Clock = (Pal ? 1662607.0 : 1789773.0) / 16.0;
	const double Hz = 32.7032 * std::pow(2.0, std::max(0, std::min(PITCHES - 1, Pitch)) / 12.0);
	return static_cast<int>(Clock / Hz - 0.5);
}

std::vector<std::pair<int, int> > PlayedFrames(const SongInfo &Song, const std::vector<std::vector<Cell> > &Channels,
	const std::vector<int> &Columns, int &LoopRow)
{
	const int Length = std::max(1, Song.PatternLength);
	size_t Rows = 0;
	for (size_t c = 0; c < Channels.size(); ++c)
		Rows = std::max(Rows, Channels[c].size());
	const int Frames = std::max(1, static_cast<int>((Rows + Length - 1) / Length));
	const SongFlow Flow = FollowSong(Channels, Columns, Length, Frames);
	LoopRow = Flow.LoopRow(Length);
	int EndTick = 0;
	const std::vector<TimedRow> Timed = TimeRows(Channels, Columns, Flow, Length, Song.Speed, Song.Tempo, Song.SplitPoint,
		Song.FrameRate, 2, EndTick);
	std::vector<std::pair<int, int> > Out;
	for (size_t i = 0; i < Timed.size(); ++i)
		Out.push_back(std::make_pair(Timed[i].Row, std::max(0, (i + 1 < Timed.size() ? Timed[i + 1].Tick : EndTick) - Timed[i].Tick)));
	return Out;
}

std::vector<PulseNote> PlayPulse(const SongInfo &Song, const std::vector<Cell> &Cells, int Columns,
	const std::vector<std::pair<int, int> > &Played)
{
	std::vector<PulseNote> Notes;
	const int Count = std::max(1, std::min(EFFECT_COLUMNS, Columns));
	// The channel handler's state (CChannelHandler, CChannelHandler2A03)
	int Pitch = 0, Period = 0, Fine = FINE_CENTER, VibratoSpeed = 0, VibratoDepth = 0, VibratoPhase = 0;
	int Level = 127, Slide = 0, Effect = EFF_NONE, PortaSpeed = 0, PortaTo = 0, Arpeggio = 0, ArpState = 0, NoteCut = 0;
	int Instrument = NO_INSTRUMENT, Volume = 15;
	bool Gate = false, Releasing = false;
	SequenceInfo VolumeSequence;
	const SequenceInfo *pSteps = 0;
	SequencePlay Volumes, Steps, Pitches, HiPitches;
	int Current = -1, Last = -1;
	bool Attack = false;
	auto SetupSlide = [&](int Type, int Param) {
		PortaSpeed = ((Param & 0xF0) >> 3) + 1;
		Effect = Type;
		Pitch += Type == EFF_SLIDE_UP ? (Param & 15) : -(Param & 15);
		PortaTo = PulsePeriod(Pitch, Song.Pal);
	};
	for (size_t i = 0; i < Played.size(); ++i) {
		const int Row = Played[i].first;
		const Cell C = Row >= 0 && Row < static_cast<int>(Cells.size()) ? Cells[static_cast<size_t>(Row)] : Cell();
		// HandleNoteData: the effects, then the volume, the instrument and the note
		if (C.Note != NOTE_NONE)
			NoteCut = 0;
		int Post = EFF_NONE, PostParam = 0;
		for (int k = 0; k < Count; ++k) {
			const int Param = C.Param[k];
			switch (C.Effect[k]) {
			case EFF_VIBRATO:
				VibratoDepth = Param & 15;
				VibratoSpeed = Param >> 4;
				if (Param == 0)
					VibratoPhase = 0;
				break;
			case EFF_PITCH:
				Fine = Param;
				break;
			case EFF_VOLUME_SLIDE:
				Slide = Param;
				break;
			case EFF_ARPEGGIO:
				Arpeggio = Param;
				Effect = EFF_ARPEGGIO;
				break;
			case EFF_PORTAMENTO:
				PortaSpeed = Param;
				Effect = EFF_PORTAMENTO;
				if (Param == 0)
					PortaTo = 0;
				break;
			case EFF_PORTA_UP:
			case EFF_PORTA_DOWN:
				PortaSpeed = Param;
				Effect = C.Effect[k];
				break;
			case EFF_SLIDE_UP:
			case EFF_SLIDE_DOWN:
				Post = C.Effect[k];
				PostParam = Param;
				SetupSlide(Post, PostParam);
				break;
			case EFF_NOTE_CUT:
				NoteCut = Param + 1;
				break;
			}
		}
		if (C.Volume < NO_VOLUME)
			Level = C.Volume << 3;
		if (C.Instrument != NO_INSTRUMENT && !C.IsEnd())
			Instrument = C.Instrument;
		if (C.HasNote()) {
			// The instrument's sequences start over, and the note plays (RunNote)
			const InstrumentInfo *pInfo = FindInfo(Song, Instrument);
			VolumeSequence = SequenceInfo();
			if (pInfo != 0) {
				VolumeSequence.Items = pInfo->Volume;
				VolumeSequence.Loop = pInfo->VolumeLoop;
				VolumeSequence.Release = pInfo->VolumeRelease;
			}
			pSteps = pInfo != 0 ? &pInfo->Steps : 0;
			Volumes.Start(&VolumeSequence);
			Steps.Start(pSteps);
			Pitches.Start(pInfo != 0 ? &pInfo->Pitch : 0);
			HiPitches.Start(pInfo != 0 ? &pInfo->HiPitch : 0);
			Pitch = C.Pitch();
			const int Target = PulsePeriod(Pitch, Song.Pal);
			if (PortaSpeed > 0 && Effect == EFF_PORTAMENTO) {
				if (Period == 0)
					Period = Target;
				PortaTo = Target;
			}
			else
				Period = Target;
			Gate = true;
			Releasing = false;
			Volume = 15;
			if (Post != EFF_NONE)
				SetupSlide(Post, PostParam);
			else if (Effect == EFF_SLIDE_UP || Effect == EFF_SLIDE_DOWN)
				Effect = EFF_NONE;
			PulseNote Note;
			Note.Row = Row;
			Note.Pitch = C.Pitch();
			Note.Fine = Fine;
			Notes.push_back(Note);
			Current = static_cast<int>(Notes.size()) - 1;
			Attack = true;
		}
		else if (C.Note == NOTE_HALT) {
			Gate = false;
			Releasing = false;
			Period = 0;
			PortaTo = 0;
			Current = -1;
		}
		else if (C.Note == NOTE_RELEASE && !Releasing) {
			Releasing = true;
			Volumes.Release();
			Steps.Release();
			Pitches.Release();
			HiPitches.Release();
		}

		// Each frame: ProcessChannel (the note cut, the volume slide, the
		// vibrato's phase, the slides and arpeggio, the sequences), then
		// RefreshChannel
		if (Current >= 0 && Played[i].second > 0)
			Notes[static_cast<size_t>(Current)].Rows.push_back(std::make_pair(Row,
				static_cast<int>(Notes[static_cast<size_t>(Current)].Periods.size())));
		for (int f = 0; f < Played[i].second; ++f) {
			if (NoteCut > 0 && --NoteCut == 0) {
				Gate = false;
				Period = 0;
				PortaTo = 0;
			}
			Level = std::max(0, Level - (Slide & 15));
			Level = std::min(127, Level + (Slide >> 4));
			VibratoPhase = (VibratoPhase + VibratoSpeed) & 63;
			const int Before = Period;
			switch (Effect) {
			case EFF_ARPEGGIO:
				if (Arpeggio != 0) {
					if (ArpState == 0)
						Period = PulsePeriod(Pitch, Song.Pal);
					else if (ArpState == 1) {
						Period = PulsePeriod(Pitch + (Arpeggio >> 4), Song.Pal);
						if ((Arpeggio & 15) == 0)
							++ArpState;
					}
					else
						Period = PulsePeriod(Pitch + (Arpeggio & 15), Song.Pal);
					ArpState = (ArpState + 1) % 3;
				}
				break;
			case EFF_PORTAMENTO:
				if (PortaSpeed > 0 && PortaTo > 0) {
					if (Period > PortaTo)
						Period = std::max(PortaTo, LimitPeriod(Period - PortaSpeed));
					else if (Period < PortaTo)
						Period = std::min(PortaTo, LimitPeriod(Period + PortaSpeed));
				}
				break;
			case EFF_SLIDE_UP:
				if (PortaSpeed > 0) {
					Period = LimitPeriod(Period - PortaSpeed);
					if (Period < PortaTo) {
						Period = PortaTo;
						PortaTo = 0;
						Effect = EFF_NONE;
					}
				}
				break;
			case EFF_SLIDE_DOWN:
				if (PortaSpeed > 0) {
					Period = LimitPeriod(Period + PortaSpeed);
					if (Period > PortaTo) {
						Period = PortaTo;
						PortaTo = 0;
						Effect = EFF_NONE;
					}
				}
				break;
			case EFF_PORTA_DOWN:
				if (Period > 0)
					Period = LimitPeriod(Period + PortaSpeed);
				break;
			case EFF_PORTA_UP:
				if (Period > 0)
					Period = LimitPeriod(Period - PortaSpeed);
				break;
			}
			bool Moved = Period != Before;
			if (Gate) {
				int Value = 0;
				bool Ended = false;
				if (Volumes.Step(Releasing, Value, Ended))
					Volume = Value;
				const int Was = Period;
				if (Steps.Step(Releasing, Value, Ended)) {
					if (pSteps->Setting == 1)			// fixed
						Period = PulsePeriod(Value, Song.Pal);
					else if (pSteps->Setting == 2) {	// relative
						Pitch += Value;
						Period = PulsePeriod(Pitch, Song.Pal);
					}
					else
						Period = PulsePeriod(Pitch + Value, Song.Pal);
				}
				else if (Ended && pSteps != 0 && pSteps->Setting == 1)
					Period = PulsePeriod(Pitch, Song.Pal);
				Moved = Moved || Period != Was;
				if (Pitches.Step(Releasing, Value, Ended))
					Period = LimitPeriod(Period + Value);
				if (HiPitches.Step(Releasing, Value, Ended))
					Period = LimitPeriod(Period + Value * 16);
			}
			const int Out = LimitPeriod(Period - VibratoAt(VibratoDepth, VibratoPhase) + (FINE_CENTER - Fine));
			const int Column = Level >> 3;
			const int Loud = Column > 0 && Volume > 0 ? std::max(1, Column * std::min(15, Volume) / 15) : 0;
			const bool Sounds = Gate && Loud > 0;
			if (Current >= 0) {
				PulseNote &Note = Notes[static_cast<size_t>(Current)];
				Note.Periods.push_back(Sounds ? Out : -1);
				Note.Steady.push_back(Sounds && !Attack && !Moved && Last >= 0 ? 1 : 0);
			}
			Last = Sounds ? Out : -1;
			Attack = false;
		}
	}
	return Notes;
}

int PulseClicks(const PulseNote &Note, int Shift, int From)
{
	int Clicks = 0;
	for (size_t f = 1; f < Note.Periods.size() && f < Note.Steady.size(); ++f) {
		const bool Moved = static_cast<int>(f) == From && From > 0;	// the nudge coming in
		if ((!Note.Steady[f] && !Moved) || Note.Periods[f] < 0 || Note.Periods[f - 1] < 0)
			continue;
		const int Now = LimitPeriod(Note.Periods[f] + (static_cast<int>(f) >= From ? Shift : 0));
		const int Was = LimitPeriod(Note.Periods[f - 1] + (static_cast<int>(f) - 1 >= From ? Shift : 0));
		if ((Now >> 8) != (Was >> 8))
			++Clicks;
	}
	return Clicks;
}

std::vector<ClickFix> SmoothClicks(const SongInfo &Song, std::vector<Cell> &Cells, int &Columns, int First, int End,
	const std::vector<std::pair<int, int> > &Played, int LoopRow, bool Fix, bool &Lost)
{
	std::vector<ClickFix> Out;
	const std::vector<PulseNote> Notes = PlayPulse(Song, Cells, Columns, Played);
	std::map<int, std::vector<const PulseNote *> > Plays;	// every time each note row plays
	for (size_t i = 0; i < Notes.size(); ++i)
		Plays[Notes[i].Row].push_back(&Notes[i]);
	// Where each note's sound ends: the next note or halt
	std::map<int, int> Ends;
	int Open = -1;
	for (int r = 0; r < static_cast<int>(Cells.size()); ++r) {
		const Cell &C = Cells[static_cast<size_t>(r)];
		if (C.HasNote() || C.Note == NOTE_HALT) {
			if (Open >= 0)
				Ends[Open] = r;
			Open = C.HasNote() ? r : -1;
		}
	}
	if (Open >= 0)
		Ends[Open] = static_cast<int>(Cells.size());

	std::map<int, ClickFix> Nudges;	// by note row
	for (std::map<int, std::vector<const PulseNote *> >::const_iterator it = Plays.begin(); it != Plays.end(); ++it) {
		const int Row = it->first;
		const std::map<int, int>::const_iterator Ending = Ends.find(Row);
		const int Stop = Ending != Ends.end() ? Ending->second : Row + 1;
		if (Stop <= First || Row >= End)
			continue;
		const std::vector<const PulseNote *> &Times = it->second;
		int Clicks = 0;	// the most it makes as it plays once
		for (size_t p = 0; p < Times.size(); ++p)
			Clicks = std::max(Clicks, PulseClicks(*Times[p], 0));
		if (Clicks < 2)
			continue;
		const PulseNote &Note = *Times.front();
		const int Period = PulsePeriod(Note.Pitch, Song.Pal) + FINE_CENTER - Note.Fine;
		// The clicks left with the period moved Shift from row At on, every time it plays
		auto LeftWith = [&](int Shift, int At) {
			int Left = 0;
			for (size_t p = 0; p < Times.size(); ++p) {
				int From = -1;
				for (size_t k = 0; k < Times[p]->Rows.size() && From < 0; ++k)
					if (Times[p]->Rows[k].first == At)
						From = Times[p]->Rows[k].second;
				Left += From < 0 ? 1 : PulseClicks(*Times[p], Shift, From);
			}
			return Left;
		};
		// The least nudge that stops them, sharper first: from the note's row, or else from the row its vibrato starts
		// in, the first where only the vibrato moves its period
		int Shift = 0, At = Row;
		for (int Late = 0; Late < 2 && Shift == 0; ++Late) {
			const double Most = Late ? MAX_LATE_NUDGE_CENTS : MAX_NUDGE_CENTS;
			if (Late) {
				int Wobble = -1;
				for (size_t f = 1; f < Note.Periods.size() && Wobble < 0; ++f)
					if (Note.Steady[f] && Note.Periods[f] >= 0 && Note.Periods[f - 1] >= 0 && Note.Periods[f] != Note.Periods[f - 1])
						Wobble = static_cast<int>(f);
				for (size_t k = 0; k < Note.Rows.size(); ++k)
					if (Note.Rows[k].second <= Wobble)
						At = Note.Rows[k].first;
			}
			for (int Step = 1; Shift == 0 && NudgeCents(Period, Step) <= Most; ++Step) {
				const int Tries[2] = { -Step, Step };
				for (int t = 0; t < 2 && Shift == 0; ++t) {
					if (NudgeCents(Period, Tries[t]) > Most || Note.Fine - Tries[t] < 0 || Note.Fine - Tries[t] > 0xFF)
						continue;
					if (LeftWith(Tries[t], At) == 0)
						Shift = Tries[t];
				}
			}
		}
		ClickFix F;
		F.Row = Row;
		F.Pitch = Note.Pitch;
		F.Clicks = Clicks;
		F.Shift = Shift;
		F.At = Shift != 0 ? At : Row;
		F.Fine = Note.Fine - Shift;
		F.Left = Clicks;
		Out.push_back(F);
		if (Shift != 0)
			Nudges[Row] = F;
	}
	if (!Fix || Nudges.empty())
		return Out;

	// Every note starts with the fine pitch it had, or its nudge, and a nudge
	// that waits for the vibrato comes in on its row: a Pxx goes where the one
	// in effect would differ
	std::map<int, int> Want, Starts;	// the fine pitch a row needs; the ones the notes start with
	for (std::map<int, std::vector<const PulseNote *> >::const_iterator it = Plays.begin(); it != Plays.end(); ++it) {
		const std::map<int, ClickFix>::const_iterator n = Nudges.find(it->first);
		const int Had = it->second.front()->Fine;
		Want[it->first] = n != Nudges.end() && n->second.At == it->first ? n->second.Fine : Had;
		if (n != Nudges.end() && n->second.At != it->first)
			Want[n->second.At] = n->second.Fine;
		Starts[it->first] = Want[it->first];
	}
	int Before = FINE_CENTER, After = FINE_CENTER;	// the fine pitch the song ends with, as it was and now
	for (int r = 0; r < static_cast<int>(Cells.size()); ++r) {
		Cell &C = Cells[static_cast<size_t>(r)];
		int Own = OwnFine(C, Columns);
		if (Own >= 0)
			Before = Own;
		const std::map<int, int>::const_iterator w = Want.find(r);
		if (w != Want.end() && (Own >= 0 ? Own : After) != w->second) {
			if (PutFine(C, w->second, Columns))
				Own = w->second;
			else
				Lost = true;
		}
		if (Own >= 0)
			After = Own;
	}
	// A song that now ends on a nudge: the loop's first note starts with its own
	if (LoopRow >= 0 && After != Before) {
		for (std::map<int, int>::const_iterator w = Starts.lower_bound(LoopRow); w != Starts.end(); ++w) {
			Cell &C = Cells[static_cast<size_t>(w->first)];
			if (OwnFine(C, Columns) < 0 && !PutFine(C, w->second, Columns))
				Lost = true;
			break;
		}
	}

	// What's left
	const std::vector<PulseNote> Now = PlayPulse(Song, Cells, Columns, Played);
	std::map<int, int> Left;
	for (size_t i = 0; i < Now.size(); ++i)
		Left[Now[i].Row] = std::max(Left[Now[i].Row], PulseClicks(Now[i], 0));
	for (size_t i = 0; i < Out.size(); ++i)
		Out[i].Left = Left[Out[i].Row];
	return Out;
}

}

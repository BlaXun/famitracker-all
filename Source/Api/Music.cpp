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

#include "Music.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ftapi {

namespace {

const char *const SHARP_NAMES[] = { "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-" };
const char *const SHARP_CLASSES[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
const char *const FLAT_CLASSES[] = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };

// Letters as EFF_CHAR (FamiTrackerTypes.h), by effect - 1
const char EFFECT_LETTERS[EFF_COUNT - 1] = {
	'F', 'B', 'D', 'C', 'E', '3', 0, 'H', 'I', '0', '4', '7', 'P', 'G', 'Z', '1', '2', 'V', 'Y', 'Q', 'R', 'A', 'S',
	'X', 0, 'H', 'I', 'J', 'W', 'H', 'I', 'J'
};

struct ModeInfo {
	const char *Name;
	int Steps[7];
	int ParentOffset;	// semitones from the major key with the same notes up to this mode's tonic
};

const ModeInfo MODES[] = {
	{ "major", { 0, 2, 4, 5, 7, 9, 11 }, 0 },
	{ "dorian", { 0, 2, 3, 5, 7, 9, 10 }, 2 },
	{ "phrygian", { 0, 1, 3, 5, 7, 8, 10 }, 4 },
	{ "lydian", { 0, 2, 4, 6, 7, 9, 11 }, 5 },
	{ "mixolydian", { 0, 2, 4, 5, 7, 9, 10 }, 7 },
	{ "minor", { 0, 2, 3, 5, 7, 8, 10 }, 9 },
	{ "locrian", { 0, 1, 3, 5, 6, 8, 10 }, 11 },
	{ "harmonic minor", { 0, 2, 3, 5, 7, 8, 11 }, 9 },
};
const int MODE_COUNT = sizeof(MODES) / sizeof(MODES[0]);

int Mod12(int Value)
{
	return ((Value % 12) + 12) % 12;
}

std::string Trimmed(const std::string &Text)
{
	size_t First = 0, Last = Text.size();
	while (First < Last && std::isspace(static_cast<unsigned char>(Text[First])))
		++First;
	while (Last > First && std::isspace(static_cast<unsigned char>(Text[Last - 1])))
		--Last;
	return Text.substr(First, Last - First);
}

// A note letter and accidental at the start of Text: the pitch class, and
// how many characters it took
bool ParseNoteClass(const std::string &Text, int &PitchClass, size_t &Used)
{
	static const int LETTER_CLASS[] = { 9, 11, 0, 2, 4, 5, 7 };	// A to G
	if (Text.empty())
		return false;
	const char Letter = static_cast<char>(std::tolower(static_cast<unsigned char>(Text[0])));
	if (Letter < 'a' || Letter > 'g')
		return false;
	PitchClass = LETTER_CLASS[Letter - 'a'];
	Used = 1;
	if (Text.size() > 1) {
		if (Text[1] == '#') {
			++PitchClass;
			Used = 2;
		}
		else if (Text[1] == 'b' && (Text.size() == 2 || Text[2] != 'a')) {
			--PitchClass;
			Used = 2;
		}
		else if (Text.compare(1, 3, "\xE2\x99\xAF") == 0) {	// U+266F sharp
			++PitchClass;
			Used = 4;
		}
		else if (Text.compare(1, 3, "\xE2\x99\xAD") == 0) {	// U+266D flat
			--PitchClass;
			Used = 4;
		}
	}
	return true;
}

bool UsesFlats(const Key &K)
{
	const int Mode = (K.Mode >= 0 && K.Mode < MODE_COUNT) ? K.Mode : 0;
	const int Parent = Mod12(K.Tonic - MODES[Mode].ParentOffset);
	return Parent == 5 || Parent == 10 || Parent == 3 || Parent == 8 || Parent == 1;	// F, Bb, Eb, Ab, Db major
}

}	// namespace

bool HasEffect(const Cell &C, int Effect, int Columns)
{
	for (int k = 0; k < Columns && k < EFFECT_COLUMNS; ++k)
		if (C.Effect[k] == Effect)
			return true;
	return false;
}

// Cells

Cell::Cell() : Note(NOTE_NONE), Octave(0), Instrument(NO_INSTRUMENT), Volume(NO_VOLUME)
{
	for (int i = 0; i < EFFECT_COLUMNS; ++i) {
		Effect[i] = EFF_NONE;
		Param[i] = 0;
	}
}

bool Cell::IsEmpty() const
{
	if (Note != NOTE_NONE || Instrument != NO_INSTRUMENT || Volume != NO_VOLUME)
		return false;
	for (int i = 0; i < EFFECT_COLUMNS; ++i)
		if (Effect[i] != EFF_NONE)
			return false;
	return true;
}

int Cell::Pitch() const
{
	return HasNote() ? Octave * 12 + Note - 1 : -1;
}

void Cell::SetPitch(int Value)
{
	Value = std::max(0, std::min(PITCHES - 1, Value));
	Note = static_cast<unsigned char>(Value % 12 + 1);
	Octave = static_cast<unsigned char>(Value / 12);
}

bool Cell::operator==(const Cell &Other) const
{
	if (Note != Other.Note || Instrument != Other.Instrument || Volume != Other.Volume)
		return false;
	if (HasNote() && Octave != Other.Octave)
		return false;
	for (int i = 0; i < EFFECT_COLUMNS; ++i) {
		if (Effect[i] != Other.Effect[i])
			return false;
		if (Effect[i] != EFF_NONE && Param[i] != Other.Param[i])
			return false;
	}
	return true;
}

// Names

std::string PitchName(int Pitch)
{
	if (Pitch < 0 || Pitch >= PITCHES)
		return "?";
	char Name[8];
	std::snprintf(Name, sizeof(Name), "%s%d", SHARP_NAMES[Pitch % 12], Pitch / 12);
	return Name;
}

std::string NoisePitchName(int Pitch)
{
	if (Pitch < 0 || Pitch >= NOISE_PITCHES)
		return "?";
	char Name[8];
	std::snprintf(Name, sizeof(Name), "%X-#", Pitch);
	return Name;
}

std::string Lowercase(const std::string &Text)
{
	std::string Out = Text;
	for (size_t i = 0; i < Out.size(); ++i)
		Out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(Out[i])));
	return Out;
}

bool ParsePitch(const std::string &Input, int &Pitch)
{
	const std::string Text = Trimmed(Input);
	int Class;
	size_t Used;
	if (!ParseNoteClass(Text, Class, Used))
		return false;
	size_t Pos = Used;
	if (Pos < Text.size() && Text[Pos] == '-')
		++Pos;
	if (Pos >= Text.size() || Text.size() - Pos > 1 || !std::isdigit(static_cast<unsigned char>(Text[Pos])))
		return false;
	const int Octave = Text[Pos] - '0';
	const int Value = Octave * 12 + Class;
	if (Value < 0 || Value >= PITCHES)
		return false;
	Pitch = Value;
	return true;
}

bool ParseNoisePitch(const std::string &Input, int &Pitch, std::string &Drum)
{
	std::string Text = Lowercase(Trimmed(Input));
	Drum.clear();
	if (Text == "kick" || Text == "bass drum" || Text == "kick drum") {
		Pitch = 0x4;
		Drum = "kick";
		return true;
	}
	if (Text == "snare" || Text == "snare drum" || Text == "clap") {
		Pitch = 0xA;
		Drum = "snare";
		return true;
	}
	if (Text == "hihat" || Text == "hi-hat" || Text == "hat" || Text == "closed hi-hat" || Text == "closed hihat") {
		Pitch = 0xE;
		Drum = "hi-hat";
		return true;
	}
	if (Text == "open hi-hat" || Text == "open hihat" || Text == "open hat") {
		Pitch = 0xD;
		Drum = "open hi-hat";
		return true;
	}
	if (Text == "crash" || Text == "cymbal" || Text == "crash cymbal") {
		Pitch = 0xC;
		Drum = "crash";
		return true;
	}
	if (Text == "tom" || Text == "high tom" || Text == "hi tom" || Text == "mid tom" || Text == "middle tom" || Text == "low tom" ||
		Text == "floor tom") {
		Pitch = Text.compare(0, 3, "low") == 0 || Text.compare(0, 5, "floor") == 0 ? 0x5 : Text.compare(0, 3, "mid") == 0 ? 0x7 : 0x9;
		Drum = "tom";
		return true;
	}
	if (Text.compare(0, 5, "noise") == 0)
		Text = Trimmed(Text.substr(5));
	if (Text.size() == 3 && Text[1] == '-' && Text[2] == '#')
		Text = Text.substr(0, 1);
	if (Text.empty() || Text.size() > 2)
		return false;
	char *pEnd = 0;
	const long Value = (Text.size() == 1) ? std::strtol(Text.c_str(), &pEnd, 16) : std::strtol(Text.c_str(), &pEnd, 10);
	if (pEnd == 0 || *pEnd != 0 || Value < 0 || Value >= NOISE_PITCHES)
		return false;
	Pitch = static_cast<int>(Value);
	return true;
}

char EffectLetter(int Effect)
{
	return (Effect > EFF_NONE && Effect < EFF_COUNT) ? EFFECT_LETTERS[Effect - 1] : 0;
}

std::string EffectText(int Effect, int Param)
{
	const char Letter = EffectLetter(Effect);
	if (Letter == 0)
		return std::string();
	char Text[8];
	std::snprintf(Text, sizeof(Text), "%c%02X", Letter, Param & 0xFF);
	return Text;
}

bool ParseEffect(const std::string &Input, int &Effect, int &Param)
{
	std::string Text;
	for (size_t i = 0; i < Input.size(); ++i)
		if (!std::isspace(static_cast<unsigned char>(Input[i])))
			Text += Input[i];
	if (Text.size() != 3)
		return false;
	const char Letter = static_cast<char>(std::toupper(static_cast<unsigned char>(Text[0])));
	Effect = EFF_NONE;
	for (int e = 1; e < EFF_COUNT; ++e) {
		if (EFFECT_LETTERS[e - 1] == Letter) {
			Effect = e;
			break;
		}
	}
	if (Effect == EFF_NONE)
		return false;
	char *pEnd = 0;
	const std::string Hex = Text.substr(1);
	const long Value = std::strtol(Hex.c_str(), &pEnd, 16);
	if (pEnd == 0 || *pEnd != 0 || !std::isxdigit(static_cast<unsigned char>(Hex[0])))
		return false;
	Param = static_cast<int>(Value);
	return true;
}

std::string FormatNumber(double Value)
{
	char Text[32];
	std::snprintf(Text, sizeof(Text), "%.3f", Value);
	std::string Out = Text;
	while (!Out.empty() && Out[Out.size() - 1] == '0')
		Out.erase(Out.size() - 1);
	if (!Out.empty() && Out[Out.size() - 1] == '.')
		Out.erase(Out.size() - 1);
	if (Out == "-0")
		Out = "0";
	return Out;
}

bool IsStickyEffect(int Effect)
{
	switch (Effect) {
	case EFF_ARPEGGIO:
	case EFF_PORTA_UP:
	case EFF_PORTA_DOWN:
	case EFF_PORTAMENTO:
	case EFF_VIBRATO:
	case EFF_TREMOLO:
	case EFF_VOLUME_SLIDE:
		return true;
	}
	return false;
}

bool IsSongEffect(int Effect)
{
	return Effect == EFF_SPEED || Effect == EFF_JUMP || Effect == EFF_SKIP || Effect == EFF_HALT;
}

// Bars and beats

Timing::Timing(int Length, int FrameCount, int FirstHighlight, int SecondHighlight) :
	PatternLength(std::max(1, Length)), Frames(std::max(1, FrameCount)), RowsPerBeat(FirstHighlight), RowsPerBar(SecondHighlight)
{
	if (RowsPerBar <= 0 || RowsPerBar > 256)
		RowsPerBar = 16;
	if (RowsPerBeat <= 0 || RowsPerBeat > RowsPerBar)
		RowsPerBeat = std::max(1, RowsPerBar / 4);
}

int Timing::BarsPerFrame() const
{
	return (PatternLength + RowsPerBar - 1) / RowsPerBar;
}

int Timing::BarCount() const
{
	return Frames * BarsPerFrame();
}

int Timing::BarStart(int Bar) const
{
	const int Index = std::max(0, Bar - 1);
	return Index / BarsPerFrame() * PatternLength + Index % BarsPerFrame() * RowsPerBar;
}

int Timing::BarLength(int Bar) const
{
	const int Index = std::max(0, Bar - 1);
	const int InFrame = Index % BarsPerFrame() * RowsPerBar;
	return std::min(RowsPerBar, PatternLength - InFrame);
}

int Timing::BarOf(int Row) const
{
	Row = std::max(0, Row);
	return Row / PatternLength * BarsPerFrame() + Row % PatternLength / RowsPerBar + 1;
}

double Timing::BeatOf(int Row) const
{
	return 1.0 + double(Row - BarStart(BarOf(Row))) / RowsPerBeat;
}

int Timing::BeatsPerBar(int Bar) const
{
	return std::max(1, BarLength(Bar) / RowsPerBeat);
}

bool Timing::PositionToRow(int Bar, double Beat, int &Row, bool &Exact) const
{
	if (Bar < 1 || !(Beat >= 1.0))
		return false;
	const double Offset = (Beat - 1.0) * RowsPerBeat;
	const long Rounded = std::lround(Offset);
	if (Rounded < 0 || Rounded >= BarLength(Bar))
		return false;
	Exact = std::fabs(Offset - Rounded) < 1e-6;
	Row = BarStart(Bar) + static_cast<int>(Rounded);
	return true;
}

int Timing::BeatsToRows(double Beats, bool &Exact) const
{
	const double Rows = Beats * RowsPerBeat;
	const long Rounded = std::lround(Rows);
	Exact = std::fabs(Rows - Rounded) < 1e-6 && Rounded >= 1;
	return static_cast<int>(std::max(1L, Rounded));
}

std::string Timing::Position(int Row) const
{
	char Text[64];
	std::snprintf(Text, sizeof(Text), "bar %d beat %s", BarOf(Row), FormatNumber(BeatOf(Row)).c_str());
	return Text;
}

std::string Timing::Length(int Rows) const
{
	const double Beats = double(Rows) / RowsPerBeat;
	return FormatNumber(Beats) + (std::fabs(Beats - 1.0) < 1e-9 ? " beat" : " beats");
}

// Tempo

double RowsPerMinute(int Speed, int Tempo, int FrameRate)
{
	if (Tempo <= 0 || Speed <= 0 || FrameRate <= 0)
		return 0.0;
	const int Decrement = (Tempo * 24) / Speed;
	const int Remainder = (Tempo * 24) % Speed;
	return 60.0 * FrameRate * Decrement / (60.0 * FrameRate - Remainder);
}

double BeatsPerMinute(int Speed, int Tempo, int FrameRate, int RowsPerBeat)
{
	return RowsPerMinute(Speed, Tempo, FrameRate) / std::max(1, RowsPerBeat);
}

bool EvenRows(int Speed, int Tempo, int FrameRate)
{
	// CSoundGen adds 60 * FrameRate - Remainder at each row and takes
	// Decrement off each tick: when that comes out even, every row does
	if (Tempo <= 0 || Speed <= 0 || FrameRate <= 0)
		return false;
	const int Decrement = (Tempo * 24) / Speed;
	const int Remainder = (Tempo * 24) % Speed;
	return Decrement > 0 && (60 * FrameRate - Remainder) % Decrement == 0;
}

int DefaultTempo(int FrameRate)
{
	return FrameRate * 5 / 2;
}

void ChooseSpeedTempo(double Bpm, int RowsPerBeat, int FrameRate, int CurrentSpeed, int SplitPoint, int &Speed, int &Tempo,
	int FixedSpeed, int FixedTempo)
{
	const int MaxSpeed = std::max(1, std::min(31, SplitPoint - 1));
	const int MinTempo = std::max(32, SplitPoint);
	const int FirstSpeed = FixedSpeed > 0 ? std::min(FixedSpeed, MaxSpeed) : 1;
	const int LastSpeed = FixedSpeed > 0 ? FirstSpeed : MaxSpeed;
	const int FirstTempo = FixedTempo > 0 ? std::max(MinTempo, std::min(255, FixedTempo)) : MinTempo;
	const int LastTempo = FixedTempo > 0 ? FirstTempo : 255;
	const double Close = std::max(0.5, Bpm * 0.01);
	const int Default = DefaultTempo(FrameRate);

	// Lower is better, compared in this order
	struct Score {
		bool Far;			// not within 1%
		bool Uneven, OtherTempo, OtherSpeed;	// within 1% only
		double Error;
		bool Slower;		// a tie rounds up
		int Distance;		// from the default tempo
	};
	const auto Better = [](const Score &a, const Score &b) {
		if (a.Far != b.Far)
			return !a.Far;
		if (!a.Far && a.Uneven != b.Uneven)
			return !a.Uneven;
		if (!a.Far && a.OtherTempo != b.OtherTempo)
			return !a.OtherTempo;
		if (!a.Far && a.OtherSpeed != b.OtherSpeed)
			return !a.OtherSpeed;
		if (std::fabs(a.Error - b.Error) > 1e-9)
			return a.Error < b.Error;
		if (a.Slower != b.Slower)
			return !a.Slower;
		return a.Distance < b.Distance;
	};
	bool Found = false;
	Score Best = Score();
	Speed = FirstSpeed;
	Tempo = FirstTempo;
	for (int s = FirstSpeed; s <= LastSpeed; ++s) {
		for (int t = FirstTempo; t <= LastTempo; ++t) {
			const double Played = BeatsPerMinute(s, t, FrameRate, RowsPerBeat);
			Score Here;
			Here.Error = std::fabs(Played - Bpm);
			Here.Far = Here.Error > Close;
			Here.Uneven = !EvenRows(s, t, FrameRate);
			Here.OtherTempo = t != Default;
			Here.OtherSpeed = s != CurrentSpeed;
			Here.Slower = Played < Bpm;
			Here.Distance = std::abs(t - Default);
			if (!Found || Better(Here, Best)) {
				Found = true;
				Best = Here;
				Speed = s;
				Tempo = t;
			}
		}
	}
}

// Keys

int SongFlow::LoopRow(int PatternLength) const
{
	if (Halts() || LoopStep >= static_cast<int>(Steps.size()))
		return -1;
	return Steps[LoopStep].Frame * PatternLength + Steps[LoopStep].FirstRow;
}

int SongFlow::EndRow(int PatternLength) const
{
	if (Steps.empty())
		return 0;
	return Steps.back().Frame * PatternLength + Steps.back().EndRow;
}

int SongFlow::PlayedRows() const
{
	int Rows = 0;
	for (size_t i = 0; i < Steps.size(); ++i)
		Rows += Steps[i].EndRow - Steps[i].FirstRow;
	return Rows;
}

int SongFlow::LoopRows() const
{
	if (Halts())
		return 0;
	int Rows = 0;
	for (size_t i = static_cast<size_t>(LoopStep); i < Steps.size(); ++i)
		Rows += Steps[i].EndRow - Steps[i].FirstRow;
	return Rows;
}

std::vector<bool> SongFlow::Played(int TotalRows, int PatternLength) const
{
	std::vector<bool> Out(static_cast<size_t>(std::max(0, TotalRows)), false);
	for (size_t i = 0; i < Steps.size(); ++i)
		for (int r = Steps[i].FirstRow; r < Steps[i].EndRow; ++r) {
			const int Row = Steps[i].Frame * PatternLength + r;
			if (Row >= 0 && Row < TotalRows)
				Out[static_cast<size_t>(Row)] = true;
		}
	return Out;
}

SongFlow FollowSong(const std::vector<std::vector<Cell> > &Channels, const std::vector<int> &EffectColumns, int PatternLength,
	int Frames)
{
	SongFlow Flow;
	if (PatternLength < 1 || Frames < 1)
		return Flow;
	std::vector<int> Started(static_cast<size_t>(Frames) * static_cast<size_t>(PatternLength), -1);	// step that started there
	int Frame = 0, Row = 0;
	for (;;) {
		const size_t Start = static_cast<size_t>(Frame) * static_cast<size_t>(PatternLength) + static_cast<size_t>(Row);
		if (Started[Start] >= 0) {
			Flow.LoopStep = Started[Start];
			return Flow;
		}
		Started[Start] = static_cast<int>(Flow.Steps.size());
		FlowStep Step;
		Step.Frame = Frame;
		Step.FirstRow = Row;
		Step.EndRow = PatternLength;
		int NextFrame = Frame + 1 < Frames ? Frame + 1 : 0, NextRow = 0;
		for (int r = Row; r < PatternLength; ++r) {
			int Jump = -1, Skip = -1;
			bool Halt = false;
			const size_t SongRow = static_cast<size_t>(Frame) * static_cast<size_t>(PatternLength) + static_cast<size_t>(r);
			for (size_t c = 0; c < Channels.size(); ++c) {
				if (SongRow >= Channels[c].size())
					continue;
				const Cell &C = Channels[c][SongRow];
				const int Columns = c < EffectColumns.size() ? std::max(1, std::min(EFFECT_COLUMNS, EffectColumns[c])) : 1;
				for (int k = 0; k < Columns; ++k) {
					// The last one read wins, as in CSoundGen::EvaluateGlobalEffects
					if (C.Effect[k] == EFF_JUMP)
						Jump = C.Param[k];
					else if (C.Effect[k] == EFF_SKIP)
						Skip = C.Param[k];
					else if (C.Effect[k] == EFF_HALT)
						Halt = true;
				}
			}
			if (Halt) {
				Step.EndRow = r + 1;
				Flow.Steps.push_back(Step);
				Flow.LoopStep = -1;
				return Flow;
			}
			if (Jump >= 0) {	// a jump goes before a skip
				Step.EndRow = r + 1;
				NextFrame = std::min(Jump, Frames - 1);
				NextRow = 0;
				break;
			}
			if (Skip >= 0) {
				Step.EndRow = r + 1;
				NextRow = std::min(Skip, PatternLength - 1);
				break;
			}
		}
		Flow.Steps.push_back(Step);
		Frame = NextFrame;
		Row = NextRow;
	}
}

std::vector<TimedRow> TimeRows(const std::vector<std::vector<Cell> > &Channels, const std::vector<int> &EffectColumns,
	const SongFlow &Flow, int PatternLength, int Speed, int Tempo, int SplitPoint, int FrameRate, int Passes, int &EndTick)
{
	std::vector<TimedRow> Out;
	int Accumulator = 0, Tick = 0;
	int Decrement = 0, Remainder = 0;
	auto Setup = [&]() {
		Speed = std::max(1, Speed);
		Decrement = (Tempo * 24) / Speed;
		Remainder = (Tempo * 24) % Speed;
	};
	Setup();
	const int Steps = static_cast<int>(Flow.Steps.size());
	const int Loops = Flow.Halts() ? 1 : std::max(1, Passes);
	const int LoopStep = Flow.Halts() ? Steps : Flow.LoopStep;
	for (int Pass = 0; Pass < Loops; ++Pass) {
		for (int s = Pass == 0 ? 0 : LoopStep; s < Steps; ++s) {
			const FlowStep &Step = Flow.Steps[static_cast<size_t>(s)];
			for (int r = Step.FirstRow; r < Step.EndRow; ++r) {
				const int Row = Step.Frame * PatternLength + r;
				TimedRow Timed;
				Timed.Row = Row;
				Timed.Tick = Tick;
				Timed.Pass = s < LoopStep ? 0 : Pass;
				Timed.LoopStart = s == LoopStep && r == Step.FirstRow;
				Out.push_back(Timed);
				// Speed and tempo commands take effect on their row
				for (size_t c = 0; c < Channels.size(); ++c) {
					if (Row >= static_cast<int>(Channels[c].size()))
						continue;
					const Cell &C = Channels[c][static_cast<size_t>(Row)];
					const int Columns = c < EffectColumns.size() ? std::max(1, std::min(EFFECT_COLUMNS, EffectColumns[c])) : 1;
					for (int k = 0; k < Columns; ++k)
						if (C.Effect[k] == EFF_SPEED) {
							const int Value = std::max(1, static_cast<int>(C.Param[k]));
							if (Value >= SplitPoint)
								Tempo = Value;
							else
								Speed = Value;
							Setup();
						}
				}
				// The ticks until the next row
				do {
					if (Accumulator <= 0)
						Accumulator += 60 * FrameRate - Remainder;
					Accumulator -= Decrement;
					++Tick;
				} while (Accumulator > 0 && Decrement > 0);
			}
		}
	}
	EndTick = Tick;
	return Out;
}

int ModeCount()
{
	return MODE_COUNT;
}

const char *ModeName(int Mode)
{
	return (Mode >= 0 && Mode < MODE_COUNT) ? MODES[Mode].Name : "major";
}

const int *ModeSteps(int Mode)
{
	return MODES[(Mode >= 0 && Mode < MODE_COUNT) ? Mode : 0].Steps;
}

bool ParseMode(const std::string &Input, int &Mode)
{
	const std::string Text = Lowercase(Trimmed(Input));
	if (Text.empty() || Text == "major" || Text == "maj" || Text == "ionian" || Text == "dur") {
		Mode = MODE_MAJOR;
		return true;
	}
	if (Text == "minor" || Text == "min" || Text == "m" || Text == "aeolian" || Text == "natural minor" || Text == "moll") {
		Mode = MODE_MINOR;
		return true;
	}
	for (int m = 0; m < MODE_COUNT; ++m) {
		if (Text == MODES[m].Name) {
			Mode = m;
			return true;
		}
	}
	return false;
}

bool ParseKey(const std::string &Input, Key &Out)
{
	const std::string Text = Trimmed(Input);
	int Class;
	size_t Used;
	if (!ParseNoteClass(Text, Class, Used))
		return false;
	std::string Rest = Trimmed(Text.substr(Used));
	const std::string Lower = Lowercase(Rest);
	if (Lower.compare(0, 5, "sharp") == 0) {
		++Class;
		Rest = Trimmed(Rest.substr(5));
	}
	else if (Lower.compare(0, 4, "flat") == 0) {
		--Class;
		Rest = Trimmed(Rest.substr(4));
	}
	int Mode;
	if (!ParseMode(Rest, Mode))
		return false;
	Out.Tonic = Mod12(Class);
	Out.Mode = Mode;
	return true;
}

std::string KeyName(const Key &K)
{
	return NoteClassName(K.Tonic, K) + " " + ModeName(K.Mode);
}

std::string NoteClassName(int PitchClass, const Key &K)
{
	return (UsesFlats(K) ? FLAT_CLASSES : SHARP_CLASSES)[Mod12(PitchClass)];
}

bool InKey(int Pitch, const Key &K)
{
	const int Relative = Mod12(Pitch - K.Tonic);
	const int *pSteps = ModeSteps(K.Mode);
	for (int d = 0; d < 7; ++d)
		if (pSteps[d] == Relative)
			return true;
	return K.Mode == MODE_MINOR && Relative == 11;	// the raised 7th of harmonic minor
}

int MapToKey(int Pitch, const Key &From, const Key &To)
{
	const int Relative = Mod12(Pitch - From.Tonic);
	const int Base = Pitch - Relative;
	const int *pFrom = ModeSteps(From.Mode), *pTo = ModeSteps(To.Mode);
	int Degree = 0;
	for (int d = 0; d < 7; ++d)
		if (pFrom[d] <= Relative)
			Degree = d;
	const int Offset = Relative - pFrom[Degree];	// above the degree: a note between two degrees
	int NewRelative = pTo[Degree] + Offset;
	const int Ceiling = Degree < 6 ? pTo[Degree + 1] : 12;
	if (Offset > 0 && NewRelative >= Ceiling)
		NewRelative = Ceiling - 1;
	int Move = Mod12(To.Tonic - From.Tonic);
	if (Move > 6)
		Move -= 12;
	return Base + NewRelative + Move;
}

// Reading notes

std::vector<NoteInfo> ReadNotes(const std::vector<Cell> &Cells, int EffectColumns)
{
	const int Columns = std::max(1, std::min(EFFECT_COLUMNS, EffectColumns));
	std::vector<NoteInfo> Notes;
	int Instrument = NO_INSTRUMENT, Volume = 15;
	bool Open = false;
	for (size_t r = 0; r < Cells.size(); ++r) {
		const Cell &C = Cells[r];
		const int Row = static_cast<int>(r);
		if (C.Volume < NO_VOLUME)
			Volume = C.Volume;
		if (C.Instrument != NO_INSTRUMENT && !C.IsEnd())
			Instrument = C.Instrument;
		if (C.HasNote()) {
			if (Open)
				Notes.back().Rows = Row - Notes.back().Row;
			NoteInfo Note;
			Note.Row = Row;
			Note.Rows = 1;
			Note.Pitch = C.Pitch();
			Note.Instrument = Instrument;
			Note.Volume = Volume;
			Note.InstrumentSet = C.Instrument != NO_INSTRUMENT;
			Note.VolumeSet = C.Volume < NO_VOLUME;
			Note.EndNote = NOTE_NONE;
			Notes.push_back(Note);
			Open = true;
		}
		else if (C.IsEnd()) {
			if (Open) {
				Notes.back().Rows = Row - Notes.back().Row;
				Notes.back().EndNote = C.Note;
			}
			Open = false;
		}
		if (Open) {
			for (int k = 0; k < Columns; ++k)
				if (C.Effect[k] != EFF_NONE && !IsSongEffect(C.Effect[k]))
					Notes.back().Effects.push_back(std::make_pair(Row - Notes.back().Row, std::make_pair(int(C.Effect[k]), int(C.Param[k]))));
		}
	}
	if (Open)
		Notes.back().Rows = static_cast<int>(Cells.size()) - Notes.back().Row;
	return Notes;
}

ChannelState::ChannelState() : Instrument(NO_INSTRUMENT), Volume(15), Sounding(false), SoundingInstrument(NO_INSTRUMENT)
{
	std::memset(Sticky, 0, sizeof(Sticky));
}

ChannelState StateAt(const std::vector<Cell> &Cells, int Row, int EffectColumns)
{
	const int Columns = std::max(1, std::min(EFFECT_COLUMNS, EffectColumns));
	ChannelState State;
	const int End = std::min(Row, static_cast<int>(Cells.size()));
	for (int r = 0; r < End; ++r) {
		const Cell &C = Cells[r];
		if (C.Volume < NO_VOLUME)
			State.Volume = C.Volume;
		if (C.Instrument != NO_INSTRUMENT && !C.IsEnd())
			State.Instrument = C.Instrument;
		for (int k = 0; k < Columns; ++k) {
			if (IsStickyEffect(C.Effect[k]))
				State.Sticky[C.Effect[k]] = C.Param[k];
			else if (C.Effect[k] == EFF_NOTE_CUT)
				State.Sounding = false;
		}
		if (C.HasNote()) {
			State.Sounding = true;
			State.SoundingInstrument = State.Instrument;
		}
		else if (C.IsEnd())
			State.Sounding = false;
	}
	return State;
}

bool KeepFollowing(const std::vector<Cell> &Old, int First, int End, std::vector<Cell> &Out, int EffectColumns)
{
	const int Columns = std::max(1, std::min(EFFECT_COLUMNS, EffectColumns));
	const int SongRows = static_cast<int>(Old.size());
	int Next = -1;
	for (int r = std::max(0, End); r < SongRows; ++r) {
		if (Old[r].HasNote()) {
			Next = r;
			break;
		}
	}
	if (Next < 0 || Next < First)
		return true;
	std::vector<Cell> New(Old);
	for (size_t i = 0; i < Out.size() && First + i < New.size(); ++i)
		New[First + i] = Out[i];
	if (First + static_cast<int>(Out.size()) > Next)
		return true;	// the note was rewritten itself
	const ChannelState OldState = StateAt(Old, Next, Columns);
	const ChannelState NewState = StateAt(New, Next, Columns);
	const bool Instrument = OldState.Instrument != NewState.Instrument && OldState.Instrument != NO_INSTRUMENT &&
		Old[Next].Instrument == NO_INSTRUMENT;
	const bool Volume = OldState.Volume != NewState.Volume && Old[Next].Volume == NO_VOLUME;
	bool Effects = false;
	for (int e = 0; e < EFF_COUNT; ++e)
		if (OldState.Sticky[e] != NewState.Sticky[e] && !HasEffect(Old[Next], e, Columns))
			Effects = true;
	if (!Instrument && !Volume && !Effects)
		return true;
	while (First + static_cast<int>(Out.size()) <= Next)
		Out.push_back(Old[First + Out.size()]);
	Cell &C = Out[Next - First];
	if (Instrument)
		C.Instrument = static_cast<unsigned char>(OldState.Instrument);
	if (Volume)
		C.Volume = static_cast<unsigned char>(OldState.Volume);
	bool Fits = true;
	for (int e = 0; e < EFF_COUNT; ++e) {
		if (OldState.Sticky[e] != NewState.Sticky[e] && !HasEffect(C, e, Columns) && !AddEffect(C, e, OldState.Sticky[e], Columns))
			Fits = false;
	}
	return Fits;
}

bool AddEffect(Cell &Target, int Effect, int Param, int EffectColumns)
{
	const int Columns = std::max(1, std::min(EFFECT_COLUMNS, EffectColumns));
	for (int k = 0; k < Columns; ++k) {
		if (Target.Effect[k] == Effect) {
			Target.Param[k] = static_cast<unsigned char>(Param);
			return true;
		}
	}
	for (int k = 0; k < Columns; ++k) {
		if (Target.Effect[k] == EFF_NONE) {
			Target.Effect[k] = static_cast<unsigned char>(Effect);
			Target.Param[k] = static_cast<unsigned char>(Param);
			return true;
		}
	}
	return false;
}

bool KeepSongEffects(const Cell &Old, Cell &New, int EffectColumns)
{
	const int Columns = std::max(1, std::min(EFFECT_COLUMNS, EffectColumns));
	bool Keep[EFFECT_COLUMNS];
	for (int k = 0; k < EFFECT_COLUMNS; ++k) {
		Keep[k] = IsSongEffect(Old.Effect[k]);
		for (int j = 0; j < EFFECT_COLUMNS && Keep[k]; ++j) {
			const bool InNew = New.Effect[j] == Old.Effect[k] && New.Param[j] == Old.Param[k];
			const bool Twice = j < k && Keep[j] && Old.Effect[j] == Old.Effect[k] && Old.Param[j] == Old.Param[k];
			if (InNew || Twice)
				Keep[k] = false;
		}
	}
	bool Fits = true;
	for (int k = 0; k < EFFECT_COLUMNS; ++k) {
		if (!Keep[k])
			continue;
		if (New.Effect[k] != EFF_NONE) {
			int Free = -1;
			for (int j = 0; j < Columns && Free < 0; ++j)
				if (!Keep[j] && New.Effect[j] == EFF_NONE)
					Free = j;
			if (Free >= 0) {
				New.Effect[Free] = New.Effect[k];
				New.Param[Free] = New.Param[k];
			}
			else
				Fits = false;
		}
		New.Effect[k] = Old.Effect[k];
		New.Param[k] = Old.Param[k];
	}
	return Fits;
}

// Writing notes

void BuildPart(const std::vector<Cell> &Old, int First, int End, const std::vector<PartNote> &Notes,
	const PartOptions &Options, std::vector<Cell> &Out, std::vector<std::string> &Warnings)
{
	const int Columns = std::max(1, std::min(EFFECT_COLUMNS, Options.EffectColumns));
	const int SongRows = static_cast<int>(Old.size());
	const Cell Empty;
	struct Local {
		static bool Releases(const PartOptions &O, int Instrument)
		{
			return Instrument >= 0 && Instrument < static_cast<int>(O.Releases.size()) && O.Releases[Instrument];
		}
	};
	Out.clear();
	if (End <= First)
		return;

	// The range as it is, emptied but for the song's speed, jumps and halts
	for (int r = First; r < End; ++r) {
		Cell C = r < SongRows ? Old[r] : Empty;
		C.Note = NOTE_NONE;
		C.Octave = 0;
		C.Instrument = NO_INSTRUMENT;
		C.Volume = NO_VOLUME;
		for (int k = 0; k < EFFECT_COLUMNS; ++k) {
			if (!IsSongEffect(C.Effect[k])) {
				C.Effect[k] = EFF_NONE;
				C.Param[k] = 0;
			}
		}
		Out.push_back(C);
	}

	const ChannelState Before = StateAt(Old, First, Columns);
	int Instrument = Before.Instrument;
	bool EffectsLost = false;

	for (size_t i = 0; i < Notes.size(); ++i) {
		const PartNote &N = Notes[i];
		if (N.Row < First || N.Row >= End)
			continue;
		Cell &C = Out[N.Row - First];
		C.SetPitch(N.Pitch);
		if (N.Instrument != NO_INSTRUMENT) {
			C.Instrument = static_cast<unsigned char>(N.Instrument);
			Instrument = N.Instrument;
		}
		if (N.Volume >= 0)
			C.Volume = static_cast<unsigned char>(std::min(15, N.Volume));
		for (size_t e = 0; e < N.Effects.size(); ++e)
			if (!AddEffect(C, N.Effects[e].first, N.Effects[e].second, Columns))
				EffectsLost = true;
		const int EndRow = std::min(End, N.Row + std::max(1, N.Rows));
		for (size_t e = 0; e < N.LaterEffects.size(); ++e) {
			const int Row = N.Row + N.LaterEffects[e].first;
			if (Row >= N.Row && Row < EndRow && !AddEffect(Out[Row - First], N.LaterEffects[e].second.first,
				N.LaterEffects[e].second.second, Columns))
				EffectsLost = true;
		}

		// The end: the next note, or a release or halt
		if (i + 1 < Notes.size() && Notes[i + 1].Row == EndRow)
			continue;
		const unsigned char EndNote = Local::Releases(Options, Instrument) ? NOTE_RELEASE : NOTE_HALT;
		Cell *pEnd = 0;
		if (EndRow < End)
			pEnd = &Out[EndRow - First];
		else if (EndRow >= SongRows || Old[EndRow].HasNote() || Old[EndRow].IsEnd())
			continue;	// lasts to the end of the range, and the song ends or something follows anyway
		else if (Options.StopAtEnd) {
			if (EndRow - 1 > N.Row)
				pEnd = &Out[EndRow - 1 - First];
		}
		else {
			Cell After = Old[EndRow];
			After.Octave = 0;
			Out.push_back(After);
			pEnd = &Out.back();
		}
		if (pEnd != 0) {
			pEnd->Note = EndNote;
			for (size_t e = 0; e < N.EndEffects.size(); ++e)
				if (!AddEffect(*pEnd, N.EndEffects[e].first, N.EndEffects[e].second, Columns))
					EffectsLost = true;
		}
	}

	// Entering the range: a note still sounding stops, effects still on go off
	if (Before.Sounding && !Out[0].HasNote())
		Out[0].Note = Local::Releases(Options, Before.SoundingInstrument) ? NOTE_RELEASE : NOTE_HALT;
	for (int e = 0; e < EFF_COUNT; ++e) {
		if (Before.Sticky[e] != 0 && !HasEffect(Out[0], e, Columns) && !AddEffect(Out[0], e, 0, Columns))
			EffectsLost = true;
	}

	// The next note after the range plays as it did
	if (Options.KeepFollowing && !KeepFollowing(Old, First, End, Out, Columns))
		EffectsLost = true;

	if (EffectsLost)
		Warnings.push_back("Some effects didn't fit in the channel's visible effect columns and were left out.");
}

}	// namespace ftapi

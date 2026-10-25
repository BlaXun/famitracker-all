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

// Chords typed or strummed for Edit > Add chords and bass line (see
// ChordInput.h): read with the tools' chord names, given back as the
// transcription engine's chord spans.

#include "ChordInput.h"
#include "Chords.h"
#include "Music.h"

#include <algorithm>
#include <cmath>

namespace ftapi {

namespace {

Key ToolKey(const transcribe::Key &K)
{
	return Key(((K.tonic % 12) + 12) % 12, K.minor ? MODE_MINOR : MODE_MAJOR);
}

std::vector<BarSpan> Spans(const std::vector<transcribe::Bar> &Bars)
{
	std::vector<BarSpan> Out;
	for (size_t i = 0; i < Bars.size(); ++i)
		Out.push_back(BarSpan(Bars[i].row, Bars[i].length));
	return Out;
}

transcribe::ChordSpan EngineChord(const ChordAt &A)
{
	transcribe::ChordSpan Span;
	Span.row = A.Row;
	Span.length = A.Rows;
	Span.chord.root = ((A.C.Root % 12) + 12) % 12;
	Span.chord.tones = A.C.Tones;
	if (A.C.IsMinor())
		Span.chord.quality = A.C.Fifth() == 6 && !A.C.Has(10) && !A.C.Has(9) ? transcribe::Chord::Diminished : transcribe::Chord::Minor;
	else
		Span.chord.quality = transcribe::Chord::Major;
	if (A.C.Tones == std::vector<int>({ 0, 4, 7 }) || A.C.Tones == std::vector<int>({ 0, 3, 7 }) ||
		A.C.Tones == std::vector<int>({ 0, 3, 6 }))
		Span.chord.tones.clear();	// a plain triad: its quality says it
	const int Bass = ((A.C.Bass % 12) + 12) % 12;
	Span.chord.bass = Bass != Span.chord.root ? Bass : -1;
	return Span;
}

// The key chords are in: their notes against the key profiles (transcribe::FindKey), the first chord's root
// counting as the first note and the last one, so it is the likeliest key note
transcribe::Key KeyOfChords(const std::vector<ChordAt> &Chords)
{
	std::vector<transcribe::MelodyNote> Notes;
	if (Chords.empty())
		return transcribe::Key();
	int Last = 0;
	for (size_t c = 0; c < Chords.size(); ++c) {
		const std::vector<int> Classes = Chords[c].C.PitchClasses();
		for (size_t k = 0; k < Classes.size(); ++k) {
			transcribe::MelodyNote N;
			N.row = Chords[c].Row;
			N.length = std::max(1, Chords[c].Rows);
			N.midi = 60 + Classes[k];
			N.weight = 1.0;
			Notes.push_back(N);
		}
		Last = std::max(Last, Chords[c].Row + Chords[c].Rows);
	}
	transcribe::MelodyNote Home;
	Home.midi = 60 + ((Chords.front().C.Root % 12) + 12) % 12;
	Home.length = 1;
	Home.weight = 0.001;
	Home.row = Chords.front().Row - 1;
	Notes.insert(Notes.begin(), Home);
	Home.row = Last + 1;
	Notes.push_back(Home);
	return transcribe::FindKey(Notes);
}

}	// namespace

int TypedChordBars(const std::string &Text)
{
	return static_cast<int>(SplitProgression(Text).size());
}

bool LayOutTypedChords(const std::string &Text, const std::vector<transcribe::Bar> &Bars, int RowsPerBeat,
	const transcribe::Key *pKey, transcribe::Key &KeyOut, std::vector<transcribe::ChordSpan> &Chords, std::string &Names,
	std::string &Error)
{
	Chords.clear();
	Names.clear();
	const std::vector<std::string> Written = SplitProgression(Text);
	if (Written.empty()) {
		Error = "Type the chords, like \"C G Am F\" (a chord a bar) or \"C | G | Am F\".";
		return false;
	}
	if (Bars.empty()) {
		Error = "There are no bars for the chords.";
		return false;
	}
	const std::vector<BarSpan> BarSpans = Spans(Bars);
	const Key Reading = pKey != NULL ? ToolKey(*pKey) : Key(0, MODE_MAJOR);
	std::vector<ChordAt> Laid;
	if (!LayOutProgression(Written, BarSpans, RowsPerBeat, Reading, Laid, Error))
		return false;
	KeyOut = pKey != NULL ? *pKey : KeyOfChords(Laid);
	for (size_t i = 0; i < Laid.size(); ++i)
		Chords.push_back(EngineChord(Laid[i]));
	Names = ProgressionText(Laid, BarSpans, ToolKey(KeyOut), false, 1000);
	return true;
}

bool HearStrummedChords(const std::vector<transcribe::Strum> &Strums, double Start, int StartRow, double RowsPerSecond,
	int PatternLength, int RowsPerBar, int RowsPerBeat, StrummedChords &Out, std::string &Error)
{
	Out = StrummedChords();
	if (RowsPerSecond <= 0.0 || PatternLength < 1 || RowsPerBar < 1) {
		Error = "The song's tempo or bars can't be read.";
		return false;
	}
	const int Beat = std::max(1, RowsPerBeat);
	double Loudest = -1000.0;
	for (size_t i = 0; i < Strums.size(); ++i)
		Loudest = std::max(Loudest, Strums[i].level);

	// Each strum's notes on the rows it sounds (from just before the start row on: earlier ones were the count-in)
	std::vector<HeardNote> Heard;
	std::vector<transcribe::MelodyNote> ForKey;
	int First = -1, Last = -1;
	for (size_t i = 0; i < Strums.size(); ++i) {
		const transcribe::Strum &S = Strums[i];
		int Row = StartRow + static_cast<int>(std::floor((S.time - Start) * RowsPerSecond + 0.5));
		if (Row < StartRow - Beat / 2)
			continue;
		Row = std::max(Row, StartRow);
		const double Until = std::min(S.end, S.time + RowsPerBar / RowsPerSecond);
		const int Rows = std::max(1, static_cast<int>(std::floor((Until - S.time) * RowsPerSecond + 0.5)));
		const double Loudness = std::max(0.2, std::pow(10.0, (S.level - Loudest) / 20.0));
		int Lowest = -1;
		for (size_t k = 0; k < S.notes.size() && Lowest < 0; ++k)
			if (S.notes[k].weight >= 0.25)
				Lowest = static_cast<int>(k);
		for (size_t k = 0; k < S.notes.size(); ++k) {
			HeardNote N;
			N.Row = Row;
			N.Rows = Rows;
			N.PitchClass = ((S.notes[k].midi % 12) + 12) % 12;
			N.Weight = S.notes[k].weight * Loudness;
			N.Bass = static_cast<int>(k) == Lowest;
			Heard.push_back(N);
			transcribe::MelodyNote M;
			M.row = Row;
			M.length = Rows;
			M.midi = S.notes[k].midi;
			M.weight = std::min(1.0, N.Weight);
			ForKey.push_back(M);
		}
		if (First < 0)
			First = Row;
		Last = Row;
	}
	if (Heard.empty()) {
		Error = "No chords were heard. Strum a little louder or nearer the microphone, a chord or two a bar.";
		return false;
	}

	// The bars from the first strum's to the last one's, and the chords heard in them
	const std::vector<transcribe::Bar> Bars = transcribe::SongBars(First, Last + 1, PatternLength, RowsPerBar);
	const std::vector<BarSpan> BarSpans = Spans(Bars);
	Out.Key = transcribe::FindKey(ForKey);
	const std::vector<ChordAt> Chords = HearChords(Heard, BarSpans, Beat, ToolKey(Out.Key), true);
	if (Chords.empty() || Bars.empty()) {
		Error = "No chords could be told apart in the take. Strum each chord clearly, and let it ring.";
		return false;
	}
	// The key the chords are in, now that they're known
	Out.Key = KeyOfChords(Chords);
	Out.FirstRow = Bars.front().row;
	Out.BarCount = static_cast<int>(Bars.size());
	Out.Names = ProgressionText(Chords, BarSpans, ToolKey(Out.Key), false, 1000);
	return true;
}

}	// namespace ftapi

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

// Chords typed or strummed for Edit > Add chords and bass line, read with the
// tools' chord names and given back as the transcription engine's chord
// spans, and tapped tempos as speed and tempo (see RecorderInput.h).

#include "RecorderInput.h"
#include "Chords.h"
#include "Music.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

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

// The delay through a take of strums (rows), from how far past a beat each strum lies (Phases, 0 to Beat): where
// most of them agree, within a sixth of a beat, from a quarter of a beat early to three quarters late. A microphone
// delay not measured (or a slow device) puts every strum the same way late; when as many strums agree on two
// delays (strummed down and up, on the beats and between them), the smaller one, early ones counting double.
double TakeDelay(const std::vector<double> &Phases, int RowsPerBeat)
{
	const double Beat = std::max(1, RowsPerBeat);
	double Delay = 0.0, Cost = 0.0;
	size_t Most = 0;
	for (size_t i = 0; i < Phases.size(); ++i) {
		size_t Count = 0;
		double Sum = 0.0;
		for (size_t k = 0; k < Phases.size(); ++k) {
			double Apart = Phases[k] - Phases[i];
			Apart -= Beat * std::floor(Apart / Beat + 0.5);	// the shorter way round the beat
			if (std::fabs(Apart) <= Beat / 6.0) {
				++Count;
				Sum += Apart;
			}
		}
		double Mean = Phases[i] + Sum / double(Count);
		Mean -= Beat * std::floor((Mean + Beat / 4.0) / Beat);	// from a quarter of a beat early to three quarters late
		const double Weight = Mean >= 0.0 ? Mean : -2.0 * Mean;
		if (Count > Most || (Count == Most && Weight < Cost)) {
			Most = Count;
			Delay = Mean;
			Cost = Weight;
		}
	}
	return Delay;
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

	// The take's delay (TakeDelay), from the strums after the count-in
	std::vector<double> Phases;
	for (size_t i = 0; i < Strums.size(); ++i) {
		const double At = StartRow + (Strums[i].time - Start) * RowsPerSecond;
		if (At < StartRow - Beat / 2.0)
			continue;
		const double Pattern = std::floor(At / PatternLength) * PatternLength;
		Phases.push_back(std::fmod(At - Pattern, double(Beat)));
	}
	const double Delay = TakeDelay(Phases, Beat);

	// Where each strum lands, the delay taken out (from just before the start row on: earlier ones were the
	// count-in): on the beat when it's within a third of a beat of one, so a strum a little early or late doesn't
	// carry the chord before it into the bar
	std::vector<int> Landed(Strums.size(), -1);
	for (size_t i = 0; i < Strums.size(); ++i) {
		const double At = StartRow + (Strums[i].time - Start) * RowsPerSecond - Delay;
		int Row = static_cast<int>(std::floor(At + 0.5));
		const double Pattern = std::floor(At / PatternLength) * PatternLength;
		const double OnBeat = Pattern + std::floor((At - Pattern) / Beat + 0.5) * Beat;
		if (std::fabs(At - OnBeat) <= Beat / 3.0)
			Row = static_cast<int>(OnBeat);
		if (Row >= StartRow - Beat / 2)
			Landed[i] = std::max(Row, StartRow);
	}

	// Each strum's notes on the rows it sounds: until the next strum, at most a bar
	std::vector<HeardNote> Heard;
	std::vector<transcribe::MelodyNote> ForKey;
	int First = -1, Last = -1;
	for (size_t i = 0; i < Strums.size(); ++i) {
		const transcribe::Strum &S = Strums[i];
		const int Row = Landed[i];
		if (Row < 0)
			continue;
		int Rows = static_cast<int>(std::floor((std::min(S.end, S.time + RowsPerBar / RowsPerSecond) - S.time) * RowsPerSecond + 0.5));
		if (i + 1 < Strums.size() && Landed[i + 1] >= 0)
			Rows = Landed[i + 1] - Row;
		Rows = std::max(1, std::min(Rows, RowsPerBar));
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

bool TappedSpeedTempo(double Tapped, int RowsPerBeat, int FrameRate, int CurrentSpeed, int SplitPoint, int &Speed,
	int &Tempo, double &Bpm)
{
	if (!(Tapped > 0.0))
		return false;
	const int Beat = std::max(1, RowsPerBeat);
	// The nearest tempo with rows of even length, over every speed and tempo FamiTracker has
	const int MaxSpeed = std::max(1, std::min(31, SplitPoint - 1));
	const int MinTempo = std::max(32, SplitPoint);
	double Even = 0.0;
	for (int s = 1; s <= MaxSpeed; ++s)
		for (int t = MinTempo; t <= 255; ++t)
			if (EvenRows(s, t, FrameRate)) {
				const double Played = BeatsPerMinute(s, t, FrameRate, Beat);
				if (Even <= 0.0 || std::fabs(Played - Tapped) < std::fabs(Even - Tapped) - 1e-9)
					Even = Played;
			}
	const double Wanted = Even > 0.0 && std::fabs(Even - Tapped) <= 0.03 * Tapped ? Even : std::floor(Tapped + 0.5);
	ChooseSpeedTempo(Wanted, Beat, FrameRate, CurrentSpeed, SplitPoint, Speed, Tempo);
	Bpm = BeatsPerMinute(Speed, Tempo, FrameRate, Beat);
	return true;
}

bool LayoutShowsFrames(int Speed)
{
	return Speed == 1;
}

std::string LayoutFramesText(int Speed, int Tempo, int FrameRate)
{
	const double Rows = RowsPerMinute(Speed, Tempo, FrameRate);
	const double Frames = Rows > 0.0 ? 60.0 * FrameRate / Rows : static_cast<double>(Speed);
	const double Whole = std::floor(Frames + 0.5);
	char Text[32];
	if (std::fabs(Frames - Whole) < 0.005) {
		std::snprintf(Text, sizeof(Text), Whole == 1.0 ? "%.0f frame" : "%.0f frames", Whole);
		return Text;
	}
	std::snprintf(Text, sizeof(Text), "%.2f", Frames);
	std::string Shown = Text;
	while (Shown[Shown.size() - 1] == '0')
		Shown.erase(Shown.size() - 1);
	return Shown + " frames";
}

bool LayoutWantsFrames(int Bpm)
{
	return Bpm == 1 || Bpm > LAYOUT_MOST_BPM;
}

}	// namespace ftapi

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

#include "TuneShape.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>

namespace ftapi {

namespace {

int Sign(int Value)
{
	return Value > 0 ? 1 : Value < 0 ? -1 : 0;
}

}	// namespace

TuneMeasure MeasureTune(const std::vector<ShapeNote> &In, int Bars, int BarRows, int RowsPerBeat)
{
	TuneMeasure M;
	BarRows = std::max(1, BarRows);
	RowsPerBeat = std::max(1, RowsPerBeat);
	std::vector<ShapeNote> Notes;
	for (size_t i = 0; i < In.size(); ++i)
		if (In[i].Bar >= 0 && In[i].Bar < Bars)
			Notes.push_back(In[i]);
	std::sort(Notes.begin(), Notes.end(), [](const ShapeNote &a, const ShapeNote &b) { return a.Start < b.Start; });
	M.Bars = Bars;
	M.Notes = static_cast<int>(Notes.size());
	if (Notes.empty())
		return M;
	const size_t Count = Notes.size();

	// Each note's length as the ear counts a rhythm: to the next note's start, a bar at most
	std::vector<int> Length(Count);
	for (size_t i = 0; i < Count; ++i)
		Length[i] = std::min(BarRows, i + 1 < Count ? Notes[i + 1].Start - Notes[i].Start : Notes[i].Rows);

	// The bars' rhythms
	std::map<int, std::vector<int> > ByBar;
	for (size_t i = 0; i < Count; ++i)
		ByBar[Notes[i].Bar].push_back(Notes[i].Start - Notes[i].Bar * BarRows);
	std::set<std::vector<int> > Heard;
	int Again = 0;
	for (std::map<int, std::vector<int> >::const_iterator it = ByBar.begin(); it != ByBar.end(); ++it)
		Again += Heard.insert(it->second).second ? 0 : 1;
	// Bars that come back: a bar, or two bars from the first, with an earlier one's rhythm (where its notes start) and its
	// shape (three of four moves or more the same way, up, down or the same; three notes or more, not all one pitch), as
	// written, moved or ornamented; or in the rhythm alone
	{
		std::set<int> ShapeBack, RhythmBack;
		for (int Size = 1; Size <= 2; ++Size) {
			std::vector<std::vector<int> > Onsets, Pitches;
			std::vector<int> FirstBars;
			for (int First = 0; First + Size <= Bars; First += Size) {
				std::vector<int> On, P;
				for (size_t i = 0; i < Count; ++i)
					if (Notes[i].Bar >= First && Notes[i].Bar < First + Size) {
						On.push_back(Notes[i].Start - First * BarRows);
						P.push_back(Notes[i].Pitch);
					}
				Onsets.push_back(On);
				Pitches.push_back(P);
				FirstBars.push_back(First);
			}
			for (size_t u = 1; u < Onsets.size(); ++u) {
				const std::vector<int> &P = Pitches[u];
				if (P.size() < (Size == 1 ? 2u : 3u))
					continue;
				bool Moving = false;
				for (size_t m = 1; m < P.size(); ++m)
					Moving = Moving || P[m] != P[m - 1];
				bool Rhythm = false, Shape = false;
				for (size_t a = 0; a < u && !Shape; ++a) {
					// The notes on the steps both start notes on: all of them (the same rhythm), or the earlier one's but for one
					// at most with up to three notes added between (the motif ornamented: vary_part's embellish)
					std::vector<int> Here, There;
					for (size_t i = 0, j = 0; i < Onsets[u].size() && j < Onsets[a].size(); ) {
						if (Onsets[u][i] == Onsets[a][j]) {
							Here.push_back(P[i++]);
							There.push_back(Pitches[a][j++]);
						}
						else if (Onsets[u][i] < Onsets[a][j])
							++i;
						else
							++j;
					}
					const bool Same = Onsets[a] == Onsets[u];
					Rhythm = Rhythm || Same;
					if (!Same && (Here.size() < 3 || Here.size() + 1 < Onsets[a].size() || Onsets[u].size() > Onsets[a].size() + 3))
						continue;
					bool HereMoves = false;
					int Alike = 0;
					for (size_t m = 1; m < Here.size(); ++m) {
						HereMoves = HereMoves || Here[m] != Here[m - 1];
						Alike += Sign(Here[m] - Here[m - 1]) == Sign(There[m] - There[m - 1]) ? 1 : 0;
					}
					Shape = Here.size() >= 3 && Moving && HereMoves && Alike * 4 >= static_cast<int>(Here.size() - 1) * 3;
				}
				for (int k = 0; k < Size; ++k)
					(Shape ? ShapeBack : Rhythm ? RhythmBack : ShapeBack).insert(Shape || Rhythm ? FirstBars[u] + k : -1);
			}
		}
		ShapeBack.erase(-1);
		for (std::set<int>::const_iterator it = ShapeBack.begin(); it != ShapeBack.end(); ++it)
			RhythmBack.erase(*it);
		M.ShapeBars = static_cast<int>(ShapeBack.size());
		M.RhythmBars = static_cast<int>(RhythmBack.size());
	}
	M.Rhythms = static_cast<int>(Heard.size());
	M.RhythmReturn = ByBar.size() > 1 ? Again / static_cast<double>(ByBar.size() - 1) : 0.0;
	M.NotesPerBar = Count / static_cast<double>(ByBar.size());
	std::map<int, int> Lengths;
	for (size_t i = 0; i < Count; ++i)
		++Lengths[Length[i]];
	int Main = 0;
	for (std::map<int, int>::const_iterator it = Lengths.begin(); it != Lengths.end(); ++it)
		Main = std::max(Main, it->second);
	M.Lengths = static_cast<int>(Lengths.size());
	M.MainLength = Main / static_cast<double>(Count);

	// The opening motif: K notes from note From, their starts apart as the first K's (and the last one's length) and
	// starting at the same place in the beat; how alike their pitches are: 3 as written, 2 the same moves, 1 the same shape
	// a step or so wider, 0 the rhythm only, -1 not its rhythm
	auto Match = [&](size_t From, size_t K) {
		if (From + K > Count)
			return -1;
		if ((Notes[From].Start - Notes[0].Start) % RowsPerBeat != 0)
			return -1;
		for (size_t m = 0; m < K; ++m)
			if (Notes[From + m].Start - Notes[From].Start != Notes[m].Start - Notes[0].Start)
				return -1;
		if (Length[From + K - 1] != Length[K - 1])
			return -1;
		bool Same = true, Moves = true, Shape = true;
		for (size_t m = 0; m < K; ++m) {
			Same = Same && Notes[From + m].Pitch == Notes[m].Pitch;
			if (m == 0)
				continue;
			const int Here = Notes[From + m].Pitch - Notes[From + m - 1].Pitch, There = Notes[m].Pitch - Notes[m - 1].Pitch;
			Moves = Moves && Here == There;
			Shape = Shape && Sign(Here) == Sign(There) && std::abs(Here - There) <= 2;
		}
		return Same ? 3 : Moves ? 2 : Shape ? 1 : 0;
	};
	for (size_t K = 3; K <= 6 && K <= Count / 2; ++K) {
		bool Back = false;
		for (size_t j = K; j + K <= Count && !Back; ++j)
			Back = Match(j, K) >= 1;
		if (Back)
			M.MotifNotes = static_cast<int>(K);
	}
	const size_t K = static_cast<size_t>(std::max(3, M.MotifNotes));
	for (size_t j = K; j + K <= Count && K <= Count / 2;) {
		const int How = Match(j, K);
		if (How < 0) {
			++j;
			continue;
		}
		++M.Returns;
		M.Exact += How == 3 ? 1 : 0;
		M.Moved += How == 1 || How == 2 ? 1 : 0;
		M.Rhythmic += How == 0 ? 1 : 0;
		j += K;
	}

	// Range, leaps and repeated notes
	int Low = Notes[0].Pitch, High = Notes[0].Pitch, Same = 0, Run = 1, StepMoves = 0;
	M.SameRun = 1;
	for (size_t i = 0; i < Count; ++i) {
		Low = std::min(Low, Notes[i].Pitch);
		High = std::max(High, Notes[i].Pitch);
		if (i == 0)
			continue;
		const int Move = Notes[i].Pitch - Notes[i - 1].Pitch, Size = std::abs(Move);
		Same += Size == 0 ? 1 : 0;
		StepMoves += Size == 1 || Size == 2 ? 1 : 0;
		Run = Size == 0 ? Run + 1 : 1;
		M.SameRun = std::max(M.SameRun, Run);
		M.Biggest = std::max(M.Biggest, Size);
		if (Size >= 5) {
			++M.Leaps;
			if (i + 1 < Count) {
				const int Next = Notes[i + 1].Pitch - Notes[i].Pitch;
				M.Answered += Next != 0 && Sign(Next) != Sign(Move) && std::abs(Next) <= Size ? 1 : 0;
			}
		}
	}
	M.Range = High - Low;
	M.Repeats = Count > 1 ? Same / static_cast<double>(Count - 1) : 0.0;
	M.Steps = Count > 1 ? StepMoves / static_cast<double>(Count - 1) : 0.0;
	int Turns = 0;
	for (size_t i = 2; i < Count; ++i) {
		const int A = Notes[i - 1].Pitch - Notes[i - 2].Pitch, B = Notes[i].Pitch - Notes[i - 1].Pitch;
		Turns += A != 0 && B != 0 && Sign(A) != Sign(B) && std::abs(A) <= 2 && std::abs(B) <= 2 ? 1 : 0;
	}
	M.Zigzag = Count > 2 ? Turns / static_cast<double>(Count - 2) : 0.0;

	// The phrases of four bars: their ranges and high points
	double Ranges = 0, PeaksAt = 0, Pitches4 = 0;
	for (int First = 0; First < Bars; First += 4) {
		std::vector<size_t> In4;
		for (size_t i = 0; i < Count; ++i)
			if (Notes[i].Bar >= First && Notes[i].Bar < First + 4)
				In4.push_back(i);
		if (In4.size() < 3)
			continue;
		++M.Phrases;
		int Top = -1, Bottom = 1000, AtTop = 0;
		size_t TopAt = In4[0];
		for (size_t n = 0; n < In4.size(); ++n) {
			const int P = Notes[In4[n]].Pitch;
			Bottom = std::min(Bottom, P);
			if (P > Top) {
				Top = P;
				TopAt = In4[n];
				AtTop = 1;
			}
			else if (P == Top)
				++AtTop;
		}
		Ranges += Top - Bottom;
		std::set<int> Classes;
		for (size_t n = 0; n < In4.size(); ++n)
			Classes.insert(Notes[In4[n]].Pitch);
		Pitches4 += static_cast<double>(Classes.size());
		M.Peaked += AtTop == 1 && Top > Bottom ? 1 : 0;
		PeaksAt += (Notes[TopAt].Start - First * BarRows) / static_cast<double>(4 * BarRows);
	}
	M.PhraseRange = M.Phrases > 0 ? Ranges / M.Phrases : M.Range;
	M.PhrasePitches = M.Phrases > 0 ? Pitches4 / M.Phrases : 0.0;
	M.PeakAt = M.Phrases > 0 ? PeaksAt / M.Phrases : 0.0;
	return M;
}

std::string TuneMeasureText(const TuneMeasure &M)
{
	char Text[512];
	std::snprintf(Text, sizeof(Text), "bars %d notes %d (%.1f a bar) motif %d returns %d (exact %d moved %d rhythm %d) bars back %d shape %d rhythm only, rhythms %d "
		"back %.2f lengths %d main %.2f range %d phrase %.1f leaps %d biggest %d answered %d peaked %d/%d at %.2f repeats %.2f run %d "
		"steps %.2f zigzag %.2f pitches %.1f", M.Bars, M.Notes, M.NotesPerBar, M.MotifNotes, M.Returns, M.Exact, M.Moved, M.Rhythmic, M.ShapeBars, M.RhythmBars, M.Rhythms,
		M.RhythmReturn, M.Lengths, M.MainLength, M.Range, M.PhraseRange, M.Leaps, M.Biggest, M.Answered, M.Peaked, M.Phrases,
		M.PeakAt, M.Repeats, M.SameRun, M.Steps, M.Zigzag, M.PhrasePitches);
	return Text;
}

// What the user's ear heard (round 2 of the songs made by following what_next, gen4): the Caves' tune "not catchy at
// all", the Hut's "kinda boring and monotone in its melody", Night Harbor's "just there"; the Guard Tower's melody was
// fine. Measured over each melody's first 8 bars (song_file_checks -tunes), against the 44 songs he keeps (his game's
// tracks, his soundtrack's and the local models' he liked):
// - The motif coming back: in the kept songs a bar (or two) comes back with an earlier one's rhythm and its shape (three
//   moves of four the same way; ornamented counts) in 0-6 of the first 8 bars, most 2-4 (Skylight 5, Verdant Run 4,
//   Applefall 3, City 6, Ironworks 4; nine in none, through-composed or in long notes: 05 The Copper Spoon, 13 Mirage
//   Bazaar); where the rhythm comes back without the shape, at most 3 bars do with no shape coming back at all (05/06
//   New City's long notes, 3). The three the user heard as monotone bring the rhythm back in 4-5 bars and the shape in none:
//   the Hut 4 (its answers and sequences, fitted to their chords note by note, wandered around Bb3), Night Harbor 5 (C5 A4
//   G4, A4 A4 C5, C5 C5 Bb4 on one rhythm), the Caves 4 (its G4 merged away, F4 struck twice). That's WANDER_BARS.
// - Range: each 4-bar phrase spans 4.5-18 semitones in the kept songs (05 The Copper Spoon 4.5, 008 5.0, the most 7-16),
//   the Caves 6.0, Night Harbor 7.0: narrow alone isn't monotone, but under NARROW_PHRASE no kept tune goes.
// - Rhythm: the kept tunes play 2-7 rhythms in 8 bars (Night.ftm's one note a bar aside), their commonest note length at
//   most 82% of the notes (The Lamplighter's Round, 2.9 notes a bar; 80% at 1.5 a bar): one rhythm in every bar, or one
//   length for nearly every note with 4 notes a bar or more, is a typed line, not a tune: review_arrangement's "keeps one
//   or two note lengths" and check_song's phrases say so already, so it isn't checked here again.
// - Leaps: 0-13 moves of a fourth or more in 8 bars (most 2-6, about half followed by a step back); repeated notes up to
//   32% of the moves (13 Mirage Bazaar's oud); high points: most phrases have one note higher than the rest, a third of the
//   way to two thirds in (0.3-0.6 on average): none of those tells the user's three from the kept ones, so they're taught
//   (HookAdvice), not checked.
// - Hooks (a melody's first two bars): the kept ones play 3-16 notes, range 3-21 semitones (most 7-12), 2-8 pitches;
//   the Caves' hook (F4 - - G4 F4 - - - | E4 - G4) and the Guard Tower's, which the user found fine, are alike there (ranges
//   3 and 4): a hook isn't monotone by these until it hardly moves (MonotoneHook's), and then no kept one is
const int WANDER_BARS = 4;
const double NARROW_PHRASE = 4.0;

std::vector<std::string> MonotoneMelody(const TuneMeasure &M)
{
	std::vector<std::string> Out;
	if (M.Bars < 8 || M.Notes < 8)
		return Out;
	if (M.ShapeBars == 0 && M.RhythmBars >= WANDER_BARS)
		Out.push_back("its rhythm comes back in " + std::to_string(M.RhythmBars) + " of its first 8 bars but its shape never does, "
			"so the notes wander instead of bringing a motif back");
	if (M.Phrases >= 2 && M.PhraseRange < NARROW_PHRASE)
		Out.push_back("each phrase stays within " + std::to_string(static_cast<int>(M.PhraseRange + 0.5)) + " semitones");
	return Out;
}

std::vector<std::string> MonotoneHook(const TuneMeasure &M)
{
	std::vector<std::string> Out;
	if (M.Notes < 4)
		return Out;
	if (M.Range <= 2)
		Out.push_back("it moves within a step (" + std::to_string(M.Range) + " semitones)");
	else if (M.PhrasePitches <= 2)
		Out.push_back("it plays only two notes");
	if (M.Repeats >= 0.5 && M.Range <= 4)
		Out.push_back("it strikes the same note again and again");
	if (M.MainLength >= 0.99 && M.Notes >= 5 && M.Range <= 5)
		Out.push_back("its notes are all one length");
	return Out;
}

std::string HookAdvice()
{
	return "A hook a player remembers is a motif of 2-5 notes in one rhythm you could clap (short notes into a long one, a "
		"dotted pair, a pickup into the bar), stated, then again a step or two higher or lower (the same rhythm and shape), then "
		"answered (that rhythm going the other way, or a held note); one leap (a fourth to a sixth) with a step back after it, "
		"one high point a phrase past its middle, no note three times running, a fifth or more from its lowest to its highest. "
		"In C major over | C | F | Dm | G |, a token an eighth: \"1: E4 F4 G4 - C5 - - - | 2: A4 - G4 - F4 - . . | 3: F4 G4 A4 - "
		"D5 - - - | 4: B4 - A4 - G4 - . .\" (bar 3 is bar 1 a step higher, bar 4 bar 2's answer).";
}

}	// namespace ftapi

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

#include "GrowMelody.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

namespace ftapi {

namespace {

int ClassOf(int Pitch)
{
	return ((Pitch % 12) + 12) % 12;
}

// A generator of its own, so a variant gives the same versions on every computer and in every call
class GrowRandom {
public:
	explicit GrowRandom(uint32_t Seed) : m_State(Seed != 0 ? Seed : 0x9E3779B9u)
	{
		for (int i = 0; i < 8; ++i)
			Next();
	}
	uint32_t Next()
	{
		m_State ^= m_State << 13;
		m_State ^= m_State >> 17;
		m_State ^= m_State << 5;
		return m_State;
	}
	int Below(int Count)
	{
		return Count <= 1 ? 0 : static_cast<int>(Next() % static_cast<uint32_t>(Count));
	}
	bool Chance(int Percent)
	{
		return Below(100) < Percent;
	}

private:
	uint32_t m_State;
};

uint32_t Mix(uint32_t a, uint32_t b)
{
	uint32_t h = (a ^ 0x85EBCA6Bu) * 0x9E3779B1u + b * 0xC2B2AE35u + 0x27D4EB2Fu;
	h ^= h >> 15;
	h *= 0x2C1B3C6Du;
	h ^= h >> 12;
	h *= 0x297A2D39u;
	h ^= h >> 15;
	return h;
}

// The key's scale in every octave, for moving notes by its steps
class ScaleLadder {
public:
	explicit ScaleLadder(const Key &K)
	{
		const int *pSteps = ModeSteps(K.Mode);
		for (int p = 0; p < PITCHES; ++p)
			for (int s = 0; s < 7; ++s)
				if (ClassOf(p - K.Tonic) == pSteps[s]) {
					m_Pitches.push_back(p);
					break;
				}
	}
	bool On(int Pitch) const
	{
		return std::binary_search(m_Pitches.begin(), m_Pitches.end(), Pitch);
	}
	// The rung at or under Pitch, and how far over it Pitch is
	int Rung(int Pitch, int *pOver = 0) const
	{
		const std::vector<int>::const_iterator it = std::upper_bound(m_Pitches.begin(), m_Pitches.end(), Pitch);
		const int Index = static_cast<int>(it - m_Pitches.begin()) - 1;
		if (pOver != 0)
			*pOver = Index >= 0 ? Pitch - m_Pitches[static_cast<size_t>(Index)] : 0;
		return Index;
	}
	// Pitch moved by Steps of the scale (a note between its notes keeps how far over the one under it it is)
	int Shift(int Pitch, int Steps) const
	{
		int Over = 0;
		const int Index = Rung(Pitch, &Over) + Steps;
		if (Index < 0 || Index >= static_cast<int>(m_Pitches.size()))
			return std::max(0, std::min(PITCHES - 1, Pitch + Steps * 2));
		return m_Pitches[static_cast<size_t>(Index)] + Over;
	}

private:
	std::vector<int> m_Pitches;
};

// What a bar of a version does
enum bar_role {
	BAR_MOTIF,		// the motif, as written where it fits the chords, else moved by the scale's steps to fit
	BAR_SEQUENCE,	// the motif moved up or down by the scale's steps, its shape kept
	BAR_VARIED,		// the motif's opening kept, its end changed
	BAR_ANSWER,		// the motif's rhythm turned the other way, settling on a chord tone
	BAR_RUN,		// the motif's opening notes twice, moving up or down
	BAR_CLIMB,		// up to the section's high point, held
	BAR_CLOSE,		// down to a close, then a rest
	BAR_HALF,		// a half close (on the dominant or the second), then a rest
};

// The versions' forms
enum grow_form {
	FORM_PERIOD,	// a question and its answer: the motif, an answer, a sequence, a half close; then again to the close
	FORM_SENTENCE,	// the motif and its sequence, then a run to the close
	FORM_CALLS,		// two-bar calls: the motif twice with other endings, then a climb
	FORM_COUNT
};

const char *const FORM_NAMES[FORM_COUNT] = { "question and answer", "hook and sequence", "two-bar calls" };

struct BarPlan {
	int Role;			// bar_role
	int MotifBar;		// the motif's bar its rhythm comes from (a two-bar motif's second for the second bar of its roles)
	int Direction;		// a sequence's or a run's way: 1 up, -1 down
	bool Comma;			// ends on a note held two beats or more (a phrase's middle)
	bool End;			// ends a phrase: a rest of a beat or more
	bool Peak;			// holds the section's high point
	int Section;
	bool Exact;			// the motif as written
	int Shift;			// the scale's steps the motif moves (a sequence, a statement moved to fit, a run's first notes)
	int RestSteps;		// the rest at its end (End)
	BarPlan() : Role(BAR_MOTIF), MotifBar(0), Direction(1), Comma(false), End(false), Peak(false), Section(0), Exact(false), Shift(0),
		RestSteps(0) {}
};

// A block of bars as the forms write them: a letter a bar (M the motif, S a sequence, V a variation, A an answer, R a run
// on the motif's opening, C the climb to the high point, E the close, H a half close), ^ or v the way a sequence or a run
// goes, a comma for a phrase's middle (a note held two beats) and a full stop for its end (a rest)
void AddBlock(const std::string &Text, int MotifBars, int Section, std::vector<BarPlan> &Out)
{
	std::vector<std::string> Tokens;
	std::string Token;
	for (size_t i = 0; i <= Text.size(); ++i) {
		if (i == Text.size() || Text[i] == ' ') {
			if (!Token.empty())
				Tokens.push_back(Token);
			Token.clear();
		}
		else
			Token += Text[i];
	}
	for (size_t t = 0; t < Tokens.size(); ++t) {
		const std::string &T = Tokens[t];
		BarPlan B;
		B.Section = Section;
		switch (T[0]) {
		case 'M': B.Role = BAR_MOTIF; break;
		case 'S': B.Role = BAR_SEQUENCE; break;
		case 'V': B.Role = BAR_VARIED; break;
		case 'A': B.Role = BAR_ANSWER; break;
		case 'R': B.Role = BAR_RUN; break;
		case 'C': B.Role = BAR_CLIMB; B.Peak = true; B.Comma = true; break;
		case 'E': B.Role = BAR_CLOSE; B.End = true; break;
		default: B.Role = BAR_HALF; B.End = true; break;
		}
		for (size_t k = 1; k < T.size(); ++k) {
			if (T[k] == '^')
				B.Direction = 1;
			else if (T[k] == 'v')
				B.Direction = -1;
			else if (T[k] == ',')
				B.Comma = true;
			else if (T[k] == '.')
				B.End = true;
		}
		if (B.End)
			B.Comma = false;
		// A two-bar motif's roles take two bars, the second with the rhythm of the motif's second bar
		if (MotifBars > 1 && (B.Role == BAR_MOTIF || B.Role == BAR_SEQUENCE || B.Role == BAR_ANSWER || B.Role == BAR_VARIED)) {
			const bool Second = t > 0 && Tokens[t - 1][0] == T[0] && !Out.empty() && Out.back().MotifBar == 0;
			B.MotifBar = B.Role == BAR_VARIED || Second ? 1 : 0;
		}
		Out.push_back(B);
	}
}

// The forms' blocks of four bars: the first of a section, one before the high point, the block with the high point when
// more follow, the block with it when it's the last, the last after it, and a section of four bars
struct FormBlocks {
	const char *Open, *Response, *Middle, *Close, *Return, *Single, *OpenPeak;
};

// A one-bar motif: lively and action songs in phrases of four bars with a held note in their middle (or two-bar calls),
// calm songs in phrases of two, each ending with a rest
const FormBlocks ONE_BAR[2][FORM_COUNT] = {
	{
		{ "M A, S^ H.", "M A, S^ E.", "S^ A, C H.", "M A, C E.", "M A, Rv E.", "M A, C E.", "M A, C H." },
		{ "M S^, R^ H.", "M S^, Rv E.", "S^ R^, C H.", "M S^, C E.", "M S^, Rv E.", "M S^, C E.", "M S^, C H." },
		{ "M A. M H.", "M A. Rv E.", "S^ A. C H.", "M R^, C E.", "M A. Rv E.", "M A. C E.", "M A. C H." },
	},
	{
		{ "M A. S^ H.", "M A. S^ E.", "S^ A. C H.", "M A. C E.", "M A. Rv E.", "M A. C E.", "M A. C H." },
		{ "M S^. Rv H.", "M S^. Rv E.", "S^ R^. C H.", "M S^. C E.", "M S^. Rv E.", "M S^. C E.", "M S^. C H." },
		{ "M A. M H.", "M A. Rv E.", "S^ A. C H.", "M R^. C E.", "M A. Rv E.", "M A. C E.", "M A. C H." },
	},
};

// A two-bar motif is a phrase's first half itself
const FormBlocks TWO_BAR[FORM_COUNT] = {
	{ "M M A A.", "M M S^ E.", "S^ S^, C H.", "M V, C E.", "M M Rv E.", "M M C E.", "M M C H." },
	{ "M M S^ S^.", "M M Rv E.", "S^ S^, C H.", "M V, C E.", "M V, Rv E.", "M V, C E.", "M V, C H." },
	{ "M M A A.", "M V, Rv E.", "A A, C H.", "A A, C E.", "M M Rv E.", "M M C E.", "M M C H." },
};

// A section of Count bars: blocks of four (the last two of three and two where four don't divide it, or one of two or
// three), its high point in the last block of four, or in the one before it when three or more come before
void SectionPlan(int Count, int MotifBars, bool MotifBreathes, int Form, bool Calm, int Section, std::vector<BarPlan> &Out)
{
	std::vector<int> Sizes;
	if (Count <= 4)
		Sizes.push_back(Count);
	else {
		const int Fours = Count / 4, Left = Count % 4;
		for (int i = 0; i < Fours - (Left == 1 ? 1 : 0); ++i)
			Sizes.push_back(4);
		if (Left == 1) {
			Sizes.push_back(3);
			Sizes.push_back(2);
		}
		else if (Left > 0)
			Sizes.push_back(Left);
	}
	int LastFour = -1, Fours = 0;
	for (size_t i = 0; i < Sizes.size(); ++i)
		if (Sizes[i] == 4) {
			LastFour = static_cast<int>(i);
			++Fours;
		}
	const int PeakBlock = LastFour < 0 ? 0 : Fours >= 3 ? LastFour - 1 : LastFour;
	const FormBlocks &F = MotifBars > 1 ? TWO_BAR[Form] : ONE_BAR[Calm ? 1 : 0][Form];
	for (size_t i = 0; i < Sizes.size(); ++i) {
		const int Block = static_cast<int>(i);
		const bool First = Block == 0, Last = Block + 1 == static_cast<int>(Sizes.size()), Peak = Block == PeakBlock;
		std::string Text;
		if (Sizes[i] == 4) {
			if (First)
				Text = Peak ? (Last ? F.Single : F.OpenPeak) : F.Open;
			else if (Peak)
				Text = Last ? F.Close : F.Middle;
			else
				Text = Block > PeakBlock ? F.Return : F.Response;
			// A two-bar motif that doesn't breathe comes back with its second bar changed, so the phrase holds a note
			if (MotifBars > 1 && !MotifBreathes && !First) {
				const size_t At = Text.find("M M");
				if (At != std::string::npos)
					Text.replace(At, 3, "M V,");
			}
		}
		else if (Sizes[i] == 3) {
			if (MotifBars > 1)
				Text = First ? "M M E." : Last ? "S^ Rv E." : "S^ Rv H.";
			else if (Peak)
				Text = First ? "M C H." : "S^ C H.";
			else
				Text = First ? (Last ? "M C E." : "M A H.") : Last ? "M Rv E." : "M Rv H.";
			if (Peak && First && Last && MotifBars == 1)
				Text = "M C E.";
		}
		else if (Sizes[i] == 2)
			Text = First ? "M E." : "Rv E.";
		else
			Text = "M";
		if (Calm && MotifBars == 1)
			for (size_t c = 0; c < Text.size(); ++c)
				if (Text[c] == ',' && (c == 0 || Text[c - 1] != 'C'))
					Text[c] = '.';
		AddBlock(Text, MotifBars, Section, Out);
	}
}

// A note of a bar's rhythm, before its pitch
struct Hit {
	int Step, Steps;	// in the bar
	int Model;			// the motif's note it follows; MODEL_NONE, MODEL_PEAK or MODEL_GOAL
	int Fragment;		// a run's first or second time through the motif's opening
};

const int MODEL_NONE = -1, MODEL_PEAK = -2, MODEL_GOAL = -3;

// A note of a version, before and after its pitch is chosen
struct Slot {
	int Step, Steps;
	int Bar;			// whose plan it follows
	int Model;			// as Hit's
	int Fragment;
	bool Fixed;			// a note of the motif as written
	int Pitch;
	bool AfterRest;
	int ModelPitch;		// the motif's note moved as the bar moves it; -1 for none
	double ModelWeight;
	int Target;			// where the line heads; -1 for none
	double TargetWeight;
	bool Settles;		// a phrase's middle or end: on a plain chord tone
	bool Early;			// the next bar's first note a step early, tied over the bar line
	Slot() : Step(0), Steps(1), Bar(0), Model(MODEL_NONE), Fragment(0), Fixed(false), Pitch(0), AfterRest(false), ModelPitch(-1),
		ModelWeight(0.0), Target(-1), TargetWeight(0.0), Settles(false), Early(false) {}
};

struct Candidate {
	int Pitch;
	double Cost;
};

const double NEVER = 1e9;

class Grower {
public:
	Grower(const GrowRequest &Q, int Form, uint32_t Seed) : m_Q(Q), m_Form(Form), m_Seed(Seed), m_R(Seed), m_Ladder(Q.K),
		m_SPB(Q.StepsPerBar), m_SPBeat(Q.StepsPerBeat), m_Beats(Q.StepsPerBar / std::max(1, Q.StepsPerBeat)), m_Calm(Q.Kind == GROW_CALM)
	{
		m_MotifMin = 1000;
		m_MotifMax = -1;
		int Last = 0;
		for (size_t i = 0; i < Q.Motif.size(); ++i) {
			m_MotifMin = std::min(m_MotifMin, Q.Motif[i].Pitch);
			m_MotifMax = std::max(m_MotifMax, Q.Motif[i].Pitch);
			Last = std::max(Last, Q.Motif[i].Step + Q.Motif[i].Steps);
		}
		m_MotifBars = std::max(1, (Last + m_SPB - 1) / m_SPB);
		m_Centre = (m_MotifMin + m_MotifMax) / 2;
		m_Low = std::max(Q.Low, m_MotifMin - (m_Calm ? 4 : 5));
		// The motif breathes by itself: a rest of a beat or more inside it, or a note held two beats
		m_MotifBreathes = false;
		for (size_t i = 0; i < Q.Motif.size(); ++i) {
			const int Next = i + 1 < Q.Motif.size() ? Q.Motif[i + 1].Step : m_MotifBars * m_SPB;
			if (Q.Motif[i].Steps >= 2 * m_SPBeat || Next - (Q.Motif[i].Step + Q.Motif[i].Steps) >= m_SPBeat)
				m_MotifBreathes = true;
		}
		const int Direction = Q.Motif.back().Pitch - Q.Motif.front().Pitch;
		m_MotifWay = Direction > 0 ? 1 : Direction < 0 ? -1 : 1;
	}

	void Grow(GrownVersion &Out)
	{
		Plan();
		m_Hits.assign(m_Plan.size(), std::vector<Hit>());
		for (size_t b = 0; b < m_Plan.size(); ++b)
			m_Hits[b] = BarRhythm(static_cast<int>(b));
		ChoosePeaks();
		ChooseShifts();
		MakeSlots();
		Tie();
		for (size_t i = 0; i < m_Slots.size(); ++i)
			m_Slots[i].AfterRest = i == 0 || m_Slots[i].Step > m_Slots[i - 1].Step + m_Slots[i - 1].Steps;
		Shape();
		Choose();
		if (m_Calm)
			HoldRepeats();
		Describe(Out);
	}

private:
	const GrowRequest &m_Q;
	int m_Form;
	uint32_t m_Seed;
	GrowRandom m_R;
	ScaleLadder m_Ladder;
	int m_SPB, m_SPBeat, m_Beats;
	bool m_Calm;
	int m_MotifMin, m_MotifMax, m_MotifBars, m_Centre, m_Low, m_MotifWay;
	bool m_MotifBreathes;
	std::vector<BarPlan> m_Plan;
	std::vector<int> m_SectionFirst;		// each section's first bar
	std::vector<int> m_Peak;				// each section's high point, -1 for none
	std::vector<std::vector<Hit> > m_Hits;	// each bar's rhythm
	std::vector<Slot> m_Slots;

	int Sections() const { return static_cast<int>(m_SectionFirst.size()); }

	// How strong a step of a bar is: 3 its first, 2 its middle (in bars of an even number of beats), 1 another beat,
	// 0 between the beats
	int Strength(int Step) const
	{
		const int InBar = Step % m_SPB;
		if (InBar == 0)
			return 3;
		if (InBar % m_SPBeat != 0)
			return 0;
		return m_Beats % 2 == 0 && InBar / m_SPBeat == m_Beats / 2 ? 2 : 1;
	}

	const Chord *ChordAt(int Step) const
	{
		if (Step < 0 || Step >= static_cast<int>(m_Q.StepChord.size()))
			return 0;
		const int Index = m_Q.StepChord[static_cast<size_t>(Step)];
		return Index >= 0 && Index < static_cast<int>(m_Q.Chords.size()) ? &m_Q.Chords[static_cast<size_t>(Index)] : 0;
	}

	// The chords a note sounds on at the strong steps it covers
	std::vector<const Chord *> StrongChords(int Step, int Steps) const
	{
		std::vector<const Chord *> Out;
		for (int s = Step; s < Step + Steps; ++s)
			if (Strength(s) >= 2) {
				const Chord *pChord = ChordAt(s);
				if (pChord != 0 && std::find(Out.begin(), Out.end(), pChord) == Out.end())
					Out.push_back(pChord);
			}
		return Out;
	}

	// In the key: the mode's notes, and in a minor key the raised seventh over a chord that has it (its plain seventh not)
	bool Allowed(int Pitch, int Step, int Steps) const
	{
		const int Relative = ClassOf(Pitch - m_Q.K.Tonic);
		bool Raised = false;
		if (m_Q.K.Mode == MODE_MINOR)
			for (int s = Step; s < Step + Steps && !Raised; ++s) {
				const Chord *pChord = ChordAt(s);
				Raised = pChord != 0 && pChord->HasPitchClass(ClassOf(m_Q.K.Tonic + 11));
			}
		if (m_Q.K.Mode == MODE_MINOR && Raised)
			return Relative == 11 || (Relative != 10 && m_Ladder.On(Pitch));
		return m_Ladder.On(Pitch);
	}

	// Whether a note rubs a semitone (or a semitone and octaves) against another part for a while, as check_song hears it
	bool Rubs(int Pitch, int Step, int Steps) const
	{
		const int Row = Step * m_Q.RowsPerStep, End = (Step + Steps) * m_Q.RowsPerStep;
		for (size_t i = 0; i < m_Q.Others.size(); ++i) {
			const GrowOther &O = m_Q.Others[i];
			if (std::abs(Pitch - O.Pitch) % 12 != 1)
				continue;
			if (std::min(End, O.End) - std::max(Row, O.Row) >= std::max(1, m_Q.RubRows))
				return true;
		}
		return false;
	}

	// Without other parts to hear: a note a semitone over one of the chord's (the fourth over a major chord) rubs once
	// they're written
	bool Avoided(int Pitch, int Step, int Steps) const
	{
		if (!m_Q.Others.empty() || Steps * m_Q.RowsPerStep < std::max(1, m_Q.RubRows))
			return false;
		const Chord *pChord = ChordAt(Step);
		if (pChord == 0 || pChord->HasPitchClass(ClassOf(Pitch)))
			return false;
		return pChord->HasPitchClass(ClassOf(Pitch - 1));
	}

	bool FitsChords(int Pitch, const std::vector<const Chord *> &Chords) const
	{
		for (size_t c = 0; c < Chords.size(); ++c)
			if (!Chords[c]->HasPitchClass(ClassOf(Pitch)))
				return false;
		return true;
	}

	// The plan

	void Plan()
	{
		// The sections: as given, those under four bars joined to the one before
		std::vector<int> Starts(1, 0);
		std::vector<int> Given(m_Q.Sections);
		std::sort(Given.begin(), Given.end());
		for (size_t i = 0; i < Given.size(); ++i)
			if (Given[i] > Starts.back() && Given[i] < m_Q.Bars)
				Starts.push_back(Given[i]);
		for (size_t i = 1; i < Starts.size(); ) {
			const int Length = (i + 1 < Starts.size() ? Starts[i + 1] : m_Q.Bars) - Starts[i];
			const int Before = Starts[i] - Starts[i - 1];
			if (Length < 4 || Before < 4)
				Starts.erase(Starts.begin() + static_cast<std::ptrdiff_t>(i));
			else
				++i;
		}
		m_SectionFirst = Starts;
		m_Plan.clear();
		for (size_t s = 0; s < Starts.size(); ++s) {
			const int Count = (s + 1 < Starts.size() ? Starts[s + 1] : m_Q.Bars) - Starts[s];
			SectionPlan(Count, m_MotifBars, m_MotifBreathes, m_Form, m_Calm, static_cast<int>(s), m_Plan);
		}
		m_Plan.resize(static_cast<size_t>(m_Q.Bars));
		// The rests where phrases end: a beat, in a calm song now and then two
		for (size_t b = 0; b < m_Plan.size(); ++b)
			if (m_Plan[b].End)
				m_Plan[b].RestSteps = m_Calm && m_Beats >= 4 && !(b > 0 && m_Plan[b - 1].Role == BAR_CLIMB) && m_R.Chance(40) ?
					2 * m_SPBeat : m_SPBeat;
	}

	// The motif's notes in its bar Bar as a bar's rhythm (a note held into it from the bar before struck again at its start)
	std::vector<Hit> MotifRhythm(int Bar) const
	{
		std::vector<Hit> Out;
		const int From = Bar * m_SPB, To = From + m_SPB;
		for (size_t j = 0; j < m_Q.Motif.size(); ++j) {
			const GrowNote &N = m_Q.Motif[j];
			const int Start = std::max(N.Step, From), Stop = std::min(N.Step + N.Steps, To);
			if (Stop <= Start)
				continue;
			Hit H;
			H.Step = Start - From;
			H.Steps = Stop - Start;
			H.Model = static_cast<int>(j);
			H.Fragment = 0;
			Out.push_back(H);
		}
		return Out;
	}

	// Beats From to To (steps in a bar) as notes in the kind's figures: calm mostly a note a beat, often held on, lively
	// notes a beat and pairs of eighths, action mostly eighths
	void Figures(int From, int To, std::vector<Hit> &Out, bool EveryBeat = false)
	{
		struct Figure {
			const char *Steps;
			int Weight[3];		// calm, lively, action
		};
		static const Figure TWO[] = { { "2", { 6, 4, 1 } }, { "11", { 1, 5, 9 } } };
		static const Figure THREE[] = { { "3", { 6, 3, 1 } }, { "21", { 2, 4, 3 } }, { "111", { 0, 2, 6 } }, { "12", { 0, 1, 1 } } };
		static const Figure FOUR[] = { { "4", { 6, 3, 1 } }, { "22", { 2, 5, 4 } }, { "211", { 0, 2, 3 } }, { "1111", { 0, 0, 3 } },
			{ "31", { 1, 1, 1 } } };
		static const Figure SIX[] = { { "6", { 6, 3, 1 } }, { "42", { 2, 4, 2 } }, { "222", { 0, 3, 5 } }, { "2211", { 0, 1, 3 } },
			{ "33", { 1, 1, 1 } } };
		static const int HOLD[3] = { 40, 18, 5 };	// percent: a beat holds the note before it on
		const Figure *pFigures = 0;
		int Count = 0;
		switch (m_SPBeat) {
		case 2: pFigures = TWO; Count = 2; break;
		case 3: pFigures = THREE; Count = 4; break;
		case 4: pFigures = FOUR; Count = 5; break;
		case 6: pFigures = SIX; Count = 5; break;
		default: break;
		}
		const int Kind = std::max(0, std::min(2, m_Q.Kind));
		for (int Beat = From; Beat < To; Beat += m_SPBeat) {
			const int Length = std::min(m_SPBeat, To - Beat);
			if (Beat > 0 && !EveryBeat && !Out.empty() && Out.back().Step + Out.back().Steps == Beat && Out.back().Model == MODEL_NONE &&
				m_R.Chance(HOLD[Kind])) {
				Out.back().Steps += Length;
				continue;
			}
			const char *pSteps = "";
			if (pFigures != 0 && Length == m_SPBeat) {
				int Total = 0;
				for (int f = 0; f < Count; ++f)
					Total += pFigures[f].Weight[Kind];
				int Pick = m_R.Below(Total);
				for (int f = 0; f < Count; ++f) {
					if (Pick < pFigures[f].Weight[Kind]) {
						pSteps = pFigures[f].Steps;
						break;
					}
					Pick -= pFigures[f].Weight[Kind];
				}
			}
			if (*pSteps == 0) {
				const Hit H = { Beat, Length, MODEL_NONE, 0 };
				Out.push_back(H);
				continue;
			}
			int At = Beat;
			for (const char *p = pSteps; *p != 0; ++p) {
				const Hit H = { At, *p - '0', MODEL_NONE, 0 };
				Out.push_back(H);
				At += *p - '0';
			}
		}
	}

	// The last note that can start Hold steps before the bar's end holds to it; the notes after it go
	void HoldAtEnd(std::vector<Hit> &Hits, int Hold) const
	{
		int Keep = -1;
		for (size_t i = 0; i < Hits.size(); ++i)
			if (Hits[i].Step <= m_SPB - Hold)
				Keep = static_cast<int>(i);
		if (Keep < 0) {
			Hits.clear();
			const Hit H = { 0, m_SPB, MODEL_NONE, 0 };
			Hits.push_back(H);
			return;
		}
		Hits.resize(static_cast<size_t>(Keep) + 1);
		Hits.back().Steps = m_SPB - Hits.back().Step;
	}

	// The bar ends with Rest steps of rest, its last note held up to it
	void RestAtEnd(std::vector<Hit> &Hits, int Rest) const
	{
		const int Stop = m_SPB - Rest;
		std::vector<Hit> Kept;
		for (size_t i = 0; i < Hits.size(); ++i)
			if (Hits[i].Step + std::min(m_SPBeat, Stop) <= Stop)
				Kept.push_back(Hits[i]);
		if (Kept.empty()) {
			const Hit H = { 0, Stop, MODEL_NONE, 0 };
			Kept.push_back(H);
		}
		for (size_t i = 0; i < Kept.size(); ++i)
			Kept[i].Steps = std::min(Kept[i].Steps, Stop - Kept[i].Step);
		Kept.back().Steps = Stop - Kept.back().Step;
		Hits = Kept;
	}

	std::vector<Hit> BarRhythm(int b)
	{
		const BarPlan &B = m_Plan[static_cast<size_t>(b)];
		std::vector<Hit> H;
		const int Half = m_Beats % 2 == 0 ? m_SPB / 2 : -1;
		switch (B.Role) {
		case BAR_MOTIF:
		case BAR_SEQUENCE:
		case BAR_VARIED:
		case BAR_ANSWER:
			H = MotifRhythm(B.MotifBar);
			if (H.empty())
				Figures(0, m_SPB, H);
			break;
		case BAR_RUN: {
			// The motif's opening notes twice, from the bar's start and from its middle
			std::vector<Hit> Head;
			const std::vector<Hit> Motif = MotifRhythm(0);
			for (size_t i = 0; i < Motif.size() && Half > 0; ++i)
				if (Motif[i].Step < Half) {
					Hit h = Motif[i];
					h.Steps = std::min(h.Steps, Half - h.Step);
					Head.push_back(h);
				}
			if (Head.size() >= 2 && Head.front().Step == 0 && m_Calm) {
				// A calm song's run: the motif's opening at half speed, once across the bar
				Head.back().Steps = Half - Head.back().Step;
				for (size_t i = 0; i < Head.size(); ++i) {
					Hit h = Head[i];
					h.Step *= 2;
					h.Steps *= 2;
					H.push_back(h);
				}
			}
			else if (Head.size() >= 2 && Head.front().Step == 0) {
				Head.back().Steps = Half - Head.back().Step;
				for (int f = 0; f < 2; ++f)
					for (size_t i = 0; i < Head.size(); ++i) {
						Hit h = Head[i];
						h.Step += f * Half;
						h.Fragment = f;
						H.push_back(h);
					}
			}
			else
				Figures(0, m_SPB, H);
			break;
		}
		case BAR_CLIMB: {
			// Up to the high point on the bar's middle (its first beat in bars of three), held two beats
			if (Half > 0) {
				Figures(0, Half, H, m_Calm);
				const Hit P = { Half, m_SPB - Half, MODEL_PEAK, 0 };
				H.push_back(P);
			}
			else {
				const int Held = std::min(m_SPB, 2 * m_SPBeat);
				const Hit P = { 0, Held, MODEL_PEAK, 0 };
				H.push_back(P);
				Figures(Held, m_SPB, H);
			}
			break;
		}
		default: {
			// The close: its last note from the bar's start or its middle, held up to the rest
			const int Stop = m_SPB - B.RestSteps;
			static const int EARLY[3] = { 55, 35, 25 };	// percent: the last note from the bar's start
			int Final = 0;
			const bool AfterPeak = b > 0 && m_Plan[static_cast<size_t>(b) - 1].Role == BAR_CLIMB;
			if (Half > 0 && Half + m_SPBeat <= Stop && (AfterPeak || !m_R.Chance(EARLY[std::max(0, std::min(2, m_Q.Kind))])))
				Final = Half;
			if (Final > 0)
				Figures(0, Final, H, AfterPeak && m_Calm);
			const Hit F = { Final, Stop - Final, MODEL_GOAL, 0 };
			H.push_back(F);
			break;
		}
		}
		// A calm song's copies of the motif strike on the beats only (a note between them held on from the one before)
		if (m_Calm && (B.Role == BAR_MOTIF || B.Role == BAR_SEQUENCE || B.Role == BAR_VARIED || B.Role == BAR_ANSWER) && H.size() > 1) {
			std::vector<Hit> Beats;
			for (size_t i = 0; i < H.size(); ++i) {
				if (!Beats.empty() && H[i].Step % m_SPBeat != 0 && Beats.back().Step + Beats.back().Steps == H[i].Step) {
					Beats.back().Steps += H[i].Steps;
					continue;
				}
				Beats.push_back(H[i]);
			}
			H = Beats;
		}
		if (B.Comma && B.Role != BAR_CLIMB)
			HoldAtEnd(H, std::min(m_SPB, 2 * m_SPBeat));
		if (B.End && B.Role != BAR_CLOSE && B.Role != BAR_HALF)
			RestAtEnd(H, B.RestSteps);
		return H;
	}

	// The high points and the motif's moves

	// Each section's high point: a chord tone above the motif's highest note, on the climb's held note: a step or two
	// above it on a calm song, a third or a fourth on a lively one, more on an action track
	void ChoosePeaks()
	{
		m_Peak.assign(m_SectionFirst.size(), -1);
		for (size_t b = 0; b < m_Plan.size(); ++b) {
			if (!m_Plan[b].Peak)
				continue;
			const int Section = m_Plan[b].Section;
			int At = -1, Length = 0;
			for (size_t i = 0; i < m_Hits[b].size(); ++i)
				if (m_Hits[b][i].Model == MODEL_PEAK) {
					At = static_cast<int>(b) * m_SPB + m_Hits[b][i].Step;
					Length = m_Hits[b][i].Steps;
				}
			if (At < 0)
				continue;
			static const int RISE[3] = { 3, 4, 6 };	// semitones over the motif's top, by kind
			const int Wanted = RISE[std::max(0, std::min(2, m_Q.Kind))];
			const std::vector<const Chord *> Chords = StrongChords(At, Length);
			int Best = -1;
			double BestCost = NEVER;
			for (int Pass = 0; Pass < 2 && Best < 0; ++Pass)
				for (int p = m_MotifMax + 1; p <= std::min(m_Q.High, m_MotifMax + 12); ++p) {
					if (!Allowed(p, At, Length) || (Pass == 0 && !FitsChords(p, Chords)) || Rubs(p, At, Length))
						continue;
					const double Cost = std::abs(p - m_MotifMax - Wanted) + (p - m_MotifMax < 2 ? 3.0 : 0.0) +
						0.6 * (Mix(m_Seed, static_cast<uint32_t>(p)) % 1000) / 1000.0;
					if (Cost < BestCost) {
						BestCost = Cost;
						Best = p;
					}
				}
			m_Peak[static_cast<size_t>(Section)] = Best;
		}
	}

	// The highest a section's notes go but for its high point
	int Ceiling(int Section) const
	{
		const int Peak = m_Peak[static_cast<size_t>(Section)];
		return Peak >= 0 ? Peak - 1 : std::min(m_Q.High, m_MotifMax);
	}

	// How badly the motif's notes of a bar fit at bar Bar moved by Steps of the scale (a run's second time through its
	// opening by Steps + Again): off the chord on a strong step, against another part, outside the key or the section's
	// notes, a leap of the motif's turned into a tritone
	double FitCost(int Bar, const std::vector<Hit> &Hits, int Steps, int Again = 0, int From = -1) const
	{
		const int Ceil = Ceiling(m_Plan[static_cast<size_t>(Bar)].Section);
		double Cost = 0.0;
		int Before = From;
		for (size_t i = 0; i < Hits.size(); ++i) {
			if (Hits[i].Model < 0) {
				Before = -1;
				continue;
			}
			const GrowNote &N = m_Q.Motif[static_cast<size_t>(Hits[i].Model)];
			const int Pitch = m_Ladder.Shift(N.Pitch, Steps + Hits[i].Fragment * Again), Step = Bar * m_SPB + Hits[i].Step;
			if (Before >= 0 && (std::abs(Pitch - Before) == 6 || std::abs(Pitch - Before) > 12))
				Cost += 2.5;
			if (Before >= 0 && Before == From)
				Cost += 0.3 * std::max(0, std::abs(Pitch - From) - 4);	// from where the line is
			Before = Pitch;
			if (!FitsChords(Pitch, StrongChords(Step, Hits[i].Steps)))
				Cost += 3.0;
			if (Rubs(Pitch, Step, Hits[i].Steps))
				Cost += 6.0;
			if (Avoided(Pitch, Step, Hits[i].Steps))
				Cost += 2.0;
			if (Steps != 0 && !Allowed(Pitch, Step, Hits[i].Steps))
				Cost += 4.0;
			if (Pitch > Ceil || Pitch < m_Low)
				Cost += 8.0;
		}
		return Cost;
	}

	// The scale's steps a bar moves the motif: the way its plan goes, as near as fits (a run's second time through its
	// opening a step further that way)
	int BestShift(int Bar, const std::vector<Hit> &Hits, int Direction, bool Any, int Again = 0, int From = -1) const
	{
		int Best = 0;
		double BestCost = NEVER;
		for (int k = -3; k <= 4; ++k) {
			if (k == 0 && !Any)
				continue;
			double Cost = FitCost(Bar, Hits, k, Again, From) + 0.35 * std::abs(k) + (std::abs(k) >= 3 ? 0.6 : 0.0);
			if (!Any && (k > 0) != (Direction > 0))
				Cost += 1.5;
			Cost += 0.3 * (Mix(m_Seed + static_cast<uint32_t>(Bar) * 977u, static_cast<uint32_t>(k + 8)) % 1000) / 1000.0;
			if (Cost < BestCost) {
				BestCost = Cost;
				Best = k;
			}
		}
		return Best;
	}

	// The motif's note a bar's last note follows, moved as the bar moves it; -1 when the bar's notes aren't the motif's
	int LastModel(size_t b) const
	{
		const BarPlan &B = m_Plan[b];
		if (B.Role != BAR_MOTIF && B.Role != BAR_SEQUENCE && B.Role != BAR_RUN)
			return -1;
		for (size_t i = m_Hits[b].size(); i-- > 0; )
			if (m_Hits[b][i].Model >= 0)
				return m_Ladder.Shift(m_Q.Motif[static_cast<size_t>(m_Hits[b][i].Model)].Pitch, B.Shift +
					(B.Role == BAR_RUN ? m_Hits[b][i].Fragment * B.Direction : 0));
		return -1;
	}

	// How well the motif fits where it's first stated (its own notes off the chord there, a passing note on beat 3, count
	// as fitting): a later statement as written fits at least that well
	double StatementCost(int Bar) const
	{
		double Cost = FitCost(Bar, MotifRhythm(0), 0);
		if (m_MotifBars > 1 && Bar + 1 < static_cast<int>(m_Plan.size()))
			Cost += FitCost(Bar + 1, MotifRhythm(1), 0);
		return Cost;
	}

	void ChooseShifts()
	{
		const double First = StatementCost(0);
		for (size_t b = 0; b < m_Plan.size(); ++b) {
			BarPlan &B = m_Plan[b];
			const int Bar = static_cast<int>(b);
			const int From = b > 0 ? LastModel(b - 1) : -1;	// where the line is, when the bar before follows the motif
			if (B.Role == BAR_MOTIF) {
				if (B.MotifBar == 1 && b > 0 && m_Plan[b - 1].Role == BAR_MOTIF) {
					B.Exact = m_Plan[b - 1].Exact;
					B.Shift = m_Plan[b - 1].Shift;
					continue;
				}
				// As written where it fits as well as the first time (always then); else moved to fit
				double Cost = FitCost(Bar, MotifRhythm(B.MotifBar), 0);
				const bool Pair = m_MotifBars > 1 && b + 1 < m_Plan.size() && m_Plan[b + 1].Role == BAR_MOTIF && m_Plan[b + 1].MotifBar == 1;
				if (Pair)
					Cost += FitCost(Bar + 1, MotifRhythm(1), 0);
				const double Before = Pair || m_MotifBars == 1 ? First : FitCost(0, MotifRhythm(0), 0);
				B.Exact = b == 0 || Cost < 0.5 || Cost <= Before + 0.01;
				B.Shift = B.Exact ? 0 : BestShift(Bar, m_Hits[b], 1, true, 0, From);
				if (!B.Exact && B.Shift == 0)
					B.Exact = true;
			}
			else if (B.Role == BAR_SEQUENCE || B.Role == BAR_RUN) {
				if (B.Role == BAR_SEQUENCE && B.MotifBar == 1 && b > 0 && m_Plan[b - 1].Role == BAR_SEQUENCE) {
					B.Shift = m_Plan[b - 1].Shift;
					continue;
				}
				B.Shift = BestShift(Bar, m_Hits[b], B.Direction, false, B.Role == BAR_RUN ? B.Direction : 0, From);
			}
			else if (B.Role == BAR_VARIED) {
				std::vector<Hit> Head;
				for (size_t i = 0; i < m_Hits[b].size(); ++i)
					if (m_Hits[b][i].Step < m_SPB / 2)
						Head.push_back(m_Hits[b][i]);
				B.Shift = FitCost(Bar, Head, 0) < 0.5 ? 0 : BestShift(Bar, Head, m_MotifWay, true, 0, From);
			}
		}
	}

	// The notes

	void MakeSlots()
	{
		m_Slots.clear();
		for (size_t b = 0; b < m_Plan.size(); ++b) {
			const BarPlan &B = m_Plan[b];
			const int First = static_cast<int>(b) * m_SPB;
			if (B.Role == BAR_MOTIF && B.Exact) {
				// The motif as written; a two-bar one's note held over its bar line stays one note
				const bool Both = B.MotifBar == 0 && b + 1 < m_Plan.size() && m_Plan[b + 1].Role == BAR_MOTIF &&
					m_Plan[b + 1].MotifBar == 1 && m_Plan[b + 1].Exact;
				const int From = B.MotifBar * m_SPB, To = From + m_SPB;
				for (size_t j = 0; j < m_Q.Motif.size(); ++j) {
					const GrowNote &N = m_Q.Motif[j];
					if (N.Step < From || N.Step >= To)
						continue;
					Slot S;
					S.Step = First + N.Step - From;
					S.Steps = Both ? N.Steps : std::min(N.Steps, To - N.Step);
					S.Bar = static_cast<int>(b);
					S.Model = static_cast<int>(j);
					S.Fixed = true;
					S.Pitch = N.Pitch;
					m_Slots.push_back(S);
				}
				continue;
			}
			for (size_t i = 0; i < m_Hits[b].size(); ++i) {
				const Hit &H = m_Hits[b][i];
				Slot S;
				S.Step = First + H.Step;
				S.Steps = H.Steps;
				S.Bar = static_cast<int>(b);
				S.Model = H.Model;
				S.Fragment = H.Fragment;
				m_Slots.push_back(S);
			}
		}
		std::sort(m_Slots.begin(), m_Slots.end(), [](const Slot &a, const Slot &b) { return a.Step < b.Step; });
		// Nothing overlaps (a note held over from a bar before ends where the next starts)
		for (size_t i = 0; i + 1 < m_Slots.size(); ++i)
			m_Slots[i].Steps = std::max(1, std::min(m_Slots[i].Steps, m_Slots[i + 1].Step - m_Slots[i].Step));
	}

	// Whether a note could sound over Step to Step + Steps: its own pitch (a fixed one, the high point) or some pitch of
	// its section fits the chords on the strong steps it covers
	bool CanCover(const Slot &S, int Steps) const
	{
		const std::vector<const Chord *> Chords = StrongChords(S.Step, Steps);
		if (S.Fixed)
			return FitsChords(S.Pitch, Chords);
		const int Section = m_Plan[static_cast<size_t>(S.Bar)].Section;
		if (S.Model == MODEL_PEAK && m_Peak[static_cast<size_t>(Section)] >= 0)
			return FitsChords(m_Peak[static_cast<size_t>(Section)], Chords);
		for (int p = m_Low; p <= Ceiling(Section); ++p)
			if (Allowed(p, S.Step, Steps) && FitsChords(p, Chords))
				return true;
		return false;
	}

	// Ties across the bar lines inside a phrase: a held note running on into the next bar's first beat, or the next
	// bar's first note coming a step early (an anticipation)
	void Tie()
	{
		static const int HOLD_OVER[3] = { 35, 45, 25 }, EARLY[3] = { 0, 35, 50 };	// percent, by kind
		const int Kind = std::max(0, std::min(2, m_Q.Kind));
		for (int b = 0; b + 1 < static_cast<int>(m_Plan.size()); ++b) {
			const BarPlan &Here = m_Plan[static_cast<size_t>(b)], &Next = m_Plan[static_cast<size_t>(b) + 1];
			if (Here.End || (Here.Role == BAR_MOTIF && Here.Exact) || (Next.Role == BAR_MOTIF && Next.Exact) || Here.Section != Next.Section)
				continue;
			const int Line = (b + 1) * m_SPB;
			int Last = -1, First = -1;
			for (size_t i = 0; i < m_Slots.size(); ++i) {
				if (m_Slots[i].Bar == b)
					Last = static_cast<int>(i);
				if (m_Slots[i].Bar == b + 1 && First < 0)
					First = static_cast<int>(i);
			}
			if (Last < 0 || First != Last + 1)
				continue;
			Slot &L = m_Slots[static_cast<size_t>(Last)];
			Slot &F = m_Slots[static_cast<size_t>(First)];
			if (L.Step + L.Steps != Line || F.Step != Line || F.Model == MODEL_GOAL)
				continue;
			const bool Holds = Here.Comma && (Next.Role == BAR_RUN || Next.Role == BAR_CLIMB || Next.Role == BAR_CLOSE ||
				Next.Role == BAR_HALF || Next.Role == BAR_ANSWER);
			if (Holds && F.Model != MODEL_PEAK && m_R.Chance(HOLD_OVER[Kind])) {
				// The held note runs on through the next bar's first beat; the notes there go, or start after it
				const int Until = Line + m_SPBeat;
				if (!CanCover(L, Until - L.Step))
					continue;
				bool Keeps = true;
				for (size_t i = static_cast<size_t>(First); i < m_Slots.size() && m_Slots[i].Step < Until; ++i)
					Keeps = Keeps && m_Slots[i].Model != MODEL_PEAK && m_Slots[i].Model != MODEL_GOAL &&
						(m_Slots[i].Bar == b + 1);
				if (!Keeps)
					continue;
				std::vector<Slot> Kept;
				for (size_t i = 0; i < m_Slots.size(); ++i) {
					Slot S = m_Slots[i];
					if (static_cast<int>(i) >= First && S.Step < Until) {
						if (S.Step + S.Steps <= Until)
							continue;
						S.Steps -= Until - S.Step;
						S.Step = Until;
					}
					Kept.push_back(S);
				}
				Kept[static_cast<size_t>(Last)].Steps = Until - Kept[static_cast<size_t>(Last)].Step;
				if (static_cast<size_t>(Last) + 1 >= Kept.size() || Kept[static_cast<size_t>(Last) + 1].Bar != b + 1)
					continue;	// the next bar kept no note of its own
				m_Slots = Kept;
			}
			else if (!Here.Comma && L.Steps >= 2 && !F.Fixed && F.Model != MODEL_PEAK && m_R.Chance(EARLY[Kind])) {
				Slot Early = F;
				Early.Step -= 1;
				Early.Steps += 1;
				Early.Early = true;
				if (!CanCover(Early, Early.Steps))
					continue;
				L.Steps -= 1;
				F = Early;
			}
		}
	}

	// Where each note heads: the motif's notes moved as its bar moves them, the climb up to the high point, the close down
	// to its goal, the answer the other way from the motif
	void Shape()
	{
		const int Tonic = NearestClass(m_Q.K.Tonic, m_Centre - 2), Dominant = NearestClass(m_Q.K.Tonic + 7, m_Centre);
		int Level = m_Q.Motif.back().Pitch;
		for (size_t b = 0; b < m_Plan.size(); ++b) {
			const BarPlan &B = m_Plan[b];
			std::vector<size_t> Mine;
			for (size_t i = 0; i < m_Slots.size(); ++i)
				if (m_Slots[i].Bar == static_cast<int>(b))
					Mine.push_back(i);
			const int Count = static_cast<int>(Mine.size());
			const int Section = B.Section, Peak = m_Peak[static_cast<size_t>(Section)];
			// An answer starts on a chord tone a little past where the line is, the way the motif went, and turns back
			int Answer = -1, Reference = -1;
			if (B.Role == BAR_ANSWER && Count > 0) {
				const Slot &S = m_Slots[Mine[0]];
				for (size_t j = 0; j < m_Q.Motif.size() && Reference < 0; ++j)
					if (m_Q.Motif[j].Step + m_Q.Motif[j].Steps > B.MotifBar * m_SPB)
						Reference = m_Q.Motif[j].Pitch;
				const std::vector<const Chord *> Chords = StrongChords(S.Step, std::max(S.Steps, 1));
				const Chord *pChord = Chords.empty() ? ChordAt(S.Step) : Chords.front();
				const int Near = Level + m_MotifWay * 3;
				for (int p = m_Low; p <= Ceiling(Section); ++p)
					if (Allowed(p, S.Step, S.Steps) && (pChord == 0 || pChord->HasPitchClass(ClassOf(p))) &&
						(Answer < 0 || std::abs(p - Near) < std::abs(Answer - Near)))
						Answer = p;
			}
			for (int n = 0; n < Count; ++n) {
				Slot &S = m_Slots[Mine[static_cast<size_t>(n)]];
				const bool Head = (S.Step % m_SPB) < m_SPB / 2;
				switch (B.Role) {
				case BAR_MOTIF:
				case BAR_SEQUENCE:
					if (!S.Fixed && S.Model >= 0) {
						S.ModelPitch = m_Ladder.Shift(m_Q.Motif[static_cast<size_t>(S.Model)].Pitch, B.Shift);
						S.ModelWeight = 2.0;
					}
					break;
				case BAR_VARIED:
					if (S.Model >= 0 && Head) {
						S.ModelPitch = m_Ladder.Shift(m_Q.Motif[static_cast<size_t>(S.Model)].Pitch, B.Shift);
						S.ModelWeight = 2.2;
					}
					else {
						S.Target = Level + (m_Centre - Level) * (n + 1) / std::max(1, Count);
						S.TargetWeight = 0.25;
					}
					break;
				case BAR_ANSWER:
					if (S.Model >= 0 && Answer >= 0) {
						// The motif upside down, from a chord tone near where the line is
						const int From = m_Ladder.Rung(m_Q.Motif[static_cast<size_t>(S.Model)].Pitch) - m_Ladder.Rung(Reference);
						S.ModelPitch = m_Ladder.Shift(Answer, -From);
						S.ModelWeight = 1.2;
					}
					else {
						const int To = m_Centre - m_MotifWay * 2;
						S.Target = Level + (To - Level) * (n + 1) / std::max(1, Count);
						S.TargetWeight = 0.3;
					}
					break;
				case BAR_RUN:
					if (S.Model >= 0) {
						S.ModelPitch = m_Ladder.Shift(m_Q.Motif[static_cast<size_t>(S.Model)].Pitch, B.Shift + S.Fragment * B.Direction);
						S.ModelWeight = 1.6;
					}
					else {
						S.Target = Level + B.Direction * 2 * (n + 1);
						S.TargetWeight = 0.25;
					}
					break;
				case BAR_CLIMB:
					if (S.Model != MODEL_PEAK) {
						const int Top = (Peak >= 0 ? Peak : m_MotifMax + 2) - 2;
						const bool Before = n + 1 < Count && m_Slots[Mine[static_cast<size_t>(n) + 1]].Model == MODEL_PEAK;
						S.Target = Before ? Top : Level + (Top - Level) * (n + 1) / std::max(1, Count);
						S.TargetWeight = Before ? 0.7 : 0.45;
					}
					break;
				default: {
					const int Goal = B.Role == BAR_HALF ? Dominant : Tonic;
					S.Target = S.Model == MODEL_GOAL ? Goal : Level + (Goal - Level) * (n + 1) / (Count + 1);
					S.TargetWeight = S.Model == MODEL_GOAL ? 0.25 : 0.35;
					break;
				}
				}
				if (n + 1 == Count && (B.Comma || B.End))
					S.Settles = true;
			}
			// Where the line is at the bar's end, for the next bar's heading
			for (int n = Count - 1; n >= 0; --n) {
				const Slot &S = m_Slots[Mine[static_cast<size_t>(n)]];
				const int At = S.Fixed ? S.Pitch : S.Model == MODEL_PEAK && Peak >= 0 ? Peak : S.ModelPitch >= 0 ? S.ModelPitch :
					S.Target >= 0 ? S.Target : -1;
				if (At >= 0) {
					Level = At;
					break;
				}
			}
		}
	}

	// A pitch class's note nearest Near, inside the melody's notes
	int NearestClass(int Class, int Near) const
	{
		int Best = -1;
		for (int p = m_Low; p <= std::max(m_Low, std::min(m_Q.High, m_MotifMax + 5)); ++p)
			if (ClassOf(p) == ClassOf(Class) && (Best < 0 || std::abs(p - Near) < std::abs(Best - Near)))
				Best = p;
		return Best >= 0 ? Best : Near;
	}

	// The pitches

	// How a chord's note sits on a strong beat: its third best, then the fifth and the root; a seventh, ninth or
	// suspension a little worse
	double ChordToneCost(int Pitch, const Chord &C) const
	{
		const int Above = ClassOf(Pitch - C.Root);
		if (Above == 0)
			return 0.2;
		if (C.Third() == Above)
			return 0.0;
		if (C.Fifth() == Above)
			return 0.12;
		return 0.5;
	}

	double GoalCost(int Pitch, const Slot &S) const
	{
		const int Relative = ClassOf(Pitch - m_Q.K.Tonic);
		const Chord *pChord = ChordAt(S.Step);
		const int Above = pChord != 0 ? ClassOf(Pitch - pChord->Root) : -1;
		const BarPlan &B = m_Plan[static_cast<size_t>(S.Bar)];
		if (B.Role == BAR_HALF) {
			if (Relative == 7)
				return 0.0;
			if (Relative == ModeSteps(m_Q.K.Mode)[1])
				return 0.3;
			if (Relative == 0)
				return 2.0;
			return pChord != 0 && pChord->Third() == Above ? 0.5 : 0.8;
		}
		const bool Final = S.Bar + 1 == static_cast<int>(m_Plan.size());
		if (Relative == 0)
			return 0.0;
		if (pChord != 0 && pChord->HasPitchClass(m_Q.K.Tonic))
			return Final ? 3.0 : 1.2;
		if (Above == 0)
			return Final ? 1.2 : 0.6;
		if (pChord != 0 && pChord->Third() == Above)
			return Final ? 1.5 : 0.5;
		return Final ? 2.0 : 0.9;
	}

	std::vector<Candidate> Candidates(size_t Index) const
	{
		const Slot &S = m_Slots[Index];
		std::vector<Candidate> Out;
		if (S.Fixed) {
			const Candidate C = { S.Pitch, 0.0 };
			Out.push_back(C);
			return Out;
		}
		const int Section = m_Plan[static_cast<size_t>(S.Bar)].Section, Peak = m_Peak[static_cast<size_t>(Section)];
		if (S.Model == MODEL_PEAK && Peak >= 0) {
			const Candidate C = { Peak, 0.0 };
			Out.push_back(C);
			return Out;
		}
		const int High = S.Model == MODEL_PEAK ? std::min(m_Q.High, m_MotifMax + 4) : Ceiling(Section);
		const std::vector<const Chord *> Strong = StrongChords(S.Step, S.Steps);
		const Chord *pOnset = ChordAt(S.Step);
		const int Onset = Strength(S.Step);
		for (int Pass = 0; Pass < 3 && Out.empty(); ++Pass)
			for (int p = m_Low; p <= High; ++p) {
				if (Pass < 2 && !Allowed(p, S.Step, S.Steps))
					continue;
				double Cost = 0.0;
				if (!FitsChords(p, Strong)) {
					if (Pass == 0)
						continue;
					Cost += 4.0;
				}
				if (!Strong.empty())
					Cost += ChordToneCost(p, *Strong.front());
				else if (Onset == 1 && pOnset != 0 && !pOnset->HasPitchClass(ClassOf(p)))
					Cost += m_Calm ? 0.8 : 0.5;
				if (Rubs(p, S.Step, S.Steps))
					Cost += 40.0;
				if (Avoided(p, S.Step, S.Steps))
					Cost += 5.0;
				if (S.Settles && pOnset != 0 && ChordToneCost(p, *pOnset) >= 0.5)
					Cost += 0.4;
				if (S.Model == MODEL_GOAL)
					Cost += GoalCost(p, S);
				if (S.ModelPitch >= 0)
					Cost += S.ModelWeight * std::abs(m_Ladder.Rung(p) - m_Ladder.Rung(S.ModelPitch)) + (p != S.ModelPitch &&
						m_Ladder.Rung(p) == m_Ladder.Rung(S.ModelPitch) ? 0.5 : 0.0);
				if (S.Target >= 0)
					Cost += S.TargetWeight * std::abs(p - S.Target);
				Cost += 0.12 * std::max(0, std::abs(p - m_Centre) - 7);
				Cost += 0.3 * (Mix(m_Seed ^ static_cast<uint32_t>(Index * 131u), static_cast<uint32_t>(p)) % 1000) / 1000.0;
				const Candidate C = { p, Cost };
				Out.push_back(C);
			}
		if (Out.empty()) {
			const Candidate C = { std::max(m_Low, std::min(High, m_Centre)), 0.0 };
			Out.push_back(C);
		}
		return Out;
	}

	// A step from one note to the next: steps are free, leaps cost more the wider they are (a calm song's most), a
	// tritone and a harmonic minor's augmented second worst; less after a rest, where a phrase starts afresh
	double Pair(const Slot &To, int a, int b) const
	{
		static const double COST[3][13] = {
			{ 1.1, 0.0, 0.0, 1.4, 1.6, 2.6, 7.0, 4.0, 6.0, 6.0, 8.0, 9.0, 7.0 },
			{ 1.5, 0.0, 0.0, 0.5, 0.6, 1.1, 5.0, 1.7, 2.8, 3.0, 4.5, 5.5, 3.6 },
			{ 0.5, 0.1, 0.0, 0.4, 0.4, 0.7, 3.5, 0.9, 2.2, 2.4, 3.5, 4.5, 1.6 },
		};
		const int d = std::abs(b - a);
		double Cost = d <= 12 ? COST[std::max(0, std::min(2, m_Q.Kind))][d] : 14.0 + d - 12;
		if (d == 3) {
			const int x = ClassOf(a - m_Q.K.Tonic), y = ClassOf(b - m_Q.K.Tonic);
			if ((x == 8 && y == 11) || (x == 11 && y == 8))
				Cost += 3.0;
		}
		if (To.Early && d == 0)
			Cost += 3.0;	// an anticipation of the note before it is no anticipation
		return To.AfterRest ? 0.35 * Cost : Cost;
	}

	// Three notes: a leap goes on the other way by step (not on in the same direction; a chord's notes one after another,
	// thirds, may), and a note isn't struck three times
	double Triple(const Slot &Middle, const Slot &Last, int a, int b, int c) const
	{
		if (Middle.AfterRest || Last.AfterRest)
			return 0.0;
		const int d1 = b - a, d2 = c - b;
		double Cost = 0.0;
		if (std::abs(d1) >= 5 && d2 != 0) {
			if ((d1 > 0) == (d2 > 0))
				Cost += m_Calm ? 3.0 : 1.6;
			else if (std::abs(d2) <= 2)
				Cost -= 0.4;
		}
		if (d1 == 0 && d2 == 0)
			Cost += m_Calm ? 0.4 : m_Q.Kind == GROW_ACTION ? 0.2 : 1.2;
		if (std::abs(d1) >= 3 && std::abs(d2) >= 3 && (d1 > 0) == (d2 > 0) && std::abs(d1) + std::abs(d2) > 8)
			Cost += m_Q.Kind == GROW_ACTION ? 0.3 : 0.8;
		return Cost;
	}

	// The cheapest line through every note's candidates, two notes back at a time
	void Choose()
	{
		const size_t N = m_Slots.size();
		if (N == 0)
			return;
		std::vector<std::vector<Candidate> > C(N);
		for (size_t i = 0; i < N; ++i)
			C[i] = Candidates(i);
		if (N == 1) {
			size_t Best = 0;
			for (size_t k = 1; k < C[0].size(); ++k)
				if (C[0][k].Cost < C[0][Best].Cost)
					Best = k;
			m_Slots[0].Pitch = C[0][Best].Pitch;
			return;
		}
		// Best[i][j * |C[i]| + k]: the cheapest line to note i - 1 on its candidate j and note i on its candidate k
		std::vector<std::vector<double> > Best(N);
		std::vector<std::vector<int> > Back(N);
		Best[1].assign(C[0].size() * C[1].size(), NEVER);
		for (size_t j = 0; j < C[0].size(); ++j)
			for (size_t k = 0; k < C[1].size(); ++k)
				Best[1][j * C[1].size() + k] = C[0][j].Cost + C[1][k].Cost + Pair(m_Slots[1], C[0][j].Pitch, C[1][k].Pitch);
		for (size_t i = 2; i < N; ++i) {
			const size_t A = C[i - 2].size(), B = C[i - 1].size(), K = C[i].size();
			Best[i].assign(B * K, NEVER);
			Back[i].assign(B * K, 0);
			for (size_t j = 0; j < B; ++j)
				for (size_t k = 0; k < K; ++k) {
					double Least = NEVER;
					int From = 0;
					for (size_t h = 0; h < A; ++h) {
						const double Before = Best[i - 1][h * B + j];
						if (Before >= NEVER)
							continue;
						const double Cost = Before + Triple(m_Slots[i - 1], m_Slots[i], C[i - 2][h].Pitch, C[i - 1][j].Pitch, C[i][k].Pitch);
						if (Cost < Least) {
							Least = Cost;
							From = static_cast<int>(h);
						}
					}
					Best[i][j * K + k] = Least + Pair(m_Slots[i], C[i - 1][j].Pitch, C[i][k].Pitch) + C[i][k].Cost;
					Back[i][j * K + k] = From;
				}
		}
		const size_t K = C[N - 1].size();
		size_t BestAt = 0;
		for (size_t x = 1; x < Best[N - 1].size(); ++x)
			if (Best[N - 1][x] < Best[N - 1][BestAt])
				BestAt = x;
		size_t j = BestAt / K, k = BestAt % K;
		for (size_t i = N - 1; i >= 2; --i) {
			m_Slots[i].Pitch = C[i][k].Pitch;
			const size_t h = static_cast<size_t>(Back[i][j * C[i].size() + k]);
			k = j;
			j = h;
		}
		m_Slots[1].Pitch = C[1][k].Pitch;
		m_Slots[0].Pitch = C[0][j].Pitch;
	}

	// A calm song's line holds a note on rather than striking it again between the beats (the motif's own notes stay)
	void HoldRepeats()
	{
		std::vector<Slot> Out;
		for (size_t i = 0; i < m_Slots.size(); ++i) {
			const Slot &S = m_Slots[i];
			if (!Out.empty() && !S.Fixed && !Out.back().Fixed && S.Pitch == Out.back().Pitch && Strength(S.Step) == 0 &&
				Out.back().Step + Out.back().Steps == S.Step && S.Model != MODEL_PEAK && S.Model != MODEL_GOAL) {
				Out.back().Steps += S.Steps;
				continue;
			}
			Out.push_back(S);
		}
		m_Slots = Out;
	}

	// The words

	std::string Name(int Pitch) const
	{
		std::string Text = PitchName(Pitch);
		if (Text.size() == 3 && Text[1] == '-')
			Text.erase(1, 1);
		return Text;
	}

	std::string BarsText(const std::vector<int> &Bars) const
	{
		std::string Out = Bars.size() == 1 ? "bar " : "bars ";
		for (size_t i = 0; i < Bars.size(); ++i)
			Out += (i == 0 ? "" : i + 1 == Bars.size() ? " and " : ", ") + std::to_string(m_Q.FirstBar + Bars[i]);
		return Out;
	}

	static const char *StepsText(int Steps)
	{
		static const char *const NAMES[] = { "", "a step", "a third", "a fourth", "a fifth" };
		return NAMES[std::min(4, std::abs(Steps))];
	}

	// What a bar does, in words ("" for a two-bar role's second bar)
	std::string BarWords(int Bar, bool &HookSaid) const
	{
		const BarPlan &B = m_Plan[static_cast<size_t>(Bar)];
		if (B.MotifBar != 0 && (B.Role == BAR_MOTIF || B.Role == BAR_SEQUENCE || B.Role == BAR_ANSWER))
			return std::string();
		const std::string Way = B.Shift > 0 ? " higher" : " lower";
		switch (B.Role) {
		case BAR_MOTIF:
		case BAR_SEQUENCE:
			if (B.Role == BAR_MOTIF && B.Exact) {
				const bool Again = HookSaid;
				HookSaid = true;
				return Again ? "the hook again" : "the hook";
			}
			return "the hook " + std::string(StepsText(B.Shift)) + Way;
		case BAR_VARIED:
			return B.Shift == 0 ? "the hook with a new ending" : "the hook " + std::string(StepsText(B.Shift)) + Way + " with a new ending";
		case BAR_ANSWER:
			return "an answer";
		case BAR_RUN:
			return B.Direction > 0 ? "its opening rising" : "its opening falling";
		case BAR_CLIMB: {
			const int Peak = m_Peak[static_cast<size_t>(B.Section)];
			return Peak >= 0 ? "up to the high point " + Name(Peak) : "a climb";
		}
		case BAR_HALF:
			return "a half close on " + Name(LastPitchIn(Bar));
		default: {
			const int Pitch = LastPitchIn(Bar);
			return (ClassOf(Pitch - m_Q.K.Tonic) == 0 ? "home on " : "a close on ") + Name(Pitch);
		}
		}
	}

	void Describe(GrownVersion &Out)
	{
		Out = GrownVersion();
		for (size_t i = 0; i < m_Slots.size(); ++i)
			Out.Notes.push_back(GrowNote(m_Slots[i].Step, m_Slots[i].Steps, m_Slots[i].Pitch));
		Out.Form = FORM_NAMES[m_Form];
		GrowBreaths(Out.Notes, m_SPB, m_SPBeat, m_Q.Bars, m_Q.SpanOffset, Out.Spans, Out.Breaths);
		for (size_t b = 0; b < m_Plan.size(); ++b)
			if (m_Plan[b].Role == BAR_MOTIF && m_Plan[b].Exact && m_Plan[b].MotifBar == 0)
				Out.Hooks.push_back(static_cast<int>(b));
		// Each section's high point, where it is
		for (int s = 0; s < Sections(); ++s) {
			const int From = m_SectionFirst[static_cast<size_t>(s)] * m_SPB;
			const int To = (s + 1 < Sections() ? m_SectionFirst[static_cast<size_t>(s) + 1] : m_Q.Bars) * m_SPB;
			int Top = -1, Bar = 0;
			for (size_t i = 0; i < m_Slots.size(); ++i)
				if (m_Slots[i].Step >= From && m_Slots[i].Step < To && m_Slots[i].Pitch > Top) {
					Top = m_Slots[i].Pitch;
					Bar = m_Slots[i].Step / m_SPB;
				}
			Out.Peaks.push_back(Top);
			Out.PeakBars.push_back(Bar);
		}
		// Phrase by phrase, from one rest at a bar's end to the next
		std::string Text = Out.Form;
		Text[0] = static_cast<char>(Text[0] - 'a' + 'A');
		Text += ": ";
		bool HookSaid = false;
		int Start = 0;
		for (size_t b = 0; b < m_Plan.size(); ++b) {
			if (!m_Plan[b].End && b + 1 < m_Plan.size())
				continue;
			const int Stop = static_cast<int>(b);
			Out.Phrases.push_back(Stop + 1 - Start);
			std::vector<std::string> Bits;
			for (int x = Start; x <= Stop; ++x) {
				const std::string Words = BarWords(x, HookSaid);
				if (!Words.empty())
					Bits.push_back(Words);
			}
			Text += (Start == 0 ? "" : "; ") + (Stop == Start ? "bar " + std::to_string(m_Q.FirstBar + Start) :
				"bars " + std::to_string(m_Q.FirstBar + Start) + "-" + std::to_string(m_Q.FirstBar + Stop)) + " ";
			for (size_t i = 0; i < Bits.size(); ++i)
				Text += (i == 0 ? "" : i + 1 == Bits.size() ? " and " : ", ") + Bits[i];
			Start = Stop + 1;
		}
		std::vector<int> Lengths(Out.Phrases);
		std::sort(Lengths.begin(), Lengths.end());
		Lengths.erase(std::unique(Lengths.begin(), Lengths.end()), Lengths.end());
		Text += ". Phrases of ";
		for (size_t i = Lengths.size(); i-- > 0; )
			Text += std::to_string(Lengths[i]) + (i == 0 ? "" : i == 1 ? " and " : ", ");
		Text += Lengths.size() == 1 && Lengths[0] == 1 ? " bar" : " bars";
		std::string Peaks;
		for (int s = 0; s < Sections(); ++s)
			if (m_Peak[static_cast<size_t>(s)] >= 0)
				Peaks += (Peaks.empty() ? "" : " and ") + Name(Out.Peaks[static_cast<size_t>(s)]) + " in bar " +
					std::to_string(m_Q.FirstBar + Out.PeakBars[static_cast<size_t>(s)]);
		if (!Peaks.empty())
			Text += std::string(Sections() > 1 ? ", the high points " : ", the high point ") + Peaks;
		Text += "; it breathes in " + std::to_string(Out.Breaths) + " of " + std::to_string(Out.Spans) + " two-bar spans.";
		Out.Shape = Text;
	}

	int LastPitchIn(int Bar) const
	{
		int Pitch = -1;
		for (size_t i = 0; i < m_Slots.size(); ++i)
			if (m_Slots[i].Step / m_SPB == Bar)
				Pitch = m_Slots[i].Pitch;
		return Pitch;
	}
};

bool SameNotes(const std::vector<GrowNote> &a, const std::vector<GrowNote> &b)
{
	if (a.size() != b.size())
		return false;
	for (size_t i = 0; i < a.size(); ++i)
		if (a[i].Step != b[i].Step || a[i].Steps != b[i].Steps || a[i].Pitch != b[i].Pitch)
			return false;
	return true;
}

}	// namespace

void GrowBreaths(const std::vector<GrowNote> &Notes, int StepsPerBar, int StepsPerBeat, int Bars, int SpanOffset, int &Spans,
	int &Breaths)
{
	Spans = Breaths = 0;
	const int Bar = std::max(1, StepsPerBar), Beat = std::max(1, StepsPerBeat), Total = Bars * Bar;
	if (Bars <= 0)
		return;
	auto SpanOf = [&](int Step) {
		const int b = std::max(0, std::min(Bars - 1, Step / Bar));
		return (b + SpanOffset) / 2;
	};
	std::set<int> Paused, Playing;
	for (size_t i = 0; i < Notes.size(); ++i) {
		const int Start = Notes[i].Step, Stop = Notes[i].Step + std::max(1, Notes[i].Steps);
		for (int Span = SpanOf(Start); Span <= SpanOf(Stop - 1); ++Span)
			Playing.insert(Span);
		if (Stop - Start >= 2 * Beat)
			Paused.insert(SpanOf(Start));
		const int Next = i + 1 < Notes.size() ? Notes[i + 1].Step : Total;
		if (Next - Stop >= Beat)
			Paused.insert(SpanOf(Stop));
	}
	// The spans that start inside the bars (review_arrangement counts a span where its first bar is)
	for (std::set<int>::const_iterator it = Playing.begin(); it != Playing.end(); ++it) {
		if (*it * 2 - SpanOffset < 0)
			continue;
		++Spans;
		if (Paused.count(*it) > 0)
			++Breaths;
	}
}

bool GrowMelody(const GrowRequest &Q, std::vector<GrownVersion> &Out, std::string &Error)
{
	Out.clear();
	if (Q.Motif.size() < 2) {
		Error = "A motif needs two notes or more.";
		return false;
	}
	if (Q.StepsPerBeat < 1 || Q.StepsPerBar < Q.StepsPerBeat || Q.StepsPerBar % Q.StepsPerBeat != 0 || Q.RowsPerStep < 1) {
		Error = "The bars don't hold whole beats of steps.";
		return false;
	}
	int Last = 0;
	for (size_t i = 0; i < Q.Motif.size(); ++i) {
		if (Q.Motif[i].Steps < 1 || Q.Motif[i].Step < 0 || (i > 0 && Q.Motif[i].Step < Q.Motif[i - 1].Step + Q.Motif[i - 1].Steps)) {
			Error = "The motif's notes overlap.";
			return false;
		}
		Last = std::max(Last, Q.Motif[i].Step + Q.Motif[i].Steps);
	}
	const int MotifBars = (Last + Q.StepsPerBar - 1) / Q.StepsPerBar;
	if (MotifBars > 2) {
		Error = "A motif is a bar or two long.";
		return false;
	}
	if (Q.Bars < 2 * MotifBars) {
		Error = MotifBars > 1 ? "A two-bar motif grows over four bars or more." : "A motif grows over two bars or more.";
		return false;
	}
	if (static_cast<int>(Q.StepChord.size()) < Q.Bars * Q.StepsPerBar) {
		Error = "The chords don't cover the bars.";
		return false;
	}
	uint32_t Hash = 0x811C9DC5u;
	for (size_t i = 0; i < Q.Motif.size(); ++i)
		Hash = Mix(Hash, static_cast<uint32_t>(Q.Motif[i].Pitch * 4096 + Q.Motif[i].Step * 64 + Q.Motif[i].Steps));
	Hash = Mix(Hash, static_cast<uint32_t>(Q.Bars * 16 + Q.Kind));
	const int Count = Q.Bars >= 4 ? 3 : 2;
	const int Variant = std::max(1, Q.Variant);
	for (int v = 0; v < Count; ++v) {
		const int Form = (v + Variant - 1) % FORM_COUNT;
		for (int Try = 0; Try < 4; ++Try) {
			const uint32_t Seed = Mix(Mix(Hash, static_cast<uint32_t>(Variant) * 7919u + static_cast<uint32_t>(v)), static_cast<uint32_t>(Try));
			Grower G(Q, Form, Seed);
			GrownVersion V;
			G.Grow(V);
			bool Same = false;
			for (size_t o = 0; o < Out.size(); ++o)
				Same = Same || SameNotes(Out[o].Notes, V.Notes);
			if (!Same || Try == 3) {
				Out.push_back(V);
				break;
			}
		}
	}
	return true;
}

}	// namespace ftapi

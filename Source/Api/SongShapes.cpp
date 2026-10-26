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

#include "SongShapes.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace ftapi {

namespace {

struct PartRow {
	const char *Name;
	int Bars;
	const char *Line;
};

struct ShapeRow {
	const char *Id;
	const char *Title;
	const char *About;
	const char *Kind;
	bool Loops;
	int LoopPart;
	const char *Words;		// the words that ask for it, a space between
	PartRow Parts[8];		// ended by a part without a name
};

// The forms the tools teach. A lively stage and a town as the recipes and
// the soundtrack make them: an intro of 4 bars that plays once, a loop of 36
// (A, A', B, the climax or A'', 8 bars each) and a break of 4 before the loop
// comes round; a calm night as the soundtrack's night songs were, its break
// between B and the last A (the calm recipe's). A boss with a short, tense intro and a 6-bar
// climax (the style guide's soundtrack topic: B as the climax, frames of 2
// bars so the 2-bar intro loops cleanly); a small place's short loop, a
// jingle that stops. Plain words: the user reads no notation.
const ShapeRow SHAPES[] = {
	{ "stage", "Lively stage", "A level to explore: it bounces, builds to a climax, breathes and loops.", "lively", true, 1,
		"stage stages lively level levels platformer platform explore exploring adventure jump",
		{ { "Intro", 4, "Plays once as the stage starts: the beat and the chords build up, without the tune." },
		{ "A", 8, "The tune, the part people hum." },
		{ "A'", 8, "The tune again, changed a little: a second line joins in and the drums get busier." },
		{ "B", 8, "Something different, on other chords, so A sounds fresh when it comes back." },
		{ "Climax", 8, "The high point: the tune at its biggest, with a harmony, a little louder than A." },
		{ "Break", 4, "A breath before the loop comes round: the tune and the drums rest." },
		{ 0, 0, 0 } } },
	{ "night", "Calm night", "A place to rest or a night scene: slow and soft, and the tune never presses.", "calm", true, 1,
		"night nights calm rest resting sleep sleeping lullaby peaceful relax relaxing evening dream quiet",
		{ { "Intro", 4, "The chords alone, softly, before the tune comes in. It plays once." },
		{ "A", 8, "The tune: few, slow notes, with rests between its phrases." },
		{ "A'", 8, "The tune again, deeper: an echo a beat behind it, or a second voice." },
		{ "B", 8, "Other chords, a little higher or darker: somewhere new before coming home." },
		{ "Break", 4, "The tune rests while one small sound plays (a far-off clock, a sigh) before it comes back." },
		{ "A''", 8, "The tune once more, softly answered, ending without coming to rest so the loop flows on." },
		{ 0, 0, 0 } } },
	{ "town", "Town", "A town, a village or a field: a friendly tune that can loop for a long time without tiring.", "general", true, 1,
		"town towns village city field fields map overworld general",
		{ { "Intro", 4, "Plays once as you arrive: the chords and a hint of the tune." },
		{ "A", 8, "The town's tune, easy to hum." },
		{ "A'", 8, "The tune again with something new: a second line, other drums." },
		{ "B", 8, "Another corner of town: other chords and another rhythm, so A sounds fresh after it." },
		{ "A''", 8, "The tune's last time round, and its fullest: the town's high point." },
		{ "Break", 4, "A short rest before the loop comes round, so the start sounds fresh again." },
		{ 0, 0, 0 } } },
	{ "boss", "Boss fight", "A boss or a chase: tense from the first bar, a theme that drives, a climax at the fight's peak.", "action",
		true, 1, "boss bosses battle battles fight fighting action chase combat danger enemy",
		{ { "Intro", 2, "A short, tense start: the fight is about to begin. It plays once." },
		{ "A", 8, "The fight's theme: a riff to remember, over drums that drive." },
		{ "A'", 8, "The theme again, pushed harder: busier drums, a harmony, the bass in octaves." },
		{ "Climax", 6, "The fight at its peak: other chords, the highest notes, every channel at full tilt." },
		{ "Break", 2, "Two bars to catch a breath before the fight goes round again." },
		{ 0, 0, 0 } } },
	{ "small", "Short loop", "A small place (a shop, a house, a menu): a short loop that stays pleasant heard again and again.",
		"general", true, 1, "small short shop shops store house home menu room inn cafe",
		{ { "Intro", 2, "Two bars that play once as you come in." },
		{ "A", 4, "A short, friendly tune." },
		{ "A'", 4, "The tune again, a little different." },
		{ "B", 4, "A contrast that leads back to A, so the loop joins smoothly." },
		{ 0, 0, 0 } } },
	{ "jingle", "Jingle", "A jingle that plays once (an item found, a level cleared): a short phrase, a last chord, and the song "
		"stops.", "general", false, 0, "jingle jingles fanfare sting victory item once cue",
		{ { "Phrase", 2, "The jingle's idea: a quick figure rising to its last note (falling, for a sad one)." },
		{ "Last chord", 2, "One chord held and ringing away; the song stops here instead of looping." },
		{ 0, 0, 0 } } },
};

// When words ask for more than one (a "night town"), the first of these
const char *const PRIORITY[] = { "jingle", "boss", "night", "small", "stage", "town" };

std::string Lower(const std::string &Text)
{
	std::string Out(Text);
	for (size_t i = 0; i < Out.size(); ++i)
		Out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(Out[i])));
	return Out;
}

std::string Trimmed(const std::string &Text)
{
	size_t First = 0, End = Text.size();
	while (First < End && std::isspace(static_cast<unsigned char>(Text[First])))
		++First;
	while (End > First && std::isspace(static_cast<unsigned char>(Text[End - 1])))
		--End;
	return Text.substr(First, End - First);
}

// The words in a text: letters and digits, lowercase
std::vector<std::string> WordsOf(const std::string &Text)
{
	std::vector<std::string> Out;
	std::string Word;
	for (size_t i = 0; i <= Text.size(); ++i) {
		const unsigned char c = i < Text.size() ? static_cast<unsigned char>(Text[i]) : ' ';
		if (std::isalnum(c))
			Word += static_cast<char>(std::tolower(c));
		else if (!Word.empty()) {
			Out.push_back(Word);
			Word.clear();
		}
	}
	return Out;
}

int Gcd(int a, int b)
{
	while (b != 0) {
		const int t = a % b;
		a = b;
		b = t;
	}
	return a;
}

std::vector<SongShape> BuildShapes()
{
	std::vector<SongShape> Out;
	for (size_t s = 0; s < sizeof(SHAPES) / sizeof(SHAPES[0]); ++s) {
		const ShapeRow &Row = SHAPES[s];
		SongShape Shape;
		Shape.Id = Row.Id;
		Shape.Title = Row.Title;
		Shape.About = Row.About;
		Shape.Kind = Row.Kind;
		Shape.Loops = Row.Loops;
		Shape.LoopPart = Row.LoopPart;
		for (size_t p = 0; p < sizeof(Row.Parts) / sizeof(Row.Parts[0]) && Row.Parts[p].Name != 0; ++p) {
			ShapePart Part;
			Part.Name = Row.Parts[p].Name;
			Part.Bars = Row.Parts[p].Bars;
			Part.Line = Row.Parts[p].Line;
			Shape.Parts.push_back(Part);
		}
		Out.push_back(Shape);
	}
	return Out;
}

const ShapeRow *RowOf(const SongShape &Shape)
{
	for (size_t s = 0; s < sizeof(SHAPES) / sizeof(SHAPES[0]); ++s)
		if (Shape.Id == SHAPES[s].Id)
			return &SHAPES[s];
	return 0;
}

const SongShape *ShapeById(const std::string &Id)
{
	const std::vector<SongShape> &All = SongShapes();
	for (size_t i = 0; i < All.size(); ++i)
		if (All[i].Id == Id)
			return &All[i];
	return 0;
}

// A section's name taken apart: what it names ("a", "verse"), as written ("A", "Verse"), and how it's marked a
// repeat: primes ("A'", "A''") or a number after it ("A2", "A 2", "Chorus 3")
struct NameParts {
	std::string Base, Written;
	int Primes, Number;
	bool Repeat() const { return Primes > 0 || Number >= 2; }
	// A third time or later ("A''", "A 3"): often the fullest, the song's high point
	bool Late() const { return Primes >= 2 || Number >= 3 || (Primes >= 1 && Number >= 2); }
};

NameParts SplitName(const std::string &Name)
{
	NameParts Out;
	Out.Primes = 0;
	Out.Number = 0;
	std::string Text = Trimmed(Name);
	for (;;) {
		size_t End = Text.size();
		while (End > 0 && (Text[End - 1] == '\'' || Text[End - 1] == '`'))
			--End;
		Out.Primes += static_cast<int>(Text.size() - End);
		Text = Trimmed(Text.substr(0, End));
		size_t Digits = Text.size();
		while (Digits > 0 && std::isdigit(static_cast<unsigned char>(Text[Digits - 1])))
			--Digits;
		if (Digits == Text.size() || Digits == 0 || Text.size() - Digits > 3)
			break;
		Out.Number = std::atoi(Text.c_str() + Digits);
		Text = Trimmed(Text.substr(0, Digits));
	}
	Out.Written = Text;
	Out.Base = Lower(Text);
	return Out;
}

bool BaseIs(const std::string &Base, const char *const *pWords, size_t Count)
{
	for (size_t i = 0; i < Count; ++i)
		if (Base == pWords[i])
			return true;
	return false;
}

#define WORDS_ARE(List) BaseIs(Base, List, sizeof(List) / sizeof(List[0]))

// What a name says of its part, the shapes aside
std::string LineOfName(const std::string &Name)
{
	const NameParts P = SplitName(Name);
	const std::string &Base = P.Base;
	if (Base.empty())
		return std::string();
	static const char *const INTRO[] = { "intro", "introduction", "opening" };
	static const char *const TUNE[] = { "a", "theme", "main", "main theme", "melody", "tune" };
	static const char *const VERSE[] = { "verse" };
	static const char *const CHORUS[] = { "chorus", "refrain", "hook" };
	static const char *const BUILD[] = { "pre-chorus", "prechorus", "pre chorus", "build", "build-up", "buildup", "build up", "rise" };
	static const char *const BRIDGE[] = { "bridge" };
	static const char *const CLIMAX[] = { "climax", "peak", "finale" };
	static const char *const BREAK[] = { "break", "breakdown", "pause", "breath" };
	static const char *const OUTRO[] = { "outro", "ending", "end", "coda", "finish" };
	static const char *const INTERLUDE[] = { "interlude", "transition" };
	static const char *const SOLO[] = { "solo" };
	static const char *const LOOP[] = { "loop" };
	static const char *const PHRASE[] = { "phrase" };
	static const char *const CHORD[] = { "last chord", "final chord" };
	static const char *const FANFARE[] = { "fanfare" };
	if (WORDS_ARE(INTRO))
		return "Plays once at the start, before the loop: it sets the mood.";
	if (WORDS_ARE(BREAK))
		return "A breath: the tune (and often the drums) rest for a few bars.";
	if (WORDS_ARE(OUTRO))
		return "The ending: the song winds down as it finishes.";
	if (WORDS_ARE(INTERLUDE))
		return "A short passage between two parts, without the main tune.";
	if (WORDS_ARE(CHORD))
		return "One chord held and ringing away at the very end.";
	// A repeat of a part with a tune: that part again
	if (P.Repeat()) {
		if (P.Late())
			return P.Written + " once more, often its fullest: the song's high point.";
		return P.Written + " again, changed a little so the repeat sounds fresh.";
	}
	if (WORDS_ARE(TUNE))
		return "The tune, the part people hum.";
	if (Base == "b")
		return "Something different, on other chords, so A sounds fresh when it comes back.";
	if (Base.size() == 1 && Base[0] >= 'c' && Base[0] <= 'z')
		return "Another part of its own, for more variety before the tune comes back.";
	if (WORDS_ARE(VERSE))
		return "The part that tells: the calmer half of the tune, before the chorus.";
	if (WORDS_ARE(CHORUS))
		return "The catchiest part, the one that comes back most.";
	if (WORDS_ARE(BUILD))
		return "Builds up to the part that comes next.";
	if (WORDS_ARE(BRIDGE))
		return "A part that goes somewhere new before the main tune comes back.";
	if (WORDS_ARE(CLIMAX))
		return "The high point: the tune at its biggest and loudest.";
	if (WORDS_ARE(SOLO))
		return "One instrument steps forward and plays a line of its own.";
	if (WORDS_ARE(LOOP))
		return "The part that repeats while the game plays.";
	if (WORDS_ARE(PHRASE))
		return "A musical sentence: a few bars that rise or fall to a point of rest.";
	if (WORDS_ARE(FANFARE))
		return "A short, bright call that announces something.";
	return std::string();
}

#undef WORDS_ARE

bool SameName(const std::string &a, const std::string &b)
{
	return Lower(Trimmed(a)) == Lower(Trimmed(b));
}

}	// namespace

int SongShape::Bars() const
{
	int Total = 0;
	for (size_t i = 0; i < Parts.size(); ++i)
		Total += Parts[i].Bars;
	return Total;
}

int SongShape::FrameBars() const
{
	int Frame = 0;
	for (size_t i = 0; i < Parts.size(); ++i)
		Frame = Gcd(Frame, std::max(1, Parts[i].Bars));
	return std::max(1, Frame);
}

std::string SongShape::PartsText() const
{
	std::string Out;
	for (size_t i = 0; i < Parts.size(); ++i) {
		char Bars[16];
		std::snprintf(Bars, sizeof(Bars), " %d", Parts[i].Bars);
		Out += (i > 0 ? ", " : "") + Parts[i].Name + Bars;
	}
	return Out;
}

const std::vector<SongShape> &SongShapes()
{
	static const std::vector<SongShape> SHAPE_LIST = BuildShapes();
	return SHAPE_LIST;
}

const SongShape *FindSongShape(const std::string &Words)
{
	const std::string Asked = Lower(Trimmed(Words));
	if (Asked.empty())
		return 0;
	const std::vector<SongShape> &All = SongShapes();
	for (size_t i = 0; i < All.size(); ++i)
		if (Asked == All[i].Id || Asked == Lower(All[i].Title))
			return &All[i];
	// The words: the shape that most of them ask for, the first of PRIORITY on a tie
	const std::vector<std::string> Given = WordsOf(Asked);
	const SongShape *pBest = 0;
	int Best = 0;
	for (size_t p = 0; p < sizeof(PRIORITY) / sizeof(PRIORITY[0]); ++p) {
		const SongShape *pShape = ShapeById(PRIORITY[p]);
		const ShapeRow *pRow = pShape != 0 ? RowOf(*pShape) : 0;
		if (pRow == 0)
			continue;
		const std::vector<std::string> Own = WordsOf(pRow->Words);
		int Count = 0;
		for (size_t g = 0; g < Given.size(); ++g)
			Count += std::find(Own.begin(), Own.end(), Given[g]) != Own.end() ? 1 : 0;
		if (Count > Best) {
			Best = Count;
			pBest = pShape;
		}
	}
	return pBest;
}

std::string SongShapeIds()
{
	const std::vector<SongShape> &All = SongShapes();
	std::string Out;
	for (size_t i = 0; i < All.size(); ++i)
		Out += (i == 0 ? "" : i + 1 == All.size() ? " or " : ", ") + All[i].Id;
	return Out;
}

const SongShape &ShapeForKind(const std::string &Kind)
{
	const std::string Word = Lower(Trimmed(Kind));
	const char *Id = Word == "lively" ? "stage" : Word == "calm" ? "night" : Word == "action" ? "boss" : "town";
	const SongShape *pShape = ShapeById(Id);
	return pShape != 0 ? *pShape : SongShapes()[0];
}

std::string PartLine(const std::string &Name, const SongShape *pShape)
{
	if (pShape != 0)
		for (size_t i = 0; i < pShape->Parts.size(); ++i)
			if (SameName(pShape->Parts[i].Name, Name))
				return pShape->Parts[i].Line;
	return LineOfName(Name);
}

const SongShape *MatchSongShape(const SectionList &Sections, const std::string &TrackName, double Bpm, const SongShape *pPrefer)
{
	const SongShape *pNamed = FindSongShape(TrackName);
	const bool Calm = (pNamed != 0 && pNamed->Id == "night") || (Bpm > 0.0 && Bpm <= 90.0);
	const std::vector<SongShape> &All = SongShapes();
	const SongShape *pFound = 0;
	for (size_t s = 0; s < All.size(); ++s) {
		const SongShape &Shape = All[s];
		if (Shape.Parts.size() != Sections.size())
			continue;
		bool Same = true;
		for (size_t i = 0; i < Sections.size() && Same; ++i)
			Same = SameName(Sections[i].Name, Shape.Parts[i].Name);
		if (!Same)
			continue;
		// Two shapes sharing their parts' names (none do now)
		if (pPrefer != 0 && Shape.Id == pPrefer->Id)
			return &Shape;
		if (pFound == 0 || (Calm && Shape.Id == "night") || (!Calm && pFound->Id == "night"))
			pFound = &Shape;
	}
	return pFound;
}

const SongShape *GuessSongShape(const SectionList &Sections, const std::string &TrackName, double Bpm)
{
	const SongShape *pFound = MatchSongShape(Sections, TrackName, Bpm);
	return pFound != 0 ? pFound : FindSongShape(TrackName);
}

std::string SectionHoverText(const std::string &Name, int FirstBar, int LastBar, const SongShape *pShape)
{
	char Bars[48];
	if (LastBar > FirstBar)
		std::snprintf(Bars, sizeof(Bars), " (bars %d-%d)", FirstBar, LastBar);
	else
		std::snprintf(Bars, sizeof(Bars), " (bar %d)", FirstBar);
	const std::string Line = PartLine(Name, pShape);
	return Trimmed(Name) + Bars + (Line.empty() ? std::string() : ": " + Line);
}

std::string FitProgression(const std::string &Progression, int Bars)
{
	std::vector<std::string> Chords;
	std::string Chord;
	for (size_t i = 0; i <= Progression.size(); ++i) {
		if (i == Progression.size() || Progression[i] == '|') {
			const std::string Text = Trimmed(Chord);
			if (!Text.empty())
				Chords.push_back(Text);
			Chord.clear();
		}
		else
			Chord += Progression[i];
	}
	const int Count = static_cast<int>(Chords.size());
	if (Count == 0 || Bars < 1 || Bars == Count)
		return Progression;
	std::vector<std::string> Fitted;
	const int Ending = std::min(2, std::min(Count, Bars - 1));	// the last chords, kept at the end
	for (int b = 0; b < Bars - Ending; ++b)
		Fitted.push_back(Chords[static_cast<size_t>(b % Count)]);
	for (int e = Ending; e > 0; --e)
		Fitted.push_back(Chords[static_cast<size_t>(Count - e)]);
	std::string Out = "|";
	for (size_t i = 0; i < Fitted.size(); ++i)
		Out += " " + Fitted[i] + " |";
	return Out;
}

int ShapeLayout::FirstBar(int Part) const
{
	if (Part < 0 || Part >= static_cast<int>(PartBars.size()) || PartBars[static_cast<size_t>(Part)] == 0)
		return 0;
	int Bar = 1;
	for (int i = 0; i < Part; ++i)
		Bar += PartBars[static_cast<size_t>(i)];
	return Bar;
}

bool LayOutShape(const SongShape &Shape, bool Empty, int RowsPerBar, int PatternLength, int Frames, int MaxFrames, bool AddFrames,
	ShapeLayout &Out, std::string &Error)
{
	Out = ShapeLayout();
	RowsPerBar = std::max(1, std::min(SHAPE_MAX_ROWS, RowsPerBar));
	PatternLength = std::max(1, std::min(SHAPE_MAX_ROWS, PatternLength));
	Frames = std::max(1, Frames);
	MaxFrames = std::max(1, MaxFrames);
	if (Shape.Parts.empty()) {
		Error = "That shape has no parts.";
		return false;
	}

	// Bars a frame: a track without notes keeps its own when every part is whole frames of it, else takes the most whole
	// bars every part is (and that fit a frame); a track with notes keeps its frames, as the tools count their bars
	const int Whole = Shape.FrameBars();
	int Bpf = 0, Length = PatternLength;
	if (Empty) {
		const int Own = PatternLength % RowsPerBar == 0 ? PatternLength / RowsPerBar : 0;
		if (Own > 0 && Whole % Own == 0 && Shape.Bars() / Own <= MaxFrames)
			Bpf = Own;
		else {
			Bpf = Whole;
			while (Bpf > 1 && (Bpf * RowsPerBar > SHAPE_MAX_ROWS || Whole % Bpf != 0))
				--Bpf;
			Length = Bpf * RowsPerBar;
		}
	}
	else
		Bpf = (PatternLength + RowsPerBar - 1) / RowsPerBar;
	Out.BarsPerFrame = Bpf;
	Out.RowsPerFrame = Length;

	// Each part in whole frames: exactly on a track of its own frames, the nearest whole frames (at least one) on a track
	// whose frames are another length
	std::vector<int> PartFrames;
	int Needed = 0;
	for (size_t i = 0; i < Shape.Parts.size(); ++i) {
		const int Bars = std::max(1, Shape.Parts[i].Bars);
		const int Count = std::max(1, (Bars + Bpf / 2) / Bpf);
		if (Count * Bpf != Bars)
			Out.Refitted.push_back(static_cast<int>(i));
		PartFrames.push_back(Count);
		Needed += Count;
	}
	Out.NeededFrames = Needed;
	if (Needed > MaxFrames) {
		char Text[200];
		std::snprintf(Text, sizeof(Text), "The %s takes %d frames of %d bars, more than a track holds (%d).", Lower(Shape.Title).c_str(),
			Needed, Bpf, MaxFrames);
		Error = Text;
		return false;
	}

	// The frames the track has afterwards, and the parts on them
	int Placed = Needed;
	if (Empty || Needed <= Frames || AddFrames)
		Out.Frames = Empty ? Needed : std::max(Frames, Needed);
	else {
		Out.Frames = Frames;
		Placed = Frames;
	}
	Out.FrameChange = Out.Frames - Frames;
	int Frame = 0;
	for (size_t i = 0; i < Shape.Parts.size(); ++i) {
		const int Count = PartFrames[i];
		if (Frame >= Placed) {
			Out.PartBars.push_back(0);
			++Out.Missing;
			continue;
		}
		const int Last = std::min(Frame + Count, Placed) - 1;
		if (Last < Frame + Count - 1)
			Out.Cut = static_cast<int>(i);
		NameFrames(Out.Sections, Frame, Last, Shape.Parts[i].Name);
		Out.PartBars.push_back((Last - Frame + 1) * Bpf);
		Frame += Count;
	}
	// The loop goes back to the part after the intro; a jingle stops
	Out.LoopFrame = -1;
	if (Shape.Loops) {
		int Loop = 0;
		for (int i = 0; i < Shape.LoopPart && i < static_cast<int>(PartFrames.size()); ++i)
			Loop += PartFrames[static_cast<size_t>(i)];
		Out.LoopFrame = Loop < Out.Frames ? Loop : 0;
	}
	return true;
}

}	// namespace ftapi

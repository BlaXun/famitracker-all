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
#include <cstring>

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
		"stage stages lively level levels platformer platform explore exploring adventure jump area areas zone zones world "
		"factory factories cave caves cavern caverns dungeon dungeons forest forests woods castle castles ruins mine mines desert "
		"jungle temple temples tower towers mountain mountains swamp island islands beach underwater sky ice snow volcano sewer",
		{ { "Intro", 4, "Plays once as the stage starts: the beat and the chords build up, without the tune." },
		{ "A", 8, "The tune, the part people hum." },
		{ "A'", 8, "The tune again, changed a little: a second line joins in and the drums get busier." },
		{ "B", 8, "Something different, on other chords, so A sounds fresh when it comes back." },
		{ "Climax", 8, "The high point: the tune at its biggest, with a harmony, a little louder than A." },
		{ "Break", 4, "A breath before the loop comes round: the tune and the drums rest." },
		{ 0, 0, 0 } } },
	{ "night", "Calm night", "A place to rest or a night scene: slow and soft, and the tune never presses.", "calm", true, 1,
		"night nights calm rest resting sleep sleeping sleepy lullaby peaceful relax relaxing evening dream dreamy quiet gentle "
		"moon moonlit starry ambient",
		{ { "Intro", 4, "The chords alone, softly, before the tune comes in. It plays once." },
		{ "A", 8, "The tune: few, slow notes, with rests between its phrases." },
		{ "A'", 8, "The tune again, deeper: an echo a beat behind it, or a second voice." },
		{ "B", 8, "Other chords, a little higher or darker: somewhere new before coming home." },
		{ "Break", 4, "The tune rests while one small sound plays (a far-off clock, a sigh) before it comes back." },
		{ "A''", 8, "The tune once more, softly answered, ending without coming to rest so the loop flows on." },
		{ 0, 0, 0 } } },
	{ "town", "Town", "A town, a village or a field: a friendly tune that can loop for a long time without tiring.", "general", true, 1,
		"town towns village villages city cities square squares market markets harbor harbors harbour port ports field fields "
		"map overworld general",
		{ { "Intro", 4, "Plays once as you arrive: the chords and a hint of the tune." },
		{ "A", 8, "The town's tune, easy to hum." },
		{ "A'", 8, "The tune again with something new: a second line, other drums." },
		{ "B", 8, "Another corner of town: other chords and another rhythm, so A sounds fresh after it." },
		{ "A''", 8, "The tune's last time round, and its fullest: the town's high point." },
		{ "Break", 4, "A short rest before the loop comes round, so the start sounds fresh again." },
		{ 0, 0, 0 } } },
	{ "boss", "Boss fight", "A boss or a chase: tense from the first bar, a theme that drives, a climax at the fight's peak.", "action",
		true, 1, "boss bosses battle battles fight fighting action chase combat danger dangerous enemy enemies threat menace "
		"menacing duel showdown",
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

// When words ask for more than one (a "night town"), the first of these. A small place's words come last: "home" or
// "small" says less than a place's own word, so a "Home village" is a town and a "Small town" too
const char *const PRIORITY[] = { "jingle", "boss", "night", "stage", "town", "small" };

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

// The lines of the parts a shape's variations add, the shape's own words for them (the others are LineOfName's)
struct ExtraRow {
	const char *Shape, *Name, *Line;
};

const ExtraRow EXTRA_LINES[] = {
	{ "stage", "A''", "The tune once more, fuller still: a harmony or busier drums keep it climbing." },
	{ "stage", "A'''", "The tune's last time round before the loop, at its fullest." },
	{ "stage", "B'", "B again, changed a little, so the contrast comes back fresh." },
	{ "stage", "Bridge", "Somewhere new, on chords of its own, before the climax lifts the tune." },
	{ "stage", "Outro", "A turnaround: the tune winds down and the drums thin out before the loop comes round." },
	{ "night", "Climax", "The tune once more, higher, at its warmest; no drums added, it only glows a little brighter." },
	{ "night", "Bridge", "A gentle detour on other chords, softer still, before the tune comes home." },
	{ "night", "B'", "The other chords once more, a little changed, before coming home." },
	{ "night", "A'''", "The tune's last time, softest of all, ending open so the loop flows on." },
	{ "night", "Outro", "The tune fades into the chords alone, softly, before the loop starts again." },
	{ "town", "Climax", "The town at its liveliest: the tune higher, with a harmony, before it settles." },
	{ "town", "Bridge", "A side street: other chords and a new rhythm before the tune comes home." },
	{ "town", "B'", "That other corner again, changed a little, before the tune comes home." },
	{ "town", "A'''", "The tune once more, leading back to the start." },
	{ "town", "Outro", "A turnaround: the tune settles and the parts thin out before the loop comes round." },
	{ "boss", "B", "A second theme on other chords: the fight changes its pattern." },
	{ "boss", "B'", "That second theme again, pushed harder." },
	{ "boss", "A''", "The theme once more, at full force, before the peak." },
	{ "boss", "Bridge", "A short, uneasy turn on other chords before the fight peaks." },
	{ "boss", "Outro", "The theme's last notes, fewer parts: the tension winds down for a moment." },
	{ "small", "A''", "The tune once more, rounding the loop off." },
	{ "small", "Break", "A short rest before the loop comes round." },
	{ "small", "B'", "The contrast again, a little changed." },
	{ "small", "Climax", "The tune at its brightest, a little higher, before the loop comes round." },
	{ "small", "Bridge", "A short detour on other chords." },
	{ "small", "Outro", "A short turnaround back to the start." },
};

std::string ExtraLine(const std::string &Shape, const std::string &Name)
{
	for (size_t i = 0; i < sizeof(EXTRA_LINES) / sizeof(EXTRA_LINES[0]); ++i)
		if (Shape == EXTRA_LINES[i].Shape && SameName(Name, EXTRA_LINES[i].Name))
			return EXTRA_LINES[i].Line;
	return std::string();
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
	const char *Id = Word == "lively" ? "stage" : Word == "calm" ? "night" : Word == "action" ? "boss" : "town";	// (cozy: town)
	const SongShape *pShape = ShapeById(Id);
	return pShape != 0 ? *pShape : SongShapes()[0];
}

const SongShape &ShapeForKind(const std::string &Kind, const std::string &TrackName)
{
	const SongShape &Own = ShapeForKind(Kind);
	const SongShape *pNamed = FindSongShape(TrackName);
	return pNamed != 0 && pNamed->Loops && pNamed->Kind == Own.Kind ? *pNamed : Own;
}

bool IsClimaxName(const std::string &Name)
{
	static const char *const CLIMAX[] = { "climax", "peak", "finale" };
	return BaseIs(SplitName(Name).Base, CLIMAX, sizeof(CLIMAX) / sizeof(CLIMAX[0]));
}

std::string PartLine(const std::string &Name, const SongShape *pShape)
{
	if (pShape != 0) {
		for (size_t i = 0; i < pShape->Parts.size(); ++i)
			if (SameName(pShape->Parts[i].Name, Name))
				return pShape->Parts[i].Line;
		// A part its variations add (a calm night's climax...)
		const std::string Extra = ExtraLine(pShape->Id, Name);
		if (!Extra.empty())
			return Extra;
	}
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
	const SongShape *pFound = LaidOutShape(Sections, TrackName, Bpm);
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

// Variations (from feat-shape-variations)

namespace {

// What a part of a form is, by its name
enum part_kind_t { PK_INTRO, PK_A, PK_B, PK_BRIDGE, PK_CLIMAX, PK_BREAK, PK_OUTRO, PK_OTHER };

part_kind_t KindOfPart(const std::string &Name)
{
	const NameParts P = SplitName(Name);
	const std::string &Base = P.Base;
	if (Base == "intro" || Base == "introduction" || Base == "opening")
		return PK_INTRO;
	if (Base == "a")
		return PK_A;
	if (Base == "b")
		return PK_B;
	if (Base == "bridge")
		return PK_BRIDGE;
	if (IsClimaxName(Name))
		return PK_CLIMAX;
	if (Base == "break" || Base == "breakdown" || Base == "pause")
		return PK_BREAK;
	if (Base == "outro" || Base == "ending" || Base == "coda" || Base == "turnaround")
		return PK_OUTRO;
	return PK_OTHER;
}

bool IsFreeKind(part_kind_t Kind)
{
	return Kind == PK_INTRO || Kind == PK_BREAK || Kind == PK_OUTRO;
}

// The variants of each shape after its plain form: the soundtrack's ways of varying one (a second B, a bridge in B's
// place, B before the tune comes back, a turnaround in the break's place; a night with its break at the end, its climax;
// a boss with a second theme or a longer peak; a small place with its contrast first)
struct VariantRow {
	const char *Shape, *Parts;
};

const VariantRow VARIANTS[] = {
	{ "stage", "Intro 4, A 8, A' 8, B 8, B' 8, Climax 8, Break 4" },
	{ "stage", "Intro 4, A 8, A' 8, Bridge 8, Climax 8, Break 4" },
	{ "stage", "Intro 4, A 8, B 8, A' 8, Climax 8, Break 4" },
	{ "stage", "Intro 4, A 8, A' 8, B 8, Climax 8, Outro 4" },
	{ "night", "Intro 4, A 8, A' 8, B 8, A'' 8, Break 4" },
	{ "night", "Intro 4, A 8, B 8, A' 8, Break 4, A'' 8" },
	{ "night", "Intro 4, A 8, A' 8, B 8, Break 4, Climax 8" },
	{ "town", "Intro 4, A 8, A' 8, B 8, B' 8, A'' 8, Break 4" },
	{ "town", "Intro 4, A 8, A' 8, Bridge 8, A'' 8, Break 4" },
	{ "town", "Intro 4, A 8, A' 8, B 8, A'' 8, Outro 4" },
	{ "boss", "Intro 2, A 8, B 8, A' 8, Climax 6, Break 2" },
	{ "boss", "Intro 4, A 8, A' 8, Climax 8, Break 4" },
	{ "boss", "Intro 2, A 8, A' 8, Bridge 4, Climax 6, Break 2" },
	{ "small", "Intro 2, A 4, B 4, A' 4" },
	{ "small", "Intro 2, A 4, A' 4, B 4, Break 2" },
	{ "small", "Intro 2, A 4, A' 4, B 4, A'' 4" },
};

// What a longer song of each shape gets, in turn (its own way of growing), before its phrases grow longer
struct GrowRow {
	const char *Shape, *Words;
};

const GrowRow GROWTH[] = {
	{ "stage", "b a bridge a" }, { "night", "climax b a" }, { "town", "b bridge a" }, { "boss", "b a bridge" }, { "small", "a b break" },
};

// A part of a form being made
struct FormItem {
	std::string Name;
	part_kind_t Kind;
	int Bars;
};

typedef std::vector<FormItem> FormList;

std::string Num(int Value)
{
	char Text[16];
	std::snprintf(Text, sizeof(Text), "%d", Value);
	return Text;
}

int FormBars(const FormList &Form)
{
	int Total = 0;
	for (size_t i = 0; i < Form.size(); ++i)
		Total += Form[i].Bars;
	return Total;
}

std::string FormText(const FormList &Form)
{
	std::string Out;
	for (size_t i = 0; i < Form.size(); ++i)
		Out += (i > 0 ? ", " : "") + Form[i].Name + " " + Num(Form[i].Bars);
	return Out;
}

int CountKind(const FormList &Form, part_kind_t Kind)
{
	int Count = 0;
	for (size_t i = 0; i < Form.size(); ++i)
		Count += Form[i].Kind == Kind ? 1 : 0;
	return Count;
}

int LastOfKind(const FormList &Form, part_kind_t Kind)
{
	for (size_t i = Form.size(); i > 0; --i)
		if (Form[i - 1].Kind == Kind)
			return static_cast<int>(i - 1);
	return -1;
}

int BodyCount(const FormList &Form)
{
	int Count = 0;
	for (size_t i = 0; i < Form.size(); ++i)
		Count += IsFreeKind(Form[i].Kind) ? 0 : 1;
	return Count;
}

// A's and B's names in order: A, A', A''...
void RenameFamilies(FormList &Form)
{
	int As = 0, Bs = 0;
	for (size_t i = 0; i < Form.size(); ++i) {
		if (Form[i].Kind == PK_A)
			Form[i].Name = "A" + std::string(static_cast<size_t>(As++), '\'');
		else if (Form[i].Kind == PK_B)
			Form[i].Name = "B" + std::string(static_cast<size_t>(Bs++), '\'');
	}
}

FormList FormOf(const std::vector<ShapePart> &Parts)
{
	FormList Out;
	for (size_t i = 0; i < Parts.size(); ++i) {
		FormItem Item;
		Item.Name = Parts[i].Name;
		Item.Kind = KindOfPart(Parts[i].Name);
		Item.Bars = Parts[i].Bars;
		Out.push_back(Item);
	}
	return Out;
}

// A phrase of the form (its A's bars) and a short part (its intro's, else 4 bars, 2 for a phrase of 4)
int PhraseOf(const FormList &Form)
{
	for (size_t i = 0; i < Form.size(); ++i)
		if (Form[i].Kind == PK_A)
			return Form[i].Bars;
	return 8;
}

int ShortOf(const FormList &Form)
{
	for (size_t i = 0; i < Form.size(); ++i)
		if (Form[i].Kind == PK_INTRO)
			return Form[i].Bars;
	return PhraseOf(Form) >= 8 ? 4 : 2;
}

// The words add and drop take, as one of the kinds (PK_A for another time round of A); false for none
bool PartWord(const std::string &Text, part_kind_t &Kind, bool &Itself)
{
	const std::string Word = Lower(Trimmed(Text));
	Itself = false;
	static const char *const CLIMAX[] = { "climax", "peak", "high point", "highpoint", "finale" };
	static const char *const BREAK[] = { "break", "pause", "rest", "breakdown", "breath" };
	static const char *const OUTRO[] = { "outro", "ending", "end", "coda", "turnaround" };
	static const char *const INTRO[] = { "intro", "introduction", "opening" };
	static const char *const BRIDGE[] = { "bridge" };
	static const char *const B[] = { "b", "b'", "b''", "second b", "another b", "b again", "contrast", "b part" };
	static const char *const A[] = { "a'", "a''", "a'''", "a again", "another a", "repeat", "a repeat", "extra a", "a part again" };
	struct Row {
		const char *const *pWords;
		size_t Count;
		part_kind_t Kind;
	};
	const Row ROWS[] = { { CLIMAX, 5, PK_CLIMAX }, { BREAK, 5, PK_BREAK }, { OUTRO, 5, PK_OUTRO }, { INTRO, 3, PK_INTRO },
		{ BRIDGE, 1, PK_BRIDGE }, { B, 8, PK_B }, { A, 9, PK_A } };
	for (const Row &R : ROWS)
		if (BaseIs(Word, R.pWords, R.Count)) {
			Kind = R.Kind;
			return true;
		}
	if (Word == "a" || Word == "a part" || Word == "tune") {
		Kind = PK_A;
		Itself = true;
		return true;
	}
	return false;
}

const char *KindWord(part_kind_t Kind)
{
	switch (Kind) {
	case PK_INTRO: return "intro";
	case PK_A: return "repeat of A";
	case PK_B: return "B";
	case PK_BRIDGE: return "bridge";
	case PK_CLIMAX: return "climax";
	case PK_BREAK: return "break";
	case PK_OUTRO: return "outro";
	default: return "part";
	}
}

std::string Article(const char *Word)
{
	return std::string(std::strchr("aeiou", Word[0]) != 0 ? "an " : "a ") + Word;
}

// Puts in a part of that kind where the form's own way puts it; false with Error when it has one (or as many as make sense)
bool AddKind(FormList &Form, part_kind_t Kind, const std::string &Title, std::string &Error)
{
	const int Phrase = PhraseOf(Form), Short = ShortOf(Form);
	FormItem Item;
	Item.Kind = Kind;
	Item.Bars = Phrase;
	size_t At = Form.size();
	// After the body's last part: before the free parts that end the form
	size_t AfterBody = Form.size();
	while (AfterBody > 0 && IsFreeKind(Form[AfterBody - 1].Kind))
		--AfterBody;
	switch (Kind) {
	case PK_INTRO:
		if (CountKind(Form, PK_INTRO) > 0) {
			Error = "The " + Lower(Title) + " has an intro already.";
			return false;
		}
		Item.Name = "Intro";
		Item.Bars = Short;
		At = 0;
		break;
	case PK_BREAK:
	case PK_OUTRO:
		if (CountKind(Form, Kind) > 0) {
			Error = "The " + Lower(Title) + " has " + Article(KindWord(Kind)) + " already.";
			return false;
		}
		Item.Name = Kind == PK_BREAK ? "Break" : "Outro";
		Item.Bars = Short;
		// A break before an outro that ends the form; an outro last
		At = Kind == PK_BREAK && !Form.empty() && Form.back().Kind == PK_OUTRO ? Form.size() - 1 : Form.size();
		break;
	case PK_CLIMAX:
	case PK_BRIDGE: {
		if (CountKind(Form, Kind) > 0) {
			Error = "The " + Lower(Title) + " has " + Article(KindWord(Kind)) + " already.";
			return false;
		}
		Item.Name = Kind == PK_CLIMAX ? "Climax" : "Bridge";
		// After the contrast (the last B, or the bridge for a climax); else after A's first repeat, else after A
		int After = LastOfKind(Form, PK_B);
		if (Kind == PK_CLIMAX)
			After = std::max(After, LastOfKind(Form, PK_BRIDGE));
		if (After < 0) {
			int As = 0;
			for (size_t i = 0; i < Form.size() && After < 0; ++i)
				if (Form[i].Kind == PK_A && ++As == (Kind == PK_CLIMAX ? CountKind(Form, PK_A) : std::min(2, CountKind(Form, PK_A))))
					After = static_cast<int>(i);
		}
		At = After >= 0 ? static_cast<size_t>(After) + 1 : AfterBody;
		break;
	}
	case PK_B: {
		const int Bs = CountKind(Form, PK_B);
		if (Bs >= 3) {
			Error = "The " + Lower(Title) + " has B, B' and B'' already.";
			return false;
		}
		Item.Name = "B";
		if (Bs > 0) {
			At = static_cast<size_t>(LastOfKind(Form, PK_B)) + 1;
			Item.Bars = Form[At - 1].Bars;
		}
		else {
			int As = 0, After = -1;
			for (size_t i = 0; i < Form.size(); ++i)
				if (Form[i].Kind == PK_A && ++As <= 2)
					After = static_cast<int>(i);
			At = After >= 0 ? static_cast<size_t>(After) + 1 : AfterBody;
		}
		break;
	}
	case PK_A:
		if (CountKind(Form, PK_A) >= 4) {
			Error = "The " + Lower(Title) + " plays A four times already (A, A', A'', A''').";
			return false;
		}
		Item.Name = "A";
		At = AfterBody;
		break;
	default:
		return false;
	}
	Form.insert(Form.begin() + static_cast<std::ptrdiff_t>(At), Item);
	RenameFamilies(Form);
	return true;
}

// Takes the parts of that kind out (B: its last; A: its last repeat); false with Error when it has none, or the form
// would make no sense without it
bool DropKind(FormList &Form, part_kind_t Kind, bool Itself, const std::string &Title, std::string &Error)
{
	if (Kind == PK_A && Itself) {
		Error = "A is the tune every other part comes back to, so it stays (drop a' takes away a repeat of it).";
		return false;
	}
	FormList Out = Form;
	if (Kind == PK_A || Kind == PK_B) {
		const int Last = LastOfKind(Out, Kind);
		if (Last < 0 || (Kind == PK_A && CountKind(Out, PK_A) < 2)) {
			Error = "The " + Lower(Title) + " has no " + KindWord(Kind) + " to drop.";
			return false;
		}
		Out.erase(Out.begin() + Last);
	}
	else {
		FormList Kept;
		for (size_t i = 0; i < Out.size(); ++i)
			if (Out[i].Kind != Kind)
				Kept.push_back(Out[i]);
		if (Kept.size() == Out.size()) {
			Error = "The " + Lower(Title) + " has no " + KindWord(Kind) + " to drop.";
			return false;
		}
		Out = Kept;
	}
	if (BodyCount(Out) < 2) {
		Error = "That would leave only A: a song that loops needs A and at least one more part (its repeat or B), or it tires "
			"the ear.";
		return false;
	}
	RenameFamilies(Out);
	Form = Out;
	return true;
}

// The body's phrases twice as long, or half (parts of 8 bars or more that halve to whole frames: never under 4 bars, an
// intro or a break kept short); false when nothing changes
bool ScalePhrases(FormList &Form, bool Longer)
{
	bool Changed = false;
	for (size_t i = 0; i < Form.size(); ++i) {
		if (IsFreeKind(Form[i].Kind))
			continue;
		if (Longer && Form[i].Bars <= 16) {
			Form[i].Bars *= 2;
			Changed = true;
		}
		else if (!Longer && Form[i].Bars >= 8 && Form[i].Bars % 4 == 0) {
			Form[i].Bars /= 2;
			Changed = true;
		}
	}
	return Changed;
}

// The form nearest Target bars, made in the shape's own way: parts added (GROWTH) and then longer phrases, or the
// extra parts taken out and then shorter phrases; never a part the request added taken out nor one it dropped put in.
// Ties go to the form with fewer changes
FormList FitBars(const FormList &Form, const std::string &ShapeId, int Target, const std::vector<part_kind_t> &Added,
	const std::vector<part_kind_t> &Dropped)
{
	auto Has = [](const std::vector<part_kind_t> &List, part_kind_t Kind) {
		return std::find(List.begin(), List.end(), Kind) != List.end();
	};
	std::vector<FormList> Tries(1, Form);
	// Longer
	FormList Grown = Form;
	for (size_t g = 0; g < sizeof(GROWTH) / sizeof(GROWTH[0]); ++g) {
		if (ShapeId != GROWTH[g].Shape)
			continue;
		const std::vector<std::string> Words = WordsOf(GROWTH[g].Words);
		for (size_t w = 0; w < Words.size(); ++w) {
			part_kind_t Kind;
			bool Itself;
			std::string Ignored;
			if (!PartWord(Words[w], Kind, Itself) || Has(Dropped, Kind) || !AddKind(Grown, Kind, std::string(), Ignored))
				continue;
			Tries.push_back(Grown);
		}
	}
	// ...and each of those with its phrases twice as long (four times, the last)
	const size_t Parts = Tries.size();
	for (size_t i = 0; i < Parts; ++i) {
		FormList Long = Tries[i];
		if (ScalePhrases(Long, true))
			Tries.push_back(Long);
	}
	if (ScalePhrases(Grown, true) && ScalePhrases(Grown, true))
		Tries.push_back(Grown);
	// Shorter
	FormList Cut = Form;
	const part_kind_t LESS[] = { PK_A, PK_B, PK_BRIDGE };
	for (part_kind_t Kind : LESS) {
		if (Has(Added, Kind))
			continue;
		// The extra times round: A's repeats past A', B's past B, the bridge
		while ((Kind == PK_A && CountKind(Cut, PK_A) > 2) || (Kind == PK_B && CountKind(Cut, PK_B) > 1) ||
			(Kind == PK_BRIDGE && CountKind(Cut, PK_BRIDGE) > 0 && BodyCount(Cut) > 3)) {
			Cut.erase(Cut.begin() + LastOfKind(Cut, Kind));
			RenameFamilies(Cut);
			Tries.push_back(Cut);
		}
	}
	for (int Twice = 0; Twice < 2 && ScalePhrases(Cut, false); ++Twice)
		Tries.push_back(Cut);
	size_t Best = 0;
	for (size_t i = 1; i < Tries.size(); ++i)
		if (std::abs(FormBars(Tries[i]) - Target) < std::abs(FormBars(Tries[Best]) - Target))
			Best = i;
	return Tries[Best];
}

// FNV-1a of the name as words: the same name, the same variant on every computer
unsigned int NameHash(const std::string &Name)
{
	const std::vector<std::string> Words = WordsOf(Name);
	unsigned int Hash = 2166136261u;
	for (size_t w = 0; w < Words.size(); ++w) {
		const std::string Word = (w > 0 ? " " : "") + Words[w];
		for (size_t i = 0; i < Word.size(); ++i) {
			Hash ^= static_cast<unsigned char>(Word[i]);
			Hash *= 16777619u;
		}
	}
	return Hash;
}

// Whether the word at i is said not to be: "not hectic", "no climax", "without a break"
bool Negated(const std::vector<std::string> &Words, size_t i)
{
	static const char *const NOT[] = { "not", "no", "without", "never", "isn", "nothing", "less", "don", "doesn", "nor" };
	for (size_t k = 1; k <= 3 && k <= i; ++k)
		if (BaseIs(Words[i - k], NOT, sizeof(NOT) / sizeof(NOT[0])))
			return true;
	return false;
}

void AddOnce(std::vector<std::string> &List, const std::string &Word)
{
	if (std::find(List.begin(), List.end(), Word) == List.end())
		List.push_back(Word);
}

}	// namespace

std::vector<SongShape> VariantsOf(const SongShape &Shape)
{
	std::vector<SongShape> Out(1, Shape);
	for (size_t v = 0; v < sizeof(VARIANTS) / sizeof(VARIANTS[0]); ++v) {
		if (Shape.Id != VARIANTS[v].Shape)
			continue;
		SongShape Variant = Shape;
		std::string Error;
		std::vector<ShapePart> Parts;
		ReadShapeParts(VARIANTS[v].Parts, Parts, Error);
		Variant.Parts = Parts;
		for (size_t p = 0; p < Variant.Parts.size(); ++p)
			Variant.Parts[p].Line = PartLine(Variant.Parts[p].Name, &Shape);
		Out.push_back(Variant);
	}
	return Out;
}

int VariantOfName(const std::string &TrackName, int Count)
{
	if (Count <= 1 || WordsOf(TrackName).empty())
		return 1;
	return 1 + static_cast<int>(NameHash(TrackName) % static_cast<unsigned int>(Count));
}

std::string ShapePartWords()
{
	return "climax, break, bridge, outro, intro, b (a contrast, or another one) or a' (another time round of A)";
}

bool ReadShapeParts(const std::string &Text, std::vector<ShapePart> &Parts, std::string &Error)
{
	Parts.clear();
	std::string Item;
	std::vector<std::string> Items;
	for (size_t i = 0; i <= Text.size(); ++i) {
		if (i == Text.size() || Text[i] == ',' || Text[i] == ';' || Text[i] == '\n' || Text[i] == '|') {
			if (!Trimmed(Item).empty())
				Items.push_back(Trimmed(Item));
			Item.clear();
		}
		else
			Item += Text[i];
	}
	if (Items.empty()) {
		Error = "parts: give each part with its bars, like \"Intro 4, A 8, A' 8, B 8, Break 4\".";
		return false;
	}
	if (Items.size() > 24) {
		Error = "parts: at most 24 parts.";
		return false;
	}
	for (size_t i = 0; i < Items.size(); ++i) {
		const std::string &One = Items[i];
		size_t Digits = One.size();
		while (Digits > 0 && std::isdigit(static_cast<unsigned char>(One[Digits - 1])))
			--Digits;
		std::string Name = Trimmed(One.substr(0, Digits));
		while (!Name.empty() && (Name[Name.size() - 1] == ':' || Name[Name.size() - 1] == '=' || Name[Name.size() - 1] == '-'))
			Name = Trimmed(Name.substr(0, Name.size() - 1));
		if (Digits == One.size() || Name.empty() || (Digits > 0 && !std::isspace(static_cast<unsigned char>(One[Digits - 1])) &&
			One[Digits - 1] != ':' && One[Digits - 1] != '=' && One[Digits - 1] != '-')) {
			Error = "parts: \"" + One + "\" needs a name and its bars, like \"A 8\".";
			return false;
		}
		const int Bars = Digits + 4 < One.size() ? 999 : std::atoi(One.c_str() + Digits);
		const std::string Problem = SectionNameProblem(Name);
		if (!Problem.empty()) {
			Error = "parts: \"" + Name + "\": " + Problem;
			return false;
		}
		if (Bars < 2 || Bars > 64 || Bars % 2 != 0) {
			Error = "parts: " + Name + " has " + Num(Bars) + " bars; give each part an even number from 2 to 64 (4 or 8 for most), so "
				"the frames hold whole bars that every part fills.";
			return false;
		}
		for (size_t p = 0; p < Parts.size(); ++p)
			if (SameName(Parts[p].Name, Name)) {
				Error = "parts: \"" + Name + "\" comes twice; name a repeat with a prime (A', A'') so each part has a name of its own.";
				return false;
			}
		ShapePart Part;
		Part.Name = Name;
		Part.Bars = Bars;
		Parts.push_back(Part);
	}
	return true;
}

ShapeWords ReadShapeWords(const std::string &About)
{
	ShapeWords Out;
	const std::vector<std::string> Words = WordsOf(About);
	if (Words.empty())
		return Out;
	auto Any = [&](const char *const *pList, size_t Count, bool Wanted, std::string *pWord) {
		for (size_t i = 0; i < Words.size(); ++i)
			if (BaseIs(Words[i], pList, Count) && Negated(Words, i) != Wanted) {
				if (pWord != 0)
					*pWord = Words[i];
				return true;
			}
		return false;
	};
#define ANY(List, Wanted, pWord) Any(List, sizeof(List) / sizeof(List[0]), Wanted, pWord)
	static const char *const INTENSE[] = { "hectic", "frantic", "fast", "intense", "wild", "busy", "chaotic", "furious", "energetic",
		"rushed", "rushing", "panic", "urgent" };
	static const char *const GENTLE[] = { "calm", "gentle", "slow", "quiet", "sleepy", "soft", "peaceful", "relaxed", "relaxing",
		"mellow", "cozy", "cosy", "easy", "tranquil", "serene" };
	std::string Gentle, Intense;
	const bool IsGentle = ANY(GENTLE, true, &Gentle) || ANY(INTENSE, false, &Gentle);
	const bool IsIntense = !IsGentle && ANY(INTENSE, true, &Intense);
	if (!Gentle.empty() && ANY(INTENSE, false, 0))
		Gentle = "not " + Gentle;
	// The shape most of the words ask for (the words said not to be left out)
	std::string Asked;
	for (size_t i = 0; i < Words.size(); ++i)
		if (!Negated(Words, i))
			Asked += Words[i] + " ";
	const SongShape *pShape = FindSongShape(Asked);
	const SongShape *const pAsked = pShape;	// (before a gentle boss becomes a stage or a night)
	std::vector<std::string> Why;
	std::string ShapeWord;
	if (pShape != 0) {
		const ShapeRow *pRow = RowOf(*pShape);
		const std::vector<std::string> Own = pRow != 0 ? WordsOf(pRow->Words) : std::vector<std::string>();
		for (size_t i = 0; i < Words.size() && ShapeWord.empty(); ++i)
			if (!Negated(Words, i) && std::find(Own.begin(), Own.end(), Words[i]) != Own.end())
				ShapeWord = Words[i];
	}
	if (pShape != 0 && pShape->Id == "boss" && IsGentle) {
		// Danger, but not hectic: a place to explore (a stage), else a calm one
		const SongShape *pStage = ShapeById("stage");
		const ShapeRow *pRow = pStage != 0 ? RowOf(*pStage) : 0;
		const std::vector<std::string> Own = pRow != 0 ? WordsOf(pRow->Words) : std::vector<std::string>();
		std::string Place;
		for (size_t i = 0; i < Words.size() && Place.empty(); ++i)
			if (!Negated(Words, i) && std::find(Own.begin(), Own.end(), Words[i]) != Own.end() && Words[i] != "lively")
				Place = Words[i];
		pShape = ShapeById(Place.empty() ? "night" : "stage");
		Why.push_back("\"" + ShapeWord + "\" but \"" + Gentle + "\": " + (Place.empty() ? "a calm place, not a fight" :
			"\"" + Place + "\" is a place to explore, not a fight"));
	}
	else if (pShape != 0)
		Why.push_back("\"" + ShapeWord + "\"");
	else if (IsGentle) {
		pShape = ShapeById("night");
		Why.push_back("\"" + Gentle + "\"");
	}
	else if (IsIntense) {
		pShape = ShapeById("stage");
		Why.push_back("\"" + Intense + "\"");
	}
	// A warm place indoors (a shop, a hut, an inn, a grandmother keeping the place), not at night, not a dark one and not
	// a fight: the cozy kind, in its shape (a small place's short loop where the words name one, a shop or an inn, else a
	// town's, which a hut gets: 40 bars). Its words were gentle ones, which took it for a night song: "Lost n Found", a
	// cozy, run-down hut, came out in C minor and sounded depressing
	static const char *const COZY[] = { "cozy", "cosy", "homey", "homely", "warm", "warmth", "friendly", "hut", "cabin", "cottage",
		"shop", "store", "inn", "tavern", "bakery", "cafe", "teahouse", "house", "kitchen", "hearth", "fireplace", "indoors", "indoor",
		"granny", "grandma", "grandmother", "grandpa", "elderly", "shopkeeper" };
	static const char *const NIGHT[] = { "night", "nights", "nighttime", "sleep", "sleeping", "sleepy", "asleep", "dream", "dreams",
		"dreamy", "dreaming", "lullaby", "rest", "resting", "moon", "moonlit", "starry" };
	static const char *const DARK[] = { "haunted", "spooky", "creepy", "scary", "eerie", "abandoned", "cursed", "dark", "sinister" };
	std::string Cozy;
	if (ANY(COZY, true, &Cozy) && !ANY(NIGHT, true, 0) && !ANY(DARK, true, 0) && !IsIntense &&
		(pAsked == 0 || pAsked->Id == "night" || pAsked->Id == "town" || pAsked->Id == "small")) {
		Out.Kind = "cozy";
		if (pShape == 0 || pShape->Id == "night") {
			const SongShape *pSmall = ShapeById("small");
			const ShapeRow *pRow = pSmall != 0 ? RowOf(*pSmall) : 0;
			const std::vector<std::string> Own = pRow != 0 ? WordsOf(pRow->Words) : std::vector<std::string>();
			std::string Place;
			for (size_t i = 0; i < Words.size() && Place.empty(); ++i)
				if (!Negated(Words, i) && std::find(Own.begin(), Own.end(), Words[i]) != Own.end())
					Place = Words[i];
			pShape = ShapeById(Place.empty() ? "town" : "small");
			Why.clear();
			Why.push_back("\"" + Cozy + "\": a warm place, cozy" + (Place.empty() ? std::string() : ", \"" + Place + "\" a small one"));
		}
		else if (!Why.empty() && Why.back() == "\"" + Cozy + "\"")
			Why.back() += ": a warm place, cozy";
		else if (!ShapeWord.empty())
			Why.back() = "\"" + Cozy + "\" and \"" + ShapeWord + "\": a warm place, cozy";
		else
			Why.push_back("\"" + Cozy + "\": cozy");
	}
	else if (pShape != 0)
		Out.Kind = pShape->Kind;
	Out.pShape = pShape;
	// Parts asked for, or said not to be
	struct PartAsk {
		const char *Words;
		const char *Part;
	};
	const PartAsk ASKS[] = { { "climax climaxes peak finale highpoint", "climax" }, { "outro ending coda turnaround", "outro" },
		{ "break pause breather", "break" }, { "bridge", "bridge" }, { "intro introduction", "intro" } };
	bool ClimaxAsked = false;
	for (const PartAsk &A : ASKS) {
		const std::vector<std::string> List = WordsOf(A.Words);
		for (size_t i = 0; i < Words.size(); ++i) {
			bool Match = std::find(List.begin(), List.end(), Words[i]) != List.end();
			if (!Match && std::string(A.Part) == "climax" && Words[i] == "high" && i + 1 < Words.size() && Words[i + 1] == "point")
				Match = true;
			if (!Match)
				continue;
			if (Negated(Words, i)) {
				AddOnce(Out.Drop, A.Part);
				Why.push_back(std::string("no ") + A.Part);
			}
			else {
				AddOnce(Out.Add, A.Part);
				Why.push_back(std::string("with ") + Article(A.Part));
				ClimaxAsked = ClimaxAsked || std::string(A.Part) == "climax";
			}
			break;
		}
	}
	// A stage that isn't hectic: no climax, the tune's last time round its high point
	if (pShape != 0 && pShape->Id == "stage" && IsGentle && !ClimaxAsked) {
		AddOnce(Out.Drop, "climax");
		AddOnce(Out.Add, "a'");
		Why.push_back("so no climax: the tune's last time round is its high point");
	}
	// How long
	static const char *const LONGER[] = { "long", "longer", "lengthy", "extended", "big", "bigger", "large", "vast", "huge" };
	static const char *const SHORTER[] = { "short", "shorter", "brief", "quick", "tiny" };
	std::string Length;
	if (ANY(LONGER, true, &Length)) {
		Out.Length = 1.4;
		Why.push_back("\"" + Length + "\": longer");
	}
	else if (ANY(SHORTER, true, &Length) && !(pShape != 0 && pShape->Id == "small")) {
		Out.Length = 0.7;
		Why.push_back("\"" + Length + "\": shorter");
	}
#undef ANY
	for (size_t i = 0; i < Why.size(); ++i)
		Out.Why += (i == 0 ? "" : "; ") + Why[i];
	return Out;
}

bool MakeSongShape(const ShapeRequest &Request, ShapeChoice &Out, std::string &Error)
{
	Out = ShapeChoice();
	// The shape: asked for, else the words', else the general recipe's town
	const ShapeWords Words = ReadShapeWords(Request.About);
	const SongShape *pBase = 0;
	if (!Trimmed(Request.Shape).empty()) {
		pBase = FindSongShape(Request.Shape);
		if (pBase == 0) {
			Error = "shape: one of " + SongShapeIds() + " (or a word for the song: lively, calm, battle, shop, jingle).";
			return false;
		}
	}
	else if (Words.pShape != 0)
		pBase = Words.pShape;
	else {
		// A list of parts alone: the shape the track's name says, else the general one
		pBase = !Trimmed(Request.Parts).empty() ? FindSongShape(Request.TrackName) : 0;
		if (pBase == 0 || !pBase->Loops)
			pBase = ShapeById("town");
	}
	if (pBase == 0) {
		Error = "No shape.";
		return false;
	}
	Out.pBase = pBase;
	if (!Trimmed(Request.About).empty()) {
		std::string Line = Trimmed(Request.Shape).empty() ? "the " + Lower(pBase->Title) + (Words.pShape != 0 ? std::string() :
			std::string(" (no word for a shape, so the general one)")) : "the " + Lower(pBase->Title) + " asked for";
		Out.FromWords = "From the words: " + Line + (Words.Why.empty() ? std::string(".") : " (" + Words.Why + ").");
		Out.Kind = Words.Kind;
		if (Words.Kind == "cozy")
			Out.FromWords += " A cozy place: what_next with the same about (or kind cozy) follows the cozy recipe, in a major key.";
	}
	const std::string &Title = pBase->Title;
	FormList Form;
	SongShape &Shape = Out.Shape;
	Shape = *pBase;
	if (!Trimmed(Request.Parts).empty()) {
		if (!Request.Add.empty() || !Request.Drop.empty() || Request.Bars > 0) {
			Error = "parts is the whole list of parts: leave add, drop and bars out with it (or write the parts you want into it).";
			return false;
		}
		std::vector<ShapePart> Parts;
		if (!ReadShapeParts(Request.Parts, Parts, Error))
			return false;
		Form = FormOf(Parts);
		if (BodyCount(Form) < 1) {
			Error = "parts: give at least one part with a tune (A, B...), not only an intro, a break or an outro.";
			return false;
		}
		// A repeat (and a climax, which starts from A's tune) no longer than the part it plays again
		for (size_t i = 0; i < Form.size(); ++i) {
			const NameParts P = SplitName(Form[i].Name);
			for (size_t j = 0; j < i; ++j) {
				const NameParts Q = SplitName(Form[j].Name);
				const bool Repeats = P.Repeat() && P.Base == Q.Base && !Q.Repeat();
				const bool Climbs = Form[i].Kind == PK_CLIMAX && Form[j].Kind == PK_A && !Q.Repeat();
				if ((Repeats || Climbs) && Form[i].Bars > Form[j].Bars) {
					Error = "parts: " + Form[i].Name + (Repeats ? " plays " : " starts from ") + Form[j].Name + "'s music again, so it takes " +
						Num(Form[j].Bars) + " bars or fewer, not " + Num(Form[i].Bars) + ".";
					return false;
				}
			}
		}
		if (!pBase->Loops && Form.size() > 4) {
			Error = "parts: a jingle plays once and stops: a phrase or two and a last chord, not " + Num(static_cast<int>(Form.size())) +
				" parts.";
			return false;
		}
	}
	else {
		const std::vector<SongShape> Variants = VariantsOf(*pBase);
		Out.Variants = static_cast<int>(Variants.size());
		if (Request.Variant > Out.Variants || Request.Variant < 0) {
			Error = Out.Variants == 1 ? "variant: the " + Lower(Title) + " has one form only (variant 1)." : "variant: 1 to " +
				Num(Out.Variants) + " for the " + Lower(Title) + ".";
			return false;
		}
		Out.VariantFromName = Request.Variant == 0;
		Out.Variant = Request.Variant > 0 ? Request.Variant : VariantOfName(Request.TrackName, Out.Variants);
		for (size_t v = 0; v < Variants.size(); ++v)
			if (static_cast<int>(v) + 1 != Out.Variant)
				Out.Others.push_back("variant " + Num(static_cast<int>(v) + 1) + ": " + Variants[v].PartsText());
		Form = FormOf(Variants[static_cast<size_t>(Out.Variant - 1)].Parts);
		const int VariantBars = FormBars(Form);

		// Parts added and dropped: the request's own, else the words'
		const bool OwnAdd = !Request.Add.empty(), OwnDrop = !Request.Drop.empty();
		const std::vector<std::string> &Adds = OwnAdd ? Request.Add : Words.Add;
		const std::vector<std::string> &Drops = OwnDrop ? Request.Drop : Words.Drop;
		if (!pBase->Loops && (OwnAdd || OwnDrop)) {
			Error = "A jingle is a phrase and a last chord that plays once: it takes no other parts (bars makes its phrase longer, up "
				"to 8 bars in all).";
			return false;
		}
		std::vector<part_kind_t> Added, Dropped;
		for (int Pass = 0; Pass < 2 && pBase->Loops; ++Pass) {
			const std::vector<std::string> &List = Pass == 0 ? Drops : Adds;
			const bool Own = Pass == 0 ? OwnDrop : OwnAdd;
			for (size_t i = 0; i < List.size(); ++i) {
				part_kind_t Kind;
				bool Itself;
				if (!PartWord(List[i], Kind, Itself)) {
					Error = std::string(Pass == 0 ? "drop" : "add") + ": " + ShapePartWords() + " (not \"" + Trimmed(List[i]) + "\").";
					return false;
				}
				if (Pass == 1 && std::find(Dropped.begin(), Dropped.end(), Kind) != Dropped.end() && Kind != PK_A && Kind != PK_B) {
					Error = std::string("The ") + KindWord(Kind) + " is both added and dropped: say one.";
					return false;
				}
				std::string Why;
				const bool Done = Pass == 0 ? DropKind(Form, Kind, Itself, Title, Why) : AddKind(Form, Kind, Title, Why);
				if (!Done) {
					if (Own) {
						Error = Why;
						return false;
					}
					continue;	// a word about the place that doesn't fit this form
				}
				(Pass == 0 ? Dropped : Added).push_back(Kind);
				Out.Changes.push_back(Pass == 0 ? std::string("Dropped the ") + KindWord(Kind) + "." : std::string("Added ") +
					Article(KindWord(Kind)) + ".");
			}
		}
		// The length: asked, else the words' (longer or shorter than the variant), in whole phrases
		int Target = Request.Bars;
		if (Target == 0 && Words.Length != 1.0)
			Target = std::max(8, static_cast<int>(VariantBars * Words.Length / 4.0 + 0.5) * 4);
		if (Target != 0 && (Target < (pBase->Loops ? 8 : 4) || Target > 256)) {
			Error = pBase->Loops ? "bars: from 8 to 256 (about how long the song is)." : "bars: a jingle takes 4 to 8 bars.";
			return false;
		}
		if (Target > 0 && !pBase->Loops) {
			// A jingle: its phrase longer, up to 8 bars in all
			if (Target > 8) {
				Error = "bars: a jingle plays once in 8 bars at most.";
				return false;
			}
			const int Phrase = std::max(2, (Target - Form.back().Bars) / 2 * 2);
			Form.front().Bars = Phrase;
		}
		else if (Target > 0) {
			const int Before = FormBars(Form);
			const std::string Was = FormText(Form);
			Form = FitBars(Form, pBase->Id, Target, Added, Dropped);
			const int After = FormBars(Form);
			if (After != Before)
				Out.Changes.push_back((After > Before ? "Made longer, toward " : "Made shorter, toward ") + Num(Target) + " bars, in the " +
					Lower(Title) + "'s own way: " + Was + " became " + FormText(Form) + ".");
			if (After != Target)
				Out.Changes.push_back(Num(After) + " bars is the nearest to " + Num(Target) + " in whole phrases" + (After == Before ?
					" (the form as it was)" : "") + ".");
		}
	}
	// The form as a shape: each part's line, the loop after the intro
	Shape.Parts.clear();
	for (size_t i = 0; i < Form.size(); ++i) {
		ShapePart Part;
		Part.Name = Form[i].Name;
		Part.Bars = Form[i].Bars;
		Part.Line = PartLine(Part.Name, pBase);
		Shape.Parts.push_back(Part);
	}
	Shape.LoopPart = Shape.Loops && !Form.empty() && Form[0].Kind == PK_INTRO && Form.size() > 1 ? 1 : 0;
	if (Shape.Bars() > 256) {
		Error = "The form takes " + Num(Shape.Bars()) + " bars; 256 at most.";
		return false;
	}
	return true;
}

const SongShape *LaidOutShape(const SectionList &Sections, const std::string &TrackName, double Bpm, const SongShape *pPrefer)
{
	if (pPrefer != 0) {
		const SongShape *pExact = MatchSongShape(Sections, TrackName, Bpm, pPrefer);
		if (pExact == pPrefer)
			return pExact;
	}
	// Read as a laid-out form: from the first frame on without a gap, every name a part's, a repeat marked with primes
	bool Reads = Sections.size() >= 3 && Sections[0].First == 0;
	bool Marked = false;
	for (size_t i = 0; i < Sections.size() && Reads; ++i) {
		const NameParts P = SplitName(Sections[i].Name);
		const part_kind_t Kind = KindOfPart(Sections[i].Name);
		Reads = Kind != PK_OTHER && P.Number == 0 && (i == 0 || Sections[i].First == Sections[i - 1].Last + 1);
		Marked = Marked || P.Primes > 0 || Kind == PK_CLIMAX || Kind == PK_BRIDGE || Kind == PK_OUTRO;
	}
	Reads = Reads && Marked;
	if (Reads && pPrefer != 0)
		return pPrefer;
	const SongShape *pExact = MatchSongShape(Sections, TrackName, Bpm);
	if (pExact != 0 || !Reads)
		return pExact;
	// The shape whose parts' names the sections share most, in order (on a tie a calm one's night, else the one the
	// track's name says)
	const SongShape *pNamed = FindSongShape(TrackName);
	const bool Calm = (pNamed != 0 && pNamed->Id == "night") || (Bpm > 0.0 && Bpm <= 90.0);
	const std::vector<SongShape> &All = SongShapes();
	const SongShape *pBest = 0;
	int Best = 1;
	for (size_t s = 0; s < All.size(); ++s) {
		const SongShape &Shape = All[s];
		if (!Shape.Loops)
			continue;
		const size_t N = Sections.size(), M = Shape.Parts.size();
		std::vector<std::vector<int> > Common(N + 1, std::vector<int>(M + 1, 0));
		for (size_t i = 1; i <= N; ++i)
			for (size_t j = 1; j <= M; ++j)
				Common[i][j] = SameName(Sections[i - 1].Name, Shape.Parts[j - 1].Name) ? Common[i - 1][j - 1] + 1 :
					std::max(Common[i - 1][j], Common[i][j - 1]);
		const int Score = Common[N][M];
		if (Score > Best || (Score == Best && pBest != 0 && ((Calm && Shape.Id == "night") || (!Calm && &Shape == pNamed)))) {
			Best = Score;
			pBest = &Shape;
		}
	}
	return pBest;
}

}	// namespace ftapi

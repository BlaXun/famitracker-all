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

#include "Arrange.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <functional>

namespace ftapi {

// Styles

const std::vector<StyleInfo> &ChordStyles()
{
	static const std::vector<StyleInfo> STYLES = {
		{ "arpeggio", "each chord held as FamiTracker's fast 0xy arpeggio, struck again on the rhythm (once a bar by "
			"default): the classic NES shimmer", "arp|arpeggiated|shimmer|held|sustained" },
		{ "stabs", "short 0xy chord hits on a rhythm, offbeats by default: bouncy, for upbeat towns, pop and ska",
			"stab|hits|offbeat|skank|staccato|bouncy" },
		{ "broken", "the chord's notes one after another in a pattern (up, down, updown, alberti, rolling): flowing, for "
			"calm and RPG music and ballads", "brokenchords|arpeggios|flowing|harp|picked" },
		{ "guide", "one long note a chord on its third or seventh, moving by the smallest steps: a quiet pad line under a "
			"busy melody", "guidetones|pad|sustain|long|line" },
		{ "lush", "each chord an instrument of its own whose arpeggio loops through four or more of its notes, two frames "
			"each, so sevenths and ninths ring (plain chords get them: color rich by default); held like arpeggio, softly "
			"(volume 6) under the melody: lush, jazzy and atmospheric, on 2A03, MMC5 or VRC6 pulses",
			"lusharp|jazzarp|jazzy|atmospheric|instrumentarp|instrumentarpeggio|slowarp|sevenths" },
		{ "voices", "each chord's notes on channels of their own (chord_channels, 2-4 of them), its third and seventh first, "
			"each voice moving to the next chord's nearest note: real chords on the VRC7's FM channels (an electric piano "
			"comping), the N163's or the pulses; held by default, or struck on a rhythm", "spread|shell|shellvoicing|comp|"
			"comping|multichannel|polyphonic|piano|epiano|pad" },
	};
	return STYLES;
}

const std::vector<StyleInfo> &ChordRhythms()
{
	static const std::vector<StyleInfo> RHYTHMS = {
		{ "bars", "once a bar and on chord changes", "bar|once|whole|held" },
		{ "beats", "every beat", "beat|quarters|quarter|crotchets" },
		{ "offbeats", "between the beats (the \"and\"s): oom-pah with a bass on the beats", "offbeat|upbeats|ands|skank|reggae|ska" },
		{ "eighths", "every half beat: driving", "eighth|8ths|quavers|straight" },
		{ "half", "every half bar", "halves|halfnotes|minims" },
		{ "tresillo", "3+3+2 eighths (beats 1, 2.5 and 4): pop and Latin syncopation", "332|syncopated|pop|latin|habanera" },
		{ "charleston", "beat 1 and the \"and\" of 2: swing", "swing|jazz" },
		{ "waltz", "every beat but the first: oom-pah-pah", "oompahpah|3/4|three" },
	};
	return RHYTHMS;
}

const std::vector<StyleInfo> &BrokenPatterns()
{
	static const std::vector<StyleInfo> PATTERNS = {
		{ "up", "low to high", "ascending|rising" },
		{ "down", "high to low", "descending|falling" },
		{ "updown", "up and back down", "upanddown|pingpong|bounce" },
		{ "alberti", "low, high, middle, high: classical", "classical|albertibass" },
		{ "rolling", "root, fifth, then the third and a top note an octave up, up and back: wide and flowing, like an RPG "
			"prelude", "open|wide|flowing|prelude|rpg" },
	};
	return PATTERNS;
}

const std::vector<StyleInfo> &BassStyles()
{
	static const std::vector<StyleInfo> STYLES = {
		{ "root_fifth", "the root, then the fifth halfway through the bar", "rootfifth|default|twofeel|countryboom" },
		{ "roots", "the root held for each chord: plain and calm", "root|held|whole|sustained|plain" },
		{ "half", "the root every half bar", "halves|halfnotes" },
		{ "octaves", "the root jumping between octaves every half beat: energetic, disco, action", "octave|disco|jumping|bounce" },
		{ "walking", "a note every beat walking to the next chord's root, with approach notes: jazz, swing, towns",
			"walk|jazz|swing" },
		{ "syncopated", "the root on 1 and on the \"and\" of 2, the fifth on 4 (3+3+2): pop, funk, Latin",
			"syncopation|tresillo|funk|pop|latin|332" },
		{ "arpeggio", "root, fifth, octave, fifth every half beat: rolling, adventurous", "arpeggiated|arp|rolling" },
		{ "driving", "the root every half beat, short: rock and battle", "eighths|pumping|rock|battle|chug" },
		{ "pedal", "the key's tonic held under every chord: tension, suspense", "drone|pedalpoint|tonic" },
	};
	return STYLES;
}

const std::vector<StyleInfo> &Grooves()
{
	static const std::vector<StyleInfo> GROOVES = {
		{ "basic", "rock beat: kick on 1 and 3, snare on 2 and 4, hi-hats every half beat; with kit samples, a stage's sampled "
			"backbeat", "rock|standard|pop|backbeat|normal|stage|level|explore|platformer|bouncy" },
		{ "four_on_floor", "kick every beat, snare on 2 and 4, open hi-hats between the beats: dance, disco, upbeat",
			"fouronthefloor|dance|disco|house|fourfloor" },
		{ "half_time", "kick on 1, snare on 3: heavy and slow", "halftime|heavy|slow" },
		{ "shuffle", "swung hi-hats: swing, blues", "swing|swung|blues" },
		{ "march", "snare on and between the beats: military, heroic", "military|marching|heroic|snareroll" },
		{ "breakbeat", "syncopated kick and ghost snares: funky", "funk|funky|breaks|break" },
		{ "calm", "soft kick on 1, a quiet snare on 3, hi-hats on the beats: ambient, town, ballad",
			"soft|light|ambient|ballad|quiet" },
		{ "driving", "kicks on 1 and 3 and their \"and\"s, hi-hats every quarter beat: battle and action",
			"battle|action|fast|intense|boss" },
		{ "waltz", "in three: kick on 1, soft snares on 2 and 3", "3/4|three|triple" },
		{ "lively", "soft closed hi-hats on the eighths (sixteenth pairs now and then, more when busy), an open hi-hat every "
			"other bar, a soft kick on 1 and the \"and\" of 2 or 3, no snare; small fills (a hi-hat roll or soft toms) and a "
			"soft splash after: a gentle place, not too upbeat (a stage's beat is basic, with kit samples)",
			"flowing|flow|gentle|river|soft stage|calm stage" },
	};
	return GROOVES;
}

const std::vector<StyleInfo> &HarmonyIntervals()
{
	static const std::vector<StyleInfo> INTERVALS = {
		{ "third_below", "a third under the melody, in the key and on the chords' notes: the classic sweet duet",
			"third|thirds|3rdbelow|below" },
		{ "sixth_below", "a sixth under: wider and warmer", "sixth|sixths|6thbelow" },
		{ "third_above", "a third over the melody: bright", "above|3rdabove|thirdover" },
		{ "fifth_below", "a fifth under: hollow, medieval, heroic", "fifth|fifths|5thbelow|power" },
		{ "octave_below", "the melody an octave lower: fuller and bolder", "octave|octaves|unison|doubled" },
	};
	return INTERVALS;
}

namespace {

std::string Simple(const std::string &Text)
{
	std::string Out;
	for (size_t i = 0; i < Text.size(); ++i) {
		const unsigned char c = static_cast<unsigned char>(Text[i]);
		if (std::isalnum(c) || c == '/')
			Out += static_cast<char>(std::tolower(c));
	}
	return Out;
}

}	// namespace

int FindStyle(const std::vector<StyleInfo> &Styles, const std::string &Name)
{
	const std::string Wanted = Simple(Name);
	if (Wanted.empty())
		return -1;
	for (size_t i = 0; i < Styles.size(); ++i)
		if (Simple(Styles[i].Name) == Wanted)
			return static_cast<int>(i);
	for (size_t i = 0; i < Styles.size(); ++i) {
		const std::string Aliases = Styles[i].Aliases;
		size_t From = 0;
		while (From <= Aliases.size()) {
			size_t To = Aliases.find('|', From);
			if (To == std::string::npos)
				To = Aliases.size();
			if (Simple(Aliases.substr(From, To - From)) == Wanted)
				return static_cast<int>(i);
			From = To + 1;
		}
	}
	return -1;
}

std::vector<std::string> StyleNames(const std::vector<StyleInfo> &Styles)
{
	std::vector<std::string> Names;
	for (size_t i = 0; i < Styles.size(); ++i)
		Names.push_back(Styles[i].Name);
	return Names;
}

std::string StyleList(const std::vector<StyleInfo> &Styles)
{
	std::string Out;
	for (size_t i = 0; i < Styles.size(); ++i) {
		if (i > 0)
			Out += "; ";
		Out += std::string(Styles[i].Name) + " (" + Styles[i].Description + ")";
	}
	return Out;
}

namespace {

int Mod12(int Value)
{
	return ((Value % 12) + 12) % 12;
}

int Clamp(int Value, int Low, int High)
{
	return std::max(Low, std::min(High, Value));
}

// The pitch of a pitch class nearest Near within [Low, High]; the first one
// above Low when the range is too small to have one
int NearestPitch(int PitchClass, int Near, int Low, int High)
{
	int Best = -1;
	for (int p = Low; p <= High; ++p)
		if (Mod12(p) == Mod12(PitchClass) && (Best < 0 || std::abs(p - Near) < std::abs(Best - Near)))
			Best = p;
	if (Best < 0)
		Best = Low + Mod12(PitchClass - Low);
	return Best;
}

// A bass note: near the one before, leaning to the lower half of the register
int BassPitch(int PitchClass, int Previous, int Low, int High)
{
	const int Middle = (Low + High) / 2;
	int Best = -1;
	double BestCost = 1e9;
	for (int p = Low; p <= High; ++p) {
		if (Mod12(p) != Mod12(PitchClass))
			continue;
		const double Cost = std::abs(p - Previous) + 0.4 * std::max(0, p - Middle);
		if (Cost < BestCost) {
			BestCost = Cost;
			Best = p;
		}
	}
	if (Best < 0)
		Best = Low + Mod12(PitchClass - Low);
	return Best;
}

// The next pitch of the key's scale above (Direction 1) or below (-1); in a
// harmonic minor that's three semitones between its sixth and its raised seventh
int ScaleStep(int Pitch, int Direction, const Key &K)
{
	for (int Step = 1; Step <= 3; ++Step)
		if (InKey(Pitch + Direction * Step, K) && !(K.Mode == MODE_MINOR && Mod12(Pitch + Direction * Step - K.Tonic) == 11 &&
			Step == 1 && InKey(Pitch + Direction * 2, K) && Direction < 0))
			return Pitch + Direction * Step;
	return Pitch + Direction;
}

// The parts of the chords within each bar
struct Segment {
	int Row, End;
	const ChordAt *pChord;
	const BarSpan *pBar;
};

std::vector<Segment> Segments(const std::vector<ChordAt> &Chords, const std::vector<BarSpan> &Bars)
{
	std::vector<Segment> Out;
	for (size_t b = 0; b < Bars.size(); ++b) {
		const int BarEnd = Bars[b].Row + Bars[b].Rows;
		for (size_t c = 0; c < Chords.size(); ++c) {
			const int From = std::max(Chords[c].Row, Bars[b].Row);
			const int To = std::min(Chords[c].Row + Chords[c].Rows, BarEnd);
			if (To <= From)
				continue;
			Segment S;
			S.Row = From;
			S.End = To;
			S.pChord = &Chords[c];
			S.pBar = &Bars[b];
			Out.push_back(S);
		}
	}
	std::stable_sort(Out.begin(), Out.end(), [](const Segment &a, const Segment &b) { return a.Row < b.Row; });
	return Out;
}

const Segment *SegmentAt(const std::vector<Segment> &Segs, int Row)
{
	for (size_t i = 0; i < Segs.size(); ++i)
		if (Segs[i].Row <= Row && Row < Segs[i].End)
			return &Segs[i];
	return 0;
}

// How much softer than the part's volume a note on Row is: none on a bar's
// first row, 1 on its other beats, 2 between the beats
int Softer(int Row, const BarSpan &Bar, int RowsPerBeat)
{
	const int Offset = Row - Bar.Row;
	if (Offset == 0)
		return 0;
	return Offset % std::max(1, RowsPerBeat) == 0 ? 1 : 2;
}

PartNote MakeNote(int Row, int Rows, int Pitch, int Volume)
{
	PartNote N;
	N.Row = Row;
	N.Rows = std::max(1, Rows);
	N.Pitch = Clamp(Pitch, 0, PITCHES - 1);
	N.Volume = Volume < 0 ? -1 : Clamp(Volume, 1, 15);
	return N;
}

// Where a rhythm strikes in a bar, as rows from its start
std::vector<int> RhythmRows(int Rhythm, int BarRows, int RowsPerBeat)
{
	const int R = std::max(1, RowsPerBeat);
	const int Beats = std::max(1, BarRows / R);
	const int Half = R / 2;
	std::vector<int> Rows;
	switch (Rhythm) {
	case RHYTHM_BEATS:
		for (int b = 0; b < Beats; ++b)
			Rows.push_back(b * R);
		break;
	case RHYTHM_OFFBEATS:
		for (int b = 0; b < Beats; ++b)
			Rows.push_back(b * R + (Half > 0 ? Half : 0));
		break;
	case RHYTHM_EIGHTHS:
		for (int b = 0; b < Beats; ++b) {
			Rows.push_back(b * R);
			if (Half > 0)
				Rows.push_back(b * R + Half);
		}
		break;
	case RHYTHM_HALF:
		Rows.push_back(0);
		if (Beats >= 2)
			Rows.push_back(Beats / 2 * R);
		break;
	case RHYTHM_TRESILLO:
		Rows.push_back(0);
		Rows.push_back(3 * R / 2);
		Rows.push_back(3 * R);
		break;
	case RHYTHM_CHARLESTON:
		Rows.push_back(0);
		Rows.push_back(3 * R / 2);
		break;
	case RHYTHM_WALTZ:
		for (int b = 1; b < Beats; ++b)
			Rows.push_back(b * R);
		if (Beats == 1)
			Rows.push_back(0);
		break;
	default:	// RHYTHM_BARS
		Rows.push_back(0);
		break;
	}
	std::vector<int> Out;
	for (size_t i = 0; i < Rows.size(); ++i)
		if (Rows[i] >= 0 && Rows[i] < BarRows && std::find(Out.begin(), Out.end(), Rows[i]) == Out.end())
			Out.push_back(Rows[i]);
	std::sort(Out.begin(), Out.end());
	return Out;
}

// How much a tone of a chord tells of it, for choosing which to play
double ToneWeight(const Chord &C, int Interval, bool RootInBass)
{
	const int i = Mod12(Interval);
	const bool HasThird = C.Has(3) || C.Has(4);
	if (i == 0)
		return RootInBass ? 2.0 : 3.0;
	if (i == 3 || i == 4)
		return (i == 3 && C.Has(4)) ? 1.8 : 3.0;		// a sharp ninth, or the third
	if (!HasThird && (i == 2 || i == 5))
		return 3.0;									// a suspension stands for the third
	if (i == 7)
		return C.Tones.size() <= 2 ? 3.0 : 0.8;
	if (i == 6 || i == 8)
		return C.Has(7) ? 1.6 : 2.5;				// an added #11 or b13, or an altered fifth
	if (i == 10 || i == 11)
		return 2.6;
	if (i == 9)
		return (C.Has(10) || C.Has(11)) ? 1.8 : 2.4;	// a thirteenth, or the sixth
	if (i == 1 || i == 2)
		return 2.0;									// ninths
	return 1.5;
}

// The room a chord keeps while it sounds on [Row, End): its notes from Floor to Ceiling, under the lowest of the
// melody's notes sounding there (CHORD_ROOM under it) and over the highest of the bass's (BASS_ROOM over it), or the
// part's limits where neither plays. Bass is the bass's highest note there, -1000 for none.
struct Room {
	int Floor, Ceiling, Bass;
	bool Set() const { return Floor > -1000 || Ceiling < 1000; }
};

Room RoomFor(const ChordPartOptions &O, int Row, int End)
{
	Room R;
	R.Floor = O.Floor >= 0 ? O.Floor : -1000;
	R.Ceiling = O.Ceiling >= 0 ? O.Ceiling : 1000;
	R.Bass = -1000;
	int Lowest = 1000;
	for (size_t i = 0; i < O.Above.size(); ++i)
		if (O.Above[i].Row < End && O.Above[i].Row + O.Above[i].Rows > Row)
			Lowest = std::min(Lowest, O.Above[i].Pitch);
	for (size_t i = 0; i < O.Below.size(); ++i)
		if (O.Below[i].Row < End && O.Below[i].Row + O.Below[i].Rows > Row)
			R.Bass = std::max(R.Bass, O.Below[i].Pitch);
	if (Lowest < 1000)
		R.Ceiling = Lowest - CHORD_ROOM;
	else
		for (size_t i = 0; i < O.Open.size(); ++i)
			if (O.Open[i].Row <= Row && End <= O.Open[i].Row + O.Open[i].Rows)
				R.Ceiling = 1000;	// bars the melody doesn't play in
	if (R.Bass > -1000)
		R.Floor = R.Bass + BASS_ROOM;
	return R;
}

// What a chord's notes from Bottom to Top cost for leaving their room: a top a whole step under the melody's lowest note
// a little (where a chord's shape fits no lower), a semitone under it or into the melody a lot, and each semitone into the
// bass as much as moving twelve
double RoomCost(const Room &R, int Bottom, int Top)
{
	const int Over = Top - R.Ceiling;
	return (Over >= 1 ? 1.0 : 0.0) + 6.0 * std::max(0, Over - 1) + 3.0 * std::max(0, R.Floor - Bottom);
}

// The pitch of a chord's lowest note (PitchClass) whose notes, reaching Span above it, fit the room best, near Near and
// in [Low, High] where they can; never under Least
int RoomyPitch(int PitchClass, int Span, int Near, int Low, int High, const Room &R, int Least = 0)
{
	int Best = -1;
	double BestCost = 1e18;
	for (int p = std::max(std::max(0, Least), Low - 36); p <= std::min(PITCHES - 1, std::max(High, Least) + 12); ++p) {
		if (Mod12(p - PitchClass) != 0)
			continue;
		const double Cost = 0.25 * std::abs(p - Near) + 0.5 * (std::max(0, Low - p) + std::max(0, p - High)) + RoomCost(R, p, p + Span);
		if (Cost < BestCost) {
			BestCost = Cost;
			Best = p;
		}
	}
	return Best >= 0 ? Best : NearestPitch(PitchClass, Near, Low, High);
}

// A tone of a broken chord over its room's ceiling (into the melody: within a semitone of its lowest note, or over it)
// goes an octave down where that keeps the tones apart and over the floor
void FoldUnder(std::vector<int> &Tones, size_t Used, const Room &Space)
{
	for (int Pass = 0; Pass < 4 && Space.Set(); ++Pass) {
		const size_t Top = std::min(Used, Tones.size()) - 1;
		const int Lower = Tones[Top] - 12;
		if (Tones[Top] <= Space.Ceiling + 1 || Lower < Space.Floor - BASS_ROOM ||
			std::find(Tones.begin(), Tones.end(), Lower) != Tones.end())
			break;
		Tones[Top] = Lower;
		std::sort(Tones.begin(), Tones.end());
	}
}

// The notes of a chord for a broken pattern, low to high, in the room the melody and the bass leave it when Space is set
std::vector<int> BrokenTones(const Chord &C, int Pattern, int Near, int Low, int High, bool RootInBass, const Room &Space)
{
	std::vector<int> Tones;
	if (Pattern == BROKEN_ROLLING) {
		// Open: root, fifth, then the third and a top note an octave up
		const int Fifth = C.Fifth() >= 0 ? C.Fifth() : 7;
		const int Third = C.Third() >= 0 ? C.Third() : 0;
		const int Top = C.Seventh() >= 0 ? C.Seventh() : C.Has(2) && Third != 2 ? 2 : C.Has(9) ? 9 : Fifth;
		const int Root = Space.Set() ? RoomyPitch(C.Root, 12 + std::max(Third, Top), Near, Low, High, Space) :
			NearestPitch(C.Root, Near, Low, High);
		Tones.push_back(Root);
		Tones.push_back(Root + Fifth);
		Tones.push_back(Root + 12 + Third);
		Tones.push_back(Root + 12 + Top);
		std::sort(Tones.begin(), Tones.end());
		Tones.erase(std::unique(Tones.begin(), Tones.end()), Tones.end());
		while (Tones.size() < 4)
			Tones.push_back(Tones[Tones.size() - 2] + 12);
		FoldUnder(Tones, 4, Space);
		return Tones;
	}
	// Close: the four tones that tell most (or three and the lowest an octave up),
	// from the inversion whose lowest note is nearest the last chord's (and, with
	// a room, whose notes keep in it)
	std::vector<int> Classes = C.PitchClasses();
	std::stable_sort(Classes.begin(), Classes.end(), [&](int a, int b) {
		return ToneWeight(C, a - C.Root, RootInBass) > ToneWeight(C, b - C.Root, RootInBass);
	});
	if (Classes.size() > 4)
		Classes.resize(4);
	const size_t Used = Pattern == BROKEN_ALBERTI ? 3 : 4;	// alberti plays the lowest three
	auto Build = [&](int Base, int Lowest) {
		std::vector<int> Out;
		for (size_t i = 0; i < Classes.size(); ++i)
			Out.push_back(Base + Mod12(Classes[i] - Lowest));
		std::sort(Out.begin(), Out.end());
		while (Out.size() < 4)
			Out.push_back(Out[Out.size() < 3 ? Out.size() - 1 : Out.size() - 3] + 12);
		std::sort(Out.begin(), Out.end());
		return Out;
	};
	int BestBase = 0;
	double BestCost = 1e9;
	int BestLowest = C.Root;
	for (size_t i = 0; i < Classes.size(); ++i) {
		const int Nearest = NearestPitch(Classes[i], Near, Low, High);
		for (int Octave = Space.Set() ? -2 : 0; Octave <= (Space.Set() ? 1 : 0); ++Octave) {
			const int Base = Nearest + 12 * Octave;
			if (Base < 0)
				continue;
			double Cost = std::abs(Base - Near) + (Classes[i] == C.Root ? 0.0 : 0.5);
			if (Space.Set())
				Cost += 0.5 * (std::max(0, Low - Base) + std::max(0, Base - High)) + RoomCost(Space, Base, Build(Base, Classes[i])[Used - 1]);
			if (Cost < BestCost) {
				BestCost = Cost;
				BestBase = Base;
				BestLowest = Classes[i];
			}
		}
	}
	Tones = Build(BestBase, BestLowest);
	FoldUnder(Tones, Used, Space);
	return Tones;
}

// A note of a chord for a slow line: its third or seventh, else another tone,
// near Near and moving as little as it can (in its room, when Space is set)
int GuidePitch(const Chord &C, int Near, int Low, int High, const Room &Space)
{
	int Best = Near;
	double BestCost = 1e9;
	const int From = Space.Set() ? std::max(0, Low - 12) : Low;
	for (size_t t = 0; t < C.Tones.size(); ++t) {
		const int Interval = C.Tones[t];
		double Weight;
		if (Interval == C.Third())
			Weight = 3.0;
		else if (Interval == C.Seventh())
			Weight = 2.6;
		else if (Interval == 0)
			Weight = 1.0;
		else if (Interval == C.Fifth())
			Weight = 1.2;
		else
			Weight = 2.0;
		for (int p = From; p <= High; ++p) {
			if (Mod12(p - C.Root) != Interval)
				continue;
			double Cost = 0.6 * std::abs(p - Near) - Weight - (p == Near ? 0.5 : 0.0);
			if (Space.Set())
				Cost += 0.5 * std::max(0, Low - p) + RoomCost(Space, p, p);
			if (Cost < BestCost) {
				BestCost = Cost;
				Best = p;
			}
		}
	}
	return Best;
}

}	// namespace

// Chords

ChordPartOptions::ChordPartOptions() : Style(CHORDS_ARPEGGIO), Rhythm(-1), Pattern(BROKEN_UP), StepRows(0), RowsPerBeat(4),
	Low(31), High(43), Volume(15), RootInBass(true), Ceiling(-1), Floor(-1), Lowest(-1)
{
}

Voicing ArpeggioVoicing(const Chord &C, const Voicing *pPrevious, int Near, int Low, int High, bool RootInBass, int Floor,
	int Ceiling)
{
	const std::vector<int> Classes = C.PitchClasses();
	const int Around = pPrevious != 0 ? pPrevious->Base : Near;
	Room Space;
	Space.Floor = Floor;
	Space.Ceiling = Ceiling;
	Space.Bass = -1000;
	Voicing Best;
	if (Classes.size() <= 2) {
		// The root, the other tone (a fifth in a power chord) and the octave
		Best.Base = Space.Set() ? RoomyPitch(C.Root, 12, Around, Low, High, Space) : NearestPitch(C.Root, Around, Low, High);
		Best.X = Classes.size() == 2 ? Mod12(Classes[1] - Classes[0]) : 7;
		Best.Y = 12;
		if (Best.X == 0)
			Best.X = 7;
		return Best;
	}
	double BestCost = 1e18;
	const size_t n = Classes.size();
	for (size_t a = 0; a < n; ++a) {
		for (size_t b = a + 1; b < n; ++b) {
			for (size_t c = b + 1; c < n; ++c) {
				const int Set[3] = { Classes[a], Classes[b], Classes[c] };
				double Weight = 0.0;
				for (int k = 0; k < 3; ++k)
					Weight += ToneWeight(C, Set[k] - C.Root, RootInBass);
				for (int Lowest = 0; Lowest < 3; ++Lowest) {
					const int t0 = Set[Lowest], t1 = Set[(Lowest + 1) % 3], t2 = Set[(Lowest + 2) % 3];
					for (int Order = 0; Order < 2; ++Order) {
						const int x = Mod12((Order == 0 ? t1 : t2) - t0);
						int y = Mod12((Order == 0 ? t2 : t1) - t0);
						if (y < x)
							y += 12;
						if (x == 0 || y == x || y > 15)
							continue;
						// Where it goes: in the register near the chord before; with a room, an octave lower or higher too
						const int Nearest = NearestPitch(t0, Around, Low, High);
						for (int Octave = Space.Set() ? -2 : 0; Octave <= (Space.Set() ? 1 : 0); ++Octave) {
							const int Base = Nearest + 12 * Octave;
							if (Base < 0 || Base + y >= PITCHES)
								continue;
							double Cost = -Weight;
							if (pPrevious != 0)
								Cost += 0.15 * (std::abs(Base - pPrevious->Base) + std::abs(Base + x - pPrevious->Base - pPrevious->X) +
									std::abs(Base + y - pPrevious->Base - pPrevious->Y));
							else
								Cost += 0.15 * std::abs(Base - Near);
							const int Inversion = Mod12(t0 - C.Root);
							if (Inversion != 0)
								Cost += Inversion == 7 ? 0.9 : 0.6;
							const int Gaps[2] = { x, y - x };
							for (int g = 0; g < 2; ++g)
								Cost += Gaps[g] == 1 ? 2.0 : Gaps[g] == 2 ? 0.5 : 0.0;
							if (y > 12)
								Cost += 0.3;
							if (Space.Set())
								Cost += 0.5 * (std::max(0, Low - Base) + std::max(0, Base - High)) + RoomCost(Space, Base, Base + y);
							if (Cost < BestCost) {
								BestCost = Cost;
								Best.Base = Base;
								Best.X = x;
								Best.Y = y;
							}
						}
					}
				}
			}
		}
	}
	return Best;
}

std::vector<PartNote> ChordPart(const std::vector<ChordAt> &Chords, const std::vector<BarSpan> &Bars, const ChordPartOptions &O)
{
	std::vector<PartNote> Out;
	if (Chords.empty() || Bars.empty())
		return Out;
	const int R = std::max(1, O.RowsPerBeat);
	const int First = Bars.front().Row, End = Bars.back().Row + Bars.back().Rows;
	const std::vector<Segment> Segs = Segments(Chords, Bars);
	const int Low = std::min(O.Low, O.High), High = std::max(O.Low, O.High);
	const int Middle = (Low + High) / 2;
	auto VolumeAt = [&](int Row, const BarSpan &Bar, int Least) -> int {
		if (O.Volume < 0)
			return -1;
		return std::max(1, O.Volume - std::max(0, Softer(Row, Bar, R) - Least));
	};

	if (O.Style == CHORDS_GUIDE) {
		int Previous = Middle + 5;
		for (size_t c = 0; c < Chords.size(); ++c) {
			const int From = std::max(First, Chords[c].Row), To = std::min(End, Chords[c].Row + Chords[c].Rows);
			if (To <= From)
				continue;
			const int Pitch = GuidePitch(Chords[c].C, Previous, Low, High + 5, RoomFor(O, From, To));
			const BarSpan *pBar = 0;
			for (size_t b = 0; b < Bars.size(); ++b)
				if (Bars[b].Row <= From && From < Bars[b].Row + Bars[b].Rows)
					pBar = &Bars[b];
			Out.push_back(MakeNote(From, To - From, Pitch, pBar != 0 ? VolumeAt(From, *pBar, 0) : O.Volume));
			Previous = Pitch;
		}
		TidyPart(Out, First, End);
		return Out;
	}

	if (O.Style == CHORDS_BROKEN) {
		static const int UP[] = { 0, 1, 2, 3 }, DOWN[] = { 3, 2, 1, 0 }, UPDOWN[] = { 0, 1, 2, 3, 2, 1 },
			ALBERTI[] = { 0, 2, 1, 2 }, ROLLING[] = { 0, 1, 2, 3, 2, 1 };
		const int *pOrder = UP;
		int Length = 4;
		switch (O.Pattern) {
		case BROKEN_DOWN: pOrder = DOWN; break;
		case BROKEN_UPDOWN: pOrder = UPDOWN; Length = 6; break;
		case BROKEN_ALBERTI: pOrder = ALBERTI; break;
		case BROKEN_ROLLING: pOrder = ROLLING; Length = 6; break;
		}
		const int Step = O.StepRows > 0 ? O.StepRows : std::max(1, R / 2);
		int Near = O.Pattern == BROKEN_ROLLING ? Low + 2 : Middle;
		for (size_t s = 0; s < Segs.size(); ++s) {
			const Segment &S = Segs[s];
			std::vector<int> Tones = BrokenTones(S.pChord->C, O.Pattern, Near, Low, High, O.RootInBass, RoomFor(O, S.Row, S.End));
			if (O.Lowest >= 0) {
				// Out of the mud: a tone under the floor an octave up (or two), the chord's shape filled out again where
				// two of its tones then meet
				for (size_t t = 0; t < Tones.size(); ++t)
					while (Tones[t] < O.Lowest && Tones[t] + 12 < PITCHES)
						Tones[t] += 12;
				std::sort(Tones.begin(), Tones.end());
				Tones.erase(std::unique(Tones.begin(), Tones.end()), Tones.end());
				while (Tones.size() < 4)
					Tones.push_back(Tones[Tones.size() >= 2 ? Tones.size() - 2 : 0] + 12);
			}
			// The softest a note gets in this bar (offbeat-only bars aren't softer)
			for (int Row = S.Row, k = 0; Row < S.End; Row += Step, ++k) {
				const int Pitch = Tones[static_cast<size_t>(pOrder[k % Length])];
				Out.push_back(MakeNote(Row, std::min(Step, S.End - Row), Pitch, VolumeAt(Row, *S.pBar, 0)));
			}
			Near = Tones[0];
		}
		TidyPart(Out, First, End);
		return Out;
	}

	// The 0xy arpeggio, held or struck short on a rhythm; lush chords are held
	// on a root, the arpeggio in the instrument
	const bool Lush = O.Style == CHORDS_LUSH;
	const bool Held = O.Style != CHORDS_STABS;
	const int Rhythm = O.Rhythm >= 0 ? O.Rhythm : (Held ? RHYTHM_BARS : RHYTHM_OFFBEATS);
	std::vector<std::pair<int, const BarSpan *> > Hits;
	for (size_t b = 0; b < Bars.size(); ++b) {
		const std::vector<int> Rows = RhythmRows(Rhythm, Bars[b].Rows, R);
		for (size_t i = 0; i < Rows.size(); ++i)
			Hits.push_back(std::make_pair(Bars[b].Row + Rows[i], &Bars[b]));
		if (Held) {
			// A chord that changes between hits is struck where it starts
			for (size_t s = 0; s < Segs.size(); ++s)
				if (Segs[s].pBar == &Bars[b])
					Hits.push_back(std::make_pair(Segs[s].Row, &Bars[b]));
		}
	}
	std::sort(Hits.begin(), Hits.end());
	Hits.erase(std::unique(Hits.begin(), Hits.end()), Hits.end());

	// The softest a hit gets is the least soft hit of its bar (all offbeats: none)
	std::vector<int> Least(Bars.size(), 2);
	for (size_t h = 0; h < Hits.size(); ++h) {
		const size_t b = static_cast<size_t>(Hits[h].second - &Bars[0]);
		Least[b] = std::min(Least[b], Softer(Hits[h].first, *Hits[h].second, R));
	}

	std::vector<Voicing> Voicings(Segs.size());
	std::vector<bool> Voiced(Segs.size(), false);
	Voicing Previous;
	bool HasPrevious = false;
	for (size_t h = 0; h < Hits.size(); ++h) {
		const int Row = Hits[h].first;
		const Segment *pSeg = SegmentAt(Segs, Row);
		if (pSeg == 0)
			continue;
		const size_t s = static_cast<size_t>(pSeg - &Segs[0]);
		if (!Voiced[s]) {
			// Neighbouring segments of one chord sound the same; the chord keeps its room while it sounds
			const Room Space = RoomFor(O, std::max(First, pSeg->pChord->Row), std::min(End, pSeg->pChord->Row + pSeg->pChord->Rows));
			if (s > 0 && Voiced[s - 1] && Segs[s - 1].pChord == pSeg->pChord)
				Voicings[s] = Voicings[s - 1];
			else if (Lush) {
				// The arpeggio climbs from the root to the shape's top note: the whole of it in the room, the root no lower
				// than Lowest (a rolling shape spans two octaves, and a room low under the melody put its roots at Bb0)
				const std::vector<int> Shape = LushShape(pSeg->pChord->C, O.Pattern);
				const int Span = Shape.empty() ? 0 : *std::max_element(Shape.begin(), Shape.end());
				const int Near = HasPrevious ? Previous.Base : Middle;
				Voicings[s].Base = Space.Set() ? RoomyPitch(pSeg->pChord->C.Root, Span, Near, Low, High, Space, O.Lowest) :
					NearestPitch(pSeg->pChord->C.Root, Near, Low, High);
				while (O.Lowest >= 0 && Voicings[s].Base < O.Lowest && Voicings[s].Base + 12 < PITCHES)
					Voicings[s].Base += 12;
			}
			else
				Voicings[s] = ArpeggioVoicing(pSeg->pChord->C, HasPrevious ? &Previous : 0, Middle, Low, High, O.RootInBass, Space.Floor,
					Space.Ceiling);
			Voiced[s] = true;
			Previous = Voicings[s];
			HasPrevious = true;
		}
		const int Next = h + 1 < Hits.size() ? Hits[h + 1].first : End;
		const int ChordEnd = pSeg->pChord->Row + pSeg->pChord->Rows;
		int Rows = std::min(Next, ChordEnd) - Row;
		if (!Held)
			Rows = std::max(1, std::min((Next - Row + 1) / 2, std::max(1, R / 2)));
		const size_t b = static_cast<size_t>(Hits[h].second - &Bars[0]);
		PartNote N = MakeNote(Row, Rows, Voicings[s].Base, VolumeAt(Row, *Hits[h].second, Least[b]));
		if (Lush)
			N.Arpeggio = LushShape(pSeg->pChord->C, O.Pattern);
		else
			N.Effects.push_back(std::make_pair(static_cast<int>(EFF_ARPEGGIO), Voicings[s].Param()));
		Out.push_back(N);
	}
	TidyPart(Out, First, End);
	if (!Out.empty() && !Lush)
		Out.back().EndEffects.push_back(std::make_pair(static_cast<int>(EFF_ARPEGGIO), 0));	// 000: the arpeggio stops
	return Out;
}

std::vector<int> VoiceTones(const Chord &C, int Voices, bool RootInBass)
{
	const int Third = C.Third(), Fifth = C.Fifth(), Seventh = C.Seventh();
	std::vector<int> Order;
	auto Add = [&](int Tone) {
		if (Tone >= 0 && std::find(Order.begin(), Order.end(), Tone) == Order.end())
			Order.push_back(Tone);
	};
	if (!RootInBass)
		Add(0);		// a chord with no bass under it needs its root heard
	Add(Third);
	Add(Seventh);
	for (size_t i = 0; i < C.Tones.size(); ++i) {
		const int Tone = Mod12(C.Tones[i]);
		if (Tone != 0 && Tone != Third && Tone != Fifth && Tone != Seventh)
			Add(Tone);	// ninths, elevenths, sixths
	}
	Add(Fifth);
	Add(0);
	std::vector<int> Out;
	for (int v = 0; v < Voices && !Order.empty(); ++v)
		Out.push_back(Order[static_cast<size_t>(v) % Order.size()]);
	return Out;
}

std::vector<std::vector<PartNote> > VoicedChordParts(const std::vector<ChordAt> &Chords, const std::vector<BarSpan> &Bars,
	const ChordPartOptions &O, int Voices)
{
	Voices = std::max(1, std::min(4, Voices));
	std::vector<std::vector<PartNote> > Parts(static_cast<size_t>(Voices));
	if (Chords.empty() || Bars.empty())
		return Parts;
	const int R = std::max(1, O.RowsPerBeat);
	const int First = Bars.front().Row, End = Bars.back().Row + Bars.back().Rows;
	const std::vector<Segment> Segs = Segments(Chords, Bars);
	const int Low = std::min(O.Low, O.High), High = std::max(O.Low, O.High);
	const int Rhythm = O.Rhythm >= 0 ? O.Rhythm : RHYTHM_BARS;
	const bool Held = O.Style != CHORDS_STABS;
	std::vector<std::pair<int, const BarSpan *> > Hits;
	for (size_t b = 0; b < Bars.size(); ++b) {
		const std::vector<int> Rows = RhythmRows(Rhythm, Bars[b].Rows, R);
		for (size_t i = 0; i < Rows.size(); ++i)
			Hits.push_back(std::make_pair(Bars[b].Row + Rows[i], &Bars[b]));
		for (size_t s = 0; s < Segs.size(); ++s)
			if (Segs[s].pBar == &Bars[b])
				Hits.push_back(std::make_pair(Segs[s].Row, &Bars[b]));	// a chord that changes between hits is struck where it starts
	}
	std::sort(Hits.begin(), Hits.end());
	Hits.erase(std::unique(Hits.begin(), Hits.end()), Hits.end());
	std::vector<int> Least(Bars.size(), 2);
	for (size_t h = 0; h < Hits.size(); ++h) {
		const size_t b = static_cast<size_t>(Hits[h].second - &Bars[0]);
		Least[b] = std::min(Least[b], Softer(Hits[h].first, *Hits[h].second, R));
	}

	// Each chord's voicing, led from the one before
	std::vector<int> Previous;
	const Chord *pPrevious = 0;
	std::vector<int> Current;
	for (size_t h = 0; h < Hits.size(); ++h) {
		const int Row = Hits[h].first;
		const Segment *pSeg = SegmentAt(Segs, Row);
		if (pSeg == 0)
			continue;
		if (pPrevious == 0 || !(*pPrevious == pSeg->pChord->C)) {
			const Chord &C = pSeg->pChord->C;
			const int From = std::max(First, pSeg->pChord->Row), To = std::min(End, pSeg->pChord->Row + pSeg->pChord->Rows);
			// The bass holds the root where it sounds through most of the chord; where it doesn't (bars before it comes in,
			// a heartbeat of short notes) the voices take the root, at the bottom
			bool Rooted = O.RootInBass;
			if (Rooted && !O.Below.empty()) {
				int Covered = 0;
				for (size_t i = 0; i < O.Below.size(); ++i)
					Covered += std::max(0, std::min(To, O.Below[i].Row + O.Below[i].Rows) - std::max(From, O.Below[i].Row));
				Rooted = Covered * 2 >= To - From;
			}
			std::vector<int> Tones = VoiceTones(C, Voices, Rooted);
			if (!O.Above.empty()) {
				// A tone a melody note held against it rubs a semitone from while the chord sounds (a maj7 under the tune's
				// root) gives way to the chord's other tones; its third stays, which says what it is
				std::vector<int> All, Clear, Rubbing;
				const std::vector<int> Order = VoiceTones(C, static_cast<int>(C.Tones.size()) + 1, Rooted);
				for (size_t i = 0; i < Order.size(); ++i)
					if (std::find(All.begin(), All.end(), Order[i]) == All.end())
						All.push_back(Order[i]);
				for (size_t i = 0; i < All.size(); ++i)
					(All[i] != C.Third() && MelodyRub(O.Above, From, To, Mod12(C.Root + All[i]), R) >= 2.0 ? Rubbing : Clear).push_back(All[i]);
				if (!Rubbing.empty() && !Clear.empty()) {
					Clear.insert(Clear.end(), Rubbing.begin(), Rubbing.end());
					Tones.clear();
					for (int v = 0; v < Voices; ++v)
						Tones.push_back(Clear[static_cast<size_t>(v) % Clear.size()]);
				}
			}
			// The register: under the melody's lowest note while the chord sounds and over the bass (its room), in the part's
			// own where that leaves room enough for the voices; squeezed between the two, as near the melody as the bass lets it
			int Lo = Low, Hi = High;
			const Room Space = RoomFor(O, From, To);
			if (Space.Set()) {
				const int Need = 3 * Voices - 2;	// a close voicing's span
				Hi = std::min(Space.Ceiling, High);
				Lo = std::max(Space.Floor, Low);
				if (Hi - Lo < Need) {
					Lo = std::max(Space.Floor, Hi - Need);
					if (Hi - Lo < Need) {
						Lo = Space.Bass > -1000 ? std::max(Space.Bass + 1, Hi - Need) : Hi - Need;
						Hi = std::max(Hi, Lo + Need);
					}
				}
			}
			std::vector<int> Perm(Tones.size());
			for (size_t i = 0; i < Perm.size(); ++i)
				Perm[i] = static_cast<int>(i);
			double BestCost = 1e18;
			std::vector<int> Best;
			// The first chord: close, from the lower part of the register up (in a room, from wherever in its lower half fits
			// it best)
			const bool Led = Previous.size() == Perm.size();
			const int Usual = Lo + (Hi - Lo) / 4;
			const int FromLow = !Led && Space.Set() ? Lo - 2 : Usual, FromHigh = !Led && Space.Set() ? Lo + (Hi - Lo) / 2 : Usual;
			do {
				for (int Bottom = FromLow; Bottom <= FromHigh; ++Bottom) {
					std::vector<int> Pitches;
					double Cost = 0.2 * std::abs(Bottom - Usual);
					for (size_t v = 0; v < Perm.size(); ++v) {
						const int PitchClass = Mod12(C.Root + Tones[static_cast<size_t>(Perm[v])]);
						int Pitch;
						if (Led) {
							Pitch = NearestPitch(PitchClass, Previous[v], Lo - 2, Hi + 2);
							// (over a chord no bass holds, a voice goes up an octave rather than cross the one under it, so the
							// root can stay at the bottom: an Fmaj7's E over its F, not a semitone under it)
							if (!Rooted && !Pitches.empty() && Pitch <= Pitches.back() && Pitch + 12 < PITCHES)
								Pitch += 12;
						}
						else {
							const int Start = Pitches.empty() ? Bottom : Pitches.back() + 1;
							Pitch = Start + Mod12(PitchClass - Start);
						}
						if (Led)
							Cost += std::abs(Pitch - Previous[v]);
						if (!Pitches.empty() && Pitch <= Pitches.back())
							Cost += 100.0;	// voices crossing or in unison
						for (size_t w = 0; w < Pitches.size(); ++w)
							if (Pitch - Pitches[w] == 1 || Pitch - Pitches[w] == 13)
								Cost += 8.0;	// two voices a semitone or a minor ninth apart rub (G# and A in an F#m(add9)): worse
												// than a voice a little out of the register
						Cost += 3.0 * (std::max(0, Lo - Pitch) + std::max(0, Pitch - Hi));	// out of the register, the more the worse
						if (Space.Set() && Pitch > Space.Ceiling + 1)
							Cost += 6.0 * (Pitch - Space.Ceiling - 1);	// a semitone under the melody's lowest note, or in it
						Pitches.push_back(Pitch);
					}
					Cost += 0.1 * (Pitches.back() - Pitches.front());	// close rather than spread
					if (!Rooted && std::find(Tones.begin(), Tones.end(), 0) != Tones.end()) {
						const size_t Lowest = static_cast<size_t>(std::min_element(Pitches.begin(), Pitches.end()) - Pitches.begin());
						if (Tones[static_cast<size_t>(Perm[Lowest])] != 0)
							Cost += 10.0;	// the root that no bass holds at the bottom (an Fmaj7 over its E rubs, a semitone under F)
					}
					if (Cost < BestCost) {
						BestCost = Cost;
						Best = Pitches;
					}
				}
			} while (std::next_permutation(Perm.begin(), Perm.end()));
			Current = Best;
			Previous = Best;
			pPrevious = &pSeg->pChord->C;
		}
		const int Next = h + 1 < Hits.size() ? Hits[h + 1].first : End;
		const int ChordEnd = pSeg->pChord->Row + pSeg->pChord->Rows;
		int Rows = std::min(Next, ChordEnd) - Row;
		if (!Held)
			Rows = std::max(1, std::min((Next - Row + 1) / 2, std::max(1, R / 2)));
		const size_t b = static_cast<size_t>(Hits[h].second - &Bars[0]);
		const int Volume = O.Volume < 0 ? -1 : std::max(1, O.Volume - std::max(0, Softer(Row, *Hits[h].second, R) - Least[b]));
		for (size_t v = 0; v < Current.size() && v < Parts.size(); ++v)
			Parts[v].push_back(MakeNote(Row, Rows, Current[v], Volume));
	}
	for (size_t v = 0; v < Parts.size(); ++v)
		TidyPart(Parts[v], First, End);
	return Parts;
}

std::vector<int> LushShape(const Chord &C, int Pattern)
{
	const bool Third = C.Has(3) || C.Has(4);
	const bool Seventh = C.Has(10) || C.Has(11);
	std::vector<int> Up;
	for (size_t i = 0; i < C.Tones.size(); ++i) {
		int Tone = Mod12(C.Tones[i]);
		if ((Tone == 1 || Tone == 2 || Tone == 5) && Third)
			Tone += 12;		// a ninth or an eleventh over the third
		else if (Tone == 9 && Seventh)
			Tone += 12;		// a thirteenth over the seventh (without one it's a sixth, or a diminished seventh)
		else if ((Tone == 6 || Tone == 8) && C.Has(7))
			Tone += 12;		// a sharp eleventh or a flat thirteenth over the fifth
		Up.push_back(Tone);
	}
	std::sort(Up.begin(), Up.end());
	Up.erase(std::unique(Up.begin(), Up.end()), Up.end());
	if (Up.empty() || Up[0] != 0)
		Up.insert(Up.begin(), 0);
	if (Up.size() < 3 && Up.back() < 12)
		Up.push_back(12);	// a power chord, or a lone root: the octave too
	const size_t n = Up.size();
	std::vector<int> Out;
	switch (Pattern) {
	case BROKEN_DOWN:
		Out.assign(Up.rbegin(), Up.rend());
		break;
	case BROKEN_UPDOWN:
		Out = Up;
		for (size_t i = n - 1; i-- > 1; )
			Out.push_back(Up[i]);
		break;
	case BROKEN_ALBERTI:
		for (size_t i = 0; i < n; i += 2)
			Out.push_back(Up[i]);
		for (size_t i = 1; i < n; i += 2)
			Out.push_back(Up[i]);
		break;
	case BROKEN_ROLLING: {
		// The root and fifth below, the rest an octave up; a tone that would
		// land a semitone from another is left out. The third says what the
		// chord is, so it stays: a ninth over a minor third goes up another
		// octave (an eleventh over a major third, which would go too high, out)
		std::vector<int> Spread;
		for (size_t i = 0; i < n; ++i)
			if (Up[i] == 0 || Up[i] == 7 || Up[i] >= 12)
				Spread.push_back(Up[i]);
		for (size_t i = 0; i < n; ++i) {
			if (Up[i] == 0 || Up[i] == 7 || Up[i] >= 12)
				continue;
			const int Raised = Up[i] + 12;
			if (Up[i] == 3 || Up[i] == 4) {
				std::vector<int> Kept;
				for (size_t k = 0; k < Spread.size(); ++k) {
					if (Spread[k] < 12 || std::abs(Spread[k] - Raised) > 1)
						Kept.push_back(Spread[k]);
					else if (Spread[k] + 12 <= 26)
						Kept.push_back(Spread[k] + 12);
				}
				Spread = Kept;
				Spread.push_back(Raised);
				continue;
			}
			bool Rubs = false;
			for (size_t k = 0; k < Spread.size(); ++k)
				Rubs = Rubs || std::abs(Spread[k] - Raised) <= 1;
			if (!Rubs || Spread.size() < 3)
				Spread.push_back(Raised);
		}
		std::sort(Spread.begin(), Spread.end());
		Spread.erase(std::unique(Spread.begin(), Spread.end()), Spread.end());
		Out = Spread;
		break;
	}
	default:
		Out = Up;
		break;
	}
	return Out;
}

// Bass

BassOptions::BassOptions() : Style(BASS_ROOT_FIFTH), RowsPerBeat(4), Low(24), High(43), K(), Volume(15)
{
}

namespace {

bool ClashesWithChord(int Pitch, const Chord &C);

// A walking line from Root to the note before Target, a note a beat: chord
// tones on the way, and an approach note into Target. No note of it grinds a
// semitone against the chord it walks under (a held V at a half cadence, or
// the chords' parts over it): a chromatic approach that would gives way to
// a step of the scale into the target, or to its fifth.
std::vector<int> WalkLine(int Root, int Target, int Beats, const Chord &C, const Key &K, int Low, int High)
{
	std::vector<int> Line(1, Root);
	if (Beats <= 1)
		return Line;
	const bool Same = Target == Root;
	int Direction = Target > Root ? 1 : -1;
	if (Same)
		Direction = Root + 7 <= High ? 1 : -1;	// out and back
	const int Lowest = std::max(0, Low - 5), Highest = High + 12;
	// The approach: a semitone below when rising into the target, a scale step above when falling; the other ways into it
	// where that grinds against the chord, then its fifth
	const int Into = Same ? Root : Target;
	std::vector<int> Ways;
	if (Same ? Direction > 0 : Direction < 0) {
		Ways.push_back(ScaleStep(Into, 1, K));
		Ways.push_back(Into + 1);
		Ways.push_back(ScaleStep(Into, -1, K));
		Ways.push_back(Into - 1);
	}
	else {
		Ways.push_back(Into - 1);
		Ways.push_back(ScaleStep(Into, -1, K));
		Ways.push_back(ScaleStep(Into, 1, K));
		Ways.push_back(Into + 1);
	}
	Ways.push_back(Into - 7);
	Ways.push_back(Into + 7);
	std::vector<int> Clean;
	for (size_t w = 0; w < Ways.size(); ++w)
		if (Ways[w] != Root && Ways[w] >= Lowest && Ways[w] <= Highest && !ClashesWithChord(Ways[w], C) &&
			std::find(Clean.begin(), Clean.end(), Ways[w]) == Clean.end())
			Clean.push_back(Ways[w]);
	int Approach = Clean.empty() ? Ways[0] : Clean[0];
	if (Beats == 2) {
		Line.push_back(Approach == Root ? Root + 7 * Direction : Approach);
		return Line;
	}
	// The notes between the root and the approach, in the direction of travel:
	// chord tones first, then the scale's that don't grind against the chord,
	// then the scale's that do, then the ones between (a chromatic walk),
	// those nearest the approach first; with the first way into the target
	// that leaves room for notes that don't grind, if one does
	const int Needed = Beats - 2;
	std::vector<int> Middle;
	if (Same) {
		std::vector<int> Pool;
		for (int p = Root + Direction; p != Root + 12 * Direction; p += Direction)
			if (C.HasPitchClass(Mod12(p)) && p != Approach)
				Pool.push_back(p);
		for (int k = 0; k < Needed && !Pool.empty(); ++k)
			Middle.push_back(Pool[static_cast<size_t>(k * static_cast<int>(Pool.size()) / Needed)]);
	}
	else {
		for (size_t Try = 0; Try < std::max<size_t>(1, Clean.size()) && Middle.empty(); ++Try) {
			const int Way = Clean.empty() ? Approach : Clean[Try];
			const int Along = Way > Root ? 1 : -1;
			std::vector<std::pair<int, int> > Ranked;	// (rank, pitch)
			for (int p = Root + Along; p != Way && std::abs(p - Root) <= 14; p += Along) {
				if (p == Target)
					continue;	// not there before its time
				const bool Grinds = ClashesWithChord(p, C);
				const int Rank = C.HasPitchClass(Mod12(p)) ? 0 : InKey(p, K) ? (Grinds ? 2 : 1) : (Grinds ? 4 : 3);
				Ranked.push_back(std::make_pair(Rank * 100 + std::abs(Way - p), p));
			}
			if (static_cast<int>(Ranked.size()) < Needed)
				continue;
			std::sort(Ranked.begin(), Ranked.end());
			bool Grinding = false;
			for (int k = 0; k < Needed; ++k)
				Grinding = Grinding || Ranked[static_cast<size_t>(k)].first / 100 == 2 || Ranked[static_cast<size_t>(k)].first / 100 == 4;
			if (Grinding && Try + 1 < Clean.size())
				continue;	// another way in may walk cleaner
			for (int k = 0; k < Needed; ++k)
				Middle.push_back(Ranked[static_cast<size_t>(k)].second);
			std::sort(Middle.begin(), Middle.end());
			if (Along < 0)
				std::reverse(Middle.begin(), Middle.end());
			Approach = Way;
			Direction = Along;
		}
	}
	if (static_cast<int>(Middle.size()) < Needed) {
		Middle.clear();
		// Too close: scale steps, out and back (a step further past one that grinds against the chord)
		int p = Root;
		for (int k = 0; k < Needed; ++k) {
			const int Way = k < (Needed + 1) / 2 ? -Direction : Direction;
			p = ScaleStep(p, Way, K);
			if (ClashesWithChord(p, C) && !ClashesWithChord(ScaleStep(p, Way, K), C))
				p = ScaleStep(p, Way, K);
			Middle.push_back(p);
		}
	}
	Line.insert(Line.end(), Middle.begin(), Middle.end());
	Line.push_back(Approach);
	for (size_t i = 0; i < Line.size(); ++i) {
		// In the bass's reach: an octave over or under rather than another note
		while (Line[i] < Lowest && Line[i] + 12 <= Highest)
			Line[i] += 12;
		while (Line[i] > Highest && Line[i] - 12 >= Lowest)
			Line[i] -= 12;
		Line[i] = Clamp(Line[i], Lowest, Highest);
	}
	return Line;
}

// Whether a bass note on [Row, Row + Rows) rubs: a semitone from one of the chord's notes, or a semitone under one even
// where it's one itself (a ninth under the third, a major seventh under the root: the chords sound over the bass, as broken
// chords and arpeggios play every note), or a semitone and octaves from a melody note sounding with it for half a beat or
// its whole length (as check_song hears a clash)
bool BassRubs(int Pitch, int Row, int Rows, const Chord &C, const BassOptions &O)
{
	if (ClashesWithChord(Pitch, C) || C.HasPitchClass(Mod12(Pitch + 1)))
		return true;
	const int Least = std::min(std::max(1, Rows), std::max(1, O.RowsPerBeat / 2));
	for (size_t i = 0; i < O.Above.size(); ++i) {
		const NoteInfo &M = O.Above[i];
		const int Overlap = std::min(Row + Rows, M.Row + M.Rows) - std::max(Row, M.Row);
		if (Overlap >= Least && std::abs(M.Pitch - Pitch) % 12 == 1)
			return true;
	}
	return false;
}

// A walking line again where a note of it rubs (Bad(beat, pitch)): of the lines from Root whose other notes don't, each the
// chord's, the key's or a chromatic step into the next, the one that walks smoothest into Target, keeping Old's notes where
// it can. Lines of six notes or more change only their notes that rub, each to the nearest that doesn't, or the note before
template <class Rubbing>
std::vector<int> CleanWalk(const std::vector<int> &Old, int Target, const Chord &C, const Key &K, int Lowest, int Highest,
	const Rubbing &Bad)
{
	bool Rubs = false;
	for (size_t k = 1; k < Old.size(); ++k)
		Rubs = Rubs || Bad(k, Old[k]);
	if (!Rubs)
		return Old;
	const int Root = Old[0];
	if (Old.size() > 5) {
		std::vector<int> Out = Old;
		for (size_t k = 1; k < Out.size(); ++k) {
			if (!Bad(k, Out[k]))
				continue;
			int Best = Out[k - 1];
			for (int d = 1; d <= 4 && Best == Out[k - 1]; ++d)
				for (int s = -1; s <= 1; s += 2) {
					const int p = Out[k] + s * d;
					if (p >= Lowest && p <= Highest && !Bad(k, p) && (C.HasPitchClass(Mod12(p)) || InKey(p, K))) {
						Best = p;
						break;
					}
				}
			Out[k] = Best;
		}
		return Out;
	}
	std::vector<std::vector<int> > Choices(Old.size());
	for (size_t k = 1; k < Old.size(); ++k)
		for (int p = std::max(Lowest, Root - 12); p <= std::min(Highest, Root + 12); ++p)
			if (!Bad(k, p))
				Choices[k].push_back(p);
	std::vector<int> Line(Old.size(), Root), Best = Old;
	double BestCost = 1e18;
	// Each line, the cost of its notes: what they are, how they move and how the last goes into the target
	std::function<void(size_t, double)> Walk = [&](size_t k, double Cost) {
		if (Cost - 0.2 * static_cast<double>(Line.size() - k) >= BestCost)
			return;	// (a note kept as it was takes off at most 0.2)
		if (k == Line.size()) {
			const int Into = std::abs(Target - Line.back());
			Cost += Into == 1 || Into == 2 ? 0.0 : Into == 0 ? 1.5 : Into == 5 || Into == 7 ? 0.6 : Into <= 4 ? 0.8 :
				1.5 + 0.2 * std::max(0, Into - 7);
			for (size_t i = 1; i < Line.size(); ++i) {
				// A chromatic note steps by a semitone into the next
				const int Next = i + 1 < Line.size() ? Line[i + 1] : Target;
				if (!C.HasPitchClass(Mod12(Line[i])) && !InKey(Line[i], K))
					Cost += std::abs(Next - Line[i]) == 1 ? 1.0 : 2.5;
			}
			if (Cost < BestCost) {
				BestCost = Cost;
				Best = Line;
			}
			return;
		}
		for (size_t c = 0; c < Choices[k].size(); ++c) {
			const int p = Choices[k][c];
			const int Leap = std::abs(p - Line[k - 1]);
			double Step = C.HasPitchClass(Mod12(p)) ? 0.0 : InKey(p, K) ? 0.6 : 0.0;
			Step += Leap == 0 ? 1.5 : Leap <= 2 ? 0.0 : Leap <= 4 ? 0.5 : Leap <= 7 ? 1.0 : 2.0 + 0.2 * (Leap - 7);
			if (k >= 2 && Line[k - 1] != Line[k - 2] && p != Line[k - 1] && (p > Line[k - 1]) != (Line[k - 1] > Line[k - 2]))
				Step += 0.3;	// turning back
			if (p == Old[k])
				Step -= 0.2;	// the line as it was, where that doesn't rub
			if (p == Target)
				Step += 1.0;	// not there before its time
			Line[k] = p;
			Walk(k + 1, Cost + Step);
		}
	};
	Walk(1, 0.0);
	return Best;
}

}	// namespace

std::vector<PartNote> BassPart(const std::vector<ChordAt> &Chords, const std::vector<BarSpan> &Bars, const BassOptions &O)
{
	std::vector<PartNote> Out;
	if (Chords.empty() || Bars.empty())
		return Out;
	const int R = std::max(1, O.RowsPerBeat);
	const int Half = std::max(1, R / 2);
	const int First = Bars.front().Row, End = Bars.back().Row + Bars.back().Rows;
	const int Low = std::min(O.Low, O.High), High = std::max(O.Low, O.High);
	const std::vector<Segment> Segs = Segments(Chords, Bars);
	int Previous = Low + (High - Low) / 3;
	auto Add = [&](int Row, int Rows, int Pitch) {
		Out.push_back(MakeNote(Row, Rows, Pitch, -1));
	};

	for (size_t i = 0; i < Segs.size(); ++i) {
		const Segment &S = Segs[i];
		const Chord &C = S.pChord->C;
		const int Length = S.End - S.Row;
		const int Root = O.Style == BASS_PEDAL ? BassPitch(O.K.Tonic, Previous, Low, High) : BassPitch(C.Bass, Previous, Low, High);
		const int FifthUp = Root + (C.Fifth() >= 0 ? C.Fifth() : 7);
		const int Fifth = FifthUp <= High + 2 ? FifthUp : FifthUp - 12;
		const bool Contiguous = i + 1 < Segs.size() && Segs[i + 1].Row == S.End;
		const Chord *pNext = Contiguous ? &Segs[i + 1].pChord->C : 0;
		int Last = Root;

		switch (O.Style) {
		case BASS_ROOTS:
		case BASS_PEDAL:
			Add(S.Row, Length, Root);
			break;
		case BASS_HALF: {
			const int BarHalf = std::max(1, S.pBar->Rows / R / 2) * R;
			for (int Row = S.Row; Row < S.End; ) {
				const int Offset = Row - S.pBar->Row;
				const int Next = std::min(S.End, S.pBar->Row + (Offset / BarHalf + 1) * BarHalf);
				Add(Row, Next - Row, Root);
				Row = Next;
			}
			break;
		}
		case BASS_OCTAVES:
		case BASS_DRIVING:
		case BASS_ARPEGGIO: {
			const int Pattern[4] = { Root, Fifth >= Root ? Fifth : Fifth + 12, Root + 12, Fifth >= Root ? Fifth : Fifth + 12 };
			for (int Row = S.Row, k = 0; Row < S.End; Row += Half, ++k) {
				int Pitch = Root;
				if (O.Style == BASS_OCTAVES)
					Pitch = k % 2 == 0 ? Root : Root + 12;
				else if (O.Style == BASS_ARPEGGIO)
					Pitch = Pattern[k % 4];
				const int Rows = std::min(Half, S.End - Row);
				Add(Row, O.Style == BASS_DRIVING && Rows > 1 ? Rows - 1 : Rows, Pitch);
			}
			break;
		}
		case BASS_WALKING: {
			const int Beats = std::max(1, Length / R);
			int Target = Root;
			if (pNext != 0)
				Target = BassPitch(pNext->Bass, Root, Low, High);
			// Its passing notes off the chord's notes and the melody's, a semitone away (where the way the line takes into
			// the target leaves no other, the notes before it change)
			auto Bad = [&](size_t k, int Pitch) {
				const int Row = S.Row + static_cast<int>(k) * R;
				return Row < S.End && BassRubs(Pitch, Row, std::min(R, S.End - Row), C, O);
			};
			const std::vector<int> Line = CleanWalk(WalkLine(Root, Target, Beats, C, O.K, Low, High), Target, C, O.K,
				std::max(0, Low - 5), High + 12, Bad);
			for (size_t k = 0; k < Line.size(); ++k) {
				const int Row = S.Row + static_cast<int>(k) * R;
				const int To = k + 1 < Line.size() ? Row + R : S.End;
				if (Row < S.End)
					Add(Row, std::min(To, S.End) - Row, Line[k]);
			}
			Last = Line.back();
			break;
		}
		case BASS_SYNCOPATED: {
			// From the bar's start: root at 0, root at 1.5 beats, fifth at 3 (an approach at 3.5 into a new chord)
			const int Rows[3] = { 0, 3 * R / 2, 3 * R };
			for (int k = 0; k < 3; ++k) {
				const int Row = S.Row + Rows[k];
				if (Row >= S.End)
					break;
				const int To = k < 2 ? std::min(S.End, S.Row + Rows[k + 1]) : S.End;
				int Pitch = k == 2 ? Fifth : Root;
				// (the fifth gives way to the root where another part rubs a semitone against it: a counter-line arriving
				// early on the next chord's root)
				if (k == 2 && BassRubs(Pitch, Row, To - Row, C, O) && !BassRubs(Root, Row, To - Row, C, O))
					Pitch = Root;
				if (k == 2 && pNext != 0 && pNext->Bass != C.Bass && To - Row >= R) {
					const int Target = BassPitch(pNext->Bass, Root, Low, High);
					Add(Row, Half, Pitch);
					// The approach: a semitone into the target, else a step of the scale or a whole tone into it, else the
					// fifth held on, else the root; none a semitone from the chord's notes or the other parts' sounding with
					// it (the fifth held on where every one does)
					const int Toward = Target > Pitch ? 1 : -1;
					const int Ways[5] = { Target - Toward, ScaleStep(Target, -Toward, O.K), Target - 2 * Toward, Pitch, Root };
					int Approach = Pitch;
					for (int w = 0; w < 5; ++w)
						if (!BassRubs(Ways[w], Row + Half, To - Row - Half, C, O)) {
							Approach = Ways[w];
							break;
						}
					Add(Row + Half, To - Row - Half, Approach);
					Last = Approach;
					continue;
				}
				Add(Row, To - Row, Pitch);
			}
			break;
		}
		default: {	// BASS_ROOT_FIFTH
			Add(S.Row, Length, Root);
			const int Beats = S.pBar->Rows / R;
			if (Beats >= 4 && S.Row == S.pBar->Row) {
				const int Middle = S.pBar->Row + (Beats / 2) * R;
				if (Middle < S.End) {
					Out.back().Rows = Middle - S.Row;
					Add(Middle, S.End - Middle, Fifth);
				}
			}
			break;
		}
		}
		Previous = O.Style == BASS_WALKING || O.Style == BASS_SYNCOPATED ? Last : Root;
	}
	TidyPart(Out, First, End);
	if (!Out.empty() && O.Volume >= 0)
		Out.front().Volume = Clamp(O.Volume, 1, 15);
	return Out;
}

// Drums

DrumOptions::DrumOptions() : Groove(GROOVE_BASIC), Intensity(2), FillEvery(4), PhraseStart(0), Crash(true), Samples(false),
	RowsPerBeat(4)
{
}

namespace {

// A groove on sixteenths, four to a beat: 'x' a hit, 'o' a softer kick or
// an open hi-hat, 'g' a ghost snare
struct GrooveDef {
	const char *Kick;
	const char *Snare;
	const char *Hat;
};

const GrooveDef GROOVE_DEFS[] = {
	{ "x.......x.x.....", "....x.......x...", "x.x.x.x.x.x.x.x." },	// basic
	{ "x...x...x...x...", "....x.......x...", "..o...o...o...o." },	// four on the floor
	{ "x.........x.....", "........x.......", "x.x.x.x.x.x.x.x." },	// half time
	{ "x.......x.......", "....x.......x...", "x..xx..xx..xx..x" },	// shuffle
	{ "x.......x.......", "x.xxx.x.x.xxx.xx", "................" },	// march
	{ "x.....x...x.....", "....x....g..x..g", "x.x.x.x.x.x.x.x." },	// breakbeat
	{ "x...............", "........g.......", "x...x...x...x..." },	// calm
	{ "x.x.....x.x.....", "....x.......x...", "xxxxxxxxxxxxxxxx" },	// driving
	{ "x...........", "....g...g...", "x.x.x.x.x.x." },				// waltz
	{ "x.....x.........", "................", "x.x.x.x.x.x.x.x." },	// lively (every other bar: LIVELY_SECOND_KICK)
};

// The lively groove's kick in every other bar: on 1 and the "and" of 3 (the first bar's is on the "and" of 2), so two bars
// make its phrase; its hits are soft by design, the loudest LIVELY_LOUDEST
const char *const LIVELY_SECOND_KICK = "x.........x.....";
const int LIVELY_LOUDEST = 10;

// The hi-hats at D-#: at E-# most of the noise's sound lies above hearing and they come out about 4 dB quieter
const int KICK_PITCH = 0x4, SNARE_PITCH = 0xA, HAT_PITCH = 0xD, OPEN_PITCH = 0xD, CRASH_PITCH = 0xC;
const int TOM_PITCHES[3] = { 0x9, 0x7, 0x5 };	// high, middle, low

DrumHit Hit(int Row, int Pitch, int Volume, const char *Sound)
{
	DrumHit H;
	H.Row = Row;
	H.Pitch = Pitch;
	H.Volume = Clamp(Volume, 1, 15);
	H.Sound = Sound;
	H.Sampled = false;
	return H;
}

// Which sound wins when two land on a row (the noise channel plays one at a time)
int Priority(const DrumHit &H)
{
	if (H.Sound == "crash")
		return 6;
	if (H.Sound == "kick")
		return 5;
	if (H.Sound == "snare" || H.Sound == "tom")
		return H.Volume >= 9 ? 4 : 1;	// a ghost snare gives way to hi-hats
	if (H.Sound == "open hi-hat")
		return 3;
	return 2;
}

// The fill of a bar: hits from Start (a sixteenth) to the bar's end, which
// is Steps sixteenths long; Kind turns between toms, a snare roll and kick and snare
void AddFill(std::vector<DrumHit> &Hits, int Kind, int Start, int Steps, int BarRow, const std::vector<int> &StepRows)
{
	const int Count = Steps - Start;
	for (int k = 0; k < Count; ++k) {
		const int Step = Start + k;
		const int Row = BarRow + StepRows[static_cast<size_t>(Step)];
		const int Volume = 9 + (6 * (k + 1)) / Count;	// rising to 15
		const bool Sixteenths = Count <= 4 || k >= Count / 2;
		if (!Sixteenths && k % 2 == 1)
			continue;
		switch (Kind % 3) {
		case 0: {	// toms, high to low
			const int Tom = std::min(2, 3 * k / std::max(1, Count));
			Hits.push_back(Hit(Row, TOM_PITCHES[Tom], Volume, "tom"));
			break;
		}
		case 1:		// a snare roll
			Hits.push_back(Hit(Row, SNARE_PITCH, Volume, "snare"));
			break;
		default:	// kick and snare
			Hits.push_back(k % 2 == 0 && k + 1 < Count ? Hit(Row, KICK_PITCH, 14, "kick") : Hit(Row, SNARE_PITCH, Volume, "snare"));
			break;
		}
	}
}

// A lively stage's fill, small: from Start (a sixteenth) to the bar's end, a roll of closed hi-hats in sixteenths rising
// from 4 to 7, or soft toms, high to low, rising from 6 to 9 (eighths, then sixteenths in the last beat), in turn by Kind
void AddLivelyFill(std::vector<DrumHit> &Hits, int Kind, int Start, int Steps, int BarRow, const std::vector<int> &StepRows)
{
	const int Count = Steps - Start;
	for (int k = 0; k < Count; ++k) {
		const int Row = BarRow + StepRows[static_cast<size_t>(Start + k)];
		if (Kind % 2 == 0) {
			Hits.push_back(Hit(Row, HAT_PITCH, 4 + (3 * (k + 1)) / Count, "hi-hat"));
			continue;
		}
		if (Count > 4 && k < Count - 4 && k % 2 == 1)
			continue;
		const int Tom = std::min(2, 3 * k / std::max(1, Count));
		Hits.push_back(Hit(Row, TOM_PITCHES[Tom], 6 + (3 * (k + 1)) / Count, "tom"));
	}
}

}	// namespace

int GrooveLoudest(int Groove)
{
	return Groove == GROOVE_LIVELY ? LIVELY_LOUDEST : 15;
}

// Light (Intensity 1) thins the kick and the snare first: the kick on the bar's first beat and its middle only, the snare's
// backbeat only on the bar's last beat that has one, softer (the march's snares, its groove, on the beats), no ghost
// notes; the hi-hats keep the groove's eighths (the noise channel plays one sound at a time, so with a kick or a snare on
// every beat, hi-hats on the beats alone were all lost). Busy (3) adds pushes, ghost notes and sixteenths.
std::vector<DrumHit> DrumPart(const std::vector<BarSpan> &Bars, const DrumOptions &O)
{
	std::vector<DrumHit> Hits;
	const int R = std::max(1, O.RowsPerBeat);
	const int GrooveIndex = Clamp(O.Groove, 0, static_cast<int>(sizeof(GROOVE_DEFS) / sizeof(GROOVE_DEFS[0])) - 1);
	const GrooveDef &G = GROOVE_DEFS[GrooveIndex];
	const int PatternSteps = static_cast<int>(std::string(G.Kick).size());
	const int Intensity = Clamp(O.Intensity, 1, 3);
	const bool Lively = GrooveIndex == GROOVE_LIVELY;
	int Fills = 0;

	for (size_t b = 0; b < Bars.size(); ++b) {
		const BarSpan &Bar = Bars[b];
		const int Beats = std::max(1, Bar.Rows / R);
		const int Steps = Beats * 4;
		// Sixteenths to rows: as close as the rows allow
		std::vector<int> StepRows;
		for (int s = 0; s < Steps; ++s)
			StepRows.push_back((s / 4) * R + (s % 4) * R / 4);
		const int BarInPhrase = O.PhraseStart + static_cast<int>(b);
		const bool Fill = O.FillEvery > 0 && (BarInPhrase + 1) % O.FillEvery == 0 && Beats >= 2;
		const bool Second = BarInPhrase % 2 == 1;
		int FillStart = Steps;
		if (Fill) {
			const int FillBeats = Lively ? (Intensity == 3 ? 2 : 1) : Intensity == 1 ? 1 : Intensity == 3 ? 2 : (Fills % 2 == 0 ? 1 : 2);
			FillStart = std::max(0, Steps - 4 * std::min(FillBeats, Beats - 1));
		}
		// Light: the bar's last backbeat, the one snare it keeps
		int LastBackbeat = -1;
		for (int s = 0; s < FillStart; s += 4)
			if (G.Snare[s % PatternSteps] == 'x')
				LastBackbeat = s;

		std::vector<DrumHit> Bar16;
		for (int s = 0; s < FillStart; ++s) {
			const int p = s % PatternSteps;
			const int Row = Bar.Row + StepRows[static_cast<size_t>(s)];
			const bool OnBeat = s % 4 == 0, Eighth = s % 2 == 0;
			// Kick (the lively groove's soft, its second bar's on the "and" of 3)
			char k = (Lively && Second ? LIVELY_SECOND_KICK : G.Kick)[p];
			if (Intensity == 1 && (!OnBeat || (s != 0 && s * 2 != Steps)))
				k = '.';
			if (Intensity == 3 && k == '.' && Second && s == Steps - 2 && GrooveIndex != GROOVE_CALM && !Lively)
				k = 'o';	// a push into the next bar
			if (k != '.')
				Bar16.push_back(Hit(Row, KICK_PITCH, Lively ? (s == 0 ? LIVELY_LOUDEST : 8) : k == 'o' ? 12 : s == 0 ? 15 : OnBeat ? 14 : 13,
					"kick"));
			// Snare
			char sn = G.Snare[p];
			if (Intensity == 1 && (sn == 'g' || (sn == 'x' && (GrooveIndex == GROOVE_MARCH ? !OnBeat : s != LastBackbeat))))
				sn = '.';
			if (Intensity == 3 && sn == '.' && (s % 8 == 7) && GrooveIndex != GROOVE_CALM && GrooveIndex != GROOVE_MARCH && !Lively)
				sn = 'g';	// ghost notes before the backbeats
			if (sn != '.') {
				int Volume = sn == 'g' ? 6 : 14;
				if (GrooveIndex == GROOVE_MARCH && sn == 'x')
					Volume = OnBeat ? 14 : 10;
				if (GrooveIndex == GROOVE_CALM || GrooveIndex == GROOVE_WALTZ || Intensity == 1)
					Volume = sn == 'g' ? 7 : 11;
				Bar16.push_back(Hit(Row, SNARE_PITCH, Volume, "snare"));
			}
			// Hi-hats (a shuffle's swung ones are its eighths)
			char h = G.Hat[p];
			if (Intensity == 1 && h == 'x' && !Eighth && GrooveIndex != GROOVE_SHUFFLE)
				h = '.';
			if (Lively && Intensity >= 2 && ((Second && s == 7) || (Intensity == 3 && (Second ? s == 11 : s == 7 || s == 15))))
				h = 'x';	// a sixteenth pair now and then (busy: more of them)
			if (Intensity == 3 && h == '.' && GrooveIndex != GROOVE_CALM && GrooveIndex != GROOVE_MARCH &&
				GrooveIndex != GROOVE_FOUR_ON_FLOOR && GrooveIndex != GROOVE_SHUFFLE && !Lively && !Eighth)
				h = 'x';	// sixteenths in between
			if (h == 'x' && Second && s == Steps - 2 && (Intensity >= 2 || Lively) && GrooveIndex != GROOVE_CALM)
				h = 'o';	// an open hi-hat into the next bar, every other bar
			if (h != '.') {
				int Volume = OnBeat ? 10 : Eighth ? 8 : 6;
				if (Lively)
					Volume = OnBeat ? 6 : Eighth ? 5 : 4;
				else {
					if (GrooveIndex == GROOVE_CALM)
						Volume -= 2;
					if (Intensity == 1)
						Volume -= 1;
				}
				Bar16.push_back(h == 'o' ? Hit(Row, OPEN_PITCH, Lively ? 7 : Intensity == 1 ? 9 : 11, "open hi-hat") :
					Hit(Row, HAT_PITCH, Volume, "hi-hat"));
			}
		}
		if (Fill) {
			if (Lively)
				AddLivelyFill(Bar16, Fills, FillStart, Steps, Bar.Row, StepRows);
			else
				AddFill(Bar16, Fills, FillStart, Steps, Bar.Row, StepRows);
			++Fills;
		}
		// A crash where the drums start, and after a fill (on a lively stage a soft splash)
		const bool AfterFill = O.FillEvery > 0 && BarInPhrase % O.FillEvery == 0 && (b > 0 || O.PhraseStart > 0);
		if (O.Crash && ((b == 0 && O.PhraseStart == 0) || AfterFill))
			Bar16.push_back(Hit(Bar.Row, CRASH_PITCH, Lively ? 7 : 15, "crash"));

		// Sampled drums: the kick, the snare (its ghost notes stay soft on the
		// noise) and the toms
		if (O.Samples)
			for (size_t i = 0; i < Bar16.size(); ++i)
				Bar16[i].Sampled = Bar16[i].Sound == "kick" || Bar16[i].Sound == "tom" || (Bar16[i].Sound == "snare" && Bar16[i].Volume >= 9);
		// One sound a row on each channel: the one that matters most. The
		// noise channel plays one sound at a time, so no hi-hat for a beat
		// after a crash, which it would cut off.
		std::stable_sort(Bar16.begin(), Bar16.end(), [](const DrumHit &a, const DrumHit &b) {
			return a.Row < b.Row || (a.Row == b.Row && (a.Sampled != b.Sampled ? a.Sampled : Priority(a) > Priority(b)));
		});
		int Crash = -1;
		for (size_t i = 0; i < Bar16.size(); ++i) {
			if (i > 0 && Bar16[i].Row == Bar16[i - 1].Row && Bar16[i].Sampled == Bar16[i - 1].Sampled)
				continue;
			if (Bar16[i].Sound == "crash")
				Crash = Bar16[i].Row;
			else if (Crash >= 0 && Bar16[i].Row - Crash < std::max(1, O.RowsPerBeat) &&
				(Bar16[i].Sound == "hi-hat" || Bar16[i].Sound == "open hi-hat"))
				continue;
			Hits.push_back(Bar16[i]);
		}
	}
	return Hits;
}

// Harmony

HarmonyOptions::HarmonyOptions() : Interval(HARMONY_THIRD_BELOW), MinRows(0), RowsPerBeat(4), K(), Low(24), High(72), VolumeDrop(3)
{
}

namespace {

// Pitch moved by Degrees steps of the key's scale (a note outside the scale
// moves from the scale note below it)
int ScaleDegrees(int Pitch, int Degrees, const Key &K)
{
	int p = Pitch;
	const int Direction = Degrees > 0 ? 1 : -1;
	for (int d = 0; d < std::abs(Degrees); ++d)
		p = ScaleStep(p, Direction, K);
	return p;
}

bool Rubs(int a, int b)
{
	const int d = Mod12(a - b);
	return d == 1 || d == 11 || d == 2 || d == 10;
}

// A note a semitone from one of the chord's own (and not one itself): the
// scale's C under an A major chord's C#, the harsh minor ninth
bool ClashesWithChord(int Pitch, const Chord &C)
{
	if (C.HasPitchClass(Mod12(Pitch)))
		return false;
	return C.HasPitchClass(Mod12(Pitch + 1)) || C.HasPitchClass(Mod12(Pitch - 1));
}

// The semitones a slide takes a note (Qxy up, Rxy down by y); 0 for other effects
int SlideSemitones(int Effect, int Param)
{
	return Effect == EFF_SLIDE_UP ? (Param & 0x0F) : Effect == EFF_SLIDE_DOWN ? -(Param & 0x0F) : 0;
}

}	// namespace

std::vector<PartNote> HarmonyPart(const std::vector<NoteInfo> &Melody, const std::vector<ChordAt> &Chords, const HarmonyOptions &O)
{
	std::vector<PartNote> Out;
	const int R = std::max(1, O.RowsPerBeat);
	int Previous = -1;
	// The interval's own note for a melody note, in the key: its scale's steps, and for a note outside the scale (a
	// chromatic passing note, a phrygian second's) the size of the interval that lands in the key (a sixth under D#
	// in D minor is G, not the F a scale's steps would give, a seventh). Over a chord with notes from outside the key
	// (pScale), the scale it sounds in instead (ChordScale): under a borrowed Ebadd9 in G major G aeolian's notes, not a
	// G major third or the Db of an Eb7
	auto Plain = [&](int m, const std::vector<int> *pScale) {
		auto Fits = [&](int Pitch) { return pScale != 0 ? InChordScale(Pitch, *pScale) : InKey(Pitch, O.K); };
		auto Steps = [&](int Pitch, int Degrees) {
			if (pScale == 0)
				return ScaleDegrees(Pitch, Degrees, O.K);
			for (int d = 0; d < std::abs(Degrees); ++d)
				Pitch = ChordScaleStep(Pitch, Degrees > 0 ? 1 : -1, *pScale);
			return Pitch;
		};
		if (O.Interval != HARMONY_OCTAVE_BELOW && !Fits(m)) {
			static const int THIRDS[] = { -3, -4 }, SIXTHS[] = { -8, -9 }, ABOVE[] = { 3, 4 }, FIFTHS[] = { -7, -8, -6 };
			const int *pSizes = O.Interval == HARMONY_SIXTH_BELOW ? SIXTHS : O.Interval == HARMONY_THIRD_ABOVE ? ABOVE :
				O.Interval == HARMONY_FIFTH_BELOW ? FIFTHS : THIRDS;
			const int Count = O.Interval == HARMONY_FIFTH_BELOW ? 3 : 2;
			for (int k = 0; k < Count; ++k)
				if (Fits(m + pSizes[k]))
					return m + pSizes[k];
		}
		switch (O.Interval) {
		case HARMONY_SIXTH_BELOW: return Steps(m, 2) - 12;
		case HARMONY_THIRD_ABOVE: return Steps(m, 2);
		case HARMONY_FIFTH_BELOW: return Steps(m, -4);
		case HARMONY_OCTAVE_BELOW: return m - 12;
		default: return Steps(m, -2);
		}
	};
	for (size_t i = 0; i < Melody.size(); ++i) {
		const NoteInfo &N = Melody[i];
		if (O.MinRows > 0 && N.Rows < O.MinRows)
			continue;
		// Where the melody note lands: a scoop (polish's, Q61 on a note written a semitone low) counts where it ends
		int p = N.Pitch;
		for (size_t e = 0; e < N.Effects.size(); ++e)
			if (N.Effects[e].first == 0)
				p += SlideSemitones(N.Effects[e].second.first, N.Effects[e].second.second);
		const ChordAt *pChord = ChordAtRow(Chords, N.Row);
		std::vector<int> ChordNotes;
		const bool Borrowed = pChord != 0 && OutsideKey(pChord->C, O.K);
		if (Borrowed)
			ChordNotes = ChordScale(O.K, pChord->C);
		const std::vector<int> *pScale = Borrowed ? &ChordNotes : 0;
		int h = Plain(p, pScale);
		// On the chord's notes where the melody holds or lands on a beat: the
		// interval's own sizes first, then the sweet ones (thirds and sixths)
		const bool Matters = N.Rows >= R || N.Row % R == 0;
		if (O.Interval != HARMONY_OCTAVE_BELOW && pChord != 0 && Matters && !pChord->C.HasPitchClass(Mod12(h))) {
			static const int THIRDS[] = { 3, 4, 8, 9 }, SIXTHS[] = { 8, 9, 3, 4 }, FIFTHS[] = { 7, 5, 8, 9 };
			const int *pSizes = O.Interval == HARMONY_SIXTH_BELOW ? SIXTHS : O.Interval == HARMONY_FIFTH_BELOW ? FIFTHS : THIRDS;
			const bool Below = O.Interval != HARMONY_THIRD_ABOVE;
			for (int k = 0; k < 4; ++k) {
				const int Distance = pSizes[k] + (O.Interval == HARMONY_SIXTH_BELOW && k >= 2 ? 12 : 0);
				const int Candidate = Below ? p - Distance : p + Distance;
				if (pChord->C.HasPitchClass(Mod12(Candidate)) && !Rubs(Candidate, p)) {
					h = Candidate;
					break;
				}
			}
		}
		if (O.Interval != HARMONY_OCTAVE_BELOW && pChord != 0 && ClashesWithChord(h, pChord->C)) {
			// Between the beats too, not a semitone from the chord's notes: the
			// chord's version of the degree (C# over A major, not the scale's C),
			// a third or sixth from the melody where it can be. Over a chord of
			// the key's own notes the line stays in the key (under a Dsus4 in D
			// major, G rather than F), over one from outside it in the scale the
			// chord sounds in, or on a sweet color of it (its sixth or ninth, a
			// minor chord's eleventh): no Eb7's Db under an Ebadd9 in G major, but
			// the F# of A6 under an A major chord in A minor
			const bool Diatonic = !Borrowed;
			auto Color = [&](int Pitch) {
				const int Interval = Mod12(Pitch - pChord->C.Root), Third = pChord->C.Third();
				return pChord->C.Has(Interval) || Interval == 2 || (Third == 4 && Interval == 9) || (Third == 3 && Interval == 5);
			};
			int Best = h, BestScore = -1;
			for (int Step = -1; Step <= 1; Step += 2) {
				const int Candidate = h + Step;
				if (ClashesWithChord(Candidate, pChord->C) || Rubs(Candidate, p))
					continue;
				if (Diatonic && !InKey(Candidate, O.K) && !pChord->C.HasPitchClass(Mod12(Candidate)))
					continue;
				if (!Diatonic && !InChordScale(Candidate, ChordNotes) && !Color(Candidate))
					continue;
				const int Gap = Mod12(p - Candidate);
				const int Score = (Gap == 3 || Gap == 4 || Gap == 8 || Gap == 9 ? 2 : 0) + (pChord->C.HasPitchClass(Mod12(Candidate)) ? 1 : 0);
				if (Score > BestScore) {
					BestScore = Score;
					Best = Candidate;
				}
			}
			if (BestScore < 0) {
				// Neither neighbor fits: the nearest of the chord's notes a third
				// to a sixth from the melody (a fifth under a sus chord's)
				const bool Below = O.Interval != HARMONY_THIRD_ABOVE;
				for (int Distance = 3; Distance <= 9; ++Distance) {
					const int Candidate = Below ? p - Distance : p + Distance;
					if (pChord->C.HasPitchClass(Mod12(Candidate))) {
						Best = Candidate;
						break;
					}
				}
			}
			h = Best;
		}
		while (h < O.Low && h + 12 <= O.High)
			h += 12;
		while (h > O.High && h - 12 >= O.Low)
			h -= 12;
		if (h < 0 || h >= PITCHES)
			continue;
		PartNote Note = MakeNote(N.Row, N.Rows, h, std::max(1, N.Volume - O.VolumeDrop));
		// The melody's effects, but its pitch: the voice comes in on its own note (no scoop copied a semitone off it), a slide
		// later in the note goes to the voice's own note for the melody's new one, and 1xx/2xx, which move by the period,
		// stay the melody's; a fall as the note ends goes along as it is (a sigh, not a pitch)
		int Melodic = p;
		for (size_t e = 0; e < N.Effects.size(); ++e) {
			int Effect = N.Effects[e].second.first, Param = N.Effects[e].second.second;
			if (Effect == EFF_ARPEGGIO || Effect == EFF_PAN || IsSongEffect(Effect))	// the harmony keeps its own channel's place
				continue;
			if (Effect == EFF_PORTA_UP || Effect == EFF_PORTA_DOWN)
				continue;
			const int Slide = SlideSemitones(Effect, Param);
			if (Slide != 0) {
				if (N.Effects[e].first == 0)
					continue;
				if (N.Rows - N.Effects[e].first > std::max(1, R / 2)) {
					// The voice moves as far in the key as the melody's own note does
					const int Semitones = Plain(Melodic + Slide, pScale) - Plain(Melodic, pScale);
					Melodic += Slide;
					if (Semitones == 0)
						continue;
					Effect = Semitones > 0 ? EFF_SLIDE_UP : EFF_SLIDE_DOWN;
					Param = (Param & 0xF0) | std::min(15, std::abs(Semitones));
				}
			}
			if (N.Effects[e].first == 0)
				Note.Effects.push_back(std::make_pair(Effect, Param));
			else
				Note.LaterEffects.push_back(std::make_pair(N.Effects[e].first, std::make_pair(Effect, Param)));
		}
		Out.push_back(Note);
		Previous = h;
	}
	(void)Previous;
	return Out;
}

// Counter-melody

CounterOptions::CounterOptions() : Above(false), RowsPerBeat(4), K(), Low(36), High(55), Volume(10), Rests(RESTS_FILL), Slow(false)
{
}

std::vector<BarSpan> MelodyRests(const std::vector<NoteInfo> &Melody, int First, int End, int RowsPerBeat)
{
	std::vector<BarSpan> Out;
	if (End <= First)
		return Out;
	std::vector<bool> Sounds(static_cast<size_t>(End - First), false);
	for (size_t i = 0; i < Melody.size(); ++i)
		for (int r = std::max(First, Melody[i].Row); r < std::min(End, Melody[i].Row + Melody[i].Rows); ++r)
			Sounds[static_cast<size_t>(r - First)] = true;
	const int Least = std::max(1, RowsPerBeat / 2);
	for (int r = First; r < End; ) {
		if (Sounds[static_cast<size_t>(r - First)]) {
			++r;
			continue;
		}
		int To = r;
		while (To < End && !Sounds[static_cast<size_t>(To - First)])
			++To;
		if (To - r >= Least)
			Out.push_back(BarSpan(r, To - r));
		r = To;
	}
	return Out;
}

namespace {

// The melody's notes sounding on [From, To)
std::vector<const NoteInfo *> Sounding(const std::vector<NoteInfo> &Melody, int From, int To)
{
	std::vector<const NoteInfo *> Out;
	for (size_t i = 0; i < Melody.size(); ++i)
		if (Melody[i].Row < To && Melody[i].Row + Melody[i].Rows > From)
			Out.push_back(&Melody[i]);
	return Out;
}

// How badly a counter-melody note fits against the melody: semitones and
// sevenths apart, unisons, and crossing it
double Clash(int Pitch, int From, int To, const std::vector<NoteInfo> &Melody, bool Above)
{
	double Cost = 0.0;
	const std::vector<const NoteInfo *> Notes = Sounding(Melody, From, To);
	for (size_t i = 0; i < Notes.size(); ++i) {
		const int m = Notes[i]->Pitch;
		const int Overlap = std::min(To, Notes[i]->Row + Notes[i]->Rows) - std::max(From, Notes[i]->Row);
		const double Weight = Overlap >= 2 ? 1.0 : 0.4;
		const int d = Mod12(m - Pitch);
		if (d == 1 || d == 11)
			Cost += 4.0 * Weight;
		else if (d == 6)
			Cost += 1.5 * Weight;
		else if (d == 2 || d == 10)
			Cost += 0.8 * Weight;
		if (m == Pitch)
			Cost += 3.0 * Weight;
		else if (d == 0)
			Cost += 2.0 * Weight;	// the melody an octave away: a doubling, not a line of its own
		if (!Above && Pitch > m - 2)
			Cost += 5.0 * Weight;
		if (Above && Pitch < m + 2)
			Cost += 5.0 * Weight;
	}
	return Cost;
}

// The note of a chord for the line: its third or seventh near Near, clear of the melody
int CounterPitch(const Chord &C, int Near, int From, int To, const std::vector<NoteInfo> &Melody, const CounterOptions &O, bool Moving)
{
	int Best = Near;
	double BestCost = 1e9;
	for (int p = O.Low; p <= O.High; ++p) {
		const int Interval = Mod12(p - C.Root);
		if (!C.Has(Interval))
			continue;
		double Weight = Interval == C.Third() ? 3.0 : Interval == C.Seventh() ? 2.8 : Interval == 0 ? 1.0 :
			Interval == C.Fifth() ? 1.3 : 2.0;
		double Cost = 0.5 * std::abs(p - Near) - Weight + Clash(p, From, To, Melody, O.Above);
		if (Moving && p == Near)
			Cost += 1.0;	// a line, not a held note
		if (std::abs(p - Near) > 7)
			Cost += 2.0;
		if (Cost < BestCost) {
			BestCost = Cost;
			Best = p;
		}
	}
	return Best;
}

// A note for a line moving over a chord: one of its tones, or of the key's
// that doesn't rub a semitone against a tone of the chord from outside the key
// (no F under an F#7 in G, no B over a C#dim7's Bb). Over the key's own
// chords, any note of the key.
bool FitsChord(int Pitch, const Chord &C, const Key &K)
{
	if (C.HasPitchClass(Mod12(Pitch)))
		return true;
	if (!InKey(Pitch, K))
		return false;
	for (int d = -1; d <= 1; d += 2)
		if (C.HasPitchClass(Mod12(Pitch + d)) && !InKey(Pitch + d, K))
			return false;
	return true;
}

// A step of the line up or down from Pitch: the key's next note, or over a
// chord from outside the key the next note that fits it
int ChordStep(int Pitch, int Direction, const Chord &C, const Key &K)
{
	const int Step = ScaleStep(Pitch, Direction, K);
	if (FitsChord(Step, C, K))
		return Step;
	for (int d = 1; d <= 4; ++d)
		if (FitsChord(Pitch + Direction * d, C, K))
			return Pitch + Direction * d;
	return Step;
}

// A note near Pitch that fits against the melody and the chord: itself, a chord tone or a scale step
int Fitted(int Pitch, int From, int To, const ChordAt *pChord, const std::vector<NoteInfo> &Melody, const CounterOptions &O)
{
	auto Fits = [&](int p) { return pChord != 0 ? FitsChord(p, pChord->C, O.K) : InKey(p, O.K); };
	if (Clash(Pitch, From, To, Melody, O.Above) < 0.8 && Fits(Pitch))
		return Pitch;
	int Best = Pitch;
	double BestCost = Fits(Pitch) ? Clash(Pitch, From, To, Melody, O.Above) : 1e9;
	for (int d = -4; d <= 4; ++d) {
		const int p = Pitch + d;
		if (d == 0 || p < O.Low - 2 || p > O.High + 2)
			continue;
		const bool ChordTone = pChord != 0 && pChord->C.HasPitchClass(Mod12(p));
		if (!Fits(p))
			continue;
		const double Cost = Clash(p, From, To, Melody, O.Above) + 0.3 * std::abs(d) + (ChordTone ? 0.0 : 0.5);
		if (Cost < BestCost) {
			BestCost = Cost;
			Best = p;
		}
	}
	return Best;
}

// A note for a line that has held Pitch long enough: a step of the scale, or a near note of the chord, that the chord
// takes (no semitone against its tones) and that keeps clear of the melody; Pitch when none does
int MoveOn(int Pitch, int From, int To, const ChordAt *pChord, const std::vector<NoteInfo> &Melody, const CounterOptions &O)
{
	int Best = Pitch;
	double BestCost = 1e9;
	for (int d = -4; d <= 4; ++d) {
		const int p = Pitch + d;
		if (d == 0 || p < O.Low - 2 || p > O.High + 2)
			continue;
		const bool ChordTone = pChord != 0 && pChord->C.HasPitchClass(Mod12(p));
		if (pChord != 0 ? !(ChordTone || (FitsChord(p, pChord->C, O.K) && !ClashesWithChord(p, pChord->C))) : !InKey(p, O.K))
			continue;
		const double Cost = Clash(p, From, To, Melody, O.Above) + 0.35 * std::abs(d) + (ChordTone ? 0.0 : 0.4) +
			(std::abs(d) > 2 ? 0.8 : 0.0);
		if (Cost < BestCost) {
			BestCost = Cost;
			Best = p;
		}
	}
	return Best;
}

// The line breathing with the melody: silent in its rests (a note there cut, the part after a rest struck again where the
// melody comes back, on a note that fits there), and with Answer a short answer in each rest of two beats or more after
// the melody has started: from half a beat into the rest, by step toward where the line goes on, at most three notes and
// two beats, ending half a beat before the melody comes back. Answers counts them.
std::vector<PartNote> Breathe(const std::vector<PartNote> &Line, const std::vector<NoteInfo> &Melody, const std::vector<ChordAt> &Chords,
	int First, int End, const CounterOptions &O, bool Answer, int &Answers)
{
	const int R = std::max(1, O.RowsPerBeat), Half = std::max(1, R / 2);
	const std::vector<BarSpan> Rests = MelodyRests(Melody, First, End, R);
	std::vector<PartNote> Kept;
	for (size_t i = 0; i < Line.size(); ++i) {
		const PartNote &N = Line[i];
		const int To = N.Row + N.Rows;
		int From = N.Row;
		bool Cut = false;
		auto Piece = [&](int a, int b) {
			if (b <= a || (Cut && b - a < Half))
				return;	// a sliver between rests
			PartNote P = N;
			P.Row = a;
			P.Rows = b - a;
			if (a != N.Row) {
				const ChordAt *pHere = ChordAtRow(Chords, a);
				P.Pitch = Fitted(P.Pitch, a, std::min(b, a + R), pHere, Melody, O);
				P.Volume = O.Volume;
			}
			Kept.push_back(P);
		};
		for (size_t k = 0; k < Rests.size() && From < To; ++k) {
			const BarSpan &Rest = Rests[k];
			if (Rest.Row >= To || Rest.Row + Rest.Rows <= From)
				continue;
			Cut = true;
			Piece(From, Rest.Row);
			From = std::max(From, Rest.Row + Rest.Rows);
		}
		Piece(From, To);
	}
	Answers = 0;
	int Started = End;
	for (size_t i = 0; i < Melody.size(); ++i)
		if (Melody[i].Row + Melody[i].Rows > First && Melody[i].Row < End)
			Started = std::min(Started, std::max(First, Melody[i].Row));
	for (size_t k = 0; Answer && k < Rests.size(); ++k) {
		const BarSpan &Rest = Rests[k];
		if (Rest.Rows < 2 * R || Rest.Row < Started)
			continue;
		const int Start = (Rest.Row + Half + Half - 1) / Half * Half;
		const int Stop = std::min(Rest.Row + Rest.Rows - Half, Start + 2 * R);
		const ChordAt *pChord = ChordAtRow(Chords, Start);
		if (Stop - Start < Half || pChord == 0)
			continue;
		// From the line's note before the rest toward its note after it
		int Before = -1, After = -1, BeforeRow = -1, AfterRow = End;
		for (size_t i = 0; i < Kept.size(); ++i) {
			if (Kept[i].Row < Rest.Row && Kept[i].Row > BeforeRow) {
				Before = Kept[i].Pitch;
				BeforeRow = Kept[i].Row;
			}
			else if (Kept[i].Row >= Rest.Row + Rest.Rows && Kept[i].Row < AfterRow) {
				After = Kept[i].Pitch;
				AfterRow = Kept[i].Row;
			}
		}
		const int Near = Before >= 0 ? Before : After >= 0 ? After : (O.Above ? O.Low + 4 : O.High - 4);
		const int Head = CounterPitch(pChord->C, Near, Start, Stop, Melody, O, true);
		const int Toward = After >= 0 ? After : Head;
		const int Length = Stop - Start >= 2 * R ? R : Half;	// quarter notes in a long rest, eighths in a short one
		const size_t Count = static_cast<size_t>(std::max(1, std::min(3, (Stop - Start) / Length)));
		const int Direction = Toward > Head ? 1 : Toward < Head ? -1 : (O.Above ? 1 : -1);
		std::vector<int> Path(1, Head);
		for (int p = Head; Path.size() < Count; ) {
			p = ChordStep(p, Direction, pChord->C, O.K);
			if (Toward != Head && (Direction > 0 ? p >= Toward : p <= Toward))
				break;	// not onto where the line goes on before its time
			Path.push_back(p);
		}
		for (size_t n = 0; n < Path.size(); ++n) {
			const int Row = Start + static_cast<int>(n) * Length;
			const int Rows = n + 1 < Path.size() ? Length : Stop - Row;
			if (Rows <= 0)
				break;
			const ChordAt *pHere = ChordAtRow(Chords, Row);
			Kept.push_back(MakeNote(Row, Rows, Fitted(Clamp(Path[n], O.Low - 2, O.High + 2), Row, Row + Rows, pHere != 0 ? pHere : pChord,
				Melody, O), O.Volume));
		}
		++Answers;
	}
	std::stable_sort(Kept.begin(), Kept.end(), [](const PartNote &a, const PartNote &b) { return a.Row < b.Row; });
	return Kept;
}

}	// namespace

std::vector<PartNote> CounterMelody(const std::vector<NoteInfo> &Melody, const std::vector<ChordAt> &Chords,
	const std::vector<BarSpan> &Bars, const CounterOptions &O, int *pAnswers)
{
	std::vector<PartNote> Out;
	if (pAnswers != 0)
		*pAnswers = 0;
	if (Bars.empty() || Chords.empty())
		return Out;
	const int R = std::max(1, O.RowsPerBeat);
	const int First = Bars.front().Row, End = Bars.back().Row + Bars.back().Rows;

	// Windows of half a bar (a whole one when it's short, or for a slow line)
	struct Window { int From, To; const BarSpan *pBar; };
	std::vector<Window> Windows;
	for (size_t b = 0; b < Bars.size(); ++b) {
		const int Beats = Bars[b].Rows / R;
		if (Beats >= 4 && !O.Slow) {
			const int Middle = Bars[b].Row + Beats / 2 * R;
			const Window A = { Bars[b].Row, Middle, &Bars[b] }, Z = { Middle, Bars[b].Row + Bars[b].Rows, &Bars[b] };
			Windows.push_back(A);
			Windows.push_back(Z);
		}
		else {
			const Window W = { Bars[b].Row, Bars[b].Row + Bars[b].Rows, &Bars[b] };
			Windows.push_back(W);
		}
	}

	// A line, not a drone: where a note would hold on past a bar, it moves on, mostly by step, to one that fits
	int RunPitch = -1, RunStart = 0, RunEnd = -1;
	auto Put = [&](int Row, int Rows, int Pitch, const ChordAt *pHere, int BarRows) {
		if (Pitch == RunPitch && Row == RunEnd && Row + Rows - RunStart > BarRows)
			Pitch = MoveOn(Pitch, Row, std::min(Row + Rows, Row + R), pHere, Melody, O);
		if (Pitch != RunPitch || Row != RunEnd) {
			RunPitch = Pitch;
			RunStart = Row;
		}
		RunEnd = Row + Rows;
		Out.push_back(MakeNote(Row, Rows, Pitch, O.Volume));
		return Pitch;
	};
	int Previous = O.Above ? O.Low + 4 : O.High - 4;
	for (size_t w = 0; w < Windows.size(); ++w) {
		const Window &W = Windows[w];
		const ChordAt *pChord = ChordAtRow(Chords, W.From);
		if (pChord == 0)
			continue;
		int Onsets = 0;
		for (size_t i = 0; i < Melody.size(); ++i)
			if (Melody[i].Row >= W.From && Melody[i].Row < W.To)
				++Onsets;
		const int Beats = std::max(1, (W.To - W.From) / R);
		const bool Busy = Onsets >= std::max(3, Beats + 1);
		const bool Calm = Onsets <= 1;
		const int Target = CounterPitch(pChord->C, Previous, W.From, std::min(W.To, W.From + R), Melody, O, !Busy);

		if (Busy || Beats == 1) {
			Previous = Put(W.From, W.To - W.From, Target, pChord, W.pBar->Rows);
			continue;
		}
		// Where the line heads: the next window's note
		int Next = Target;
		if (w + 1 < Windows.size()) {
			const ChordAt *pNext = ChordAtRow(Chords, Windows[w + 1].From);
			if (pNext != 0)
				Next = CounterPitch(pNext->C, Target, Windows[w + 1].From, std::min(Windows[w + 1].To, Windows[w + 1].From + R),
					Melody, O, false);
		}
		// The way to it by steps of the scale, the next note itself left out;
		// a neighbour note and back when the line stays
		std::vector<int> Path(1, Target);
		if (Next == Target) {
			if (Calm) {
				// The neighbour that sounds best against the melody, away from it on a tie
				const int Away = ChordStep(Target, O.Above ? 1 : -1, pChord->C, O.K), Toward = ChordStep(Target, O.Above ? -1 : 1, pChord->C, O.K);
				const int From = W.To - R;
				Path.push_back(Clash(Toward, From, W.To, Melody, O.Above) + 0.2 < Clash(Away, From, W.To, Melody, O.Above) ? Toward : Away);
			}
		}
		else {
			const int Direction = Next > Target ? 1 : -1;
			for (int p = ChordStep(Target, Direction, pChord->C, O.K); Direction > 0 ? p < Next : p > Next; p = ChordStep(p, Direction, pChord->C, O.K))
				Path.push_back(p);
		}
		// As many notes as there are beats (two while the melody moves some), the
		// last ones moving, the first held: a pickup into the next chord. A slow
		// line holds its note
		const size_t Most = static_cast<size_t>(O.Slow ? 1 : Calm ? Beats : 2);
		if (Path.size() > Most) {
			std::vector<int> Fewer(1, Target);
			for (size_t k = 1; k < Most; ++k)
				Fewer.push_back(Path[Path.size() - Most + k]);
			Path = Fewer;
		}
		const int Moving = static_cast<int>(Path.size()) - 1;
		for (size_t k = 0; k < Path.size(); ++k) {
			const int Row = k == 0 ? W.From : W.To - (Moving - static_cast<int>(k) + 1) * R;
			const int To = k + 1 < Path.size() ? W.To - (Moving - static_cast<int>(k)) * R : W.To;
			if (To <= Row)
				continue;
			const int Pitch = Clamp(Path[k], O.Low - 2, O.High + 2);
			const ChordAt *pHere = ChordAtRow(Chords, Row);	// the chord can change within the window
			const int Played = k == 0 ? Pitch : Fitted(Pitch, Row, std::min(To, Row + R), pHere != 0 ? pHere : pChord, Melody, O);
			Previous = Put(Row, To - Row, Played, pHere != 0 ? pHere : pChord, W.pBar->Rows);
		}
	}
	// Repeated notes become one held note
	std::vector<PartNote> Merged;
	for (size_t i = 0; i < Out.size(); ++i) {
		if (!Merged.empty() && Merged.back().Pitch == Out[i].Pitch && Merged.back().Row + Merged.back().Rows == Out[i].Row) {
			Merged.back().Rows += Out[i].Rows;
			continue;
		}
		Merged.push_back(Out[i]);
	}
	if (O.Rests != RESTS_FILL) {
		int Answers = 0;
		Merged = Breathe(Merged, Melody, Chords, First, End, O, O.Rests == RESTS_ANSWER, Answers);
		if (pAnswers != 0)
			*pAnswers = Answers;
	}
	TidyPart(Merged, First, End);
	return Merged;
}

void TidyPart(std::vector<PartNote> &Notes, int First, int End)
{
	std::stable_sort(Notes.begin(), Notes.end(), [](const PartNote &a, const PartNote &b) { return a.Row < b.Row; });
	std::vector<PartNote> Out;
	for (size_t i = 0; i < Notes.size(); ++i) {
		if (Notes[i].Row < First || Notes[i].Row >= End)
			continue;
		if (!Out.empty() && Out.back().Row == Notes[i].Row)
			continue;
		Out.push_back(Notes[i]);
	}
	int Volume = -2;
	for (size_t i = 0; i < Out.size(); ++i) {
		const int Limit = i + 1 < Out.size() ? Out[i + 1].Row : End;
		Out[i].Rows = std::max(1, std::min(Out[i].Rows, Limit - Out[i].Row));
		if (Out[i].Volume >= 0) {
			if (Out[i].Volume == Volume)
				Out[i].Volume = -1;
			else
				Volume = Out[i].Volume;
		}
	}
	Notes = Out;
}

}	// namespace ftapi

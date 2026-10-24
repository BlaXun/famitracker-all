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

#include "Chords.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <initializer_list>

namespace ftapi {

namespace {

const int MAJOR_STEPS[7] = { 0, 2, 4, 5, 7, 9, 11 };

int Mod12(int Value)
{
	return ((Value % 12) + 12) % 12;
}

std::string Trim(const std::string &Text)
{
	size_t First = 0, Last = Text.size();
	while (First < Last && std::isspace(static_cast<unsigned char>(Text[First])))
		++First;
	while (Last > First && std::isspace(static_cast<unsigned char>(Text[Last - 1])))
		--Last;
	return Text.substr(First, Last - First);
}

bool Starts(const std::string &Text, size_t At, const char *Word)
{
	return Text.compare(At, std::strlen(Word), Word) == 0;
}

bool StartsNoCase(const std::string &Text, size_t At, const char *Word)
{
	const size_t Length = std::strlen(Word);
	if (At + Length > Text.size())
		return false;
	for (size_t k = 0; k < Length; ++k)
		if (std::tolower(static_cast<unsigned char>(Text[At + k])) != Word[k])
			return false;
	return true;
}

bool DigitAt(const std::string &Text, size_t At)
{
	return At < Text.size() && std::isdigit(static_cast<unsigned char>(Text[At]));
}

std::string ReplaceAll(const std::string &Text, const char *From, const char *To)
{
	std::string Out;
	const size_t Length = std::strlen(From);
	for (size_t i = 0; i < Text.size(); ) {
		if (Text.compare(i, Length, From) == 0) {
			Out += To;
			i += Length;
		}
		else
			Out += Text[i++];
	}
	return Out;
}

// A chord as written, its symbols as letters and without spaces or brackets
std::string Plain(const std::string &Text)
{
	static const char *const SYMBOLS[][2] = {
		{ "\xE2\x99\xAF", "#" },				// U+266F sharp sign
		{ "\xE2\x99\xAD", "b" },				// U+266D flat sign
		{ "\xE2\x88\x92", "-" },				// U+2212 minus sign
		{ "\xC3\xB8" "7", "m7b5" },			// U+00F8 o with a stroke: half-diminished
		{ "\xC3\xB8", "m7b5" },
		{ "\xC3\x98" "7", "m7b5" },
		{ "\xC3\x98", "m7b5" },
		{ "\xC2\xB0", "dim" },				// U+00B0 degree sign: diminished
		{ "\xC2\xBA", "dim" },				// U+00BA, typed for it too
		{ "\xCE\x94" "7", "maj7" },			// U+0394 delta: major seventh
		{ "\xCE\x94" "9", "maj9" },
		{ "\xCE\x94", "maj7" },
		{ "\xE2\x96\xB3" "7", "maj7" },		// U+25B3 white triangle
		{ "\xE2\x96\xB3", "maj7" },
		{ "6/9", "69" },
	};
	std::string Out = Text;
	for (size_t i = 0; i < sizeof(SYMBOLS) / sizeof(SYMBOLS[0]); ++i)
		Out = ReplaceAll(Out, SYMBOLS[i][0], SYMBOLS[i][1]);
	std::string Kept;
	for (size_t i = 0; i < Out.size(); ++i)
		if (Out[i] != ' ' && Out[i] != '(' && Out[i] != ')' && Out[i] != '\t')
			Kept += Out[i];
	return Kept;
}

// A note name at the start of Text ("C", "F#", "Bb"): its pitch class, the
// characters it took, and 1 for a sharp, -1 for a flat
bool ReadNote(const std::string &Text, size_t At, int &PitchClass, size_t &Used, int &Spelling)
{
	Spelling = 0;
	static const int LETTER_CLASS[] = { 9, 11, 0, 2, 4, 5, 7 };	// A to G
	if (At >= Text.size())
		return false;
	const char Letter = static_cast<char>(std::tolower(static_cast<unsigned char>(Text[At])));
	if (Letter < 'a' || Letter > 'g')
		return false;
	PitchClass = LETTER_CLASS[Letter - 'a'];
	size_t i = At + 1;
	if (i < Text.size() && (Text[i] == '#' || Text[i] == 'b')) {
		Spelling = Text[i] == '#' ? 1 : -1;
		PitchClass += Spelling;
		++i;
	}
	PitchClass = Mod12(PitchClass);
	Used = i - At;
	return true;
}

// A degree in Roman numerals at the start of Text, with the sharp or flat
// before it: 1-7, upper case (major) or not, and the characters it took
bool ReadDegree(const std::string &Text, size_t At, int &Degree, int &Accidental, bool &Upper, size_t &Used)
{
	static const char *const NUMERALS[] = { "VII", "III", "IV", "VI", "II", "V", "I" };	// the longest first
	static const int VALUES[] = { 7, 3, 4, 6, 2, 5, 1 };
	size_t i = At;
	Accidental = 0;
	if (i < Text.size() && (Text[i] == 'b' || Text[i] == '#')) {
		Accidental = Text[i] == '#' ? 1 : -1;
		++i;
	}
	for (size_t n = 0; n < sizeof(NUMERALS) / sizeof(NUMERALS[0]); ++n) {
		const std::string Numeral = NUMERALS[n];
		std::string Lower = Numeral;
		for (size_t k = 0; k < Lower.size(); ++k)
			Lower[k] = static_cast<char>(std::tolower(static_cast<unsigned char>(Lower[k])));
		if (Text.compare(i, Numeral.size(), Numeral) == 0)
			Upper = true;
		else if (Text.compare(i, Lower.size(), Lower) == 0)
			Upper = false;
		else
			continue;
		Degree = VALUES[n];
		Used = i + Numeral.size() - At;
		return true;
	}
	return false;
}

bool IsDegreeStart(const std::string &Text)
{
	size_t i = 0;
	if (i < Text.size() && (Text[i] == 'b' || Text[i] == '#'))
		++i;
	return i < Text.size() && (Text[i] == 'I' || Text[i] == 'V' || Text[i] == 'i' || Text[i] == 'v');
}

// The root of a degree: on the key's own scale, or with a sharp or flat
// against the major scale ("bVII" in C is Bb, and in A minor G)
int DegreeRoot(int Degree, int Accidental, const Key &K)
{
	if (Accidental == 0)
		return Mod12(K.Tonic + ModeSteps(K.Mode)[Degree - 1]);
	return Mod12(K.Tonic + MAJOR_STEPS[Degree - 1] + Accidental);
}

void AddTone(std::vector<int> &Tones, int Interval)
{
	const int Tone = Mod12(Interval);
	if (std::find(Tones.begin(), Tones.end(), Tone) == Tones.end())
		Tones.push_back(Tone);
}

// The tones of a chord's suffix ("m7", "maj9", "sus4", "7b9"), from a major
// or (MinorBase) a minor triad
bool ReadSuffix(const std::string &Text, bool MinorBase, std::vector<int> &Tones)
{
	int Third = MinorBase ? 3 : 4, Fifth = 7, Seventh = -1;
	bool Major7 = false, Diminished = false, Quality = false;
	std::vector<int> Added;
	size_t i = 0;

	// Major (maj, M), minor (m, min, mi, -), diminished (dim, o) or augmented (aug, +)
	if (StartsNoCase(Text, i, "maj")) {
		Major7 = true;
		i += 3;
	}
	else if (Starts(Text, i, "ma") && DigitAt(Text, i + 2)) {
		Major7 = true;
		i += 2;
	}
	else if (Starts(Text, i, "M") && (i + 1 == Text.size() || DigitAt(Text, i + 1))) {
		Major7 = true;
		i += 1;
	}
	else if (StartsNoCase(Text, i, "min")) {
		Third = 3;
		i += 3;
	}
	else if (Starts(Text, i, "mi") && (i + 2 == Text.size() || DigitAt(Text, i + 2))) {
		Third = 3;
		i += 2;
	}
	else if (Starts(Text, i, "m")) {
		Third = 3;
		i += 1;
	}
	else if (Starts(Text, i, "-") && (i + 1 == Text.size() || DigitAt(Text, i + 1))) {
		Third = 3;
		i += 1;
	}
	else if (StartsNoCase(Text, i, "dim")) {
		Third = 3;
		Fifth = 6;
		Diminished = true;
		i += 3;
	}
	else if (Starts(Text, i, "o")) {
		Third = 3;
		Fifth = 6;
		Diminished = true;
		i += 1;
	}
	else if (StartsNoCase(Text, i, "aug")) {
		Fifth = 8;
		i += 3;
	}
	else if (Starts(Text, i, "+") && (i + 1 == Text.size() || DigitAt(Text, i + 1))) {
		Fifth = 8;
		i += 1;
	}
	Quality = i > 0;

	// The number: 5 (just the fifth), 6, 6/9, 7, 9, 11 or 13; 2 and 4 add a ninth or suspend
	if (DigitAt(Text, i)) {
		int Number = 0;
		while (DigitAt(Text, i))
			Number = Number * 10 + (Text[i++] - '0');
		const int SeventhTone = Major7 ? 11 : Diminished ? 9 : 10;
		switch (Number) {
		case 2: Added.push_back(2); break;
		case 4: Third = 5; break;
		case 5:
			if (Quality)
				return false;
			Third = -1;
			break;
		case 6: Added.push_back(9); break;
		case 69: Added.push_back(9); Added.push_back(2); break;
		case 7: Seventh = SeventhTone; break;
		case 9: Seventh = SeventhTone; Added.push_back(2); break;
		case 11: Seventh = SeventhTone; Added.push_back(2); Added.push_back(5); break;
		case 13: Seventh = SeventhTone; Added.push_back(2); Added.push_back(9); break;
		default: return false;
		}
	}

	// Suspensions, added notes and altered ones, in any order
	while (i < Text.size()) {
		static const struct { const char *Word; int Kind; int Value; } WORDS[] = {
			// Kind 0: added tone, 1: third, 2: fifth, 3: seventh
			{ "sus2", 1, 2 }, { "sus4", 1, 5 }, { "sus", 1, 5 },
			{ "add9", 0, 2 }, { "add2", 0, 2 }, { "add11", 0, 5 }, { "add4", 0, 5 }, { "add13", 0, 9 }, { "add6", 0, 9 },
			{ "b13", 0, 8 }, { "-13", 0, 8 }, { "#11", 0, 6 }, { "+11", 0, 6 }, { "b9", 0, 1 }, { "-9", 0, 1 },
			{ "#9", 0, 3 }, { "+9", 0, 3 }, { "b5", 2, 6 }, { "-5", 2, 6 }, { "#5", 2, 8 }, { "+5", 2, 8 },
			{ "maj7", 3, 11 }, { "Maj7", 3, 11 }, { "M7", 3, 11 }, { "no3", 1, -1 }, { "omit3", 1, -1 }, { "no5", 2, -1 },
			{ "omit5", 2, -1 }, { ",", -1, 0 },
		};
		bool Found = false;
		for (size_t w = 0; w < sizeof(WORDS) / sizeof(WORDS[0]) && !Found; ++w) {
			if (!Starts(Text, i, WORDS[w].Word))
				continue;
			i += std::strlen(WORDS[w].Word);
			Found = true;
			switch (WORDS[w].Kind) {
			case 0: Added.push_back(WORDS[w].Value); break;
			case 1: Third = WORDS[w].Value; break;
			case 2: Fifth = WORDS[w].Value; break;
			case 3: Seventh = WORDS[w].Value; break;
			}
		}
		if (!Found)
			return false;
	}

	Tones.assign(1, 0);
	if (Third >= 0)
		AddTone(Tones, Third);
	if (Fifth >= 0)
		AddTone(Tones, Fifth);
	if (Seventh >= 0)
		AddTone(Tones, Seventh);
	for (size_t a = 0; a < Added.size(); ++a)
		AddTone(Tones, Added[a]);
	std::sort(Tones.begin(), Tones.end());
	return Tones.size() >= 2;
}

// Chord names by their tones, as bits above the root
struct ChordKind {
	unsigned Bits;
	const char *Suffix;		// after the root: "m7"
	const char *Degree;		// after a Roman numeral, whose case gives the third: "7" ("V7", "ii7")
};

unsigned Bits(std::initializer_list<int> Tones)
{
	unsigned Out = 0;
	for (int Tone : Tones)
		Out |= 1u << Tone;
	return Out;
}

const std::vector<ChordKind> &ChordKinds()
{
	static const std::vector<ChordKind> KINDS = {
		{ Bits({ 0, 4, 7 }), "", "" },
		{ Bits({ 0, 3, 7 }), "m", "" },
		{ Bits({ 0, 3, 6 }), "dim", "o" },
		{ Bits({ 0, 4, 8 }), "aug", "+" },
		{ Bits({ 0, 2, 7 }), "sus2", "sus2" },
		{ Bits({ 0, 5, 7 }), "sus4", "sus4" },
		{ Bits({ 0, 7 }), "5", "5" },
		{ Bits({ 0, 4, 7, 11 }), "maj7", "maj7" },
		{ Bits({ 0, 4, 7, 10 }), "7", "7" },
		{ Bits({ 0, 3, 7, 10 }), "m7", "7" },
		{ Bits({ 0, 3, 6, 10 }), "m7b5", "\xC3\xB8" "7" },
		{ Bits({ 0, 3, 6, 9 }), "dim7", "o7" },
		{ Bits({ 0, 3, 7, 11 }), "m(maj7)", "maj7" },
		{ Bits({ 0, 4, 8, 10 }), "aug7", "+7" },
		{ Bits({ 0, 4, 8, 11 }), "maj7#5", "+maj7" },
		{ Bits({ 0, 5, 7, 10 }), "7sus4", "7sus4" },
		{ Bits({ 0, 2, 7, 10 }), "7sus2", "7sus2" },
		{ Bits({ 0, 4, 7, 9 }), "6", "6" },
		{ Bits({ 0, 3, 7, 9 }), "m6", "6" },
		{ Bits({ 0, 2, 4, 7 }), "add9", "add9" },
		{ Bits({ 0, 2, 3, 7 }), "m(add9)", "add9" },
		{ Bits({ 0, 4, 5, 7 }), "add11", "add11" },
		{ Bits({ 0, 2, 4, 7, 10 }), "9", "9" },
		{ Bits({ 0, 2, 4, 7, 11 }), "maj9", "maj9" },
		{ Bits({ 0, 2, 3, 7, 10 }), "m9", "9" },
		{ Bits({ 0, 2, 4, 7, 9 }), "6/9", "6/9" },
		{ Bits({ 0, 2, 3, 7, 9 }), "m6/9", "6/9" },
		{ Bits({ 0, 1, 4, 7, 10 }), "7b9", "7b9" },
		{ Bits({ 0, 3, 4, 7, 10 }), "7#9", "7#9" },
		{ Bits({ 0, 2, 5, 7, 10 }), "9sus4", "9sus4" },
		{ Bits({ 0, 4, 6, 10 }), "7b5", "7b5" },
		{ Bits({ 0, 4, 6, 7, 11 }), "maj7#11", "maj7#11" },
		{ Bits({ 0, 2, 4, 5, 7, 10 }), "11", "11" },
		{ Bits({ 0, 2, 3, 5, 7, 10 }), "m11", "11" },
		{ Bits({ 0, 2, 4, 7, 9, 10 }), "13", "13" },
		{ Bits({ 0, 2, 4, 7, 9, 11 }), "maj13", "maj13" },
	};
	return KINDS;
}

unsigned ToneBits(const Chord &C)
{
	unsigned Out = 0;
	for (size_t i = 0; i < C.Tones.size(); ++i)
		Out |= 1u << Mod12(C.Tones[i]);
	return Out;
}

// The suffix of a chord that isn't in the list: its triad's and the other tones in brackets
std::string OtherSuffix(const Chord &C)
{
	static const char *const EXTRA[12] = { "", "b9", "9", "#9", "11", "11", "#11", "", "b13", "13", "7", "maj7" };
	std::string Out = C.Has(3) && !C.Has(4) ? "m" : "";
	std::string Extras;
	for (size_t i = 0; i < C.Tones.size(); ++i) {
		const int Tone = C.Tones[i];
		if (Tone == 0 || Tone == 7 || Tone == 3 || Tone == 4)
			continue;
		if (!Extras.empty())
			Extras += ",";
		Extras += Tone == 5 && !C.Has(3) && !C.Has(4) ? "sus4" : Tone == 2 && !C.Has(3) && !C.Has(4) ? "sus2" :
			Tone == 6 && !C.Has(7) ? "b5" : Tone == 8 && !C.Has(7) ? "#5" : EXTRA[Tone];
	}
	if (!Extras.empty())
		Out += "(" + Extras + ")";
	return Out;
}

const ChordKind *FindKind(const Chord &C)
{
	const unsigned Wanted = ToneBits(C);
	const std::vector<ChordKind> &Kinds = ChordKinds();
	for (size_t i = 0; i < Kinds.size(); ++i)
		if (Kinds[i].Bits == Wanted)
			return &Kinds[i];
	return 0;
}

std::vector<int> TonesOf(std::initializer_list<int> List)
{
	return std::vector<int>(List);
}

// The degree (0-6) of a pitch class in the key's scale, -1 if it isn't in it
int ScaleDegree(int PitchClass, const Key &K)
{
	const int *pSteps = ModeSteps(K.Mode);
	const int Relative = Mod12(PitchClass - K.Tonic);
	for (int d = 0; d < 7; ++d)
		if (pSteps[d] == Relative)
			return d;
	return -1;
}

bool InScale(int PitchClass, const Key &K)
{
	return ScaleDegree(PitchClass, K) >= 0;
}

bool IsMinorMode(const Key &K)
{
	return ModeSteps(K.Mode)[2] == 3;
}

// A note outside the key's scale is a degree of the scale raised when it's
// in the major scale (the F# of #vi in A minor) or a tritone from the tonic,
// and otherwise lowered (the Bb of bVII in C major)
bool RaisedDegree(int PitchClass, const Key &K)
{
	const int Relative = Mod12(PitchClass - K.Tonic);
	if (Relative == 6)
		return true;
	for (int d = 0; d < 7; ++d)
		if (MAJOR_STEPS[d] == Relative)
			return true;
	return false;
}

// A chord's root or bass by name: as it was written (Spelling 1 for a
// sharp, -1 for a flat), else as the key spells its notes, and with a sharp
// or flat as its degree has one for the others
std::string NoteName(int PitchClass, const Key &K, int Spelling)
{
	static const char *const SHARPS[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
	static const char *const FLATS[] = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
	const int Class = Mod12(PitchClass);
	const bool Plain = std::strlen(SHARPS[Class]) == 1;
	if (Spelling != 0 && !Plain)
		return (Spelling > 0 ? SHARPS : FLATS)[Class];
	if (ScaleDegree(PitchClass, K) >= 0)
		return NoteClassName(PitchClass, K);
	return (RaisedDegree(PitchClass, K) ? SHARPS : FLATS)[Class];
}

}	// namespace

// Chords

Chord::Chord() : Root(0), Tones(TonesOf({ 0, 4, 7 })), Bass(0), RootSpelling(0), BassSpelling(0)
{
}

Chord::Chord(int Root, const std::vector<int> &Intervals) : Root(Mod12(Root)), Bass(Mod12(Root)), RootSpelling(0), BassSpelling(0)
{
	Tones.assign(1, 0);
	for (size_t i = 0; i < Intervals.size(); ++i)
		AddTone(Tones, Intervals[i]);
	std::sort(Tones.begin(), Tones.end());
}

bool Chord::Has(int Interval) const
{
	return std::find(Tones.begin(), Tones.end(), Mod12(Interval)) != Tones.end();
}

bool Chord::HasPitchClass(int PitchClass) const
{
	return Has(PitchClass - Root);
}

int Chord::Third() const
{
	if (Has(4))
		return 4;
	if (Has(3))
		return 3;
	if (Has(5))
		return 5;
	if (Has(2))
		return 2;
	return -1;
}

int Chord::Fifth() const
{
	if (Has(7))
		return 7;
	if (Has(6) && Has(3) && !Has(4))
		return 6;
	if (Has(8) && Has(4))
		return 8;
	return -1;
}

int Chord::Seventh() const
{
	if (Has(10))
		return 10;
	if (Has(11))
		return 11;
	if (Has(9) && Has(6) && Has(3))
		return 9;
	return -1;
}

bool Chord::IsMinor() const
{
	return Has(3) && !Has(4);
}

bool Chord::IsTriad() const
{
	if (Tones.size() != 3)
		return false;
	const unsigned B = ToneBits(*this);
	return B == Bits({ 0, 4, 7 }) || B == Bits({ 0, 3, 7 }) || B == Bits({ 0, 3, 6 }) || B == Bits({ 0, 4, 8 });
}

std::vector<int> Chord::PitchClasses() const
{
	std::vector<int> Out;
	for (size_t i = 0; i < Tones.size(); ++i)
		Out.push_back(Mod12(Root + Tones[i]));
	return Out;
}

bool Chord::operator==(const Chord &Other) const
{
	return Root == Other.Root && Bass == Other.Bass && ToneBits(*this) == ToneBits(Other);
}

// Names

bool ParseChord(const std::string &Input, const Key &K, Chord &Out)
{
	const std::string Text = Plain(Trim(Input));
	if (Text.empty())
		return false;

	// After a slash: the bass note (C/E), or the chord a degree belongs to (V7/V)
	std::string Main = Text, After;
	const size_t Slash = Text.find('/');
	if (Slash != std::string::npos) {
		Main = Text.substr(0, Slash);
		After = Text.substr(Slash + 1);
		if (Main.empty() || After.empty() || After.find('/') != std::string::npos)
			return false;
	}

	Chord C;
	if (IsDegreeStart(Main)) {
		int Degree, Accidental;
		bool Upper;
		size_t Used;
		if (!ReadDegree(Main, 0, Degree, Accidental, Upper, Used))
			return false;
		int Root = DegreeRoot(Degree, Accidental, K);
		int SlashBass = -1;
		if (!After.empty() && IsDegreeStart(After)) {
			// After V or vii, a degree whose dominant it is: V7/V is the V7 of the V.
			// After other degrees, the bass note: IV/V is IV over the V's root.
			int Of, OfAccidental;
			bool OfUpper;
			size_t OfUsed;
			if (!ReadDegree(After, 0, Of, OfAccidental, OfUpper, OfUsed) || OfUsed != After.size())
				return false;
			const int Target = DegreeRoot(Of, OfAccidental, K);
			if (Degree == 5 || Degree == 7)
				Root = Mod12(Target + MAJOR_STEPS[Degree - 1] + Accidental);
			else
				SlashBass = Target;
			After.clear();
		}
		std::vector<int> Tones;
		if (!ReadSuffix(Main.substr(Used), !Upper, Tones))
			return false;
		C = Chord(Root, Tones);
		C.RootSpelling = Accidental;
		if (SlashBass >= 0)
			C.Bass = SlashBass;
	}
	else {
		int Root, Spelling;
		size_t Used;
		if (!ReadNote(Main, 0, Root, Used, Spelling))
			return false;
		std::vector<int> Tones;
		if (!ReadSuffix(Main.substr(Used), false, Tones))
			return false;
		C = Chord(Root, Tones);
		C.RootSpelling = Spelling;
	}
	if (!After.empty()) {
		int Bass, Spelling;
		size_t Used;
		if (!ReadNote(After, 0, Bass, Used, Spelling) || Used != After.size())
			return false;
		C.Bass = Bass;
		C.BassSpelling = Spelling;
	}
	Out = C;
	return true;
}

std::string ChordName(const Chord &C, const Key &K)
{
	std::string Name = NoteName(C.Root, K, C.RootSpelling);
	const ChordKind *pKind = FindKind(C);
	Name += pKind != 0 ? pKind->Suffix : OtherSuffix(C);
	if (C.Bass != C.Root)
		Name += "/" + NoteName(C.Bass, K, C.BassSpelling);
	return Name;
}

std::string ChordDegree(const Chord &C, const Key &K)
{
	static const char *const NUMERALS[7] = { "I", "II", "III", "IV", "V", "VI", "VII" };
	const int Relative = Mod12(C.Root - K.Tonic);
	std::string Prefix;
	int Degree = ScaleDegree(C.Root, K);
	if (Degree < 0) {
		const int *pSteps = ModeSteps(K.Mode);
		const bool Sharp = RaisedDegree(C.Root, K);
		for (int Pass = 0; Pass < 2 && Degree < 0; ++Pass) {
			const bool Raise = Pass == 0 ? Sharp : !Sharp;
			for (int d = 0; d < 7 && Degree < 0; ++d) {
				if (Mod12(pSteps[d] + (Raise ? 1 : -1)) == Relative) {
					Degree = d;
					Prefix = Raise ? "#" : "b";
				}
			}
		}
	}
	std::string Numeral = NUMERALS[Degree < 0 ? 0 : Degree];
	const bool Lower = C.Has(3) && !C.Has(4);
	if (Lower)
		for (size_t i = 0; i < Numeral.size(); ++i)
			Numeral[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(Numeral[i])));
	std::string Out = Prefix + Numeral;
	const ChordKind *pKind = FindKind(C);
	if (pKind != 0)
		Out += pKind->Degree;
	else {
		std::string Suffix = OtherSuffix(C);
		if (Lower && !Suffix.empty() && Suffix[0] == 'm')
			Suffix = Suffix.substr(1);
		Out += Suffix;
	}
	if (C.Bass != C.Root)
		Out += "/" + NoteName(C.Bass, K, C.BassSpelling);
	return Out;
}

// Progressions

namespace {

// The words of a bar or a progression, between spaces and commas (a lone
// dash is a separator too); a word like "I-vi" that isn't a chord is split
// at its dashes
std::vector<std::string> SplitWords(const std::string &Text)
{
	std::vector<std::string> Words;
	std::string Word;
	for (size_t i = 0; i <= Text.size(); ++i) {
		const char c = i < Text.size() ? Text[i] : ' ';
		if (!std::isspace(static_cast<unsigned char>(c)) && c != ',') {
			Word += c;
			continue;
		}
		if (!Word.empty() && Word != "-") {
			Chord Unused;
			if (Word.find('-') != std::string::npos && Word.size() > 1 && !ParseChord(Word, Key(), Unused)) {
				std::string Piece;
				for (size_t k = 0; k <= Word.size(); ++k) {
					if (k == Word.size() || Word[k] == '-') {
						if (!Piece.empty())
							Words.push_back(Piece);
						Piece.clear();
					}
					else
						Piece += Word[k];
				}
			}
			else
				Words.push_back(Word);
		}
		Word.clear();
	}
	return Words;
}

}	// namespace

std::vector<std::string> SplitProgression(const std::string &Input)
{
	std::vector<std::string> Bars;
	const std::string Text = Trim(Input);
	if (Text.find('|') != std::string::npos) {
		std::vector<std::string> Parts;
		std::string Part;
		for (size_t i = 0; i <= Text.size(); ++i) {
			if (i == Text.size() || Text[i] == '|') {
				Parts.push_back(Trim(Part));
				Part.clear();
			}
			else
				Part += Text[i];
		}
		// "| C | Am |" has nothing before the first bar line and after the last
		size_t First = 0, Last = Parts.size();
		while (First < Last && Parts[First].empty())
			++First;
		while (Last > First && Parts[Last - 1].empty())
			--Last;
		for (size_t i = First; i < Last; ++i)
			Bars.push_back(Parts[i].empty() ? "." : Parts[i]);
		return Bars;
	}
	// One chord a bar
	return SplitWords(Text);
}

bool WrittenAsDegrees(const std::vector<std::string> &BarTexts)
{
	for (size_t b = 0; b < BarTexts.size(); ++b) {
		const std::vector<std::string> Words = SplitWords(BarTexts[b]);
		for (size_t w = 0; w < Words.size(); ++w)
			if (IsDegreeStart(Plain(Words[w])))
				return true;
	}
	return false;
}

namespace {

bool IsHold(const std::string &Word)
{
	return Word == "." || Word == ".." || Word == "..." || Word == "_";
}

bool IsNoChord(const std::string &Word)
{
	std::string Lower;
	for (size_t i = 0; i < Word.size(); ++i)
		if (Word[i] != '.')
			Lower += static_cast<char>(std::tolower(static_cast<unsigned char>(Word[i])));
	return Lower == "nc" || Lower == "rest";
}

struct Share {
	enum Kind { CHORD, HOLD, NONE } What;
	Chord C;
};

}	// namespace

bool LayOutProgression(const std::vector<std::string> &BarTexts, const std::vector<BarSpan> &Bars, int RowsPerBeat,
	const Key &K, std::vector<ChordAt> &Out, std::string &Error)
{
	Out.clear();
	const int Beat = std::max(1, RowsPerBeat);

	// Each written bar as its shares
	std::vector<std::vector<Share> > Written;
	for (size_t b = 0; b < BarTexts.size(); ++b) {
		const std::vector<std::string> Words = SplitWords(BarTexts[b]);
		if (Words.empty())
			continue;
		if (Words.size() == 1 && Words[0] == "%") {
			if (Written.empty()) {
				Error = "\"%\" repeats the bar before, but it's the first one.";
				return false;
			}
			Written.push_back(Written.back());
			continue;
		}
		std::vector<Share> Shares;
		for (size_t w = 0; w < Words.size(); ++w) {
			Share S;
			if (IsHold(Words[w]))
				S.What = Share::HOLD;
			else if (IsNoChord(Words[w]))
				S.What = Share::NONE;
			else if (Words[w] == "%") {
				Error = "\"%\" repeats a whole bar, so it goes alone between bar lines.";
				return false;
			}
			else if (ParseChord(Words[w], K, S.C))
				S.What = Share::CHORD;
			else {
				Error = "\"" + Words[w] + "\" isn't a chord. Write chords like C, Am7, F#m, Bbmaj7, G7, Dsus4, Cadd9 or C/E, "
					"or degrees of the key like I, vi, IV, V7, bVII or V7/V; \".\" holds the chord before, \"%\" repeats a bar.";
				return false;
			}
			Shares.push_back(S);
		}
		Written.push_back(Shares);
	}
	if (Written.empty()) {
		Error = "There are no chords in the progression.";
		return false;
	}

	for (size_t b = 0; b < Bars.size(); ++b) {
		const std::vector<Share> &Shares = Written[b % Written.size()];
		const int Count = static_cast<int>(Shares.size());
		const int Rows = Bars[b].Rows;
		const int Beats = Rows / Beat;
		// The shares' first rows: whole beats when there are enough, the first ones the longer
		std::vector<int> Starts;
		if (Beats >= Count) {
			const int Each = Beats / Count, Extra = Beats % Count;
			int At = 0;
			for (int i = 0; i < Count; ++i) {
				Starts.push_back(Bars[b].Row + At * Beat);
				At += Each + (i < Extra ? 1 : 0);
			}
		}
		else {
			const int Each = Rows / Count, Extra = Rows % Count;
			int At = 0;
			for (int i = 0; i < Count; ++i) {
				Starts.push_back(Bars[b].Row + At);
				At += Each + (i < Extra ? 1 : 0);
			}
		}
		for (int i = 0; i < Count; ++i) {
			const int From = Starts[static_cast<size_t>(i)];
			const int To = i + 1 < Count ? Starts[static_cast<size_t>(i) + 1] : Bars[b].Row + Rows;
			if (To <= From)
				continue;
			const Share &S = Shares[static_cast<size_t>(i)];
			if (S.What == Share::HOLD) {
				if (!Out.empty() && Out.back().Row + Out.back().Rows == From)
					Out.back().Rows = To - Out.back().Row;
				continue;
			}
			if (S.What == Share::NONE)
				continue;
			ChordAt A;
			A.Row = From;
			A.Rows = To - From;
			A.C = S.C;
			Out.push_back(A);
		}
	}
	if (Out.empty()) {
		Error = "There are no chords in the progression.";
		return false;
	}
	return true;
}

std::string ProgressionText(const std::vector<ChordAt> &Chords, const std::vector<BarSpan> &Bars, const Key &K, bool Degrees,
	size_t MaxBars)
{
	std::string Text;
	for (size_t b = 0; b < Bars.size(); ++b) {
		if (b == MaxBars) {
			Text += "| ...";
			return Text;
		}
		Text += "|";
		const int End = Bars[b].Row + Bars[b].Rows;
		bool Any = false;
		for (size_t c = 0; c < Chords.size(); ++c) {
			const ChordAt &A = Chords[c];
			const int From = std::max(A.Row, Bars[b].Row);
			if (From < std::min(A.Row + A.Rows, End) && (From == Bars[b].Row || From == A.Row)) {
				Text += " ";
				Text += Degrees ? ChordDegree(A.C, K) : ChordName(A.C, K);
				Any = true;
			}
		}
		Text += Any ? " " : " N.C. ";
	}
	Text += "|";
	return Text;
}

Chord DiatonicChord(const Key &K, int Degree, bool Seventh)
{
	const int *pSteps = ModeSteps(K.Mode);
	const int d = ((Degree % 7) + 7) % 7;
	std::vector<int> Tones;
	Tones.push_back(0);
	Tones.push_back(Mod12(pSteps[(d + 2) % 7] - pSteps[d]));
	Tones.push_back(Mod12(pSteps[(d + 4) % 7] - pSteps[d]));
	if (Seventh)
		Tones.push_back(Mod12(pSteps[(d + 6) % 7] - pSteps[d]));
	return Chord(K.Tonic + pSteps[d], Tones);
}

// Colors

bool ParseColor(const std::string &Input, chord_color &Out)
{
	std::string Text;
	for (size_t i = 0; i < Input.size(); ++i)
		if (std::isalnum(static_cast<unsigned char>(Input[i])))
			Text += static_cast<char>(std::tolower(static_cast<unsigned char>(Input[i])));
	if (Text.empty() || Text == "plain" || Text == "triads" || Text == "simple" || Text == "none") {
		Out = COLOR_PLAIN;
		return true;
	}
	if (Text == "sevenths" || Text == "seventh" || Text == "7ths" || Text == "7th" || Text == "7" || Text == "jazzy") {
		Out = COLOR_SEVENTHS;
		return true;
	}
	if (Text == "rich" || Text == "colorful" || Text == "colourful" || Text == "lush" || Text == "extended") {
		Out = COLOR_RICH;
		return true;
	}
	return false;
}

double MelodyRub(const std::vector<NoteInfo> &Melody, int Row, int End, int PitchClass, int RowsPerBeat)
{
	const double Beat = std::max(1, RowsPerBeat);
	double Rub = 0.0;
	for (size_t i = 0; i < Melody.size(); ++i) {
		const NoteInfo &N = Melody[i];
		const int From = std::max(N.Row, Row), To = std::min(N.Row + N.Rows, End);
		if (To <= From)
			continue;
		const int Distance = Mod12(N.Pitch - PitchClass);
		if (Distance != 1 && Distance != 11)
			continue;
		double Weight = (To - From) / Beat;
		if (N.Row <= Row && Row < N.Row + N.Rows)
			Weight *= 1.5;	// heard as the chord comes in
		if (N.Rows * 2 < RowsPerBeat)
			Weight *= 0.5;	// a quick passing note
		Rub += Weight;
	}
	return Rub;
}

const ChordAt *ChordAtRow(const std::vector<ChordAt> &Chords, int Row)
{
	for (size_t i = 0; i < Chords.size(); ++i)
		if (Chords[i].Row <= Row && Row < Chords[i].Row + Chords[i].Rows)
			return &Chords[i];
	return 0;
}

namespace {

const double RUB_LIMIT = 0.75;	// beats of a melody note a semitone from a color tone

// The first of Candidates (tones above the root) whose added tones don't rub
// against the melody, as a chord; Fallback if none fits
Chord FirstFitting(const Chord &Base, const std::vector<std::vector<int> > &Candidates, const std::vector<NoteInfo> &Melody,
	int Row, int End, const Key &K, bool KeyOnly, int RowsPerBeat)
{
	for (size_t c = 0; c < Candidates.size(); ++c) {
		Chord C(Base.Root, Candidates[c]);
		C.Bass = Base.Bass;
		C.RootSpelling = Base.RootSpelling;
		C.BassSpelling = Base.BassSpelling;
		bool Fits = true;
		for (size_t t = 0; t < C.Tones.size() && Fits; ++t) {
			const int Tone = C.Tones[t];
			if (Base.Has(Tone))
				continue;
			const int PitchClass = Mod12(C.Root + Tone);
			if ((KeyOnly && !InScale(PitchClass, K)) || MelodyRub(Melody, Row, End, PitchClass, RowsPerBeat) >= RUB_LIMIT)
				Fits = false;
		}
		if (Fits)
			return C;
	}
	return Base;
}

}	// namespace

void ColorChords(std::vector<ChordAt> &Chords, const std::vector<NoteInfo> &Melody, const Key &K, int RowsPerBeat,
	chord_color Color)
{
	if (Color == COLOR_PLAIN)
		return;
	const int Beat = std::max(1, RowsPerBeat);
	std::vector<ChordAt> Out;
	for (size_t i = 0; i < Chords.size(); ++i) {
		ChordAt A = Chords[i];
		const Chord Base = A.C;
		const unsigned B = ToneBits(Base);
		const bool Major = B == Bits({ 0, 4, 7 }), Minor = B == Bits({ 0, 3, 7 }), Diminished = B == Bits({ 0, 3, 6 });
		if (!Major && !Minor && !Diminished) {
			Out.push_back(A);
			continue;
		}
		const int End = A.Row + A.Rows;
		const int Degree = ScaleDegree(Base.Root, K);
		const int Relative = Mod12(Base.Root - K.Tonic);
		// The key's own seventh above the root, when the root is in the key
		const int KeySeventh = Degree >= 0 ? Mod12(ModeSteps(K.Mode)[(Degree + 6) % 7] - ModeSteps(K.Mode)[Degree]) : -1;
		std::vector<std::vector<int> > Candidates;
		bool KeyOnly = Degree >= 0;

		if (Color == COLOR_SEVENTHS) {
			if (Major)
				Candidates.push_back(TonesOf({ 0, 4, 7, KeySeventh == 11 ? 11 : 10 }));
			else if (Minor)
				Candidates.push_back(TonesOf({ 0, 3, 7, 10 }));
			else
				Candidates.push_back(TonesOf({ 0, 3, 6, KeySeventh == 9 ? 9 : 10 }));
			KeyOnly = false;	// a V7 in minor has its seventh in the key, its third isn't
		}
		else if (Major) {
			const bool Tonic = Relative == 0, Subdominant = Relative == 5, Dominant = Relative == 7;
			const bool Resolves = i + 1 < Chords.size() && Chords[i + 1].Row == End && Chords[i + 1].C.Root == K.Tonic;
			if (Dominant && Resolves && A.Rows >= 2 * Beat) {
				// A suspended fourth that resolves: V7sus4 for the first half, then V7
				const int Half = (A.Rows / Beat + 1) / 2 * Beat;
				const int SusPitch = Mod12(Base.Root + 5), Seventh = Mod12(Base.Root + 10);
				if (MelodyRub(Melody, A.Row, A.Row + Half, SusPitch, RowsPerBeat) < RUB_LIMIT &&
					MelodyRub(Melody, A.Row, End, Seventh, RowsPerBeat) < RUB_LIMIT) {
					ChordAt First = A;
					First.Rows = Half;
					First.C = Base;
					First.C.Tones = TonesOf({ 0, 5, 7, 10 });
					ChordAt Second = A;
					Second.Row = A.Row + Half;
					Second.Rows = A.Rows - Half;
					Second.C = Base;
					Second.C.Tones = TonesOf({ 0, 4, 7, 10 });
					Out.push_back(First);
					Out.push_back(Second);
					continue;
				}
			}
			if (Tonic) {
				Candidates.push_back(TonesOf({ 0, 2, 4, 7 }));
				Candidates.push_back(TonesOf({ 0, 4, 7, 11 }));
				Candidates.push_back(TonesOf({ 0, 4, 7, 9 }));
			}
			else if (Subdominant) {
				Candidates.push_back(TonesOf({ 0, 4, 7, 11 }));
				Candidates.push_back(TonesOf({ 0, 2, 4, 7 }));
			}
			else if (Dominant || Degree < 0) {
				Candidates.push_back(TonesOf({ 0, 4, 7, 10 }));
				KeyOnly = false;
			}
			else if (KeySeventh == 11) {
				Candidates.push_back(TonesOf({ 0, 4, 7, 11 }));
				Candidates.push_back(TonesOf({ 0, 2, 4, 7 }));
			}
			else {
				Candidates.push_back(TonesOf({ 0, 2, 4, 7 }));
				Candidates.push_back(TonesOf({ 0, 4, 7, 10 }));
			}
		}
		else if (Minor) {
			const bool MinorKey = IsMinorMode(K);
			if (Relative == 0 && MinorKey) {
				Candidates.push_back(TonesOf({ 0, 2, 3, 7 }));
				Candidates.push_back(TonesOf({ 0, 3, 7, 10 }));
			}
			else if ((!MinorKey && (Relative == 2 || Relative == 9)) || (MinorKey && Relative == 5)) {
				Candidates.push_back(TonesOf({ 0, 2, 3, 7, 10 }));
				Candidates.push_back(TonesOf({ 0, 3, 7, 10 }));
			}
			else
				Candidates.push_back(TonesOf({ 0, 3, 7, 10 }));
		}
		else
			Candidates.push_back(TonesOf({ 0, 3, 6, KeySeventh == 9 ? 9 : 10 }));

		A.C = FirstFitting(Base, Candidates, Melody, A.Row, End, K, KeyOnly, RowsPerBeat);
		Out.push_back(A);
	}
	Chords = Out;
}

// Hearing chords

std::vector<ChordAt> HearChords(const std::vector<HeardNote> &Notes, const std::vector<BarSpan> &Bars, int RowsPerBeat,
	const Key &K, bool HalfBars)
{
	// Triads, and chords that need every tone but the fifth heard
	struct Template { std::vector<int> Tones; double Penalty; bool NeedsAll; };
	static const Template TEMPLATES[] = {
		{ TonesOf({ 0, 4, 7 }), 0.0, false }, { TonesOf({ 0, 3, 7 }), 0.0, false }, { TonesOf({ 0, 3, 6 }), 0.08, false },
		{ TonesOf({ 0, 4, 8 }), 0.12, false }, { TonesOf({ 0, 4, 7, 10 }), 0.05, true }, { TonesOf({ 0, 4, 7, 11 }), 0.06, true },
		{ TonesOf({ 0, 3, 7, 10 }), 0.05, true }, { TonesOf({ 0, 3, 6, 10 }), 0.08, true }, { TonesOf({ 0, 5, 7 }), 0.08, true },
		{ TonesOf({ 0, 2, 4, 7 }), 0.08, true }, { TonesOf({ 0, 5, 7, 10 }), 0.1, true }, { TonesOf({ 0, 3, 6, 9 }), 0.12, true },
	};
	const int Beat = std::max(1, RowsPerBeat);
	// The notes sounding in a slot, found without going through all of them for each: short notes by their first row,
	// from where the longest of them could have started; long ones (a drone) every time. Taken in their own order, so
	// the sums come out exactly as going through them all
	const int LONG_NOTE = 64;
	std::vector<size_t> ByRow, Long;
	int LongestShort = 0;
	for (size_t n = 0; n < Notes.size(); ++n) {
		if (Notes[n].Rows > LONG_NOTE)
			Long.push_back(n);
		else {
			ByRow.push_back(n);
			LongestShort = std::max(LongestShort, Notes[n].Rows);
		}
	}
	auto EarlierRow = [&](size_t a, size_t b) { return Notes[a].Row < Notes[b].Row; };
	std::stable_sort(ByRow.begin(), ByRow.end(), EarlierRow);
	auto FirstFrom = [&](int Row) {
		return std::lower_bound(ByRow.begin(), ByRow.end(), Row, [&](size_t n, int Value) { return Notes[n].Row < Value; });
	};
	std::vector<size_t> Sounding;
	std::vector<double> Heard;
	std::vector<ChordAt> Out;
	for (size_t b = 0; b < Bars.size(); ++b) {
		const BarSpan &Bar = Bars[b];
		std::vector<std::pair<int, int> > Slots;
		const int Beats = Bar.Rows / Beat;
		if (HalfBars && Beats >= 2) {
			const int Middle = Bar.Row + (Beats / 2) * Beat;
			Slots.push_back(std::make_pair(Bar.Row, Middle));
			Slots.push_back(std::make_pair(Middle, Bar.Row + Bar.Rows));
		}
		else
			Slots.push_back(std::make_pair(Bar.Row, Bar.Row + Bar.Rows));
		for (size_t s = 0; s < Slots.size(); ++s) {
			const int From = Slots[s].first, To = Slots[s].second;
			double Weight[12] = { 0 }, BassWeight[12] = { 0 }, Total = 0.0;
			Sounding.clear();
			for (std::vector<size_t>::const_iterator it = FirstFrom(From - LongestShort), End = FirstFrom(To); it != End; ++it)
				Sounding.push_back(*it);
			Sounding.insert(Sounding.end(), Long.begin(), Long.end());
			std::sort(Sounding.begin(), Sounding.end());
			// The bass is one voice: its long, loud notes count for at most half as much as the parts over it,
			// so a bass on the fifth (Dm7 over A) doesn't turn the chord into one on the fifth
			Heard.assign(Sounding.size(), 0.0);
			double Upper = 0.0, Low = 0.0;
			for (size_t k = 0; k < Sounding.size(); ++k) {
				const HeardNote &N = Notes[Sounding[k]];
				const int A = std::max(N.Row, From), Z = std::min(N.Row + N.Rows, To);
				if (Z <= A)
					continue;
				double W = (Z - A) / static_cast<double>(Beat) * N.Weight;
				if (N.Row >= From && N.Row < To && (N.Row - Bar.Row) % Beat == 0)
					W *= 1.25;	// struck on a beat
				Heard[k] = W;
				(N.Bass ? Low : Upper) += W;
			}
			const double BassShare = Low > 0.0 && Upper > 0.0 ? std::min(1.0, 0.5 * Upper / Low) : 1.0;
			for (size_t k = 0; k < Sounding.size(); ++k) {
				const HeardNote &N = Notes[Sounding[k]];
				if (Heard[k] <= 0.0)
					continue;
				const double W = N.Bass ? Heard[k] * BassShare : Heard[k];
				Weight[Mod12(N.PitchClass)] += W;
				Total += W;
				if (N.Bass)
					BassWeight[Mod12(N.PitchClass)] += W;
			}
			if (Total < 0.5)
				continue;
			double Best = -1e9;
			Chord BestChord;
			for (int Root = 0; Root < 12; ++Root) {
				for (size_t t = 0; t < sizeof(TEMPLATES) / sizeof(TEMPLATES[0]); ++t) {
					const Template &T = TEMPLATES[t];
					Chord C(Root, T.Tones);
					double Score = 0.0;
					bool Missing = false;
					for (int pc = 0; pc < 12; ++pc)
						Score += C.HasPitchClass(pc) ? Weight[pc] : -0.7 * Weight[pc];
					for (size_t k = 0; k < C.Tones.size(); ++k)
						if (Weight[Mod12(Root + C.Tones[k])] < 0.05 * Total && C.Tones[k] != 7)
							Missing = true;
					if (Missing && T.NeedsAll)
						continue;
					if (Weight[Root] < 0.05 * Total)
						Score -= 0.3 * Total;	// a root nothing plays
					Score += 0.6 * BassWeight[Root];
					for (size_t k = 0; k < C.Tones.size(); ++k)
						if (!InScale(Mod12(Root + C.Tones[k]), K))
							Score -= 0.05 * Total;
					Score -= T.Penalty * Total;
					if (Score > Best) {
						Best = Score;
						BestChord = C;
					}
				}
			}
			double Explained = 0.0;
			for (int pc = 0; pc < 12; ++pc)
				if (BestChord.HasPitchClass(pc))
					Explained += Weight[pc];
			if (Explained < 0.6 * Total)
				continue;
			// The bass's note, when it isn't the root
			int Lowest = -1;
			double LowestWeight = 0.0;
			for (int pc = 0; pc < 12; ++pc)
				if (BassWeight[pc] > LowestWeight) {
					LowestWeight = BassWeight[pc];
					Lowest = pc;
				}
			if (Lowest >= 0 && Lowest != BestChord.Root && BestChord.HasPitchClass(Lowest) && BassWeight[BestChord.Root] < 0.5 * LowestWeight)
				BestChord.Bass = Lowest;
			if (!Out.empty() && Out.back().C == BestChord && Out.back().Row + Out.back().Rows == From) {
				Out.back().Rows = To - Out.back().Row;
				continue;
			}
			ChordAt A;
			A.Row = From;
			A.Rows = To - From;
			A.C = BestChord;
			Out.push_back(A);
		}
	}
	return Out;
}

}	// namespace ftapi

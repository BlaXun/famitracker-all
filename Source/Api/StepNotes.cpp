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

#include "StepNotes.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace ftapi {

namespace {

std::string LowerWord(const std::string &Word)
{
	std::string Lower;
	for (size_t i = 0; i < Word.size(); ++i)
		Lower += static_cast<char>(std::tolower(static_cast<unsigned char>(Word[i])));
	return Lower;
}

// Two words that make one drum's name ("open hihat", "low tom", "kick drum"), joined as one step. Only those pairs: hits
// in a row stay steps of their own ("hihat hihat", "kick hihat"), which joining every word before a second word of a
// name ("hihat") ran together
bool DrumPair(const std::string &First, const std::string &Second)
{
	const std::string A = LowerWord(First), B = LowerWord(Second);
	if (B == "drum")
		return A == "bass" || A == "kick" || A == "snare";
	if (B == "hihat" || B == "hi-hat" || B == "hat")
		return A == "open" || A == "closed";
	if (B == "cymbal")
		return A == "crash";
	if (B == "tom")
		return A == "high" || A == "hi" || A == "mid" || A == "middle" || A == "low" || A == "floor";
	return false;
}

bool AllOf(const std::string &Word, char c)
{
	if (Word.empty())
		return false;
	for (size_t i = 0; i < Word.size(); ++i)
		if (Word[i] != c)
			return false;
	return true;
}

bool AllDigits(const std::string &Word)
{
	if (Word.empty())
		return false;
	for (size_t i = 0; i < Word.size(); ++i)
		if (!std::isdigit(static_cast<unsigned char>(Word[i])))
			return false;
	return true;
}

// A noise pitch after the word "noise": "10", "F", "F-#", "F#" (as ParseNoisePitch reads them)
bool NoisePitchWord(const std::string &Word)
{
	if (AllDigits(Word))
		return true;
	if (Word.empty() || !std::isxdigit(static_cast<unsigned char>(Word[0])))
		return false;
	return Word.size() == 1 || (Word.size() == 2 && Word[1] == '#') || (Word.size() == 3 && Word[1] == '-' && Word[2] == '#');
}

std::string Number(int Value)
{
	char Buffer[16];
	std::snprintf(Buffer, sizeof(Buffer), "%d", Value);
	return Buffer;
}

// What the text holds, in order: words, bar lines (|) and line breaks
struct Piece {
	enum { WORD, BAR, BREAK } Kind;
	std::string Word;
};

std::vector<Piece> Pieces(const std::string &Text)
{
	std::vector<Piece> Out;
	std::string Word;
	auto Flush = [&]() {
		if (!Word.empty()) {
			Piece P;
			P.Kind = Piece::WORD;
			P.Word = Word;
			Out.push_back(P);
			Word.clear();
		}
	};
	for (size_t i = 0; i < Text.size(); ++i) {
		const char c = Text[i];
		if (c == ' ' || c == '\t' || c == '\r' || c == ',')
			Flush();
		else if (c == '|' || c == '\n') {
			Flush();
			if (c == '|' && i > 0 && Text[i - 1] == '|')
				continue;	// a double bar line ("||", between sections) is one
			Piece P;
			P.Kind = c == '|' ? Piece::BAR : Piece::BREAK;
			Out.push_back(P);
		}
		else
			Word += c;
	}
	Flush();
	// Drum names of two words are one step: "open hihat", "low tom", "noise 10", "noise F-#"
	std::vector<Piece> Joined;
	for (size_t i = 0; i < Out.size(); ++i) {
		if (Out[i].Kind == Piece::WORD && i + 1 < Out.size() && Out[i + 1].Kind == Piece::WORD && !AllOf(Out[i].Word, '-') &&
			!AllOf(Out[i].Word, '.') && Out[i].Word.back() != ':' &&
			(DrumPair(Out[i].Word, Out[i + 1].Word) || ((Out[i].Word == "noise" || Out[i].Word == "Noise") && NoisePitchWord(Out[i + 1].Word)))) {
			Piece P = Out[i];
			P.Word += " " + Out[i + 1].Word;
			Joined.push_back(P);
			++i;
		}
		else
			Joined.push_back(Out[i]);
	}
	return Joined;
}

}	// namespace

int StepLine::Steps() const
{
	return Bars.empty() ? 0 : Bars.back().First + Bars.back().Steps;
}

int StepLineBarOf(const StepLine &Line, int Step, int *pInBar)
{
	for (size_t b = 0; b < Line.Bars.size(); ++b)
		if (Step >= Line.Bars[b].First && Step < Line.Bars[b].First + Line.Bars[b].Steps) {
			if (pInBar != 0)
				*pInBar = Step - Line.Bars[b].First;
			return static_cast<int>(b);
		}
	return -1;
}

bool ParseStepLine(const std::string &Text, int FirstBar, StepLine &Line, std::string &Error)
{
	Line = StepLine();
	Line.FirstBar = FirstBar;
	Error.clear();
	// The line's bar b as the user knows it: by its number in the song once that's known
	auto BarName = [&](size_t b) {
		return Line.FirstBar > 0 ? "bar " + Number(Line.FirstBar + static_cast<int>(b)) : "the line's bar " + Number(static_cast<int>(b) + 1);
	};
	auto Where = [&](size_t b, int InBar) {
		return BarName(b) + ", step " + Number(InBar + 1);
	};

	const std::vector<Piece> All = Pieces(Text);
	StepLineBar Bar;
	int Step = 0;					// the line's next step
	enum { NOTHING, NOTE, REST } Open = NOTHING;	// what the last step played
	bool Started = false;			// a pitch or a rest came (a - before them holds the note before the line)
	bool AfterBreak = true;			// at the text's start or a line's: a | there ends no bar
	auto EndBar = [&]() {
		Line.Bars.push_back(Bar);
		Bar = StepLineBar();
		Bar.First = Step;
	};
	for (size_t i = 0; i < All.size(); ++i) {
		const Piece &P = All[i];
		const size_t b = Line.Bars.size();
		if (P.Kind == Piece::BREAK) {
			if (Bar.Steps > 0)
				EndBar();
			AfterBreak = true;
			continue;
		}
		if (P.Kind == Piece::BAR) {
			if (Bar.Steps > 0)
				EndBar();
			else if (Bar.Number > 0) {
				Error = BarName(b) + " (\"" + Number(Bar.Number) + ":\") has no steps. A bar of rests is . . . . (a . a step).";
				return false;
			}
			else if (!AfterBreak) {
				// Bar lines at the very end close the line; between steps they'd leave a bar out
				bool Last = true;
				for (size_t j = i + 1; j < All.size() && Last; ++j)
					Last = All[j].Kind != Piece::WORD;
				if (!Last) {
					Error = BarName(b) + " is empty: two | with no steps between them. A bar of rests is . . . . (a . a step).";
					return false;
				}
			}
			AfterBreak = false;
			continue;
		}
		AfterBreak = false;
		std::string Word = P.Word;
		// The bar's number at its start, "5:", maybe joined to its first step ("5:D4")
		const size_t Colon = Word.find(':');
		if (Colon != std::string::npos && Colon > 0 && AllDigits(Word.substr(0, Colon))) {
			const int Written = std::atoi(Word.substr(0, Colon).c_str());
			if (Bar.Steps > 0) {
				Error = "\"" + Word.substr(0, Colon + 1) + "\" in the middle of " + BarName(b) + ": a bar's number goes at its start "
					"(put | or a new line before it).";
				return false;
			}
			if (Written < 1) {
				Error = "Bars count from 1, so \"" + Word.substr(0, Colon + 1) + "\" isn't one.";
				return false;
			}
			if (Line.FirstBar == 0)
				Line.FirstBar = Written - static_cast<int>(b);
			if (Line.FirstBar < 1 || Written != Line.FirstBar + static_cast<int>(b)) {
				Error = "The line says bar " + Number(Written) + " where its bar " + Number(static_cast<int>(b) + 1) + " is " +
					(Line.FirstBar >= 1 ? "bar " + Number(Line.FirstBar + static_cast<int>(b)) : std::string("before bar 1")) +
					": a bar is missing or one too many before it (or from_bar is another).";
				return false;
			}
			Bar.Number = Written;
			Word = Word.substr(Colon + 1);
			if (Word.empty())
				continue;
		}
		if (AllOf(Word, '-') || AllOf(Word, '.')) {
			// Holds and rests ("--" is two)
			for (size_t k = 0; k < Word.size(); ++k) {
				if (Word[k] == '.') {
					Open = REST;
					Started = true;
				}
				else if (Open == NOTE)
					++Line.Notes.back().Steps;
				else if (!Started)
					++Line.Held;
				++Bar.Steps;
				++Step;
			}
			continue;
		}
		// A pitch, maybe with holds joined to it ("B4--") and its volume ("B4v12")
		int Holds = 0;
		while (Word.size() > 1 && Word.back() == '-') {
			Word.erase(Word.size() - 1);
			++Holds;
		}
		StepLineNote Note;
		Note.Step = Step;
		const size_t V = Word.find_last_of("vV");
		if (V != std::string::npos && V > 0 && V + 1 < Word.size() && Word.size() - V <= 3 && AllDigits(Word.substr(V + 1))) {
			Note.Volume = std::atoi(Word.substr(V + 1).c_str());
			if (Note.Volume > 15) {
				Error = Where(b, Bar.Steps) + ": \"" + P.Word + "\" has volume " + Number(Note.Volume) + "; a volume is 0-15 (B4v12).";
				return false;
			}
			Word.erase(V);
		}
		Note.Pitch = Word;
		Note.Steps = 1 + Holds;
		Line.Notes.push_back(Note);
		Open = NOTE;
		Started = true;
		Bar.Steps += 1 + Holds;
		Step += 1 + Holds;
	}
	if (Bar.Steps > 0)
		EndBar();
	else if (Bar.Number > 0) {
		Error = BarName(Line.Bars.size()) + " (\"" + Number(Bar.Number) + ":\") has no steps. A bar of rests is . . . . (a . a step).";
		return false;
	}
	if (Line.Bars.empty()) {
		Error = "The line has no steps: write a pitch (D4), - or . for each step, like \"D4 G4 A4 B4 - A4 G4 - | B4 - - - . . D5 -\".";
		return false;
	}
	return true;
}

std::string FormatStepLine(const StepLine &Line)
{
	std::vector<std::string> Tokens(static_cast<size_t>(Line.Steps()), ".");
	for (int s = 0; s < Line.Held && s < Line.Steps(); ++s)
		Tokens[static_cast<size_t>(s)] = "-";
	for (size_t n = 0; n < Line.Notes.size(); ++n) {
		const StepLineNote &Note = Line.Notes[n];
		for (int s = 0; s < Note.Steps; ++s) {
			const int At = Note.Step + s;
			if (At < 0 || At >= Line.Steps())
				continue;
			Tokens[static_cast<size_t>(At)] = s > 0 ? "-" : Note.Volume >= 0 ? Note.Pitch + "v" + Number(Note.Volume) : Note.Pitch;
		}
	}
	std::string Out;
	for (size_t b = 0; b < Line.Bars.size(); ++b) {
		if (b > 0)
			Out += "\n";
		if (Line.Bars[b].Number > 0)
			Out += Number(Line.Bars[b].Number) + ":";
		for (int s = 0; s < Line.Bars[b].Steps; ++s) {
			if (s > 0 || Line.Bars[b].Number > 0)
				Out += " ";
			Out += Tokens[static_cast<size_t>(Line.Bars[b].First + s)];
		}
	}
	return Out;
}

}	// namespace ftapi

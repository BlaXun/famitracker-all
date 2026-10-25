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

// Tutorials as text (see TutorialScript.h)

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "TutorialScript.h"

namespace {

const char *const PARTS[] = {
	"window", "pattern", "frames", "instruments", "song-settings", "song-info", "octave", "toolbar", "status-bar", "clips",
	"piano-roll", "drum-grid", "button", "menu",
};

const char *const COMMANDS[] = {
	"play", "play-from-start", "stop", "edit-mode", "new", "open", "save", "save-as", "export-for-games", "create-wav",
	"new-instrument", "instrument-library", "edit-instrument", "drum-grid", "save-clip", "clips", "piano-roll",
	"clips-panel", "record", "insert-recording", "add-backing", "voice-effect", "insert-frame", "duplicate-frame",
	"remove-frame", "module-properties", "undo", "redo", "profile", "connect-claude", "pattern-editor",
};

std::string Trim(const std::string &Text)
{
	size_t First = 0, Last = Text.size();
	while (First < Last && std::isspace(static_cast<unsigned char>(Text[First])))
		++First;
	while (Last > First && std::isspace(static_cast<unsigned char>(Text[Last - 1])))
		--Last;
	return Text.substr(First, Last - First);
}

std::string Lower(const std::string &Text)
{
	std::string Out = Text;
	for (size_t i = 0; i < Out.size(); ++i)
		Out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(Out[i])));
	return Out;
}

// "button play" as "button" and "play"
void SplitWord(const std::string &Text, std::string &First, std::string &Rest)
{
	const size_t Space = Text.find_first_of(" \t");
	First = Lower(Text.substr(0, Space));
	Rest = Space == std::string::npos ? std::string() : Trim(Text.substr(Space + 1));
}

std::string AtLine(int Line)
{
	char Buffer[32];
	std::snprintf(Buffer, sizeof(Buffer), "Line %d: ", Line);
	return Buffer;
}

bool IsCommand(const std::string &Name)
{
	for (size_t i = 0; i < sizeof(COMMANDS) / sizeof(COMMANDS[0]); ++i)
		if (Name == COMMANDS[i])
			return true;
	return false;
}

bool ParseWait(const std::string &Value, tutorial_step_t &Step, std::string &Problem)
{
	std::string Word, Rest;
	SplitWord(Value, Word, Rest);
	Step.Count = 1;
	if (Word == "next")
		Step.Wait = WAIT_NEXT;
	else if (Word == "edit-mode")
		Step.Wait = WAIT_EDIT_MODE;
	else if (Word == "edit-mode-off")
		Step.Wait = WAIT_EDIT_MODE_OFF;
	else if (Word == "play")
		Step.Wait = WAIT_PLAY;
	else if (Word == "stop")
		Step.Wait = WAIT_STOP;
	else if (Word == "instrument")
		Step.Wait = WAIT_INSTRUMENT;
	else if (Word == "clip")
		Step.Wait = WAIT_CLIP;
	else if (Word == "saved")
		Step.Wait = WAIT_SAVED;
	else if (Word == "notes") {
		Step.Wait = WAIT_NOTES;
		if (!Rest.empty()) {
			char *pEnd = 0;
			const long Count = std::strtol(Rest.c_str(), &pEnd, 10);
			if (pEnd == Rest.c_str() || *pEnd != 0 || Count < 1 || Count > 256) {
				Problem = "\"notes\" takes a number of notes, 1 to 256.";
				return false;
			}
			Step.Count = static_cast<int>(Count);
		}
		return true;
	}
	else if (Word == "command") {
		Step.Wait = WAIT_COMMAND;
		Step.Command = Lower(Rest);
		if (!IsCommand(Step.Command)) {
			Problem = "\"" + Rest + "\" isn't a command a tutorial can wait for.";
			return false;
		}
		return true;
	}
	else {
		Problem = "a step can't wait for \"" + Value + "\".";
		return false;
	}
	if (!Rest.empty()) {
		Problem = "\"" + Word + "\" takes nothing after it.";
		return false;
	}
	return true;
}

bool ParseShow(const std::string &Value, tutorial_step_t &Step, std::string &Problem)
{
	SplitWord(Value, Step.Show, Step.ShowName);
	if (!IsTutorialPart(Step.Show)) {
		Problem = "a step can't show \"" + Value + "\".";
		return false;
	}
	if (Step.Show == "button") {
		Step.ShowName = Lower(Step.ShowName);
		if (!IsCommand(Step.ShowName)) {
			Problem = "there's no toolbar button called \"" + Step.ShowName + "\".";
			return false;
		}
	}
	else if (Step.Show == "menu") {
		if (Step.ShowName.empty()) {
			Problem = "\"menu\" takes the menu's name, as in \"menu Edit\".";
			return false;
		}
	}
	else if (!Step.ShowName.empty()) {
		Problem = "\"" + Step.Show + "\" takes nothing after it.";
		return false;
	}
	return true;
}

}	// namespace

bool IsTutorialPart(const std::string &Name)
{
	for (size_t i = 0; i < sizeof(PARTS) / sizeof(PARTS[0]); ++i)
		if (Name == PARTS[i])
			return true;
	return false;
}

const std::vector<std::string> &TutorialCommandNames()
{
	static const std::vector<std::string> Names(COMMANDS, COMMANDS + sizeof(COMMANDS) / sizeof(COMMANDS[0]));
	return Names;
}

bool ParseTutorial(const std::string &Text, tutorial_t &Tutorial, std::string &Error)
{
	Tutorial = tutorial_t();
	Error.clear();
	std::string *pCarry = 0;		// the text that lines without a name carry on
	bool Blank = false;				// an empty line since the last one
	int Line = 0;
	size_t Start = 0;
	while (Start <= Text.size()) {
		size_t End = Text.find('\n', Start);
		if (End == std::string::npos)
			End = Text.size();
		const std::string Raw = Text.substr(Start, End - Start);
		Start = End + 1;
		++Line;
		const std::string Content = Trim(Raw);
		if (Content.empty()) {
			Blank = true;
			continue;
		}
		if (Content[0] == ';' || Content[0] == '#')
			continue;
		const std::string Low = Lower(Content);
		if (Low == "step" || Low == "[step]") {
			Tutorial.Steps.push_back(tutorial_step_t());
			pCarry = 0;
			Blank = false;
			continue;
		}

		const size_t Colon = Content.find(':');
		const std::string Name = Colon == std::string::npos ? std::string() : Lower(Trim(Content.substr(0, Colon)));
		const std::string Value = Colon == std::string::npos ? std::string() : Trim(Content.substr(Colon + 1));
		tutorial_step_t *pStep = Tutorial.Steps.empty() ? 0 : &Tutorial.Steps.back();
		std::string Problem;
		if (Name == "title" || Name == "about") {
			if (pStep != 0)
				Problem = "the " + Name + " goes before the first step.";
			else {
				std::string &Field = Name == "title" ? Tutorial.Title : Tutorial.About;
				Field = Value;
				pCarry = &Field;
			}
		}
		else if (Name == "show" || Name == "text" || Name == "wait") {
			if (pStep == 0)
				Problem = "\"" + Name + "\" belongs to a step: put \"step\" on a line before it.";
			else if (Name == "show")
				ParseShow(Value, *pStep, Problem);
			else if (Name == "wait")
				ParseWait(Value, *pStep, Problem);
			else {
				pStep->Text = Value;
				pCarry = &pStep->Text;
			}
			if (Name != "text")
				pCarry = 0;
		}
		else if (pCarry != 0) {
			// More of the text before
			if (!pCarry->empty())
				*pCarry += Blank ? "\n" : " ";
			*pCarry += Content;
		}
		else
			Problem = "\"" + Content + "\" is neither a name with a value (\"text: ...\") nor more of a text.";
		Blank = false;
		if (!Problem.empty()) {
			Error = AtLine(Line) + Problem;
			return false;
		}
	}

	if (Tutorial.Title.empty()) {
		Error = "The tutorial has no title (\"title: ...\").";
		return false;
	}
	if (Tutorial.Steps.empty()) {
		Error = "The tutorial has no steps.";
		return false;
	}
	for (size_t i = 0; i < Tutorial.Steps.size(); ++i) {
		if (Tutorial.Steps[i].Text.empty()) {
			char Buffer[64];
			std::snprintf(Buffer, sizeof(Buffer), "Step %d has no text.", static_cast<int>(i + 1));
			Error = Buffer;
			return false;
		}
		if (Tutorial.Steps[i].Show.empty())
			Tutorial.Steps[i].Show = "window";
	}
	return true;
}

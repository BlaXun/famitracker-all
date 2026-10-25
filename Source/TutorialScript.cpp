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
	"piano-roll", "drum-grid", "mixer", "button", "menu", "channel",
};

const char *const COMMANDS[] = {
	"play", "play-from-start", "stop", "edit-mode", "new", "open", "save", "save-as", "export-for-games", "create-wav",
	"new-instrument", "instrument-library", "edit-instrument", "drum-grid", "save-clip", "clips", "piano-roll",
	"clips-panel", "record", "insert-recording", "add-backing", "voice-effect", "insert-frame", "duplicate-frame",
	"remove-frame", "module-properties", "undo", "redo", "profile", "connect-claude", "pattern-editor",
};

// The channels a song can have, as FamiTracker names them (lower case), and
// the chips that go in front of a name two chips share; "vrc6 plus" before
// "vrc6", which it starts with
const char *const CHANNEL_NAMES[] = {
	"pulse 1", "pulse 2", "triangle", "noise", "dpcm", "sawtooth", "fm channel 1", "fm channel 2", "fm channel 3",
	"fm channel 4", "fm channel 5", "fm channel 6", "fds", "namco 1", "namco 2", "namco 3", "namco 4", "namco 5",
	"namco 6", "namco 7", "namco 8", "soft 1", "soft 2", "soft 3", "soft 4", "square 1", "square 2", "square 3",
};

const char *const CHIP_NAMES[] = { "vrc6 plus", "2a03", "vrc6", "vrc7", "fds", "mmc5", "n163", "5b" };

const int MAX_BAR = 9999;

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

// A whole number from First to Last, nothing else
bool ReadNumber(const std::string &Text, int First, int Last, int &Number, int Base = 10)
{
	const std::string Digits = Trim(Text);
	if (Digits.empty())
		return false;
	char *pEnd = 0;
	const long Value = std::strtol(Digits.c_str(), &pEnd, Base);
	if (pEnd == Digits.c_str() || *pEnd != 0 || Value < First || Value > Last || !std::isxdigit(static_cast<unsigned char>(Digits[0])))
		return false;
	Number = static_cast<int>(Value);
	return true;
}

// "vrc6 pulse 1" as "vrc6" and "pulse 1"; no chip for "triangle"
void SplitChannel(const std::string &Name, std::string &Chip, std::string &Channel)
{
	const std::string Low = Lower(Trim(Name));
	Chip.clear();
	Channel = Low;
	for (size_t i = 0; i < sizeof(CHIP_NAMES) / sizeof(CHIP_NAMES[0]); ++i) {
		const std::string Prefix = std::string(CHIP_NAMES[i]) + " ";
		if (Low.compare(0, Prefix.size(), Prefix) == 0) {
			Chip = CHIP_NAMES[i];
			Channel = Trim(Low.substr(Prefix.size()));
			return;
		}
	}
}

// "Pulse 1, Triangle" as a list of channels
bool ParseChannels(const std::string &Name, const std::string &Value, std::vector<std::string> &Channels, std::string &Problem)
{
	Channels.clear();
	size_t Start = 0;
	while (Start <= Value.size()) {
		size_t End = Value.find(',', Start);
		if (End == std::string::npos)
			End = Value.size();
		const std::string Channel = Trim(Value.substr(Start, End - Start));
		Start = End + 1;
		if (Channel.empty()) {
			Problem = "\"" + Name + "\" takes channels apart by commas, as in \"" + Name + ": Pulse 1, Triangle\".";
			return false;
		}
		if (!IsTutorialChannel(Channel)) {
			Problem = "there's no channel called \"" + Channel + "\".";
			return false;
		}
		Channels.push_back(Channel);
	}
	return true;
}

// "bar 5" or "bars 5-8"
bool ParsePlay(const std::string &Value, tutorial_step_t &Step, std::string &Problem)
{
	std::string Word, Rest;
	SplitWord(Value, Word, Rest);
	bool Read = false;
	if (Word == "bar") {
		Read = ReadNumber(Rest, 1, MAX_BAR, Step.PlayFirst);
		Step.PlayLast = Step.PlayFirst;
	}
	else if (Word == "bars") {
		const size_t Dash = Rest.find('-');
		Read = Dash != std::string::npos && ReadNumber(Rest.substr(0, Dash), 1, MAX_BAR, Step.PlayFirst) &&
			ReadNumber(Rest.substr(Dash + 1), 1, MAX_BAR, Step.PlayLast) && Step.PlayLast >= Step.PlayFirst;
	}
	if (!Read) {
		Step.PlayFirst = Step.PlayLast = 0;
		Problem = "\"play\" takes the bars to play, as in \"play: bars 5-8\" or \"play: bar 3\".";
	}
	return Read;
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
	else if (Word == "heard")
		Step.Wait = WAIT_HEARD;
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
	else if (Step.Show == "channel") {
		if (Step.ShowName.empty()) {
			Problem = "\"channel\" takes the channel's name, as in \"channel Triangle\".";
			return false;
		}
		if (!IsTutorialChannel(Step.ShowName)) {
			Problem = "there's no channel called \"" + Step.ShowName + "\".";
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

bool IsTutorialChannel(const std::string &Name)
{
	std::string Chip, Channel;
	SplitChannel(Name, Chip, Channel);
	for (size_t i = 0; i < sizeof(CHANNEL_NAMES) / sizeof(CHANNEL_NAMES[0]); ++i)
		if (Channel == CHANNEL_NAMES[i])
			return true;
	return false;
}

int FindTutorialChannel(const std::string &Name, const std::vector<tutorial_channel_t> &Channels)
{
	std::string Chip, Channel;
	SplitChannel(Name, Chip, Channel);
	for (size_t i = 0; i < Channels.size(); ++i) {
		const tutorial_channel_t &c = Channels[i];
		if (Lower(c.Name) != Channel)
			continue;
		if (Chip.empty() || Chip == Lower(c.Chip) || (Chip == "vrc6 plus" && c.Vrc6Plus))
			return static_cast<int>(i);
	}
	return -1;
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
		else if (Name == "show" || Name == "text" || Name == "wait" || Name == "bar" || Name == "play" || Name == "solo" ||
			Name == "mute" || Name == "instrument") {
			if (pStep == 0)
				Problem = "\"" + Name + "\" belongs to a step: put \"step\" on a line before it.";
			else if (Name == "show")
				ParseShow(Value, *pStep, Problem);
			else if (Name == "wait")
				ParseWait(Value, *pStep, Problem);
			else if (Name == "bar") {
				if (!ReadNumber(Value, 1, MAX_BAR, pStep->Bar))
					Problem = "\"bar\" takes the bar's number, as in \"bar: 5\".";
			}
			else if (Name == "play")
				ParsePlay(Value, *pStep, Problem);
			else if (Name == "solo")
				ParseChannels(Name, Value, pStep->Solo, Problem);
			else if (Name == "mute")
				ParseChannels(Name, Value, pStep->Mute, Problem);
			else if (Name == "instrument") {
				if (!ReadNumber(Value, 0, 0x3F, pStep->Instrument, 16)) {
					pStep->Instrument = -1;
					Problem = "\"instrument\" takes the instrument's number as the list shows it, 00 to 3F.";
				}
			}
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
		tutorial_step_t &Step = Tutorial.Steps[i];
		char Buffer[160];
		Buffer[0] = 0;
		if (Step.Text.empty())
			std::snprintf(Buffer, sizeof(Buffer), "Step %d has no text.", static_cast<int>(i + 1));
		else if (Step.Wait == WAIT_HEARD && Step.PlayFirst == 0)
			std::snprintf(Buffer, sizeof(Buffer), "Step %d waits for its bars to be heard, but has no \"play\" line.", static_cast<int>(i + 1));
		else if (!Step.Solo.empty() && !Step.Mute.empty())
			std::snprintf(Buffer, sizeof(Buffer), "Step %d both solos and mutes channels; one of the two says it.", static_cast<int>(i + 1));
		if (Buffer[0] != 0) {
			Error = Buffer;
			return false;
		}
		if (Step.Show.empty())
			Step.Show = "window";
	}
	return true;
}

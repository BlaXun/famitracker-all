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

// Tests for reading tutorials (TutorialScript.cpp), and the built-in ones
// in res/tutorials (TUTORIALS_DIR) all read

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "TutorialScript.h"

namespace {

int g_checks = 0, g_failures = 0;

#define CHECK(x) do { ++g_checks; if (!(x)) { ++g_failures; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

void TestReading()
{
	const std::string Text =
		"; a comment\r\n"
		"title: Your first tune\r\n"
		"about: Type a melody\r\n"
		"  and hear it.\r\n"
		"\r\n"
		"step\r\n"
		"show: pattern\r\n"
		"text: This is the pattern editor.\r\n"
		"  Time runs down the page.\r\n"
		"\r\n"
		"  A second paragraph: with a colon in it.\r\n"
		"wait: edit-mode\r\n"
		"\r\n"
		"[step]\r\n"
		"show: button play\r\n"
		"text: Press play.\r\n"
		"wait: notes 4\r\n"
		"step\r\n"
		"text: Save it.\r\n"
		"wait: command Save\r\n"
		"step\r\n"
		"show: menu Edit\r\n"
		"text: The end.\r\n";
	tutorial_t T;
	std::string Error;
	CHECK(ParseTutorial(Text, T, Error));
	CHECK(Error.empty());
	CHECK(T.Title == "Your first tune");
	CHECK(T.About == "Type a melody and hear it.");
	CHECK(T.Steps.size() == 4);
	if (T.Steps.size() != 4)
		return;
	CHECK(T.Steps[0].Show == "pattern" && T.Steps[0].ShowName.empty());
	CHECK(T.Steps[0].Text == "This is the pattern editor. Time runs down the page.\nA second paragraph: with a colon in it.");
	CHECK(T.Steps[0].Wait == WAIT_EDIT_MODE);
	CHECK(T.Steps[1].Show == "button" && T.Steps[1].ShowName == "play");
	CHECK(T.Steps[1].Wait == WAIT_NOTES && T.Steps[1].Count == 4);
	CHECK(T.Steps[2].Show == "window");		// nothing to show: the whole window
	CHECK(T.Steps[2].Wait == WAIT_COMMAND && T.Steps[2].Command == "save");
	CHECK(T.Steps[3].Show == "menu" && T.Steps[3].ShowName == "Edit" && T.Steps[3].Wait == WAIT_NEXT);
}

void TestWaits()
{
	const char *const Waits[] = { "next", "edit-mode", "edit-mode-off", "play", "stop", "instrument", "clip", "saved", "notes" };
	const tutorial_wait_t Expected[] = { WAIT_NEXT, WAIT_EDIT_MODE, WAIT_EDIT_MODE_OFF, WAIT_PLAY, WAIT_STOP, WAIT_INSTRUMENT,
		WAIT_CLIP, WAIT_SAVED, WAIT_NOTES };
	for (size_t i = 0; i < sizeof(Waits) / sizeof(Waits[0]); ++i) {
		tutorial_t T;
		std::string Error;
		CHECK(ParseTutorial(std::string("title: T\nstep\ntext: x\nwait: ") + Waits[i] + "\n", T, Error));
		CHECK(T.Steps.size() == 1 && T.Steps[0].Wait == Expected[i] && T.Steps[0].Count == 1);
	}
	for (size_t i = 0; i < TutorialCommandNames().size(); ++i) {
		tutorial_t T;
		std::string Error;
		const std::string &Name = TutorialCommandNames()[i];
		CHECK(ParseTutorial("title: T\nstep\nshow: button " + Name + "\ntext: x\nwait: command " + Name + "\n", T, Error));
	}
	CHECK(IsTutorialPart("piano-roll") && !IsTutorialPart("kitchen"));
}

void TestErrors()
{
	struct { const char *Text; const char *Error; } Cases[] = {
		{ "step\ntext: x\n", "The tutorial has no title" },
		{ "title: T\n", "The tutorial has no steps." },
		{ "title: T\nstep\nshow: pattern\n", "Step 1 has no text." },
		{ "title: T\nstep\nshow: kitchen\ntext: x\n", "Line 3: a step can't show \"kitchen\"." },
		{ "title: T\nstep\nshow: button fly\ntext: x\n", "Line 3: there's no toolbar button called \"fly\"." },
		{ "title: T\nstep\nshow: menu\ntext: x\n", "Line 3: \"menu\" takes the menu's name" },
		{ "title: T\nstep\nshow: pattern now\ntext: x\n", "Line 3: \"pattern\" takes nothing after it." },
		{ "title: T\nstep\ntext: x\nwait: lunch\n", "Line 4: a step can't wait for \"lunch\"." },
		{ "title: T\nstep\ntext: x\nwait: notes many\n", "Line 4: \"notes\" takes a number of notes" },
		{ "title: T\nstep\ntext: x\nwait: notes 0\n", "Line 4: \"notes\" takes a number of notes" },
		{ "title: T\nstep\ntext: x\nwait: play now\n", "Line 4: \"play\" takes nothing after it." },
		{ "title: T\nstep\ntext: x\nwait: command fly\n", "Line 4: \"fly\" isn't a command" },
		{ "title: T\ntext: x\n", "Line 2: \"text\" belongs to a step" },
		{ "title: T\nstep\ntext: x\ntitle: again\n", "Line 4: the title goes before the first step." },
		{ "hello\n", "Line 1: \"hello\" is neither" },
	};
	for (size_t i = 0; i < sizeof(Cases) / sizeof(Cases[0]); ++i) {
		tutorial_t T;
		std::string Error;
		const bool Read = ParseTutorial(Cases[i].Text, T, Error);
		CHECK(!Read);
		CHECK(Error.compare(0, std::string(Cases[i].Error).size(), Cases[i].Error) == 0);
		if (Read || Error.compare(0, std::string(Cases[i].Error).size(), Cases[i].Error) != 0)
			std::printf("  case %d: \"%s\"\n", static_cast<int>(i), Error.c_str());
	}
}

// The tutorials that go into FamiTracker.exe
void TestBuiltIn()
{
	const char *const Files[] = { "first-tune.txt", "sounds-and-drums.txt", "clips-and-piano-roll.txt", "game-music.txt" };
	for (size_t i = 0; i < sizeof(Files) / sizeof(Files[0]); ++i) {
		std::ifstream In((std::string(TUTORIALS_DIR) + "/" + Files[i]).c_str(), std::ios::binary);
		CHECK(In.good());
		std::stringstream Text;
		Text << In.rdbuf();
		tutorial_t T;
		std::string Error;
		const bool Read = ParseTutorial(Text.str(), T, Error);
		CHECK(Read);
		if (!Read)
			std::printf("  %s: %s\n", Files[i], Error.c_str());
		CHECK(T.Steps.size() >= 3 && !T.About.empty());
		// Plain ASCII, as FamiTracker shows it with the ANSI code page
		bool Ascii = true;
		for (size_t k = 0; k < Text.str().size(); ++k)
			if (static_cast<unsigned char>(Text.str()[k]) > 126)
				Ascii = false;
		CHECK(Ascii);
	}
}

}	// namespace

int main()
{
	TestReading();
	TestWaits();
	TestErrors();
	TestBuiltIn();
	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}

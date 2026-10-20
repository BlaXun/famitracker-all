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

#pragma once

// Help > Tutorials: a tutorial is a text file of steps. Each step points at a
// part of the window, says something about it and waits for something: the
// Next button, or the user doing what it says (Tutorial.cpp watches). The
// built-in tutorials are in res/tutorials and go into FamiTracker.exe; more
// can be put in a Tutorials folder next to it, so new ones need no rebuild.
//
//   ; a comment
//   title: Your first tune
//   about: Type a melody, hear it and save it.
//
//   step
//   show: pattern
//   text: This is the pattern editor.
//     Lines that don't start with a "name:" carry on the text before them,
//     and an empty line inside a text starts a new paragraph.
//   wait: edit-mode
//
// "show" is a part of the window (window, pattern, frames, instruments,
// song-settings, song-info, octave, toolbar, status-bar, clips, piano-roll),
// a toolbar button ("button play") or a menu ("menu Edit"). "wait" is next
// (the default), edit-mode, edit-mode-off, play, stop, notes N, instrument,
// clip, saved, or command NAME for a menu item, button or shortcut key; the
// names are listed with TutorialCommandNames.
//
// This is plain C++ that the tests build on Linux (Source/Tests).

#include <string>
#include <vector>

enum tutorial_wait_t {
	WAIT_NEXT,				// the Next button
	WAIT_EDIT_MODE,			// editing switched on
	WAIT_EDIT_MODE_OFF,
	WAIT_PLAY,				// the song playing
	WAIT_STOP,				// and stopped
	WAIT_NOTES,				// Count more notes in the frame shown than when the step began
	WAIT_INSTRUMENT,		// another instrument
	WAIT_CLIP,				// another clip
	WAIT_SAVED,				// the song saved to a file
	WAIT_COMMAND,			// the command Name (a menu item, toolbar button or shortcut key)
};

struct tutorial_step_t {
	std::string Show;		// "pattern", "button", "menu", ...
	std::string ShowName;	// what follows: "play" for "button play", "Edit" for "menu Edit"
	std::string Text;		// paragraphs apart by "\n"
	tutorial_wait_t Wait;
	int Count;				// WAIT_NOTES
	std::string Command;	// WAIT_COMMAND

	tutorial_step_t() : Wait(WAIT_NEXT), Count(1) {}
};

struct tutorial_t {
	std::string Title;
	std::string About;
	std::vector<tutorial_step_t> Steps;
};

// Reads a tutorial. False, with what's wrong and the line in Error, when it
// can't be read: an unknown name, a step without text, no steps at all.
bool ParseTutorial(const std::string &Text, tutorial_t &Tutorial, std::string &Error);

// The parts of the window a step can show, and the commands it can wait for
// or show the toolbar button of (Tutorial.cpp knows their IDs)
bool IsTutorialPart(const std::string &Name);
const std::vector<std::string> &TutorialCommandNames();

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
// song-settings, song-info, octave, toolbar, status-bar, clips, piano-roll,
// drum-grid, mixer), a toolbar button ("button play"), a menu ("menu Edit")
// or a channel's column in the pattern editor ("channel Triangle"); a step
// about the pattern editor (or a channel), the clips panel, the piano roll,
// the drum grid or the mixer shows it if it's hidden (View > Pattern editor
// hides the pattern editor, and the piano roll takes its place). "wait" is next
// (the default), edit-mode, edit-mode-off, play, stop, notes N, instrument,
// clip, saved, heard, or command NAME for a menu item, button or shortcut
// key; the names are listed with TutorialCommandNames.
//
// The example songs (Help > Example songs, res/examples) come with
// walkthroughs: tutorials whose steps also take the user to a bar, play some
// bars and let one part be heard alone:
//
//   step
//   show: channel Triangle
//   play: bars 5-8
//   solo: Triangle
//   text: The bass, on its own.
//   wait: heard
//
// "bar: N" puts the cursor at the start of bar N as the step begins (bars
// count from 1, as the piano roll's ruler numbers them). "play: bar N" or
// "play: bars N-M" gives the note a Play button that plays those bars and
// stops after them (the cursor goes to the first unless "bar" says
// otherwise); "wait: heard" goes on once they've played to the end. "solo:
// A, B" lets only those channels be heard while the step lasts, and "mute:
// A, B" silences them; the channels come back as they were after it.
// "instrument: NN" picks an instrument, numbered in hex as the list shows it.
// Channels are named as the pattern editor's header names them ("Pulse 1",
// "Triangle", "Sawtooth", "Soft 1"), with the chip in front where two chips
// have a channel of that name ("VRC6 Pulse 1"; without it, the first one,
// which is the 2A03's).
//
// Help > Exercises' scripts (res/exercises) are tutorials with check steps:
//
//   step
//   show: piano-roll
//   play: bars 1-4
//   check: home-note
//   text: End the tune on the [[home note]].
//   hint: The tune is in C major: its home note is C.
//   answer: Pulse 1, bar 4: C4 - - - - - - -
//
// "check" names a check of Api/Exercises.h: the step gets a Check button
// that runs it and shows what it says, and goes on once it passes (a check
// step takes no "wait"). "hint" gives it a Hint button. Each "answer" is a
// part of a good answer: a channel, the bar it starts in, the instrument it
// plays when the channel has none there yet ("answer: Pulse 1, bar 1, Lead:
// ..."), and notes in steps as write_notes takes them (a line that starts with
// no name carries them on). Its Hear it button writes them into the song for
// a moment while the step's "play" bars (else the answer's own) play. In every text, "[[words]]" links to the glossary's entry the
// words name, and "[[words|id]]" to that entry.
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
	WAIT_HEARD,				// the step's bars (play) played to their end
	WAIT_CHECK,				// the step's check (Help > Exercises) passing
};

// A part of a check step's good answer
struct tutorial_answer_t {
	std::string Channel;	// as the pattern editor names it ("Pulse 1", "Triangle")
	int Bar;				// where its notes start, from 1
	std::string Instrument;	// the instrument it plays, by its name in the song; "" for the channel's own there
	std::string Notes;		// in steps, as write_notes takes them: "C4 - - - | E4 D4 C4 -"
	tutorial_answer_t() : Bar(1) {}
	int LastBar() const;	// the bar its notes end in (a bar more for each "|")
};

struct tutorial_step_t {
	std::string Show;		// "pattern", "button", "menu", "channel"...
	std::string ShowName;	// what follows: "play" for "button play", "Edit" for "menu Edit", "Triangle" for "channel Triangle"
	std::string Text;		// paragraphs apart by "\n"
	tutorial_wait_t Wait;
	int Count;				// WAIT_NOTES
	std::string Command;	// WAIT_COMMAND
	int Bar;				// the bar the cursor goes to as the step begins (from 1), 0 for none
	int PlayFirst;			// the bars the step's Play button plays (from 1, both in), 0 for no button
	int PlayLast;
	std::vector<std::string> Solo;	// the channels heard alone while the step lasts
	std::vector<std::string> Mute;	// the channels silent while it lasts
	int Instrument;			// picked as the step begins, -1 for none
	std::string Check;		// WAIT_CHECK: the check its Check button runs ("home-note"), "" for none
	std::string Hint;		// what its Hint button shows, "" for none
	std::vector<tutorial_answer_t> Answers;	// a good answer, which its Hear it button lets the user hear

	tutorial_step_t() : Wait(WAIT_NEXT), Count(1), Bar(0), PlayFirst(0), PlayLast(0), Instrument(-1) {}
	int CursorBar() const { return Bar > 0 ? Bar : PlayFirst; }	// where the step puts the cursor, 0 for nowhere
	// The bars Hear it plays: the step's own (play), else from the answer's first bar to its last; false for none
	bool HearBars(int &First, int &Last) const;
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

// A channel as the song has it, for finding the one a tutorial names
struct tutorial_channel_t {
	std::string Name;		// as FamiTracker names it: "Pulse 1", "Triangle", "Soft 1"
	std::string Chip;		// "2A03", "VRC6", "VRC7", "FDS", "MMC5", "N163" or "5B"
	bool Vrc6Plus;			// one of the VRC6 Plus's channels (its VRC6 channels and the soft layer)
	tutorial_channel_t(const std::string &Name, const std::string &Chip, bool Vrc6Plus = false) :
		Name(Name), Chip(Chip), Vrc6Plus(Vrc6Plus) {}
};

// Whether Name names a channel FamiTracker can have ("Triangle", "VRC6 Pulse
// 1"), whatever the case
bool IsTutorialChannel(const std::string &Name);
// The column of the song's channel that Name names (Channels in column
// order), or -1 when the song has none of that name
int FindTutorialChannel(const std::string &Name, const std::vector<tutorial_channel_t> &Channels);

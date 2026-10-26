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

// Help > Tutorials: tutorials (TutorialScript.h) played over the main
// window. The window dims except for the part a step is about, which gets a
// bright frame, and a note beside it says what to do; the step goes on when
// the user has done it (or presses Next). The dimming lets the mouse through,
// so FamiTracker works as always while a tutorial runs.
//
// The built-in tutorials are res/tutorials/*.txt, in FamiTracker.exe as
// RCDATA; more are read from a Tutorials folder next to it.

#include <string>

class CMainFrame;
struct tutorial_t;

void ShowTutorials(CMainFrame *pMainFrame);	// the list; the one picked starts
// Help > Example songs: the example of that menu item opens as a new song
// (res/examples, also RCDATA) and its walkthrough starts
void OpenExampleSong(CMainFrame *pMainFrame, UINT nID);
void TutorialCommand(UINT nID);				// CMainFrame::OnCommand: a menu item, button or shortcut was used
bool TutorialRunning();
// The exercise whose note shows (Help > Exercises), for View > Ask's model: its title, what the step asks now (the
// glossary's links as their words) and the step's check (check_exercise's, "" for none); false for none
bool ExerciseInProgress(std::string &Title, std::string &Step, std::string &Check);
bool ExerciseRunning();						// an exercise's note is open (the piano roll leaves an empty song's groove box out)

// Help > Exercises (ExercisesDlg.cpp): an exercise's note over its song,
// which has just opened. Its check steps (TutorialScript.h) have Hint, Hear
// it and Check buttons; Check runs the check_exercise tool and shows what it
// says, the glossary's words in it linked to their entries and its bars to
// play, and the exercise is done once its last check passes.
void StartExerciseNote(CMainFrame *pMainFrame, const tutorial_t &Script, const std::string &Exercise);
// A built-in text (RCDATA: a tutorial, a walkthrough, an exercise's script), by its name or MAKEINTRESOURCE(its number);
// false when there's none
bool ReadBuiltInText(LPCTSTR Resource, std::string &Text);
// A built-in song (RCDATA, by name or number) opened as a new song, named Title until it's saved (FamiTracker first asks
// whether to save the song that's open); false when it didn't open
bool OpenBuiltInSong(CMainFrame *pMainFrame, LPCTSTR Song, LPCTSTR Title);

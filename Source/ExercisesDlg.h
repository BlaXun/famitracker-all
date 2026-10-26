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

// Help > Exercises (Api/Exercises.h): the list, each exercise's state kept
// between runs (the "Exercises" registry section: not tried, started, done),
// an exercise started (its starter song from res/exercises, in
// FamiTracker.exe as RCDATA, opened as a new untitled song as the example
// songs are, or a new song as File > New makes it; then its note, Tutorial.cpp)
// and the note's Hear it: a good answer written into the song for a moment
// while its bars play, then taken back.

#include <string>

class CMainFrame;
struct tutorial_step_t;

void ShowExercises(CMainFrame *pMainFrame);				// Help > Exercises
void StartExercise(CMainFrame *pMainFrame, UINT nID);	// ID_HELP_EXERCISE_FIRST + its place in the list
UINT ExerciseCommand(const std::string &Id);			// that command for an exercise, 0 for none

enum exercise_state_t { EXERCISE_NOT_TRIED, EXERCISE_STARTED, EXERCISE_DONE };
int GetExerciseState(const std::string &Id);
void SetExerciseState(const std::string &Id, int State);	// done stays done

// Hear it: the step's good answer (its answer lines, written as write_notes writes them, one undoable change) in the song
// while the step's bars play, in a small window beside Near that waits for Done; then the song as it was. False, the
// status bar saying why, when the answer can't be written
bool HearGoodAnswer(CMainFrame *pMainFrame, const tutorial_step_t &Step, const CRect &Near);

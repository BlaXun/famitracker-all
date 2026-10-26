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

// what_next's answer as data (format "json", written by WhatNextTool in
// Tools.cpp) read back for FamiTracker's What next panel
// (Source/WhatNextPanel.cpp), and the recipe's stages both use: the frame,
// the parts, the form, depth, polish, the mix, the loop and the export. Also
// grow_melody's versions, for the panel's Grow one. Plain C++14, tested on
// Linux.

#include <string>
#include <vector>

namespace ftapi {

// The recipe's stages, in their order
enum next_stage {
	STAGE_FRAME, STAGE_PARTS, STAGE_FORM, STAGE_DEPTH, STAGE_POLISH, STAGE_MIX, STAGE_LOOP, STAGE_EXPORT,
	NEXT_STAGES
};

const char *NextStageId(int Stage);			// "frame", as the JSON names it; "" for none
const char *NextStageTitle(int Stage);		// "The frame", for people
int ParseNextStage(const std::string &Id);	// -1 for none
// The stage a step (by its id: "tempo", "chords", "echo"...) or a milestone (by its name: "tempo", "chords and bass",
// "the lead's sound"...) belongs to; -1 for check_song's fixes, which come before the stages, and for the end
int NextStageOfStep(const std::string &Id);
int NextStageOfMilestone(const std::string &Name);

struct NextMilestone {
	std::string Name;		// "tempo", "chords and bass"
	std::string Text;		// what's done: "tempo 150 BPM"; empty while it's left
	int Stage;
	bool Done;
	NextMilestone() : Stage(-1), Done(false) {}
};

// The step what_next gives (the first of its "steps")
struct NextStepInfo {
	std::string Id;			// as skip names it: "tempo", "melody", a kind of check_song's finding ("click")...; "done"
	std::string What;		// in plain words: "Set the tempo to 150 BPM"
	std::string Tool;		// empty when only the user can do it
	std::string Arguments;	// its arguments, as JSON
	std::string Call;		// as the text reply gives it: "set_tempo {\"bpm\":150}", or "ask the user"
	std::string Why;
	std::string Severity;	// a check_song fix's ("high", "medium", "low"); empty for the recipe's steps
	int Stage;				// -1 for a fix of check_song's (before the stages) and the end
	bool AskUser;			// only the user can do it (Why says what)
	bool OwnNotes;			// it offers notes to start from (a hook, a phrase): the user's own tune is better there
	int Calls;				// the calls it takes: more than 1 when calls follow it (Why gives them)
	int FromBar, ToBar;		// the bars it works on (from 1), 0 when it doesn't say
	int Channel;			// the channel it writes (from 0), -1 when it doesn't say
	NextStepInfo() : Stage(-1), AskUser(false), OwnNotes(false), Calls(1), FromBar(0), ToBar(0), Channel(-1) {}
};

struct NextAnswer {
	int Track;				// from 0
	std::string Kind;		// "general", "calm", "lively", "action"
	std::string KindNote;	// how the kind was chosen when none was given; empty when one was
	bool Done;				// every step is done (the step is play)
	NextStepInfo Step;
	std::vector<NextStepInfo> Steps;	// every step found (with all), the next first
	int DoneCount, Total;	// milestones done, and all that count here
	std::vector<NextMilestone> Milestones;
	int StageDone[NEXT_STAGES], StageCount[NEXT_STAGES];	// each stage's milestones done, and all of them
	std::vector<std::string> Notes;
	NextAnswer();
	void Clear();
	// The stage the song is at: the step's, else the first with a milestone left; -1 when done
	int CurrentStage() const;
};

// From what_next's JSON; false with the reason when it isn't that
bool ReadNextAnswer(const std::string &Text, NextAnswer &Out, std::string &Error);

// grow_melody's versions in its reply (without pick): the sentence on each one's shape, in order; empty when the reply
// lists none
std::vector<std::string> GrownVersions(const std::string &Reply);

}	// namespace ftapi

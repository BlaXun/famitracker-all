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

#include "NextSteps.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include "Json.h"

namespace ftapi {

namespace {

const char *const STAGE_IDS[NEXT_STAGES] = { "frame", "parts", "form", "depth", "polish", "mix", "loop", "export" };
const char *const STAGE_TITLES[NEXT_STAGES] = { "The frame", "The parts", "The form", "Depth", "Polish", "The mix", "The loop",
	"The export" };

// what_next's steps (as skip names them) and milestones (as its reply names them), each with its stage; keep them in
// step with WhatNext in Tools.cpp (TestWhatNextJson checks that every one it gives has a stage). An empty song's shape
// (song_shape, its sections laid out) is part of the frame: it comes after the tempo and the key, before any part
struct staged_t {
	const char *Name;
	int Stage;
};

const staged_t STEP_STAGES[] = {
	{ "tempo", STAGE_FRAME }, { "key", STAGE_FRAME }, { "shape", STAGE_FRAME },
	{ "lead", STAGE_PARTS }, { "chords", STAGE_PARTS }, { "melody", STAGE_PARTS }, { "drums", STAGE_PARTS },
	{ "sections", STAGE_FORM }, { "form", STAGE_FORM },
	{ "second_line", STAGE_DEPTH }, { "variation", STAGE_DEPTH }, { "break", STAGE_DEPTH }, { "intro", STAGE_DEPTH },
	{ "tips", STAGE_DEPTH }, { "echo", STAGE_DEPTH }, { "colors", STAGE_DEPTH },
	{ "listen", STAGE_POLISH }, { "polish", STAGE_POLISH }, { "mix", STAGE_MIX }, { "loop", STAGE_LOOP }, { "seam", STAGE_LOOP },
	{ "save", STAGE_EXPORT }, { "export", STAGE_EXPORT },
};

const staged_t MILESTONE_STAGES[] = {
	{ "tempo", STAGE_FRAME }, { "key", STAGE_FRAME }, { "shape", STAGE_FRAME },
	{ "the lead's sound", STAGE_PARTS }, { "chords and bass", STAGE_PARTS }, { "melody", STAGE_PARTS }, { "drums", STAGE_PARTS },
	{ "sections", STAGE_FORM }, { "form", STAGE_FORM },
	{ "second line", STAGE_DEPTH }, { "repeats varied", STAGE_DEPTH }, { "break", STAGE_DEPTH }, { "intro", STAGE_DEPTH },
	{ "review tips", STAGE_DEPTH }, { "echo", STAGE_DEPTH },
	{ "listened", STAGE_POLISH }, { "polish", STAGE_POLISH }, { "mix", STAGE_MIX }, { "loop", STAGE_LOOP }, { "seam", STAGE_LOOP },
	{ "saved", STAGE_EXPORT }, { "exported", STAGE_EXPORT },
};

int StageIn(const staged_t *pList, size_t Count, const std::string &Name)
{
	for (size_t i = 0; i < Count; ++i)
		if (Name == pList[i].Name)
			return pList[i].Stage;
	return -1;
}

int IntOf(const Json *pObject, const char *Name, int Default)
{
	const Json *pValue = pObject != 0 ? pObject->Get(Name) : 0;
	return pValue != 0 && pValue->IsNumber() ? static_cast<int>(pValue->Integer()) : Default;
}

std::string TextOf(const Json *pObject, const char *Name)
{
	const Json *pValue = pObject != 0 ? pObject->Get(Name) : 0;
	return pValue != 0 && pValue->IsString() ? pValue->String() : std::string();
}

bool BoolOf(const Json *pObject, const char *Name)
{
	const Json *pValue = pObject != 0 ? pObject->Get(Name) : 0;
	return pValue != 0 && pValue->IsBool() && pValue->Bool();
}

NextStepInfo StepOf(const Json &Item)
{
	NextStepInfo S;
	S.Id = TextOf(&Item, "id");
	S.What = TextOf(&Item, "what");
	S.Tool = TextOf(&Item, "tool");
	const Json *pArguments = Item.Get("arguments");
	S.Arguments = pArguments != 0 && pArguments->IsObject() ? WriteJson(*pArguments) : std::string("{}");
	S.Call = TextOf(&Item, "call");
	S.Why = TextOf(&Item, "why");
	S.Severity = TextOf(&Item, "severity");
	S.Stage = ParseNextStage(TextOf(&Item, "stage"));
	S.AskUser = BoolOf(&Item, "ask_user");
	S.OwnNotes = BoolOf(&Item, "own_notes");
	S.Calls = std::max(1, IntOf(&Item, "calls", 1));
	S.FromBar = std::max(0, IntOf(&Item, "from_bar", 0));
	S.ToBar = std::max(S.FromBar, IntOf(&Item, "to_bar", S.FromBar));
	S.Channel = IntOf(&Item, "channel", 0) - 1;
	return S;
}

}	// namespace

const char *NextStageId(int Stage)
{
	return Stage >= 0 && Stage < NEXT_STAGES ? STAGE_IDS[Stage] : "";
}

const char *NextStageTitle(int Stage)
{
	return Stage >= 0 && Stage < NEXT_STAGES ? STAGE_TITLES[Stage] : "";
}

int ParseNextStage(const std::string &Id)
{
	for (int s = 0; s < NEXT_STAGES; ++s)
		if (Id == STAGE_IDS[s])
			return s;
	return -1;
}

int NextStageOfStep(const std::string &Id)
{
	return StageIn(STEP_STAGES, sizeof(STEP_STAGES) / sizeof(STEP_STAGES[0]), Id);
}

int NextStageOfMilestone(const std::string &Name)
{
	return StageIn(MILESTONE_STAGES, sizeof(MILESTONE_STAGES) / sizeof(MILESTONE_STAGES[0]), Name);
}

NextAnswer::NextAnswer()
{
	Clear();
}

void NextAnswer::Clear()
{
	Track = 0;
	Kind.clear();
	KindNote.clear();
	Done = false;
	Step = NextStepInfo();
	Steps.clear();
	DoneCount = Total = 0;
	Milestones.clear();
	for (int s = 0; s < NEXT_STAGES; ++s)
		StageDone[s] = StageCount[s] = 0;
	Notes.clear();
}

int NextAnswer::CurrentStage() const
{
	if (Done)
		return -1;
	if (Step.Stage >= 0)
		return Step.Stage;
	for (int s = 0; s < NEXT_STAGES; ++s)
		if (StageDone[s] < StageCount[s])
			return s;
	return -1;
}

bool ReadNextAnswer(const std::string &Text, NextAnswer &Out, std::string &Error)
{
	Out.Clear();
	Json Root;
	if (!ParseJson(Text, Root, &Error) || !Root.IsObject() || Root.Get("step") == 0 || !Root.Get("step")->IsObject()) {
		if (Error.empty())
			Error = "what_next didn't answer with its step.";
		return false;
	}
	Out.Track = std::max(0, IntOf(&Root, "track", 1) - 1);
	Out.Kind = TextOf(&Root, "kind");
	Out.KindNote = TextOf(&Root, "kind_note");
	Out.Done = BoolOf(&Root, "done");
	Out.Step = StepOf(*Root.Get("step"));
	const Json *pSteps = Root.Get("steps");
	for (size_t i = 0; pSteps != 0 && pSteps->IsArray() && i < pSteps->Size(); ++i)
		if (pSteps->At(i).IsObject())
			Out.Steps.push_back(StepOf(pSteps->At(i)));
	if (Out.Steps.empty())
		Out.Steps.push_back(Out.Step);
	Out.DoneCount = std::max(0, IntOf(&Root, "done_count", 0));
	Out.Total = std::max(Out.DoneCount, IntOf(&Root, "total", 0));
	const Json *pMilestones = Root.Get("milestones");
	for (size_t i = 0; pMilestones != 0 && pMilestones->IsArray() && i < pMilestones->Size(); ++i) {
		const Json &Item = pMilestones->At(i);
		if (!Item.IsObject())
			continue;
		NextMilestone M;
		M.Name = TextOf(&Item, "name");
		M.Text = TextOf(&Item, "text");
		M.Stage = ParseNextStage(TextOf(&Item, "stage"));
		M.Done = BoolOf(&Item, "done");
		Out.Milestones.push_back(M);
		if (M.Stage >= 0) {
			++Out.StageCount[M.Stage];
			Out.StageDone[M.Stage] += M.Done ? 1 : 0;
		}
	}
	const Json *pNotes = Root.Get("notes");
	for (size_t i = 0; pNotes != 0 && pNotes->IsArray() && i < pNotes->Size(); ++i)
		if (pNotes->At(i).IsString())
			Out.Notes.push_back(pNotes->At(i).String());
	return true;
}

namespace {

// A piece of a why that speaks to a model: it names a call ({...}) or a tool (a word joined by _, like find_sound), or the
// reply
bool ForModel(const std::string &Text)
{
	if (Text.find('{') != std::string::npos || Text.find("reply") != std::string::npos || Text.find("what_next") != std::string::npos)
		return true;
	for (size_t i = 1; i + 1 < Text.size(); ++i)
		if (Text[i] == '_' && std::islower(static_cast<unsigned char>(Text[i - 1])) && std::islower(static_cast<unsigned char>(Text[i + 1])))
			return true;
	return false;
}

}	// namespace

std::string PlainWhy(const std::string &Why)
{
	std::string Text = Why;
	static const char *const HEADS[] = { "check_song: ", "review_arrangement: " };
	for (size_t h = 0; h < sizeof(HEADS) / sizeof(HEADS[0]); ++h)
		if (Text.compare(0, std::strlen(HEADS[h]), HEADS[h]) == 0)
			Text.erase(0, std::strlen(HEADS[h]));
	// Brackets that speak to a model go (with the space before them)
	for (size_t Open = Text.find('('); Open != std::string::npos; Open = Text.find('(', Open + 1)) {
		int Depth = 0;
		size_t Close = Open;
		for (; Close < Text.size(); ++Close) {
			Depth += Text[Close] == '(' ? 1 : Text[Close] == ')' ? -1 : 0;
			if (Depth == 0)
				break;
		}
		if (Close >= Text.size() || !ForModel(Text.substr(Open, Close - Open + 1)))
			continue;
		const size_t From = Open > 0 && Text[Open - 1] == ' ' ? Open - 1 : Open;
		Text.erase(From, Close + 1 - From);
		Open = From > 0 ? From - 1 : 0;
		if (Text.empty())
			break;
	}
	// Then the sentences that do (a sentence ends at ". " or the end)
	std::string Out;
	size_t Start = 0;
	while (Start < Text.size()) {
		size_t End = Text.find(". ", Start);
		End = End == std::string::npos ? Text.size() : End + 1;
		const std::string Sentence = Text.substr(Start, End - Start);
		if (!ForModel(Sentence)) {
			std::string Kept = Sentence;
			while (!Kept.empty() && Kept[0] == ' ')
				Kept.erase(0, 1);
			Out += (Out.empty() ? "" : " ") + Kept;
		}
		Start = End;
	}
	while (!Out.empty() && Out[Out.size() - 1] == ' ')
		Out.erase(Out.size() - 1);
	return Out.empty() ? Why : Out;
}

std::vector<std::string> GrownVersions(const std::string &Reply)
{
	// "\n\nVersion 1. Its shape...\n1: D4 ...": the sentence after each version's number, to the line's end
	std::vector<std::string> Out;
	for (int v = 1; v <= 9; ++v) {
		const std::string Head = "\nVersion " + std::to_string(v) + ". ";
		const size_t At = Reply.find(Head);
		if (At == std::string::npos)
			break;
		const size_t From = At + Head.size(), To = Reply.find('\n', From);
		Out.push_back(Reply.substr(From, To == std::string::npos ? std::string::npos : To - From));
	}
	return Out;
}

}	// namespace ftapi

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

#include "ProblemList.h"

#include <algorithm>
#include "Json.h"

namespace ftapi {

namespace {

const char *const KIND_IDS[PROBLEM_KINDS] = { "vanishing", "click", "clash", "range", "silent", "busy", "key", "held" };
const char *const KIND_TITLES[PROBLEM_KINDS] = {
	"Notes that vanish", "Notes that click", "Clashes", "Out of range", "Silent bars", "Busy bars", "Outside the key",
	"Leads that hold steady"
};
const int SEVERITIES[PROBLEM_KINDS] = { 3, 3, 3, 2, 1, 1, 1, 2 };

int IntOf(const Json &Object, const char *Name, int Default)
{
	const Json *pValue = Object.Get(Name);
	return pValue != 0 && pValue->IsNumber() ? static_cast<int>(pValue->Integer()) : Default;
}

std::string TextOf(const Json &Object, const char *Name)
{
	const Json *pValue = Object.Get(Name);
	return pValue != 0 && pValue->IsString() ? pValue->String() : std::string();
}

// A place in the JSON (frame and row) as a row from the song's start
int RowOf(const Json &Object, int PatternLength)
{
	return std::max(0, IntOf(Object, "frame", 0)) * PatternLength + std::max(0, IntOf(Object, "row", 0));
}

}	// namespace

const char *ProblemKindId(int Kind)
{
	return Kind >= 0 && Kind < PROBLEM_KINDS ? KIND_IDS[Kind] : "";
}

const char *ProblemKindTitle(int Kind)
{
	return Kind >= 0 && Kind < PROBLEM_KINDS ? KIND_TITLES[Kind] : "";
}

int ProblemSeverity(int Kind)
{
	return Kind >= 0 && Kind < PROBLEM_KINDS ? SEVERITIES[Kind] : 1;
}

ProblemList::ProblemList()
{
	Clear();
}

void ProblemList::Clear()
{
	m_iTrack = 0;
	m_iPatternLength = 64;
	m_sKey.clear();
	for (int k = 0; k < PROBLEM_KINDS; ++k) {
		m_iCounts[k] = 0;
		m_Lines[k].clear();
	}
	m_Findings.clear();
}

bool ProblemList::Read(const std::string &Text, std::string &Error)
{
	Clear();
	Json Root;
	if (!ParseJson(Text, Root, &Error) || !Root.IsObject() || Root.Get("findings") == 0 || !Root.Get("findings")->IsArray()) {
		if (Error.empty())
			Error = "check_song didn't answer with its findings.";
		return false;
	}
	m_iTrack = std::max(0, IntOf(Root, "track", 1) - 1);
	m_iPatternLength = std::max(1, IntOf(Root, "pattern_length", 64));
	m_sKey = TextOf(Root, "key");
	const Json *pCounts = Root.Get("counts");
	for (int k = 0; k < PROBLEM_KINDS; ++k)
		m_iCounts[k] = pCounts != 0 && pCounts->IsObject() ? std::max(0, IntOf(*pCounts, KIND_IDS[k], 0)) : 0;

	const Json &List = *Root.Get("findings");
	for (size_t i = 0; i < List.Size(); ++i) {
		const Json &Item = List.At(i);
		if (!Item.IsObject())
			continue;
		const std::string Id = TextOf(Item, "kind");
		ProblemFinding F;
		F.Kind = -1;
		for (int k = 0; k < PROBLEM_KINDS; ++k)
			if (Id == KIND_IDS[k])
				F.Kind = k;
		if (F.Kind < 0)
			continue;	// a kind of a later version
		F.Channel = IntOf(Item, "channel", 0) - 1;
		F.Row = RowOf(Item, m_iPatternLength);
		F.Rows = std::max(1, IntOf(Item, "rows", 1));
		F.ChannelName = TextOf(Item, "channel_name");
		F.Position = TextOf(Item, "position");
		F.Summary = TextOf(Item, "summary");
		F.Message = TextOf(Item, "message");
		F.Nudge = TextOf(Item, "nudge");
		F.Boundary = std::max(0, IntOf(Item, "boundary", 0));
		const Json *pNotes = Item.Get("notes");
		for (size_t n = 0; pNotes != 0 && pNotes->IsArray() && n < pNotes->Size(); ++n) {
			const Json &Note = pNotes->At(n);
			if (!Note.IsObject())
				continue;
			ProblemNote N;
			N.Channel = IntOf(Note, "channel", F.Channel + 1) - 1;
			N.Row = RowOf(Note, m_iPatternLength);
			N.Rows = std::max(1, IntOf(Note, "rows", 1));
			N.Name = TextOf(Note, "note");
			N.Position = TextOf(Note, "position");
			if (N.Channel >= 0)
				F.Notes.push_back(N);
		}
		const Json *pFix = Item.Get("fix");
		if (pFix != 0 && pFix->IsObject() && !TextOf(*pFix, "tool").empty() && pFix->Get("arguments") != 0) {
			F.FixTool = TextOf(*pFix, "tool");
			F.FixArguments = WriteJson(*pFix->Get("arguments"));
			F.FixDoes = TextOf(*pFix, "does");
		}
		m_Findings.push_back(F);
	}

	// The lines: a finding each, or a note each of notes that vanish, in the song's order
	for (size_t i = 0; i < m_Findings.size(); ++i) {
		const ProblemFinding &F = m_Findings[i];
		ProblemLine Line;
		Line.Finding = static_cast<int>(i);
		if (F.Kind == PROBLEM_VANISHING && !F.Notes.empty()) {
			for (size_t n = 0; n < F.Notes.size(); ++n) {
				Line.Note = static_cast<int>(n);
				Line.Channel = F.Notes[n].Channel;
				Line.Row = F.Notes[n].Row;
				m_Lines[F.Kind].push_back(Line);
			}
			continue;
		}
		Line.Channel = F.Channel;
		Line.Row = F.Row;
		m_Lines[F.Kind].push_back(Line);
	}
	for (int k = 0; k < PROBLEM_KINDS; ++k)
		std::stable_sort(m_Lines[k].begin(), m_Lines[k].end(), [](const ProblemLine &a, const ProblemLine &b) {
			return a.Row != b.Row ? a.Row < b.Row : a.Channel < b.Channel;
		});
	return true;
}

int ProblemList::Count(int Kind) const
{
	return Kind >= 0 && Kind < PROBLEM_KINDS ? m_iCounts[Kind] : 0;
}

int ProblemList::Total() const
{
	int Sum = 0;
	for (int k = 0; k < PROBLEM_KINDS; ++k)
		Sum += m_iCounts[k];
	return Sum;
}

const std::vector<ProblemLine> &ProblemList::Lines(int Kind) const
{
	static const std::vector<ProblemLine> NONE;
	return Kind >= 0 && Kind < PROBLEM_KINDS ? m_Lines[Kind] : NONE;
}

const ProblemNote *ProblemList::LineNote(const ProblemLine &Line) const
{
	if (Line.Finding < 0 || Line.Finding >= static_cast<int>(m_Findings.size()))
		return 0;
	const ProblemFinding &F = m_Findings[static_cast<size_t>(Line.Finding)];
	if (F.Notes.empty())
		return 0;
	return &F.Notes[Line.Note >= 0 && Line.Note < static_cast<int>(F.Notes.size()) ? static_cast<size_t>(Line.Note) : 0];
}

std::string ProblemList::LinePosition(const ProblemLine &Line) const
{
	if (Line.Finding < 0 || Line.Finding >= static_cast<int>(m_Findings.size()))
		return std::string();
	const ProblemFinding &F = m_Findings[static_cast<size_t>(Line.Finding)];
	const ProblemNote *pNote = Line.Note >= 0 ? LineNote(Line) : 0;
	return pNote != 0 && !pNote->Position.empty() ? pNote->Position : F.Position;
}

std::string ProblemList::LineText(const ProblemLine &Line) const
{
	if (Line.Finding < 0 || Line.Finding >= static_cast<int>(m_Findings.size()))
		return std::string();
	const ProblemFinding &F = m_Findings[static_cast<size_t>(Line.Finding)];
	std::string Text = F.Summary.empty() ? F.Message : F.Summary;
	if (F.Kind == PROBLEM_VANISHING && Line.Note >= 0) {
		const ProblemNote *pNote = LineNote(Line);
		if (pNote != 0 && !pNote->Name.empty())
			return pNote->Name + ": " + Text;
	}
	if (!Text.empty() && Text[0] >= 'a' && Text[0] <= 'z')
		Text[0] = static_cast<char>(Text[0] - 'a' + 'A');
	return Text;
}

ProblemMarks::ProblemMarks() : m_iTrack(-1), m_iChannels(0), m_iRows(0)
{
}

void ProblemMarks::Clear()
{
	m_iTrack = -1;
	m_iChannels = m_iRows = 0;
	m_Marks.clear();
}

void ProblemMarks::Set(const ProblemList &List, int Channels, int Rows, int Selected)
{
	Clear();
	if (Channels <= 0 || Rows <= 0 || List.Findings().empty())
		return;
	m_iTrack = List.Track();
	m_iChannels = Channels;
	m_iRows = Rows;
	m_Marks.assign(static_cast<size_t>(Channels) * static_cast<size_t>(Rows), 0);
	auto Put = [&](int Channel, int Row, int Kind, bool Chosen) {
		if (Channel < 0 || Channel >= m_iChannels || Row < 0 || Row >= m_iRows)
			return;
		unsigned char &Mark = m_Marks[static_cast<size_t>(Channel) * static_cast<size_t>(m_iRows) + static_cast<size_t>(Row)];
		const int Old = Mark & ~SELECTED;
		if (Old == 0 || Kind + 1 < Old)	// the kinds go heard worst first
			Mark = static_cast<unsigned char>((Mark & SELECTED) | (Kind + 1));
		if (Chosen)
			Mark = static_cast<unsigned char>(Mark | SELECTED);
	};
	const std::vector<ProblemFinding> &Findings = List.Findings();
	for (size_t i = 0; i < Findings.size(); ++i) {
		const ProblemFinding &F = Findings[i];
		const bool Chosen = static_cast<int>(i) == Selected;
		if (F.Notes.empty())
			Put(F.Channel, F.Row, F.Kind, Chosen);	// a busy bar: its first row
		for (size_t n = 0; n < F.Notes.size(); ++n)
			Put(F.Notes[n].Channel, F.Notes[n].Row, F.Kind, Chosen);
	}
}

bool ProblemMarks::operator==(const ProblemMarks &Other) const
{
	return m_iTrack == Other.m_iTrack && m_iChannels == Other.m_iChannels && m_iRows == Other.m_iRows && m_Marks == Other.m_Marks;
}

int ProblemMarks::At(int Track, int Channel, int Row) const
{
	if (Track != m_iTrack || Channel < 0 || Channel >= m_iChannels || Row < 0 || Row >= m_iRows)
		return 0;
	return m_Marks[static_cast<size_t>(Channel) * static_cast<size_t>(m_iRows) + static_cast<size_t>(Row)];
}

unsigned long ProblemMarks::Color(int Mark)
{
	const int Kind = KindOf(Mark);
	unsigned long Red = 120, Green = 140, Blue = 175;	// worth a look
	if (ProblemSeverity(Kind) >= 3) {
		Red = 235;
		Green = 60;
		Blue = 50;
	}
	else if (ProblemSeverity(Kind) == 2) {
		Red = 240;
		Green = 150;
		Blue = 30;
	}
	return Red | (Green << 8) | (Blue << 16);
}

}	// namespace ftapi

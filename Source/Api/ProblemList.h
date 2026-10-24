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

// check_song's findings read back from its JSON (format "json"), for
// FamiTracker's problems panel (Source/ProblemsPanel.cpp): the findings by
// kind, heard worst first, a line each in the panel (a line a note for notes
// that vanish), and the marks the pattern editor and the piano roll draw
// where they are. Plain C++14, tested on Linux.

#include <string>
#include <vector>

namespace ftapi {

// check_song's kinds, heard worst first (the order of its text and its JSON)
enum problem_kind {
	PROBLEM_VANISHING, PROBLEM_CLICK, PROBLEM_CLASH, PROBLEM_RANGE, PROBLEM_SILENT, PROBLEM_BUSY, PROBLEM_KEY, PROBLEM_KINDS
};

const char *ProblemKindId(int Kind);	// "vanishing", as check_song names it
const char *ProblemKindTitle(int Kind);	// "Notes that vanish", for people
int ProblemSeverity(int Kind);			// 3: heard worst (notes that vanish, clicks, clashes), 2: out of range, 1: worth a look

struct ProblemNote {
	int Channel;		// from 0
	int Row;			// from the song's start: frame * pattern length + row
	int Rows;
	std::string Name;		// "C-4", empty when it's not known
	std::string Position;	// "bar 3 beat 2"
	ProblemNote() : Channel(0), Row(0), Rows(1) {}
};

struct ProblemFinding {
	int Kind;			// problem_kind
	int Channel;		// from 0, -1 for none (a silent bar)
	int Row, Rows;		// where it starts in the song, and how long it lasts
	std::string ChannelName, Position, Summary, Message;	// UTF-8
	std::vector<ProblemNote> Notes;	// the notes it's about
	std::string FixTool;		// the tool call that fixes it ("polish"), empty for none...
	std::string FixArguments;	// ...with its arguments, as JSON...
	std::string FixDoes;		// ...and what it does, in words
	std::string Nudge;			// a click's nudge ("P81, 7 cents sharp"), empty when none stops it
	int Boundary;				// the period a click's vibrato crosses (256), 0 for other kinds
	ProblemFinding() : Kind(PROBLEM_KEY), Channel(-1), Row(0), Rows(1), Boundary(0) {}
};

// A line of the panel: a finding, or one note of it (notes that vanish get a
// line each, so each is a place to go)
struct ProblemLine {
	int Finding;		// into ProblemList::Findings()
	int Note;			// into its notes, -1 for the finding as a whole
	int Channel, Row;	// where it is
	ProblemLine() : Finding(0), Note(-1), Channel(-1), Row(0) {}
};

class ProblemList {
public:
	ProblemList();

	// From check_song's JSON; false with the reason when it isn't that (and
	// the list is empty)
	bool Read(const std::string &Text, std::string &Error);
	void Clear();

	int Track() const { return m_iTrack; }		// from 0
	int PatternLength() const { return m_iPatternLength; }
	const std::string &Key() const { return m_sKey; }	// "C major", empty without notes
	int Count(int Kind) const;		// as check_song counts them (notes that vanish by the note)
	int Total() const;
	const std::vector<ProblemFinding> &Findings() const { return m_Findings; }
	// A kind's lines in the song's order: by row, then channel
	const std::vector<ProblemLine> &Lines(int Kind) const;
	// The note a line is about (its own, or its finding's first); null for none
	const ProblemNote *LineNote(const ProblemLine &Line) const;
	// What the panel shows of a line, in UTF-8: where it is ("bar 3 beat 2"), and what's wrong, with the note when
	// the finding has several ("D-4: "Fair pluck" (03) is gone within 3 frames, ...")
	std::string LinePosition(const ProblemLine &Line) const;
	std::string LineText(const ProblemLine &Line) const;

private:
	int m_iTrack, m_iPatternLength;
	std::string m_sKey;
	int m_iCounts[PROBLEM_KINDS];
	std::vector<ProblemFinding> m_Findings;
	std::vector<ProblemLine> m_Lines[PROBLEM_KINDS];
};

// Where the findings are, for the pattern editor and the piano roll: on each
// channel's rows of a track, the worst kind found on a note that starts there
// (or on a busy bar's first row), and whether the finding the panel has
// selected is there. Silent bars have no channel, so no marks.
class ProblemMarks {
public:
	static const int SELECTED = 0x80;	// added to a mark where the selected finding is

	ProblemMarks();
	void Clear();
	// The marks of a list's findings on a track of Channels channels and Rows rows; Selected: a finding (into its
	// Findings()) to set apart, -1 for none
	void Set(const ProblemList &List, int Channels, int Rows, int Selected);
	// The mark on a channel's row of a track (counted from the song's start): 0 for none, else the kind + 1, plus
	// SELECTED
	int At(int Track, int Channel, int Row) const;
	bool IsEmpty() const { return m_Marks.empty(); }
	bool operator==(const ProblemMarks &Other) const;
	bool operator!=(const ProblemMarks &Other) const { return !(*this == Other); }

	static int KindOf(int Mark) { return (Mark & ~SELECTED) - 1; }
	static bool IsSelected(int Mark) { return (Mark & SELECTED) != 0; }
	// The colour a mark is drawn in, as Windows writes colours (0x00BBGGRR): red for what's heard worst, amber for
	// out of range, a grey blue for the rest
	static unsigned long Color(int Mark);

private:
	int m_iTrack, m_iChannels, m_iRows;
	std::vector<unsigned char> m_Marks;	// a channel's rows after another's
};

}	// namespace ftapi

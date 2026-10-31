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

// Song shapes (ROADMAP.md 16): the form of a kind of song as named sections,
// each with its bars and a line on what the part is for, laid out in one
// click: FamiTracker's Module > Song shape (SongShapeDlg.cpp) and the
// song_shape tool (Tools.cpp), which what_next gives an empty song. The
// forms are the ones the tools teach (the recipes' forms, what_next's, the
// soundtrack's practice of an intro that plays once, loops of about 36 bars
// and a break before the loop comes round), so the app and the tools agree.
//
// A section's line is worked out from its name (and the shape the track's
// sections match) when it's shown, so nothing new is saved: the sections
// stay as Sections.h keeps them. Plain C++14, tested on Linux.

#include <string>
#include <vector>
#include "Sections.h"

namespace ftapi {

// A part of a shape: a section of its own
struct ShapePart {
	std::string Name;	// the section's: "Intro", "A", "A'"
	int Bars;
	std::string Line;	// what it's for, in plain words
};

struct SongShape {
	std::string Id;		// what song_shape takes: "stage"
	std::string Title;	// "Lively stage"
	std::string About;	// what the song is for, a line
	std::string Kind;	// what_next's kind for it: "lively", "calm", "action" or "general"
	bool Loops;			// false: it plays once and stops (a jingle)
	int LoopPart;		// the part the loop goes back to (the one after the intro)
	std::vector<ShapePart> Parts;

	int Bars() const;
	// The most bars a frame can hold with every part whole frames (their greatest common divisor)
	int FrameBars() const;
	// "Intro 4, A 8, A' 8, B 8, Climax 8, Break 4"
	std::string PartsText() const;
};

// Every shape, in the order FamiTracker lists them
const std::vector<SongShape> &SongShapes();
// A shape by its id, its title or the words for its kind of song ("lively", "calm", "battle", "shop"...); null for none
const SongShape *FindSongShape(const std::string &Words);
// The ids, "stage, night, town, boss, small or jingle"
std::string SongShapeIds();
// The shape what_next lays out for one of its kinds ("lively", "calm", "action"; anything else is the general recipe's)
const SongShape &ShapeForKind(const std::string &Kind);
// The same for a track of that name: the shape its name asks for when that's one of the kind's that loops (a "Shop" or
// "A first small song" gets the general recipe's short loop instead of its town), else the kind's own
const SongShape &ShapeForKind(const std::string &Kind, const std::string &TrackName);

// What a section is for, in plain words: its line in the shape when it's one
// of its parts (pShape null: none), else what its name says (Intro, A, A',
// A2, B, Bridge, Verse, Chorus, Climax, Break, Outro...); "" when the name
// says nothing of it
std::string PartLine(const std::string &Name, const SongShape *pShape = 0);
// Whether a section's name makes it the song's high point: Climax, Peak or Finale (numbered or primed too: "Climax 2"),
// the words PartLine gives the climax's line for
bool IsClimaxName(const std::string &Name);
// The shape a track's sections were laid out in: their names, in order, are
// its parts' (should two shapes share their names, pPrefer when it's one of
// them, else a calm word in the track's name or a tempo of 90 BPM or under
// picks the calm night); null for none
const SongShape *MatchSongShape(const SectionList &Sections, const std::string &TrackName, double Bpm,
	const SongShape *pPrefer = 0);
// That shape, else the one the track's name says ("Boss", "Night", "Shop"):
// the song's kind, for the lines of its sections; null for none
const SongShape *GuessSongShape(const SectionList &Sections, const std::string &TrackName, double Bpm);
// The line shown where a section is named (the frame editor, the piano roll's
// ruler): "A' (bars 13-20): The tune again, ..."; FirstBar and LastBar from 1
std::string SectionHoverText(const std::string &Name, int FirstBar, int LastBar, const SongShape *pShape);

// A progression of a chord a bar ("| I | V | vi | IV |") fitted to a part of
// Bars bars: its chords from the start, as far as they go and round again,
// its last two bars kept at the part's end (where it turns home or leads on)
std::string FitProgression(const std::string &Progression, int Bars);

// A shape laid out on a track
struct ShapeLayout {
	int RowsPerFrame;		// the track's pattern length afterwards (another only on a track without notes)
	int BarsPerFrame;
	int Frames;				// the frames the track has afterwards
	int FrameChange;		// frames put in at its end (taken out, below 0: only empty frames of a track without notes)
	int NeededFrames;		// the frames the whole shape takes
	SectionList Sections;	// the track's sections afterwards
	std::vector<int> PartBars;	// each part's bars as laid out, 0 for a part that didn't fit
	int LoopFrame;			// the frame the song goes back to after its end; -1 when it stops there
	std::vector<int> Refitted;	// the parts whose bars went to whole frames of the track's (a track with notes)
	int Cut;				// a track with notes and too few frames: the part cut short at its end, -1 for none
	int Missing;			// ...and the parts after it, left out
	ShapeLayout() : RowsPerFrame(64), BarsPerFrame(4), Frames(1), FrameChange(0), NeededFrames(1), LoopFrame(-1), Cut(-1),
		Missing(0) {}
	int FirstBar(int Part) const;	// a part's first bar as laid out (from 1); 0 when it didn't fit
};

// The most rows a frame can have (FamiTracker's MAX_PATTERN_LENGTH)
const int SHAPE_MAX_ROWS = 256;

// Works out a shape's layout on a track of Frames frames of PatternLength
// rows, RowsPerBar rows a bar. A track without notes (Empty) gets the frames
// the shape needs: of its own length when every part is whole frames of it,
// else of the most whole bars that every part is (2 bars for a boss's 2-bar
// intro); a loop back to the part after the intro, or a stop at the end. A
// track with notes keeps its frames and every note: its sections go over the
// frames it has (a part of bars that aren't whole frames of its takes the
// frames nearest them), and the frames the shape needs past its end are put
// in only with AddFrames. False with Error when the shape doesn't fit
// (more frames than MaxFrames).
bool LayOutShape(const SongShape &Shape, bool Empty, int RowsPerBar, int PatternLength, int Frames, int MaxFrames, bool AddFrames,
	ShapeLayout &Out, std::string &Error);

}	// namespace ftapi

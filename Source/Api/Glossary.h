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

// The glossary: musical, sound and tracker terms and every effect, each in
// plain words for someone with no music training (a term, the other names it
// goes by, its category, two to four sentences, see-also links) and, where a
// sound helps, a short Hear it example. FamiTracker's Help > Glossary window
// (GlossaryDlg.cpp) shows it and plays the examples on the song's channels
// (GlossaryHear.cpp); the explain tool gives Claude and local models the same
// words. The effects' entries come from Effects.h's table, which the effects
// tool reads too. All of it ASCII.

#include <string>
#include <vector>

namespace ftapi {

struct ChannelInfo;

enum glossary_category_t {
	GLOSSARY_MUSIC,		// making music: notes, chords, rhythm, song parts
	GLOSSARY_SOUNDS,	// sounds: timbre, envelopes, the NES's channels and chips
	GLOSSARY_TRACKER,	// the tracker's own words: rows, patterns, frames, instruments
	GLOSSARY_EFFECTS,	// every effect of FamiTracker 0.4.6, and this FamiTracker's 8xx
	GLOSSARY_CATEGORIES
};

const char *GlossaryCategoryName(int Category);	// "Making music", "Sounds", "The tracker", "Effects"

struct GlossaryEntry {
	std::string Id;						// "chord", "effect-4xy": what links and tooltips name
	std::string Term;					// "Chord", "Vibrato (4xy)"
	std::vector<std::string> Names;		// the other names it goes by: "chords", "4xx", "vibrato effect"
	int Category;						// glossary_category_t
	std::string Text;					// two to four sentences in plain words
	std::vector<std::string> SeeAlso;	// ids of other entries
	std::string Hear;					// the id of its Hear it example (GlossaryExample), "" for none
	std::string HearHow;				// where there's no example: how to hear it in FamiTracker, "" when nothing does
};

// Every entry, by category, each category in alphabetical order of its terms
const std::vector<GlossaryEntry> &GlossaryEntries();

// The entry a word names: its id, its term or one of its names, case, spaces and punctuation aside ("Pulse width",
// "duty", "4XY", "4" and "vibrato effect" all find theirs); NULL for none
const GlossaryEntry *FindGlossaryEntry(const std::string &Name);

// The entries that match the words (each word in the term, a name or the text), best first: the term or a name
// that is the words, then one that starts with them, then the words in a term or a name, then in the text. Every
// entry of the category (-1 for all) when the words are empty
std::vector<const GlossaryEntry *> SearchGlossary(const std::string &Words, int Category = -1);

// The entry's first sentence, for a tooltip
std::string GlossaryFirstSentence(const GlossaryEntry &Entry);

// The entry as the explain tool gives it: the term, its other names, the text, what Hear it plays and the terms to
// see too
std::string GlossaryText(const GlossaryEntry &Entry);

// The entry for an effect as the pattern editor shows it on a channel (an effect_id, the FDS's and the 5B's H, I and J
// by the channel's chip); "" for none
std::string EffectGlossaryId(int Effect, const ChannelInfo &Channel);

// The labels in FamiTracker's windows that explain themselves with the glossary (GlossaryTips.cpp): the part of the
// window ("song settings", "instrument editor", "mixer", "piano roll"), the label as it shows and its entry's id
struct GlossaryLabel {
	const char *Where;
	const char *Label;
	const char *Id;
};
const std::vector<GlossaryLabel> &GlossaryLabels();
// A label's entry there (its text as shown, case aside), "" for none
std::string GlossaryLabelId(const std::string &Where, const std::string &Label);

// Hear it: short examples played on the song's channels, a note at a time as the note keys play them, each channel
// with a plain instrument of its chip (no sequences: the volume column and the effects shape the notes)
enum hear_voice_t {
	HEAR_PULSE1, HEAR_PULSE2, HEAR_TRIANGLE, HEAR_NOISE,	// the 2A03's
	HEAR_VRC6_PULSE, HEAR_SAWTOOTH, HEAR_MMC5, HEAR_N163, HEAR_FDS, HEAR_VRC7, HEAR_5B,
	HEAR_HERE,		// the channel an effect is on, with its note's own instrument (EffectHearExample)
	HEAR_VOICES
};

const int HEAR_HALT = -1;		// a Note: the channel stops
const int HEAR_NONE = -2;		// a Note: no new note, only the volume or the effect

struct HearEvent {
	int At;			// ms from the start
	int Voice;		// hear_voice_t
	int Note;		// a pitch (C-0 = 0; the noise's 0-15, higher brighter), HEAR_HALT or HEAR_NONE
	int Volume;		// 0-15, or -1 to leave it
	int Effect;		// an effect_id in the first effect column (EFF_NONE for none) and its parameter
	int Param;
};

struct HearExample {
	std::string Id;
	std::string Says;			// what it plays, in a sentence
	std::string Chip;			// the expansion chip its channels need ("VRC6", "VRC6 Plus", "MMC5", "N163", "FDS",
								// "VRC7", "5B"), "" for the 2A03 alone
	std::vector<HearEvent> Events;	// in time order; every voice ends halted
	int Length() const { return Events.empty() ? 0 : Events.back().At; }
};

// The example of that id; false when there's none
bool GlossaryExample(const std::string &Id, HearExample &Out);
std::vector<std::string> GlossaryExampleIds();

// An effect as written in the pattern editor, heard with its own numbers: the note plain, then with the effect,
// on the channel it's on (HEAR_HERE) at Pitch (the note it plays there) and Volume. False with why when it can't be
// heard that way (the song's own effects: Fxx, Bxx, Dxx, Cxx)
bool EffectHearExample(int Effect, int Param, const ChannelInfo &Channel, int Pitch, int Volume, HearExample &Out,
	std::string &Why);

// The words the sound browser and the chips use for a chip the song doesn't have: how to hear it there
std::string HearChipHow(const std::string &Chip);

}	// namespace ftapi

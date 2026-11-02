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

// Flavours (feat-flavours): a second axis beside a song's kind. The kind says the mood and the form (cozy, mysterious,
// adventure...); the flavour says the place's material and colour (cold, hot, wet, mechanical...): which of the kind's
// sounds, chord and bass styles, grooves and tempos the song leans to, a lead's pulse width and vibrato, the chords'
// colours, an ambience on the noise and the drums' kit. A flavour is guidance drawn into the song's pools, never a
// preset: it weighs the entries it leans to more (FLAVOUR_WEIGHT), so every entry of a kind's pool stays possible, any
// flavour goes with any kind (a cold cozy shop, a hot mysterious cave), and another seed still gives other picks.
// Plain C++14, tested on Linux.

#include <string>
#include <vector>

namespace ftapi {

// Append only (what_next's JSON and the panels name them by Name, not number)
enum flavour_id { FLAVOUR_NONE, FLAVOUR_COLD, FLAVOUR_HOT, FLAVOUR_WET, FLAVOUR_MECHANICAL, FLAVOUR_NATURE, FLAVOUR_SPOOKY,
	FLAVOUR_AIRY, FLAVOUR_ANCIENT, FLAVOUR_MAGICAL, FLAVOUR_COUNT };

// How much more often a pool's entry the flavour leans to is picked than another (each entry stays possible)
const int FLAVOUR_WEIGHT = 3;

struct FlavourInfo {
	int Id;					// flavour_id
	const char *Name;		// "cold"
	const char *Aliases;	// other words ParseFlavour takes as its name, a space between ("icy frozen")
	const char *Words;		// words about a place (about, a track's name) that pick it, a space between
	const char *Pairs;		// two words together, a comma after each: " mine cart, steam engine,"
	const char *Material;	// what it sounds like, for the replies: "ice and snow: thin, glassy, bell-like sounds, high and sparse"
	// What it leans to among a kind's pools ('|' between; entries a kind's pool lacks are left out, never added)
	const char *Leads;		// design_instrument's kinds of lead
	const char *ChordStyles;
	const char *BassStyles;
	const char *Grooves;	// add_drums's grooves ("" leans to none)
	int TempoLean;			// -1 slower, +1 faster, 0 neither: within the kind's tempos (FlavourTempos)
	// What it gives the lead on a pulse channel, when the kind's own call doesn't say (0: nothing)
	const char *LeadDuty;	// design_instrument's duty: "12.5%"
	const char *LeadVibrato;// design_instrument's vibrato: "none", "faint", "gentle"
	const char *LeadWords;	// what that does, for the step's why: "thin and glassy, without a vibrato"
	const char *Intensity;	// add_drums's intensity where the kind's is the normal one (0: the kind's)
	bool NoiseKit;			// drums on the noise alone (no kit samples), clanks and ticks
	bool Colors;			// the chords' rich colours once the tune is there (polish touches colors), on a kind written plain
	int Progression;		// 0, or a chord borrowed into the kind's progression (FlavourProgression): 1 spooky, 2 ancient
	const char *Ambience;	// add_ambience's kinds it leans to, '|' between ("" none)
	const char *Recipe;		// the recipe's line: its sounds, harmony, tempo, ambience and drums in a few words
	const char *Listen;		// what to listen for once it's finished
};

const FlavourInfo &FlavourOf(int Flavour);
// A flavour as asked ("cold", "Icy", "none": FLAVOUR_NONE); false when it's none of them
bool ParseFlavour(const std::string &Text, int &Flavour);
// The flavours' names, "cold, hot, wet, ... or magical"
std::string FlavourNames();

// The flavour words about a place ask for: the one most of the words name, the first of the table on a tie
// (FLAVOUR_NONE for none); Words: the words that named it
struct FlavourWords {
	int Flavour;
	std::vector<std::string> Words;
	FlavourWords() : Flavour(FLAVOUR_NONE) {}
};
FlavourWords ReadFlavourWords(const std::string &Text);
// "Flavour: cold (from the words ice, snow): ice and snow: ... Pass flavour "cold" to what_next, song_shape, get_recipe and
// finish_song." Where: "the words", "the track's name" ("" for a flavour given: no "from")
std::string FlavourLine(int Flavour, const std::vector<std::string> &Words, const std::string &Where);

// Whether the flavour leans to a pool's entry (one of the flavour's own list Leans: "a|b|c")
bool FlavourLeansTo(const char *Leans, const std::string &Entry);
// A weighted pick of a pool's entries: an entry Leans names counts FLAVOUR_WEIGHT times; without a seed (0) the first such
// entry, else the pool's first. Leaned: the entry picked is one Leans names. With Leans null or empty: the pool's own pick
// (Hash % size), so a song without a flavour keeps its picks
std::string WeightedPick(const std::vector<std::string> &Pool, const char *Leans, unsigned int Hash, bool Seeded, bool &Leaned);

// A kind's tempo pool with the flavour's lean (TempoLean): the pool's entries, and one even tempo (900 / speed BPM, rows
// of whole frames at tempo 150) a speed past its slowest or fastest within the kind's range; Leans: the entries on the
// flavour's side of the kind's own (Own, the pool's first)
void FlavourTempos(const std::vector<std::string> &Pool, int Lean, double Slowest, double Fastest, std::vector<std::string> &Out,
	std::string &Leans);

// The chords of a kind's progression ("| i | VII | III | VII | i | VII | v | VII |") with the flavour's borrowed chord: spooky
// the Neapolitan (bII) before a dominant in minor, the minor iv for the first IV in major; ancient bVII for a V inside a
// major progression. The progression as it was for the other flavours (or where nothing fits)
// Pedal: the bass holds the tonic under the bars (bass_style pedal), where spooky's bII would grind a semitone against it
// (the bII's root over the tonic: a mysterious spooky song's F over its E pedal, clashes no polish could clear), so it
// borrows none there
std::string FlavourProgression(int Flavour, const std::string &Progression, bool Minor, bool Pedal = false);

// The ambience the flavour leans to for a song (add_ambience's kinds by name: "wind"), picked by Hash among its kinds; ""
// for none
std::string FlavourAmbience(int Flavour, unsigned int Hash);

// The recipe's lines for a flavour (Core: one short line for the small set); "" for none
std::string FlavourRecipe(int Flavour, bool Core);

}	// namespace ftapi

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

// Grooves to start from: chords, a bass line and drums for a mood (town,
// overworld, battle, dungeon...), each part played the way that mood is
// usually arranged. The make_groove tool writes them into the song, and so
// does FamiTracker's Edit > Start from a groove (GrooveDlg.cpp, through that
// tool); MakeGrooveParts makes the parts alone, as notes, for anything else
// that lays a groove down. Plain C++14, independent of MFC (the parts are
// made in Tools.cpp with the tools' arranging, so every caller gets the same
// groove), tested on Linux.

#include "Arrange.h"
#include "Song.h"

#include <string>
#include <vector>

namespace ftapi {

// A mood a groove can be in
struct GrooveMood {
	std::string Name;		// "overworld", as make_groove takes it
	std::string Use;		// what it's for: "setting out on an adventure"
	int Bpm;				// the tempo it suits
	bool Minor;				// in a minor key
	GrooveMood() : Bpm(120), Minor(false) {}
};

// make_groove's moods, in its order
std::vector<GrooveMood> GrooveMoods();

// A mood by its name or another word for it ("village", "fight", "spooky",
// "jazzy"), ignoring case and spaces; false if none
bool FindGrooveMood(const std::string &Word, GrooveMood &Out);

// The key a groove for a song starts in (FamiTracker's Start from a groove):
// the song's own key in the mood's mode (Minor), which is the key itself or
// its relative minor or major, with the same notes (G major's is E minor;
// another mode counts as the major key with its notes, D dorian as C major,
// and a harmonic minor as its minor); for a song without a key (pSongKey
// NULL) C major or A minor, the white keys
Key GrooveKey(const Key *pSongKey, bool Minor);

// What a groove is made for
struct GrooveRequest {
	std::string Mood;			// a mood's name or another word for it
	Key K;						// the key: as given with KeyGiven, otherwise its key note in the mood's mode
	bool KeyGiven;
	std::vector<BarSpan> Bars;	// the bars it plays, in song rows (SongBars in Transcribe.h lays them out)
	int RowsPerBeat;
	double Bpm;					// its tempo; 0 for the one the mood suits
	int FrameRate, Speed, SplitPoint;	// the song's, for FamiTracker's speed and tempo at that tempo
	bool Chords, Bass, Drums;	// the parts to make
	// The channels the chords and the bass go on (their kind and chip set the register and the chords'
	// loudness; the chords of a lush mood are lush only on a 2A03, MMC5 or VRC6 pulse or a 5B channel, and
	// held as the 0xy arpeggio elsewhere): by default the 2A03's second pulse and the triangle
	ChannelInfo ChordChannel, BassChannel;
	// A melody written there already, on MelodyChannel: the chords keep under it and their colors off its notes
	std::vector<NoteInfo> Melody;
	ChannelInfo MelodyChannel;
	int PhraseStart;			// bars of a four-bar phrase before the first one, so drum fills land where phrases end
	bool Samples;				// a sampled kick, snare and toms (for the DPCM channel), the hi-hats on the noise
	GrooveRequest();
};

// The groove, as notes for the caller to give instruments and write
struct GrooveParts {
	GrooveMood Mood;
	Key K;
	std::vector<ChordAt> Chords;	// the mood's four-bar progression on the bars, repeating
	std::string ChordText;			// "| C | Bb | F | C |"
	int ChordStyle;					// the chord_style they play in (Arrange.h)
	// The preset that plays the chords ("pluck", "soft chords", "warm pad"); for lush chords ("lush chords")
	// each note's Arpeggio (its chord's notes) makes an instrument of that sound
	std::string ChordSound;
	std::vector<PartNote> ChordNotes;	// their instruments left out (NO_INSTRUMENT)
	std::vector<PartNote> BassNotes;	// the "bass" preset plays them
	std::vector<DrumHit> Drums;		// each with the preset that plays it (DrumHit::Sound)
	int DrumsLoudest;				// the volume their loudest hit plays at (GrooveLoudest: softer for calm grooves)
	double Bpm;						// the tempo, and FamiTracker's speed and tempo for it as set_tempo picks them
	int Speed, Tempo;
	std::string Summary;			// in words: "arpeggio (tresillo) chords, an octaves bass and basic drums"
	GrooveParts() : ChordStyle(0), DrumsLoudest(15), Bpm(0.0), Speed(6), Tempo(150) {}
};

// The parts of a groove. False with the reason in Error: no such mood, no
// bars, or no part asked for.
bool MakeGrooveParts(const GrooveRequest &Request, GrooveParts &Out, std::string &Error);

}	// namespace ftapi

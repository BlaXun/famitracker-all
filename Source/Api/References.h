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

// The user's reference library for the Claude Code connection: tracks of NSF
// files that study_nsf studied (Source/Nsf/Study.h), kept as JSON files in the
// library folder (Documents\FamiTracker references), so that new music can
// lean on how they're made. A reference holds the study in words (tempo and
// form, the harmony, each channel's role, register and rhythm, the drums'
// grooves, the tricks between channels, how the sounds are shaped) and the
// track's main sounds with their sequences, which reference_sound adds to a
// song as instruments. The melodies aren't kept: music written from a
// reference takes its craft, not its tunes. Also the built-in guide to the
// classic NES styles, from studies of Konami's, Capcom's and Nintendo's
// soundtracks.

#include <string>
#include <vector>
#include "Presets.h"

namespace nsf {
struct NsfFile;
struct TrackStudy;
}

namespace ftapi {

struct ReferenceSequence {
	std::vector<int> Items;		// empty when the sound doesn't use it
	int Loop, Release;			// -1 for none
	bool Fixed;					// an arpeggio of fixed notes rather than steps from the note
	ReferenceSequence() : Loop(-1), Release(-1), Fixed(false) {}
};

struct ReferenceSound {
	std::string Channel;		// "Pulse 1"
	std::string Role;			// "the lead", "bass", "snare"
	std::string Words;			// its shape in words
	std::string Chip;			// whose instrument it was: "2A03", "VRC6", "N163" or "FDS"
	bool Noise, Triangle, Sawtooth;
	int Share;					// percent of its channel's notes
	ReferenceSequence Sequences[5];	// volume, arpeggio, pitch, hi-pitch, duty (SEQUENCE_*)
	ReferenceSound() : Noise(false), Triangle(false), Sawtooth(false), Share(0) {}
};

struct Reference {
	std::string Id;				// "bucky-o-hare-08": its file's name without ".json"
	std::string Title, Game, Composer, Copyright;
	int Track;					// in the NSF, from 1
	std::string Source;			// the NSF's path
	std::string Added;			// "2026-10-21"
	std::string Chips;			// the NSF's expansion chips, "" for none
	std::vector<std::string> Tags;
	std::string Notes;			// what the user or Claude noted about it
	double Bpm;
	std::string Key;			// "E minor", "" when unclear
	int LoopBars;				// 0 when it doesn't loop
	std::string Study;			// the study in words
	std::vector<ReferenceSound> Sounds;
	Reference() : Track(0), Bpm(0), LoopBars(0) {}
};

// "bucky-o-hare-08"
std::string ReferenceId(const std::string &Game, int Track);
// A reference from the study of track Track (from 0) of an NSF, with tags
// found from it (tempo, mode, chips, techniques)
Reference MakeReference(const nsf::NsfFile &File, int Track, const nsf::TrackStudy &Study, const std::string &Source,
	const std::string &Date);

// As the library keeps it: indented JSON
std::string ReferenceFile(const Reference &R);
bool ReadReferenceFile(const std::string &Text, Reference &Out, std::string &Error);

// One line: "bucky-o-hare-08: Center of Magma Tanker (Bucky O'Hare, track 8),
// 125 BPM, E minor, a 20-bar loop [konami, fast, minor, echo]"
std::string ReferenceLine(const Reference &R);
// All of it: the study, then the sounds numbered with their sequences, the
// tags and the notes
std::string ReferenceText(const Reference &R);
// How well a reference matches the words of a search, 0 for not at all:
// tags, title, game and composer count most, then the study's words
int ReferenceMatch(const Reference &R, const std::string &Query);

// One of a reference's sounds as an instrument preset, named Name, with the
// duty translated for the other chip (2A03 or VRC6)
Preset ReferencePreset(const ReferenceSound &Sound, const std::string &Name);
// A short name for an instrument from a sound: "Magma Tanker lead"
std::string ReferenceSoundName(const Reference &R, const ReferenceSound &Sound);

// The built-in guide to the classic NES styles: what's in it, a section, or
// the sections matching words
std::string StyleGuide(const std::string &Topic = std::string());
std::vector<std::string> StyleGuideTopics();

}	// namespace ftapi

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
// library folder (References, next to FamiTracker.exe), so that new music can
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
struct TrackerSong;
struct Module;
}

namespace ftapi {

struct FdsSound;
struct WaveSound;

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
	ReferenceSequence Sequences[5];	// volume, arpeggio, pitch, hi-pitch, duty (SEQUENCE_*); an FDS sound's volume 0-32
	std::vector<int> FdsWave;	// an FDS sound's wave (64 samples, 0-63) and modulation table (32 steps, 0-7); empty for
	std::vector<int> FdsMod;	// the others' and in references studied before these were kept
	int ModSpeed, ModDepth, ModDelay;	// its modulator
	std::vector<std::vector<int> > N163Waves;	// an N163 sound's waves (1-16 of 4-32 samples, 0-15) and the sequence that
	ReferenceSequence N163WaveSteps;	// picks among them (empty for one wave); empty for the others' and in references
										// studied before these were kept
	ReferenceSound() : Noise(false), Triangle(false), Sawtooth(false), Share(0), ModSpeed(0), ModDepth(0), ModDelay(0) {}
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

// A reference from the study of a tracker module (MOD, S3M, XM or IT) or a
// MIDI file, from Source, its path: no game or track, tagged with its format
// and "tracker-module" (a MIDI file's with "midi", its meter when it isn't
// 4/4, and its copyright kept), and no sounds (its sounds are samples, or
// General MIDI's)
std::string ModuleReferenceId(const std::string &Title, const std::string &Format);
Reference MakeModuleReference(const nsf::TrackerSong &Song, const nsf::TrackStudy &Study, const std::string &Source,
	const std::string &Date);

// A reference from the study of track Track (from 0) of a FamiTracker module
// (.ftm) at Source: its title (or the file's name) as the game, the track's
// name as the title, its author as the composer, its chips, and its sounds
// with their sequences as written, tagged "famitracker"
Reference MakeFtmReference(const nsf::Module &M, int Track, const nsf::TrackStudy &Study, const std::string &Source,
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
// duty translated for the other chip (2A03 or VRC6); an FDS sound's volume
// counted 0-15, its pitch sequence as the 2A03's period moves (four times
// coarser, turned around), and a duty as bright as its wave
Preset ReferencePreset(const ReferenceSound &Sound, const std::string &Name);
// One of a reference's sounds as an FDS instrument, named Name: an FDS
// sound's own wave, modulator and sequences; another chip's played on the
// FDS, its volume counted 0-31, its pitch sequence four times finer and
// turned around, and a wave from its channel (its duty's pulse, a sawtooth or
// a triangle). How says in words what wave it plays.
FdsSound ReferenceFdsSound(const ReferenceSound &Sound, const std::string &Name, std::string &How);
// One of a reference's sounds as an N163 wave sound (Wavetables.h; the VRC6 Plus's soft layer plays them), named Name:
// an N163 sound's own waves and wave sequence, each wave resampled to the 16 samples that play in tune; another
// chip's with a wave from its channel (its duty's pulse, a wave for each duty its duty sequence steps through, the
// triangle, the sawtooth, an FDS sound's wave resampled). Its volume counted 0-15 (an FDS sound's from 0-32, a
// triangle's as it plays: full or silent), its arpeggio as it was, its pitch and hi-pitch sequences in the N163's
// steps, which move a frequency (a 2A03 or VRC6 sound's turned around, alike around A-3, 440 Hz, with four channels;
// an FDS sound's four times coarser). How says in words what it plays.
WaveSound ReferenceWaveSound(const ReferenceSound &Sound, const std::string &Name, std::string &How);
// A short name for an instrument from a sound: "Magma Tanker lead"
std::string ReferenceSoundName(const Reference &R, const ReferenceSound &Sound);

// The built-in guide to the classic NES styles: what's in it, a section, or
// the sections matching words
std::string StyleGuide(const std::string &Topic = std::string());
std::vector<std::string> StyleGuideTopics();
// One section by its topic's name ("calm"), its title then its text; empty
// when there's none
std::string StyleGuideTopic(const std::string &Name);

}	// namespace ftapi

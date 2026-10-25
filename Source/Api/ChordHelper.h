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

// The piano roll's chord helper (PianoRoll.cpp, PianoRollChords.cpp): a
// chord, or a progression typed as names a chord a bar, spread across the
// channels that play nothing in some bars, a held voice on each, voiced
// together under the melody and over the bass as voiced chords are
// (VoicedChordParts in Arrange.h), each with a sound for its chip: on the
// N163 (the VRC6 Plus's soft layer) every voice gets an instrument of its
// own, since two channels playing one share its wave's place in the chip's
// memory. Where too few channels are free, the chord can go on fewer voices,
// or on one channel as the arpeggio (0xy) of a held note. Pitches are
// FamiTracker's (C-3 is middle C), rows count from the song's first row.
// Plain C++14, independent of MFC, so it is tested on Linux
// (tests/ChordHelperTests.cpp).

#include "Arrange.h"
#include "Chords.h"
#include "Music.h"

#include <string>
#include <vector>

namespace ftapi {

struct FdsSound;

// A channel of the song as the helper sees it
struct HelperChannel {
	std::string Name;			// as FamiTracker names it, for messages: "Pulse 1", "VRC6 Pulse 2", "Soft 1"
	std::string Chip;			// "2A03", "MMC5", "VRC6", "VRC7", "FDS", "N163", "5B"
	int Kind;					// channel_kind
	bool Soft;					// on the VRC6 Plus's soft layer (an N163 channel)
	int EffectColumns;			// visible, 1-4
	std::vector<Cell> Cells;	// from the song's first row, the song's rows of them
	HelperChannel() : Kind(KIND_PULSE), Soft(false), EffectColumns(1) {}
};

// An instrument of the song
struct HelperInstrument {
	int Index;					// 0-63
	std::string Name;
	std::string Chip;			// the chip it plays on: "2A03" (the MMC5's too), "VRC6", "VRC7", "FDS", "N163", "5B"
	bool Releases;				// its notes end with a release (===) rather than a halt (---)
	int WavePos, WaveSize;		// an N163 instrument's wave in the chip's memory, in samples; -1 for the others
	HelperInstrument() : Index(0), Releases(false), WavePos(-1), WaveSize(-1) {}
};

struct HelperSong {
	std::vector<HelperChannel> Channels;	// the module's, in its columns' order
	std::vector<HelperInstrument> Instruments;
	int PatternLength, Frames;
	int FirstHighlight, SecondHighlight;	// rows a beat and a bar, as the row highlight says (Timing mends nonsense)
	Key K;						// the track's key, chosen or found from its notes...
	bool HasKey;				// ...when it has one (chords written as degrees need it)
	int N163Channels;			// the N163's channels in the module: its waves have the memory they leave
	HelperSong() : PatternLength(64), Frames(1), FirstHighlight(4), SecondHighlight(16), HasKey(false), N163Channels(0) {}
	Timing Time() const { return Timing(PatternLength, Frames, FirstHighlight, SecondHighlight); }
	int Rows() const { return PatternLength * Frames; }
};

// What the helper is asked for
struct ChordRequest {
	std::string Chords;			// a chord ("Am7", "C/E", or a degree of the key: "vi") or a chord a bar ("C G Am F",
								// "| C | Am F |", as LayOutProgression reads them)
	int First, End;				// the rows it goes on, widened to whole bars
	bool Grow;					// chords for more bars than that go on over the bars after, as far as Limit
	int Limit;					// the rows the chords may reach (the song's, or a frame more); 0: the song's
	int Voices;					// 2-4, or 0: as many as the chords have notes (3 for triads, 4 with a seventh or a color)
	bool Arpeggio;				// on one channel, as the arpeggio (0xy) of held notes, instead of voices
	std::vector<int> Prefer;	// channels that take the voices first while they're free (the last chord's, lowest first)
	std::vector<ChordAt> LeadFrom;	// the chords the voices on Prefer play just before First, to lead from
	ChordRequest() : First(0), End(0), Grow(false), Limit(0), Voices(0), Arpeggio(false) {}
};

// What a voice plays
struct VoiceSound {
	int Instrument;				// the song's instrument, or -1: Sound is made into one
	std::string Sound;			// a preset's name ("soft chords") or, on the N163, a wave sound's ("string pad")
	std::string Name;			// the new instrument's name, when it isn't the sound's own: an N163 voice's ("String pad 2")
	int WavePos;				// where a new N163 instrument's wave goes in the chip's memory (samples); -1 otherwise
	VoiceSound() : Instrument(-1), WavePos(-1) {}
};

struct ChordVoice {
	int Channel;
	VoiceSound Sound;
	std::vector<PartNote> Notes;	// as written on the channel, without an instrument
	ChordVoice() : Channel(-1) {}
};

struct ChordPlan {
	int First, End;					// the rows it writes
	std::vector<BarSpan> Bars;
	std::vector<ChordAt> Chords;	// on Bars
	std::vector<int> Free;			// the channels free there that can take a voice, in the order they take them
	int Wanted;						// the voices the chords want (2-4)
	bool TooFew;					// fewer free channels than Wanted, and Voices wasn't asked for: Voices has those there are
	bool Arpeggio;					// on one channel as arpeggios
	std::vector<ChordVoice> Voices;	// lowest first; one for an arpeggio; none when there's nothing to write
	int Melody, Bass;				// the channels heard playing the melody and the bass there, -1 for none
	std::vector<std::string> Remarks;	// what the user should know: chords left out, a voice that found no sound
	std::string Error;				// why nothing can be written; empty when something can
	ChordPlan() : First(0), End(0), Wanted(3), TooFew(false), Arpeggio(false), Melody(-1), Bass(-1) {}
};

// Can a voice of a chord go on this channel? A melodic one with a volume of its own: not the triangle, the noise or the DPCM
bool TakesVoice(const HelperChannel &Channel);

// The FDS sound voices of Sound (a preset's name, "soft chords") play on the FDS: the one of that name or word, else the one
// most like that preset (as the tools pick it), else the first
const FdsSound *VoiceFdsSound(const std::string &Sound);

// Does anything sound on the channel in rows [First, End)? A note that starts before and still sounds there counts.
bool PlaysIn(const HelperChannel &Channel, int First, int End);

// The channels that can take a voice and play nothing in [First, End), in the order voices go to them: the VRC7's, the
// N163's (the VRC6 Plus's soft layer), the 5B's, the pulses, the FDS's, then the sawtooth; in each, from the left
std::vector<int> FreeChannels(const HelperSong &Song, int First, int End);

// Rows [From, To) widened to whole bars, as the row highlight counts them (a bar at least)
void WholeBars(const HelperSong &Song, int From, int To, int &First, int &End);

// How many voices chords want: as many notes as the fullest of them has, 2-4
int VoicesWanted(const std::vector<ChordAt> &Chords);

// The chords of Request on the song: their bars, the free channels, the voices (or the arpeggio) with their notes and
// sounds. False, with Plan.Error, when nothing can be written: chords that don't read, no free channel. Too few free
// channels for the voices wanted is no error: Plan.TooFew says so, and Plan.Voices has as many voices as channels are
// free (none with only one: then only the arpeggio can go there, which Request.Arpeggio asks for).
bool PlanChords(const HelperSong &Song, const ChordRequest &Request, ChordPlan &Plan);

// A channel's new cells, from Row on
struct ChannelCells {
	int Channel;
	int Row;
	std::vector<Cell> Cells;
	ChannelCells() : Channel(-1), Row(0) {}
};

// The cells the plan writes into its voices' channels, voice v playing the song's instrument Instruments[v] (Song's
// instruments say which end their notes with a release), as the tools write parts (BuildPart): what follows each voice
// plays on as before, and a voice held to the end of a song that loops stops on the last row, rather than ringing on
// into the loop on a channel that has nothing there
std::vector<ChannelCells> PlanCells(const HelperSong &Song, const ChordPlan &Plan, const std::vector<int> &Instruments,
	std::vector<std::string> &Warnings);

// The plan in words, for the piano roll's status line: "Am on bar 5", "C G Am F on bars 5-8"
std::string PlanChordsText(const HelperSong &Song, const ChordPlan &Plan);
// "A-3, C-4 and E-4 on Pulse 1, Pulse 2 and VRC6 Pulse 2", "an arpeggio (037) on Pulse 2"
std::string PlanVoicesText(const HelperSong &Song, const ChordPlan &Plan);

}	// namespace ftapi

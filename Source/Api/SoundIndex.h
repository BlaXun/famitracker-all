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

// Every sound a song can get, in one list: the ready-made sounds for each
// chip it has on (the presets of Presets.h for the 2A03, MMC5, VRC6 and 5B,
// the 5B's own, the VRC7's patches and custom patches, the N163's wave sounds
// of Wavetables.h and the FDS's of FdsSounds.h), the sounds of the user's
// reference library (each study's, as reference_sound makes them), and the
// song's own instruments. Each says what kind of sound it is, where it comes
// from, the channel it plays on and its words, and warns of what the tools
// warn of: notes that vanish (Tools.cpp's VanishRemark) and a vibrato that
// clicks on the 2A03's pulses (PulseClicks.h). Searched by words and filtered
// by kind, source and channel. FamiTracker's Instrument > Sound browser lists
// them (ToolSet::Sounds) and find_sound searches them. Plain C++14, tested on
// Linux.

#include <memory>
#include <string>
#include <vector>
#include "References.h"
#include "Song.h"

namespace ftapi {

// Where a sound plays: a kind of channel (append only: the browser lists them in this order)
enum sound_target {
	TARGET_PULSE,			// the 2A03's pulses
	TARGET_TRIANGLE,
	TARGET_NOISE,
	TARGET_DPCM,
	TARGET_VRC6_PULSE,
	TARGET_SAWTOOTH,
	TARGET_MMC5,			// its pulses, which play 2A03 instruments
	TARGET_N163,
	TARGET_FDS,
	TARGET_VRC7,
	TARGET_S5B,
	TARGET_COUNT
};

// Where a sound comes from
enum sound_source {
	SOURCE_PRESET,			// FamiTracker's ready-made sounds
	SOURCE_REFERENCE,		// a study in the reference library
	SOURCE_SONG,			// the song's own instrument
	SOURCE_COUNT
};

// How it becomes an instrument
enum sound_make {
	MAKE_NONE,				// the song's own: it is one
	MAKE_PRESET,			// a preset (Key) for Chip: "2A03", "VRC6" (Sawtooth for the sawtooth's) or "5B"
	MAKE_5B,				// one of the 5B's own sounds (Key)
	MAKE_VRC7,				// a VRC7 patch (Number, 1-15)
	MAKE_VRC7_CUSTOM,		// a VRC7 custom patch (Key)
	MAKE_N163,				// an N163 wave sound (Key)
	MAKE_FDS,				// an FDS sound (Key)
	MAKE_REFERENCE			// a reference's sound (Reference, Number from 1) for Chip, as reference_sound makes it
};

struct SoundEntry {
	std::string Name;		// the instrument it becomes: "Soft lead", "Bass (saw)", "Magma Tanker lead"
	std::string Kind;		// a SoundKinds() id: "lead", "bass", ...
	int Source;				// sound_source
	int Target;				// sound_target
	int Channel;			// the song's channel it's heard on: the first of its kind (a song's instrument's, the one it
							// plays most on)
	std::string ChannelName;	// that channel's: "Pulse 1", "VRC6 Sawtooth"
	std::string Words;		// what it sounds like: a ready-made sound's description, a study's words for it
	std::string Origin;		// "preset", "VRC7 patch 3", "Bucky O'Hare, track 8 (sound 2: Pulse 1, the lead)", "instrument 03"
	bool Home;				// listed when no channel is asked for: each sound once, where it's meant to play
	bool Low;				// a bass, heard low
	bool Drum;
	int InSong;				// the song's instrument of its name for its chip, -1 for none (for the song's own, itself)
	int Heard;				// the frames a note of it at full volume is heard for (60 when it lasts)
	std::vector<int> ClickPitches;	// on a 2A03 or MMC5 pulse: the pitches its vibrato clicks on held
	std::string Warning;	// both in words, empty for none
	// Making it
	int Make;				// sound_make
	std::string Chip;		// the instrument's: "2A03", "VRC6", "5B", "VRC7", "N163" or "FDS"
	bool Sawtooth;
	std::string Key;		// the ready-made sound's name; the reference's id
	int Number;				// the VRC7 patch, the reference's sound (from 1), the song's instrument
	std::shared_ptr<const ReferenceSound> Reference;	// MAKE_REFERENCE: the sound, and the name it's made under
	std::string Short;		// (ReferenceSoundName; Name has the sawtooth's " (saw)" after it)
	std::vector<int> Pitches;	// the song's own: the pitches it plays on its channel (a few), for hearing a drum
	std::string Extra;		// more words it's found by: a wave or FDS sound's, a reference's id, composer and tags
	std::string Search;		// all its words in lower case, as SoundMatch looks for them (IndexWords)

	SoundEntry() : Source(SOURCE_PRESET), Target(TARGET_PULSE), Channel(-1), Home(false), Low(false), Drum(false), InSong(-1),
		Heard(60), Make(MAKE_NONE), Sawtooth(false), Number(0) {}
};

// The kinds of sound, for the filter: id ("lead"), label ("Leads") and the words that mean it
struct SoundKind {
	std::string Id, Label, Words;
};
const std::vector<SoundKind> &SoundKinds();
// The kind a name or description speaks of ("plucky bass" a bass, "glass bell" a bell), "" for none
std::string KindOfWords(const std::string &Text);
// A kind as asked for ("bells", "Drums", "chords"): its id, "" for none
std::string KindId(const std::string &Asked);

// A kind of channel: "Pulse channels (2A03)", "VRC6 sawtooth"
std::string TargetName(int Target);
// The kind of a song's channel, -1 for one no sound is listed for (the MMC5's voice)
int TargetOf(const ChannelInfo &Channel);
// The instrument chip a kind of channel plays: "2A03" (also the MMC5's), "VRC6", ...
std::string TargetChip(int Target);
// "ready-made", "reference library", "this song"
std::string SourceName(int Source);

// What the song plays an instrument on in the selected track: the channel with most of its notes (-1 when it
// plays none) and the pitches it plays there (a few, from the lowest)
struct SoundUse {
	int Channel;
	int Notes;
	std::vector<int> Pitches;
	std::vector<int> Channels;	// every channel it plays on
	SoundUse() : Channel(-1), Notes(0) {}
};

// Every sound the song can get, for every kind of channel it has: the ready-made sounds of its chips, each
// sound of the references in Library, and its own instruments (Uses: by instrument, 0-63). Cursor is the
// channel the user is on, -1 for none: the ready-made sounds that play there are at home there.
std::vector<SoundEntry> ListSounds(const SongInfo &Song, const std::vector<Reference> &Library,
	const std::vector<SoundUse> &Uses, int Cursor);

// What to look for
struct SoundQuery {
	std::string Words;		// "warm bell", a game, a reference's title; empty for all
	std::string Kind;		// a SoundKinds() id, empty for any
	int Source;				// sound_source, -1 for any
	int Target;				// sound_target, -1 for each sound once, where it's at home
	SoundQuery() : Source(-1), Target(-1) {}
};
// How well a sound matches words, 0 for not at all: every word must be found (or, not Every, one at least; a
// word matches the start of one of the sound's, so "pluck" finds "plucky"), in its name most, then its kind,
// origin and words
int SoundMatch(const SoundEntry &Sound, const std::string &Words, bool Every = true);
// Search set to the sound's words as they are now (ListSounds does it; after changing a sound, again)
void IndexWords(SoundEntry &Sound);
// The sounds that match, as indices into All: best first with words, otherwise by kind, then source. When no
// sound has every word, the ones with some of them, and Some says so
std::vector<size_t> FindSounds(const std::vector<SoundEntry> &All, const SoundQuery &Query, bool *pSome = 0);

// The notes a preview plays: ms from its start, and FamiTracker's pitch (C-0 = 0; a noise pitch 0-15 on the
// noise), PREVIEW_RELEASE or PREVIEW_HALT. A melody up and back, low for a bass, a beat for a drum; the FDS's
// two octaves up and the 5B's one, where a pulse would play them
const int PREVIEW_RELEASE = -1, PREVIEW_HALT = -2;
struct PreviewStep {
	int At;
	int Note;
};
std::vector<PreviewStep> PreviewSteps(const SoundEntry &Sound);

// A sound's sequences as FamiTracker reports an instrument made from them (InstrumentInfo, as GetSong gives
// it): its volume, arpeggio, pitch and hi-pitch, and a 5B instrument's noise / mode; an FDS sound's volume
// counted 0-15 (FdsVolumeAs15)
InstrumentInfo SequencesInfo(const std::vector<PresetSequence> &Sequences, const std::string &Chip);

// From Tools.cpp's check for notes that vanish (check_song, VanishRemark): the frames a note of the
// instrument at full volume is heard for (up to Limit), and the warning the tools give when that's four
// or fewer (empty otherwise): with the advice for the tools, or for someone choosing a sound
int SoundHeardFrames(const InstrumentInfo &Info, int Limit);
std::string VanishWarning(int Heard, bool ForTools);
bool SoundVanishes(int Heard);

}	// namespace ftapi

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

// A FamiTracker module as data: macros, DPCM samples, instruments for each
// chip and tracks of patterns, written out as FamiTracker 0.4.6's text
// format (what File > Import text reads, CTextExport::ImportFile).

#include <cstdint>
#include <string>
#include <vector>

namespace nsf {

// FamiTracker's expansion chip numbers (SNDCHIP_*)
enum expansion_t { EXP_NONE = 0, EXP_VRC6 = 1, EXP_VRC7 = 2, EXP_FDS = 4, EXP_MMC5 = 8, EXP_N163 = 16 };

// The channels of a module with a layout of its own (a tracker module's, for
// a study): notes, or drums
enum layout_kind { LAYOUT_MELODIC, LAYOUT_DRUMS };

// Macro (sequence) types and arpeggio settings
enum seq_type { SEQ_VOLUME, SEQ_ARPEGGIO, SEQ_PITCH, SEQ_HIPITCH, SEQ_DUTY, SEQ_TYPES };
enum arp_setting { ARP_ABSOLUTE = 0, ARP_FIXED = 1, ARP_RELATIVE = 2 };

// Which chip's macro list a macro is in
enum macro_chip { MACROS_2A03, MACROS_VRC6, MACROS_N163 };

struct Macro {
	std::vector<int> Items;
	int Loop;		// -1 for none
	int Release;	// -1 for none
	int Setting;
	Macro() : Loop(-1), Release(-1), Setting(0) {}
	Macro(const std::vector<int> &Values, int LoopPoint = -1, int ReleasePoint = -1, int Arp = 0) :
		Items(Values), Loop(LoopPoint), Release(ReleasePoint), Setting(Arp) {}
	bool operator==(const Macro &o) const { return Items == o.Items && Loop == o.Loop && Release == o.Release && Setting == o.Setting; }
	bool Empty() const { return Items.empty(); }
};

enum inst_kind { INST_2A03, INST_VRC6, INST_VRC7, INST_FDS, INST_N163 };

// A DPCM sample on a key of a 2A03 instrument
struct DpcmKey {
	int Note;		// 0-95, the key (C-0 = 0)
	int Sample;		// index into Module::Samples
	int Pitch;		// 0-15
	bool Loop;
	int Delta;		// the delta counter to start from, -1 to leave it
	DpcmKey() : Note(0), Sample(0), Pitch(15), Loop(false), Delta(-1) {}
};

struct Instrument {
	int Kind;
	std::string Name;
	bool Unused;				// an empty slot between instruments (a FamiTracker module's own numbering has gaps):
								// no instrument, and left out of the text and the studies
	Macro Macros[SEQ_TYPES];	// 2A03, VRC6, N163; FDS uses volume, arpeggio and pitch
	int MacroIndex[SEQ_TYPES];	// 2A03, VRC6, N163: each macro's index in its chip's list (Module::Macros) as a
								// FamiTracker module numbers them; -1 to find it there by its items, or for none
	std::vector<DpcmKey> Keys;	// 2A03
	int Patch;					// VRC7: 0 for the custom patch below, 1-15 built in
	uint8_t Regs[8];
	std::vector<int> FdsWave;	// 64 values 0-63
	std::vector<int> FdsMod;	// 32 values 0-7
	int ModRate, ModDepth, ModDelay;
	int WaveSize, WavePos;		// N163
	std::vector<std::vector<int> > Waves;
	std::string Drum;			// a tracker module's drum (Tracker.h): "kick", "snare", "hi-hat", "cymbal", "tom",
								// "percussion", or "drum" when its name doesn't say; "" for notes
	Instrument() : Kind(INST_2A03), Unused(false), Patch(1), ModRate(0), ModDepth(0), ModDelay(0), WaveSize(32), WavePos(0)
	{
		for (int t = 0; t < SEQ_TYPES; ++t)
			MacroIndex[t] = -1;
		for (int i = 0; i < 8; ++i)
			Regs[i] = 0;
	}
};

// Special notes
enum { NOTE_NONE = -1, NOTE_HALT = -2, NOTE_RELEASE = -3 };

struct Effect {
	char Letter;	// as FamiTracker writes it: 'F', 'B', '0', '4', ... 0 for none
	int Param;
	Effect() : Letter(0), Param(0) {}
	Effect(char L, int P) : Letter(L), Param(P) {}
	bool operator==(const Effect &o) const { return Letter == o.Letter && (Letter == 0 || Param == o.Param); }
};

const int MAX_EFFECTS = 4;

struct Cell {
	int Note;			// NOTE_* or 0-95 (C-0 = 0); on the noise channel 0-15
	int Instrument;		// -1 for none
	int Volume;			// -1 for none
	Effect Effects[MAX_EFFECTS];
	Cell() : Note(NOTE_NONE), Instrument(-1), Volume(-1) {}
	bool Empty() const;
	int EffectCount() const;		// used columns, from the left
	bool AddEffect(char Letter, int Param);	// in the first free column; false when full
	bool operator==(const Cell &o) const;
};

struct Track {
	std::string Name;
	int PatternLength;
	int Speed, Tempo;
	std::vector<int> Columns;					// effect columns per channel, 1-4
	std::vector<std::vector<int> > Frames;		// a pattern number per channel for each frame
	std::vector<std::vector<std::vector<Cell> > > Patterns;	// [channel][pattern][row]
	Track() : PatternLength(64), Speed(6), Tempo(150) {}
};

struct Module {
	std::string Title, Author, Copyright;
	std::vector<std::string> Comment;	// lines
	int Machine;			// 0 NTSC, 1 PAL
	int FrameRate;			// engine speed, 0 for the machine's
	int Expansion;			// expansion_t
	int N163Channels;
	std::vector<Macro> Macros[3][SEQ_TYPES];	// [macro_chip][type]
	std::vector<std::vector<uint8_t> > Samples;
	std::vector<std::string> SampleNames;
	std::vector<Instrument> Instruments;
	std::vector<Track> Tracks;
	// A layout of its own in place of the chips' channels (a tracker module's,
	// Tracker.h): a name and a layout_kind for each channel. Such a module is
	// for studying; it isn't written as FamiTracker text.
	std::vector<std::string> ChannelNames;
	std::vector<int> ChannelKinds;

	Module() : Machine(0), FrameRate(0), Expansion(EXP_NONE), N163Channels(0) {}
	int ChannelCount() const;
	// The macro's index in its chip's list, added when it's new
	int AddMacro(int Chip, int Type, const Macro &M);
};

// The channel names of a module's columns, for the text and reports
std::vector<std::string> ModuleChannelNames(int Expansion, int N163Channels);
// The module's channel names: its own layout's, or its chips'
std::vector<std::string> ChannelNamesOf(const Module &M);

// FamiTracker's text format; ASCII only (other characters become '?')
std::string WriteText(const Module &M);

// Note names as FamiTracker writes them
std::string NoteName(int Note);		// "C#4"

}	// namespace nsf

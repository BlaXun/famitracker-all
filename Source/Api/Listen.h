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

// Listening to a song as it sounds (the listen tool, Tools.cpp's ListenTool).
// FamiTracker renders the mix and each part alone in one pass
// (SongHost::RenderParts), noting the sample where each row starts; this
// compares what each part sounds like with what its notes say it should:
// how each held note's pitch moves (a warble, too deep a vibrato), where it
// holds (out of tune), two parts at one pitch a few cents apart (beating),
// clicks, a part sounding on after its notes, notes its render hardly
// sounds; and in the mix, clipping, jumps in loudness, harsh or muddy
// stretches and the melody covered in its own band. Each finding says in
// plain words what is heard, for someone without music training. Plain
// C++14, tested on Linux (tests/ListenTests.cpp); its types are named apart
// from the tools' (they share namespace ftapi). Its thresholds were measured
// on the soundtrack's renders (each constant says on what). On a long track
// the render takes most of the time: a 149-second track with eight parts took
// 39 s under Wine in all, its listening (this file) about 6 s of it.

#include <string>
#include <utility>
#include <vector>

namespace ftapi {

// What asks a note's pitch to move, besides nothing
enum listen_motion {
	LISTEN_STILL,				// nothing: it should hold its pitch
	LISTEN_VIBRATO_EFFECT,		// 4xy
	LISTEN_VIBRATO_SEQUENCE,	// its instrument's pitch sequence, looping
	LISTEN_MODULATOR			// the FDS's modulator: the instrument's, or Hxx, Ixx and Jxx
};

// A note as written, where it sounds in its part's render
struct ListenNote {
	int Row, Rows;			// song rows: where it starts, and the rows until what ends it (for the reply's places)
	long long Begin, End;	// the samples it sounds in: from where its row starts (and its delay, Gxx) to where the next
							// note, the release or the halt comes, as the song plays; -1 when it isn't in the render
	double Hz;				// where it should sound: the note written as its chip sounds it (its octaves, an N163 wave's
							// cycle) and the detune Pxx asks for; 0 for a sound without a pitch (drums, the 5B's noise)
	double ChipHz;			// where the chip's register puts it (its period table rounds); 0 when that's Hz
	bool Steady;			// nothing written moves its pitch but a vibrato: no slide, portamento, arpeggio or bend
	int Motion;				// listen_motion
	bool Released;			// a release (===) ends it: its sound fades as its instrument's release has it
	bool Halted;			// a halt (---) ends it: its sound stops there
	ListenNote() : Row(0), Rows(1), Begin(-1), End(-1), Hz(0.0), ChipHz(0.0), Steady(true), Motion(LISTEN_STILL), Released(false),
		Halted(false) {}
};

// A part: a channel, rendered alone
struct ListenPart {
	std::string Name;			// "FDS", "VRC6 Pulse 1"
	int Channel;				// the song's channel, from 0
	bool Pitched;				// plays pitches (not the noise or the DPCM)
	bool Smooth;				// its wave has no jumps of its own (the FDS's, the N163's rounder waves, the triangle's), so a
								// jump as a note starts is a pop
	bool Rings;					// it sends to the mixer's space (Space): its sound rings on after its notes by design
	bool Restarts;				// its wave starts over while a note holds when its period's high byte changes (the 2A03's and
								// the MMC5's pulses, as 0.4.6 plays them): a vibrato across a multiple of 256 clicks
	bool Melody;				// it carries the tune
	std::vector<float> Samples;	// its render alone, both sides averaged
	std::vector<ListenNote> Notes;	// in the order they play
	ListenPart() : Channel(0), Pitched(true), Smooth(false), Rings(false), Restarts(false), Melody(false) {}
};

struct ListenSong {
	int Rate;
	std::vector<float> Left, Right;		// the mix
	std::vector<ListenPart> Parts;
	std::vector<long long> Bars;		// the samples where the bars listened to start, the first time through, and where the
										// last ends
	std::vector<bool> SectionStarts;	// per bar there: a section (or a block of four bars, without sections) starts with it,
										// so a change of loudness there is the music's
	long long LoopStart;				// the sample where the loop starts the first time through, -1 when the song halts...
	long long Seam;						// ...and where it starts again: the render goes on past the jump back; -1 when it doesn't
	ListenSong() : Rate(44100), LoopStart(-1), Seam(-1) {}
};

// A held note's pitch as heard (MeasurePitch), in cents from where it should sound (ListenNote::Hz)
struct ListenPitch {
	bool Valid;			// enough of it has a pitch to tell
	double Seconds;		// how long it was measured
	double Tracked;		// the share of that time a pitch was heard, 0-1
	double Steady;		// where it holds: the middle of its pitch outside a wobble (of a wobble, when it wobbles throughout)
	bool Wobbles;		// its pitch swings 12 cents or more either way for two of its cycles or more...
	double Low, High;	// ...between these (cents from Hz)...
	double Swing;		// ...typically this far either way...
	double Rate;		// ...this many times a second (0 when it doesn't repeat evenly)...
	double WobbleFrom;	// ...from this many seconds into the note
	double WobbleSeconds;	// for this long
	double HeldSeconds;		// how long it holds its pitch (before a wobble, or all of it without one)
	ListenPitch() : Valid(false), Seconds(0.0), Tracked(0.0), Steady(0.0), Wobbles(false), Low(0.0), High(0.0), Swing(0.0), Rate(0.0),
		WobbleFrom(0.0), WobbleSeconds(0.0), HeldSeconds(0.0) {}
};

// The pitch of the samples [Begin, End) of a part's render, which should sound Hz: tracked every 5 ms (YIN, the recorder's
// transcribe::TrackPitch), its attack and end left out
ListenPitch MeasurePitch(const std::vector<float> &Samples, int Rate, long long Begin, long long End, double Hz);

// Findings, heard worst first
enum listen_kind {
	LISTEN_WOBBLE,		// a held note's pitch swings: a warble (faster than 8 times a second, wider than 40 cents) or too
						// deep a vibrato (wider than 60 cents)
	LISTEN_TUNING,		// held notes off their pitch
	LISTEN_BEATING,		// two parts at one pitch, or octaves apart, a few cents off each other
	LISTEN_CLICK,		// clicks inside held notes, pops as notes start, a click at the loop's seam
	LISTEN_CLIPPING,	// the mix past full scale
	LISTEN_DRONE,		// a part sounding on where its notes have ended
	LISTEN_UNHEARD,		// written notes the part's own render hardly sounds
	LISTEN_COVERED,		// the melody covered in its own band by another part
	LISTEN_JUMP,		// the loudness jumps where no section starts
	LISTEN_HARSH,		// more bright, edgy sound than the soundtrack's own tracks have
	LISTEN_MUDDY,		// more low-middle boom than they have
	LISTEN_KINDS
};

const char *ListenKindId(int Kind);		// "wobble", as the listen tool's JSON names it
const char *ListenKindTitle(int Kind);	// "Warbles and deep vibratos", for people

struct ListenFinding {
	int Kind;				// listen_kind
	std::string What;		// the kind's case: "warble" or "vibrato"; "inside", "start" or "seam"; "chip" (out of tune by the
							// chip's own tuning) or "sound"; empty otherwise
	int Part;				// into ListenSong::Parts, -1 for the mix
	int Other;				// the other part (beating; covered: the one covering it most; harsh, muddy: the part making most
							// of it), -1 for none
	std::vector<int> Notes;	// the part's notes it's heard on, into ListenPart::Notes
	long long Begin, End;	// where, in samples
	int Severity;			// 3: a listener notices it, 2: worth a listen, 1: a detail
	double Sure;			// how sure the measurement is, 0-1
	std::vector<std::pair<std::string, double> > Numbers;	// by name, as the JSON gives them ("cents_low", "rate")
	std::string Heard;		// what's heard, in plain words
	std::string Summary;	// a few words, for a list beside the song
	ListenFinding() : Kind(0), Part(-1), Other(-1), Begin(0), End(0), Severity(1), Sure(0.0) {}
	double Number(const std::string &Name, double Default = 0.0) const;
	void Set(const std::string &Name, double Value);
};

// What listening measured besides the findings, for the reply and for checking the check against songs
struct ListenStats {
	int NotesMeasured;		// held notes whose pitch was tracked
	int Parts;				// parts listened to
	double Seconds;			// of the song
	struct Measured {
		int Part, Note;		// into ListenSong::Parts and its notes
		ListenPitch Pitch;
		int Clicks;			// heard inside it
		Measured() : Part(0), Note(0), Clicks(0) {}
	};
	std::vector<Measured> Notes;			// each held note measured
	std::vector<double> PartDb;				// each part's usual note: its loudest 10 ms, dB of full scale
	std::vector<double> HarshShares, MudShares;	// each bar's share of power from 2 kHz up, and from 160 to 500 Hz, in dB
	ListenStats() : NotesMeasured(0), Parts(0), Seconds(0.0) {}
};

// Everything heard, worst first (by kind, then severity, then place)
std::vector<ListenFinding> ListenToSong(const ListenSong &Song, ListenStats *pStats = 0);

// Where a note written on a channel sounds, by FamiTracker 0.4.6's note tables (CSoundGen::LoadMachineSettings): Hz, the
// equal-tempered pitch (A-3 440 Hz) as the chip sounds it (the triangle, the VRC7 and the 5B an octave under, the FDS two,
// an N163 wave of Cycle samples a cycle at 16 / Cycle of it), moved by Detune steps of Pxx from P80 (P81 is one step
// sharper: a period a step shorter, or a frequency a step higher); and ChipHz, where the chip's register really puts it
// (its period rounds), 0 when that isn't worked out (the VRC7's, a sound without a pitch). Chip and Kind as the tools'
// channels have them ("2A03", KIND_PULSE...), Channels the N163's count (0: 4)
struct ListenTuning {
	double Hz, ChipHz;
	double CentsPerStep;	// what one step of the register (Pxx's, 4xy's) moves the note, in cents
	ListenTuning() : Hz(0.0), ChipHz(0.0), CentsPerStep(0.0) {}
};
ListenTuning ListenNoteTuning(const std::string &Chip, int Kind, int Pitch, int Detune, int Channels, int Cycle, bool Pal);

// How far 4xy with this depth (1-15) swings a note either way, in cents, as 0.4.6's new vibrato table moves its register
// (CSoundGen::GenerateVibratoTable: 9 steps at depth 6, 2 at depth 2); and the depth that swings it nearest Cents (1-15).
// On the FDS a step is about 1.7 cents at A-5, so 452 is about 3 cents either way there and 456 about 15
double ListenVibratoCents(const std::string &Chip, int Kind, int Pitch, int Depth, int Channels, bool Pal);
int ListenVibratoDepth(const std::string &Chip, int Kind, int Pitch, double Cents, int Channels, bool Pal);

// Words for people: a pitch distance ("a quarter of a semitone", "about a semitone and a half"), and how often ("14 times
// a second", "about once a second")
std::string ListenCentsWords(double Cents);
std::string ListenTimesWords(double PerSecond);

}	// namespace ftapi

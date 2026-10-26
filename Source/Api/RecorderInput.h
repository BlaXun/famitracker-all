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

// What the user gives the recorder by hand (InsertRecording.cpp, RecordDlg.cpp,
// BackingDlg.cpp, GrooveDlg.cpp): chords for Edit > Add chords and bass line
// instead of FamiTracker finding them from the melody, typed by name or
// strummed on a guitar (or played on a piano) and heard, and a tempo tapped
// along with the beat. Names are read as the tools read them (Chords.h), and
// the chords come back in the transcription engine's terms (Transcribe.h),
// whose bass line and arpeggios play them. Plain C++14, independent of MFC,
// tested on Linux.

#include "../Transcribe/Transcribe.h"

#include <string>
#include <vector>

namespace ftapi {

// Chords typed by name ("C G Am F", a chord a bar, or "| C | G | Am F |" with
// bar lines; "Am7", "Gsus4", "C/E", "N.C.", "." holding the chord before, "%"
// repeating a bar, or degrees of the key: "I vi IV V"), laid on Bars: the
// bars repeat the typed ones when there are more of them. The key reads the
// degrees and spells the names: *pKey when given, otherwise the one the
// chords are in (their notes against the key profiles, the first chord's root
// counting most), returned in KeyOut. Names: the chords bar by bar as laid out
// ("| C | G | Am F |"). False with the reason in Error.
bool LayOutTypedChords(const std::string &Text, const std::vector<transcribe::Bar> &Bars, int RowsPerBeat,
	const transcribe::Key *pKey, transcribe::Key &KeyOut, std::vector<transcribe::ChordSpan> &Chords, std::string &Names,
	std::string &Error);

// How many bars typed chords are ("C G Am F": 4, "| C | G F |": 2); 0 when
// there are none
int TypedChordBars(const std::string &Text);

// The chords of a strummed take, bar by bar
struct StrummedChords {
	int FirstRow;				// the row of the bar its first strum is in (song rows)
	int BarCount;				// bars from there to the last strum's
	transcribe::Key Key;		// the key they're in
	std::string Names;			// "| C | G | Am F |", as LayOutTypedChords reads them back
	StrummedChords() : FirstRow(0), BarCount(0) {}
};

// The chords heard in strums (transcribe::HearStrums), each bar's or half
// bar's from the notes sounding there (the lowest one counted as the bass),
// as the tools hear chords in a song's parts. The delay through the take is
// taken out first (where most strums agree they lie past the beats, up to
// three quarters of a beat: a microphone delay not measured, a slow device),
// then a strum within a third of a beat of a beat counts from the beat.
// Start: the second of the take
// that lands on song row StartRow; the song plays RowsPerSecond, its bars as
// FamiTracker highlights them (a bar starts on each pattern's first row and
// every RowsPerBar rows after it). False, with the reason, when no chord
// could be heard.
bool HearStrummedChords(const std::vector<transcribe::Strum> &Strums, double Start, int StartRow, double RowsPerSecond,
	int PatternLength, int RowsPerBar, int RowsPerBeat, StrummedChords &Out, std::string &Error);

// The speed and tempo for a tempo tapped along with the beat (BPM, from
// transcribe::TempoOfTaps): the nearest tempo whose rows all last as long
// (EvenRows in Music.h) when one is within 3% of the taps, closer than anyone
// taps, otherwise the tapped tempo as a whole BPM; either way speed and tempo
// as set_tempo picks them for it (ChooseSpeedTempo). Bpm: the tempo the song
// plays at then. False for a tempo of 0 or less.
bool TappedSpeedTempo(double Tapped, int RowsPerBeat, int FrameRate, int CurrentSpeed, int SplitPoint, int &Speed,
	int &Tempo, double &Bpm);

// FamiTracker's song settings in the BPM layout (View > Profile) show BPM
// from LAYOUT_LEAST_BPM to LAYOUT_MOST_BPM. A song at speed 1 (a sound
// effect, ambience: a row a frame at the default tempo) has no beat to count
// there, and its BPM read 900 at four rows a beat: the layout shows the
// frames each row lasts instead (LayoutFramesText, "1 frame", under the
// label Speed), and a BPM typed or stepped to that LayoutWantsFrames (1, as
// the box shows it, or past the most) sets speed 1 at the default tempo.
const int LAYOUT_LEAST_BPM = 20, LAYOUT_MOST_BPM = 400;
bool LayoutShowsFrames(int Speed);
std::string LayoutFramesText(int Speed, int Tempo, int FrameRate);	// "1 frame", "1.25 frames"
bool LayoutWantsFrames(int Bpm);
// The status bar's tempo for such a song (LayoutShowsFrames), where it showed 900 BPM: "1 frame a row", "1.25 frames a
// row"; the longest it can be, for the pane's width
std::string StatusFramesText(int Speed, int Tempo, int FrameRate);
const char *const STATUS_FRAMES_LONGEST = "8.88 frames a row";

}	// namespace ftapi

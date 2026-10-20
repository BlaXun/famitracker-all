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

// Turning a recording (whistle, hum, finger taps) into notes in the song that
// is open: Edit > Insert recording for WAV files, and the Record dialog for
// takes recorded in FamiTracker. See RECORDER.md.

class CMainFrame;
class CFamiTrackerDoc;
namespace transcribe { struct Audio; struct TakeTiming; }

// What a recording holds
enum recording_kind_t {
	RECORDING_WHISTLE,		// a melody, listened for at 300-4200 Hz
	RECORDING_VOICE,		// humming or singing, 70-1100 Hz
	RECORDING_DRUMS			// finger taps or beatboxing
};

// Guesses what a recording holds. Description says it in words ("whistling").
void GuessRecording(const transcribe::Audio &Audio, recording_kind_t &Kind, CString &Description);

// Reads the WAV file at Path and guesses what it holds. Returns false, with
// the reason in Message, when the file can't be read.
bool GuessRecording(LPCTSTR Path, recording_kind_t &Kind, CString &Description, CString &Message);

// Transcribes a recording as Kind and writes the notes into Channel (drums
// always into the noise channel), from row Row of frame Frame on and into the
// following frames; frames are added at the end of the song when needed.
// Timing says which moment of the recording lands on that row: its first note
// (a WAV file), or the downbeat after a count-in (a take recorded against the
// click). With Expression, a melody keeps how it was played: louder notes play
// louder, slides into notes stay slides, and long notes get vibrato. One
// Ctrl+Z undoes it.
//
// Returns false, with the reason in Message, when nothing was inserted. On
// success Message says what was inserted.
bool InsertTake(CMainFrame *pMainFrm, const transcribe::Audio &Audio, const transcribe::TakeTiming &Timing,
	recording_kind_t Kind, int Channel, bool Expression, int Frame, int Row, CString &Message);

// The same for the WAV file at Path, its first note on the cursor row.
bool InsertRecording(CMainFrame *pMainFrm, LPCTSTR Path, recording_kind_t Kind, int Channel, bool Expression,
	CString &Message);

// A channel's name with its chip, e.g. "VRC6 Pulse 1", for messages and lists
CString RecordingChannelName(const CFamiTrackerDoc *pDoc, int Channel);

// Can a melody go on this channel? (Not on noise or DPCM.)
bool IsMelodyChannel(const CFamiTrackerDoc *pDoc, int Channel);

// Is this a bass channel (triangle or sawtooth)? A melody there is placed an
// octave or so lower, around C3.
bool IsBassChannel(const CFamiTrackerDoc *pDoc, int Channel);

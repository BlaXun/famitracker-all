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

// Edit > Insert recording: turns a WAV recording (whistle, hum, finger taps)
// into notes at the pattern cursor, in the song that is open. See RECORDER.md.

class CMainFrame;
class CFamiTrackerDoc;

// What a recording holds
enum recording_kind_t {
	RECORDING_WHISTLE,		// a melody, listened for at 300-4200 Hz
	RECORDING_VOICE,		// humming or singing, 70-1100 Hz
	RECORDING_DRUMS			// finger taps or beatboxing
};

// Reads the WAV file at Path and guesses what it holds. Description says it
// in words ("whistling"). Returns false, with the reason in Message, when the
// file can't be read.
bool GuessRecording(LPCTSTR Path, recording_kind_t &Kind, CString &Description, CString &Message);

// Transcribes the WAV file at Path as Kind and writes the notes into Channel
// (drums always into the noise channel), starting at the cursor's frame and
// row and running on into the following frames; frames are added at the end
// of the song when needed. With Expression, a melody keeps how it was played:
// louder notes play louder, slides into notes stay slides, and long notes get
// vibrato. One Ctrl+Z undoes it.
//
// Returns false, with the reason in Message, when nothing was inserted. On
// success Message says what was inserted.
bool InsertRecording(CMainFrame *pMainFrm, LPCTSTR Path, recording_kind_t Kind, int Channel, bool Expression,
	CString &Message);

// A channel's name with its chip, e.g. "VRC6 Pulse 1", for messages and lists
CString RecordingChannelName(const CFamiTrackerDoc *pDoc, int Channel);

// Can a melody go on this channel? (Not on noise or DPCM.)
bool IsMelodyChannel(const CFamiTrackerDoc *pDoc, int Channel);

// Is this a bass channel (triangle or sawtooth)? A melody there is placed an
// octave or so lower, around C3.
bool IsBassChannel(const CFamiTrackerDoc *pDoc, int Channel);

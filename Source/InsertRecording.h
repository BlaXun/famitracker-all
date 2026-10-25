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
struct stChanNote;
namespace ftapi { class SectionStore; class KeyStore; struct KeyChoice; class MixStore; }
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
// success Message says what was inserted. With a ClipName, the notes are kept
// as a new clip of that name instead of going into the song.
bool InsertTake(CMainFrame *pMainFrm, const transcribe::Audio &Audio, const transcribe::TakeTiming &Timing,
	recording_kind_t Kind, int Channel, bool Expression, int Frame, int Row, CString &Message, LPCTSTR ClipName = NULL);

// The same for the WAV file at Path, its first note on the cursor row.
bool InsertRecording(CMainFrame *pMainFrm, LPCTSTR Path, recording_kind_t Kind, int Channel, bool Expression,
	CString &Message, LPCTSTR ClipName = NULL);

// Clips (Clips.h): saving the selection in the pattern editor as a new clip,
// and placing a clip at the cursor (its first column on the cursor's channel,
// from the cursor row on) as a copy or linked, as one undoable action. Both
// return false with the reason in Message.
bool SaveSelectionAsClip(CMainFrame *pMainFrm, LPCTSTR Name, CString &Message);
bool PlaceClip(CMainFrame *pMainFrm, int Index, bool Linked, CString &Message);

// The clip editor's changes: clip Index gets these cells (all of them, a column
// at a time) and its linked copies in the song follow, which Ctrl+Z undoes.
void ApplyClipEdit(CMainFrame *pMainFrm, int Index, const stChanNote *pCells, int Count);

// The piano roll's edits: Count cells written into Channel exactly as given,
// from Row on (counting rows from the start of the song, frame * pattern
// length + row), as one undoable action. Frames are added at the end of the
// song when the cells run past it, and a frame that changes gets a pattern of
// its own first if another frame plays the same one. Linked copies of clips
// stay linked, so the clip and its other copies follow the edit. The cursor
// goes to CursorRow (-1: the first row written), in Channel's note column or,
// with KeepChannel, in the channel and column it's in. Returns false, with the
// reason in Message, when nothing was written.
bool WriteChannelRows(CMainFrame *pMainFrm, int Channel, int Row, const stChanNote *pCells, int Count, int CursorRow,
	CString &Message, bool KeepChannel = false);

// The selected track's key (Api/Keys.h): Key when Chosen, otherwise back to
// auto (found from the notes), as one undoable action. False when nothing
// changed.
bool SetTrackKey(CMainFrame *pMainFrm, bool Chosen, const ftapi::KeyChoice &Key);

// The mixer window's faders and starting pans (Api/ChannelMix.h) become Mix,
// as one undoable action; pBefore is what Undo brings back when the change is
// already in the song (a fader dragged, one action for the drag). False when
// nothing changed.
bool SetChannelMix(CMainFrame *pMainFrm, const ftapi::MixStore &Mix, const ftapi::MixStore *pBefore = NULL);

// Edit > Add chords and bass line: chords and a bass line that fit the melody
// in one channel, written into two others, over the bars the melody covers
// (bars and beats as the row highlight shows them).
struct backing_choice_t {
	int MelodyChannel;
	int ChordChannel;		// a pulse channel, -1 for no chords
	int BassChannel;		// the triangle or sawtooth, -1 for no bass line
	int KeyTonic;			// 0-11 (C-B), -1 to find the key
	bool KeyMinor;
	bool HalfBars;			// chords may change halfway through a bar
};

// Has the channel any notes?
bool HasNotes(const CFamiTrackerDoc *pDoc, unsigned int Track, int Channel);

// A key's name, e.g. "Bb major", "F# minor"
CString KeyName(int Tonic, bool Minor);

// What the backing would be: its key ("C minor"), its chords bar by bar
// ("| C | F C | G | C |") and the frames it covers. Returns false, with the
// reason in Message, when there is no melody to fit chords to.
bool DescribeBacking(const CFamiTrackerDoc *pDoc, unsigned int Track, const backing_choice_t &Choice, CString &Key,
	CString &Chords, int &FirstFrame, int &LastFrame, CString &Message);

// Writes the backing into the song as one undoable action, replacing what
// the chord and bass channels played over those bars. Adds Chords and Bass
// instruments the first time. Message says what was added, or why nothing was.
bool AddBacking(CMainFrame *pMainFrm, const backing_choice_t &Choice, CString &Message);

// Can chords go on this channel? (A 2A03, VRC6 or MMC5 pulse.)
bool IsChordChannel(const CFamiTrackerDoc *pDoc, int Channel);

// A channel's name with its chip, e.g. "VRC6 Pulse 1", for messages and lists
CString RecordingChannelName(const CFamiTrackerDoc *pDoc, int Channel);

// Can a melody go on this channel? (Not on noise or DPCM.)
bool IsMelodyChannel(const CFamiTrackerDoc *pDoc, int Channel);

// Is this a bass channel (triangle or sawtooth)? A melody there is placed an
// octave or so lower, around C3.
bool IsBassChannel(const CFamiTrackerDoc *pDoc, int Channel);

// The Claude Code connection (McpConnection.cpp): whole cells written into
// channels from Row of Frame on, as one undoable action, and frames added at
// the end of the song when needed. An exact part's rows become its cells as
// they are (note, instrument, volume and all effect columns). Every other
// part's rows are replaced too, but for the song's own speed, jumps, skips
// and halts there, which stay, the part's effects going around them (as a
// clip dragged into the song does). With a LinkClipId (a clip's ID, -1 for
// none), what's written is a linked copy of that clip, its first column in
// the first part's channel. Before the cells, MoveBy frames are put in at
// frame MoveFrame (MoveBy > 0), each with new empty patterns as Insert frame
// gives them, or the -MoveBy frames from MoveFrame on are taken out (MoveBy
// < 0); the frames after keep their patterns, and Frame counts with the
// frames moved. Count can be 0 then. The cursor goes to song row Cursor (-1
// for Row of Frame). The track's sections follow the frames moved, or with
// pSections become its track 0's (Api/Sections.h) in the same action; with
// pKeys, its track 0's key (or auto, when it has none) becomes the track's
// (Api/Keys.h). False with the reason in Message.
struct song_part_t {
	int Channel;
	const stChanNote *pCells;	// Rows of them, from the first row on
	int Rows;
	bool Exact;
};
bool WriteSongParts(CMainFrame *pMainFrm, const song_part_t *pParts, int Count, int Frame, int Row, int LinkClipId,
	int MoveFrame, int MoveBy, int Cursor, CString &Message, const ftapi::SectionStore *pSections = NULL,
	const ftapi::KeyStore *pKeys = NULL);

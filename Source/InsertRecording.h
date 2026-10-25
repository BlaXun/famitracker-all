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
	RECORDING_DRUMS,		// finger taps or beatboxing
	RECORDING_CHORDS		// a strummed guitar or a piano's chords, for Add chords and bass line (HearRecordedChords)
};

// Guesses what a recording holds. Description says it in words ("whistling").
void GuessRecording(const transcribe::Audio &Audio, recording_kind_t &Kind, CString &Description);

// Reads the WAV file at Path and guesses what it holds. Returns false, with
// the reason in Message, when the file can't be read.
bool GuessRecording(LPCTSTR Path, recording_kind_t &Kind, CString &Description, CString &Message);

// A take sung over a selection (Record from microphone with rows selected):
// the song rows [First, End) of its channel (frame * pattern length + row).
// The take replaces them and nothing else, and the rest of the song sounds as
// it did (CChannelNotes::ReplaceRows in NoteEdit.h); a note sung a little
// early, up to Early rows before First, starts at First.
struct take_selection_t {
	int First;
	int End;
	int Early;
};

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
// as a new clip of that name instead of going into the song. With pSelection,
// the take (whose row 0 is at Row of Frame, at or before the selection) goes
// over the selected rows only; as a clip, the clip holds those rows of it.
bool InsertTake(CMainFrame *pMainFrm, const transcribe::Audio &Audio, const transcribe::TakeTiming &Timing,
	recording_kind_t Kind, int Channel, bool Expression, int Frame, int Row, CString &Message, LPCTSTR ClipName = NULL,
	const take_selection_t *pSelection = NULL);

// The same for the WAV file at Path, its first note on the cursor row.
bool InsertRecording(CMainFrame *pMainFrm, LPCTSTR Path, recording_kind_t Kind, int Channel, bool Expression,
	CString &Message, LPCTSTR ClipName = NULL);

// The chords strummed (or played on a piano) in a recording, bar by bar, as
// names for Add chords and bass line to use ("| C | G | Am F |"): Timing says
// which moment lands on song row StartRow (a WAV file's first strum, or the
// downbeat after a take's count-in); the bars are the song's. FirstRow is the
// row of the bar of the first strum. False with the reason in Message.
bool HearRecordedChords(const CFamiTrackerDoc *pDoc, unsigned int Track, const transcribe::Audio &Audio,
	const transcribe::TakeTiming &Timing, int StartRow, CString &Names, int &FirstRow, CString &Message);
bool HearRecordedChords(const CFamiTrackerDoc *pDoc, unsigned int Track, LPCTSTR Path, int StartRow, CString &Names,
	int &FirstRow, CString &Message);	// a WAV file

// The selected track's speed and tempo, as one undoable action (Tap the
// tempo, Start from a groove). False when nothing changed.
bool SetSongTempo(CMainFrame *pMainFrm, int Speed, int Tempo);

// The speed and tempo for a tempo tapped along with the beat, a beat being
// the song's first row highlight (ftapi::TappedSpeedTempo: the nearest tempo
// with rows of even length, when one is near). Bpm: what the song plays at
// then. False for no tempo.
bool TappedSongTempo(const CFamiTrackerDoc *pDoc, unsigned int Track, double Tapped, int &Speed, int &Tempo, double &Bpm);

// The speed and tempo for a tempo in BPM, as the set_tempo tool picks them
// (rows of even length first: ftapi::ChooseSpeedTempo), with the beat as the
// song's first row highlight
void SpeedTempoFor(const CFamiTrackerDoc *pDoc, unsigned int Track, double Bpm, int &Speed, int &Tempo);

// The song's tempo in BPM, a beat being the first row highlight
double SongBpm(const CFamiTrackerDoc *pDoc, unsigned int Track);
CString BpmText(double Bpm);	// "112.5", "150"

// Edit > Start from a groove (GrooveDlg.cpp): a mood's chords, bass line and
// drums from the start of a bar on, written by the make_groove tool (whose
// parts ftapi::MakeGrooveParts in Api/Grooves.h makes), and the song's speed
// and tempo for Bpm, as one undoable step. The cursor goes to the start of the
// groove on MelodyChannel, where the melody is sung. False with the reason in
// Message; otherwise Message says what was written.
struct groove_choice_t {
	CString Mood;			// a mood's name, as make_groove takes it ("town")
	double Bpm;
	int KeyTonic;			// 0-11 (C-B)
	bool KeyMinor;
	int Row;				// song row of the bar it starts at
	int Bars;
	int ChordChannel, BassChannel, DrumChannel;	// -1 for none
	int MelodyChannel;		// left free for the melody
};
bool MakeGroove(CMainFrame *pMainFrm, const groove_choice_t &Choice, CString &Message);

// The channels a groove goes on, with the melody's left free: the chords on a
// pulse (the VRC6's second, else the 2A03's second, else another), the bass on
// the sawtooth or else the triangle, the drums on the noise; -1 for none
void GrooveChannels(const CFamiTrackerDoc *pDoc, int MelodyChannel, int &Chords, int &Bass, int &Drums);

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
	int MelodyChannel;		// -1 for none (with chords given)
	int ChordChannel;		// a pulse channel, -1 for no chords
	int BassChannel;		// the triangle or sawtooth, -1 for no bass line
	int KeyTonic;			// 0-11 (C-B), -1 to find the key
	bool KeyMinor;
	bool HalfBars;			// chords may change halfway through a bar
	// Chords given by name ("C G Am F", a chord a bar, or "C | G | Am F"), typed or heard from a strummed
	// guitar, instead of chords found from the melody; empty to find them. They start at the bar of
	// ChordsRow (a song row), or with -1 at the melody's first bar, and cover the bars typed, repeating
	// over the melody's when it has more.
	CString Chords;
	int ChordsRow;
};

// Has the channel any notes?
bool HasNotes(const CFamiTrackerDoc *pDoc, unsigned int Track, int Channel);

// A key's name, e.g. "Bb major", "F# minor"
CString KeyName(int Tonic, bool Minor);

// What the backing would be: its key ("C minor"), its chords bar by bar
// ("| C | F C | G | C |") and the frames it covers. Returns false, with the
// reason in Message, when there is no melody to fit chords to (or the chords
// given can't be read).
bool DescribeBacking(const CFamiTrackerDoc *pDoc, unsigned int Track, const backing_choice_t &Choice, CString &Key,
	CString &Chords, int &FirstFrame, int &LastFrame, CString &Message, int *pFirstRow = NULL);	// the first bar's song row

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

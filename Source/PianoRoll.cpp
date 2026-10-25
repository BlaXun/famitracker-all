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

// The piano roll and the bar it is docked in (see PianoRoll.h).

// Standard and engine headers come before stdafx.h, whose DEBUG_NEW macro
// would otherwise rewrite the placement new inside the standard library.
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <vector>
#include "Transcribe/Transcribe.h"
#include "Api/Sections.h"
#include "Api/ChordHelper.h"
#include "stdafx.h"
#pragma push_macro("new")
#undef new
#include <afxpriv.h>		// WM_SIZEPARENT, for laying out the docked bar
#pragma pop_macro("new")
#include "FamiTracker.h"
#include "FamiTrackerDoc.h"
#include "FamiTrackerView.h"
#include "MainFrm.h"
#include "PatternEditor.h"
#include "SoundGen.h"
#include "TrackerChannel.h"
#include "Settings.h"
#include "Clips.h"
#include "ClipsDlg.h"
#include "InsertRecording.h"
#include "NoteEdit.h"
#include "ProblemsPanel.h"
#include "McpConnection.h"
#include "Api/Tools.h"
#include "PianoRoll.h"
#include "PianoRollChords.h"

namespace {

const int PITCHES = OCTAVE_RANGE * 12;	// C-0 to B-7
const int NOISE_PITCHES = 16;			// the noise channel's 0-F
const UINT_PTR TIMER_ID = 1;
const UINT TIMER_MS = 30;
const DWORD MESSAGE_MS = 7000;			// how long a message stays in the status line
const DWORD CHORD_MESSAGE_MS = 15000;	// the chord helper's, which say where the voices went and what to look out for
const DWORD NUDGE_SOUND_MS = 220;		// how long a note sounds after moving it with the keys
const TCHAR SECTION[] = _T("Piano roll");
const DWORD HARMONY_DELAY = 400;		// the key and chords are worked out again this long after the song changed
const DWORD HARMONY_RETRY = 500;		// or this much later, when the Claude Code connection is busy

// Sizes at 96 DPI (scaled with SX and SY)
const int KEYS_WIDTH = 52;
const int HEADER_HEIGHT = 26;
const int RULER_HEIGHT = 20;
const int CHORDS_HEIGHT = 16;			// the chord strip under the ruler
const int STATUS_HEIGHT = 18;
const int KEY_HEIGHT = 10;
const int ROW_WIDTHS[] = { 2, 3, 4, 6, 8, 10, 12, 16, 20, 24, 32, 40, 48 };	// zoom steps
const int ZOOMS = sizeof(ROW_WIDTHS) / sizeof(ROW_WIDTHS[0]);
const int DEFAULT_ZOOM = 6;
const int DRAG_DISTANCE = 3;			// how far the mouse moves before a click becomes a drag
const int DROP_EDGE = 24;				// a clip dragged in this close to the left or right edge scrolls the roll
const int DROP_STEP = 12;				// that much each time, at least
const int END_GRAB = 6;					// the part of a note's right end that changes its length
const int DIVIDER = 6;					// the line along the top of the docked piano roll
const int DEFAULT_HEIGHT = 280;
const int MIN_HEIGHT = 120;
const int MIN_VIEW_HEIGHT = 90;			// what the pattern editor keeps at least

const TCHAR *const NOTE_NAMES[] = {
	_T("C-"), _T("C#"), _T("D-"), _T("D#"), _T("E-"), _T("F-"), _T("F#"), _T("G-"), _T("G#"), _T("A-"), _T("A#"), _T("B-")
};

enum tool_t { TOOL_DRAW, TOOL_PAINT, TOOL_SELECT };

enum grid_t { GRID_ROW, GRID_HALF_BEAT, GRID_BEAT, GRID_TWO_BEATS, GRID_BAR, GRIDS };
const TCHAR *const GRID_NAMES[GRIDS] = { _T("Row"), _T("Half a beat"), _T("Beat"), _T("Two beats"), _T("Bar") };

// The Key menu's scales: a mode (by its place in Api/Music.cpp's), or a major or minor key's five pentatonic notes
struct KeyMenuScale {
	int Mode;
	bool Pentatonic;
	const TCHAR *Name;		// with what it sounds like
};
const KeyMenuScale KEY_SCALES[] = {
	{ ftapi::MODE_MAJOR, false, _T("Major (bright, happy)") },
	{ ftapi::MODE_MINOR, false, _T("Minor (sad, serious)") },
	{ ftapi::MODE_MAJOR, true, _T("Major pentatonic (five notes that always fit)") },
	{ ftapi::MODE_MINOR, true, _T("Minor pentatonic (five notes, bluesy)") },
	{ 1, false, _T("Dorian (minor with a hopeful sixth)") },
	{ 4, false, _T("Mixolydian (major with a bluesy seventh)") },
	{ 3, false, _T("Lydian (dreamy, floating)") },
	{ 2, false, _T("Phrygian (dark, Spanish)") },
	{ 7, false, _T("Harmonic minor (minor with a strong pull home)") },
	{ 6, false, _T("Locrian (unsettled)") },
};
const int KEY_SCALE_COUNT = sizeof(KEY_SCALES) / sizeof(KEY_SCALES[0]);

// The buttons along the top
enum button_t { BUTTON_CHANNEL, BUTTON_DRAW, BUTTON_PAINT, BUTTON_SELECT, BUTTON_GRID, BUTTON_KEY, BUTTON_SNAP, BUTTON_INSTRUMENT, BUTTON_EFFECTS,
	BUTTON_CHORDS, BUTTON_CLIPS, BUTTON_CLOSE };

// Menu items (TrackPopupMenu returns them; they are never sent as commands)
const UINT MENU_CHANNEL = 100;			// + channel
const UINT MENU_GRID = 200;				// + grid_t
const UINT MENU_KEY_AUTO = 300;
const UINT MENU_KEY_SNAP = 301;
const UINT MENU_KEY = 4000;				// + scale (into KEY_SCALES) * 12 + tonic
const UINT MENU_INSTRUMENT = 500;		// + instrument: for new notes
const UINT MENU_GIVE = 600;				// + instrument: for the selected notes
const UINT MENU_CLIP = 1000;			// + clip
const UINT MENU_CLIP_LINKED = 2000;
const UINT MENU_CLIP_SAVE = 2001;
const UINT MENU_CLIP_MANAGE = 2002;
const UINT MENU_EFFECT = 3000;			// + index into EFFECT_CHOICES
const UINT MENU_CHORD_HEARD = 5000;		// the chords the parts play there (the chord strip's)
const UINT MENU_CHORD_KEY = 5010;		// + degree (0-6): the key's chords
const UINT MENU_CHORD_SEVENTH = 5020;	// + degree: with sevenths
const UINT MENU_CHORD_TYPE = 5030;		// typed in a box
const UINT MENU_CHORD_AGAIN = 5031;		// the last chords again on the next bar
const UINT MENU_CHORD_VOICES = 5040;	// + 0 (as many as a chord has notes) or 2-4
const UINT MENU_CHORD_FEWER = 5050;		// too few free channels: fewer voices...
const UINT MENU_CHORD_ARPEGGIO = 5051;	// ...or the arpeggio on one
const UINT MENU_CHORD_CANCEL = 5052;

struct RollNote {
	int Start;			// rows from the start of the song
	int End;			// the row of the next note, release or halt in the channel
	int Pitch;			// where it lands: 0 = C-0 ... 95 = B-7
	int Written;		// the pitch in the cell (a Qxy or Rxy slide starts there)
	int Instrument;		// -1 when none was set yet
	int Pan;			// 0x00 left, 0x80 the middle, 0xFF right (the 8xx effect: this FamiTracker's stereo)
	bool Released;		// ends on a release (===)
	std::vector<CChannelNotes::EffectSpan> Effects;	// what works while it sounds
};

// Effects the Effects menu puts on notes
enum effect_action_t { FX_LASTING, FX_KEY_CHORD, FX_SLIDE_IN, FX_BEND, FX_CLEAR, FX_PAN };
enum effect_group_t { GROUP_VIBRATO, GROUP_TREMOLO, GROUP_CHORD, GROUP_SLIDE, GROUP_GLIDE, GROUP_BEND, GROUP_FADE, GROUP_PAN, EFFECT_GROUPS,
	GROUP_NONE = EFFECT_GROUPS };
const TCHAR *const GROUP_NAMES[EFFECT_GROUPS] = {
	_T("Vibrato"), _T("Tremolo (the volume wobbles)"), _T("Chord (arpeggio)"), _T("Slide into the note"), _T("Glide from the note before"),
	_T("Bend at the end"), _T("Fade out"), _T("Pan (stereo)")
};
struct EffectChoice {
	int Group;				// effect_group_t
	int Action;				// effect_action_t
	unsigned char Number;	// the lasting effect (FX_LASTING)
	int Value;				// its parameter; semitones for slides and bends (up from below, or up; negative: from above, or down)
	int Speed;				// slides and bends, 1-15
	const TCHAR *Name;
};
const EffectChoice EFFECT_CHOICES[] = {
	{ GROUP_VIBRATO, FX_LASTING, EF_VIBRATO, 0x52, 0, _T("Gentle") },
	{ GROUP_VIBRATO, FX_LASTING, EF_VIBRATO, 0x54, 0, _T("Medium") },
	{ GROUP_VIBRATO, FX_LASTING, EF_VIBRATO, 0x67, 0, _T("Strong") },
	{ GROUP_VIBRATO, FX_LASTING, EF_VIBRATO, 0x00, 0, _T("No vibrato") },
	{ GROUP_TREMOLO, FX_LASTING, EF_TREMOLO, 0x53, 0, _T("Gentle") },
	{ GROUP_TREMOLO, FX_LASTING, EF_TREMOLO, 0x77, 0, _T("Strong") },
	{ GROUP_TREMOLO, FX_LASTING, EF_TREMOLO, 0x00, 0, _T("No tremolo") },
	{ GROUP_CHORD, FX_LASTING, EF_ARPEGGIO, 0x47, 0, _T("Major") },
	{ GROUP_CHORD, FX_LASTING, EF_ARPEGGIO, 0x37, 0, _T("Minor") },
	{ GROUP_CHORD, FX_KEY_CHORD, EF_ARPEGGIO, 0x00, 0, _T("The key's chord on each note") },
	{ GROUP_CHORD, FX_LASTING, EF_ARPEGGIO, 0x70, 0, _T("Power chord (a fifth)") },
	{ GROUP_CHORD, FX_LASTING, EF_ARPEGGIO, 0xC0, 0, _T("Octave") },
	{ GROUP_CHORD, FX_LASTING, EF_ARPEGGIO, 0x57, 0, _T("Sus4") },
	{ GROUP_CHORD, FX_LASTING, EF_ARPEGGIO, 0x27, 0, _T("Sus2") },
	{ GROUP_CHORD, FX_LASTING, EF_ARPEGGIO, 0x36, 0, _T("Diminished") },
	{ GROUP_CHORD, FX_LASTING, EF_ARPEGGIO, 0x48, 0, _T("Augmented") },
	{ GROUP_CHORD, FX_LASTING, EF_ARPEGGIO, 0x00, 0, _T("No chord") },
	{ GROUP_SLIDE, FX_SLIDE_IN, 0, 1, 3, _T("From a semitone below") },
	{ GROUP_SLIDE, FX_SLIDE_IN, 0, 2, 3, _T("From 2 semitones below") },
	{ GROUP_SLIDE, FX_SLIDE_IN, 0, 3, 3, _T("From 3 semitones below") },
	{ GROUP_SLIDE, FX_SLIDE_IN, 0, 5, 4, _T("From 5 semitones below") },
	{ GROUP_SLIDE, FX_SLIDE_IN, 0, 12, 6, _T("From an octave below") },
	{ GROUP_SLIDE, FX_SLIDE_IN, 0, -1, 3, _T("From a semitone above") },
	{ GROUP_SLIDE, FX_SLIDE_IN, 0, -2, 3, _T("From 2 semitones above") },
	{ GROUP_SLIDE, FX_SLIDE_IN, 0, 0, 0, _T("No slide") },
	{ GROUP_GLIDE, FX_LASTING, EF_PORTAMENTO, 0x0C, 0, _T("Quick") },
	{ GROUP_GLIDE, FX_LASTING, EF_PORTAMENTO, 0x06, 0, _T("Medium") },
	{ GROUP_GLIDE, FX_LASTING, EF_PORTAMENTO, 0x03, 0, _T("Slow") },
	{ GROUP_GLIDE, FX_LASTING, EF_PORTAMENTO, 0x00, 0, _T("No glide") },
	{ GROUP_BEND, FX_BEND, 0, -1, 2, _T("Down a semitone") },
	{ GROUP_BEND, FX_BEND, 0, -2, 2, _T("Down 2 semitones") },
	{ GROUP_BEND, FX_BEND, 0, -12, 5, _T("Fall an octave") },
	{ GROUP_BEND, FX_BEND, 0, 1, 2, _T("Up a semitone") },
	{ GROUP_BEND, FX_BEND, 0, 0, 0, _T("No bend") },
	{ GROUP_FADE, FX_LASTING, EF_VOLUME_SLIDE, 0x04, 0, _T("Quick") },
	{ GROUP_FADE, FX_LASTING, EF_VOLUME_SLIDE, 0x02, 0, _T("Medium") },
	{ GROUP_FADE, FX_LASTING, EF_VOLUME_SLIDE, 0x01, 0, _T("Slow") },
	{ GROUP_FADE, FX_LASTING, EF_VOLUME_SLIDE, 0x00, 0, _T("No fade") },
	{ GROUP_PAN, FX_PAN, EF_PAN, PAN_LEFT, 0, _T("Left") },
	{ GROUP_PAN, FX_PAN, EF_PAN, 0x40, 0, _T("Halfway left") },
	{ GROUP_PAN, FX_PAN, EF_PAN, PAN_CENTER, 0, _T("Center") },
	{ GROUP_PAN, FX_PAN, EF_PAN, 0xC0, 0, _T("Halfway right") },
	{ GROUP_PAN, FX_PAN, EF_PAN, PAN_RIGHT, 0, _T("Right") },
	{ GROUP_NONE, FX_CLEAR, 0, 0, 0, _T("Remove all effects (the pan stays)") },
};
const int EFFECT_CHOICE_COUNT = sizeof(EFFECT_CHOICES) / sizeof(EFFECT_CHOICES[0]);

// The effect a choice writes, as the pattern editor shows it ("452", "Q32"); empty for none
CString EffectCode(const EffectChoice &Choice)
{
	CString Code;
	switch (Choice.Action) {
	case FX_LASTING:
		if (Choice.Value != 0)
			Code.Format(_T("%c%02X"), EFF_CHAR[Choice.Number - 1], Choice.Value);
		break;
	case FX_SLIDE_IN:
	case FX_BEND:
		if (Choice.Value != 0)
			Code.Format(_T("%c%X%X"), Choice.Value > 0 ? _T('Q') : _T('R'), Choice.Speed, std::min(15, std::abs(Choice.Value)));
		break;
	case FX_PAN:
		Code.Format(_T("%c%02X"), EFF_CHAR[EF_PAN - 1], Choice.Value);
		break;
	}
	return Code;
}

// A pan in words: "left", "center", "50% right"
CString PanText(int Pan)
{
	if (Pan == PAN_CENTER)
		return _T("center");
	if (Pan <= PAN_LEFT)
		return _T("left");
	if (Pan >= PAN_RIGHT)
		return _T("right");
	const double Position = Pan < PAN_CENTER ? (Pan - PAN_CENTER) / 128.0 : (Pan - PAN_CENTER) / 127.0;
	CString Text;
	Text.Format(_T("%d%% %s"), std::max(1, static_cast<int>(std::fabs(Position) * 100.0 + 0.5)), Position < 0.0 ? _T("left") : _T("right"));
	return Text;
}

// A chord's name from an arpeggio's two notes above the root
CString ChordName(int Param)
{
	switch (Param) {
	case 0x47: return _T("major");
	case 0x37: return _T("minor");
	case 0x70: case 0x07: return _T("power chord");
	case 0xC0: case 0x0C: return _T("octave");
	case 0x57: return _T("sus4");
	case 0x27: return _T("sus2");
	case 0x36: return _T("diminished");
	case 0x48: return _T("augmented");
	case 0x49: return _T("major sixth");
	case 0x38: return _T("first inversion");
	case 0x59: return _T("second inversion");
	}
	return CString();
}

bool IsBlackKey(int Pitch)
{
	const int Note = Pitch % 12;
	return Note == 1 || Note == 3 || Note == 6 || Note == 8 || Note == 10;
}

COLORREF Mix(COLORREF From, COLORREF To, double Amount)
{
	const double r = GetRValue(From) + (GetRValue(To) - GetRValue(From)) * Amount;
	const double g = GetGValue(From) + (GetGValue(To) - GetGValue(From)) * Amount;
	const double b = GetBValue(From) + (GetBValue(To) - GetBValue(From)) * Amount;
	return RGB(int(r + 0.5), int(g + 0.5), int(b + 0.5));
}

// A colour per channel: cool for the 2A03, warm for the VRC6
COLORREF ChannelColor(int ChanID, int Channel)
{
	switch (ChanID) {
	case CHANID_SQUARE1: return RGB(0x50, 0x9C, 0xFF);
	case CHANID_SQUARE2: return RGB(0x3C, 0xD0, 0xF0);
	case CHANID_TRIANGLE: return RGB(0x50, 0xE0, 0x70);
	case CHANID_NOISE: return RGB(0xB8, 0xB8, 0xB8);
	case CHANID_DPCM: return RGB(0xC0, 0x8C, 0xFF);
	case CHANID_VRC6_PULSE1: return RGB(0xFF, 0xA0, 0x38);
	case CHANID_VRC6_PULSE2: return RGB(0xFF, 0xD8, 0x48);
	case CHANID_VRC6_SAWTOOTH: return RGB(0xFF, 0x5C, 0x5C);
	}
	static const COLORREF OTHERS[] = { RGB(0xFF, 0x70, 0xC8), RGB(0x8C, 0xFF, 0xD0), RGB(0xE0, 0xE0, 0x60), RGB(0x90, 0xA0, 0xFF) };
	return OTHERS[Channel % 4];
}

CString PitchName(int Pitch)
{
	CString Name;
	Name.Format(_T("%s%d"), NOTE_NAMES[Pitch % 12], Pitch / 12);
	return Name;
}

// The noise channel's notes as the pattern editor shows them: 0-# to F-#
CString NoiseName(int Pitch)
{
	CString Name;
	Name.Format(_T("%X-#"), Pitch & 0x0F);
	return Name;
}

CString Plural(int Count, LPCTSTR One, LPCTSTR Many)
{
	CString Text;
	Text.Format(_T("%d %s"), Count, Count == 1 ? One : Many);
	return Text;
}

void Outline(CDC &dc, const CRect &Rect, COLORREF Color, int Width = 1)
{
	dc.FillSolidRect(Rect.left, Rect.top, Rect.Width(), Width, Color);
	dc.FillSolidRect(Rect.left, Rect.bottom - Width, Rect.Width(), Width, Color);
	dc.FillSolidRect(Rect.left, Rect.top, Width, Rect.Height(), Color);
	dc.FillSolidRect(Rect.right - Width, Rect.top, Width, Rect.Height(), Color);
}

// A small triangle pointing down, for buttons that open a menu
void Arrow(CDC &dc, int X, int Y, COLORREF Color)
{
	for (int i = 0; i < 4; ++i)
		dc.FillSolidRect(X + i, Y + i, 7 - 2 * i, 1, Color);
}

// An x, for closing
void Cross(CDC &dc, const CRect &Rect, COLORREF Color)
{
	const int Size = std::min(Rect.Width(), Rect.Height()) / 2;
	const int X = Rect.left + (Rect.Width() - Size) / 2, Y = Rect.top + (Rect.Height() - Size) / 2;
	for (int i = 0; i < Size; ++i) {
		dc.FillSolidRect(X + i, Y + i, 2, 1, Color);
		dc.FillSolidRect(X + Size - 1 - i, Y + i, 2, 1, Color);
	}
}

// What the problems panel found on a note (ProblemsPanel.h): a dot at its
// start, and a ring around it when the panel has that finding selected
void ProblemDot(CDC &dc, const CRect &Note, int Mark)
{
	if (Mark == 0)
		return;
	const COLORREF Color = ProblemMarkColor(Mark);
	if (ProblemMarkSelected(Mark)) {
		CRect Ring(Note);
		Ring.InflateRect(std::max(1, SX(2)), std::max(1, SY(1)));
		Outline(dc, Ring, Color, std::max(1, SX(2)));
	}
	const int Size = std::max(3, std::min(static_cast<int>(Note.Height()) - 2, SY(6)));
	const int X = Note.left + std::max(1, SX(2)), Y = Note.top + (Note.Height() - Size) / 2;
	dc.FillSolidRect(X - 1, Y - 1, Size + 2, Size + 2, RGB(255, 255, 255));
	dc.FillSolidRect(X, Y, Size, Size, Color);
}

// The first of a channel's notes still sounding after Row (notes don't
// overlap, so their ends are in order too)
size_t FirstEndingAfter(const std::vector<RollNote> &Notes, int Row)
{
	size_t Low = 0, High = Notes.size();
	while (Low < High) {
		const size_t Mid = (Low + High) / 2;
		if (Notes[Mid].End <= Row)
			Low = Mid + 1;
		else
			High = Mid;
	}
	return Low;
}

// The note sounding at Row in a channel's notes, -1 if none
int NoteAt(const std::vector<RollNote> &Notes, int Row)
{
	int Low = 0, High = static_cast<int>(Notes.size());	// the first note starting after Row
	while (Low < High) {
		const int Mid = (Low + High) / 2;
		if (Notes[static_cast<size_t>(Mid)].Start <= Row)
			Low = Mid + 1;
		else
			High = Mid;
	}
	return (Low > 0 && Notes[static_cast<size_t>(Low - 1)].End > Row) ? Low - 1 : -1;
}

// A channel's notes as the piano roll shows them, up to row Rows
std::vector<RollNote> RollNotes(const CChannelNotes &Channel, int Rows)
{
	std::vector<RollNote> Out;
	for (int i = 0; i < Channel.GetNoteCount(); ++i) {
		const CChannelNotes::Note &Note = Channel.GetNote(i);
		if (Note.Start >= Rows)
			break;
		RollNote Shown;
		Shown.Start = Note.Start;
		Shown.End = std::min(Note.End, Rows);
		Shown.Pitch = Note.Pitch;
		Shown.Written = Note.Written;
		Shown.Instrument = Note.Instrument;
		Shown.Pan = Note.Pan;
		Shown.Released = Note.Released;
		Channel.GetEffects(i, Shown.Effects);
		Out.push_back(Shown);
	}
	return Out;
}

bool Contains(const std::vector<int> &List, int Value)
{
	return std::find(List.begin(), List.end(), Value) != List.end();
}

bool AltHeld()
{
	return ::GetKeyState(VK_MENU) < 0;
}

int Luma(COLORREF Color)
{
	return (GetRValue(Color) * 3 + GetGValue(Color) * 6 + GetBValue(Color)) / 10;
}

// "A, C and E"
CString PitchClassList(const std::vector<int> &Classes, const ftapi::Key &K)
{
	CString Text;
	for (size_t i = 0; i < Classes.size(); ++i) {
		if (i > 0)
			Text += i + 1 == Classes.size() ? _T(" and ") : _T(", ");
		Text += ftapi::NoteClassName(Classes[i], K).c_str();
	}
	return Text;
}

}	// namespace

struct CPianoRollWnd::Impl {
	CWnd *pWnd;

	// The song as last read
	std::vector<std::vector<RollNote> > Notes;	// per channel, in time order
	std::vector<int> ChanIDs;
	unsigned int Track;
	unsigned int Frames;
	unsigned int Length;
	int Beat, Bar;		// rows, from the row highlight (0: none)
	ftapi::SectionList Sections;	// the track's (Api/Sections.h), named on the ruler
	bool Dirty;
	bool KeyChosen;					// the track's key, chosen in the Key menu (Api/Keys.h)...
	ftapi::KeyChoice ChosenKey;
	ftapi::TrackHarmony Harmony;	// ...else the one its notes fit, with the chords the parts play (Api/Tools.h)
	bool HarmonyRead;				// for HarmonyTrack
	unsigned int HarmonyTrack;
	bool HarmonyStale;				// the song changed since: worked out again at HarmonyDue
	DWORD HarmonyDue;
	int HoverChord;					// into Harmony.Chords, under the mouse in the chord strip; -1 for none

	// The view
	int Zoom;			// into ROW_WIDTHS
	int ScrollX;		// pixels
	int ScrollY;
	int Channel;		// the bright one, edited: the pattern editor cursor's
	bool Placed;		// scrolled to the notes once
	bool Shown;
	bool Focused;
	CRect Client, Header, Keys, Ruler, ChordStrip, Grid, Status;

	// The rest of FamiTracker, as last seen
	int CursorFrame, CursorRow, CursorChannel;
	bool Playing;
	int PlayRow;		// from the start of the song, -1 when not playing this track

	// The mouse
	int HoverChannel, HoverNote;
	int PreviewChannel, PreviewPitch;	// a note being played, -1: none
	DWORD PreviewUntil;					// when a note played by the keys stops, 0: when told

	// Editing
	int Tool;			// tool_t
	int GridChoice;		// grid_t
	bool Snap;			// new and moved notes land on the key's notes
	bool LinkedClips;	// clips are placed as linked copies
	std::vector<int> Selection;		// where the selected notes of the bright channel start
	CString Message;	// the last message for the status line...
	DWORD MessageTime;	// ...and when it came...
	DWORD MessageMs;	// ...and how long it stays

	// What the mouse or a key is doing to the notes
	enum drag_t { DRAG_NONE, DRAG_KEYS, DRAG_RULER, DRAG_DRAW, DRAG_MOVE, DRAG_RESIZE, DRAG_ERASE, DRAG_SELECT, DRAG_PAINT, DRAG_RANGE };
	enum edit_t { EDIT_NONE, EDIT_ADD, EDIT_REMOVE, EDIT_RESIZE, EDIT_MOVE, EDIT_PAINT, EDIT_PASTE, EDIT_INSTRUMENT, EDIT_EFFECT };
	drag_t Drag;
	CPoint DragFrom;
	bool DragMoved;		// further than DRAG_DISTANCE
	bool DragShift;
	bool DragAlt;		// rows instead of the grid
	edit_t EditKind;
	CChannelNotes Before;	// the bright channel when the edit began, with room for one more frame...
	CChannelNotes After;	// ...and with the edit
	int SongRows;		// the song's rows when the edit began
	bool Previewing;	// the bright channel is drawn from After
	std::vector<RollNote> PreviewNotes;
	int PreviewRows;	// the song's rows with the edit
	std::vector<int> Marked;		// notes to show selected in the preview
	int EditStart, EditEnd, EditPitch, EditInstrument;	// a note added or resized
	std::vector<int> Moving;		// notes moved or copied...
	int GrabStart;		// ...the one the mouse holds...
	double GrabRow;		// ...where it was grabbed (rows, with the fraction)...
	int GrabPitch;
	bool GrabWasSelected;
	int MoveRows, MoveSteps, MoveSemitones, MovePitch;	// ...how far, and where the held one lands
	bool MoveCopy;
	std::vector<int> Erased;
	std::map<int, int> Stroke;		// a painted melody: grid step (its first row) to pitch
	int StrokeLast;
	int PasteRow;
	int GiveInstrument;
	int ChosenEffect;	// into EFFECT_CHOICES
	mutable bool EffectFits;	// the effects fitted in the effect columns
	CRect Band;			// selecting with Ctrl+drag
	int RangeFrom, RangeTo;	// selecting whole rows along the ruler (the Select tool)
	bool PatternSelecting;	// the pattern editor had a selection at the last look
	bool RangeKept;		// the rows last dragged along the ruler with the Select tool are where chords go (Chords)

	// Copied notes
	CChannelNotes Copied;
	bool HasCopied;

	// Placing a clip
	int PlacingClip;	// index, -1 when not placing one
	int PlaceRow;		// where it goes, -1 off the grid
	std::vector<std::vector<RollNote> > ClipNotes;	// per column
	bool PanelDrop;		// PlacingClip is dragged in from the clips panel, which places it

	// The chord helper (the Chords button: Api/ChordHelper.h works chords out, PianoRollChords.h writes them)
	int ChordVoices;				// the voices a chord gets: 0 for as many as it has notes, or 2-4
	CString TypedChords;			// typed last
	unsigned int AgainTrack;		// the chords written last, for Again on the next bar: their track...
	int AgainRow;					// ...the row after them (-1: none yet)...
	CString AgainChords;			// ...their last bar's chords ("| Am |", "| F G |")...
	std::vector<ftapi::ChordAt> AgainLead;	// ...as chords, for the voices to lead from...
	std::vector<int> AgainChannels;	// ...the channels of the voices, lowest first...
	int AgainVoices;				// ...how many there were...
	bool AgainArpeggio;				// ...or the arpeggio on one

	// The buttons along the top, where they were drawn last
	struct Button {
		int Id;
		CRect Rect;
	};
	std::vector<Button> Buttons;

	CBitmap Back;		// the picture is drawn here, then copied to the window
	CSize BackSize;

	explicit Impl(CWnd *pWindow) :
		pWnd(pWindow), Track(0), Frames(1), Length(64), Beat(4), Bar(16), Dirty(true), KeyChosen(false), HarmonyRead(false),
		HarmonyTrack(static_cast<unsigned int>(-1)), HarmonyStale(true), HarmonyDue(0), HoverChord(-1),
		Zoom(DEFAULT_ZOOM), ScrollX(0), ScrollY(0), Channel(0), Placed(false), Shown(false), Focused(false),
		CursorFrame(-1), CursorRow(-1), CursorChannel(-1), Playing(false), PlayRow(-1),
		HoverChannel(-1), HoverNote(-1), PreviewChannel(-1), PreviewPitch(-1), PreviewUntil(0),
		Tool(TOOL_DRAW), GridChoice(GRID_BEAT), Snap(false), LinkedClips(false), MessageTime(0), MessageMs(MESSAGE_MS),
		Drag(DRAG_NONE), DragMoved(false), DragShift(false), DragAlt(false), EditKind(EDIT_NONE), SongRows(0), Previewing(false), PreviewRows(0),
		EditStart(0), EditEnd(0), EditPitch(0), EditInstrument(-1), GrabStart(0), GrabRow(0.0), GrabPitch(0), GrabWasSelected(false),
		MoveRows(0), MoveSteps(0), MoveSemitones(0), MovePitch(0), MoveCopy(false), StrokeLast(-1), PasteRow(0),
		GiveInstrument(-1), ChosenEffect(0), EffectFits(true), RangeFrom(0), RangeTo(0), PatternSelecting(false), RangeKept(false), HasCopied(false), PlacingClip(-1),
		PlaceRow(-1), PanelDrop(false), ChordVoices(0), AgainTrack(static_cast<unsigned int>(-1)), AgainRow(-1), AgainVoices(0),
		AgainArpeggio(false), BackSize(0, 0)
	{
	}

	static CFamiTrackerView *View() { return CFamiTrackerView::GetView(); }
	static CMainFrame *Main() { return static_cast<CMainFrame*>(theApp.m_pMainWnd); }
	static CFamiTrackerDoc *Doc()
	{
		CFamiTrackerView *pView = View();
		return pView != NULL ? pView->GetDocument() : NULL;
	}

	// Settings

	void Load()
	{
		const int SavedZoom = static_cast<int>(theApp.GetProfileInt(SECTION, _T("Zoom"), DEFAULT_ZOOM));
		Zoom = (SavedZoom >= 0 && SavedZoom < ZOOMS) ? SavedZoom : DEFAULT_ZOOM;
		Tool = std::max(static_cast<int>(TOOL_DRAW), std::min(static_cast<int>(TOOL_SELECT),
			static_cast<int>(theApp.GetProfileInt(SECTION, _T("Tool"), TOOL_DRAW))));
		GridChoice = std::min(static_cast<int>(theApp.GetProfileInt(SECTION, _T("Grid"), GRID_BEAT)), GRIDS - 1);
		// Snapping to the key: on at first for those who had picked a key in the old Key menu, which always snapped
		const int OldKey = static_cast<int>(theApp.GetProfileInt(SECTION, _T("Key"), static_cast<UINT>(-1)));
		Snap = theApp.GetProfileInt(SECTION, _T("Snap to key"), OldKey >= 0 && OldKey <= 11 ? 1 : 0) != 0;
		LinkedClips = theApp.GetProfileInt(SECTION, _T("Linked clips"), 0) != 0;
		ChordVoices = static_cast<int>(theApp.GetProfileInt(SECTION, _T("Chord voices"), 0));
		if (ChordVoices != 0 && (ChordVoices < 2 || ChordVoices > 4))
			ChordVoices = 0;
	}

	void Save() const
	{
		theApp.WriteProfileInt(SECTION, _T("Zoom"), Zoom);
		theApp.WriteProfileInt(SECTION, _T("Tool"), Tool);
		theApp.WriteProfileInt(SECTION, _T("Grid"), GridChoice);
		theApp.WriteProfileInt(SECTION, _T("Snap to key"), Snap ? 1 : 0);
		theApp.WriteProfileInt(SECTION, _T("Linked clips"), LinkedClips ? 1 : 0);
		theApp.WriteProfileInt(SECTION, _T("Chord voices"), ChordVoices);
	}

	void Say(LPCTSTR Text, DWORD Ms = MESSAGE_MS)
	{
		Message = Text;
		MessageTime = ::GetTickCount();
		MessageMs = Ms;
	}

	// Geometry

	int TotalRows() const { return static_cast<int>(Frames * Length); }
	int EditRows() const { return static_cast<int>(std::min(Frames + 1, static_cast<unsigned int>(MAX_FRAMES)) * Length); }	// one more frame to write into
	int RowWidth() const { return SX(ROW_WIDTHS[Zoom]); }
	int KeyHeight() const { return std::max(4, SY(KEY_HEIGHT)); }

	void Layout()
	{
		pWnd->GetClientRect(&Client);
		Header = CRect(Client.left, Client.top, Client.right, Client.top + SY(HEADER_HEIGHT));
		Keys = CRect(Client.left, Header.bottom + SY(RULER_HEIGHT) + SY(CHORDS_HEIGHT), Client.left + SX(KEYS_WIDTH),
			Client.bottom - SY(STATUS_HEIGHT));
		Ruler = CRect(Keys.right, Header.bottom, Client.right, Header.bottom + SY(RULER_HEIGHT));
		ChordStrip = CRect(Keys.right, Ruler.bottom, Client.right, Keys.top);
		Grid = CRect(Keys.right, Keys.top, Client.right, Keys.bottom);
		Status = CRect(Client.left, Keys.bottom, Client.right, Client.bottom);
	}

	int RowX(int Row) const { return Grid.left + Row * RowWidth() - ScrollX; }
	int PitchTop(int Pitch) const { return Grid.top + (PITCHES - 1 - Pitch) * KeyHeight() - ScrollY; }
	double RowAtF(int X) const { return double(X - Grid.left + ScrollX) / RowWidth(); }
	int RowAt(int X) const
	{
		const int Offset = X - Grid.left + ScrollX;
		return Offset >= 0 ? Offset / RowWidth() : -1;
	}
	int PitchAt(int Y) const
	{
		const int Offset = Y - Grid.top + ScrollY;
		return Offset >= 0 ? PITCHES - 1 - Offset / KeyHeight() : PITCHES;
	}
	double PitchAtF(int Y) const	// with the fraction: a lane's middle is its pitch
	{
		return PITCHES - 0.5 - (Y - Grid.top + ScrollY + 0.5) / KeyHeight();
	}
	CRect NoteRect(const RollNote &Note) const
	{
		CRect Rect(RowX(Note.Start), PitchTop(Note.Pitch) + 1, RowX(Note.End), PitchTop(Note.Pitch) + KeyHeight());
		if (Rect.Width() < 2)
			Rect.right = Rect.left + 2;
		return Rect;
	}

	// The grid notes snap to: rows, beats and bars count from the start of each frame

	int GridRows() const
	{
		const int BeatRows = Beat > 0 ? Beat : 4;
		const int BarRows = Bar > 0 ? Bar : BeatRows * 4;
		int Rows = BeatRows;
		switch (GridChoice) {
		case GRID_ROW: Rows = 1; break;
		case GRID_HALF_BEAT: Rows = std::max(1, BeatRows / 2); break;
		case GRID_TWO_BEATS: Rows = BeatRows * 2; break;
		case GRID_BAR: Rows = BarRows; break;
		}
		return std::max(1, std::min(Rows, static_cast<int>(Length)));
	}
	int FrameStart(int Row) const { return Row - Row % static_cast<int>(Length); }
	int SnapDown(int Row) const
	{
		const int Start = FrameStart(Row), Step = GridRows();
		return Start + (Row - Start) / Step * Step;
	}
	int NextLine(int Row) const		// the first grid line after Row
	{
		const int Start = FrameStart(Row), Step = GridRows();
		return std::min(Start + ((Row - Start) / Step + 1) * Step, Start + static_cast<int>(Length));
	}
	int NearestLine(double Row) const
	{
		const int Whole = std::max(0, static_cast<int>(std::floor(Row)));
		const int Start = FrameStart(Whole), Step = GridRows();
		const int Line = static_cast<int>(std::floor((Row - Start) / Step + 0.5)) * Step;
		return Start + std::min(Line, static_cast<int>(Length));
	}

	// The bright channel's kind

	bool IsNoise(int Which) const { return Which >= 0 && Which < static_cast<int>(ChanIDs.size()) && ChanIDs[static_cast<size_t>(Which)] == CHANID_NOISE; }
	bool IsDPCM(int Which) const { return Which >= 0 && Which < static_cast<int>(ChanIDs.size()) && ChanIDs[static_cast<size_t>(Which)] == CHANID_DPCM; }
	int Highest() const { return IsNoise(Channel) ? NOISE_PITCHES - 1 : PITCHES - 1; }
	int ClampPitch(int Pitch) const { return std::max(0, std::min(Highest(), Pitch)); }

	// The key: the track's chosen one (the Key menu), else the one its notes fit as the tools find it (worked out with
	// the chords a moment after a change). Its lanes are lighter, the tonic's marked; with Snap, new and moved notes
	// land on its notes (Alt: any note)

	bool HasKey() const { return KeyChosen || (HarmonyRead && HarmonyTrack == Track && Harmony.HasFound); }
	ftapi::KeyChoice TheKey() const { return KeyChosen ? ChosenKey : ftapi::KeyChoice(Harmony.Found); }
	bool KeyApplies() const { return HasKey() && !IsNoise(Channel) && !IsDPCM(Channel); }
	bool InKey(int Pitch) const { return ftapi::KeyHasPitch(TheKey(), Pitch); }
	bool SnapsToKey() const { return Snap && KeyApplies() && !AltHeld(); }
	int SnapPitch(double Pitch) const	// the lane under the mouse (Pitch: PitchAtF), or with Snap the key's nearest
	{
		if (!SnapsToKey())
			return ClampPitch(static_cast<int>(std::floor(Pitch + 0.5)));
		return ftapi::SnapToKey(TheKey(), Pitch, 0, Highest());
	}
	CString NameOf(int Which, int Pitch) const
	{
		return IsNoise(Which) ? NoiseName(Pitch) : PitchName(Pitch);
	}

	CString KeyText() const		// "A minor", "D dorian"
	{
		return HasKey() ? CString(ftapi::KeyChoiceName(TheKey()).c_str()) : CString(_T("none yet"));
	}

	// Instruments

	bool CanRelease(int Instrument) const
	{
		CFamiTrackerDoc *pDoc = Doc();
		if (pDoc == NULL || Instrument < 0 || Instrument >= MAX_INSTRUMENTS || !pDoc->IsInstrumentUsed(static_cast<unsigned int>(Instrument)))
			return false;
		CInstrument *pInstrument = pDoc->GetInstrument(static_cast<unsigned int>(Instrument));	// retained
		if (pInstrument == NULL)
			return false;
		const bool Release = pInstrument->CanRelease();
		pInstrument->Release();
		return Release;
	}

	// Can the instrument play on the bright channel's chip?
	bool Fits(int Instrument) const
	{
		CFamiTrackerDoc *pDoc = Doc();
		if (pDoc == NULL || Instrument < 0 || Instrument >= MAX_INSTRUMENTS || !pDoc->IsInstrumentUsed(static_cast<unsigned int>(Instrument)) ||
			Channel >= static_cast<int>(ChanIDs.size()))
			return false;
		const inst_type_t Type = pDoc->GetInstrumentType(static_cast<unsigned int>(Instrument));
		switch (pDoc->GetChipType(Channel)) {
		case SNDCHIP_NONE: case SNDCHIP_MMC5: return Type == INST_2A03;
		case SNDCHIP_VRC6: return Type == INST_VRC6;
		case SNDCHIP_VRC7: return Type == INST_VRC7;
		case SNDCHIP_FDS: return Type == INST_FDS;
		case SNDCHIP_N163: return Type == INST_N163;
		case SNDCHIP_S5B: return Type == INST_S5B;
		}
		return false;
	}

	// The instrument for new notes at Row: the one picked in the instrument list, or else the one the
	// channel plays there
	int NewInstrument(int Row) const
	{
		CMainFrame *pMain = Main();
		const int Picked = pMain != NULL ? pMain->GetSelectedInstrument() : -1;
		if (Fits(Picked))
			return Picked;
		if (Channel < static_cast<int>(Notes.size())) {
			const std::vector<RollNote> &List = Notes[static_cast<size_t>(Channel)];
			for (size_t i = List.size(); i-- > 0; ) {
				if (List[i].Start <= Row && Fits(List[i].Instrument))
					return List[i].Instrument;
			}
			for (size_t i = 0; i < List.size(); ++i) {
				if (Fits(List[i].Instrument))
					return List[i].Instrument;
			}
		}
		return (Picked >= 0 && Picked < MAX_INSTRUMENTS) ? Picked : -1;
	}

	CString InstrumentText(int Instrument) const
	{
		CFamiTrackerDoc *pDoc = Doc();
		CString Text;
		if (pDoc == NULL || Instrument < 0 || Instrument >= MAX_INSTRUMENTS || !pDoc->IsInstrumentUsed(static_cast<unsigned int>(Instrument)))
			return _T("none");
		char Name[256] = "";
		pDoc->GetInstrumentName(static_cast<unsigned int>(Instrument), Name);
		Text.Format(_T("%02X %s"), Instrument, static_cast<LPCTSTR>(CString(Name)));
		return Text;
	}

	// Reading the song

	// A channel's cells from the document, Rows rows of them (the ones past the song empty)
	void ReadChannel(int Which, CChannelNotes &Out, int Rows, bool ForEditing) const
	{
		CFamiTrackerDoc *pDoc = Doc();
		Out.Reset(Rows);
		if (pDoc == NULL)
			return;
		Out.SetEffectColumns(static_cast<int>(pDoc->GetEffColumns(Track, static_cast<unsigned int>(Which))) + 1);
		Out.SetPitchRange(0, IsNoise(Which) ? NOISE_PITCHES - 1 : PITCHES - 1);
		if (ForEditing) {
			for (int i = 0; i < MAX_INSTRUMENTS; ++i)
				Out.SetCanRelease(i, CanRelease(i));
		}
		for (unsigned int f = 0; f < Frames; ++f) {
			for (unsigned int r = 0; r < Length; ++r) {
				const int Row = static_cast<int>(f * Length + r);
				if (Row >= Rows)
					return;
				stChanNote Cell;
				pDoc->GetNoteData(Track, f, static_cast<unsigned int>(Which), r, &Cell);
				Out.SetCell(Row, Cell);
			}
		}
	}

	// Reads every channel's notes
	void Read()
	{
		Dirty = false;
		Notes.clear();
		ChanIDs.clear();
		HoverChannel = HoverNote = -1;
		CFamiTrackerDoc *pDoc = Doc();
		CMainFrame *pMain = Main();
		if (pDoc == NULL || pMain == NULL || !pDoc->IsFileLoaded())
			return;

		Track = static_cast<unsigned int>(std::max(0, pMain->GetSelectedTrack()));
		if (Track >= pDoc->GetTrackCount())
			Track = 0;
		Frames = std::max(1U, pDoc->GetFrameCount(Track));
		Length = std::max(1U, pDoc->GetPatternLength(Track));
		Beat = std::max(0, pDoc->GetFirstHighlight());
		Bar = std::max(0, pDoc->GetSecondHighlight());
		Sections = pDoc->GetSections()->Get(static_cast<int>(Track));
		KeyChosen = pDoc->GetKeys()->Get(static_cast<int>(Track), ChosenKey);
		HarmonyStale = true;
		HarmonyDue = ::GetTickCount() + (HarmonyRead && HarmonyTrack == Track ? HARMONY_DELAY : 0);
		if (HarmonyTrack != Track)
			HarmonyRead = false;	// another track's: not shown

		const int Channels = static_cast<int>(pDoc->GetAvailableChannels());
		for (int c = 0; c < Channels; ++c)
			ChanIDs.push_back(pDoc->GetChannelType(c));
		for (int c = 0; c < Channels; ++c) {
			CChannelNotes Cells;
			ReadChannel(c, Cells, TotalRows(), false);
			Notes.push_back(RollNotes(Cells, TotalRows()));
		}
		if (Channel >= Channels)
			Channel = std::max(0, Channels - 1);

		// Selected notes that are gone aren't selected any more
		std::vector<int> Still;
		for (size_t i = 0; i < Selection.size(); ++i) {
			if (FindStart(Selection[i]) >= 0)
				Still.push_back(Selection[i]);
		}
		Selection.swap(Still);
	}

	// The bright channel's note starting at Start, -1 if none
	int FindStart(int Start) const
	{
		if (Channel >= static_cast<int>(Notes.size()))
			return -1;
		const std::vector<RollNote> &List = Notes[static_cast<size_t>(Channel)];
		for (size_t i = 0; i < List.size(); ++i) {
			if (List[i].Start == Start)
				return static_cast<int>(i);
		}
		return -1;
	}

	const std::vector<RollNote> &Visible(int Which) const
	{
		return (Previewing && Which == Channel) ? PreviewNotes : Notes[static_cast<size_t>(Which)];
	}

	// Scrolling

	void Clamp()
	{
		ScrollX = std::max(0, std::min(ScrollX, EditRows() * RowWidth() - Grid.Width()));
		ScrollY = std::max(0, std::min(ScrollY, PITCHES * KeyHeight() - Grid.Height()));
	}

	void UpdateScrollBars()
	{
		Clamp();
		SCROLLINFO Info;
		memset(&Info, 0, sizeof(Info));
		Info.cbSize = sizeof(Info);
		Info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
		Info.nMax = std::max(0, EditRows() * RowWidth() - 1);
		Info.nPage = static_cast<UINT>(std::max(0, Grid.Width()));
		Info.nPos = ScrollX;
		pWnd->SetScrollInfo(SB_HORZ, &Info);
		Info.nMax = PITCHES * KeyHeight() - 1;
		Info.nPage = static_cast<UINT>(std::max(0, Grid.Height()));
		Info.nPos = ScrollY;
		pWnd->SetScrollInfo(SB_VERT, &Info);
	}

	// Keeps Row in view: paging ahead while playing, otherwise only when it's off screen
	void FollowRow(int Row, bool Playback)
	{
		const int Width = Grid.Width();
		if (Width <= 0)
			return;
		const int X = Row * RowWidth() - ScrollX;
		if (Playback) {
			if (X < 0 || X > Width * 85 / 100)
				ScrollX = Row * RowWidth() - Width / 10;
		}
		else if (X < 0 || X > Width - 2 * RowWidth())
			ScrollX = Row * RowWidth() - Width / 3;
		UpdateScrollBars();
	}

	// Vertically on the bright channel's notes, or on C-4
	void CenterOnNotes()
	{
		int Low = PITCHES, High = -1;
		if (Channel >= 0 && Channel < static_cast<int>(Notes.size())) {
			const std::vector<RollNote> &List = Notes[static_cast<size_t>(Channel)];
			for (size_t i = 0; i < List.size(); ++i) {
				Low = std::min(Low, List[i].Pitch);
				High = std::max(High, List[i].Pitch);
			}
		}
		const int Middle = High >= 0 ? (Low + High) / 2 : (IsNoise(Channel) ? 8 : 48);
		ScrollY = (PITCHES - 1 - Middle) * KeyHeight() - Grid.Height() / 2;
		UpdateScrollBars();
	}

	// The mouse over notes

	bool HitNote(CPoint Point, int &OutChannel, int &OutNote) const
	{
		if (!Grid.PtInRect(Point) || Notes.empty())
			return false;
		const int Row = RowAt(Point.x), Pitch = PitchAt(Point.y);
		for (int Pass = 0; Pass < 2; ++Pass) {	// the bright channel first
			for (size_t c = 0; c < Notes.size(); ++c) {
				if ((static_cast<int>(c) == Channel) != (Pass == 0))
					continue;
				const std::vector<RollNote> &List = Visible(static_cast<int>(c));
				const int Index = NoteAt(List, Row);
				if (Index >= 0 && List[static_cast<size_t>(Index)].Pitch == Pitch) {
					OutChannel = static_cast<int>(c);
					OutNote = Index;
					return true;
				}
			}
		}
		return false;
	}

	// On the right end of a note of the bright channel, where dragging changes its length?
	bool OnEnd(CPoint Point, const RollNote &Note) const
	{
		const CRect Rect = NoteRect(Note);
		const int Grab = std::min(SX(END_GRAB), std::max(2, Rect.Width() / 3));
		return Rect.Width() >= SX(4) && Point.x >= Rect.right - Grab && Point.x < Rect.right + 1;
	}

	int HitButton(CPoint Point) const
	{
		for (size_t i = 0; i < Buttons.size(); ++i) {
			if (Buttons[i].Rect.PtInRect(Point))
				return Buttons[i].Id;
		}
		return -1;
	}

	// The pattern editor and the sound

	// Moves the pattern editor's cursor
	void GoTo(int ToChannel, int Row)
	{
		CFamiTrackerView *pView = View();
		if (pView == NULL || ToChannel < 0 || ToChannel >= static_cast<int>(Notes.size()))
			return;
		Row = std::max(0, std::min(Row, TotalRows() - 1));
		const unsigned int Frame = static_cast<unsigned int>(Row) / Length, InFrame = static_cast<unsigned int>(Row) % Length;
		pView->SelectFrame(Frame);
		pView->GetPatternEditor()->MoveToRow(static_cast<int>(InFrame));
		pView->SelectChannel(static_cast<unsigned int>(ToChannel));
		if (ToChannel != Channel)
			Selection.clear();
		Channel = ToChannel;
		CursorFrame = pView->GetPatternEditor()->GetFrame();	// seen, so it doesn't scroll
		CursorRow = pView->GetPatternEditor()->GetRow();
		CursorChannel = pView->GetPatternEditor()->GetChannel();
	}

	// The problems panel's notes in sight (CPianoRollWnd::ShowNotes)
	void ShowNotes(int ToChannel, const int *pStarts, int Count, bool Select)
	{
		CFamiTrackerView *pView = View();
		if (pView == NULL || pStarts == NULL || Count <= 0)
			return;
		Layout();
		if (Dirty)
			Read();
		if (ToChannel < 0 || ToChannel >= static_cast<int>(Notes.size()))
			return;
		CancelDrag();
		PlacingClip = -1;
		// The pattern editor's cursor as it is now, seen, so the next look leaves the bright channel and the selection be
		CPatternEditor *pEditor = pView->GetPatternEditor();
		if (Select)
			pEditor->CancelSelection();		// one selection at a time
		PatternSelecting = pEditor->IsSelecting();
		CursorFrame = pEditor->GetFrame();
		CursorRow = pEditor->GetRow();
		CursorChannel = pEditor->GetChannel();
		Channel = ToChannel;
		Selection.clear();
		int Low = PITCHES, High = -1;
		for (int i = 0; i < Count; ++i) {
			const int Index = FindStart(pStarts[i]);
			if (Index < 0)
				continue;
			const RollNote &Note = Notes[static_cast<size_t>(Channel)][static_cast<size_t>(Index)];
			Low = std::min(Low, Note.Pitch);
			High = std::max(High, Note.Pitch);
			if (Select && !Contains(Selection, Note.Start))
				Selection.push_back(Note.Start);
		}
		FollowRow(pStarts[0], false);
		if (High >= 0 && (Low < PitchAt(Grid.bottom - 1) || High > PitchAt(Grid.top)))
			ScrollY = (PITCHES - 1 - (Low + High) / 2) * KeyHeight() - Grid.Height() / 2;
		Placed = true;
		UpdateScrollBars();
		pWnd->Invalidate(FALSE);
	}

	// The instrument list follows the note clicked
	void PickInstrument(int Start)
	{
		const int Index = FindStart(Start);
		CMainFrame *pMain = Main();
		if (Index >= 0 && pMain != NULL) {
			const int Instrument = Notes[static_cast<size_t>(Channel)][static_cast<size_t>(Index)].Instrument;
			if (Instrument >= 0 && Instrument < MAX_INSTRUMENTS && Doc()->IsInstrumentUsed(static_cast<unsigned int>(Instrument)))
				pMain->SelectInstrument(Instrument);
		}
	}

	void Preview(int Pitch, int Instrument, DWORD For = 0)
	{
		CMainFrame *pMain = Main();
		if (pMain == NULL || Pitch < 0 || Pitch >= PITCHES || Channel >= static_cast<int>(Notes.size()))
			return;
		if (PreviewChannel == Channel && PreviewPitch == Pitch && For == 0)
			return;
		StopPreview();
		if (Instrument < 0 || Instrument >= MAX_INSTRUMENTS)
			Instrument = pMain->GetSelectedInstrument();
		stChanNote Note;
		memset(&Note, 0, sizeof(Note));
		Note.Note = static_cast<unsigned char>(Pitch % 12 + 1);
		Note.Octave = static_cast<unsigned char>(Pitch / 12);
		Note.Vol = MAX_VOLUME;
		Note.Instrument = static_cast<unsigned char>((Instrument >= 0 && Instrument < MAX_INSTRUMENTS) ? Instrument : MAX_INSTRUMENTS);
		theApp.GetSoundGenerator()->QueueNote(Channel, Note, NOTE_PRIO_2);
		PreviewChannel = Channel;
		PreviewPitch = Pitch;
		PreviewUntil = For > 0 ? ::GetTickCount() + For : 0;
	}

	void StopPreview()
	{
		if (PreviewChannel < 0)
			return;
		stChanNote Note;
		memset(&Note, 0, sizeof(Note));
		Note.Note = HALT;
		Note.Vol = MAX_VOLUME;
		Note.Instrument = MAX_INSTRUMENTS;
		theApp.GetSoundGenerator()->QueueNote(PreviewChannel, Note, NOTE_PRIO_2);
		PreviewChannel = PreviewPitch = -1;
		PreviewUntil = 0;
	}

	// Follows the song, the cursor and playback; true if the picture changed
	bool Poll()
	{
		CFamiTrackerView *pView = View();
		CFamiTrackerDoc *pDoc = Doc();
		CMainFrame *pMain = Main();
		if (pView == NULL || pDoc == NULL || pMain == NULL || !pDoc->IsFileLoaded())
			return false;
		bool Changed = false;

		// Another track, or frames, rows or channels changed without a notice
		if (static_cast<unsigned int>(pMain->GetSelectedTrack()) != Track || Track >= pDoc->GetTrackCount() ||
			pDoc->GetFrameCount(Track) != Frames || pDoc->GetPatternLength(Track) != Length ||
			pDoc->GetAvailableChannels() != Notes.size() || pDoc->GetFirstHighlight() != Beat || pDoc->GetSecondHighlight() != Bar) {
			CancelDrag();
			if (static_cast<unsigned int>(pMain->GetSelectedTrack()) != Track)
				RangeKept = false;
			Read();
			UpdateScrollBars();
			Changed = true;
		}

		// The cursor, whose channel is the bright one
		const CPatternEditor *pEditor = pView->GetPatternEditor();
		// One selection at a time: one made in the pattern editor ends the piano roll's
		const bool PatternSelects = pEditor->IsSelecting();
		if (PatternSelects && !PatternSelecting && (!Selection.empty() || RangeKept) && Drag == DRAG_NONE) {
			Selection.clear();
			RangeKept = false;
			Changed = true;
		}
		PatternSelecting = PatternSelects;
		const int Frame = pEditor->GetFrame(), Row = pEditor->GetRow(), CursorCh = pEditor->GetChannel();
		if (CursorCh != CursorChannel) {
			const int Now = std::max(0, std::min(CursorCh, static_cast<int>(Notes.size()) - 1));
			if (Now != Channel) {
				CancelDrag();
				Selection.clear();
				PlacingClip = -1;
			}
			Channel = Now;
			Changed = true;
		}
		const bool Moved = Frame != CursorFrame || Row != CursorRow;
		CursorFrame = Frame;
		CursorRow = Row;
		CursorChannel = CursorCh;

		// Playback
		const bool NowPlaying = theApp.IsPlaying();
		int Played = -1;
		CSoundGen *pSound = theApp.GetSoundGenerator();
		if (NowPlaying && pSound != NULL && static_cast<unsigned int>(pSound->GetPlayerTrack()) == Track)
			Played = pSound->GetPlayerFrame() * static_cast<int>(Length) + pSound->GetPlayerRow();
		if (NowPlaying != Playing || Played != PlayRow) {
			Playing = NowPlaying;
			PlayRow = Played;
			if (Played >= 0 && pView->GetFollowMode() && Drag == DRAG_NONE)
				FollowRow(Played, true);
			Changed = true;
		}
		else if (Moved) {
			if (!Playing && Drag == DRAG_NONE)
				FollowRow(Frame * static_cast<int>(Length) + Row, false);
			Changed = true;
		}

		// A note played with the keys stops; a message goes away
		if (PreviewUntil != 0 && static_cast<int>(::GetTickCount() - PreviewUntil) >= 0)
			StopPreview();
		if (!Message.IsEmpty() && ::GetTickCount() - MessageTime >= MessageMs) {
			Message.Empty();
			Changed = true;
		}
		return Changed;
	}

	// Editing

	// The bright channel as it is, with room for one more frame
	void BeginEdit(edit_t Kind)
	{
		EditKind = Kind;
		SongRows = TotalRows();
		ReadChannel(Channel, Before, EditRows(), true);
		After = Before;
		Previewing = false;
		Erased.clear();
		Marked.clear();
	}

	// What the notes being moved land on
	std::vector<int> MovedPitches() const
	{
		std::vector<int> Pitches;
		for (size_t i = 0; i < Moving.size(); ++i) {
			const int Index = Before.FindNote(Moving[i]);
			const int Pitch = Index >= 0 ? Before.GetNote(Index).Pitch : 0;
			const int To = (KeyApplies() && MoveSteps != 0) ? ftapi::StepInKey(TheKey(), Pitch, MoveSteps) : Pitch + MoveSemitones;
			Pitches.push_back(ClampPitch(To));
		}
		return Pitches;
	}

	// A painted melody's notes: a note per grid step, steps of the same pitch joined
	struct Painted {
		int Start, End, Pitch;
	};
	int NextStep(int Row) const { return DragAlt ? Row + 1 : NextLine(Row); }
	std::vector<Painted> PaintedNotes() const
	{
		std::vector<Painted> Out;
		for (std::map<int, int>::const_iterator It = Stroke.begin(); It != Stroke.end(); ++It) {
			const int End = std::min(NextStep(It->first), EditRows());
			if (!Out.empty() && Out.back().End == It->first && Out.back().Pitch == It->second)
				Out.back().End = End;
			else {
				const Painted Note = { It->first, End, It->second };
				Out.push_back(Note);
			}
		}
		return Out;
	}

	void Apply(CChannelNotes &Notes_) const
	{
		switch (EditKind) {
		case EDIT_ADD:
			Notes_.Add(EditStart, EditEnd, EditPitch, EditInstrument);
			break;
		case EDIT_REMOVE:
			for (size_t i = 0; i < Erased.size(); ++i)
				Notes_.Remove(Erased[i]);
			break;
		case EDIT_RESIZE:
			Notes_.Resize(EditStart, EditEnd);
			break;
		case EDIT_MOVE:
			if (!Moving.empty()) {
				const std::vector<int> Pitches = MovedPitches();
				Notes_.Move(&Moving[0], static_cast<int>(Moving.size()), MoveRows, &Pitches[0], MoveCopy);
			}
			break;
		case EDIT_PAINT: {
			const std::vector<Painted> Melody = PaintedNotes();
			for (size_t i = 0; i < Melody.size(); ++i)
				Notes_.Add(Melody[i].Start, Melody[i].End, Melody[i].Pitch, EditInstrument);
			break;
		}
		case EDIT_PASTE:
			Notes_.Paste(Copied, PasteRow, 0);
			break;
		case EDIT_INSTRUMENT:
			if (!Selection.empty())
				Notes_.SetInstrument(&Selection[0], static_cast<int>(Selection.size()), GiveInstrument);
			break;
		case EDIT_EFFECT:
			if (!Selection.empty())
				EffectFits = PutEffect(Notes_);
			break;
		case EDIT_NONE:
			break;
		}
	}

	// Effects

	int BeatRows() const { return Beat > 0 ? Beat : 4; }

	// The chord of the key on a note, as an arpeggio's two notes above it: its third and fifth in
	// the key's mode (a pentatonic key's from its major or minor); a minor key's raised seventh a
	// diminished chord, and a major chord outside the key
	unsigned char KeyChord(int Pitch) const
	{
		if (!KeyApplies())
			return 0x47;
		const ftapi::Key K = TheKey().K;
		const int *pSteps = ftapi::ModeSteps(K.Mode);
		auto InMode = [&](int p) {
			const int Relative = ((p - K.Tonic) % 12 + 12) % 12;
			for (int d = 0; d < 7; ++d)
				if (pSteps[d] == Relative)
					return true;
			return false;
		};
		if (!InMode(Pitch))
			return InKey(Pitch) ? 0x36 : 0x47;
		int Steps[2] = { 0, 0 };
		int Above = Pitch;
		for (int Step = 1; Step <= 4; ++Step) {
			do
				++Above;
			while (!InMode(Above));
			if (Step == 2)
				Steps[0] = Above - Pitch;
			if (Step == 4)
				Steps[1] = Above - Pitch;
		}
		return static_cast<unsigned char>((std::min(15, Steps[0]) << 4) | std::min(15, Steps[1]));
	}

	// The chosen effect on the selected notes; false if some didn't fit in the effect columns
	bool PutEffect(CChannelNotes &Notes_) const
	{
		const EffectChoice &Choice = EFFECT_CHOICES[ChosenEffect];
		const int Count = static_cast<int>(Selection.size());
		switch (Choice.Action) {
		case FX_LASTING:
		case FX_KEY_CHORD: {
			std::vector<unsigned char> Params(Selection.size(), static_cast<unsigned char>(Choice.Value));
			std::vector<int> Delays(Selection.size(), 0);
			for (size_t k = 0; k < Selection.size(); ++k) {
				const int Index = Notes_.FindNote(Selection[k]);
				if (Index < 0)
					continue;
				const CChannelNotes::Note &Note = Notes_.GetNote(Index);
				const int Rows = Note.End - Note.Start;
				if (Choice.Number == EF_VIBRATO || Choice.Number == EF_TREMOLO)
					Delays[k] = Rows >= 2 ? std::max(1, BeatRows() / 4) : 0;	// it starts once the note has begun
				else if (Choice.Number == EF_VOLUME_SLIDE)
					Delays[k] = Rows / 2;		// the second half fades
				if (Choice.Action == FX_KEY_CHORD)
					Params[k] = KeyChord(Note.Pitch);
			}
			return Notes_.SetLasting(&Selection[0], Count, Choice.Number, &Params[0], &Delays[0]);
		}
		case FX_SLIDE_IN:
			return Notes_.SetSlideIn(&Selection[0], Count, Choice.Value, Choice.Speed);
		case FX_BEND:
			return Notes_.SetBend(&Selection[0], Count, Choice.Value, Choice.Speed, std::max(1, BeatRows() / 2));
		case FX_CLEAR:
			return Notes_.ClearEffects(&Selection[0], Count);
		case FX_PAN:
			return Notes_.SetPan(&Selection[0], Count, static_cast<unsigned char>(Choice.Value));
		}
		return true;
	}

	// Which effects suit the bright channel: just the pan for samples, no pitch effects for drums,
	// nothing with the volume for the triangle (it has none)
	bool EffectFitsChannel(int Group) const
	{
		if (Channel >= static_cast<int>(ChanIDs.size()))
			return false;
		if (Group == GROUP_PAN)
			return true;
		if (IsDPCM(Channel))
			return false;
		if (IsNoise(Channel))
			return Group == GROUP_TREMOLO || Group == GROUP_FADE || Group == GROUP_NONE;
		if (ChanIDs[static_cast<size_t>(Channel)] == CHANID_TRIANGLE)
			return Group != GROUP_TREMOLO && Group != GROUP_FADE;
		return true;
	}

	// The Effects menu, at Point (screen coordinates), for the selected notes
	void EffectsMenu(CPoint Point)
	{
		if (Dirty)
			Read();
		if (Notes.empty())
			return;
		const bool Any = !Selection.empty();
		CMenu Menu;
		Menu.CreatePopupMenu();
		if (Selection.empty())
			Menu.AppendMenu(MF_STRING | MF_GRAYED, 0, _T("Select notes first: click one, or drag around some with the Select tool (or Ctrl)"));
		else {
			CString Text;
			Text.Format(IsDPCM(Channel) ? _T("For the %s selected (samples play without effects, but they can be panned):") :
				_T("For the %s selected:"), static_cast<LPCTSTR>(Plural(static_cast<int>(Selection.size()), _T("note"), _T("notes"))));
			Menu.AppendMenu(MF_STRING | MF_GRAYED, 0, Text);
		}
		Menu.AppendMenu(MF_SEPARATOR);
		for (int Group = 0; Group < EFFECT_GROUPS; ++Group) {
			CMenu Choices;
			Choices.CreatePopupMenu();
			for (int i = 0; i < EFFECT_CHOICE_COUNT; ++i) {
				const EffectChoice &Choice = EFFECT_CHOICES[i];
				if (Choice.Group != Group)
					continue;
				const bool Off = Choice.Action != FX_KEY_CHORD && Choice.Action != FX_PAN && Choice.Value == 0;
				if (Off)
					Choices.AppendMenu(MF_SEPARATOR);
				CString Item = Choice.Name;
				UINT Flags = MF_STRING;
				if (Choice.Action == FX_KEY_CHORD && !KeyApplies()) {
					Item += _T(" (no key yet: draw some notes, or choose one with Key)");
					Flags |= MF_GRAYED;
				}
				const CString Code = EffectCode(Choice);
				if (!Code.IsEmpty())
					Item += _T("\t") + Code;
				Choices.AppendMenu(Flags, MENU_EFFECT + i, Item);
			}
			CString Name = GROUP_NAMES[Group];
			if (Any && !EffectFitsChannel(Group))
				Name += IsDPCM(Channel) ? _T(" (not for samples)") : IsNoise(Channel) ? _T(" (not for drums)") : _T(" (the triangle has no volume)");
			Menu.AppendMenu(MF_POPUP | (Any && EffectFitsChannel(Group) ? 0 : MF_GRAYED), reinterpret_cast<UINT_PTR>(Choices.Detach()), Name);
		}
		Menu.AppendMenu(MF_SEPARATOR);
		for (int i = 0; i < EFFECT_CHOICE_COUNT; ++i) {
			if (EFFECT_CHOICES[i].Group == GROUP_NONE)
				Menu.AppendMenu(MF_STRING | (Any && !IsDPCM(Channel) ? 0 : MF_GRAYED), MENU_EFFECT + i, EFFECT_CHOICES[i].Name);
		}
		const int Choice = static_cast<int>(Menu.TrackPopupMenu(TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
			Point.x, Point.y, pWnd));
		if (Choice >= static_cast<int>(MENU_EFFECT) && Choice < static_cast<int>(MENU_EFFECT) + EFFECT_CHOICE_COUNT)
			ApplyEffect(Choice - static_cast<int>(MENU_EFFECT));
	}

	void ApplyEffect(int Index)
	{
		if (Selection.empty() || Index < 0 || Index >= EFFECT_CHOICE_COUNT)
			return;
		const EffectChoice &Choice = EFFECT_CHOICES[Index];
		BeginEdit(EDIT_EFFECT);
		ChosenEffect = Index;
		EffectFits = true;
		Recompute();
		int First = 0, Last = 0;
		const bool Changed = After.Compare(Before, First, Last);
		const bool Fitted = EffectFits;
		if (!Commit(Selection.front()))
			return;
		CString Text;
		const CString Notes_ = Plural(static_cast<int>(Selection.size()), _T("note"), _T("notes"));
		const CString Code = EffectCode(Choice);
		if (!Changed)
			Text.Format(_T("The %s had that already."), static_cast<LPCTSTR>(Notes_));
		else if (Choice.Group == GROUP_NONE)
			Text.Format(_T("Took the effects off %s. Ctrl+Z puts them back."), static_cast<LPCTSTR>(Notes_));
		else
			Text.Format(_T("%s: %s%s%s%s on %s. Ctrl+Z undoes it."), GROUP_NAMES[Choice.Group], Choice.Name,
				Code.IsEmpty() ? _T("") : _T(" ("), static_cast<LPCTSTR>(Code), Code.IsEmpty() ? _T("") : _T(")"), static_cast<LPCTSTR>(Notes_));
		if (Choice.Action == FX_PAN && Changed)
			Text += _T(" The notes after them keep their pan; you hear it in stereo (an NSF or the NES plays it in the middle).");
		if (!Fitted)
			Text += _T(" Some effects didn't fit: add an effect column with the small arrow at the channel's name in the pattern editor.");
		else if (Choice.Action == FX_BEND && Choice.Value != 0)
			Text += _T(" (Notes of one row are too short to bend.)");
		Say(Text);
	}

	// What works on a note, in words: "vibrato 452 after 1 row, chord 047 (major)"
	CString EffectsText(const RollNote &Note) const
	{
		CString Text;
		for (size_t i = 0; i < Note.Effects.size(); ++i) {
			const CChannelNotes::EffectSpan &Span = Note.Effects[i];
			if (IsSlideEffect(Span.Number) && Span.From == Note.Start)
				continue;	// the slide into it is told already
			CString Code, Part;
			if (Span.Number > EF_NONE && Span.Number < EF_COUNT)
				Code.Format(_T("%c%02X"), EFF_CHAR[Span.Number - 1], Span.Param);
			const bool Carried = Span.From == Note.Start && Span.Carried;
			switch (Span.Number) {
			case EF_VIBRATO:
				Part = _T("vibrato ") + Code;
				break;
			case EF_TREMOLO:
				Part = _T("tremolo ") + Code;
				break;
			case EF_ARPEGGIO: {
				Part = _T("chord ") + Code;
				const CString Name = ChordName(Span.Param);
				if (!Name.IsEmpty())
					Part += _T(" (") + Name + _T(")");
				break;
			}
			case EF_PORTAMENTO:
				Part = (Span.From == Note.Start ? _T("glides in ") : _T("glide ")) + Code;
				break;
			case EF_PORTA_UP:
				Part = _T("pitch rising ") + Code;
				break;
			case EF_PORTA_DOWN:
				Part = _T("pitch falling ") + Code;
				break;
			case EF_VOLUME_SLIDE:
				Part = ((Span.Param & 0x0F) != 0 ? _T("fades out ") : _T("fades in ")) + Code;
				break;
			case EF_SLIDE_UP:
			case EF_SLIDE_DOWN:
				Part.Format(_T("bends %s %d %s %s"), Span.Number == EF_SLIDE_UP ? _T("up") : _T("down"), Span.Param & 0x0F,
					(Span.Param & 0x0F) == 1 ? _T("semitone") : _T("semitones"), static_cast<LPCTSTR>(Code));
				break;
			default:
				Part = Code;
				break;
			}
			if (Carried)
				Part += _T(" from an earlier note");
			else if (Span.From > Note.Start && !IsSlideEffect(Span.Number) && Span.To > Span.From + 1) {
				CString After;
				After.Format(_T(" after %s"), static_cast<LPCTSTR>(Plural(Span.From - Note.Start, _T("row"), _T("rows"))));
				Part += After;
			}
			if (!Text.IsEmpty())
				Text += _T(", ");
			Text += Part;
		}
		return Text;
	}

	static bool IsSlideEffect(unsigned char Number)
	{
		return Number == EF_SLIDE_UP || Number == EF_SLIDE_DOWN;
	}

	// Works the edit out again on the channel as it was, for the picture
	void Recompute()
	{
		After = Before;
		Apply(After);
		int First = 0, Last = 0, Rows = SongRows;
		if (After.Compare(Before, First, Last) && Last >= SongRows) {
			// The song grows: a note that sounded on to its old end still ends there
			After = Before;
			const int Open = After.NoteAt(SongRows - 1);
			if (Open >= 0 && After.GetNote(Open).End > SongRows)
				After.Resize(After.GetNote(Open).Start, SongRows);
			Apply(After);
			Rows = (Last / static_cast<int>(Length) + 1) * static_cast<int>(Length);
		}
		Previewing = true;
		PreviewNotes = RollNotes(After, Rows);
		PreviewRows = Rows;

		// The notes to show selected
		Marked.clear();
		switch (EditKind) {
		case EDIT_ADD:
			Marked.push_back(EditStart);
			break;
		case EDIT_MOVE:
			for (size_t i = 0; i < Moving.size(); ++i)
				Marked.push_back(Moving[i] + MoveRows);
			break;
		case EDIT_PAINT: {
			const std::vector<Painted> Melody = PaintedNotes();
			for (size_t i = 0; i < Melody.size(); ++i)
				Marked.push_back(Melody[i].Start);
			break;
		}
		case EDIT_PASTE:
			for (int i = 0; i < Copied.GetNoteCount(); ++i)
				Marked.push_back(PasteRow + Copied.GetNote(i).Start);
			break;
		default:
			Marked = Selection;
			break;
		}
	}

	// Writes the edit into the song, one undoable action; the cursor goes to CursorRow
	bool Commit(int CursorRow)
	{
		Previewing = false;
		EditKind = EDIT_NONE;
		int First = 0, Last = 0;
		if (!After.Compare(Before, First, Last))
			return true;
		std::vector<stChanNote> Cells(static_cast<size_t>(Last - First + 1));
		for (size_t i = 0; i < Cells.size(); ++i)
			After.GetCell(First + static_cast<int>(i), Cells[i]);
		CString Error;
		if (!WriteChannelRows(Main(), Channel, First, &Cells[0], static_cast<int>(Cells.size()), CursorRow, Error)) {
			Say(Error);
			return false;
		}
		Dirty = true;
		return true;
	}

	void CancelDrag()
	{
		if (Drag == DRAG_NONE)
			return;
		Drag = DRAG_NONE;
		EditKind = EDIT_NONE;
		Previewing = false;
		Band.SetRectEmpty();
		StopPreview();
		if (::GetCapture() == pWnd->GetSafeHwnd())
			::ReleaseCapture();
	}

	// The mouse

	void MouseDown(CPoint Point, UINT Flags, bool Right)
	{
		Layout();
		if (Drag != DRAG_NONE)
			return;
		if (Dirty)
			Read();
		const bool Ctrl = (Flags & MK_CONTROL) != 0, Shift = (Flags & MK_SHIFT) != 0;
		DragFrom = Point;
		DragMoved = false;
		DragShift = Shift;
		DragAlt = AltHeld();

		if (!Right) {
			const int Id = HitButton(Point);
			if (Id >= 0) {
				Press(Id);
				return;
			}
		}
		if (Notes.empty())
			return;
		if (Right && ChordStrip.PtInRect(Point) && PlacingClip < 0) {
			// Chords for the bar under the mouse (or the selection it's in)
			CPoint Screen = Point;
			pWnd->ClientToScreen(&Screen);
			ChordsMenu(Screen, std::max(0, std::min(RowAt(Point.x), EditRows() - 1)));
			return;
		}
		RangeKept = false;
		if (PlacingClip >= 0) {
			if (Right)
				PlacingClip = -1;	// never mind
			else if (Grid.PtInRect(Point))
				PlaceClipHere();
			return;
		}
		if (Keys.PtInRect(Point)) {
			// Play the key, and the ones the mouse slides over
			if (!Right) {
				Drag = DRAG_KEYS;
				Preview(PitchAt(Point.y), NewInstrument(0));
			}
			return;
		}
		if (Ruler.PtInRect(Point) || ChordStrip.PtInRect(Point)) {
			if (!Right) {
				// The Select tool: dragging along the ruler selects the notes in those rows
				Drag = Tool == TOOL_SELECT ? DRAG_RANGE : DRAG_RULER;
				RangeFrom = RangeTo = std::max(0, std::min(RowAt(Point.x), EditRows() - 1));
				GoTo(Channel, RowAt(Point.x));
			}
			return;
		}
		if (!Grid.PtInRect(Point) || RowAt(Point.x) >= EditRows())
			return;

		if (Right) {
			BeginEdit(EDIT_REMOVE);
			Drag = DRAG_ERASE;
			EraseAt(Point);
			return;
		}
		int HitChannel = -1, HitIndex = -1;
		const bool OnNote = HitNote(Point, HitChannel, HitIndex);
		if (Ctrl || (Tool == TOOL_SELECT && !OnNote)) {
			// A box around notes selects them (the Select tool needs no Ctrl)
			Drag = DRAG_SELECT;
			Band = CRect(Point, Point);
			return;
		}
		if (OnNote && HitChannel == Channel) {
			const RollNote Hit = Notes[static_cast<size_t>(Channel)][static_cast<size_t>(HitIndex)];
			if (OnEnd(Point, Hit)) {
				BeginEdit(EDIT_RESIZE);
				Drag = DRAG_RESIZE;
				EditStart = Hit.Start;
				EditEnd = Hit.End;
				return;
			}
			// Grab it, with the other selected notes; with Shift, a copy
			GrabWasSelected = Contains(Selection, Hit.Start);
			if (!GrabWasSelected) {
				if (!Shift)
					Selection.clear();
				Selection.push_back(Hit.Start);
			}
			BeginEdit(EDIT_MOVE);
			Drag = DRAG_MOVE;
			Moving = Selection;
			GrabStart = Hit.Start;
			GrabRow = RowAtF(Point.x);
			GrabPitch = MovePitch = Hit.Pitch;
			MoveRows = MoveSteps = MoveSemitones = 0;
			MoveCopy = Shift;
			Preview(Hit.Pitch, Hit.Instrument);
			return;
		}
		if (OnNote && !Shift) {
			// Another channel's note: that channel is edited from now on
			GoTo(HitChannel, Notes[static_cast<size_t>(HitChannel)][static_cast<size_t>(HitIndex)].Start);
			return;
		}

		// An empty spot: a new note, or a melody painted from here
		Selection.clear();
		const int Row = RowAt(Point.x);
		if (Tool == TOOL_PAINT) {
			BeginEdit(EDIT_PAINT);
			Drag = DRAG_PAINT;
			EditInstrument = NewInstrument(Row);
			Stroke.clear();
			StrokeLast = -1;
			PaintAt(Point);
		}
		else {
			BeginEdit(EDIT_ADD);
			Drag = DRAG_DRAW;
			EditStart = DragAlt ? Row : SnapDown(Row);
			EditEnd = std::min(NextStep(EditStart), EditRows());
			EditPitch = SnapPitch(PitchAtF(Point.y));
			EditInstrument = NewInstrument(EditStart);
			Recompute();
			Preview(EditPitch, EditInstrument);
		}
	}

	void MouseMove(CPoint Point)
	{
		Layout();
		if (std::abs(Point.x - DragFrom.x) >= SX(DRAG_DISTANCE) || std::abs(Point.y - DragFrom.y) >= SY(DRAG_DISTANCE))
			DragMoved = true;
		switch (Drag) {
		case DRAG_NONE:
			Hover(Point);
			if (PlacingClip >= 0) {
				const int Row = Grid.PtInRect(Point) ? RowAt(Point.x) : -1;
				const int Place = Row < 0 ? -1 : std::min(AltHeld() ? Row : SnapDown(Row), TotalRows() - 1);
				if (Place != PlaceRow) {
					PlaceRow = Place;
					pWnd->Invalidate(FALSE);
				}
			}
			return;
		case DRAG_KEYS: {
			const int Pitch = PitchAt(Point.y);
			if (Pitch != PreviewPitch && Pitch >= 0 && Pitch < PITCHES)
				Preview(Pitch, NewInstrument(0));
			break;
		}
		case DRAG_RULER:
			GoTo(Channel, std::max(0, RowAt(Point.x)));
			break;
		case DRAG_RANGE: {
			const int Row = std::max(0, std::min(RowAt(Point.x), EditRows() - 1));
			if (DragMoved && Row != RangeTo) {
				RangeTo = Row;
				SelectRows(std::min(RangeFrom, RangeTo), std::max(RangeFrom, RangeTo) + 1);
			}
			break;
		}
		case DRAG_SELECT:
			Band = CRect(std::min(DragFrom.x, Point.x), std::min(DragFrom.y, Point.y), std::max(DragFrom.x, Point.x) + 1,
				std::max(DragFrom.y, Point.y) + 1);
			if (DragMoved)
				SelectBand();
			break;
		case DRAG_ERASE:
			EraseAt(Point);
			break;
		case DRAG_DRAW: {
			const int Row = std::max(EditStart, static_cast<int>(std::floor(RowAtF(Point.x))));
			EditEnd = std::min(std::max(NextStep(Row), EditStart + 1), EditRows());
			const int Pitch = SnapPitch(PitchAtF(Point.y));
			if (Pitch != EditPitch) {
				EditPitch = Pitch;
				Preview(EditPitch, EditInstrument);
			}
			Recompute();
			break;
		}
		case DRAG_MOVE: {
			if (!DragMoved)
				return;
			const int Step = DragAlt ? 1 : GridRows();
			int Rows = static_cast<int>(std::floor((RowAtF(Point.x) - GrabRow) / Step + 0.5)) * Step;
			int First = EditRows(), Last = 0;
			for (size_t i = 0; i < Moving.size(); ++i) {
				First = std::min(First, Moving[i]);
				Last = std::max(Last, Moving[i]);
			}
			Rows = std::max(-First, std::min(Rows, EditRows() - 1 - Last));
			const int Target = SnapPitch(PitchAtF(Point.y));
			if (Rows != MoveRows || Target != MovePitch || !Previewing) {
				const bool NewPitch = Target != MovePitch;
				MoveRows = Rows;
				MovePitch = Target;
				const bool ByKey = SnapsToKey();	// by steps of the key, so they stay in it; otherwise by semitones
				MoveSteps = ByKey ? ftapi::KeyStepsBetween(TheKey(), GrabPitch, Target) : 0;
				MoveSemitones = ByKey ? 0 : Target - GrabPitch;
				Recompute();
				if (NewPitch) {
					const int Index = Before.FindNote(GrabStart);
					Preview(Target, Index >= 0 ? Before.GetNote(Index).Instrument : -1);
				}
			}
			break;
		}
		case DRAG_RESIZE: {
			const int End = DragAlt ? static_cast<int>(std::floor(RowAtF(Point.x) + 0.5)) : NearestLine(RowAtF(Point.x));
			EditEnd = std::max(EditStart + 1, std::min(End, EditRows()));
			Recompute();
			break;
		}
		case DRAG_PAINT:
			PaintAt(Point);
			break;
		}
		pWnd->Invalidate(FALSE);
	}

	void MouseUp(bool Right)
	{
		const drag_t Was = Drag;
		if (Was == DRAG_NONE)
			return;
		Drag = DRAG_NONE;
		StopPreview();
		switch (Was) {
		case DRAG_DRAW:
			if (Commit(EditStart))
				Selection.assign(1, EditStart);
			break;
		case DRAG_PAINT: {
			const std::vector<Painted> Melody = PaintedNotes();
			if (!Melody.empty() && Commit(Melody.front().Start)) {
				Selection.clear();
				for (size_t i = 0; i < Melody.size(); ++i)
					Selection.push_back(Melody[i].Start);
			}
			break;
		}
		case DRAG_MOVE:
			if (!DragMoved) {
				// A click: the note is the one selected, and the cursor and instrument list follow it.
				// Shift+click takes a selected note out of the selection.
				EditKind = EDIT_NONE;
				Previewing = false;
				if (DragShift && GrabWasSelected)
					Selection.erase(std::find(Selection.begin(), Selection.end(), GrabStart));
				else if (!DragShift)
					Selection.assign(1, GrabStart);
				GoTo(Channel, GrabStart);
				PickInstrument(GrabStart);
			}
			else if (Commit(GrabStart + MoveRows)) {
				Selection.clear();
				for (size_t i = 0; i < Moving.size(); ++i)
					Selection.push_back(Moving[i] + MoveRows);
			}
			break;
		case DRAG_RESIZE:
			Commit(EditStart);
			break;
		case DRAG_ERASE:
			Commit(-1);
			for (size_t i = 0; i < Erased.size(); ++i) {
				const std::vector<int>::iterator It = std::find(Selection.begin(), Selection.end(), Erased[i]);
				if (It != Selection.end())
					Selection.erase(It);
			}
			break;
		case DRAG_SELECT:
			if (!DragMoved) {
				// Ctrl+click: a note in or out of the selection
				int HitChannel = -1, HitIndex = -1;
				if (HitNote(DragFrom, HitChannel, HitIndex) && HitChannel == Channel) {
					const int Start = Notes[static_cast<size_t>(Channel)][static_cast<size_t>(HitIndex)].Start;
					const std::vector<int>::iterator It = std::find(Selection.begin(), Selection.end(), Start);
					if (It != Selection.end())
						Selection.erase(It);
					else
						Selection.push_back(Start);
				}
				else
					Selection.clear();
			}
			Band.SetRectEmpty();
			break;
		case DRAG_RANGE:
			if (DragMoved) {
				SelectRows(std::min(RangeFrom, RangeTo), std::max(RangeFrom, RangeTo) + 1);
				RangeKept = true;
			}
			break;
		default:
			break;
		}
		// One selection at a time: the pattern editor's goes when the piano roll has one
		if (!Selection.empty())
			CancelPatternSelection();
		EditKind = EDIT_NONE;
		Previewing = false;
	}

	void DoubleClick(CPoint Point, UINT Flags)
	{
		// On another channel's note: that channel is edited from now on, even with Shift held. On one of
		// the bright channel's notes: the Effects menu for it
		int HitChannel = -1, HitIndex = -1;
		Layout();
		const bool Hit = Drag == DRAG_NONE && PlacingClip < 0 && HitNote(Point, HitChannel, HitIndex);
		if (Hit && HitChannel != Channel)
			GoTo(HitChannel, Notes[static_cast<size_t>(HitChannel)][static_cast<size_t>(HitIndex)].Start);
		else if (Hit && (Flags & (MK_CONTROL | MK_SHIFT)) == 0 && !OnEnd(Point, Notes[static_cast<size_t>(Channel)][static_cast<size_t>(HitIndex)])) {
			const int Start = Notes[static_cast<size_t>(Channel)][static_cast<size_t>(HitIndex)].Start;
			if (!Contains(Selection, Start))
				Selection.assign(1, Start);
			pWnd->Invalidate(FALSE);
			pWnd->UpdateWindow();
			CPoint Screen = Point;
			pWnd->ClientToScreen(&Screen);
			EffectsMenu(Screen);
		}
		else
			MouseDown(Point, Flags, false);
	}

	void Hover(CPoint Point)
	{
		int HoverCh = -1, Note = -1;
		HitNote(Point, HoverCh, Note);
		const int Chord = ChordAt(Point);
		if (Chord != HoverChord) {
			HoverChord = Chord;
			pWnd->Invalidate(FALSE);
		}
		if (HoverCh != HoverChannel || Note != HoverNote) {
			HoverChannel = HoverCh;
			HoverNote = Note;
			pWnd->Invalidate(FALSE);
		}
	}

	void EraseAt(CPoint Point)
	{
		int HitChannel = -1, HitIndex = -1;
		if (!HitNote(Point, HitChannel, HitIndex) || HitChannel != Channel)
			return;
		const int Start = Visible(Channel)[static_cast<size_t>(HitIndex)].Start;
		if (Contains(Erased, Start))
			return;
		Erased.push_back(Start);
		Recompute();
	}

	// The bright channel's notes that start in rows [From, To)
	void SelectRows(int From, int To)
	{
		Selection.clear();
		if (Channel < 0 || Channel >= static_cast<int>(Notes.size()))
			return;
		const std::vector<RollNote> &List = Notes[static_cast<size_t>(Channel)];
		for (size_t i = 0; i < List.size(); ++i)
			if (List[i].Start >= From && List[i].Start < To)
				Selection.push_back(List[i].Start);
	}

	void CancelPatternSelection()
	{
		CFamiTrackerView *pView = View();
		if (pView != NULL && pView->GetPatternEditor()->IsSelecting()) {
			pView->GetPatternEditor()->CancelSelection();
			pView->Invalidate(FALSE);
		}
		PatternSelecting = false;
	}

	void SelectBand()
	{
		Selection.clear();
		if (Channel >= static_cast<int>(Notes.size()))
			return;
		const std::vector<RollNote> &List = Notes[static_cast<size_t>(Channel)];
		for (size_t i = 0; i < List.size(); ++i) {
			const CRect Rect = NoteRect(List[i]);
			if (Rect.left < Band.right && Rect.right > Band.left && Rect.top < Band.bottom && Rect.bottom > Band.top)
				Selection.push_back(List[i].Start);
		}
	}

	// Painting a melody: the step under the mouse gets the pitch under it, and steps the mouse skipped
	// slope from the last pitch to this one
	void PaintAt(CPoint Point)
	{
		const int Row = std::max(0, std::min(RowAt(Point.x), EditRows() - 1));
		const int Step = DragAlt ? Row : SnapDown(Row);
		const int Pitch = SnapPitch(PitchAtF(Point.y));
		if (StrokeLast >= 0 && Step != StrokeLast) {
			const int From = Stroke[StrokeLast];
			std::vector<int> Between;
			if (Step > StrokeLast) {
				for (int s = NextStep(StrokeLast); s < Step; s = NextStep(s))
					Between.push_back(s);
			}
			else {
				for (int s = DragAlt ? StrokeLast - 1 : SnapDown(StrokeLast - 1); s > Step; s = DragAlt ? s - 1 : SnapDown(s - 1))
					Between.push_back(s);
			}
			for (size_t i = 0; i < Between.size(); ++i) {
				const double Part = double(i + 1) / double(Between.size() + 1);
				Stroke[Between[i]] = SnapPitch(From + static_cast<int>(std::floor((Pitch - From) * Part + 0.5)));
			}
		}
		const bool Changed = Stroke.count(Step) == 0 || Stroke[Step] != Pitch || StrokeLast != Step;
		Stroke[Step] = Pitch;
		StrokeLast = Step;
		if (Changed) {
			Preview(Pitch, EditInstrument);
			Recompute();
		}
	}

	// Keys

	bool KeyDown(UINT Key, bool Ctrl, bool Shift, bool Alt)
	{
		if (Key == VK_ESCAPE) {
			if (Drag != DRAG_NONE)
				CancelDrag();
			else if (PlacingClip >= 0)
				PlacingClip = -1;
			else {
				Selection.clear();
				RangeKept = false;
			}
			pWnd->Invalidate(FALSE);
			return true;
		}
		if (Drag != DRAG_NONE)
			return false;
		if (Dirty)
			Read();
		if (Notes.empty())
			return false;
		bool Done = false;
		switch (Key) {
		case VK_DELETE:
		case VK_BACK:
			if (!Ctrl && !Alt) {
				DeleteSelection();
				Done = true;
			}
			break;
		case 'A':
			if (Ctrl && !Shift && !Alt) {
				Selection.clear();
				RangeKept = false;
				const std::vector<RollNote> &List = Notes[static_cast<size_t>(Channel)];
				for (size_t i = 0; i < List.size(); ++i)
					Selection.push_back(List[i].Start);
				if (!Selection.empty())
					CancelPatternSelection();
				Done = true;
			}
			break;
		case 'C':
			if (Ctrl && !Alt) {
				if (Shift)
					SaveSelectionAsClip();
				else
					CopySelection(false);
				Done = true;
			}
			break;
		case 'X':
			if (Ctrl && !Shift && !Alt) {
				CopySelection(true);
				Done = true;
			}
			break;
		case 'V':
			if (Ctrl && !Shift && !Alt) {
				PasteCopied();
				Done = true;
			}
			break;
		case VK_UP:
		case VK_DOWN:
			if (!Selection.empty() && !Ctrl && !Alt) {
				Transpose(Key == VK_UP ? 1 : -1, Shift);
				Done = true;
			}
			break;
		case VK_LEFT:
		case VK_RIGHT:
			if (!Selection.empty() && !Ctrl) {
				Nudge(Key == VK_RIGHT ? 1 : -1, Alt);
				Done = true;
			}
			break;
		case 'D':
			if (!Ctrl && !Shift && !Alt && !TypesNotes()) {
				SetTool(TOOL_DRAW);
				Done = true;
			}
			break;
		case 'B':
			if (!Ctrl && !Shift && !Alt && !TypesNotes()) {
				SetTool(TOOL_PAINT);
				Done = true;
			}
			break;
		case 'S':
			if (!Ctrl && !Shift && !Alt && !TypesNotes()) {
				SetTool(TOOL_SELECT);
				Done = true;
			}
			break;
		case 'E':
			if (!Ctrl && !Shift && !Alt && !TypesNotes()) {
				// The Effects menu where the mouse is, for the selected notes or the one under it
				CPoint Mouse;
				::GetCursorPos(&Mouse);
				CPoint Inside = Mouse;
				pWnd->ScreenToClient(&Inside);
				Layout();
				int HitChannel = -1, HitIndex = -1;
				if (Selection.empty() && HitNote(Inside, HitChannel, HitIndex) && HitChannel == Channel)
					Selection.assign(1, Notes[static_cast<size_t>(Channel)][static_cast<size_t>(HitIndex)].Start);
				pWnd->Invalidate(FALSE);
				pWnd->UpdateWindow();
				EffectsMenu(Mouse);
				Done = true;
			}
			break;
		case 'H':
			if (!Ctrl && !Shift && !Alt && !TypesNotes()) {
				// The Chords menu where the mouse is
				CPoint Mouse;
				::GetCursorPos(&Mouse);
				pWnd->Invalidate(FALSE);
				pWnd->UpdateWindow();
				ChordsMenu(Mouse, -1);
				Done = true;
			}
			break;
		}
		if (Done)
			pWnd->Invalidate(FALSE);
		return Done;
	}

	void SetTool(int NewTool)
	{
		Tool = NewTool;
		Save();
	}

	// In the pattern editor's place (View > Pattern editor), the note keys are notes
	static bool TypesNotes()
	{
		return Main() != NULL && !Main()->IsPatternEditorShown() && View() != NULL;
	}

	void DeleteSelection()
	{
		if (Selection.empty()) {
			Say(_T("Select notes to delete first: click one, or drag around some with the Select tool (or Ctrl)."));
			return;
		}
		BeginEdit(EDIT_REMOVE);
		Erased = Selection;
		Recompute();
		if (Commit(-1))
			Selection.clear();
	}

	// Up or down a note of the key (a semitone without one), or an octave
	void Transpose(int Way, bool Octave)
	{
		BeginEdit(EDIT_MOVE);
		Moving = Selection;
		MoveRows = 0;
		MoveCopy = false;
		const bool ByKey = !Octave && Snap && KeyApplies();
		MoveSteps = ByKey ? Way : 0;
		MoveSemitones = Octave ? 12 * Way : (ByKey ? 0 : Way);
		Recompute();
		const std::vector<int> Pitches = MovedPitches();
		if (Commit(Selection.front()) && !Pitches.empty()) {
			const int Index = Before.FindNote(Selection.front());
			Preview(Pitches.front(), Index >= 0 ? Before.GetNote(Index).Instrument : -1, NUDGE_SOUND_MS);
		}
	}

	// Earlier or later by a grid step (with Alt, a row)
	void Nudge(int Way, bool Fine)
	{
		const int Step = Fine ? 1 : GridRows();
		int First = EditRows(), Last = 0;
		for (size_t i = 0; i < Selection.size(); ++i) {
			First = std::min(First, Selection[i]);
			Last = std::max(Last, Selection[i]);
		}
		const int Rows = std::max(-First, std::min(Way * Step, EditRows() - 1 - Last));
		if (Rows == 0)
			return;
		BeginEdit(EDIT_MOVE);
		Moving = Selection;
		MoveRows = Rows;
		MoveCopy = false;
		MoveSteps = MoveSemitones = 0;
		Recompute();
		if (Commit(Selection.front() + Rows)) {
			for (size_t i = 0; i < Selection.size(); ++i)
				Selection[i] += Rows;
		}
	}

	void CopySelection(bool Cut)
	{
		if (Selection.empty()) {
			Say(_T("Select notes to copy first: click one, or drag around some with the Select tool (or Ctrl)."));
			return;
		}
		CChannelNotes Cells;
		ReadChannel(Channel, Cells, TotalRows(), true);
		Copied = Cells.Copy(&Selection[0], static_cast<int>(Selection.size()));
		HasCopied = Copied.GetNoteCount() > 0;
		const int Count = Copied.GetNoteCount();
		if (Cut)
			DeleteSelection();
		CString Text;
		Text.Format(_T("%s %s. Ctrl+V puts them where the mouse is (or at the cursor)."), Cut ? _T("Cut") : _T("Copied"),
			static_cast<LPCTSTR>(Plural(Count, _T("note"), _T("notes"))));
		Say(Text);
	}

	void PasteCopied()
	{
		if (!HasCopied) {
			Say(_T("Copy notes first: select them and press Ctrl+C."));
			return;
		}
		CPoint Mouse;
		::GetCursorPos(&Mouse);
		pWnd->ScreenToClient(&Mouse);
		int Row = CursorFrame * static_cast<int>(Length) + CursorRow;
		if (Grid.PtInRect(Mouse) && RowAt(Mouse.x) >= 0)
			Row = AltHeld() ? RowAt(Mouse.x) : SnapDown(RowAt(Mouse.x));
		Row = std::max(0, std::min(Row, EditRows() - 1));
		BeginEdit(EDIT_PASTE);
		PasteRow = Row;
		Recompute();
		const std::vector<int> Pasted = Marked;
		if (Commit(Row))
			Selection = Pasted;
	}

	void GiveSelected(int Instrument)
	{
		if (Selection.empty())
			return;
		BeginEdit(EDIT_INSTRUMENT);
		GiveInstrument = Instrument;
		Recompute();
		Commit(Selection.front());
	}

	// The chord helper (the Chords button, H, or a right-click on the chord strip): a chord, or a chord a bar, on the
	// channels with nothing to play over some bars, a held voice on each, under the melody and over the bass. Api/ChordHelper.h
	// works the voices out, PianoRollChords.h makes their sounds and writes them, one Ctrl+Z.

	ftapi::Timing SongTiming() const
	{
		return ftapi::Timing(static_cast<int>(Length), static_cast<int>(Frames), Beat, Bar);
	}

	// The rows the chords go on: the rows dragged along the ruler with the Select tool (whatever notes of the channel that
	// selected), the selected notes' bars (where they start, and the bar the last one ends in when it fills half of it), the
	// pattern editor's selection, or the bar the cursor is in, after which chords for more bars go on (Grow)
	void ChordSpan(int &From, int &To, bool &Grow) const
	{
		Grow = false;
		const ftapi::Timing T = SongTiming();
		if (RangeKept) {
			From = std::min(RangeFrom, RangeTo);
			To = std::max(RangeFrom, RangeTo) + 1;
			return;
		}
		if (!Selection.empty() && Channel < static_cast<int>(Notes.size())) {
			int First = -1, LastStart = 0, LastEnd = 0;
			for (size_t i = 0; i < Selection.size(); ++i) {
				const int Index = FindStart(Selection[i]);
				if (Index < 0)
					continue;
				const RollNote &Note = Notes[static_cast<size_t>(Channel)][static_cast<size_t>(Index)];
				First = First < 0 ? Note.Start : std::min(First, Note.Start);
				LastStart = std::max(LastStart, Note.Start);
				LastEnd = std::max(LastEnd, Note.End);
			}
			if (First >= 0) {
				From = First;
				To = std::max(First + 1, LastEnd);
				const int Tail = T.BarOf(To - 1);
				if (Tail > T.BarOf(LastStart) && (To - T.BarStart(Tail)) * 2 < T.BarLength(Tail))
					To = T.BarStart(Tail);
				return;
			}
		}
		CFamiTrackerView *pView = View();
		if (pView != NULL && pView->GetPatternEditor()->IsSelecting()) {
			const CSelection Rows = pView->GetPatternEditor()->GetSelection();
			const int Frame = std::max(0, pView->GetPatternEditor()->GetFrame());
			const int Top = std::max(0, Rows.GetRowStart()), Bottom = std::min(static_cast<int>(Length) - 1, Rows.GetRowEnd());
			if (Bottom >= Top) {
				From = Frame * static_cast<int>(Length) + Top;
				To = Frame * static_cast<int>(Length) + Bottom + 1;
				return;
			}
		}
		From = std::max(0, CursorFrame) * static_cast<int>(Length) + std::max(0, CursorRow);
		To = From + 1;
		Grow = true;
	}

	// "bar 5", "bars 5-8"
	CString BarsText(int First, int End) const
	{
		const ftapi::Timing T = SongTiming();
		const int From = T.BarOf(First), To = T.BarOf(std::max(First, End - 1));
		CString Text;
		if (From == To)
			Text.Format(_T("bar %d"), From);
		else
			Text.Format(_T("bars %d-%d"), From, To);
		return Text;
	}

	// The chords the strip names over [First, End), a bar at a time ("| Am | F G |"); empty when it names none there
	CString HeardChords(int First, int End) const
	{
		const std::vector<ftapi::HeardChord> &Chords = ShownChords();
		const ftapi::Timing T = SongTiming();
		CString Text;
		bool Any = false;
		for (int Row = First; Row < End; ) {
			const int To = std::min(End, T.BarStart(T.BarOf(Row)) + T.BarLength(T.BarOf(Row)));
			if (To <= Row)
				break;
			CString Part;
			int Covered = Row;
			for (size_t i = 0; i < Chords.size(); ++i) {
				const ftapi::HeardChord &Chord = Chords[i];
				if (Chord.Row + Chord.Rows <= Row || Chord.Row >= To)
					continue;
				if (Chord.Row > Covered)
					Part += _T(" N.C.");	// none before it in the bar
				Part += _T(" ") + CString(Chord.Name.c_str());
				Covered = Chord.Row + Chord.Rows;
				Any = true;
			}
			Text += _T("|") + (Part.IsEmpty() ? CString(_T(" N.C.")) : Part) + _T(" ");
			Row = To;
		}
		return Any ? Text + _T("|") : CString();
	}

	// A progression to show, without its bar lines and empty shares: "| Am | F G |" is "Am F G"
	static CString ChordsShown(const CString &Progression)
	{
		CString Out;
		int Start = 0;
		for (CString Word = Progression.Tokenize(_T(" |"), Start); !Word.IsEmpty(); Word = Progression.Tokenize(_T(" |"), Start)) {
			if (Word.CompareNoCase(_T("N.C.")) == 0)
				continue;
			if (!Out.IsEmpty())
				Out += _T(" ");
			Out += Word;
		}
		return Out;
	}

	ftapi::HelperSong ChordSong() const
	{
		ftapi::HelperSong Song;
		CFamiTrackerDoc *pDoc = Doc();
		if (pDoc != NULL)
			ReadChordSong(pDoc, Track, TheKey().K, HasKey(), Song);
		return Song;
	}

	// The Chords menu at Point (screen coordinates), for the span; AtRow: a row of the chord strip right-clicked, whose bar it
	// is for unless that's in the selection
	void ChordsMenu(CPoint Point, int AtRow)
	{
		CFamiTrackerDoc *pDoc = Doc();
		if (Dirty)
			Read();
		if (pDoc == NULL || Notes.empty())
			return;
		int From = 0, To = 0;
		bool Grow = false;
		ChordSpan(From, To, Grow);
		if (AtRow >= 0 && (Grow || AtRow < From || AtRow >= To)) {
			From = AtRow;
			To = AtRow + 1;
			Grow = true;
		}
		const ftapi::HelperSong Song = ChordSong();
		int First = 0, End = 0;
		ftapi::WholeBars(Song, From, To, First, End);
		End = std::max(First + 1, std::min(End, EditRows()));
		const std::vector<int> Free = ftapi::FreeChannels(Song, First, End);
		const ftapi::Key K = TheKey().K;
		const CString Where = BarsText(First, End);

		CMenu Menu, KeyChords, Sevenths, Voices;
		Menu.CreatePopupMenu();
		KeyChords.CreatePopupMenu();
		Sevenths.CreatePopupMenu();
		Voices.CreatePopupMenu();
		CString Item;
		if (Free.empty())
			Item.Format(_T("No channel is free in %s: each one that can play a chord's notes plays there"), static_cast<LPCTSTR>(Where));
		else
			Item.Format(_T("Chords on %s%s, a note on each free channel (%s)"), static_cast<LPCTSTR>(Where), Grow ? _T("") :
				_T(" (the selection)"), static_cast<LPCTSTR>(Plural(static_cast<int>(Free.size()), _T("is free"), _T("are free"))));
		Menu.AppendMenu(MF_STRING | MF_GRAYED, 0, Item);
		Menu.AppendMenu(MF_SEPARATOR);
		const UINT Can = Free.empty() ? MF_GRAYED : 0;
		const CString Heard = HeardChords(First, End);
		if (!Heard.IsEmpty()) {
			Item.Format(_T("%s: the chords the parts play there"), static_cast<LPCTSTR>(ChordsShown(Heard)));
			Menu.AppendMenu(MF_STRING | Can, MENU_CHORD_HEARD, Item);
		}
		for (int d = 0; d < 7; ++d) {
			const ftapi::Chord Plain = ftapi::DiatonicChord(K, d, false), Seventh = ftapi::DiatonicChord(K, d, true);
			Item.Format(_T("%s%s\t%s"), ftapi::ChordName(Plain, K).c_str(), d == 0 ? _T(" (home)") : _T(""), ftapi::ChordDegree(Plain, K).c_str());
			KeyChords.AppendMenu(MF_STRING | Can, MENU_CHORD_KEY + d, Item);
			Item.Format(_T("%s\t%s"), ftapi::ChordName(Seventh, K).c_str(), ftapi::ChordDegree(Seventh, K).c_str());
			Sevenths.AppendMenu(MF_STRING | Can, MENU_CHORD_SEVENTH + d, Item);
		}
		KeyChords.AppendMenu(MF_SEPARATOR);
		KeyChords.AppendMenu(MF_POPUP, reinterpret_cast<UINT_PTR>(Sevenths.Detach()), _T("With sevenths (richer)"));
		if (HasKey())
			Item.Format(_T("The chords of %s"), static_cast<LPCTSTR>(KeyText()));
		else
			Item = _T("The key's chords (no key yet: draw some notes, or choose one with Key)");
		Menu.AppendMenu(MF_POPUP | (HasKey() ? 0 : MF_GRAYED), reinterpret_cast<UINT_PTR>(KeyChords.Detach()), Item);
		Menu.AppendMenu(MF_STRING | Can, MENU_CHORD_TYPE, _T("Type a chord, or a chord a bar..."));
		Menu.AppendMenu(MF_SEPARATOR);
		const bool Again = AgainHolds();
		if (Again)
			Item.Format(_T("Again on the next bar: %s on %s"), static_cast<LPCTSTR>(ChordsShown(AgainChords)),
				static_cast<LPCTSTR>(BarsText(AgainRow, AgainRow + 1)));
		else
			Item = _T("Again on the next bar (once chords are in)");
		Menu.AppendMenu(MF_STRING | (Again ? 0 : MF_GRAYED), MENU_CHORD_AGAIN, Item);
		Menu.AppendMenu(MF_SEPARATOR);
		Voices.AppendMenu(MF_STRING | (ChordVoices == 0 ? MF_CHECKED : 0), MENU_CHORD_VOICES,
			_T("As many as the chord has notes (3, or 4 with a seventh)"));
		for (int n = 2; n <= 4; ++n) {
			Item.Format(_T("%d voices"), n);
			Voices.AppendMenu(MF_STRING | (ChordVoices == n ? MF_CHECKED : 0), MENU_CHORD_VOICES + n, Item);
		}
		Menu.AppendMenu(MF_POPUP, reinterpret_cast<UINT_PTR>(Voices.Detach()), _T("Voices"));

		const int Choice = static_cast<int>(Menu.TrackPopupMenu(TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
			Point.x, Point.y, pWnd));
		if (Choice == static_cast<int>(MENU_CHORD_HEARD))
			PutChords(Heard, First, End, false, Point);
		else if (Choice >= static_cast<int>(MENU_CHORD_KEY) && Choice < static_cast<int>(MENU_CHORD_KEY) + 7)
			PutChords(CString(ftapi::ChordName(ftapi::DiatonicChord(K, Choice - static_cast<int>(MENU_CHORD_KEY), false), K).c_str()),
				From, To, Grow, Point);
		else if (Choice >= static_cast<int>(MENU_CHORD_SEVENTH) && Choice < static_cast<int>(MENU_CHORD_SEVENTH) + 7)
			PutChords(CString(ftapi::ChordName(ftapi::DiatonicChord(K, Choice - static_cast<int>(MENU_CHORD_SEVENTH), true), K).c_str()),
				From, To, Grow, Point);
		else if (Choice == static_cast<int>(MENU_CHORD_TYPE)) {
			CClipNameDlg Dialog(_T("Chords"), _T("A chord (Am, F, G7), or a chord a bar (C G Am F):"),
				!TypedChords.IsEmpty() ? TypedChords : ChordsShown(Heard), pWnd, 200);
			if (Dialog.DoModal() != IDOK)
				return;
			TypedChords = Dialog.GetName();
			PutChords(TypedChords, From, To, Grow, Point);
		}
		else if (Choice == static_cast<int>(MENU_CHORD_AGAIN) && Again)
			PutChords(AgainChords, AgainRow, AgainRow + 1, false, Point, true);
		else if (Choice == static_cast<int>(MENU_CHORD_VOICES) || (Choice >= static_cast<int>(MENU_CHORD_VOICES) + 2 &&
			Choice <= static_cast<int>(MENU_CHORD_VOICES) + 4)) {
			ChordVoices = Choice - static_cast<int>(MENU_CHORD_VOICES);
			Save();
			if (ChordVoices == 0)
				Say(_T("Chords get a voice for each of their notes now: 3, or 4 with a seventh."));
			else {
				CString Text;
				Text.Format(_T("Chords get %d voices now, each on a channel of its own."), ChordVoices);
				Say(Text);
			}
		}
		pWnd->Invalidate(FALSE);
	}

	// The chords written last are still there for Again to go on from (not undone, nor another song): each of their
	// voices' channels has a note sounding where their last bar starts
	bool AgainHolds() const
	{
		if (AgainRow <= 0 || AgainTrack != Track || AgainRow >= EditRows() || AgainChannels.empty())
			return false;
		const int Last = AgainLead.empty() ? AgainRow - 1 : AgainLead.front().Row;
		for (size_t i = 0; i < AgainChannels.size(); ++i) {
			const int Column = AgainChannels[i];
			if (Column < 0 || Column >= static_cast<int>(Notes.size()))
				return false;
			const std::vector<RollNote> &List = Notes[static_cast<size_t>(Column)];
			bool Sounds = false;
			for (size_t n = 0; n < List.size() && !Sounds; ++n)
				Sounds = List[n].Start <= Last && List[n].End > Last;
			if (!Sounds)
				return false;
		}
		return true;
	}

	// Too few channels are free for the voices the chords want (for their notes, as Voices sets, or as the chord before had,
	// Why): fewer voices, the arpeggio on one channel, or nothing
	enum fewer_t { FEWER_NOTES, FEWER_ASKED, FEWER_AGAIN };
	int FewerMenu(const ftapi::HelperSong &Song, const ftapi::ChordPlan &Plan, fewer_t Why, CPoint Point) const
	{
		CString Free;
		for (size_t i = 0; i < Plan.Free.size(); ++i) {
			if (i > 0)
				Free += i + 1 == Plan.Free.size() ? _T(" and ") : _T(", ");
			Free += Song.Channels[static_cast<size_t>(Plan.Free[i])].Name.c_str();
		}
		bool One = true;
		for (size_t i = 1; i < Plan.Chords.size(); ++i)
			One = One && Plan.Chords[i].C == Plan.Chords[0].C;
		CString Head, Item;
		const CString Where = BarsText(Plan.First, Plan.End);
		const LPCTSTR Is = Plan.Free.size() == 1 ? _T("is") : _T("are");
		if (Why == FEWER_ASKED)
			Head.Format(_T("Chords get %d voices (Voices), but only %s %s free in %s"), Plan.Wanted, static_cast<LPCTSTR>(Free), Is,
				static_cast<LPCTSTR>(Where));
		else if (Why == FEWER_AGAIN)
			Head.Format(_T("The chord before had %d voices, but only %s %s free in %s"), Plan.Wanted, static_cast<LPCTSTR>(Free), Is,
				static_cast<LPCTSTR>(Where));
		else if (One && !Plan.Chords.empty())
			Head.Format(_T("%s has %d notes, but only %s %s free in %s"), ftapi::ChordName(Plan.Chords[0].C, Song.K).c_str(), Plan.Wanted,
				static_cast<LPCTSTR>(Free), Is, static_cast<LPCTSTR>(Where));
		else
			Head.Format(_T("The chords have up to %d notes, but only %s %s free in %s"), Plan.Wanted, static_cast<LPCTSTR>(Free), Is,
				static_cast<LPCTSTR>(Where));
		CMenu Menu;
		Menu.CreatePopupMenu();
		Menu.AppendMenu(MF_STRING | MF_GRAYED, 0, Head);
		Menu.AppendMenu(MF_SEPARATOR);
		if (Plan.Free.size() >= 2) {
			if (Why != FEWER_NOTES)
				Item.Format(_T("Play %d voices, one on each"), static_cast<int>(Plan.Free.size()));
			else
				Item.Format(_T("Play %d of %s notes, one on each"), static_cast<int>(Plan.Free.size()), One ? _T("its") : _T("their"));
			Menu.AppendMenu(MF_STRING, MENU_CHORD_FEWER, Item);
		}
		if (!Plan.Free.empty()) {
			size_t Most = 0;	// an arpeggio (0xy) plays three notes
			for (size_t i = 0; i < Plan.Chords.size(); ++i)
				Most = std::max(Most, Plan.Chords[i].C.PitchClasses().size());
			Item.Format(_T("Play %s%s notes in turn on %s, as an arpeggio (0xy)"), Most > 3 ? _T("three of ") : _T(""),
				One ? _T("its") : _T("their"), Song.Channels[static_cast<size_t>(Plan.Free[0])].Name.c_str());
			Menu.AppendMenu(MF_STRING, MENU_CHORD_ARPEGGIO, Item);
		}
		Menu.AppendMenu(MF_SEPARATOR);
		Menu.AppendMenu(MF_STRING, MENU_CHORD_CANCEL, _T("Never mind"));
		return static_cast<int>(Menu.TrackPopupMenu(TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
			Point.x, Point.y, pWnd));
	}

	// Writes the chords Text on the rows [From, To) (whole bars of them), asking first when too few channels are free; with
	// Again, on the voices' channels of the chords written last, led from them
	void PutChords(const CString &Text, int From, int To, bool Grow, CPoint Point, bool Again = false)
	{
		CFamiTrackerDoc *pDoc = Doc();
		CMainFrame *pMain = Main();
		if (pDoc == NULL || pMain == NULL)
			return;
		ftapi::HelperSong Song = ChordSong();
		ftapi::ChordRequest Request;
		Request.Chords = static_cast<LPCSTR>(CStringA(Text));
		Request.First = From;
		Request.End = To;
		Request.Grow = Grow;
		Request.Limit = EditRows();
		Request.Voices = ChordVoices;
		if (Again) {
			Request.Prefer = AgainChannels;
			Request.LeadFrom = AgainLead;
			Request.Voices = AgainArpeggio ? 0 : AgainVoices;
			Request.Arpeggio = AgainArpeggio;
		}
		ftapi::ChordPlan Plan;
		if (!ftapi::PlanChords(Song, Request, Plan)) {
			Say(CString(Plan.Error.c_str()), CHORD_MESSAGE_MS);
			return;
		}
		if (Plan.TooFew) {
			pWnd->Invalidate(FALSE);
			pWnd->UpdateWindow();
			const int Choice = FewerMenu(Song, Plan, Again ? FEWER_AGAIN : Request.Voices > 0 ? FEWER_ASKED : FEWER_NOTES, Point);
			if (Choice == static_cast<int>(MENU_CHORD_FEWER))
				Request.Voices = static_cast<int>(Plan.Free.size());
			else if (Choice == static_cast<int>(MENU_CHORD_ARPEGGIO))
				Request.Arpeggio = true;
			else
				return;
			if (!ftapi::PlanChords(Song, Request, Plan)) {
				Say(CString(Plan.Error.c_str()), CHORD_MESSAGE_MS);
				return;
			}
		}
		if (Plan.Voices.empty()) {
			Say(_T("There's no channel there for the chord's notes."), CHORD_MESSAGE_MS);
			return;
		}
		CString Sounds, Message;
		const int Cursor = std::max(0, CursorFrame) * static_cast<int>(Length) + std::max(0, CursorRow);
		if (!WriteChordPlan(pMain, Song, Plan, Cursor, Sounds, Message)) {
			Say(Message, CHORD_MESSAGE_MS);
			return;
		}
		Dirty = true;

		// For Again: the last bar's chords, the channels and how many
		const ftapi::Timing T = SongTiming();
		const int LastBar = std::max(Plan.First, T.BarStart(T.BarOf(Plan.End - 1)));
		AgainLead.clear();
		for (size_t i = 0; i < Plan.Chords.size(); ++i) {
			ftapi::ChordAt A = Plan.Chords[i];
			if (A.Row + A.Rows <= LastBar)
				continue;
			if (A.Row < LastBar) {
				A.Rows -= LastBar - A.Row;
				A.Row = LastBar;
			}
			AgainLead.push_back(A);
		}
		AgainChords = ftapi::ProgressionText(AgainLead, std::vector<ftapi::BarSpan>(1, ftapi::BarSpan(LastBar, Plan.End - LastBar)),
			TheKey().K).c_str();
		AgainTrack = Track;
		AgainRow = Plan.End;
		AgainChannels.clear();
		for (size_t v = 0; v < Plan.Voices.size(); ++v)
			AgainChannels.push_back(Plan.Voices[v].Channel);
		AgainVoices = static_cast<int>(Plan.Voices.size());
		AgainArpeggio = Plan.Arpeggio;

		CString Said;
		Said.Format(_T("%s: %s (%s). Ctrl+Z undoes it; Chords > Again puts %s on the next bar."), ftapi::PlanChordsText(Song, Plan).c_str(),
			ftapi::PlanVoicesText(Song, Plan).c_str(), static_cast<LPCTSTR>(Sounds), static_cast<LPCTSTR>(ChordsShown(AgainChords)));
		for (size_t i = 0; i < Plan.Remarks.size(); ++i)
			Said += CString(_T(" ")) + Plan.Remarks[i].c_str();
		Said += Message;
		Say(Said, CHORD_MESSAGE_MS);
		pWnd->Invalidate(FALSE);
	}

	// Clips

	void SaveSelectionAsClip()
	{
		CFamiTrackerDoc *pDoc = Doc();
		if (Selection.empty() || pDoc == NULL) {
			Say(_T("Select the notes for the clip first: click one, or drag around some with the Select tool (or Ctrl)."));
			return;
		}
		CChannelNotes Cells;
		ReadChannel(Channel, Cells, TotalRows(), true);
		CChannelNotes Clip = Cells.Copy(&Selection[0], static_cast<int>(Selection.size()));
		Clip.SelfContained();
		CClipLibrary *pClips = pDoc->GetClips();
		CClipNameDlg Dialog(_T("Save notes as clip"), _T("Name of the new clip:"), pClips->NewName(_T("Clip"), true), pWnd);
		if (Dialog.DoModal() != IDOK)
			return;
		const int Index = pClips->Add(Dialog.GetName(), 1, Clip.GetRows());
		if (Index < 0) {
			Say(_T("There are too many clips already. Delete some first: right-click them in the clips panel."));
			return;
		}
		pClips->SetSource(Index, 0, ChanIDs[static_cast<size_t>(Channel)]);
		for (int r = 0; r < Clip.GetRows(); ++r) {
			stChanNote Cell;
			Clip.GetCell(r, Cell);
			pClips->SetCell(Index, 0, r, Cell);
		}
		pDoc->SetModifiedFlag();
		CString Text;
		Text.Format(_T("Saved %s as the clip \"%s\". It's in the clips panel: drag it to where it goes."),
			static_cast<LPCTSTR>(Plural(Clip.GetNoteCount(), _T("note"), _T("notes"))), static_cast<LPCTSTR>(Dialog.GetName()));
		Say(Text);
	}

	// The clip's notes, to show where it would go
	bool LoadClip(int Index)
	{
		CFamiTrackerDoc *pDoc = Doc();
		CClipLibrary *pClips = pDoc != NULL ? pDoc->GetClips() : NULL;
		if (pClips == NULL || Index < 0 || Index >= pClips->GetCount())
			return false;
		ClipNotes.clear();
		for (int c = 0; c < pClips->GetColumns(Index); ++c) {
			CChannelNotes Column;
			Column.Reset(pClips->GetRows(Index));
			Column.SetEffectColumns(MAX_EFFECT_COLUMNS);
			for (int r = 0; r < pClips->GetRows(Index); ++r) {
				stChanNote Cell;
				pClips->GetCell(Index, c, r, Cell);
				Column.SetCell(r, Cell);
			}
			ClipNotes.push_back(RollNotes(Column, Column.GetRows()));
		}
		return true;
	}

	void StartPlacing(int Index)
	{
		if (!LoadClip(Index))
			return;
		PanelDrop = false;
		PlacingClip = Index;
		PlaceRow = -1;
		Selection.clear();
	}

	// A clip dragged in from the clips panel: where it would start, shown as
	// when the Clips button places one (the clips panel places it)
	bool ShowDrop(CPoint Point, int Index, bool Scroll)
	{
		Layout();
		// The bright channel is the cursor's, at once (the clips panel puts the cursor back where it
		// was if the drag scrolled the pattern editor on its way here)
		CFamiTrackerView *pView = View();
		if (pView != NULL && !Notes.empty()) {
			const int Cursor = pView->GetPatternEditor()->GetChannel();
			if (Cursor >= 0 && Cursor < static_cast<int>(Notes.size()) && Cursor != Channel) {
				Channel = CursorChannel = Cursor;
				Selection.clear();
				PlacingClip = -1;
				pWnd->Invalidate(FALSE);
			}
		}
		if (Notes.empty() || Channel < 0 || Channel >= static_cast<int>(Notes.size()) || !Grid.PtInRect(Point)) {
			HideDrop();
			return false;
		}
		if (!PanelDrop || PlacingClip != Index) {
			if (!LoadClip(Index)) {
				HideDrop();
				return false;
			}
			PanelDrop = true;
			PlacingClip = Index;
			PlaceRow = -1;
		}
		if (Scroll) {
			// Close to the left or right edge: on that way
			const int Edge = SX(DROP_EDGE), Step = std::max(RowWidth(), SX(DROP_STEP));
			const int Most = std::max(0, EditRows() * RowWidth() - Grid.Width());
			int To = ScrollX;
			if (Point.x < Grid.left + Edge)
				To = std::max(0, ScrollX - Step);
			else if (Point.x >= Grid.right - Edge)
				To = std::min(Most, ScrollX + Step);
			if (To != ScrollX) {
				ScrollX = To;
				UpdateScrollBars();
				pWnd->Invalidate(FALSE);
			}
		}
		const int Row = RowAt(Point.x);
		const int Place = Row < 0 ? -1 : std::min(AltHeld() ? Row : SnapDown(Row), TotalRows() - 1);
		if (Place != PlaceRow) {
			PlaceRow = Place;
			pWnd->Invalidate(FALSE);
		}
		return PlaceRow >= 0;
	}

	bool HideDrop()
	{
		if (!PanelDrop)
			return false;
		PanelDrop = false;
		PlacingClip = -1;
		PlaceRow = -1;
		ClipNotes.clear();
		pWnd->Invalidate(FALSE);
		return true;
	}

	void PlaceClipHere()
	{
		const int Index = PlacingClip, Row = PlaceRow;
		PlacingClip = -1;
		if (Row < 0)
			return;
		GoTo(Channel, Row);
		CString Text;
		PlaceClip(Main(), Index, LinkedClips, Text);
		Say(Text);
	}

	// The buttons along the top

	CPoint Below(int Id) const
	{
		CPoint Point(0, 0);
		for (size_t i = 0; i < Buttons.size(); ++i) {
			if (Buttons[i].Id == Id)
				Point = CPoint(Buttons[i].Rect.left, Buttons[i].Rect.bottom);
		}
		pWnd->ClientToScreen(&Point);
		return Point;
	}

	int PopUp(CMenu &Menu, int Id) const
	{
		const CPoint Point = Below(Id);
		return static_cast<int>(Menu.TrackPopupMenu(TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
			Point.x, Point.y, pWnd));
	}

	void Press(int Id)
	{
		CFamiTrackerDoc *pDoc = Doc();
		if (pDoc == NULL)
			return;
		switch (Id) {
		case BUTTON_CHANNEL: {
			CMenu Menu;
			Menu.CreatePopupMenu();
			for (size_t c = 0; c < ChanIDs.size(); ++c)
				Menu.AppendMenu(MF_STRING | (static_cast<int>(c) == Channel ? MF_CHECKED : 0), MENU_CHANNEL + c,
					RecordingChannelName(pDoc, static_cast<int>(c)));
			const int Choice = PopUp(Menu, Id);
			if (Choice >= static_cast<int>(MENU_CHANNEL) && Choice < static_cast<int>(MENU_CHANNEL + ChanIDs.size()))
				GoTo(Choice - static_cast<int>(MENU_CHANNEL), CursorFrame * static_cast<int>(Length) + CursorRow);
			break;
		}
		case BUTTON_DRAW:
			SetTool(TOOL_DRAW);
			break;
		case BUTTON_PAINT:
			SetTool(TOOL_PAINT);
			break;
		case BUTTON_SELECT:
			SetTool(TOOL_SELECT);
			break;
		case BUTTON_GRID: {
			CMenu Menu;
			Menu.CreatePopupMenu();
			const int Saved = GridChoice;
			for (int g = 0; g < GRIDS; ++g) {
				GridChoice = g;
				CString Item;
				Item.Format(_T("%s (%s)"), GRID_NAMES[g], static_cast<LPCTSTR>(Plural(GridRows(), _T("row"), _T("rows"))));
				Menu.AppendMenu(MF_STRING | (g == Saved ? MF_CHECKED : 0), MENU_GRID + g, Item);
			}
			GridChoice = Saved;
			const int Choice = PopUp(Menu, Id);
			if (Choice >= static_cast<int>(MENU_GRID) && Choice < static_cast<int>(MENU_GRID) + GRIDS) {
				GridChoice = Choice - static_cast<int>(MENU_GRID);
				Save();
			}
			break;
		}
		case BUTTON_KEY:
			KeyMenu(Id);
			break;
		case BUTTON_SNAP:
			ToggleSnap();
			break;
		case BUTTON_INSTRUMENT: {
			CMenu Menu, Give;
			Menu.CreatePopupMenu();
			Give.CreatePopupMenu();
			const int Picked = Main()->GetSelectedInstrument();
			int Count = 0;
			for (int i = 0; i < MAX_INSTRUMENTS; ++i) {
				if (!pDoc->IsInstrumentUsed(static_cast<unsigned int>(i)))
					continue;
				CString Item = InstrumentText(i);
				if (!Fits(i))
					Item += _T("  (another chip)");
				Menu.AppendMenu(MF_STRING | (i == Picked ? MF_CHECKED : 0), MENU_INSTRUMENT + i, Item);
				Give.AppendMenu(MF_STRING, MENU_GIVE + i, Item);
				++Count;
			}
			if (Count == 0)
				Menu.AppendMenu(MF_STRING | MF_GRAYED, 0, _T("The song has no instruments yet"));
			Menu.AppendMenu(MF_SEPARATOR);
			Menu.AppendMenu(MF_POPUP | (Selection.empty() || Count == 0 ? MF_GRAYED : 0), reinterpret_cast<UINT_PTR>(Give.Detach()),
				_T("Give the selected notes"));
			const int Choice = PopUp(Menu, Id);
			if (Choice >= static_cast<int>(MENU_INSTRUMENT) && Choice < static_cast<int>(MENU_INSTRUMENT) + MAX_INSTRUMENTS)
				Main()->SelectInstrument(Choice - static_cast<int>(MENU_INSTRUMENT));
			else if (Choice >= static_cast<int>(MENU_GIVE) && Choice < static_cast<int>(MENU_GIVE) + MAX_INSTRUMENTS)
				GiveSelected(Choice - static_cast<int>(MENU_GIVE));
			break;
		}
		case BUTTON_CLIPS: {
			CClipLibrary *pClips = pDoc->GetClips();
			CMenu Menu;
			Menu.CreatePopupMenu();
			for (int i = 0; i < pClips->GetCount() && i < static_cast<int>(MENU_CLIP_LINKED - MENU_CLIP); ++i) {
				CString Item;
				Item.Format(_T("Place \"%s\""), static_cast<LPCTSTR>(pClips->GetName(i)));
				Menu.AppendMenu(MF_STRING, MENU_CLIP + i, Item);
			}
			if (pClips->GetCount() == 0)
				Menu.AppendMenu(MF_STRING | MF_GRAYED, 0, _T("The song has no clips yet"));
			Menu.AppendMenu(MF_SEPARATOR);
			Menu.AppendMenu(MF_STRING | (LinkedClips ? MF_CHECKED : 0), MENU_CLIP_LINKED, _T("Place linked copies (they change with the clip)"));
			Menu.AppendMenu(MF_STRING | (Selection.empty() ? MF_GRAYED : 0), MENU_CLIP_SAVE, _T("Save the selected notes as a clip...\tCtrl+Shift+C"));
			Menu.AppendMenu(MF_STRING, MENU_CLIP_MANAGE, _T("Show the clips panel"));
			const int Choice = PopUp(Menu, Id);
			if (Choice >= static_cast<int>(MENU_CLIP) && Choice < static_cast<int>(MENU_CLIP_LINKED))
				StartPlacing(Choice - static_cast<int>(MENU_CLIP));
			else if (Choice == static_cast<int>(MENU_CLIP_LINKED)) {
				LinkedClips = !LinkedClips;
				Save();
			}
			else if (Choice == static_cast<int>(MENU_CLIP_SAVE))
				SaveSelectionAsClip();
			else if (Choice == static_cast<int>(MENU_CLIP_MANAGE))
				Main()->PostMessage(WM_COMMAND, ID_EDIT_CLIPS);
			break;
		}
		case BUTTON_EFFECTS:
			EffectsMenu(Below(Id));
			break;
		case BUTTON_CHORDS:
			ChordsMenu(Below(Id), -1);
			break;
		case BUTTON_CLOSE:
			Main()->PostMessage(WM_COMMAND, ID_VIEW_PIANOROLL);
			break;
		}
		pWnd->Invalidate(FALSE);
	}

	// The key and the chords (Api/Keys.h, Api/Tools.h): chosen in the Key menu, or found from the notes as the
	// Claude Code tools find them

	// Worked out a moment after the song changed (a long song takes a while), never while dragging, and kept
	void UpdateHarmony()
	{
		CMainFrame *pMain = Main();
		CMcpConnection *pTools = pMain != NULL ? pMain->GetMcpConnection() : NULL;
		ftapi::TrackHarmony Fresh;
		if (pTools == NULL || !pTools->Harmony(Fresh)) {
			HarmonyDue = ::GetTickCount() + HARMONY_RETRY;	// busy with Claude Code: a little later
			return;
		}
		Harmony = Fresh;
		HarmonyRead = true;
		HarmonyTrack = Track;
		HarmonyStale = false;
		HoverChord = -1;
	}

	bool HarmonyDueNow() const
	{
		return HarmonyStale && Drag == DRAG_NONE && static_cast<int>(::GetTickCount() - HarmonyDue) >= 0;
	}

	const std::vector<ftapi::HeardChord> &ShownChords() const
	{
		static const std::vector<ftapi::HeardChord> NONE;
		return (HarmonyRead && HarmonyTrack == Track) ? Harmony.Chords : NONE;
	}

	// The chord in the strip under Point, -1 for none
	int ChordAt(CPoint Point) const
	{
		if (!ChordStrip.PtInRect(Point))
			return -1;
		const int Row = RowAt(Point.x);
		const std::vector<ftapi::HeardChord> &Chords = ShownChords();
		for (size_t i = 0; i < Chords.size(); ++i)
			if (Row >= Chords[i].Row && Row < Chords[i].Row + Chords[i].Rows)
				return static_cast<int>(i);
		return -1;
	}

	void ChooseKey(bool Chosen, const ftapi::KeyChoice &Key)
	{
		if (!SetTrackKey(Main(), Chosen, Key))
			return;		// it was that already
		CString Text;
		if (Chosen)
			Text.Format(_T("The track's key is %s now: its notes are the lighter lanes%s. Ctrl+Z undoes it."),
				ftapi::KeyChoiceName(Key).c_str(), Snap ? _T(", and new notes land on them") : _T(""));
		else if (Harmony.HasFound)
			Text.Format(_T("The key is found from the notes again: %s. Ctrl+Z undoes it."), ftapi::KeyName(Harmony.Found).c_str());
		else
			Text = _T("The key is found from the notes again, once there are some. Ctrl+Z undoes it.");
		Say(Text);
	}

	void ToggleSnap()
	{
		Snap = !Snap;
		Save();
		Say(Snap ? _T("Notes you draw or move land on the key's notes now; hold Alt for any note.") :
			_T("Notes you draw or move go on any note now (Snap to key is off)."));
	}

	// The Key menu: Auto, or a tonic in a mode, and snapping
	void KeyMenu(int Id)
	{
		CMenu Menu;
		Menu.CreatePopupMenu();
		CString Auto = _T("Auto: the key the notes fit");
		if (HarmonyRead && HarmonyTrack == Track && Harmony.HasFound)
			Auto += CString(_T(" (")) + ftapi::KeyName(Harmony.Found).c_str() + _T(")");
		Menu.AppendMenu(MF_STRING | (!KeyChosen ? MF_CHECKED : 0), MENU_KEY_AUTO, Auto);
		Menu.AppendMenu(MF_SEPARATOR);
		for (int s = 0; s < KEY_SCALE_COUNT; ++s) {
			if (s == 4)
				Menu.AppendMenu(MF_SEPARATOR);	// the modes after the usual four
			CMenu Tonics;
			Tonics.CreatePopupMenu();
			bool Here = false;
			for (int t = 0; t < 12; ++t) {
				const ftapi::KeyChoice Key(ftapi::Key(t, KEY_SCALES[s].Mode), KEY_SCALES[s].Pentatonic);
				const bool This = KeyChosen && ChosenKey == Key;
				Here = Here || This;
				Tonics.AppendMenu(MF_STRING | (This ? MF_CHECKED : 0), MENU_KEY + s * 12 + t, CString(ftapi::KeyChoiceName(Key).c_str()));
			}
			Menu.AppendMenu(MF_POPUP | (Here ? MF_CHECKED : 0), reinterpret_cast<UINT_PTR>(Tonics.Detach()), KEY_SCALES[s].Name);
		}
		Menu.AppendMenu(MF_SEPARATOR);
		Menu.AppendMenu(MF_STRING | (Snap ? MF_CHECKED : 0), MENU_KEY_SNAP, _T("Snap notes to the key (hold Alt for any note)"));
		const int Choice = PopUp(Menu, Id);
		if (Choice == static_cast<int>(MENU_KEY_AUTO))
			ChooseKey(false, ftapi::KeyChoice());
		else if (Choice == static_cast<int>(MENU_KEY_SNAP))
			ToggleSnap();
		else if (Choice >= static_cast<int>(MENU_KEY) && Choice < static_cast<int>(MENU_KEY) + KEY_SCALE_COUNT * 12) {
			const KeyMenuScale &Scale = KEY_SCALES[(Choice - static_cast<int>(MENU_KEY)) / 12];
			ChooseKey(true, ftapi::KeyChoice(ftapi::Key((Choice - static_cast<int>(MENU_KEY)) % 12, Scale.Mode), Scale.Pentatonic));
		}
	}

	// A chord of the strip in words, for the status line
	CString ChordText(const ftapi::HeardChord &Chord) const
	{
		const ftapi::Key K = TheKey().K;
		CString Where, Text;
		if (Bar > 0) {
			const int InFrame = Chord.Row % static_cast<int>(Length), Rows = std::min(Chord.Rows, static_cast<int>(Length) - InFrame);
			const int BarsPerFrame = (static_cast<int>(Length) + Bar - 1) / Bar;
			const int First = Chord.Row / static_cast<int>(Length) * BarsPerFrame + InFrame / Bar + 1;
			const int Last = Chord.Row / static_cast<int>(Length) * BarsPerFrame + (InFrame + Rows - 1) / Bar + 1;
			if (InFrame % Bar != 0 || (Rows < Bar && Last == First))
				Where.Format(_T("Bar %d, from beat %d: "), First, Beat > 0 ? InFrame % Bar / Beat + 1 : 1);
			else if (Last > First)
				Where.Format(_T("Bars %d-%d: "), First, Last);
			else
				Where.Format(_T("Bar %d: "), First);
		}
		Text.Format(_T("%s%s (%s in %s), the notes %s. "), static_cast<LPCTSTR>(Where), Chord.Name.c_str(), Chord.Degree.c_str(),
			ftapi::KeyName(K).c_str(), static_cast<LPCTSTR>(PitchClassList(Chord.PitchClasses, K)));
		if (Chord.InKey)
			Text += _T("They fit best while it sounds (the lit lanes); the key's other notes pass between them.");
		else
			Text += _T("It borrows notes from outside the key (the lit lanes), which fit while it sounds.");
		Text += _T(" Right-click: chords on the free channels.");
		return Text;
	}

	// The status line

	CString StatusText() const
	{
		CFamiTrackerDoc *pDoc = Doc();
		if (pDoc == NULL || Notes.empty())
			return CString();
		if (!Message.IsEmpty())
			return Message;
		CString Text;
		if (PlacingClip >= 0 && PanelDrop) {
			Text.Format(_T("Let go where the clip \"%s\" should start (it goes on %s%s). Hold Ctrl for a linked copy, Esc to stop."),
				static_cast<LPCTSTR>(pDoc->GetClips()->GetName(PlacingClip)), static_cast<LPCTSTR>(RecordingChannelName(pDoc, Channel)),
				ClipNotes.size() > 1 ? _T(" and the channels to its right") : _T(""));
			return Text;
		}
		if (PlacingClip >= 0) {
			Text.Format(_T("Click where the clip \"%s\" should start (it goes on %s%s). Right-click or Esc: never mind."),
				static_cast<LPCTSTR>(pDoc->GetClips()->GetName(PlacingClip)), static_cast<LPCTSTR>(RecordingChannelName(pDoc, Channel)),
				ClipNotes.size() > 1 ? _T(" and the channels to its right") : _T(""));
			return Text;
		}
		switch (Drag) {
		case DRAG_DRAW:
			Text.Format(_T("%s, %s"), static_cast<LPCTSTR>(NameOf(Channel, EditPitch)), static_cast<LPCTSTR>(Plural(EditEnd - EditStart, _T("row"), _T("rows"))));
			return Text;
		case DRAG_RESIZE:
			Text.Format(_T("%s long"), static_cast<LPCTSTR>(Plural(EditEnd - EditStart, _T("row"), _T("rows"))));
			return Text;
		case DRAG_MOVE:
			if (DragMoved) {
				Text.Format(_T("%s %s: %+d rows, to %s"), MoveCopy ? _T("Copying") : _T("Moving"),
					static_cast<LPCTSTR>(Plural(static_cast<int>(Moving.size()), _T("note"), _T("notes"))), MoveRows,
					static_cast<LPCTSTR>(NameOf(Channel, MovePitch)));
				return Text;
			}
			break;
		case DRAG_PAINT:
			Text.Format(_T("Painting a melody: %s"), static_cast<LPCTSTR>(Plural(static_cast<int>(PaintedNotes().size()), _T("note"), _T("notes"))));
			return Text;
		case DRAG_SELECT:
		case DRAG_RANGE:
			Text.Format(_T("%s selected"), static_cast<LPCTSTR>(Plural(static_cast<int>(Selection.size()), _T("note"), _T("notes"))));
			return Text;
		case DRAG_ERASE:
			return _T("Erasing: right-drag over notes");
		default:
			break;
		}
		if (HoverChord >= 0 && HoverChord < static_cast<int>(ShownChords().size()))
			return ChordText(ShownChords()[static_cast<size_t>(HoverChord)]);
		if (HoverChannel >= 0 && HoverChannel < static_cast<int>(Notes.size()) && HoverNote >= 0 &&
			HoverNote < static_cast<int>(Visible(HoverChannel).size()))
			return NoteText(HoverChannel, Visible(HoverChannel)[static_cast<size_t>(HoverNote)]);
		if (!Selection.empty()) {
			Text.Format(_T("%s selected   |   Ctrl+Shift+C: save as a clip. Drag to move (Shift: copy), arrows to move, ")
				_T("Shift+arrows: octave, Delete, Ctrl+C / Ctrl+V, E: effects, %s: chords on their bars, Esc: none"),
				static_cast<LPCTSTR>(Plural(static_cast<int>(Selection.size()), _T("note"), _T("notes"))), TypesNotes() ? _T("Chords") : _T("H"));
			return Text;
		}
		if (Tool == TOOL_PAINT)
			return _T("Paint: drag to paint a melody, a note per grid step; hold a pitch for a longer note. Right-click erases.");
		if (Tool == TOOL_SELECT)
			return TypesNotes() ? _T("Select: drag around notes, or along the ruler above for whole bars. Click a note to select it (Shift: ")
				_T("more). Then Ctrl+Shift+C saves them as a clip, and Chords puts chords on their bars.") :
				_T("Select: drag around notes, or along the ruler above for whole bars. Click a note to select it (Shift: more). ")
				_T("Then Ctrl+Shift+C saves them as a clip, and H puts chords on their bars.");
		if (SnapsToKey())
			return _T("Click to add a note: it lands on the key's notes (the lighter lanes; Alt: any note). Drag a note to move it ")
				_T("(Shift: copy), its end to change its length. Right-click erases. Ctrl+drag selects. E: effects. Ctrl+Z undoes.");
		return _T("Click to add a note, drag to make it longer. Drag a note to move it (Shift: copy), its end to change its ")
			_T("length. Right-click erases. Ctrl+drag selects. Double-click a note (or press E) for effects. ") +
			CString(TypesNotes() ? _T("Chords: a chord on the free channels. ") : _T("H: chords. ")) + _T("Ctrl+Z undoes.");
	}

	CString NoteText(int Which, const RollNote &Note) const
	{
		CFamiTrackerDoc *pDoc = Doc();
		const int Rows = Note.End - Note.Start;
		CString Text;
		Text.Format(_T("%s: %s, %s"), static_cast<LPCTSTR>(RecordingChannelName(pDoc, Which)),
			static_cast<LPCTSTR>(NameOf(Which, Note.Pitch)), static_cast<LPCTSTR>(Plural(Rows, _T("row"), _T("rows"))));
		if (Note.Written != Note.Pitch) {
			CString Slide;
			Slide.Format(_T(", sliding from %s"), static_cast<LPCTSTR>(PitchName(Note.Written)));
			Text += Slide;
		}
		if (Note.Instrument >= 0 && pDoc->IsInstrumentUsed(static_cast<unsigned int>(Note.Instrument))) {
			Text += _T(", instrument ");
			Text += InstrumentText(Note.Instrument);
		}
		const CString Effects = EffectsText(Note);
		if (!Effects.IsEmpty())
			Text += _T(", ") + Effects;
		if (Note.Pan != PAN_CENTER) {
			CString Pan;
			Pan.Format(_T(", panned %s (%c%02X)"), static_cast<LPCTSTR>(PanText(Note.Pan)), EFF_CHAR[EF_PAN - 1], Note.Pan);
			Text += Pan;
		}
		if (Bar > 0) {
			const int InFrame = Note.Start % static_cast<int>(Length);
			const int BarsPerFrame = (static_cast<int>(Length) + Bar - 1) / Bar;
			CString Where;
			Where.Format(_T(", bar %d"), Note.Start / static_cast<int>(Length) * BarsPerFrame + InFrame / Bar + 1);
			if (Beat > 0 && Beat < Bar) {
				CString InBar;
				InBar.Format(_T(" beat %d"), InFrame % Bar / Beat + 1);
				Where += InBar;
			}
			Text += Where;
		}
		if (Which == Channel && KeyApplies() && !InKey(Note.Pitch)) {
			Text += _T("   |   outside ");
			Text += KeyText();
		}
		if (Which != Channel)
			Text += _T("   |   Click: edit this channel");
		return Text;
	}

	// Drawing

	void DrawButton(CDC &dc, int Id, const CString &Text, int &X, int Right, bool Pressed, bool Menu, COLORREF Swatch = CLR_INVALID)
	{
		const CSettings *pSettings = theApp.GetSettings();
		const COLORREF Back_ = pSettings->Appearance.iColBackground, TextColor = pSettings->Appearance.iColPatternText;
		const CSize Size = dc.GetTextExtent(Text);
		const int Width = Size.cx + SX(16) + (Menu ? SX(12) : 0) + (Swatch != CLR_INVALID ? SX(14) : 0);
		if (X + Width > Right)
			return;	// no room
		const CRect Rect(X, Header.top + SY(3), X + Width, Header.bottom - SY(3));
		dc.FillSolidRect(&Rect, Pressed ? Mix(Back_, pSettings->Appearance.iColCursor, 0.45) : Mix(Back_, TextColor, 0.20));
		Outline(dc, Rect, Mix(Back_, TextColor, 0.42));
		int TextX = Rect.left + SX(8);
		if (Swatch != CLR_INVALID) {
			const int Size_ = SY(9);
			dc.FillSolidRect(TextX, Rect.top + (Rect.Height() - Size_) / 2, Size_, Size_, Swatch);
			TextX += SX(14);
		}
		dc.SetTextColor(TextColor);
		CRect Label(TextX, Rect.top, Rect.right, Rect.bottom);
		dc.DrawText(Text, &Label, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		if (Menu)
			Arrow(dc, Rect.right - SX(14), Rect.top + Rect.Height() / 2 - 1, TextColor);
		const Button Made = { Id, Rect };
		Buttons.push_back(Made);
		X = Rect.right + SX(4);
	}

	void DrawHeader(CDC &dc, COLORREF Panel)
	{
		const CSettings *pSettings = theApp.GetSettings();
		const COLORREF Back_ = pSettings->Appearance.iColBackground, TextColor = pSettings->Appearance.iColPatternText;
		dc.FillSolidRect(&Header, Focused ? Mix(Panel, pSettings->Appearance.iColCursor, 0.22) : Panel);
		dc.FillSolidRect(Header.left, Header.bottom - 1, Header.Width(), 1, Mix(Back_, TextColor, 0.30));
		Buttons.clear();

		// The close button on the right
		const CRect Close(Header.right - SX(26), Header.top + SY(3), Header.right - SX(4), Header.bottom - SY(3));
		Cross(dc, Close, TextColor);
		const Button Closing = { BUTTON_CLOSE, Close };
		Buttons.push_back(Closing);

		CFamiTrackerDoc *pDoc = Doc();
		if (pDoc == NULL || Notes.empty() || Channel >= static_cast<int>(ChanIDs.size()))
			return;
		const int Right = Close.left - SX(6);
		int X = Header.left + SX(6);
		DrawButton(dc, BUTTON_CHANNEL, RecordingChannelName(pDoc, Channel), X, Right, false, true,
			ChannelColor(ChanIDs[static_cast<size_t>(Channel)], Channel));
		X += SX(8);
		DrawButton(dc, BUTTON_DRAW, _T("Draw"), X, Right, Tool == TOOL_DRAW, false);
		DrawButton(dc, BUTTON_PAINT, _T("Paint melody"), X, Right, Tool == TOOL_PAINT, false);
		DrawButton(dc, BUTTON_SELECT, _T("Select"), X, Right, Tool == TOOL_SELECT, false);
		X += SX(8);
		CString Text;
		Text.Format(_T("Grid: %s"), GRID_NAMES[GridChoice]);
		DrawButton(dc, BUTTON_GRID, Text, X, Right, false, true);
		if (!HasKey())
			Text = _T("Key: auto");
		else
			Text.Format(KeyChosen ? _T("Key: %s") : _T("Key: %s (auto)"), static_cast<LPCTSTR>(KeyText()));
		DrawButton(dc, BUTTON_KEY, Text, X, Right, KeyChosen, true);
		DrawButton(dc, BUTTON_SNAP, _T("Snap to key"), X, Right, Snap, false);
		CString Name = InstrumentText(NewInstrument(CursorFrame * static_cast<int>(Length) + std::max(0, CursorRow)));
		if (Name.GetLength() > 22)
			Name = Name.Left(21) + _T("...");
		Text.Format(_T("Instrument: %s"), static_cast<LPCTSTR>(Name));
		DrawButton(dc, BUTTON_INSTRUMENT, Text, X, Right, false, true);
		DrawButton(dc, BUTTON_EFFECTS, _T("Effects"), X, Right, false, true);
		DrawButton(dc, BUTTON_CHORDS, _T("Chords"), X, Right, false, true);
		DrawButton(dc, BUTTON_CLIPS, PlacingClip >= 0 ? _T("Placing a clip") : _T("Clips"), X, Right, PlacingClip >= 0, true);
	}

	// What works on a note, drawn on it with the pen selected: vibrato as a wave, tremolo dotted along
	// its top, a chord's other notes as thin bars in their lanes, a glide as a line from the note before,
	// a fade as a wedge, a bend as a line to where it goes, other effects as small marks on their rows
	void DrawEffects(CDC &dc, const std::vector<RollNote> &List, size_t Index, const CRect &Rect, COLORREF Edge, COLORREF Full,
		COLORREF Back_) const
	{
		const RollNote &Note = List[Index];
		const int KeyH = KeyHeight(), Middle = Rect.top + Rect.Height() / 2;
		for (size_t s = 0; s < Note.Effects.size(); ++s) {
			const CChannelNotes::EffectSpan &Span = Note.Effects[s];
			const int X1 = std::max(static_cast<int>(Rect.left), RowX(Span.From));
			const int X2 = std::max(X1 + 1, std::min(static_cast<int>(Rect.right), RowX(Span.To)));
			switch (Span.Number) {
			case EF_VIBRATO: {
				const int Swing = std::max(1, (Rect.Height() - 3) / 3), Step = std::max(2, SX(3));
				bool Up = true;
				dc.MoveTo(X1, Middle);
				for (int X = X1 + Step; X <= X2; X += Step, Up = !Up)
					dc.LineTo(X, Up ? Middle - Swing : Middle + Swing);
				break;
			}
			case EF_TREMOLO:
				for (int X = X1 + 1; X < X2; X += std::max(3, SX(4)))
					dc.FillSolidRect(X, Rect.top + 1, std::max(1, SX(2)), std::max(1, SY(2)), Edge);
				break;
			case EF_ARPEGGIO: {
				const int Above[2] = { Span.Param >> 4, Span.Param & 0x0F };
				for (int k = 0; k < 2; ++k) {
					if (Above[k] == 0 || Note.Pitch + Above[k] >= PITCHES)
						continue;
					const int Top = PitchTop(Note.Pitch + Above[k]);
					dc.FillSolidRect(X1, Top + KeyH / 4, X2 - X1, std::max(1, KeyH - 2 * (KeyH / 4)), Mix(Back_, Full, 0.5));
				}
				break;
			}
			case EF_PORTAMENTO:
				if (Span.From == Note.Start && Index > 0) {
					// From the note before into this one
					const RollNote &Earlier = List[Index - 1];
					dc.MoveTo(std::min(RowX(Earlier.End) - 1, static_cast<int>(Rect.left)), PitchTop(Earlier.Pitch) + KeyH / 2);
					dc.LineTo(Rect.left + std::max(2, std::min(2 * RowWidth(), Rect.Width() / 2)), Middle);
				}
				break;
			case EF_PORTA_UP:
			case EF_PORTA_DOWN:
				dc.MoveTo(X1, Middle);
				dc.LineTo(X2, Span.Number == EF_PORTA_UP ? Rect.top : Rect.bottom - 1);
				break;
			case EF_VOLUME_SLIDE: {
				// The note thins out as it fades (or in as it grows)
				const bool Down = (Span.Param & 0x0F) != 0;
				const int Width = X2 - X1, Height = Rect.Height() - 2;
				for (int X = X1; X < X2; X += 2) {
					const double Part = double(X - X1) / Width;
					const int Covered = static_cast<int>((Down ? Part : 1.0 - Part) * Height + 0.5);
					if (Covered > 0)
						dc.FillSolidRect(X, Rect.top + 1, std::min(2, X2 - X), Covered, Mix(Full, Back_, 0.6));
				}
				break;
			}
			case EF_SLIDE_UP:
			case EF_SLIDE_DOWN:
				if (Span.From > Note.Start) {
					// A bend: from its row to the lane it goes to
					const int Semitones = Span.Param & 0x0F;
					const int To = std::max(0, std::min(PITCHES - 1, Note.Pitch + (Span.Number == EF_SLIDE_UP ? Semitones : -Semitones)));
					const int X = RowX(Span.From);
					dc.MoveTo(X, Middle);
					dc.LineTo(std::max(X + RowWidth(), static_cast<int>(Rect.right)), PitchTop(To) + KeyH / 2);
				}
				break;
			default: {
				const int Size = std::max(2, std::min(KeyH / 2, SX(4)));
				dc.FillSolidRect(RowX(Span.From) + 1, Rect.top + 1, Size, Size, Mix(Edge, RGB(255, 255, 255), 0.65));
				break;
			}
			}
		}
	}

	// The lanes: one per semitone. With a key, its notes' lanes are a little lighter than the others (whether the
	// colours are dark or light), the tonic's warmer, and a chord under the mouse in the chord strip lights its notes'
	// lanes while it sounds; without one, the piano's white keys' lanes lighter
	void DrawLanes(CDC &dc, int FirstPitch, int LastPitch, COLORREF OctaveLine) const
	{
		const CSettings *pSettings = theApp.GetSettings();
		const COLORREF Back_ = pSettings->Appearance.iColBackground, Text = pSettings->Appearance.iColPatternText;
		const bool Light = Luma(Back_) > Luma(Text);
		const COLORREF White = RGB(255, 255, 255), Black = RGB(0, 0, 0), Warm = RGB(255, 168, 64), Lit = RGB(96, 200, 255);
		const COLORREF InLane = Light ? Mix(Back_, White, 0.5) : Mix(Back_, Text, 0.09);
		const COLORREF OutLane = Light ? Mix(Back_, Text, 0.075) : Mix(Back_, Black, 0.3);
		const COLORREF TonicLane = Mix(InLane, Warm, Light ? 0.22 : 0.16);
		const COLORREF Unused = Mix(Back_, Black, 0.45);
		const bool Keyed = KeyApplies();
		const ftapi::KeyChoice K = TheKey();
		const int KeyH = KeyHeight();
		for (int p = FirstPitch; p <= LastPitch; ++p) {
			const int Y = PitchTop(p);
			COLORREF Lane = IsBlackKey(p) ? Back_ : Mix(Back_, Text, 0.07);
			if (Keyed)
				Lane = !ftapi::KeyHasPitch(K, p) ? OutLane : (ftapi::IsTonic(K, p) ? TonicLane : InLane);
			if (IsNoise(Channel) && p >= NOISE_PITCHES)
				Lane = Unused;
			dc.FillSolidRect(Grid.left, Y, Grid.Width(), KeyH, Lane);
			if (p % 12 == 0)
				dc.FillSolidRect(Grid.left, Y + KeyH - 1, Grid.Width(), 1, OctaveLine);
		}
		// The notes of the chord under the mouse, while it sounds
		if (HoverChord >= 0 && HoverChord < static_cast<int>(ShownChords().size()) && !IsNoise(Channel) && !IsDPCM(Channel)) {
			const ftapi::HeardChord &Chord = ShownChords()[static_cast<size_t>(HoverChord)];
			const int X1 = std::max(static_cast<int>(Grid.left), RowX(Chord.Row));
			const int X2 = std::min(static_cast<int>(Grid.right), RowX(Chord.Row + Chord.Rows));
			for (int p = FirstPitch; p <= LastPitch && X2 > X1; ++p)
				if (std::find(Chord.PitchClasses.begin(), Chord.PitchClasses.end(), p % 12) != Chord.PitchClasses.end())
					dc.FillSolidRect(X1, PitchTop(p), X2 - X1, KeyH, Mix(Light ? InLane : Back_, Lit, Light ? 0.30 : 0.22));
		}
	}

	// The chord strip under the ruler: the chord of each bar (or half bar, where it changes there) as the Claude Code
	// tools hear the parts play it, named in the key's spelling; chords from outside the key in another colour, the one
	// under the mouse and the one playing marked
	void DrawChordStrip(CDC &dc, COLORREF Panel, int FirstRow, int LastRow) const
	{
		const CSettings *pSettings = theApp.GetSettings();
		const COLORREF Back_ = pSettings->Appearance.iColBackground, Text = pSettings->Appearance.iColPatternText;
		const COLORREF Strip = Mix(Panel, Back_, 0.45), Line = Mix(Strip, Text, 0.25);
		const COLORREF Outside = Mix(Text, RGB(255, 120, 200), 0.55);
		const COLORREF Play = Mix(CPatternEditor::ROW_PLAY_COLOR, RGB(255, 255, 255), 0.55);
		dc.IntersectClipRect(&ChordStrip);
		dc.FillSolidRect(&ChordStrip, Strip);
		dc.FillSolidRect(ChordStrip.left, ChordStrip.bottom - 1, ChordStrip.Width(), 1, Line);
		const std::vector<ftapi::HeardChord> &Chords = ShownChords();
		for (size_t i = 0; i < Chords.size(); ++i) {
			const ftapi::HeardChord &Chord = Chords[i];
			if (Chord.Row + Chord.Rows <= FirstRow || Chord.Row > LastRow)
				continue;
			const int X1 = RowX(Chord.Row), X2 = RowX(Chord.Row + Chord.Rows);
			const bool Sounding = Playing && PlayRow >= Chord.Row && PlayRow < Chord.Row + Chord.Rows;
			if (static_cast<int>(i) == HoverChord || Sounding)
				dc.FillSolidRect(X1, ChordStrip.top, X2 - X1, ChordStrip.Height() - 1,
					Mix(Strip, Sounding ? Play : Text, static_cast<int>(i) == HoverChord ? 0.22 : 0.14));
			dc.FillSolidRect(X1, ChordStrip.top + SY(2), 1, ChordStrip.Height() - SY(4), Line);
			const CString Name(Chord.Name.c_str());
			CRect Label(std::max(X1, static_cast<int>(ChordStrip.left)) + SX(4), ChordStrip.top, X2 - SX(2), ChordStrip.bottom - 1);
			if (Label.Width() >= dc.GetTextExtent(Name).cx) {
				dc.SetTextColor(Chord.InKey ? Text : Outside);
				dc.DrawText(Name, &Label, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
			}
		}
		if (CursorFrame >= 0 && CursorRow >= 0)
			dc.FillSolidRect(RowX(CursorFrame * static_cast<int>(Length) + CursorRow), ChordStrip.top, 1, ChordStrip.Height(),
				pSettings->Appearance.iColCursor);
		dc.SelectClipRgn(NULL);
	}

	// Where a note is panned: an arrow inside it, at its left end pointing left (after the problems
	// panel's dot, Dotted, when it has one) or at its right end pointing right, filled when it's
	// (nearly) all the way over
	void DrawPan(CDC &dc, const RollNote &Note, const CRect &Rect, COLORREF Color, bool Dotted) const
	{
		const bool Left = Note.Pan < PAN_CENTER;
		const int Skip = (Left && Dotted) ? std::max(3, std::min(static_cast<int>(Rect.Height()) - 2, SY(6))) + std::max(1, SX(2)) + 2 : 0;
		if (Note.Pan == PAN_CENTER || Rect.Width() < 6 + Skip || Rect.Height() < 5)
			return;
		const bool Far = Left ? Note.Pan <= 0x20 : Note.Pan >= 0xE0;
		const int Height = Rect.Height() - 2, Middle = Rect.top + Rect.Height() / 2;
		const int Width = std::max(2, std::min(Height / 2 + 1, (Rect.Width() - Skip) / 2 - 1));
		const int Tip = Left ? Rect.left + 2 + Skip : Rect.right - 3, Base = Left ? Tip + Width : Tip - Width;
		POINT Points[3];
		Points[0].x = Tip;
		Points[0].y = Middle;
		Points[1].x = Base;
		Points[1].y = Rect.top + 1;
		Points[2].x = Base;
		Points[2].y = Rect.top + Height;
		CPen Pen(PS_SOLID, 1, Color);
		CBrush Brush(Color);
		CPen *pOldPen = dc.SelectObject(&Pen);
		CBrush *pOldBrush = Far ? dc.SelectObject(&Brush) : static_cast<CBrush*>(dc.SelectStockObject(NULL_BRUSH));
		dc.Polygon(Points, 3);
		dc.SelectObject(pOldBrush);
		dc.SelectObject(pOldPen);
	}

	void Draw(CDC &dc)
	{
		if (Dirty)
			Read();
		Clamp();
		const CSettings *pSettings = theApp.GetSettings();
		const COLORREF Back_ = pSettings->Appearance.iColBackground;
		const COLORREF Text = pSettings->Appearance.iColPatternText;
		const COLORREF Cursor = pSettings->Appearance.iColCursor;
		const COLORREF LaneWhite = Mix(Back_, Text, 0.07);
		const COLORREF RowLine = Mix(Back_, Text, 0.10), BeatLine = Mix(Back_, Text, 0.22), BarLine = Mix(Back_, Text, 0.38);
		const COLORREF FrameLine = Mix(Back_, Text, 0.62), OctaveLine = Mix(Back_, Text, 0.20);
		const COLORREF Panel = Mix(Back_, Text, 0.13), Past = Mix(Back_, RGB(0, 0, 0), 0.5), Room = Mix(Back_, Text, 0.04);
		const COLORREF Play = Mix(CPatternEditor::ROW_PLAY_COLOR, RGB(255, 255, 255), 0.55);
		const COLORREF OffKey = RGB(255, 80, 80), Picked = RGB(255, 255, 255);
		const int RowW = RowWidth(), KeyH = KeyHeight();
		const bool Keyed = KeyApplies();

		dc.FillSolidRect(&Client, Back_);
		CFont *pOldFont = static_cast<CFont*>(dc.SelectStockObject(DEFAULT_GUI_FONT));
		dc.SetBkMode(TRANSPARENT);

		const int FirstPitch = std::max(0, PitchAt(Grid.bottom - 1)), LastPitch = std::min(PITCHES - 1, PitchAt(Grid.top));
		const int FirstRow = std::max(0, RowAt(Grid.left)), LastRow = std::min(EditRows() - 1, RowAt(Grid.right));
		const int SongEnd = Previewing ? std::max(TotalRows(), PreviewRows) : TotalRows();

		// The grid: a lane per semitone (the key's lighter), lines for rows, beats, bars and frames
		dc.IntersectClipRect(&Grid);
		DrawLanes(dc, FirstPitch, LastPitch, OctaveLine);
		if (CursorFrame >= 0 && CursorRow >= 0) {
			const int Row = CursorFrame * static_cast<int>(Length) + CursorRow;
			dc.FillSolidRect(RowX(Row), Grid.top, RowW, Grid.Height(), Mix(LaneWhite, Cursor, 0.18));
		}
		for (int Row = FirstRow; Row <= LastRow; ++Row) {
			const int InFrame = Row % static_cast<int>(Length);
			COLORREF Color;
			if (InFrame == 0)
				Color = FrameLine;
			else if (Bar > 0 && InFrame % Bar == 0)
				Color = BarLine;
			else if (Beat > 0 && InFrame % Beat == 0)
				Color = BeatLine;
			else if (RowW >= SX(6))
				Color = RowLine;
			else
				continue;
			dc.FillSolidRect(RowX(Row), Grid.top, 1, Grid.Height(), Color);
		}
		// Past the end of the song: one more frame to write into (the song grows), then nothing
		const int EndX = RowX(SongEnd), StopX = RowX(EditRows());
		if (EndX < Grid.right)
			dc.FillSolidRect(EndX, Grid.top, std::max(0, std::min(StopX, static_cast<int>(Grid.right)) - EndX), Grid.Height(),
				Mix(Room, Back_, 0.3));
		if (EndX < Grid.right && EndX >= Grid.left)
			dc.FillSolidRect(EndX, Grid.top, std::max(1, SX(2)), Grid.Height(), FrameLine);
		if (StopX < Grid.right)
			dc.FillSolidRect(StopX, Grid.top, Grid.right - StopX, Grid.Height(), Past);

		// Notes: the other channels faint, then the bright one, with its effects
		for (int Pass = 0; Pass < 2; ++Pass) {
			for (size_t c = 0; c < Notes.size(); ++c) {
				const bool Bright = static_cast<int>(c) == Channel;
				if (Bright != (Pass == 1))
					continue;
				const COLORREF Full = ChannelColor(ChanIDs[c], static_cast<int>(c));
				const COLORREF Fill = Bright ? Full : Mix(Back_, Full, 0.38);
				const COLORREF Edge = Bright ? Mix(Full, RGB(0, 0, 0), 0.45) : Mix(Back_, Full, 0.55);
				CPen EffectPen(PS_SOLID, 1, Edge);
				CPen *pOldPen = dc.SelectObject(&EffectPen);
				const std::vector<RollNote> &List = Visible(static_cast<int>(c));
				const std::vector<int> &Chosen = (Bright && Previewing) ? Marked : Selection;
				const int Sounding = (Playing && PlayRow >= 0 && !(Bright && Previewing)) ? NoteAt(List, PlayRow) : -1;
				for (size_t i = FirstEndingAfter(List, FirstRow); i < List.size() && List[i].Start <= LastRow; ++i) {
					const RollNote &Note = List[i];
					if (Note.End <= FirstRow || Note.Pitch < FirstPitch || Note.Pitch > LastPitch)
						continue;
					const CRect Rect = NoteRect(Note);
					const bool Selected = Bright && Contains(Chosen, Note.Start);
					COLORREF Inside = static_cast<int>(i) == Sounding ? Mix(Fill, RGB(255, 255, 255), 0.45) : Fill;
					if (Selected)
						Inside = Mix(Inside, RGB(255, 255, 255), 0.35);
					dc.FillSolidRect(&Rect, Inside);
					Outline(dc, Rect, Edge);
					if (Note.Written != Note.Pitch && Note.Written >= FirstPitch - 12 && Note.Written <= LastPitch + 12) {
						// A slide: from where it starts to where it lands
						const int From = PitchTop(Note.Written) + KeyH / 2, To = PitchTop(Note.Pitch) + KeyH / 2;
						dc.FillSolidRect(Rect.left, std::min(From, To), std::max(1, SX(2)), std::abs(To - From) + 1, Edge);
						dc.FillSolidRect(Rect.left - SX(3), From, SX(4), std::max(1, SY(2)), Edge);
					}
					if (Bright && !Note.Effects.empty() && Rect.Height() >= 4)
						DrawEffects(dc, List, i, Rect, Edge, Full, Back_);
					const int Mark = (Bright && Previewing) ? 0 : ProblemMarkAt(static_cast<int>(Track), static_cast<int>(c), Note.Start);
					if (Bright)
						DrawPan(dc, Note, Rect, Edge, Mark != 0);
					if (Bright && Keyed && !InKey(Note.Pitch))
						Outline(dc, Rect, OffKey);
					ProblemDot(dc, Rect, Mark);
					if (Selected)
						Outline(dc, Rect, Picked, Rect.Height() >= SY(8) ? std::max(1, SX(2)) : 1);
					else if (static_cast<int>(c) == HoverChannel && static_cast<int>(i) == HoverNote)
						Outline(dc, Rect, Picked);
				}
				dc.SelectObject(pOldPen);
			}
		}

		// A clip being placed: where it would go
		if (PlacingClip >= 0 && PlaceRow >= 0) {
			for (size_t Column = 0; Column < ClipNotes.size(); ++Column) {
				const int Target = Channel + static_cast<int>(Column);
				if (Target >= static_cast<int>(Notes.size()))
					break;
				const COLORREF Full = ChannelColor(ChanIDs[static_cast<size_t>(Target)], Target);
				for (size_t i = 0; i < ClipNotes[Column].size(); ++i) {
					RollNote Note = ClipNotes[Column][i];
					Note.Start += PlaceRow;
					Note.End += PlaceRow;
					const CRect Rect = NoteRect(Note);
					dc.FillSolidRect(&Rect, Mix(Back_, Full, Column == 0 ? 0.55 : 0.30));
					Outline(dc, Rect, Picked);
				}
			}
			dc.FillSolidRect(RowX(PlaceRow), Grid.top, std::max(1, SX(2)), Grid.Height(), Picked);
		}

		// Selecting with Ctrl+drag (or the Select tool), or rows along the ruler
		if (Drag == DRAG_SELECT && !Band.IsRectEmpty())
			Outline(dc, Band, Cursor);
		if ((Drag == DRAG_RANGE && DragMoved) || (RangeKept && Drag == DRAG_NONE))
			Outline(dc, CRect(RowX(std::min(RangeFrom, RangeTo)), Grid.top, RowX(std::max(RangeFrom, RangeTo) + 1), Grid.bottom),
				Drag == DRAG_RANGE ? Cursor : Mix(Back_, Cursor, 0.6));

		// The cursor and the play position
		if (CursorFrame >= 0 && CursorRow >= 0)
			dc.FillSolidRect(RowX(CursorFrame * static_cast<int>(Length) + CursorRow), Grid.top, 1, Grid.Height(), Cursor);
		if (Playing && PlayRow >= 0)
			dc.FillSolidRect(RowX(PlayRow), Grid.top, std::max(2, SX(2)), Grid.Height(), Play);
		dc.SelectClipRgn(NULL);

		// The ruler: bar numbers (frame numbers without a bar highlight)
		dc.IntersectClipRect(&Ruler);
		dc.FillSolidRect(&Ruler, Panel);
		dc.SetTextColor(Text);
		const int BarRows = Bar > 0 ? Bar : static_cast<int>(Length);
		int Every = 1;	// label every bar, or every 2nd, 4th... when they're narrow
		while (Every * BarRows * RowW < SX(26) && Every < 1024)
			Every *= 2;
		const int BarsPerFrame = (static_cast<int>(Length) + BarRows - 1) / BarRows;
		for (int Row = FirstRow - (FirstRow % static_cast<int>(Length)); Row <= LastRow; ) {
			const int InFrame = Row % static_cast<int>(Length);
			const int Number = Row / static_cast<int>(Length) * BarsPerFrame + InFrame / BarRows + 1;
			const int X = RowX(Row);
			dc.FillSolidRect(X, InFrame == 0 ? Ruler.top : Ruler.top + Ruler.Height() / 2, 1, Ruler.Height(), InFrame == 0 ? FrameLine : BarLine);
			if ((Number - 1) % Every == 0) {
				CString Label;
				Label.Format(_T("%d"), Number);
				dc.TextOut(X + SX(3), Ruler.top + SY(3), Label);
			}
			const int Next = InFrame + BarRows;
			Row += (Next >= static_cast<int>(Length)) ? static_cast<int>(Length) - InFrame : BarRows;
		}
		// Sections: a band in their color under their bars, the name at the first bar (after its number) as far as the
		// next section; one that starts off to the left is named at the left edge
		for (size_t i = 0; i < Sections.size(); ++i) {
			const ftapi::Section &Section = Sections[i];
			const int From = Section.First * static_cast<int>(Length), To = (Section.Last + 1) * static_cast<int>(Length);
			if (To <= FirstRow || From > LastRow)
				continue;
			int Red, Green, Blue;
			ftapi::SectionRgb(ftapi::SectionColor(Section.Name), Red, Green, Blue);
			const COLORREF Color = RGB(Red, Green, Blue);
			const int Left = std::max(static_cast<int>(Ruler.left), RowX(From)), Right = std::min(static_cast<int>(Ruler.right), RowX(To));
			dc.FillSolidRect(Left, Ruler.bottom - SY(3), Right - Left, SY(3), Color);
			const int FirstBar = Section.First * BarsPerFrame + 1;
			CString Number;
			if (RowX(From) >= Ruler.left && (FirstBar - 1) % Every == 0)
				Number.Format(_T("%d"), FirstBar);
			const CString Name(Section.Name.c_str());
			CRect Tag(Left + SX(3) + (Number.IsEmpty() ? 0 : dc.GetTextExtent(Number).cx + SX(3)), Ruler.top + SY(2), Right - SX(2),
				Ruler.bottom - SY(4));
			if (Tag.Width() < SX(14))
				continue;
			Tag.right = std::min(Tag.right, Tag.left + dc.GetTextExtent(Name).cx + SX(8));
			const COLORREF Fill = Mix(Panel, Color, 0.75);
			dc.FillSolidRect(&Tag, Fill);
			Outline(dc, Tag, Color);
			dc.SetTextColor((GetRValue(Fill) * 3 + GetGValue(Fill) * 6 + GetBValue(Fill)) / 10 >= 140 ? RGB(16, 16, 20) : RGB(250, 250, 250));
			CRect Label(Tag);
			Label.DeflateRect(SX(4), 0);
			dc.DrawText(Name, &Label, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
			dc.SetTextColor(Text);
		}
		if (CursorFrame >= 0 && CursorRow >= 0)
			dc.FillSolidRect(RowX(CursorFrame * static_cast<int>(Length) + CursorRow), Ruler.top, 1, Ruler.Height(), Cursor);
		if (Playing && PlayRow >= 0)
			dc.FillSolidRect(RowX(PlayRow), Ruler.top, std::max(2, SX(2)), Ruler.Height(), Play);
		dc.SelectClipRgn(NULL);
		DrawChordStrip(dc, Panel, FirstRow, LastRow);

		// The keyboard, with the notes of the key marked
		dc.IntersectClipRect(&Keys);
		const int Sounding = (Playing && PlayRow >= 0 && Channel < static_cast<int>(Notes.size())) ?
			NoteAt(Notes[static_cast<size_t>(Channel)], PlayRow) : -1;
		const int SoundingPitch = Sounding >= 0 ? Notes[static_cast<size_t>(Channel)][static_cast<size_t>(Sounding)].Pitch : -1;
		const COLORREF Lit = Channel < static_cast<int>(ChanIDs.size()) ?
			ChannelColor(ChanIDs[static_cast<size_t>(Channel)], Channel) : RGB(0x80, 0xC0, 0xFF);
		for (int p = FirstPitch; p <= LastPitch; ++p) {
			const int Y = PitchTop(p);
			const bool Pressed = p == PreviewPitch || p == SoundingPitch;
			dc.FillSolidRect(Keys.left, Y, Keys.Width(), KeyH, Pressed && !IsBlackKey(p) ? Lit : RGB(236, 236, 236));
			if (IsBlackKey(p))
				dc.FillSolidRect(Keys.left, Y, Keys.Width() * 62 / 100, KeyH, Pressed ? Mix(Lit, RGB(0, 0, 0), 0.3) : RGB(40, 40, 44));
			if (p % 12 == 0 || p % 12 == 5)	// between B and C, and E and F
				dc.FillSolidRect(Keys.left, Y + KeyH - 1, Keys.Width(), 1, RGB(150, 150, 150));
			if (Keyed && InKey(p)) {
				const int Dot = std::max(2, KeyH / 3);
				dc.FillSolidRect(Keys.left + Keys.Width() * 62 / 100 + SX(3), Y + (KeyH - Dot) / 2, Dot, Dot,
					ftapi::IsTonic(TheKey(), p) ? RGB(230, 120, 40) : RGB(120, 160, 220));
			}
			if (p % 12 == 0 && KeyH >= SY(8)) {
				dc.SetTextColor(RGB(80, 80, 80));
				CRect Label(Keys.left, Y, Keys.right - SX(3), Y + KeyH);
				dc.DrawText(PitchName(p), &Label, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
			}
		}
		dc.FillSolidRect(Keys.right - 1, Keys.top, 1, Keys.Height(), RGB(110, 110, 110));
		dc.SelectClipRgn(NULL);

		// The corner above the keyboard, the buttons, and the status line
		dc.FillSolidRect(Client.left, Ruler.top, Keys.Width(), Keys.top - Ruler.top, Panel);
		CRect Corner(Client.left, ChordStrip.top, Keys.right - SX(4), ChordStrip.bottom);
		dc.SetTextColor(Mix(Panel, Text, 0.6));
		dc.DrawText(_T("Chords"), &Corner, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		DrawHeader(dc, Panel);
		dc.FillSolidRect(&Status, Panel);
		dc.SetTextColor(Text);
		CRect Line(Status);
		Line.DeflateRect(SX(6), 0);
		dc.DrawText(StatusText(), &Line, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

		dc.SelectObject(pOldFont);
	}
};

// The window

BEGIN_MESSAGE_MAP(CPianoRollWnd, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_SIZE()
	ON_WM_HSCROLL()
	ON_WM_VSCROLL()
	ON_WM_MOUSEWHEEL()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONDBLCLK()
	ON_WM_LBUTTONUP()
	ON_WM_RBUTTONDOWN()
	ON_WM_RBUTTONUP()
	ON_WM_MOUSEMOVE()
	ON_WM_CAPTURECHANGED()
	ON_WM_SETCURSOR()
	ON_WM_KEYDOWN()
	ON_WM_KEYUP()
	ON_WM_GETDLGCODE()
	ON_WM_SETFOCUS()
	ON_WM_KILLFOCUS()
	ON_WM_TIMER()
	ON_WM_DESTROY()
END_MESSAGE_MAP()

CPianoRollWnd::CPianoRollWnd() : m_pImpl(new Impl(this))
{
}

CPianoRollWnd::~CPianoRollWnd()
{
	delete m_pImpl;
}

bool CPianoRollWnd::Create(CWnd *pParent)
{
	m_pImpl->Load();
	const CString Class = AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(NULL, IDC_ARROW), NULL, NULL);
	return CreateEx(0, Class, _T("Piano roll"), WS_CHILD | WS_VISIBLE | WS_HSCROLL | WS_VSCROLL | WS_CLIPSIBLINGS,
		CRect(0, 0, 0, 0), pParent, 1) != FALSE;
}

void CPianoRollWnd::Activate(bool Shown)
{
	Impl &d = *m_pImpl;
	d.Shown = Shown;
	if (!Shown) {
		d.CancelDrag();
		d.StopPreview();
		d.PlacingClip = -1;
		d.PanelDrop = false;
		KillTimer(TIMER_ID);
		return;
	}
	d.Layout();
	d.Read();
	d.CursorChannel = -1;	// the bright channel is the cursor's
	d.Poll();
	if (!d.Placed && d.Grid.Height() > 0) {
		d.CenterOnNotes();
		d.Placed = true;
	}
	if (d.CursorFrame >= 0)
		d.FollowRow(d.CursorFrame * static_cast<int>(d.Length) + d.CursorRow, false);
	d.UpdateScrollBars();
	SetTimer(TIMER_ID, TIMER_MS, NULL);
	Invalidate(FALSE);
}

void CPianoRollWnd::SongChanged()
{
	m_pImpl->Dirty = true;
	if (m_pImpl->Shown && GetSafeHwnd() != NULL)
		Invalidate(FALSE);
}

void CPianoRollWnd::ShowNotes(int Channel, const int *pStarts, int Count, bool Select)
{
	if (GetSafeHwnd() != NULL && m_pImpl->Shown)
		m_pImpl->ShowNotes(Channel, pStarts, Count, Select);
}

bool CPianoRollWnd::ShowClipDrop(const CPoint &ScreenPoint, int Index, bool Scroll, int &Channel, int &Frame, int &Row)
{
	Impl &d = *m_pImpl;
	if (GetSafeHwnd() == NULL)
		return false;
	// Only where the roll can be seen at that point
	CPoint Point = ScreenPoint;
	ScreenToClient(&Point);
	if (!d.Shown || !IsWindowVisible() || ::WindowFromPoint(ScreenPoint) != m_hWnd || d.Length == 0 || !d.ShowDrop(Point, Index, Scroll)) {
		d.HideDrop();
		return false;
	}
	Channel = d.Channel;
	Frame = d.PlaceRow / static_cast<int>(d.Length);
	Row = d.PlaceRow % static_cast<int>(d.Length);
	return true;
}

bool CPianoRollWnd::HideClipDrop()
{
	return GetSafeHwnd() != NULL && m_pImpl->HideDrop();
}

bool CPianoRollWnd::HasSelection() const
{
	return GetSafeHwnd() != NULL && m_pImpl->Shown && !m_pImpl->Selection.empty();
}

void CPianoRollWnd::SaveSelectionAsClip()
{
	m_pImpl->SaveSelectionAsClip();
	Invalidate(FALSE);
}

bool CPianoRollWnd::EditCommand(UINT nID)
{
	Impl &d = *m_pImpl;
	switch (nID) {
	case ID_EDIT_CUT: return d.KeyDown('X', true, false, false);
	case ID_EDIT_COPY: return d.KeyDown('C', true, false, false);
	case ID_EDIT_PASTE: return d.KeyDown('V', true, false, false);
	case ID_EDIT_DELETE: return d.KeyDown(VK_DELETE, false, false, false);
	case ID_EDIT_SELECTALL: return d.KeyDown('A', true, false, false);
	default: return false;
	}
}

bool CPianoRollWnd::CanEditCommand(UINT nID) const
{
	const Impl &d = *m_pImpl;
	if (d.Drag != Impl::DRAG_NONE)
		return false;
	switch (nID) {
	case ID_EDIT_CUT: case ID_EDIT_COPY: case ID_EDIT_DELETE: return !d.Selection.empty();
	case ID_EDIT_PASTE: return d.HasCopied;
	case ID_EDIT_SELECTALL: return true;
	default: return false;
	}
}

BOOL CPianoRollWnd::PreTranslateMessage(MSG *pMsg)
{
	// Keys for the notes come here before FamiTracker's shortcuts see them
	if ((pMsg->message == WM_KEYDOWN || pMsg->message == WM_SYSKEYDOWN) && pMsg->hwnd == GetSafeHwnd()) {
		const bool Ctrl = ::GetKeyState(VK_CONTROL) < 0, Shift = ::GetKeyState(VK_SHIFT) < 0, Alt = ::GetKeyState(VK_MENU) < 0;
		if (m_pImpl->KeyDown(static_cast<UINT>(pMsg->wParam), Ctrl, Shift, Alt))
			return TRUE;
	}
	return CWnd::PreTranslateMessage(pMsg);
}

void CPianoRollWnd::OnPaint()
{
	CPaintDC PaintDC(this);
	Impl &d = *m_pImpl;
	d.Layout();
	if (d.Client.Width() <= 0 || d.Client.Height() <= 0)
		return;
	if (d.BackSize != d.Client.Size()) {
		d.Back.DeleteObject();
		d.Back.CreateCompatibleBitmap(&PaintDC, d.Client.Width(), d.Client.Height());
		d.BackSize = d.Client.Size();
	}
	CDC dc;
	dc.CreateCompatibleDC(&PaintDC);
	CBitmap *pOld = dc.SelectObject(&d.Back);
	d.Draw(dc);
	PaintDC.BitBlt(0, 0, d.Client.Width(), d.Client.Height(), &dc, 0, 0, SRCCOPY);
	dc.SelectObject(pOld);
}

BOOL CPianoRollWnd::OnEraseBkgnd(CDC *pDC)
{
	return TRUE;	// all of it is painted
}

void CPianoRollWnd::OnSize(UINT nType, int cx, int cy)
{
	CWnd::OnSize(nType, cx, cy);
	Impl &d = *m_pImpl;
	d.Layout();
	if (!d.Placed && d.Shown && d.Grid.Height() > 0) {
		d.CenterOnNotes();
		d.Placed = true;
	}
	d.UpdateScrollBars();
	Invalidate(FALSE);
}

void CPianoRollWnd::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar *pScrollBar)
{
	Impl &d = *m_pImpl;
	const int Step = d.RowWidth() * std::max(1, d.Beat);
	switch (nSBCode) {
	case SB_LINELEFT: d.ScrollX -= Step; break;
	case SB_LINERIGHT: d.ScrollX += Step; break;
	case SB_PAGELEFT: d.ScrollX -= d.Grid.Width() * 9 / 10; break;
	case SB_PAGERIGHT: d.ScrollX += d.Grid.Width() * 9 / 10; break;
	case SB_LEFT: d.ScrollX = 0; break;
	case SB_RIGHT: d.ScrollX = d.EditRows() * d.RowWidth(); break;
	case SB_THUMBTRACK:
	case SB_THUMBPOSITION: {
		SCROLLINFO Info;
		memset(&Info, 0, sizeof(Info));
		Info.cbSize = sizeof(Info);
		Info.fMask = SIF_TRACKPOS;
		GetScrollInfo(SB_HORZ, &Info);
		d.ScrollX = Info.nTrackPos;	// full 32 bits, unlike nPos
		break;
	}
	}
	d.UpdateScrollBars();
	Invalidate(FALSE);
}

void CPianoRollWnd::OnVScroll(UINT nSBCode, UINT nPos, CScrollBar *pScrollBar)
{
	Impl &d = *m_pImpl;
	switch (nSBCode) {
	case SB_LINEUP: d.ScrollY -= d.KeyHeight(); break;
	case SB_LINEDOWN: d.ScrollY += d.KeyHeight(); break;
	case SB_PAGEUP: d.ScrollY -= d.KeyHeight() * 12; break;
	case SB_PAGEDOWN: d.ScrollY += d.KeyHeight() * 12; break;
	case SB_TOP: d.ScrollY = 0; break;
	case SB_BOTTOM: d.ScrollY = PITCHES * d.KeyHeight(); break;
	case SB_THUMBTRACK:
	case SB_THUMBPOSITION: {
		SCROLLINFO Info;
		memset(&Info, 0, sizeof(Info));
		Info.cbSize = sizeof(Info);
		Info.fMask = SIF_TRACKPOS;
		GetScrollInfo(SB_VERT, &Info);
		d.ScrollY = Info.nTrackPos;
		break;
	}
	}
	d.UpdateScrollBars();
	Invalidate(FALSE);
}

BOOL CPianoRollWnd::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
	Impl &d = *m_pImpl;
	d.Layout();
	int Notches = zDelta / WHEEL_DELTA;
	if (Notches == 0)
		Notches = zDelta > 0 ? 1 : -1;
	if (nFlags & MK_CONTROL) {
		// Zoom in or out around the row under the mouse
		ScreenToClient(&pt);
		const int MouseX = std::max(0, std::min(static_cast<int>(pt.x - d.Grid.left), d.Grid.Width()));
		const double Row = double(MouseX + d.ScrollX) / d.RowWidth();
		d.Zoom = std::max(0, std::min(ZOOMS - 1, d.Zoom + (Notches > 0 ? 1 : -1)));
		d.ScrollX = static_cast<int>(Row * d.RowWidth()) - MouseX;
		d.Save();
	}
	else if (nFlags & MK_SHIFT)
		d.ScrollX -= Notches * d.RowWidth() * std::max(1, d.Beat) * 2;
	else
		d.ScrollY -= Notches * d.KeyHeight() * 3;
	d.UpdateScrollBars();
	Invalidate(FALSE);
	return TRUE;
}

void CPianoRollWnd::OnLButtonDown(UINT nFlags, CPoint point)
{
	SetFocus();
	m_pImpl->MouseDown(point, nFlags, false);
	if (m_pImpl->Drag != Impl::DRAG_NONE)
		SetCapture();
	Invalidate(FALSE);
}

void CPianoRollWnd::OnLButtonDblClk(UINT nFlags, CPoint point)
{
	SetFocus();
	m_pImpl->DoubleClick(point, nFlags);
	if (m_pImpl->Drag != Impl::DRAG_NONE)
		SetCapture();
	Invalidate(FALSE);
}

void CPianoRollWnd::OnLButtonUp(UINT nFlags, CPoint point)
{
	if (m_pImpl->Drag != Impl::DRAG_ERASE)
		m_pImpl->MouseUp(false);
	if (GetCapture() == this && m_pImpl->Drag == Impl::DRAG_NONE)
		ReleaseCapture();
	Invalidate(FALSE);
}

void CPianoRollWnd::OnRButtonDown(UINT nFlags, CPoint point)
{
	SetFocus();
	m_pImpl->MouseDown(point, nFlags, true);
	if (m_pImpl->Drag != Impl::DRAG_NONE)
		SetCapture();
	Invalidate(FALSE);
}

void CPianoRollWnd::OnRButtonUp(UINT nFlags, CPoint point)
{
	if (m_pImpl->Drag == Impl::DRAG_ERASE)
		m_pImpl->MouseUp(true);
	if (GetCapture() == this && m_pImpl->Drag == Impl::DRAG_NONE)
		ReleaseCapture();
	Invalidate(FALSE);
}

void CPianoRollWnd::OnCaptureChanged(CWnd *pWnd)
{
	// Another window took the mouse (a menu, a dialog): what the mouse was doing stops
	if (pWnd != this)
		m_pImpl->CancelDrag();
	CWnd::OnCaptureChanged(pWnd);
}

void CPianoRollWnd::OnMouseMove(UINT nFlags, CPoint point)
{
	m_pImpl->MouseMove(point);
}

BOOL CPianoRollWnd::OnSetCursor(CWnd *pWnd, UINT nHitTest, UINT message)
{
	Impl &d = *m_pImpl;
	if (nHitTest != HTCLIENT)
		return CWnd::OnSetCursor(pWnd, nHitTest, message);
	CPoint Point;
	::GetCursorPos(&Point);
	ScreenToClient(&Point);
	d.Layout();
	LPCTSTR Shape = IDC_ARROW;
	switch (d.Drag) {
	case Impl::DRAG_MOVE: Shape = d.DragMoved ? IDC_SIZEALL : IDC_ARROW; break;
	case Impl::DRAG_RESIZE: Shape = IDC_SIZEWE; break;
	case Impl::DRAG_DRAW:
	case Impl::DRAG_PAINT: Shape = IDC_CROSS; break;
	case Impl::DRAG_NONE:
		if (d.Grid.PtInRect(Point) && !d.Notes.empty()) {
			int HitChannel = -1, HitIndex = -1;
			if (d.PlacingClip >= 0)
				Shape = IDC_CROSS;
			else if (d.HitNote(Point, HitChannel, HitIndex) && HitChannel == d.Channel)
				Shape = d.OnEnd(Point, d.Notes[static_cast<size_t>(HitChannel)][static_cast<size_t>(HitIndex)]) ? IDC_SIZEWE : IDC_SIZEALL;
			else if (HitChannel >= 0 && ::GetKeyState(VK_SHIFT) >= 0)
				Shape = IDC_HAND;
			else if (d.RowAt(Point.x) < d.EditRows())
				Shape = IDC_CROSS;
		}
		else if (d.HitButton(Point) >= 0)
			Shape = IDC_HAND;
		break;
	default:
		break;
	}
	::SetCursor(::LoadCursor(NULL, Shape));
	return TRUE;
}

void CPianoRollWnd::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	switch (nChar) {
	case VK_LEFT: OnHScroll(SB_LINELEFT, 0, NULL); return;
	case VK_RIGHT: OnHScroll(SB_LINERIGHT, 0, NULL); return;
	case VK_UP: OnVScroll(SB_LINEUP, 0, NULL); return;
	case VK_DOWN: OnVScroll(SB_LINEDOWN, 0, NULL); return;
	case VK_PRIOR: OnVScroll(SB_PAGEUP, 0, NULL); return;
	case VK_NEXT: OnVScroll(SB_PAGEDOWN, 0, NULL); return;
	case VK_HOME: OnHScroll(SB_LEFT, 0, NULL); return;
	case VK_END: OnHScroll(SB_RIGHT, 0, NULL); return;
	}
	// In the pattern editor's place, its note keys play and write notes here (and the keypad's digits pick
	// an instrument, F2-F9 an octave)
	if (Impl::TypesNotes() && Impl::View()->PianoRollKey(nChar, true))
		return;
	CWnd::OnKeyDown(nChar, nRepCnt, nFlags);
}

void CPianoRollWnd::OnKeyUp(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	if (Impl::TypesNotes())
		Impl::View()->PianoRollKey(nChar, false);
	CWnd::OnKeyUp(nChar, nRepCnt, nFlags);
}

UINT CPianoRollWnd::OnGetDlgCode()
{
	return DLGC_WANTALLKEYS | DLGC_WANTARROWS | DLGC_WANTCHARS;
}

void CPianoRollWnd::OnSetFocus(CWnd *pOldWnd)
{
	CWnd::OnSetFocus(pOldWnd);
	m_pImpl->Focused = true;
	Invalidate(FALSE);
}

void CPianoRollWnd::OnKillFocus(CWnd *pNewWnd)
{
	CWnd::OnKillFocus(pNewWnd);
	m_pImpl->Focused = false;
	Invalidate(FALSE);
}

void CPianoRollWnd::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == TIMER_ID) {
		Impl &d = *m_pImpl;
		d.Layout();
		bool Changed = d.Poll() || d.Dirty;
		if (d.HarmonyDueNow()) {
			d.UpdateHarmony();
			Changed = true;
		}
		if (Changed)
			Invalidate(FALSE);
		return;
	}
	CWnd::OnTimer(nIDEvent);
}

void CPianoRollWnd::OnDestroy()
{
	KillTimer(TIMER_ID);
	m_pImpl->StopPreview();
	m_pImpl->Save();
	CWnd::OnDestroy();
}

// The bar it is docked in

BEGIN_MESSAGE_MAP(CPianoRollBar, CControlBar)
	ON_WM_SIZE()
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_MOUSEMOVE()
	ON_WM_SETCURSOR()
	ON_WM_CAPTURECHANGED()
	ON_WM_DESTROY()
	ON_MESSAGE(WM_SIZEPARENT, OnSizeParent)
END_MESSAGE_MAP()

CPianoRollBar::CPianoRollBar() : m_bFill(false), m_iHeight(DEFAULT_HEIGHT), m_iRoom(0), m_iDragFrom(-1), m_iDragHeight(0), m_iDragMax(0)
{
}

CPianoRollBar::~CPianoRollBar()
{
}

bool CPianoRollBar::Create(CFrameWnd *pFrame)
{
	m_iHeight = std::max(SY(MIN_HEIGHT), static_cast<int>(theApp.GetProfileInt(SECTION, _T("Height"), SY(DEFAULT_HEIGHT))));
	m_dwStyle = CBRS_ALIGN_BOTTOM;
	const CString Class = AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(NULL, IDC_ARROW), NULL, NULL);
	if (!CWnd::Create(Class, _T("Piano roll"), WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, CRect(0, 0, 0, 0), pFrame,
		AFX_IDW_CONTROLBAR_FIRST + 32))
		return false;
	// Laid out after the frame's other bars, so it sits right under the pattern editor
	SetWindowPos(&CWnd::wndBottom, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	return m_wndRoll.Create(this);
}

void CPianoRollBar::Opened(bool Open)
{
	m_wndRoll.Activate(Open);
	if (!Open && ::GetFocus() == m_wndRoll.GetSafeHwnd()) {
		// The keys go back to the pattern editor
		CFrameWnd *pFrame = GetParentFrame();
		CView *pView = pFrame != NULL ? pFrame->GetActiveView() : NULL;
		if (pView != NULL)
			pView->SetFocus();
	}
	theApp.WriteProfileInt(SECTION, _T("Open"), Open ? 1 : 0);
}

void CPianoRollBar::Fill(bool On)
{
	if (On == m_bFill)
		return;
	m_bFill = On;
	m_iDragFrom = -1;
	if (GetSafeHwnd() != NULL && m_wndRoll.GetSafeHwnd() != NULL) {
		CRect Client;
		GetClientRect(&Client);
		m_wndRoll.MoveWindow(0, Divider(), Client.Width(), std::max(0, static_cast<int>(Client.Height()) - Divider()));
		Invalidate(FALSE);
	}
}

void CPianoRollBar::SongChanged()
{
	m_wndRoll.SongChanged();
}

bool CPianoRollBar::WasOpen()
{
	return theApp.GetProfileInt(SECTION, _T("Open"), 0) != 0;
}

int CPianoRollBar::Divider() const
{
	return m_bFill ? 0 : std::max(4, SY(DIVIDER));	// nothing to share the room with in the pattern editor's place
}

int CPianoRollBar::MaxHeight() const
{
	return std::max(SY(MIN_HEIGHT), m_iRoom - SY(MIN_VIEW_HEIGHT));
}

LRESULT CPianoRollBar::OnSizeParent(WPARAM wParam, LPARAM lParam)
{
	// The room left for the pattern editor and this bar
	const AFX_SIZEPARENTPARAMS *pLayout = reinterpret_cast<const AFX_SIZEPARENTPARAMS*>(lParam);
	m_iRoom = pLayout->rect.bottom - pLayout->rect.top;
	return CControlBar::OnSizeParent(wParam, lParam);
}

CSize CPianoRollBar::CalcFixedLayout(BOOL bStretch, BOOL bHorz)
{
	return CSize(32767, std::max(0, m_bFill ? m_iRoom : std::min(m_iHeight, MaxHeight())));
}

void CPianoRollBar::OnUpdateCmdUI(CFrameWnd *pTarget, BOOL bDisableIfNoHndler)
{
	// Nothing in it is a command
}

BOOL CPianoRollBar::PreTranslateMessage(MSG *pMsg)
{
	// Not as a dialog (as control bars are), so FamiTracker's shortcuts and the piano roll's keys work
	return CWnd::PreTranslateMessage(pMsg);
}

void CPianoRollBar::OnSize(UINT nType, int cx, int cy)
{
	CControlBar::OnSize(nType, cx, cy);
	if (m_wndRoll.GetSafeHwnd() != NULL)
		m_wndRoll.MoveWindow(0, Divider(), cx, std::max(0, cy - Divider()));
}

void CPianoRollBar::OnPaint()
{
	CPaintDC dc(this);
	CRect Rect;
	GetClientRect(&Rect);
	const int Line = Divider();
	if (Line <= 0)
		return;		// filling the pattern editor's place: the piano roll covers it all
	dc.FillSolidRect(Rect.left, Rect.top, Rect.Width(), Line, ::GetSysColor(COLOR_3DFACE));
	dc.FillSolidRect(Rect.left, Rect.top, Rect.Width(), 1, ::GetSysColor(COLOR_3DHILIGHT));
	dc.FillSolidRect(Rect.left, Rect.top + Line - 1, Rect.Width(), 1, ::GetSysColor(COLOR_3DSHADOW));
	// A grip in the middle
	const int X = Rect.left + Rect.Width() / 2 - SX(12);
	for (int i = 0; i < 4; ++i)
		dc.FillSolidRect(X + i * SX(7), Rect.top + Line / 2 - 1, SX(3), 2, ::GetSysColor(COLOR_3DSHADOW));
}

BOOL CPianoRollBar::OnEraseBkgnd(CDC *pDC)
{
	return TRUE;	// the line is painted, the piano roll covers the rest
}

void CPianoRollBar::OnLButtonDown(UINT nFlags, CPoint point)
{
	if (m_bFill || point.y >= Divider())
		return;
	// Drag the line to make the piano roll taller or shorter
	CPoint Screen = point;
	ClientToScreen(&Screen);
	m_iDragFrom = static_cast<int>(Screen.y);
	m_iDragHeight = m_iHeight;
	m_iDragMax = MaxHeight();
	SetCapture();
}

void CPianoRollBar::OnMouseMove(UINT nFlags, CPoint point)
{
	if (m_iDragFrom < 0)
		return;
	CPoint Screen = point;
	ClientToScreen(&Screen);
	const int Height = std::max(SY(MIN_HEIGHT), std::min(m_iDragMax, m_iDragHeight - (static_cast<int>(Screen.y) - m_iDragFrom)));
	if (Height != m_iHeight) {
		m_iHeight = Height;
		CFrameWnd *pFrame = GetParentFrame();
		if (pFrame != NULL)
			pFrame->RecalcLayout();
	}
}

void CPianoRollBar::OnLButtonUp(UINT nFlags, CPoint point)
{
	if (m_iDragFrom >= 0) {
		m_iDragFrom = -1;
		ReleaseCapture();
		theApp.WriteProfileInt(SECTION, _T("Height"), m_iHeight);
	}
}

void CPianoRollBar::OnCaptureChanged(CWnd *pWnd)
{
	m_iDragFrom = -1;
	CControlBar::OnCaptureChanged(pWnd);
}

BOOL CPianoRollBar::OnSetCursor(CWnd *pWnd, UINT nHitTest, UINT message)
{
	if (pWnd == this && nHitTest == HTCLIENT) {
		::SetCursor(::LoadCursor(NULL, IDC_SIZENS));
		return TRUE;
	}
	return CControlBar::OnSetCursor(pWnd, nHitTest, message);
}

void CPianoRollBar::OnDestroy()
{
	theApp.WriteProfileInt(SECTION, _T("Height"), m_iHeight);
	CControlBar::OnDestroy();
}

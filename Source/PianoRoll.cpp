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

// The piano roll window (see PianoRoll.h).

// Standard headers come before stdafx.h, whose DEBUG_NEW macro would
// otherwise rewrite the placement new inside the standard library.
#include <algorithm>
#include <cstring>
#include <vector>
#include "stdafx.h"
#include "FamiTracker.h"
#include "FamiTrackerDoc.h"
#include "FamiTrackerView.h"
#include "MainFrm.h"
#include "PatternEditor.h"
#include "SoundGen.h"
#include "TrackerChannel.h"
#include "Settings.h"
#include "InsertRecording.h"
#include "PianoRoll.h"

namespace {

const int PITCHES = OCTAVE_RANGE * 12;	// C-0 to B-7
const UINT_PTR TIMER_ID = 1;
const UINT TIMER_MS = 30;
const TCHAR SECTION[] = _T("Piano roll");

// Sizes at 96 DPI (scaled with SX and SY)
const int KEYS_WIDTH = 52;
const int RULER_HEIGHT = 20;
const int STATUS_HEIGHT = 18;
const int KEY_HEIGHT = 10;
const int ROW_WIDTHS[] = { 2, 3, 4, 6, 8, 10, 12, 16, 20, 24, 32, 40, 48 };	// zoom steps
const int ZOOMS = sizeof(ROW_WIDTHS) / sizeof(ROW_WIDTHS[0]);
const int DEFAULT_ZOOM = 6;

const TCHAR *const NOTE_NAMES[] = {
	_T("C-"), _T("C#"), _T("D-"), _T("D#"), _T("E-"), _T("F-"), _T("F#"), _T("G-"), _T("G#"), _T("A-"), _T("A#"), _T("B-")
};

struct RollNote {
	int Start;			// rows from the start of the song
	int End;			// the row of the next note, release or halt in the channel
	int Pitch;			// 0 = C-0 ... 95 = B-7
	int Instrument;		// -1 when none was set yet
	bool Released;		// ends on a release (===)
};

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

void Outline(CDC &dc, const CRect &Rect, COLORREF Color)
{
	dc.FillSolidRect(Rect.left, Rect.top, Rect.Width(), 1, Color);
	dc.FillSolidRect(Rect.left, Rect.bottom - 1, Rect.Width(), 1, Color);
	dc.FillSolidRect(Rect.left, Rect.top, 1, Rect.Height(), Color);
	dc.FillSolidRect(Rect.right - 1, Rect.top, 1, Rect.Height(), Color);
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
	bool Dirty;

	// The view
	int Zoom;			// into ROW_WIDTHS
	int ScrollX;		// pixels
	int ScrollY;
	int Channel;		// shown brightest: the pattern editor cursor's
	bool Placed;		// scrolled to the notes once
	CRect Client, Keys, Ruler, Grid, Status;

	// The rest of FamiTracker, as last seen
	int CursorFrame, CursorRow, CursorChannel;
	bool Playing;
	int PlayRow;		// from the start of the song, -1 when not playing this track

	// The mouse
	int HoverChannel, HoverNote;
	int PreviewChannel, PreviewPitch;	// a key being played, -1: none

	CBitmap Back;		// the picture is drawn here, then copied to the window
	CSize BackSize;

	explicit Impl(CWnd *pWindow) :
		pWnd(pWindow), Track(0), Frames(1), Length(64), Beat(4), Bar(16), Dirty(true),
		Zoom(DEFAULT_ZOOM), ScrollX(0), ScrollY(0), Channel(0), Placed(false),
		CursorFrame(-1), CursorRow(-1), CursorChannel(-1), Playing(false), PlayRow(-1),
		HoverChannel(-1), HoverNote(-1), PreviewChannel(-1), PreviewPitch(-1), BackSize(0, 0)
	{
	}

	static CFamiTrackerView *View() { return CFamiTrackerView::GetView(); }
	static CMainFrame *Main() { return static_cast<CMainFrame*>(theApp.m_pMainWnd); }
	static CFamiTrackerDoc *Doc()
	{
		CFamiTrackerView *pView = View();
		return pView != NULL ? pView->GetDocument() : NULL;
	}

	int TotalRows() const { return static_cast<int>(Frames * Length); }
	int RowWidth() const { return SX(ROW_WIDTHS[Zoom]); }
	int KeyHeight() const { return std::max(4, SY(KEY_HEIGHT)); }

	void Layout()
	{
		pWnd->GetClientRect(&Client);
		Keys = CRect(Client.left, Client.top + SY(RULER_HEIGHT), Client.left + SX(KEYS_WIDTH), Client.bottom - SY(STATUS_HEIGHT));
		Ruler = CRect(Keys.right, Client.top, Client.right, Keys.top);
		Grid = CRect(Keys.right, Keys.top, Client.right, Keys.bottom);
		Status = CRect(Client.left, Keys.bottom, Client.right, Client.bottom);
	}

	int RowX(int Row) const { return Grid.left + Row * RowWidth() - ScrollX; }
	int PitchTop(int Pitch) const { return Grid.top + (PITCHES - 1 - Pitch) * KeyHeight() - ScrollY; }
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

	// Reads every channel's notes
	void Read()
	{
		Dirty = false;
		Notes.clear();
		ChanIDs.clear();
		HoverChannel = HoverNote = -1;
		CFamiTrackerDoc *pDoc = Doc();
		CMainFrame *pMain = Main();
		if (pDoc == NULL || pMain == NULL)
			return;

		Track = static_cast<unsigned int>(std::max(0, pMain->GetSelectedTrack()));
		if (Track >= pDoc->GetTrackCount())
			Track = 0;
		Frames = std::max(1U, pDoc->GetFrameCount(Track));
		Length = std::max(1U, pDoc->GetPatternLength(Track));
		Beat = std::max(0, pDoc->GetFirstHighlight());
		Bar = std::max(0, pDoc->GetSecondHighlight());

		const int Channels = static_cast<int>(pDoc->GetAvailableChannels());
		Notes.resize(static_cast<size_t>(std::max(0, Channels)));
		for (int c = 0; c < Channels; ++c) {
			ChanIDs.push_back(pDoc->GetChannelType(c));
			std::vector<RollNote> &List = Notes[static_cast<size_t>(c)];
			int Instrument = -1;
			bool Open = false;
			for (unsigned int f = 0; f < Frames; ++f) {
				for (unsigned int r = 0; r < Length; ++r) {
					stChanNote Cell;
					pDoc->GetNoteData(Track, f, static_cast<unsigned int>(c), r, &Cell);
					const int Row = static_cast<int>(f * Length + r);
					if (Cell.Note >= C && Cell.Note <= B) {
						if (Open)
							List.back().End = Row;
						if (Cell.Instrument < MAX_INSTRUMENTS)
							Instrument = Cell.Instrument;
						RollNote Note;
						Note.Start = Row;
						Note.End = Row + 1;
						Note.Pitch = std::min(PITCHES - 1, Cell.Octave * 12 + Cell.Note - 1);
						Note.Instrument = Instrument;
						Note.Released = false;
						List.push_back(Note);
						Open = true;
					}
					else if ((Cell.Note == RELEASE || Cell.Note == HALT) && Open) {
						List.back().End = Row;
						List.back().Released = (Cell.Note == RELEASE);
						Open = false;
					}
				}
			}
			if (Open)
				List.back().End = TotalRows();
		}
		if (Channel >= Channels)
			Channel = std::max(0, Channels - 1);
	}

	void Clamp()
	{
		ScrollX = std::max(0, std::min(ScrollX, TotalRows() * RowWidth() - Grid.Width()));
		ScrollY = std::max(0, std::min(ScrollY, PITCHES * KeyHeight() - Grid.Height()));
	}

	void UpdateScrollBars()
	{
		Clamp();
		SCROLLINFO Info;
		memset(&Info, 0, sizeof(Info));
		Info.cbSize = sizeof(Info);
		Info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
		Info.nMax = std::max(0, TotalRows() * RowWidth() - 1);
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
		const int Middle = High >= 0 ? (Low + High) / 2 : 48;
		ScrollY = (PITCHES - 1 - Middle) * KeyHeight() - Grid.Height() / 2;
		UpdateScrollBars();
	}

	bool HitNote(CPoint Point, int &OutChannel, int &OutNote) const
	{
		if (!Grid.PtInRect(Point))
			return false;
		const int Row = RowAt(Point.x), Pitch = PitchAt(Point.y);
		for (int Pass = 0; Pass < 2; ++Pass) {	// the bright channel first
			for (size_t c = 0; c < Notes.size(); ++c) {
				if ((static_cast<int>(c) == Channel) != (Pass == 0))
					continue;
				const int Index = NoteAt(Notes[c], Row);
				if (Index >= 0 && Notes[c][static_cast<size_t>(Index)].Pitch == Pitch) {
					OutChannel = static_cast<int>(c);
					OutNote = Index;
					return true;
				}
			}
		}
		return false;
	}

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
		Channel = ToChannel;
		CursorFrame = pView->GetPatternEditor()->GetFrame();	// seen, so it doesn't scroll
		CursorRow = pView->GetPatternEditor()->GetRow();
		CursorChannel = pView->GetPatternEditor()->GetChannel();
	}

	void Preview(int Pitch)
	{
		CMainFrame *pMain = Main();
		if (pMain == NULL || Pitch < 0 || Pitch >= PITCHES || Channel >= static_cast<int>(Notes.size()))
			return;
		StopPreview();
		const int Instrument = pMain->GetSelectedInstrument();
		stChanNote Note;
		memset(&Note, 0, sizeof(Note));
		Note.Note = static_cast<unsigned char>(Pitch % 12 + 1);
		Note.Octave = static_cast<unsigned char>(Pitch / 12);
		Note.Vol = MAX_VOLUME;
		Note.Instrument = static_cast<unsigned char>((Instrument >= 0 && Instrument < MAX_INSTRUMENTS) ? Instrument : MAX_INSTRUMENTS);
		theApp.GetSoundGenerator()->QueueNote(Channel, Note, NOTE_PRIO_2);
		PreviewChannel = Channel;
		PreviewPitch = Pitch;
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
	}

	// Follows the song, the cursor and playback; true if the picture changed
	bool Poll()
	{
		CFamiTrackerView *pView = View();
		CFamiTrackerDoc *pDoc = Doc();
		CMainFrame *pMain = Main();
		if (pView == NULL || pDoc == NULL || pMain == NULL)
			return false;
		bool Changed = false;

		// Another track, or frames, rows or channels changed without a notice
		if (static_cast<unsigned int>(pMain->GetSelectedTrack()) != Track || Track >= pDoc->GetTrackCount() ||
			pDoc->GetFrameCount(Track) != Frames || pDoc->GetPatternLength(Track) != Length ||
			pDoc->GetAvailableChannels() != Notes.size() || pDoc->GetFirstHighlight() != Beat || pDoc->GetSecondHighlight() != Bar) {
			Read();
			UpdateScrollBars();
			Changed = true;
		}

		// The cursor, whose channel is the bright one
		const CPatternEditor *pEditor = pView->GetPatternEditor();
		const int Frame = pEditor->GetFrame(), Row = pEditor->GetRow(), CursorCh = pEditor->GetChannel();
		if (CursorCh != CursorChannel) {
			Channel = std::max(0, std::min(CursorCh, static_cast<int>(Notes.size()) - 1));
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
			if (Played >= 0 && pView->GetFollowMode())
				FollowRow(Played, true);
			Changed = true;
		}
		else if (Moved) {
			if (!Playing)
				FollowRow(Frame * static_cast<int>(Length) + Row, false);
			Changed = true;
		}
		return Changed;
	}

	// The bright channel and a hint, or the note under the mouse
	CString StatusText() const
	{
		CFamiTrackerDoc *pDoc = Doc();
		if (pDoc == NULL || Notes.empty())
			return CString();
		if (HoverChannel < 0 || HoverNote < 0) {
			CString Hint = RecordingChannelName(pDoc, Channel);
			Hint += _T("   |   Click a note to go to it in the pattern editor, or a key to hear it. ")
				_T("Wheel: up and down, Shift+wheel: sideways, Ctrl+wheel: zoom.");
			return Hint;
		}
		const RollNote &Note = Notes[static_cast<size_t>(HoverChannel)][static_cast<size_t>(HoverNote)];
		const int Rows = Note.End - Note.Start;
		CString Text;
		Text.Format(_T("%s: %s, %d row%s"), static_cast<LPCTSTR>(RecordingChannelName(pDoc, HoverChannel)),
			static_cast<LPCTSTR>(PitchName(Note.Pitch)), Rows, Rows == 1 ? _T("") : _T("s"));
		if (Note.Instrument >= 0 && pDoc->IsInstrumentUsed(static_cast<unsigned int>(Note.Instrument))) {
			char Name[256] = "";
			pDoc->GetInstrumentName(static_cast<unsigned int>(Note.Instrument), Name);
			CString Instrument;
			Instrument.Format(_T(", instrument %02X %s"), Note.Instrument, Name);
			Text += Instrument;
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
		return Text;
	}

	void Draw(CDC &dc)
	{
		if (Dirty)
			Read();
		Clamp();
		const CSettings *pSettings = theApp.GetSettings();
		const COLORREF Back = pSettings->Appearance.iColBackground;
		const COLORREF Text = pSettings->Appearance.iColPatternText;
		const COLORREF Cursor = pSettings->Appearance.iColCursor;
		const COLORREF LaneWhite = Mix(Back, Text, 0.07), LaneBlack = Back;
		const COLORREF RowLine = Mix(Back, Text, 0.10), BeatLine = Mix(Back, Text, 0.22), BarLine = Mix(Back, Text, 0.38);
		const COLORREF FrameLine = Mix(Back, Text, 0.62), OctaveLine = Mix(Back, Text, 0.20);
		const COLORREF Panel = Mix(Back, Text, 0.13), Past = Mix(Back, RGB(0, 0, 0), 0.5);
		const COLORREF Play = Mix(CPatternEditor::ROW_PLAY_COLOR, RGB(255, 255, 255), 0.55);
		const int RowW = RowWidth(), KeyH = KeyHeight();

		dc.FillSolidRect(&Client, Back);
		CFont *pOldFont = static_cast<CFont*>(dc.SelectStockObject(DEFAULT_GUI_FONT));
		dc.SetBkMode(TRANSPARENT);

		const int FirstPitch = std::max(0, PitchAt(Grid.bottom - 1)), LastPitch = std::min(PITCHES - 1, PitchAt(Grid.top));
		const int FirstRow = std::max(0, RowAt(Grid.left)), LastRow = std::min(TotalRows() - 1, RowAt(Grid.right));

		// The grid: a lane per semitone, lines for rows, beats, bars and frames
		dc.IntersectClipRect(&Grid);
		for (int p = FirstPitch; p <= LastPitch; ++p) {
			const int Y = PitchTop(p);
			dc.FillSolidRect(Grid.left, Y, Grid.Width(), KeyH, IsBlackKey(p) ? LaneBlack : LaneWhite);
			if (p % 12 == 0)
				dc.FillSolidRect(Grid.left, Y + KeyH - 1, Grid.Width(), 1, OctaveLine);
		}
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
		const int EndX = RowX(TotalRows());
		if (EndX < Grid.right)
			dc.FillSolidRect(EndX, Grid.top, Grid.right - EndX, Grid.Height(), Past);

		// Notes: the other channels faint, then the bright one
		for (int Pass = 0; Pass < 2; ++Pass) {
			for (size_t c = 0; c < Notes.size(); ++c) {
				const bool Bright = static_cast<int>(c) == Channel;
				if (Bright != (Pass == 1))
					continue;
				const COLORREF Full = ChannelColor(ChanIDs[c], static_cast<int>(c));
				const COLORREF Fill = Bright ? Full : Mix(Back, Full, 0.38);
				const COLORREF Edge = Bright ? Mix(Full, RGB(0, 0, 0), 0.45) : Mix(Back, Full, 0.55);
				const std::vector<RollNote> &List = Notes[c];
				const int Sounding = (Playing && PlayRow >= 0) ? NoteAt(List, PlayRow) : -1;
				for (size_t i = FirstEndingAfter(List, FirstRow); i < List.size() && List[i].Start <= LastRow; ++i) {
					const RollNote &Note = List[i];
					if (Note.End <= FirstRow || Note.Pitch < FirstPitch || Note.Pitch > LastPitch)
						continue;
					CRect Rect(RowX(Note.Start), PitchTop(Note.Pitch) + 1, RowX(Note.End), PitchTop(Note.Pitch) + KeyH);
					if (Rect.Width() < 2)
						Rect.right = Rect.left + 2;
					dc.FillSolidRect(&Rect, static_cast<int>(i) == Sounding ? Mix(Fill, RGB(255, 255, 255), 0.45) : Fill);
					Outline(dc, Rect, Edge);
					if (static_cast<int>(c) == HoverChannel && static_cast<int>(i) == HoverNote)
						Outline(dc, Rect, RGB(255, 255, 255));
				}
			}
		}

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
		if (CursorFrame >= 0 && CursorRow >= 0)
			dc.FillSolidRect(RowX(CursorFrame * static_cast<int>(Length) + CursorRow), Ruler.top, 1, Ruler.Height(), Cursor);
		if (Playing && PlayRow >= 0)
			dc.FillSolidRect(RowX(PlayRow), Ruler.top, std::max(2, SX(2)), Ruler.Height(), Play);
		dc.SelectClipRgn(NULL);

		// The keyboard
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
			if (p % 12 == 0 && KeyH >= SY(8)) {
				dc.SetTextColor(RGB(80, 80, 80));
				CRect Label(Keys.left, Y, Keys.right - SX(3), Y + KeyH);
				dc.DrawText(PitchName(p), &Label, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
			}
		}
		dc.FillSolidRect(Keys.right - 1, Keys.top, 1, Keys.Height(), RGB(110, 110, 110));
		dc.SelectClipRgn(NULL);

		// The corner above the keyboard, and the status line
		dc.FillSolidRect(Client.left, Client.top, Keys.Width(), Ruler.Height(), Panel);
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
	ON_WM_LBUTTONUP()
	ON_WM_MOUSEMOVE()
	ON_WM_CAPTURECHANGED()
	ON_WM_KEYDOWN()
	ON_WM_TIMER()
	ON_WM_CLOSE()
	ON_WM_DESTROY()
	ON_WM_GETMINMAXINFO()
END_MESSAGE_MAP()

CPianoRollWnd::CPianoRollWnd() : m_pImpl(new Impl(this))
{
}

CPianoRollWnd::~CPianoRollWnd()
{
	delete m_pImpl;
}

bool CPianoRollWnd::Create(CMainFrame *pMainFrame)
{
	// Where it was last time; at first along the bottom of the main window
	CRect Main;
	pMainFrame->GetWindowRect(&Main);
	const CRect Default(Main.left + SX(40), Main.top + Main.Height() * 45 / 100, Main.right - SX(40), Main.bottom - SY(30));
	CRect Rect(theApp.GetProfileInt(SECTION, _T("Left"), Default.left), theApp.GetProfileInt(SECTION, _T("Top"), Default.top),
		theApp.GetProfileInt(SECTION, _T("Right"), Default.right), theApp.GetProfileInt(SECTION, _T("Bottom"), Default.bottom));
	if (Rect.Width() < SX(300) || Rect.Height() < SY(200) || ::MonitorFromRect(&Rect, MONITOR_DEFAULTTONULL) == NULL)
		Rect = Default;
	const int Zoom = static_cast<int>(theApp.GetProfileInt(SECTION, _T("Zoom"), DEFAULT_ZOOM));
	m_pImpl->Zoom = (Zoom >= 0 && Zoom < ZOOMS) ? Zoom : DEFAULT_ZOOM;

	const CString Class = AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(NULL, IDC_ARROW), NULL, theApp.LoadIcon(IDR_MAINFRAME));
	return CreateEx(0, Class, _T("Piano roll"), WS_OVERLAPPEDWINDOW | WS_HSCROLL | WS_VSCROLL, Rect, pMainFrame, 0) != FALSE;
}

void CPianoRollWnd::Show()
{
	Impl &d = *m_pImpl;
	if (IsIconic())
		ShowWindow(SW_RESTORE);
	ShowWindow(SW_SHOW);
	d.Layout();
	d.Read();
	d.CursorChannel = -1;	// the bright channel is the cursor's
	d.Poll();
	if (!d.Placed) {
		d.CenterOnNotes();
		d.Placed = true;
	}
	if (d.CursorFrame >= 0)
		d.FollowRow(d.CursorFrame * static_cast<int>(d.Length) + d.CursorRow, false);
	d.UpdateScrollBars();
	SetTimer(TIMER_ID, TIMER_MS, NULL);
	Invalidate(FALSE);
}

bool CPianoRollWnd::IsShown() const
{
	return GetSafeHwnd() != NULL && IsWindowVisible() != FALSE;
}

void CPianoRollWnd::SongChanged()
{
	m_pImpl->Dirty = true;
	if (IsShown())
		Invalidate(FALSE);
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
	m_pImpl->Layout();
	m_pImpl->UpdateScrollBars();
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
	case SB_RIGHT: d.ScrollX = d.TotalRows() * d.RowWidth(); break;
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
	Impl &d = *m_pImpl;
	SetFocus();
	d.Layout();
	if (d.Keys.PtInRect(point)) {
		// Play the key, and the ones the mouse slides over
		d.Preview(d.PitchAt(point.y));
		SetCapture();
	}
	else if (d.Grid.PtInRect(point)) {
		int Channel, Note;
		if (d.HitNote(point, Channel, Note))
			d.GoTo(Channel, d.Notes[static_cast<size_t>(Channel)][static_cast<size_t>(Note)].Start);
		else
			d.GoTo(d.Channel, d.RowAt(point.x));
	}
	else if (d.Ruler.PtInRect(point))
		d.GoTo(d.Channel, d.RowAt(point.x));
	Invalidate(FALSE);
}

void CPianoRollWnd::OnLButtonUp(UINT nFlags, CPoint point)
{
	if (GetCapture() == this)
		ReleaseCapture();
	m_pImpl->StopPreview();
	Invalidate(FALSE);
}

void CPianoRollWnd::OnCaptureChanged(CWnd *pWnd)
{
	m_pImpl->StopPreview();
	CWnd::OnCaptureChanged(pWnd);
}

void CPianoRollWnd::OnMouseMove(UINT nFlags, CPoint point)
{
	Impl &d = *m_pImpl;
	d.Layout();
	if (d.PreviewChannel >= 0) {
		const int Pitch = d.PitchAt(point.y);
		if (Pitch != d.PreviewPitch && Pitch >= 0 && Pitch < PITCHES) {
			d.Preview(Pitch);
			Invalidate(FALSE);
		}
		return;
	}
	int Channel = -1, Note = -1;
	d.HitNote(point, Channel, Note);
	if (Channel != d.HoverChannel || Note != d.HoverNote) {
		d.HoverChannel = Channel;
		d.HoverNote = Note;
		Invalidate(FALSE);
	}
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
	CWnd::OnKeyDown(nChar, nRepCnt, nFlags);
}

void CPianoRollWnd::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == TIMER_ID) {
		m_pImpl->Layout();
		if (m_pImpl->Poll() || m_pImpl->Dirty)
			Invalidate(FALSE);
		return;
	}
	CWnd::OnTimer(nIDEvent);
}

void CPianoRollWnd::OnClose()
{
	// Hidden, not destroyed: View > Piano roll brings it back as it was
	m_pImpl->StopPreview();
	KillTimer(TIMER_ID);
	ShowWindow(SW_HIDE);
}

void CPianoRollWnd::OnDestroy()
{
	KillTimer(TIMER_ID);
	WINDOWPLACEMENT Placement;
	memset(&Placement, 0, sizeof(Placement));
	Placement.length = sizeof(Placement);
	if (GetWindowPlacement(&Placement)) {
		const RECT &Rect = Placement.rcNormalPosition;
		theApp.WriteProfileInt(SECTION, _T("Left"), Rect.left);
		theApp.WriteProfileInt(SECTION, _T("Top"), Rect.top);
		theApp.WriteProfileInt(SECTION, _T("Right"), Rect.right);
		theApp.WriteProfileInt(SECTION, _T("Bottom"), Rect.bottom);
	}
	theApp.WriteProfileInt(SECTION, _T("Zoom"), m_pImpl->Zoom);
	CWnd::OnDestroy();
}

void CPianoRollWnd::OnGetMinMaxInfo(MINMAXINFO *lpMMI)
{
	lpMMI->ptMinTrackSize.x = SX(320);
	lpMMI->ptMinTrackSize.y = SY(200);
	CWnd::OnGetMinMaxInfo(lpMMI);
}

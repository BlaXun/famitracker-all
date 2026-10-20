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

// The clips panel (see ClipsPanel.h)

// Standard headers come before stdafx.h, whose DEBUG_NEW macro would
// otherwise rewrite the placement new inside the standard library.
#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>
#include "stdafx.h"
#include "FamiTracker.h"
#include "FamiTrackerDoc.h"
#include "FamiTrackerView.h"
#include "MainFrm.h"
#include "PatternEditor.h"
#include "SoundGen.h"
#include "TrackerChannel.h"
#include "Clips.h"
#include "ClipsDlg.h"
#include "InsertRecording.h"
#include "ClipsPanel.h"

namespace {

LPCTSTR SECTION = _T("Clips panel");	// registry section: open, width

// Sizes at 96 DPI (scaled with SX and SY)
const int DEFAULT_WIDTH = 230;
const int MIN_WIDTH = 150;
const int MIN_VIEW_WIDTH = 300;			// what the pattern editor keeps at least
const int DIVIDER = 5;
const int MARGIN = 6;
const int HEADER_HEIGHT = 26;
const int CARD_GAP = 6;
const int NAME_HEIGHT = 18;
const int PICTURE_HEIGHT = 28;
const int INFO_HEIGHT = 15;
const int CARD_HEIGHT = 4 + NAME_HEIGHT + PICTURE_HEIGHT + 2 + INFO_HEIGHT + 4;
const int PLAY_SIZE = 16;

const UINT_PTR TMR_POLL = 1;			// looks at the clips again
const UINT POLL_MS = 400;
const UINT_PTR TMR_PLAY = 2;			// plays a clip
const UINT PLAY_MS = 10;
const int DRAG_DISTANCE = 5;

// Colours
const COLORREF PANEL_BACK = RGB(232, 234, 238);
const COLORREF CARD_BACK = RGB(255, 255, 255);
const COLORREF CARD_HOVER = RGB(238, 244, 252);
const COLORREF CARD_BORDER = RGB(196, 200, 208);
const COLORREF CARD_SELECTED = RGB(60, 120, 215);
const COLORREF PICTURE_BACK = RGB(26, 28, 36);
const COLORREF PICTURE_LINE = RGB(48, 52, 64);
const COLORREF TEXT_DIM = RGB(100, 104, 112);
const COLORREF LINK_TEXT = RGB(40, 100, 200);
const COLORREF COLUMN_COLOURS[] = { RGB(120, 220, 120), RGB(110, 190, 255), RGB(255, 180, 90), RGB(240, 120, 200),
	RGB(230, 230, 110), RGB(150, 150, 255), RGB(110, 230, 220), RGB(255, 130, 120) };

// Menu items
enum {
	MENU_PLACE = 1, MENU_PLACE_LINKED, MENU_LISTEN, MENU_EDIT, MENU_RENAME, MENU_DUPLICATE, MENU_UNLINK, MENU_DELETE, MENU_SAVE
};

struct thumb_note_t {
	int Column;
	int Row;
	int Rows;
	int Pitch;			// note number, or the noise's 0-15
	bool Drum;
};

struct card_t {
	int Id;
	CString Name;
	CString Info;						// "Pulse 1 - 2 bars - 2 linked copies"
	int Rows;
	int Columns;
	int Links;
	std::vector<thumb_note_t> Notes;
	int Low, High;						// pitch range of the notes that aren't drums
};

bool IsPlayable(const stChanNote &Cell)
{
	return Cell.Note >= C && Cell.Note <= B;
}

bool IsEmptyCell(const stChanNote &Cell)
{
	if (Cell.Note != NONE || Cell.Vol != MAX_VOLUME || Cell.Instrument != MAX_INSTRUMENTS)
		return false;
	for (int i = 0; i < MAX_EFFECT_COLUMNS; ++i)
		if (Cell.EffNumber[i] != EF_NONE)
			return false;
	return true;
}

CString Plural(int Number, LPCTSTR One, LPCTSTR Many)
{
	CString Text;
	Text.Format(_T("%d %s"), Number, Number == 1 ? One : Many);
	return Text;
}

}	// namespace

struct CClipsPanelWnd::Impl {
	CClipsPanelWnd *pWnd;
	std::vector<card_t> Cards;
	std::vector<CRect> CardRects;		// in client coordinates, scrolled
	CRect SaveRect;						// "+ Save selection"
	std::string Signature;				// what the clips were at the last look
	bool Stale;
	int Scroll;							// pixels scrolled down
	int Content;						// height of everything
	int Selected;						// clip ID, -1 for none
	int Hover;							// card index, -1 for none
	bool HoverPlay;

	// Pressing and dragging
	int Pressed;						// card index, -1
	bool PressedPlay;
	CPoint PressPoint;
	bool Dragging;
	bool Dropping;						// over the pattern editor, the cursor follows
	int StartFrame, StartRow, StartChannel;	// the cursor before the drag

	// Playing
	int PlayId;							// clip ID, -1 while nothing plays
	DWORD PlayStart;
	double RowsPerSecond;
	int PlayedRows;
	std::vector<int> PlayChannels;		// a channel per column, -1 for none

	CFont Font, BoldFont, SmallFont;

	Impl(CClipsPanelWnd *p) : pWnd(p), Stale(true), Scroll(0), Content(0), Selected(-1), Hover(-1), HoverPlay(false),
		Pressed(-1), PressedPlay(false), Dragging(false), Dropping(false), StartFrame(0), StartRow(0), StartChannel(0),
		PlayId(-1), PlayStart(0), RowsPerSecond(10.0), PlayedRows(0)
	{
	}

	CMainFrame *Main() const
	{
		return static_cast<CMainFrame*>(AfxGetMainWnd());
	}

	CFamiTrackerDoc *Doc() const
	{
		CMainFrame *pMain = Main();
		return pMain != NULL ? static_cast<CFamiTrackerDoc*>(pMain->GetActiveDocument()) : NULL;
	}

	CFamiTrackerView *View() const
	{
		return CFamiTrackerView::GetView();
	}

	void Status(LPCTSTR Text) const
	{
		CMainFrame *pMain = Main();
		if (pMain != NULL)
			pMain->SetMessageText(Text);
	}

	void MakeFonts()
	{
		LOGFONT Log;
		memset(&Log, 0, sizeof(Log));
		::GetObject(::GetStockObject(DEFAULT_GUI_FONT), sizeof(Log), &Log);
		Log.lfHeight = -SY(12);
		_tcscpy_s(Log.lfFaceName, LF_FACESIZE, _T("Segoe UI"));
		Font.CreateFontIndirect(&Log);
		Log.lfWeight = FW_BOLD;
		BoldFont.CreateFontIndirect(&Log);
		Log.lfWeight = FW_NORMAL;
		Log.lfHeight = -SY(11);
		SmallFont.CreateFontIndirect(&Log);
	}

	// The song's channel that a clip's column came from, -1 if the song has none of that kind
	int ChannelOf(const CFamiTrackerDoc *pDoc, int ChanID) const
	{
		if (ChanID < 0)
			return -1;
		const int Channels = static_cast<int>(pDoc->GetAvailableChannels());
		for (int c = 0; c < Channels; ++c)
			if (pDoc->GetChannelType(c) == ChanID)
				return c;
		return -1;
	}

	// What the clips are now, cheaply: names, sizes, links and the cells
	std::string MakeSignature(const CFamiTrackerDoc *pDoc) const
	{
		const CClipLibrary *pClips = pDoc->GetClips();
		std::string Out;
		unsigned int Hash = 2166136261u;
		for (int i = 0; i < pClips->GetCount(); ++i) {
			CString Line;
			Line.Format(_T("%d|%s|%d|%d|%d|%d;"), pClips->GetId(i), static_cast<LPCTSTR>(pClips->GetName(i)), pClips->GetColumns(i),
				pClips->GetRows(i), pClips->GetLinkCount(i), pDoc->GetSecondHighlight());
			Out += static_cast<LPCSTR>(CStringA(Line));
			for (int c = 0; c < pClips->GetColumns(i); ++c) {
				Hash = (Hash ^ static_cast<unsigned int>(pClips->GetSource(i, c) + 7)) * 16777619u;
				for (int r = 0; r < pClips->GetRows(i); ++r) {
					stChanNote Cell;
					pClips->GetCell(i, c, r, Cell);
					const unsigned int Parts[] = { Cell.Note, Cell.Octave, Cell.Instrument, Cell.Vol, Cell.EffNumber[0], Cell.EffParam[0] };
					for (size_t k = 0; k < sizeof(Parts) / sizeof(Parts[0]); ++k)
						Hash = (Hash ^ Parts[k]) * 16777619u;
				}
			}
		}
		CString End;
		End.Format(_T("#%u"), Hash);
		Out += static_cast<LPCSTR>(CStringA(End));
		return Out;
	}

	// Reads the clips again if they changed; true if they did
	bool Refresh()
	{
		CFamiTrackerDoc *pDoc = Doc();
		if (pDoc == NULL)
			return false;
		const std::string Now = MakeSignature(pDoc);
		if (!Stale && Now == Signature)
			return false;
		Stale = false;
		Signature = Now;

		const CClipLibrary *pClips = pDoc->GetClips();
		Cards.clear();
		const int Bar = pDoc->GetSecondHighlight();
		for (int i = 0; i < pClips->GetCount(); ++i) {
			card_t Card;
			Card.Id = pClips->GetId(i);
			Card.Name = pClips->GetName(i);
			Card.Rows = pClips->GetRows(i);
			Card.Columns = pClips->GetColumns(i);
			Card.Links = pClips->GetLinkCount(i);
			Card.Low = 1000;
			Card.High = -1000;

			// Where it came from: the channels' names
			CString Where;
			for (int c = 0; c < Card.Columns && c < 3; ++c) {
				const int Channel = ChannelOf(pDoc, pClips->GetSource(i, c));
				CString Name = Channel >= 0 ? CString(pDoc->GetChannel(Channel)->GetChannelName()) : CString(_T("?"));
				Where += (c > 0 ? _T(", ") : _T("")) + Name;
			}
			if (Card.Columns > 3)
				Where += _T(", ...");
			CString Length;
			if (Bar > 0 && Card.Rows % Bar == 0)
				Length = Plural(Card.Rows / Bar, _T("bar"), _T("bars"));
			else if (Bar > 0 && Card.Rows > Bar)
				Length.Format(_T("%.1f bars"), static_cast<double>(Card.Rows) / Bar);
			else
				Length = Plural(Card.Rows, _T("row"), _T("rows"));
			Card.Info = Where + _T(" - ") + Length;
			if (Card.Links > 0)
				Card.Info += _T(" - ") + Plural(Card.Links, _T("linked copy"), _T("linked copies"));

			// Its notes, as long as they sound
			for (int c = 0; c < Card.Columns; ++c) {
				const bool Drum = pClips->GetSource(i, c) == CHANID_NOISE;
				int Start = -1, Pitch = 0;
				for (int r = 0; r <= Card.Rows; ++r) {
					stChanNote Cell;
					memset(&Cell, 0, sizeof(Cell));
					bool Ends = r == Card.Rows;
					if (!Ends) {
						pClips->GetCell(i, c, r, Cell);
						Ends = Cell.Note != NONE;
					}
					if (Ends && Start >= 0) {
						thumb_note_t Note = { c, Start, r - Start, Pitch, Drum };
						Card.Notes.push_back(Note);
						if (!Drum) {
							Card.Low = std::min(Card.Low, Pitch);
							Card.High = std::max(Card.High, Pitch);
						}
						Start = -1;
					}
					if (r < Card.Rows && IsPlayable(Cell)) {
						Start = r;
						Pitch = Drum ? ((Cell.Octave * 12 + Cell.Note - 1) & 0x0F) : Cell.Octave * 12 + Cell.Note - 1;
					}
				}
			}
			Cards.push_back(Card);
		}
		if (Find(Selected) < 0)
			Selected = -1;
		return true;
	}

	int Find(int Id) const
	{
		for (size_t i = 0; i < Cards.size(); ++i)
			if (Cards[i].Id == Id)
				return static_cast<int>(i);
		return -1;
	}

	// The clip library's index of a card
	int LibraryIndex(int Card) const
	{
		CFamiTrackerDoc *pDoc = Doc();
		if (pDoc == NULL || Card < 0 || Card >= static_cast<int>(Cards.size()))
			return -1;
		return pDoc->GetClips()->FindIndex(Cards[static_cast<size_t>(Card)].Id);
	}

	void Layout()
	{
		CRect Client;
		pWnd->GetClientRect(&Client);
		CardRects.clear();
		int Y = SY(HEADER_HEIGHT) + SY(MARGIN) - Scroll;
		for (size_t i = 0; i < Cards.size(); ++i) {
			CardRects.push_back(CRect(SX(MARGIN), Y, std::max(SX(MARGIN) + SX(60), Client.right - SX(MARGIN)), Y + SY(CARD_HEIGHT)));
			Y += SY(CARD_HEIGHT) + SY(CARD_GAP);
		}
		Content = Y + Scroll + (Cards.empty() ? SY(120) : SY(70));
		SaveRect = CRect(Client.right - SX(MARGIN) - SX(118), SY(4) - Scroll, Client.right - SX(MARGIN), SY(HEADER_HEIGHT) - SY(2) - Scroll);

		SCROLLINFO Info;
		memset(&Info, 0, sizeof(Info));
		Info.cbSize = sizeof(Info);
		Info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
		Info.nMin = 0;
		Info.nMax = std::max(0, Content - 1);
		Info.nPage = static_cast<UINT>(std::max(1, static_cast<int>(Client.Height())));
		Info.nPos = Scroll;
		pWnd->SetScrollInfo(SB_VERT, &Info, TRUE);
	}

	void ScrollTo(int Position)
	{
		CRect Client;
		pWnd->GetClientRect(&Client);
		const int Max = std::max(0, Content - Client.Height());
		const int New = std::max(0, std::min(Max, Position));
		if (New == Scroll)
			return;
		Scroll = New;
		Layout();
		pWnd->Invalidate(FALSE);
	}

	CRect PlayRect(int Card) const
	{
		const CRect &R = CardRects[static_cast<size_t>(Card)];
		return CRect(R.right - SX(4) - SX(PLAY_SIZE), R.top + SY(3), R.right - SX(4), R.top + SY(3) + SY(PLAY_SIZE));
	}

	int CardAt(const CPoint &Point, bool *pOnPlay) const
	{
		for (size_t i = 0; i < CardRects.size(); ++i) {
			if (CardRects[i].PtInRect(Point)) {
				if (pOnPlay != NULL)
					*pOnPlay = PlayRect(static_cast<int>(i)).PtInRect(Point) != FALSE;
				return static_cast<int>(i);
			}
		}
		if (pOnPlay != NULL)
			*pOnPlay = false;
		return -1;
	}

	// Drawing

	void DrawPicture(CDC &dc, const CRect &Box, const card_t &Card) const
	{
		dc.FillSolidRect(&Box, PICTURE_BACK);
		if (Card.Rows <= 0)
			return;
		CFamiTrackerDoc *pDoc = Doc();
		const int Beat = pDoc != NULL ? std::max(1, static_cast<int>(pDoc->GetFirstHighlight())) : 4;
		const int Bar = pDoc != NULL ? std::max(1, static_cast<int>(pDoc->GetSecondHighlight())) : 16;
		const double Step = static_cast<double>(Box.Width()) / Card.Rows;
		// Bars, and beats when there's room
		for (int r = 0; r < Card.Rows; r += (Step * Beat >= 4.0 ? Beat : Bar)) {
			const int X = Box.left + static_cast<int>(r * Step);
			dc.FillSolidRect(X, Box.top, 1, Box.Height(), r % Bar == 0 ? RGB(70, 76, 92) : PICTURE_LINE);
		}
		const int Span = std::max(12, Card.High - Card.Low + 1);
		const int Low = Card.High >= Card.Low ? (Card.Low + Card.High) / 2 - Span / 2 : 0;
		const int Inner = Box.Height() - 4;
		for (size_t n = 0; n < Card.Notes.size(); ++n) {
			const thumb_note_t &Note = Card.Notes[n];
			const COLORREF Colour = COLUMN_COLOURS[static_cast<size_t>(Note.Column) % (sizeof(COLUMN_COLOURS) / sizeof(COLUMN_COLOURS[0]))];
			const int X1 = Box.left + static_cast<int>(Note.Row * Step);
			if (Note.Drum) {
				// A drum hit: a dot, high for a hi-hat, low for a kick
				const int Y = Box.bottom - 3 - (Note.Pitch * Inner) / 16;
				dc.FillSolidRect(X1, Y - 1, std::max(2, static_cast<int>(Step)), 3, Colour);
				continue;
			}
			const int X2 = std::max(X1 + 2, Box.left + static_cast<int>((Note.Row + Note.Rows) * Step) - 1);
			const int Y = Box.bottom - 3 - ((Note.Pitch - Low) * Inner) / Span;
			dc.FillSolidRect(X1, std::max(static_cast<int>(Box.top) + 1, Y - 1), X2 - X1, 3, Colour);
		}
	}

	void DrawPlayButton(CDC &dc, const CRect &R, bool Playing, bool Hot) const
	{
		CBrush Brush(Playing ? CARD_SELECTED : (Hot ? RGB(90, 150, 230) : RGB(150, 160, 175)));
		CPen Pen(PS_SOLID, 1, Playing ? CARD_SELECTED : (Hot ? RGB(90, 150, 230) : RGB(150, 160, 175)));
		CBrush *pOldBrush = dc.SelectObject(&Brush);
		CPen *pOldPen = dc.SelectObject(&Pen);
		dc.Ellipse(&R);
		CBrush White(RGB(255, 255, 255));
		CPen WhitePen(PS_SOLID, 1, RGB(255, 255, 255));
		dc.SelectObject(&White);
		dc.SelectObject(&WhitePen);
		if (Playing) {
			// Stop: a square
			const int S = R.Width() / 4;
			dc.Rectangle(R.CenterPoint().x - S, R.CenterPoint().y - S, R.CenterPoint().x + S, R.CenterPoint().y + S);
		}
		else {
			POINT Points[3];
			const int S = R.Width() / 4;
			Points[0].x = R.CenterPoint().x - S + 1;
			Points[0].y = R.CenterPoint().y - S - 1;
			Points[1].x = R.CenterPoint().x - S + 1;
			Points[1].y = R.CenterPoint().y + S + 1;
			Points[2].x = R.CenterPoint().x + S + 1;
			Points[2].y = R.CenterPoint().y;
			dc.Polygon(Points, 3);
		}
		dc.SelectObject(pOldBrush);
		dc.SelectObject(pOldPen);
	}

	void Paint(CDC &dc, const CRect &Client)
	{
		dc.FillSolidRect(&Client, PANEL_BACK);
		dc.SetBkMode(TRANSPARENT);
		CFont *pOldFont = dc.SelectObject(&BoldFont);

		// The heading, and saving the selection as a clip
		CString Title;
		Title.Format(Cards.empty() ? _T("Clips") : _T("Clips (%d)"), static_cast<int>(Cards.size()));
		dc.SetTextColor(RGB(30, 32, 40));
		CRect TitleRect(SX(MARGIN), SY(4) - Scroll, Client.right, SY(HEADER_HEIGHT) - Scroll);
		dc.DrawText(Title, &TitleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		CFamiTrackerView *pView = View();
		const bool CanSave = pView != NULL && pView->GetPatternEditor()->IsSelecting();
		dc.SelectObject(&Font);
		dc.SetTextColor(CanSave ? LINK_TEXT : RGB(150, 154, 162));
		dc.DrawText(_T("+ Save selection"), &SaveRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

		for (size_t i = 0; i < Cards.size(); ++i) {
			const CRect &R = CardRects[i];
			if (R.bottom < Client.top || R.top > Client.bottom)
				continue;
			const card_t &Card = Cards[i];
			const bool IsSelected = Card.Id == Selected, IsHover = static_cast<int>(i) == Hover;
			CBrush Back(IsHover ? CARD_HOVER : CARD_BACK);
			CPen Border(PS_SOLID, IsSelected ? 2 : 1, IsSelected ? CARD_SELECTED : CARD_BORDER);
			CBrush *pOldBrush = dc.SelectObject(&Back);
			CPen *pOldPen = dc.SelectObject(&Border);
			dc.RoundRect(&R, CPoint(SX(8), SY(8)));
			dc.SelectObject(pOldBrush);
			dc.SelectObject(pOldPen);

			// The name, and the play button
			dc.SelectObject(&BoldFont);
			dc.SetTextColor(RGB(30, 32, 40));
			CRect NameRect(R.left + SX(8), R.top + SY(3), R.right - SX(8) - SX(PLAY_SIZE), R.top + SY(3) + SY(NAME_HEIGHT));
			dc.DrawText(Card.Name, &NameRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
			DrawPlayButton(dc, PlayRect(static_cast<int>(i)), Card.Id == PlayId, IsHover && HoverPlay);

			// Its notes
			CRect Picture(R.left + SX(6), R.top + SY(4) + SY(NAME_HEIGHT), R.right - SX(6),
				R.top + SY(4) + SY(NAME_HEIGHT) + SY(PICTURE_HEIGHT));
			DrawPicture(dc, Picture, Card);

			// What it holds
			dc.SelectObject(&SmallFont);
			dc.SetTextColor(TEXT_DIM);
			CRect InfoRect(R.left + SX(8), Picture.bottom + SY(2), R.right - SX(8), Picture.bottom + SY(2) + SY(INFO_HEIGHT));
			dc.DrawText(Card.Info, &InfoRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
		}

		// How it works
		dc.SelectObject(&SmallFont);
		dc.SetTextColor(TEXT_DIM);
		const int Top = CardRects.empty() ? SY(HEADER_HEIGHT) + SY(MARGIN) - Scroll : CardRects.back().bottom + SY(CARD_GAP);
		CRect HintRect(SX(MARGIN) + SX(2), Top, Client.right - SX(MARGIN), Top + SY(200));
		LPCTSTR Hint = Cards.empty() ?
			_T("No clips yet. Select rows in the pattern editor and press + Save selection (Ctrl+Shift+C), or tick ")
			_T("\"Save the take as a clip\" when you record. Your clips show up here.") :
			_T("Drag a clip into the song to put it in there (hold Ctrl to link it: linked copies change together), ")
			_T("or double-click it to put it in at the cursor. Right-click for more.");
		dc.DrawText(Hint, &HintRect, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
		dc.SelectObject(pOldFont);
	}

	// Putting clips in

	bool PatternCell(const CPoint &Screen, int &Channel, int &Row) const
	{
		CFamiTrackerView *pView = View();
		CFamiTrackerDoc *pDoc = Doc();
		if (pView == NULL || pDoc == NULL || ::WindowFromPoint(Screen) != pView->GetSafeHwnd())
			return false;
		CPoint Point = Screen;
		pView->ScreenToClient(&Point);
		CPatternEditor *pEditor = pView->GetPatternEditor();
		if (!pEditor->IsOverPattern(Point) || !pEditor->IsInsidePattern(Point) || pEditor->IsInsideRowColumn(Point))
			return false;
		const CCursorPos Position = pEditor->GetCellAtPoint(Point);
		const int Length = static_cast<int>(pDoc->GetPatternLength(static_cast<unsigned int>(Main()->GetSelectedTrack())));
		if (Position.m_iRow < 0 || Position.m_iRow >= Length || Position.m_iChannel < 0 ||
			Position.m_iChannel >= static_cast<int>(pDoc->GetAvailableChannels()))
			return false;
		Channel = Position.m_iChannel;
		Row = Position.m_iRow;
		return true;
	}

	void MoveCursor(int Channel, int Row) const
	{
		CFamiTrackerView *pView = View();
		if (pView == NULL)
			return;
		pView->GetPatternEditor()->MoveToRow(Row);
		pView->SelectChannel(static_cast<unsigned int>(Channel));
	}

	void Place(int Card, bool Linked)
	{
		const int Index = LibraryIndex(Card);
		if (Index < 0)
			return;
		StopPlaying();
		CString Message;
		if (PlaceClip(Main(), Index, Linked, Message))
			Status(Message);
		else
			AfxMessageBox(Message, MB_OK | MB_ICONINFORMATION);
		Stale = true;
		pWnd->Invalidate(FALSE);
	}

	void BeginDrag()
	{
		CFamiTrackerView *pView = View();
		Dragging = true;
		Dropping = false;
		if (pView != NULL) {
			StartFrame = static_cast<int>(pView->GetSelectedFrame());
			StartRow = static_cast<int>(pView->GetSelectedRow());
			StartChannel = static_cast<int>(pView->GetSelectedChannel());
		}
	}

	void Drag(const CPoint &Screen)
	{
		int Channel, Row;
		if (PatternCell(Screen, Channel, Row)) {
			// The cursor shows where it goes
			MoveCursor(Channel, Row);
			Dropping = true;
			CString Text;
			const CFamiTrackerDoc *pDoc = Doc();
			Text.Format(_T("Let go to put \"%s\" in here: %s, row %d. Hold Ctrl to link it."),
				static_cast<LPCTSTR>(Cards[static_cast<size_t>(Pressed)].Name), pDoc->GetChannel(Channel)->GetChannelName(), Row);
			Status(Text);
		}
		else {
			if (Dropping)
				CancelDrop();
			Status(_T("Drag the clip into the pattern editor, where it should start."));
		}
	}

	void CancelDrop()
	{
		CFamiTrackerView *pView = View();
		if (Dropping && pView != NULL) {
			pView->SelectFrame(static_cast<unsigned int>(StartFrame));
			MoveCursor(StartChannel, StartRow);
		}
		Dropping = false;
	}

	void EndDrag(const CPoint &Screen, bool Linked)
	{
		int Channel, Row;
		const int Card = Pressed;
		Dragging = false;
		if (PatternCell(Screen, Channel, Row)) {
			MoveCursor(Channel, Row);
			Dropping = false;
			Place(Card, Linked);
		}
		else {
			CancelDrop();
			Status(_T(""));
		}
	}

	// Playing a clip on its channels, at the song's tempo

	void Play(int Card)
	{
		CFamiTrackerDoc *pDoc = Doc();
		const int Index = LibraryIndex(Card);
		if (pDoc == NULL || Index < 0)
			return;
		const bool Same = PlayId == Cards[static_cast<size_t>(Card)].Id;
		StopPlaying();
		if (Same)
			return;		// the button stops it
		if (theApp.IsPlaying())
			theApp.StopPlayer();
		const CClipLibrary *pClips = pDoc->GetClips();
		const unsigned int Track = static_cast<unsigned int>(Main()->GetSelectedTrack());
		const int Speed = std::max(1, static_cast<int>(pDoc->GetSongSpeed(Track)));
		const int Tempo = static_cast<int>(pDoc->GetSongTempo(Track));
		RowsPerSecond = Tempo > 0 ? Tempo * 24.0 / (Speed * 60.0) : static_cast<double>(pDoc->GetFrameRate()) / Speed;
		CFamiTrackerView *pView = View();
		const int Cursor = pView != NULL ? static_cast<int>(pView->GetSelectedChannel()) : 0;
		PlayChannels.clear();
		for (int c = 0; c < pClips->GetColumns(Index); ++c) {
			int Channel = ChannelOf(pDoc, pClips->GetSource(Index, c));
			if (Channel < 0 && Cursor + c < static_cast<int>(pDoc->GetAvailableChannels()))
				Channel = Cursor + c;
			PlayChannels.push_back(Channel);
		}
		PlayId = Cards[static_cast<size_t>(Card)].Id;
		PlayStart = ::GetTickCount();
		PlayedRows = 0;
		pWnd->SetTimer(TMR_PLAY, PLAY_MS, NULL);
		Status(_T("Playing the clip on its channels. Its play button stops it."));
		Tick();
	}

	void Tick()
	{
		CFamiTrackerDoc *pDoc = Doc();
		const int Index = pDoc != NULL ? pDoc->GetClips()->FindIndex(PlayId) : -1;
		if (Index < 0) {
			StopPlaying();
			return;
		}
		const CClipLibrary *pClips = pDoc->GetClips();
		const int Rows = pClips->GetRows(Index);
		const int Due = static_cast<int>((::GetTickCount() - PlayStart) * RowsPerSecond / 1000.0) + 1;
		for (; PlayedRows < std::min(Due, Rows); ++PlayedRows) {
			for (size_t c = 0; c < PlayChannels.size(); ++c) {
				if (PlayChannels[c] < 0)
					continue;
				stChanNote Cell;
				pClips->GetCell(Index, static_cast<int>(c), PlayedRows, Cell);
				if (!IsEmptyCell(Cell))
					theApp.GetSoundGenerator()->QueueNote(PlayChannels[c], Cell, NOTE_PRIO_2);
			}
		}
		if (Due > Rows + static_cast<int>(RowsPerSecond / 3))	// a moment for the last notes
			StopPlaying();
	}

	void StopPlaying()
	{
		if (PlayId < 0)
			return;
		pWnd->KillTimer(TMR_PLAY);
		for (size_t c = 0; c < PlayChannels.size(); ++c) {
			if (PlayChannels[c] < 0)
				continue;
			stChanNote Note;
			memset(&Note, 0, sizeof(Note));
			Note.Note = HALT;
			Note.Vol = MAX_VOLUME;
			Note.Instrument = MAX_INSTRUMENTS;
			theApp.GetSoundGenerator()->QueueNote(PlayChannels[c], Note, NOTE_PRIO_2);
		}
		PlayId = -1;
		PlayChannels.clear();
		pWnd->Invalidate(FALSE);
	}

	// The right-click menu
	void Menu(int Card, const CPoint &Screen)
	{
		CFamiTrackerDoc *pDoc = Doc();
		CFamiTrackerView *pView = View();
		if (pDoc == NULL)
			return;
		CMenu Popup;
		Popup.CreatePopupMenu();
		const int Index = LibraryIndex(Card);
		if (Index >= 0) {
			Popup.AppendMenu(MF_STRING, MENU_PLACE, _T("Put in at the cursor\tDouble-click"));
			Popup.AppendMenu(MF_STRING, MENU_PLACE_LINKED, _T("Put in at the cursor, linked"));
			Popup.AppendMenu(MF_STRING, MENU_LISTEN, PlayId == Cards[static_cast<size_t>(Card)].Id ? _T("Stop") : _T("Listen"));
			Popup.AppendMenu(MF_SEPARATOR);
			Popup.AppendMenu(MF_STRING, MENU_EDIT, _T("Edit its notes..."));
			Popup.AppendMenu(MF_STRING, MENU_RENAME, _T("Rename..."));
			Popup.AppendMenu(MF_STRING, MENU_DUPLICATE, _T("Duplicate..."));
			Popup.AppendMenu(MF_STRING | (pDoc->GetClips()->GetLinkCount(Index) > 0 ? 0 : MF_GRAYED), MENU_UNLINK,
				_T("Unlink its copies (they stay as plain notes)"));
			Popup.AppendMenu(MF_STRING, MENU_DELETE, _T("Delete..."));
			Popup.AppendMenu(MF_SEPARATOR);
		}
		const bool CanSave = pView != NULL && pView->GetPatternEditor()->IsSelecting();
		Popup.AppendMenu(MF_STRING | (CanSave ? 0 : MF_GRAYED), MENU_SAVE, _T("Save the selection as a clip...\tCtrl+Shift+C"));
		const UINT Choice = Popup.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY, Screen.x, Screen.y, pWnd);
		switch (Choice) {
		case MENU_PLACE:
			Place(Card, false);
			break;
		case MENU_PLACE_LINKED:
			Place(Card, true);
			break;
		case MENU_LISTEN:
			Play(Card);
			break;
		case MENU_EDIT:
			Edit(Card);
			break;
		case MENU_RENAME:
			Rename(Card);
			break;
		case MENU_DUPLICATE:
			Duplicate(Card);
			break;
		case MENU_UNLINK:
			Unlink(Card);
			break;
		case MENU_DELETE:
			Delete(Card);
			break;
		case MENU_SAVE:
			Main()->SendMessage(WM_COMMAND, ID_EDIT_SAVECLIP);
			break;
		}
		Stale = true;
		pWnd->Invalidate(FALSE);
	}

	void Edit(int Card)
	{
		CFamiTrackerDoc *pDoc = Doc();
		const int Index = LibraryIndex(Card);
		if (Index < 0)
			return;
		StopPlaying();
		CClipEditDlg Dialog(pDoc, Index, Main());
		if (Dialog.DoModal() != IDOK || !Dialog.IsChanged())
			return;
		const int Links = pDoc->GetClips()->GetLinkCount(Index);
		ApplyClipEdit(Main(), Index, Dialog.GetCells(), Dialog.GetCellCount());
		CString Text;
		if (Links > 0)
			Text.Format(_T("Changed \"%s\" and its %s in the song. Ctrl+Z undoes it."), static_cast<LPCTSTR>(pDoc->GetClips()->GetName(Index)),
				static_cast<LPCTSTR>(Plural(Links, _T("linked copy"), _T("linked copies"))));
		else
			Text.Format(_T("Changed \"%s\"."), static_cast<LPCTSTR>(pDoc->GetClips()->GetName(Index)));
		Status(Text);
	}

	void Rename(int Card)
	{
		CFamiTrackerDoc *pDoc = Doc();
		const int Index = LibraryIndex(Card);
		if (Index < 0)
			return;
		CClipLibrary *pClips = pDoc->GetClips();
		CClipNameDlg Dialog(_T("Rename clip"), _T("New name:"), pClips->GetName(Index), Main());
		if (Dialog.DoModal() != IDOK)
			return;
		pClips->SetName(Index, Dialog.GetName());
		pDoc->SetModifiedFlag();
	}

	void Duplicate(int Card)
	{
		CFamiTrackerDoc *pDoc = Doc();
		const int Index = LibraryIndex(Card);
		if (Index < 0)
			return;
		CClipLibrary *pClips = pDoc->GetClips();
		CClipNameDlg Dialog(_T("Duplicate clip"), _T("Name of the copy:"), pClips->NewName(pClips->GetName(Index) + _T(" copy"), false), Main());
		if (Dialog.DoModal() != IDOK)
			return;
		const int Copy = pClips->Duplicate(Index, Dialog.GetName());
		if (Copy < 0) {
			AfxMessageBox(_T("There are too many clips already. Delete some first."), MB_OK | MB_ICONINFORMATION);
			return;
		}
		pDoc->SetModifiedFlag();
		Selected = pClips->GetId(Copy);
		Status(_T("The copy is a clip of its own: changing it leaves the original and its linked copies alone."));
	}

	void Unlink(int Card)
	{
		CFamiTrackerDoc *pDoc = Doc();
		const int Index = LibraryIndex(Card);
		if (Index < 0)
			return;
		pDoc->GetClips()->Unlink(Index);
		pDoc->SetModifiedFlag();
		Status(_T("Its linked copies are plain notes now: they don't change along any more."));
	}

	void Delete(int Card)
	{
		CFamiTrackerDoc *pDoc = Doc();
		const int Index = LibraryIndex(Card);
		if (Index < 0)
			return;
		CClipLibrary *pClips = pDoc->GetClips();
		CString Question;
		Question.Format(_T("Delete the clip \"%s\"?"), static_cast<LPCTSTR>(pClips->GetName(Index)));
		if (pClips->GetLinkCount(Index) > 0)
			Question += _T(" Its linked copies stay in the song as plain notes.");
		if (AfxMessageBox(Question, MB_YESNO | MB_ICONQUESTION) != IDYES)
			return;
		if (PlayId == Cards[static_cast<size_t>(Card)].Id)
			StopPlaying();
		pClips->Remove(Index);
		pDoc->SetModifiedFlag();
	}
};

// The panel

BEGIN_MESSAGE_MAP(CClipsPanelWnd, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_SIZE()
	ON_WM_VSCROLL()
	ON_WM_MOUSEWHEEL()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_LBUTTONDBLCLK()
	ON_WM_RBUTTONUP()
	ON_WM_MOUSEMOVE()
	ON_WM_CAPTURECHANGED()
	ON_WM_SETCURSOR()
	ON_WM_TIMER()
	ON_WM_DESTROY()
END_MESSAGE_MAP()

CClipsPanelWnd::CClipsPanelWnd() : m_pImpl(NULL)
{
	m_pImpl = new Impl(this);
}

CClipsPanelWnd::~CClipsPanelWnd()
{
	delete m_pImpl;
}

bool CClipsPanelWnd::Create(CWnd *pParent)
{
	const CString Class = AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(NULL, IDC_ARROW), NULL, NULL);
	if (!CWnd::Create(Class, _T("Clips"), WS_CHILD | WS_VISIBLE | WS_VSCROLL, CRect(0, 0, 0, 0), pParent, 1))
		return false;
	m_pImpl->MakeFonts();
	SetTimer(TMR_POLL, POLL_MS, NULL);
	return true;
}

void CClipsPanelWnd::SongChanged()
{
	m_pImpl->Stale = true;
	if (GetSafeHwnd() != NULL)
		Invalidate(FALSE);
}

void CClipsPanelWnd::OnPaint()
{
	CPaintDC dc(this);
	if (m_pImpl->Refresh())
		m_pImpl->Layout();
	CRect Client;
	GetClientRect(&Client);
	CDC Memory;
	Memory.CreateCompatibleDC(&dc);
	CBitmap Bitmap;
	Bitmap.CreateCompatibleBitmap(&dc, std::max(1, static_cast<int>(Client.Width())), std::max(1, static_cast<int>(Client.Height())));
	CBitmap *pOld = Memory.SelectObject(&Bitmap);
	m_pImpl->Paint(Memory, Client);
	dc.BitBlt(0, 0, Client.Width(), Client.Height(), &Memory, 0, 0, SRCCOPY);
	Memory.SelectObject(pOld);
}

BOOL CClipsPanelWnd::OnEraseBkgnd(CDC *pDC)
{
	return TRUE;
}

void CClipsPanelWnd::OnSize(UINT nType, int cx, int cy)
{
	CWnd::OnSize(nType, cx, cy);
	m_pImpl->Refresh();
	m_pImpl->Layout();
	m_pImpl->ScrollTo(m_pImpl->Scroll);
	Invalidate(FALSE);
}

void CClipsPanelWnd::OnVScroll(UINT nSBCode, UINT nPos, CScrollBar *pScrollBar)
{
	CRect Client;
	GetClientRect(&Client);
	int Position = m_pImpl->Scroll;
	switch (nSBCode) {
	case SB_LINEUP: Position -= SY(30); break;
	case SB_LINEDOWN: Position += SY(30); break;
	case SB_PAGEUP: Position -= Client.Height(); break;
	case SB_PAGEDOWN: Position += Client.Height(); break;
	case SB_THUMBTRACK: case SB_THUMBPOSITION: Position = static_cast<int>(nPos); break;
	case SB_TOP: Position = 0; break;
	case SB_BOTTOM: Position = m_pImpl->Content; break;
	}
	m_pImpl->ScrollTo(Position);
}

BOOL CClipsPanelWnd::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
	m_pImpl->ScrollTo(m_pImpl->Scroll - zDelta * SY(40) / WHEEL_DELTA);
	return TRUE;
}

void CClipsPanelWnd::OnLButtonDown(UINT nFlags, CPoint point)
{
	SetFocus();
	if (m_pImpl->SaveRect.PtInRect(point)) {
		AfxGetMainWnd()->SendMessage(WM_COMMAND, ID_EDIT_SAVECLIP);
		SongChanged();
		return;
	}
	bool OnPlay = false;
	const int Card = m_pImpl->CardAt(point, &OnPlay);
	m_pImpl->Pressed = Card;
	m_pImpl->PressedPlay = OnPlay;
	m_pImpl->PressPoint = point;
	m_pImpl->Dragging = false;
	if (Card >= 0) {
		m_pImpl->Selected = m_pImpl->Cards[static_cast<size_t>(Card)].Id;
		SetCapture();
	}
	Invalidate(FALSE);
}

void CClipsPanelWnd::OnMouseMove(UINT nFlags, CPoint point)
{
	if (m_pImpl->Pressed >= 0 && GetCapture() == this) {
		if (!m_pImpl->Dragging && !m_pImpl->PressedPlay &&
			(std::abs(point.x - m_pImpl->PressPoint.x) > SX(DRAG_DISTANCE) || std::abs(point.y - m_pImpl->PressPoint.y) > SY(DRAG_DISTANCE)))
			m_pImpl->BeginDrag();
		if (m_pImpl->Dragging) {
			CPoint Screen = point;
			ClientToScreen(&Screen);
			m_pImpl->Drag(Screen);
			::SetCursor(::LoadCursor(NULL, m_pImpl->Dropping ? IDC_HAND : IDC_NO));
		}
		return;
	}

	// Hovering: a card lights up, and the status bar says what it does
	bool OnPlay = false;
	const int Card = m_pImpl->CardAt(point, &OnPlay);
	if (Card != m_pImpl->Hover || OnPlay != m_pImpl->HoverPlay) {
		m_pImpl->Hover = Card;
		m_pImpl->HoverPlay = OnPlay;
		Invalidate(FALSE);
		if (Card >= 0) {
			CString Text;
			if (OnPlay)
				Text.Format(_T("Play \"%s\" on its channels"), static_cast<LPCTSTR>(m_pImpl->Cards[static_cast<size_t>(Card)].Name));
			else
				Text.Format(_T("\"%s\": drag it into the song, or double-click to put it in at the cursor. Right-click for more."),
					static_cast<LPCTSTR>(m_pImpl->Cards[static_cast<size_t>(Card)].Name));
			m_pImpl->Status(Text);
		}
	}
}

void CClipsPanelWnd::OnLButtonUp(UINT nFlags, CPoint point)
{
	const int Card = m_pImpl->Pressed;
	const bool WasDragging = m_pImpl->Dragging;
	if (GetCapture() == this) {
		if (WasDragging) {
			CPoint Screen = point;
			ClientToScreen(&Screen);
			m_pImpl->EndDrag(Screen, (nFlags & MK_CONTROL) != 0);
		}
		m_pImpl->Pressed = -1;
		ReleaseCapture();
	}
	if (!WasDragging && Card >= 0 && m_pImpl->PressedPlay) {
		bool OnPlay = false;
		if (m_pImpl->CardAt(point, &OnPlay) == Card && OnPlay)
			m_pImpl->Play(Card);
	}
	m_pImpl->Pressed = -1;
	Invalidate(FALSE);
}

void CClipsPanelWnd::OnLButtonDblClk(UINT nFlags, CPoint point)
{
	bool OnPlay = false;
	const int Card = m_pImpl->CardAt(point, &OnPlay);
	if (Card >= 0 && !OnPlay)
		m_pImpl->Place(Card, (nFlags & MK_CONTROL) != 0);
}

void CClipsPanelWnd::OnRButtonUp(UINT nFlags, CPoint point)
{
	const int Card = m_pImpl->CardAt(point, NULL);
	if (Card >= 0) {
		m_pImpl->Selected = m_pImpl->Cards[static_cast<size_t>(Card)].Id;
		Invalidate(FALSE);
	}
	CPoint Screen = point;
	ClientToScreen(&Screen);
	m_pImpl->Menu(Card, Screen);
}

void CClipsPanelWnd::OnCaptureChanged(CWnd *pWnd)
{
	if (m_pImpl->Dragging) {
		m_pImpl->Dragging = false;
		m_pImpl->CancelDrop();
	}
	m_pImpl->Pressed = -1;
	CWnd::OnCaptureChanged(pWnd);
}

BOOL CClipsPanelWnd::OnSetCursor(CWnd *pWnd, UINT nHitTest, UINT message)
{
	if (nHitTest == HTCLIENT) {
		CPoint Point;
		::GetCursorPos(&Point);
		ScreenToClient(&Point);
		bool OnPlay = false;
		if (m_pImpl->CardAt(Point, &OnPlay) >= 0 || m_pImpl->SaveRect.PtInRect(Point)) {
			::SetCursor(::LoadCursor(NULL, IDC_HAND));
			return TRUE;
		}
	}
	return CWnd::OnSetCursor(pWnd, nHitTest, message);
}

void CClipsPanelWnd::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == TMR_PLAY) {
		m_pImpl->Tick();
		return;
	}
	if (nIDEvent == TMR_POLL) {
		// The mouse left: nothing lights up
		CPoint Point;
		::GetCursorPos(&Point);
		CRect Window;
		GetWindowRect(&Window);
		if (m_pImpl->Hover >= 0 && !Window.PtInRect(Point) && GetCapture() != this) {
			m_pImpl->Hover = -1;
			Invalidate(FALSE);
		}
		// Clips added or changed elsewhere (a recording, Claude Code, an edit)
		if (IsWindowVisible() && m_pImpl->Refresh()) {
			m_pImpl->Layout();
			Invalidate(FALSE);
		}
		return;
	}
	CWnd::OnTimer(nIDEvent);
}

void CClipsPanelWnd::OnDestroy()
{
	KillTimer(TMR_POLL);
	m_pImpl->StopPlaying();
	CWnd::OnDestroy();
}

// The bar it is docked in

BEGIN_MESSAGE_MAP(CClipsBar, CControlBar)
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

CClipsBar::CClipsBar() : m_iWidth(DEFAULT_WIDTH), m_iRoom(0), m_iDragFrom(-1), m_iDragWidth(0), m_iDragMax(0)
{
}

CClipsBar::~CClipsBar()
{
}

bool CClipsBar::Create(CFrameWnd *pFrame)
{
	m_iWidth = std::max(SX(MIN_WIDTH), static_cast<int>(theApp.GetProfileInt(SECTION, _T("Width"), SX(DEFAULT_WIDTH))));
	m_dwStyle = CBRS_ALIGN_RIGHT;
	const CString Class = AfxRegisterWndClass(0, ::LoadCursor(NULL, IDC_ARROW), NULL, NULL);
	if (!CWnd::Create(Class, _T("Clips"), WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, CRect(0, 0, 0, 0), pFrame,
		AFX_IDW_CONTROLBAR_FIRST + 33))
		return false;
	// Laid out last, so it sits right beside the pattern editor
	SetWindowPos(&CWnd::wndBottom, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	return m_wndPanel.Create(this);
}

void CClipsBar::Opened(bool Open)
{
	if (!Open && ::GetFocus() == m_wndPanel.GetSafeHwnd()) {
		CFrameWnd *pFrame = GetParentFrame();
		CView *pView = pFrame != NULL ? pFrame->GetActiveView() : NULL;
		if (pView != NULL)
			pView->SetFocus();
	}
	if (Open)
		m_wndPanel.SongChanged();
	theApp.WriteProfileInt(SECTION, _T("Open"), Open ? 1 : 0);
}

void CClipsBar::SongChanged()
{
	m_wndPanel.SongChanged();
}

void CClipsBar::Focus()
{
	m_wndPanel.SetFocus();
}

bool CClipsBar::WasOpen()
{
	return theApp.GetProfileInt(SECTION, _T("Open"), 1) != 0;
}

int CClipsBar::Divider() const
{
	return std::max(4, SX(DIVIDER));
}

int CClipsBar::MaxWidth() const
{
	return std::max(SX(MIN_WIDTH), m_iRoom - SX(MIN_VIEW_WIDTH));
}

LRESULT CClipsBar::OnSizeParent(WPARAM wParam, LPARAM lParam)
{
	const AFX_SIZEPARENTPARAMS *pLayout = reinterpret_cast<const AFX_SIZEPARENTPARAMS*>(lParam);
	m_iRoom = pLayout->rect.right - pLayout->rect.left;
	return CControlBar::OnSizeParent(wParam, lParam);
}

CSize CClipsBar::CalcFixedLayout(BOOL bStretch, BOOL bHorz)
{
	return CSize(std::max(0, std::min(m_iWidth, MaxWidth())), 32767);
}

void CClipsBar::OnUpdateCmdUI(CFrameWnd *pTarget, BOOL bDisableIfNoHndler)
{
}

BOOL CClipsBar::PreTranslateMessage(MSG *pMsg)
{
	// Not as a dialog (as control bars are), so FamiTracker's shortcuts work
	return CWnd::PreTranslateMessage(pMsg);
}

void CClipsBar::OnSize(UINT nType, int cx, int cy)
{
	CControlBar::OnSize(nType, cx, cy);
	if (m_wndPanel.GetSafeHwnd() != NULL)
		m_wndPanel.MoveWindow(Divider(), 0, std::max(0, cx - Divider()), cy);
}

void CClipsBar::OnPaint()
{
	CPaintDC dc(this);
	CRect Rect;
	GetClientRect(&Rect);
	const int Line = Divider();
	dc.FillSolidRect(Rect.left, Rect.top, Line, Rect.Height(), ::GetSysColor(COLOR_3DFACE));
	dc.FillSolidRect(Rect.left, Rect.top, 1, Rect.Height(), ::GetSysColor(COLOR_3DHILIGHT));
	dc.FillSolidRect(Rect.left + Line - 1, Rect.top, 1, Rect.Height(), ::GetSysColor(COLOR_3DSHADOW));
	// A grip in the middle
	const int Y = Rect.top + Rect.Height() / 2 - SY(12);
	for (int i = 0; i < 4; ++i)
		dc.FillSolidRect(Rect.left + Line / 2 - 1, Y + i * SY(7), 2, SY(3), ::GetSysColor(COLOR_3DSHADOW));
}

BOOL CClipsBar::OnEraseBkgnd(CDC *pDC)
{
	return TRUE;
}

void CClipsBar::OnLButtonDown(UINT nFlags, CPoint point)
{
	if (point.x >= Divider())
		return;
	CPoint Screen = point;
	ClientToScreen(&Screen);
	m_iDragFrom = static_cast<int>(Screen.x);
	m_iDragWidth = m_iWidth;
	m_iDragMax = MaxWidth();
	SetCapture();
}

void CClipsBar::OnMouseMove(UINT nFlags, CPoint point)
{
	if (m_iDragFrom < 0)
		return;
	CPoint Screen = point;
	ClientToScreen(&Screen);
	const int Width = std::max(SX(MIN_WIDTH), std::min(m_iDragMax, m_iDragWidth - (static_cast<int>(Screen.x) - m_iDragFrom)));
	if (Width != m_iWidth) {
		m_iWidth = Width;
		CFrameWnd *pFrame = GetParentFrame();
		if (pFrame != NULL)
			pFrame->RecalcLayout();
	}
}

void CClipsBar::OnLButtonUp(UINT nFlags, CPoint point)
{
	if (m_iDragFrom >= 0) {
		m_iDragFrom = -1;
		ReleaseCapture();
		theApp.WriteProfileInt(SECTION, _T("Width"), m_iWidth);
	}
}

void CClipsBar::OnCaptureChanged(CWnd *pWnd)
{
	m_iDragFrom = -1;
	CControlBar::OnCaptureChanged(pWnd);
}

BOOL CClipsBar::OnSetCursor(CWnd *pWnd, UINT nHitTest, UINT message)
{
	if (pWnd == this && nHitTest == HTCLIENT) {
		::SetCursor(::LoadCursor(NULL, IDC_SIZEWE));
		return TRUE;
	}
	return CControlBar::OnSetCursor(pWnd, nHitTest, message);
}

void CClipsBar::OnDestroy()
{
	theApp.WriteProfileInt(SECTION, _T("Width"), m_iWidth);
	CControlBar::OnDestroy();
}

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

// View > Drum grid (see DrumGridPanel.h)

// Standard and API headers come before stdafx.h, whose DEBUG_NEW macro would
// otherwise rewrite the placement new inside the standard library.
#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>
#include "Api/Presets.h"
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
#include "InsertRecording.h"
#include "InstrumentLibrary.h"
#include "DrumGrid.h"
#include "DrumGridPanel.h"

namespace {

const UINT_PTR TMR_FOLLOW = 1;			// follows the cursor and the song, and shows where the song plays
const UINT FOLLOW_MS = 40;

LPCTSTR SECTION = _T("Drum grid");		// registry section: open, where, its size

// Sizes at 96 DPI (scaled with SX and SY)
const int DEFAULT_HEIGHT = 200;			// along the bottom
const int DEFAULT_WIDTH = 430;			// along the right
const int MIN_HEIGHT = 110;
const int MIN_WIDTH = 240;
const int MIN_VIEW_WIDTH = 300;			// what the pattern editor keeps at least, beside the panel
const int DIVIDER = 5;
const int MARGIN = 6;
const int GAP = 4;						// between the controls of a group
const int GROUP_GAP = 16;				// between the groups
const int BUTTON_HEIGHT = 23;
const int BUTTON_PAD = 16;				// a button's width past its text
const int SMALL_BUTTON = 26;			// the < and > buttons
const int TEXT_PAD = 4;
const int LINE_GAP = 4;
const int LANE_MOST = 30;				// a drum's line is this tall at most
const int STATUS_HEIGHT = 16;

// A colour for each drum, and grey for other notes
const COLORREF LANE_COLOURS[DRUM_LANES + 1] = {
	RGB(225, 85, 70), RGB(240, 170, 50), RGB(70, 165, 230), RGB(140, 110, 225), RGB(150, 150, 155)
};
const COLORREF GRID_BACK = RGB(250, 250, 252);
const COLORREF GRID_BEAT = RGB(236, 238, 243);	// every other beat, shaded
const COLORREF GRID_LINE = RGB(205, 208, 215);
const COLORREF GRID_BAR = RGB(120, 125, 135);
const COLORREF PLAYING = RGB(255, 236, 150);

// The buttons
enum button_t {
	BUTTON_PREV, BUTTON_NEXT, BUTTON_BEAT1, BUTTON_BEAT2, BUTTON_BEAT3, BUTTON_BEAT4, BUTTON_CLEAR, BUTTON_PLAY,
	BUTTON_COPY_NEXT, BUTTON_COPY_ALL, BUTTONS
};
const UINT BUTTON_IDS[BUTTONS] = {
	IDC_DRUM_PREV, IDC_DRUM_NEXT, IDC_DRUM_BEAT1, IDC_DRUM_BEAT2, IDC_DRUM_BEAT3, IDC_DRUM_BEAT4, IDC_DRUM_CLEAR, IDC_DRUM_PLAY,
	IDC_DRUM_COPYNEXT, IDC_DRUM_COPYALL
};

// The controls along the top, in the order they flow (FlowDrumControls): the
// buttons and the texts between them, in three groups that stay together
enum control_t {
	CONTROL_PREV, CONTROL_BAR, CONTROL_NEXT, CONTROL_WHERE,
	CONTROL_BEATS, CONTROL_BEAT1, CONTROL_BEAT2, CONTROL_BEAT3, CONTROL_BEAT4, CONTROL_CLEAR,
	CONTROL_PLAY, CONTROL_COPY_NEXT, CONTROL_COPY_ALL,
	CONTROLS
};
const int CONTROL_GROUPS[CONTROLS] = { 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2 };
const int CONTROL_BUTTONS[CONTROLS] = {		// the button each control is, -1 for a text
	BUTTON_PREV, -1, BUTTON_NEXT, -1, -1, BUTTON_BEAT1, BUTTON_BEAT2, BUTTON_BEAT3, BUTTON_BEAT4, BUTTON_CLEAR,
	BUTTON_PLAY, BUTTON_COPY_NEXT, BUTTON_COPY_ALL
};

LPCTSTR PLAY_TEXT = _T("Play from this bar");
LPCTSTR STOP_TEXT = _T("Stop");
LPCTSTR BEATS_TEXT = _T("Beats:");
LPCTSTR START_TEXT = _T("Click the steps to put drums on them, or drag along a line. Ctrl+Z undoes a change, Ctrl+Y does it again.");

// The right-click menu
const UINT MENU_DOCK = 1;
const UINT MENU_CLOSE = 2;

}	// namespace

class CDrumGridPanelWnd;

// The grid: a line for each drum, a column for each step
class CDrumGridWnd : public CWnd
{
public:
	CDrumGridWnd() : m_pPanel(NULL), m_iHoverLane(-1), m_iHoverStep(-1), m_iDragLane(-1), m_iDragMode(DRUM_NONE), m_iLastStep(-1) {}

	bool Create(CDrumGridPanelWnd *pPanel, CWnd *pParent);
	bool Dragging() const { return m_iDragLane >= 0; }

protected:
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC *pDC);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnCaptureChanged(CWnd *pWnd);

	DECLARE_MESSAGE_MAP()

private:
	int Lines() const;					// the drums, and "Other" when the bar has other notes
	int LabelWidth() const;
	CRect CellRect(int Lane, int Step) const;
	bool HitTest(const CPoint &Point, int &Lane, int &Step) const;
	int StepAt(int X) const;
	void Finish();

	CDrumGridPanelWnd *m_pPanel;
	int m_iHoverLane, m_iHoverStep;
	int m_iDragLane;					// the line being painted along, -1 for none
	int m_iDragMode;					// the drum put on (on empty steps), or DRUM_NONE: taking them off
	int m_iLastStep;
	CFont m_Font;
};

// The panel: the grid, its buttons along the top and a line of news under it
class CDrumGridPanelWnd : public CWnd
{
public:
	CDrumGridPanelWnd();

	bool Create(CWnd *pParent, CMainFrame *pMainFrame);
	void Opened(bool Open);				// follows the cursor while shown
	void SongChanged();					// the bar is read again

	// For the grid
	int Steps() const { return static_cast<int>(m_Cells.size()); }
	int StepsPerBeat() const { return m_iStepsPerBeat; }
	int LaneAt(int Step) const;
	int PlayingStep() const { return m_iPlaying; }
	bool HasOther() const;
	void PaintStep(int Step, int Lane, int Mode, bool Replace);
	void Commit();
	void Hint(LPCTSTR Text);

protected:
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC *pDC);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnDestroy();
	afx_msg void OnContextMenu(CWnd *pWnd, CPoint point);
	afx_msg void OnPrevious();
	afx_msg void OnNext();
	afx_msg void OnBeat1();
	afx_msg void OnBeat2();
	afx_msg void OnBeat3();
	afx_msg void OnBeat4();
	afx_msg void OnClear();
	afx_msg void OnPlay();
	afx_msg void OnCopyNext();
	afx_msg void OnCopyAll();

	DECLARE_MESSAGE_MAP()

private:
	CFamiTrackerDoc *Doc() const;
	bool Ready() const;					// a song to show, with a noise channel
	unsigned int Track() const;
	int Length() const;					// rows a frame
	int TotalRows() const;
	int BarStart() const { return m_Follow.Bar * m_iBarSteps; }
	int CursorRow() const;				// the pattern editor's cursor, counting rows from the song's start
	bool Editing() const;				// a drag along a line, whose change isn't written yet
	bool ReadShape();					// the track's channel, frames and bars; true if they changed
	void Load();						// the bar shown, as the song has it
	void Look();						// follows the song and the cursor
	void FindKit();
	bool EnsureDrum(int Lane);
	void Beat(int Beat);
	void CopyTo(int First, int Last);	// bars
	void Preview(int Lane);
	void Status(LPCTSTR Text);
	void SetText(CString &Text, LPCTSTR New, const CRect &Where);
	void EnableButtons();
	void Layout();
	void ReturnKeys();					// after a button, the keys go back to where notes are typed

	CMainFrame *m_pMainFrame;
	CDrumGridWnd m_Grid;
	CButton m_Buttons[BUTTONS];
	CFont m_Font;
	int m_iWidths[CONTROLS];			// each control's width, for its text
	CRect m_rcControls[CONTROLS];		// and where it is: the texts are painted there
	CRect m_rcStatus;
	CString m_strBar, m_strWhere, m_strStatus;
	bool m_bReload;						// the song changed: the bar is read again when it isn't being edited
	bool m_bOwnChange;					// adding a drum's instrument, which tells of a change
	unsigned int m_iTrack;				// what the bar was read from
	int m_iLength, m_iFrames;
	int m_iNoise;						// the noise channel
	int m_iBarSteps;					// rows a bar: the second row highlight
	int m_iStepsPerBeat;				// the first row highlight
	int m_iBars;
	int m_iPlaying;						// the step the song plays, -1
	bool m_bDirty;						// the grid has changes the song hasn't
	drum_follow_t m_Follow;				// the bar shown and the cursor's bar followed (DrumGrid.h)
	drum_kit_t m_Kit;
	std::vector<stChanNote> m_Cells;	// the bar's cells in the noise channel
};

// The grid

BEGIN_MESSAGE_MAP(CDrumGridWnd, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_MOUSEMOVE()
	ON_WM_CAPTURECHANGED()
END_MESSAGE_MAP()

bool CDrumGridWnd::Create(CDrumGridPanelWnd *pPanel, CWnd *pParent)
{
	m_pPanel = pPanel;
	const CString Class = AfxRegisterWndClass(0, ::LoadCursor(NULL, IDC_HAND), NULL, NULL);
	if (!CWnd::CreateEx(WS_EX_CLIENTEDGE, Class, _T("Drums"), WS_CHILD | WS_VISIBLE, CRect(0, 0, 0, 0), pParent, IDC_DRUM_GRID))
		return false;
	LOGFONT Log;
	memset(&Log, 0, sizeof(Log));
	::GetObject(::GetStockObject(DEFAULT_GUI_FONT), sizeof(Log), &Log);
	m_Font.CreateFontIndirect(&Log);
	return true;
}

int CDrumGridWnd::Lines() const
{
	return m_pPanel->HasOther() ? DRUM_LANES + 1 : DRUM_LANES;
}

int CDrumGridWnd::LabelWidth() const
{
	return SX(84);
}

CRect CDrumGridWnd::CellRect(int Lane, int Step) const
{
	CRect Client;
	GetClientRect(&Client);
	const int Steps = std::max(1, m_pPanel->Steps());
	const int Width = Client.Width() - LabelWidth();
	const int Height = Client.Height() / std::max(1, Lines());
	const int Left = LabelWidth() + Width * Step / Steps;
	const int Right = LabelWidth() + Width * (Step + 1) / Steps;
	return CRect(Left, Lane * Height, Right, (Lane + 1) * Height);
}

int CDrumGridWnd::StepAt(int X) const
{
	CRect Client;
	GetClientRect(&Client);
	const int Steps = std::max(1, m_pPanel->Steps());
	const int Width = std::max(1, Client.Width() - LabelWidth());
	return std::max(0, std::min(Steps - 1, (X - LabelWidth()) * Steps / Width));
}

bool CDrumGridWnd::HitTest(const CPoint &Point, int &Lane, int &Step) const
{
	CRect Client;
	GetClientRect(&Client);
	if (Point.x < LabelWidth() || !Client.PtInRect(Point) || m_pPanel->Steps() == 0)
		return false;
	const int Height = Client.Height() / std::max(1, Lines());
	Lane = std::min(Lines() - 1, static_cast<int>(Point.y) / std::max(1, Height));
	Step = StepAt(Point.x);
	return true;
}

void CDrumGridWnd::OnPaint()
{
	CPaintDC dc(this);
	CRect Client;
	GetClientRect(&Client);
	CDC Memory;
	Memory.CreateCompatibleDC(&dc);
	CBitmap Bitmap;
	Bitmap.CreateCompatibleBitmap(&dc, std::max(1, static_cast<int>(Client.Width())), std::max(1, static_cast<int>(Client.Height())));
	CBitmap *pOldBitmap = Memory.SelectObject(&Bitmap);
	CFont *pOldFont = Memory.SelectObject(&m_Font);
	Memory.FillSolidRect(&Client, GRID_BACK);
	Memory.SetBkMode(TRANSPARENT);

	const int Steps = m_pPanel->Steps();
	const int Beat = std::max(1, m_pPanel->StepsPerBeat());
	const int Playing = m_pPanel->PlayingStep();
	for (int Lane = 0; Lane < Lines(); ++Lane) {
		// The drum's name, with its colour
		const CRect Row = CellRect(Lane, 0);
		CRect Label(SX(6), Row.top, LabelWidth() - SX(4), Row.bottom);
		Memory.FillSolidRect(SX(6), Row.CenterPoint().y - SY(4), SX(8), SY(8), LANE_COLOURS[Lane]);
		Label.left += SX(14);
		Memory.SetTextColor(RGB(40, 42, 50));
		Memory.DrawText(CString(DrumName(Lane)), &Label, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		for (int s = 0; s < Steps; ++s) {
			CRect Cell = CellRect(Lane, s);
			COLORREF Back = (s / Beat) % 2 == 1 ? GRID_BEAT : GRID_BACK;
			if (s == Playing)
				Back = PLAYING;
			Memory.FillSolidRect(&Cell, Back);
			Memory.FillSolidRect(Cell.left, Cell.bottom - 1, Cell.Width(), 1, GRID_LINE);
			Memory.FillSolidRect(Cell.left, Cell.top, 1, Cell.Height(), s % Beat == 0 ? GRID_BAR : GRID_LINE);
			if (m_pPanel->LaneAt(s) == Lane) {
				CRect Hit = Cell;
				Hit.DeflateRect(std::max(2, Cell.Width() / 6), std::max(2, Cell.Height() / 5));
				CBrush Brush(LANE_COLOURS[Lane]);
				CPen Pen(PS_SOLID, 1, LANE_COLOURS[Lane]);
				CBrush *pOldBrush = Memory.SelectObject(&Brush);
				CPen *pOldPen = Memory.SelectObject(&Pen);
				Memory.RoundRect(&Hit, CPoint(SX(5), SY(5)));
				Memory.SelectObject(pOldBrush);
				Memory.SelectObject(pOldPen);
			}
			if (Lane == m_iHoverLane && s == m_iHoverStep) {
				CBrush Frame(RGB(60, 120, 215));
				Memory.FrameRect(&Cell, &Frame);
			}
		}
	}
	// The beats' numbers along the bottom line
	Memory.SetTextColor(RGB(120, 124, 132));
	for (int s = 0; s < Steps; s += Beat) {
		const CRect Cell = CellRect(Lines() - 1, s);
		CRect Number(Cell.left + SX(2), Cell.bottom - SY(13), Cell.left + SX(30), Cell.bottom - 1);
		CString Text;
		Text.Format(_T("%d"), s / Beat + 1);
		Memory.DrawText(Text, &Number, DT_LEFT | DT_BOTTOM | DT_SINGLELINE);
	}

	dc.BitBlt(0, 0, Client.Width(), Client.Height(), &Memory, 0, 0, SRCCOPY);
	Memory.SelectObject(pOldFont);
	Memory.SelectObject(pOldBitmap);
}

BOOL CDrumGridWnd::OnEraseBkgnd(CDC *pDC)
{
	return TRUE;
}

void CDrumGridWnd::OnLButtonDown(UINT nFlags, CPoint point)
{
	int Lane, Step;
	if (!HitTest(point, Lane, Step))
		return;
	// A drum goes on, or off if it's there: and so along the line while dragging
	m_iDragLane = Lane;
	m_iDragMode = m_pPanel->LaneAt(Step) == Lane ? DRUM_NONE : Lane;
	if (Lane == DRUM_OTHER)
		m_iDragMode = DRUM_NONE;	// other notes can only be taken off
	m_iLastStep = Step;
	m_pPanel->PaintStep(Step, Lane, m_iDragMode, true);
	SetCapture();
}

void CDrumGridWnd::OnMouseMove(UINT nFlags, CPoint point)
{
	if (m_iDragLane >= 0 && GetCapture() == this) {
		const int Step = StepAt(point.x);
		if (Step != m_iLastStep) {
			const int From = std::min(Step, m_iLastStep), To = std::max(Step, m_iLastStep);
			for (int s = From; s <= To; ++s)
				m_pPanel->PaintStep(s, m_iDragLane, m_iDragMode, false);	// other drums stay
			m_iLastStep = Step;
		}
		return;
	}
	int Lane = -1, Step = -1;
	if (!HitTest(point, Lane, Step))
		Lane = Step = -1;
	if (Lane != m_iHoverLane || Step != m_iHoverStep) {
		m_iHoverLane = Lane;
		m_iHoverStep = Step;
		Invalidate(FALSE);
		if (Lane >= 0) {
			CString Text;
			if (Lane == DRUM_OTHER)
				Text.Format(_T("Step %d: another sound on the noise channel. Click to take it off."), Step + 1);
			else if (m_pPanel->LaneAt(Step) == Lane)
				Text.Format(_T("Step %d: click to take the %s off."), Step + 1, static_cast<LPCTSTR>(CString(DrumName(Lane)).MakeLower()));
			else
				Text.Format(_T("Step %d: click to put a %s here, or drag along the line to fill the empty steps."), Step + 1,
					static_cast<LPCTSTR>(CString(DrumName(Lane)).MakeLower()));
			m_pPanel->Hint(Text);
		}
	}
}

void CDrumGridWnd::Finish()
{
	if (m_iDragLane < 0)
		return;
	m_iDragLane = -1;
	m_pPanel->Commit();
}

void CDrumGridWnd::OnLButtonUp(UINT nFlags, CPoint point)
{
	if (GetCapture() == this)
		ReleaseCapture();	// which finishes it
	Finish();
}

void CDrumGridWnd::OnCaptureChanged(CWnd *pWnd)
{
	Finish();
	CWnd::OnCaptureChanged(pWnd);
}

// The panel

BEGIN_MESSAGE_MAP(CDrumGridPanelWnd, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_SIZE()
	ON_WM_TIMER()
	ON_WM_DESTROY()
	ON_WM_CONTEXTMENU()
	ON_BN_CLICKED(IDC_DRUM_PREV, OnPrevious)
	ON_BN_CLICKED(IDC_DRUM_NEXT, OnNext)
	ON_BN_CLICKED(IDC_DRUM_BEAT1, OnBeat1)
	ON_BN_CLICKED(IDC_DRUM_BEAT2, OnBeat2)
	ON_BN_CLICKED(IDC_DRUM_BEAT3, OnBeat3)
	ON_BN_CLICKED(IDC_DRUM_BEAT4, OnBeat4)
	ON_BN_CLICKED(IDC_DRUM_CLEAR, OnClear)
	ON_BN_CLICKED(IDC_DRUM_PLAY, OnPlay)
	ON_BN_CLICKED(IDC_DRUM_COPYNEXT, OnCopyNext)
	ON_BN_CLICKED(IDC_DRUM_COPYALL, OnCopyAll)
END_MESSAGE_MAP()

CDrumGridPanelWnd::CDrumGridPanelWnd() :
	m_pMainFrame(NULL),
	m_bReload(true),
	m_bOwnChange(false),
	m_iTrack(0),
	m_iLength(0),
	m_iFrames(0),
	m_iNoise(-1),
	m_iBarSteps(16),
	m_iStepsPerBeat(4),
	m_iBars(1),
	m_iPlaying(-1),
	m_bDirty(false)
{
	for (int c = 0; c < CONTROLS; ++c)
		m_iWidths[c] = 0;
}

bool CDrumGridPanelWnd::Create(CWnd *pParent, CMainFrame *pMainFrame)
{
	m_pMainFrame = pMainFrame;
	LOGFONT Log;
	memset(&Log, 0, sizeof(Log));
	::GetObject(::GetStockObject(DEFAULT_GUI_FONT), sizeof(Log), &Log);
	m_Font.CreateFontIndirect(&Log);
	const CString Class = AfxRegisterWndClass(0, ::LoadCursor(NULL, IDC_ARROW), NULL, NULL);
	if (!CWnd::Create(Class, _T("Drum grid"), WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, CRect(0, 0, 0, 0), pParent, 1))
		return false;
	if (!m_Grid.Create(this, this))
		return false;

	// The buttons, and each control as wide as its text needs
	CClientDC dc(this);
	CFont *pOldFont = dc.SelectObject(&m_Font);
	for (int c = 0; c < CONTROLS; ++c) {
		CString Text;
		switch (c) {
		case CONTROL_PREV: Text = _T("<"); break;
		case CONTROL_NEXT: Text = _T(">"); break;
		case CONTROL_BAR: Text = _T("Bar 888 of 888"); break;
		case CONTROL_WHERE: Text = _T("frame 88, rows 88-88"); break;
		case CONTROL_BEATS: Text = BEATS_TEXT; break;
		case CONTROL_CLEAR: Text = _T("Clear the bar"); break;
		case CONTROL_PLAY: Text = PLAY_TEXT; break;
		case CONTROL_COPY_NEXT: Text = _T("Copy to the next bar"); break;
		case CONTROL_COPY_ALL: Text = _T("Copy to every bar"); break;
		default: Text = CString(BeatName(c - CONTROL_BEAT1)); break;
		}
		const int Button = CONTROL_BUTTONS[c];
		const int Width = static_cast<int>(dc.GetTextExtent(Text).cx);
		m_iWidths[c] = Button >= 0 ? std::max(SX(SMALL_BUTTON), Width + SX(BUTTON_PAD)) : Width + SX(TEXT_PAD);
		if (Button < 0)
			continue;
		if (!m_Buttons[Button].Create(Text, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(0, 0, 0, 0), this, BUTTON_IDS[Button]))
			return false;
		m_Buttons[Button].SetFont(&m_Font);
	}
	m_iWidths[CONTROL_PLAY] = std::max(m_iWidths[CONTROL_PLAY], static_cast<int>(dc.GetTextExtent(CString(STOP_TEXT)).cx) + SX(BUTTON_PAD));
	dc.SelectObject(pOldFont);
	m_strStatus = START_TEXT;
	EnableButtons();
	return true;
}

CFamiTrackerDoc *CDrumGridPanelWnd::Doc() const
{
	return static_cast<CFamiTrackerDoc*>(m_pMainFrame->GetActiveDocument());
}

bool CDrumGridPanelWnd::Ready() const
{
	const CFamiTrackerDoc *pDoc = Doc();
	return pDoc != NULL && pDoc->IsFileLoaded();
}

unsigned int CDrumGridPanelWnd::Track() const
{
	return static_cast<unsigned int>(std::max(0, m_pMainFrame->GetSelectedTrack()));
}

int CDrumGridPanelWnd::Length() const
{
	return std::max(1, static_cast<int>(Doc()->GetPatternLength(Track())));
}

int CDrumGridPanelWnd::TotalRows() const
{
	return Length() * static_cast<int>(Doc()->GetFrameCount(Track()));
}

int CDrumGridPanelWnd::CursorRow() const
{
	CFamiTrackerView *pView = CFamiTrackerView::GetView();
	if (pView == NULL)
		return BarStart();
	return static_cast<int>(pView->GetSelectedFrame()) * Length() + static_cast<int>(pView->GetSelectedRow());
}

bool CDrumGridPanelWnd::Editing() const
{
	return m_bDirty || m_Grid.Dragging();
}

bool CDrumGridPanelWnd::ReadShape()
{
	CFamiTrackerDoc *pDoc = Doc();
	int Noise = -1;
	for (int c = 0; c < static_cast<int>(pDoc->GetAvailableChannels()) && Noise < 0; ++c)
		if (pDoc->GetChannelType(c) == CHANID_NOISE)
			Noise = c;
	// Bars as the row highlight has them
	const int BarSteps = pDoc->GetSecondHighlight() > 0 ? static_cast<int>(pDoc->GetSecondHighlight()) : 16;
	const int PerBeat = std::min(BarSteps, pDoc->GetFirstHighlight() > 0 ? static_cast<int>(pDoc->GetFirstHighlight()) : 4);
	const unsigned int TrackNow = Track();
	const int LengthNow = Length(), Frames = static_cast<int>(pDoc->GetFrameCount(TrackNow));
	const bool Changed = Noise != m_iNoise || BarSteps != m_iBarSteps || PerBeat != m_iStepsPerBeat || TrackNow != m_iTrack ||
		LengthNow != m_iLength || Frames != m_iFrames;
	m_iNoise = Noise;
	m_iBarSteps = BarSteps;
	m_iStepsPerBeat = PerBeat;
	m_iTrack = TrackNow;
	m_iLength = LengthNow;
	m_iFrames = Frames;
	m_iBars = std::max(1, (TotalRows() + m_iBarSteps - 1) / m_iBarSteps);
	return Changed;
}

void CDrumGridPanelWnd::FindKit()
{
	for (int Lane = 0; Lane < DRUM_LANES; ++Lane)
		m_Kit.Instruments[Lane] = FindNamedInstrument(Doc(), INST_2A03, CString(DrumName(Lane)));
}

void CDrumGridPanelWnd::Load()
{
	m_bReload = false;
	m_bDirty = false;
	if (Ready())
		ReadShape();
	if (!Ready() || m_iNoise < 0) {
		m_Cells.clear();
		SetText(m_strBar, _T(""), m_rcControls[CONTROL_BAR]);
		SetText(m_strWhere, _T(""), m_rcControls[CONTROL_WHERE]);
		EnableButtons();
		m_Grid.Invalidate(FALSE);
		return;
	}
	CFamiTrackerDoc *pDoc = Doc();
	const int Total = TotalRows();
	m_Follow.Bar = std::max(0, std::min(m_iBars - 1, m_Follow.Bar));
	FindKit();
	const int Start = BarStart();
	const int Steps = std::max(0, std::min(m_iBarSteps, Total - Start));
	m_Cells.assign(static_cast<size_t>(Steps), stChanNote());
	for (int s = 0; s < Steps; ++s) {
		const int Row = Start + s;
		pDoc->GetNoteData(Track(), static_cast<unsigned int>(Row / Length()), static_cast<unsigned int>(m_iNoise),
			static_cast<unsigned int>(Row % Length()), &m_Cells[static_cast<size_t>(s)]);
	}

	CString Text;
	Text.Format(_T("Bar %d of %d"), m_Follow.Bar + 1, m_iBars);
	SetText(m_strBar, Text, m_rcControls[CONTROL_BAR]);
	Text.Format(_T("frame %02X, rows %02X-%02X"), Start / Length(), Start % Length(), (Start + std::max(1, Steps) - 1) % Length());
	SetText(m_strWhere, Text, m_rcControls[CONTROL_WHERE]);
	EnableButtons();
	m_Grid.Invalidate(FALSE);
}

void CDrumGridPanelWnd::EnableButtons()
{
	const bool Song = !m_Cells.empty();
	const bool More = Song && m_Follow.Bar + 1 < m_iBars;
	const bool Enable[BUTTONS] = { Song && m_Follow.Bar > 0, More, Song, Song, Song, Song, Song, Song, More, Song && m_iBars > 1 };
	for (int b = 0; b < BUTTONS; ++b)
		if (m_Buttons[b].GetSafeHwnd() != NULL && (m_Buttons[b].IsWindowEnabled() != FALSE) != Enable[b])
			m_Buttons[b].EnableWindow(Enable[b] ? TRUE : FALSE);
}

void CDrumGridPanelWnd::Opened(bool Open)
{
	if (Open) {
		m_Follow.CursorBar = -1;	// to the cursor's bar at once
		m_bReload = true;
		Look();
		SetTimer(TMR_FOLLOW, FOLLOW_MS, NULL);
		return;
	}
	KillTimer(TMR_FOLLOW);
	if (m_Grid.Dragging() && ::GetCapture() == m_Grid.GetSafeHwnd())
		ReleaseCapture();		// the drag is written
	ReturnKeys();
}

void CDrumGridPanelWnd::SongChanged()
{
	if (m_bOwnChange)
		return;			// a drum's instrument added: the grid has it
	m_bReload = true;
	if (Editing()) {
		// The song changed under a drag (Ctrl+Z, Claude Code...): the drag ends unwritten, and the grid shows the
		// song as it is now
		m_bDirty = false;
		if (::GetCapture() == m_Grid.GetSafeHwnd())
			ReleaseCapture();
	}
}

// Follows the song and the pattern editor's cursor, and shows where the song plays
void CDrumGridPanelWnd::Look()
{
	if (!Ready()) {
		if (!m_Cells.empty() || !m_strBar.IsEmpty())
			Load();
		return;
	}
	if (!Editing()) {
		const bool Shape = ReadShape();
		CFamiTrackerView *pView = CFamiTrackerView::GetView();
		bool Moved = false;
		if (pView != NULL)
			Moved = FollowCursor(m_Follow, CursorRow() / std::max(1, m_iBarSteps), m_iBars);
		if (Shape || Moved || m_bReload)
			Load();
	}
	// The step the song plays, when it's in this bar
	int Playing = -1;
	if (theApp.IsPlaying()) {
		CSoundGen *pSound = theApp.GetSoundGenerator();
		if (pSound != NULL && pSound->GetPlayerTrack() == static_cast<int>(Track())) {
			const int Row = pSound->GetPlayerFrame() * Length() + pSound->GetPlayerRow();
			if (Row >= BarStart() && Row < BarStart() + Steps())
				Playing = Row - BarStart();
		}
	}
	const CString Wanted = theApp.IsPlaying() ? STOP_TEXT : PLAY_TEXT;
	CString Shown;
	m_Buttons[BUTTON_PLAY].GetWindowText(Shown);
	if (Shown != Wanted)
		m_Buttons[BUTTON_PLAY].SetWindowText(Wanted);
	if (Playing != m_iPlaying) {
		m_iPlaying = Playing;
		m_Grid.Invalidate(FALSE);
	}
}

int CDrumGridPanelWnd::LaneAt(int Step) const
{
	if (Step < 0 || Step >= Steps())
		return DRUM_NONE;
	return DrumOf(m_Cells[static_cast<size_t>(Step)], m_Kit);
}

bool CDrumGridPanelWnd::HasOther() const
{
	for (int s = 0; s < Steps(); ++s)
		if (LaneAt(s) == DRUM_OTHER)
			return true;
	return false;
}

// The drum's instrument, made from its sound preset the first time
bool CDrumGridPanelWnd::EnsureDrum(int Lane)
{
	if (Lane < 0 || Lane >= DRUM_LANES)
		return false;
	if (m_Kit.Instruments[Lane] >= 0)
		return true;
	const ftapi::Preset *pPreset = ftapi::FindPreset(DrumPreset(Lane));
	if (pPreset == NULL)
		return false;
	CString Error;
	m_bOwnChange = true;
	const int Index = AddPresetInstrument(Doc(), *pPreset, false, false, Error);
	m_bOwnChange = false;
	if (Index < 0) {
		Status(Error);
		return false;
	}
	m_Kit.Instruments[Lane] = Index;
	CString Text;
	Text.Format(_T("Added the instrument \"%s\" (%02X) for the drums."), static_cast<LPCTSTR>(CString(DrumName(Lane))), Index);
	Status(Text);
	return true;
}

void CDrumGridPanelWnd::Preview(int Lane)
{
	if (Lane < 0 || Lane >= DRUM_LANES || m_Kit.Instruments[Lane] < 0 || theApp.IsPlaying())
		return;
	stChanNote Note;
	memset(&Note, 0, sizeof(Note));
	Note.Note = static_cast<unsigned char>(DrumPitch(Lane) % 12 + 1);
	Note.Octave = static_cast<unsigned char>(DrumPitch(Lane) / 12);
	Note.Vol = MAX_VOLUME;
	Note.Instrument = static_cast<unsigned char>(m_Kit.Instruments[Lane]);
	theApp.GetSoundGenerator()->QueueNote(m_iNoise, Note, NOTE_PRIO_2);
}

// Mode: the drum put on, or DRUM_NONE to take Lane's off. Without Replace,
// steps with another drum keep it (dragging along a line fills the gaps).
void CDrumGridPanelWnd::PaintStep(int Step, int Lane, int Mode, bool Replace)
{
	if (Step < 0 || Step >= Steps())
		return;
	stChanNote &Cell = m_Cells[static_cast<size_t>(Step)];
	const int Now = DrumOf(Cell, m_Kit);
	if (Mode == DRUM_NONE) {
		if (Now != Lane)
			return;		// only that line's drums come off
		SetDrum(Cell, DRUM_NONE, m_Kit);
	}
	else {
		if (Now == Mode || (!Replace && Now != DRUM_NONE) || !EnsureDrum(Mode))
			return;
		SetDrum(Cell, Mode, m_Kit);
		Preview(Mode);
	}
	m_bDirty = true;
	m_Grid.Invalidate(FALSE);
}

void CDrumGridPanelWnd::Commit()
{
	if (!m_bDirty || m_Cells.empty() || !Ready())
		return;
	m_bDirty = false;
	CString Message;
	if (!WriteChannelRows(m_pMainFrame, m_iNoise, BarStart(), &m_Cells[0], Steps(), CursorRow(), Message))
		Status(Message);
	Load();		// as the song has it now
}

void CDrumGridPanelWnd::Beat(int Which)
{
	if (m_Cells.empty() || Editing() || !Ready())
		return;
	for (int Lane = 0; Lane < DRUM_LANES; ++Lane)
		if (BeatUses(Which, Lane) && !EnsureDrum(Lane))
			return;
	ApplyBeat(m_Cells, Which, m_iStepsPerBeat, m_Kit);
	m_bDirty = true;
	Commit();
	CString Text;
	if (Which == BEAT_CLEAR)
		Text = _T("No drums in this bar now. Ctrl+Z brings them back.");
	else
		Text.Format(_T("The \"%s\" beat is in this bar. Copy it to the next bar or to every bar, or change steps by clicking."),
			static_cast<LPCTSTR>(CString(BeatName(Which))));
	Status(Text);
}

void CDrumGridPanelWnd::OnBeat1() { Beat(BEAT_BASIC); ReturnKeys(); }
void CDrumGridPanelWnd::OnBeat2() { Beat(BEAT_FOUR_ON_FLOOR); ReturnKeys(); }
void CDrumGridPanelWnd::OnBeat3() { Beat(BEAT_HALF_TIME); ReturnKeys(); }
void CDrumGridPanelWnd::OnBeat4() { Beat(BEAT_BUSY); ReturnKeys(); }
void CDrumGridPanelWnd::OnClear() { Beat(BEAT_CLEAR); ReturnKeys(); }

// This bar's drums into bars First to Last, as one undoable change; their effects stay
void CDrumGridPanelWnd::CopyTo(int First, int Last)
{
	if (m_Cells.empty() || Editing() || !Ready())
		return;
	CFamiTrackerDoc *pDoc = Doc();
	const int Total = TotalRows();
	const int From = First * m_iBarSteps;
	const int To = std::min(Total, (Last + 1) * m_iBarSteps);
	if (To <= From)
		return;
	std::vector<stChanNote> Cells(static_cast<size_t>(To - From));
	for (int Row = From; Row < To; ++Row) {
		stChanNote &Cell = Cells[static_cast<size_t>(Row - From)];
		pDoc->GetNoteData(Track(), static_cast<unsigned int>(Row / Length()), static_cast<unsigned int>(m_iNoise),
			static_cast<unsigned int>(Row % Length()), &Cell);
		if (Row / m_iBarSteps == m_Follow.Bar)
			continue;	// the bar itself, when it's among them
		const stChanNote &Source = m_Cells[static_cast<size_t>((Row - From) % m_iBarSteps) % m_Cells.size()];
		Cell.Note = Source.Note;
		Cell.Octave = Source.Octave;
		Cell.Instrument = Source.Instrument;
		Cell.Vol = Source.Vol;
	}
	CString Message;
	if (!WriteChannelRows(m_pMainFrame, m_iNoise, From, &Cells[0], To - From, CursorRow(), Message)) {
		Status(Message);
		return;
	}
	Load();
	if (First == Last)
		Message.Format(_T("Copied this bar's drums to bar %d. Ctrl+Z undoes it."), First + 1);
	else
		Message = _T("Copied this bar's drums to every bar of the song. Ctrl+Z undoes it.");
	Status(Message);
}

void CDrumGridPanelWnd::OnCopyNext()
{
	if (m_Follow.Bar + 1 < m_iBars)
		CopyTo(m_Follow.Bar + 1, m_Follow.Bar + 1);
	ReturnKeys();
}

void CDrumGridPanelWnd::OnCopyAll()
{
	if (m_iBars > 1)
		CopyTo(0, m_iBars - 1);
	ReturnKeys();
}

void CDrumGridPanelWnd::OnPrevious()
{
	if (!Editing() && m_Follow.Bar > 0) {
		--m_Follow.Bar;
		Load();
	}
	ReturnKeys();
}

void CDrumGridPanelWnd::OnNext()
{
	if (!Editing() && m_Follow.Bar + 1 < m_iBars) {
		++m_Follow.Bar;
		Load();
	}
	ReturnKeys();
}

void CDrumGridPanelWnd::OnPlay()
{
	if (theApp.IsPlaying())
		theApp.StopPlayer();
	else if (!m_Cells.empty() && Ready()) {
		// From the bar's first row: the cursor goes there, in the channel it's in
		CFamiTrackerView *pView = CFamiTrackerView::GetView();
		if (pView != NULL) {
			pView->SelectFrame(static_cast<unsigned int>(BarStart() / Length()));
			pView->GetPatternEditor()->MoveToRow(BarStart() % Length());
			theApp.StartPlayer(MODE_PLAY_CURSOR);
		}
	}
	ReturnKeys();
}

void CDrumGridPanelWnd::ReturnKeys()
{
	// A button clicked keeps the keyboard: it goes back to where notes are typed
	const HWND hFocus = ::GetFocus();
	if (hFocus == NULL || (hFocus != m_hWnd && !::IsChild(m_hWnd, hFocus)))
		return;
	CView *pView = m_pMainFrame->GetActiveView();
	if (pView != NULL)
		pView->SetFocus();		// or the piano roll, while the pattern editor is hidden
}

void CDrumGridPanelWnd::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == TMR_FOLLOW) {
		Look();
		return;
	}
	CWnd::OnTimer(nIDEvent);
}

void CDrumGridPanelWnd::OnDestroy()
{
	KillTimer(TMR_FOLLOW);
	CWnd::OnDestroy();
}

void CDrumGridPanelWnd::OnContextMenu(CWnd *pWnd, CPoint point)
{
	CDrumGridBar *pBar = static_cast<CDrumGridBar*>(GetParent());
	CMenu Popup;
	if (pBar == NULL || !Popup.CreatePopupMenu())
		return;
	if (point.x == -1 && point.y == -1) {
		// From the keyboard
		CRect Rect;
		GetWindowRect(&Rect);
		point = Rect.TopLeft();
	}
	Popup.AppendMenu(MF_STRING, MENU_DOCK, pBar->IsOnRight() ? _T("Dock at the bottom") : _T("Dock on the right"));
	Popup.AppendMenu(MF_STRING, MENU_CLOSE, _T("Close"));
	const UINT Chosen = Popup.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY, point.x, point.y, this);
	if (Chosen == MENU_DOCK)
		pBar->DockOnRight(!pBar->IsOnRight());
	else if (Chosen == MENU_CLOSE)
		m_pMainFrame->PostMessage(WM_COMMAND, ID_EDIT_DRUMGRID);
}

void CDrumGridPanelWnd::Layout()
{
	if (m_Grid.GetSafeHwnd() == NULL)
		return;
	CRect Client;
	GetClientRect(&Client);
	const int Inner = std::max(1, static_cast<int>(Client.Width()) - 2 * SX(MARGIN));
	const std::vector<int> Widths(m_iWidths, m_iWidths + CONTROLS);
	const std::vector<int> Groups(CONTROL_GROUPS, CONTROL_GROUPS + CONTROLS);
	std::vector<drum_place_t> Places;
	const int Lines = FlowDrumControls(Widths, Groups, Inner, SX(GAP), SX(GROUP_GAP), Places);
	const int LineHeight = SY(BUTTON_HEIGHT) + SY(LINE_GAP);
	for (int c = 0; c < CONTROLS; ++c) {
		const int X = SX(MARGIN) + Places[static_cast<size_t>(c)].X;
		const int Y = SY(MARGIN) + Places[static_cast<size_t>(c)].Line * LineHeight;
		m_rcControls[c] = CRect(X, Y, X + m_iWidths[c], Y + SY(BUTTON_HEIGHT));
		if (CONTROL_BUTTONS[c] >= 0)
			m_Buttons[CONTROL_BUTTONS[c]].MoveWindow(&m_rcControls[c]);
	}
	// The grid under them, its lines no taller than they need, and the news under it
	const int Top = SY(MARGIN) + Lines * LineHeight;
	const int Room = std::max(0, static_cast<int>(Client.bottom) - SY(MARGIN) - SY(STATUS_HEIGHT) - Top);
	const int Height = std::min(Room, SY(LANE_MOST) * (DRUM_LANES + 1));
	m_Grid.MoveWindow(SX(MARGIN), Top, Inner, Height);
	m_rcStatus = CRect(SX(MARGIN), Top + Height + SY(3), static_cast<int>(Client.right) - SX(MARGIN), Top + Height + SY(3) + SY(STATUS_HEIGHT));
	Invalidate(FALSE);
}

void CDrumGridPanelWnd::OnSize(UINT nType, int cx, int cy)
{
	CWnd::OnSize(nType, cx, cy);
	Layout();
}

void CDrumGridPanelWnd::OnPaint()
{
	CPaintDC dc(this);
	CRect Client;
	GetClientRect(&Client);
	dc.FillSolidRect(&Client, ::GetSysColor(COLOR_3DFACE));
	CFont *pOldFont = dc.SelectObject(&m_Font);
	dc.SetBkMode(TRANSPARENT);
	dc.SetTextColor(::GetSysColor(COLOR_BTNTEXT));
	CRect Rect = m_rcControls[CONTROL_BAR];
	dc.DrawText(m_strBar, &Rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	Rect = m_rcControls[CONTROL_WHERE];
	dc.DrawText(m_strWhere, &Rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	Rect = m_rcControls[CONTROL_BEATS];
	dc.DrawText(CString(BEATS_TEXT), &Rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	Rect = m_rcStatus;
	dc.DrawText(m_strStatus, &Rect, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
	dc.SelectObject(pOldFont);
}

BOOL CDrumGridPanelWnd::OnEraseBkgnd(CDC *pDC)
{
	return TRUE;	// all of it is painted
}

void CDrumGridPanelWnd::SetText(CString &Text, LPCTSTR New, const CRect &Where)
{
	if (Text == New)
		return;
	Text = New;
	if (GetSafeHwnd() != NULL)
		InvalidateRect(&Where, FALSE);
}

void CDrumGridPanelWnd::Hint(LPCTSTR Text)
{
	m_pMainFrame->SetMessageText(Text);
}

void CDrumGridPanelWnd::Status(LPCTSTR Text)
{
	SetText(m_strStatus, Text, m_rcStatus);
}

// The bar it is docked in

BEGIN_MESSAGE_MAP(CDrumGridBar, CControlBar)
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

CDrumGridBar::CDrumGridBar() : m_pPanel(NULL), m_bRight(false), m_iHeight(DEFAULT_HEIGHT), m_iWidth(DEFAULT_WIDTH), m_iRoom(0),
	m_iDragFrom(-1), m_iDragSize(0), m_iDragMax(0)
{
}

CDrumGridBar::~CDrumGridBar()
{
	delete m_pPanel;	// its window went with this one, its parent
}

bool CDrumGridBar::Create(CMainFrame *pFrame)
{
	m_bRight = theApp.GetProfileInt(SECTION, _T("On the right"), 0) != 0;
	m_iHeight = std::max(SY(MIN_HEIGHT), static_cast<int>(theApp.GetProfileInt(SECTION, _T("Height"), SY(DEFAULT_HEIGHT))));
	m_iWidth = std::max(SX(MIN_WIDTH), static_cast<int>(theApp.GetProfileInt(SECTION, _T("Width"), SX(DEFAULT_WIDTH))));
	m_dwStyle = m_bRight ? CBRS_ALIGN_RIGHT : CBRS_ALIGN_BOTTOM;
	const CString Class = AfxRegisterWndClass(0, ::LoadCursor(NULL, IDC_ARROW), NULL, NULL);
	if (!CWnd::Create(Class, _T("Drum grid"), WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, CRect(0, 0, 0, 0), pFrame,
		AFX_IDW_CONTROLBAR_FIRST + 36))
		return false;
	m_pPanel = new CDrumGridPanelWnd();
	return m_pPanel->Create(this, pFrame);
}

void CDrumGridBar::Opened(bool Open)
{
	if (m_pPanel != NULL && m_pPanel->GetSafeHwnd() != NULL)
		m_pPanel->Opened(Open);
	theApp.WriteProfileInt(SECTION, _T("Open"), Open ? 1 : 0);
}

void CDrumGridBar::SongChanged()
{
	if (m_pPanel != NULL && m_pPanel->GetSafeHwnd() != NULL)
		m_pPanel->SongChanged();
}

bool CDrumGridBar::WasOpen()
{
	return theApp.GetProfileInt(SECTION, _T("Open"), 0) != 0;
}

void CDrumGridBar::DockOnRight(bool Right)
{
	if (Right == m_bRight)
		return;
	m_bRight = Right;
	m_dwStyle = (m_dwStyle & ~CBRS_ALIGN_ANY) | (Right ? CBRS_ALIGN_RIGHT : CBRS_ALIGN_BOTTOM);
	theApp.WriteProfileInt(SECTION, _T("On the right"), Right ? 1 : 0);
	CFrameWnd *pFrame = GetParentFrame();
	if (pFrame != NULL)
		pFrame->RecalcLayout();
	Invalidate();
}

int CDrumGridBar::Divider() const
{
	return std::max(4, m_bRight ? SX(DIVIDER) : SY(DIVIDER));
}

int CDrumGridBar::MaxSize() const
{
	if (m_bRight)
		return std::max(SX(MIN_WIDTH), m_iRoom - SX(MIN_VIEW_WIDTH));
	return std::max(SY(MIN_HEIGHT), m_iRoom / 2);	// the pattern editor (and the piano roll) keep half
}

LRESULT CDrumGridBar::OnSizeParent(WPARAM wParam, LPARAM lParam)
{
	const AFX_SIZEPARENTPARAMS *pLayout = reinterpret_cast<const AFX_SIZEPARENTPARAMS*>(lParam);
	m_iRoom = m_bRight ? pLayout->rect.right - pLayout->rect.left : pLayout->rect.bottom - pLayout->rect.top;
	return CControlBar::OnSizeParent(wParam, lParam);
}

CSize CDrumGridBar::CalcFixedLayout(BOOL bStretch, BOOL bHorz)
{
	const int Shown = std::max(0, std::min(Size(), MaxSize()));
	return m_bRight ? CSize(Shown, 32767) : CSize(32767, Shown);
}

void CDrumGridBar::OnUpdateCmdUI(CFrameWnd *pTarget, BOOL bDisableIfNoHndler)
{
	// Nothing in it is a command
}

BOOL CDrumGridBar::PreTranslateMessage(MSG *pMsg)
{
	// Not as a dialog (as control bars are), so FamiTracker's shortcuts work
	return CWnd::PreTranslateMessage(pMsg);
}

void CDrumGridBar::OnSize(UINT nType, int cx, int cy)
{
	CControlBar::OnSize(nType, cx, cy);
	if (m_pPanel == NULL || m_pPanel->GetSafeHwnd() == NULL)
		return;
	if (m_bRight)
		m_pPanel->MoveWindow(Divider(), 0, std::max(0, cx - Divider()), cy);
	else
		m_pPanel->MoveWindow(0, Divider(), cx, std::max(0, cy - Divider()));
}

void CDrumGridBar::OnPaint()
{
	CPaintDC dc(this);
	CRect Rect;
	GetClientRect(&Rect);
	const int Line = Divider();
	const COLORREF Face = ::GetSysColor(COLOR_3DFACE), Light = ::GetSysColor(COLOR_3DHILIGHT), Shadow = ::GetSysColor(COLOR_3DSHADOW);
	if (m_bRight) {
		dc.FillSolidRect(Rect.left, Rect.top, Line, Rect.Height(), Face);
		dc.FillSolidRect(Rect.left, Rect.top, 1, Rect.Height(), Light);
		dc.FillSolidRect(Rect.left + Line - 1, Rect.top, 1, Rect.Height(), Shadow);
		const int Y = Rect.top + Rect.Height() / 2 - SY(12);
		for (int i = 0; i < 4; ++i)
			dc.FillSolidRect(Rect.left + Line / 2 - 1, Y + i * SY(7), 2, SY(3), Shadow);
	}
	else {
		dc.FillSolidRect(Rect.left, Rect.top, Rect.Width(), Line, Face);
		dc.FillSolidRect(Rect.left, Rect.top, Rect.Width(), 1, Light);
		dc.FillSolidRect(Rect.left, Rect.top + Line - 1, Rect.Width(), 1, Shadow);
		const int X = Rect.left + Rect.Width() / 2 - SX(12);
		for (int i = 0; i < 4; ++i)
			dc.FillSolidRect(X + i * SX(7), Rect.top + Line / 2 - 1, SX(3), 2, Shadow);
	}
}

BOOL CDrumGridBar::OnEraseBkgnd(CDC *pDC)
{
	return TRUE;	// the line is painted, the panel covers the rest
}

void CDrumGridBar::OnLButtonDown(UINT nFlags, CPoint point)
{
	if ((m_bRight ? point.x : point.y) >= Divider())
		return;
	// Drag the line to make the panel bigger or smaller
	CPoint Screen = point;
	ClientToScreen(&Screen);
	m_iDragFrom = static_cast<int>(m_bRight ? Screen.x : Screen.y);
	m_iDragSize = Size();
	m_iDragMax = MaxSize();
	SetCapture();
}

void CDrumGridBar::OnMouseMove(UINT nFlags, CPoint point)
{
	if (m_iDragFrom < 0)
		return;
	CPoint Screen = point;
	ClientToScreen(&Screen);
	const int Moved = static_cast<int>(m_bRight ? Screen.x : Screen.y) - m_iDragFrom;
	const int Least = m_bRight ? SX(MIN_WIDTH) : SY(MIN_HEIGHT);
	const int New = std::max(Least, std::min(m_iDragMax, m_iDragSize - Moved));
	if (New != Size()) {
		Size() = New;
		CFrameWnd *pFrame = GetParentFrame();
		if (pFrame != NULL)
			pFrame->RecalcLayout();
	}
}

void CDrumGridBar::OnLButtonUp(UINT nFlags, CPoint point)
{
	if (m_iDragFrom >= 0) {
		m_iDragFrom = -1;
		ReleaseCapture();
		theApp.WriteProfileInt(SECTION, m_bRight ? _T("Width") : _T("Height"), Size());
	}
}

void CDrumGridBar::OnCaptureChanged(CWnd *pWnd)
{
	m_iDragFrom = -1;
	CControlBar::OnCaptureChanged(pWnd);
}

BOOL CDrumGridBar::OnSetCursor(CWnd *pWnd, UINT nHitTest, UINT message)
{
	if (pWnd == this && nHitTest == HTCLIENT) {
		::SetCursor(::LoadCursor(NULL, m_bRight ? IDC_SIZEWE : IDC_SIZENS));
		return TRUE;
	}
	return CControlBar::OnSetCursor(pWnd, nHitTest, message);
}

void CDrumGridBar::OnDestroy()
{
	theApp.WriteProfileInt(SECTION, _T("Height"), m_iHeight);
	theApp.WriteProfileInt(SECTION, _T("Width"), m_iWidth);
	CControlBar::OnDestroy();
}

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

// Edit > Drum grid (see DrumGridDlg.h)

// Standard and API headers come before stdafx.h, whose DEBUG_NEW macro would
// otherwise rewrite the placement new inside the standard library.
#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>
#include "Api/Presets.h"
#include "stdafx.h"
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
#include "DrumGridDlg.h"

namespace {

const UINT_PTR TMR_FOLLOW = 1;			// shows where the song plays
const UINT FOLLOW_MS = 40;

// A colour for each drum, and grey for other notes
const COLORREF LANE_COLOURS[DRUM_LANES + 1] = {
	RGB(225, 85, 70), RGB(240, 170, 50), RGB(70, 165, 230), RGB(140, 110, 225), RGB(150, 150, 155)
};
const COLORREF GRID_BACK = RGB(250, 250, 252);
const COLORREF GRID_BEAT = RGB(236, 238, 243);	// every other beat, shaded
const COLORREF GRID_LINE = RGB(205, 208, 215);
const COLORREF GRID_BAR = RGB(120, 125, 135);
const COLORREF PLAYING = RGB(255, 236, 150);

}	// namespace

class CDrumGridDlg;

// The grid: a line for each drum, a column for each step
class CDrumGridWnd : public CWnd
{
public:
	CDrumGridWnd() : m_pDlg(NULL), m_iHoverLane(-1), m_iHoverStep(-1), m_iDragLane(-1), m_iDragMode(DRUM_NONE), m_iLastStep(-1) {}

	bool Create(CDrumGridDlg *pDlg, const CRect &Rect, CWnd *pParent);

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

	CDrumGridDlg *m_pDlg;
	int m_iHoverLane, m_iHoverStep;
	int m_iDragLane;					// the line being painted along, -1 for none
	int m_iDragMode;					// the drum put on, or DRUM_NONE: taking them off
	int m_iLastStep;
	CFont m_Font;
};

class CDrumGridDlg : public CDialog
{
public:
	CDrumGridDlg(CMainFrame *pMainFrame);

	// For the grid
	int Steps() const { return static_cast<int>(m_Cells.size()); }
	int StepsPerBeat() const { return m_iStepsPerBeat; }
	int LaneAt(int Step) const;
	int PlayingStep() const { return m_iPlaying; }
	bool HasOther() const;
	void PaintStep(int Step, int Lane, int Mode);
	void Commit();
	void Hint(LPCTSTR Text);

protected:
	virtual BOOL OnInitDialog();
	virtual void OnOK();
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnDestroy();
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
	unsigned int Track() const;
	int Length() const;					// rows a frame
	int TotalRows() const;
	int BarStart() const { return m_iBar * m_iBarSteps; }
	void Load();
	void FindKit();
	bool EnsureDrum(int Lane);
	void Beat(int Beat);
	void CopyTo(int First, int Last);	// bars
	void Preview(int Lane);
	void Status(LPCTSTR Text);

	CMainFrame *m_pMainFrame;
	CDrumGridWnd m_Grid;
	int m_iNoise;						// the noise channel
	int m_iBarSteps;					// rows a bar: the second row highlight
	int m_iStepsPerBeat;				// the first row highlight
	int m_iBar;
	int m_iBars;
	int m_iPlaying;						// the step the song plays, -1
	bool m_bDirty;						// the grid has changes the song hasn't
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

bool CDrumGridWnd::Create(CDrumGridDlg *pDlg, const CRect &Rect, CWnd *pParent)
{
	m_pDlg = pDlg;
	const CString Class = AfxRegisterWndClass(0, ::LoadCursor(NULL, IDC_HAND), NULL, NULL);
	if (!CWnd::CreateEx(WS_EX_CLIENTEDGE, Class, _T("Drums"), WS_CHILD | WS_VISIBLE, Rect, pParent, 0))
		return false;
	LOGFONT Log;
	memset(&Log, 0, sizeof(Log));
	::GetObject(::GetStockObject(DEFAULT_GUI_FONT), sizeof(Log), &Log);
	m_Font.CreateFontIndirect(&Log);
	return true;
}

int CDrumGridWnd::Lines() const
{
	return m_pDlg->HasOther() ? DRUM_LANES + 1 : DRUM_LANES;
}

int CDrumGridWnd::LabelWidth() const
{
	return SX(84);
}

CRect CDrumGridWnd::CellRect(int Lane, int Step) const
{
	CRect Client;
	GetClientRect(&Client);
	const int Steps = std::max(1, m_pDlg->Steps());
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
	const int Steps = std::max(1, m_pDlg->Steps());
	const int Width = std::max(1, Client.Width() - LabelWidth());
	return std::max(0, std::min(Steps - 1, (X - LabelWidth()) * Steps / Width));
}

bool CDrumGridWnd::HitTest(const CPoint &Point, int &Lane, int &Step) const
{
	CRect Client;
	GetClientRect(&Client);
	if (Point.x < LabelWidth() || !Client.PtInRect(Point) || m_pDlg->Steps() == 0)
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

	const int Steps = m_pDlg->Steps();
	const int Beat = std::max(1, m_pDlg->StepsPerBeat());
	const int Playing = m_pDlg->PlayingStep();
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
			if (m_pDlg->LaneAt(s) == Lane) {
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
	m_iDragMode = m_pDlg->LaneAt(Step) == Lane ? DRUM_NONE : Lane;
	if (Lane == DRUM_OTHER)
		m_iDragMode = DRUM_NONE;	// other notes can only be taken off
	m_iLastStep = Step;
	m_pDlg->PaintStep(Step, Lane, m_iDragMode);
	SetCapture();
}

void CDrumGridWnd::OnMouseMove(UINT nFlags, CPoint point)
{
	if (m_iDragLane >= 0 && GetCapture() == this) {
		const int Step = StepAt(point.x);
		if (Step != m_iLastStep) {
			const int From = std::min(Step, m_iLastStep), To = std::max(Step, m_iLastStep);
			for (int s = From; s <= To; ++s)
				m_pDlg->PaintStep(s, m_iDragLane, m_iDragMode);
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
			else if (m_pDlg->LaneAt(Step) == Lane)
				Text.Format(_T("Step %d: click to take the %s off."), Step + 1, static_cast<LPCTSTR>(CString(DrumName(Lane)).MakeLower()));
			else
				Text.Format(_T("Step %d: click to put a %s here, or drag along the line."), Step + 1,
					static_cast<LPCTSTR>(CString(DrumName(Lane)).MakeLower()));
			m_pDlg->Hint(Text);
		}
	}
}

void CDrumGridWnd::Finish()
{
	if (m_iDragLane < 0)
		return;
	m_iDragLane = -1;
	m_pDlg->Commit();
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

// The dialog

BEGIN_MESSAGE_MAP(CDrumGridDlg, CDialog)
	ON_WM_TIMER()
	ON_WM_DESTROY()
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

CDrumGridDlg::CDrumGridDlg(CMainFrame *pMainFrame) :
	CDialog(IDD_DRUM_GRID, pMainFrame),
	m_pMainFrame(pMainFrame),
	m_iNoise(-1),
	m_iBarSteps(16),
	m_iStepsPerBeat(4),
	m_iBar(0),
	m_iBars(1),
	m_iPlaying(-1),
	m_bDirty(false)
{
}

CFamiTrackerDoc *CDrumGridDlg::Doc() const
{
	return static_cast<CFamiTrackerDoc*>(m_pMainFrame->GetActiveDocument());
}

unsigned int CDrumGridDlg::Track() const
{
	return static_cast<unsigned int>(std::max(0, m_pMainFrame->GetSelectedTrack()));
}

int CDrumGridDlg::Length() const
{
	return std::max(1, static_cast<int>(Doc()->GetPatternLength(Track())));
}

int CDrumGridDlg::TotalRows() const
{
	return Length() * static_cast<int>(Doc()->GetFrameCount(Track()));
}

BOOL CDrumGridDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	CFamiTrackerDoc *pDoc = Doc();
	for (int c = 0; c < static_cast<int>(pDoc->GetAvailableChannels()) && m_iNoise < 0; ++c)
		if (pDoc->GetChannelType(c) == CHANID_NOISE)
			m_iNoise = c;

	// The grid goes where the dialog keeps a place for it
	CWnd *pPlace = GetDlgItem(IDC_DRUM_GRID);
	CRect Rect;
	pPlace->GetWindowRect(&Rect);
	ScreenToClient(&Rect);
	pPlace->ShowWindow(SW_HIDE);
	m_Grid.Create(this, Rect, this);

	// Bars as the row highlight has them; the one the cursor is in
	m_iBarSteps = pDoc->GetSecondHighlight() > 0 ? pDoc->GetSecondHighlight() : 16;
	m_iStepsPerBeat = pDoc->GetFirstHighlight() > 0 ? pDoc->GetFirstHighlight() : 4;
	if (m_iStepsPerBeat > m_iBarSteps)
		m_iStepsPerBeat = m_iBarSteps;
	CFamiTrackerView *pView = CFamiTrackerView::GetView();
	if (pView != NULL)
		m_iBar = (static_cast<int>(pView->GetSelectedFrame()) * Length() + static_cast<int>(pView->GetSelectedRow())) / m_iBarSteps;

	for (int i = 0; i < 4; ++i)
		SetDlgItemText(IDC_DRUM_BEAT1 + i, CString(BeatName(i)));
	Load();
	Status(_T("Click the steps to put drums on them, or drag along a line. Each change is one Ctrl+Z in the song."));
	SetTimer(TMR_FOLLOW, FOLLOW_MS, NULL);
	return TRUE;
}

void CDrumGridDlg::OnDestroy()
{
	KillTimer(TMR_FOLLOW);
	CDialog::OnDestroy();
}

void CDrumGridDlg::FindKit()
{
	for (int Lane = 0; Lane < DRUM_LANES; ++Lane)
		m_Kit.Instruments[Lane] = FindNamedInstrument(Doc(), INST_2A03, CString(DrumName(Lane)));
}

void CDrumGridDlg::Load()
{
	CFamiTrackerDoc *pDoc = Doc();
	const int Total = TotalRows();
	m_iBars = std::max(1, (Total + m_iBarSteps - 1) / m_iBarSteps);
	m_iBar = std::max(0, std::min(m_iBars - 1, m_iBar));
	FindKit();
	const int Start = BarStart();
	const int Steps = std::max(0, std::min(m_iBarSteps, Total - Start));
	m_Cells.assign(static_cast<size_t>(Steps), stChanNote());
	for (int s = 0; s < Steps; ++s) {
		const int Row = Start + s;
		pDoc->GetNoteData(Track(), static_cast<unsigned int>(Row / Length()), static_cast<unsigned int>(m_iNoise),
			static_cast<unsigned int>(Row % Length()), &m_Cells[static_cast<size_t>(s)]);
	}
	m_bDirty = false;

	CString Text;
	Text.Format(_T("Bar %d of %d"), m_iBar + 1, m_iBars);
	SetDlgItemText(IDC_DRUM_BAR, Text);
	Text.Format(_T("frame %02X, rows %02X-%02X"), Start / Length(), Start % Length(), (Start + std::max(1, Steps) - 1) % Length());
	SetDlgItemText(IDC_DRUM_WHERE, Text);
	GetDlgItem(IDC_DRUM_PREV)->EnableWindow(m_iBar > 0 ? TRUE : FALSE);
	GetDlgItem(IDC_DRUM_NEXT)->EnableWindow(m_iBar + 1 < m_iBars ? TRUE : FALSE);
	GetDlgItem(IDC_DRUM_COPYNEXT)->EnableWindow(m_iBar + 1 < m_iBars ? TRUE : FALSE);
	GetDlgItem(IDC_DRUM_COPYALL)->EnableWindow(m_iBars > 1 ? TRUE : FALSE);
	if (m_Grid.GetSafeHwnd() != NULL)
		m_Grid.Invalidate(FALSE);
}

int CDrumGridDlg::LaneAt(int Step) const
{
	if (Step < 0 || Step >= Steps())
		return DRUM_NONE;
	return DrumOf(m_Cells[static_cast<size_t>(Step)], m_Kit);
}

bool CDrumGridDlg::HasOther() const
{
	for (int s = 0; s < Steps(); ++s)
		if (LaneAt(s) == DRUM_OTHER)
			return true;
	return false;
}

// The drum's instrument, made from its sound preset the first time
bool CDrumGridDlg::EnsureDrum(int Lane)
{
	if (Lane < 0 || Lane >= DRUM_LANES)
		return false;
	if (m_Kit.Instruments[Lane] >= 0)
		return true;
	const ftapi::Preset *pPreset = ftapi::FindPreset(DrumPreset(Lane));
	if (pPreset == NULL)
		return false;
	CString Error;
	const int Index = AddPresetInstrument(Doc(), *pPreset, false, false, Error);
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

void CDrumGridDlg::Preview(int Lane)
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

void CDrumGridDlg::PaintStep(int Step, int Lane, int Mode)
{
	if (Step < 0 || Step >= Steps())
		return;
	stChanNote &Cell = m_Cells[static_cast<size_t>(Step)];
	if (Mode == DRUM_NONE) {
		if (DrumOf(Cell, m_Kit) != Lane)
			return;		// only that line's drums come off
		SetDrum(Cell, DRUM_NONE, m_Kit);
	}
	else {
		if (DrumOf(Cell, m_Kit) == Mode || !EnsureDrum(Mode))
			return;
		SetDrum(Cell, Mode, m_Kit);
		Preview(Mode);
	}
	m_bDirty = true;
	m_Grid.Invalidate(FALSE);
}

void CDrumGridDlg::Commit()
{
	if (!m_bDirty || m_Cells.empty())
		return;
	m_bDirty = false;
	CString Message;
	if (!WriteChannelRows(m_pMainFrame, m_iNoise, BarStart(), &m_Cells[0], Steps(), BarStart(), Message))
		Status(Message);
	Load();		// as the song has it now
}

void CDrumGridDlg::Beat(int Which)
{
	if (m_Cells.empty())
		return;
	for (int Lane = 0; Lane < DRUM_LANES; ++Lane)
		if (BeatUses(Which, Lane) && !EnsureDrum(Lane))
			return;
	ApplyBeat(m_Cells, Which, m_iStepsPerBeat, m_Kit);
	m_bDirty = true;
	Commit();
	CString Text;
	if (Which == BEAT_CLEAR)
		Text = _T("No drums in this bar now. Ctrl+Z in the song brings them back.");
	else
		Text.Format(_T("The \"%s\" beat is in this bar. Copy it to the next bar or to every bar, or change steps by clicking."),
			static_cast<LPCTSTR>(CString(BeatName(Which))));
	Status(Text);
}

void CDrumGridDlg::OnBeat1() { Beat(BEAT_BASIC); }
void CDrumGridDlg::OnBeat2() { Beat(BEAT_FOUR_ON_FLOOR); }
void CDrumGridDlg::OnBeat3() { Beat(BEAT_HALF_TIME); }
void CDrumGridDlg::OnBeat4() { Beat(BEAT_BUSY); }
void CDrumGridDlg::OnClear() { Beat(BEAT_CLEAR); }

// This bar's drums into bars First to Last, as one undoable change; their effects stay
void CDrumGridDlg::CopyTo(int First, int Last)
{
	CFamiTrackerDoc *pDoc = Doc();
	const int Total = TotalRows();
	const int From = First * m_iBarSteps;
	const int To = std::min(Total, (Last + 1) * m_iBarSteps);
	if (m_Cells.empty() || To <= From)
		return;
	std::vector<stChanNote> Cells(static_cast<size_t>(To - From));
	for (int Row = From; Row < To; ++Row) {
		stChanNote &Cell = Cells[static_cast<size_t>(Row - From)];
		pDoc->GetNoteData(Track(), static_cast<unsigned int>(Row / Length()), static_cast<unsigned int>(m_iNoise),
			static_cast<unsigned int>(Row % Length()), &Cell);
		if (Row / m_iBarSteps == m_iBar)
			continue;	// the bar itself, when it's among them
		const stChanNote &Source = m_Cells[static_cast<size_t>((Row - From) % m_iBarSteps) % m_Cells.size()];
		Cell.Note = Source.Note;
		Cell.Octave = Source.Octave;
		Cell.Instrument = Source.Instrument;
		Cell.Vol = Source.Vol;
	}
	CString Message;
	if (!WriteChannelRows(m_pMainFrame, m_iNoise, From, &Cells[0], To - From, BarStart(), Message)) {
		Status(Message);
		return;
	}
	Load();
	if (First == Last)
		Message.Format(_T("Copied this bar's drums to bar %d. Ctrl+Z in the song undoes it."), First + 1);
	else
		Message = _T("Copied this bar's drums to every bar of the song. Ctrl+Z in the song undoes it.");
	Status(Message);
}

void CDrumGridDlg::OnCopyNext()
{
	if (m_iBar + 1 < m_iBars)
		CopyTo(m_iBar + 1, m_iBar + 1);
}

void CDrumGridDlg::OnCopyAll()
{
	if (m_iBars > 1)
		CopyTo(0, m_iBars - 1);
}

void CDrumGridDlg::OnPrevious()
{
	if (m_iBar > 0) {
		--m_iBar;
		Load();
	}
}

void CDrumGridDlg::OnNext()
{
	if (m_iBar + 1 < m_iBars) {
		++m_iBar;
		Load();
	}
}

void CDrumGridDlg::OnPlay()
{
	if (theApp.IsPlaying()) {
		theApp.StopPlayer();
		SetDlgItemText(IDC_DRUM_PLAY, _T("Play from this bar"));
		return;
	}
	CFamiTrackerView *pView = CFamiTrackerView::GetView();
	if (pView == NULL)
		return;
	pView->SelectFrame(static_cast<unsigned int>(BarStart() / Length()));
	pView->GetPatternEditor()->MoveToRow(BarStart() % Length());
	pView->SelectChannel(static_cast<unsigned int>(m_iNoise));
	theApp.StartPlayer(MODE_PLAY_CURSOR);
	SetDlgItemText(IDC_DRUM_PLAY, _T("Stop"));
}

void CDrumGridDlg::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent != TMR_FOLLOW) {
		CDialog::OnTimer(nIDEvent);
		return;
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
	else {
		CString Text;
		GetDlgItemText(IDC_DRUM_PLAY, Text);
		if (Text != _T("Play from this bar"))
			SetDlgItemText(IDC_DRUM_PLAY, _T("Play from this bar"));
	}
	if (Playing != m_iPlaying) {
		m_iPlaying = Playing;
		m_Grid.Invalidate(FALSE);
	}
}

void CDrumGridDlg::OnOK()
{
	// Enter doesn't close it
}

void CDrumGridDlg::Hint(LPCTSTR Text)
{
	m_pMainFrame->SetMessageText(Text);
}

void CDrumGridDlg::Status(LPCTSTR Text)
{
	SetDlgItemText(IDC_DRUM_STATUS, Text);
}

void ShowDrumGrid(CMainFrame *pMainFrame)
{
	CFamiTrackerDoc *pDoc = static_cast<CFamiTrackerDoc*>(pMainFrame->GetActiveDocument());
	if (pDoc == NULL || !pDoc->IsFileLoaded())
		return;
	CDrumGridDlg Dialog(pMainFrame);
	Dialog.DoModal();
}

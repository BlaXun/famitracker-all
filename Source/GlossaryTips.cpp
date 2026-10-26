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

// Explanations in place (see GlossaryTips.h)

#include "stdafx.h"
#include <algorithm>
#include <string>
#include <vector>
#include "Api/Effects.h"
#include "Api/Glossary.h"
#include "Api/Music.h"
#include "Api/Song.h"
#include "FamiTracker.h"
#include "FamiTrackerDoc.h"
#include "FamiTrackerView.h"
#include "PatternEditor.h"
#include "MainFrm.h"
#include "Accelerator.h"
#include "GlossaryDlg.h"
#include "GlossaryHear.h"
#include "GlossaryTips.h"

namespace {

const UINT_PTR TMR_POLL = 1;
const UINT POLL_MS = 100;
const int REST_TICKS = 4;		// the mouse rests this many polls on a part before its tip shows

// The song settings' labels: those with an id of their own, and the ones whose text says what they are
const UINT LABEL_IDS[] = { IDC_SPEED_LABEL, IDC_TEMPO_LABEL, IDC_ROWS_LABEL, IDC_FRAMES_LABEL };

CString KeyName(UINT Command)
{
	CString Text;
	if (!theApp.GetAccelerator()->GetShortcutString(Command, Text))
		return CString();
	Text.Trim();
	return Text == _T("None") ? CString() : Text;
}

// "F1: more in the glossary. Ctrl+Shift+H: hear it." with the keys as the user has them
CString MoreHint(bool Clickable, bool Hear)
{
	CString Text = Clickable ? _T("Click (or F1) for more in the glossary.") : _T("F1: more in the glossary.");
	if (Hear) {
		const CString Key = KeyName(ID_HELP_HEAREFFECT);
		Text += Key.IsEmpty() ? CString(_T(" Help > Hear the effect at the cursor plays it.")) : _T(" ") + Key + _T(": hear it.");
	}
	return Text;
}

// A label the glossary explains: a click opens it there (the hand says so)
class CGlossaryLabel : public CStatic
{
public:
	explicit CGlossaryLabel(CMainFrame *pMainFrame) : m_pMainFrame(pMainFrame) {}

protected:
	afx_msg BOOL OnSetCursor(CWnd *pWnd, UINT nHitTest, UINT message)
	{
		::SetCursor(::LoadCursor(NULL, IDC_HAND));
		return TRUE;
	}
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point)
	{
		CString Text;
		GetWindowText(Text);
		const std::string Id = ftapi::GlossaryLabelId("song settings", static_cast<LPCTSTR>(Text));
		if (!Id.empty())
			ShowGlossaryAt(m_pMainFrame, Id);
		CStatic::OnLButtonUp(nFlags, point);
	}
	virtual void PostNcDestroy()
	{
		delete this;
	}
	DECLARE_MESSAGE_MAP()

private:
	CMainFrame *m_pMainFrame;
};

BEGIN_MESSAGE_MAP(CGlossaryLabel, CStatic)
	ON_WM_SETCURSOR()
	ON_WM_LBUTTONUP()
END_MESSAGE_MAP()

struct area_t {
	HWND hWnd;
	glossary_hit_t HitTest;
};

class CGlossaryTips : public CWnd
{
public:
	CGlossaryTips() : m_pMainFrame(NULL), m_iStill(0), m_bShown(false), m_hShownWnd(NULL), m_bCell(false), m_iCellFrame(0),
		m_iCellRow(0), m_iCellChannel(0), m_iCellColumn(0), m_iStatusKey(-1), m_bRestored(false) {}

	bool Start(CMainFrame *pMainFrame);
	void Stop();
	void AddArea(CWnd *pWnd, const glossary_hit_t &HitTest);
	bool ShownEntry(std::string &Id) const;
	bool ShownCell(int &Frame, int &Row, int &Channel, int &Column) const;
	std::string CursorEntry() const;

protected:
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	DECLARE_MESSAGE_MAP()

private:
	bool TipAt(CPoint Screen, HWND &hWnd, glossary_tip_t &Tip);
	void Show(const glossary_tip_t &Tip, CPoint Screen);
	void Hide();
	void ShowCursorEffect();
	void FindLabels();
	static BOOL CALLBACK FindLabel(HWND hWnd, LPARAM lParam);

	CMainFrame *m_pMainFrame;
	CToolTipCtrl m_Tip;
	std::vector<area_t> m_Areas;
	std::vector<HWND> m_Labels;
	CPoint m_Mouse;
	int m_iStill;
	bool m_bShown;
	HWND m_hShownWnd;
	glossary_tip_t m_Shown;
	bool m_bCell;				// the tip shows a cell's effect...
	int m_iCellFrame, m_iCellRow, m_iCellChannel, m_iCellColumn;	// ...this one
	long long m_iStatusKey;		// the cursor's effect the status bar says, -1 for none
	CString m_strStatus;
	bool m_bRestored;			// the glossary shown again if it was open, once the main window shows
};

BEGIN_MESSAGE_MAP(CGlossaryTips, CWnd)
	ON_WM_TIMER()
END_MESSAGE_MAP()

CGlossaryTips *g_pTips = NULL;

bool CGlossaryTips::Start(CMainFrame *pMainFrame)
{
	m_pMainFrame = pMainFrame;
	if (!CreateEx(0, AfxRegisterWndClass(0), _T("Glossary tips"), 0, 0, 0, 0, 0, HWND_MESSAGE, NULL))
		return false;
	if (!m_Tip.Create(pMainFrame, TTS_ALWAYSTIP | TTS_NOPREFIX))
		return false;
	TOOLINFO Info;
	memset(&Info, 0, sizeof(Info));
	Info.cbSize = sizeof(Info);
	Info.uFlags = TTF_TRACK | TTF_ABSOLUTE;
	Info.hwnd = pMainFrame->GetSafeHwnd();
	Info.uId = 1;
	Info.lpszText = const_cast<LPTSTR>(_T(""));
	m_Tip.SendMessage(TTM_ADDTOOL, 0, reinterpret_cast<LPARAM>(&Info));
	m_Tip.SetMaxTipWidth(SX(380));
	FindLabels();
	SetTimer(TMR_POLL, POLL_MS, NULL);
	return true;
}

void CGlossaryTips::Stop()
{
	if (m_hWnd != NULL) {
		KillTimer(TMR_POLL);
		DestroyWindow();
	}
	if (m_Tip.m_hWnd != NULL)
		m_Tip.DestroyWindow();
	m_Areas.clear();
}

void CGlossaryTips::AddArea(CWnd *pWnd, const glossary_hit_t &HitTest)
{
	if (pWnd == NULL || pWnd->GetSafeHwnd() == NULL)
		return;
	const area_t Area = { pWnd->GetSafeHwnd(), HitTest };
	m_Areas.push_back(Area);
}

BOOL CALLBACK CGlossaryTips::FindLabel(HWND hWnd, LPARAM lParam)
{
	CGlossaryTips *pThis = reinterpret_cast<CGlossaryTips*>(lParam);
	TCHAR Class[16] = { 0 };
	::GetClassName(hWnd, Class, 16);
	if (_tcsicmp(Class, _T("Static")) != 0)
		return TRUE;
	const UINT Id = static_cast<UINT>(::GetDlgCtrlID(hWnd));
	bool Wanted = false;
	for (size_t i = 0; i < sizeof(LABEL_IDS) / sizeof(LABEL_IDS[0]); ++i)
		Wanted = Wanted || Id == LABEL_IDS[i];
	if (!Wanted) {
		// The labels without an id of their own are known by their words
		TCHAR Text[64] = { 0 };
		::GetWindowText(hWnd, Text, 64);
		Wanted = !ftapi::GlossaryLabelId("song settings", Text).empty();
	}
	if (Wanted && CWnd::FromHandlePermanent(hWnd) == NULL) {
		CGlossaryLabel *pLabel = new CGlossaryLabel(pThis->m_pMainFrame);
		if (pLabel->SubclassWindow(hWnd)) {
			pLabel->ModifyStyle(0, SS_NOTIFY);	// so the mouse reaches it
			pThis->m_Labels.push_back(hWnd);
		}
		else
			delete pLabel;
	}
	return TRUE;
}

// The song settings' labels, in the main window's bars (the dialog bar, the octave bar): each made to take clicks
void CGlossaryTips::FindLabels()
{
	::EnumChildWindows(m_pMainFrame->GetSafeHwnd(), FindLabel, reinterpret_cast<LPARAM>(this));
}

bool CGlossaryTips::ShownEntry(std::string &Id) const
{
	Id = m_bShown ? m_Shown.Id : std::string();
	return !Id.empty();
}

bool CGlossaryTips::ShownCell(int &Frame, int &Row, int &Channel, int &Column) const
{
	if (!m_bShown || !m_bCell)
		return false;
	Frame = m_iCellFrame;
	Row = m_iCellRow;
	Channel = m_iCellChannel;
	Column = m_iCellColumn;
	return true;
}

std::string CGlossaryTips::CursorEntry() const
{
	CFamiTrackerView *pView = CFamiTrackerView::GetView();
	if (pView == NULL || pView->GetPatternEditor() == NULL || !m_pMainFrame->IsPatternEditorShown())
		return std::string();
	const CPatternEditor *pEditor = pView->GetPatternEditor();
	CString Line;
	std::string Id;
	if (!GlossaryEffectLine(m_pMainFrame->GetSelectedTrack(), pEditor->GetFrame(), pEditor->GetRow(), pEditor->GetChannel(),
		pEditor->GetColumn(), Line, Id))
		return std::string();
	return Id;
}

// The tip for what's under a point of the screen, in the window it's in
bool CGlossaryTips::TipAt(CPoint Screen, HWND &hWnd, glossary_tip_t &Tip)
{
	HWND hUnder = ::WindowFromPoint(Screen);
	if (hUnder == NULL || ::GetAncestor(hUnder, GA_ROOTOWNER) != m_pMainFrame->GetSafeHwnd())
		return false;
	CPoint Point = Screen;
	::ScreenToClient(hUnder, &Point);
	hWnd = hUnder;
	m_bCell = false;
	// A window that draws its own parts
	for (size_t i = 0; i < m_Areas.size(); ++i)
		if (m_Areas[i].hWnd == hUnder)
			return m_Areas[i].HitTest(Point, Tip);
	// An effect in the pattern editor
	CFamiTrackerView *pView = CFamiTrackerView::GetView();
	if (pView != NULL && pView->GetSafeHwnd() == hUnder && pView->GetPatternEditor() != NULL) {
		int Frame = 0, Row = 0, Channel = 0, Column = 0;
		if (!pView->GetPatternEditor()->GetCellAtPoint(Point, Frame, Row, Channel, Column))
			return false;
		CString Line;
		if (!GlossaryEffectLine(m_pMainFrame->GetSelectedTrack(), Frame, Row, Channel, Column, Line, Tip.Id))
			return false;
		Tip.Text = Line + _T("\n") + MoreHint(false, true);
		Tip.Key = ((Frame * 256 + Row) * 64 + Channel) * 16 + Column;
		m_bCell = true;
		m_iCellFrame = Frame;
		m_iCellRow = Row;
		m_iCellChannel = Channel;
		m_iCellColumn = Column;
		return true;
	}
	TCHAR Class[32] = { 0 };
	::GetClassName(hUnder, Class, 32);
	// A sequence's name in the instrument editor's list (its third column)
	if (_tcsicmp(Class, WC_LISTVIEW) == 0 && ::GetDlgCtrlID(hUnder) == IDC_INSTSETTINGS) {
		LVHITTESTINFO Hit;
		memset(&Hit, 0, sizeof(Hit));
		Hit.pt = Point;
		const int Item = ListView_HitTest(hUnder, &Hit);
		if (Item < 0)
			return false;
		TCHAR Text[64] = { 0 };
		ListView_GetItemText(hUnder, Item, 2, Text, 64);
		return GlossaryLabelTip("instrument editor", Text, Item, Tip);
	}
	// A song setting's label
	for (size_t i = 0; i < m_Labels.size(); ++i)
		if (m_Labels[i] == hUnder) {
			TCHAR Text[64] = { 0 };
			::GetWindowText(hUnder, Text, 64);
			return GlossaryLabelTip("song settings", Text, static_cast<int>(i), Tip, true);
		}
	return false;
}

void CGlossaryTips::Show(const glossary_tip_t &Tip, CPoint Screen)
{
	TOOLINFO Info;
	memset(&Info, 0, sizeof(Info));
	Info.cbSize = sizeof(Info);
	Info.hwnd = m_pMainFrame->GetSafeHwnd();
	Info.uId = 1;
	CString Text = Tip.Text;
	Info.lpszText = Text.GetBuffer();
	m_Tip.SendMessage(TTM_UPDATETIPTEXT, 0, reinterpret_cast<LPARAM>(&Info));
	Text.ReleaseBuffer();
	// Under and to the right of the mouse, inside the screen it's on
	const DWORD Size = static_cast<DWORD>(m_Tip.SendMessage(TTM_GETBUBBLESIZE, 0, reinterpret_cast<LPARAM>(&Info)));
	const int Width = LOWORD(Size), Height = HIWORD(Size);
	CRect Screens(0, 0, ::GetSystemMetrics(SM_CXSCREEN), ::GetSystemMetrics(SM_CYSCREEN));
	MONITORINFO Monitor;
	Monitor.cbSize = sizeof(Monitor);
	if (::GetMonitorInfo(::MonitorFromPoint(Screen, MONITOR_DEFAULTTONEAREST), &Monitor))
		Screens = Monitor.rcWork;
	int x = Screen.x + SX(12), y = Screen.y + SY(22);
	if (x + Width > Screens.right)
		x = std::max(static_cast<int>(Screens.left), static_cast<int>(Screens.right) - Width);
	if (y + Height > Screens.bottom)
		y = std::max(static_cast<int>(Screens.top), Screen.y - SY(8) - Height);
	m_Tip.SendMessage(TTM_TRACKPOSITION, 0, MAKELPARAM(x, y));
	m_Tip.SendMessage(TTM_TRACKACTIVATE, TRUE, reinterpret_cast<LPARAM>(&Info));
	m_bShown = true;
	m_Shown = Tip;
}

void CGlossaryTips::Hide()
{
	if (!m_bShown)
		return;
	TOOLINFO Info;
	memset(&Info, 0, sizeof(Info));
	Info.cbSize = sizeof(Info);
	Info.hwnd = m_pMainFrame->GetSafeHwnd();
	Info.uId = 1;
	m_Tip.SendMessage(TTM_TRACKACTIVATE, FALSE, reinterpret_cast<LPARAM>(&Info));
	m_bShown = false;
	m_hShownWnd = NULL;
	m_bCell = false;
}

// The status bar says what the effect under the pattern editor's cursor does, when the cursor gets to it (not while the
// song plays, when the cursor follows it)
void CGlossaryTips::ShowCursorEffect()
{
	CFamiTrackerView *pView = CFamiTrackerView::GetView();
	CStatusBar *pBar = static_cast<CStatusBar*>(m_pMainFrame->GetMessageBar());
	if (pView == NULL || pView->GetPatternEditor() == NULL || pBar == NULL || theApp.IsPlaying())
		return;
	const CPatternEditor *pEditor = pView->GetPatternEditor();
	const int Track = m_pMainFrame->GetSelectedTrack();
	const int Frame = pEditor->GetFrame(), Row = pEditor->GetRow(), Channel = pEditor->GetChannel(), Column = pEditor->GetColumn();
	CString Line;
	std::string Id;
	const bool Effect = m_pMainFrame->IsPatternEditorShown() && GlossaryEffectLine(Track, Frame, Row, Channel, Column, Line, Id);
	if (!Effect) {
		// Gone from the effect: the line goes, unless something else has said something since
		if (m_iStatusKey >= 0) {
			CString Now;
			pBar->GetPaneText(0, Now);
			if (Now == m_strStatus)
				m_pMainFrame->SetMessageText(AFX_IDS_IDLEMESSAGE);
			m_iStatusKey = -1;
		}
		return;
	}
	const CString Text = Line + _T(". ") + MoreHint(false, true);
	const long long Key = (((static_cast<long long>(Track) * 256 + Frame) * 256 + Row) * 64 + Channel) * 16 + Column;
	if (Key == m_iStatusKey && Text == m_strStatus)
		return;
	m_iStatusKey = Key;
	m_strStatus = Text;
	m_pMainFrame->SetMessageText(Text);
}

void CGlossaryTips::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent != TMR_POLL) {
		CWnd::OnTimer(nIDEvent);
		return;
	}
	if (!m_bRestored && m_pMainFrame->IsWindowVisible()) {
		m_bRestored = true;
		RestoreGlossary(m_pMainFrame);
	}
	ShowCursorEffect();
	// Tips only while FamiTracker is the program in front, and the mouse isn't held down
	HWND hFront = ::GetForegroundWindow();
	const bool Front = hFront != NULL && ::GetAncestor(hFront, GA_ROOTOWNER) == m_pMainFrame->GetSafeHwnd();
	const bool Pressed = (::GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0 || (::GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
	CPoint Mouse;
	::GetCursorPos(&Mouse);
	if (!Front || Pressed) {
		Hide();
		m_iStill = 0;
		m_Mouse = Mouse;
		return;
	}
	// Windows that went away
	for (size_t i = 0; i < m_Areas.size(); )
		if (!::IsWindow(m_Areas[i].hWnd))
			m_Areas.erase(m_Areas.begin() + static_cast<std::ptrdiff_t>(i));
		else
			++i;
	const bool Moved = Mouse != m_Mouse;
	m_Mouse = Mouse;
	m_iStill = Moved ? 0 : m_iStill + 1;
	if (m_bShown) {
		// It stays while the mouse is on the same part; its words follow an edit there
		HWND hWnd = NULL;
		glossary_tip_t Tip;
		const bool Cell = m_bCell;
		const int CellFrame = m_iCellFrame, CellRow = m_iCellRow, CellChannel = m_iCellChannel, CellColumn = m_iCellColumn;
		if (!TipAt(Mouse, hWnd, Tip) || hWnd != m_hShownWnd || Tip.Key != m_Shown.Key) {
			Hide();
			m_iStill = 0;
		}
		else if (Tip.Text != m_Shown.Text || Tip.Id != m_Shown.Id) {
			const HWND hShown = m_hShownWnd;
			Show(Tip, Mouse);
			m_hShownWnd = hShown;
		}
		else {
			m_bCell = Cell;
			m_iCellFrame = CellFrame;
			m_iCellRow = CellRow;
			m_iCellChannel = CellChannel;
			m_iCellColumn = CellColumn;
		}
		return;
	}
	if (m_iStill != REST_TICKS)
		return;
	HWND hWnd = NULL;
	glossary_tip_t Tip;
	if (TipAt(Mouse, hWnd, Tip) && !Tip.Text.IsEmpty()) {
		const bool Cell = m_bCell;
		Show(Tip, Mouse);
		m_hShownWnd = hWnd;
		m_bCell = Cell;
	}
}

}	// namespace

void StartGlossaryHelp(CMainFrame *pMainFrame)
{
	if (g_pTips != NULL)
		return;
	g_pTips = new CGlossaryTips();
	if (!g_pTips->Start(pMainFrame)) {
		g_pTips->Stop();
		delete g_pTips;
		g_pTips = NULL;
	}
}

void StopGlossaryHelp()
{
	CloseGlossary();
	CloseGlossaryPlayer();
	if (g_pTips != NULL) {
		g_pTips->Stop();
		delete g_pTips;
		g_pTips = NULL;
	}
}

void AddGlossaryTipArea(CWnd *pWnd, const glossary_hit_t &HitTest)
{
	if (g_pTips != NULL)
		g_pTips->AddArea(pWnd, HitTest);
}

bool GlossaryLabelTip(const char *Where, LPCTSTR Label, int Key, glossary_tip_t &Tip, bool Clickable)
{
	CString Plain(Label);
	Plain.Remove(_T('&'));
	const std::string Id = ftapi::GlossaryLabelId(Where, static_cast<LPCTSTR>(Plain));
	const ftapi::GlossaryEntry *pEntry = Id.empty() ? NULL : ftapi::FindGlossaryEntry(Id);
	if (pEntry == NULL)
		return false;
	Tip.Id = Id;
	Tip.Key = Key;
	Tip.Text = CString(ftapi::GlossaryFirstSentence(*pEntry).c_str()) + _T("\n") + MoreHint(Clickable, false);
	return true;
}

bool GlossaryHelpKey(CMainFrame *pMainFrame)
{
	if (g_pTips == NULL)
		return false;
	std::string Id;
	if (!g_pTips->ShownEntry(Id)) {
		CFamiTrackerView *pView = CFamiTrackerView::GetView();
		if (pView == NULL || CWnd::GetFocus() != pView)
			return false;
		Id = g_pTips->CursorEntry();
		if (Id.empty())
			return false;
	}
	ShowGlossaryAt(pMainFrame, Id);
	return true;
}

bool GlossaryTipCell(int &Frame, int &Row, int &Channel, int &Column)
{
	return g_pTips != NULL && g_pTips->ShownCell(Frame, Row, Channel, Column);
}

bool GlossaryEffectLine(int Track, int Frame, int Row, int Channel, int Column, CString &Line, std::string &Id)
{
	CFamiTrackerDoc *pDoc = CFamiTrackerDoc::GetDoc();
	if (pDoc == NULL || !pDoc->IsFileLoaded() || Track < 0 || Frame < 0 || Row < 0 || Channel < 0 ||
		Channel >= static_cast<int>(pDoc->GetAvailableChannels()) || Frame >= static_cast<int>(pDoc->GetFrameCount(Track)) ||
		Row >= static_cast<int>(pDoc->GetPatternLength(Track)) || Column < 0 || Column > 15)
		return false;
	const int Index = CPatternEditor::GetSelectColumn(Column) - COLUMN_EFF1;
	if (Index < 0 || Index > static_cast<int>(pDoc->GetEffColumns(Track, Channel)))
		return false;
	stChanNote Cell;
	pDoc->GetNoteData(Track, Frame, Channel, Row, &Cell);
	const int Effect = Cell.EffNumber[Index];
	if (Effect <= EF_NONE || Effect >= EF_COUNT || EFF_CHAR[Effect - 1] == 0)
		return false;
	const ftapi::ChannelInfo Info = GlossaryChannelInfo(pDoc, Channel);
	const std::string Said = ftapi::DescribeEffect(Effect, Cell.EffParam[Index], Info, pDoc->GetSpeedSplitPoint());
	if (Said.empty())
		return false;
	Line.Format(_T("%c%02X  %s"), EFF_CHAR[Effect - 1], Cell.EffParam[Index], static_cast<LPCTSTR>(CString(Said.c_str())));
	Id = ftapi::EffectGlossaryId(Effect, Info);
	return true;
}

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

#include "stdafx.h"
#include <afxpriv.h>		// WM_SIZEPARENT, for laying out the docked bar
#include <algorithm>
#include "FamiTracker.h"
#include "MainFrm.h"
#include "PanelBar.h"

namespace {

const int DIVIDER = 5;
const int STRIP_HEIGHT = 22;			// the tabs
const int TAB_PAD = 10;					// beside a tab's name
const int TAB_GAP = 2;
const int TAB_LEAST = 46;
const int MARK_SIZE = 7;				// a tab's dot
const int MIN_VIEW_WIDTH = 300;			// what the note views keep at least beside a panel on the right
const UINT_PTR TMR_TABS = 0x5154;		// the marks looked at again
const UINT TABS_MS = 500;
// Its own window's system menu (a right-click on its title)
const UINT SC_TAB_BOTTOM = 0x0110;
const UINT SC_TAB_RIGHT = 0x0120;

std::vector<CPanelBar*> &AllBars()
{
	static std::vector<CPanelBar*> Bars;
	return Bars;
}

}	// namespace

// ---- Its own window

BEGIN_MESSAGE_MAP(CPanelWindow, CWnd)
	ON_WM_SIZE()
	ON_WM_CLOSE()
	ON_WM_SETFOCUS()
	ON_WM_SYSCOMMAND()
	ON_WM_EXITSIZEMOVE()
	ON_WM_ERASEBKGND()
	ON_WM_DESTROY()
END_MESSAGE_MAP()

CPanelWindow::CPanelWindow(CPanelBar *pBar) : m_pBar(pBar)
{
}

CPanelWindow::~CPanelWindow()
{
}

bool CPanelWindow::Make(CWnd *pOwner, LPCTSTR Title, const CRect &Rect)
{
	// A tool window owned by the main window: above it, minimized with it, no button on the taskbar
	const CString Class = AfxRegisterWndClass(0, ::LoadCursor(NULL, IDC_ARROW), ::GetSysColorBrush(COLOR_3DFACE), NULL);
	if (!CreateEx(WS_EX_TOOLWINDOW, Class, Title, WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_CLIPCHILDREN,
		Rect, pOwner, 0))
		return false;
	CMenu *pSystem = GetSystemMenu(FALSE);
	if (pSystem != NULL) {
		pSystem->AppendMenu(MF_SEPARATOR, 0, (LPCTSTR)NULL);
		pSystem->AppendMenu(MF_STRING, SC_TAB_BOTTOM, _T("Tab at the bottom"));
		pSystem->AppendMenu(MF_STRING, SC_TAB_RIGHT, _T("Tab on the right"));
	}
	return true;
}

void CPanelWindow::SavePlace()
{
	if (GetSafeHwnd() == NULL)
		return;
	WINDOWPLACEMENT Placement;
	Placement.length = sizeof(Placement);
	if (!GetWindowPlacement(&Placement))
		return;
	const CRect Rect(Placement.rcNormalPosition);
	if (Rect.Width() <= 0 || Rect.Height() <= 0)
		return;
	theApp.WriteProfileInt(m_pBar->m_pSection, _T("Window left"), Rect.left);
	theApp.WriteProfileInt(m_pBar->m_pSection, _T("Window top"), Rect.top);
	theApp.WriteProfileInt(m_pBar->m_pSection, _T("Window width"), Rect.Width());
	theApp.WriteProfileInt(m_pBar->m_pSection, _T("Window height"), Rect.Height());
}

void CPanelWindow::OnSize(UINT nType, int cx, int cy)
{
	CWnd::OnSize(nType, cx, cy);
	CWnd *pPanel = m_pBar->Panel();
	if (pPanel != NULL && pPanel->GetSafeHwnd() != NULL && pPanel->GetParent() == this)
		pPanel->MoveWindow(0, 0, cx, cy);
}

void CPanelWindow::OnClose()
{
	// Closed as View's item closes it: it stays a window of its own for next time
	m_pBar->ClosedOwnWindow();
}

void CPanelWindow::OnSetFocus(CWnd *pOldWnd)
{
	// Activated (a click on its title): the keys go to the panel
	CWnd *pPanel = m_pBar->Panel();
	if (pPanel != NULL && pPanel->GetSafeHwnd() != NULL && pPanel->GetParent() == this)
		pPanel->SetFocus();
	else
		CWnd::OnSetFocus(pOldWnd);
}

void CPanelWindow::OnSysCommand(UINT nID, LPARAM lParam)
{
	switch (nID & 0xFFF0) {
	case SC_TAB_BOTTOM:
		m_pBar->PickPlace(PANEL_BOTTOM, 0);
		return;
	case SC_TAB_RIGHT:
		m_pBar->PickPlace(PANEL_RIGHT, 0);
		return;
	}
	CWnd::OnSysCommand(nID, lParam);
}

void CPanelWindow::OnExitSizeMove()
{
	SavePlace();
	CWnd::OnExitSizeMove();
}

BOOL CPanelWindow::OnEraseBkgnd(CDC *pDC)
{
	return TRUE;	// the panel covers it
}

void CPanelWindow::OnDestroy()
{
	// Its panel goes back into the bar first, so it lives as long as the bar does (FamiTracker closing destroys
	// the windows the main window owns before the main window itself)
	SavePlace();
	CWnd *pPanel = m_pBar->Panel();
	if (pPanel != NULL && pPanel->GetSafeHwnd() != NULL && pPanel->GetParent() == this && m_pBar->GetSafeHwnd() != NULL) {
		pPanel->ShowWindow(SW_HIDE);
		pPanel->SetParent(m_pBar);
	}
	CWnd::OnDestroy();
}

// ---- The bar

BEGIN_MESSAGE_MAP(CPanelBar, CControlBar)
	ON_WM_SIZE()
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_MOUSEMOVE()
	ON_WM_RBUTTONUP()
	ON_WM_SETCURSOR()
	ON_WM_CAPTURECHANGED()
	ON_WM_TIMER()
	ON_WM_DESTROY()
	ON_MESSAGE(WM_SIZEPARENT, OnSizeParent)
END_MESSAGE_MAP()

CPanelBar::CPanelBar(int Panel, LPCTSTR Section, LPCTSTR Title, int DefaultHeight, int DefaultWidth, int MinHeight,
	int MinWidth) :
	m_iPanel(Panel), m_pSection(Section), m_pTitle(Title), m_iMinHeight(MinHeight), m_iMinWidth(MinWidth), m_pPanel(NULL),
	m_wndOwn(this), m_Place(PANEL_BOTTOM), m_bOpen(false), m_iHeight(DefaultHeight), m_iWidth(DefaultWidth), m_iRoom(0),
	m_iDragFrom(-1), m_iDragSize(0), m_iDragMax(0)
{
	AllBars().push_back(this);
}

CPanelBar::~CPanelBar()
{
	std::vector<CPanelBar*> &Bars = AllBars();
	Bars.erase(std::remove(Bars.begin(), Bars.end(), this), Bars.end());
}

CPanelBar *CPanelBar::Of(const CWnd *pPanel)
{
	const std::vector<CPanelBar*> &Bars = AllBars();
	for (size_t i = 0; pPanel != NULL && i < Bars.size(); ++i)
		if (Bars[i]->m_pPanel == pPanel || (Bars[i]->m_pPanel != NULL && pPanel->GetSafeHwnd() != NULL &&
			Bars[i]->m_pPanel->GetSafeHwnd() == pPanel->GetSafeHwnd()))
			return Bars[i];
	return NULL;
}

bool CPanelBar::CreateBar(CFrameWnd *pFrame, UINT nID)
{
	const bool HasPlace = theApp.GetProfileInt(m_pSection, _T("Place"), -1) != static_cast<UINT>(-1);
	m_Place = StoredPanelPlace(static_cast<int>(theApp.GetProfileInt(m_pSection, _T("Place"), 0)), HasPlace,
		theApp.GetProfileInt(m_pSection, _T("On the right"), 0) != 0);
	m_iHeight = std::max(SY(m_iMinHeight), static_cast<int>(theApp.GetProfileInt(m_pSection, _T("Height"), SY(m_iHeight))));
	m_iWidth = std::max(SX(m_iMinWidth), static_cast<int>(theApp.GetProfileInt(m_pSection, _T("Width"), SX(m_iWidth))));
	m_dwStyle = m_Place == PANEL_RIGHT ? CBRS_ALIGN_RIGHT : CBRS_ALIGN_BOTTOM;
	LOGFONT Log;
	memset(&Log, 0, sizeof(Log));
	::GetObject(::GetStockObject(DEFAULT_GUI_FONT), sizeof(Log), &Log);
	_tcscpy_s(Log.lfFaceName, LF_FACESIZE, _T("Segoe UI"));
	Log.lfHeight = -SY(12);
	m_Font.CreateFontIndirect(&Log);
	Log.lfWeight = FW_BOLD;
	m_BoldFont.CreateFontIndirect(&Log);
	const CString Class = AfxRegisterWndClass(0, ::LoadCursor(NULL, IDC_ARROW), NULL, NULL);
	if (!CWnd::Create(Class, m_pTitle, WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, CRect(0, 0, 0, 0), pFrame, nID))
		return false;
	SetTimer(TMR_TABS, TABS_MS, NULL);
	return true;
}

void CPanelBar::SetPanel(CWnd *pPanel)
{
	m_pPanel = pPanel;
	PlacePanel();
}

CWnd *CPanelBar::ShownWnd()
{
	if (m_pPanel != NULL && m_wndOwn.GetSafeHwnd() != NULL && m_pPanel->GetParent() == &m_wndOwn)
		return &m_wndOwn;
	return this;
}

void CPanelBar::GiveKeysBack()
{
	const HWND Focus = ::GetFocus();
	if (m_pPanel == NULL || m_pPanel->GetSafeHwnd() == NULL || Focus == NULL)
		return;
	if (Focus != m_pPanel->GetSafeHwnd() && !::IsChild(m_pPanel->GetSafeHwnd(), Focus))
		return;
	CFrameWnd *pFrame = GetParentFrame();
	CView *pView = pFrame != NULL ? pFrame->GetActiveView() : NULL;
	if (pView != NULL)
		pView->SetFocus();		// the piano roll's when the pattern editor is hidden (CFamiTrackerView::OnSetFocus)
}

void CPanelBar::Opened(bool Open)
{
	if (!Open)
		GiveKeysBack();
	m_bOpen = Open;
	PanelOpened(Open);
	theApp.WriteProfileInt(m_pSection, _T("Open"), Open ? 1 : 0);
}

void CPanelBar::SetPlace(panel_place_t Place)
{
	if (Place == m_Place || Place < PANEL_BOTTOM || Place >= PANEL_PLACES)
		return;
	m_Place = Place;
	m_dwStyle = (m_dwStyle & ~CBRS_ALIGN_ANY) | (Place == PANEL_RIGHT ? CBRS_ALIGN_RIGHT : CBRS_ALIGN_BOTTOM);
	theApp.WriteProfileInt(m_pSection, _T("Place"), static_cast<int>(Place));
	theApp.WriteProfileInt(m_pSection, _T("On the right"), Place == PANEL_RIGHT ? 1 : 0);	// for older builds
	PlacePanel();
	Invalidate();
}

CRect CPanelBar::WindowRect() const
{
	// As remembered; the first time beside the main window's right edge, as big as it was docked
	CRect Main;
	const CWnd *pMain = GetParentFrame();
	if (pMain != NULL)
		pMain->GetWindowRect(&Main);
	const int Width = std::max(SX(m_iMinWidth), static_cast<int>(theApp.GetProfileInt(m_pSection, _T("Window width"),
		static_cast<UINT>(std::max(m_iWidth, SX(320))))));
	const int Height = std::max(SY(m_iMinHeight), static_cast<int>(theApp.GetProfileInt(m_pSection, _T("Window height"),
		static_cast<UINT>(std::max(m_iHeight, SY(320))))));
	const int DefaultLeft = Main.right - Width - SX(40), DefaultTop = Main.top + SY(160);
	const int Left = static_cast<int>(theApp.GetProfileInt(m_pSection, _T("Window left"), static_cast<UINT>(DefaultLeft)));
	const int Top = static_cast<int>(theApp.GetProfileInt(m_pSection, _T("Window top"), static_cast<UINT>(DefaultTop)));
	CRect Wanted(Left, Top, Left + Width, Top + Height);
	// On a screen still (a monitor taken away since)
	if (::MonitorFromRect(&Wanted, MONITOR_DEFAULTTONULL) == NULL)
		Wanted.OffsetRect(DefaultLeft - Wanted.left, DefaultTop - Wanted.top);
	return Wanted;
}

void CPanelBar::Appear(bool Docked, bool OwnWindow)
{
	if (m_pPanel == NULL || m_pPanel->GetSafeHwnd() == NULL)
		return;
	if (!Docked && !OwnWindow)
		GiveKeysBack();
	CFrameWnd *pFrame = GetParentFrame();
	if (OwnWindow) {
		if (m_wndOwn.GetSafeHwnd() == NULL && !m_wndOwn.Make(pFrame, m_pTitle, WindowRect()))
			OwnWindow = false;
	}
	if (OwnWindow) {
		if (m_pPanel->GetParent() != &m_wndOwn) {
			m_pPanel->SetParent(&m_wndOwn);
			CRect Client;
			m_wndOwn.GetClientRect(&Client);
			m_pPanel->MoveWindow(0, 0, Client.Width(), Client.Height());
			m_pPanel->ShowWindow(SW_SHOW);
		}
		if (!m_wndOwn.IsWindowVisible())
			m_wndOwn.ShowWindow(SW_SHOWNOACTIVATE);		// the keys stay where they are (the note views)
	}
	else {
		if (m_pPanel->GetParent() != this) {
			m_pPanel->SetParent(this);
			m_pPanel->ShowWindow(SW_SHOW);
			PlacePanel();
		}
		if (m_wndOwn.GetSafeHwnd() != NULL && m_wndOwn.IsWindowVisible()) {
			m_wndOwn.SavePlace();
			m_wndOwn.ShowWindow(SW_HIDE);
		}
	}
	const bool Shown = (GetStyle() & WS_VISIBLE) != 0;
	if (pFrame != NULL && Shown != Docked)
		pFrame->ShowControlBar(this, Docked ? TRUE : FALSE, FALSE);
	if (Docked)
		TabsChanged();
}

void CPanelBar::ClosedOwnWindow()
{
	CMainFrame *pMain = dynamic_cast<CMainFrame*>(GetParentFrame());
	if (pMain != NULL)
		pMain->ShowPanel(m_iPanel, false);
}

void CPanelBar::TabsChanged()
{
	m_strTabs.Empty();
	if (GetSafeHwnd() != NULL)
		InvalidateRect(StripRect(), FALSE);
}

int CPanelBar::Divider() const
{
	return std::max(4, m_Place == PANEL_RIGHT ? SX(DIVIDER) : SY(DIVIDER));
}

int CPanelBar::StripHeight() const
{
	return SY(STRIP_HEIGHT);
}

CRect CPanelBar::StripRect() const
{
	CRect Client;
	GetClientRect(&Client);
	if (m_Place == PANEL_RIGHT)
		return CRect(Client.left + Divider(), Client.top, Client.right, Client.top + StripHeight());
	return CRect(Client.left, Client.top + Divider(), Client.right, Client.top + Divider() + StripHeight());
}

int CPanelBar::LeastHeight(int Width) const
{
	return std::max(SY(m_iMinHeight), PanelLeast(Width)) + Divider() + StripHeight();
}

void CPanelBar::SetBottomHeight(int Height)
{
	m_iHeight = std::max(SY(m_iMinHeight), Height);
}

void CPanelBar::SetRightWidth(int Width)
{
	m_iWidth = std::max(SX(m_iMinWidth), Width);
}

int CPanelBar::MaxSize() const
{
	if (m_Place == PANEL_RIGHT)
		return std::max(SX(m_iMinWidth), m_iRoom - SX(MIN_VIEW_WIDTH));
	// As tall as the note views and the mixer leave it (CMainFrame::PlaceBottomPanel)
	const CMainFrame *pMain = dynamic_cast<const CMainFrame*>(GetParentFrame());
	if (pMain != NULL)
		return std::max(SY(m_iMinHeight), pMain->BottomPanelMost(this));
	return std::max(SY(m_iMinHeight), m_iRoom / 2);
}

LRESULT CPanelBar::OnSizeParent(WPARAM wParam, LPARAM lParam)
{
	AFX_SIZEPARENTPARAMS *pLayout = reinterpret_cast<AFX_SIZEPARENTPARAMS*>(lParam);
	const bool Right = m_Place == PANEL_RIGHT;
	m_iRoom = Right ? pLayout->rect.right - pLayout->rect.left : pLayout->rect.bottom - pLayout->rect.top;
	// Along the bottom the main window places it, above the mixer
	CMainFrame *pMain = dynamic_cast<CMainFrame*>(GetParentFrame());
	if (m_Place == PANEL_BOTTOM && pMain != NULL && pMain->PlaceBottomPanel(this, pLayout)) {
		RecalcDelayShow(pLayout);	// a show the layout was to make, made
		return 0;
	}
	return CControlBar::OnSizeParent(wParam, lParam);
}

CSize CPanelBar::CalcFixedLayout(BOOL bStretch, BOOL bHorz)
{
	const int Shown = std::max(0, std::min(Size(), MaxSize()));
	return m_Place == PANEL_RIGHT ? CSize(Shown, 32767) : CSize(32767, Shown);
}

void CPanelBar::OnUpdateCmdUI(CFrameWnd *pTarget, BOOL bDisableIfNoHndler)
{
}

BOOL CPanelBar::PreTranslateMessage(MSG *pMsg)
{
	// Not as a dialog (as control bars are): the panel sends its keys where they belong
	return CWnd::PreTranslateMessage(pMsg);
}

void CPanelBar::PlacePanel()
{
	if (GetSafeHwnd() == NULL || m_pPanel == NULL || m_pPanel->GetSafeHwnd() == NULL || m_pPanel->GetParent() != this)
		return;
	CRect Client;
	GetClientRect(&Client);
	if (m_Place == PANEL_RIGHT)
		m_pPanel->MoveWindow(Divider(), StripHeight(), std::max(0, Client.Width() - Divider()),
			std::max(0, Client.Height() - StripHeight()));
	else {
		const int Top = Divider() + StripHeight();
		m_pPanel->MoveWindow(0, Top, Client.Width(), std::max(0, Client.Height() - Top));
	}
}

void CPanelBar::OnSize(UINT nType, int cx, int cy)
{
	CControlBar::OnSize(nType, cx, cy);
	PlacePanel();
	Invalidate(FALSE);
}

CString CPanelBar::TabText(const CPanelBar *pBar) const
{
	CString Text(pBar->Title());
	const int News = pBar->News();
	if (News > 0) {
		CString Count;
		Count.Format(_T(" (%d)"), News);
		Text += Count;
	}
	return Text;
}

CString CPanelBar::TabsSignature() const
{
	CString Signature;
	const CMainFrame *pMain = dynamic_cast<const CMainFrame*>(GetParentFrame());
	if (pMain == NULL)
		return Signature;
	const std::vector<CPanelBar*> Tabs = pMain->PanelTabs(m_Place);
	for (size_t i = 0; i < Tabs.size(); ++i) {
		CString One;
		One.Format(_T("%d:%s:%d:%d|"), Tabs[i]->PanelIndex(), static_cast<LPCTSTR>(TabText(Tabs[i])),
			Tabs[i]->News() == PANEL_NEWS_MARK ? 1 : 0, pMain->SelectedPanel(m_Place) == Tabs[i]->PanelIndex() ? 1 : 0);
		Signature += One;
	}
	return Signature;
}

void CPanelBar::LayOutTabs(CDC &dc)
{
	m_Tabs.clear();
	const CMainFrame *pMain = dynamic_cast<const CMainFrame*>(GetParentFrame());
	if (pMain == NULL)
		return;
	const std::vector<CPanelBar*> Bars = pMain->PanelTabs(m_Place);
	const CRect Strip = StripRect();
	std::vector<int> Wanted;
	for (size_t i = 0; i < Bars.size(); ++i) {
		CFont *pOld = dc.SelectObject(Bars[i] == this ? &m_BoldFont : &m_Font);
		const CSize Text = dc.GetTextExtent(TabText(Bars[i]));
		dc.SelectObject(pOld);
		Wanted.push_back(Text.cx + 2 * SX(TAB_PAD) + (Bars[i]->News() == PANEL_NEWS_MARK ? SX(MARK_SIZE + 4) : 0));
	}
	const int Left = Strip.left + SX(4);
	const int Room = Strip.Width() - SX(8) - static_cast<int>(Bars.size()) * SX(TAB_GAP);
	const std::vector<int> Widths = TabWidths(Wanted, Room, SX(TAB_LEAST));
	int x = Left;
	for (size_t i = 0; i < Bars.size(); ++i) {
		tab_t Tab;
		Tab.pBar = Bars[i];
		Tab.Rect = CRect(x, Strip.top + SY(3), x + Widths[i], Strip.bottom);
		m_Tabs.push_back(Tab);
		x += Widths[i] + SX(TAB_GAP);
	}
}

void CPanelBar::OnPaint()
{
	CPaintDC dc(this);
	CRect Rect;
	GetClientRect(&Rect);
	const int Line = Divider();
	const COLORREF Face = ::GetSysColor(COLOR_3DFACE), Light = ::GetSysColor(COLOR_3DHILIGHT), Shadow = ::GetSysColor(COLOR_3DSHADOW);
	// The line to drag
	if (m_Place == PANEL_RIGHT) {
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
	// The tabs: the one shown joins the panel under it
	const CRect Strip = StripRect();
	dc.FillSolidRect(&Strip, Face);
	dc.FillSolidRect(Strip.left, Strip.bottom - 1, Strip.Width(), 1, Shadow);
	LayOutTabs(dc);
	m_strTabs = TabsSignature();
	dc.SetBkMode(TRANSPARENT);
	for (size_t i = 0; i < m_Tabs.size(); ++i) {
		const CRect &Tab = m_Tabs[i].Rect;
		const bool Shown = m_Tabs[i].pBar == this;
		if (Shown) {
			dc.FillSolidRect(Tab.left, Tab.top, Tab.Width(), Tab.Height(), ::GetSysColor(COLOR_WINDOW));
			dc.FillSolidRect(Tab.left, Tab.top, Tab.Width(), 1, Shadow);
			dc.FillSolidRect(Tab.left, Tab.top, 1, Tab.Height(), Shadow);
			dc.FillSolidRect(Tab.right - 1, Tab.top, 1, Tab.Height(), Shadow);
			dc.FillSolidRect(Tab.left + 1, Tab.top + 1, Tab.Width() - 2, SY(2), ::GetSysColor(COLOR_HIGHLIGHT));
		}
		else
			dc.FillSolidRect(Tab.right - 1, Tab.top + SY(4), 1, Tab.Height() - SY(8), Shadow);
		const int News = m_Tabs[i].pBar->News();
		CRect Text(Tab);
		Text.DeflateRect(SX(TAB_PAD), 0);
		if (News == PANEL_NEWS_MARK)
			Text.right -= SX(MARK_SIZE + 4);
		CFont *pOld = dc.SelectObject(Shown ? &m_BoldFont : &m_Font);
		dc.SetTextColor(::GetSysColor(Shown ? COLOR_WINDOWTEXT : COLOR_BTNTEXT));
		dc.DrawText(TabText(m_Tabs[i].pBar), &Text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
		dc.SelectObject(pOld);
		if (News == PANEL_NEWS_MARK) {
			// Something new in it: a dot after its name
			const int Size = SX(MARK_SIZE);
			const int x = Text.right + SX(4), y = Tab.top + (Tab.Height() - Size) / 2;
			CBrush Brush(RGB(213, 94, 0));	// Okabe and Ito's vermillion, as the agent log's colors
			CPen Pen(PS_SOLID, 1, RGB(213, 94, 0));
			CBrush *pOldBrush = dc.SelectObject(&Brush);
			CPen *pOldPen = dc.SelectObject(&Pen);
			dc.Ellipse(x, y, x + Size, y + Size);
			dc.SelectObject(pOldBrush);
			dc.SelectObject(pOldPen);
		}
	}
	// The panel under the strip paints itself; along the right, under the line's top end nothing else is left
}

BOOL CPanelBar::OnEraseBkgnd(CDC *pDC)
{
	return TRUE;
}

int CPanelBar::TabAtPoint(CPoint Point) const
{
	for (size_t i = 0; i < m_Tabs.size(); ++i)
		if (m_Tabs[i].Rect.PtInRect(Point))
			return m_Tabs[i].pBar->PanelIndex();
	return NO_PANEL;
}

void CPanelBar::OnLButtonDown(UINT nFlags, CPoint point)
{
	if (StripRect().PtInRect(point)) {
		const int Panel = TabAtPoint(point);
		CMainFrame *pMain = dynamic_cast<CMainFrame*>(GetParentFrame());
		if (Panel != NO_PANEL && Panel != m_iPanel && pMain != NULL)
			pMain->SelectPanelTab(Panel);
		return;
	}
	const bool Right = m_Place == PANEL_RIGHT;
	if ((Right ? point.x : point.y) >= Divider())
		return;
	CPoint Screen = point;
	ClientToScreen(&Screen);
	m_iDragFrom = static_cast<int>(Right ? Screen.x : Screen.y);
	CRect Shown;
	GetWindowRect(&Shown);
	m_iDragSize = Right ? Size() : Shown.Height();	// along the bottom as it shows, shorter while room is short
	m_iDragMax = MaxSize();
	SetCapture();
}

void CPanelBar::OnMouseMove(UINT nFlags, CPoint point)
{
	if (m_iDragFrom < 0)
		return;
	const bool Right = m_Place == PANEL_RIGHT;
	CPoint Screen = point;
	ClientToScreen(&Screen);
	const int Moved = static_cast<int>(Right ? Screen.x : Screen.y) - m_iDragFrom;
	const int Least = Right ? SX(m_iMinWidth) : SY(m_iMinHeight);
	const int New = std::max(Least, std::min(m_iDragMax, m_iDragSize - Moved));
	if (New != Size()) {
		Size() = New;
		CFrameWnd *pFrame = GetParentFrame();
		CMainFrame *pMain = dynamic_cast<CMainFrame*>(pFrame);
		if (pMain != NULL)
			pMain->PanelDragged(this, New);	// the other tabs of its place too
		else if (pFrame != NULL)
			pFrame->RecalcLayout();
	}
}

void CPanelBar::OnLButtonUp(UINT nFlags, CPoint point)
{
	if (m_iDragFrom >= 0) {
		m_iDragFrom = -1;
		ReleaseCapture();
		theApp.WriteProfileInt(m_pSection, m_Place == PANEL_RIGHT ? _T("Width") : _T("Height"), Size());
	}
}

void CPanelBar::OnRButtonUp(UINT nFlags, CPoint point)
{
	if (!StripRect().PtInRect(point)) {
		CControlBar::OnRButtonUp(nFlags, point);
		return;
	}
	const int Panel = TabAtPoint(point);
	CPoint Screen = point;
	ClientToScreen(&Screen);
	TabMenu(Panel != NO_PANEL ? Panel : m_iPanel, Screen);
}

void CPanelBar::TabMenu(int Panel, CPoint Screen)
{
	// A tab's own: where it goes, or closed
	CPanelBar *pBar = NULL;
	for (size_t i = 0; i < m_Tabs.size(); ++i)
		if (m_Tabs[i].pBar->PanelIndex() == Panel)
			pBar = m_Tabs[i].pBar;
	if (pBar == NULL)
		pBar = this;
	enum { MENU_SHOW = 1, MENU_PLACE = 2, MENU_CLOSE = 10 };
	CMenu Popup;
	Popup.CreatePopupMenu();
	if (pBar != this) {
		CString Show;
		Show.Format(_T("Show %s"), pBar->Title());
		Popup.AppendMenu(MF_STRING, MENU_SHOW, Show);
		Popup.AppendMenu(MF_SEPARATOR, 0, (LPCTSTR)NULL);
	}
	pBar->AddPlaceItems(Popup, MENU_PLACE);
	Popup.AppendMenu(MF_SEPARATOR, 0, (LPCTSTR)NULL);
	Popup.AppendMenu(MF_STRING, MENU_CLOSE, _T("Close"));
	const UINT Picked = Popup.TrackPopupMenu(TPM_LEFTALIGN | TPM_RETURNCMD | TPM_NONOTIFY, Screen.x, Screen.y, this);
	CMainFrame *pMain = dynamic_cast<CMainFrame*>(GetParentFrame());
	if (pMain == NULL || Picked == 0)
		return;
	if (Picked == MENU_SHOW)
		pMain->SelectPanelTab(pBar->PanelIndex());
	else if (Picked == MENU_CLOSE)
		pMain->ShowPanel(pBar->PanelIndex(), false);
	else
		pBar->PickPlace(Picked, MENU_PLACE);
}

void CPanelBar::AddPlaceItems(CMenu &Menu, UINT First) const
{
	Menu.AppendMenu(MF_STRING, First + PANEL_BOTTOM, _T("Tab at the bottom"));
	Menu.AppendMenu(MF_STRING, First + PANEL_RIGHT, _T("Tab on the right"));
	Menu.AppendMenu(MF_STRING, First + PANEL_WINDOW, _T("Own window"));
	Menu.CheckMenuRadioItem(First + PANEL_BOTTOM, First + PANEL_WINDOW, First + m_Place, MF_BYCOMMAND);
}

bool CPanelBar::PickPlace(UINT Picked, UINT First)
{
	if (Picked < First || Picked > First + PANEL_WINDOW)
		return false;
	const panel_place_t Place = static_cast<panel_place_t>(Picked - First);
	CMainFrame *pMain = dynamic_cast<CMainFrame*>(GetParentFrame());
	if (pMain != NULL && Place != m_Place)
		pMain->MovePanel(m_iPanel, Place);
	return true;
}

void CPanelBar::OnCaptureChanged(CWnd *pWnd)
{
	m_iDragFrom = -1;
	CControlBar::OnCaptureChanged(pWnd);
}

BOOL CPanelBar::OnSetCursor(CWnd *pWnd, UINT nHitTest, UINT message)
{
	if (pWnd == this && nHitTest == HTCLIENT) {
		CPoint Point;
		::GetCursorPos(&Point);
		ScreenToClient(&Point);
		if (!StripRect().PtInRect(Point)) {
			::SetCursor(::LoadCursor(NULL, m_Place == PANEL_RIGHT ? IDC_SIZEWE : IDC_SIZENS));
			return TRUE;
		}
	}
	return CControlBar::OnSetCursor(pWnd, nHitTest, message);
}

void CPanelBar::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == TMR_TABS) {
		// A tab's mark changed (the agent log's new lines, the problems' count): drawn again
		if (IsWindowVisible() && TabsSignature() != m_strTabs)
			InvalidateRect(StripRect(), FALSE);
		return;
	}
	CControlBar::OnTimer(nIDEvent);
}

void CPanelBar::OnDestroy()
{
	KillTimer(TMR_TABS);
	theApp.WriteProfileInt(m_pSection, _T("Height"), m_iHeight);
	theApp.WriteProfileInt(m_pSection, _T("Width"), m_iWidth);
	if (m_wndOwn.GetSafeHwnd() != NULL)
		m_wndOwn.DestroyWindow();
	CControlBar::OnDestroy();
}

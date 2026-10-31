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

#include <vector>
#include "PanelTabs.h"

// The bar a panel (View > Problems, What next, Ask, Agent log, Drum grid) is
// docked in, and the window of its own it can lie in instead. Docked along
// the bottom of the main window or along its right, the bar shows a line to
// drag along its top (or left) edge, then a strip with a tab for each panel
// open in that place (CMainFrame::PanelTabs): only the selected tab's bar
// shows, so the panels there take one panel's room. A panel can mark its tab
// (News: a count, or a dot for something new). A right-click on a tab, or the
// Place items in a panel's own right-click menu, put it in a tab at the
// bottom, a tab on the right or its own window: a tool window owned by the
// main window, shown without taking the keys, which can go anywhere (a second
// monitor too). Where it is, its sizes and its window's place are kept in the
// panel's own registry section ("Place", "Height", "Width", "Window left",
// "Window top", "Window width", "Window height").

class CPanelBar;

// The window of its own a panel lies in (Own window)
class CPanelWindow : public CWnd
{
public:
	explicit CPanelWindow(CPanelBar *pBar);
	virtual ~CPanelWindow();

	bool Make(CWnd *pOwner, LPCTSTR Title, const CRect &Rect);
	void SavePlace();

protected:
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnClose();
	afx_msg void OnSetFocus(CWnd *pOldWnd);
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnExitSizeMove();
	afx_msg BOOL OnEraseBkgnd(CDC *pDC);
	afx_msg void OnDestroy();

	DECLARE_MESSAGE_MAP()

private:
	CPanelBar *m_pBar;

	CPanelWindow(const CPanelWindow &);
	CPanelWindow &operator=(const CPanelWindow &);
};

// A panel's tab mark (CPanelBar::News): none, a dot, or a count (over 0)
const int PANEL_NEWS_NONE = 0;
const int PANEL_NEWS_MARK = -1;

class CPanelBar : public CControlBar
{
public:
	virtual ~CPanelBar();

	int PanelIndex() const { return m_iPanel; }		// CMainFrame's PANEL_*
	LPCTSTR Title() const { return m_pTitle; }
	bool IsOpen() const { return m_bOpen; }			// in a tab (shown or not) or its own window
	panel_place_t Place() const { return m_Place; }
	bool IsOnRight() const { return m_Place == PANEL_RIGHT; }
	CWnd *Panel() const { return m_pPanel; }
	// Where it shows on screen: its own window, or this bar
	CWnd *ShownWnd();

	// By the main frame (CMainFrame::ShowPanel, ApplyPanels)
	void Opened(bool Open);							// opened or closed: the panel starts or stops its work
	void SetPlace(panel_place_t Place);				// remembered; the main frame shows it there
	void Appear(bool Docked, bool OwnWindow);		// the bar or the window shown, the panel in it
	void TabsChanged();								// the tab strip drawn again

	// Along the bottom (CMainFrame::PlaceBottomPanel): the height it was made, the least it's usable at, and the
	// height it takes when another one's line is dragged (the tabs there keep one height); along the right the same
	int BottomHeight() const { return m_iHeight; }
	int LeastHeight(int Width) const;
	void SetBottomHeight(int Height);
	int RightWidth() const { return m_iWidth; }
	void SetRightWidth(int Width);

	// Its tab's mark: PANEL_NEWS_NONE, PANEL_NEWS_MARK or a count
	virtual int News() const { return PANEL_NEWS_NONE; }

	// The place items of a right-click menu (Tab at the bottom, Tab on the right, Own window) as First, First + 1 and
	// First + 2, the place it's in checked; PickPlace moves it when Picked is one of them
	void AddPlaceItems(CMenu &Menu, UINT First) const;
	bool PickPlace(UINT Picked, UINT First);
	// The bar of a panel window, docked or in its own window; NULL for none
	static CPanelBar *Of(const CWnd *pPanel);

	virtual CSize CalcFixedLayout(BOOL bStretch, BOOL bHorz);
	virtual void OnUpdateCmdUI(CFrameWnd *pTarget, BOOL bDisableIfNoHndler);
	virtual BOOL PreTranslateMessage(MSG *pMsg);

protected:
	CPanelBar(int Panel, LPCTSTR Section, LPCTSTR Title, int DefaultHeight, int DefaultWidth, int MinHeight, int MinWidth);

	bool CreateBar(CFrameWnd *pFrame, UINT nID);	// hidden, its sizes and place as remembered; then the panel
	void SetPanel(CWnd *pPanel);					// made, a child of this bar
	virtual int PanelLeast(int Width) const = 0;	// the panel's least height at that width, along the bottom
	virtual void PanelOpened(bool Open) = 0;

	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC *pDC);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnRButtonUp(UINT nFlags, CPoint point);
	afx_msg BOOL OnSetCursor(CWnd *pWnd, UINT nHitTest, UINT message);
	afx_msg void OnCaptureChanged(CWnd *pWnd);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnDestroy();
	afx_msg LRESULT OnSizeParent(WPARAM wParam, LPARAM lParam);

	DECLARE_MESSAGE_MAP()

private:
	friend class CPanelWindow;

	int Divider() const;					// the line along the top (or left) edge
	int StripHeight() const;				// the tabs under it
	CRect StripRect() const;
	int MaxSize() const;					// leaving the note views some room
	int &Size() { return m_Place == PANEL_RIGHT ? m_iWidth : m_iHeight; }
	int Size() const { return m_Place == PANEL_RIGHT ? m_iWidth : m_iHeight; }
	void PlacePanel();						// the panel under the strip
	void LayOutTabs(CDC &dc);				// m_Tabs from the main frame's tabs of this place
	CString TabText(const CPanelBar *pBar) const;
	CString TabsSignature() const;
	int TabAtPoint(CPoint Point) const;		// the panel of the tab there, or NO_PANEL
	void TabMenu(int Panel, CPoint Screen);
	void GiveKeysBack();					// from the panel to the note views, as it goes
	CRect WindowRect() const;				// its own window's place, as remembered (on a screen)
	void ClosedOwnWindow();					// its window's close button

	struct tab_t {
		CPanelBar *pBar;
		CRect Rect;
	};

	const int m_iPanel;
	LPCTSTR const m_pSection;
	LPCTSTR const m_pTitle;
	const int m_iMinHeight, m_iMinWidth;
	CWnd *m_pPanel;
	CPanelWindow m_wndOwn;
	panel_place_t m_Place;
	bool m_bOpen;
	int m_iHeight, m_iWidth;				// along the bottom, along the right
	int m_iRoom;							// for the note views and the panel, at the last layout
	int m_iDragFrom;						// -1, or where the line was grabbed (screen x or y)
	int m_iDragSize;
	int m_iDragMax;
	std::vector<tab_t> m_Tabs;				// as last drawn
	CString m_strTabs;						// what they said (TabsSignature)
	CFont m_Font, m_BoldFont;

	CPanelBar(const CPanelBar &);
	CPanelBar &operator=(const CPanelBar &);
};

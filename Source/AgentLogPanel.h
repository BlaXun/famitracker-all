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

// View > Agent log: what the agents working on the song did and why
// (Api/AgentLog.h: write_log's lines, a quieter line for each change a tool
// made, the calls that failed), newest at the bottom, each with its time and
// its category as a colored tag with the category's name in it (Okabe and
// Ito's colors, as the piano roll's chord roles have them). Toggles along the
// top show or hide each category and the change lines (remembered); Clear
// empties the song's log after asking. Under the list a box takes a note for
// the agents (Enter sends it, Shift+Enter starts a new line), which shows in
// the log as the user's own and says whether an agent has read it
// (read_user_notes). The log is the open song's, kept while FamiTracker runs
// and in a file next to the song once it has one. Docked along the bottom of
// the main window, or along its right, as the What next panel is.

class CMcpConnection;

class CAgentLogPanelWnd : public CWnd
{
public:
	CAgentLogPanelWnd();
	virtual ~CAgentLogPanelWnd();

	bool Create(CWnd *pParent, CMcpConnection *pTools);
	void Opened(bool Open);
	void FocusNote();						// the keys go to the note box

	virtual BOOL PreTranslateMessage(MSG *pMsg);

protected:
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC *pDC);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnContextMenu(CWnd *pWnd, CPoint point);
	afx_msg void OnSend();
	afx_msg void OnClear();
	afx_msg void OnDestroy();

	DECLARE_MESSAGE_MAP()

private:
	struct Impl;	// the list, the toggles and the note box
	Impl *m_pImpl;

	CAgentLogPanelWnd(const CAgentLogPanelWnd &);
	CAgentLogPanelWnd &operator=(const CAgentLogPanelWnd &);
};

// The panel docked along the bottom of the main window, or along its right.
// The line along its top (or left) edge is dragged to make it taller (or
// wider). Where it is, its size and whether it was open are remembered.
class CAgentLogBar : public CControlBar
{
public:
	CAgentLogBar();
	virtual ~CAgentLogBar();

	bool Create(CFrameWnd *pFrame, CMcpConnection *pTools);	// hidden at first
	void Opened(bool Open);					// shown or hidden by the main frame
	void Focus();
	static bool WasOpen();					// it was open when FamiTracker closed (closed the first time)
	bool IsOnRight() const { return m_bRight; }
	// Along the bottom (CMainFrame::PlaceBottomPanel): the height it was made, the least it's usable at, its share of
	// the row it's in beside the other panels there, and the height the row takes when another one's line is dragged
	int BottomHeight() const { return m_iHeight; }
	int LeastHeight(int Width) const;
	int RowWeight() const;
	void SetBottomHeight(int Height);
	void DockOnRight(bool Right);			// along the right of the main window, or along its bottom

	virtual CSize CalcFixedLayout(BOOL bStretch, BOOL bHorz);
	virtual void OnUpdateCmdUI(CFrameWnd *pTarget, BOOL bDisableIfNoHndler);
	virtual BOOL PreTranslateMessage(MSG *pMsg);

protected:
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC *pDC);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg BOOL OnSetCursor(CWnd *pWnd, UINT nHitTest, UINT message);
	afx_msg void OnCaptureChanged(CWnd *pWnd);
	afx_msg void OnDestroy();
	afx_msg LRESULT OnSizeParent(WPARAM wParam, LPARAM lParam);

	DECLARE_MESSAGE_MAP()

private:
	int Divider() const;					// the line along the top (or left) edge
	int MaxSize() const;					// leaving the pattern editor some room
	int &Size() { return m_bRight ? m_iWidth : m_iHeight; }
	int Size() const { return m_bRight ? m_iWidth : m_iHeight; }

	CAgentLogPanelWnd m_wndPanel;
	bool m_bRight;							// docked along the right, else along the bottom
	int m_iHeight, m_iWidth;				// along the bottom, along the right
	int m_iRoom;							// for the pattern editor and the panel, at the last layout
	int m_iDragFrom;						// -1, or where the line was grabbed (screen x or y)
	int m_iDragSize;
	int m_iDragMax;

	CAgentLogBar(const CAgentLogBar &);
	CAgentLogBar &operator=(const CAgentLogBar &);
};

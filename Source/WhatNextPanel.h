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

// View > What next: the one next step for the selected track, from Claude
// Code's what_next (Api/Tools.cpp, format json, which Api/NextSteps.h reads
// back), in plain words: what to do, why, and how far the song has come (the
// recipe's stages: the frame, the parts, the form, depth, polish, the mix,
// the loop and the export). Do it makes the step's call through
// CMcpConnection::RunTool as one undoable step, as the problems panel's Fix
// does. A melody's step offers the ways to put a tune of one's own in instead
// (record it from the microphone or the phone, draw it in the piano roll, or
// grow one from a hook, hearing the version picked), and Skip leaves a step
// out for the song (what_next's skip). The panel asks again a moment after
// the song changes (CMainFrame::SongChanged, and a look every second for what
// changes without a word: instruments, speed and tempo, the track, saving),
// not while the song plays or while the user types. It's docked along the
// bottom of the main window, or along its right, in the problems panel's way.

class CMcpConnection;

class CWhatNextPanelWnd : public CWnd
{
public:
	CWhatNextPanelWnd();
	virtual ~CWhatNextPanelWnd();

	bool Create(CWnd *pParent, CMcpConnection *pTools);
	void Opened(bool Open);					// asks when shown
	void SongChanged();						// asks again in a moment

	virtual BOOL PreTranslateMessage(MSG *pMsg);

protected:
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC *pDC);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnVScroll(UINT nSBCode, UINT nPos, CScrollBar *pScrollBar);
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnRButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnMouseLeave();
	afx_msg BOOL OnSetCursor(CWnd *pWnd, UINT nHitTest, UINT message);
	afx_msg UINT OnGetDlgCode();
	afx_msg void OnSetFocus(CWnd *pOldWnd);
	afx_msg void OnKillFocus(CWnd *pNewWnd);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnDestroy();

	DECLARE_MESSAGE_MAP()

private:
	struct Impl;	// the step, its buttons, asking and doing
	Impl *m_pImpl;

	CWhatNextPanelWnd(const CWhatNextPanelWnd &);
	CWhatNextPanelWnd &operator=(const CWhatNextPanelWnd &);
};

// The panel docked along the bottom of the main window, or along its right.
// The line along its top (or left) edge is dragged to make it taller (or
// wider). Where it is, its size and whether it was open are remembered.
class CWhatNextBar : public CControlBar
{
public:
	CWhatNextBar();
	virtual ~CWhatNextBar();

	bool Create(CFrameWnd *pFrame, CMcpConnection *pTools);	// hidden at first
	void Opened(bool Open);					// shown or hidden by the main frame
	void SongChanged();
	void Focus();							// the keyboard goes to the panel
	static bool WasOpen();					// it was open when FamiTracker closed (closed the first time)
	bool IsOnRight() const { return m_bRight; }
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

	CWhatNextPanelWnd m_wndPanel;
	bool m_bRight;							// docked along the right, else along the bottom
	int m_iHeight, m_iWidth;				// along the bottom, along the right
	int m_iRoom;							// for the pattern editor and the panel, at the last layout
	int m_iDragFrom;						// -1, or where the line was grabbed (screen x or y)
	int m_iDragSize;
	int m_iDragMax;

	CWhatNextBar(const CWhatNextBar &);
	CWhatNextBar &operator=(const CWhatNextBar &);
};

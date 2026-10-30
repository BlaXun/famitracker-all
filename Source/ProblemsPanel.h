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

#include "PanelBar.h"

// View > Problems: what Claude Code's check_song finds in the selected track
// (Api/Tools.cpp), for people working by hand: notes that vanish, notes that
// click, clashes, notes out of range, silent and busy bars, notes outside the
// key. The panel lists them by kind, heard worst first, with how many of each
// (Api/ProblemList.h reads check_song's JSON), and the pattern editor and the
// piano roll mark where they are (ProblemMarkAt). Double-click a finding, or
// press Enter, to go there; its button nudges a note that clicks (polish's
// "clicks" touch, one undoable step), selects notes that vanish in the piano
// roll, or explains the rest. Play it plays the bars where a finding is (a
// bar before and one after, Api/ProblemList.h's PlayOf), its channels alone
// and then in the mix (BarsPlayer.h); Play the fix makes a fix of one call,
// plays the same bars and asks whether to keep it or take it back, while
// nothing else changes the song. The panel checks the track again a moment
// after it changes (CMainFrame::SongChanged, and a look every second for
// changes nothing tells it of: instruments, speed and tempo, the track), not
// while the song plays; Check checks at once. It's docked along the bottom of
// the main window, or along its right.

class CMcpConnection;

// Where the problems panel found something, for the pattern editor and the
// piano roll: the mark on a channel's row of a track (rows count from the
// song's start: frame * pattern length + row), 0 for none, and none while the
// panel is closed. ProblemMarkColor is the colour to draw it in, and
// ProblemMarkSelected says the panel has that finding selected.
int ProblemMarkAt(int Track, int Channel, int Row);
COLORREF ProblemMarkColor(int Mark);
bool ProblemMarkSelected(int Mark);

// The selected track's settings and instruments, cheaply: what changes without
// a notice (an instrument's envelope, the speed, the track) changes it. The
// panels that check the song (this one, the What next panel) look at it every
// second.
unsigned int SongSettingsSignature();

class CProblemsPanelWnd : public CWnd
{
public:
	CProblemsPanelWnd();
	virtual ~CProblemsPanelWnd();

	bool Create(CWnd *pParent, CMcpConnection *pTools);
	void Opened(bool Open);					// checks when shown; no marks while hidden
	void SongChanged();						// checks again in a moment
	int ProblemCount() const;				// found at the last check (0 while closed), for its tab

	virtual BOOL PreTranslateMessage(MSG *pMsg);

protected:
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC *pDC);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnVScroll(UINT nSBCode, UINT nPos, CScrollBar *pScrollBar);
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
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
	struct Impl;	// the findings, the lines drawn, checking and going there
	Impl *m_pImpl;

	CProblemsPanelWnd(const CProblemsPanelWnd &);
	CProblemsPanelWnd &operator=(const CProblemsPanelWnd &);
};

// The panel in a tab along the bottom of the main window (under the note
// views) or along its right, or in a window of its own (PanelBar.h). Where
// it is, its sizes and whether it was open are remembered.
class CProblemsBar : public CPanelBar
{
public:
	CProblemsBar();
	virtual ~CProblemsBar();

	bool Create(CFrameWnd *pFrame, CMcpConnection *pTools);	// hidden at first
	void SongChanged();
	void Focus();							// the keyboard goes to the panel
	static bool WasOpen();					// it was open when FamiTracker closed (closed the first time)
	virtual int News() const;				// its tab's mark

protected:
	virtual int PanelLeast(int Width) const;
	virtual void PanelOpened(bool Open);

private:
	CProblemsPanelWnd m_wndPanel;

	CProblemsBar(const CProblemsBar &);
	CProblemsBar &operator=(const CProblemsBar &);
};

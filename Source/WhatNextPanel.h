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

#include <string>

class CMcpConnection;

// The kind of song chosen in the panel for the open song's selected track (calm, lively or action; "" for what_next's own
// choice), kept while FamiTracker runs: the Ask panel tells the model (ftapi::AssistantFacts::Kind)
std::string WhatNextChosenKind();

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

// The panel in a tab along the bottom of the main window (under the note
// views) or along its right, or in a window of its own (PanelBar.h). Where
// it is, its sizes and whether it was open are remembered.
class CWhatNextBar : public CPanelBar
{
public:
	CWhatNextBar();
	virtual ~CWhatNextBar();

	bool Create(CFrameWnd *pFrame, CMcpConnection *pTools);	// hidden at first
	void SongChanged();
	void Focus();							// the keyboard goes to the panel
	static bool WasOpen();					// it was open when FamiTracker closed (closed the first time)

protected:
	virtual int PanelLeast(int Width) const;
	virtual void PanelOpened(bool Open);

private:
	CWhatNextPanelWnd m_wndPanel;

	CWhatNextBar(const CWhatNextBar &);
	CWhatNextBar &operator=(const CWhatNextBar &);
};

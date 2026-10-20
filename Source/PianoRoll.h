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

// View > Piano roll: the song's notes on a grid, time from left to right and
// pitch from bottom to top, with bars, beats and frames marked and a keyboard
// on the left. It sits in the main window under the pattern editor
// (CPianoRollBar). Every channel's notes are shown, the one being edited
// bright (the pattern editor cursor's channel) and the others faint. It
// follows playback and the pattern editor's cursor.
//
// Notes are edited in the bright channel with the mouse: drawn, painted as a
// melody that follows the mouse, moved, copied, made longer or shorter,
// erased; selected notes are moved, transposed, copied and pasted with the
// keyboard too. A key and scale keep new and moved notes in key. Clips are
// placed with the mouse. Each change is one undoable action on the patterns
// (CChannelNotes in NoteEdit.h works out the cells, WriteChannelRows in
// InsertRecording.h writes them).
//
// A note lasts until the next note, release or halt in its channel, the way
// it plays. The main frame tells the piano roll when the song changes
// (SongChanged).
class CPianoRollWnd : public CWnd
{
public:
	CPianoRollWnd();
	virtual ~CPianoRollWnd();

	bool Create(CWnd *pParent);				// a child window
	void Activate(bool Shown);				// follows the song while shown
	void SongChanged();						// the notes are read again before the next paint

	virtual BOOL PreTranslateMessage(MSG *pMsg);

protected:
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC *pDC);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar *pScrollBar);
	afx_msg void OnVScroll(UINT nSBCode, UINT nPos, CScrollBar *pScrollBar);
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnRButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnRButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnCaptureChanged(CWnd *pWnd);
	afx_msg BOOL OnSetCursor(CWnd *pWnd, UINT nHitTest, UINT message);
	afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
	afx_msg UINT OnGetDlgCode();
	afx_msg void OnSetFocus(CWnd *pOldWnd);
	afx_msg void OnKillFocus(CWnd *pNewWnd);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnDestroy();

	DECLARE_MESSAGE_MAP()

private:
	struct Impl;	// the notes, the view, editing and the drawing surface
	Impl *m_pImpl;

	CPianoRollWnd(const CPianoRollWnd &);
	CPianoRollWnd &operator=(const CPianoRollWnd &);
};

// The piano roll docked along the bottom of the main window, under the
// pattern editor. The line along its top is dragged to make it taller or
// shorter. Its height is remembered, and so is whether it was open.
class CPianoRollBar : public CControlBar
{
public:
	CPianoRollBar();
	virtual ~CPianoRollBar();

	bool Create(CFrameWnd *pFrame);			// hidden at first
	void Opened(bool Open);					// shown or hidden by the main frame
	void SongChanged();
	static bool WasOpen();					// it was open when FamiTracker closed

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
	int Divider() const;					// the height of the line along the top
	int MaxHeight() const;					// leaving the pattern editor some room

	CPianoRollWnd m_wndRoll;
	int m_iHeight;
	int m_iRoom;							// for the pattern editor and the piano roll, at the last layout
	int m_iDragFrom;						// -1, or where the line was grabbed (screen y)
	int m_iDragHeight;
	int m_iDragMax;

	CPianoRollBar(const CPianoRollBar &);
	CPianoRollBar &operator=(const CPianoRollBar &);
};

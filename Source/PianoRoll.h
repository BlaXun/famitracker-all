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

class CMainFrame;

// View > Piano roll: the song's notes on a grid, time from left to right and
// pitch from bottom to top, with bars, beats and frames marked and a keyboard
// on the left. Every channel's notes are shown, the current channel's (the
// pattern editor cursor's) bright and the others faint. It follows playback
// and the pattern editor's cursor. Clicking a note moves the cursor to it,
// clicking an empty spot moves it to that row, and clicking a key plays it.
//
// A note lasts until the next note, release or halt in its channel, the way
// it plays. The window is owned by the main frame and only hidden when
// closed; the main frame tells it when the song changes (SongChanged).
class CPianoRollWnd : public CWnd
{
public:
	CPianoRollWnd();
	virtual ~CPianoRollWnd();

	bool Create(CMainFrame *pMainFrame);	// hidden until Show()
	void Show();
	bool IsShown() const;
	void SongChanged();						// the notes are read again before the next paint

protected:
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC *pDC);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar *pScrollBar);
	afx_msg void OnVScroll(UINT nSBCode, UINT nPos, CScrollBar *pScrollBar);
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnCaptureChanged(CWnd *pWnd);
	afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnClose();
	afx_msg void OnDestroy();
	afx_msg void OnGetMinMaxInfo(MINMAXINFO *lpMMI);

	DECLARE_MESSAGE_MAP()

private:
	struct Impl;	// the notes, the view and the drawing surface
	Impl *m_pImpl;

	CPianoRollWnd(const CPianoRollWnd &);
	CPianoRollWnd &operator=(const CPianoRollWnd &);
};

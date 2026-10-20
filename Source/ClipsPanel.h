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

// The clips panel: the song's clips (Clips.h) along the right of the main
// window, always in sight. Each clip is a card with its name, a little
// picture of its notes and what it holds. A card is dragged into the pattern
// editor to put the clip in where it's dropped (with Ctrl: linked), or
// double-clicked to put it in at the cursor; its play button plays it, and
// its right-click menu edits, renames, duplicates, unlinks or deletes it.
// The panel follows the song: it's told when the song changes
// (CMainFrame::SongChanged) and looks again a few times a second.

class CClipsPanelWnd : public CWnd
{
public:
	CClipsPanelWnd();
	virtual ~CClipsPanelWnd();

	bool Create(CWnd *pParent);
	void SongChanged();						// the clips are looked at again before the next paint

protected:
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC *pDC);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnVScroll(UINT nSBCode, UINT nPos, CScrollBar *pScrollBar);
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
	afx_msg void OnRButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnCaptureChanged(CWnd *pWnd);
	afx_msg BOOL OnSetCursor(CWnd *pWnd, UINT nHitTest, UINT message);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnDestroy();

	DECLARE_MESSAGE_MAP()

private:
	struct Impl;	// the cards, the drawing surface, dragging and playing
	Impl *m_pImpl;

	CClipsPanelWnd(const CClipsPanelWnd &);
	CClipsPanelWnd &operator=(const CClipsPanelWnd &);
};

// The panel docked along the right of the main window, beside the pattern
// editor. The line along its left edge is dragged to make it wider or
// narrower; its width is remembered, and so is whether it was open.
class CClipsBar : public CControlBar
{
public:
	CClipsBar();
	virtual ~CClipsBar();

	bool Create(CFrameWnd *pFrame);			// hidden at first
	void Opened(bool Open);					// shown or hidden by the main frame
	void SongChanged();
	void Focus();							// the keyboard goes to the panel
	static bool WasOpen();					// it was open when FamiTracker closed (open the first time)

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
	int Divider() const;					// the width of the line along the left
	int MaxWidth() const;					// leaving the pattern editor some room

	CClipsPanelWnd m_wndPanel;
	int m_iWidth;
	int m_iRoom;							// for the pattern editor and the panel, at the last layout
	int m_iDragFrom;						// -1, or where the line was grabbed (screen x)
	int m_iDragWidth;
	int m_iDragMax;

	CClipsBar(const CClipsBar &);
	CClipsBar &operator=(const CClipsBar &);
};

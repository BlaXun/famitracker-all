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

// View > Drum grid (also in the Edit menu, Ctrl+Shift+D): the noise channel
// a bar at a time, as a grid of steps (a row each) with a line for each drum
// (DrumGrid.h), in a panel docked along the bottom of the main window or
// along its right. It shows the bar the pattern editor's cursor is in and
// goes with the cursor (in follow mode, with the song as it plays); its <
// and > buttons show the bar before or after until the cursor moves.
// Clicking a step, or dragging along a line, puts drums on or takes them
// off, and plays them; ready-made beats fill a bar, which can be copied to
// the next bar or to every bar. Each change is written into the song at
// once, as one undoable action (WriteChannelRows), and the drums'
// instruments are added from the sound presets the first time they're
// needed. The main frame tells it when the song changes (SongChanged).

class CMainFrame;
class CDrumGridPanelWnd;	// the grid and its buttons (DrumGridPanel.cpp)

// The panel docked along the bottom of the main window (under the piano
// roll), or along its right. The line along its top (or left) edge is
// dragged to make it taller (or wider); right-clicking it docks it on the
// other side or closes it. Where it is, its size and whether it was open are
// remembered.
class CDrumGridBar : public CControlBar
{
public:
	CDrumGridBar();
	virtual ~CDrumGridBar();

	bool Create(CMainFrame *pFrame);		// hidden at first
	void Opened(bool Open);					// shown or hidden by the main frame
	void SongChanged();						// the bar is read again
	static bool WasOpen();					// it was open when FamiTracker closed (closed the first time)
	bool IsOnRight() const { return m_bRight; }
	// Along the bottom (CMainFrame::PlaceBottomPanel): the height it was made, the least it's usable at, its share of
	// the row it's in beside the other panels there, and the height the row takes when another one's line is dragged
	int BottomHeight() const { return m_iHeight; }
	int LeastHeight() const;
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

	CDrumGridPanelWnd *m_pPanel;
	bool m_bRight;							// docked along the right, else along the bottom
	int m_iHeight, m_iWidth;				// along the bottom, along the right
	int m_iRoom;							// for the pattern editor and the panel, at the last layout
	int m_iDragFrom;						// -1, or where the line was grabbed (screen x or y)
	int m_iDragSize;
	int m_iDragMax;

	CDrumGridBar(const CDrumGridBar &);
	CDrumGridBar &operator=(const CDrumGridBar &);
};

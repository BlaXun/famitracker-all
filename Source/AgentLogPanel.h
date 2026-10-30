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
	bool HasNews() const;					// lines added since it last showed (its tab's dot)

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

// The panel in a tab along the bottom of the main window (under the note
// views) or along its right, or in a window of its own (PanelBar.h). Where
// it is, its sizes and whether it was open are remembered.
class CAgentLogBar : public CPanelBar
{
public:
	CAgentLogBar();
	virtual ~CAgentLogBar();

	bool Create(CFrameWnd *pFrame, CMcpConnection *pTools);	// hidden at first
	void Focus();							// the keyboard goes to the note box
	static bool WasOpen();					// it was open when FamiTracker closed (closed the first time)
	virtual int News() const;				// its tab's mark

protected:
	virtual int PanelLeast(int Width) const;
	virtual void PanelOpened(bool Open);

private:
	CAgentLogPanelWnd m_wndPanel;

	CAgentLogBar(const CAgentLogBar &);
	CAgentLogBar &operator=(const CAgentLogBar &);
};

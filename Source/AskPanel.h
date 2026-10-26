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

// View > Ask: a question box for the user's own model (Api/Assistant.h), any
// server that speaks OpenAI's chat API on their home network (llama.cpp's
// llama-server, llama-swap, Ollama, LM Studio). The answers stream in, the
// glossary's words linked ([[term]]); the conversation is kept for each song
// while FamiTracker runs, and New question starts over. The model reads the
// song with FamiTracker's tools (all of them, or the small set), which run here
// on the UI thread through CMcpConnection::RunTool between its replies. Ask
// first (the default) turns a change into a proposal with Do it (one Ctrl+Z,
// as What next's) and Skip; Let it work makes the changes as the model asks,
// each one undoable, lists them as it goes, and Take back all undoes the run.
// Stop works at any moment. Without a model the panel searches the glossary,
// the exercises and the tutorials. Its settings (the address, the model's name,
// a key kept with DPAPI, the tools, the rounds, the context) are its own small
// dialog, with Test connection. Nothing is sent until the user asks. Docked
// along the bottom of the main window, or along its right, as the What next
// panel is.

class CMcpConnection;

class CAskPanelWnd : public CWnd
{
public:
	CAskPanelWnd();
	virtual ~CAskPanelWnd();

	bool Create(CWnd *pParent, CMcpConnection *pTools);
	void Opened(bool Open);
	void SongChanged();						// another song may be open: its own conversation shows
	void FocusQuestion();

	virtual BOOL PreTranslateMessage(MSG *pMsg);

protected:
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC *pDC);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnDestroy();
	afx_msg void OnContextMenu(CWnd *pWnd, CPoint point);
	afx_msg void OnRButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnSend();
	afx_msg void OnStop();
	afx_msg void OnNewQuestion();
	afx_msg void OnSettings();
	afx_msg void OnSetupHelp();
	afx_msg void OnModeChange();
	afx_msg LRESULT OnJobMessage(WPARAM wParam, LPARAM lParam);

	DECLARE_MESSAGE_MAP()

private:
	friend class CAskTalkWnd;
	struct Impl;	// the conversations, the model's jobs, the layout
	Impl *m_pImpl;

	CAskPanelWnd(const CAskPanelWnd &);
	CAskPanelWnd &operator=(const CAskPanelWnd &);
};

// The panel docked along the bottom of the main window, or along its right. The line along its top (or left) edge is
// dragged to make it taller (or wider). Where it is, its size and whether it was open are remembered.
class CAskBar : public CControlBar
{
public:
	CAskBar();
	virtual ~CAskBar();

	bool Create(CFrameWnd *pFrame, CMcpConnection *pTools);	// hidden at first
	void Opened(bool Open);					// shown or hidden by the main frame
	void SongChanged();
	void Focus();							// the keyboard goes to the question box
	static bool WasOpen();					// it was open when FamiTracker closed (closed the first time)
	bool IsOnRight() const { return m_bRight; }
	// Along the bottom (CMainFrame::PlaceBottomPanel): the height it was made, the least it's usable at, its share of
	// the row it's in beside the other panels there, and the height the row takes when another one's line is dragged
	int BottomHeight() const { return m_iHeight; }
	int LeastHeight(int Width) const;
	int RowWeight() const;
	void SetBottomHeight(int Height);
	void DockOnRight(bool Right);

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
	int Divider() const;
	int MaxSize() const;
	int &Size() { return m_bRight ? m_iWidth : m_iHeight; }
	int Size() const { return m_bRight ? m_iWidth : m_iHeight; }

	CAskPanelWnd m_wndPanel;
	bool m_bRight;
	int m_iHeight, m_iWidth;
	int m_iRoom;
	int m_iDragFrom;
	int m_iDragSize;
	int m_iDragMax;

	CAskBar(const CAskBar &);
	CAskBar &operator=(const CAskBar &);
};

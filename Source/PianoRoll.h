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
// keyboard too. The track's key (chosen in the Key menu and kept in the
// module, or found from the notes as the Claude Code tools find it) lights
// its notes' lanes, and with Snap to key new and moved notes land on them; a
// strip under the ruler names the chords the parts play (Api/Tools.h,
// ToolSet::Harmony), worked out a moment after a change. Clips are placed
// with the mouse. Each change is one undoable action on the patterns
// (CChannelNotes in NoteEdit.h works out the cells, WriteChannelRows in
// InsertRecording.h writes them).
//
// The Groove button (and, in an empty song, "Start from a groove" in the
// middle) lays a mood's drums, bass line and chords under the selected bars
// or from the cursor's bar on, as Edit > Start from a groove does (MakeGroove
// in InsertRecording.h), one Ctrl+Z. The buttons along the top go on as many
// rows as the width needs (FlowRollButtons in PianoRollUi.h), and a click
// that only closed a menu does nothing here (ClosedAMenu).
//
// A note lasts until the next note, release or halt in its channel, the way
// it plays. The main frame tells the piano roll when the song changes
// (SongChanged).
//
// While the pattern editor is hidden (View > Pattern editor), the piano roll
// fills its place and takes its note keys: they play notes, and while editing
// is on write them at the cursor line, a step apart, as the pattern editor
// does (CFamiTrackerView::PianoRollKey); the letters D, B, S and E are notes
// then, not the tools' keys.
class CPianoRollWnd : public CWnd
{
public:
	CPianoRollWnd();
	virtual ~CPianoRollWnd();

	bool Create(CWnd *pParent);				// a child window
	void Activate(bool Shown);				// follows the song while shown
	void SongChanged();						// the notes are read again before the next paint

	// A clip dragged in from the clips panel (ClipsPanel.cpp). Over the notes,
	// the roll shows where it would go, on the channel being edited and the
	// ones to its right, as placing one with the Clips button does; with
	// Scroll, a point close to the left or right edge scrolls the roll that
	// way, so calling it on a timer keeps it scrolling. False, showing
	// nothing, when the point isn't over the notes; otherwise Channel, Frame
	// and Row say where the clip would start.
	bool ShowClipDrop(const CPoint &ScreenPoint, int Index, bool Scroll, int &Channel, int &Frame, int &Row);
	bool HideClipDrop();					// true if it showed one

	// Notes selected in the piano roll, which Edit > Save selection as clip
	// (and the clips panel's + Save selection) keeps when the pattern editor
	// has no selection: there is one selection at a time
	bool HasSelection() const;
	void SaveSelectionAsClip();				// asks for a name
	// The bars the selected notes start in, for Record from microphone to sing over: the bright channel and the
	// song rows [First, End) from the first note's bar to the end of the last one's; false without a selection
	bool SelectedBars(int &Channel, int &First, int &End) const;

	// The problems panel's notes in sight (ProblemsPanel.h): the roll goes to
	// where the pattern editor's cursor is now (the panel put it on the first
	// of them) and up or down to the notes, which start at pStarts on Channel,
	// the bright one from then on; with Select they're the selection
	void ShowNotes(int Channel, const int *pStarts, int Count, bool Select);

	// Edit > Cut, Copy, Paste, Delete and Select all while the piano roll has
	// the keys: as Ctrl+X, Ctrl+C, Ctrl+V, Delete and Ctrl+A do in it
	bool EditCommand(UINT nID);
	bool CanEditCommand(UINT nID) const;

	// The keys left the program's window while the piano roll had them (a
	// dialog, a message box, another program): true once, while it shows. As
	// the window is activated again MFC gives them to the pattern editor, so
	// the main frame gives them back (CMainFrame::OnActivate).
	bool TakeKeysBack();

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
	afx_msg void OnKeyUp(UINT nChar, UINT nRepCnt, UINT nFlags);
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
// shorter. Its height is remembered, and so is whether it was open. While the
// pattern editor is hidden, it fills the pattern editor's place (Fill), and
// gets its own height back when the pattern editor shows again.
class CPianoRollBar : public CControlBar
{
public:
	CPianoRollBar();
	virtual ~CPianoRollBar();

	bool Create(CFrameWnd *pFrame);			// hidden at first
	void Opened(bool Open);					// shown or hidden by the main frame
	bool IsOpen() const { return m_bOpen; }	// between Opened(true) and Opened(false): View > Piano roll's check mark
	void Fill(bool On);						// all the room the other bars leave, without the line
	void SongChanged();
	static bool WasOpen();					// it was open when FamiTracker closed
	CPianoRollWnd *GetRoll() { return &m_wndRoll; }

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
	bool m_bOpen;
	bool m_bFill;							// in the pattern editor's place
	int m_iHeight;
	int m_iRoom;							// for the pattern editor and the piano roll, at the last layout
	int m_iDragFrom;						// -1, or where the line was grabbed (screen y)
	int m_iDragHeight;
	int m_iDragMax;

	CPianoRollBar(const CPianoRollBar &);
	CPianoRollBar &operator=(const CPianoRollBar &);
};

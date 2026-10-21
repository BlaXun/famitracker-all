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

// View > Keyboard: a strip along the bottom of the main window with a
// piano's keys, from C in the octave chosen up to E two octaves higher, and
// on each the computer key that plays it (Z, S, X... for the lower octave,
// Q, 2, W... for the two above, as FamiTracker maps them). Keys light up
// while a note is held, from the computer keyboard, MIDI or the mouse, and
// clicking one plays it; while editing, the note goes into the song as if
// typed. Each C is named with its octave.

class CKeyboardBar : public CControlBar
{
public:
	CKeyboardBar();
	virtual ~CKeyboardBar();

	bool Create(CFrameWnd *pFrame);			// hidden at first
	void Opened(bool Open);					// shown or hidden by the main frame
	static bool WasOpen();					// it was open when FamiTracker closed (open the first time)

	virtual CSize CalcFixedLayout(BOOL bStretch, BOOL bHorz);
	virtual void OnUpdateCmdUI(CFrameWnd *pTarget, BOOL bDisableIfNoHndler);

protected:
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC *pDC);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnCaptureChanged(CWnd *pWnd);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnDestroy();

	DECLARE_MESSAGE_MAP()

private:
	int KeyAt(const CPoint &Point) const;	// a semitone from the lowest C, -1 for none
	CRect KeyRect(int Semitone) const;
	bool IsBlack(int Semitone) const;
	bool Playable(int Semitone) const;		// not above B-7
	int Octave() const;
	void Press(int Semitone);				// -1 lets go

	int m_iPressed;							// the key the mouse holds, -1
	int m_iHover;
	unsigned int m_iShownOctave;
	bool m_bShownHeld[29];
	CFont m_Font, m_SmallFont, m_BoldFont;

	CKeyboardBar(const CKeyboardBar &);
	CKeyboardBar &operator=(const CKeyboardBar &);
};

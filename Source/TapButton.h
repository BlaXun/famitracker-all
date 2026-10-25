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

// Tapping the tempo, for the recorder's dialogs (Record from microphone,
// Start from a groove): a button that counts a tap the moment it's pressed
// (not when it's let go, as a click is), and the taps' tempo
// (transcribe::TempoOfTaps). The T key taps too, where a dialog passes it on
// (IsTapKey in its PreTranslateMessage).

#include <vector>

// Sent to the button's parent on every tap: wParam is the button's ID
const UINT WM_TEMPO_TAP = WM_APP + 0x260;

class CTapButton : public CButton
{
protected:
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
	afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
	DECLARE_MESSAGE_MAP()
};

// The taps so far, timed with the performance counter
class CTapTempo
{
public:
	void Tap();							// one now
	void Clear();
	bool Tapping() const;				// taps came, and no pause long enough to count as done
	bool Done() const;					// taps came, then a pause of a second and a half
	int Taps() const;					// in the last run of taps
	double Bpm() const;					// their tempo, 0 until there are four
	double Spread() const;				// how unevenly they came, as a share of a beat

private:
	static double Now();
	std::vector<double> m_Times;
};

// Is this the T key pressed (not held, without Ctrl or Alt) outside a box the
// user types or picks in? Then it's a tap.
bool IsTapKey(const MSG *pMsg);

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

// Tapping the tempo (see TapButton.h)

// Standard and engine headers come before stdafx.h, whose DEBUG_NEW macro
// would otherwise rewrite the placement new inside the standard library.
#include <algorithm>
#include <string>
#include <vector>
#include "Transcribe/Transcribe.h"
#include "stdafx.h"
#include "TapButton.h"

namespace {

const double DONE_SECONDS = 1.5;	// a pause this long after the taps: they're done

}	// namespace

BEGIN_MESSAGE_MAP(CTapButton, CButton)
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONDBLCLK()
	ON_WM_KEYDOWN()
END_MESSAGE_MAP()

void CTapButton::OnLButtonDown(UINT nFlags, CPoint point)
{
	// A tap is when the button goes down
	CWnd *pParent = GetParent();
	if (pParent != NULL)
		pParent->SendMessage(WM_TEMPO_TAP, static_cast<WPARAM>(GetDlgCtrlID()), 0);
	CButton::OnLButtonDown(nFlags, point);
}

void CTapButton::OnLButtonDblClk(UINT nFlags, CPoint point)
{
	// Quick taps come as double clicks: each is a tap of its own
	CWnd *pParent = GetParent();
	if (pParent != NULL)
		pParent->SendMessage(WM_TEMPO_TAP, static_cast<WPARAM>(GetDlgCtrlID()), 0);
	CButton::OnLButtonDown(nFlags, point);
}

void CTapButton::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	// Space taps while the button has the focus (held down, it doesn't repeat)
	if (nChar == VK_SPACE) {
		if ((nFlags & KF_REPEAT) == 0) {
			CWnd *pParent = GetParent();
			if (pParent != NULL)
				pParent->SendMessage(WM_TEMPO_TAP, static_cast<WPARAM>(GetDlgCtrlID()), 0);
		}
		return;	// not a click as well
	}
	CButton::OnKeyDown(nChar, nRepCnt, nFlags);
}

double CTapTempo::Now()
{
	LARGE_INTEGER Count, Frequency;
	if (!QueryPerformanceFrequency(&Frequency) || Frequency.QuadPart == 0 || !QueryPerformanceCounter(&Count))
		return GetTickCount() / 1000.0;
	return static_cast<double>(Count.QuadPart) / static_cast<double>(Frequency.QuadPart);
}

void CTapTempo::Tap()
{
	const double Time = Now();
	if (!m_Times.empty() && Time - m_Times.back() > 2.0)
		m_Times.clear();	// a new run of taps
	m_Times.push_back(Time);
	if (m_Times.size() > 64)
		m_Times.erase(m_Times.begin());
}

void CTapTempo::Clear()
{
	m_Times.clear();
}

bool CTapTempo::Tapping() const
{
	return !m_Times.empty() && Now() - m_Times.back() < DONE_SECONDS;
}

bool CTapTempo::Done() const
{
	return !m_Times.empty() && Now() - m_Times.back() >= DONE_SECONDS;
}

int CTapTempo::Taps() const
{
	return std::max(static_cast<int>(m_Times.empty() ? 0 : 1), transcribe::TempoOfTaps(m_Times, 2).taps);
}

double CTapTempo::Bpm() const
{
	return transcribe::TempoOfTaps(m_Times).bpm;
}

double CTapTempo::Spread() const
{
	return transcribe::TempoOfTaps(m_Times).spread;
}

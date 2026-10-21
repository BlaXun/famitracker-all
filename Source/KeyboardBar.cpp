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

// View > Keyboard (see KeyboardBar.h)

// Standard headers come before stdafx.h, whose DEBUG_NEW macro would
// otherwise rewrite the placement new inside the standard library.
#include <algorithm>
#include "stdafx.h"
#include "FamiTracker.h"
#include "FamiTrackerDoc.h"
#include "FamiTrackerView.h"
#include "MainFrm.h"
#include "KeyboardBar.h"

namespace {

LPCTSTR SECTION = _T("Keyboard");		// registry: open

// Sizes at 96 DPI (scaled with SX and SY)
const int HEIGHT = 60;
const int LABEL_WIDTH = 92;
const int KEY_WIDTH = 30;				// a white key, at most
const int MARGIN = 4;

const int SEMITONES = 29;				// C up to E two octaves higher
const int WHITE_SEMITONES[] = { 0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17, 19, 21, 23, 24, 26, 28 };
const int WHITE_KEYS = sizeof(WHITE_SEMITONES) / sizeof(WHITE_SEMITONES[0]);

// The computer keys that play them (CFamiTrackerView::TranslateKeyDefault):
// the bottom row of letters the octave chosen, the top row the two above
const char LOWER_KEYS[12] = { 'Z', 'S', 'X', 'D', 'C', 'V', 'G', 'B', 'H', 'N', 'J', 'M' };
const char UPPER_KEYS[17] = { 'Q', '2', 'W', '3', 'E', 'R', '5', 'T', '6', 'Y', '7', 'U', 'I', '9', 'O', '0', 'P' };

const UINT_PTR TMR_HELD = 1;			// looks at the notes held
const UINT HELD_MS = 40;

// Colours
const COLORREF PANEL = RGB(46, 48, 56);
const COLORREF WHITE_KEY = RGB(250, 250, 247);
const COLORREF WHITE_HOVER = RGB(232, 240, 252);
const COLORREF WHITE_HELD = RGB(120, 200, 255);
const COLORREF BLACK_KEY = RGB(28, 28, 32);
const COLORREF BLACK_HOVER = RGB(70, 74, 86);
const COLORREF BLACK_HELD = RGB(40, 130, 225);
const COLORREF WHITE_OFF = RGB(150, 152, 160);		// above B-7, the highest note
const COLORREF BLACK_OFF = RGB(64, 66, 74);
const COLORREF KEY_EDGE = RGB(110, 112, 122);
const COLORREF LETTER_DARK = RGB(40, 42, 50);
const COLORREF LETTER_LIGHT = RGB(220, 222, 230);
const COLORREF NOTE_NAME = RGB(150, 120, 60);
const COLORREF LABEL = RGB(228, 230, 238);
const COLORREF LABEL_DIM = RGB(150, 155, 168);

const char *const NOTE_NAMES[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

const int HIGHEST_NOTE = MIDI_NOTE(7, 12);	// B-7

char ComputerKey(int Semitone)
{
	return Semitone < 12 ? LOWER_KEYS[Semitone] : UPPER_KEYS[Semitone - 12];
}

int WhiteIndex(int Semitone)
{
	for (int i = 0; i < WHITE_KEYS; ++i)
		if (WHITE_SEMITONES[i] == Semitone)
			return i;
	return -1;
}

}	// namespace

BEGIN_MESSAGE_MAP(CKeyboardBar, CControlBar)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_MOUSEMOVE()
	ON_WM_CAPTURECHANGED()
	ON_WM_TIMER()
	ON_WM_DESTROY()
END_MESSAGE_MAP()

CKeyboardBar::CKeyboardBar() : m_iPressed(-1), m_iHover(-1), m_iShownOctave(0)
{
	memset(m_bShownHeld, 0, sizeof(m_bShownHeld));
}

CKeyboardBar::~CKeyboardBar()
{
}

bool CKeyboardBar::Create(CFrameWnd *pFrame)
{
	m_dwStyle = CBRS_ALIGN_BOTTOM;
	// No double clicks: quick clicks on a key each play it
	const CString Class = AfxRegisterWndClass(0, ::LoadCursor(NULL, IDC_HAND), NULL, NULL);
	if (!CWnd::Create(Class, _T("Keyboard"), WS_CHILD | WS_CLIPSIBLINGS, CRect(0, 0, 0, 0), pFrame, AFX_IDW_CONTROLBAR_FIRST + 34))
		return false;
	LOGFONT Log;
	memset(&Log, 0, sizeof(Log));
	::GetObject(::GetStockObject(DEFAULT_GUI_FONT), sizeof(Log), &Log);
	_tcscpy_s(Log.lfFaceName, LF_FACESIZE, _T("Segoe UI"));
	Log.lfHeight = -SY(12);
	m_Font.CreateFontIndirect(&Log);
	Log.lfHeight = -SY(10);
	m_SmallFont.CreateFontIndirect(&Log);
	Log.lfHeight = -SY(13);
	Log.lfWeight = FW_BOLD;
	m_BoldFont.CreateFontIndirect(&Log);
	SetTimer(TMR_HELD, HELD_MS, NULL);
	return true;
}

void CKeyboardBar::Opened(bool Open)
{
	if (!Open && m_iPressed >= 0)
		Press(-1);
	theApp.WriteProfileInt(SECTION, _T("Open"), Open ? 1 : 0);
}

bool CKeyboardBar::WasOpen()
{
	return theApp.GetProfileInt(SECTION, _T("Open"), 1) != 0;
}

CSize CKeyboardBar::CalcFixedLayout(BOOL bStretch, BOOL bHorz)
{
	return CSize(32767, SY(HEIGHT));
}

void CKeyboardBar::OnUpdateCmdUI(CFrameWnd *pTarget, BOOL bDisableIfNoHndler)
{
	// Nothing in it is a command
}

int CKeyboardBar::Octave() const
{
	CFamiTrackerView *pView = CFamiTrackerView::GetView();
	return pView != NULL ? static_cast<int>(pView->GetOctave()) : 3;
}

bool CKeyboardBar::IsBlack(int Semitone) const
{
	const int Step = Semitone % 12;
	return Step == 1 || Step == 3 || Step == 6 || Step == 8 || Step == 10;
}

CRect CKeyboardBar::KeyRect(int Semitone) const
{
	CRect Client;
	GetClientRect(&Client);
	const int Top = SY(MARGIN), Bottom = std::max(Top + 1, static_cast<int>(Client.bottom) - SY(MARGIN));
	const int Left = SX(LABEL_WIDTH);
	const int Width = std::max(SX(10), std::min(SX(KEY_WIDTH), (static_cast<int>(Client.Width()) - Left - SX(MARGIN)) / WHITE_KEYS));
	if (!IsBlack(Semitone)) {
		const int Index = WhiteIndex(Semitone);
		return CRect(Left + Index * Width, Top, Left + (Index + 1) * Width, Bottom);
	}
	// Between the white keys either side, shorter
	const int Edge = Left + (WhiteIndex(Semitone - 1) + 1) * Width;
	const int Half = std::max(2, Width * 3 / 10);
	return CRect(Edge - Half, Top, Edge + Half, Top + (Bottom - Top) * 6 / 10);
}

bool CKeyboardBar::Playable(int Semitone) const
{
	return MIDI_NOTE(Octave() + Semitone / 12, Semitone % 12 + 1) <= HIGHEST_NOTE;
}

int CKeyboardBar::KeyAt(const CPoint &Point) const
{
	// The black keys lie over the white ones
	for (int s = 0; s < SEMITONES; ++s)
		if (IsBlack(s) && KeyRect(s).PtInRect(Point))
			return s;
	for (int s = 0; s < SEMITONES; ++s)
		if (!IsBlack(s) && KeyRect(s).PtInRect(Point))
			return s;
	return -1;
}

void CKeyboardBar::Press(int Semitone)
{
	CFamiTrackerView *pView = CFamiTrackerView::GetView();
	const int Octave = this->Octave();
	if (pView != NULL && m_iPressed >= 0)
		pView->PlayKeyboardNote(MIDI_NOTE(Octave + m_iPressed / 12, m_iPressed % 12 + 1), false);
	m_iPressed = Semitone;
	if (pView != NULL && Semitone >= 0)
		pView->PlayKeyboardNote(MIDI_NOTE(Octave + Semitone / 12, Semitone % 12 + 1), true);
	Invalidate(FALSE);
}

void CKeyboardBar::OnPaint()
{
	CPaintDC dc(this);
	CRect Client;
	GetClientRect(&Client);
	CDC Memory;
	Memory.CreateCompatibleDC(&dc);
	CBitmap Bitmap;
	Bitmap.CreateCompatibleBitmap(&dc, std::max(1, static_cast<int>(Client.Width())), std::max(1, static_cast<int>(Client.Height())));
	CBitmap *pOldBitmap = Memory.SelectObject(&Bitmap);
	Memory.FillSolidRect(&Client, PANEL);
	Memory.FillSolidRect(Client.left, Client.top, Client.Width(), 1, RGB(80, 84, 96));
	Memory.SetBkMode(TRANSPARENT);

	CFamiTrackerView *pView = CFamiTrackerView::GetView();
	const int Octave = this->Octave();

	// What the keys are
	CFont *pOldFont = Memory.SelectObject(&m_BoldFont);
	Memory.SetTextColor(LABEL);
	CString Text;
	Text.Format(_T("Octave %d"), Octave);
	CRect Label(SX(8), SY(MARGIN + 4), SX(LABEL_WIDTH - 6), SY(MARGIN + 22));
	Memory.DrawText(Text, &Label, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
	Memory.SelectObject(&m_SmallFont);
	Memory.SetTextColor(LABEL_DIM);
	Label = CRect(SX(8), SY(MARGIN + 23), SX(LABEL_WIDTH - 6), Client.bottom - SY(MARGIN));
	Memory.DrawText(_T("the letters on the keys play them"), &Label, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);

	// White keys, then black ones over them
	for (int Pass = 0; Pass < 2; ++Pass) {
		for (int s = 0; s < SEMITONES; ++s) {
			const bool Black = IsBlack(s);
			if (Black != (Pass == 1))
				continue;
			const CRect Key = KeyRect(s);
			const int Note = MIDI_NOTE(Octave + s / 12, s % 12 + 1);
			const bool Held = s == m_iPressed || (pView != NULL && pView->IsNoteHeld(Note));
			COLORREF Fill = Black ? BLACK_KEY : WHITE_KEY;
			if (Note > HIGHEST_NOTE)
				Fill = Black ? BLACK_OFF : WHITE_OFF;
			else if (Held)
				Fill = Black ? BLACK_HELD : WHITE_HELD;
			else if (s == m_iHover)
				Fill = Black ? BLACK_HOVER : WHITE_HOVER;
			Memory.FillSolidRect(&Key, Fill);
			CBrush Edge(KEY_EDGE);
			Memory.FrameRect(&Key, &Edge);

			// The computer key, at the bottom of the key
			const TCHAR Letter[2] = { static_cast<TCHAR>(ComputerKey(s)), 0 };
			Memory.SelectObject(&m_Font);
			Memory.SetTextColor(Black ? LETTER_LIGHT : LETTER_DARK);
			CRect Bottom(Key.left, Key.bottom - SY(18), Key.right, Key.bottom - SY(2));
			Memory.DrawText(Letter, 1, &Bottom, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);
			// and the octave on each C
			if (s % 12 == 0) {
				CString Name;
				Name.Format(_T("C%d"), Octave + s / 12);
				Memory.SelectObject(&m_SmallFont);
				Memory.SetTextColor(NOTE_NAME);
				CRect Top(Key.left, Key.top + SY(2), Key.right, Key.top + SY(15));
				Memory.DrawText(Name, &Top, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);
			}
		}
	}

	dc.BitBlt(0, 0, Client.Width(), Client.Height(), &Memory, 0, 0, SRCCOPY);
	Memory.SelectObject(pOldFont);
	Memory.SelectObject(pOldBitmap);
}

BOOL CKeyboardBar::OnEraseBkgnd(CDC *pDC)
{
	return TRUE;
}

void CKeyboardBar::OnLButtonDown(UINT nFlags, CPoint point)
{
	// The keyboard stays with the pattern editor: no focus taken
	const int Key = KeyAt(point);
	if (Key < 0 || !Playable(Key))
		return;
	Press(Key);
	SetCapture();
}

void CKeyboardBar::OnLButtonUp(UINT nFlags, CPoint point)
{
	if (GetCapture() == this)
		ReleaseCapture();		// which lets go of the key
	else if (m_iPressed >= 0)
		Press(-1);
}

void CKeyboardBar::OnMouseMove(UINT nFlags, CPoint point)
{
	const int Key = KeyAt(point);
	// Held down, sliding over the keys plays each
	if (GetCapture() == this && Key >= 0 && Key != m_iPressed && Playable(Key))
		Press(Key);
	if (Key != m_iHover) {
		m_iHover = Key;
		Invalidate(FALSE);
		if (Key >= 0) {
			CFamiTrackerView *pView = CFamiTrackerView::GetView();
			const CString Name(NOTE_NAMES[Key % 12]);
			CString Text;
			if (!Playable(Key))
				Text.Format(_T("%s%d is higher than the highest note, B-7. A lower octave brings it in."), static_cast<LPCTSTR>(Name),
					Octave() + Key / 12);
			else
				Text.Format(_T("%s%d: the %c key plays it. Click to play it%s."), static_cast<LPCTSTR>(Name),
					Octave() + Key / 12, ComputerKey(Key),
					pView != NULL && pView->GetEditMode() ? _T(", and it goes into the song as if typed") : _T(""));
			GetParentFrame()->SetMessageText(Text);
		}
	}
}

void CKeyboardBar::OnCaptureChanged(CWnd *pWnd)
{
	if (m_iPressed >= 0)
		Press(-1);
	CControlBar::OnCaptureChanged(pWnd);
}

void CKeyboardBar::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent != TMR_HELD) {
		CControlBar::OnTimer(nIDEvent);
		return;
	}
	// Repainted when a note is held or let go, or the octave changes
	CFamiTrackerView *pView = CFamiTrackerView::GetView();
	if (pView == NULL || !IsWindowVisible())
		return;
	const int Octave = this->Octave();
	bool Changed = static_cast<unsigned int>(Octave) != m_iShownOctave;
	m_iShownOctave = static_cast<unsigned int>(Octave);
	for (int s = 0; s < SEMITONES; ++s) {
		const bool Held = pView->IsNoteHeld(MIDI_NOTE(Octave + s / 12, s % 12 + 1));
		Changed |= Held != m_bShownHeld[s];
		m_bShownHeld[s] = Held;
	}
	// The mouse left the keys
	CPoint Point;
	::GetCursorPos(&Point);
	ScreenToClient(&Point);
	if (m_iHover >= 0 && KeyAt(Point) != m_iHover && GetCapture() != this) {
		m_iHover = -1;
		Changed = true;
	}
	if (Changed)
		Invalidate(FALSE);
}

void CKeyboardBar::OnDestroy()
{
	KillTimer(TMR_HELD);
	CControlBar::OnDestroy();
}

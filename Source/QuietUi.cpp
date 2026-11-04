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

#include "stdafx.h"
#include <algorithm>
#include "QuietUi.h"

namespace {

struct made_t {
	HWND hWnd;
	bool Closed;	// told to close (and noted) once
};

int g_iDepth = 0;
DWORD g_iThread = 0;
HHOOK g_hHook = NULL;
UINT_PTR g_iTimer = 0;
std::vector<CString> g_Notes;
std::vector<made_t> g_Made;		// the dialogs made while quiet

const int MSGBOX_TEXT = 0xFFFF;		// a message box's text control, in Windows and in Wine
const UINT QUIET_TIMER_MS = 500;

bool IsDialogWindow(HWND hWnd)
{
	TCHAR Class[16] = _T("");
	return GetClassName(hWnd, Class, 16) > 0 && _tcscmp(Class, _T("#32770")) == 0;
}

// One line: "Could not save file.\n\nAccess is denied." -> "Could not save file. Access is denied."
CString OneLine(LPCTSTR pText)
{
	CString Text(pText != NULL ? pText : _T(""));
	Text.Replace(_T('\r'), _T(' '));
	Text.Replace(_T('\n'), _T(' '));
	Text.Replace(_T('\t'), _T(' '));
	while (Text.Replace(_T("  "), _T(" ")) > 0)
		;
	Text.Trim();
	return Text;
}

CString WindowText(HWND hWnd)
{
	CString Text;
	if (hWnd != NULL) {
		const int Length = GetWindowTextLength(hWnd);
		if (Length > 0) {
			GetWindowText(hWnd, Text.GetBuffer(Length + 1), Length + 1);
			Text.ReleaseBuffer();
		}
	}
	return Text;
}

// Closes a dialog as Escape would (No for a question without Cancel), noting what it said the first time
void CloseDialog(made_t &Made)
{
	if (!IsWindow(Made.hWnd))
		return;
	if (!Made.Closed) {
		Made.Closed = true;
		const CString Title = OneLine(WindowText(Made.hWnd));
		const CString Text = OneLine(WindowText(GetDlgItem(Made.hWnd, MSGBOX_TEXT)));
		CString Said = Text.IsEmpty() ? _T("a window") + (Title.IsEmpty() ? CString() : _T(" called \"") + Title + _T("\"")) :
			Text;
		CQuietUi::Note(Said);
	}
	int Command = IDCANCEL;
	if (GetDlgItem(Made.hWnd, IDCANCEL) == NULL && GetDlgItem(Made.hWnd, IDNO) != NULL)
		Command = IDNO;
	PostMessage(Made.hWnd, WM_COMMAND, MAKEWPARAM(Command, BN_CLICKED), 0);
}

made_t *FindMade(HWND hWnd)
{
	for (size_t i = 0; i < g_Made.size(); ++i)
		if (g_Made[i].hWnd == hWnd)
			return &g_Made[i];
	return NULL;
}

LRESULT CALLBACK QuietHook(int Code, WPARAM wParam, LPARAM lParam)
{
	if (Code >= 0 && g_iDepth > 0 && GetCurrentThreadId() == g_iThread) {
		const HWND hWnd = reinterpret_cast<HWND>(wParam);
		if (Code == HCBT_CREATEWND) {
			const CBT_CREATEWND *pCreate = reinterpret_cast<const CBT_CREATEWND*>(lParam);
			if (pCreate != NULL && pCreate->lpcs != NULL && (pCreate->lpcs->style & WS_CHILD) == 0 && IsDialogWindow(hWnd) &&
				FindMade(hWnd) == NULL) {
				made_t Made = { hWnd, false };
				g_Made.push_back(Made);
			}
		}
		else if (Code == HCBT_ACTIVATE) {
			made_t *pMade = FindMade(hWnd);
			if (pMade != NULL)
				CloseDialog(*pMade);
		}
		else if (Code == HCBT_DESTROYWND) {
			for (size_t i = 0; i < g_Made.size(); ++i)
				if (g_Made[i].hWnd == hWnd) {
					g_Made.erase(g_Made.begin() + i);
					break;
				}
		}
	}
	return CallNextHookEx(g_hHook, Code, wParam, lParam);
}

// A modal window's own message loop runs the thread's timers too
void CALLBACK QuietTimer(HWND hWnd, UINT Message, UINT_PTR Id, DWORD Time)
{
	(void)hWnd;
	(void)Message;
	(void)Id;
	(void)Time;
	CQuietUi::Dismiss();
}

}	// namespace

CQuietUi::CQuietUi()
{
	if (g_iDepth > 0 && GetCurrentThreadId() != g_iThread)
		return;		// another thread's: not counted (the destructor checks the same)
	if (g_iDepth++ == 0) {
		g_iThread = GetCurrentThreadId();
		g_Notes.clear();
		g_Made.clear();
		g_hHook = SetWindowsHookEx(WH_CBT, QuietHook, NULL, g_iThread);
		g_iTimer = SetTimer(NULL, 0, QUIET_TIMER_MS, QuietTimer);
	}
}

CQuietUi::~CQuietUi()
{
	if (g_iDepth == 0 || GetCurrentThreadId() != g_iThread)
		return;
	if (g_iDepth == 1) {
		Dismiss();	// told to close; they go as their loops read it
		if (g_hHook != NULL)
			UnhookWindowsHookEx(g_hHook);
		g_hHook = NULL;
		if (g_iTimer != 0)
			KillTimer(NULL, g_iTimer);
		g_iTimer = 0;
		g_Made.clear();
	}
	--g_iDepth;
}

bool CQuietUi::IsOn()
{
	return g_iDepth > 0 && GetCurrentThreadId() == g_iThread;
}

int CQuietUi::Answer(LPCTSTR pText, UINT Type)
{
	Note(OneLine(pText));
	switch (Type & MB_TYPEMASK) {
	case MB_OKCANCEL:
	case MB_RETRYCANCEL:
	case MB_YESNOCANCEL:
	case MB_CANCELTRYCONTINUE:
		return IDCANCEL;
	case MB_YESNO:
		return IDNO;
	case MB_ABORTRETRYIGNORE:
		return IDABORT;
	default:
		return IDOK;
	}
}

void CQuietUi::Note(const CString &Text)
{
	if (!IsOn() || Text.IsEmpty() || g_Notes.size() >= MAX_NOTES || (!g_Notes.empty() && g_Notes.back() == Text))
		return;
	g_Notes.push_back(Text);
}

void CQuietUi::TakeNotes(std::vector<CString> &Notes)
{
	Notes.clear();
	if (!IsOn())
		return;
	Notes.swap(g_Notes);
}

int CQuietUi::Dismiss()
{
	if (!IsOn())
		return 0;
	int Closed = 0;
	for (size_t i = 0; i < g_Made.size(); ++i)
		if (IsWindow(g_Made[i].hWnd)) {
			CloseDialog(g_Made[i]);
			++Closed;
		}
	return Closed;
}

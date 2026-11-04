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

#include <vector>

// While a call of the Claude Code connection runs, nobody may be at the computer: a modal window FamiTracker opened then
// (a message box, File > Save As's dialog) held the call, and every request after it heard "busy", until someone closed
// it. A CQuietUi, held around such a call (CMcpLink::Answer, and the Ask panel's calls), keeps every window shut:
// - AfxMessageBox, which every message of the document's goes through ("Could not save file", a file that can't be
//   opened...), answers its safe default unseen (CFamiTrackerApp::DoMessageBox): OK, No to a yes-or-no question, Cancel
//   where there is one, Abort;
// - any other dialog or message box the UI thread makes meanwhile (a window of the dialog class #32770 that isn't a
//   child: ::MessageBox, a CDialog's DoModal, a file dialog) is closed as Escape closes it (No where there's no Cancel)
//   as soon as it activates (a CBT hook), and by a timer every half second while it's open, and by Dismiss, which the
//   connection calls when another request comes while the call still waits;
// what each would have said is kept (Note) for the call's reply (TakeNotes). Scopes nest as one; only the thread that
// began it is quiet, so a message of the sound thread's still shows.
class CQuietUi {
public:
	CQuietUi();
	~CQuietUi();

	// On this thread now
	static bool IsOn();
	// A message box's answer while quiet: its safe default, its text kept
	static int Answer(LPCTSTR pText, UINT Type);
	// Keeps what a window would have said (on its own line, at most MAX_NOTES)
	static void Note(const CString &Text);
	// What was kept since the scope began or the last take, the oldest first; then forgotten
	static void TakeNotes(std::vector<CString> &Notes);
	// Closes the dialogs made while quiet that are still open; how many
	static int Dismiss();

	static const size_t MAX_NOTES = 20;

private:
	CQuietUi(const CQuietUi &);
	CQuietUi &operator=(const CQuietUi &);
};

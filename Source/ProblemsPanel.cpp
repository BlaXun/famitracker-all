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

// The problems panel (see ProblemsPanel.h)

// Standard headers come before stdafx.h, whose DEBUG_NEW macro would
// otherwise rewrite the placement new inside the standard library.
#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>
#include "Api/ProblemList.h"
#include "stdafx.h"
#pragma push_macro("new")
#undef new
#include <afxpriv.h>		// WM_SIZEPARENT, for laying out the docked bar
#pragma pop_macro("new")
#include "FamiTracker.h"
#include "FamiTrackerDoc.h"
#include "FamiTrackerView.h"
#include "MainFrm.h"
#include "PatternEditor.h"
#include "SoundGen.h"
#include "PianoRoll.h"
#include "McpConnection.h"
#include "ProblemsPanel.h"

namespace {

LPCTSTR SECTION = _T("Problems panel");	// registry section: open, where, its size, checking as you edit, kinds folded

// Sizes at 96 DPI (scaled with SX and SY)
const int DEFAULT_HEIGHT = 150;			// along the bottom
const int DEFAULT_WIDTH = 340;			// along the right
const int MIN_HEIGHT = 60;
const int MIN_WIDTH = 200;
const int MIN_VIEW_WIDTH = 300;			// what the pattern editor keeps at least, beside the panel
const int DIVIDER = 5;
const int HEADER_HEIGHT = 26;
const int LINE_HEIGHT = 19;
const int MARGIN = 6;
const int INDENT = 24;					// a finding, under its kind
const int WHERE_WIDTH = 104;			// "bar 12 beat 3.5"
const int CHANNEL_WIDTH = 104;			// "VRC6 Sawtooth"
const int ACTION_WIDTH = 62;			// the button on the selected line
const int NARROW_WIDTH = 470;			// narrower than this, a finding takes two lines

const UINT_PTR TMR_CHECK = 1;			// checks a moment after the song changed
const UINT CHECK_MS = 800;
const UINT WAIT_MS = 500;				// tries again: the song plays, or Claude Code is being answered
const UINT_PTR TMR_LOOK = 2;			// looks for changes nothing tells the panel of
const UINT LOOK_MS = 1000;

// Colours, as the clips panel's
const COLORREF PANEL_BACK = RGB(232, 234, 238);
const COLORREF LIST_BACK = RGB(255, 255, 255);
const COLORREF KIND_BACK = RGB(241, 243, 247);
const COLORREF LINE_HOVER = RGB(238, 244, 252);
const COLORREF LINE_SELECTED = RGB(204, 225, 252);
const COLORREF LINE_SELECTED_DIM = RGB(222, 226, 234);	// while the panel doesn't have the keyboard
const COLORREF TEXT_DARK = RGB(30, 32, 40);
const COLORREF TEXT_DIM = RGB(100, 104, 112);
const COLORREF BUTTON_BACK = RGB(250, 251, 253);
const COLORREF BUTTON_BORDER = RGB(160, 166, 178);
const COLORREF BUTTON_HOT = RGB(60, 120, 215);

// The right-click menu
enum {
	MENU_GO = 1, MENU_ACTION, MENU_EXPLAIN, MENU_COPY, MENU_FOLD, MENU_CHECK, MENU_AUTO, MENU_DOCK, MENU_CLOSE
};

// What a finding's button does
enum action_t { ACTION_NONE, ACTION_NUDGE, ACTION_SHOW, ACTION_EXPLAIN };

// What the pattern editor and the piano roll mark (ProblemMarkAt)
ftapi::ProblemMarks g_Marks;

// Text from the tools (UTF-8) as FamiTracker shows it (the Windows code page)
CString FromUtf8(const std::string &Text)
{
	if (Text.empty())
		return CString();
	const int Wide = ::MultiByteToWideChar(CP_UTF8, 0, Text.c_str(), static_cast<int>(Text.size()), NULL, 0);
	if (Wide <= 0)
		return CString(Text.c_str());
	std::vector<wchar_t> Buffer(static_cast<size_t>(Wide));
	::MultiByteToWideChar(CP_UTF8, 0, Text.c_str(), static_cast<int>(Text.size()), &Buffer[0], Wide);
	const int Bytes = ::WideCharToMultiByte(CP_ACP, 0, &Buffer[0], Wide, NULL, 0, NULL, NULL);
	if (Bytes <= 0)
		return CString(Text.c_str());
	std::vector<char> Out(static_cast<size_t>(Bytes) + 1, 0);
	::WideCharToMultiByte(CP_ACP, 0, &Buffer[0], Wide, &Out[0], Bytes, NULL, NULL);
	return CString(&Out[0]);
}

CString Plural(int Count, LPCTSTR One, LPCTSTR Many)
{
	CString Text;
	Text.Format(_T("%d %s"), Count, Count == 1 ? One : Many);
	return Text;
}

// A line of the list as it's drawn: a kind's heading, or one of its lines
struct shown_t {
	int Kind;
	int Line;		// into the kind's lines, -1 for its heading
	int Top;		// in the list, as if nothing were scrolled
	int Height;
};

// A small triangle: pointing down (open) or right (folded)
void Triangle(CDC &dc, int X, int Y, bool Open, COLORREF Color)
{
	for (int i = 0; i < 4; ++i) {
		if (Open)
			dc.FillSolidRect(X + i, Y + i, 7 - 2 * i, 1, Color);
		else
			dc.FillSolidRect(X + i, Y - 3 + i, 1, 7 - 2 * i, Color);
	}
}

}	// namespace

int ProblemMarkAt(int Track, int Channel, int Row)
{
	return g_Marks.At(Track, Channel, Row);
}

COLORREF ProblemMarkColor(int Mark)
{
	return static_cast<COLORREF>(ftapi::ProblemMarks::Color(Mark));
}

bool ProblemMarkSelected(int Mark)
{
	return ftapi::ProblemMarks::IsSelected(Mark);
}

struct CProblemsPanelWnd::Impl {
	CProblemsPanelWnd *pWnd;
	CMcpConnection *pTools;
	ftapi::ProblemList List;
	std::vector<shown_t> Shown;
	bool Folded[ftapi::PROBLEM_KINDS];
	int Selected;						// into Shown, -1 for none
	int Hover;							// into Shown, -1 for none
	bool HoverButton;
	int Scroll;							// pixels the list is scrolled down
	int Content;						// the list's height
	bool Open;							// shown
	bool Checked;						// the list is the track as it was at the last check
	bool Waiting;						// a check is due
	bool Auto;							// checks a moment after each change
	bool Focused;
	CString Trouble;					// why the last check didn't go through
	DWORD CheckTime;					// how long the last check took (ms)
	unsigned int Signature;				// the song's settings and instruments at the last check
	CRect Header, Body, CheckRect, MenuRect;
	bool Narrow;
	CFont Font, BoldFont, SmallFont;

	Impl(CProblemsPanelWnd *p) : pWnd(p), pTools(NULL), Selected(-1), Hover(-1), HoverButton(false), Scroll(0), Content(0), Open(false),
		Checked(false), Waiting(false), Auto(true), Focused(false), CheckTime(0), Signature(0), Narrow(false)
	{
		const UINT Folds = theApp.GetProfileInt(SECTION, _T("Folded"), 0);
		for (int k = 0; k < ftapi::PROBLEM_KINDS; ++k)
			Folded[k] = (Folds & (1u << k)) != 0;
		Auto = theApp.GetProfileInt(SECTION, _T("Check as you edit"), 1) != 0;
	}

	static CMainFrame *Main() { return static_cast<CMainFrame*>(theApp.m_pMainWnd); }
	static CFamiTrackerView *View() { return CFamiTrackerView::GetView(); }
	static CFamiTrackerDoc *Doc()
	{
		CMainFrame *pMain = Main();
		return pMain != NULL ? static_cast<CFamiTrackerDoc*>(pMain->GetActiveDocument()) : NULL;
	}

	static void Say(LPCTSTR Text)
	{
		CMainFrame *pMain = Main();
		if (pMain != NULL)
			pMain->SetMessageText(Text);
	}

	void MakeFonts()
	{
		LOGFONT Log;
		memset(&Log, 0, sizeof(Log));
		::GetObject(::GetStockObject(DEFAULT_GUI_FONT), sizeof(Log), &Log);
		Log.lfHeight = -SY(12);
		_tcscpy_s(Log.lfFaceName, LF_FACESIZE, _T("Segoe UI"));
		Font.CreateFontIndirect(&Log);
		Log.lfWeight = FW_BOLD;
		BoldFont.CreateFontIndirect(&Log);
		Log.lfWeight = FW_NORMAL;
		Log.lfHeight = -SY(11);
		SmallFont.CreateFontIndirect(&Log);
	}

	void SaveFolds() const
	{
		UINT Folds = 0;
		for (int k = 0; k < ftapi::PROBLEM_KINDS; ++k)
			if (Folded[k])
				Folds |= 1u << k;
		theApp.WriteProfileInt(SECTION, _T("Folded"), Folds);
	}

	// The lines

	const ftapi::ProblemLine *LineOf(int Index) const
	{
		if (Index < 0 || Index >= static_cast<int>(Shown.size()) || Shown[static_cast<size_t>(Index)].Line < 0)
			return NULL;
		const shown_t &S = Shown[static_cast<size_t>(Index)];
		return &List.Lines(S.Kind)[static_cast<size_t>(S.Line)];
	}

	const ftapi::ProblemFinding *FindingOf(int Index) const
	{
		const ftapi::ProblemLine *pLine = LineOf(Index);
		return pLine != NULL ? &List.Findings()[static_cast<size_t>(pLine->Finding)] : NULL;
	}

	action_t ActionOf(int Index) const
	{
		const ftapi::ProblemFinding *pFinding = FindingOf(Index);
		if (pFinding == NULL)
			return ACTION_NONE;
		if (!pFinding->FixTool.empty())
			return ACTION_NUDGE;
		if (pFinding->Kind == ftapi::PROBLEM_VANISHING && !pFinding->Notes.empty())
			return ACTION_SHOW;
		return ACTION_EXPLAIN;
	}

	static LPCTSTR ActionName(action_t Action)
	{
		switch (Action) {
		case ACTION_NUDGE: return _T("Nudge");
		case ACTION_SHOW: return _T("Show");
		case ACTION_EXPLAIN: return _T("Explain");
		default: return _T("");
		}
	}

	// The lines as drawn, from the list and the kinds folded
	void Arrange()
	{
		CRect Client;
		pWnd->GetClientRect(&Client);
		Narrow = Client.Width() < SX(NARROW_WIDTH);
		Header = CRect(Client.left, Client.top, Client.right, Client.top + SY(HEADER_HEIGHT));
		Body = CRect(Client.left, Header.bottom, Client.right, std::max(Header.bottom, Client.bottom));
		const int Margin = std::max(2, SX(MARGIN));
		CheckRect = CRect(Client.right - Margin - SX(28) - SX(4) - SX(62), Header.top + SY(3), Client.right - Margin - SX(28) - SX(4),
			Header.bottom - SY(4));
		MenuRect = CRect(Client.right - Margin - SX(28), Header.top + SY(3), Client.right - Margin, Header.bottom - SY(4));

		Shown.clear();
		int Top = 0;
		for (int k = 0; k < ftapi::PROBLEM_KINDS; ++k) {
			const std::vector<ftapi::ProblemLine> &Lines = List.Lines(k);
			if (Lines.empty())
				continue;
			const shown_t Heading = { k, -1, Top, SY(LINE_HEIGHT) };
			Shown.push_back(Heading);
			Top += Heading.Height;
			if (Folded[k])
				continue;
			const int Height = Narrow ? SY(LINE_HEIGHT) * 2 - SY(2) : SY(LINE_HEIGHT);
			for (size_t i = 0; i < Lines.size(); ++i) {
				const shown_t Line = { k, static_cast<int>(i), Top, Height };
				Shown.push_back(Line);
				Top += Height;
			}
		}
		Content = Top + (Shown.empty() ? 0 : SY(4));
		if (Selected >= static_cast<int>(Shown.size()))
			Selected = static_cast<int>(Shown.size()) - 1;
		if (Hover >= static_cast<int>(Shown.size()))
			Hover = -1;
		ScrollTo(Scroll, false);
	}

	CRect LineRect(int Index) const
	{
		const shown_t &S = Shown[static_cast<size_t>(Index)];
		return CRect(Body.left, Body.top + S.Top - Scroll, Body.right, Body.top + S.Top - Scroll + S.Height);
	}

	CRect ButtonRect(int Index) const
	{
		const CRect Line = LineRect(Index);
		const int Height = SY(LINE_HEIGHT);
		return CRect(Line.right - SX(MARGIN) - SX(ACTION_WIDTH), Line.top + SY(2), Line.right - SX(MARGIN), Line.top + Height - SY(2));
	}

	int LineAt(const CPoint &Point, bool *pOnButton) const
	{
		if (pOnButton != NULL)
			*pOnButton = false;
		if (!Body.PtInRect(Point))
			return -1;
		for (size_t i = 0; i < Shown.size(); ++i) {
			if (!LineRect(static_cast<int>(i)).PtInRect(Point))
				continue;
			if (pOnButton != NULL && Shown[i].Line >= 0 && (static_cast<int>(i) == Selected || static_cast<int>(i) == Hover))
				*pOnButton = ButtonRect(static_cast<int>(i)).PtInRect(Point) != FALSE;
			return static_cast<int>(i);
		}
		return -1;
	}

	void ScrollTo(int Position, bool Redraw = true)
	{
		const int Max = std::max(0, Content - Body.Height());
		Scroll = std::max(0, std::min(Max, Position));
		SCROLLINFO Info;
		memset(&Info, 0, sizeof(Info));
		Info.cbSize = sizeof(Info);
		Info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
		Info.nMin = 0;
		Info.nMax = std::max(0, Content - 1);
		Info.nPage = static_cast<UINT>(std::max(1, Body.Height()));
		Info.nPos = Scroll;
		pWnd->SetScrollInfo(SB_VERT, &Info, TRUE);
		if (Redraw)
			pWnd->Invalidate(FALSE);
	}

	// Scrolls so a line is in sight
	void Reveal(int Index)
	{
		if (Index < 0 || Index >= static_cast<int>(Shown.size()))
			return;
		const shown_t &S = Shown[static_cast<size_t>(Index)];
		if (S.Top < Scroll)
			ScrollTo(S.Top);
		else if (S.Top + S.Height > Scroll + Body.Height())
			ScrollTo(S.Top + S.Height - Body.Height());
	}

	// What the selected line was, to find it again after a check: its kind, channel and row
	struct place_t {
		int Kind, Channel, Row, Index;
	};

	place_t SelectedPlace() const
	{
		place_t Place = { -1, -1, -1, -1 };
		if (Selected < 0 || Selected >= static_cast<int>(Shown.size()))
			return Place;
		const shown_t &S = Shown[static_cast<size_t>(Selected)];
		Place.Kind = S.Kind;
		Place.Index = S.Line;
		const ftapi::ProblemLine *pLine = LineOf(Selected);
		if (pLine != NULL) {
			Place.Channel = pLine->Channel;
			Place.Row = pLine->Row;
		}
		return Place;
	}

	// The same line again (the same kind, channel and row), else the one that took its place in its kind (a fixed
	// one's next), else the kind's heading
	void Reselect(const place_t &Place)
	{
		Selected = -1;
		if (Place.Kind < 0)
			return;
		int Heading = -1, Same = -1, Next = -1, Last = -1;
		for (size_t i = 0; i < Shown.size(); ++i) {
			if (Shown[i].Kind != Place.Kind)
				continue;
			if (Shown[i].Line < 0) {
				Heading = static_cast<int>(i);
				continue;
			}
			const ftapi::ProblemLine *pLine = LineOf(static_cast<int>(i));
			if (pLine->Channel == Place.Channel && pLine->Row == Place.Row && Same < 0)
				Same = static_cast<int>(i);
			if (Shown[i].Line == Place.Index)
				Next = static_cast<int>(i);
			Last = static_cast<int>(i);
		}
		if (Place.Index < 0)
			Selected = Heading;
		else if (Same >= 0)
			Selected = Same;
		else if (Next >= 0)
			Selected = Next;
		else if (Last >= 0)
			Selected = Last;
		else
			Selected = Heading;
	}

	// Checking

	// The song's settings and instruments, cheaply: what changes without a notice (an instrument's envelope, the
	// speed, the track) changes this
	unsigned int MakeSignature() const
	{
		CFamiTrackerDoc *pDoc = Doc();
		CMainFrame *pMain = Main();
		if (pDoc == NULL || pMain == NULL || !pDoc->IsFileLoaded())
			return 0;
		unsigned int Hash = 2166136261u;
		auto Add = [&Hash](unsigned int Value) {
			Hash = (Hash ^ Value) * 16777619u;
		};
		const unsigned int Track = static_cast<unsigned int>(std::max(0, pMain->GetSelectedTrack()));
		Add(Track);
		if (Track >= pDoc->GetTrackCount())
			return Hash;
		Add(pDoc->GetFrameCount(Track));
		Add(pDoc->GetPatternLength(Track));
		Add(pDoc->GetSongSpeed(Track));
		Add(pDoc->GetSongTempo(Track));
		Add(static_cast<unsigned int>(pDoc->GetFirstHighlight()));
		Add(static_cast<unsigned int>(pDoc->GetSecondHighlight()));
		Add(pDoc->GetExpansionChip());
		Add(pDoc->GetMachine());
		Add(pDoc->GetNamcoChannels());
		const unsigned int Channels = pDoc->GetAvailableChannels();
		Add(Channels);
		for (unsigned int c = 0; c < Channels; ++c)
			Add(pDoc->GetEffColumns(Track, c));
		for (unsigned int i = 0; i < MAX_INSTRUMENTS; ++i) {
			if (!pDoc->IsInstrumentUsed(i))
				continue;
			Add(i);
			CInstrument *pInstrument = pDoc->GetInstrument(i);	// retained
			if (pInstrument == NULL)
				continue;
			const int Type = pInstrument->GetType();
			Add(static_cast<unsigned int>(Type));
			for (int s = 0; s < SEQ_COUNT; ++s) {
				int Chip = SNDCHIP_NONE, Enabled = 0, Index = 0;
				if (Type == INST_2A03) {
					const CInstrument2A03 *p = static_cast<const CInstrument2A03 *>(pInstrument);
					Enabled = p->GetSeqEnable(s);
					Index = p->GetSeqIndex(s);
				}
				else if (Type == INST_VRC6) {
					const CInstrumentVRC6 *p = static_cast<const CInstrumentVRC6 *>(pInstrument);
					Chip = SNDCHIP_VRC6;
					Enabled = p->GetSeqEnable(s);
					Index = p->GetSeqIndex(s);
				}
				else if (Type == INST_N163) {
					const CInstrumentN163 *p = static_cast<const CInstrumentN163 *>(pInstrument);
					Chip = SNDCHIP_N163;
					Enabled = p->GetSeqEnable(s);
					Index = p->GetSeqIndex(s);
				}
				Add(static_cast<unsigned int>(Enabled));
				if (Enabled == 0)
					continue;
				Add(static_cast<unsigned int>(Index));
				const CSequence *pSeq = pDoc->GetSequence(Chip, static_cast<unsigned int>(Index), s);
				if (pSeq == NULL)
					continue;
				Add(pSeq->GetItemCount());
				Add(pSeq->GetLoopPoint());
				Add(pSeq->GetReleasePoint());
				Add(pSeq->GetSetting());
				for (unsigned int k = 0; k < pSeq->GetItemCount(); ++k)
					Add(static_cast<unsigned int>(static_cast<unsigned char>(pSeq->GetItem(static_cast<int>(k)))));
			}
			if (Type == INST_N163) {
				const CInstrumentN163 *p = static_cast<const CInstrumentN163 *>(pInstrument);
				Add(static_cast<unsigned int>(p->GetWaveSize()));
			}
			pInstrument->Release();
		}
		return Hash;
	}

	// A check in a moment (After ms), or now
	void CheckSoon(UINT After)
	{
		Waiting = true;
		if (pWnd->GetSafeHwnd() != NULL)
			pWnd->SetTimer(TMR_CHECK, After, NULL);
	}

	// check_song on the selected track, read back; the marks follow. Now: also while the song plays; Wait: with the
	// wait cursor (asked for)
	void Check(bool Now, bool Wait = false)
	{
		pWnd->KillTimer(TMR_CHECK);
		CFamiTrackerDoc *pDoc = Doc();
		if (pTools == NULL || pDoc == NULL || !pDoc->IsFileLoaded()) {
			Waiting = false;
			List.Clear();
			Checked = false;
			Trouble = _T("No song is open.");
			Changed();
			return;
		}
		if (pTools->IsBusy()) {			// answering Claude Code: in a moment
			CheckSoon(WAIT_MS);
			return;
		}
		if (!Now && theApp.IsPlaying()) {	// not while the song plays: when it stops
			CheckSoon(WAIT_MS);
			pWnd->Invalidate(FALSE);
			return;
		}
		Waiting = false;
		if (Wait)
			pWnd->BeginWaitCursor();
		const DWORD Start = ::GetTickCount();
		std::string Reply;
		const bool Done = pTools->RunTool("check_song", "{\"format\":\"json\"}", Reply);
		CheckTime = ::GetTickCount() - Start;
		if (Wait)
			pWnd->EndWaitCursor();
		Signature = MakeSignature();
		const place_t Place = SelectedPlace();
		std::string Error;
		if (!Done || !List.Read(Reply, Error)) {
			List.Clear();
			Checked = false;
			Trouble = FromUtf8(Done ? Error : Reply);
		}
		else {
			Checked = true;
			Trouble.Empty();
		}
		Arrange();
		Reselect(Place);
		Changed();
	}

	// The list changed, or the selection did: the marks follow, and everything is drawn again
	void Changed()
	{
		const ftapi::ProblemMarks Before = g_Marks;
		CFamiTrackerDoc *pDoc = Doc();
		CMainFrame *pMain = Main();
		const ftapi::ProblemFinding *pFinding = FindingOf(Selected);
		const int Chosen = pFinding != NULL ? static_cast<int>(pFinding - &List.Findings()[0]) : -1;
		if (!Open || !Checked || pDoc == NULL || pMain == NULL || List.Track() != pMain->GetSelectedTrack())
			g_Marks.Clear();
		else {
			const unsigned int Track = static_cast<unsigned int>(List.Track());
			g_Marks.Set(List, static_cast<int>(pDoc->GetAvailableChannels()),
				static_cast<int>(pDoc->GetPatternLength(Track) * pDoc->GetFrameCount(Track)), Chosen);
		}
		if (Before != g_Marks)
			RedrawViews();
		pWnd->Invalidate(FALSE);
	}

	static void RedrawViews()
	{
		CFamiTrackerView *pView = View();
		if (pView != NULL && pView->GetSafeHwnd() != NULL) {
			pView->GetPatternEditor()->InvalidatePatternData();
			pView->Invalidate(FALSE);
		}
		CMainFrame *pMain = Main();
		CPianoRollWnd *pRoll = pMain != NULL ? pMain->GetPianoRollWnd() : NULL;
		if (pRoll != NULL)
			pRoll->Invalidate(FALSE);
	}

	// Going there, and fixing

	void Select(int Index, bool Show = true)
	{
		if (Shown.empty())
			Index = -1;
		else
			Index = std::max(0, std::min(Index, static_cast<int>(Shown.size()) - 1));
		if (Index == Selected)
			return;
		Selected = Index;
		Reveal(Index);
		Changed();
		const ftapi::ProblemFinding *pFinding = FindingOf(Index);
		if (Show && pFinding != NULL)
			Say(FromUtf8(pFinding->Message));
	}

	void Fold(int Kind, bool Fold)
	{
		if (Kind < 0 || Kind >= ftapi::PROBLEM_KINDS || Folded[Kind] == Fold)
			return;
		const place_t Place = SelectedPlace();
		Folded[Kind] = Fold;
		SaveFolds();
		Arrange();
		if (Place.Kind == Kind && Fold) {
			for (size_t i = 0; i < Shown.size(); ++i)
				if (Shown[i].Kind == Kind && Shown[i].Line < 0)
					Selected = static_cast<int>(i);
		}
		else
			Reselect(Place);
		Changed();
	}

	// The pattern editor's cursor to a line's place (its frame, row and channel), and the piano roll there too
	bool GoTo(int Index)
	{
		const ftapi::ProblemLine *pLine = LineOf(Index);
		CFamiTrackerView *pView = View();
		CFamiTrackerDoc *pDoc = Doc();
		CMainFrame *pMain = Main();
		if (pLine == NULL || pView == NULL || pDoc == NULL || pMain == NULL)
			return false;
		const unsigned int Track = static_cast<unsigned int>(std::max(0, pMain->GetSelectedTrack()));
		if (List.Track() != static_cast<int>(Track)) {
			Say(_T("That was found in another track: check this one first."));
			return false;
		}
		if (theApp.IsPlaying() && pView->GetFollowMode()) {
			Say(_T("Stop playing to go there (the cursor follows the song while it plays)."));
			return false;
		}
		const int Length = static_cast<int>(std::max(1U, pDoc->GetPatternLength(Track)));
		const int Frame = pLine->Row / Length, Row = pLine->Row % Length;
		if (Frame >= static_cast<int>(pDoc->GetFrameCount(Track))) {
			Say(_T("That's past the song's end now; the panel checks again in a moment."));
			return false;
		}
		const int Channel = pLine->Channel >= 0 && pLine->Channel < static_cast<int>(pDoc->GetAvailableChannels()) ?
			pLine->Channel : static_cast<int>(pView->GetSelectedChannel());
		pView->SelectFrame(static_cast<unsigned int>(Frame));
		pView->GetPatternEditor()->MoveToRow(Row);
		pView->SelectChannel(static_cast<unsigned int>(Channel));	// draws the cursor where it is now
		const ftapi::ProblemNote *pNote = List.LineNote(*pLine);
		CPianoRollWnd *pRoll = pMain->GetPianoRollWnd();
		if (pRoll != NULL && pLine->Channel >= 0) {
			const int Start = pNote != NULL && pNote->Channel == pLine->Channel ? pNote->Row : pLine->Row;
			pRoll->ShowNotes(pLine->Channel, &Start, 1, false);
		}
		const ftapi::ProblemFinding *pFinding = FindingOf(Index);
		if (pFinding != NULL)
			Say(FromUtf8(pFinding->Message));
		return true;
	}

	// Nudge: the fix check_song gives (polish's "clicks" touch on the note's bar), one undoable step; then a check,
	// so the count goes down
	void Nudge(int Index)
	{
		const ftapi::ProblemFinding *pFinding = FindingOf(Index);
		const ftapi::ProblemLine *pLine = LineOf(Index);
		if (pFinding == NULL || pLine == NULL || pFinding->FixTool.empty() || pTools == NULL)
			return;
		const ftapi::ProblemNote *pNote = List.LineNote(*pLine);
		CString Done;
		Done.Format(_T("Nudged %s at %s: %s. Ctrl+Z undoes it."), pNote != NULL ? static_cast<LPCTSTR>(FromUtf8(pNote->Name)) : _T("the note"),
			static_cast<LPCTSTR>(FromUtf8(pFinding->Position)), static_cast<LPCTSTR>(FromUtf8(pFinding->FixDoes)));
		std::string Reply;
		if (!pTools->RunTool(pFinding->FixTool.c_str(), pFinding->FixArguments, Reply)) {
			CString Text = FromUtf8(Reply);
			Text.Replace(_T("\n"), _T("\r\n"));
			AfxMessageBox(Text, MB_OK | MB_ICONINFORMATION);
			return;
		}
		Check(true, true);
		Say(Done);
	}

	// Show: notes that vanish, selected in the piano roll (opened for them), where another instrument can be given
	// them; the cursor on the line's own note
	void ShowNotes(int Index)
	{
		const ftapi::ProblemFinding *pFinding = FindingOf(Index);
		const ftapi::ProblemLine *pLine = LineOf(Index);
		CMainFrame *pMain = Main();
		if (pFinding == NULL || pLine == NULL || pMain == NULL || pFinding->Notes.empty())
			return;
		if (pMain->GetPianoRollWnd() == NULL)
			pMain->ShowPianoRoll(true);
		if (!GoTo(Index))
			return;
		CPianoRollWnd *pRoll = pMain->GetPianoRollWnd();
		if (pRoll == NULL)
			return;
		std::vector<int> Starts;
		const int Channel = pLine->Channel;
		const ftapi::ProblemNote *pNote = List.LineNote(*pLine);
		if (pNote != NULL)
			Starts.push_back(pNote->Row);	// the line's own first, where the roll goes
		for (size_t n = 0; n < pFinding->Notes.size(); ++n)
			if (pFinding->Notes[n].Channel == Channel && (pNote == NULL || pFinding->Notes[n].Row != pNote->Row))
				Starts.push_back(pFinding->Notes[n].Row);
		if (Starts.empty())
			return;
		pRoll->ShowNotes(Channel, &Starts[0], static_cast<int>(Starts.size()), true);
		CString Text;
		Text.Format(_T("%s selected in the piano roll: its Instrument button > Give the selected notes gives them a sound that ")
			_T("lasts, such as the held notes' one."), static_cast<LPCTSTR>(Plural(static_cast<int>(Starts.size()),
			_T("note that vanishes"), _T("notes that vanish"))));
		Say(Text);
	}

	void Explain(int Index) const
	{
		const ftapi::ProblemFinding *pFinding = FindingOf(Index);
		if (pFinding == NULL)
			return;
		CString Text = FromUtf8(pFinding->Message);
		if (!pFinding->FixDoes.empty())
			Text += _T("\r\n\r\nNudge ") + FromUtf8(pFinding->FixDoes) + _T(", as one Ctrl+Z.");
		Text.Replace(_T("\n"), _T("\r\n"));
		Text.Replace(_T("\r\r\n"), _T("\r\n"));
		AfxMessageBox(Text, MB_OK | MB_ICONINFORMATION);
	}

	void Act(int Index)
	{
		switch (ActionOf(Index)) {
		case ACTION_NUDGE: Nudge(Index); break;
		case ACTION_SHOW: ShowNotes(Index); break;
		case ACTION_EXPLAIN: Explain(Index); break;
		default: break;
		}
	}

	void Copy(int Index) const
	{
		const ftapi::ProblemFinding *pFinding = FindingOf(Index);
		if (pFinding == NULL || !pWnd->OpenClipboard())
			return;
		const CString Text = FromUtf8(pFinding->Message);
		::EmptyClipboard();
		HGLOBAL hMemory = ::GlobalAlloc(GMEM_MOVEABLE, static_cast<SIZE_T>(Text.GetLength() + 1));
		if (hMemory != NULL) {
			char *pText = static_cast<char*>(::GlobalLock(hMemory));
			if (pText != NULL) {
				memcpy(pText, static_cast<LPCSTR>(Text), static_cast<size_t>(Text.GetLength()) + 1);
				::GlobalUnlock(hMemory);
				if (::SetClipboardData(CF_TEXT, hMemory) == NULL)
					::GlobalFree(hMemory);
			}
			else
				::GlobalFree(hMemory);
		}
		::CloseClipboard();
	}

	void Menu(int Index, CPoint Screen)
	{
		CMenu Popup;
		if (!Popup.CreatePopupMenu())
			return;
		const ftapi::ProblemFinding *pFinding = FindingOf(Index);
		const action_t Action = ActionOf(Index);
		if (pFinding != NULL) {
			Popup.AppendMenu(MF_STRING, MENU_GO, _T("Go there\tEnter"));
			if (Action == ACTION_NUDGE)
				Popup.AppendMenu(MF_STRING, MENU_ACTION, _T("Nudge it\tSpace"));
			else if (Action == ACTION_SHOW)
				Popup.AppendMenu(MF_STRING, MENU_ACTION, _T("Select them in the piano roll\tSpace"));
			Popup.AppendMenu(MF_STRING, MENU_EXPLAIN, Action == ACTION_EXPLAIN ? _T("Explain...\tSpace") : _T("Explain..."));
			Popup.AppendMenu(MF_STRING, MENU_COPY, _T("Copy\tCtrl+C"));
			Popup.AppendMenu(MF_SEPARATOR);
		}
		else if (Index >= 0 && Index < static_cast<int>(Shown.size())) {
			Popup.AppendMenu(MF_STRING, MENU_FOLD, Folded[Shown[static_cast<size_t>(Index)].Kind] ? _T("Unfold") : _T("Fold"));
			Popup.AppendMenu(MF_SEPARATOR);
		}
		Popup.AppendMenu(MF_STRING, MENU_CHECK, _T("Check now\tF5"));
		Popup.AppendMenu(MF_STRING | (Auto ? MF_CHECKED : MF_UNCHECKED), MENU_AUTO, _T("Check as you edit"));
		CProblemsBar *pBar = static_cast<CProblemsBar*>(pWnd->GetParent());
		Popup.AppendMenu(MF_STRING, MENU_DOCK, pBar->IsOnRight() ? _T("Dock at the bottom") : _T("Dock on the right"));
		Popup.AppendMenu(MF_STRING, MENU_CLOSE, _T("Close"));
		const UINT Chosen = Popup.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY, Screen.x, Screen.y, pWnd);
		switch (Chosen) {
		case MENU_GO: GoTo(Index); break;
		case MENU_ACTION: Act(Index); break;
		case MENU_EXPLAIN: Explain(Index); break;
		case MENU_COPY: Copy(Index); break;
		case MENU_FOLD:
			if (Index >= 0 && Index < static_cast<int>(Shown.size()))
				Fold(Shown[static_cast<size_t>(Index)].Kind, !Folded[Shown[static_cast<size_t>(Index)].Kind]);
			break;
		case MENU_CHECK: Check(true, true); break;
		case MENU_AUTO:
			Auto = !Auto;
			theApp.WriteProfileInt(SECTION, _T("Check as you edit"), Auto ? 1 : 0);
			if (Auto)
				CheckSoon(CHECK_MS);
			pWnd->Invalidate(FALSE);
			break;
		case MENU_DOCK: pBar->DockOnRight(!pBar->IsOnRight()); break;
		case MENU_CLOSE: AfxGetMainWnd()->PostMessage(WM_COMMAND, ID_VIEW_PROBLEMS); break;
		default: break;
		}
	}

	// The keys, before FamiTracker's shortcuts see them; true when one was used
	bool KeyDown(UINT Code, bool Ctrl, bool Shift, bool Alt)
	{
		if (Alt)
			return false;
		const int Page = std::max(1, Body.Height() / std::max(1, SY(LINE_HEIGHT)) - 1);
		const int Last = static_cast<int>(Shown.size()) - 1;
		const bool OnHeading = Selected >= 0 && Selected <= Last && Shown[static_cast<size_t>(Selected)].Line < 0;
		const int Kind = Selected >= 0 && Selected <= Last ? Shown[static_cast<size_t>(Selected)].Kind : -1;
		switch (Code) {
		case VK_UP: Select(Selected < 0 ? 0 : Selected - 1); return true;
		case VK_DOWN: Select(Selected + 1); return true;
		case VK_PRIOR: Select(Selected < 0 ? 0 : Selected - Page); return true;
		case VK_NEXT: Select(Selected + Page); return true;
		case VK_HOME: Select(0); return true;
		case VK_END: Select(Last); return true;
		case VK_LEFT:
			if (OnHeading)
				Fold(Kind, true);
			else if (Kind >= 0) {
				for (int i = Selected; i >= 0; --i)
					if (Shown[static_cast<size_t>(i)].Line < 0) {
						Select(i);
						break;
					}
			}
			return true;
		case VK_RIGHT:
			if (OnHeading)
				Fold(Kind, false);
			return true;
		case VK_RETURN:
			if (OnHeading)
				Fold(Kind, !Folded[Kind]);
			else
				GoTo(Selected);
			return true;
		case VK_SPACE:
			if (OnHeading)
				Fold(Kind, !Folded[Kind]);
			else
				Act(Selected);
			return true;
		case VK_F5:
			Check(true, true);
			return true;
		case VK_ESCAPE: {
			CFamiTrackerView *pView = View();
			if (pView != NULL)
				pView->SetFocus();
			return true;
		}
		case 'C':
			if (!Ctrl || Shift || FindingOf(Selected) == NULL)
				return false;
			Copy(Selected);
			return true;
		case VK_F10:
			if (!Shift)
				return false;
			// Shift+F10: the menu, as the menu key opens it
		case VK_APPS: {
			const CRect Rect = Selected >= 0 && Selected <= Last ? LineRect(Selected) : Body;
			CPoint Point(Rect.left + SX(INDENT), std::min(Rect.bottom, Body.bottom));
			pWnd->ClientToScreen(&Point);
			Menu(Selected, Point);
			return true;
		}
		default:
			return false;
		}
	}

	// Drawing

	void DrawButton(CDC &dc, const CRect &Rect, LPCTSTR Text, bool Hot, bool Dim)
	{
		CBrush Back(BUTTON_BACK);
		CPen Border(PS_SOLID, 1, Hot ? BUTTON_HOT : BUTTON_BORDER);
		CBrush *pOldBrush = dc.SelectObject(&Back);
		CPen *pOldPen = dc.SelectObject(&Border);
		dc.RoundRect(&Rect, CPoint(SX(6), SY(6)));
		dc.SelectObject(pOldBrush);
		dc.SelectObject(pOldPen);
		CFont *pOld = dc.SelectObject(&Font);
		dc.SetTextColor(Dim ? TEXT_DIM : (Hot ? BUTTON_HOT : TEXT_DARK));
		CRect Inside(Rect);
		dc.DrawText(Text, &Inside, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		dc.SelectObject(pOld);
	}

	// A kind's colour, as its marks are drawn
	static COLORREF KindColor(int Kind)
	{
		return ProblemMarkColor(Kind + 1);
	}

	// "75 vanishing, 3 clicking, 2 clashes": what the last check found, in short
	CString Summary() const
	{
		static const LPCTSTR WORDS[ftapi::PROBLEM_KINDS][2] = {
			{ _T("note vanishes"), _T("notes vanish") }, { _T("note clicks"), _T("notes click") }, { _T("clash"), _T("clashes") },
			{ _T("out of range"), _T("out of range") }, { _T("silent bar"), _T("silent bars") }, { _T("busy bar"), _T("busy bars") },
			{ _T("outside the key"), _T("outside the key") }
		};
		CString Text;
		for (int k = 0; k < ftapi::PROBLEM_KINDS; ++k) {
			const int Count = static_cast<int>(List.Lines(k).size());
			if (Count == 0)
				continue;
			if (!Text.IsEmpty())
				Text += _T(", ");
			Text += Plural(Count, WORDS[k][0], WORDS[k][1]);
		}
		return Text;
	}

	void Paint(CDC &dc, const CRect &Client)
	{
		dc.FillSolidRect(&Client, LIST_BACK);
		dc.SetBkMode(TRANSPARENT);
		CFont *pOldFont = dc.SelectObject(&BoldFont);

		// The heading: what was found, and the buttons
		dc.FillSolidRect(&Header, PANEL_BACK);
		dc.FillSolidRect(Header.left, Header.bottom - 1, Header.Width(), 1, RGB(208, 212, 220));
		dc.SetTextColor(TEXT_DARK);
		CRect Title(Header.left + SX(MARGIN), Header.top, CheckRect.left - SX(MARGIN), Header.bottom);
		dc.DrawText(_T("Problems"), &Title, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		const int TitleWidth = dc.GetTextExtent(_T("Problems")).cx;
		dc.SelectObject(&Font);
		dc.SetTextColor(TEXT_DIM);
		CString State;
		if (!Trouble.IsEmpty())
			State = Trouble;
		else if (!Checked)
			State = Waiting ? _T("Checks when the song stops playing") : _T("Not checked yet");
		else if (List.Findings().empty())
			State = _T("Nothing stands out");
		else
			State = Summary();
		if (Checked && Waiting && Trouble.IsEmpty())
			State += theApp.IsPlaying() ? _T(" (checks again when the song stops)") : _T(" (checking soon)");
		CRect StateRect(Title.left + TitleWidth + SX(10), Header.top, CheckRect.left - SX(MARGIN), Header.bottom);
		dc.DrawText(State, &StateRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
		DrawButton(dc, CheckRect, _T("Check"), false, false);
		DrawButton(dc, MenuRect, _T(""), false, false);
		Triangle(dc, MenuRect.CenterPoint().x - 3, MenuRect.CenterPoint().y - 1, true, TEXT_DARK);

		// The lines
		dc.IntersectClipRect(&Body);
		const bool Focus = Focused;
		const int Line = SY(LINE_HEIGHT);
		for (size_t i = 0; i < Shown.size(); ++i) {
			const CRect R = LineRect(static_cast<int>(i));
			if (R.bottom < Body.top || R.top > Body.bottom)
				continue;
			const shown_t &S = Shown[i];
			const bool IsSelected = static_cast<int>(i) == Selected, IsHover = static_cast<int>(i) == Hover;
			if (S.Line < 0) {
				// A kind: its colour, its name and how many
				dc.FillSolidRect(&R, IsSelected ? (Focus ? LINE_SELECTED : LINE_SELECTED_DIM) : (IsHover ? LINE_HOVER : KIND_BACK));
				Triangle(dc, R.left + SX(MARGIN), R.top + Line / 2 - (Folded[S.Kind] ? 0 : 2), !Folded[S.Kind], TEXT_DIM);
				const int Dot = std::max(6, SX(8));
				dc.FillSolidRect(R.left + SX(MARGIN) + SX(12), R.top + (Line - Dot) / 2, Dot, Dot, KindColor(S.Kind));
				dc.SelectObject(&BoldFont);
				dc.SetTextColor(TEXT_DARK);
				const CString Name(ftapi::ProblemKindTitle(S.Kind));
				CRect NameRect(R.left + SX(MARGIN) + SX(26), R.top, R.right - SX(MARGIN), R.top + Line);
				dc.DrawText(Name, &NameRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
				NameRect.left += dc.GetTextExtent(Name).cx + SX(10);
				dc.SelectObject(&Font);
				dc.SetTextColor(TEXT_DIM);
				CString Count;
				Count.Format(_T("%d"), static_cast<int>(List.Lines(S.Kind).size()));
				dc.DrawText(Count, &NameRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
				continue;
			}
			// A finding: its mark, where, on which channel, what's wrong, and its button
			const ftapi::ProblemLine &L = List.Lines(S.Kind)[static_cast<size_t>(S.Line)];
			const ftapi::ProblemFinding &F = List.Findings()[static_cast<size_t>(L.Finding)];
			if (IsSelected || IsHover)
				dc.FillSolidRect(&R, IsSelected ? (Focus ? LINE_SELECTED : LINE_SELECTED_DIM) : LINE_HOVER);
			dc.FillSolidRect(R.left + SX(INDENT) - SX(9), R.top + SY(4), std::max(2, SX(3)), Line - SY(8), KindColor(S.Kind));
			const bool Button = IsSelected || IsHover;
			const int Right = R.right - SX(MARGIN) - (Button ? SX(ACTION_WIDTH) + SX(6) : 0);
			dc.SelectObject(&Font);
			const CString Where = FromUtf8(List.LinePosition(L)), Channel = FromUtf8(F.ChannelName), What = FromUtf8(List.LineText(L));
			if (Narrow) {
				// Where and the channel on one line, what's wrong under them
				dc.SetTextColor(TEXT_DIM);
				CRect Top(R.left + SX(INDENT), R.top, Right, R.top + Line);
				dc.DrawText(Channel.IsEmpty() ? Where : Where + _T(", ") + Channel, &Top, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
				dc.SetTextColor(TEXT_DARK);
				CRect Bottom(R.left + SX(INDENT), R.top + Line - SY(3), R.right - SX(MARGIN), R.bottom);
				dc.DrawText(What, &Bottom, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
			}
			else {
				dc.SetTextColor(TEXT_DIM);
				CRect WhereRect(R.left + SX(INDENT), R.top, R.left + SX(INDENT) + SX(WHERE_WIDTH), R.bottom);
				dc.DrawText(Where, &WhereRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
				CRect ChannelRect(WhereRect.right, R.top, WhereRect.right + SX(CHANNEL_WIDTH), R.bottom);
				dc.DrawText(Channel, &ChannelRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
				dc.SetTextColor(TEXT_DARK);
				CRect WhatRect(ChannelRect.right, R.top, std::max(static_cast<int>(ChannelRect.right), Right), R.bottom);
				dc.DrawText(What, &WhatRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
			}
			if (Button)
				DrawButton(dc, ButtonRect(static_cast<int>(i)), ActionName(ActionOf(static_cast<int>(i))), IsHover && HoverButton, false);
		}

		// Nothing to list: why, and what the panel does
		if (Shown.empty()) {
			dc.SelectObject(&Font);
			dc.SetTextColor(TEXT_DIM);
			CRect Hint(Body.left + SX(MARGIN) + SX(2), Body.top + SY(6), Body.right - SX(MARGIN), Body.bottom);
			CString Text;
			if (Checked)
				Text = _T("Nothing stands out in this track. The panel checks it a moment after each change for notes that vanish, ")
					_T("notes that click, clashes, notes out of range, silent and busy bars and notes outside the key, and marks ")
					_T("what it finds in the pattern editor and the piano roll.");
			else if (!Trouble.IsEmpty())
				Text = Trouble;
			else
				Text = _T("Press Check to look for notes that vanish, notes that click, clashes and more.");
			if (!Auto)
				Text += _T(" (Checking as you edit is off: right-click to switch it on.)");
			dc.DrawText(Text, &Hint, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
		}
		dc.SelectClipRgn(NULL);
		dc.SelectObject(pOldFont);
	}
};

// The panel

BEGIN_MESSAGE_MAP(CProblemsPanelWnd, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_SIZE()
	ON_WM_VSCROLL()
	ON_WM_MOUSEWHEEL()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONDBLCLK()
	ON_WM_RBUTTONUP()
	ON_WM_MOUSEMOVE()
	ON_WM_MOUSELEAVE()
	ON_WM_SETCURSOR()
	ON_WM_GETDLGCODE()
	ON_WM_SETFOCUS()
	ON_WM_KILLFOCUS()
	ON_WM_TIMER()
	ON_WM_DESTROY()
END_MESSAGE_MAP()

CProblemsPanelWnd::CProblemsPanelWnd() : m_pImpl(NULL)
{
	m_pImpl = new Impl(this);
}

CProblemsPanelWnd::~CProblemsPanelWnd()
{
	delete m_pImpl;
}

bool CProblemsPanelWnd::Create(CWnd *pParent, CMcpConnection *pTools)
{
	m_pImpl->pTools = pTools;
	const CString Class = AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(NULL, IDC_ARROW), NULL, NULL);
	if (!CWnd::Create(Class, _T("Problems"), WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP, CRect(0, 0, 0, 0), pParent, 1))
		return false;
	m_pImpl->MakeFonts();
	return true;
}

void CProblemsPanelWnd::Opened(bool Open)
{
	m_pImpl->Open = Open;
	if (GetSafeHwnd() == NULL)
		return;
	if (Open) {
		SetTimer(TMR_LOOK, LOOK_MS, NULL);
		m_pImpl->Check(true);
	}
	else {
		KillTimer(TMR_CHECK);
		KillTimer(TMR_LOOK);
		m_pImpl->Waiting = false;
		m_pImpl->Changed();		// no marks while it's closed
	}
}

void CProblemsPanelWnd::SongChanged()
{
	if (!m_pImpl->Open || GetSafeHwnd() == NULL)
		return;
	if (m_pImpl->Auto)
		m_pImpl->CheckSoon(CHECK_MS);
	else if (m_pImpl->Checked && m_pImpl->List.Track() != static_cast<CMainFrame*>(theApp.m_pMainWnd)->GetSelectedTrack())
		m_pImpl->Changed();	// another track: its marks go
}

BOOL CProblemsPanelWnd::PreTranslateMessage(MSG *pMsg)
{
	// The list's keys come here before FamiTracker's shortcuts see them
	if ((pMsg->message == WM_KEYDOWN || pMsg->message == WM_SYSKEYDOWN) && pMsg->hwnd == GetSafeHwnd()) {
		const bool Ctrl = ::GetKeyState(VK_CONTROL) < 0, Shift = ::GetKeyState(VK_SHIFT) < 0, Alt = ::GetKeyState(VK_MENU) < 0;
		if (m_pImpl->KeyDown(static_cast<UINT>(pMsg->wParam), Ctrl, Shift, Alt))
			return TRUE;
	}
	return CWnd::PreTranslateMessage(pMsg);
}

void CProblemsPanelWnd::OnPaint()
{
	CPaintDC dc(this);
	CRect Client;
	GetClientRect(&Client);
	CDC Memory;
	Memory.CreateCompatibleDC(&dc);
	CBitmap Bitmap;
	Bitmap.CreateCompatibleBitmap(&dc, std::max(1, static_cast<int>(Client.Width())), std::max(1, static_cast<int>(Client.Height())));
	CBitmap *pOld = Memory.SelectObject(&Bitmap);
	m_pImpl->Paint(Memory, Client);
	dc.BitBlt(0, 0, Client.Width(), Client.Height(), &Memory, 0, 0, SRCCOPY);
	Memory.SelectObject(pOld);
}

BOOL CProblemsPanelWnd::OnEraseBkgnd(CDC *pDC)
{
	return TRUE;
}

void CProblemsPanelWnd::OnSize(UINT nType, int cx, int cy)
{
	CWnd::OnSize(nType, cx, cy);
	m_pImpl->Arrange();
	Invalidate(FALSE);
}

void CProblemsPanelWnd::OnVScroll(UINT nSBCode, UINT nPos, CScrollBar *pScrollBar)
{
	int Position = m_pImpl->Scroll;
	const int Line = SY(LINE_HEIGHT);
	switch (nSBCode) {
	case SB_LINEUP: Position -= Line; break;
	case SB_LINEDOWN: Position += Line; break;
	case SB_PAGEUP: Position -= std::max(Line, m_pImpl->Body.Height() - Line); break;
	case SB_PAGEDOWN: Position += std::max(Line, m_pImpl->Body.Height() - Line); break;
	case SB_THUMBTRACK: case SB_THUMBPOSITION: {
		SCROLLINFO Info;
		memset(&Info, 0, sizeof(Info));
		Info.cbSize = sizeof(Info);
		Info.fMask = SIF_TRACKPOS;
		Position = GetScrollInfo(SB_VERT, &Info) ? Info.nTrackPos : static_cast<int>(nPos);
		break;
	}
	case SB_TOP: Position = 0; break;
	case SB_BOTTOM: Position = m_pImpl->Content; break;
	}
	m_pImpl->ScrollTo(Position);
}

BOOL CProblemsPanelWnd::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
	m_pImpl->ScrollTo(m_pImpl->Scroll - zDelta * SY(LINE_HEIGHT) * 3 / WHEEL_DELTA);
	return TRUE;
}

void CProblemsPanelWnd::OnLButtonDown(UINT nFlags, CPoint point)
{
	SetFocus();
	if (m_pImpl->CheckRect.PtInRect(point)) {
		m_pImpl->Check(true, true);
		return;
	}
	if (m_pImpl->MenuRect.PtInRect(point)) {
		CPoint Screen(m_pImpl->MenuRect.left, m_pImpl->MenuRect.bottom);
		ClientToScreen(&Screen);
		m_pImpl->Menu(-1, Screen);
		return;
	}
	bool OnButton = false;
	const int Index = m_pImpl->LineAt(point, &OnButton);
	if (Index < 0)
		return;
	const shown_t &S = m_pImpl->Shown[static_cast<size_t>(Index)];
	if (S.Line < 0) {
		m_pImpl->Select(Index, false);
		m_pImpl->Fold(S.Kind, !m_pImpl->Folded[S.Kind]);
		return;
	}
	m_pImpl->Select(Index);
	if (OnButton)
		m_pImpl->Act(Index);
}

void CProblemsPanelWnd::OnLButtonDblClk(UINT nFlags, CPoint point)
{
	bool OnButton = false;
	const int Index = m_pImpl->LineAt(point, &OnButton);
	if (Index < 0 || OnButton)
		return;
	if (m_pImpl->Shown[static_cast<size_t>(Index)].Line < 0)
		return;		// a kind's heading folds with one click
	m_pImpl->Select(Index, false);
	m_pImpl->GoTo(Index);
}

void CProblemsPanelWnd::OnRButtonUp(UINT nFlags, CPoint point)
{
	SetFocus();
	const int Index = m_pImpl->LineAt(point, NULL);
	if (Index >= 0)
		m_pImpl->Select(Index, false);
	CPoint Screen = point;
	ClientToScreen(&Screen);
	m_pImpl->Menu(Index, Screen);
}

void CProblemsPanelWnd::OnMouseMove(UINT nFlags, CPoint point)
{
	bool OnButton = false;
	const int Index = m_pImpl->LineAt(point, &OnButton);
	if (Index == m_pImpl->Hover && OnButton == m_pImpl->HoverButton)
		return;
	if (m_pImpl->Hover < 0 && Index >= 0) {
		// Told when the mouse leaves, so nothing stays lit
		TRACKMOUSEEVENT Track;
		memset(&Track, 0, sizeof(Track));
		Track.cbSize = sizeof(Track);
		Track.dwFlags = TME_LEAVE;
		Track.hwndTrack = m_hWnd;
		::TrackMouseEvent(&Track);
	}
	m_pImpl->Hover = Index;
	m_pImpl->HoverButton = OnButton;
	Invalidate(FALSE);
	const ftapi::ProblemFinding *pFinding = m_pImpl->FindingOf(Index);
	if (pFinding == NULL)
		return;
	CString Text;
	switch (OnButton ? m_pImpl->ActionOf(Index) : ACTION_NONE) {
	case ACTION_NUDGE:
		Text = _T("Nudge: ") + FromUtf8(pFinding->FixDoes) + _T(", as one Ctrl+Z");
		break;
	case ACTION_SHOW:
		Text = _T("Show: select these notes in the piano roll, to give them a sound that lasts");
		break;
	case ACTION_EXPLAIN:
		Text = _T("Explain: what's wrong there, and what helps");
		break;
	default:
		Text = _T("Double-click to go there. ") + FromUtf8(pFinding->Summary);
		break;
	}
	m_pImpl->Say(Text);
}

void CProblemsPanelWnd::OnMouseLeave()
{
	if (m_pImpl->Hover >= 0) {
		m_pImpl->Hover = -1;
		m_pImpl->HoverButton = false;
		Invalidate(FALSE);
	}
	CWnd::OnMouseLeave();
}

BOOL CProblemsPanelWnd::OnSetCursor(CWnd *pWnd, UINT nHitTest, UINT message)
{
	if (nHitTest == HTCLIENT) {
		CPoint Point;
		::GetCursorPos(&Point);
		ScreenToClient(&Point);
		bool OnButton = false;
		m_pImpl->LineAt(Point, &OnButton);
		if (OnButton || m_pImpl->CheckRect.PtInRect(Point) || m_pImpl->MenuRect.PtInRect(Point)) {
			::SetCursor(::LoadCursor(NULL, IDC_HAND));
			return TRUE;
		}
	}
	return CWnd::OnSetCursor(pWnd, nHitTest, message);
}

UINT CProblemsPanelWnd::OnGetDlgCode()
{
	return DLGC_WANTARROWS | DLGC_WANTCHARS | DLGC_WANTALLKEYS;
}

void CProblemsPanelWnd::OnSetFocus(CWnd *pOldWnd)
{
	CWnd::OnSetFocus(pOldWnd);
	m_pImpl->Focused = true;
	if (m_pImpl->Selected < 0 && !m_pImpl->Shown.empty())
		m_pImpl->Select(0, false);
	Invalidate(FALSE);
}

void CProblemsPanelWnd::OnKillFocus(CWnd *pNewWnd)
{
	CWnd::OnKillFocus(pNewWnd);
	m_pImpl->Focused = false;
	Invalidate(FALSE);
}

void CProblemsPanelWnd::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == TMR_CHECK) {
		KillTimer(TMR_CHECK);
		m_pImpl->Check(false);
		return;
	}
	if (nIDEvent == TMR_LOOK) {
		// The mouse went away without a word
		if (m_pImpl->Hover >= 0) {
			CPoint Point;
			::GetCursorPos(&Point);
			CRect Window;
			GetWindowRect(&Window);
			if (!Window.PtInRect(Point)) {
				m_pImpl->Hover = -1;
				Invalidate(FALSE);
			}
		}
		// Changes nothing tells the panel of: an instrument's envelope, the speed and tempo, the track
		if (m_pImpl->Open && m_pImpl->Auto && !m_pImpl->Waiting && m_pImpl->MakeSignature() != m_pImpl->Signature)
			m_pImpl->CheckSoon(CHECK_MS);
		return;
	}
	CWnd::OnTimer(nIDEvent);
}

void CProblemsPanelWnd::OnDestroy()
{
	KillTimer(TMR_CHECK);
	KillTimer(TMR_LOOK);
	g_Marks.Clear();
	CWnd::OnDestroy();
}

// The bar it is docked in

BEGIN_MESSAGE_MAP(CProblemsBar, CControlBar)
	ON_WM_SIZE()
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_MOUSEMOVE()
	ON_WM_SETCURSOR()
	ON_WM_CAPTURECHANGED()
	ON_WM_DESTROY()
	ON_MESSAGE(WM_SIZEPARENT, OnSizeParent)
END_MESSAGE_MAP()

CProblemsBar::CProblemsBar() : m_bRight(false), m_iHeight(DEFAULT_HEIGHT), m_iWidth(DEFAULT_WIDTH), m_iRoom(0), m_iDragFrom(-1),
	m_iDragSize(0), m_iDragMax(0)
{
}

CProblemsBar::~CProblemsBar()
{
}

bool CProblemsBar::Create(CFrameWnd *pFrame, CMcpConnection *pTools)
{
	m_bRight = theApp.GetProfileInt(SECTION, _T("On the right"), 0) != 0;
	m_iHeight = std::max(SY(MIN_HEIGHT), static_cast<int>(theApp.GetProfileInt(SECTION, _T("Height"), SY(DEFAULT_HEIGHT))));
	m_iWidth = std::max(SX(MIN_WIDTH), static_cast<int>(theApp.GetProfileInt(SECTION, _T("Width"), SX(DEFAULT_WIDTH))));
	m_dwStyle = m_bRight ? CBRS_ALIGN_RIGHT : CBRS_ALIGN_BOTTOM;
	const CString Class = AfxRegisterWndClass(0, ::LoadCursor(NULL, IDC_ARROW), NULL, NULL);
	if (!CWnd::Create(Class, _T("Problems"), WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, CRect(0, 0, 0, 0), pFrame,
		AFX_IDW_CONTROLBAR_FIRST + 50))
		return false;
	return m_wndPanel.Create(this, pTools);
}

void CProblemsBar::Opened(bool Open)
{
	if (!Open && ::GetFocus() == m_wndPanel.GetSafeHwnd()) {
		CFrameWnd *pFrame = GetParentFrame();
		CView *pView = pFrame != NULL ? pFrame->GetActiveView() : NULL;
		if (pView != NULL)
			pView->SetFocus();
	}
	m_wndPanel.Opened(Open);
	theApp.WriteProfileInt(SECTION, _T("Open"), Open ? 1 : 0);
}

void CProblemsBar::SongChanged()
{
	m_wndPanel.SongChanged();
}

void CProblemsBar::Focus()
{
	m_wndPanel.SetFocus();
}

bool CProblemsBar::WasOpen()
{
	return theApp.GetProfileInt(SECTION, _T("Open"), 0) != 0;
}

void CProblemsBar::DockOnRight(bool Right)
{
	if (Right == m_bRight)
		return;
	m_bRight = Right;
	m_dwStyle = (m_dwStyle & ~CBRS_ALIGN_ANY) | (Right ? CBRS_ALIGN_RIGHT : CBRS_ALIGN_BOTTOM);
	theApp.WriteProfileInt(SECTION, _T("On the right"), Right ? 1 : 0);
	CFrameWnd *pFrame = GetParentFrame();
	if (pFrame != NULL)
		pFrame->RecalcLayout();
	Invalidate();
}

int CProblemsBar::Divider() const
{
	return std::max(4, m_bRight ? SX(DIVIDER) : SY(DIVIDER));
}

int CProblemsBar::MaxSize() const
{
	if (m_bRight)
		return std::max(SX(MIN_WIDTH), m_iRoom - SX(MIN_VIEW_WIDTH));
	return std::max(SY(MIN_HEIGHT), m_iRoom / 2);	// the pattern editor (and the piano roll) keep half
}

LRESULT CProblemsBar::OnSizeParent(WPARAM wParam, LPARAM lParam)
{
	const AFX_SIZEPARENTPARAMS *pLayout = reinterpret_cast<const AFX_SIZEPARENTPARAMS*>(lParam);
	m_iRoom = m_bRight ? pLayout->rect.right - pLayout->rect.left : pLayout->rect.bottom - pLayout->rect.top;
	return CControlBar::OnSizeParent(wParam, lParam);
}

CSize CProblemsBar::CalcFixedLayout(BOOL bStretch, BOOL bHorz)
{
	const int Shown = std::max(0, std::min(Size(), MaxSize()));
	return m_bRight ? CSize(Shown, 32767) : CSize(32767, Shown);
}

void CProblemsBar::OnUpdateCmdUI(CFrameWnd *pTarget, BOOL bDisableIfNoHndler)
{
}

BOOL CProblemsBar::PreTranslateMessage(MSG *pMsg)
{
	// Not as a dialog (as control bars are), so FamiTracker's shortcuts and the panel's keys work
	return CWnd::PreTranslateMessage(pMsg);
}

void CProblemsBar::OnSize(UINT nType, int cx, int cy)
{
	CControlBar::OnSize(nType, cx, cy);
	if (m_wndPanel.GetSafeHwnd() == NULL)
		return;
	if (m_bRight)
		m_wndPanel.MoveWindow(Divider(), 0, std::max(0, cx - Divider()), cy);
	else
		m_wndPanel.MoveWindow(0, Divider(), cx, std::max(0, cy - Divider()));
}

void CProblemsBar::OnPaint()
{
	CPaintDC dc(this);
	CRect Rect;
	GetClientRect(&Rect);
	const int Line = Divider();
	const COLORREF Face = ::GetSysColor(COLOR_3DFACE), Light = ::GetSysColor(COLOR_3DHILIGHT), Shadow = ::GetSysColor(COLOR_3DSHADOW);
	if (m_bRight) {
		dc.FillSolidRect(Rect.left, Rect.top, Line, Rect.Height(), Face);
		dc.FillSolidRect(Rect.left, Rect.top, 1, Rect.Height(), Light);
		dc.FillSolidRect(Rect.left + Line - 1, Rect.top, 1, Rect.Height(), Shadow);
		const int Y = Rect.top + Rect.Height() / 2 - SY(12);
		for (int i = 0; i < 4; ++i)
			dc.FillSolidRect(Rect.left + Line / 2 - 1, Y + i * SY(7), 2, SY(3), Shadow);
	}
	else {
		dc.FillSolidRect(Rect.left, Rect.top, Rect.Width(), Line, Face);
		dc.FillSolidRect(Rect.left, Rect.top, Rect.Width(), 1, Light);
		dc.FillSolidRect(Rect.left, Rect.top + Line - 1, Rect.Width(), 1, Shadow);
		const int X = Rect.left + Rect.Width() / 2 - SX(12);
		for (int i = 0; i < 4; ++i)
			dc.FillSolidRect(X + i * SX(7), Rect.top + Line / 2 - 1, SX(3), 2, Shadow);
	}
}

BOOL CProblemsBar::OnEraseBkgnd(CDC *pDC)
{
	return TRUE;
}

void CProblemsBar::OnLButtonDown(UINT nFlags, CPoint point)
{
	if ((m_bRight ? point.x : point.y) >= Divider())
		return;
	CPoint Screen = point;
	ClientToScreen(&Screen);
	m_iDragFrom = static_cast<int>(m_bRight ? Screen.x : Screen.y);
	m_iDragSize = Size();
	m_iDragMax = MaxSize();
	SetCapture();
}

void CProblemsBar::OnMouseMove(UINT nFlags, CPoint point)
{
	if (m_iDragFrom < 0)
		return;
	CPoint Screen = point;
	ClientToScreen(&Screen);
	const int Moved = static_cast<int>(m_bRight ? Screen.x : Screen.y) - m_iDragFrom;
	const int Least = m_bRight ? SX(MIN_WIDTH) : SY(MIN_HEIGHT);
	const int New = std::max(Least, std::min(m_iDragMax, m_iDragSize - Moved));
	if (New != Size()) {
		Size() = New;
		CFrameWnd *pFrame = GetParentFrame();
		if (pFrame != NULL)
			pFrame->RecalcLayout();
	}
}

void CProblemsBar::OnLButtonUp(UINT nFlags, CPoint point)
{
	if (m_iDragFrom >= 0) {
		m_iDragFrom = -1;
		ReleaseCapture();
		theApp.WriteProfileInt(SECTION, m_bRight ? _T("Width") : _T("Height"), Size());
	}
}

void CProblemsBar::OnCaptureChanged(CWnd *pWnd)
{
	m_iDragFrom = -1;
	CControlBar::OnCaptureChanged(pWnd);
}

BOOL CProblemsBar::OnSetCursor(CWnd *pWnd, UINT nHitTest, UINT message)
{
	if (pWnd == this && nHitTest == HTCLIENT) {
		::SetCursor(::LoadCursor(NULL, m_bRight ? IDC_SIZEWE : IDC_SIZENS));
		return TRUE;
	}
	return CControlBar::OnSetCursor(pWnd, nHitTest, message);
}

void CProblemsBar::OnDestroy()
{
	theApp.WriteProfileInt(SECTION, _T("Height"), m_iHeight);
	theApp.WriteProfileInt(SECTION, _T("Width"), m_iWidth);
	CControlBar::OnDestroy();
}

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

// The What next panel (see WhatNextPanel.h)

// Standard headers come before stdafx.h, whose DEBUG_NEW macro would
// otherwise rewrite the placement new inside the standard library.
#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include "Api/Json.h"
#include "Api/NextSteps.h"
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
#include "Action.h"
#include "InsertRecording.h"
#include "McpConnection.h"
#include "BarsPlayer.h"
#include "Api/Music.h"
#include "ProblemsPanel.h"
#include "WhatNextPanel.h"

namespace {

LPCTSTR SECTION = _T("What next panel");	// registry section: open, where, its size

// Sizes at 96 DPI (scaled with SX and SY)
const int DEFAULT_HEIGHT = 150;			// along the bottom
const int DEFAULT_WIDTH = 340;			// along the right
const int MIN_HEIGHT = 70;
const int ROW_LEAST = 150;				// beside the others along the bottom: its step's title, why and buttons
const int MIN_WIDTH = 220;
const int HEADER_HEIGHT = 26;
const int STAGE_HEIGHT = 18;			// a stage's pill
const int MARGIN = 8;
const int BUTTON_HEIGHT = 22;
const int LINE = 16;					// a line of text, for scrolling

const UINT_PTR TMR_ASK = 1;				// asks a moment after the song changed
const UINT ASK_MS = 1500;
const UINT WAIT_MS = 500;				// tries again: the song plays, Claude Code is being answered, the user types
const DWORD IDLE_MS = 1000;				// no key or click for this long before it asks by itself
const UINT_PTR TMR_LOOK = 2;			// looks for changes nothing tells the panel of
const UINT LOOK_MS = 1000;
const UINT_PTR TMR_PLAY = 3;			// the grown melody's bars as they play
const UINT PLAY_MS = 100;

// Colours, as the problems panel's
const COLORREF PANEL_BACK = RGB(232, 234, 238);
const COLORREF BODY_BACK = RGB(255, 255, 255);
const COLORREF TEXT_DARK = RGB(30, 32, 40);
const COLORREF TEXT_DIM = RGB(100, 104, 112);
const COLORREF BUTTON_BACK = RGB(250, 251, 253);
const COLORREF BUTTON_BORDER = RGB(160, 166, 178);
const COLORREF BUTTON_HOT = RGB(60, 120, 215);
const COLORREF PRIMARY_BACK = RGB(60, 120, 215);
const COLORREF PRIMARY_HOT = RGB(40, 96, 190);
const COLORREF STAGE_DONE = RGB(208, 234, 210);
const COLORREF STAGE_DONE_TEXT = RGB(30, 100, 40);
const COLORREF STAGE_NOW = RGB(204, 225, 252);
const COLORREF STAGE_PART = RGB(236, 240, 246);
const COLORREF STAGE_BORDER = RGB(190, 196, 206);

// What a button does
enum action_t {
	ACT_DO, ACT_SKIP, ACT_GROW, ACT_RECORD, ACT_DRAW, ACT_SAVE_AS, ACT_PLAY_SONG, ACT_STOP, ACT_ASK
};

// The menus' items
enum {
	MENU_ASK = 1, MENU_NOTES, MENU_CLOSE, MENU_UNSKIP = 20, MENU_VERSION = 60, MENU_MIC = 70,
	MENU_PHONE, MENU_PLACE = 100,	// three: CPanelBar::AddPlaceItems
	MENU_KIND = 200					// a kind each (KIND_COUNT; it was 10, which more than ten kinds would take past MENU_UNSKIP)
};

const int HOVER_STAGE = 1000;			// Hover: a stage's pill, from here on

const LPCTSTR STAGE_LABELS[ftapi::NEXT_STAGES] = {
	_T("Frame"), _T("Parts"), _T("Form"), _T("Depth"), _T("Polish"), _T("Mix"), _T("Loop"), _T("Export")
};

// The kinds of song what_next knows ("" is its own choice: the general recipe, or calm when the track's name says so)
// (what_next's kinds, NextKindInfo in Api/WhatNext.cpp, in the order of next_kind but general first)
const char *const KIND_IDS[] = { "", "calm", "lively", "cozy", "action", "mysterious", "adventure", "floating" };
const LPCTSTR KIND_ITEMS[] = {
	_T("General: the recipe for any song"), _T("Calm: a night or an ambient place"), _T("Lively: a stage"),
	_T("Cozy: a warm place indoors (a shop, a hut, an inn)"), _T("Action: a boss or a battle"),
	_T("Mysterious: caves, sewers, ruins, forgotten places"), _T("Adventure: open fields, travel, an airship flight"),
	_T("Floating: the sky, clouds, underwater, waiting at a dock")
};
const int KIND_COUNT = sizeof(KIND_IDS) / sizeof(KIND_IDS[0]);

// Text from the tools (UTF-8) as FamiTracker shows it (the Windows code page), and back
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

std::string ToUtf8(const CString &Text)
{
	if (Text.IsEmpty())
		return std::string();
	const int Wide = ::MultiByteToWideChar(CP_ACP, 0, Text, Text.GetLength(), NULL, 0);
	if (Wide <= 0)
		return std::string(static_cast<LPCSTR>(Text));
	std::vector<wchar_t> Buffer(static_cast<size_t>(Wide));
	::MultiByteToWideChar(CP_ACP, 0, Text, Text.GetLength(), &Buffer[0], Wide);
	const int Bytes = ::WideCharToMultiByte(CP_UTF8, 0, &Buffer[0], Wide, NULL, 0, NULL, NULL);
	if (Bytes <= 0)
		return std::string(static_cast<LPCSTR>(Text));
	std::string Out(static_cast<size_t>(Bytes), '\0');
	::WideCharToMultiByte(CP_UTF8, 0, &Buffer[0], Wide, &Out[0], Bytes, NULL, NULL);
	return Out;
}

// A small triangle pointing down, for a button with a menu
void DownArrow(CDC &dc, int X, int Y, COLORREF Color)
{
	for (int i = 0; i < 4; ++i)
		dc.FillSolidRect(X + i, Y + i, 7 - 2 * i, 1, Color);
}

// A key or a click this recently: the user is working, so the panel doesn't ask now (asking takes a moment)
bool UserBusy(DWORD Within)
{
	LASTINPUTINFO Input;
	memset(&Input, 0, sizeof(Input));
	Input.cbSize = sizeof(Input);
	return ::GetLastInputInfo(&Input) && ::GetTickCount() - Input.dwTime < Within;
}

// The kind chosen for a song's track (CFamiTrackerDoc::GetSongSerial, the track), while FamiTracker runs
std::map<std::pair<unsigned int, int>, std::string> &ChosenKinds()
{
	static std::map<std::pair<unsigned int, int>, std::string> Kinds;
	return Kinds;
}

}	// namespace

std::string WhatNextChosenKind()
{
	CMainFrame *pMain = static_cast<CMainFrame*>(theApp.m_pMainWnd);
	CFamiTrackerDoc *pDoc = pMain != NULL ? static_cast<CFamiTrackerDoc*>(pMain->GetActiveDocument()) : NULL;
	if (pDoc == NULL)
		return std::string();
	const std::map<std::pair<unsigned int, int>, std::string>::const_iterator it = ChosenKinds().find(std::pair<unsigned int, int>(
		pDoc->GetSongSerial(), pMain->GetSelectedTrack()));
	return it != ChosenKinds().end() ? it->second : std::string();
}

struct CWhatNextPanelWnd::Impl {
	// A button of the body
	struct button_t {
		int Action;			// action_t
		CString Text;
		bool Menu;			// opens a menu
		bool Primary;		// the step's own
		CRect Rect;			// in the body, as if nothing were scrolled
	};

	CWhatNextPanelWnd *pWnd;
	CMcpConnection *pTools;
	ftapi::NextAnswer Answer;
	bool Have;						// Answer is the song's next step, as it was when asked
	bool Stale;						// the song changed since: its buttons wait for the next answer
	bool Waiting;					// an answer is due
	bool Open, Focused;
	CString Trouble;				// why the last ask didn't go through
	CString Last;					// what the last click did
	unsigned int Signature;			// the song's settings at the last ask
	unsigned int AskedSong;			// the song (CFamiTrackerDoc::GetSongSerial) and track it was asked for
	int AskedTrack;
	DWORD AskTime;					// how long the last ask took (ms)
	std::map<std::pair<unsigned int, int>, std::vector<std::string> > Skips;	// a song's track: the steps skipped (its kind: ChosenKinds)
	CBarsPlayer Player;				// the bars a melody was grown in
	CString PlayingWhat;
	bool Doing;						// the panel's own call runs: the changes it makes are what Last says

	// Laid out by Arrange
	CRect Header, Body, MenuRect, KindRect, ProgressRect;
	CRect StageRects[ftapi::NEXT_STAGES];
	CRect WhatRect, WhyRect;		// in the body, as if nothing were scrolled
	CRect LastRect;
	std::vector<button_t> Buttons;
	CString WhatText, WhyText;
	CString ProgressText;			// how far the song has come, as short as the header's room needs
	bool Narrow;					// the stages on a line of their own
	int Scroll, Content;
	int Hover;						// a button, HOVER_STAGE + a stage, -1 for none
	bool HoverKind, HoverMenu;
	CFont Font, BoldFont, BigFont, SmallFont;

	Impl(CWhatNextPanelWnd *p) : pWnd(p), pTools(NULL), Have(false), Stale(false), Waiting(false), Open(false), Focused(false),
		Signature(0), AskedSong(0), AskedTrack(-1), AskTime(0), Doing(false), Narrow(false), Scroll(0), Content(0), Hover(-1),
		HoverKind(false), HoverMenu(false)
	{
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
		Log.lfHeight = -SY(14);
		BigFont.CreateFontIndirect(&Log);
		Log.lfWeight = FW_NORMAL;
		Log.lfHeight = -SY(11);
		SmallFont.CreateFontIndirect(&Log);
	}

	// The song and track the panel is about now
	std::pair<unsigned int, int> SongKey() const
	{
		const CFamiTrackerDoc *pDoc = Doc();
		const CMainFrame *pMain = Main();
		return std::make_pair(pDoc != NULL ? pDoc->GetSongSerial() : 0u, pMain != NULL ? pMain->GetSelectedTrack() : -1);
	}

	std::string ChosenKind() const
	{
		const std::map<std::pair<unsigned int, int>, std::string>::const_iterator it = ChosenKinds().find(SongKey());
		return it != ChosenKinds().end() ? it->second : std::string();
	}

	std::vector<std::string> Skipped() const
	{
		const std::map<std::pair<unsigned int, int>, std::vector<std::string> >::const_iterator it = Skips.find(SongKey());
		return it != Skips.end() ? it->second : std::vector<std::string>();
	}

	// The answer is about the song and track shown now
	bool Current() const
	{
		return Have && SongKey() == std::make_pair(AskedSong, AskedTrack);
	}

	// Asking

	// The song's settings, its instruments, whether it's saved and where: what changes without a notice
	unsigned int MakeSignature() const
	{
		CFamiTrackerDoc *pDoc = Doc();
		unsigned int Hash = SongSettingsSignature();
		auto Add = [&Hash](unsigned int Value) {
			Hash = (Hash ^ Value) * 16777619u;
		};
		if (pDoc == NULL)
			return Hash;
		Add(pDoc->GetSongSerial());
		Add(pDoc->IsModified() ? 1u : 0u);
		const CString Path = pDoc->GetPathName();
		for (int i = 0; i < Path.GetLength(); ++i)
			Add(static_cast<unsigned char>(Path[i]));
		return Hash;
	}

	void AskSoon(UINT After)
	{
		Waiting = true;
		if (pWnd->GetSafeHwnd() != NULL)
			pWnd->SetTimer(TMR_ASK, After, NULL);
	}

	// what_next (format json) on the selected track, with the kind chosen and the steps skipped. Now: also while the song
	// plays or the user works; Wait: with the wait cursor (asked for)
	void Ask(bool Now, bool Wait = false)
	{
		pWnd->KillTimer(TMR_ASK);
		CFamiTrackerDoc *pDoc = Doc();
		CMainFrame *pMain = Main();
		if (pTools == NULL || pDoc == NULL || pMain == NULL || !pDoc->IsFileLoaded()) {
			Waiting = false;
			Have = false;
			Trouble = _T("No song is open.");
			Changed();
			return;
		}
		if (pTools->IsBusy()) {			// answering Claude Code: in a moment
			AskSoon(WAIT_MS);
			return;
		}
		// Not while the song plays, a dialog is open (Play the fix asks Keep or Take back) or the user types: asking takes
		// a moment, the longer the song
		if (!Now && (theApp.IsPlaying() || Player.IsPlaying() || !pMain->IsWindowEnabled() ||
			UserBusy(std::max(IDLE_MS, static_cast<DWORD>(2 * AskTime))))) {
			AskSoon(WAIT_MS);
			pWnd->Invalidate(FALSE);
			return;
		}
		Waiting = false;
		ftapi::Json Args = ftapi::Json::MakeObject();
		Args.Set("format", "json");
		const std::string Kind = ChosenKind();
		if (!Kind.empty())
			Args.Set("kind", Kind);
		const std::vector<std::string> Skip = Skipped();
		if (!Skip.empty()) {
			ftapi::Json List = ftapi::Json::MakeArray();
			for (size_t i = 0; i < Skip.size(); ++i)
				List.Push(Skip[i]);
			Args.Set("skip", List);
		}
		if (Wait)
			pWnd->BeginWaitCursor();
		const DWORD Start = ::GetTickCount();
		std::string Reply;
		const bool Done = pTools->RunTool("what_next", ftapi::WriteJson(Args), Reply);
		AskTime = ::GetTickCount() - Start;
		if (Wait)
			pWnd->EndWaitCursor();
		Signature = MakeSignature();
		const std::pair<unsigned int, int> Key = SongKey();
		AskedSong = Key.first;
		AskedTrack = Key.second;
		std::string Error;
		if (!Done || !ftapi::ReadNextAnswer(Reply, Answer, Error)) {
			Have = false;
			Trouble = FromUtf8(Done ? Error : Reply);
		}
		else {
			Have = true;
			Trouble.Empty();
		}
		Stale = false;
		Changed();
	}

	// The answer changed, or what's shown: laid out and drawn again
	void Changed()
	{
		if (pWnd->GetSafeHwnd() == NULL)
			return;
		Arrange();
		pWnd->Invalidate(FALSE);
	}

	// What the panel says now: what to do and why, in plain words
	void MakeTexts()
	{
		WhatText.Empty();
		WhyText.Empty();
		const ftapi::NextStepInfo &N = Answer.Step;
		if (!Trouble.IsEmpty() && !Current()) {
			WhatText = _T("What next couldn't ask");
			WhyText = Trouble;
			return;
		}
		if (!Current()) {
			WhyText = theApp.IsPlaying() ? _T("Says what to do next when the song stops playing.") : _T("Asking what to do next...");
			return;
		}
		if (Answer.Done) {
			WhatText = _T("The song is done");
			WhyText.Format(_T("Every step of the %s recipe is done: play it, and change what you'd like. The panel says when something ")
				_T("new comes up."), static_cast<LPCTSTR>(FromUtf8(Answer.Kind)));
			return;
		}
		WhatText = FromUtf8(N.What);
		if (IsMelodyStep()) {
			CString Where = Bars(N.FromBar, N.ToBar);
			if (N.Tool == "grow_melody" && !N.OwnNotes)
				WhyText.Format(_T("There's no melody in %s yet. Grow one grows the song's own hook over the chords there: three ")
					_T("versions to pick from, each played as you pick it. Or sing it (Record it) or draw it (Draw it)."),
					static_cast<LPCTSTR>(Where));
			else
				WhyText.Format(_T("There's no melody in %s yet, and a tune of your own is best: sing, hum or whistle it (Record ")
					_T("it), or draw it in the piano roll (Draw it). Or let FamiTracker %s, and change it as you like."),
					static_cast<LPCTSTR>(Where), N.Tool == "grow_melody" ? _T("grow one from a hook over the chords (Grow one: ")
					_T("three versions, each played as you pick it)") : _T("write a phrase on the chords to start from (Use this phrase)"));
			return;
		}
		WhyText = FromUtf8(N.Calls > 1 ? N.Why : ftapi::PlainWhy(N.Why));	// (the calls of several are Claude Code's: given whole)
		if (N.Calls > 1) {
			CString More;
			More.Format(_T("\nThis step takes %d calls, which Claude Code makes (File > Connect Claude Code): it can't be done ")
				_T("with a click here. Skip leaves it out."), N.Calls);
			WhyText += More;
		}
	}

	bool IsMelodyStep() const
	{
		const ftapi::NextStepInfo &N = Answer.Step;
		return Current() && !Answer.Done && N.Id == "melody" && (N.Tool == "grow_melody" || N.Tool == "write_notes");
	}

	static CString Bars(int From, int To)
	{
		CString Text;
		if (From <= 0)
			Text = _T("these bars");
		else if (To > From)
			Text.Format(_T("bars %d-%d"), From, To);
		else
			Text.Format(_T("bar %d"), From);
		return Text;
	}

	// The buttons for what's shown now
	void MakeButtons()
	{
		Buttons.clear();
		auto Add = [this](int Action, LPCTSTR Text, bool Menu, bool Primary) {
			button_t B;
			B.Action = Action;
			B.Text = Text;
			B.Menu = Menu;
			B.Primary = Primary;
			Buttons.push_back(B);
		};
		if (Player.IsPlaying())
			Add(ACT_STOP, _T("Stop"), false, false);
		if (!Current() || Stale) {
			if (Doc() != NULL && Doc()->IsFileLoaded())
				Add(ACT_ASK, _T("Ask now"), false, !Player.IsPlaying());
			return;
		}
		const ftapi::NextStepInfo &N = Answer.Step;
		if (Answer.Done) {
			Add(ACT_PLAY_SONG, _T("Play the song"), false, true);
			return;
		}
		if (IsMelodyStep()) {
			if (N.Tool == "grow_melody")
				Add(ACT_GROW, _T("Grow one"), true, true);
			else
				Add(ACT_DO, _T("Use this phrase"), false, true);
			Add(ACT_RECORD, _T("Record it"), true, false);
			Add(ACT_DRAW, _T("Draw it"), false, false);
		}
		else if (N.AskUser) {
			if (N.Id == "save")
				Add(ACT_SAVE_AS, _T("Save as..."), false, true);
		}
		else if (N.Calls <= 1 && !N.Tool.empty())
			Add(ACT_DO, _T("Do it"), false, true);
		Add(ACT_SKIP, _T("Skip"), false, false);
	}

	CString KindLabel() const
	{
		const std::string Kind = Current() ? Answer.Kind : ChosenKind();
		CString Name = FromUtf8(Kind.empty() ? std::string("general") : Kind);
		if (!Name.IsEmpty())
			Name.SetAt(0, static_cast<TCHAR>(_totupper(Name[0])));
		return _T("Kind: ") + Name;
	}

	// How far the song has come: "A fix first. 3 of 20 done", shorter for less room (Form 0, 1, 2)
	CString ProgressLabel(int Form = 0) const
	{
		CString Text;
		if (!Current() || Answer.Total <= 0)
			return Text;
		Text.Format(Form < 2 ? _T("%d of %d done") : _T("%d/%d"), Answer.DoneCount, Answer.Total);
		if (Form == 0 && !Answer.Done && Answer.Step.Stage < 0 && !Answer.Step.AskUser)
			Text = _T("A fix first. ") + Text;
		return Text;
	}

	// Lays out the header (the title, the stages, the progress, the kind, the menu) and the body (what, why, the
	// buttons, the last click's words)
	void Arrange()
	{
		CRect Client;
		pWnd->GetClientRect(&Client);
		CClientDC dc(pWnd);
		CFont *pOld = dc.SelectObject(&Font);
		const int Margin = SX(MARGIN);
		MakeTexts();
		MakeButtons();

		// The header's right: the menu, the kind, the progress
		MenuRect = CRect(Client.right - Margin - SX(28), Client.top + SY(3), Client.right - Margin, Client.top + SY(HEADER_HEIGHT) - SY(4));
		const CString Kind = KindLabel();
		const int KindWidth = dc.GetTextExtent(Kind).cx + SX(34);
		KindRect = CRect(MenuRect.left - SX(4) - KindWidth, MenuRect.top, MenuRect.left - SX(4), MenuRect.bottom);
		// The stages after the title, or on a line of their own when they don't fit there; the progress as long as there's
		// room for it beside the title
		dc.SelectObject(&BoldFont);
		const int TitleRight = Client.left + Margin + dc.GetTextExtent(_T("What next")).cx + SX(14);
		dc.SelectObject(&Font);
		int ProgressWidth = 0;
		for (int Form = 0; Form < 4; ++Form) {
			ProgressText = Form < 3 ? ProgressLabel(Form) : CString();
			ProgressWidth = ProgressText.IsEmpty() ? 0 : dc.GetTextExtent(ProgressText).cx;
			if (KindRect.left - SX(10) - ProgressWidth >= TitleRight)
				break;
		}
		ProgressRect = CRect(KindRect.left - SX(10) - ProgressWidth, Client.top, KindRect.left - SX(10), Client.top + SY(HEADER_HEIGHT));
		dc.SelectObject(&SmallFont);
		int Widths[ftapi::NEXT_STAGES], Total = 0;
		for (int s = 0; s < ftapi::NEXT_STAGES; ++s) {
			Widths[s] = dc.GetTextExtent(STAGE_LABELS[s]).cx + SX(14);
			Total += Widths[s] + SX(3);
		}
		Narrow = TitleRight + Total > ProgressRect.left - SX(8);
		int X = TitleRight, Y = Client.top + (SY(HEADER_HEIGHT) - SY(STAGE_HEIGHT)) / 2;
		Header = CRect(Client.left, Client.top, Client.right, Client.top + SY(HEADER_HEIGHT));
		if (Narrow) {
			X = Client.left + Margin;
			Y = Client.top + SY(HEADER_HEIGHT);
			Header.bottom = Y + SY(STAGE_HEIGHT) + SY(5);
			const int Room = Client.Width() - 2 * Margin;
			if (Total > Room && Total > 0)
				for (int s = 0; s < ftapi::NEXT_STAGES; ++s)
					Widths[s] = std::max(SX(12), (Widths[s] + SX(3)) * Room / Total - SX(3));
		}
		for (int s = 0; s < ftapi::NEXT_STAGES; ++s) {
			StageRects[s] = CRect(X, Y, X + Widths[s], Y + SY(STAGE_HEIGHT));
			X += Widths[s] + SX(3);
		}
		Body = CRect(Client.left, Header.bottom, Client.right, std::max(static_cast<int>(Header.bottom), static_cast<int>(Client.bottom)));

		// The body, as if nothing were scrolled (the text wraps a little short of the room: fonts measured and drawn apart
		// can draw past it)
		const int Width = std::max(SX(60), Body.Width() - 2 * Margin - SX(4));
		int Top = SY(5);
		WhatRect.SetRectEmpty();
		if (!WhatText.IsEmpty()) {
			dc.SelectObject(&BigFont);
			CRect R(Margin, Top, Margin + Width, Top);
			dc.DrawText(WhatText, &R, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
			WhatRect = CRect(Margin, Top, Margin + Width, Top + R.Height());
			Top = WhatRect.bottom + SY(3);
		}
		WhyRect.SetRectEmpty();
		if (!WhyText.IsEmpty()) {
			dc.SelectObject(&Font);
			CRect R(Margin, Top, Margin + Width, Top);
			dc.DrawText(WhyText, &R, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
			WhyRect = CRect(Margin, Top, Margin + Width, Top + R.Height());
			Top = WhyRect.bottom + SY(6);
		}
		// The buttons in a row, onto the next when there's no room
		dc.SelectObject(&Font);
		int Left = Margin;
		for (size_t i = 0; i < Buttons.size(); ++i) {
			const int W = dc.GetTextExtent(Buttons[i].Text).cx + SX(22) + (Buttons[i].Menu ? SX(12) : 0);
			if (Left > Margin && Left + W > Margin + Width) {
				Left = Margin;
				Top += SY(BUTTON_HEIGHT) + SY(4);
			}
			Buttons[i].Rect = CRect(Left, Top, Left + W, Top + SY(BUTTON_HEIGHT));
			Left += W + SX(6);
		}
		if (!Buttons.empty())
			Top += SY(BUTTON_HEIGHT) + SY(5);
		LastRect.SetRectEmpty();
		const CString Said = LastText();
		if (!Said.IsEmpty()) {
			dc.SelectObject(&SmallFont);
			CRect R(Margin, Top, Margin + Width, Top);
			dc.DrawText(Said, &R, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
			LastRect = CRect(Margin, Top, Margin + Width, Top + R.Height());
			Top = LastRect.bottom + SY(4);
		}
		Content = Top + SY(4);
		dc.SelectObject(pOld);
		ScrollTo(Scroll, false);
	}

	// What the last click did, or what plays now, or why the panel waits
	CString LastText() const
	{
		if (Player.IsPlaying())
			return PlayingWhat;
		if (Current() && Stale)
			return theApp.IsPlaying() ? _T("The song changed: the panel asks again when it stops playing.") :
				_T("The song changed: the panel asks again in a moment.");
		return Last;
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

	// A body rectangle where it is on the window now
	CRect Shown(const CRect &Rect) const
	{
		CRect Out(Rect);
		Out.OffsetRect(Body.left, Body.top - Scroll);
		return Out;
	}

	int ButtonAt(const CPoint &Point) const
	{
		if (!Body.PtInRect(Point))
			return -1;
		for (size_t i = 0; i < Buttons.size(); ++i)
			if (Shown(Buttons[i].Rect).PtInRect(Point))
				return static_cast<int>(i);
		return -1;
	}

	int StageAt(const CPoint &Point) const
	{
		for (int s = 0; s < ftapi::NEXT_STAGES; ++s)
			if (StageRects[s].PtInRect(Point))
				return s;
		return -1;
	}

	// Doing

	// The cursor to a bar's first row on a channel (the pattern editor's, which the piano roll and the recorder follow);
	// pRow: that row, from the song's start
	bool MoveCursor(int Bar, int Channel, int *pRow = NULL)
	{
		CFamiTrackerView *pView = View();
		CFamiTrackerDoc *pDoc = Doc();
		CMainFrame *pMain = Main();
		if (pView == NULL || pDoc == NULL || pMain == NULL)
			return false;
		if (theApp.IsPlaying())
			theApp.StopPlayerAndWait();	// the cursor follows the song while it plays
		const unsigned int Track = static_cast<unsigned int>(std::max(0, pMain->GetSelectedTrack()));
		const ftapi::Timing Time(static_cast<int>(pDoc->GetPatternLength(Track)), static_cast<int>(pDoc->GetFrameCount(Track)),
			pDoc->GetFirstHighlight(), pDoc->GetSecondHighlight());
		const int Row = Time.BarStart(std::max(1, Bar));
		if (Row >= Time.TotalRows())
			return false;
		if (pRow != NULL)
			*pRow = Row;
		const int Length = std::max(1, Time.PatternLength);
		pView->SelectFrame(static_cast<unsigned int>(Row / Length));
		pView->GetPatternEditor()->MoveToRow(Row % Length);
		if (Channel >= 0 && Channel < static_cast<int>(pDoc->GetAvailableChannels()))
			pView->SelectChannel(static_cast<unsigned int>(Channel));
		else
			pView->SelectChannel(pView->GetSelectedChannel());	// draws the cursor where it is now
		return true;
	}

	// A call of the tools as one undoable step (what it adds to the instruments stays: instruments aren't undone); the
	// reply in Reply. A call that fails is taken back. Undoable: Ctrl+Z undoes it; Added: instruments it added
	bool RunAsOne(const std::string &Tool, const std::string &Arguments, const std::string &Label, std::string &Reply, bool &Undoable,
		int &Added)
	{
		CMainFrame *pMain = Main();
		CFamiTrackerDoc *pDoc = Doc();
		Undoable = false;
		Added = 0;
		if (pMain == NULL || pDoc == NULL || pTools == NULL)
			return false;
		const long long Before = pMain->GetActionSerial();
		const int Instruments = static_cast<int>(pDoc->GetInstrumentCount());
		pWnd->BeginWaitCursor();
		Doing = true;
		pMain->BeginActionGroup();
		const bool Done = pTools->RunTool(Tool.c_str(), Arguments, Reply);
		pMain->EndActionGroup();
		Doing = false;
		pWnd->EndWaitCursor();
		Undoable = pMain->GetActionSerial() != Before;
		Added = std::max(0, static_cast<int>(pDoc->GetInstrumentCount()) - Instruments);
		if (!Done) {
			if (Undoable) {	// what it changed before it failed, and nothing to redo of it
				pMain->SendMessage(WM_COMMAND, ID_EDIT_UNDO);
				pMain->ForgetRedo();
			}
			Undoable = false;
			return false;
		}
		CAction *pAction = Undoable ? pMain->GetUndoAction() : NULL;
		if (pAction != NULL)
			pAction->SetLabel(Tool + ": " + Label + " (the What next panel)");
		return true;
	}

	static void ShowReply(const std::string &Reply)
	{
		CString Text = FromUtf8(Reply);
		Text.Replace(_T("\r\n"), _T("\n"));
		Text.Replace(_T("\n"), _T("\r\n"));
		AfxMessageBox(Text, MB_OK | MB_ICONINFORMATION);
	}

	// What a click did, and what Ctrl+Z does about it: adding an instrument isn't undone (as anywhere in FamiTracker)
	CString DoneWords(const CString &What, bool Undoable, int Added) const
	{
		CString Text = _T("Done: ") + What + _T(".");
		const CString Stays = Added > 1 ? _T("the new instruments stay (Instrument > Remove instrument takes them out)") :
			_T("the new instrument stays (Instrument > Remove instrument takes it out)");
		if (Undoable && Added > 0)
			Text += _T(" Ctrl+Z undoes it; ") + Stays + _T(".");
		else if (Undoable)
			Text += _T(" Ctrl+Z undoes it.");
		else if (Added > 0)
			Text += _T(" Ctrl+Z doesn't take an instrument out: ") + Stays + _T(".");
		return Text;
	}

	// What a slow call says while it runs, empty for a quick one: the listen renders the song and each part alone and then
	// listens, the finish polishes, mixes, listens and renders the file, all on the UI thread
	static CString WorkingWords(const std::string &Tool)
	{
		if (Tool == "listen")
			return _T("Listening to the song: FamiTracker plays it to itself, the mix and each part alone, then listens. This ")
				_T("takes a few seconds, longer for a long song...");
		if (Tool == "finish_song")
			return _T("Finishing the song: polish, the mix, the loop's seam, a listen and the game's file. This takes a few ")
				_T("seconds, longer for a long song...");
		if (Tool == "export_for_game")
			return _T("Making the game's file: FamiTracker plays the song into it. This takes a few seconds...");
		return CString();
	}

	// Says what runs, in the panel and the status bar, drawn at once: the call keeps the UI thread until it's done (a
	// render lets the windows draw, then puts the status bar's text back)
	void ShowWorking(const CString &Text)
	{
		Last = Text;
		Say(Last);
		Changed();
		if (pWnd->GetSafeHwnd() != NULL)
			pWnd->UpdateWindow();
		CMainFrame *pMain = Main();
		CWnd *pBar = pMain != NULL ? pMain->GetMessageBar() : NULL;
		if (pBar != NULL)
			pBar->UpdateWindow();
	}

	// Do it: the step's call, one Ctrl+Z
	void DoIt()
	{
		if (!Current() || Stale || Answer.Done)
			return;
		const ftapi::NextStepInfo N = Answer.Step;
		if (N.Tool.empty() || N.Calls > 1)
			return;
		if (pTools == NULL || pTools->IsBusy()) {
			Say(_T("FamiTracker is busy with Claude Code; try again in a moment."));
			return;
		}
		const CString Working = WorkingWords(N.Tool);
		if (!Working.IsEmpty())
			ShowWorking(Working);
		std::string Reply;
		bool Undoable = false;
		int Added = 0;
		if (!RunAsOne(N.Tool, N.Arguments, N.What, Reply, Undoable, Added)) {
			if (!Working.IsEmpty()) {
				Last.Empty();
				Say(_T(""));
				Changed();
			}
			ShowReply(Reply);
			AskSoon(WAIT_MS);
			return;
		}
		Last = DoneWords(FromUtf8(N.What), Undoable, Added);
		Say(Last);
		Stale = true;
		AskSoon(WAIT_MS);	// (the song changed, or a file did)
		Changed();
	}

	// Skip: the step left out of this track (what_next's skip) until it's brought back from the menu
	void Skip()
	{
		if (!Current() || Answer.Done || Answer.Step.Id.empty())
			return;
		std::vector<std::string> &List = Skips[SongKey()];
		if (std::find(List.begin(), List.end(), Answer.Step.Id) == List.end())
			List.push_back(Answer.Step.Id);
		Last = _T("Skipped: ") + FromUtf8(Answer.Step.What) + _T(". The menu (") + _T("the arrow at the top right) brings it back.");
		Say(Last);
		Ask(true, true);
	}

	// Grow one: the versions grow_melody grows from the hook, to pick from; the one picked is written (one Ctrl+Z) and
	// its bars play
	void Grow(const CPoint &Screen)
	{
		if (!IsMelodyStep() || Stale || Answer.Step.Tool != "grow_melody")
			return;
		const ftapi::NextStepInfo N = Answer.Step;
		if (pTools == NULL || pTools->IsBusy()) {
			Say(_T("FamiTracker is busy with Claude Code; try again in a moment."));
			return;
		}
		std::string Reply;
		pWnd->BeginWaitCursor();
		const bool Listed = pTools->RunTool("grow_melody", N.Arguments, Reply);
		pWnd->EndWaitCursor();
		const std::vector<std::string> Versions = ftapi::GrownVersions(Reply);
		if (!Listed || Versions.empty()) {
			ShowReply(Reply);
			return;
		}
		CMenu Popup;
		if (!Popup.CreatePopupMenu())
			return;
		for (size_t v = 0; v < Versions.size(); ++v) {
			CString Shape = FromUtf8(Versions[v]);
			if (Shape.GetLength() > 90)
				Shape = Shape.Left(87) + _T("...");
			Shape.Replace(_T("&"), _T("&&"));
			CString Item;
			Item.Format(_T("Version %d: %s"), static_cast<int>(v) + 1, static_cast<LPCTSTR>(Shape));
			Popup.AppendMenu(MF_STRING, MENU_VERSION + static_cast<UINT>(v), Item);
		}
		const int Chosen = static_cast<int>(Popup.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY, Screen.x,
			Screen.y, pWnd));
		const int Pick = Chosen - MENU_VERSION + 1;
		if (Chosen < MENU_VERSION || Pick > static_cast<int>(Versions.size()))
			return;
		ftapi::Json Args;
		if (!ftapi::ParseJson(N.Arguments, Args) || !Args.IsObject())
			return;
		Args.Set("pick", Pick);
		bool Undoable = false;
		int Added = 0;
		if (!RunAsOne("grow_melody", ftapi::WriteJson(Args), N.What, Reply, Undoable, Added)) {
			ShowReply(Reply);
			return;
		}
		CString Words;
		Words.Format(_T("Version %d grown in %s"), Pick, static_cast<LPCTSTR>(Bars(N.FromBar, N.ToBar)));
		Last = DoneWords(Words, Undoable, Added) + (Undoable ? _T(" (Then Grow one again picks another version.)") : _T(""));
		Stale = true;
		// Heard at once: its bars, in the mix
		CFamiTrackerDoc *pDoc = Doc();
		CMainFrame *pMain = Main();
		if (pDoc != NULL && pMain != NULL && N.FromBar > 0) {
			const unsigned int Track = static_cast<unsigned int>(std::max(0, pMain->GetSelectedTrack()));
			const ftapi::Timing Time(static_cast<int>(pDoc->GetPatternLength(Track)), static_cast<int>(pDoc->GetFrameCount(Track)),
				pDoc->GetFirstHighlight(), pDoc->GetSecondHighlight());
			if (Player.Play(Time.BarStart(N.FromBar), std::min(Time.TotalRows(), Time.BarEnd(std::max(N.FromBar, N.ToBar))),
				std::vector<int>(), CBarsPlayer::MIX)) {
				PlayingWhat.Format(_T("Playing version %d (%s). Stop stops it; Ctrl+Z takes it back."), Pick,
					static_cast<LPCTSTR>(Bars(N.FromBar, N.ToBar)));
				pWnd->SetTimer(TMR_PLAY, PLAY_MS, NULL);
			}
		}
		Say(Player.IsPlaying() ? PlayingWhat : Last);
		AskSoon(WAIT_MS);
		Changed();
	}

	// Record it: from the microphone at the melody's first bar on its channel (Record from microphone sings over the
	// song from a bar before), or from the phone (its takes come as clips to place)
	void Record(const CPoint &Screen)
	{
		if (!IsMelodyStep())
			return;
		const ftapi::NextStepInfo N = Answer.Step;	// (the panel may ask again while the menu is open)
		CMenu Popup;
		if (!Popup.CreatePopupMenu())
			return;
		Popup.AppendMenu(MF_STRING, MENU_MIC, _T("Sing, hum or whistle it into the microphone..."));
		Popup.AppendMenu(MF_STRING, MENU_PHONE, _T("Record it on your phone..."));
		const int Chosen = static_cast<int>(Popup.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY, Screen.x,
			Screen.y, pWnd));
		CMainFrame *pMain = Main();
		if (pMain == NULL)
			return;
		if (Chosen == MENU_MIC) {
			MoveCursor(N.FromBar, N.Channel);
			Last.Format(_T("Record the melody for %s: the take goes in at the cursor, on the melody's channel."),
				static_cast<LPCTSTR>(Bars(N.FromBar, N.ToBar)));
			Changed();
			pMain->PostMessage(WM_COMMAND, ID_EDIT_RECORD);
		}
		else if (Chosen == MENU_PHONE) {
			MoveCursor(N.FromBar, N.Channel);
			Last.Format(_T("Hum the melody into your phone: each take comes as a clip in the clips panel; drag it to %s."),
				static_cast<LPCTSTR>(Bars(N.FromBar, N.ToBar)));
			Changed();
			pMain->PostMessage(WM_COMMAND, ID_EDIT_RECORDPHONE);
		}
	}

	// Draw it: the piano roll at the melody's first bar, on its channel, with the keys
	void Draw()
	{
		if (!IsMelodyStep())
			return;
		CMainFrame *pMain = Main();
		CFamiTrackerDoc *pDoc = Doc();
		if (pMain == NULL || pDoc == NULL)
			return;
		const ftapi::NextStepInfo N = Answer.Step;
		if (pMain->GetPianoRollWnd() == NULL)
			pMain->ShowPianoRoll(true);
		int Row = 0;
		if (!MoveCursor(N.FromBar, N.Channel, &Row))
			return;
		CPianoRollWnd *pRoll = pMain->GetPianoRollWnd();
		if (pRoll == NULL)
			return;
		if (N.Channel >= 0)
			pRoll->ShowNotes(N.Channel, &Row, 1, false);	// there, on the melody's channel (no note starts there yet)
		pRoll->SetFocus();
		const CString Channel = N.Channel >= 0 && N.Channel < static_cast<int>(pDoc->GetAvailableChannels()) ?
			RecordingChannelName(pDoc, N.Channel) : CString(_T("the melody's channel"));
		Last.Format(_T("Draw the melody in the piano roll, %s on %s: pick Draw and click to add a note (the key's notes are the ")
			_T("lighter lanes), or Paint and drag. Ctrl+Z undoes."), static_cast<LPCTSTR>(Bars(N.FromBar, N.ToBar)),
			static_cast<LPCTSTR>(Channel));
		Say(Last);
		Changed();
	}

	void Act(int Index, const CPoint &Screen)
	{
		if (Index < 0 || Index >= static_cast<int>(Buttons.size()))
			return;
		switch (Buttons[static_cast<size_t>(Index)].Action) {
		case ACT_DO: DoIt(); break;
		case ACT_SKIP: Skip(); break;
		case ACT_GROW: Grow(Screen); break;
		case ACT_RECORD: Record(Screen); break;
		case ACT_DRAW: Draw(); break;
		case ACT_SAVE_AS: {
			CMainFrame *pMain = Main();
			if (pMain != NULL)
				pMain->PostMessage(WM_COMMAND, ID_FILE_SAVE_AS);
			break;
		}
		case ACT_PLAY_SONG:
			if (theApp.IsPlaying())
				theApp.StopPlayerAndWait();
			theApp.StartPlayer(MODE_PLAY_START);
			break;
		case ACT_STOP:
			Player.Stop();
			pWnd->KillTimer(TMR_PLAY);
			Changed();
			break;
		case ACT_ASK: Ask(true, true); break;
		default: break;
		}
	}

	// What a button does, for the status bar
	CString ButtonWords(int Index) const
	{
		if (Index < 0 || Index >= static_cast<int>(Buttons.size()))
			return CString();
		const ftapi::NextStepInfo &N = Answer.Step;
		switch (Buttons[static_cast<size_t>(Index)].Action) {
		case ACT_DO: return _T("Do it: ") + FromUtf8(N.What) + _T(", as one Ctrl+Z");
		case ACT_SKIP: return _T("Skip: leave this step out of the song (the panel's menu brings it back)");
		case ACT_GROW: return _T("Grow one: versions of the melody grown from a hook over the chords; the one you pick plays");
		case ACT_RECORD: return _T("Record it: sing, hum or whistle the melody into the microphone, or into your phone");
		case ACT_DRAW: return _T("Draw it: the piano roll opens at these bars, on the melody's channel");
		case ACT_SAVE_AS: return _T("Save as: give the song a name and a place (the game's file goes next to it)");
		case ACT_PLAY_SONG: return _T("Play the song from the start");
		case ACT_STOP: return _T("Stop playing");
		case ACT_ASK: return _T("Ask what to do next now (F5)");
		default: return CString();
		}
	}

	// A stage's milestones, for the status bar: "The parts: done the lead's sound, chords and bass; left the melody, drums"
	CString StageWords(int Stage) const
	{
		CString Text = FromUtf8(ftapi::NextStageTitle(Stage));
		if (!Current())
			return Text;
		CString Done, Left;
		for (size_t i = 0; i < Answer.Milestones.size(); ++i) {
			const ftapi::NextMilestone &M = Answer.Milestones[i];
			if (M.Stage != Stage)
				continue;
			CString &List = M.Done ? Done : Left;
			List += (List.IsEmpty() ? _T("") : _T(", ")) + FromUtf8(M.Done ? M.Text : M.Name);
		}
		if (Done.IsEmpty() && Left.IsEmpty())
			return Text + _T(": nothing for this song");
		Text += _T(":");
		if (!Done.IsEmpty())
			Text += _T(" done ") + Done + (Left.IsEmpty() ? _T("") : _T(";"));
		if (!Left.IsEmpty())
			Text += _T(" left ") + Left;
		return Text;
	}

	void Menu(const CPoint &Screen)
	{
		CMenu Popup;
		if (!Popup.CreatePopupMenu())
			return;
		Popup.AppendMenu(MF_STRING, MENU_ASK, _T("Ask again\tF5"));
		if (Current() && !Answer.Notes.empty())
			Popup.AppendMenu(MF_STRING, MENU_NOTES, _T("Notes from what next..."));
		const std::vector<std::string> Skip = Skipped();
		if (!Skip.empty()) {
			Popup.AppendMenu(MF_SEPARATOR);
			for (size_t i = 0; i < Skip.size() && i < 30; ++i)
				Popup.AppendMenu(MF_STRING, MENU_UNSKIP + static_cast<UINT>(i), _T("Bring back the step skipped: ") + FromUtf8(Skip[i]));
		}
		Popup.AppendMenu(MF_SEPARATOR);
		// Where it goes: a tab at the bottom, on the right, or its own window
		CPanelBar *pBar = CPanelBar::Of(pWnd);
		if (pBar != NULL)
			pBar->AddPlaceItems(Popup, MENU_PLACE);
		Popup.AppendMenu(MF_STRING, MENU_CLOSE, _T("Close"));
		const int Chosen = static_cast<int>(Popup.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY, Screen.x,
			Screen.y, pWnd));
		if (Chosen == MENU_ASK)
			Ask(true, true);
		else if (Chosen == MENU_NOTES) {
			CString Text;
			for (size_t i = 0; i < Answer.Notes.size(); ++i)
				Text += (i == 0 ? _T("") : _T("\r\n\r\n")) + FromUtf8(Answer.Notes[i]);
			AfxMessageBox(Text, MB_OK | MB_ICONINFORMATION);
		}
		else if (Chosen >= MENU_UNSKIP && Chosen < MENU_UNSKIP + static_cast<int>(Skip.size())) {
			std::vector<std::string> &List = Skips[SongKey()];
			const std::string Id = Skip[static_cast<size_t>(Chosen - MENU_UNSKIP)];
			List.erase(std::remove(List.begin(), List.end(), Id), List.end());
			Last = _T("Brought back: ") + FromUtf8(Id) + _T(".");
			Ask(true, true);
		}
		else if (Chosen == MENU_CLOSE)
			AfxGetMainWnd()->PostMessage(WM_COMMAND, ID_VIEW_WHATNEXT);
		else if (pBar != NULL)
			pBar->PickPlace(static_cast<UINT>(Chosen), MENU_PLACE);	// a tab at the bottom, on the right, its own window
	}

	// The kind of song: what_next follows that recipe for this track
	void KindMenu(const CPoint &Screen)
	{
		CMenu Popup;
		if (!Popup.CreatePopupMenu())
			return;
		const std::string Now = ChosenKind();
		for (int k = 0; k < KIND_COUNT; ++k)
			Popup.AppendMenu(MF_STRING | (Now == KIND_IDS[k] ? MF_CHECKED : MF_UNCHECKED), MENU_KIND + static_cast<UINT>(k), KIND_ITEMS[k]);
		const int Chosen = static_cast<int>(Popup.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY, Screen.x,
			Screen.y, pWnd));
		if (Chosen < MENU_KIND || Chosen >= MENU_KIND + KIND_COUNT)
			return;
		ChosenKinds()[SongKey()] = KIND_IDS[Chosen - MENU_KIND];
		Ask(true, true);
	}

	// Drawing

	void DrawButton(CDC &dc, const CRect &Rect, LPCTSTR Text, bool Hot, bool Primary, bool Arrow)
	{
		const COLORREF Back = Primary ? (Hot ? PRIMARY_HOT : PRIMARY_BACK) : BUTTON_BACK;
		CBrush Brush(Back);
		CPen Border(PS_SOLID, 1, Primary ? Back : (Hot ? BUTTON_HOT : BUTTON_BORDER));
		CBrush *pOldBrush = dc.SelectObject(&Brush);
		CPen *pOldPen = dc.SelectObject(&Border);
		dc.RoundRect(&Rect, CPoint(SX(6), SY(6)));
		dc.SelectObject(pOldBrush);
		dc.SelectObject(pOldPen);
		CFont *pOld = dc.SelectObject(Primary ? &BoldFont : &Font);
		const COLORREF Ink = Primary ? RGB(255, 255, 255) : (Hot ? BUTTON_HOT : TEXT_DARK);
		dc.SetTextColor(Ink);
		CRect Inside(Rect);
		if (Arrow)
			Inside.right -= SX(14);
		dc.DrawText(Text, &Inside, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		if (Arrow)
			DownArrow(dc, Rect.right - SX(16), Rect.CenterPoint().y - 1, Ink);
		dc.SelectObject(pOld);
	}

	void Paint(CDC &dc, const CRect &Client)
	{
		dc.FillSolidRect(&Client, BODY_BACK);
		dc.SetBkMode(TRANSPARENT);
		CFont *pOldFont = dc.SelectObject(&BoldFont);

		// The header: the title, the stages, how far the song has come, the kind and the menu
		dc.FillSolidRect(&Header, PANEL_BACK);
		dc.FillSolidRect(Header.left, Header.bottom - 1, Header.Width(), 1, RGB(208, 212, 220));
		dc.SetTextColor(TEXT_DARK);
		CRect Title(Header.left + SX(MARGIN), Header.top, Header.right, Header.top + SY(HEADER_HEIGHT));
		dc.DrawText(_T("What next"), &Title, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		const int Now = Current() && !Answer.Done ? Answer.Step.Stage : -1;	// (a fix comes before the stages: none lit)
		dc.SelectObject(&SmallFont);
		for (int s = 0; s < ftapi::NEXT_STAGES; ++s) {
			const CRect &R = StageRects[s];
			const int Count = Current() ? Answer.StageCount[s] : 0, Done = Current() ? Answer.StageDone[s] : 0;
			const bool Whole = Count > 0 && Done == Count, Here = s == Now;
			const COLORREF Fill = Here ? STAGE_NOW : Whole ? STAGE_DONE : Done > 0 ? STAGE_PART : PANEL_BACK;
			CBrush Brush(Fill);
			CPen Border(PS_SOLID, 1, Here ? BUTTON_HOT : Whole ? RGB(150, 200, 156) : STAGE_BORDER);
			CBrush *pOldBrush = dc.SelectObject(&Brush);
			CPen *pOldPen = dc.SelectObject(&Border);
			dc.RoundRect(&R, CPoint(SX(8), SY(8)));
			dc.SelectObject(pOldBrush);
			dc.SelectObject(pOldPen);
			dc.SelectObject(Here ? &BoldFont : &SmallFont);
			dc.SetTextColor(Here ? TEXT_DARK : Whole ? STAGE_DONE_TEXT : (Count == 0 && Current()) ? RGB(170, 174, 182) : TEXT_DIM);
			CRect Text(R);
			dc.DrawText(STAGE_LABELS[s], &Text, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
		}
		dc.SelectObject(&Font);
		dc.SetTextColor(TEXT_DIM);
		CRect Progress(ProgressRect);
		dc.DrawText(ProgressText, &Progress, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		DrawButton(dc, KindRect, KindLabel(), HoverKind, false, true);
		DrawButton(dc, MenuRect, _T(""), HoverMenu, false, false);
		DownArrow(dc, MenuRect.CenterPoint().x - 3, MenuRect.CenterPoint().y - 1, TEXT_DARK);

		// The body: what, why, the buttons, the last click
		dc.IntersectClipRect(&Body);
		const bool Dim = Current() && Stale;
		if (!WhatRect.IsRectEmpty()) {
			dc.SelectObject(&BigFont);
			dc.SetTextColor(Dim ? TEXT_DIM : TEXT_DARK);
			CRect R = Shown(WhatRect);
			dc.DrawText(WhatText, &R, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX | DT_NOCLIP);
		}
		if (!WhyRect.IsRectEmpty()) {
			dc.SelectObject(&Font);
			dc.SetTextColor(Dim || !Current() ? TEXT_DIM : TEXT_DARK);
			CRect R = Shown(WhyRect);
			dc.DrawText(WhyText, &R, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX | DT_NOCLIP);
		}
		for (size_t i = 0; i < Buttons.size(); ++i)
			DrawButton(dc, Shown(Buttons[i].Rect), Buttons[i].Text, Hover == static_cast<int>(i), Buttons[i].Primary, Buttons[i].Menu);
		if (!LastRect.IsRectEmpty()) {
			dc.SelectObject(&SmallFont);
			dc.SetTextColor(TEXT_DIM);
			CRect R = Shown(LastRect);
			dc.DrawText(LastText(), &R, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX | DT_NOCLIP);
		}
		if (Focused && !Buttons.empty()) {
			CRect R = Shown(Buttons[0].Rect);
			R.InflateRect(SX(2), SY(2));
			dc.DrawFocusRect(&R);
		}
		dc.SelectClipRgn(NULL);
		dc.SelectObject(pOldFont);
	}
};

// The panel

BEGIN_MESSAGE_MAP(CWhatNextPanelWnd, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_SIZE()
	ON_WM_VSCROLL()
	ON_WM_MOUSEWHEEL()
	ON_WM_LBUTTONDOWN()
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

CWhatNextPanelWnd::CWhatNextPanelWnd() : m_pImpl(NULL)
{
	m_pImpl = new Impl(this);
}

CWhatNextPanelWnd::~CWhatNextPanelWnd()
{
	delete m_pImpl;
}

bool CWhatNextPanelWnd::Create(CWnd *pParent, CMcpConnection *pTools)
{
	m_pImpl->pTools = pTools;
	const CString Class = AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(NULL, IDC_ARROW), NULL, NULL);
	if (!CWnd::Create(Class, _T("What next"), WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP, CRect(0, 0, 0, 0), pParent, 1))
		return false;
	m_pImpl->MakeFonts();
	return true;
}

void CWhatNextPanelWnd::Opened(bool Open)
{
	m_pImpl->Open = Open;
	if (GetSafeHwnd() == NULL)
		return;
	if (Open) {
		SetTimer(TMR_LOOK, LOOK_MS, NULL);
		m_pImpl->Ask(true);
	}
	else {
		KillTimer(TMR_ASK);
		KillTimer(TMR_LOOK);
		KillTimer(TMR_PLAY);
		m_pImpl->Player.Stop();
		m_pImpl->Waiting = false;
	}
}

void CWhatNextPanelWnd::SongChanged()
{
	if (!m_pImpl->Open || GetSafeHwnd() == NULL)
		return;
	if (!m_pImpl->Doing)
		m_pImpl->Last.Empty();	// changed otherwise (Ctrl+Z, an edit, Claude Code): what the last click did is past
	if (m_pImpl->Current() && !m_pImpl->Stale) {
		m_pImpl->Stale = true;	// the step may not be the next any more: its buttons wait for the next answer
		m_pImpl->Changed();
	}
	m_pImpl->AskSoon(ASK_MS);
}

BOOL CWhatNextPanelWnd::PreTranslateMessage(MSG *pMsg)
{
	// The panel's keys come here before FamiTracker's shortcuts see them
	if ((pMsg->message == WM_KEYDOWN || pMsg->message == WM_SYSKEYDOWN) && pMsg->hwnd == GetSafeHwnd()) {
		const bool Alt = ::GetKeyState(VK_MENU) < 0;
		switch (Alt ? 0 : pMsg->wParam) {
		case VK_RETURN: case VK_SPACE: {
			if (m_pImpl->Buttons.empty())
				return TRUE;
			CRect R = m_pImpl->Shown(m_pImpl->Buttons[0].Rect);
			CPoint Screen(R.left, R.bottom);
			ClientToScreen(&Screen);
			m_pImpl->Act(0, Screen);
			return TRUE;
		}
		case VK_F5:
			m_pImpl->Ask(true, true);
			return TRUE;
		case VK_ESCAPE: {
			CFamiTrackerView *pView = CFamiTrackerView::GetView();
			if (pView != NULL)
				pView->SetFocus();
			return TRUE;
		}
		case VK_UP: case VK_PRIOR:
			m_pImpl->ScrollTo(m_pImpl->Scroll - SY(LINE) * (pMsg->wParam == VK_UP ? 1 : 4));
			return TRUE;
		case VK_DOWN: case VK_NEXT:
			m_pImpl->ScrollTo(m_pImpl->Scroll + SY(LINE) * (pMsg->wParam == VK_DOWN ? 1 : 4));
			return TRUE;
		case VK_APPS: {
			CPoint Screen(m_pImpl->MenuRect.left, m_pImpl->MenuRect.bottom);
			ClientToScreen(&Screen);
			m_pImpl->Menu(Screen);
			return TRUE;
		}
		default:
			break;
		}
	}
	return CWnd::PreTranslateMessage(pMsg);
}

void CWhatNextPanelWnd::OnPaint()
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

BOOL CWhatNextPanelWnd::OnEraseBkgnd(CDC *pDC)
{
	return TRUE;
}

void CWhatNextPanelWnd::OnSize(UINT nType, int cx, int cy)
{
	CWnd::OnSize(nType, cx, cy);
	m_pImpl->Arrange();
	Invalidate(FALSE);
}

void CWhatNextPanelWnd::OnVScroll(UINT nSBCode, UINT nPos, CScrollBar *pScrollBar)
{
	int Position = m_pImpl->Scroll;
	const int Line = SY(LINE);
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

BOOL CWhatNextPanelWnd::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
	m_pImpl->ScrollTo(m_pImpl->Scroll - zDelta * SY(LINE) * 3 / WHEEL_DELTA);
	return TRUE;
}

void CWhatNextPanelWnd::OnLButtonDown(UINT nFlags, CPoint point)
{
	SetFocus();
	if (m_pImpl->MenuRect.PtInRect(point)) {
		CPoint Screen(m_pImpl->MenuRect.left, m_pImpl->MenuRect.bottom);
		ClientToScreen(&Screen);
		m_pImpl->Menu(Screen);
		return;
	}
	if (m_pImpl->KindRect.PtInRect(point)) {
		CPoint Screen(m_pImpl->KindRect.left, m_pImpl->KindRect.bottom);
		ClientToScreen(&Screen);
		m_pImpl->KindMenu(Screen);
		return;
	}
	const int Index = m_pImpl->ButtonAt(point);
	if (Index >= 0) {
		const CRect R = m_pImpl->Shown(m_pImpl->Buttons[static_cast<size_t>(Index)].Rect);
		CPoint Screen(R.left, R.bottom);
		ClientToScreen(&Screen);
		m_pImpl->Act(Index, Screen);
	}
}

void CWhatNextPanelWnd::OnRButtonUp(UINT nFlags, CPoint point)
{
	SetFocus();
	CPoint Screen = point;
	ClientToScreen(&Screen);
	m_pImpl->Menu(Screen);
}

void CWhatNextPanelWnd::OnMouseMove(UINT nFlags, CPoint point)
{
	int Index = m_pImpl->ButtonAt(point);
	const int Stage = m_pImpl->StageAt(point);
	if (Index < 0 && Stage >= 0)
		Index = HOVER_STAGE + Stage;
	const bool Kind = m_pImpl->KindRect.PtInRect(point) != FALSE, Menu = m_pImpl->MenuRect.PtInRect(point) != FALSE;
	if (Index == m_pImpl->Hover && Kind == m_pImpl->HoverKind && Menu == m_pImpl->HoverMenu)
		return;
	if (m_pImpl->Hover < 0 && !m_pImpl->HoverKind && !m_pImpl->HoverMenu) {
		// Told when the mouse leaves, so nothing stays lit
		TRACKMOUSEEVENT Track;
		memset(&Track, 0, sizeof(Track));
		Track.cbSize = sizeof(Track);
		Track.dwFlags = TME_LEAVE;
		Track.hwndTrack = m_hWnd;
		::TrackMouseEvent(&Track);
	}
	m_pImpl->Hover = Index;
	m_pImpl->HoverKind = Kind;
	m_pImpl->HoverMenu = Menu;
	Invalidate(FALSE);
	if (Index >= HOVER_STAGE)
		m_pImpl->Say(m_pImpl->StageWords(Index - HOVER_STAGE));
	else if (Index >= 0)
		m_pImpl->Say(m_pImpl->ButtonWords(Index));
	else if (Kind)
		m_pImpl->Say(_T("The kind of song: what next follows its recipe (a calm night, a lively stage, a cozy room, an action track)"));
}

void CWhatNextPanelWnd::OnMouseLeave()
{
	if (m_pImpl->Hover >= 0 || m_pImpl->HoverKind || m_pImpl->HoverMenu) {
		m_pImpl->Hover = -1;
		m_pImpl->HoverKind = m_pImpl->HoverMenu = false;
		Invalidate(FALSE);
	}
	CWnd::OnMouseLeave();
}

BOOL CWhatNextPanelWnd::OnSetCursor(CWnd *pWnd, UINT nHitTest, UINT message)
{
	if (m_pImpl->Doing) {	// its call runs (a render lets the windows go on meanwhile)
		::SetCursor(::LoadCursor(NULL, IDC_WAIT));
		return TRUE;
	}
	if (nHitTest == HTCLIENT) {
		CPoint Point;
		::GetCursorPos(&Point);
		ScreenToClient(&Point);
		if (m_pImpl->ButtonAt(Point) >= 0 || m_pImpl->KindRect.PtInRect(Point) || m_pImpl->MenuRect.PtInRect(Point)) {
			::SetCursor(::LoadCursor(NULL, IDC_HAND));
			return TRUE;
		}
	}
	return CWnd::OnSetCursor(pWnd, nHitTest, message);
}

UINT CWhatNextPanelWnd::OnGetDlgCode()
{
	return DLGC_WANTARROWS | DLGC_WANTCHARS | DLGC_WANTALLKEYS;
}

void CWhatNextPanelWnd::OnSetFocus(CWnd *pOldWnd)
{
	CWnd::OnSetFocus(pOldWnd);
	m_pImpl->Focused = true;
	Invalidate(FALSE);
}

void CWhatNextPanelWnd::OnKillFocus(CWnd *pNewWnd)
{
	CWnd::OnKillFocus(pNewWnd);
	m_pImpl->Focused = false;
	Invalidate(FALSE);
}

void CWhatNextPanelWnd::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == TMR_ASK) {
		KillTimer(TMR_ASK);
		m_pImpl->Ask(false);
		return;
	}
	if (nIDEvent == TMR_PLAY) {
		if (m_pImpl->Player.Tick() || !m_pImpl->Player.IsPlaying()) {
			if (!m_pImpl->Player.IsPlaying()) {
				KillTimer(TMR_PLAY);
				m_pImpl->Say(m_pImpl->Last);
			}
			m_pImpl->Changed();
		}
		return;
	}
	if (nIDEvent == TMR_LOOK) {
		// The mouse went away without a word
		if (m_pImpl->Hover >= 0 || m_pImpl->HoverKind || m_pImpl->HoverMenu) {
			CPoint Point;
			::GetCursorPos(&Point);
			CRect Window;
			GetWindowRect(&Window);
			if (!Window.PtInRect(Point)) {
				m_pImpl->Hover = -1;
				m_pImpl->HoverKind = m_pImpl->HoverMenu = false;
				Invalidate(FALSE);
			}
		}
		// Changes nothing tells the panel of: an instrument's envelope, the speed and tempo, the track, saving
		if (m_pImpl->Open && !m_pImpl->Waiting && m_pImpl->MakeSignature() != m_pImpl->Signature) {
			if (m_pImpl->Current() && !m_pImpl->Stale) {
				m_pImpl->Stale = true;
				m_pImpl->Changed();
			}
			m_pImpl->AskSoon(ASK_MS);
		}
		return;
	}
	CWnd::OnTimer(nIDEvent);
}

void CWhatNextPanelWnd::OnDestroy()
{
	KillTimer(TMR_ASK);
	KillTimer(TMR_LOOK);
	KillTimer(TMR_PLAY);
	if (m_pImpl->Player.IsPlaying())
		m_pImpl->Player.Stop();
	CWnd::OnDestroy();
}

// The bar it is docked in, or its own window (PanelBar.h)

CWhatNextBar::CWhatNextBar() : CPanelBar(CMainFrame::PANEL_WHAT_NEXT, SECTION, _T("What next"), DEFAULT_HEIGHT, DEFAULT_WIDTH,
	MIN_HEIGHT, MIN_WIDTH)
{
}

CWhatNextBar::~CWhatNextBar()
{
}

bool CWhatNextBar::Create(CFrameWnd *pFrame, CMcpConnection *pTools)
{
	if (!CreateBar(pFrame, AFX_IDW_CONTROLBAR_FIRST + 51) || !m_wndPanel.Create(this, pTools))
		return false;
	SetPanel(&m_wndPanel);
	return true;
}

void CWhatNextBar::PanelOpened(bool Open)
{
	m_wndPanel.Opened(Open);
}

void CWhatNextBar::SongChanged()
{
	m_wndPanel.SongChanged();
}

void CWhatNextBar::Focus()
{
	m_wndPanel.SetFocus();
}

bool CWhatNextBar::WasOpen()
{
	return theApp.GetProfileInt(SECTION, _T("Open"), 0) != 0;
}

int CWhatNextBar::PanelLeast(int Width) const
{
	// Its header and stages, the step's title, a few lines of why and its buttons (Do it), wrapped as narrow as it may be
	return SY(ROW_LEAST);
}

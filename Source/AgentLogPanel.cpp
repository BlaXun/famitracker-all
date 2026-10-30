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

// The agent log panel (see AgentLogPanel.h)

// Standard headers come before stdafx.h, whose DEBUG_NEW macro would
// otherwise rewrite the placement new inside the standard library.
#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include "stdafx.h"
#pragma push_macro("new")
#undef new
#include <afxpriv.h>		// WM_SIZEPARENT, for laying out the docked bar
#pragma pop_macro("new")
#include "Api/AgentLog.h"
#include "FamiTracker.h"
#include "FamiTrackerDoc.h"
#include "FamiTrackerView.h"
#include "MainFrm.h"
#include "McpConnection.h"
#include "AgentLogPanel.h"

namespace {

LPCTSTR SECTION = _T("Agent log panel");	// registry section: open, where, its size, the categories hidden

// Sizes at 96 DPI (scaled with SX and SY)
const int DEFAULT_HEIGHT = 200;				// along the bottom
const int DEFAULT_WIDTH = 360;				// along the right
const int MIN_HEIGHT = 110;
const int ROW_LEAST = 150;					// beside the others along the bottom: the toggles, a few lines, the note box
const int MIN_WIDTH = 240;
const int MARGIN = 6;
const int TOGGLE_HEIGHT = 18;				// a category's toggle along the top
const int TOGGLE_GAP = 4;
const int CLEAR_WIDTH = 54;
const int NOTE_LABEL = 16;					// "Note for the agent" over the note box
const int NOTE_HEIGHT = 38;					// the note box, two lines
const int SEND_WIDTH = 60;
const int TAG_HEIGHT = 15;					// an entry's colored tag
const int LINE = 16;						// for scrolling

const UINT_PTR TMR_LOOK = 1;				// a new entry, a note read, another song
const UINT LOOK_MS = 300;

// Colours, as the What next panel's
const COLORREF PANEL_BACK = RGB(232, 234, 238);
const COLORREF BODY_BACK = RGB(255, 255, 255);
const COLORREF TEXT_DARK = RGB(30, 32, 40);
const COLORREF TEXT_DIM = RGB(110, 114, 122);
const COLORREF RULE = RGB(232, 234, 238);
const COLORREF UNREAD_COLOUR = RGB(170, 80, 0);
const COLORREF TOGGLE_OFF_BORDER = RGB(180, 184, 192);

// Each category's tag: Okabe and Ito's colors (as the piano roll's chord roles), told apart by the name inside too.
// The change lines are grey and quieter; the user's own notes black
const COLORREF TAG_COLORS[ftapi::LOG_CATEGORIES] = {
	RGB(240, 228, 66),		// idea: yellow
	RGB(0, 114, 178),		// plan: blue
	RGB(86, 180, 233),		// research: sky blue
	RGB(0, 158, 115),		// fix: bluish green
	RGB(204, 121, 167),		// check: reddish purple
	RGB(230, 159, 0),		// note: orange
	RGB(213, 94, 0),		// error: vermilion
	RGB(196, 199, 205),		// change: grey
	RGB(40, 40, 40),		// you: black
};

// The toggles' words: the categories', the change lines' "Changes"
const TCHAR *ToggleLabel(int Category)
{
	static const TCHAR *const LABELS[ftapi::LOG_CATEGORIES] = { _T("Idea"), _T("Plan"), _T("Research"), _T("Fix"), _T("Check"),
		_T("Note"), _T("Error"), _T("Changes"), _T("You") };
	return Category >= 0 && Category < ftapi::LOG_CATEGORIES ? LABELS[Category] : _T("");
}

// Dark text on a light tag, white on a dark one
COLORREF TagText(COLORREF Back)
{
	const int Light = (GetRValue(Back) * 299 + GetGValue(Back) * 587 + GetBValue(Back) * 114) / 1000;
	return Light >= 150 ? RGB(0, 0, 0) : RGB(255, 255, 255);
}

COLORREF Mix(COLORREF A, COLORREF B, double Share)
{
	return RGB(static_cast<int>(GetRValue(A) + (GetRValue(B) - GetRValue(A)) * Share),
		static_cast<int>(GetGValue(A) + (GetGValue(B) - GetGValue(A)) * Share),
		static_cast<int>(GetBValue(A) + (GetBValue(B) - GetBValue(A)) * Share));
}

// UTF-8 (the log's) as the wide text it's drawn in, and the note box's text (the Windows code page) as UTF-8
std::wstring Wide(const std::string &Text)
{
	if (Text.empty())
		return std::wstring();
	const int Count = ::MultiByteToWideChar(CP_UTF8, 0, Text.c_str(), static_cast<int>(Text.size()), NULL, 0);
	if (Count <= 0)
		return std::wstring();
	std::wstring Out(static_cast<size_t>(Count), L'\0');
	::MultiByteToWideChar(CP_UTF8, 0, Text.c_str(), static_cast<int>(Text.size()), &Out[0], Count);
	return Out;
}

std::string ToUtf8(const CString &Text)
{
	if (Text.IsEmpty())
		return std::string();
	const int Count = ::MultiByteToWideChar(CP_ACP, 0, Text, Text.GetLength(), NULL, 0);
	if (Count <= 0)
		return std::string(static_cast<LPCSTR>(Text));
	std::vector<wchar_t> Buffer(static_cast<size_t>(Count));
	::MultiByteToWideChar(CP_ACP, 0, Text, Text.GetLength(), &Buffer[0], Count);
	const int Bytes = ::WideCharToMultiByte(CP_UTF8, 0, &Buffer[0], Count, NULL, 0, NULL, NULL);
	if (Bytes <= 0)
		return std::string(static_cast<LPCSTR>(Text));
	std::string Out(static_cast<size_t>(Bytes), '\0');
	::WideCharToMultiByte(CP_UTF8, 0, &Buffer[0], Count, &Out[0], Bytes, NULL, NULL);
	return Out;
}

int DrawWide(CDC &dc, const std::wstring &Text, CRect &Rect, UINT Format)
{
	return ::DrawTextW(dc.GetSafeHdc(), Text.c_str(), static_cast<int>(Text.size()), &Rect, Format);
}

class CAgentLogList;

// What the panel shows and does, shared with its list
struct LogView {
	CWnd *pPanel;
	CMcpConnection *pTools;
	CFont TextFont, SmallFont, TagFont, ControlFont;
	CAgentLogList *pList;
	CEdit NoteBox;
	CButton SendButton, ClearButton;
	bool Open;
	unsigned Hidden;					// a bit for each category hidden (1 << category)
	std::vector<CRect> Toggles;			// in the panel, by category
	int TopHeight;						// the toggles' rows
	// The entries shown, as read the last time the log changed, and where each is in the list
	std::vector<ftapi::LogEntry> Entries;
	struct row_t {
		size_t Entry;
		int Top, Height;
	};
	std::vector<row_t> Rows;
	long long Version;					// the log's, as last read
	// The log and its version as it last showed on screen: lines added since, while another tab shows, mark its tab
	const ftapi::AgentLog *pSeenLog;
	long long SeenVersion;
	int Width;							// the list's, as laid out
	int Scroll, Content;
	bool Stick;							// at the bottom: new entries keep it there
	std::map<long long, std::pair<int, int> > Heights;	// an entry's height by its id, at a width
	int TimeWidth, TagWidth, LineHeight;

	LogView() : pPanel(NULL), pTools(NULL), pList(NULL), Open(false), Hidden(0), TopHeight(0), Version(-1),
		pSeenLog(NULL), SeenVersion(-1), Width(0), Scroll(0),
		Content(0), Stick(true), TimeWidth(0), TagWidth(0), LineHeight(0) {}

	ftapi::AgentLog *Log() const
	{
		return pTools != NULL ? pTools->SongLog() : NULL;
	}

	void MakeFonts()
	{
		LOGFONT Log;
		memset(&Log, 0, sizeof(Log));
		::GetObject(::GetStockObject(DEFAULT_GUI_FONT), sizeof(Log), &Log);
		_tcscpy_s(Log.lfFaceName, LF_FACESIZE, _T("Segoe UI"));
		Log.lfHeight = -SY(13);
		TextFont.CreateFontIndirect(&Log);
		Log.lfHeight = -SY(11);
		SmallFont.CreateFontIndirect(&Log);
		Log.lfWeight = FW_BOLD;
		TagFont.CreateFontIndirect(&Log);
		Log.lfWeight = FW_NORMAL;
		Log.lfHeight = -SY(12);
		ControlFont.CreateFontIndirect(&Log);
	}

	bool Shows(const ftapi::LogEntry &Entry) const
	{
		return (Hidden & (1u << Entry.Category)) == 0;
	}

	// The list's text area: after the time and the tag
	int TextLeft() const
	{
		return SX(MARGIN) + TimeWidth + SX(MARGIN) + TagWidth + SX(MARGIN);
	}

	void Measure(CDC &dc)
	{
		CFont *pOld = dc.SelectObject(&SmallFont);
		TimeWidth = dc.GetTextExtent(_T("00:00")).cx;
		dc.SelectObject(&TagFont);
		TagWidth = 0;
		for (int c = 0; c < ftapi::LOG_CATEGORIES; ++c)
			TagWidth = std::max(TagWidth, static_cast<int>(dc.GetTextExtent(CString(ftapi::LogCategoryLabel(c))).cx));
		TagWidth += SX(10);
		dc.SelectObject(&TextFont);
		TEXTMETRIC Metrics;
		dc.GetTextMetrics(&Metrics);
		LineHeight = Metrics.tmHeight;
		dc.SelectObject(pOld);
	}

	// An entry's text as drawn: a change line smaller, a note of the user's with a line saying whether it was read
	int EntryHeight(CDC &dc, const ftapi::LogEntry &Entry, int ListWidth)
	{
		std::map<long long, std::pair<int, int> >::const_iterator it = Heights.find(Entry.Id);
		if (it != Heights.end() && it->second.first == ListWidth)
			return it->second.second;
		const int Room = std::max(SX(40), ListWidth - TextLeft() - SX(MARGIN));
		CRect Rect(0, 0, Room, 0);
		CFont *pOld = dc.SelectObject(Entry.Category == ftapi::LOG_CHANGE ? &SmallFont : &TextFont);
		const std::wstring Text = Wide(Entry.Text);
		DrawWide(dc, Text.empty() ? std::wstring(L" ") : Text, Rect, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
		int Height = std::max(static_cast<int>(Rect.Height()), SY(TAG_HEIGHT));
		if (Entry.Category == ftapi::LOG_USER) {
			dc.SelectObject(&SmallFont);
			Height += dc.GetTextExtent(_T("Read")).cy;
		}
		dc.SelectObject(pOld);
		Height += SY(Entry.Category == ftapi::LOG_CHANGE ? 4 : 8);
		Heights[Entry.Id] = std::make_pair(ListWidth, Height);
		return Height;
	}

	// The log read again when it changed (Force: the filter or the width did)
	void Refresh(bool Force);
	void Relayout();
	void ScrollTo(int To);
	void UpdateScrollBar();
	void PaintList(CDC &dc, const CRect &Client);
	void LayOutTop(int Width);
	void Arrange(int cx, int cy);
	void Toggle(int Category);
	void Send();
	void Clear();
	void Copy();
	void Menu(CPoint Screen);
};

// The list of entries: drawn by LogView, scrolled with its own bar
class CAgentLogList : public CWnd
{
public:
	CAgentLogList() : m_pView(NULL) {}

	bool Create(CWnd *pParent, LogView *pView)
	{
		m_pView = pView;
		const CString Class = AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(NULL, IDC_ARROW), NULL, NULL);
		return CWnd::CreateEx(0, Class, _T(""), WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP, CRect(0, 0, 0, 0), pParent,
			IDC_AGENTLOG_LIST) != FALSE;
	}

protected:
	afx_msg void OnPaint()
	{
		CPaintDC Paint(this);
		CRect Client;
		GetClientRect(&Client);
		// Drawn off screen, then shown at once
		CDC dc;
		dc.CreateCompatibleDC(&Paint);
		CBitmap Bitmap;
		Bitmap.CreateCompatibleBitmap(&Paint, std::max(1, Client.Width()), std::max(1, Client.Height()));
		CBitmap *pOld = dc.SelectObject(&Bitmap);
		m_pView->PaintList(dc, Client);
		Paint.BitBlt(0, 0, Client.Width(), Client.Height(), &dc, 0, 0, SRCCOPY);
		dc.SelectObject(pOld);
	}

	afx_msg BOOL OnEraseBkgnd(CDC *pDC)
	{
		return TRUE;
	}

	afx_msg void OnSize(UINT nType, int cx, int cy)
	{
		CWnd::OnSize(nType, cx, cy);
		m_pView->Refresh(true);
	}

	afx_msg void OnVScroll(UINT nSBCode, UINT nPos, CScrollBar *pScrollBar)
	{
		CRect Client;
		GetClientRect(&Client);
		int To = m_pView->Scroll;
		switch (nSBCode) {
		case SB_LINEUP: To -= SY(LINE); break;
		case SB_LINEDOWN: To += SY(LINE); break;
		case SB_PAGEUP: To -= Client.Height(); break;
		case SB_PAGEDOWN: To += Client.Height(); break;
		case SB_TOP: To = 0; break;
		case SB_BOTTOM: To = m_pView->Content; break;
		case SB_THUMBTRACK:
		case SB_THUMBPOSITION: {
			SCROLLINFO Info;
			memset(&Info, 0, sizeof(Info));
			Info.cbSize = sizeof(Info);
			Info.fMask = SIF_TRACKPOS;
			GetScrollInfo(SB_VERT, &Info);
			To = Info.nTrackPos;
			break;
		}
		default: return;
		}
		m_pView->ScrollTo(To);
	}

	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
	{
		m_pView->ScrollTo(m_pView->Scroll - zDelta * SY(LINE) * 3 / WHEEL_DELTA);
		return TRUE;
	}

	afx_msg void OnLButtonDown(UINT nFlags, CPoint point)
	{
		SetFocus();
	}

	afx_msg void OnRButtonUp(UINT nFlags, CPoint point)
	{
		CPoint Screen = point;
		ClientToScreen(&Screen);
		m_pView->Menu(Screen);
	}

	afx_msg UINT OnGetDlgCode()
	{
		return DLGC_WANTARROWS;
	}

	DECLARE_MESSAGE_MAP()

private:
	LogView *m_pView;
};

BEGIN_MESSAGE_MAP(CAgentLogList, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_SIZE()
	ON_WM_VSCROLL()
	ON_WM_MOUSEWHEEL()
	ON_WM_LBUTTONDOWN()
	ON_WM_RBUTTONUP()
	ON_WM_GETDLGCODE()
END_MESSAGE_MAP()

void LogView::Refresh(bool Force)
{
	const ftapi::AgentLog *pLog = Log();
	const long long Now = pLog != NULL ? pLog->Version() : -1;
	if (Now == Version && !Force)
		return;
	if (Now != Version) {
		Entries = pLog != NULL ? pLog->Entries() : std::vector<ftapi::LogEntry>();	// new entries keep the list at the bottom (Stick)
		Version = Now;
	}
	Relayout();
}

void LogView::Relayout()
{
	if (pList == NULL || pList->GetSafeHwnd() == NULL)
		return;
	CRect Client;
	pList->GetClientRect(&Client);
	Width = Client.Width();
	CClientDC dc(pList);
	if (LineHeight == 0)
		Measure(dc);
	Rows.clear();
	int Top = 0;
	for (size_t i = 0; i < Entries.size(); ++i) {
		if (!Shows(Entries[i]))
			continue;
		row_t Row;
		Row.Entry = i;
		Row.Top = Top;
		Row.Height = EntryHeight(dc, Entries[i], Width);
		Rows.push_back(Row);
		Top += Row.Height;
	}
	Content = Top;
	const int Most = std::max(0, Content - Client.Height());
	Scroll = Stick ? Most : std::min(Scroll, Most);
	UpdateScrollBar();
	pList->Invalidate(FALSE);
}

void LogView::UpdateScrollBar()
{
	CRect Client;
	pList->GetClientRect(&Client);
	SCROLLINFO Info;
	memset(&Info, 0, sizeof(Info));
	Info.cbSize = sizeof(Info);
	Info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
	Info.nMin = 0;
	Info.nMax = std::max(0, Content - 1);
	Info.nPage = static_cast<UINT>(std::max(1, static_cast<int>(Client.Height())));
	Info.nPos = Scroll;
	pList->SetScrollInfo(SB_VERT, &Info, TRUE);
}

void LogView::ScrollTo(int To)
{
	if (pList == NULL)
		return;
	CRect Client;
	pList->GetClientRect(&Client);
	const int Most = std::max(0, Content - Client.Height());
	Scroll = std::max(0, std::min(Most, To));
	Stick = Scroll >= Most;	// scrolled up, it stays where the user put it
	UpdateScrollBar();
	pList->Invalidate(FALSE);
}

void LogView::PaintList(CDC &dc, const CRect &Client)
{
	dc.FillSolidRect(&Client, BODY_BACK);
	if (LineHeight == 0)
		Measure(dc);
	dc.SetBkMode(TRANSPARENT);
	CFont *pOld = dc.SelectObject(&TextFont);
	if (Rows.empty()) {
		dc.SelectObject(&SmallFont);
		dc.SetTextColor(TEXT_DIM);
		CRect Rect(Client);
		Rect.DeflateRect(SX(MARGIN) * 2, SY(MARGIN) * 2);
		const TCHAR *const EMPTY = Entries.empty() ?
			_T("Nothing in this song's log yet. An agent connected to FamiTracker writes here what it does and why (write_log), ")
			_T("and every change it makes shows as a grey line. Write a note for it in the box below.") :
			_T("The categories shown above are all switched off. Click one to show its lines.");
		dc.DrawText(EMPTY, -1, &Rect, DT_WORDBREAK | DT_NOPREFIX);
		dc.SelectObject(pOld);
		return;
	}
	const int Left = SX(MARGIN), TagLeft = Left + TimeWidth + SX(MARGIN), Text = TextLeft();
	for (size_t r = 0; r < Rows.size(); ++r) {
		const row_t &Row = Rows[r];
		const int Top = Row.Top - Scroll;
		if (Top + Row.Height < 0)
			continue;
		if (Top > Client.Height())
			break;
		const ftapi::LogEntry &Entry = Entries[Row.Entry];
		const bool Change = Entry.Category == ftapi::LOG_CHANGE;
		const int Pad = SY(Change ? 2 : 4);
		// The time
		dc.SelectObject(&SmallFont);
		dc.SetTextColor(TEXT_DIM);
		CRect TimeRect(Left, Top + Pad, TagLeft, Top + Pad + SY(TAG_HEIGHT));
		dc.DrawText(CString(ftapi::LogClockText(Entry.Time).c_str()), -1, &TimeRect, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
		// The category's tag, its name in it
		const COLORREF Back = TAG_COLORS[Entry.Category >= 0 && Entry.Category < ftapi::LOG_CATEGORIES ? Entry.Category : 0];
		CRect Tag(TagLeft, Top + Pad, TagLeft + TagWidth, Top + Pad + SY(TAG_HEIGHT));
		if (Change)
			Tag.DeflateRect(SX(2), SY(1));
		dc.FillSolidRect(&Tag, Back);
		dc.SelectObject(Change ? &SmallFont : &TagFont);
		dc.SetTextColor(Change ? RGB(60, 62, 70) : TagText(Back));
		dc.DrawText(CString(ftapi::LogCategoryLabel(Entry.Category)), -1, &Tag, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
		// The text, wrapped
		dc.SelectObject(Change ? &SmallFont : &TextFont);
		dc.SetTextColor(Change ? TEXT_DIM : TEXT_DARK);
		CRect TextRect(Text, Top + Pad, std::max(Text + SX(40), static_cast<int>(Client.right) - SX(MARGIN)), Top + Row.Height);
		const std::wstring Words = Wide(Entry.Text);
		CRect Measured(TextRect);
		DrawWide(dc, Words, Measured, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
		DrawWide(dc, Words, TextRect, DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
		if (Entry.Category == ftapi::LOG_USER) {
			// Whether an agent has read it
			dc.SelectObject(&SmallFont);
			dc.SetTextColor(Entry.Read ? TEXT_DIM : UNREAD_COLOUR);
			CRect State(Text, Measured.bottom, TextRect.right, Top + Row.Height);
			dc.DrawText(Entry.Read ? _T("Read by an agent") : _T("Not read by an agent yet"), -1, &State, DT_SINGLELINE | DT_NOPREFIX);
		}
		dc.FillSolidRect(Left, Top + Row.Height - 1, Client.Width() - Left * 2, 1, RULE);
	}
	dc.SelectObject(pOld);
}

void LogView::LayOutTop(int Width)
{
	// The toggles flow into rows, Clear at the first row's right
	Toggles.assign(static_cast<size_t>(ftapi::LOG_CATEGORIES), CRect());
	CClientDC dc(pPanel);
	CFont *pOld = dc.SelectObject(&SmallFont);
	const int Height = SY(TOGGLE_HEIGHT), Gap = SX(TOGGLE_GAP);
	int X = SX(MARGIN), Y = SY(MARGIN) / 2 + SY(2), Right = Width - SX(MARGIN) - SX(CLEAR_WIDTH) - Gap;
	for (int c = 0; c < ftapi::LOG_CATEGORIES; ++c) {
		const int W = dc.GetTextExtent(ToggleLabel(c)).cx + SX(22);
		if (X + W > Right && X > SX(MARGIN)) {
			X = SX(MARGIN);
			Y += Height + Gap;
			Right = Width - SX(MARGIN);
		}
		Toggles[static_cast<size_t>(c)] = CRect(X, Y, X + W, Y + Height);
		X += W + Gap;
	}
	dc.SelectObject(pOld);
	TopHeight = Y + Height + SY(MARGIN) / 2 + SY(2);
}

void LogView::Arrange(int cx, int cy)
{
	LayOutTop(cx);
	const int Margin = SX(MARGIN);
	ClearButton.MoveWindow(cx - Margin - SX(CLEAR_WIDTH), SY(MARGIN) / 2 + SY(1), SX(CLEAR_WIDTH), SY(TOGGLE_HEIGHT) + SY(2));
	const int NoteTop = cy - SY(NOTE_HEIGHT) - Margin;
	const int ListBottom = NoteTop - SY(NOTE_LABEL);
	pList->MoveWindow(0, TopHeight, cx, std::max(0, ListBottom - TopHeight));
	NoteBox.MoveWindow(Margin, NoteTop, std::max(SX(40), cx - Margin * 3 - SX(SEND_WIDTH)), SY(NOTE_HEIGHT));
	SendButton.MoveWindow(cx - Margin - SX(SEND_WIDTH), NoteTop, SX(SEND_WIDTH), SY(NOTE_HEIGHT));
}

void LogView::Toggle(int Category)
{
	Hidden ^= 1u << Category;
	theApp.WriteProfileInt(SECTION, _T("Hidden"), static_cast<int>(Hidden));
	pPanel->Invalidate(FALSE);
	Refresh(true);
}

void LogView::Send()
{
	CString Typed;
	NoteBox.GetWindowText(Typed);
	Typed.Trim();
	if (Typed.IsEmpty())
		return;
	ftapi::AgentLog *pLog = Log();
	if (pLog == NULL) {
		::MessageBeep(MB_ICONEXCLAMATION);
		return;
	}
	pLog->Add(ftapi::LOG_USER, ToUtf8(Typed), ftapi::LogTimeNow());
	pTools->SaveAgentLog();
	NoteBox.SetWindowText(_T(""));
	Stick = true;
	Refresh(true);
}

void LogView::Clear()
{
	ftapi::AgentLog *pLog = Log();
	if (pLog == NULL || pLog->Entries().empty())
		return;
	if (AfxMessageBox(_T("Clear this song's agent log? Every line goes, the notes you wrote too, and its file next to the song ")
		_T("is emptied."), MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
		return;
	pLog = Log();	// the song may have changed while the question was open
	if (pLog == NULL)
		return;
	pLog->Clear();
	pTools->SaveAgentLog();
	Stick = true;
	Refresh(true);
}

void LogView::Copy()
{
	// The lines shown, as the file has them
	std::wstring Text;
	for (size_t r = 0; r < Rows.size(); ++r) {
		const ftapi::LogEntry &Entry = Entries[Rows[r].Entry];
		std::string Line = ftapi::LogTimeText(Entry.Time) + "  [" + ftapi::LogCategoryLabel(Entry.Category) + "]  ";
		for (size_t c = 0; c < Entry.Text.size(); ++c)
			Line += Entry.Text[c] == '\n' ? std::string("\r\n  ") : std::string(1, Entry.Text[c]);
		Text += Wide(Line) + L"\r\n";
	}
	if (Text.empty() || !pPanel->OpenClipboard())
		return;
	::EmptyClipboard();
	const size_t Bytes = (Text.size() + 1) * sizeof(wchar_t);
	HGLOBAL hMemory = ::GlobalAlloc(GMEM_MOVEABLE, Bytes);
	if (hMemory != NULL) {
		void *pMemory = ::GlobalLock(hMemory);
		if (pMemory != NULL) {
			memcpy(pMemory, Text.c_str(), Bytes);
			::GlobalUnlock(hMemory);
			if (::SetClipboardData(CF_UNICODETEXT, hMemory) == NULL)
				::GlobalFree(hMemory);
		}
		else
			::GlobalFree(hMemory);
	}
	::CloseClipboard();
}

void LogView::Menu(CPoint Screen)
{
	CPanelBar *pBar = CPanelBar::Of(pPanel);
	CMenu Popup;
	Popup.CreatePopupMenu();
	Popup.AppendMenu(MF_STRING | (Rows.empty() ? MF_GRAYED : 0), 1, _T("Copy the lines shown"));
	Popup.AppendMenu(MF_STRING | (Entries.empty() ? MF_GRAYED : 0), 2, _T("Clear the log..."));
	Popup.AppendMenu(MF_SEPARATOR, 0, (LPCTSTR)NULL);
	if (pBar != NULL)
		pBar->AddPlaceItems(Popup, 10);	// a tab at the bottom, on the right, its own window
	Popup.AppendMenu(MF_STRING, 4, _T("Close"));
	const UINT Picked = Popup.TrackPopupMenu(TPM_LEFTALIGN | TPM_RETURNCMD | TPM_NONOTIFY, Screen.x, Screen.y, pPanel);
	switch (Picked) {
	case 1: Copy(); break;
	case 2: Clear(); break;
	case 4:
		AfxGetMainWnd()->PostMessage(WM_COMMAND, ID_VIEW_AGENTLOG);
		break;
	default:
		if (pBar != NULL)
			pBar->PickPlace(Picked, 10);
		break;
	}
}

}	// namespace

// ---- The panel

struct CAgentLogPanelWnd::Impl : public LogView {
	CAgentLogList List;
};

BEGIN_MESSAGE_MAP(CAgentLogPanelWnd, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_SIZE()
	ON_WM_TIMER()
	ON_WM_LBUTTONDOWN()
	ON_WM_CONTEXTMENU()
	ON_WM_DESTROY()
	ON_BN_CLICKED(IDC_AGENTLOG_SEND, OnSend)
	ON_BN_CLICKED(IDC_AGENTLOG_CLEAR, OnClear)
END_MESSAGE_MAP()

CAgentLogPanelWnd::CAgentLogPanelWnd() : m_pImpl(new Impl())
{
}

CAgentLogPanelWnd::~CAgentLogPanelWnd()
{
	delete m_pImpl;
}

bool CAgentLogPanelWnd::Create(CWnd *pParent, CMcpConnection *pTools)
{
	Impl &I = *m_pImpl;
	I.pPanel = this;
	I.pTools = pTools;
	I.pList = &I.List;
	I.Hidden = static_cast<unsigned>(theApp.GetProfileInt(SECTION, _T("Hidden"), 0)) & ((1u << ftapi::LOG_CATEGORIES) - 1);
	const CString Class = AfxRegisterWndClass(0, ::LoadCursor(NULL, IDC_ARROW), NULL, NULL);
	if (!CWnd::Create(Class, _T("Agent log"), WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, CRect(0, 0, 0, 0), pParent,
		AFX_IDW_PANE_FIRST + 8))
		return false;
	I.MakeFonts();
	if (!I.List.Create(this, m_pImpl))
		return false;
	const DWORD Button = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON;
	I.NoteBox.CWnd::CreateEx(WS_EX_CLIENTEDGE, _T("EDIT"), _T(""), WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE |
		ES_AUTOVSCROLL | ES_WANTRETURN, CRect(0, 0, 0, 0), this, IDC_AGENTLOG_NOTE);
	I.SendButton.Create(_T("Send"), Button, CRect(0, 0, 0, 0), this, IDC_AGENTLOG_SEND);
	I.ClearButton.Create(_T("Clear"), Button, CRect(0, 0, 0, 0), this, IDC_AGENTLOG_CLEAR);
	CWnd *const CONTROLS[] = { &I.NoteBox, &I.SendButton, &I.ClearButton };
	for (size_t i = 0; i < sizeof(CONTROLS) / sizeof(CONTROLS[0]); ++i)
		CONTROLS[i]->SetFont(&I.ControlFont);
	I.NoteBox.SetLimitText(static_cast<UINT>(ftapi::MAX_LOG_TEXT));
	SetTimer(TMR_LOOK, LOOK_MS, NULL);
	return true;
}

void CAgentLogPanelWnd::Opened(bool Open)
{
	m_pImpl->Open = Open;
	if (Open) {
		m_pImpl->Stick = true;
		m_pImpl->Refresh(true);
	}
}

void CAgentLogPanelWnd::FocusNote()
{
	if (m_pImpl->NoteBox.GetSafeHwnd() != NULL)
		m_pImpl->NoteBox.SetFocus();
}

BOOL CAgentLogPanelWnd::PreTranslateMessage(MSG *pMsg)
{
	// The panel's keys go to its controls before FamiTracker's shortcuts see them (Space is one, as are many letters):
	// typing a note must type it
	Impl &I = *m_pImpl;
	const bool Key = pMsg->message == WM_KEYDOWN || pMsg->message == WM_SYSKEYDOWN || pMsg->message == WM_CHAR ||
		pMsg->message == WM_SYSCHAR || pMsg->message == WM_KEYUP;
	if (!Key || pMsg->hwnd == NULL || (pMsg->hwnd != GetSafeHwnd() && !::IsChild(GetSafeHwnd(), pMsg->hwnd)))
		return CWnd::PreTranslateMessage(pMsg);
	const bool Down = pMsg->message == WM_KEYDOWN;
	const WPARAM Code = pMsg->wParam;
	const bool Shift = ::GetKeyState(VK_SHIFT) < 0, Control = ::GetKeyState(VK_CONTROL) < 0;
	if (pMsg->message == WM_SYSKEYDOWN || pMsg->message == WM_SYSCHAR)
		return CWnd::PreTranslateMessage(pMsg);		// Alt: the menus
	if (Down && Code >= VK_F1 && Code <= VK_F12)
		return CWnd::PreTranslateMessage(pMsg);		// play, stop and the others
	if (Down && Code == VK_ESCAPE) {
		CFamiTrackerView *pView = CFamiTrackerView::GetView();
		if (pView != NULL)
			pView->SetFocus();
		return TRUE;
	}
	if (Down && Code == VK_TAB) {
		CWnd *const ORDER[] = { &I.NoteBox, &I.SendButton, &I.ClearButton, &I.List };
		const size_t Count = sizeof(ORDER) / sizeof(ORDER[0]);
		size_t At = 0;
		for (size_t i = 0; i < Count; ++i)
			if (ORDER[i]->GetSafeHwnd() == pMsg->hwnd)
				At = i;
		ORDER[(At + (Shift ? Count - 1 : 1)) % Count]->SetFocus();
		return TRUE;
	}
	if (pMsg->hwnd == I.NoteBox.GetSafeHwnd()) {
		if (Down && Code == VK_RETURN && !Shift && !Control) {
			I.Send();
			return TRUE;
		}
		if (pMsg->message == WM_CHAR && (Code == '\r' || Code == '\n') && !Shift)
			return TRUE;		// the Enter that sent
		if (Down && Control && Code == 'A') {
			I.NoteBox.SetSel(0, -1);
			return TRUE;
		}
		// Straight to the box, past the shortcuts
		::TranslateMessage(pMsg);
		::DispatchMessage(pMsg);
		return TRUE;
	}
	if (pMsg->hwnd == I.List.GetSafeHwnd() && Down) {
		CRect Client;
		I.List.GetClientRect(&Client);
		switch (Code) {
		case VK_UP: I.ScrollTo(I.Scroll - SY(LINE)); return TRUE;
		case VK_DOWN: I.ScrollTo(I.Scroll + SY(LINE)); return TRUE;
		case VK_PRIOR: I.ScrollTo(I.Scroll - Client.Height()); return TRUE;
		case VK_NEXT: I.ScrollTo(I.Scroll + Client.Height()); return TRUE;
		case VK_HOME: I.ScrollTo(0); return TRUE;
		case VK_END: I.ScrollTo(I.Content); return TRUE;
		default:
			if (Control && Code == 'C') {
				I.Copy();
				return TRUE;
			}
			break;
		}
	}
	return CWnd::PreTranslateMessage(pMsg);
}

void CAgentLogPanelWnd::OnPaint()
{
	CPaintDC dc(this);
	Impl &I = *m_pImpl;
	CRect Client;
	GetClientRect(&Client);
	dc.FillSolidRect(0, 0, Client.Width(), I.TopHeight, PANEL_BACK);
	const int NoteTop = Client.Height() - SY(NOTE_HEIGHT) - SX(MARGIN);
	const int LabelTop = NoteTop - SY(NOTE_LABEL);
	dc.FillSolidRect(0, LabelTop, Client.Width(), Client.Height() - LabelTop, PANEL_BACK);
	dc.SetBkMode(TRANSPARENT);
	CFont *pOld = dc.SelectObject(&I.SmallFont);
	// The toggles: on, filled with a light shade of the category's color beside its swatch; off, white, the name dim
	for (int c = 0; c < ftapi::LOG_CATEGORIES && static_cast<size_t>(c) < I.Toggles.size(); ++c) {
		const CRect &R = I.Toggles[static_cast<size_t>(c)];
		const bool On = (I.Hidden & (1u << c)) == 0;
		const COLORREF Color = TAG_COLORS[c];
		dc.FillSolidRect(&R, On ? Mix(Color, RGB(255, 255, 255), 0.75) : BODY_BACK);
		CBrush Border(On ? Mix(Color, RGB(0, 0, 0), 0.2) : TOGGLE_OFF_BORDER);
		dc.FrameRect(&R, &Border);
		const int Swatch = R.Height() - SY(8);
		CRect Box(R.left + SX(5), R.top + SY(4), R.left + SX(5) + Swatch, R.top + SY(4) + Swatch);
		if (On)
			dc.FillSolidRect(&Box, Color);
		else {
			CBrush Hollow(TOGGLE_OFF_BORDER);
			dc.FrameRect(&Box, &Hollow);
		}
		CRect Words(Box.right + SX(4), R.top, R.right - SX(3), R.bottom);
		dc.SetTextColor(On ? TEXT_DARK : TEXT_DIM);
		dc.DrawText(ToggleLabel(c), -1, &Words, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
	}
	// Over the note box
	dc.SetTextColor(TEXT_DIM);
	CRect Label(SX(MARGIN), LabelTop, Client.Width() - SX(MARGIN), NoteTop);
	dc.DrawText(_T("Note for the agent (Enter sends it, Shift+Enter starts a new line):"), -1, &Label,
		DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
	dc.SelectObject(pOld);
}

BOOL CAgentLogPanelWnd::OnEraseBkgnd(CDC *pDC)
{
	return TRUE;
}

void CAgentLogPanelWnd::OnSize(UINT nType, int cx, int cy)
{
	CWnd::OnSize(nType, cx, cy);
	if (m_pImpl->List.GetSafeHwnd() != NULL)
		m_pImpl->Arrange(cx, cy);
	Invalidate(FALSE);
}

void CAgentLogPanelWnd::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == TMR_LOOK && m_pImpl->Open) {
		m_pImpl->Refresh(false);
		// On screen (or another song's log, whose lines aren't new): seen. None while a tool runs
		const ftapi::AgentLog *pLog = m_pImpl->Log();
		if (pLog != NULL && (IsWindowVisible() || pLog != m_pImpl->pSeenLog)) {
			m_pImpl->pSeenLog = pLog;
			m_pImpl->SeenVersion = m_pImpl->Version;
		}
	}
	CWnd::OnTimer(nIDEvent);
}

bool CAgentLogPanelWnd::HasNews() const
{
	return m_pImpl->Open && m_pImpl->pSeenLog != NULL && m_pImpl->Version != -1 && m_pImpl->Version != m_pImpl->SeenVersion;
}

void CAgentLogPanelWnd::OnLButtonDown(UINT nFlags, CPoint point)
{
	for (size_t c = 0; c < m_pImpl->Toggles.size(); ++c)
		if (m_pImpl->Toggles[c].PtInRect(point)) {
			m_pImpl->Toggle(static_cast<int>(c));
			return;
		}
	CWnd::OnLButtonDown(nFlags, point);
}

void CAgentLogPanelWnd::OnContextMenu(CWnd *pWnd, CPoint point)
{
	if (point.x == -1 && point.y == -1) {
		point = CPoint(0, 0);
		ClientToScreen(&point);
	}
	m_pImpl->Menu(point);
}

void CAgentLogPanelWnd::OnSend()
{
	m_pImpl->Send();
	FocusNote();
}

void CAgentLogPanelWnd::OnClear()
{
	m_pImpl->Clear();
}

void CAgentLogPanelWnd::OnDestroy()
{
	KillTimer(TMR_LOOK);
	CWnd::OnDestroy();
}

// ---- The bar it is docked in, or its own window (PanelBar.h)

CAgentLogBar::CAgentLogBar() : CPanelBar(CMainFrame::PANEL_AGENT_LOG, SECTION, _T("Agent log"), DEFAULT_HEIGHT, DEFAULT_WIDTH,
	MIN_HEIGHT, MIN_WIDTH)
{
}

CAgentLogBar::~CAgentLogBar()
{
}

bool CAgentLogBar::Create(CFrameWnd *pFrame, CMcpConnection *pTools)
{
	if (!CreateBar(pFrame, AFX_IDW_CONTROLBAR_FIRST + 55) || !m_wndPanel.Create(this, pTools))
		return false;
	SetPanel(&m_wndPanel);
	return true;
}

void CAgentLogBar::PanelOpened(bool Open)
{
	m_wndPanel.Opened(Open);
}

void CAgentLogBar::Focus()
{
	m_wndPanel.FocusNote();
}

bool CAgentLogBar::WasOpen()
{
	return theApp.GetProfileInt(SECTION, _T("Open"), 0) != 0;
}

int CAgentLogBar::PanelLeast(int Width) const
{
	// The toggles (on more rows when narrow), a few lines and the note box
	return SY(ROW_LEAST) + (Width < SX(420) ? SY(TOGGLE_HEIGHT + TOGGLE_GAP) : 0);
}

int CAgentLogBar::News() const
{
	// Lines added while another tab showed
	return m_wndPanel.HasNews() ? PANEL_NEWS_MARK : PANEL_NEWS_NONE;
}

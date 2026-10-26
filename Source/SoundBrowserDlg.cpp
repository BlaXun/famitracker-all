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

// Instrument > Sound browser (see SoundBrowserDlg.h). The list comes from the
// tools' index (CMcpConnection::Sounds, ToolSet::Sounds in Api/Tools.cpp) and
// shows it in groups (ftapi::GroupSounds and SoundRows): with every kind, a
// kind's heading over its groups' headings (the ready-made sounds, each game of
// the reference library, the song's own), which fold. The rows are a list view
// with owner data whose headings are drawn by custom draw, so it needs no
// list-view groups (owner-data lists have none, and Wine's comctl32 draws them
// only in part) and stays quick with thousands of sounds. A sound is made into
// an instrument as add_instrument or reference_sound makes it
// (CMcpConnection::MakeSound), and heard with FamiTracker's own engine
// (CSoundPreview, SoundPreview.h): tried as an instrument of its own (one at a
// time), which comes out of the song again, sequences and all, unless it's
// kept. Removing an instrument makes the channels let go of an FDS
// instrument's sequences first (CFamiTrackerDoc::RemoveInstrument), so a
// channel never runs a removed trial's. The studied sounds of the games
// FamiTracker carries (Api/GameSounds.h) come with the reference library's,
// game by game. The ready-made sounds of the chips the song doesn't have
// (SoundEntry::AddsChip) are listed after the song's own, folded, and can't be
// heard until their chip is in: Add to the song asks first
// (SoundEntry::Question), then the chip comes in as Module > Module properties
// puts it in (which can't be undone there either, and clears the undo history)
// and the sound with it. Enter in Find picks the sound that matches best
// (ftapi::BestSound), wherever its group is in the list; the kinds list shows
// only the kinds with sounds found (ftapi::KindsListed); every sound is heard
// at its SoundEntry::Volume, about as loud as a 2A03 pulse at 15.

#include "stdafx.h"
#include <algorithm>
#include <map>
#include <string>
#include <vector>
#include "Api/SoundIndex.h"
#include "FamiTracker.h"
#include "FamiTrackerDoc.h"
#include "FamiTrackerView.h"
#include "MainFrm.h"
#include "SoundGen.h"
#include "TrackerChannel.h"
#include "McpConnection.h"
#include "InstrumentLibrary.h"
#include "SoundPreview.h"
#include "SoundBrowserDlg.h"

namespace {

const UINT_PTR TMR_FIND = 2;		// a moment after typing in Find
const UINT_PTR TMR_RETRY = 3;		// the list again, when Claude Code kept FamiTracker busy
const UINT_PTR TMR_SETTLE = 4;		// an arrow key held down: the sound it stops on plays
const UINT_PTR TMR_MARKS = 5;		// the play marks follow what plays
const UINT FIND_DELAY_MS = 250, RETRY_MS = 1000, SETTLE_MS = 200, MARKS_MS = 100;
const UINT WM_SB_MIDI_NOTE = WM_APP + 0x51;	// a MIDI key, from the view (CFamiTrackerView::SetNoteListener)
const UINT WM_SB_ROW_CLICK = WM_APP + 0x52;	// a row clicked (wParam the row; lParam 1 for a double-click's second)

enum { MARK_PLAY, MARK_PLAYING, MARK_NONE };	// the list's images

// The list's columns, in percent of its width
const struct { LPCTSTR Name; int Percent; } LIST_COLUMNS[] = {
	{ _T("Sound"), 25 }, { _T("Plays on"), 12 }, { _T("About"), 35 }, { _T("In song"), 11 }, { _T("Warning"), 17 },
};
const int COLUMN_COUNT = sizeof(LIST_COLUMNS) / sizeof(LIST_COLUMNS[0]);

// Kept while FamiTracker runs: where the browser was, the kind picked (in ftapi::SoundKinds(), -1 for all) and the headings
// folded or opened by hand
CRect g_Place(0, 0, 0, 0);
int g_Kind = -1;
std::map<std::string, bool> g_Folds;

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
	std::vector<char> Out(static_cast<size_t>(Bytes));
	::WideCharToMultiByte(CP_UTF8, 0, &Buffer[0], Wide, &Out[0], Bytes, NULL, NULL);
	return std::string(Out.begin(), Out.end());
}

// "2,343"
CString Thousands(int Count)
{
	CString Text;
	Text.Format(_T("%d"), Count);
	for (int At = Text.GetLength() - 3; At > 0; At -= 3)
		Text.Insert(At, _T(','));
	return Text;
}

// A sound for a chip the song doesn't have yet: why it doesn't play here, and how it comes in
CString ChipFirstText(const ftapi::SoundEntry &Sound)
{
	return FromUtf8("\"" + Sound.Name + "\" is for the " + (Sound.AddsChip == "VRC6 Plus" ? std::string("VRC6 Plus's soft layer") :
		Sound.AddsChip) + ", which the song doesn't have yet, so it can't play here. Add to the song asks first, then adds the chip "
		"and the sound, and it plays then.");
}

COLORREF Blend(COLORREF A, COLORREF B, int PercentA)
{
	return RGB((GetRValue(A) * PercentA + GetRValue(B) * (100 - PercentA)) / 100,
		(GetGValue(A) * PercentA + GetGValue(B) * (100 - PercentA)) / 100,
		(GetBValue(A) * PercentA + GetBValue(B) * (100 - PercentA)) / 100);
}

}	// namespace

// The sound list: a click on a row is the dialog's, every time (a sound plays, the one picked too; a heading
// folds), and no drag starts. It's painted again after a key moves it: Wine draws the focus rectangle by XOR, and a
// step that scrolls leaves pieces of it over the headings
class CSoundList : public CListCtrl
{
protected:
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
	afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
	DECLARE_MESSAGE_MAP()

private:
	void Clicked(CPoint Point, bool Again);
};

BEGIN_MESSAGE_MAP(CSoundList, CListCtrl)
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONDBLCLK()
	ON_WM_KEYDOWN()
END_MESSAGE_MAP()

void CSoundList::Clicked(CPoint Point, bool Again)
{
	SetFocus();
	LVHITTESTINFO Hit;
	memset(&Hit, 0, sizeof(Hit));
	Hit.pt = Point;
	int Row = SubItemHitTest(&Hit);
	if (Row < 0)
		Row = HitTest(Point);
	if (Row >= 0)
		GetParent()->SendMessage(WM_SB_ROW_CLICK, static_cast<WPARAM>(Row), Again ? 1 : 0);
}

void CSoundList::OnLButtonDown(UINT nFlags, CPoint point)
{
	Clicked(point, false);
}

void CSoundList::OnLButtonDblClk(UINT nFlags, CPoint point)
{
	Clicked(point, true);
}

void CSoundList::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	CListCtrl::OnKeyDown(nChar, nRepCnt, nFlags);
	Invalidate(FALSE);
}

class CSoundBrowserDlg : public CDialog
{
public:
	explicit CSoundBrowserDlg(CMainFrame *pMainFrame);

protected:
	virtual BOOL OnInitDialog();
	virtual BOOL PreTranslateMessage(MSG *pMsg);
	virtual void OnOK();
	virtual void OnCancel();
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnDestroy();
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnGetMinMaxInfo(MINMAXINFO *pInfo);
	afx_msg void OnFindChanged();
	afx_msg void OnFilterChanged();
	afx_msg void OnListen();
	afx_msg void OnAdd();
	afx_msg void OnGetDispInfo(NMHDR *pNMHDR, LRESULT *pResult);
	afx_msg void OnItemChanged(NMHDR *pNMHDR, LRESULT *pResult);
	afx_msg void OnCustomDraw(NMHDR *pNMHDR, LRESULT *pResult);
	afx_msg LRESULT OnRowClick(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnMidiNote(WPARAM wParam, LPARAM lParam);

	DECLARE_MESSAGE_MAP()

private:
	CFamiTrackerDoc *Doc() const;
	CSoundPreview *Preview() const;
	bool LoadSounds();
	void FillFilters();
	void Refilter();
	void FillKinds(const std::vector<int> &Counts, int Total);
	void ShowRows();
	int FirstSoundRow() const;
	void PickBest();
	void SelectRow(int Row);
	void ToggleFold(int Row);
	bool ListKey(UINT Key);
	const ftapi::SoundEntry *Selected() const;
	void ShowSelected();
	CString HeadingLabel(const ftapi::SoundRow &Row) const;
	void DrawHeading(CDC *pDC, int Row);
	void ListenTo(int Sound);
	int SoundInstrument(int Sound, CString &Error);
	void DropTrial();
	void AddWithChip();
	void PlayKey(int Note, bool Down);
	void UpdateMarks();
	void MakeMarks();
	void Layout();
	int TextHeight(int Id, int Width);
	void SetStatus(LPCTSTR Text);

	CMainFrame *m_pMainFrame;
	CEdit m_cFind;
	CComboBox m_cFrom, m_cChannel;
	CListBox m_cKinds;
	CSoundList m_cList;
	CImageList m_Marks;
	CFont m_BoldFont;
	std::vector<ftapi::SoundEntry> m_All;	// every sound
	std::vector<size_t> m_Found;			// what the words and the filters find, of every kind
	std::vector<ftapi::SoundGroup> m_Groups;	// the kind picked's (or every kind's) in groups
	std::vector<ftapi::SoundRow> m_Rows;	// the list's rows
	ftapi::SoundView m_View;
	std::vector<int> m_Targets;				// the channel filter's kinds of channel, after "All channels"
	int m_iKind;							// the kind picked on the left, in ftapi::SoundKinds(); -1 for every kind
	std::vector<int> m_KindRows;			// the kinds list's kinds, after "All sounds" (ftapi::KindsListed)
	bool m_bSome;							// no sound had every word: these have some
	int m_iSelected;						// the sound picked, in m_All; -1 for none
	std::string m_SelectedKey;				// or the heading picked, by its key
	int m_iTrial;							// the instrument a sound is tried in, -1 for none
	int m_iTrialSound;						// the sound it was made for
	int m_iKept;							// instruments added to the song
	int m_iPlaying;							// the sound heard now (its play mark), -1 for none
	int m_iMarked;							// the sound whose mark shows it playing
	bool m_bWasModified;
	bool m_bFilling;						// the list being filled: its changes aren't the user's
	bool m_bClicking;						// a click picks the row: the click plays it
	bool m_bKeyHeld;						// an arrow key repeating: the sound plays when it rests
	CSize m_MinSize;						// the window's, at its smallest
};

BEGIN_MESSAGE_MAP(CSoundBrowserDlg, CDialog)
	ON_WM_TIMER()
	ON_WM_DESTROY()
	ON_WM_SIZE()
	ON_WM_GETMINMAXINFO()
	ON_EN_CHANGE(IDC_SB_FIND, OnFindChanged)
	ON_LBN_SELCHANGE(IDC_SB_KIND, OnFilterChanged)
	ON_CBN_SELCHANGE(IDC_SB_FROM, OnFilterChanged)
	ON_CBN_SELCHANGE(IDC_SB_CHANNEL, OnFilterChanged)
	ON_BN_CLICKED(IDC_SB_LISTEN, OnListen)
	ON_BN_CLICKED(IDC_SB_ADD, OnAdd)
	ON_NOTIFY(LVN_GETDISPINFO, IDC_SB_LIST, OnGetDispInfo)
	ON_NOTIFY(LVN_ITEMCHANGED, IDC_SB_LIST, OnItemChanged)
	ON_NOTIFY(NM_CUSTOMDRAW, IDC_SB_LIST, OnCustomDraw)
	ON_MESSAGE(WM_SB_ROW_CLICK, OnRowClick)
	ON_MESSAGE(WM_SB_MIDI_NOTE, OnMidiNote)
END_MESSAGE_MAP()

CSoundBrowserDlg::CSoundBrowserDlg(CMainFrame *pMainFrame) :
	CDialog(IDD_SOUND_BROWSER, pMainFrame),
	m_pMainFrame(pMainFrame),
	m_iKind(-1),
	m_bSome(false),
	m_iSelected(-1),
	m_iTrial(-1),
	m_iTrialSound(-1),
	m_iKept(0),
	m_iPlaying(-1),
	m_iMarked(-1),
	m_bWasModified(false),
	m_bFilling(false),
	m_bClicking(false),
	m_bKeyHeld(false),
	m_MinSize(0, 0)
{
}

CFamiTrackerDoc *CSoundBrowserDlg::Doc() const
{
	return static_cast<CFamiTrackerDoc*>(m_pMainFrame->GetActiveDocument());
}

CSoundPreview *CSoundBrowserDlg::Preview() const
{
	return m_pMainFrame->GetSoundPreview();
}

BOOL CSoundBrowserDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	m_cFind.SubclassDlgItem(IDC_SB_FIND, this);
	m_cKinds.SubclassDlgItem(IDC_SB_KIND, this);
	m_cFrom.SubclassDlgItem(IDC_SB_FROM, this);
	m_cChannel.SubclassDlgItem(IDC_SB_CHANNEL, this);
	m_cList.SubclassDlgItem(IDC_SB_LIST, this);
	m_cList.SetExtendedStyle(m_cList.GetExtendedStyle() | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
	MakeMarks();
	m_cList.SetImageList(&m_Marks, LVSIL_SMALL);
	LOGFONT Font;
	m_cList.GetFont()->GetLogFont(&Font);
	Font.lfWeight = FW_BOLD;
	m_BoldFont.CreateFontIndirect(&Font);
	for (int c = 0; c < COLUMN_COUNT; ++c)
		m_cList.InsertColumn(c, LIST_COLUMNS[c].Name, LVCFMT_LEFT, 60);

	// Its smallest: room for the filters in two rows, a few sounds and the buttons
	CRect Smallest(0, 0, 300, 230), Window, Client;
	MapDialogRect(&Smallest);
	GetWindowRect(&Window);
	GetClientRect(&Client);
	m_MinSize = CSize(Smallest.Width() + Window.Width() - Client.Width(), Smallest.Height() + Window.Height() - Client.Height());
	// Where it was last time, when that's still on a screen
	if (!g_Place.IsRectEmpty() && ::MonitorFromRect(&g_Place, MONITOR_DEFAULTTONULL) != NULL)
		MoveWindow(&g_Place);
	Layout();

	m_View.Folds = g_Folds;
	m_bWasModified = Doc()->IsModified() != FALSE;
	CFamiTrackerView *pView = CFamiTrackerView::GetView();
	if (pView != NULL)
		pView->SetNoteListener(m_hWnd, WM_SB_MIDI_NOTE);
	Preview()->Stop();		// what the main window played
	if (LoadSounds()) {
		FillFilters();
		Refilter();
		SetStatus(_T("Click a sound (or its play mark) to hear it, and again to hear it again. The arrow keys play each sound ")
			_T("they reach, and the note keys play the one picked at their notes. Add to the song keeps the one you like."));
	}
	else
		SetTimer(TMR_RETRY, RETRY_MS, NULL);	// (the status says why)
	SetTimer(TMR_MARKS, MARKS_MS, NULL);
	m_cFind.SetFocus();
	return FALSE;	// the focus is set
}

// The play marks: a triangle in each sound's row, green while that sound plays
void CSoundBrowserDlg::MakeMarks()
{
	const int Size = 16;
	const COLORREF MASK = RGB(255, 0, 255);
	CClientDC Screen(this);
	CDC Dc;
	Dc.CreateCompatibleDC(&Screen);
	CBitmap Bitmap;
	Bitmap.CreateCompatibleBitmap(&Screen, Size * 3, Size);
	CBitmap *pOldBitmap = Dc.SelectObject(&Bitmap);
	Dc.FillSolidRect(0, 0, Size * 3, Size, MASK);
	const COLORREF COLORS[] = { RGB(80, 104, 150), RGB(24, 150, 56) };
	for (int i = 0; i < 2; ++i) {
		const int x = i * Size;
		const POINT Triangle[] = { { x + 5, 3 }, { x + 5, 12 }, { x + 12, 7 } };
		CBrush Brush(COLORS[i]);
		CPen Pen(PS_SOLID, 1, COLORS[i]);
		CBrush *pOldBrush = Dc.SelectObject(&Brush);
		CPen *pOldPen = Dc.SelectObject(&Pen);
		Dc.Polygon(Triangle, 3);
		Dc.SelectObject(pOldBrush);
		Dc.SelectObject(pOldPen);
	}
	Dc.SelectObject(pOldBitmap);
	m_Marks.Create(Size, Size, ILC_COLOR24 | ILC_MASK, 3, 0);
	m_Marks.Add(&Bitmap, MASK);
}

bool CSoundBrowserDlg::LoadSounds()
{
	CMcpConnection *pTools = m_pMainFrame->GetMcpConnection();
	CFamiTrackerView *pView = CFamiTrackerView::GetView();
	const int Cursor = pView != NULL ? static_cast<int>(pView->GetSelectedChannel()) : -1;
	std::string Error;
	CWaitCursor Wait;
	if (pTools == NULL || !pTools->Sounds(Cursor, m_All, Error)) {
		m_All.clear();
		SetStatus(Error.empty() ? CString(_T("The sounds couldn't be listed.")) : FromUtf8(Error));
		return false;
	}
	return true;
}

void CSoundBrowserDlg::FillFilters()
{
	m_cFrom.ResetContent();
	m_cFrom.AddString(_T("Everywhere"));
	m_cFrom.AddString(_T("Ready-made sounds"));		// in sound_source's order
	m_cFrom.AddString(_T("Games and references"));	// the games' FamiTracker carries and the reference library's
	m_cFrom.AddString(_T("This song"));
	m_cFrom.SetCurSel(0);

	// The kinds of channel the song has sounds for
	m_Targets.clear();
	for (int t = 0; t < ftapi::TARGET_COUNT; ++t)
		for (size_t i = 0; i < m_All.size(); ++i)
			if (m_All[i].Target == t) {
				m_Targets.push_back(t);
				break;
			}
	m_cChannel.ResetContent();
	m_cChannel.AddString(_T("All channels"));
	for (size_t t = 0; t < m_Targets.size(); ++t) {
		// A chip the song doesn't have yet: its sounds are there to discover
		bool Missing = true;
		for (size_t i = 0; i < m_All.size() && Missing; ++i)
			Missing = m_All[i].Target != m_Targets[t] || !m_All[i].AddsChip.empty();
		m_cChannel.AddString(FromUtf8(ftapi::TargetName(m_Targets[t])) + (Missing ? _T(" (not in the song yet)") : _T("")));
	}
	m_cChannel.SetCurSel(0);

	m_iKind = g_Kind >= 0 && static_cast<size_t>(g_Kind) < ftapi::SoundKinds().size() ? g_Kind : -1;
	std::vector<int> None(ftapi::SoundKinds().size(), 0);
	FillKinds(None, 0);
}

// The kinds on the left, each with how many sounds it has of those found: a kind with none isn't listed (but the one
// picked), and the kind picked stays picked
void CSoundBrowserDlg::FillKinds(const std::vector<int> &Counts, int Total)
{
	const std::vector<ftapi::SoundKind> &Kinds = ftapi::SoundKinds();
	const int Top = m_cKinds.GetTopIndex();
	const std::vector<int> Listed = ftapi::KindsListed(Counts, m_iKind);
	m_KindRows.clear();
	for (size_t i = 0; i < Listed.size(); ++i)
		if (Listed[i] >= 0 && static_cast<size_t>(Listed[i]) < Kinds.size())
			m_KindRows.push_back(Listed[i]);
	m_cKinds.SetRedraw(FALSE);
	m_cKinds.ResetContent();
	m_cKinds.AddString(_T("All sounds (") + Thousands(Total) + _T(")"));
	int Picked = 0;
	for (size_t r = 0; r < m_KindRows.size(); ++r) {
		const size_t k = static_cast<size_t>(m_KindRows[r]);
		m_cKinds.AddString(FromUtf8(Kinds[k].Label) + _T(" (") + Thousands(k < Counts.size() ? Counts[k] : 0) + _T(")"));
		if (m_KindRows[r] == m_iKind)
			Picked = static_cast<int>(r) + 1;
	}
	m_cKinds.SetCurSel(Picked);
	m_cKinds.SetTopIndex(Top >= 0 && Top < m_cKinds.GetCount() ? Top : 0);
	m_cKinds.SetRedraw(TRUE);
	m_cKinds.Invalidate();
}

void CSoundBrowserDlg::Refilter()
{
	ftapi::SoundQuery Query;
	CString Words;
	m_cFind.GetWindowText(Words);
	Query.Words = ToUtf8(Words);
	const int From = m_cFrom.GetCurSel();
	Query.Source = From > 0 ? From - 1 : -1;
	const int Channel = m_cChannel.GetCurSel();
	Query.Target = Channel > 0 && static_cast<size_t>(Channel) <= m_Targets.size() ? m_Targets[static_cast<size_t>(Channel - 1)] : -1;
	m_Found = ftapi::FindSounds(m_All, Query, &m_bSome);
	const std::vector<int> Counts = ftapi::KindCounts(m_All, m_Found);

	// The kind picked on the left, or every kind under headings of their own: words that find nothing of the kind
	// picked, but sounds of other kinds, show every kind
	const std::vector<ftapi::SoundKind> &Kinds = ftapi::SoundKinds();
	Words.Trim();
	if (!Words.IsEmpty() && m_iKind >= 0 && static_cast<size_t>(m_iKind) < Counts.size() && Counts[static_cast<size_t>(m_iKind)] == 0 &&
		!m_Found.empty()) {
		m_iKind = -1;
		g_Kind = -1;
	}
	FillKinds(Counts, static_cast<int>(m_Found.size()));
	const bool OneKind = m_iKind >= 0 && static_cast<size_t>(m_iKind) < Kinds.size();
	std::vector<size_t> Shown;
	if (OneKind) {
		const std::string &Id = Kinds[static_cast<size_t>(m_iKind)].Id;
		for (size_t f = 0; f < m_Found.size(); ++f)
			if (m_All[m_Found[f]].Kind == Id)
				Shown.push_back(m_Found[f]);
	}
	else
		Shown = m_Found;
	m_Groups = ftapi::GroupSounds(m_All, Shown);
	m_View.EveryKind = !OneKind;
	m_View.Words = !Words.IsEmpty();
	ShowRows();

	CString Count;
	if (m_bSome)
		Count.Format(_T("None has all the words; %s with some"), static_cast<LPCTSTR>(Thousands(static_cast<int>(m_Found.size()))));
	else if (!m_View.EveryKind)
		Count = Thousands(static_cast<int>(Shown.size())) + _T(" of ") + Thousands(static_cast<int>(m_Found.size())) + _T(" sounds");
	else
		Count = Thousands(static_cast<int>(m_Found.size())) + (m_Found.size() == 1 ? _T(" sound") : _T(" sounds"));
	SetDlgItemText(IDC_SB_COUNT, Count);
}

// The rows again, the sound or heading picked still picked while it shows
void CSoundBrowserDlg::ShowRows()
{
	m_Rows = ftapi::SoundRows(m_Groups, m_View);
	m_bFilling = true;
	m_cList.SetItemCountEx(static_cast<int>(m_Rows.size()), LVSICF_NOSCROLL);
	m_cList.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
	int Row = m_iSelected >= 0 ? ftapi::SoundRowOf(m_Rows, static_cast<size_t>(m_iSelected)) : -1;
	for (size_t r = 0; r < m_Rows.size() && Row < 0 && !m_SelectedKey.empty(); ++r)
		if (m_Rows[r].Type != ftapi::ROW_SOUND && ftapi::RowKey(m_Groups, m_Rows[r]) == m_SelectedKey)
			Row = static_cast<int>(r);
	if (Row >= 0) {
		m_cList.SetItemState(Row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		m_cList.SetSelectionMark(Row);
		m_cList.EnsureVisible(Row, FALSE);
	}
	else {
		m_iSelected = -1;
		m_SelectedKey.clear();
		if (!m_Rows.empty())
			m_cList.EnsureVisible(0, FALSE);
	}
	m_cList.Invalidate();
	m_bFilling = false;
	ShowSelected();
}

int CSoundBrowserDlg::FirstSoundRow() const
{
	for (size_t r = 0; r < m_Rows.size(); ++r)
		if (m_Rows[r].Type == ftapi::ROW_SOUND)
			return static_cast<int>(r);
	return -1;
}

// Enter in Find: the sound that matches the words best (ftapi::BestSound: one that plays here before another chip's alike),
// wherever its group is in the list, its headings opened if folded, picked, scrolled to and heard; the status says so
void CSoundBrowserDlg::PickBest()
{
	CString Words;
	m_cFind.GetWindowText(Words);
	Words.Trim();
	const int Best = ftapi::BestSound(m_All, m_Groups, ToUtf8(Words));
	if (Best < 0) {
		SelectRow(FirstSoundRow());
		return;
	}
	if (ftapi::OpenTo(m_Groups, m_View, static_cast<size_t>(Best))) {
		g_Folds = m_View.Folds;
		ShowRows();
	}
	m_bClicking = true;		// it plays below, also when it was picked already
	SelectRow(ftapi::SoundRowOf(m_Rows, static_cast<size_t>(Best)));
	m_bClicking = false;
	KillTimer(TMR_SETTLE);
	ListenTo(Best);
	if (!Words.IsEmpty() && m_iPlaying == Best) {	// (else the status says why it doesn't play)
		const ftapi::SoundEntry &E = m_All[static_cast<size_t>(Best)];
		SetStatus(_T("The best match for \"") + Words + _T("\" is picked: ") + FromUtf8(E.Name) + _T(" (") + FromUtf8(E.ChannelName) +
			_T(", ") + FromUtf8(E.Origin) + _T("). Up and Down go through the others."));
	}
}

// Picks a row as the user would (OnItemChanged sees it)
void CSoundBrowserDlg::SelectRow(int Row)
{
	if (Row < 0 || static_cast<size_t>(Row) >= m_Rows.size())
		return;
	if ((m_cList.GetItemState(Row, LVIS_SELECTED) & LVIS_SELECTED) == 0)
		m_cList.SetItemState(-1, 0, LVIS_SELECTED);
	m_cList.SetItemState(Row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	m_cList.SetSelectionMark(Row);
	m_cList.EnsureVisible(Row, FALSE);
}

// A heading folds its sounds away, or shows them again; it stays picked
void CSoundBrowserDlg::ToggleFold(int Row)
{
	if (Row < 0 || static_cast<size_t>(Row) >= m_Rows.size() || m_Rows[static_cast<size_t>(Row)].Type == ftapi::ROW_SOUND)
		return;
	const ftapi::SoundRow &Heading = m_Rows[static_cast<size_t>(Row)];
	const std::string Key = ftapi::RowKey(m_Groups, Heading);
	m_View.Folds[Key] = !Heading.Folded;
	g_Folds[Key] = !Heading.Folded;
	m_iSelected = -1;
	m_SelectedKey = Key;
	ShowRows();
}

// The keys the list takes from the dialog: Space plays the sound picked again (or folds a heading), Enter folds a
// heading (on a sound it adds it, as the dialog's button), Left and Right fold and open as in a tree
bool CSoundBrowserDlg::ListKey(UINT Key)
{
	const int Row = m_cList.GetNextItem(-1, LVNI_SELECTED);
	if (Row < 0 || static_cast<size_t>(Row) >= m_Rows.size())
		return false;
	const ftapi::SoundRow &R = m_Rows[static_cast<size_t>(Row)];
	const bool Heading = R.Type != ftapi::ROW_SOUND;
	switch (Key) {
	case VK_SPACE:
		if (Heading)
			ToggleFold(Row);
		else {
			KillTimer(TMR_SETTLE);
			ListenTo(static_cast<int>(R.Sound));
		}
		return true;
	case VK_RETURN:
		if (!Heading)
			return false;
		ToggleFold(Row);
		return true;
	case VK_RIGHT:
		if (Heading && R.Folded)
			ToggleFold(Row);
		else if (Heading && static_cast<size_t>(Row) + 1 < m_Rows.size())
			SelectRow(Row + 1);
		return true;
	case VK_LEFT:
		if (Heading && !R.Folded)
			ToggleFold(Row);
		else
			SelectRow(ftapi::RowHeading(m_Rows, Row));
		return true;
	}
	return false;
}

const ftapi::SoundEntry *CSoundBrowserDlg::Selected() const
{
	return m_iSelected >= 0 && static_cast<size_t>(m_iSelected) < m_All.size() ? &m_All[static_cast<size_t>(m_iSelected)] : NULL;
}

CString CSoundBrowserDlg::HeadingLabel(const ftapi::SoundRow &Row) const
{
	if (Row.Group >= m_Groups.size())
		return CString();
	const ftapi::SoundGroup &G = m_Groups[Row.Group];
	if (Row.Type == ftapi::ROW_KIND) {
		const std::vector<ftapi::SoundKind> &Kinds = ftapi::SoundKinds();
		for (size_t k = 0; k < Kinds.size(); ++k)
			if (Kinds[k].Id == G.Kind)
				return FromUtf8(Kinds[k].Label);
		return FromUtf8(G.Kind);
	}
	return FromUtf8(G.Label);
}

void CSoundBrowserDlg::ShowSelected()
{
	const ftapi::SoundEntry *pSound = Selected();
	CString Text;
	const int Row = m_cList.GetSafeHwnd() != NULL ? m_cList.GetNextItem(-1, LVNI_SELECTED) : -1;
	if (pSound != NULL) {
		std::string About = pSound->Name + (pSound->Words.empty() ? std::string() : ": " + pSound->Words) + ". From " + pSound->Origin +
			"; it plays on " + pSound->ChannelName + ".";
		if (!pSound->Warning.empty())
			About += " " + pSound->Warning;
		if (!pSound->AddsChip.empty())
			About += " It's for the " + pSound->AddsChip + ", which the song doesn't have yet: Add to the song asks first, then adds the "
				"chip and the sound.";
		if (pSound->Target == ftapi::TARGET_FDS)
			About += " The FDS sounds two octaves under the note written: play it two octaves above where a pulse would.";
		else if (pSound->Target == ftapi::TARGET_S5B)
			About += " The 5B sounds an octave under the note written.";
		Text = FromUtf8(About);
	}
	else if (Row >= 0 && static_cast<size_t>(Row) < m_Rows.size() && m_Rows[static_cast<size_t>(Row)].Type != ftapi::ROW_SOUND) {
		// A heading: what's under it, and how it opens
		const ftapi::SoundRow &R = m_Rows[static_cast<size_t>(Row)];
		const ftapi::SoundGroup &G = m_Groups[R.Group];
		const CString Count = Thousands(R.Count) + (R.Count == 1 ? _T(" sound") : _T(" sounds"));
		if (R.Type == ftapi::ROW_KIND)
			Text = HeadingLabel(R) + _T(": ") + Count + _T(".");
		else if (!G.Chip.empty())
			Text = HeadingLabel(R) + _T(": ") + Count + _T(" for a chip the song doesn't have yet. Add to the song asks first, then ")
				_T("adds the chip (its channels come after the song's own, and every note stays) and the sound.");
		else if (G.Source == ftapi::SOURCE_REFERENCE && G.Builtin)
			Text = HeadingLabel(R) + _T(": ") + Count + _T(" from the game's soundtrack, which FamiTracker comes with: each one the ")
				_T("way a part of a track sounded.");
		else if (G.Source == ftapi::SOURCE_PRESET)
			Text = HeadingLabel(R) + _T(": ") + Count + _T(" made for the song's chips, good ones to start from.");
		else if (G.Source == ftapi::SOURCE_SONG)
			Text = HeadingLabel(R) + _T(": ") + Count + _T(" the song has already.");
		else
			Text = HeadingLabel(R) + _T(": ") + Count + _T(" from your reference library, each one the way a part of a track sounded.");
		Text += R.Folded ? _T(" Click the heading (or press Right) to show them.") : _T(" Click the heading (or press Left) to fold them away.");
	}
	else if (m_View.Words && m_Found.empty())
		Text = _T("No sound has those words. Try fewer or other words: a kind of sound (bell, pluck, pad, bass), how it sounds ")
			_T("(warm, soft, bright) or a game.");
	else
		Text = m_All.empty() ? _T("") : _T("Pick a kind on the left and click a sound to hear it, or type what you're looking for in Find ")
			_T("(bell, warm, pluck, bass, a game).");
	SetDlgItemText(IDC_SB_TEXT, Text);
	GetDlgItem(IDC_SB_LISTEN)->EnableWindow(pSound != NULL ? TRUE : FALSE);
	GetDlgItem(IDC_SB_ADD)->EnableWindow(pSound != NULL ? TRUE : FALSE);
}

// The instrument a sound plays with: the song's own, one of its name already there, or one it's tried in
int CSoundBrowserDlg::SoundInstrument(int Sound, CString &Error)
{
	ftapi::SoundEntry &E = m_All[static_cast<size_t>(Sound)];
	CFamiTrackerDoc *pDoc = Doc();
	if (E.Make == ftapi::MAKE_NONE)
		return pDoc->IsInstrumentUsed(static_cast<unsigned int>(E.Number)) ? E.Number : -1;
	if (E.InSong >= 0 && E.InSong < MAX_INSTRUMENTS && pDoc->IsInstrumentUsed(static_cast<unsigned int>(E.InSong)))
		return E.InSong;
	if (m_iTrial >= 0 && m_iTrialSound == Sound)
		return m_iTrial;
	DropTrial();
	bool Used[MAX_INSTRUMENTS];
	for (int i = 0; i < MAX_INSTRUMENTS; ++i)
		Used[i] = pDoc->IsInstrumentUsed(static_cast<unsigned int>(i));
	CMcpConnection *pTools = m_pMainFrame->GetMcpConnection();
	std::string Why;
	const int Index = pTools != NULL ? pTools->MakeSound(E, Why) : -1;
	if (Index < 0 || Index >= MAX_INSTRUMENTS) {
		Error = Why.empty() ? CString(_T("The sound couldn't be tried.")) : FromUtf8(Why);
		return -1;
	}
	if (Used[Index])
		E.InSong = Index;	// the song had one of its name: it plays, and stays
	else {
		m_iTrial = Index;
		m_iTrialSound = Sound;
	}
	return Index;
}

// The sound tried last goes out of the song again
void CSoundBrowserDlg::DropTrial()
{
	if (m_iTrial < 0)
		return;
	Preview()->Stop();
	RemoveInstrumentAndSequences(Doc(), static_cast<unsigned int>(m_iTrial));
	m_iTrial = -1;
	m_iTrialSound = -1;
}

// A sound's phrase, from the start: what played before stops
void CSoundBrowserDlg::ListenTo(int Sound)
{
	if (Sound < 0 || static_cast<size_t>(Sound) >= m_All.size())
		return;
	if (!m_All[static_cast<size_t>(Sound)].AddsChip.empty()) {
		SetStatus(ChipFirstText(m_All[static_cast<size_t>(Sound)]));
		return;
	}
	if (m_All[static_cast<size_t>(Sound)].Channel < 0)
		return;
	CString Error;
	const int Instrument = SoundInstrument(Sound, Error);
	if (Instrument < 0) {
		SetStatus(Error);
		return;
	}
	const ftapi::SoundEntry &E = m_All[static_cast<size_t>(Sound)];
	if (Preview()->Play(E.Channel, Instrument, ftapi::PreviewSteps(E), E.Volume))
		m_iPlaying = Sound;
	UpdateMarks();
}

void CSoundBrowserDlg::OnListen()
{
	KillTimer(TMR_SETTLE);
	ListenTo(m_iSelected);
}

// A note key (or a MIDI key) pressed or let go while the list is picked: the sound picked plays it
void CSoundBrowserDlg::PlayKey(int Note, bool Down)
{
	if (!Down) {
		Preview()->KeyUp(Note);
		return;
	}
	const ftapi::SoundEntry *pSound = Selected();
	if (pSound != NULL && !pSound->AddsChip.empty())
		SetStatus(ChipFirstText(*pSound));
	if (pSound == NULL || pSound->Channel < 0)
		return;
	CString Error;
	const int Instrument = SoundInstrument(m_iSelected, Error);
	if (Instrument < 0) {
		SetStatus(Error);
		return;
	}
	if (Preview()->KeyDown(pSound->Channel, Instrument, Note, pSound->Volume))
		m_iPlaying = m_iSelected;
	UpdateMarks();
}

// The play marks: the sound heard shows a green one while it plays
void CSoundBrowserDlg::UpdateMarks()
{
	if (m_iPlaying >= 0 && Preview()->PlayingInstrument() < 0)
		m_iPlaying = -1;
	if (m_iPlaying == m_iMarked)
		return;
	for (const int Sound : { m_iMarked, m_iPlaying }) {
		const int Row = Sound >= 0 ? ftapi::SoundRowOf(m_Rows, static_cast<size_t>(Sound)) : -1;
		if (Row >= 0)
			m_cList.RedrawItems(Row, Row);
	}
	m_iMarked = m_iPlaying;
}

BOOL CSoundBrowserDlg::PreTranslateMessage(MSG *pMsg)
{
	const bool Plain = ::GetKeyState(VK_CONTROL) >= 0 && ::GetKeyState(VK_MENU) >= 0;
	if (pMsg->message == WM_KEYDOWN && pMsg->hwnd == m_cFind.GetSafeHwnd() && (pMsg->wParam == VK_RETURN || pMsg->wParam == VK_DOWN)) {
		// Enter in Find: the best match is picked and heard (PickBest); Down goes to the list
		KillTimer(TMR_FIND);
		Refilter();
		m_cList.SetFocus();
		const int Row = m_cList.GetNextItem(-1, LVNI_SELECTED);
		if (pMsg->wParam == VK_RETURN)
			PickBest();
		else if (Row < 0)
			SelectRow(FirstSoundRow());
		return TRUE;
	}
	if ((pMsg->message == WM_KEYDOWN || pMsg->message == WM_KEYUP) && pMsg->hwnd == m_cList.GetSafeHwnd() && Plain) {
		switch (pMsg->wParam) {
		case VK_UP: case VK_DOWN: case VK_PRIOR: case VK_NEXT: case VK_HOME: case VK_END:
			// The list moves; a key held down plays the sound it stops on
			m_bKeyHeld = pMsg->message == WM_KEYDOWN && (pMsg->lParam & (1 << 30)) != 0;
			return CDialog::PreTranslateMessage(pMsg);
		}
		if (pMsg->message == WM_KEYDOWN && ListKey(static_cast<UINT>(pMsg->wParam)))
			return TRUE;
		// The note keys play the sound picked, as they do in the pattern editor (the list's own search is Find's)
		CFamiTrackerView *pView = CFamiTrackerView::GetView();
		const int Note = pView != NULL ? pView->NoteOfKey(static_cast<unsigned char>(pMsg->wParam)) : 0;
		if (Note > 0) {
			if (pMsg->message == WM_KEYUP)
				PlayKey(Note, false);
			else if ((pMsg->lParam & (1 << 30)) == 0)		// not the key repeating
				PlayKey(Note, true);
			return TRUE;
		}
	}
	return CDialog::PreTranslateMessage(pMsg);
}

LRESULT CSoundBrowserDlg::OnMidiNote(WPARAM wParam, LPARAM lParam)
{
	PlayKey(static_cast<int>(wParam), lParam != 0);
	return 0;
}

// A row clicked: a sound plays (from the start, the one picked too), a heading folds or opens
LRESULT CSoundBrowserDlg::OnRowClick(WPARAM wParam, LPARAM lParam)
{
	const int Row = static_cast<int>(wParam);
	if (Row < 0 || static_cast<size_t>(Row) >= m_Rows.size())
		return 0;
	const ftapi::SoundRow &R = m_Rows[static_cast<size_t>(Row)];
	if (R.Type != ftapi::ROW_SOUND) {
		if (lParam == 0)	// (a double-click's second click leaves it as the first left it)
			ToggleFold(Row);
		return 0;
	}
	const int Sound = static_cast<int>(R.Sound);
	m_bClicking = true;
	SelectRow(Row);
	m_bClicking = false;
	KillTimer(TMR_SETTLE);
	ListenTo(Sound);
	return 0;
}

void CSoundBrowserDlg::OnTimer(UINT_PTR nIDEvent)
{
	switch (nIDEvent) {
	case TMR_FIND:
		KillTimer(TMR_FIND);
		Refilter();
		return;
	case TMR_RETRY:
		if (LoadSounds()) {
			KillTimer(TMR_RETRY);
			FillFilters();
			Refilter();
			SetStatus(_T("Click a sound to hear it, and again to hear it again; the arrow keys and the note keys play too."));
		}
		return;
	case TMR_SETTLE:
		KillTimer(TMR_SETTLE);
		if (!m_bKeyHeld)
			ListenTo(m_iSelected);
		else
			SetTimer(TMR_SETTLE, SETTLE_MS, NULL);	// still held: when it's let go
		return;
	case TMR_MARKS:
		UpdateMarks();
		return;
	}
	CDialog::OnTimer(nIDEvent);
}

void CSoundBrowserDlg::OnFindChanged()
{
	SetTimer(TMR_FIND, FIND_DELAY_MS, NULL);
}

void CSoundBrowserDlg::OnFilterChanged()
{
	const int Row = m_cKinds.GetCurSel();
	m_iKind = Row > 0 && static_cast<size_t>(Row) <= m_KindRows.size() ? m_KindRows[static_cast<size_t>(Row - 1)] : -1;
	g_Kind = m_iKind;
	Refilter();
}

void CSoundBrowserDlg::OnGetDispInfo(NMHDR *pNMHDR, LRESULT *pResult)
{
	*pResult = 0;
	LVITEM &Item = reinterpret_cast<NMLVDISPINFO*>(pNMHDR)->item;
	if (Item.iItem < 0 || static_cast<size_t>(Item.iItem) >= m_Rows.size())
		return;
	const ftapi::SoundRow &R = m_Rows[static_cast<size_t>(Item.iItem)];
	if ((Item.mask & LVIF_IMAGE) != 0)
		Item.iImage = R.Type != ftapi::ROW_SOUND ? MARK_NONE : static_cast<int>(R.Sound) == m_iPlaying ? MARK_PLAYING : MARK_PLAY;
	if ((Item.mask & LVIF_INDENT) != 0)
		Item.iIndent = R.Type == ftapi::ROW_SOUND && m_View.EveryKind ? 1 : 0;
	if ((Item.mask & LVIF_TEXT) == 0 || Item.pszText == NULL || Item.cchTextMax <= 0)
		return;
	Item.pszText[0] = 0;
	CString Text;
	if (R.Type != ftapi::ROW_SOUND) {
		if (Item.iSubItem == 0)		// (drawn by OnCustomDraw; for screen readers)
			Text = HeadingLabel(R) + _T(" (") + Thousands(R.Count) + _T(")");
	}
	else {
		const ftapi::SoundEntry &E = m_All[R.Sound];
		switch (Item.iSubItem) {
		case 0:
			Text = FromUtf8(E.Name);
			break;
		case 1:
			Text = FromUtf8(E.ChannelName);
			break;
		case 2:
			Text = FromUtf8(E.About);
			break;
		case 3:
			if (E.InSong >= 0 && E.InSong != m_iTrial)
				Text.Format(_T("yes (%02X)"), E.InSong);
			break;
		case 4:
			if (ftapi::SoundVanishes(E.Heard))
				Text = _T("short notes");
			if (!E.ClickPitches.empty())
				Text += Text.IsEmpty() ? _T("vibrato clicks") : _T(", vibrato clicks");
			break;
		}
	}
	_tcsncpy_s(Item.pszText, static_cast<size_t>(Item.cchTextMax), Text, _TRUNCATE);
}

void CSoundBrowserDlg::OnItemChanged(NMHDR *pNMHDR, LRESULT *pResult)
{
	const NMLISTVIEW *pList = reinterpret_cast<NMLISTVIEW*>(pNMHDR);
	*pResult = 0;
	if (m_bFilling || (pList->uChanged & LVIF_STATE) == 0 || pList->iItem < 0 || static_cast<size_t>(pList->iItem) >= m_Rows.size())
		return;
	const bool Now = (pList->uNewState & LVIS_SELECTED) != 0, Before = (pList->uOldState & LVIS_SELECTED) != 0;
	if (Now == Before || !Now)
		return;
	const ftapi::SoundRow &R = m_Rows[static_cast<size_t>(pList->iItem)];
	if (R.Type != ftapi::ROW_SOUND) {
		// A heading picked: nothing to hear
		m_iSelected = -1;
		m_SelectedKey = ftapi::RowKey(m_Groups, R);
		ShowSelected();
		return;
	}
	const int Sound = static_cast<int>(R.Sound);
	const bool Other = Sound != m_iSelected;
	m_iSelected = Sound;
	m_SelectedKey.clear();
	ShowSelected();
	if (m_bClicking || !Other)
		return;		// the click plays it
	if (m_bKeyHeld)
		SetTimer(TMR_SETTLE, SETTLE_MS, NULL);	// the arrow key held down: when it rests
	else
		ListenTo(Sound);		// a step with the arrow keys plays it
}

// A heading is drawn as a row first, then over: the list keeps its focus where the row is (Wine draws the focus
// rectangle where it last drew the focused row itself)
void CSoundBrowserDlg::OnCustomDraw(NMHDR *pNMHDR, LRESULT *pResult)
{
	NMLVCUSTOMDRAW *pDraw = reinterpret_cast<NMLVCUSTOMDRAW*>(pNMHDR);
	*pResult = CDRF_DODEFAULT;
	const int Row = static_cast<int>(pDraw->nmcd.dwItemSpec);
	const bool Heading = Row >= 0 && static_cast<size_t>(Row) < m_Rows.size() && m_Rows[static_cast<size_t>(Row)].Type != ftapi::ROW_SOUND;
	if (pDraw->nmcd.dwDrawStage == CDDS_PREPAINT)
		*pResult = CDRF_NOTIFYITEMDRAW;
	else if (pDraw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT && Heading)
		*pResult = CDRF_NOTIFYPOSTPAINT;
	else if (pDraw->nmcd.dwDrawStage == CDDS_ITEMPOSTPAINT && Heading)
		DrawHeading(CDC::FromHandle(pDraw->nmcd.hdc), Row);
}

// A heading: a band across the list with a fold mark, its name and how many sounds it has
void CSoundBrowserDlg::DrawHeading(CDC *pDC, int Row)
{
	const ftapi::SoundRow &R = m_Rows[static_cast<size_t>(Row)];
	CRect Rect, Client;
	m_cList.GetItemRect(Row, &Rect, LVIR_BOUNDS);
	m_cList.GetClientRect(&Client);
	Rect.left = Client.left;
	Rect.right = std::max(Rect.right, Client.right);
	const bool Picked = (m_cList.GetItemState(Row, LVIS_SELECTED) & LVIS_SELECTED) != 0;
	const bool Focused = ::GetFocus() == m_cList.GetSafeHwnd();
	COLORREF Back, Fore;
	if (Picked && Focused) {
		Back = ::GetSysColor(COLOR_HIGHLIGHT);
		Fore = ::GetSysColor(COLOR_HIGHLIGHTTEXT);
	}
	else if (R.Type == ftapi::ROW_KIND) {
		Back = Blend(::GetSysColor(COLOR_HIGHLIGHT), ::GetSysColor(COLOR_WINDOW), Picked ? 45 : 22);
		Fore = ::GetSysColor(COLOR_WINDOWTEXT);
	}
	else {
		Back = Picked ? Blend(::GetSysColor(COLOR_BTNSHADOW), ::GetSysColor(COLOR_BTNFACE), 40) : ::GetSysColor(COLOR_BTNFACE);
		Fore = ::GetSysColor(COLOR_BTNTEXT);
	}
	pDC->FillSolidRect(&Rect, Back);
	// The fold mark: pointing right while folded, down while open
	const int Unit = std::max(3, Rect.Height() / 5), Mid = Rect.CenterPoint().y;
	const int Left = Rect.left + Unit + (R.Type == ftapi::ROW_GROUP && m_View.EveryKind ? Rect.Height() : 0);
	POINT Mark[3];
	if (R.Folded) {
		Mark[0].x = Left + Unit / 2;		Mark[0].y = Mid - Unit;
		Mark[1].x = Left + Unit / 2;		Mark[1].y = Mid + Unit;
		Mark[2].x = Left + Unit / 2 + Unit;	Mark[2].y = Mid;
	}
	else {
		Mark[0].x = Left;					Mark[0].y = Mid - Unit / 2;
		Mark[1].x = Left + 2 * Unit;		Mark[1].y = Mid - Unit / 2;
		Mark[2].x = Left + Unit;			Mark[2].y = Mid + Unit / 2 + 1;
	}
	CBrush Brush(Fore);
	CPen Pen(PS_SOLID, 1, Fore);
	CBrush *pOldBrush = pDC->SelectObject(&Brush);
	CPen *pOldPen = pDC->SelectObject(&Pen);
	pDC->Polygon(Mark, 3);
	pDC->SelectObject(pOldBrush);
	pDC->SelectObject(pOldPen);
	// Its name in bold, then the count
	const CString Label = HeadingLabel(R), Count = _T("  (") + Thousands(R.Count) + _T(")");
	CRect Text(Left + 3 * Unit, Rect.top, Rect.right - Unit, Rect.bottom);
	pDC->SetBkMode(TRANSPARENT);
	pDC->SetTextColor(Fore);
	CFont *pOldFont = pDC->SelectObject(&m_BoldFont);
	pDC->DrawText(Label, &Text, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
	Text.left += pDC->GetTextExtent(Label).cx;
	pDC->SelectObject(m_cList.GetFont());
	if (Text.left < Text.right)
		pDC->DrawText(Count, &Text, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
	pDC->SelectObject(pOldFont);
}

// Enter, outside Find (and on a sound in the list)
void CSoundBrowserDlg::OnOK()
{
	OnAdd();
}

// Close, Escape or the window's X: the sound stops at once, before the main window is woken again (which can take
// a while under Wine)
void CSoundBrowserDlg::OnCancel()
{
	Preview()->Stop();
	CDialog::OnCancel();
}

void CSoundBrowserDlg::OnAdd()
{
	const ftapi::SoundEntry *pSound = Selected();
	if (pSound == NULL)
		return;
	if (!pSound->AddsChip.empty()) {
		AddWithChip();
		return;
	}
	CString Error;
	const bool Had = pSound->Make == ftapi::MAKE_NONE || (pSound->InSong >= 0 && pSound->InSong != m_iTrial &&
		Doc()->IsInstrumentUsed(static_cast<unsigned int>(pSound->InSong)));
	const int Instrument = SoundInstrument(m_iSelected, Error);
	if (Instrument < 0) {
		SetStatus(Error);
		return;
	}
	const ftapi::SoundEntry E = m_All[static_cast<size_t>(m_iSelected)];
	const CString Name = FromUtf8(E.Name);
	CString Text;
	if (Had)
		Text.Format(_T("\"%s\" is in the song already, as instrument %02X: it's picked in the instrument list now."),
			static_cast<LPCTSTR>(Name), Instrument);
	else {
		if (Instrument == m_iTrial) {
			m_iTrial = -1;		// kept
			m_iTrialSound = -1;
		}
		++m_iKept;
		Text.Format(_T("Added \"%s\" as instrument %02X and picked it: type notes with it, or draw them in the piano roll."),
			static_cast<LPCTSTR>(Name), Instrument);
		// The sounds of its name for its chip are in the song now, and it's one of the song's own
		for (size_t i = 0; i < m_All.size(); ++i)
			if (m_All[i].Source != ftapi::SOURCE_SONG && m_All[i].Chip == E.Chip && m_All[i].Name == E.Name)
				m_All[i].InSong = Instrument;
		ftapi::SoundEntry Own = E;
		Own.Source = ftapi::SOURCE_SONG;
		Own.Make = ftapi::MAKE_NONE;
		Own.Number = Instrument;
		Own.InSong = Instrument;
		Own.Home = true;
		Own.Game.clear();
		CString Origin;
		Origin.Format(_T("this song's instrument %02X"), Instrument);
		Own.Origin = ToUtf8(Origin);
		Own.Words = (E.Builtin ? "added from " + E.Game + "'s sounds" : "added from the " + ftapi::SourceName(E.Source)) +
			(E.Words.empty() ? std::string() : ": " + E.Words);
		Own.About = Own.Words;
		Own.Reference.reset();
		ftapi::IndexWords(Own);
		m_All.push_back(Own);
		Refilter();		// under the song's own now too
	}
	m_pMainFrame->SelectInstrument(Instrument);
	m_cList.Invalidate();
	SetStatus(Text);
}

// A sound for a chip the song doesn't have: asked first (what the chip brings, that every note stays, what NSF export
// can't play), then the chip comes in as Module > Module properties puts it in (after the song's own channels; it
// can't be undone, as there) and the sound with it. The song's channels are others then, so every sound comes again.
void CSoundBrowserDlg::AddWithChip()
{
	const ftapi::SoundEntry E = m_All[static_cast<size_t>(m_iSelected)];
	const std::string Asked = E.Question.empty() ? "Add the " + E.AddsChip + " to the song?" : E.Question;
	if (AfxMessageBox(FromUtf8(Asked), MB_YESNO | MB_ICONQUESTION) != IDYES)
		return;
	Preview()->Stop();
	DropTrial();
	CFamiTrackerDoc *pDoc = Doc();
	const unsigned char ChipsBefore = pDoc->GetExpansionChip();
	const bool PlusBefore = pDoc->IsVrc6Plus();
	CMcpConnection *pTools = m_pMainFrame->GetMcpConnection();
	std::string Why;
	int Instrument = -1;
	{
		CWaitCursor Wait;
		Instrument = pTools != NULL ? pTools->MakeSound(E, Why) : -1;
	}
	if (Instrument >= 0 || pDoc->GetExpansionChip() != ChipsBefore || pDoc->IsVrc6Plus() != PlusBefore)
		++m_iKept;		// the song changed: closing the browser leaves it changed
	// Every sound again, the filters kept where they still are (the kind picked is kept by FillFilters)
	const int From = m_cFrom.GetCurSel();
	m_iSelected = -1;
	m_SelectedKey.clear();
	m_iPlaying = -1;
	m_iMarked = -1;
	if (LoadSounds()) {
		FillFilters();
		m_cFrom.SetCurSel(From >= 0 && From < m_cFrom.GetCount() ? From : 0);
		for (size_t i = 0; i < m_All.size() && Instrument >= 0; ++i)
			if (m_All[i].Source == ftapi::SOURCE_SONG && m_All[i].Number == Instrument && m_All[i].Home)
				m_iSelected = static_cast<int>(i);
		Refilter();
	}
	if (Instrument < 0 || Instrument >= MAX_INSTRUMENTS) {
		SetStatus(Why.empty() ? CString(_T("The sound couldn't be added.")) : FromUtf8(Why));
		return;
	}
	m_pMainFrame->SelectInstrument(Instrument);
	const CString Chip = FromUtf8(E.AddsChip), Name = FromUtf8(E.Name);
	CString Text;
	Text.Format(_T("Added the %s to the song (its channels come after the song's own) and \"%s\" as instrument %02X, picked: ")
		_T("click it to hear it, and type notes with it on the new channels."), static_cast<LPCTSTR>(Chip), static_cast<LPCTSTR>(Name),
		Instrument);
	SetStatus(Text);
}

void CSoundBrowserDlg::OnDestroy()
{
	GetWindowRect(&g_Place);
	KillTimer(TMR_FIND);
	KillTimer(TMR_RETRY);
	KillTimer(TMR_SETTLE);
	KillTimer(TMR_MARKS);
	CFamiTrackerView *pView = CFamiTrackerView::GetView();
	if (pView != NULL)
		pView->SetNoteListener(NULL, 0);
	Preview()->Stop();		// closing the browser stops the sound
	DropTrial();
	// Only trying sounds leaves the song as it was
	if (m_iKept == 0 && !m_bWasModified)
		Doc()->SetModifiedFlag(FALSE);
	CDialog::OnDestroy();
}

void CSoundBrowserDlg::OnSize(UINT nType, int cx, int cy)
{
	CDialog::OnSize(nType, cx, cy);
	if (nType != SIZE_MINIMIZED)
		Layout();
}

void CSoundBrowserDlg::OnGetMinMaxInfo(MINMAXINFO *pInfo)
{
	if (m_MinSize.cx > 0) {
		pInfo->ptMinTrackSize.x = m_MinSize.cx;
		pInfo->ptMinTrackSize.y = m_MinSize.cy;
	}
	CDialog::OnGetMinMaxInfo(pInfo);
}

// How tall a text control's text is at a width
int CSoundBrowserDlg::TextHeight(int Id, int Width)
{
	CString Text;
	GetDlgItemText(Id, Text);
	CClientDC Dc(this);
	CFont *pOldFont = Dc.SelectObject(GetFont());
	CRect Rect(0, 0, std::max(Width, 1), 0);
	Dc.DrawText(Text, &Rect, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
	Dc.SelectObject(pOldFont);
	return Rect.Height();
}

// The controls placed for the window's size: the filters in a row (or two, when narrow), the kinds on the left of
// the sounds, which take the room left, and the words, the status and the buttons under them
void CSoundBrowserDlg::Layout()
{
	if (m_cList.GetSafeHwnd() == NULL)
		return;
	CRect Client, Unit(0, 0, 4, 8);
	GetClientRect(&Client);
	MapDialogRect(&Unit);		// a character's average width and its height
	const int Cx = Unit.Width(), Cy = Unit.Height();
	const int W = Client.Width(), H = Client.Height();
	const int Mx = Cx * 7 / 4, My = Cy * 7 / 8, Gap = Cx, VGap = Cy / 2;
	const int Line = Cy, EditH = Cy * 12 / 8, ButtonH = Cy * 14 / 8, DropH = Cy * 16;
	const int Inner = std::max(W - 2 * Mx, Cx * 40);
	HDWP Defer = ::BeginDeferWindowPos(20);
	auto Put = [&](int Id, int Left, int Top, int Wide, int High) {
		CWnd *pWnd = GetDlgItem(Id);
		if (pWnd != NULL && Defer != NULL)
			Defer = ::DeferWindowPos(Defer, pWnd->GetSafeHwnd(), NULL, Left, Top, std::max(Wide, 1), std::max(High, 1),
				SWP_NOZORDER | SWP_NOACTIVATE);
	};
	int y = My;
	const int IntroH = std::min(TextHeight(IDC_SB_INTRO, Inner), 4 * Line);
	Put(IDC_SB_INTRO, Mx, y, Inner, IntroH);
	y += IntroH + VGap + Cy / 4;
	// Find, From and Plays on
	const int FindLabel = Cx * 6, FromLabel = Cx * 6, OnLabel = Cx * 9, FromW = Cx * 24, OnW = Cx * 26;
	const int LabelDy = (EditH - Line) / 2;
	if (Inner >= Cx * 100) {
		const int FindW = Inner - FindLabel - FromLabel - FromW - OnLabel - OnW - 4 * Gap;
		int x = Mx;
		Put(IDC_SB_FIND_LABEL, x, y + LabelDy, FindLabel, Line);
		x += FindLabel;
		Put(IDC_SB_FIND, x, y, FindW, EditH);
		x += FindW + 2 * Gap;
		Put(IDC_SB_FROM_LABEL, x, y + LabelDy, FromLabel, Line);
		x += FromLabel;
		Put(IDC_SB_FROM, x, y, FromW, DropH);
		x += FromW + 2 * Gap;
		Put(IDC_SB_CHANNEL_LABEL, x, y + LabelDy, OnLabel, Line);
		x += OnLabel;
		Put(IDC_SB_CHANNEL, x, y, Mx + Inner - x, DropH);
	}
	else {
		Put(IDC_SB_FIND_LABEL, Mx, y + LabelDy, FindLabel, Line);
		Put(IDC_SB_FIND, Mx + FindLabel, y, Inner - FindLabel, EditH);
		y += EditH + VGap;
		const int Half = (Inner - 2 * Gap) / 2;
		Put(IDC_SB_FROM_LABEL, Mx, y + LabelDy, FromLabel, Line);
		Put(IDC_SB_FROM, Mx + FromLabel, y, Half - FromLabel, DropH);
		Put(IDC_SB_CHANNEL_LABEL, Mx + Half + 2 * Gap, y + LabelDy, OnLabel, Line);
		Put(IDC_SB_CHANNEL, Mx + Half + 2 * Gap + OnLabel, y, Inner - Half - 2 * Gap - OnLabel, DropH);
	}
	y += EditH + VGap + Cy / 4;
	// From the bottom up: the buttons, the status over them, the sound's words over that
	const int ButtonsY = H - My - ButtonH;
	Put(IDC_SB_LISTEN, Mx, ButtonsY, Cx * 15, ButtonH);
	Put(IDC_SB_ADD, Mx + Cx * 15 + Gap, ButtonsY, Cx * 18, ButtonH);
	Put(IDCANCEL, Mx + Inner - Cx * 13, ButtonsY, Cx * 13, ButtonH);
	const int StatusH = TextHeight(IDC_SB_STATUS, Inner) > 2 * Line ? 3 * Line : 2 * Line, StatusY = ButtonsY - VGap - StatusH;
	Put(IDC_SB_STATUS, Mx, StatusY, Inner, StatusH);
	const int TextH = 3 * Line, TextY = StatusY - VGap - TextH;
	Put(IDC_SB_TEXT, Mx, TextY, Inner, TextH);
	// The kinds, with the count under them, and the sounds
	const int ListsBottom = std::max(TextY - VGap, y + 4 * Line);
	const int KindsW = std::max(Cx * 19, std::min(Cx * 28, Inner / 5)), CountH = 2 * Line;
	Put(IDC_SB_KIND, Mx, y, KindsW, ListsBottom - y - CountH - VGap / 2);
	Put(IDC_SB_COUNT, Mx, ListsBottom - CountH, KindsW, CountH);
	Put(IDC_SB_LIST, Mx + KindsW + Gap, y, Inner - KindsW - Gap, ListsBottom - y);
	if (Defer != NULL)
		::EndDeferWindowPos(Defer);
	// The columns share the list's width
	CRect List;
	m_cList.GetClientRect(&List);
	const int Width = std::max(List.Width() - ::GetSystemMetrics(SM_CXVSCROLL), Cx * 20);
	for (int c = 0; c < COLUMN_COUNT; ++c)
		m_cList.SetColumnWidth(c, Width * LIST_COLUMNS[c].Percent / 100);
	Invalidate();
}

void CSoundBrowserDlg::SetStatus(LPCTSTR Text)
{
	SetDlgItemText(IDC_SB_STATUS, Text);
	Layout();		// (two lines or three)
}

void ShowSoundBrowser(CMainFrame *pMainFrame)
{
	CFamiTrackerDoc *pDoc = static_cast<CFamiTrackerDoc*>(pMainFrame->GetActiveDocument());
	if (pDoc == NULL || !pDoc->IsFileLoaded())
		return;
	CSoundBrowserDlg Dialog(pMainFrame);
	Dialog.DoModal();
}

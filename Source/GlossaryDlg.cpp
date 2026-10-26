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

// Help > Glossary (see GlossaryDlg.h)

#include "stdafx.h"
#include <algorithm>
#include <string>
#include <vector>
#include "Api/Glossary.h"
#include "FamiTracker.h"
#include "MainFrm.h"
#include "Accelerator.h"
#include "GlossaryHear.h"
#include "GlossaryDlg.h"

namespace {

LPCTSTR const SECTION = _T("Glossary");
const UINT_PTR TMR_HEAR = 1;
const UINT HEAR_POLL_MS = 200;

const TCHAR *const CATEGORIES[] = { _T("All"), _T("Making music"), _T("Sounds"), _T("The tracker"), _T("Effects") };

const TCHAR INTRO[] = _T("The words of music, of the NES's sounds and of FamiTracker, in plain words. Pick a term on the ")
	_T("left, or type in Find. Hear it plays a short example on the song's channels.\r\n\r\nIn the pattern editor, rest the ")
	_T("mouse on an effect to read what its numbers do; F1 then opens it here. The labels of the song settings, the ")
	_T("instrument editor's sequences, the mixer's knobs and the piano roll's Key and Chords explain themselves the same ")
	_T("way.");

CString Text(const std::string &Ascii)
{
	return CString(Ascii.c_str());
}

// The see-also line: as links' markup ("See also: <a id="triad">Triad</a>, ..."), or the terms alone. ASCII
std::string SeeAlsoLine(const ftapi::GlossaryEntry &Entry, bool Markup)
{
	std::string Line;
	for (size_t i = 0; i < Entry.SeeAlso.size(); ++i) {
		const ftapi::GlossaryEntry *pOther = ftapi::FindGlossaryEntry(Entry.SeeAlso[i]);
		if (pOther == NULL)
			continue;
		Line += Line.empty() ? "See also: " : ", ";
		Line += Markup ? "<a id=\"" + pOther->Id + "\">" + pOther->Term + "</a>" : pOther->Term;
	}
	return Line;
}

class CGlossaryDlg : public CDialog
{
public:
	explicit CGlossaryDlg(CMainFrame *pMainFrame) : CDialog(IDD_GLOSSARY, pMainFrame), m_pMainFrame(pMainFrame),
		m_hLinks(NULL), m_bLinkControl(false), m_bPlaying(false), m_bPlaced(false) {}

	static CGlossaryDlg *s_pOpen;	// the window, while it's there (shown or hidden)

	// Shows an entry: Remember keeps the one shown before for Back
	void ShowEntry(const std::string &Id, bool Remember);
	// Shown without the keys (they stay where the user types notes), or hidden; remembered
	void Appear(bool Show);

protected:
	virtual BOOL OnInitDialog();
	virtual void OnOK();
	virtual void OnCancel();
	virtual void PostNcDestroy();
	virtual BOOL PreTranslateMessage(MSG *pMsg);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnGetMinMaxInfo(MINMAXINFO *pInfo);
	afx_msg void OnDestroy();
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnSearchChange();
	afx_msg void OnCategory();
	afx_msg void OnPick();
	afx_msg void OnHear();
	afx_msg void OnBack();
	afx_msg void OnLink(NMHDR *pNMHDR, LRESULT *pResult);
	afx_msg HBRUSH OnCtlColor(CDC *pDC, CWnd *pWnd, UINT nCtlColor);
	DECLARE_MESSAGE_MAP()

private:
	void Fill();				// the terms the search words and the category find
	void Layout();
	void Place();				// where it was, else beside the song
	void SavePlace();
	void ShowHearing();			// the Hear it button and its line, as the example plays or not
	void ShowSeeAlso(const ftapi::GlossaryEntry *pEntry);

	CMainFrame *m_pMainFrame;
	// The see-also links: a SysLink, a Unicode-only control that the MBCS build's CLinkCtrl::Create never makes (it
	// returns FALSE), so it's made with CreateWindowExW; where it can't be, a plain line names the terms
	HWND m_hLinks;
	bool m_bLinkControl;
	CFont m_TitleFont;
	std::string m_Shown;					// the entry shown ("" for the introduction)
	std::vector<std::string> m_Back;		// the ones shown before it
	bool m_bPlaying;
	bool m_bPlaced;
	CString m_strHeard;						// what the Hear it line says while nothing plays
};

CGlossaryDlg *CGlossaryDlg::s_pOpen = NULL;

BEGIN_MESSAGE_MAP(CGlossaryDlg, CDialog)
	ON_WM_SIZE()
	ON_WM_GETMINMAXINFO()
	ON_WM_DESTROY()
	ON_WM_TIMER()
	ON_WM_CTLCOLOR()
	ON_EN_CHANGE(IDC_GLOSSARY_SEARCH, OnSearchChange)
	ON_CBN_SELCHANGE(IDC_GLOSSARY_CATEGORY, OnCategory)
	ON_LBN_SELCHANGE(IDC_GLOSSARY_LIST, OnPick)
	ON_BN_CLICKED(IDC_GLOSSARY_HEAR, OnHear)
	ON_BN_CLICKED(IDC_GLOSSARY_BACK, OnBack)
	ON_NOTIFY(NM_CLICK, IDC_GLOSSARY_SEEALSO, OnLink)
	ON_NOTIFY(NM_RETURN, IDC_GLOSSARY_SEEALSO, OnLink)
END_MESSAGE_MAP()

BOOL CGlossaryDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	// The links (common controls 6, as FamiTracker's manifest asks for)
	INITCOMMONCONTROLSEX Controls;
	Controls.dwSize = sizeof(Controls);
	Controls.dwICC = ICC_LINK_CLASS;
	::InitCommonControlsEx(&Controls);
	HMENU Id = reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_GLOSSARY_SEEALSO));
	m_hLinks = ::CreateWindowExW(0, L"SysLink", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 10, 10, m_hWnd, Id,
		AfxGetInstanceHandle(), NULL);
	m_bLinkControl = m_hLinks != NULL;
	if (m_hLinks == NULL)
		m_hLinks = ::CreateWindowEx(0, _T("STATIC"), _T(""), WS_CHILD | WS_VISIBLE | SS_NOPREFIX, 0, 0, 10, 10, m_hWnd, Id,
			AfxGetInstanceHandle(), NULL);
	if (m_hLinks != NULL)
		::SendMessage(m_hLinks, WM_SETFONT, reinterpret_cast<WPARAM>(GetFont()->GetSafeHandle()), FALSE);
	// The term in bold, a little larger
	LOGFONT Font;
	GetFont()->GetLogFont(&Font);
	Font.lfWeight = FW_BOLD;
	Font.lfHeight = Font.lfHeight * 5 / 4;
	m_TitleFont.CreateFontIndirect(&Font);
	GetDlgItem(IDC_GLOSSARY_TERM)->SetFont(&m_TitleFont);
	CComboBox *pCategory = static_cast<CComboBox*>(GetDlgItem(IDC_GLOSSARY_CATEGORY));
	for (size_t i = 0; i < sizeof(CATEGORIES) / sizeof(CATEGORIES[0]); ++i)
		pCategory->AddString(CATEGORIES[i]);
	pCategory->SetCurSel(0);
	Fill();
	ShowEntry(std::string(), false);
	Place();
	Layout();
	return FALSE;	// the keys stay where they were
}

void CGlossaryDlg::Place()
{
	CRect Main;
	m_pMainFrame->GetWindowRect(&Main);
	CRect Mine;
	GetWindowRect(&Mine);
	const int Width = static_cast<int>(theApp.GetProfileInt(SECTION, _T("Width"), static_cast<UINT>(Mine.Width())));
	const int Height = static_cast<int>(theApp.GetProfileInt(SECTION, _T("Height"), static_cast<UINT>(Mine.Height())));
	// Beside the song: at the main window's right edge, under its toolbars
	const int Left = static_cast<int>(theApp.GetProfileInt(SECTION, _T("Left"), static_cast<UINT>(Main.right - Width - SX(24))));
	const int Top = static_cast<int>(theApp.GetProfileInt(SECTION, _T("Top"), static_cast<UINT>(Main.top + SY(150))));
	CRect Wanted(Left, Top, Left + std::max(Width, SX(300)), Top + std::max(Height, SY(240)));
	// On a screen still (a monitor taken away since)
	if (::MonitorFromRect(&Wanted, MONITOR_DEFAULTTONULL) == NULL)
		Wanted.OffsetRect(Main.right - Wanted.Width() - SX(24) - Wanted.left, Main.top + SY(150) - Wanted.top);
	SetWindowPos(NULL, Wanted.left, Wanted.top, Wanted.Width(), Wanted.Height(), SWP_NOZORDER | SWP_NOACTIVATE);
	m_bPlaced = true;
}

void CGlossaryDlg::SavePlace()
{
	if (!m_bPlaced || m_hWnd == NULL)
		return;
	WINDOWPLACEMENT Placement;
	Placement.length = sizeof(Placement);
	if (!GetWindowPlacement(&Placement))
		return;
	const CRect Rect(Placement.rcNormalPosition);
	theApp.WriteProfileInt(SECTION, _T("Left"), Rect.left);
	theApp.WriteProfileInt(SECTION, _T("Top"), Rect.top);
	theApp.WriteProfileInt(SECTION, _T("Width"), Rect.Width());
	theApp.WriteProfileInt(SECTION, _T("Height"), Rect.Height());
}

void CGlossaryDlg::Appear(bool Show)
{
	if (Show) {
		ShowWindow(SW_SHOWNOACTIVATE);
		ShowHearing();
	}
	else {
		SavePlace();
		ShowWindow(SW_HIDE);
	}
	theApp.WriteProfileInt(SECTION, _T("Open"), Show ? 1 : 0);
}

void CGlossaryDlg::Layout()
{
	if (m_hWnd == NULL || GetDlgItem(IDC_GLOSSARY_LIST) == NULL)
		return;
	CRect Client;
	GetClientRect(&Client);
	CRect Unit(0, 0, 7, 14);	// a margin and a control's height, in dialog units
	MapDialogRect(&Unit);
	const int Margin = Unit.Width(), Line = Unit.Height(), Gap = Margin / 2 + 1;
	const int Left = std::max(SX(140), Client.Width() * 34 / 100);
	const int Find = SX(32);
	GetDlgItem(IDC_GLOSSARY_FINDLABEL)->MoveWindow(Margin, Margin + 3, Find - 2, Line - 3);
	GetDlgItem(IDC_GLOSSARY_SEARCH)->MoveWindow(Margin + Find, Margin, Left - Find, Line);
	GetDlgItem(IDC_GLOSSARY_CATEGORY)->MoveWindow(Margin, Margin + Line + Gap, Left, Line * 10);
	const int ListTop = Margin + 2 * (Line + Gap);
	GetDlgItem(IDC_GLOSSARY_LIST)->MoveWindow(Margin, ListTop, Left, std::max(Line, static_cast<int>(Client.bottom) - Margin - ListTop));
	// The entry on the right: the term, its other names, the text, Hear it, see also, Back
	const int x = Margin * 2 + Left, Width = std::max(SX(80), Client.Width() - x - Margin);
	const int TitleHeight = Line + 4;
	GetDlgItem(IDC_GLOSSARY_TERM)->MoveWindow(x, Margin, Width, TitleHeight);
	GetDlgItem(IDC_GLOSSARY_NAMES)->MoveWindow(x, Margin + TitleHeight, Width, Line * 2);
	const int BackTop = static_cast<int>(Client.bottom) - Margin - Line - 2;
	const int LinksHeight = Line * 3;
	const int LinksTop = BackTop - Gap - LinksHeight;
	const int HearTop = LinksTop - Gap - Line * 3 - 2;	// the Hear it line: up to five lines of text
	const int TextTop = Margin + TitleHeight + Line * 2 + Gap;
	GetDlgItem(IDC_GLOSSARY_TEXT)->MoveWindow(x, TextTop, Width, std::max(Line, HearTop - Gap - TextTop));
	const int Button = SX(64);
	GetDlgItem(IDC_GLOSSARY_HEAR)->MoveWindow(x, HearTop, Button, Line + 4);
	GetDlgItem(IDC_GLOSSARY_HEARTEXT)->MoveWindow(x + Button + Margin, HearTop, std::max(SX(40), Width - Button - Margin), Line * 3 + 2);
	if (m_hLinks != NULL)
		::MoveWindow(m_hLinks, x, LinksTop, Width, LinksHeight, TRUE);
	GetDlgItem(IDC_GLOSSARY_BACK)->MoveWindow(x, BackTop, Button, Line + 2);
	Invalidate();
}

void CGlossaryDlg::OnSize(UINT nType, int cx, int cy)
{
	CDialog::OnSize(nType, cx, cy);
	Layout();
}

void CGlossaryDlg::OnGetMinMaxInfo(MINMAXINFO *pInfo)
{
	pInfo->ptMinTrackSize.x = SX(380);
	pInfo->ptMinTrackSize.y = SY(380);	// room for a few lines of the text over Hear it's five and the links
	CDialog::OnGetMinMaxInfo(pInfo);
}

void CGlossaryDlg::Fill()
{
	CString Words;
	GetDlgItemText(IDC_GLOSSARY_SEARCH, Words);
	const int Category = static_cast<CComboBox*>(GetDlgItem(IDC_GLOSSARY_CATEGORY))->GetCurSel() - 1;
	const std::vector<const ftapi::GlossaryEntry *> Found = ftapi::SearchGlossary(std::string(static_cast<LPCSTR>(CStringA(Words))), Category);
	CListBox *pList = static_cast<CListBox*>(GetDlgItem(IDC_GLOSSARY_LIST));
	pList->SetRedraw(FALSE);
	pList->ResetContent();
	const std::vector<ftapi::GlossaryEntry> &All = ftapi::GlossaryEntries();
	for (size_t i = 0; i < Found.size(); ++i) {
		const int Item = pList->AddString(Text(Found[i]->Term));
		pList->SetItemData(Item, static_cast<DWORD_PTR>(Found[i] - &All[0]));
		if (Found[i]->Id == m_Shown)
			pList->SetCurSel(Item);
	}
	if (Found.empty())
		pList->AddString(_T("(nothing found)"));
	pList->SetRedraw(TRUE);
	pList->Invalidate();
}

void CGlossaryDlg::ShowEntry(const std::string &Id, bool Remember)
{
	const ftapi::GlossaryEntry *pEntry = Id.empty() ? NULL : ftapi::FindGlossaryEntry(Id);
	const std::string Now = pEntry != NULL ? pEntry->Id : std::string();
	if (Remember && !m_Shown.empty() && Now != m_Shown)
		m_Back.push_back(m_Shown);
	m_Shown = Now;
	GetDlgItem(IDC_GLOSSARY_BACK)->EnableWindow(!m_Back.empty());
	if (pEntry == NULL) {
		SetDlgItemText(IDC_GLOSSARY_TERM, _T("Glossary"));
		SetDlgItemText(IDC_GLOSSARY_NAMES, _T(""));
		SetDlgItemText(IDC_GLOSSARY_TEXT, INTRO);
		ShowSeeAlso(NULL);
		m_strHeard.Empty();
		ShowHearing();
		return;
	}
	SetDlgItemText(IDC_GLOSSARY_TERM, Text(pEntry->Term));
	CString Names = Text(ftapi::GlossaryCategoryName(pEntry->Category));
	if (!pEntry->Names.empty()) {
		Names += _T(". Also called: ");
		for (size_t i = 0; i < pEntry->Names.size(); ++i)
			Names += (i == 0 ? _T("") : _T(", ")) + Text(pEntry->Names[i]);
	}
	SetDlgItemText(IDC_GLOSSARY_NAMES, Names + _T("."));
	SetDlgItemText(IDC_GLOSSARY_TEXT, Text(pEntry->Text));
	ShowSeeAlso(pEntry);
	// What Hear it plays, or how to hear it where it plays nothing
	ftapi::HearExample Example;
	if (!pEntry->Hear.empty() && ftapi::GlossaryExample(pEntry->Hear, Example))
		m_strHeard = Text(Example.Says);
	else
		m_strHeard = Text(pEntry->HearHow);
	ShowHearing();
	// The list shows it picked where it's listed
	CListBox *pList = static_cast<CListBox*>(GetDlgItem(IDC_GLOSSARY_LIST));
	const std::vector<ftapi::GlossaryEntry> &All = ftapi::GlossaryEntries();
	int Picked = LB_ERR;
	for (int i = 0; i < pList->GetCount(); ++i) {
		const DWORD_PTR Index = pList->GetItemData(i);
		if (Index < All.size() && All[Index].Id == m_Shown)
			Picked = i;
	}
	pList->SetCurSel(Picked);
}

void CGlossaryDlg::ShowSeeAlso(const ftapi::GlossaryEntry *pEntry)
{
	if (m_hLinks == NULL)
		return;
	const std::string Line = pEntry != NULL ? SeeAlsoLine(*pEntry, m_bLinkControl) : std::string();
	if (m_bLinkControl)
		::SetWindowTextW(m_hLinks, std::wstring(Line.begin(), Line.end()).c_str());	// ASCII
	else
		::SetWindowText(m_hLinks, Text(Line));
}

void CGlossaryDlg::ShowHearing()
{
	if (m_hWnd == NULL)
		return;
	const ftapi::GlossaryEntry *pEntry = m_Shown.empty() ? NULL : ftapi::FindGlossaryEntry(m_Shown);
	const bool Playing = IsGlossaryExamplePlaying();
	const bool Example = pEntry != NULL && !pEntry->Hear.empty();
	SetDlgItemText(IDC_GLOSSARY_HEAR, Playing && m_bPlaying ? _T("Stop") : _T("Hear it"));
	GetDlgItem(IDC_GLOSSARY_HEAR)->EnableWindow(Example || (Playing && m_bPlaying));
	SetDlgItemText(IDC_GLOSSARY_HEARTEXT, Playing && m_bPlaying ? _T("Playing: ") + GlossaryExamplePlaying() : m_strHeard);
}

void CGlossaryDlg::OnSearchChange()
{
	Fill();
}

void CGlossaryDlg::OnCategory()
{
	Fill();
}

void CGlossaryDlg::OnPick()
{
	CListBox *pList = static_cast<CListBox*>(GetDlgItem(IDC_GLOSSARY_LIST));
	const int Item = pList->GetCurSel();
	const std::vector<ftapi::GlossaryEntry> &All = ftapi::GlossaryEntries();
	if (Item == LB_ERR)
		return;
	const DWORD_PTR Index = pList->GetItemData(Item);
	if (Index < All.size())
		ShowEntry(All[Index].Id, true);
}

// Enter in Find: the first term found
void CGlossaryDlg::OnOK()
{
	CListBox *pList = static_cast<CListBox*>(GetDlgItem(IDC_GLOSSARY_LIST));
	const std::vector<ftapi::GlossaryEntry> &All = ftapi::GlossaryEntries();
	if (pList->GetCount() > 0 && pList->GetItemData(0) < All.size()) {
		pList->SetCurSel(0);
		ShowEntry(All[pList->GetItemData(0)].Id, true);
	}
}

void CGlossaryDlg::OnCancel()
{
	Appear(false);
}

void CGlossaryDlg::OnHear()
{
	if (IsGlossaryExamplePlaying() && m_bPlaying) {
		StopGlossaryExample();
		m_bPlaying = false;
		KillTimer(TMR_HEAR);
		ShowHearing();
		return;
	}
	const ftapi::GlossaryEntry *pEntry = m_Shown.empty() ? NULL : ftapi::FindGlossaryEntry(m_Shown);
	if (pEntry == NULL || pEntry->Hear.empty())
		return;
	CString Why;
	if (!PlayGlossaryExample(m_pMainFrame, pEntry->Hear, Why)) {
		m_bPlaying = false;
		SetDlgItemText(IDC_GLOSSARY_HEARTEXT, Why);
		return;
	}
	m_bPlaying = true;
	ShowHearing();
	SetTimer(TMR_HEAR, HEAR_POLL_MS, NULL);
}

void CGlossaryDlg::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent != TMR_HEAR) {
		CDialog::OnTimer(nIDEvent);
		return;
	}
	if (!IsGlossaryExamplePlaying()) {
		m_bPlaying = false;
		KillTimer(TMR_HEAR);
		ShowHearing();
	}
}

void CGlossaryDlg::OnBack()
{
	if (m_Back.empty())
		return;
	const std::string Id = m_Back.back();
	m_Back.pop_back();
	ShowEntry(Id, false);
}

void CGlossaryDlg::OnLink(NMHDR *pNMHDR, LRESULT *pResult)
{
	const NMLINK *pLink = reinterpret_cast<const NMLINK*>(pNMHDR);
	std::string Id;
	for (const WCHAR *p = pLink->item.szID; *p != 0; ++p)
		Id += static_cast<char>(*p < 128 ? *p : '?');
	ShowEntry(Id, true);
	*pResult = 0;
}

BOOL CGlossaryDlg::PreTranslateMessage(MSG *pMsg)
{
	if (pMsg->message == WM_KEYDOWN) {
		const bool Control = (::GetKeyState(VK_CONTROL) & 0x8000) != 0, Shift = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
		// FamiTracker's window commands (Ctrl+Shift: the glossary itself, the piano roll, the clips...) work from here too
		if (Control && Shift && theApp.GetAccelerator()->Translate(m_pMainFrame->GetSafeHwnd(), pMsg))
			return TRUE;
		// Up and down in Find go through the terms found
		CWnd *pFocus = GetFocus();
		if (pFocus != NULL && pFocus->GetDlgCtrlID() == IDC_GLOSSARY_SEARCH &&
			(pMsg->wParam == VK_UP || pMsg->wParam == VK_DOWN || pMsg->wParam == VK_PRIOR || pMsg->wParam == VK_NEXT)) {
			CListBox *pList = static_cast<CListBox*>(GetDlgItem(IDC_GLOSSARY_LIST));
			const int Count = pList->GetCount();
			if (Count > 0) {
				const int Step = pMsg->wParam == VK_UP ? -1 : pMsg->wParam == VK_DOWN ? 1 : pMsg->wParam == VK_PRIOR ? -10 : 10;
				const int Now = pList->GetCurSel();
				const int Next = std::max(0, std::min(Count - 1, (Now == LB_ERR ? (Step > 0 ? -1 : Count) : Now) + Step));
				pList->SetCurSel(Next);
				OnPick();
			}
			return TRUE;
		}
	}
	return CDialog::PreTranslateMessage(pMsg);
}

// The entry's text on the page's own color, though it can't be edited
HBRUSH CGlossaryDlg::OnCtlColor(CDC *pDC, CWnd *pWnd, UINT nCtlColor)
{
	if (pWnd != NULL && pWnd->GetDlgCtrlID() == IDC_GLOSSARY_TEXT) {
		pDC->SetTextColor(::GetSysColor(COLOR_WINDOWTEXT));
		pDC->SetBkColor(::GetSysColor(COLOR_WINDOW));
		return ::GetSysColorBrush(COLOR_WINDOW);
	}
	return CDialog::OnCtlColor(pDC, pWnd, nCtlColor);
}

void CGlossaryDlg::OnDestroy()
{
	SavePlace();
	if (m_bPlaying)
		StopGlossaryExample();
	CDialog::OnDestroy();
}

void CGlossaryDlg::PostNcDestroy()
{
	if (s_pOpen == this)
		s_pOpen = NULL;
	delete this;
}

CGlossaryDlg *Glossary(CMainFrame *pMainFrame)
{
	if (CGlossaryDlg::s_pOpen != NULL)
		return CGlossaryDlg::s_pOpen;
	CGlossaryDlg *pDialog = new CGlossaryDlg(pMainFrame);
	if (!pDialog->Create(IDD_GLOSSARY, pMainFrame)) {
		delete pDialog;
		return NULL;
	}
	CGlossaryDlg::s_pOpen = pDialog;
	return pDialog;
}

}	// namespace

void ToggleGlossary(CMainFrame *pMainFrame)
{
	const bool Open = IsGlossaryOpen();
	CGlossaryDlg *pDialog = Open ? CGlossaryDlg::s_pOpen : Glossary(pMainFrame);
	if (pDialog != NULL)
		pDialog->Appear(!Open);
}

bool IsGlossaryOpen()
{
	return CGlossaryDlg::s_pOpen != NULL && CGlossaryDlg::s_pOpen->IsWindowVisible();
}

void ShowGlossaryAt(CMainFrame *pMainFrame, const std::string &Id)
{
	CGlossaryDlg *pDialog = Glossary(pMainFrame);
	if (pDialog == NULL)
		return;
	pDialog->ShowEntry(Id, true);
	if (!pDialog->IsWindowVisible())
		pDialog->Appear(true);
}

void RestoreGlossary(CMainFrame *pMainFrame)
{
	if (theApp.GetProfileInt(SECTION, _T("Open"), 0) == 0)
		return;
	CGlossaryDlg *pDialog = Glossary(pMainFrame);
	if (pDialog != NULL)
		pDialog->Appear(true);
}

CWnd *GlossaryWindow()
{
	return IsGlossaryOpen() ? CGlossaryDlg::s_pOpen : NULL;
}

void CloseGlossary()
{
	if (CGlossaryDlg::s_pOpen != NULL)
		CGlossaryDlg::s_pOpen->DestroyWindow();		// (it keeps its place; whether it's open stays as last chosen)
}

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
#include <string>
#include <vector>
#include "FamiTracker.h"
#include "MainFrm.h"
#include "McpConnection.h"
#include "McpSocket.h"
#include "PhoneDlg.h"
#include "PhoneLink.h"
#include "Api/Network.h"
#include "Api/PhoneTakes.h"
#include "Api/QrCode.h"

namespace {

const UINT_PTR TMR_REFRESH = 1;
const UINT REFRESH_MS = 500;
const int ADDRESS_TICKS = 10;	// the network's addresses are looked at again every 5 seconds (Tailscale may come up later)
const int TAKES_SHOWN = 3;

// This computer's addresses a phone at home can reach: home networks' and Tailscale's
std::vector<mcp_address_t> HomeAddresses()
{
	const std::vector<mcp_address_t> Found = ListNetworkAddresses();
	std::vector<mcp_address_t> Home;
	for (size_t i = 0; i < Found.size(); ++i) {
		unsigned long Address = 0;
		if (ftapi::ParseIpv4(Found[i].Address, Address) && ftapi::IsPrivateIpv4(Address))
			Home.push_back(Found[i]);
	}
	return Home;
}

// UTF-8 (the page's names) to this computer's code page
CString FromUtf8(const std::string &Text)
{
	if (Text.empty())
		return CString();
	const int Wide = MultiByteToWideChar(CP_UTF8, 0, Text.c_str(), static_cast<int>(Text.size()), NULL, 0);
	if (Wide <= 0)
		return CString();
	std::vector<wchar_t> Chars(static_cast<size_t>(Wide));
	MultiByteToWideChar(CP_UTF8, 0, Text.c_str(), static_cast<int>(Text.size()), &Chars[0], Wide);
	const int Size = WideCharToMultiByte(CP_ACP, 0, &Chars[0], Wide, NULL, 0, NULL, NULL);
	if (Size <= 0)
		return CString();
	std::vector<char> Bytes(static_cast<size_t>(Size) + 1, 0);
	WideCharToMultiByte(CP_ACP, 0, &Chars[0], Wide, &Bytes[0], Size, NULL, NULL);
	return CString(&Bytes[0], Size);
}

}	// namespace

class CPhoneDlg : public CDialog
{
public:
	explicit CPhoneDlg(CMainFrame *pMainFrame) : CDialog(IDD_PHONE, pMainFrame), m_pMainFrame(pMainFrame), m_iHome(0),
		m_bReachable(false), m_bAllowed(false), m_iTicks(0) {}

	static CPhoneDlg *s_pOpen;	// the window, while it shows

protected:
	virtual BOOL OnInitDialog();
	virtual void OnOK();
	virtual void OnCancel();
	virtual void PostNcDestroy();
	afx_msg void OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT pDraw);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnNetworkPicked();
	afx_msg void OnAllow();
	afx_msg void OnCopy();
	DECLARE_MESSAGE_MAP()

private:
	bool ReadAddresses();	// true when they changed
	void ShowAddress();
	void ShowTakes();

	CMainFrame *m_pMainFrame;
	std::vector<mcp_address_t> m_Addresses;	// the home networks' addresses, then this PC's name
	size_t m_iHome;							// how many are addresses
	ftapi::QrSymbol m_Symbol;
	bool m_bReachable;		// the code shows: FamiTracker listens on the home network
	bool m_bAllowed;		// just now, by the button here
	int m_iTicks;
	CString m_sUrl, m_sTakes;
};

CPhoneDlg *CPhoneDlg::s_pOpen = NULL;

BEGIN_MESSAGE_MAP(CPhoneDlg, CDialog)
	ON_WM_DRAWITEM()
	ON_WM_TIMER()
	ON_CBN_SELCHANGE(IDC_PHONE_NETWORKS, OnNetworkPicked)
	ON_BN_CLICKED(IDC_PHONE_ALLOW, OnAllow)
	ON_BN_CLICKED(IDC_PHONE_COPY, OnCopy)
END_MESSAGE_MAP()

BOOL CPhoneDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	ReadAddresses();
	ShowAddress();
	ShowTakes();
	SetTimer(TMR_REFRESH, REFRESH_MS, NULL);
	CenterWindow(m_pMainFrame);
	GetDlgItem(IDCANCEL)->SetFocus();	// not the address, which would show selected from its end
	return FALSE;
}

void CPhoneDlg::OnOK()
{
	DestroyWindow();
}

void CPhoneDlg::OnCancel()
{
	DestroyWindow();
}

void CPhoneDlg::PostNcDestroy()
{
	if (s_pOpen == this)
		s_pOpen = NULL;
	delete this;
}

bool CPhoneDlg::ReadAddresses()
{
	std::vector<mcp_address_t> Found = HomeAddresses();
	const size_t Home = Found.size();
	// Last, this PC's name, which a phone finds through Tailscale (MagicDNS) wherever it is, or where the router knows it
	std::string Name = NetworkComputerName();
	for (size_t i = 0; i < Name.size(); ++i)
		Name[i] = static_cast<char>(Name[i] >= 'A' && Name[i] <= 'Z' ? Name[i] - 'A' + 'a' : Name[i]);
	if (!Name.empty()) {
		mcp_address_t ByName;
		ByName.Address = Name;
		ByName.Adapter = "This PC's name";
		ByName.Gateway = false;
		Found.push_back(ByName);
	}
	bool Same = Found.size() == m_Addresses.size();
	for (size_t i = 0; Same && i < Found.size(); ++i)
		Same = Found[i].Address == m_Addresses[i].Address && Found[i].Adapter == m_Addresses[i].Adapter;
	if (Same)
		return false;
	CComboBox *pList = static_cast<CComboBox*>(GetDlgItem(IDC_PHONE_NETWORKS));
	const int Picked = pList->GetCurSel();
	const std::string Was = Picked >= 0 && Picked < static_cast<int>(m_Addresses.size()) ? m_Addresses[static_cast<size_t>(Picked)].Address :
		std::string();
	m_Addresses = Found;
	m_iHome = Home;
	pList->ResetContent();
	int Keep = 0;
	for (size_t i = 0; i < m_Addresses.size(); ++i) {
		const CString Adapter(m_Addresses[i].Adapter.c_str());
		pList->AddString((Adapter.IsEmpty() ? CString(_T("Network")) : Adapter) + _T(": ") + CString(m_Addresses[i].Address.c_str()));
		if (m_Addresses[i].Address == Was)
			Keep = static_cast<int>(i);
	}
	pList->SetCurSel(m_Addresses.empty() ? -1 : Keep);
	pList->EnableWindow(m_Addresses.size() > 1);
	return true;
}

void CPhoneDlg::ShowAddress()
{
	bool Listening = false, Network = false;
	int Port = 0;
	std::string Token;
	CMcpConnection *pConnection = m_pMainFrame->GetMcpConnection();
	if (pConnection != NULL)
		pConnection->GetAccess(Listening, Network, Port, Token);
	const int Picked = static_cast<CComboBox*>(GetDlgItem(IDC_PHONE_NETWORKS))->GetCurSel();
	std::string Url;
	if (Picked >= 0 && Picked < static_cast<int>(m_Addresses.size()) && !Token.empty())
		Url = ftapi::PhonePageUrl(m_Addresses[static_cast<size_t>(Picked)].Address, Port, Token);
	const bool Reachable = Listening && Network && !Url.empty();

	const bool ByName = Picked >= static_cast<int>(m_iHome);
	CString Hint;
	if (!Listening || !Network)
		Hint = _T("Your phone can't reach FamiTracker yet: it only listens to this PC.");
	else if (m_Addresses.empty())
		Hint = _T("This PC isn't on a home network right now: connect it to your Wi-Fi or router.");
	else if (m_bAllowed)
		Hint = _T("FamiTracker listens on your home network now. If Windows asks whether it may use the network, allow private ")
			_T("networks.");
	else if (ByName && m_iHome == 0)
		Hint = _T("No home network address right now, so the code uses this PC's name, which a phone with Tailscale finds.");
	else if (ByName)
		Hint = _T("A phone finds this PC by its name with Tailscale (or where your router knows it); at home the address is surer.");
	else
		Hint = _T("Your phone and this PC must be on the same home network (Wi-Fi).");
	CString Old;
	GetDlgItemText(IDC_PHONE_HINT, Old);
	if (Old != Hint)
		SetDlgItemText(IDC_PHONE_HINT, Hint);
	GetDlgItem(IDC_PHONE_ALLOW)->ShowWindow(!Listening || !Network ? SW_SHOW : SW_HIDE);
	GetDlgItem(IDC_PHONE_COPY)->EnableWindow(Reachable);

	const CString Text = Reachable ? CString(Url.c_str()) : CString();
	if (Text != m_sUrl || Reachable != m_bReachable) {
		m_sUrl = Text;
		m_bReachable = Reachable;
		SetDlgItemText(IDC_PHONE_ADDRESS, m_sUrl);
		static_cast<CEdit*>(GetDlgItem(IDC_PHONE_ADDRESS))->SetSel(0, 0);	// from its start, "http://..."
		m_Symbol = ftapi::QrSymbol();
		if (Reachable && !ftapi::MakeQrSymbol(Url, ftapi::QR_MEDIUM, m_Symbol))
			m_Symbol = ftapi::QrSymbol();
		GetDlgItem(IDC_PHONE_QR)->Invalidate();
	}
}

void CPhoneDlg::ShowTakes()
{
	ftapi::PhoneTakes *pTakes = GetPhoneTakes(m_pMainFrame);
	CString Text;
	if (pTakes != NULL) {
		const std::vector<ftapi::PhoneTake> &Takes = pTakes->Takes();
		int Shown = 0;
		for (size_t i = Takes.size(); i-- > 0 && Shown < TAKES_SHOWN; ++Shown) {
			const ftapi::PhoneTake &Take = Takes[i];
			const std::string Name = !Take.Clip.empty() ? Take.Clip : !Take.Idea.empty() ? Take.Idea : std::string("Idea");
			const LPCTSTR State = Take.State == ftapi::PHONE_CLIP ? _T("in the clips") :
				Take.State == ftapi::PHONE_FAILED ? _T("kept in Recordings, no clip") : _T("waiting");
			if (!Text.IsEmpty())
				Text += _T(", ");
			Text += FromUtf8(Name) + _T(" (") + State + _T(")");
		}
		if (Takes.size() > static_cast<size_t>(TAKES_SHOWN))
			Text += _T("...");
	}
	Text = Text.IsEmpty() ? CString(_T("No ideas have arrived yet.")) : _T("Ideas from your phone: ") + Text;
	if (Text != m_sTakes) {
		m_sTakes = Text;
		SetDlgItemText(IDC_PHONE_TAKES, Text);
	}
}

void CPhoneDlg::OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT pDraw)
{
	if (nIDCtl != IDC_PHONE_QR) {
		CDialog::OnDrawItem(nIDCtl, pDraw);
		return;
	}
	CDC *pDC = CDC::FromHandle(pDraw->hDC);
	const CRect Rect(pDraw->rcItem);
	if (!m_bReachable || m_Symbol.Size == 0) {
		pDC->FillSolidRect(Rect, ::GetSysColor(COLOR_3DFACE));
		pDC->Draw3dRect(Rect, ::GetSysColor(COLOR_3DSHADOW), ::GetSysColor(COLOR_3DSHADOW));
		CRect Area = Rect;
		Area.DeflateRect(10, Rect.Height() / 3, 10, 10);
		CFont *pOld = pDC->SelectObject(GetFont());
		pDC->SetBkMode(TRANSPARENT);
		pDC->SetTextColor(::GetSysColor(COLOR_GRAYTEXT));
		pDC->DrawText(_T("The code shows here once your phone can reach FamiTracker."), Area, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
		pDC->SelectObject(pOld);
		return;
	}
	// Dark modules on white, with the quiet zone of four modules around them that scanners need
	pDC->FillSolidRect(Rect, RGB(255, 255, 255));
	const int Modules = m_Symbol.Size + 8;
	const int Scale = std::max(1, std::min(Rect.Width(), Rect.Height()) / Modules);
	const int Left = Rect.left + (Rect.Width() - Scale * Modules) / 2 + 4 * Scale;
	const int Top = Rect.top + (Rect.Height() - Scale * Modules) / 2 + 4 * Scale;
	for (int y = 0; y < m_Symbol.Size; ++y)
		for (int x = 0; x < m_Symbol.Size; ++x)
			if (m_Symbol.Dark(x, y))
				pDC->FillSolidRect(Left + x * Scale, Top + y * Scale, Scale, Scale, RGB(0, 0, 0));
}

void CPhoneDlg::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == TMR_REFRESH) {
		if (++m_iTicks % ADDRESS_TICKS == 0)
			ReadAddresses();
		ShowAddress();
		ShowTakes();
		return;
	}
	CDialog::OnTimer(nIDEvent);
}

void CPhoneDlg::OnNetworkPicked()
{
	ShowAddress();
}

void CPhoneDlg::OnAllow()
{
	CMcpConnection *pConnection = m_pMainFrame->GetMcpConnection();
	std::string Error;
	if (pConnection == NULL || !pConnection->AllowNetwork(Error)) {
		SetDlgItemText(IDC_PHONE_HINT, _T("FamiTracker couldn't listen on your network: ") + CString(Error.c_str()));
		return;
	}
	m_bAllowed = true;
	ReadAddresses();
	ShowAddress();
}

void CPhoneDlg::OnCopy()
{
	if (m_sUrl.IsEmpty() || !OpenClipboard())
		return;
	EmptyClipboard();
	const SIZE_T Size = static_cast<SIZE_T>(m_sUrl.GetLength() + 1);
	HGLOBAL hData = ::GlobalAlloc(GMEM_MOVEABLE, Size);
	if (hData != NULL) {
		void *pData = ::GlobalLock(hData);
		if (pData != NULL) {
			memcpy(pData, static_cast<LPCTSTR>(m_sUrl), Size);
			::GlobalUnlock(hData);
			if (::SetClipboardData(CF_TEXT, hData) == NULL)
				::GlobalFree(hData);
		}
		else
			::GlobalFree(hData);
	}
	CloseClipboard();
}

void ShowPhoneWindow(CMainFrame *pMainFrame)
{
	if (CPhoneDlg::s_pOpen != NULL) {
		CPhoneDlg::s_pOpen->ShowWindow(SW_SHOW);
		CPhoneDlg::s_pOpen->SetActiveWindow();
		return;
	}
	CPhoneDlg *pDialog = new CPhoneDlg(pMainFrame);
	if (!pDialog->Create(IDD_PHONE, pMainFrame)) {
		delete pDialog;
		return;
	}
	CPhoneDlg::s_pOpen = pDialog;
	pDialog->ShowWindow(SW_SHOW);
}

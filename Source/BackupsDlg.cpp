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

// File > Backups... (see BackupsDlg.h)

// Standard headers come before stdafx.h, whose DEBUG_NEW macro would
// otherwise rewrite the placement new inside the standard library.
#include <string>
#include <vector>
#include "stdafx.h"
#include "Api/Backups.h"
#include "Api/Json.h"
#include "FamiTracker.h"
#include "FamiTrackerDoc.h"
#include "MainFrm.h"
#include "McpConnection.h"
#include "BackupsDlg.h"

namespace {

// Text between the tools (UTF-8) and the dialog (the ANSI code page)
std::string BackupUtf8(const CString &Text)
{
	if (Text.IsEmpty())
		return std::string();
	const int Wide = MultiByteToWideChar(CP_ACP, 0, Text, -1, NULL, 0);
	if (Wide <= 0)
		return std::string();
	std::vector<wchar_t> Buffer(static_cast<size_t>(Wide));
	MultiByteToWideChar(CP_ACP, 0, Text, -1, &Buffer[0], Wide);
	const int Bytes = WideCharToMultiByte(CP_UTF8, 0, &Buffer[0], -1, NULL, 0, NULL, NULL);
	if (Bytes <= 0)
		return std::string();
	std::vector<char> Out(static_cast<size_t>(Bytes));
	WideCharToMultiByte(CP_UTF8, 0, &Buffer[0], -1, &Out[0], Bytes, NULL, NULL);
	return std::string(&Out[0]);
}

CString BackupAnsi(const std::string &Text)
{
	if (Text.empty())
		return CString();
	const int Wide = MultiByteToWideChar(CP_UTF8, 0, Text.c_str(), -1, NULL, 0);
	if (Wide <= 0)
		return CString();
	std::vector<wchar_t> Buffer(static_cast<size_t>(Wide));
	MultiByteToWideChar(CP_UTF8, 0, Text.c_str(), -1, &Buffer[0], Wide);
	const int Bytes = WideCharToMultiByte(CP_ACP, 0, &Buffer[0], -1, NULL, 0, "?", NULL);
	if (Bytes <= 0)
		return CString();
	std::vector<char> Out(static_cast<size_t>(Bytes));
	WideCharToMultiByte(CP_ACP, 0, &Buffer[0], -1, &Out[0], Bytes, "?", NULL);
	return CString(&Out[0]);
}

// A reply's line breaks as an edit control shows them
CString EditText(const std::string &Reply)
{
	CString Text = BackupAnsi(Reply);
	Text.Replace(_T("\r\n"), _T("\n"));
	Text.Replace(_T("\n"), _T("\r\n"));
	return Text;
}

}	// namespace

class CBackupsDlg : public CDialog
{
public:
	CBackupsDlg(CMainFrame *pMainFrm, CMcpConnection *pTools) : CDialog(IDD_BACKUPS, pMainFrm), m_pMainFrame(pMainFrm),
		m_pTools(pTools) {}

protected:
	virtual BOOL OnInitDialog();
	afx_msg void OnCreate();
	afx_msg void OnRestore();
	afx_msg void OnOpenFolder();
	afx_msg void OnListDoubleClick(NMHDR *pNMHDR, LRESULT *pResult);
	afx_msg void OnListChanged(NMHDR *pNMHDR, LRESULT *pResult);

	DECLARE_MESSAGE_MAP()

private:
	void Refresh();
	void ShowReply(const std::string &Reply);
	bool Run(const char *pTool, const ftapi::Json &Arguments, std::string &Reply);
	CString Folder() const;

	CMainFrame *m_pMainFrame;
	CMcpConnection *m_pTools;
	CListCtrl m_cList;
	std::vector<int> m_Numbers;		// the backups' numbers, row by row
	bool m_bHasFile = false;
};

BEGIN_MESSAGE_MAP(CBackupsDlg, CDialog)
	ON_BN_CLICKED(IDC_BACKUPS_CREATE, OnCreate)
	ON_BN_CLICKED(IDC_BACKUPS_RESTORE, OnRestore)
	ON_BN_CLICKED(IDC_BACKUPS_FOLDER, OnOpenFolder)
	ON_NOTIFY(NM_DBLCLK, IDC_BACKUPS_LIST, OnListDoubleClick)
	ON_NOTIFY(LVN_ITEMCHANGED, IDC_BACKUPS_LIST, OnListChanged)
END_MESSAGE_MAP()

BOOL CBackupsDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	m_cList.SubclassDlgItem(IDC_BACKUPS_LIST, this);
	m_cList.SetExtendedStyle(m_cList.GetExtendedStyle() | LVS_EX_FULLROWSELECT);
	CRect Rect;
	m_cList.GetClientRect(&Rect);
	const int Number = Rect.Width() / 10, Time = Rect.Width() * 3 / 10;
	m_cList.InsertColumn(0, _T("#"), LVCFMT_RIGHT, Number);
	m_cList.InsertColumn(1, _T("Time"), LVCFMT_LEFT, Time);
	m_cList.InsertColumn(2, _T("Label"), LVCFMT_LEFT, Rect.Width() - Number - Time - GetSystemMetrics(SM_CXVSCROLL));
	static_cast<CEdit*>(GetDlgItem(IDC_BACKUPS_LABEL))->LimitText(static_cast<UINT>(ftapi::MAX_BACKUP_LABEL));
	Refresh();
	GetDlgItem(IDC_BACKUPS_LABEL)->SetFocus();
	return FALSE;	// the focus is set
}

bool CBackupsDlg::Run(const char *pTool, const ftapi::Json &Arguments, std::string &Reply)
{
	CWaitCursor Wait;
	return m_pTools->RunTool(pTool, ftapi::WriteJson(Arguments), Reply, true);
}

CString CBackupsDlg::Folder() const
{
	CFamiTrackerDoc *pDoc = CFamiTrackerDoc::GetDoc();
	const CString Song = pDoc != NULL ? pDoc->GetPathName() : CString();
	if (Song.IsEmpty())
		return CString();
	return Song.Left(Song.ReverseFind(_T('\\')) + 1) + BackupAnsi(ftapi::BACKUPS_FOLDER);
}

void CBackupsDlg::ShowReply(const std::string &Reply)
{
	SetDlgItemText(IDC_BACKUPS_RESULT, EditText(Reply));
}

// The list again, from list_backups (format json), the newest first
void CBackupsDlg::Refresh()
{
	m_cList.DeleteAllItems();
	m_Numbers.clear();
	ftapi::Json Arguments = ftapi::Json::MakeObject();
	Arguments.Set("format", "json");
	std::string Reply;
	ftapi::Json List;
	m_bHasFile = Run("list_backups", Arguments, Reply) && ftapi::ParseJson(Reply, List) && List.IsObject();
	CFamiTrackerDoc *pDoc = CFamiTrackerDoc::GetDoc();
	CString Song = pDoc != NULL ? pDoc->GetTitle() : CString();
	if (!m_bHasFile) {
		SetDlgItemText(IDC_BACKUPS_SONG, _T("This song has no file yet, so it has no backups. Save it first (File > Save As): its ")
			_T("backups then go into a Backups folder beside it."));
		ShowReply(Reply);
	}
	else {
		const ftapi::Json *pItems = List.Get("backups");
		for (size_t i = 0; pItems != NULL && pItems->IsArray() && i < pItems->Size(); ++i) {
			const ftapi::Json &Item = pItems->At(i);
			const ftapi::Json *pNumber = Item.Get("number"), *pTime = Item.Get("time"), *pLabel = Item.Get("label");
			if (pNumber == NULL || !pNumber->IsNumber())
				continue;
			const int Number = static_cast<int>(pNumber->Number());
			CString Text;
			Text.Format(_T("%d"), Number);
			const int Row = m_cList.InsertItem(m_cList.GetItemCount(), Text);
			m_cList.SetItemText(Row, 1, pTime != NULL && pTime->IsString() ? BackupAnsi(pTime->String()) : CString());
			m_cList.SetItemText(Row, 2, pLabel != NULL && pLabel->IsString() ? BackupAnsi(pLabel->String()) : CString());
			m_Numbers.push_back(Number);
		}
		CString Heading;
		if (m_Numbers.empty())
			Heading.Format(_T("%s has no backups yet. Type why you keep one and click Create; Restore brings one back later."),
				static_cast<LPCTSTR>(Song));
		else
			Heading.Format(_T("Backups of %s, the newest first, in the Backups folder beside it. Restore keeps a backup of the song as ")
				_T("it is first, then opens the one picked; the song keeps its own file."), static_cast<LPCTSTR>(Song));
		SetDlgItemText(IDC_BACKUPS_SONG, Heading);
	}
	GetDlgItem(IDC_BACKUPS_CREATE)->EnableWindow(m_bHasFile);
	GetDlgItem(IDC_BACKUPS_LABEL)->EnableWindow(m_bHasFile);
	GetDlgItem(IDC_BACKUPS_RESTORE)->EnableWindow(FALSE);
	GetDlgItem(IDC_BACKUPS_FOLDER)->EnableWindow(m_bHasFile && GetFileAttributes(Folder()) != INVALID_FILE_ATTRIBUTES);
}

void CBackupsDlg::OnListChanged(NMHDR *pNMHDR, LRESULT *pResult)
{
	(void)pNMHDR;
	GetDlgItem(IDC_BACKUPS_RESTORE)->EnableWindow(m_cList.GetSelectedCount() == 1);
	*pResult = 0;
}

void CBackupsDlg::OnListDoubleClick(NMHDR *pNMHDR, LRESULT *pResult)
{
	(void)pNMHDR;
	*pResult = 0;
	if (m_cList.GetSelectedCount() == 1)
		OnRestore();
}

void CBackupsDlg::OnCreate()
{
	CString Label;
	GetDlgItemText(IDC_BACKUPS_LABEL, Label);
	Label.Trim();
	if (Label.IsEmpty()) {
		ShowReply("Type why you keep this backup first, like \"before messing with the intro\": it names the backup in the list.");
		GetDlgItem(IDC_BACKUPS_LABEL)->SetFocus();
		return;
	}
	ftapi::Json Arguments = ftapi::Json::MakeObject();
	Arguments.Set("label", BackupUtf8(Label));
	std::string Reply;
	const bool Kept = Run("create_backup", Arguments, Reply);
	Refresh();
	ShowReply(Reply);
	if (Kept)
		SetDlgItemText(IDC_BACKUPS_LABEL, _T(""));
}

void CBackupsDlg::OnRestore()
{
	const int Row = m_cList.GetNextItem(-1, LVNI_SELECTED);
	if (Row < 0 || Row >= static_cast<int>(m_Numbers.size()))
		return;
	CString Question;
	Question.Format(_T("Restore backup %d, \"%s\"?\n\nThe song as it is now is kept as a backup first, so you can go back to it. The ")
		_T("song keeps its own file: saving puts the restored song there. Undo starts afresh."), m_Numbers[static_cast<size_t>(Row)],
		static_cast<LPCTSTR>(m_cList.GetItemText(Row, 2)));
	if (AfxMessageBox(Question, MB_YESNO | MB_ICONQUESTION) != IDYES)
		return;
	ftapi::Json Arguments = ftapi::Json::MakeObject();
	CString Number;
	Number.Format(_T("%d"), m_Numbers[static_cast<size_t>(Row)]);
	Arguments.Set("backup", std::string(static_cast<LPCTSTR>(Number)));
	std::string Reply;
	const bool Restored = Run("restore_backup", Arguments, Reply);
	if (Restored)
		m_pMainFrame->SongChanged();
	Refresh();
	ShowReply(Reply);
}

void CBackupsDlg::OnOpenFolder()
{
	const CString Path = Folder();
	if (Path.IsEmpty() || GetFileAttributes(Path) == INVALID_FILE_ATTRIBUTES) {
		ShowReply("There's no Backups folder yet: Create keeps the first backup in it.");
		return;
	}
	ShellExecute(NULL, _T("open"), Path, NULL, NULL, SW_SHOWNORMAL);
}

void ShowBackups(CMainFrame *pMainFrm)
{
	CMcpConnection *pTools = pMainFrm != NULL ? pMainFrm->GetMcpConnection() : NULL;
	if (pTools == NULL || CFamiTrackerDoc::GetDoc() == NULL)
		return;
	CBackupsDlg Dialog(pMainFrm, pTools);
	Dialog.DoModal();
}

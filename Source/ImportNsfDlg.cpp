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

// File > Import NSF (ImportNsfDlg.h)

// Standard and importer headers come before stdafx.h, whose DEBUG_NEW macro
// would otherwise rewrite the placement new inside the standard library.
#include <string>
#include <vector>
#include "Nsf/Import.h"
#include "Nsf/NsfFile.h"
#include "Nsf/Study.h"
#include "stdafx.h"
#include "FamiTracker.h"
#include "FamiTrackerDoc.h"
#include "MainFrm.h"
#include "SoundGen.h"
#include "TextExporter.h"
#include "ImportNsfDlg.h"

namespace {

// "1:30"
CString Duration(double Seconds)
{
	const int Whole = static_cast<int>(Seconds + 0.5);
	CString Text;
	Text.Format(_T("%d:%02d"), Whole / 60, Whole % 60);
	return Text;
}

class CImportNsfDlg : public CDialog
{
public:
	CImportNsfDlg(const nsf::NsfFile &File, const std::vector<nsf::TrackSurvey> &Survey, CWnd *pParent) :
		CDialog(IDD_IMPORT_NSF, pParent), m_File(File), m_Survey(Survey) {}

	std::vector<int> m_Chosen;		// the tracks to import, from 0

protected:
	virtual BOOL OnInitDialog();
	virtual void OnOK();
	afx_msg void OnMusic() { Check(0); }
	afx_msg void OnAll() { Check(1); }
	afx_msg void OnNone() { Check(2); }

	DECLARE_MESSAGE_MAP()

private:
	void Check(int Which);		// 0: the music, 1: every track, 2: none

	const nsf::NsfFile &m_File;
	const std::vector<nsf::TrackSurvey> &m_Survey;
	CCheckListBox m_Tracks;
};

BEGIN_MESSAGE_MAP(CImportNsfDlg, CDialog)
	ON_BN_CLICKED(IDC_NSF_MUSIC, OnMusic)
	ON_BN_CLICKED(IDC_NSF_ALL, OnAll)
	ON_BN_CLICKED(IDC_NSF_NONE, OnNone)
END_MESSAGE_MAP()

BOOL CImportNsfDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	m_Tracks.SubclassDlgItem(IDC_NSF_TRACKS, this);
	CString About(m_File.Title.empty() ? _T("This NSF") : CString(_T("\"")) + CString(m_File.Title.c_str()) + _T("\""));
	if (!m_File.Artist.empty())
		About += _T(" by ") + CString(m_File.Artist.c_str());
	CString Count;
	Count.Format(_T(" has %d tracks."), m_File.Songs);
	About += Count + _T(" The ones that are music are ticked (the others are jingles and sound effects). Each becomes a track ")
		_T("of the new module, with its patterns, instruments and drums worked out from what plays; a track that plays for a ")
		_T("long time takes a few seconds.");
	SetDlgItemText(IDC_NSF_INFO, About);
	for (size_t i = 0; i < m_Survey.size(); ++i) {
		const nsf::TrackSurvey &t = m_Survey[i];
		CString Line;
		Line.Format(_T("%d. %s  (%s"), t.Track + 1, CString(t.Title.c_str()).GetString(), CString(t.Kind.c_str()).GetString());
		if (t.Seconds > 0)
			Line += _T(", ") + Duration(t.Seconds) + (t.LoopSeconds > 0 ? _T(" before it loops") : _T(""));
		Line += _T(")");
		m_Tracks.AddString(Line);
	}
	Check(0);
	return TRUE;
}

void CImportNsfDlg::Check(int Which)
{
	for (size_t i = 0; i < m_Survey.size(); ++i)
		m_Tracks.SetCheck(static_cast<int>(i), Which == 1 || (Which == 0 && m_Survey[i].Kind == "music") ? BST_CHECKED : BST_UNCHECKED);
}

void CImportNsfDlg::OnOK()
{
	m_Chosen.clear();
	for (size_t i = 0; i < m_Survey.size(); ++i)
		if (m_Tracks.GetCheck(static_cast<int>(i)) == BST_CHECKED)
			m_Chosen.push_back(m_Survey[i].Track);
	if (m_Chosen.empty()) {
		AfxMessageBox(_T("Tick the tracks to import."), MB_ICONINFORMATION);
		return;
	}
	if (static_cast<int>(m_Chosen.size()) > static_cast<int>(MAX_TRACKS)) {
		CString Text;
		Text.Format(_T("A module holds at most %d tracks; untick some."), static_cast<int>(MAX_TRACKS));
		AfxMessageBox(Text, MB_ICONINFORMATION);
		return;
	}
	CDialog::OnOK();
}

}	// namespace

CString OpenModuleText(CMainFrame *pMain, CFamiTrackerDoc *pDoc, const CString &Text, const CString &Title)
{
	if (theApp.IsPlaying())
		theApp.StopPlayerAndWait();
	CTextExport Importer;
	const CString Problem = Importer.ImportText(Text, pDoc);
	pMain->SetSongInfo(pDoc->GetSongName(), pDoc->GetSongArtist(), pDoc->GetSongCopyright());
	const CString Name = Title.IsEmpty() ? CString(pDoc->GetSongName()) : Title;
	if (!Name.IsEmpty())
		pDoc->SetTitle(Name);
	pDoc->SetModifiedFlag(TRUE);		// a new module, not saved yet: FamiTracker asks before it's closed
	pDoc->UpdateAllViews(NULL, UPDATE_PROPERTIES);
	pDoc->UpdateAllViews(NULL, UPDATE_TRACK);
	pDoc->UpdateAllViews(NULL, UPDATE_INSTRUMENT);
	theApp.GetSoundGenerator()->DocumentPropertiesChanged(pDoc);
	pMain->SelectTrack(0);
	return Problem;
}

void ImportNsfFile(CMainFrame *pMain)
{
	CFamiTrackerDoc *pDoc = static_cast<CFamiTrackerDoc*>(pMain->GetActiveDocument());
	if (pDoc == NULL || pDoc->SaveModified() == 0)
		return;
	CFileDialog FileDialog(TRUE, NULL, NULL, OFN_HIDEREADONLY | OFN_FILEMUSTEXIST,
		_T("NSF files (*.nsf, *.nsfe)|*.nsf;*.nsfe|All files (*.*)|*.*||"), pMain);
	if (FileDialog.DoModal() != IDOK)
		return;

	nsf::NsfFile File;
	std::string Error;
	std::vector<nsf::TrackSurvey> Survey;
	{
		CWaitCursor Wait;
		if (!nsf::ReadNsfFile(std::string(static_cast<LPCSTR>(CStringA(FileDialog.GetPathName()))), File, Error)) {
			AfxMessageBox(_T("This file can't be imported: ") + CString(Error.c_str()) + _T("."), MB_ICONEXCLAMATION);
			return;
		}
		Survey = nsf::SurveyTracks(File);
	}
	CImportNsfDlg Dialog(File, Survey, pMain);
	if (Dialog.DoModal() != IDOK)
		return;

	nsf::ImportOptions Options;
	Options.Tracks = Dialog.m_Chosen;
	Options.Progress = [pMain](int Done, int Total, const std::string &Track) {
		CString Text;
		Text.Format(_T("Importing track %d of %d: %s"), Done + 1, Total, CString(Track.c_str()).GetString());
		pMain->SetMessageText(Text);
		CWnd *pBar = pMain->GetMessageBar();
		if (pBar != NULL)
			pBar->UpdateWindow();
		return true;
	};
	nsf::ImportResult Result;
	bool Imported;
	{
		CWaitCursor Wait;
		Imported = nsf::ImportNsf(File, Options, Result, Error);
	}
	pMain->SetMessageText(AFX_IDS_IDLEMESSAGE);
	if (!Imported) {
		AfxMessageBox(_T("The tracks couldn't be imported: ") + CString(Error.c_str()) + _T("."), MB_ICONEXCLAMATION);
		return;
	}
	CString Title(File.Title.c_str());
	if (Title.IsEmpty())
		Title = FileDialog.GetFileTitle();
	const CString Problem = OpenModuleText(pMain, pDoc, CString(Result.Text.c_str()), Title);
	if (!Problem.IsEmpty()) {
		AfxMessageBox(_T("FamiTracker couldn't read the imported module: ") + Problem, MB_ICONEXCLAMATION);
		return;
	}
	CString Summary(Result.Summary.c_str());
	Summary.Replace(_T("\n"), _T("\r\n"));
	AfxMessageBox(Summary + _T("\r\nEach is a track of its own: pick one in the track list at the top. The module isn't saved yet."),
		MB_ICONINFORMATION);
}

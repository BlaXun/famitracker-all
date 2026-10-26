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

// Help > Exercises (see ExercisesDlg.h)

// Standard headers come before stdafx.h, whose DEBUG_NEW macro would
// otherwise rewrite the placement new inside the standard library.
#include <algorithm>
#include <string>
#include <vector>
#include "TutorialScript.h"
#include "stdafx.h"
#include "FamiTracker.h"
#include "FamiTrackerDoc.h"
#include "MainFrm.h"
#include "Action.h"
#include "McpConnection.h"
#include "BarsPlayer.h"
#include "Tutorial.h"
#include "ExercisesDlg.h"
#include "Api/Exercises.h"
#include "Api/Json.h"

namespace {

const LPCTSTR SECTION = _T("Exercises");
const UINT_PTR TMR_HEAR = 1;
const UINT HEAR_MS = 100;

// Each exercise's starter song and script in FamiTracker.exe; no song for those that start from File > New
struct exercise_files_t {
	const char *Id;
	UINT Song;
	UINT Script;
};

const exercise_files_t FILES[] = {
	{ "home-note", IDR_EXERCISE_HOME_SONG, IDR_EXERCISE_HOME_SCRIPT },
	{ "four-bars", IDR_EXERCISE_FOURBARS_SONG, IDR_EXERCISE_FOURBARS_SCRIPT },
	{ "strong-notes", IDR_EXERCISE_STRONG_SONG, IDR_EXERCISE_STRONG_SCRIPT },
	{ "phrases", IDR_EXERCISE_PHRASES_SONG, IDR_EXERCISE_PHRASES_SCRIPT },
	{ "bass", IDR_EXERCISE_BASS_SONG, IDR_EXERCISE_BASS_SCRIPT },
	{ "beat", IDR_EXERCISE_BEAT_SONG, IDR_EXERCISE_BEAT_SCRIPT },
	{ "minor", IDR_EXERCISE_MINOR_SONG, IDR_EXERCISE_MINOR_SCRIPT },
	{ "hook", IDR_EXERCISE_HOOK_SONG, IDR_EXERCISE_HOOK_SCRIPT },
	{ "song-shape", 0, IDR_EXERCISE_SHAPE_SCRIPT },
	{ "first-song", 0, IDR_EXERCISE_FIRSTSONG_SCRIPT },
};

const exercise_files_t *FilesOf(const std::string &Id)
{
	for (size_t i = 0; i < sizeof(FILES) / sizeof(FILES[0]); ++i)
		if (Id == FILES[i].Id)
			return &FILES[i];
	return NULL;
}

CFamiTrackerDoc *DocOf(CMainFrame *pMainFrame)
{
	return static_cast<CFamiTrackerDoc*>(pMainFrame->GetActiveDocument());
}

// "1. The home note (done)"
CString ListLine(size_t Index, const ftapi::ExerciseInfo &E)
{
	CString Line;
	Line.Format(_T("%d. %s"), static_cast<int>(Index + 1), static_cast<LPCTSTR>(CString(E.Title)));
	const int State = GetExerciseState(E.Id);
	if (State == EXERCISE_DONE)
		Line += _T("   (done)");
	else if (State == EXERCISE_STARTED)
		Line += _T("   (started)");
	return Line;
}

}	// namespace

int GetExerciseState(const std::string &Id)
{
	const int State = static_cast<int>(theApp.GetProfileInt(SECTION, CString(Id.c_str()), EXERCISE_NOT_TRIED));
	return std::max(static_cast<int>(EXERCISE_NOT_TRIED), std::min(static_cast<int>(EXERCISE_DONE), State));
}

void SetExerciseState(const std::string &Id, int State)
{
	if (GetExerciseState(Id) == EXERCISE_DONE && State != EXERCISE_NOT_TRIED)
		return;
	theApp.WriteProfileInt(SECTION, CString(Id.c_str()), State);
}

UINT ExerciseCommand(const std::string &Id)
{
	const std::vector<ftapi::ExerciseInfo> &List = ftapi::Exercises();
	for (size_t i = 0; i < List.size(); ++i)
		if (Id == List[i].Id && ID_HELP_EXERCISE_FIRST + i <= static_cast<size_t>(ID_HELP_EXERCISE_LAST))
			return static_cast<UINT>(ID_HELP_EXERCISE_FIRST + i);
	return 0;
}

// The list

class CExercisesDlg : public CDialog
{
public:
	explicit CExercisesDlg(CWnd *pParent) : CDialog(IDD_EXERCISES, pParent), m_iChosen(-1) {}
	int Chosen() const { return m_iChosen; }

protected:
	virtual BOOL OnInitDialog();
	virtual void OnOK();
	afx_msg void OnSelChange();
	afx_msg void OnForget();

	DECLARE_MESSAGE_MAP()

private:
	void Fill(int Select);

	int m_iChosen;
};

BEGIN_MESSAGE_MAP(CExercisesDlg, CDialog)
	ON_LBN_SELCHANGE(IDC_EXERCISE_LIST, OnSelChange)
	ON_LBN_DBLCLK(IDC_EXERCISE_LIST, OnOK)
	ON_BN_CLICKED(IDC_EXERCISE_FORGET, OnForget)
END_MESSAGE_MAP()

BOOL CExercisesDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	// The first one not done yet
	const std::vector<ftapi::ExerciseInfo> &List = ftapi::Exercises();
	int First = 0;
	while (First + 1 < static_cast<int>(List.size()) && GetExerciseState(List[static_cast<size_t>(First)].Id) == EXERCISE_DONE)
		++First;
	Fill(First);
	return TRUE;
}

void CExercisesDlg::Fill(int Select)
{
	CListBox *pList = static_cast<CListBox*>(GetDlgItem(IDC_EXERCISE_LIST));
	pList->ResetContent();
	const std::vector<ftapi::ExerciseInfo> &List = ftapi::Exercises();
	int Done = 0;
	for (size_t i = 0; i < List.size(); ++i) {
		pList->AddString(ListLine(i, List[i]));
		Done += GetExerciseState(List[i].Id) == EXERCISE_DONE ? 1 : 0;
	}
	pList->SetCurSel(Select);
	CString Progress;
	if (Done == static_cast<int>(List.size()))
		Progress = _T("All of them done: well done! Each one can be done again.");
	else
		Progress.Format(_T("%d of %d done."), Done, static_cast<int>(List.size()));
	SetDlgItemText(IDC_EXERCISE_PROGRESS, Progress);
	GetDlgItem(IDC_EXERCISE_FORGET)->EnableWindow(Done > 0 ? TRUE : FALSE);
	OnSelChange();
}

void CExercisesDlg::OnSelChange()
{
	const int Index = static_cast<CListBox*>(GetDlgItem(IDC_EXERCISE_LIST))->GetCurSel();
	const std::vector<ftapi::ExerciseInfo> &List = ftapi::Exercises();
	if (Index < 0 || Index >= static_cast<int>(List.size())) {
		SetDlgItemText(IDC_EXERCISE_ABOUT, _T(""));
		return;
	}
	const ftapi::ExerciseInfo &E = List[static_cast<size_t>(Index)];
	CString About;
	About.Format(_T("%s About %d minutes. %s"), static_cast<LPCTSTR>(CString(E.About)), E.Minutes,
		E.NewSong ? _T("It starts a new song, as File > New does.") : _T("It opens a short song made for it."));
	SetDlgItemText(IDC_EXERCISE_ABOUT, About);
}

void CExercisesDlg::OnForget()
{
	if (AfxMessageBox(_T("Forget which exercises are done? They all show as not tried again."), MB_YESNO | MB_ICONQUESTION) != IDYES)
		return;
	const std::vector<ftapi::ExerciseInfo> &List = ftapi::Exercises();
	for (size_t i = 0; i < List.size(); ++i)
		SetExerciseState(List[i].Id, EXERCISE_NOT_TRIED);
	Fill(0);
}

void CExercisesDlg::OnOK()
{
	const int Index = static_cast<CListBox*>(GetDlgItem(IDC_EXERCISE_LIST))->GetCurSel();
	if (Index < 0 || Index >= static_cast<int>(ftapi::Exercises().size()))
		return;
	m_iChosen = Index;
	CDialog::OnOK();
}

void ShowExercises(CMainFrame *pMainFrame)
{
	CExercisesDlg Dialog(pMainFrame);
	if (Dialog.DoModal() == IDOK && Dialog.Chosen() >= 0)
		StartExercise(pMainFrame, static_cast<UINT>(ID_HELP_EXERCISE_FIRST + Dialog.Chosen()));
}

void StartExercise(CMainFrame *pMainFrame, UINT nID)
{
	const std::vector<ftapi::ExerciseInfo> &List = ftapi::Exercises();
	if (nID < ID_HELP_EXERCISE_FIRST || nID - ID_HELP_EXERCISE_FIRST >= List.size())
		return;
	const ftapi::ExerciseInfo &E = List[nID - ID_HELP_EXERCISE_FIRST];
	const exercise_files_t *pFiles = FilesOf(E.Id);
	std::string Text, Error;
	tutorial_t Script;
	if (pFiles == NULL || !ReadBuiltInText(pFiles->Script, Text) || !ParseTutorial(Text, Script, Error)) {
		ASSERT(FALSE);
		return;
	}
	const CString Title = CString(E.Title) + _T(" (exercise)");
	if (E.NewSong) {
		// A new song, as File > New makes it (asking first whether to save the one that's open)
		CFamiTrackerDoc *pDoc = DocOf(pMainFrame);
		const unsigned int Before = pDoc != NULL ? pDoc->GetSongSerial() : 0;
		pMainFrame->SendMessage(WM_COMMAND, ID_FILE_NEW);
		pDoc = DocOf(pMainFrame);
		if (pDoc == NULL || pDoc->GetSongSerial() == Before)
			return;		// the song that was open stays
		pDoc->MakeUntitled(Title);
	}
	else if (!OpenBuiltInSong(pMainFrame, pFiles->Song, Title))
		return;
	SetExerciseState(E.Id, EXERCISE_STARTED);
	StartExerciseNote(pMainFrame, Script, E.Id);
}

// Hear it: the good answer in the song while its bars play

class CExerciseHearDlg : public CDialog
{
public:
	CExerciseHearDlg(CWnd *pParent, int FirstRow, int EndRow, int FirstBar, int LastBar, const CRect &Near) :
		CDialog(IDD_EXERCISE_HEAR, pParent), m_iFirstRow(FirstRow), m_iEndRow(EndRow), m_iFirstBar(FirstBar), m_iLastBar(LastBar),
		m_Near(Near), m_iShown(-2) {}

protected:
	virtual BOOL OnInitDialog();
	virtual void OnCancel();
	afx_msg void OnAgain();
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnDestroy();

	DECLARE_MESSAGE_MAP()

private:
	CString Bars() const;
	void ShowState();

	int m_iFirstRow, m_iEndRow, m_iFirstBar, m_iLastBar;
	CRect m_Near;		// the note, on the screen: the window stands beside it
	CBarsPlayer m_Player;
	int m_iShown;		// what the status says: 1 playing, -1 played
};

BEGIN_MESSAGE_MAP(CExerciseHearDlg, CDialog)
	ON_BN_CLICKED(IDC_EXERCISE_HEAR_AGAIN, OnAgain)
	ON_WM_TIMER()
	ON_WM_DESTROY()
END_MESSAGE_MAP()

CString CExerciseHearDlg::Bars() const
{
	CString Text;
	if (m_iLastBar > m_iFirstBar)
		Text.Format(_T("bars %d-%d"), m_iFirstBar, m_iLastBar);
	else
		Text.Format(_T("bar %d"), m_iFirstBar);
	return Text;
}

BOOL CExerciseHearDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	SetDlgItemText(IDC_EXERCISE_HEAR_TEXT, _T("A good answer is in the song for a moment: listen to ") + Bars() +
		_T(". When you press Done, it goes and your own notes come back."));
	// Beside the note (on its left, else under it), so the pattern editor and the piano roll show the answer as it plays
	CRect Own, Work;
	GetWindowRect(&Own);
	::SystemParametersInfo(SPI_GETWORKAREA, 0, &Work, 0);
	int X = m_Near.left - Own.Width() - SX(8), Y = m_Near.top;
	if (X < Work.left) {
		X = m_Near.left;
		Y = m_Near.bottom + SY(8);
	}
	X = std::max(static_cast<int>(Work.left), std::min(X, static_cast<int>(Work.right) - Own.Width()));
	Y = std::max(static_cast<int>(Work.top), std::min(Y, static_cast<int>(Work.bottom) - Own.Height()));
	SetWindowPos(NULL, X, Y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	SetTimer(TMR_HEAR, HEAR_MS, NULL);
	m_Player.Play(m_iFirstRow, m_iEndRow, std::vector<int>(), CBarsPlayer::MIX);
	ShowState();
	return TRUE;
}

void CExerciseHearDlg::ShowState()
{
	const int Now = m_Player.IsPlaying() ? 1 : -1;
	if (Now == m_iShown)
		return;
	m_iShown = Now;
	SetDlgItemText(IDC_EXERCISE_HEAR_STATUS, Now > 0 ? CString(_T("Playing ")) + Bars() + _T(" with a good answer.") :
		CString(_T("Played. Play again hears it once more; Done brings your own notes back.")));
	SetDlgItemText(IDC_EXERCISE_HEAR_AGAIN, Now > 0 ? _T("Stop") : _T("Play again"));
}

void CExerciseHearDlg::OnAgain()
{
	if (m_Player.IsPlaying())
		m_Player.Stop();
	else
		m_Player.Play(m_iFirstRow, m_iEndRow, std::vector<int>(), CBarsPlayer::MIX);
	ShowState();
}

void CExerciseHearDlg::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == TMR_HEAR) {
		m_Player.Tick();
		ShowState();
		return;
	}
	CDialog::OnTimer(nIDEvent);
}

void CExerciseHearDlg::OnCancel()
{
	m_Player.Stop();
	CDialog::OnCancel();
}

void CExerciseHearDlg::OnDestroy()
{
	KillTimer(TMR_HEAR);
	if (m_Player.IsPlaying())
		m_Player.Stop();
	CDialog::OnDestroy();
}

bool HearGoodAnswer(CMainFrame *pMainFrame, const tutorial_step_t &Step, const CRect &Near)
{
	CMcpConnection *pTools = pMainFrame->GetMcpConnection();
	CFamiTrackerDoc *pDoc = DocOf(pMainFrame);
	int FirstBar = 0, LastBar = 0;
	if (pTools == NULL || pDoc == NULL || !pDoc->IsFileLoaded() || Step.Answers.empty() || !Step.HearBars(FirstBar, LastBar))
		return false;
	if (pTools->IsBusy()) {
		pMainFrame->SetMessageText(_T("FamiTracker is busy with Claude Code; try Hear it again in a moment."));
		return false;
	}
	const unsigned int Track = static_cast<unsigned int>(std::max(0, pMainFrame->GetSelectedTrack()));
	const ftapi::Timing Time(static_cast<int>(pDoc->GetPatternLength(Track)), static_cast<int>(pDoc->GetFrameCount(Track)),
		pDoc->GetFirstHighlight(), pDoc->GetSecondHighlight());
	const int FirstRow = Time.BarStart(FirstBar), EndRow = std::min(Time.BarEnd(LastBar), Time.TotalRows());
	if (FirstRow >= EndRow) {
		pMainFrame->SetMessageText(_T("The song is shorter than the good answer now."));
		return false;
	}
	if (theApp.IsPlaying())
		theApp.StopPlayerAndWait();

	// The answer, as one undoable change
	const long long Before = pMainFrame->GetActionSerial();
	bool Written = true;
	std::string Reply;
	pMainFrame->BeginWaitCursor();
	pMainFrame->BeginActionGroup();
	for (size_t i = 0; i < Step.Answers.size() && Written; ++i) {
		const tutorial_answer_t &A = Step.Answers[i];
		ftapi::Json Args = ftapi::Json::MakeObject();
		Args.Set("channel", A.Channel);
		Args.Set("from_bar", A.Bar);
		Args.Set("notes", A.Notes);
		if (!A.Instrument.empty())
			Args.Set("instrument", A.Instrument);
		Written = pTools->RunTool("write_notes", ftapi::WriteJson(Args), Reply);
	}
	pMainFrame->EndActionGroup();
	pMainFrame->EndWaitCursor();
	const bool Made = pMainFrame->GetActionSerial() != Before;
	if (!Written) {
		if (Made) {		// what it wrote before it failed, and nothing to redo of it
			pMainFrame->SendMessage(WM_COMMAND, ID_EDIT_UNDO);
			pMainFrame->ForgetRedo();
		}
		pMainFrame->SetMessageText(_T("The good answer can't be played here: ") + CString(Reply.c_str()));
		return false;
	}
	CAction *pAnswer = Made ? pMainFrame->GetUndoAction() : NULL;
	if (pAnswer != NULL)
		pAnswer->SetLabel("write_notes: a good answer to hear for a moment (Help > Exercises)");
	const long long After = pMainFrame->GetActionSerial();

	CExerciseHearDlg Dialog(pMainFrame, FirstRow, EndRow, FirstBar, LastBar, Near);
	Dialog.DoModal();

	// The song as it was: the answer undone, and nothing of it for Ctrl+Y to bring back
	if (Made && pMainFrame->GetActionSerial() == After && pMainFrame->GetUndoAction() == pAnswer) {
		pMainFrame->SendMessage(WM_COMMAND, ID_EDIT_UNDO);
		pMainFrame->ForgetRedo();
		pMainFrame->SetMessageText(_T("Your own notes are back. Now it's your turn."));
	}
	return true;
}

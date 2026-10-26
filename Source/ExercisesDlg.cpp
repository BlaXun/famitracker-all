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

// Each exercise's starter song and script in FamiTracker.exe (RCDATA): Next steps' by number (IDR_EXERCISE_*), First
// steps' by name (FIRSTSTEPS_*_SONG and _SCRIPT in FamiTracker.rc, which keeps resource.h's numbers for others); no song
// for those that start from File > New
struct exercise_files_t {
	const char *Id;
	LPCTSTR Song;
	LPCTSTR Script;
};

const exercise_files_t FILES[] = {
	{ "first-note", _T("FIRSTSTEPS_NOTE_SONG"), _T("FIRSTSTEPS_NOTE_SCRIPT") },
	{ "high-low", _T("FIRSTSTEPS_HIGHLOW_SONG"), _T("FIRSTSTEPS_HIGHLOW_SCRIPT") },
	{ "long-short", _T("FIRSTSTEPS_LONGSHORT_SONG"), _T("FIRSTSTEPS_LONGSHORT_SCRIPT") },
	{ "steady-beat", _T("FIRSTSTEPS_BEAT_SONG"), _T("FIRSTSTEPS_BEAT_SCRIPT") },
	{ "loud-soft", _T("FIRSTSTEPS_LOUDSOFT_SONG"), _T("FIRSTSTEPS_LOUDSOFT_SCRIPT") },
	{ "stairs", _T("FIRSTSTEPS_STAIRS_SONG"), _T("FIRSTSTEPS_STAIRS_SCRIPT") },
	{ "rhythm", _T("FIRSTSTEPS_RHYTHM_SONG"), _T("FIRSTSTEPS_RHYTHM_SCRIPT") },
	{ "copy-repeat", _T("FIRSTSTEPS_COPY_SONG"), _T("FIRSTSTEPS_COPY_SCRIPT") },
	{ "new-sound", _T("FIRSTSTEPS_SOUND_SONG"), _T("FIRSTSTEPS_SOUND_SCRIPT") },
	{ "two-parts", _T("FIRSTSTEPS_PARTS_SONG"), _T("FIRSTSTEPS_PARTS_SCRIPT") },
	{ "tiny-tune", _T("FIRSTSTEPS_TUNE_SONG"), _T("FIRSTSTEPS_TUNE_SCRIPT") },
	{ "home-note", MAKEINTRESOURCE(IDR_EXERCISE_HOME_SONG), MAKEINTRESOURCE(IDR_EXERCISE_HOME_SCRIPT) },
	{ "four-bars", MAKEINTRESOURCE(IDR_EXERCISE_FOURBARS_SONG), MAKEINTRESOURCE(IDR_EXERCISE_FOURBARS_SCRIPT) },
	{ "strong-notes", MAKEINTRESOURCE(IDR_EXERCISE_STRONG_SONG), MAKEINTRESOURCE(IDR_EXERCISE_STRONG_SCRIPT) },
	{ "phrases", MAKEINTRESOURCE(IDR_EXERCISE_PHRASES_SONG), MAKEINTRESOURCE(IDR_EXERCISE_PHRASES_SCRIPT) },
	{ "bass", MAKEINTRESOURCE(IDR_EXERCISE_BASS_SONG), MAKEINTRESOURCE(IDR_EXERCISE_BASS_SCRIPT) },
	{ "beat", MAKEINTRESOURCE(IDR_EXERCISE_BEAT_SONG), MAKEINTRESOURCE(IDR_EXERCISE_BEAT_SCRIPT) },
	{ "minor", MAKEINTRESOURCE(IDR_EXERCISE_MINOR_SONG), MAKEINTRESOURCE(IDR_EXERCISE_MINOR_SCRIPT) },
	{ "hook", MAKEINTRESOURCE(IDR_EXERCISE_HOOK_SONG), MAKEINTRESOURCE(IDR_EXERCISE_HOOK_SCRIPT) },
	{ "song-shape", NULL, MAKEINTRESOURCE(IDR_EXERCISE_SHAPE_SCRIPT) },
	{ "first-song", NULL, MAKEINTRESOURCE(IDR_EXERCISE_FIRSTSONG_SCRIPT) },
};

// The list's rows: a group's heading has this plus the group as its item data, an exercise its place in the list
const DWORD_PTR GROUP_ROW = 0x10000;

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

// A group's exercises, and how many of them are done
void GroupDone(int Group, int &Count, int &Done)
{
	Count = Done = 0;
	const std::vector<ftapi::ExerciseInfo> &List = ftapi::Exercises();
	for (size_t i = 0; i < List.size(); ++i)
		if (List[i].Group == Group) {
			++Count;
			Done += GetExerciseState(List[i].Id) == EXERCISE_DONE ? 1 : 0;
		}
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

// The list: the groups' headings (bold, with how many of each are done), each followed by its exercises (numbered, done
// or started at the right); a group still to come has its heading alone

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
	afx_msg void OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct);

	DECLARE_MESSAGE_MAP()

private:
	CListBox *List() const { return static_cast<CListBox*>(GetDlgItem(IDC_EXERCISE_LIST)); }
	int ExerciseAt(int Row) const;		// the exercise a row shows (its place in ftapi::Exercises), -1 for a heading
	int GroupAt(int Row) const;			// a heading's group, or the group of the exercise a row shows; -1 for none
	void Fill(int Exercise);			// the list, with that exercise picked

	int m_iChosen;
	CFont m_Bold;
};

BEGIN_MESSAGE_MAP(CExercisesDlg, CDialog)
	ON_LBN_SELCHANGE(IDC_EXERCISE_LIST, OnSelChange)
	ON_LBN_DBLCLK(IDC_EXERCISE_LIST, OnOK)
	ON_BN_CLICKED(IDC_EXERCISE_FORGET, OnForget)
	ON_WM_DRAWITEM()
END_MESSAGE_MAP()

BOOL CExercisesDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	// The headings in bold, and the rows a little apart (the list draws its own rows)
	LOGFONT Font;
	GetFont()->GetLogFont(&Font);
	Font.lfWeight = FW_BOLD;
	m_Bold.CreateFontIndirect(&Font);
	{
		CClientDC dc(List());
		CFont *pOld = dc.SelectObject(&m_Bold);
		TEXTMETRIC Metric;
		dc.GetTextMetrics(&Metric);
		dc.SelectObject(pOld);
		List()->SetItemHeight(0, Metric.tmHeight + SY(6));
	}
	// The first one not done yet: for a newcomer, First steps' first
	const std::vector<ftapi::ExerciseInfo> &Exercises = ftapi::Exercises();
	int First = 0;
	while (First + 1 < static_cast<int>(Exercises.size()) && GetExerciseState(Exercises[static_cast<size_t>(First)].Id) == EXERCISE_DONE)
		++First;
	Fill(First);
	return TRUE;
}

int CExercisesDlg::ExerciseAt(int Row) const
{
	if (Row < 0 || Row >= List()->GetCount())
		return -1;
	const DWORD_PTR Data = List()->GetItemData(Row);
	return Data < GROUP_ROW && Data < ftapi::Exercises().size() ? static_cast<int>(Data) : -1;
}

int CExercisesDlg::GroupAt(int Row) const
{
	if (Row < 0 || Row >= List()->GetCount())
		return -1;
	const DWORD_PTR Data = List()->GetItemData(Row);
	if (Data >= GROUP_ROW)
		return Data - GROUP_ROW < ftapi::ExerciseGroups().size() ? static_cast<int>(Data - GROUP_ROW) : -1;
	return Data < ftapi::Exercises().size() ? ftapi::Exercises()[static_cast<size_t>(Data)].Group : -1;
}

void CExercisesDlg::Fill(int Exercise)
{
	CListBox *pList = List();
	pList->ResetContent();
	const std::vector<ftapi::ExerciseInfo> &Exercises = ftapi::Exercises();
	const std::vector<ftapi::ExerciseGroupInfo> &Groups = ftapi::ExerciseGroups();
	int Select = -1, Done = 0;
	CString Progress;
	for (size_t g = 0; g < Groups.size(); ++g) {
		const int Heading = pList->AddString(CString(Groups[g].Title));
		pList->SetItemData(Heading, GROUP_ROW + g);
		int Number = 0;
		for (size_t i = 0; i < Exercises.size(); ++i) {
			if (Exercises[i].Group != static_cast<int>(g))
				continue;
			CString Line;
			Line.Format(_T("%d. %s"), ++Number, static_cast<LPCTSTR>(CString(Exercises[i].Title)));
			const int Row = pList->AddString(Line);
			pList->SetItemData(Row, static_cast<DWORD_PTR>(i));
			if (static_cast<int>(i) == Exercise)
				Select = Row;
		}
		int Count = 0, GroupDoneCount = 0;
		GroupDone(static_cast<int>(g), Count, GroupDoneCount);
		Done += GroupDoneCount;
		if (Count > 0) {
			CString Part;
			Part.Format(_T("%s%s: %d of %d done."), Progress.IsEmpty() ? _T("") : _T("   "), static_cast<LPCTSTR>(CString(Groups[g].Title)),
				GroupDoneCount, Count);
			Progress += Part;
		}
	}
	pList->SetCurSel(Select);
	if (Done == static_cast<int>(Exercises.size()))
		Progress = _T("All of them done: well done! Each one can be done again.");
	SetDlgItemText(IDC_EXERCISE_PROGRESS, Progress);
	GetDlgItem(IDC_EXERCISE_FORGET)->EnableWindow(Done > 0 ? TRUE : FALSE);
	OnSelChange();
}

void CExercisesDlg::OnSelChange()
{
	const int Row = List()->GetCurSel(), Index = ExerciseAt(Row);
	GetDlgItem(IDOK)->EnableWindow(Index >= 0 ? TRUE : FALSE);
	if (Index < 0) {
		// A heading: who the group is for
		const int Group = GroupAt(Row);
		SetDlgItemText(IDC_EXERCISE_ABOUT, Group >= 0 ? CString(ftapi::ExerciseGroups()[static_cast<size_t>(Group)].About) : CString());
		return;
	}
	const ftapi::ExerciseInfo &E = ftapi::Exercises()[static_cast<size_t>(Index)];
	CString Minutes;
	if (E.Minutes == 1)
		Minutes = _T("About a minute.");
	else
		Minutes.Format(_T("About %d minutes."), E.Minutes);
	CString About;
	About.Format(_T("%s %s %s"), static_cast<LPCTSTR>(CString(E.About)), static_cast<LPCTSTR>(Minutes),
		E.NewSong ? _T("It starts a new song, as File > New does.") : _T("It opens a short song made for it."));
	SetDlgItemText(IDC_EXERCISE_ABOUT, About);
}

void CExercisesDlg::OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct)
{
	if (nIDCtl != IDC_EXERCISE_LIST || lpDrawItemStruct->CtlType != ODT_LISTBOX) {
		CDialog::OnDrawItem(nIDCtl, lpDrawItemStruct);
		return;
	}
	CDC *pDC = CDC::FromHandle(lpDrawItemStruct->hDC);
	const CRect Box(lpDrawItemStruct->rcItem);
	const int Row = static_cast<int>(lpDrawItemStruct->itemID);
	const bool Focus = (lpDrawItemStruct->itemState & ODS_FOCUS) != 0;
	if (Row < 0 || Row >= List()->GetCount()) {
		if (Focus)
			pDC->DrawFocusRect(&Box);
		return;
	}
	const bool Selected = (lpDrawItemStruct->itemState & ODS_SELECTED) != 0;
	const int Index = ExerciseAt(Row), Group = GroupAt(Row);
	const bool Later = Index < 0 && Group >= 0 && ftapi::ExerciseGroups()[static_cast<size_t>(Group)].Later;
	const COLORREF Text = ::GetSysColor(Selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT);
	const COLORREF Quiet = Selected ? Text : ::GetSysColor(COLOR_GRAYTEXT);
	pDC->FillSolidRect(&Box, ::GetSysColor(Selected ? COLOR_HIGHLIGHT : Index < 0 ? COLOR_BTNFACE : COLOR_WINDOW));
	// At the right: an exercise's state, how much of a group is done, or that it's still to come
	CString Right;
	if (Index >= 0) {
		const int State = GetExerciseState(ftapi::Exercises()[static_cast<size_t>(Index)].Id);
		Right = State == EXERCISE_DONE ? _T("done") : State == EXERCISE_STARTED ? _T("started") : _T("");
	}
	else if (Later)
		Right = _T("coming later");
	else if (Group >= 0) {
		int Count = 0, Done = 0;
		GroupDone(Group, Count, Done);
		Right.Format(_T("%d of %d done"), Done, Count);
	}
	CString Line;
	List()->GetText(Row, Line);
	CFont *pOld = pDC->SelectObject(Index < 0 ? &m_Bold : GetFont());
	const int OldMode = pDC->SetBkMode(TRANSPARENT);
	CRect Words(Box);
	Words.DeflateRect(Index < 0 ? SX(4) : SX(18), 0, SX(6), 0);
	pDC->SetTextColor(Quiet);
	pDC->DrawText(Right, &Words, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	if (!Right.IsEmpty())
		Words.right -= pDC->GetTextExtent(Right).cx + SX(8);
	pDC->SetTextColor(Later ? Quiet : Text);
	pDC->DrawText(Line, &Words, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
	pDC->SetBkMode(OldMode);
	pDC->SelectObject(pOld);
	if (Focus)
		pDC->DrawFocusRect(&Box);
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
	const int Index = ExerciseAt(List()->GetCurSel());
	if (Index < 0)
		return;		// a heading
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
	if (pFiles == NULL || pFiles->Script == NULL || !ReadBuiltInText(pFiles->Script, Text) || !ParseTutorial(Text, Script, Error)) {
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
		// Its track named for the exercise, as the starters' are: What next lays out the shape the name asks for (the first
		// small song's short loop)
		pDoc->SetTrackTitle(0, CString(E.Title));
		pDoc->MakeUntitled(Title);
		pMainFrame->UpdateTrackBox();
	}
	else if (pFiles->Song == NULL || !OpenBuiltInSong(pMainFrame, pFiles->Song, Title))
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
	const BOOL WasModified = pDoc->IsModified();	// (undone, the song is as it was: saved, if it was)
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
			pDoc->SetModifiedFlag(WasModified);
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
		pDoc->SetModifiedFlag(WasModified);
		pMainFrame->SetMessageText(_T("Your own notes are back. Now it's your turn."));
	}
	return true;
}

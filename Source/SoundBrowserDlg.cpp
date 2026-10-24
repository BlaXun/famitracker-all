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
// tools' index (CMcpConnection::Sounds, ToolSet::Sounds in Api/Tools.cpp), and
// a sound is made into an instrument as add_instrument or reference_sound
// makes it (CMcpConnection::MakeSound). A sound is heard with FamiTracker's
// own engine: tried as an instrument of its own (one at a time), which comes
// out of the song again, sequences and all, unless it's kept. Removing an
// instrument makes the channels let go of an FDS instrument's sequences first
// (CFamiTrackerDoc::RemoveInstrument), so a channel never runs a removed
// trial's.

#include "stdafx.h"
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
#include "SoundBrowserDlg.h"

namespace {

const UINT_PTR TMR_PLAY = 1;		// the preview's notes
const UINT_PTR TMR_FIND = 2;		// a moment after typing in Find
const UINT_PTR TMR_RETRY = 3;		// the list again, when Claude Code kept FamiTracker busy
const UINT PLAY_TIMER_MS = 15, FIND_DELAY_MS = 250, RETRY_MS = 1000;
const UINT WM_SB_MIDI_NOTE = WM_APP + 0x51;	// a MIDI key, from the view (CFamiTrackerView::SetNoteListener)

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

// A note for a channel: FamiTracker's note number (C-0 = 0; a noise pitch on the noise), RELEASE or HALT
stChanNote MakeNote(int Note, int Instrument)
{
	stChanNote Out;
	memset(&Out, 0, sizeof(Out));
	if (Note == RELEASE || Note == HALT)
		Out.Note = static_cast<unsigned char>(Note);
	else {
		Out.Note = static_cast<unsigned char>(Note % 12 + 1);
		Out.Octave = static_cast<unsigned char>(Note / 12);
	}
	Out.Vol = MAX_VOLUME;
	Out.Instrument = static_cast<unsigned char>(Instrument);
	return Out;
}

}	// namespace

class CSoundBrowserDlg : public CDialog
{
public:
	explicit CSoundBrowserDlg(CMainFrame *pMainFrame);

protected:
	virtual BOOL OnInitDialog();
	virtual BOOL PreTranslateMessage(MSG *pMsg);
	virtual void OnOK();
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnDestroy();
	afx_msg void OnFindChanged();
	afx_msg void OnFilterChanged();
	afx_msg void OnListen();
	afx_msg void OnAdd();
	afx_msg void OnGetDispInfo(NMHDR *pNMHDR, LRESULT *pResult);
	afx_msg void OnItemChanged(NMHDR *pNMHDR, LRESULT *pResult);
	afx_msg void OnItemActivate(NMHDR *pNMHDR, LRESULT *pResult);
	afx_msg LRESULT OnMidiNote(WPARAM wParam, LPARAM lParam);

	DECLARE_MESSAGE_MAP()

private:
	CFamiTrackerDoc *Doc() const;
	bool LoadSounds();
	void FillFilters();
	void Refilter();
	const ftapi::SoundEntry *Selected() const;
	void ShowSelected();
	int SoundInstrument(int Sound, CString &Error);
	void DropTrial();
	void PlayKey(int Note, bool Down);
	void StopPlaying();
	void SetStatus(LPCTSTR Text);

	CMainFrame *m_pMainFrame;
	CEdit m_cFind;
	CComboBox m_cKind, m_cFrom, m_cChannel;
	CListCtrl m_cList;
	std::vector<ftapi::SoundEntry> m_All;	// every sound
	std::vector<size_t> m_Shown;			// the list's rows: sounds in m_All
	std::vector<int> m_Targets;				// the channel filter's kinds of channel, after "All channels"
	int m_iSelected;						// the sound picked, in m_All; -1 for none
	int m_iTrial;							// the instrument a sound is tried in, -1 for none
	int m_iTrialSound;						// the sound it was made for
	int m_iKept;							// instruments added to the song
	bool m_bWasModified;
	bool m_bFilling;

	std::vector<ftapi::PreviewStep> m_Events;	// what plays
	size_t m_iNextEvent;
	DWORD m_dwStart;
	int m_iPlayChannel;						// -1 while nothing plays
	int m_iPlayInstrument;
	int m_iKeyNote;							// the note a key (or a MIDI key) holds, -1 for none
};

BEGIN_MESSAGE_MAP(CSoundBrowserDlg, CDialog)
	ON_WM_TIMER()
	ON_WM_DESTROY()
	ON_EN_CHANGE(IDC_SB_FIND, OnFindChanged)
	ON_CBN_SELCHANGE(IDC_SB_KIND, OnFilterChanged)
	ON_CBN_SELCHANGE(IDC_SB_FROM, OnFilterChanged)
	ON_CBN_SELCHANGE(IDC_SB_CHANNEL, OnFilterChanged)
	ON_BN_CLICKED(IDC_SB_LISTEN, OnListen)
	ON_BN_CLICKED(IDC_SB_ADD, OnAdd)
	ON_NOTIFY(LVN_GETDISPINFO, IDC_SB_LIST, OnGetDispInfo)
	ON_NOTIFY(LVN_ITEMCHANGED, IDC_SB_LIST, OnItemChanged)
	ON_NOTIFY(NM_DBLCLK, IDC_SB_LIST, OnItemActivate)
	ON_MESSAGE(WM_SB_MIDI_NOTE, OnMidiNote)
END_MESSAGE_MAP()

CSoundBrowserDlg::CSoundBrowserDlg(CMainFrame *pMainFrame) :
	CDialog(IDD_SOUND_BROWSER, pMainFrame),
	m_pMainFrame(pMainFrame),
	m_iSelected(-1),
	m_iTrial(-1),
	m_iTrialSound(-1),
	m_iKept(0),
	m_bWasModified(false),
	m_bFilling(false),
	m_iNextEvent(0),
	m_dwStart(0),
	m_iPlayChannel(-1),
	m_iPlayInstrument(MAX_INSTRUMENTS),
	m_iKeyNote(-1)
{
}

CFamiTrackerDoc *CSoundBrowserDlg::Doc() const
{
	return static_cast<CFamiTrackerDoc*>(m_pMainFrame->GetActiveDocument());
}

BOOL CSoundBrowserDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	m_cFind.SubclassDlgItem(IDC_SB_FIND, this);
	m_cKind.SubclassDlgItem(IDC_SB_KIND, this);
	m_cFrom.SubclassDlgItem(IDC_SB_FROM, this);
	m_cChannel.SubclassDlgItem(IDC_SB_CHANNEL, this);
	m_cList.SubclassDlgItem(IDC_SB_LIST, this);
	m_cList.SetExtendedStyle(m_cList.GetExtendedStyle() | LVS_EX_FULLROWSELECT);
	CRect Rect;
	m_cList.GetClientRect(&Rect);
	const int Width = Rect.Width() - ::GetSystemMetrics(SM_CXVSCROLL);
	static const struct { LPCTSTR Name; int Percent; } COLUMNS[] = {
		{ _T("Sound"), 26 }, { _T("Kind"), 9 }, { _T("Plays on"), 15 }, { _T("From"), 28 }, { _T("In song"), 9 }, { _T("Warning"), 13 },
	};
	for (int c = 0; c < 6; ++c)
		m_cList.InsertColumn(c, COLUMNS[c].Name, LVCFMT_LEFT, Width * COLUMNS[c].Percent / 100);

	m_bWasModified = Doc()->IsModified() != FALSE;
	CFamiTrackerView *pView = CFamiTrackerView::GetView();
	if (pView != NULL)
		pView->SetNoteListener(m_hWnd, WM_SB_MIDI_NOTE);
	if (LoadSounds()) {
		FillFilters();
		Refilter();
		SetStatus(_T("Click a sound to hear it; with the list picked, the note keys play it too. Add to the song keeps it as an ")
			_T("instrument, which you can change later in the instrument editor."));
	}
	else
		SetTimer(TMR_RETRY, RETRY_MS, NULL);
	SetTimer(TMR_PLAY, PLAY_TIMER_MS, NULL);
	m_cFind.SetFocus();
	return FALSE;	// the focus is set
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
	m_cKind.ResetContent();
	m_cKind.AddString(_T("All kinds"));
	const std::vector<ftapi::SoundKind> &Kinds = ftapi::SoundKinds();
	for (size_t k = 0; k < Kinds.size(); ++k)
		m_cKind.AddString(FromUtf8(Kinds[k].Label));
	m_cKind.SetCurSel(0);

	m_cFrom.ResetContent();
	m_cFrom.AddString(_T("Everywhere"));
	m_cFrom.AddString(_T("Ready-made sounds"));		// in sound_source's order
	m_cFrom.AddString(_T("Reference library"));
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
	for (size_t t = 0; t < m_Targets.size(); ++t)
		m_cChannel.AddString(FromUtf8(ftapi::TargetName(m_Targets[t])));
	m_cChannel.SetCurSel(0);
}

void CSoundBrowserDlg::Refilter()
{
	ftapi::SoundQuery Query;
	CString Words;
	m_cFind.GetWindowText(Words);
	Query.Words = ToUtf8(Words);
	const int Kind = m_cKind.GetCurSel();
	if (Kind > 0 && static_cast<size_t>(Kind) <= ftapi::SoundKinds().size())
		Query.Kind = ftapi::SoundKinds()[static_cast<size_t>(Kind - 1)].Id;
	const int From = m_cFrom.GetCurSel();
	Query.Source = From > 0 ? From - 1 : -1;
	const int Channel = m_cChannel.GetCurSel();
	Query.Target = Channel > 0 && static_cast<size_t>(Channel) <= m_Targets.size() ? m_Targets[static_cast<size_t>(Channel - 1)] : -1;
	m_Shown = ftapi::FindSounds(m_All, Query);

	// The sound picked stays picked while it's listed
	m_bFilling = true;
	m_cList.SetItemCountEx(static_cast<int>(m_Shown.size()), LVSICF_NOSCROLL);
	m_cList.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
	int Row = -1;
	for (size_t r = 0; r < m_Shown.size() && m_iSelected >= 0; ++r)
		if (static_cast<int>(m_Shown[r]) == m_iSelected)
			Row = static_cast<int>(r);
	if (Row >= 0) {
		m_cList.SetItemState(Row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		m_cList.EnsureVisible(Row, FALSE);
	}
	else
		m_iSelected = -1;
	m_cList.Invalidate();
	m_bFilling = false;
	CString Count;
	Count.Format(m_Shown.size() == 1 ? _T("%d sound") : _T("%d sounds"), static_cast<int>(m_Shown.size()));
	SetDlgItemText(IDC_SB_COUNT, Count);
	ShowSelected();
}

const ftapi::SoundEntry *CSoundBrowserDlg::Selected() const
{
	return m_iSelected >= 0 && static_cast<size_t>(m_iSelected) < m_All.size() ? &m_All[static_cast<size_t>(m_iSelected)] : NULL;
}

void CSoundBrowserDlg::ShowSelected()
{
	const ftapi::SoundEntry *pSound = Selected();
	CString Text;
	if (pSound != NULL) {
		std::string About = pSound->Name + (pSound->Words.empty() ? std::string() : ": " + pSound->Words) + ". From " + pSound->Origin +
			"; it plays on " + pSound->ChannelName + ".";
		if (!pSound->Warning.empty())
			About += " " + pSound->Warning;
		if (pSound->Target == ftapi::TARGET_FDS)
			About += " The FDS sounds two octaves under the note written: play it two octaves above where a pulse would.";
		else if (pSound->Target == ftapi::TARGET_S5B)
			About += " The 5B sounds an octave under the note written.";
		Text = FromUtf8(About);
	}
	else
		Text = m_All.empty() ? _T("") : _T("Pick a sound in the list, or type what you're looking for in Find (bell, warm, pluck, bass, a game).");
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
	StopPlaying();
	RemoveInstrumentAndSequences(Doc(), static_cast<unsigned int>(m_iTrial));
	m_iTrial = -1;
	m_iTrialSound = -1;
}

void CSoundBrowserDlg::OnListen()
{
	const ftapi::SoundEntry *pSound = Selected();
	if (pSound == NULL || pSound->Channel < 0)
		return;
	CString Error;
	const int Instrument = SoundInstrument(m_iSelected, Error);
	if (Instrument < 0) {
		SetStatus(Error);
		return;
	}
	StopPlaying();
	if (theApp.IsPlaying())
		theApp.StopPlayer();	// the song would play over it
	m_Events = ftapi::PreviewSteps(*pSound);
	m_iNextEvent = 0;
	m_dwStart = ::GetTickCount();
	m_iPlayChannel = pSound->Channel;
	m_iPlayInstrument = Instrument;
	OnTimer(TMR_PLAY);
}

// A note key (or a MIDI key) pressed or let go while the list is picked: the sound picked plays it
void CSoundBrowserDlg::PlayKey(int Note, bool Down)
{
	const ftapi::SoundEntry *pSound = Selected();
	if (pSound == NULL || pSound->Channel < 0 || Note < 0 || Note >= NOTE_RANGE * OCTAVE_RANGE)
		return;
	if (Down) {
		CString Error;
		const int Instrument = SoundInstrument(m_iSelected, Error);
		if (Instrument < 0) {
			SetStatus(Error);
			return;
		}
		if (theApp.IsPlaying())
			theApp.StopPlayer();
		m_Events.clear();		// the preview's notes stop coming
		m_iNextEvent = 0;
		m_iPlayChannel = pSound->Channel;
		m_iPlayInstrument = Instrument;
		stChanNote Cell = MakeNote(Note, Instrument);
		theApp.GetSoundGenerator()->QueueNote(m_iPlayChannel, Cell, NOTE_PRIO_2);
		m_iKeyNote = Note;
	}
	else if (Note == m_iKeyNote && m_iPlayChannel >= 0) {
		stChanNote Cell = MakeNote(RELEASE, m_iPlayInstrument);
		theApp.GetSoundGenerator()->QueueNote(m_iPlayChannel, Cell, NOTE_PRIO_2);
		m_iKeyNote = -1;
		// and silence a moment later, for a sound that holds through a release
		const ftapi::PreviewStep End = { 1500, ftapi::PREVIEW_HALT };
		m_Events.assign(1, End);
		m_iNextEvent = 0;
		m_dwStart = ::GetTickCount();
	}
}

void CSoundBrowserDlg::StopPlaying()
{
	if (m_iPlayChannel < 0)
		return;
	stChanNote Note = MakeNote(HALT, MAX_INSTRUMENTS);
	theApp.GetSoundGenerator()->QueueNote(m_iPlayChannel, Note, NOTE_PRIO_2);
	m_iPlayChannel = -1;
	m_iKeyNote = -1;
	m_Events.clear();
	m_iNextEvent = 0;
}

BOOL CSoundBrowserDlg::PreTranslateMessage(MSG *pMsg)
{
	if (pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_RETURN && pMsg->hwnd == m_cFind.GetSafeHwnd()) {
		// Enter in Find: the first sound found is picked and heard
		KillTimer(TMR_FIND);
		Refilter();
		if (!m_Shown.empty()) {
			m_cList.SetItemState(0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
			m_cList.EnsureVisible(0, FALSE);
		}
		return TRUE;
	}
	if ((pMsg->message == WM_KEYDOWN || pMsg->message == WM_KEYUP) && pMsg->hwnd == m_cList.GetSafeHwnd() &&
		::GetKeyState(VK_CONTROL) >= 0 && ::GetKeyState(VK_MENU) >= 0) {
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

void CSoundBrowserDlg::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == TMR_FIND) {
		KillTimer(TMR_FIND);
		Refilter();
		return;
	}
	if (nIDEvent == TMR_RETRY) {
		if (LoadSounds()) {
			KillTimer(TMR_RETRY);
			FillFilters();
			Refilter();
			SetStatus(_T("Click a sound to hear it; with the list picked, the note keys play it too."));
		}
		return;
	}
	if (nIDEvent != TMR_PLAY) {
		CDialog::OnTimer(nIDEvent);
		return;
	}
	if (m_iPlayChannel < 0 || m_Events.empty())
		return;
	const DWORD Now = ::GetTickCount() - m_dwStart;
	while (m_iNextEvent < m_Events.size() && static_cast<DWORD>(m_Events[m_iNextEvent].At) <= Now) {
		const int Step = m_Events[m_iNextEvent].Note;
		stChanNote Note = MakeNote(Step == ftapi::PREVIEW_RELEASE ? RELEASE : Step == ftapi::PREVIEW_HALT ? HALT : Step,
			m_iPlayInstrument);
		theApp.GetSoundGenerator()->QueueNote(m_iPlayChannel, Note, NOTE_PRIO_2);
		++m_iNextEvent;
	}
	if (m_iNextEvent >= m_Events.size()) {
		m_Events.clear();
		m_iNextEvent = 0;
	}
}

void CSoundBrowserDlg::OnFindChanged()
{
	SetTimer(TMR_FIND, FIND_DELAY_MS, NULL);
}

void CSoundBrowserDlg::OnFilterChanged()
{
	Refilter();
}

void CSoundBrowserDlg::OnGetDispInfo(NMHDR *pNMHDR, LRESULT *pResult)
{
	*pResult = 0;
	LVITEM &Item = reinterpret_cast<NMLVDISPINFO*>(pNMHDR)->item;
	if ((Item.mask & LVIF_TEXT) == 0 || Item.pszText == NULL || Item.cchTextMax <= 0)
		return;
	Item.pszText[0] = 0;
	if (Item.iItem < 0 || static_cast<size_t>(Item.iItem) >= m_Shown.size())
		return;
	const ftapi::SoundEntry &E = m_All[m_Shown[static_cast<size_t>(Item.iItem)]];
	CString Text;
	switch (Item.iSubItem) {
	case 0:
		Text = FromUtf8(E.Name);
		break;
	case 1:
		Text = FromUtf8(E.Kind);
		break;
	case 2:
		Text = FromUtf8(E.ChannelName);
		break;
	case 3:
		if (E.Source == ftapi::SOURCE_REFERENCE)	// the track's: "Lava Caves (Lava Game, track 3)"
			Text = FromUtf8(E.Origin.substr(0, E.Origin.rfind(", sound ")));
		else
			Text = FromUtf8(ftapi::SourceName(E.Source));
		break;
	case 4:
		if (E.InSong >= 0 && E.InSong != m_iTrial)
			Text.Format(_T("yes (%02X)"), E.InSong);
		break;
	case 5:
		if (ftapi::SoundVanishes(E.Heard))
			Text = _T("short notes");
		if (!E.ClickPitches.empty())
			Text += Text.IsEmpty() ? _T("vibrato clicks") : _T(", vibrato clicks");
		break;
	}
	_tcsncpy_s(Item.pszText, static_cast<size_t>(Item.cchTextMax), Text, _TRUNCATE);
}

void CSoundBrowserDlg::OnItemChanged(NMHDR *pNMHDR, LRESULT *pResult)
{
	const NMLISTVIEW *pList = reinterpret_cast<NMLISTVIEW*>(pNMHDR);
	*pResult = 0;
	if (m_bFilling || (pList->uChanged & LVIF_STATE) == 0 || pList->iItem < 0 ||
		static_cast<size_t>(pList->iItem) >= m_Shown.size())
		return;
	const bool Now = (pList->uNewState & LVIS_SELECTED) != 0, Before = (pList->uOldState & LVIS_SELECTED) != 0;
	if (Now == Before || !Now)
		return;
	const int Sound = static_cast<int>(m_Shown[static_cast<size_t>(pList->iItem)]);
	if (Sound == m_iSelected)
		return;
	m_iSelected = Sound;
	ShowSelected();
	OnListen();		// a click plays it
}

void CSoundBrowserDlg::OnItemActivate(NMHDR *pNMHDR, LRESULT *pResult)
{
	*pResult = 0;
	OnListen();
}

// Enter, outside Find
void CSoundBrowserDlg::OnOK()
{
	OnAdd();
}

void CSoundBrowserDlg::OnAdd()
{
	const ftapi::SoundEntry *pSound = Selected();
	if (pSound == NULL)
		return;
	CString Error;
	const bool Had = pSound->Make == ftapi::MAKE_NONE || (pSound->InSong >= 0 && pSound->InSong != m_iTrial &&
		Doc()->IsInstrumentUsed(static_cast<unsigned int>(pSound->InSong)));
	const int Instrument = SoundInstrument(m_iSelected, Error);
	if (Instrument < 0) {
		SetStatus(Error);
		return;
	}
	ftapi::SoundEntry &E = m_All[static_cast<size_t>(m_iSelected)];
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
		CString Origin;
		Origin.Format(_T("this song's instrument %02X"), Instrument);
		Own.Origin = ToUtf8(Origin);
		Own.Words = "added from the " + ftapi::SourceName(E.Source) + (E.Words.empty() ? std::string() : ": " + E.Words);
		Own.Reference.reset();
		ftapi::IndexWords(Own);
		m_All.push_back(Own);
	}
	m_pMainFrame->SelectInstrument(Instrument);
	m_cList.Invalidate();
	SetStatus(Text);
}

void CSoundBrowserDlg::OnDestroy()
{
	KillTimer(TMR_PLAY);
	KillTimer(TMR_FIND);
	KillTimer(TMR_RETRY);
	CFamiTrackerView *pView = CFamiTrackerView::GetView();
	if (pView != NULL)
		pView->SetNoteListener(NULL, 0);
	StopPlaying();
	DropTrial();
	// Only trying sounds leaves the song as it was
	if (m_iKept == 0 && !m_bWasModified)
		Doc()->SetModifiedFlag(FALSE);
	CDialog::OnDestroy();
}

void CSoundBrowserDlg::SetStatus(LPCTSTR Text)
{
	SetDlgItemText(IDC_SB_STATUS, Text);
}

void ShowSoundBrowser(CMainFrame *pMainFrame)
{
	CFamiTrackerDoc *pDoc = static_cast<CFamiTrackerDoc*>(pMainFrame->GetActiveDocument());
	if (pDoc == NULL || !pDoc->IsFileLoaded())
		return;
	CSoundBrowserDlg Dialog(pMainFrame);
	Dialog.DoModal();
}

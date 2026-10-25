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
#include "Api/Music.h"
#include "FamiTracker.h"
#include "FamiTrackerDoc.h"
#include "FamiTrackerView.h"
#include "MainFrm.h"
#include "PatternEditor.h"
#include "InsertRecording.h"
#include "InsertRecordingDlg.h"
#include "RecordDlg.h"
#include "BackingDlg.h"

IMPLEMENT_DYNAMIC(CBackingDlg, CDialog)

CBackingDlg::CBackingDlg(CFamiTrackerView *pView) :
	CDialog(CBackingDlg::IDD, pView),
	m_pView(pView),
	m_pDocument(pView->GetDocument()),
	m_iTrack(static_cast<unsigned int>(static_cast<CMainFrame*>(pView->GetParentFrame())->GetSelectedTrack())),
	m_iChordsRow(-1),
	m_iCursorBar(0),
	m_bSettingChords(false)
{
	m_Choice.MelodyChannel = -1;
	m_Choice.ChordChannel = -1;
	m_Choice.BassChannel = -1;
	m_Choice.KeyTonic = -1;
	m_Choice.KeyMinor = false;
	m_Choice.HalfBars = true;
	m_Choice.ChordsRow = -1;
}

void CBackingDlg::SetChords(const CString &Names, int Row)
{
	m_sChords = Names;
	m_iChordsRow = Row;
}

BEGIN_MESSAGE_MAP(CBackingDlg, CDialog)
	ON_CBN_SELCHANGE(IDC_BACK_MELODY, &CBackingDlg::OnMelodyChanged)
	ON_CBN_SELCHANGE(IDC_BACK_KEY, &CBackingDlg::OnOptionChanged)
	ON_CBN_SELCHANGE(IDC_BACK_CHORDS, &CBackingDlg::OnOptionChanged)
	ON_CBN_SELCHANGE(IDC_BACK_BASS, &CBackingDlg::OnOptionChanged)
	ON_CBN_SELCHANGE(IDC_BACK_CHANGES, &CBackingDlg::OnOptionChanged)
	ON_EN_CHANGE(IDC_BACK_CHORDTEXT, &CBackingDlg::OnChordsChanged)
	ON_BN_CLICKED(IDC_BACK_STRUM, &CBackingDlg::OnStrum)
	ON_BN_CLICKED(IDC_REC_FROMFILE, &CBackingDlg::OnFromFile)
END_MESSAGE_MAP()

BOOL CBackingDlg::OnInitDialog()
{
	CDialog::OnInitDialog();

	m_cMelody.SubclassDlgItem(IDC_BACK_MELODY, this);
	m_cKey.SubclassDlgItem(IDC_BACK_KEY, this);
	m_cChords.SubclassDlgItem(IDC_BACK_CHORDS, this);
	m_cBass.SubclassDlgItem(IDC_BACK_BASS, this);
	m_cChanges.SubclassDlgItem(IDC_BACK_CHANGES, this);

	m_cChanges.AddString(_T("Up to twice a bar"));
	m_cChanges.AddString(_T("Once a bar"));
	m_cChanges.SetCurSel(0);

	// Typed chords without a melody start at the cursor's bar
	CPatternEditor *pEditor = m_pView->GetPatternEditor();
	const int Length = static_cast<int>(m_pDocument->GetPatternLength(m_iTrack));
	const ftapi::Timing Time(Length, static_cast<int>(m_pDocument->GetFrameCount(m_iTrack)), m_pDocument->GetFirstHighlight(),
		m_pDocument->GetSecondHighlight());
	m_iCursorBar = Time.BarStart(Time.BarOf(pEditor->GetFrame() * Length + pEditor->GetRow()));
	static_cast<CEdit*>(GetDlgItem(IDC_BACK_CHORDTEXT))->SetLimitText(400);
	m_bSettingChords = true;
	SetDlgItemText(IDC_BACK_CHORDTEXT, m_sChords);
	m_bSettingChords = false;

	FillMelody(pEditor->GetChannel());
	FillChannels(m_cChords, true);
	FillChannels(m_cBass, false);
	FillKeys();
	UpdatePreview();
	return TRUE;
}

void CBackingDlg::FillMelody(int CursorChannel)
{
	// Channels with notes; the cursor's first, then the first VRC6 pulse. Given chords need none.
	m_cMelody.ResetContent();
	const int None = m_cMelody.AddString(_T("No melody"));
	m_cMelody.SetItemData(None, static_cast<DWORD_PTR>(-1));
	const int Channels = static_cast<int>(m_pDocument->GetAvailableChannels());
	int Cursor = -1, Pulse = -1;
	for (int i = 0; i < Channels; ++i) {
		if (!IsMelodyChannel(m_pDocument, i) || !HasNotes(m_pDocument, m_iTrack, i))
			continue;
		const int Item = m_cMelody.AddString(RecordingChannelName(m_pDocument, i));
		m_cMelody.SetItemData(Item, static_cast<DWORD_PTR>(i));
		if (i == CursorChannel)
			Cursor = Item;
		if (Pulse < 0 && m_pDocument->GetChannelType(i) == CHANID_VRC6_PULSE1)
			Pulse = Item;
	}
	m_cMelody.SetCurSel(Cursor >= 0 ? Cursor : (Pulse >= 0 ? Pulse : (m_cMelody.GetCount() > 1 ? 1 : None)));
}

void CBackingDlg::FillChannels(CComboBox &List, bool Chords)
{
	// Keep what was picked, unless the melody is there now
	const int Melody = PickedRecordingChannel(m_cMelody);
	int Picked = -2;	// nothing picked yet
	if (List.GetCount() > 0)
		Picked = PickedRecordingChannel(List);

	List.ResetContent();
	const int None = List.AddString(Chords ? _T("No chords") : _T("No bass line"));
	List.SetItemData(None, static_cast<DWORD_PTR>(-1));
	List.SetCurSel(None);
	int Default = -1, Fallback = -1;
	const int Channels = static_cast<int>(m_pDocument->GetAvailableChannels());
	for (int i = 0; i < Channels; ++i) {
		if (i == Melody || !(Chords ? IsChordChannel(m_pDocument, i) : IsBassChannel(m_pDocument, i)))
			continue;
		const int Item = List.AddString(RecordingChannelName(m_pDocument, i));
		List.SetItemData(Item, static_cast<DWORD_PTR>(i));
		const int ChanID = m_pDocument->GetChannelType(i);
		if (i == Picked)
			List.SetCurSel(Item);
		// By default the VRC6's second pulse plays the chords and its sawtooth the bass;
		// without the VRC6, the 2A03's second pulse and the triangle
		if (Chords ? ChanID == CHANID_VRC6_PULSE2 : ChanID == CHANID_VRC6_SAWTOOTH)
			Default = Item;
		if (Fallback < 0 || (Chords ? ChanID == CHANID_SQUARE2 : ChanID == CHANID_TRIANGLE))
			Fallback = Item;
	}
	if (Picked == -2 || (Picked >= 0 && List.GetCurSel() == None))
		List.SetCurSel(Default >= 0 ? Default : (Fallback >= 0 ? Fallback : None));
}

void CBackingDlg::FillKeys()
{
	// "Found: C minor", then every key
	const int Selected = m_cKey.GetCount() > 0 ? m_cKey.GetCurSel() : 0;
	backing_choice_t Choice = ReadChoice();
	Choice.KeyTonic = -1;
	CString Key, Chords, Message;
	int First = 0, Last = 0;
	CString Found(_T("Found: "));
	Found += DescribeBacking(m_pDocument, m_iTrack, Choice, Key, Chords, First, Last, Message) ? Key : CString(_T("-"));

	m_cKey.ResetContent();
	const int Auto = m_cKey.AddString(Found);
	m_cKey.SetItemData(Auto, static_cast<DWORD_PTR>(-1));
	for (int Tonic = 0; Tonic < 12; ++Tonic) {
		for (int Minor = 0; Minor < 2; ++Minor) {
			const int Item = m_cKey.AddString(KeyName(Tonic, Minor != 0));
			m_cKey.SetItemData(Item, static_cast<DWORD_PTR>(Tonic * 2 + Minor));
		}
	}
	m_cKey.SetCurSel(Selected != CB_ERR ? Selected : Auto);
}

backing_choice_t CBackingDlg::ReadChoice() const
{
	backing_choice_t Choice;
	Choice.MelodyChannel = PickedRecordingChannel(m_cMelody);
	Choice.ChordChannel = PickedRecordingChannel(m_cChords);
	Choice.BassChannel = PickedRecordingChannel(m_cBass);
	Choice.KeyTonic = -1;
	Choice.KeyMinor = false;
	const int Key = PickedRecordingChannel(m_cKey);	// item data: -1, or tonic * 2 + minor
	if (Key >= 0) {
		Choice.KeyTonic = Key / 2;
		Choice.KeyMinor = (Key % 2) != 0;
	}
	Choice.HalfBars = m_cChanges.GetCurSel() != 1;
	GetDlgItemText(IDC_BACK_CHORDTEXT, Choice.Chords);
	Choice.Chords.Trim();
	Choice.ChordsRow = m_iChordsRow;
	if (!Choice.Chords.IsEmpty() && Choice.ChordsRow < 0 && Choice.MelodyChannel < 0)
		Choice.ChordsRow = m_iCursorBar;
	return Choice;
}

void CBackingDlg::UpdatePreview()
{
	const backing_choice_t Choice = ReadChoice();
	m_cChanges.EnableWindow(Choice.Chords.IsEmpty());	// given chords change where they're written to
	if (Choice.Chords.IsEmpty() && Choice.MelodyChannel < 0) {
		SetDlgItemText(IDC_BACK_PREVIEW, m_cMelody.GetCount() > 1 ? _T("Pick the channel with the melody, or give the chords above.") :
			_T("There's no melody in this song yet. Type the chords above or strum them, or record or write a melody first."));
		GetDlgItem(IDOK)->EnableWindow(FALSE);
		return;
	}
	CString Key, Chords, Message;
	int First = 0, Last = 0;
	if (!DescribeBacking(m_pDocument, m_iTrack, Choice, Key, Chords, First, Last, Message)) {
		SetDlgItemText(IDC_BACK_PREVIEW, Message);
		GetDlgItem(IDOK)->EnableWindow(FALSE);
		return;
	}
	CString Text;
	Text.Format(_T("In %s, frames %02X to %02X:\n%s"), static_cast<LPCTSTR>(Key), First, Last, static_cast<LPCTSTR>(Chords));
	if (Choice.Chords.IsEmpty())
		Text += _T("\n(found from the melody)");
	SetDlgItemText(IDC_BACK_PREVIEW, Text);
	GetDlgItem(IDOK)->EnableWindow(Choice.ChordChannel >= 0 || Choice.BassChannel >= 0);
}

void CBackingDlg::OnMelodyChanged()
{
	FillChannels(m_cChords, true);
	FillChannels(m_cBass, false);
	FillKeys();
	UpdatePreview();
}

void CBackingDlg::OnOptionChanged()
{
	UpdatePreview();
}

void CBackingDlg::OnChordsChanged()
{
	if (m_bSettingChords)
		return;		// the dialog wrote them, and goes on itself
	CString Text;
	GetDlgItemText(IDC_BACK_CHORDTEXT, Text);
	Text.Trim();
	if (Text.IsEmpty())
		m_iChordsRow = -1;	// typed again from nothing: from the melody's first bar
	FillKeys();		// the key they're in
	UpdatePreview();
}

void CBackingDlg::UseHeardChords(const CString &Names, int Row)
{
	m_iChordsRow = Row;
	m_bSettingChords = true;
	SetDlgItemText(IDC_BACK_CHORDTEXT, Names);
	m_bSettingChords = false;
	FillKeys();
	UpdatePreview();
}

void CBackingDlg::OnStrum()
{
	// Strummed into the microphone, with the click and the song as for any take
	CRecordDlg Dialog(m_pView, this, true);
	if (Dialog.DoModal() == IDC_REC_CHORDS)
		UseHeardChords(Dialog.GetChords(), Dialog.GetChordsRow());
}

void CBackingDlg::OnFromFile()
{
	// Strummed in a WAV file at the song's tempo, its first strum on the bar the chords start at
	CFileDialog FileDialog(TRUE, _T("wav"), NULL, OFN_HIDEREADONLY | OFN_FILEMUSTEXIST,
		_T("WAV recordings (*.wav)|*.wav|All files (*.*)|*.*||"), this);
	if (FileDialog.DoModal() != IDOK)
		return;
	backing_choice_t Choice = ReadChoice();
	Choice.Chords.Empty();
	CString Key, Chords, Message;
	int First = 0, Last = 0, Row = m_iCursorBar;
	if (Choice.MelodyChannel >= 0 && DescribeBacking(m_pDocument, m_iTrack, Choice, Key, Chords, First, Last, Message, &Row) == false)
		Row = m_iCursorBar;
	CString Names;
	int FirstRow = 0;
	bool Heard;
	{
		CWaitCursor Wait;
		Heard = HearRecordedChords(m_pDocument, m_iTrack, FileDialog.GetPathName(), Row, Names, FirstRow, Message);
	}
	if (!Heard) {
		AfxMessageBox(Message, MB_OK | MB_ICONINFORMATION);
		return;
	}
	UseHeardChords(Names, FirstRow);
}

void CBackingDlg::OnOK()
{
	m_Choice = ReadChoice();
	CDialog::OnOK();
}

backing_choice_t CBackingDlg::GetChoice() const
{
	return m_Choice;
}

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
#include "FamiTracker.h"
#include "FamiTrackerDoc.h"
#include "FamiTrackerView.h"
#include "MainFrm.h"
#include "PatternEditor.h"
#include "InsertRecording.h"
#include "InsertRecordingDlg.h"
#include "BackingDlg.h"

IMPLEMENT_DYNAMIC(CBackingDlg, CDialog)

CBackingDlg::CBackingDlg(CFamiTrackerView *pView) :
	CDialog(CBackingDlg::IDD, pView),
	m_pView(pView),
	m_pDocument(pView->GetDocument()),
	m_iTrack(static_cast<unsigned int>(static_cast<CMainFrame*>(pView->GetParentFrame())->GetSelectedTrack()))
{
	m_Choice.MelodyChannel = -1;
	m_Choice.ChordChannel = -1;
	m_Choice.BassChannel = -1;
	m_Choice.KeyTonic = -1;
	m_Choice.KeyMinor = false;
	m_Choice.HalfBars = true;
}

BEGIN_MESSAGE_MAP(CBackingDlg, CDialog)
	ON_CBN_SELCHANGE(IDC_BACK_MELODY, &CBackingDlg::OnMelodyChanged)
	ON_CBN_SELCHANGE(IDC_BACK_KEY, &CBackingDlg::OnOptionChanged)
	ON_CBN_SELCHANGE(IDC_BACK_CHORDS, &CBackingDlg::OnOptionChanged)
	ON_CBN_SELCHANGE(IDC_BACK_BASS, &CBackingDlg::OnOptionChanged)
	ON_CBN_SELCHANGE(IDC_BACK_CHANGES, &CBackingDlg::OnOptionChanged)
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

	FillMelody(m_pView->GetPatternEditor()->GetChannel());
	FillChannels(m_cChords, true);
	FillChannels(m_cBass, false);
	FillKeys();
	UpdatePreview();
	return TRUE;
}

void CBackingDlg::FillMelody(int CursorChannel)
{
	// Channels with notes; the cursor's first, then the first VRC6 pulse
	m_cMelody.ResetContent();
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
	if (m_cMelody.GetCount() > 0)
		m_cMelody.SetCurSel(Cursor >= 0 ? Cursor : (Pulse >= 0 ? Pulse : 0));
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
	return Choice;
}

void CBackingDlg::UpdatePreview()
{
	if (m_cMelody.GetCount() == 0) {
		SetDlgItemText(IDC_BACK_PREVIEW, _T("There's no melody in this song yet. Record or write one first, then add ")
			_T("chords and a bass line to it."));
		GetDlgItem(IDOK)->EnableWindow(FALSE);
		return;
	}
	const backing_choice_t Choice = ReadChoice();
	CString Key, Chords, Message;
	int First = 0, Last = 0;
	if (!DescribeBacking(m_pDocument, m_iTrack, Choice, Key, Chords, First, Last, Message)) {
		SetDlgItemText(IDC_BACK_PREVIEW, Message);
		GetDlgItem(IDOK)->EnableWindow(FALSE);
		return;
	}
	CString Text;
	Text.Format(_T("In %s, frames %02X to %02X:\n%s"), static_cast<LPCTSTR>(Key), First, Last, static_cast<LPCTSTR>(Chords));
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

void CBackingDlg::OnOK()
{
	m_Choice = ReadChoice();
	CDialog::OnOK();
}

backing_choice_t CBackingDlg::GetChoice() const
{
	return m_Choice;
}

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
#include "InsertRecordingDlg.h"

int DefaultRecordingChannel(const CFamiTrackerDoc *pDoc, int CursorChannel)
{
	// Where the cursor is if a melody can go there, otherwise the first VRC6
	// pulse, otherwise the first channel
	const int Channels = static_cast<int>(pDoc->GetAvailableChannels());
	if (CursorChannel >= 0 && CursorChannel < Channels && IsMelodyChannel(pDoc, CursorChannel))
		return CursorChannel;
	for (int i = 0; i < Channels; ++i) {
		if (pDoc->GetChannelType(i) == CHANID_VRC6_PULSE1)
			return i;
	}
	return 0;
}

void FillRecordingChannels(CComboBox &List, const CFamiTrackerDoc *pDoc, bool Drums, int MelodyChannel)
{
	List.ResetContent();
	const int Channels = static_cast<int>(pDoc->GetAvailableChannels());
	for (int i = 0; i < Channels; ++i) {
		const bool Fits = Drums ? (pDoc->GetChannelType(i) == CHANID_NOISE) : IsMelodyChannel(pDoc, i);
		if (!Fits)
			continue;
		CString Name = RecordingChannelName(pDoc, i);
		if (!Drums && IsBassChannel(pDoc, i))
			Name += _T(" (bass)");
		const int Item = List.AddString(Name);
		List.SetItemData(Item, static_cast<DWORD_PTR>(i));
		if (Drums || i == MelodyChannel)
			List.SetCurSel(Item);
	}
	if (List.GetCurSel() == CB_ERR && List.GetCount() > 0)
		List.SetCurSel(0);
	List.EnableWindow(!Drums);
}

int PickedRecordingChannel(const CComboBox &List)
{
	const int Selected = List.GetCurSel();
	return (Selected != CB_ERR) ? static_cast<int>(List.GetItemData(Selected)) : -1;
}

IMPLEMENT_DYNAMIC(CInsertRecordingDlg, CDialog)

CInsertRecordingDlg::CInsertRecordingDlg(const CFamiTrackerDoc *pDoc, LPCTSTR Path, recording_kind_t Guess,
	LPCTSTR GuessText, int CursorChannel, CWnd *pParent) :
	CDialog(CInsertRecordingDlg::IDD, pParent),
	m_pDocument(pDoc),
	m_sPath(Path),
	m_iGuess(Guess),
	m_sGuessText(GuessText),
	m_iKind(Guess),
	m_iChannel(-1),
	m_iMelodyChannel(DefaultRecordingChannel(pDoc, CursorChannel)),
	m_bExpression(true)
{
}

BEGIN_MESSAGE_MAP(CInsertRecordingDlg, CDialog)
	ON_BN_CLICKED(IDC_REC_AUTO, &CInsertRecordingDlg::OnKindChanged)
	ON_BN_CLICKED(IDC_REC_WHISTLE, &CInsertRecordingDlg::OnKindChanged)
	ON_BN_CLICKED(IDC_REC_VOICE, &CInsertRecordingDlg::OnKindChanged)
	ON_BN_CLICKED(IDC_REC_DRUMS, &CInsertRecordingDlg::OnKindChanged)
END_MESSAGE_MAP()

BOOL CInsertRecordingDlg::OnInitDialog()
{
	CDialog::OnInitDialog();

	m_cChannel.SubclassDlgItem(IDC_REC_CHANNEL, this);

	CString Name = m_sPath;	// the file name without its folder
	const int Slash = Name.ReverseFind(_T('\\'));
	if (Slash >= 0)
		Name = Name.Mid(Slash + 1);
	SetDlgItemText(IDC_REC_FILE, Name);

	CString Detected;
	Detected.Format(_T("(sounds like %s)"), static_cast<LPCTSTR>(m_sGuessText));
	SetDlgItemText(IDC_REC_DETECTED, Detected);

	CheckRadioButton(IDC_REC_AUTO, IDC_REC_DRUMS, IDC_REC_AUTO);
	CheckDlgButton(IDC_REC_EXPRESSION, BST_CHECKED);
	FillChannels();

	return TRUE;
}

recording_kind_t CInsertRecordingDlg::SelectedKind() const
{
	if (IsDlgButtonChecked(IDC_REC_WHISTLE))
		return RECORDING_WHISTLE;
	if (IsDlgButtonChecked(IDC_REC_VOICE))
		return RECORDING_VOICE;
	if (IsDlgButtonChecked(IDC_REC_DRUMS))
		return RECORDING_DRUMS;
	return m_iGuess;	// let FamiTracker decide
}

void CInsertRecordingDlg::FillChannels()
{
	// Keep the melody channel picked so far
	const int Picked = PickedRecordingChannel(m_cChannel);
	if (Picked >= 0 && IsMelodyChannel(m_pDocument, Picked))
		m_iMelodyChannel = Picked;

	const bool Drums = (SelectedKind() == RECORDING_DRUMS);
	FillRecordingChannels(m_cChannel, m_pDocument, Drums, m_iMelodyChannel);
	GetDlgItem(IDC_REC_EXPRESSION)->EnableWindow(!Drums);	// drums keep their loudness anyway
}

void CInsertRecordingDlg::OnKindChanged()
{
	FillChannels();
}

void CInsertRecordingDlg::OnOK()
{
	m_iKind = SelectedKind();
	m_iChannel = PickedRecordingChannel(m_cChannel);
	m_bExpression = IsDlgButtonChecked(IDC_REC_EXPRESSION) == BST_CHECKED;
	CDialog::OnOK();
}

recording_kind_t CInsertRecordingDlg::GetKind() const
{
	return m_iKind;
}

int CInsertRecordingDlg::GetChannel() const
{
	return m_iChannel;
}

bool CInsertRecordingDlg::GetExpression() const
{
	return m_bExpression;
}

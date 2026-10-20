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
	m_iMelodyChannel(-1),
	m_bExpression(true)
{
	// A melody goes where the cursor is if it can, otherwise on the first VRC6
	// pulse, otherwise on the first channel
	const int Channels = static_cast<int>(pDoc->GetAvailableChannels());
	if (CursorChannel >= 0 && CursorChannel < Channels && IsMelodyChannel(pDoc, CursorChannel))
		m_iMelodyChannel = CursorChannel;
	for (int i = 0; i < Channels && m_iMelodyChannel < 0; ++i) {
		if (pDoc->GetChannelType(i) == CHANID_VRC6_PULSE1)
			m_iMelodyChannel = i;
	}
	if (m_iMelodyChannel < 0)
		m_iMelodyChannel = 0;
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
	const int Selected = m_cChannel.GetCurSel();
	if (Selected != CB_ERR) {
		const int Channel = static_cast<int>(m_cChannel.GetItemData(Selected));
		if (IsMelodyChannel(m_pDocument, Channel))
			m_iMelodyChannel = Channel;
	}

	// Drums: only the noise channel. A melody: any channel but noise and DPCM.
	const bool Drums = (SelectedKind() == RECORDING_DRUMS);
	m_cChannel.ResetContent();
	const int Channels = static_cast<int>(m_pDocument->GetAvailableChannels());
	for (int i = 0; i < Channels; ++i) {
		const bool Fits = Drums ? (m_pDocument->GetChannelType(i) == CHANID_NOISE) : IsMelodyChannel(m_pDocument, i);
		if (!Fits)
			continue;
		CString Name = RecordingChannelName(m_pDocument, i);
		if (!Drums && IsBassChannel(m_pDocument, i))
			Name += _T(" (bass)");
		const int Item = m_cChannel.AddString(Name);
		m_cChannel.SetItemData(Item, static_cast<DWORD_PTR>(i));
		if (Drums || i == m_iMelodyChannel)
			m_cChannel.SetCurSel(Item);
	}
	if (m_cChannel.GetCurSel() == CB_ERR && m_cChannel.GetCount() > 0)
		m_cChannel.SetCurSel(0);
	m_cChannel.EnableWindow(!Drums);
	GetDlgItem(IDC_REC_EXPRESSION)->EnableWindow(!Drums);	// drums keep their loudness anyway
}

void CInsertRecordingDlg::OnKindChanged()
{
	FillChannels();
}

void CInsertRecordingDlg::OnOK()
{
	m_iKind = SelectedKind();
	const int Selected = m_cChannel.GetCurSel();
	m_iChannel = (Selected != CB_ERR) ? static_cast<int>(m_cChannel.GetItemData(Selected)) : -1;
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

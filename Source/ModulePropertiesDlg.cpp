/*
** FamiTracker - NES/Famicom sound tracker
** Copyright (C) 2005-2014  Jonathan Liss
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

#include <vector>
#include "Api/Vrc6Plus.h"
#include "stdafx.h"
#include "FamiTracker.h"
#include "FamiTrackerDoc.h"
#include "MainFrm.h"
#include "ModulePropertiesDlg.h"
#include "ChannelMap.h"
#include "ModuleImportDlg.h"
#include "SoundGen.h"
#include "ChipSwitch.h"
#include "InstrumentLibrary.h"

LPCTSTR TRACK_FORMAT = _T("#%02i %s");

namespace {

// The expansion chips' check boxes
const struct {
	int Control;
	unsigned char Chip;
	LPCTSTR pName;
} CHIP_BOXES[] = {
	{ IDC_EXPANSION_VRC6, SNDCHIP_VRC6, _T("VRC6") },
	{ IDC_EXPANSION_VRC7, SNDCHIP_VRC7, _T("VRC7") },
	{ IDC_EXPANSION_FDS,  SNDCHIP_FDS,  _T("FDS") },
	{ IDC_EXPANSION_MMC5, SNDCHIP_MMC5, _T("MMC5") },
	{ IDC_EXPANSION_N163, SNDCHIP_N163, _T("N163") },
	{ IDC_EXPANSION_S5B,  SNDCHIP_S5B,  _T("5B") },
};

}	// namespace

// CModulePropertiesDlg dialog

//
// Contains song list editor and expansion chip selector
//

IMPLEMENT_DYNAMIC(CModulePropertiesDlg, CDialog)
CModulePropertiesDlg::CModulePropertiesDlg(CWnd* pParent /*=NULL*/)
	: CDialog(CModulePropertiesDlg::IDD, pParent), m_iSelectedSong(0), m_pDocument(NULL)
{
}

CModulePropertiesDlg::~CModulePropertiesDlg()
{
}

void CModulePropertiesDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
}


BEGIN_MESSAGE_MAP(CModulePropertiesDlg, CDialog)
	ON_BN_CLICKED(IDOK, OnBnClickedOk)
	ON_BN_CLICKED(IDC_SONG_ADD, &CModulePropertiesDlg::OnBnClickedSongAdd)
	ON_BN_CLICKED(IDC_SONG_REMOVE, &CModulePropertiesDlg::OnBnClickedSongRemove)
	ON_BN_CLICKED(IDC_SONG_UP, &CModulePropertiesDlg::OnBnClickedSongUp)
	ON_BN_CLICKED(IDC_SONG_DOWN, &CModulePropertiesDlg::OnBnClickedSongDown)
	ON_EN_CHANGE(IDC_SONGNAME, &CModulePropertiesDlg::OnEnChangeSongname)
	ON_BN_CLICKED(IDC_SONG_IMPORT, &CModulePropertiesDlg::OnBnClickedSongImport)
	ON_BN_CLICKED(IDC_EXPANSION_VRC6, &CModulePropertiesDlg::OnBnClickedExpansion)
	ON_BN_CLICKED(IDC_EXPANSION_VRC7, &CModulePropertiesDlg::OnBnClickedExpansion)
	ON_BN_CLICKED(IDC_EXPANSION_FDS, &CModulePropertiesDlg::OnBnClickedExpansion)
	ON_BN_CLICKED(IDC_EXPANSION_MMC5, &CModulePropertiesDlg::OnBnClickedExpansion)
	ON_BN_CLICKED(IDC_EXPANSION_N163, &CModulePropertiesDlg::OnBnClickedExpansion)
	ON_BN_CLICKED(IDC_EXPANSION_S5B, &CModulePropertiesDlg::OnBnClickedExpansion)
	ON_BN_CLICKED(IDC_EXPANSION_VRC6PLUS, &CModulePropertiesDlg::OnBnClickedVrc6Plus)
	ON_WM_HSCROLL()
	ON_NOTIFY(LVN_ITEMCHANGED, IDC_SONGLIST, &CModulePropertiesDlg::OnLvnItemchangedSonglist)
END_MESSAGE_MAP()


// CModulePropertiesDlg message handlers

BOOL CModulePropertiesDlg::OnInitDialog()
{
	CDialog::OnInitDialog();

	// Get active document
	CFrameWnd *pFrameWnd = static_cast<CFrameWnd*>(GetParent());
	m_pDocument = static_cast<CFamiTrackerDoc*>(pFrameWnd->GetActiveDocument());

	CListCtrl *pSongList = static_cast<CListCtrl*>(GetDlgItem(IDC_SONGLIST));
	pSongList->InsertColumn(0, _T("Songs"), 0, 150);
	pSongList->SendMessage(LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_FULLROWSELECT);

	FillSongList();

	// Expansion chips: any of them can be on
	const unsigned char Chips = m_pDocument->GetExpansionChip();
	for (size_t i = 0; i < sizeof(CHIP_BOXES) / sizeof(CHIP_BOXES[0]); ++i)
		CheckDlgButton(CHIP_BOXES[i].Control, (Chips & CHIP_BOXES[i].Chip) ? BST_CHECKED : BST_UNCHECKED);

	// Vibrato 
	CComboBox *pVibratoBox = static_cast<CComboBox*>(GetDlgItem(IDC_VIBRATO));
	pVibratoBox->SetCurSel((m_pDocument->GetVibratoStyle() == VIBRATO_NEW) ? 0 : 1);

	// Namco channel count
	CSliderCtrl *pChanSlider = static_cast<CSliderCtrl*>(GetDlgItem(IDC_CHANNELS));
	pChanSlider->SetRange(1, 8);
	pChanSlider->SetPos(m_pDocument->GetNamcoChannels());

	// VRC6 Plus (Api/Vrc6Plus.h): the VRC6 and four N163 channels as one chip
	CheckDlgButton(IDC_EXPANSION_VRC6PLUS, m_pDocument->IsVrc6Plus() ? BST_CHECKED : BST_UNCHECKED);
	UpdateVrc6Plus();

	return TRUE;  // return TRUE unless you set the focus to a control
	// EXCEPTION: OCX Property Pages should return FALSE
}

unsigned char CModulePropertiesDlg::CheckedChips() const
{
	unsigned char Chips = SNDCHIP_NONE;
	for (size_t i = 0; i < sizeof(CHIP_BOXES) / sizeof(CHIP_BOXES[0]); ++i) {
		if (IsDlgButtonChecked(CHIP_BOXES[i].Control) == BST_CHECKED)
			Chips |= CHIP_BOXES[i].Chip;
	}
	return Chips;
}

void CModulePropertiesDlg::UpdateNamcoChannels()
{
	// The N163's channel count, which its check box turns on (the VRC6 Plus's are four)
	const bool Namco = (CheckedChips() & SNDCHIP_N163) != 0;
	const bool Plus = IsDlgButtonChecked(IDC_EXPANSION_VRC6PLUS) == BST_CHECKED;
	CSliderCtrl *pSlider = static_cast<CSliderCtrl*>(GetDlgItem(IDC_CHANNELS));
	if (Plus)
		pSlider->SetPos(ftapi::VRC6_PLUS_SOFT_CHANNELS);
	pSlider->EnableWindow(Namco && !Plus ? TRUE : FALSE);
	CString channelsStr;
	channelsStr.LoadString(IDS_PROPERTIES_CHANNELS);
	if (Namco)
		channelsStr.AppendFormat(_T(" %i"), pSlider->GetPos());
	else
		channelsStr.Append(_T(" N/A"));
	SetDlgItemText(IDC_CHANNELS_NR, channelsStr);
}

void CModulePropertiesDlg::UpdateVrc6Plus()
{
	// VRC6 Plus takes the place of the VRC6 and the N163: their boxes show ticked, and stay so while it is
	const bool Plus = IsDlgButtonChecked(IDC_EXPANSION_VRC6PLUS) == BST_CHECKED;
	if (Plus) {
		CheckDlgButton(IDC_EXPANSION_VRC6, BST_CHECKED);
		CheckDlgButton(IDC_EXPANSION_N163, BST_CHECKED);
	}
	GetDlgItem(IDC_EXPANSION_VRC6)->EnableWindow(Plus ? FALSE : TRUE);
	GetDlgItem(IDC_EXPANSION_N163)->EnableWindow(Plus ? FALSE : TRUE);
	UpdateNamcoChannels();
}

void CModulePropertiesDlg::OnBnClickedVrc6Plus()
{
	// Unticked, it's the plain VRC6 again: the soft layer's N163 goes (asking first, on OK)
	if (IsDlgButtonChecked(IDC_EXPANSION_VRC6PLUS) != BST_CHECKED) {
		CheckDlgButton(IDC_EXPANSION_VRC6, BST_CHECKED);
		CheckDlgButton(IDC_EXPANSION_N163, BST_UNCHECKED);
	}
	UpdateVrc6Plus();
}

bool CModulePropertiesDlg::SwitchChips(unsigned char Chips, unsigned int NamcoChannels, bool Vrc6Plus)
{
	// The channels that go take their notes with them: ask first (ChipSwitch.h)
	const bool WasPlus = m_pDocument->IsVrc6Plus();
	const std::vector<unsigned int> Going = LeavingChannels(m_pDocument, Chips, NamcoChannels);
	std::vector<int> Tracks;
	const int Notes = CountChannelNotes(m_pDocument, Going, Tracks);
	if (Notes > 0) {
		const unsigned char Old = m_pDocument->GetExpansionChip();
		CString What;
		if (WasPlus) {	// its channels as the VRC6 Plus's
			const bool Vrc6 = !(Chips & SNDCHIP_VRC6), Namco = !(Chips & SNDCHIP_N163);
			What = Vrc6 && Namco ? _T("the VRC6 Plus's channels") : Namco ? _T("the VRC6 Plus's soft channels (Soft 1 to Soft 4)") :
				Vrc6 ? _T("the VRC6 Plus's pulses and sawtooth") : _T("");
		}
		for (size_t i = 0; i < sizeof(CHIP_BOXES) / sizeof(CHIP_BOXES[0]); ++i) {
			if (WasPlus && (CHIP_BOXES[i].Chip == SNDCHIP_VRC6 || CHIP_BOXES[i].Chip == SNDCHIP_N163))
				continue;
			if ((Old & CHIP_BOXES[i].Chip) && !(Chips & CHIP_BOXES[i].Chip)) {
				if (!What.IsEmpty())
					What += _T(" and ");
				What += CString(_T("the ")) + CHIP_BOXES[i].pName + _T("'s channels");
			}
		}
		if ((Old & Chips & SNDCHIP_N163) && NamcoChannels < m_pDocument->GetNamcoChannels()) {
			if (!What.IsEmpty())
				What += _T(" and ");
			CString Namco;
			Namco.Format(_T("N163 channels %u to %u"), NamcoChannels + 1, m_pDocument->GetNamcoChannels());
			What += Namco;
		}
		CString Message;
		Message.Format(_T("Switching removes %s, with the %i notes on them (in %i of the module's tracks). ")
			_T("The effects there that set the speed or end the song move to a channel that stays. This can't be undone.\n\n")
			_T("Switch the chips anyway?"), What.GetString(), Notes, static_cast<int>(Tracks.size()));
		if (AfxMessageBox(Message, MB_YESNO | MB_ICONWARNING) != IDYES)
			return false;
	}

	if (theApp.IsPlaying())
		theApp.StopPlayerAndWait();

	int Moved = 0;
	::SwitchChips(m_pDocument, Chips, NamcoChannels, Moved);
	m_pDocument->SetVrc6Plus(Vrc6Plus);
	if (Vrc6Plus && !WasPlus)
		AddSoftLayerSounds(m_pDocument);	// what's missing of its soft layer's sounds: choir pad, string pad, soft flute, sub bass
	m_pDocument->SetModifiedFlag();
	m_pDocument->UpdateAllViews(NULL, UPDATE_PROPERTIES);
	return true;
}

void CModulePropertiesDlg::OnBnClickedOk()
{
	CMainFrame *pMainFrame = static_cast<CMainFrame*>(GetParentFrame());

	// Expansion chips (VRC6 Plus: the VRC6 and the N163 with 4 channels, marked)
	const unsigned char Chips = CheckedChips();
	const bool Plus = IsDlgButtonChecked(IDC_EXPANSION_VRC6PLUS) == BST_CHECKED;
	const unsigned int iChannels = Plus ? static_cast<unsigned int>(ftapi::VRC6_PLUS_SOFT_CHANNELS) :
		static_cast<CSliderCtrl*>(GetDlgItem(IDC_CHANNELS))->GetPos();

	if (m_pDocument->GetExpansionChip() != Chips || ((Chips & SNDCHIP_N163) && m_pDocument->GetNamcoChannels() != iChannels) ||
		m_pDocument->IsVrc6Plus() != Plus) {
		if (!SwitchChips(Chips, iChannels, Plus))
			return;		// the dialog stays open
	}

	// Vibrato 
	CComboBox *pVibratoBox = static_cast<CComboBox*>(GetDlgItem(IDC_VIBRATO));
	m_pDocument->SetVibratoStyle((pVibratoBox->GetCurSel() == 0) ? VIBRATO_NEW : VIBRATO_OLD);

	if (pMainFrame->GetSelectedTrack() != m_iSelectedSong)
		pMainFrame->SelectTrack(m_iSelectedSong);

	pMainFrame->UpdateControls();

	theApp.GetSoundGenerator()->DocumentPropertiesChanged(m_pDocument);

	OnOK();
}

void CModulePropertiesDlg::OnBnClickedSongAdd()
{
	CString TrackTitle;

	// Try to add a track
	int NewTrack = m_pDocument->AddTrack();

	if (NewTrack == -1)
		return;
	
	m_pDocument->UpdateAllViews(NULL, UPDATE_TRACK);

	TrackTitle.Format(TRACK_FORMAT, NewTrack, m_pDocument->GetTrackTitle(NewTrack).GetString());
	static_cast<CListCtrl*>(GetDlgItem(IDC_SONGLIST))->InsertItem(NewTrack, TrackTitle);

	SelectSong(NewTrack);
}

void CModulePropertiesDlg::OnBnClickedSongRemove()
{
	ASSERT(m_iSelectedSong != -1);

	CListCtrl *pSongList = static_cast<CListCtrl*>(GetDlgItem(IDC_SONGLIST));
	CMainFrame *pMainFrame = static_cast<CMainFrame*>(GetParentFrame());
	unsigned Count = m_pDocument->GetTrackCount();
	CString TrackTitle;

	if (Count == 1)
		return; // Single track

	// Display warning first
	if (AfxMessageBox(IDS_SONG_DELETE, MB_OKCANCEL | MB_ICONWARNING) == IDCANCEL)
		return;

	pSongList->DeleteItem(m_iSelectedSong);
	m_pDocument->RemoveTrack(m_iSelectedSong);
	m_pDocument->UpdateAllViews(NULL, UPDATE_TRACK);

	Count = m_pDocument->GetTrackCount();	// Get new track count

	// Redraw track list
	for (unsigned int i = 0; i < Count; ++i) {
		TrackTitle.Format(_T("#%02i %s"), i + 1, m_pDocument->GetTrackTitle(i).GetString());
		pSongList->SetItemText(i, 0, TrackTitle);
	}

	if (m_iSelectedSong == Count)
		SelectSong(m_iSelectedSong - 1);
	else
		SelectSong(m_iSelectedSong);
}

void CModulePropertiesDlg::OnBnClickedSongUp()
{
	CListCtrl *pSongList = static_cast<CListCtrl*>(GetDlgItem(IDC_SONGLIST));
	int Song = m_iSelectedSong;
	CString Text;

	if (Song == 0)
		return;

	m_pDocument->MoveTrackUp(Song);
	m_pDocument->UpdateAllViews(NULL, UPDATE_TRACK);

	Text.Format(TRACK_FORMAT, Song + 1, m_pDocument->GetTrackTitle(Song).GetString());
	pSongList->SetItemText(Song, 0, Text);
	Text.Format(TRACK_FORMAT, Song, m_pDocument->GetTrackTitle(Song - 1).GetString());
	pSongList->SetItemText(Song - 1, 0, Text);

	SelectSong(Song - 1);
}

void CModulePropertiesDlg::OnBnClickedSongDown()
{
	CListCtrl *pSongList = static_cast<CListCtrl*>(GetDlgItem(IDC_SONGLIST));
	int Song = m_iSelectedSong;
	CString Text;

	if (Song == (m_pDocument->GetTrackCount() - 1))
		return;

	m_pDocument->MoveTrackDown(Song);
	m_pDocument->UpdateAllViews(NULL, UPDATE_TRACK);

	Text.Format(TRACK_FORMAT, Song + 1, m_pDocument->GetTrackTitle(Song).GetString());
	pSongList->SetItemText(Song, 0, Text);
	Text.Format(TRACK_FORMAT, Song + 2, m_pDocument->GetTrackTitle(Song + 1).GetString());
	pSongList->SetItemText(Song + 1, 0, Text);

	SelectSong(Song + 1);
}

void CModulePropertiesDlg::OnEnChangeSongname()
{
	CListCtrl *pSongList = static_cast<CListCtrl*>(GetDlgItem(IDC_SONGLIST));
	CEdit *pName = static_cast<CEdit*>(GetDlgItem(IDC_SONGNAME));
	CString Text, Title;

	if (m_iSelectedSong == -1)
		return;

	pName->GetWindowText(Text);

	Title.Format(TRACK_FORMAT, m_iSelectedSong + 1, Text);

	pSongList->SetItemText(m_iSelectedSong, 0, Title);
	m_pDocument->SetTrackTitle(m_iSelectedSong, Text);
	m_pDocument->UpdateAllViews(NULL, UPDATE_TRACK);
}

void CModulePropertiesDlg::SelectSong(int Song)
{
	CListCtrl *pSongList = static_cast<CListCtrl*>(GetDlgItem(IDC_SONGLIST));
	ASSERT(Song >= 0);

	pSongList->SetItemState(Song, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	pSongList->EnsureVisible(Song, FALSE);
}

void CModulePropertiesDlg::UpdateSongButtons()
{
	unsigned TrackCount = m_pDocument->GetTrackCount();

	GetDlgItem(IDC_SONG_REMOVE)->EnableWindow((TrackCount == 1) ? FALSE : TRUE);
	GetDlgItem(IDC_SONG_ADD)->EnableWindow((TrackCount == MAX_TRACKS) ? FALSE : TRUE);
	GetDlgItem(IDC_SONG_DOWN)->EnableWindow((m_iSelectedSong == (TrackCount - 1)) ? FALSE : TRUE);
	GetDlgItem(IDC_SONG_UP)->EnableWindow((m_iSelectedSong == 0) ? FALSE : TRUE);
	GetDlgItem(IDC_SONG_IMPORT)->EnableWindow((TrackCount == MAX_TRACKS) ? FALSE : TRUE);
}

void CModulePropertiesDlg::OnBnClickedSongImport()
{
	CModuleImportDlg importDlg(m_pDocument);

	// TODO use string table
	CFileDialog OpenFileDlg(TRUE, _T("ftm"), 0, OFN_HIDEREADONLY, _T("FamiTracker files (*.ftm)|*.ftm|All files (*.*)|*.*||"), theApp.GetMainWnd(), 0);

	if (OpenFileDlg.DoModal() == IDCANCEL)
		return;

	if (importDlg.LoadFile(OpenFileDlg.GetPathName(), m_pDocument) == false)
		return;

	importDlg.DoModal();

	FillSongList();
}

void CModulePropertiesDlg::OnBnClickedExpansion()
{
	UpdateNamcoChannels();
}

void CModulePropertiesDlg::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
	UpdateNamcoChannels();

	CDialog::OnHScroll(nSBCode, nPos, pScrollBar);
}

void CModulePropertiesDlg::OnLvnItemchangedSonglist(NMHDR *pNMHDR, LRESULT *pResult)
{
	LPNMLISTVIEW pNMLV = reinterpret_cast<LPNMLISTVIEW>(pNMHDR);

    if ((pNMLV->uChanged & LVIF_STATE) && (pNMLV->uNewState & LVNI_SELECTED)) {
		int Song = pNMLV->iItem;

		m_iSelectedSong = Song;

		CEdit *pName = static_cast<CEdit*>(GetDlgItem(IDC_SONGNAME));
		pName->SetWindowText(CString(m_pDocument->GetTrackTitle(Song).GetString()));

		UpdateSongButtons();
    }

	*pResult = 0;
}

void CModulePropertiesDlg::FillSongList()
{
	CListCtrl *pSongList = static_cast<CListCtrl*>(GetDlgItem(IDC_SONGLIST));
	CString Text;

	pSongList->DeleteAllItems();

	// Song editor
	int Songs = m_pDocument->GetTrackCount();

	for (int i = 0; i < Songs; ++i) {
		Text.Format(TRACK_FORMAT, i + 1, m_pDocument->GetTrackTitle(i).GetString());	// start counting songs from 1
		pSongList->InsertItem(i, Text);
	}

	// Select first song when dialog is displayed
	SelectSong(0);
}

BOOL CModulePropertiesDlg::PreTranslateMessage(MSG* pMsg)
{
	CListCtrl *pSongList = static_cast<CListCtrl*>(GetDlgItem(IDC_SONGLIST));

	if (GetFocus() == pSongList) {
		if(pMsg->message == WM_KEYDOWN) {
			switch (pMsg->wParam) {
				case VK_DELETE:
					// Delete song
					if (m_iSelectedSong != -1) {
						OnBnClickedSongRemove();
					}
					break;
				case VK_INSERT:
					// Insert song
					OnBnClickedSongAdd();
					break;
			}
		}
	}

	return CDialog::PreTranslateMessage(pMsg);
}

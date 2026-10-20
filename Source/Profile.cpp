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

// Profiles (see Profile.h)

// Standard and API headers come before stdafx.h, whose DEBUG_NEW macro would
// otherwise rewrite the placement new inside the standard library.
#include <string>
#include <vector>
#include "Api/Presets.h"
#include "Api/Music.h"
#include "stdafx.h"
#include "FamiTracker.h"
#include "FamiTrackerDoc.h"
#include "Settings.h"
#include "InstrumentLibrary.h"
#include "Profile.h"

namespace {

const int TEMPLATE_BPM = 120;

// The instruments a new game music song starts with; the first is the one
// selected, for the melody on Pulse 1
struct template_sound_t {
	const char *Preset;
	bool VRC6;
	bool Sawtooth;
};

const template_sound_t TEMPLATE_SOUNDS[] = {
	{ "bright lead", false, false },	// the pulses: a melody
	{ "soft chords", false, false },	// and chords
	{ "bass", false, false },			// the triangle
	{ "kick", false, false },			// the noise
	{ "snare", false, false },
	{ "hi-hat", false, false },
	{ "warm pad", true, false },		// the VRC6 pulses
	{ "bass", true, true },				// the VRC6 sawtooth
};

// Menu items "Fewer menu items" hides; their shortcut keys still work
const UINT HIDDEN_ITEMS[] = {
	ID_FILE_IMPORTTEXT, ID_FILE_EXPORTTEXT,
	ID_EDIT_PASTEMIX, ID_EDIT_INTERPOLATE, ID_EDIT_REVERSE, ID_EDIT_REPLACEINSTRUMENT, ID_EDIT_EXPANDPATTERNS,
	ID_EDIT_SHRINKPATTERNS, ID_EDIT_INSTRUMENTMASK, ID_EDIT_VOLUMEMASK,
	ID_MODULE_DUPLICATEFRAMEPATTERNS, ID_MODULE_CHANNELS,
	ID_INSTRUMENT_DEEPCLONE,
	ID_TRACKER_SWITCHTOTRACKINSTRUMENT, ID_TRACKER_DISPLAYREGISTERSTATE, ID_TRACKER_DPCM, ID_TRACKER_NTSC, ID_TRACKER_PAL,
	ID_HELP_PERFORMANCE,
};

// and submenus, known by an item in them: Edit > Clean up, Tracker > Engine speed
const UINT HIDDEN_SUBMENUS[] = {
	ID_CLEANUP_REMOVEUNUSEDINSTRUMENTS, ID_SPEED_DEFAULT,
};

bool GameMusic()
{
	return theApp.GetSettings()->Profile.iProfile == PROFILE_GAME_MUSIC;
}

bool InList(const UINT *pList, size_t Count, UINT Id)
{
	for (size_t i = 0; i < Count; ++i)
		if (pList[i] == Id)
			return true;
	return false;
}

bool IsSeparator(CMenu *pMenu, int Index)
{
	// (A submenu's state has its item count in the high byte, which can look like MF_SEPARATOR)
	return pMenu->GetSubMenu(Index) == NULL && (pMenu->GetMenuState(static_cast<UINT>(Index), MF_BYPOSITION) & MF_SEPARATOR) != 0;
}

// No separator first, last or after another
void TidySeparators(CMenu *pMenu)
{
	bool AfterSeparator = true;		// the top counts as one
	for (int i = 0; i < static_cast<int>(pMenu->GetMenuItemCount()); ) {
		const bool Separator = IsSeparator(pMenu, i);
		if (Separator && AfterSeparator) {
			pMenu->DeleteMenu(static_cast<UINT>(i), MF_BYPOSITION);
			continue;
		}
		AfterSeparator = Separator;
		++i;
	}
	const int Count = static_cast<int>(pMenu->GetMenuItemCount());
	if (Count > 0 && IsSeparator(pMenu, Count - 1))
		pMenu->DeleteMenu(static_cast<UINT>(Count - 1), MF_BYPOSITION);
}

}	// namespace

// View > Profile

class CProfileDlg : public CDialog
{
public:
	CProfileDlg(CWnd *pParent) : CDialog(IDD_PROFILE, pParent) {}

protected:
	virtual BOOL OnInitDialog();
	virtual void OnOK();
	afx_msg void OnChoice();

	DECLARE_MESSAGE_MAP()

private:
	void EnableChecks();
};

BEGIN_MESSAGE_MAP(CProfileDlg, CDialog)
	ON_BN_CLICKED(IDC_PROFILE_GAME, OnChoice)
	ON_BN_CLICKED(IDC_PROFILE_EVERYTHING, OnChoice)
END_MESSAGE_MAP()

BOOL CProfileDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	const CSettings *pSettings = theApp.GetSettings();
	CheckRadioButton(IDC_PROFILE_GAME, IDC_PROFILE_EVERYTHING,
		pSettings->Profile.iProfile == PROFILE_GAME_MUSIC ? IDC_PROFILE_GAME : IDC_PROFILE_EVERYTHING);
	CheckDlgButton(IDC_PROFILE_TEMPLATE, pSettings->Profile.bTemplate ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_PROFILE_BPM, pSettings->Profile.bBPM ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_PROFILE_MENUS, pSettings->Profile.bFewerMenus ? BST_CHECKED : BST_UNCHECKED);
	CString Text;
	Text.Format(_T("New songs start ready for game music: the VRC6 chip, %d BPM and %d instruments to play with ")
		_T("(a lead, chords, a bass, drums, a pad and a sawtooth bass)"), TEMPLATE_BPM,
		static_cast<int>(sizeof(TEMPLATE_SOUNDS) / sizeof(TEMPLATE_SOUNDS[0])));
	SetDlgItemText(IDC_PROFILE_TEMPLATE, Text);
	EnableChecks();
	return TRUE;
}

void CProfileDlg::EnableChecks()
{
	const BOOL Game = IsDlgButtonChecked(IDC_PROFILE_GAME) == BST_CHECKED ? TRUE : FALSE;
	GetDlgItem(IDC_PROFILE_TEMPLATE)->EnableWindow(Game);
	GetDlgItem(IDC_PROFILE_BPM)->EnableWindow(Game);
	GetDlgItem(IDC_PROFILE_MENUS)->EnableWindow(Game);
}

void CProfileDlg::OnChoice()
{
	EnableChecks();
}

void CProfileDlg::OnOK()
{
	CSettings *pSettings = theApp.GetSettings();
	pSettings->Profile.iProfile = IsDlgButtonChecked(IDC_PROFILE_GAME) == BST_CHECKED ? PROFILE_GAME_MUSIC : PROFILE_EVERYTHING;
	pSettings->Profile.bTemplate = IsDlgButtonChecked(IDC_PROFILE_TEMPLATE) == BST_CHECKED;
	pSettings->Profile.bBPM = IsDlgButtonChecked(IDC_PROFILE_BPM) == BST_CHECKED;
	pSettings->Profile.bFewerMenus = IsDlgButtonChecked(IDC_PROFILE_MENUS) == BST_CHECKED;
	CDialog::OnOK();
}

bool ProfileSongTemplate()
{
	return GameMusic() && theApp.GetSettings()->Profile.bTemplate;
}

bool ProfileBPM()
{
	return GameMusic() && theApp.GetSettings()->Profile.bBPM;
}

bool ProfileFewerMenus()
{
	return GameMusic() && theApp.GetSettings()->Profile.bFewerMenus;
}

int TemplateBPM()
{
	return TEMPLATE_BPM;
}

double SongBPM(int Speed, int Tempo, int FrameRate, int RowsPerBeat)
{
	return ftapi::BeatsPerMinute(Speed, Tempo, FrameRate, RowsPerBeat > 0 ? RowsPerBeat : 4);
}

void SpeedTempoForBPM(int Bpm, int RowsPerBeat, int FrameRate, int CurrentSpeed, int SplitPoint, int &Speed, int &Tempo)
{
	ftapi::ChooseSpeedTempo(Bpm, RowsPerBeat > 0 ? RowsPerBeat : 4, FrameRate, CurrentSpeed, SplitPoint, Speed, Tempo);
}

void ApplySongTemplate(CFamiTrackerDoc *pDoc)
{
	pDoc->LockDocument();
	pDoc->SelectExpansionChip(SNDCHIP_VRC6);
	pDoc->UnlockDocument();

	const int Beat = pDoc->GetFirstHighlight() > 0 ? pDoc->GetFirstHighlight() : 4;
	int Speed = 6, Tempo = 150;
	ftapi::ChooseSpeedTempo(TEMPLATE_BPM, Beat, static_cast<int>(pDoc->GetFrameRate()), static_cast<int>(pDoc->GetSongSpeed(0)),
		pDoc->GetSpeedSplitPoint(), Speed, Tempo);
	pDoc->SetSongSpeed(0, static_cast<unsigned int>(Speed));
	pDoc->SetSongTempo(0, static_cast<unsigned int>(Tempo));

	for (size_t i = 0; i < sizeof(TEMPLATE_SOUNDS) / sizeof(TEMPLATE_SOUNDS[0]); ++i) {
		const ftapi::Preset *pPreset = ftapi::FindPreset(TEMPLATE_SOUNDS[i].Preset);
		CString Error;
		if (pPreset != NULL)
			AddPresetInstrument(pDoc, *pPreset, TEMPLATE_SOUNDS[i].VRC6, TEMPLATE_SOUNDS[i].Sawtooth, Error, false);
	}
	pDoc->SetModifiedFlag(FALSE);
}

void RemoveAdvancedMenuItems(CMenu *pMenu)
{
	const size_t Items = sizeof(HIDDEN_ITEMS) / sizeof(HIDDEN_ITEMS[0]);
	const size_t Submenus = sizeof(HIDDEN_SUBMENUS) / sizeof(HIDDEN_SUBMENUS[0]);
	for (int i = static_cast<int>(pMenu->GetMenuItemCount()) - 1; i >= 0; --i) {
		CMenu *pSubmenu = pMenu->GetSubMenu(i);
		if (pSubmenu != NULL) {
			bool Hidden = false;
			for (int k = 0; k < static_cast<int>(pSubmenu->GetMenuItemCount()) && !Hidden; ++k)
				Hidden = InList(HIDDEN_SUBMENUS, Submenus, pSubmenu->GetMenuItemID(k));
			if (Hidden)
				pMenu->DeleteMenu(static_cast<UINT>(i), MF_BYPOSITION);
			else
				RemoveAdvancedMenuItems(pSubmenu);
		}
		else if (InList(HIDDEN_ITEMS, Items, pMenu->GetMenuItemID(i)))
			pMenu->DeleteMenu(static_cast<UINT>(i), MF_BYPOSITION);
	}
	TidySeparators(pMenu);
}

bool ShowProfileDialog(CWnd *pParent)
{
	const CSettings *pSettings = theApp.GetSettings();
	const int Profile = pSettings->Profile.iProfile;
	const bool Template = pSettings->Profile.bTemplate, BPM = pSettings->Profile.bBPM, Menus = pSettings->Profile.bFewerMenus;
	CProfileDlg Dialog(pParent);
	if (Dialog.DoModal() != IDOK)
		return false;
	return pSettings->Profile.iProfile != Profile || pSettings->Profile.bTemplate != Template || pSettings->Profile.bBPM != BPM ||
		pSettings->Profile.bFewerMenus != Menus;
}

bool AskForProfile(CWnd *pParent)
{
	CString Text;
	Text.Format(_T("Would you like FamiTracker made simpler for writing game music?\n\n")
		_T("- New songs start with the VRC6 chip, %d BPM and instruments ready to play.\n")
		_T("- The tempo shows as BPM instead of speed and tempo.\n")
		_T("- Rarely used menu items are hidden.\n\n")
		_T("Songs stay ordinary FamiTracker songs either way. View > Profile changes this at any time, ")
		_T("and View > Show everything brings back all of FamiTracker."), TEMPLATE_BPM);
	const bool Yes = pParent->MessageBox(Text, _T("FamiTracker"), MB_YESNO | MB_ICONQUESTION) == IDYES;
	CSettings *pSettings = theApp.GetSettings();
	pSettings->Profile.iProfile = Yes ? PROFILE_GAME_MUSIC : PROFILE_EVERYTHING;
	pSettings->SaveSettings();
	return Yes;
}

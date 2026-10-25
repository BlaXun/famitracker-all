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

#pragma once

#include "TapButton.h"

class CFamiTrackerView;
class CFamiTrackerDoc;

// Edit > Start from a groove (and the Record dialog's button of that name):
// pick a mood, a tempo (typed or tapped) and a key, and FamiTracker writes a
// few bars of drums, a bass line and chords from the cursor's bar on, for the
// user to sing a melody over (MakeGroove in InsertRecording.h). DoModal
// returns IDOK once the groove is in; the cursor is then where the melody
// goes, and GetResult says what was written.
class CGrooveDlg : public CDialog
{
	DECLARE_DYNAMIC(CGrooveDlg)

public:
	CGrooveDlg(CFamiTrackerView *pView, CWnd *pParent = NULL);

	enum { IDD = IDD_GROOVE };

	CString GetResult() const;
	int GetMelodyChannel() const;

protected:
	virtual BOOL OnInitDialog();
	virtual void OnOK();
	virtual BOOL PreTranslateMessage(MSG *pMsg);
	afx_msg void OnMoodChanged();
	afx_msg void OnKeyChanged();
	afx_msg void OnBarsChanged();
	afx_msg void OnBpmChanged();
	afx_msg LRESULT OnTempoTap(WPARAM wParam, LPARAM lParam);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnDestroy();

	DECLARE_MESSAGE_MAP()

private:
	int Picked(const CComboBox &List) const;	// the item data, -1 for none
	void MoodDefaults();						// the mood's tempo and key, unless the user set them
	int ReadBpm() const;
	void SetBpm(int Bpm);
	void UpdatePreview();

	CFamiTrackerView *m_pView;
	CFamiTrackerDoc *m_pDocument;
	unsigned int m_iTrack;

	CComboBox m_cMood;
	CComboBox m_cKey;
	CComboBox m_cBars;
	CTapButton m_cTap;
	CTapTempo m_Taps;

	bool m_bTempoSet;		// typed or tapped: a mood picked afterwards keeps it
	bool m_bKeyPicked;		// by the user: a mood picked afterwards keeps it
	int m_iSongTonic;		// the song's own key (Api/Music.h): its tonic and mode, -1 for none
	int m_iSongMode;
	bool m_bSettingBpm;		// the dialog is writing the tempo, not the user
	int m_iRow;				// song row of the bar the groove starts at
	int m_iMelody;			// where the melody goes, and the channels the groove goes on (-1: none)
	int m_iChords, m_iBass, m_iDrums;
	CString m_sResult;
};

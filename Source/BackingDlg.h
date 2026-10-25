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

#include "InsertRecording.h"

class CFamiTrackerView;
class CFamiTrackerDoc;

// Edit > Add chords and bass line: the chords (found from the melody, typed by
// name, or strummed on a guitar into the microphone or a WAV file and heard),
// which channel has the melody, the key (found, or picked), where the chords
// and the bass line go and how often found chords change, with a preview of
// the chords.
class CBackingDlg : public CDialog
{
	DECLARE_DYNAMIC(CBackingDlg)

public:
	CBackingDlg(CFamiTrackerView *pView);

	enum { IDD = IDD_BACKING };

	// Chords to start with, heard in a strummed take ("| C | G | Am F |"), from the bar of song row Row on
	void SetChords(const CString &Names, int Row);

	backing_choice_t GetChoice() const;

protected:
	virtual BOOL OnInitDialog();
	virtual void OnOK();
	afx_msg void OnMelodyChanged();
	afx_msg void OnOptionChanged();
	afx_msg void OnChordsChanged();
	afx_msg void OnStrum();
	afx_msg void OnFromFile();

	DECLARE_MESSAGE_MAP()

private:
	backing_choice_t ReadChoice() const;
	void FillMelody(int CursorChannel);
	void FillKeys();
	void FillChannels(CComboBox &List, bool Chords);
	void UpdatePreview();

	void UseHeardChords(const CString &Names, int Row);

	CFamiTrackerView *m_pView;
	CFamiTrackerDoc *m_pDocument;
	unsigned int m_iTrack;
	backing_choice_t m_Choice;
	CString m_sChords;		// heard or given before the dialog opened
	int m_iChordsRow;		// song row of the bar the chords start in, -1: the melody's first (or the cursor's)
	int m_iCursorBar;		// song row of the cursor's bar
	bool m_bSettingChords;	// the dialog is writing the chords, not the user

	CComboBox m_cMelody;
	CComboBox m_cKey;
	CComboBox m_cChords;
	CComboBox m_cBass;
	CComboBox m_cChanges;
};

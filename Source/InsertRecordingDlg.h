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

class CFamiTrackerDoc;

// Channel lists for the recording dialogs. A melody can go on any channel but
// noise and DPCM (bass channels are marked); drums only on the noise channel.
// The list's item data is the channel index.
int DefaultRecordingChannel(const CFamiTrackerDoc *pDoc, int CursorChannel);	// for a melody
void FillRecordingChannels(CComboBox &List, const CFamiTrackerDoc *pDoc, bool Drums, int MelodyChannel);
int PickedRecordingChannel(const CComboBox &List);	// -1 when there is none

// Edit > Insert recording asks here what the recording holds (preselected
// with FamiTracker's guess) and which channel it goes on.
class CInsertRecordingDlg : public CDialog
{
	DECLARE_DYNAMIC(CInsertRecordingDlg)

public:
	CInsertRecordingDlg(const CFamiTrackerDoc *pDoc, LPCTSTR Path, recording_kind_t Guess, LPCTSTR GuessText,
		int CursorChannel, CWnd *pParent = NULL);

	enum { IDD = IDD_INSERT_RECORDING };

	recording_kind_t GetKind() const;	// what was picked, with "Let FamiTracker decide" resolved
	int GetChannel() const;				// channel index
	bool GetExpression() const;			// keep loudness, slides and vibrato
	bool GetAsClip() const;				// keep it as a clip instead of putting it in the song

protected:
	virtual BOOL OnInitDialog();
	virtual void OnOK();
	afx_msg void OnKindChanged();

	DECLARE_MESSAGE_MAP()

private:
	recording_kind_t SelectedKind() const;
	void FillChannels();

	const CFamiTrackerDoc *m_pDocument;
	CString m_sPath;
	recording_kind_t m_iGuess;
	CString m_sGuessText;
	recording_kind_t m_iKind;
	int m_iChannel;
	int m_iMelodyChannel;		// kept while drums are picked, for when the choice changes back
	bool m_bExpression;
	bool m_bAsClip;
	CComboBox m_cChannel;
};

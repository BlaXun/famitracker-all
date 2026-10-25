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
#include "TapButton.h"

class CFamiTrackerView;
class CFamiTrackerDoc;
class CMicInput;
class CClickOutput;
struct stRecordTake;

// Edit > Record from microphone (and the toolbar's microphone button): records
// a take from the microphone, counting in and clicking along at the song's
// tempo and playing the song if asked (from a bar before the take), and puts
// its notes in at the cursor, or over the selected rows only. The tempo can be
// tapped, and Start from a groove writes drums, a bass line and chords to sing
// over first. DoModal returns IDOK when notes went in (GetResult says what),
// IDC_REC_FROMFILE when a WAV file should be inserted instead, and
// IDC_REC_CHORDS when strummed chords were heard (GetChords, GetChordsRow) for
// Add chords and bass line; with ChordsOnly it records nothing else.
class CRecordDlg : public CDialog
{
	DECLARE_DYNAMIC(CRecordDlg)

public:
	CRecordDlg(CFamiTrackerView *pView, CWnd *pParent = NULL, bool ChordsOnly = false);
	virtual ~CRecordDlg();

	enum { IDD = IDD_RECORD };

	void OpenGrooveFirst();			// Edit > Start from a groove: its dialog opens over this one
	CString GetResult() const;
	CString GetChords() const;		// "| C | G | Am F |"
	int GetChordsRow() const;		// song row of the bar they start in

protected:
	virtual BOOL OnInitDialog();
	virtual void OnOK();
	virtual void OnCancel();
	virtual BOOL PreTranslateMessage(MSG *pMsg);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnDestroy();
	afx_msg void OnRecord();
	afx_msg void OnTest();
	afx_msg void OnFromFile();
	afx_msg void OnGroove();
	afx_msg void OnKindChanged();
	afx_msg void OnDeviceChanged();
	afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar *pScrollBar);
	afx_msg LRESULT OnTempoTap(WPARAM wParam, LPARAM lParam);

	DECLARE_MESSAGE_MAP()

private:
	enum state_t {
		STATE_IDLE,			// listening to the microphone for the level meter
		STATE_COUNT_IN,		// clicking the count-in
		STATE_RECORDING,
		STATE_TEST			// clicking for taps, to measure the delay
	};
	enum click_t {			// kept in the registry: add new ones at the end
		CLICK_ALONG,		// count in, then click every beat
		CLICK_COUNT_IN,		// count in only
		CLICK_NONE,
		CLICK_BARS			// count in, then click the first beat of each bar
	};

	recording_kind_t SelectedKind(bool &Auto) const;
	void SetKind(int ID);
	void ReadSelection();
	void FillChannels();
	void FillDevices();
	void OpenDevice();
	void LoadOptions();
	void SaveOptions() const;
	void EnableOptions(bool Enable);
	void SetStatus(LPCTSTR Text);
	CString IdleStatus() const;
	void ShowLevel(float Peak);
	void ShowDelay();
	void ShowTempo();
	void UpdateTaps();
	double Delay() const;
	click_t ClickMode() const;
	double ClickLevel() const;
	void ShowClickVolume();
	void PreviewClick();
	int CursorSongRow() const;

	void StartTake();
	void StartSong();
	void StopSong();
	void CancelTake(LPCTSTR Status);
	void FinishTake();
	void UpdateCountIn();
	void UpdateRecording();
	void UpdateTest();
	void FinishTest();
	long long Played() const;
	double SecondsIntoTake() const;

	CFamiTrackerView *m_pView;
	CFamiTrackerDoc *m_pDocument;
	CMicInput *m_pInput;
	CClickOutput *m_pClick;
	stRecordTake *m_pTake;

	CComboBox m_cChannel;
	CComboBox m_cDevice;
	CComboBox m_cClick;
	CSliderCtrl m_cClickVolume;
	CProgressCtrl m_cLevel;
	CTapButton m_cTap;
	CTapTempo m_Taps;

	state_t m_iState;
	int m_iMelodyChannel;		// kept while drums are picked
	CString m_sDevice;			// microphone picked, by name ("" = Windows' choice)
	int m_iDelayMs;				// delay measured by Test timing, in ms
	float m_fLevel;				// level shown, dB
	int m_iLoudTicks;			// ticks left to show "too loud"
	int m_iQuietTicks;			// ticks in a row without sound
	int m_iTick;
	int m_iPreviewTicks;		// timer ticks until the click heard while setting its volume stops
	CString m_sResult;

	bool m_bChordsOnly;			// for Add chords and bass line: strummed chords only
	bool m_bGrooveFirst;
	CString m_sChords;			// the chords heard
	int m_iChordsRow;
	bool m_bHasSelection;		// rows selected when the dialog opened (the pattern editor's, or the piano roll's bars)
	take_selection_t m_Selection;
	CString m_sSelection;		// in words
};

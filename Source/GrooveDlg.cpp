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

// Edit > Start from a groove: a mood, a tempo and a key, and a few bars of
// drums, a bass line and chords to sing a melody over. The make_groove tool
// writes them (MakeGroove in InsertRecording.cpp), the same groove Claude Code
// gets; the preview's chords and parts come from ftapi::MakeGrooveParts
// (Api/Grooves.h), which make_groove makes its parts with.

// Standard headers come before stdafx.h, whose DEBUG_NEW macro would
// otherwise rewrite the placement new inside the standard library.
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include "stdafx.h"
#include "Api/Grooves.h"
#include "Api/Music.h"
#include "FamiTracker.h"
#include "FamiTrackerDoc.h"
#include "FamiTrackerView.h"
#include "MainFrm.h"
#include "PatternEditor.h"
#include "InsertRecording.h"
#include "TapButton.h"
#include "GrooveDlg.h"

namespace {

LPCTSTR SECTION = _T("Recorder");		// registry section, shared with the Record dialog
const UINT_PTR TMR_TAPS = 1;
const UINT TAPS_MS = 100;
const int BAR_CHOICES[] = { 4, 8, 16 };
const int SLOWEST = 40, FASTEST = 240;	// BPM

// The tools' view of a channel, for the parts' registers and the chords' style
ftapi::ChannelInfo ChannelOf(const CFamiTrackerDoc *pDoc, int Channel)
{
	ftapi::ChannelInfo Info;
	if (Channel < 0)
		return Info;
	Info.ChanId = pDoc->GetChannelType(Channel);
	switch (Info.ChanId) {
	case CHANID_SQUARE1: case CHANID_SQUARE2:
		Info.Chip = "2A03";
		Info.Kind = ftapi::KIND_PULSE;
		break;
	case CHANID_TRIANGLE:
		Info.Chip = "2A03";
		Info.Kind = ftapi::KIND_TRIANGLE;
		break;
	case CHANID_NOISE:
		Info.Chip = "2A03";
		Info.Kind = ftapi::KIND_NOISE;
		break;
	case CHANID_VRC6_PULSE1: case CHANID_VRC6_PULSE2:
		Info.Chip = "VRC6";
		Info.Kind = ftapi::KIND_PULSE;
		break;
	case CHANID_VRC6_SAWTOOTH:
		Info.Chip = "VRC6";
		Info.Kind = ftapi::KIND_SAWTOOTH;
		break;
	case CHANID_MMC5_SQUARE1: case CHANID_MMC5_SQUARE2:
		Info.Chip = "MMC5";
		Info.Kind = ftapi::KIND_PULSE;
		break;
	default:
		break;
	}
	return Info;
}

CString Capitalized(const std::string &Text)
{
	CString Out(Text.c_str());
	if (!Out.IsEmpty() && Out[0] >= _T('a') && Out[0] <= _T('z'))
		Out.SetAt(0, static_cast<TCHAR>(Out[0] - _T('a') + _T('A')));
	return Out;
}

// The tools' words for a groove's parts, as words: "root_fifth" is "root fifth", "updown" "up and down"
CString Plain(const std::string &Text)
{
	CString Out(Text.c_str());
	Out.Replace(_T("_"), _T(" "));
	Out.Replace(_T("updown"), _T("up and down"));
	return Out;
}

// Where the melody goes: the cursor's channel when a melody (not a bass line) can go there, otherwise the VRC6's
// first pulse, otherwise the 2A03's
int MelodyChannelFor(const CFamiTrackerDoc *pDoc, int Cursor)
{
	const int Channels = static_cast<int>(pDoc->GetAvailableChannels());
	if (Cursor >= 0 && Cursor < Channels && IsMelodyChannel(pDoc, Cursor) && !IsBassChannel(pDoc, Cursor))
		return Cursor;
	for (int i = 0; i < Channels; ++i)
		if (pDoc->GetChannelType(i) == CHANID_VRC6_PULSE1)
			return i;
	for (int i = 0; i < Channels; ++i)
		if (pDoc->GetChannelType(i) == CHANID_SQUARE1)
			return i;
	return 0;
}

}	// namespace

IMPLEMENT_DYNAMIC(CGrooveDlg, CDialog)

CGrooveDlg::CGrooveDlg(CFamiTrackerView *pView, CWnd *pParent) :
	CDialog(CGrooveDlg::IDD, pParent != NULL ? pParent : pView),
	m_pView(pView),
	m_pDocument(pView->GetDocument()),
	m_iTrack(static_cast<unsigned int>(static_cast<CMainFrame*>(pView->GetParentFrame())->GetSelectedTrack())),
	m_bTempoSet(false),
	m_bKeyPicked(false),
	m_bSettingBpm(false),
	m_iRow(0),
	m_iMelody(0),
	m_iChords(-1),
	m_iBass(-1),
	m_iDrums(-1)
{
}

BEGIN_MESSAGE_MAP(CGrooveDlg, CDialog)
	ON_CBN_SELCHANGE(IDC_GROOVE_MOOD, &CGrooveDlg::OnMoodChanged)
	ON_CBN_SELCHANGE(IDC_GROOVE_KEY, &CGrooveDlg::OnKeyChanged)
	ON_CBN_SELCHANGE(IDC_GROOVE_BARS, &CGrooveDlg::OnBarsChanged)
	ON_EN_CHANGE(IDC_GROOVE_BPM, &CGrooveDlg::OnBpmChanged)
	ON_MESSAGE(WM_TEMPO_TAP, &CGrooveDlg::OnTempoTap)
	ON_WM_TIMER()
	ON_WM_DESTROY()
END_MESSAGE_MAP()

BOOL CGrooveDlg::OnInitDialog()
{
	CDialog::OnInitDialog();

	m_cMood.SubclassDlgItem(IDC_GROOVE_MOOD, this);
	m_cKey.SubclassDlgItem(IDC_GROOVE_KEY, this);
	m_cBars.SubclassDlgItem(IDC_GROOVE_BARS, this);
	m_cTap.SubclassDlgItem(IDC_TAP_TEMPO, this);
	static_cast<CEdit*>(GetDlgItem(IDC_GROOVE_BPM))->SetLimitText(3);

	// The moods, as make_groove has them
	const std::vector<ftapi::GrooveMood> Moods = ftapi::GrooveMoods();
	const CString Saved = theApp.GetProfileString(SECTION, _T("Groove mood"), _T("town"));
	for (size_t i = 0; i < Moods.size(); ++i) {
		const int Item = m_cMood.AddString(Capitalized(Moods[i].Name) + _T(": ") + CString(Moods[i].Use.c_str()));
		m_cMood.SetItemData(Item, static_cast<DWORD_PTR>(i));
		if (CString(Moods[i].Name.c_str()) == Saved)
			m_cMood.SetCurSel(Item);
	}
	if (m_cMood.GetCurSel() == CB_ERR && m_cMood.GetCount() > 0)
		m_cMood.SetCurSel(0);

	// Every key: item data tonic * 2 + minor
	for (int Tonic = 0; Tonic < 12; ++Tonic) {
		for (int Minor = 0; Minor < 2; ++Minor) {
			const int Item = m_cKey.AddString(KeyName(Tonic, Minor != 0));
			m_cKey.SetItemData(Item, static_cast<DWORD_PTR>(Tonic * 2 + Minor));
		}
	}

	const int Bars = static_cast<int>(theApp.GetProfileInt(SECTION, _T("Groove bars"), 8));
	for (size_t i = 0; i < sizeof(BAR_CHOICES) / sizeof(BAR_CHOICES[0]); ++i) {
		CString Text;
		Text.Format(_T("%d bars"), BAR_CHOICES[i]);
		const int Item = m_cBars.AddString(Text);
		m_cBars.SetItemData(Item, static_cast<DWORD_PTR>(BAR_CHOICES[i]));
		if (BAR_CHOICES[i] == Bars || (i == 1 && m_cBars.GetCurSel() == CB_ERR))
			m_cBars.SetCurSel(Item);
	}

	// From the start of the cursor's bar; the melody on the cursor's channel, the parts on others
	CPatternEditor *pEditor = m_pView->GetPatternEditor();
	const int Length = static_cast<int>(m_pDocument->GetPatternLength(m_iTrack));
	const ftapi::Timing Time(Length, static_cast<int>(m_pDocument->GetFrameCount(m_iTrack)), m_pDocument->GetFirstHighlight(),
		m_pDocument->GetSecondHighlight());
	m_iRow = Time.BarStart(Time.BarOf(pEditor->GetFrame() * Length + pEditor->GetRow()));
	m_iMelody = MelodyChannelFor(m_pDocument, pEditor->GetChannel());
	GrooveChannels(m_pDocument, m_iMelody, m_iChords, m_iBass, m_iDrums);

	MoodDefaults();
	UpdatePreview();
	return TRUE;
}

int CGrooveDlg::Picked(const CComboBox &List) const
{
	const int Item = List.GetCurSel();
	return Item != CB_ERR ? static_cast<int>(List.GetItemData(Item)) : -1;
}

void CGrooveDlg::MoodDefaults()
{
	const std::vector<ftapi::GrooveMood> Moods = ftapi::GrooveMoods();
	const int Mood = Picked(m_cMood);
	if (Mood < 0 || Mood >= static_cast<int>(Moods.size()))
		return;
	if (!m_bTempoSet)
		SetBpm(Moods[static_cast<size_t>(Mood)].Bpm);
	if (!m_bKeyPicked) {
		// C major, or A minor: the white keys
		const int Wanted = Moods[static_cast<size_t>(Mood)].Minor ? 9 * 2 + 1 : 0;
		for (int i = 0; i < m_cKey.GetCount(); ++i)
			if (static_cast<int>(m_cKey.GetItemData(i)) == Wanted)
				m_cKey.SetCurSel(i);
	}
}

int CGrooveDlg::ReadBpm() const
{
	BOOL Read = FALSE;
	const int Bpm = static_cast<int>(GetDlgItemInt(IDC_GROOVE_BPM, &Read, FALSE));
	return Read ? Bpm : 0;
}

void CGrooveDlg::SetBpm(int Bpm)
{
	m_bSettingBpm = true;
	SetDlgItemInt(IDC_GROOVE_BPM, static_cast<UINT>(std::max(0, Bpm)), FALSE);
	m_bSettingBpm = false;
}

void CGrooveDlg::UpdatePreview()
{
	const std::vector<ftapi::GrooveMood> Moods = ftapi::GrooveMoods();
	const int Mood = Picked(m_cMood), Key = Picked(m_cKey), Bars = Picked(m_cBars), Bpm = ReadBpm();
	CString Text;
	bool Ready = false;
	if (Mood < 0 || Mood >= static_cast<int>(Moods.size()) || Key < 0 || Bars < 1)
		Text = _T("Pick a mood, a key and how many bars.");
	else if (Bpm < SLOWEST || Bpm > FASTEST)
		Text.Format(_T("Type a tempo from %d to %d BPM, or tap it."), SLOWEST, FASTEST);
	else if (m_iChords < 0 && m_iBass < 0 && m_iDrums < 0)
		Text = _T("This song has no channels for a groove.");
	else {
		// The parts as make_groove will write them
		const int Length = static_cast<int>(m_pDocument->GetPatternLength(m_iTrack));
		const ftapi::Timing Time(Length, static_cast<int>(m_pDocument->GetFrameCount(m_iTrack)), m_pDocument->GetFirstHighlight(),
			m_pDocument->GetSecondHighlight());
		ftapi::GrooveRequest Q;
		Q.Mood = Moods[static_cast<size_t>(Mood)].Name;
		Q.K = ftapi::Key(Key / 2, Key % 2 != 0 ? ftapi::MODE_MINOR : ftapi::MODE_MAJOR);
		Q.KeyGiven = true;
		const int First = Time.BarOf(m_iRow);
		for (int b = First; b < First + Bars; ++b)
			Q.Bars.push_back(ftapi::BarSpan(Time.BarStart(b), Time.BarLength(b)));
		Q.RowsPerBeat = Time.RowsPerBeat;
		Q.Bpm = Bpm;
		Q.FrameRate = static_cast<int>(m_pDocument->GetFrameRate());
		Q.Speed = static_cast<int>(m_pDocument->GetSongSpeed(m_iTrack));
		Q.SplitPoint = m_pDocument->GetSpeedSplitPoint();
		Q.Chords = m_iChords >= 0;
		Q.Bass = m_iBass >= 0;
		Q.Drums = m_iDrums >= 0;
		if (m_iChords >= 0)
			Q.ChordChannel = ChannelOf(m_pDocument, m_iChords);
		if (m_iBass >= 0)
			Q.BassChannel = ChannelOf(m_pDocument, m_iBass);
		ftapi::GrooveParts P;
		std::string Error;
		if (!ftapi::MakeGrooveParts(Q, P, Error))
			Text = Error.c_str();
		else {
			CString Where, Part;
			if (m_iChords >= 0)
				Where += _T("chords on ") + RecordingChannelName(m_pDocument, m_iChords);
			if (m_iBass >= 0)
				Where += (Where.IsEmpty() ? _T("") : _T(", ")) + CString(_T("bass on ")) + RecordingChannelName(m_pDocument, m_iBass);
			if (m_iDrums >= 0)
				Where += (Where.IsEmpty() ? _T("") : _T(", ")) + CString(_T("drums on ")) + RecordingChannelName(m_pDocument, m_iDrums);
			const double Played = ftapi::BeatsPerMinute(P.Speed, P.Tempo, Q.FrameRate, std::max(1, Q.RowsPerBeat));
			Text.Format(_T("%s\r\n%s: %s, from bar %d (frame %02X row %02X). You sing on %s.\r\nThe song plays at %s BPM ")
				_T("(speed %d, tempo %d). What those channels had there is replaced; Ctrl+Z takes the groove out again."),
				static_cast<LPCTSTR>(CString(P.ChordText.c_str())), static_cast<LPCTSTR>(Capitalized(std::string(CStringA(Plain(P.Summary))))),
				static_cast<LPCTSTR>(Where), First, m_iRow / Length, m_iRow % Length,
				static_cast<LPCTSTR>(RecordingChannelName(m_pDocument, m_iMelody)), static_cast<LPCTSTR>(BpmText(Played)), P.Speed,
				P.Tempo);
			Ready = true;
		}
	}
	SetDlgItemText(IDC_BACK_PREVIEW, Text);
	GetDlgItem(IDOK)->EnableWindow(Ready ? TRUE : FALSE);
}

void CGrooveDlg::OnMoodChanged()
{
	MoodDefaults();
	UpdatePreview();
}

void CGrooveDlg::OnKeyChanged()
{
	m_bKeyPicked = true;
	UpdatePreview();
}

void CGrooveDlg::OnBarsChanged()
{
	UpdatePreview();
}

void CGrooveDlg::OnBpmChanged()
{
	if (!m_bSettingBpm)
		m_bTempoSet = true;
	UpdatePreview();
}

LRESULT CGrooveDlg::OnTempoTap(WPARAM, LPARAM)
{
	m_Taps.Tap();
	const double Bpm = m_Taps.Bpm();
	if (Bpm > 0.0)
		SetBpm(static_cast<int>(std::floor(Bpm + 0.5)));	// as the taps come
	m_bTempoSet = true;
	SetTimer(TMR_TAPS, TAPS_MS, NULL);
	return 0;
}

void CGrooveDlg::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent != TMR_TAPS) {
		CDialog::OnTimer(nIDEvent);
		return;
	}
	if (!m_Taps.Done())
		return;
	// The taps are over: the nearest even tempo, when one is near
	KillTimer(TMR_TAPS);
	int Speed = 0, Tempo = 0;
	double Played = 0.0;
	if (TappedSongTempo(m_pDocument, m_iTrack, m_Taps.Bpm(), Speed, Tempo, Played))
		SetBpm(static_cast<int>(std::floor(Played + 0.5)));
	m_Taps.Clear();
	UpdatePreview();
}

void CGrooveDlg::OnDestroy()
{
	KillTimer(TMR_TAPS);
	CDialog::OnDestroy();
}

BOOL CGrooveDlg::PreTranslateMessage(MSG *pMsg)
{
	// The T key taps the tempo too, unless the tempo is being typed
	if (IsTapKey(pMsg)) {
		OnTempoTap(IDC_TAP_TEMPO, 0);
		return TRUE;
	}
	return CDialog::PreTranslateMessage(pMsg);
}

void CGrooveDlg::OnOK()
{
	const std::vector<ftapi::GrooveMood> Moods = ftapi::GrooveMoods();
	const int Mood = Picked(m_cMood), Key = Picked(m_cKey), Bars = Picked(m_cBars), Bpm = ReadBpm();
	if (Mood < 0 || Mood >= static_cast<int>(Moods.size()) || Key < 0 || Bars < 1 || Bpm < SLOWEST || Bpm > FASTEST)
		return;
	groove_choice_t Choice;
	Choice.Mood = Moods[static_cast<size_t>(Mood)].Name.c_str();
	Choice.Bpm = Bpm;
	Choice.KeyTonic = Key / 2;
	Choice.KeyMinor = Key % 2 != 0;
	Choice.Row = m_iRow;
	Choice.Bars = Bars;
	Choice.ChordChannel = m_iChords;
	Choice.BassChannel = m_iBass;
	Choice.DrumChannel = m_iDrums;
	Choice.MelodyChannel = m_iMelody;
	theApp.WriteProfileString(SECTION, _T("Groove mood"), Choice.Mood);
	theApp.WriteProfileInt(SECTION, _T("Groove bars"), Bars);

	CString Message;
	bool Made;
	{
		CWaitCursor Wait;
		Made = MakeGroove(static_cast<CMainFrame*>(m_pView->GetParentFrame()), Choice, Message);
	}
	if (!Made) {
		SetDlgItemText(IDC_BACK_PREVIEW, Message);
		return;
	}
	m_sResult = Message;
	CDialog::OnOK();
}

CString CGrooveDlg::GetResult() const
{
	return m_sResult;
}

int CGrooveDlg::GetMelodyChannel() const
{
	return m_iMelody;
}

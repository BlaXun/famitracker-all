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

// Edit > Record from microphone. The dialog listens to the microphone all the
// time, for its level meter. Record starts the click (a count-in at the
// song's tempo, then clicking along if asked) and keeps what the microphone
// hears; the song starts playing with the downbeat after the count-in. Stop
// turns the take into notes at the cursor (InsertTake) and saves it as a WAV
// file in Documents\FamiTracker recordings.
//
// Timing: the click is played and the microphone recorded by two separate
// Windows streams. While the click plays, their sample counts are read
// together, which tells where each click falls in the take. When the
// microphone hears the click (speakers, not headphones), the count-in clicks
// in the take show exactly where; otherwise a typical delay is assumed, and
// PlaceNotes settles the notes onto their rows.

// Standard and engine headers come before stdafx.h, whose DEBUG_NEW macro
// would otherwise rewrite the placement new inside the standard library.
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include "Transcribe/Transcribe.h"
#include "MicRecorder.h"
#include "stdafx.h"
#include <shlobj.h>
#include "FamiTracker.h"
#include "FamiTrackerDoc.h"
#include "FamiTrackerView.h"
#include "MainFrm.h"
#include "PatternEditor.h"
#include "SoundGen.h"
#include "Settings.h"
#include "InsertRecording.h"
#include "InsertRecordingDlg.h"
#include "Clips.h"
#include "RecordDlg.h"

namespace {

const int SAMPLE_RATE = 44100;
const UINT_PTR TMR_POLL = 1;
const UINT TIMER_MS = 10;
const int MAX_SECONDS = 300;			// a take stops by itself after five minutes
const double USUAL_DELAY = 0.030;		// seconds from a click being played to the microphone hearing a
										// response, beyond what the sample counts show, until measured
const int NOT_MEASURED = -100000;		// m_iDelayMs before Test timing has measured it
const int TEST_TAPS = 8;				// clicks to tap along to in Test timing
const double TEST_BEAT = 0.6;			// seconds between them (100 BPM)
LPCTSTR SECTION = _T("Recorder");		// registry section for the dialog's options
const int CLICK_VOLUME = 50;			// percent, at first
const double LOUDEST_CLICK = 0.5;		// the click's peak level at 100%

transcribe::Grid SongGrid(CFamiTrackerDoc *pDoc, unsigned int Track)
{
	// Rows at the song's speed and tempo, as CSoundGen plays them
	transcribe::Grid Grid;
	Grid.frameRate = static_cast<int>(pDoc->GetFrameRate());
	Grid.speed = static_cast<int>(pDoc->GetSongSpeed(Track));
	Grid.tempo = static_cast<int>(pDoc->GetSongTempo(Track));
	if (Grid.tempo == 0)
		Grid.tempo = Grid.frameRate * 5 / 2;	// no tempo: one row every <speed> frames
	return Grid;
}

double RowsPerSecond(CFamiTrackerDoc *pDoc, unsigned int Track)
{
	const double Rows = SongGrid(pDoc, Track).RowsPerSecond();
	return Rows > 0.0 ? Rows : 10.0;
}

transcribe::Beat SongBeat(CFamiTrackerDoc *pDoc, unsigned int Track)
{
	return transcribe::BeatFor(RowsPerSecond(pDoc, Track), pDoc->GetFirstHighlight(), pDoc->GetSecondHighlight());
}

// Saves a take as Documents\FamiTracker recordings\Take <date> <time>.wav.
// Returns the path, or an empty string when it couldn't be saved.
CString SaveTake(const transcribe::Audio &Audio)
{
	TCHAR Documents[MAX_PATH];
	if (FAILED(SHGetFolderPath(NULL, CSIDL_PERSONAL | CSIDL_FLAG_CREATE, NULL, SHGFP_TYPE_CURRENT, Documents)))
		return CString();
	CString Folder(Documents);
	Folder += _T("\\FamiTracker recordings");
	CreateDirectory(Folder, NULL);	// fails harmlessly when it's there already
	const CString Path = Folder + _T("\\") + CTime::GetCurrentTime().Format(_T("Take %Y-%m-%d %H-%M-%S.wav"));
	std::string Error;
	if (!transcribe::WriteWav(std::string(static_cast<LPCSTR>(CStringA(Path))), Audio, &Error))
		return CString();
	return Path;
}

// The take from the start row on, with the click silenced, for guessing what it holds
transcribe::Audio ForGuessing(const transcribe::Audio &Audio, const transcribe::TakeTiming &Timing)
{
	if (Timing.lineUpFirstNote)
		return Audio;
	transcribe::Audio Part;
	Part.sampleRate = Audio.sampleRate;
	const size_t From = size_t(std::max(0.0, Timing.start - 0.05) * Audio.sampleRate);
	if (From < Audio.samples.size())
		Part.samples.assign(Audio.samples.begin() + std::ptrdiff_t(From), Audio.samples.end());
	if (Timing.leak.heard) {
		for (size_t k = 0; k < Timing.clicks.size(); ++k) {
			const double Time = Timing.clicks[k] + Timing.leak.delay - double(From) / Audio.sampleRate;
			const long long a = std::max(0LL, static_cast<long long>((Time - 0.005) * Audio.sampleRate));
			const long long b = std::min(static_cast<long long>(Part.samples.size()), static_cast<long long>((Time + 0.045) * Audio.sampleRate));
			for (long long i = a; i < b; ++i)
				Part.samples[size_t(i)] = 0.0f;
		}
	}
	return Part;
}

}	// namespace

// A take while it's recorded
struct stRecordTake
{
	transcribe::ClickTrack Click;	// countIn 0: no click
	std::vector<short> Samples;		// from when Record was pressed
	long long Offset;				// the microphone's sample count at the first of them
	std::vector<long long> Skews;	// microphone minus click sample counts, read together
	long long SongStart;			// the microphone's sample count when the song started, -1 if it didn't
	bool PlaySong;
	bool SongPlaying;
	int MutedChannel;
	bool WasMuted;

	// What it is and where it goes
	recording_kind_t Kind;
	bool AutoKind;
	int Channel;
	bool Expression;
	bool AsClip;	// kept as a clip instead of going into the song
	int Frame;
	int Row;

	stRecordTake() : Offset(0), SongStart(-1), PlaySong(false), SongPlaying(false), MutedChannel(0), WasMuted(false),
		Kind(RECORDING_WHISTLE), AutoKind(true), Channel(0), Expression(true), AsClip(false), Frame(0), Row(0)
	{
	}

	// Reads both sample counts while the click plays (the first half minute is plenty)
	void Measure(long long Microphone, long long Clicks)
	{
		if (Clicks > 0 && Microphone >= 0 && Skews.size() < 3000)
			Skews.push_back(Microphone - Clicks);
	}

	// Seconds into the take where a beat's click was played, as far as the
	// two sample counts tell
	std::vector<double> ClickTimes(int From, int To) const
	{
		long long Skew = Offset;	// no readings: both streams started together
		if (!Skews.empty()) {
			std::vector<long long> Sorted(Skews);
			std::nth_element(Sorted.begin(), Sorted.begin() + std::ptrdiff_t(Sorted.size() / 2), Sorted.end());
			Skew = Sorted[Sorted.size() / 2];
		}
		std::vector<double> Times;
		for (int k = From; k < To; ++k)
			Times.push_back(double(Click.BeatSample(k) + Skew - Offset) / Click.sampleRate);
		return Times;
	}

	// How the take lines up with the rows: the downbeat after the count-in,
	// or the song's first row, lands on the start row. Delay: how much later
	// than the sample counts show the microphone hears what is played.
	transcribe::TakeTiming Timing(const transcribe::Audio &Audio, double SoundBuffer, double Delay) const
	{
		transcribe::TakeTiming Result;
		if (Click.countIn > 0) {
			Result.leak = transcribe::FindClickLeak(Audio, ClickTimes(0, Click.countIn));
			Result.start = ClickTimes(Click.countIn, Click.countIn + 1)[0] + (Result.leak.heard ? Result.leak.delay : Delay);
			if (Click.clickAlong) {
				const int Beats = Click.countIn + static_cast<int>(Audio.Duration() / Click.beatSeconds) + 1;
				Result.clicks = ClickTimes(Click.countIn, Beats);
			}
			Result.lineUpFirstNote = false;
		}
		else if (SongStart >= 0) {
			// The song is heard once FamiTracker's sound buffer has played
			Result.start = double(SongStart - Offset) / Audio.sampleRate + SoundBuffer + Delay;
			Result.lineUpFirstNote = false;
		}
		return Result;
	}
};

// CRecordDlg

IMPLEMENT_DYNAMIC(CRecordDlg, CDialog)

CRecordDlg::CRecordDlg(CFamiTrackerView *pView) :
	CDialog(CRecordDlg::IDD, pView),
	m_pView(pView),
	m_pDocument(pView->GetDocument()),
	m_pInput(new CMicInput()),
	m_pClick(new CClickOutput()),
	m_pTake(new stRecordTake()),
	m_iState(STATE_IDLE),
	m_iMelodyChannel(DefaultRecordingChannel(pView->GetDocument(), pView->GetPatternEditor()->GetChannel())),
	m_iDelayMs(NOT_MEASURED),
	m_fLevel(-60.0f),
	m_iLoudTicks(0),
	m_iQuietTicks(0),
	m_iTick(0),
	m_iPreviewTicks(0)
{
}

CRecordDlg::~CRecordDlg()
{
	delete m_pTake;
	delete m_pClick;
	delete m_pInput;
}

BEGIN_MESSAGE_MAP(CRecordDlg, CDialog)
	ON_WM_TIMER()
	ON_WM_DESTROY()
	ON_BN_CLICKED(IDC_REC_RECORD, &CRecordDlg::OnRecord)
	ON_BN_CLICKED(IDC_REC_TEST, &CRecordDlg::OnTest)
	ON_BN_CLICKED(IDC_REC_FROMFILE, &CRecordDlg::OnFromFile)
	ON_BN_CLICKED(IDC_REC_AUTO, &CRecordDlg::OnKindChanged)
	ON_BN_CLICKED(IDC_REC_WHISTLE, &CRecordDlg::OnKindChanged)
	ON_BN_CLICKED(IDC_REC_VOICE, &CRecordDlg::OnKindChanged)
	ON_BN_CLICKED(IDC_REC_DRUMS, &CRecordDlg::OnKindChanged)
	ON_CBN_SELCHANGE(IDC_REC_DEVICE, &CRecordDlg::OnDeviceChanged)
	ON_WM_HSCROLL()
END_MESSAGE_MAP()

BOOL CRecordDlg::OnInitDialog()
{
	CDialog::OnInitDialog();

	m_cChannel.SubclassDlgItem(IDC_REC_CHANNEL, this);
	m_cDevice.SubclassDlgItem(IDC_REC_DEVICE, this);
	m_cClick.SubclassDlgItem(IDC_REC_CLICK, this);
	m_cClickVolume.SubclassDlgItem(IDC_REC_CLICKVOLUME, this);
	m_cLevel.SubclassDlgItem(IDC_REC_LEVEL, this);
	m_cLevel.SetRange(0, 100);
	m_cClickVolume.SetRange(5, 100);
	m_cClickVolume.SetPageSize(10);

	static const struct { LPCTSTR Text; click_t Mode; } CLICKS[] = {
		{ _T("Count in, then click every beat"), CLICK_ALONG },
		{ _T("Count in, then click once a bar"), CLICK_BARS },
		{ _T("Count in only"), CLICK_COUNT_IN },
		{ _T("No count-in, no click"), CLICK_NONE },
	};
	for (size_t i = 0; i < sizeof(CLICKS) / sizeof(CLICKS[0]); ++i)
		m_cClick.SetItemData(m_cClick.AddString(CLICKS[i].Text), static_cast<DWORD_PTR>(CLICKS[i].Mode));

	LoadOptions();
	FillChannels();
	FillDevices();

	// The click's tempo, from the song's speed, tempo and row highlight
	CMainFrame *pMainFrm = static_cast<CMainFrame*>(m_pView->GetParentFrame());
	const unsigned int Track = static_cast<unsigned int>(pMainFrm->GetSelectedTrack());
	const double Rows = RowsPerSecond(m_pDocument, Track);
	const transcribe::Beat Beat = SongBeat(m_pDocument, Track);
	CString Tempo;
	Tempo.Format(_T("%d BPM"), static_cast<int>(60.0 * Rows / Beat.rows + 0.5));
	SetDlgItemText(IDC_REC_TEMPO, Tempo);

	ShowDelay();
	SetDefID(IDC_REC_RECORD);
	OpenDevice();
	SetTimer(TMR_POLL, TIMER_MS, NULL);
	if (m_pInput->IsOpen()) {
		GotoDlgCtrl(GetDlgItem(IDC_REC_RECORD));
		return FALSE;	// the focus is set
	}
	return TRUE;
}

void CRecordDlg::OnDestroy()
{
	KillTimer(TMR_POLL);
	StopSong();
	m_pClick->Close();
	m_pInput->Close();
	m_iState = STATE_IDLE;
	CDialog::OnDestroy();
}

CString CRecordDlg::GetResult() const
{
	return m_sResult;
}

// Options

recording_kind_t CRecordDlg::SelectedKind(bool &Auto) const
{
	Auto = false;
	if (IsDlgButtonChecked(IDC_REC_WHISTLE))
		return RECORDING_WHISTLE;
	if (IsDlgButtonChecked(IDC_REC_VOICE))
		return RECORDING_VOICE;
	if (IsDlgButtonChecked(IDC_REC_DRUMS))
		return RECORDING_DRUMS;
	Auto = true;	// FamiTracker decides after listening
	return RECORDING_WHISTLE;
}

void CRecordDlg::LoadOptions()
{
	static const int KINDS[] = { IDC_REC_AUTO, IDC_REC_WHISTLE, IDC_REC_VOICE, IDC_REC_DRUMS };
	const int Kind = static_cast<int>(theApp.GetProfileInt(SECTION, _T("Kind"), 0));
	CheckRadioButton(IDC_REC_AUTO, IDC_REC_DRUMS, KINDS[(Kind >= 0 && Kind < 4) ? Kind : 0]);
	const int Click = static_cast<int>(theApp.GetProfileInt(SECTION, _T("Click"), CLICK_ALONG));
	m_cClick.SetCurSel(0);
	for (int i = 0; i < m_cClick.GetCount(); ++i)
		if (static_cast<int>(m_cClick.GetItemData(i)) == Click)
			m_cClick.SetCurSel(i);
	m_cClickVolume.SetPos(static_cast<int>(theApp.GetProfileInt(SECTION, _T("Click volume"), CLICK_VOLUME)));
	ShowClickVolume();
	CheckDlgButton(IDC_REC_PLAYSONG, theApp.GetProfileInt(SECTION, _T("Play song"), 1) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_REC_EXPRESSION, theApp.GetProfileInt(SECTION, _T("Expression"), 1) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_REC_ASCLIP, theApp.GetProfileInt(SECTION, _T("As clip"), 0) ? BST_CHECKED : BST_UNCHECKED);
	m_sDevice = theApp.GetProfileString(SECTION, _T("Microphone"), _T(""));
	if (theApp.GetProfileInt(SECTION, _T("Delay measured"), 0) != 0)
		m_iDelayMs = static_cast<int>(theApp.GetProfileInt(SECTION, _T("Delay ms"), 0));
}

void CRecordDlg::SaveOptions() const
{
	int Kind = 0;
	if (IsDlgButtonChecked(IDC_REC_WHISTLE))
		Kind = 1;
	else if (IsDlgButtonChecked(IDC_REC_VOICE))
		Kind = 2;
	else if (IsDlgButtonChecked(IDC_REC_DRUMS))
		Kind = 3;
	theApp.WriteProfileInt(SECTION, _T("Kind"), Kind);
	theApp.WriteProfileInt(SECTION, _T("Click"), ClickMode());
	theApp.WriteProfileInt(SECTION, _T("Click volume"), m_cClickVolume.GetPos());
	theApp.WriteProfileInt(SECTION, _T("Play song"), IsDlgButtonChecked(IDC_REC_PLAYSONG) == BST_CHECKED ? 1 : 0);
	theApp.WriteProfileInt(SECTION, _T("Expression"), IsDlgButtonChecked(IDC_REC_EXPRESSION) == BST_CHECKED ? 1 : 0);
	theApp.WriteProfileInt(SECTION, _T("As clip"), IsDlgButtonChecked(IDC_REC_ASCLIP) == BST_CHECKED ? 1 : 0);
	theApp.WriteProfileString(SECTION, _T("Microphone"), m_sDevice);
}

void CRecordDlg::FillChannels()
{
	// Keep the melody channel picked so far
	const int Picked = PickedRecordingChannel(m_cChannel);
	if (Picked >= 0 && IsMelodyChannel(m_pDocument, Picked))
		m_iMelodyChannel = Picked;

	bool Auto;
	const bool Drums = SelectedKind(Auto) == RECORDING_DRUMS;
	FillRecordingChannels(m_cChannel, m_pDocument, Drums, m_iMelodyChannel);
	GetDlgItem(IDC_REC_EXPRESSION)->EnableWindow(!Drums);	// drums keep their loudness anyway
}

void CRecordDlg::FillDevices()
{
	m_cDevice.ResetContent();
	int Item = m_cDevice.AddString(_T("The one set in Windows"));
	m_cDevice.SetItemData(Item, static_cast<DWORD_PTR>(-1));
	m_cDevice.SetCurSel(Item);
	const int Count = CMicInput::DeviceCount();
	for (int i = 0; i < Count; ++i) {
		const CString Name(CMicInput::DeviceName(i).c_str());
		if (Name.IsEmpty())
			continue;
		Item = m_cDevice.AddString(Name);
		m_cDevice.SetItemData(Item, static_cast<DWORD_PTR>(i));
		if (Name == m_sDevice)
			m_cDevice.SetCurSel(Item);
	}
}

void CRecordDlg::OpenDevice()
{
	const int Item = m_cDevice.GetCurSel();
	const int Device = (Item != CB_ERR) ? static_cast<int>(m_cDevice.GetItemData(Item)) : -1;
	m_sDevice = (Device >= 0) ? CString(CMicInput::DeviceName(Device).c_str()) : CString();

	std::string Error;
	const bool Opened = m_pInput->Open(Device, SAMPLE_RATE, Error);
	GetDlgItem(IDC_REC_RECORD)->EnableWindow(Opened ? TRUE : FALSE);
	if (Opened)
		SetStatus(_T("Press Record. After the count-in, whistle, hum or tap; press Stop when you're done. ")
			_T("The notes go in at the cursor."));
	else
		SetStatus(CString(Error.c_str()));
	m_iQuietTicks = 0;
}

void CRecordDlg::EnableOptions(bool Enable)
{
	static const int IDS[] = {
		IDC_REC_AUTO, IDC_REC_WHISTLE, IDC_REC_VOICE, IDC_REC_DRUMS, IDC_REC_CHANNEL, IDC_REC_EXPRESSION,
		IDC_REC_DEVICE, IDC_REC_CLICK, IDC_REC_CLICKVOLUME, IDC_REC_TEST, IDC_REC_PLAYSONG, IDC_REC_ASCLIP, IDC_REC_FROMFILE
	};
	for (size_t i = 0; i < sizeof(IDS) / sizeof(IDS[0]); ++i)
		GetDlgItem(IDS[i])->EnableWindow(Enable ? TRUE : FALSE);
	if (Enable)
		FillChannels();	// the channel list and expression box stay off for drums
}

void CRecordDlg::SetStatus(LPCTSTR Text)
{
	CString Old;
	GetDlgItemText(IDC_REC_STATUS, Old);
	if (Old != Text)
		SetDlgItemText(IDC_REC_STATUS, Text);
}

double CRecordDlg::Delay() const
{
	return (m_iDelayMs != NOT_MEASURED) ? m_iDelayMs / 1000.0 : USUAL_DELAY;
}

void CRecordDlg::ShowDelay()
{
	CString Text;
	if (m_iDelayMs != NOT_MEASURED)
		Text.Format(_T("%d ms (measured)"), m_iDelayMs);
	else
		Text.Format(_T("%d ms (not measured yet)"), static_cast<int>(USUAL_DELAY * 1000.0 + 0.5));
	SetDlgItemText(IDC_REC_DELAY, Text);
}

void CRecordDlg::ShowLevel(float Peak)
{
	// The meter shows the loudest recent sound and falls back 60 dB a second
	if (Peak >= 0.0f) {
		const float Db = Peak > 1e-5f ? 20.0f * std::log10(Peak) : -100.0f;
		m_fLevel = std::max(m_fLevel, Db);
		if (Peak >= 0.99f)
			m_iLoudTicks = static_cast<int>(1000 / TIMER_MS);	// "too loud" stays up for a second
		m_iQuietTicks = (Db < -50.0f) ? m_iQuietTicks + 1 : 0;
	}
	m_fLevel = std::max(-60.0f, m_fLevel - 60.0f * static_cast<float>(TIMER_MS) / 1000.0f);
	if (m_iLoudTicks > 0)
		--m_iLoudTicks;

	if (++m_iTick % 3 != 0)
		return;	// redrawn every 30 ms
	m_cLevel.SetPos(std::max(0, std::min(100, static_cast<int>((m_fLevel + 60.0f) * 100.0f / 60.0f + 0.5f))));
	CString Text;
	if (m_iLoudTicks > 0)
		Text = _T("Too loud!");
	else if (m_iQuietTicks > 100)	// no sound for a couple of seconds
		Text = _T("No sound?");
	CString Old;
	GetDlgItemText(IDC_REC_LEVELTEXT, Old);
	if (Old != Text)
		SetDlgItemText(IDC_REC_LEVELTEXT, Text);
}

// Buttons

void CRecordDlg::OnOK()
{
	OnRecord();	// Enter
}

void CRecordDlg::OnCancel()
{
	if (m_iState != STATE_IDLE)
		CancelTake(_T(""));
	CDialog::OnCancel();
}

void CRecordDlg::OnRecord()
{
	switch (m_iState) {
	case STATE_IDLE:
		StartTake();
		break;
	case STATE_COUNT_IN:
		CancelTake(_T("Stopped during the count-in. Press Record to try again."));
		break;
	case STATE_RECORDING:
		FinishTake();
		break;
	case STATE_TEST:
		CancelTake(_T("Test stopped."));
		break;
	}
}

void CRecordDlg::OnTest()
{
	// Clicks at 100 BPM to tap along to: how late the taps arrive is the delay
	// takes are lined up with
	if (m_iState != STATE_IDLE || !m_pInput->IsOpen())
		return;
	stRecordTake &Take = *m_pTake;
	Take.Click = transcribe::ClickTrack();
	Take.Click.sampleRate = SAMPLE_RATE;
	Take.Click.beatSeconds = TEST_BEAT;
	Take.Click.beatsPerBar = 4;
	Take.Click.countIn = TEST_TAPS;
	Take.Click.clickAlong = false;
	Take.Click.level = ClickLevel();
	m_iPreviewTicks = 0;
	Take.PlaySong = false;
	Take.SongPlaying = false;
	Take.SongStart = -1;
	Take.Samples.clear();
	Take.Skews.clear();
	Take.Offset = m_pInput->Received();

	std::string Error;
	if (!m_pClick->Open(Take.Click, Error)) {
		SetStatus(CString(Error.c_str()));
		return;
	}
	m_iState = STATE_TEST;
	EnableOptions(false);
	SetDlgItemText(IDC_REC_RECORD, _T("Stop"));
	GotoDlgCtrl(GetDlgItem(IDC_REC_RECORD));
	UpdateTest();
}

void CRecordDlg::OnFromFile()
{
	if (m_iState == STATE_IDLE)
		EndDialog(IDC_REC_FROMFILE);
}

void CRecordDlg::OnKindChanged()
{
	FillChannels();
}

void CRecordDlg::OnDeviceChanged()
{
	if (m_iState == STATE_IDLE)
		OpenDevice();
}

CRecordDlg::click_t CRecordDlg::ClickMode() const
{
	const int Item = m_cClick.GetCurSel();
	return Item != CB_ERR ? static_cast<click_t>(m_cClick.GetItemData(Item)) : CLICK_ALONG;
}

double CRecordDlg::ClickLevel() const
{
	return LOUDEST_CLICK * m_cClickVolume.GetPos() / 100.0;
}

void CRecordDlg::ShowClickVolume()
{
	CString Text;
	Text.Format(_T("%d%%"), m_cClickVolume.GetPos());
	SetDlgItemText(IDC_REC_CLICKVOLUMETEXT, Text);
}

void CRecordDlg::PreviewClick()
{
	// Two clicks, the first of a bar and another, at the volume just set
	if (m_iState != STATE_IDLE)
		return;
	transcribe::ClickTrack Click;
	Click.sampleRate = SAMPLE_RATE;
	Click.leadSeconds = 0.05;
	Click.beatSeconds = 0.4;
	Click.beatsPerBar = 2;
	Click.countIn = 2;
	Click.clickAlong = false;
	Click.level = ClickLevel();
	std::string Error;
	if (m_pClick->Open(Click, Error))
		m_iPreviewTicks = 1000 / TIMER_MS;	// a second
}

void CRecordDlg::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar *pScrollBar)
{
	if (pScrollBar != NULL && pScrollBar->GetSafeHwnd() == m_cClickVolume.GetSafeHwnd()) {
		ShowClickVolume();
		if (nSBCode == TB_ENDTRACK)	// let go of, or moved with the keys: play it
			PreviewClick();
		return;
	}
	CDialog::OnHScroll(nSBCode, nPos, pScrollBar);
}

void CRecordDlg::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent != TMR_POLL) {
		CDialog::OnTimer(nIDEvent);
		return;
	}
	const long long Before = m_pInput->Received();
	const float Peak = m_pInput->Poll(m_iState != STATE_IDLE ? &m_pTake->Samples : NULL);
	m_pClick->Poll();
	if (m_iPreviewTicks > 0 && --m_iPreviewTicks == 0 && m_iState == STATE_IDLE)
		m_pClick->Close();	// the volume preview is over
	ShowLevel(m_pInput->Received() != Before ? Peak : -1.0f);

	if (m_iState == STATE_COUNT_IN)
		UpdateCountIn();
	else if (m_iState == STATE_RECORDING)
		UpdateRecording();
	else if (m_iState == STATE_TEST)
		UpdateTest();
}

// The take

long long CRecordDlg::Played() const
{
	// Click samples played so far. Without a position from Windows, the
	// microphone's count since Record was pressed stands in for it.
	const long long Position = m_pClick->IsOpen() ? m_pClick->Position() : -1;
	if (Position >= 0)
		return Position;
	return m_pInput->Received() - m_pTake->Offset;
}

void CRecordDlg::StartTake()
{
	if (!m_pInput->IsOpen())
		return;
	stRecordTake &Take = *m_pTake;
	CMainFrame *pMainFrm = static_cast<CMainFrame*>(m_pView->GetParentFrame());
	const unsigned int Track = static_cast<unsigned int>(pMainFrm->GetSelectedTrack());

	SaveOptions();
	Take.Kind = SelectedKind(Take.AutoKind);
	Take.Channel = PickedRecordingChannel(m_cChannel);
	Take.Expression = IsDlgButtonChecked(IDC_REC_EXPRESSION) == BST_CHECKED;
	Take.AsClip = IsDlgButtonChecked(IDC_REC_ASCLIP) == BST_CHECKED;
	Take.PlaySong = IsDlgButtonChecked(IDC_REC_PLAYSONG) == BST_CHECKED;

	// The take goes in at the cursor
	CPatternEditor *pEditor = m_pView->GetPatternEditor();
	Take.Frame = pEditor->GetFrame();
	Take.Row = pEditor->GetRow();

	// The click, at the song's tempo: a beat is a row highlight
	const click_t Click = ClickMode();
	m_iPreviewTicks = 0;
	const double Rows = RowsPerSecond(m_pDocument, Track);
	const transcribe::Beat Beat = SongBeat(m_pDocument, Track);
	Take.Click = transcribe::ClickTrack();
	Take.Click.sampleRate = SAMPLE_RATE;
	Take.Click.beatSeconds = Beat.rows / Rows;
	Take.Click.beatsPerBar = Beat.perBar;
	Take.Click.countIn = (Click == CLICK_NONE) ? 0 : Beat.countIn;
	Take.Click.clickAlong = (Click == CLICK_ALONG || Click == CLICK_BARS);
	Take.Click.barsOnly = (Click == CLICK_BARS);
	Take.Click.level = ClickLevel();

	Take.Samples.clear();
	Take.Skews.clear();
	Take.SongStart = -1;
	Take.SongPlaying = false;
	Take.Offset = m_pInput->Received();	// the next sample polled is the take's first

	if (Take.Click.countIn > 0) {
		std::string Error;
		if (!m_pClick->Open(Take.Click, Error)) {
			SetStatus(CString(Error.c_str()));
			return;
		}
		m_iState = STATE_COUNT_IN;
		SetStatus(_T("Get ready:"));
	}
	else {
		m_iState = STATE_RECORDING;
		if (Take.PlaySong)
			StartSong();
		UpdateRecording();
	}

	EnableOptions(false);
	SetDlgItemText(IDC_REC_RECORD, _T("Stop"));
	GotoDlgCtrl(GetDlgItem(IDC_REC_RECORD));	// Enter or Space stops it
}

void CRecordDlg::StartSong()
{
	stRecordTake &Take = *m_pTake;
	if (Take.SongPlaying)
		return;

	// The channel being recorded stays quiet, so an earlier take on it doesn't play along
	int Channel = Take.Channel;
	if (!Take.AutoKind && Take.Kind == RECORDING_DRUMS) {
		for (int i = 0; i < static_cast<int>(m_pDocument->GetAvailableChannels()); ++i) {
			if (m_pDocument->GetChannelType(i) == CHANID_NOISE)
				Channel = i;
		}
	}
	if (Channel >= 0 && Channel < MAX_CHANNELS) {
		Take.MutedChannel = Channel;
		Take.WasMuted = m_pView->IsChannelMuted(static_cast<unsigned int>(Channel));
		m_pView->SetChannelMute(Channel, true);
	}
	else
		Take.MutedChannel = -1;

	theApp.StartPlayer(MODE_PLAY_CURSOR);
	Take.SongStart = m_pInput->Position();
	if (Take.SongStart < 0)
		Take.SongStart = m_pInput->Received();
	Take.SongPlaying = true;
}

void CRecordDlg::StopSong()
{
	stRecordTake &Take = *m_pTake;
	if (!Take.SongPlaying)
		return;
	theApp.StopPlayerAndWait();
	Take.SongPlaying = false;
	if (Take.MutedChannel >= 0)
		m_pView->SetChannelMute(Take.MutedChannel, Take.WasMuted);

	// Playing moved the cursor along with the song: back to where the take goes
	CPatternEditor *pEditor = m_pView->GetPatternEditor();
	pEditor->MoveToFrame(Take.Frame);
	pEditor->MoveToRow(Take.Row);
	pEditor->InvalidateCursor();
	m_pDocument->UpdateAllViews(NULL, UPDATE_FRAME);
}

void CRecordDlg::CancelTake(LPCTSTR Status)
{
	StopSong();
	m_pClick->Close();
	m_pTake->Samples.clear();
	m_iState = STATE_IDLE;
	EnableOptions(true);
	SetDlgItemText(IDC_REC_RECORD, _T("Record"));
	SetStatus(Status);
}

void CRecordDlg::UpdateCountIn()
{
	stRecordTake &Take = *m_pTake;
	Take.Measure(m_pInput->Position(), m_pClick->Position());

	const long long Now = Played();
	const long long Downbeat = Take.Click.StartSample();

	// The song starts so that its first row is heard with the downbeat:
	// FamiTracker's sound buffer takes its length to play
	if (Take.PlaySong && !Take.SongPlaying) {
		const long long Buffer = static_cast<long long>(theApp.GetSettings()->Sound.iBufferLength) * SAMPLE_RATE / 1000;
		const long long HalfTick = static_cast<long long>(SAMPLE_RATE) * TIMER_MS / 2000;
		if (Now + Buffer + HalfTick >= Downbeat)
			StartSong();
	}

	if (Now >= Downbeat) {
		m_iState = STATE_RECORDING;
		UpdateRecording();
		return;
	}

	// "Get ready: 1 2 3 4"
	const double Seconds = double(Now) / SAMPLE_RATE - Take.Click.leadSeconds;
	const int Beats = (Seconds < 0.0) ? 0 : std::min(Take.Click.countIn, static_cast<int>(Seconds / Take.Click.beatSeconds) + 1);
	CString Text(_T("Get ready:"));
	for (int i = 0; i < Beats; ++i) {
		CString Number;
		Number.Format(_T(" %d"), i % Take.Click.beatsPerBar + 1);
		Text += Number;
	}
	SetStatus(Text);
}

void CRecordDlg::UpdateRecording()
{
	stRecordTake &Take = *m_pTake;
	if (m_pClick->IsOpen())
		Take.Measure(m_pInput->Position(), m_pClick->Position());

	const long long Samples = (Take.Click.countIn > 0) ? Played() - Take.Click.StartSample() : m_pInput->Received() - Take.Offset;
	const int Seconds = static_cast<int>(std::max(0LL, Samples) / SAMPLE_RATE);
	if (Seconds >= MAX_SECONDS) {
		FinishTake();
		return;
	}
	CString Text;
	Text.Format(_T("Recording %d:%02d. Press Stop when you're done."), Seconds / 60, Seconds % 60);
	SetStatus(Text);
}

void CRecordDlg::FinishTake()
{
	stRecordTake &Take = *m_pTake;
	StopSong();
	m_pClick->Close();
	m_pInput->Poll(&Take.Samples);
	m_iState = STATE_IDLE;
	EnableOptions(true);
	SetDlgItemText(IDC_REC_RECORD, _T("Record"));

	if (Take.Samples.size() < static_cast<size_t>(SAMPLE_RATE / 2)) {
		SetStatus(_T("That take was too short. Press Record to try again."));
		return;
	}

	SetStatus(_T("Listening to the take..."));
	GetDlgItem(IDC_REC_STATUS)->UpdateWindow();
	CWaitCursor Wait;

	transcribe::Audio Audio;
	Audio.sampleRate = SAMPLE_RATE;
	Audio.samples.resize(Take.Samples.size());
	for (size_t i = 0; i < Take.Samples.size(); ++i)
		Audio.samples[i] = Take.Samples[i] / 32768.0f;
	Take.Samples.clear();

	const CString Saved = SaveTake(Audio);
	const transcribe::TakeTiming Timing = Take.Timing(Audio, theApp.GetSettings()->Sound.iBufferLength / 1000.0, Delay());

	recording_kind_t Kind = Take.Kind;
	if (Take.AutoKind) {
		CString Description;
		GuessRecording(ForGuessing(Audio, Timing), Kind, Description);
	}

	CMainFrame *pMainFrm = static_cast<CMainFrame*>(m_pView->GetParentFrame());
	CString Message;
	const CString ClipName = m_pDocument->GetClips()->NewName(_T("Take"), true);
	if (!InsertTake(pMainFrm, Audio, Timing, Kind, Take.Channel, Take.Expression, Take.Frame, Take.Row, Message,
			Take.AsClip ? static_cast<LPCTSTR>(ClipName) : NULL)) {
		Message += _T(" Press Record to try again.");
		SetStatus(Message);
		return;
	}

	m_sResult = Message;
	if (!Saved.IsEmpty())
		m_sResult += _T(" The take is saved in Documents\\FamiTracker recordings.");
	if (Timing.leak.heard)
		m_sResult += _T(" The microphone heard the click: headphones keep it out of takes.");
	EndDialog(IDOK);
}

void CRecordDlg::UpdateTest()
{
	stRecordTake &Take = *m_pTake;
	Take.Measure(m_pInput->Position(), m_pClick->Position());
	const long long Now = Played();
	if (Now >= Take.Click.BeatSample(TEST_TAPS) + SAMPLE_RATE / 4) {
		FinishTest();
		return;
	}
	const double Seconds = double(Now) / SAMPLE_RATE - Take.Click.leadSeconds;
	const int Clicks = (Seconds < 0.0) ? 0 : std::min(TEST_TAPS, static_cast<int>(Seconds / TEST_BEAT) + 1);
	CString Text;
	Text.Format(_T("Tap along with the clicks, on the desk or the microphone: %d of %d."), Clicks, TEST_TAPS);
	SetStatus(Text);
}

void CRecordDlg::FinishTest()
{
	stRecordTake &Take = *m_pTake;
	m_pClick->Close();
	m_pInput->Poll(&Take.Samples);
	m_iState = STATE_IDLE;
	EnableOptions(true);
	SetDlgItemText(IDC_REC_RECORD, _T("Record"));

	transcribe::Audio Audio;
	Audio.sampleRate = SAMPLE_RATE;
	Audio.samples.resize(Take.Samples.size());
	for (size_t i = 0; i < Take.Samples.size(); ++i)
		Audio.samples[i] = Take.Samples[i] / 32768.0f;
	Take.Samples.clear();

	double Measured = 0.0;
	if (!transcribe::MeasureTapDelay(Audio, Take.ClickTimes(0, TEST_TAPS), 0.3, &Measured) ||
		Measured < -0.1 || Measured > 0.5) {
		SetStatus(_T("The taps weren't clear enough to measure. Tap firmly on the desk or the microphone, ")
			_T("right with each click, and try again."));
		return;
	}
	m_iDelayMs = static_cast<int>(std::floor(Measured * 1000.0 + 0.5));
	theApp.WriteProfileInt(SECTION, _T("Delay measured"), 1);
	theApp.WriteProfileInt(SECTION, _T("Delay ms"), m_iDelayMs);
	ShowDelay();
	CString Text;
	Text.Format(_T("Your taps arrived %d ms after the clicks. Takes are lined up with that from now on."), m_iDelayMs);
	SetStatus(Text);
}

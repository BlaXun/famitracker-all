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
// hears; the song starts playing a bar before the take (the count-in's
// length), so the take's first row sounds with the downbeat after the
// count-in. Stop turns the take into notes at the cursor (InsertTake), or over
// the selected rows only, and saves it as a WAV file in the Recordings folder
// next to FamiTracker.exe (AppFolders.h); a take of strummed chords is heard
// bar by bar for Add chords and bass line (HearRecordedChords). A take over a
// selection stops by itself when the selection is over.
//
// Tap tempo (TapButton.h) sets the song's speed and tempo from taps on the
// button or the T key, and Start from a groove (GrooveDlg.cpp) writes drums, a
// bass line and chords to sing over.
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
#include "Api/Music.h"
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
#include "PianoRoll.h"
#include "GrooveDlg.h"
#include "RecordDlg.h"
#include "AppFolders.h"

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
const double SELECTION_TAIL = 0.4;		// seconds a take over a selection goes on past it, for its last note's end

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

// Saves a take as Take <date> <time>.wav in the Recordings folder next to
// FamiTracker.exe (AppFolders.h). Returns the path, or an empty string when
// it couldn't be saved.
CString SaveTake(const transcribe::Audio &Audio)
{
	const CString Folder = RecordingsFolder(true);
	if (Folder.IsEmpty())
		return CString();
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

// Does the song go back to its first frame after its last one (no jump, skip
// or halt in the last frame)? Then the bar before the start is the song's last.
bool LoopsToStart(CFamiTrackerDoc *pDoc, unsigned int Track)
{
	const unsigned int Frames = pDoc->GetFrameCount(Track), Length = pDoc->GetPatternLength(Track);
	if (Frames == 0)
		return false;
	for (unsigned int c = 0; c < pDoc->GetAvailableChannels(); ++c) {
		for (unsigned int r = 0; r < Length; ++r) {
			stChanNote Note;
			pDoc->GetNoteData(Track, Frames - 1, c, r, &Note);
			for (int k = 0; k < MAX_EFFECT_COLUMNS; ++k)
				if (Note.EffNumber[k] == EF_JUMP || Note.EffNumber[k] == EF_SKIP || Note.EffNumber[k] == EF_HALT)
					return false;
		}
	}
	return true;
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
	int Frame;		// where its row 0 goes
	int Row;
	int Cursor;		// song row the cursor goes back to afterwards
	bool Selected;	// over the selected rows only
	take_selection_t Selection;
	double Seconds;	// from the downbeat until it stops by itself (over a selection), 0 until Stop

	// The song along: from a bar before the take's row 0 (a bar of the song's end when that's the first row of a
	// song that loops)
	int PlayFrom;			// song row
	double PreRoll;			// seconds from there to row 0

	stRecordTake() : Offset(0), SongStart(-1), PlaySong(false), SongPlaying(false), MutedChannel(0), WasMuted(false),
		Kind(RECORDING_WHISTLE), AutoKind(true), Channel(0), Expression(true), AsClip(false), Frame(0), Row(0), Cursor(0),
		Selected(false), Seconds(0.0), PlayFrom(0), PreRoll(0.0)
	{
		Selection.First = Selection.End = Selection.Early = 0;
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
			// The song is heard once FamiTracker's sound buffer has played, row 0 after the bar before it
			Result.start = double(SongStart - Offset) / Audio.sampleRate + SoundBuffer + Delay + PreRoll;
			Result.lineUpFirstNote = false;
		}
		return Result;
	}
};

// CRecordDlg

IMPLEMENT_DYNAMIC(CRecordDlg, CDialog)

CRecordDlg::CRecordDlg(CFamiTrackerView *pView, CWnd *pParent, bool ChordsOnly) :
	CDialog(CRecordDlg::IDD, pParent != NULL ? pParent : pView),
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
	m_iPreviewTicks(0),
	m_bChordsOnly(ChordsOnly),
	m_bGrooveFirst(false),
	m_iChordsRow(0),
	m_bHasSelection(false)
{
	m_Selection.First = m_Selection.End = m_Selection.Early = 0;
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
	ON_BN_CLICKED(IDC_REC_GROOVE, &CRecordDlg::OnGroove)
	ON_BN_CLICKED(IDC_REC_AUTO, &CRecordDlg::OnKindChanged)
	ON_BN_CLICKED(IDC_REC_WHISTLE, &CRecordDlg::OnKindChanged)
	ON_BN_CLICKED(IDC_REC_VOICE, &CRecordDlg::OnKindChanged)
	ON_BN_CLICKED(IDC_REC_DRUMS, &CRecordDlg::OnKindChanged)
	ON_BN_CLICKED(IDC_REC_CHORDS, &CRecordDlg::OnKindChanged)
	ON_BN_CLICKED(IDC_REC_SELECTION, &CRecordDlg::OnKindChanged)
	ON_CBN_SELCHANGE(IDC_REC_DEVICE, &CRecordDlg::OnDeviceChanged)
	ON_MESSAGE(WM_TEMPO_TAP, &CRecordDlg::OnTempoTap)
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
	m_cTap.SubclassDlgItem(IDC_TAP_TEMPO, this);
	m_cLevel.SetRange(0, 100);
	m_cClickVolume.SetRange(5, 100);
	m_cClickVolume.SetPageSize(10);
	RecordingsFolder(false);	// takes kept in Documents move next to FamiTracker

	static const struct { LPCTSTR Text; click_t Mode; } CLICKS[] = {
		{ _T("Count in, then click every beat"), CLICK_ALONG },
		{ _T("Count in, then click once a bar"), CLICK_BARS },
		{ _T("Count in only"), CLICK_COUNT_IN },
		{ _T("No count-in, no click"), CLICK_NONE },
	};
	for (size_t i = 0; i < sizeof(CLICKS) / sizeof(CLICKS[0]); ++i)
		m_cClick.SetItemData(m_cClick.AddString(CLICKS[i].Text), static_cast<DWORD_PTR>(CLICKS[i].Mode));

	LoadOptions();
	ReadSelection();
	FillChannels();
	FillDevices();
	if (m_bChordsOnly)
		SetWindowText(_T("Record strummed chords"));

	ShowTempo();
	ShowDelay();
	SetDefID(IDC_REC_RECORD);
	OpenDevice();
	SetTimer(TMR_POLL, TIMER_MS, NULL);
	if (m_bGrooveFirst && !m_bChordsOnly)
		PostMessage(WM_COMMAND, MAKEWPARAM(IDC_REC_GROOVE, BN_CLICKED), reinterpret_cast<LPARAM>(GetDlgItem(IDC_REC_GROOVE)->GetSafeHwnd()));
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

void CRecordDlg::OpenGrooveFirst()
{
	m_bGrooveFirst = true;
}

CString CRecordDlg::GetChords() const
{
	return m_sChords;
}

int CRecordDlg::GetChordsRow() const
{
	return m_iChordsRow;
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
	if (IsDlgButtonChecked(IDC_REC_CHORDS))
		return RECORDING_CHORDS;
	Auto = true;	// FamiTracker decides after listening
	return RECORDING_WHISTLE;
}

void CRecordDlg::SetKind(int ID)
{
	static const int KINDS[] = { IDC_REC_AUTO, IDC_REC_WHISTLE, IDC_REC_VOICE, IDC_REC_DRUMS, IDC_REC_CHORDS };
	for (size_t i = 0; i < sizeof(KINDS) / sizeof(KINDS[0]); ++i)
		CheckDlgButton(KINDS[i], KINDS[i] == ID ? BST_CHECKED : BST_UNCHECKED);
}

int CRecordDlg::CursorSongRow() const
{
	CMainFrame *pMainFrm = static_cast<CMainFrame*>(m_pView->GetParentFrame());
	const unsigned int Track = static_cast<unsigned int>(pMainFrm->GetSelectedTrack());
	CPatternEditor *pEditor = m_pView->GetPatternEditor();
	return pEditor->GetFrame() * static_cast<int>(m_pDocument->GetPatternLength(Track)) + pEditor->GetRow();
}

void CRecordDlg::ReadSelection()
{
	// The rows selected in the pattern editor (in the cursor's frame), or the bars of the notes selected in the
	// piano roll: a take can go over them only
	CMainFrame *pMainFrm = static_cast<CMainFrame*>(m_pView->GetParentFrame());
	const unsigned int Track = static_cast<unsigned int>(pMainFrm->GetSelectedTrack());
	const int Length = static_cast<int>(m_pDocument->GetPatternLength(Track));
	CPatternEditor *pEditor = m_pView->GetPatternEditor();
	CPianoRollWnd *pRoll = pMainFrm->GetPianoRollWnd();
	int Channel = -1, First = 0, End = 0;
	m_bHasSelection = false;
	if (pEditor->IsSelecting()) {
		const CSelection Selection = pEditor->GetSelection();
		const int Top = std::max(0, Selection.GetRowStart()), Bottom = std::min(Length - 1, Selection.GetRowEnd());
		if (Bottom >= Top) {
			Channel = Selection.GetChanStart();
			First = pEditor->GetFrame() * Length + Top;
			End = pEditor->GetFrame() * Length + Bottom + 1;
			m_bHasSelection = true;
			m_sSelection.Format(_T("Replace only the selected rows (frame %02X, rows %02X to %02X)"), pEditor->GetFrame(), Top, Bottom);
		}
	}
	else if (pRoll != NULL && pRoll->SelectedBars(Channel, First, End)) {
		const ftapi::Timing Time(Length, static_cast<int>(m_pDocument->GetFrameCount(Track)), m_pDocument->GetFirstHighlight(),
			m_pDocument->GetSecondHighlight());
		const int FirstBar = Time.BarOf(First), LastBar = Time.BarOf(End - 1);
		m_bHasSelection = true;
		if (FirstBar == LastBar)
			m_sSelection.Format(_T("Replace only the bar of the notes selected in the piano roll (bar %d)"), FirstBar);
		else
			m_sSelection.Format(_T("Replace only the bars of the notes selected in the piano roll (bars %d to %d)"), FirstBar, LastBar);
	}
	if (m_bHasSelection) {
		m_Selection.First = First;
		m_Selection.End = End;
		m_Selection.Early = 1;
		if (Channel >= 0 && Channel < static_cast<int>(m_pDocument->GetAvailableChannels()) && IsMelodyChannel(m_pDocument, Channel))
			m_iMelodyChannel = Channel;
	}
	else
		m_sSelection = _T("Replace only the selected rows (select some first)");
	SetDlgItemText(IDC_REC_SELECTION, m_sSelection);
	CheckDlgButton(IDC_REC_SELECTION, m_bHasSelection && !m_bChordsOnly ? BST_CHECKED : BST_UNCHECKED);
}

void CRecordDlg::LoadOptions()
{
	static const int KINDS[] = { IDC_REC_AUTO, IDC_REC_WHISTLE, IDC_REC_VOICE, IDC_REC_DRUMS };
	const int Kind = static_cast<int>(theApp.GetProfileInt(SECTION, _T("Kind"), 0));
	SetKind(m_bChordsOnly ? IDC_REC_CHORDS : KINDS[(Kind >= 0 && Kind < 4) ? Kind : 0]);
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
	if (!IsDlgButtonChecked(IDC_REC_CHORDS))	// chords are picked each time
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
	const recording_kind_t Kind = SelectedKind(Auto);
	const bool Drums = Kind == RECORDING_DRUMS, Chords = Kind == RECORDING_CHORDS;
	FillRecordingChannels(m_cChannel, m_pDocument, Drums, m_iMelodyChannel);
	if (Chords)
		m_cChannel.EnableWindow(FALSE);	// they go to Add chords and bass line
	GetDlgItem(IDC_REC_EXPRESSION)->EnableWindow(!Drums && !Chords);	// drums keep their loudness anyway
	GetDlgItem(IDC_REC_ASCLIP)->EnableWindow(!Chords);
	GetDlgItem(IDC_REC_SELECTION)->EnableWindow(m_bHasSelection && !Chords);
	if (m_bChordsOnly) {
		static const int OTHERS[] = { IDC_REC_AUTO, IDC_REC_WHISTLE, IDC_REC_VOICE, IDC_REC_DRUMS, IDC_REC_GROOVE };
		for (size_t i = 0; i < sizeof(OTHERS) / sizeof(OTHERS[0]); ++i)
			GetDlgItem(OTHERS[i])->EnableWindow(FALSE);
	}
	if (m_iState == STATE_IDLE && m_pInput->IsOpen())
		SetStatus(IdleStatus());
}

CString CRecordDlg::IdleStatus() const
{
	bool Auto;
	if (SelectedKind(Auto) == RECORDING_CHORDS)
		return _T("Press Record. After the count-in, strum along: a chord or two a bar, on the beat. Press Stop when you're ")
			_T("done, and the chords go to Add chords and bass line.");
	if (m_bHasSelection && IsDlgButtonChecked(IDC_REC_SELECTION) == BST_CHECKED)
		return _T("Press Record. The song plays a bar before the selection while counting in; come in with the downbeat. ")
			_T("The take stops by itself when the selection is over.");
	return _T("Press Record. After the count-in, whistle, hum or tap; press Stop when you're done. The notes go in at the cursor.");
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
		SetStatus(IdleStatus());
	else
		SetStatus(CString(Error.c_str()));
	m_iQuietTicks = 0;
}

void CRecordDlg::EnableOptions(bool Enable)
{
	static const int IDS[] = {
		IDC_REC_AUTO, IDC_REC_WHISTLE, IDC_REC_VOICE, IDC_REC_DRUMS, IDC_REC_CHORDS, IDC_REC_CHANNEL, IDC_REC_EXPRESSION,
		IDC_REC_SELECTION, IDC_REC_DEVICE, IDC_REC_CLICK, IDC_REC_CLICKVOLUME, IDC_REC_TEST, IDC_REC_PLAYSONG, IDC_REC_ASCLIP,
		IDC_REC_FROMFILE, IDC_TAP_TEMPO, IDC_REC_GROOVE
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

void CRecordDlg::ShowTempo()
{
	// The song's tempo, a beat being the first row highlight (as tapping and the tools count it)
	CMainFrame *pMainFrm = static_cast<CMainFrame*>(m_pView->GetParentFrame());
	const unsigned int Track = static_cast<unsigned int>(pMainFrm->GetSelectedTrack());
	SetDlgItemText(IDC_REC_TEMPO, BpmText(SongBpm(m_pDocument, Track)) + _T(" BPM"));
}

void CRecordDlg::UpdateTaps()
{
	// While the taps come, their tempo; once they stop, the song's speed and tempo for it
	if (!m_Taps.Tapping() && !m_Taps.Done())
		return;
	const double Bpm = m_Taps.Bpm();
	CString Text;
	if (!m_Taps.Done()) {
		if (Bpm > 0.0)
			Text.Format(_T("Tapping: %d BPM. Stop tapping to set it."), static_cast<int>(std::floor(Bpm + 0.5)));
		else
			Text.Format(_T("Tapping (%d): keep going, in time with the beat."), m_Taps.Taps());
		SetStatus(Text);
		return;
	}
	m_Taps.Clear();
	CMainFrame *pMainFrm = static_cast<CMainFrame*>(m_pView->GetParentFrame());
	const unsigned int Track = static_cast<unsigned int>(pMainFrm->GetSelectedTrack());
	int Speed = 0, Tempo = 0;
	double Played = 0.0;
	if (!TappedSongTempo(m_pDocument, Track, Bpm, Speed, Tempo, Played)) {
		SetStatus(_T("Tap at least four times, in time with the beat, to set the tempo."));
		return;
	}
	SetSongTempo(pMainFrm, Speed, Tempo);
	ShowTempo();
	Text.Format(_T("You tapped %d BPM: the song plays at %s BPM now (speed %d, tempo %d). After closing, Ctrl+Z puts the old ")
		_T("tempo back."), static_cast<int>(std::floor(Bpm + 0.5)), static_cast<LPCTSTR>(BpmText(Played)), Speed, Tempo);
	SetStatus(Text);
}

LRESULT CRecordDlg::OnTempoTap(WPARAM, LPARAM)
{
	if (m_iState == STATE_IDLE) {
		m_Taps.Tap();
		UpdateTaps();
	}
	return 0;
}

BOOL CRecordDlg::PreTranslateMessage(MSG *pMsg)
{
	// The T key taps the tempo too
	if (IsTapKey(pMsg) && m_iState == STATE_IDLE) {
		OnTempoTap(IDC_TAP_TEMPO, 0);
		return TRUE;
	}
	return CDialog::PreTranslateMessage(pMsg);
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
	if (m_iState != STATE_IDLE)
		return;
	bool Auto;
	if (SelectedKind(Auto) != RECORDING_CHORDS) {
		EndDialog(IDC_REC_FROMFILE);	// Insert recording
		return;
	}
	// Strummed chords in a WAV file, its first strum on the cursor's bar
	CFileDialog FileDialog(TRUE, _T("wav"), NULL, OFN_HIDEREADONLY | OFN_FILEMUSTEXIST,
		_T("WAV recordings (*.wav)|*.wav|All files (*.*)|*.*||"), this);
	if (FileDialog.DoModal() != IDOK)
		return;
	CMainFrame *pMainFrm = static_cast<CMainFrame*>(m_pView->GetParentFrame());
	const unsigned int Track = static_cast<unsigned int>(pMainFrm->GetSelectedTrack());
	const ftapi::Timing Time(static_cast<int>(m_pDocument->GetPatternLength(Track)), static_cast<int>(m_pDocument->GetFrameCount(Track)),
		m_pDocument->GetFirstHighlight(), m_pDocument->GetSecondHighlight());
	CString Names, Message;
	int FirstRow = 0;
	bool Heard;
	{
		CWaitCursor Wait;
		Heard = HearRecordedChords(m_pDocument, Track, FileDialog.GetPathName(), Time.BarStart(Time.BarOf(CursorSongRow())), Names,
			FirstRow, Message);
	}
	if (!Heard) {
		SetStatus(Message);
		return;
	}
	m_sChords = Names;
	m_iChordsRow = FirstRow;
	m_sResult = _T("Heard these chords: ") + Names;
	EndDialog(IDC_REC_CHORDS);
}

void CRecordDlg::OnGroove()
{
	// Drums, a bass line and chords to sing over, then back here to sing
	if (m_iState != STATE_IDLE)
		return;
	const bool First = m_bGrooveFirst;
	m_bGrooveFirst = false;
	CGrooveDlg Dialog(m_pView, this);
	if (Dialog.DoModal() != IDOK) {
		if (First)
			EndDialog(IDCANCEL);	// Start from a groove, called off
		return;
	}
	m_iMelodyChannel = Dialog.GetMelodyChannel();
	bool Auto;
	const recording_kind_t Kind = SelectedKind(Auto);
	if (Kind == RECORDING_DRUMS || Kind == RECORDING_CHORDS)
		SetKind(IDC_REC_AUTO);
	m_bHasSelection = false;	// the take goes where the groove starts
	CheckDlgButton(IDC_REC_SELECTION, BST_UNCHECKED);
	CheckDlgButton(IDC_REC_PLAYSONG, BST_CHECKED);
	FillChannels();
	ShowTempo();
	SetStatus(Dialog.GetResult() + _T(" Press Record and sing over it: it plays while counting in."));
	GotoDlgCtrl(GetDlgItem(IDC_REC_RECORD));
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
	if (m_iState == STATE_IDLE)
		UpdateTaps();

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

double CRecordDlg::SecondsIntoTake() const
{
	// From the downbeat (row 0 of the take) on
	const stRecordTake &Take = *m_pTake;
	if (Take.Click.countIn > 0)
		return double(Played() - Take.Click.StartSample()) / SAMPLE_RATE;
	const double Since = double(m_pInput->Received() - Take.Offset) / SAMPLE_RATE;
	return Take.SongPlaying ? Since - Take.PreRoll - theApp.GetSettings()->Sound.iBufferLength / 1000.0 : Since;
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
	const bool Chords = Take.Kind == RECORDING_CHORDS;
	Take.Channel = Chords ? -1 : PickedRecordingChannel(m_cChannel);
	Take.Expression = IsDlgButtonChecked(IDC_REC_EXPRESSION) == BST_CHECKED;
	Take.AsClip = !Chords && IsDlgButtonChecked(IDC_REC_ASCLIP) == BST_CHECKED;
	Take.PlaySong = IsDlgButtonChecked(IDC_REC_PLAYSONG) == BST_CHECKED;
	m_Taps.Clear();

	// The take goes in at the cursor, or over the selection from the beat it starts in
	const int Length = static_cast<int>(m_pDocument->GetPatternLength(Track));
	const int Total = static_cast<int>(m_pDocument->GetFrameCount(Track)) * Length;
	const double Rows = RowsPerSecond(m_pDocument, Track);
	const transcribe::Beat Beat = SongBeat(m_pDocument, Track);
	const int SecondHighlight = m_pDocument->GetSecondHighlight();
	const int BarRows = SecondHighlight > 0 ? SecondHighlight : Beat.rows * Beat.perBar;
	int Start = CursorSongRow();
	Take.Cursor = Start;
	Take.Selected = !Chords && m_bHasSelection && IsDlgButtonChecked(IDC_REC_SELECTION) == BST_CHECKED;
	Take.Seconds = 0.0;
	if (Take.Selected) {
		Take.Selection = m_Selection;
		Take.Selection.Early = std::max(1, Beat.rows / 2);	// a note sung up to half a beat early still counts
		Start = Take.Selection.First - (Take.Selection.First % Length % BarRows) % Beat.rows;
		Take.Cursor = Take.Selection.First;
		Take.Seconds = (Take.Selection.End - Start) / Rows + SELECTION_TAIL;
	}
	Take.Frame = Start / Length;
	Take.Row = Start % Length;

	// The click, at the song's tempo: a beat is a row highlight, its bars as the song's
	const click_t Click = ClickMode();
	m_iPreviewTicks = 0;
	Take.Click = transcribe::ClickTrack();
	Take.Click.sampleRate = SAMPLE_RATE;
	Take.Click.beatSeconds = Beat.rows / Rows;
	Take.Click.beatsPerBar = Beat.perBar;
	Take.Click.countIn = (Click == CLICK_NONE) ? 0 : Beat.countIn;
	Take.Click.clickAlong = (Click == CLICK_ALONG || Click == CLICK_BARS);
	Take.Click.barsOnly = (Click == CLICK_BARS);
	Take.Click.level = ClickLevel();
	if (BarRows == Beat.rows * Beat.perBar) {
		const int BeatInBar = (Start % Length % BarRows) / Beat.rows;
		Take.Click.barPhase = ((BeatInBar - Take.Click.countIn) % Beat.perBar + Beat.perBar) % Beat.perBar;
	}

	// The song along, from a bar before (the count-in's length): at the start of a song that loops, its end
	Take.PlayFrom = Start;
	Take.PreRoll = 0.0;
	if (Take.PlaySong && Length > 0) {
		const int Wanted = (Take.Click.countIn > 0 ? Take.Click.countIn : Beat.perBar) * Beat.rows;
		int From = Start - Wanted;
		if (From < 0 && Total >= Wanted && LoopsToStart(m_pDocument, Track))
			From += Total;
		else if (From < 0)
			From = 0;
		Take.PlayFrom = From;
		Take.PreRoll = (From <= Start ? Start - From : Start + Total - From) / Rows;
		const double Buffer = theApp.GetSettings()->Sound.iBufferLength / 1000.0;
		if (Take.PreRoll > 0.0)
			Take.Click.leadSeconds = std::max(Take.Click.leadSeconds, Buffer + 0.1);	// time to start the song
	}

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

	// The channel being recorded stays quiet, so an earlier take on it doesn't play along (chords mute nothing)
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

	// From a bar before the take
	CMainFrame *pMainFrm = static_cast<CMainFrame*>(m_pView->GetParentFrame());
	const int Length = static_cast<int>(m_pDocument->GetPatternLength(static_cast<unsigned int>(pMainFrm->GetSelectedTrack())));
	CPatternEditor *pEditor = m_pView->GetPatternEditor();
	if (Length > 0) {
		pEditor->MoveToFrame(Take.PlayFrom / Length);
		pEditor->MoveToRow(Take.PlayFrom % Length);
	}
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
	CMainFrame *pMainFrm = static_cast<CMainFrame*>(m_pView->GetParentFrame());
	const int Length = static_cast<int>(m_pDocument->GetPatternLength(static_cast<unsigned int>(pMainFrm->GetSelectedTrack())));
	CPatternEditor *pEditor = m_pView->GetPatternEditor();
	if (Length > 0) {
		pEditor->MoveToFrame(Take.Cursor / Length);
		pEditor->MoveToRow(Take.Cursor % Length);
	}
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

	// The song starts so that the take's first row is heard with the downbeat, the bar before it with the count-in:
	// FamiTracker's sound buffer takes its length to play
	if (Take.PlaySong && !Take.SongPlaying) {
		const long long Buffer = static_cast<long long>(theApp.GetSettings()->Sound.iBufferLength) * SAMPLE_RATE / 1000;
		const long long HalfTick = static_cast<long long>(SAMPLE_RATE) * TIMER_MS / 2000;
		const long long PreRoll = static_cast<long long>(Take.PreRoll * SAMPLE_RATE + 0.5);
		if (Now + Buffer + HalfTick >= Downbeat - PreRoll)
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

	const double Into = SecondsIntoTake();
	const int Seconds = static_cast<int>(std::max(0.0, Into));
	if (Seconds >= MAX_SECONDS || (Take.Seconds > 0.0 && Into >= Take.Seconds)) {
		FinishTake();	// over a selection: when it's over
		return;
	}
	CString Text;
	if (Take.Seconds > 0.0) {
		const int Left = static_cast<int>(std::ceil(std::max(0.0, Take.Seconds - SELECTION_TAIL - Into)));
		Text.Format(_T("Recording the selection: %d:%02d to go. It stops by itself."), Left / 60, Left % 60);
	}
	else
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
	if (Kind == RECORDING_CHORDS) {
		// Strummed chords, bar by bar, for Add chords and bass line
		const int Length = static_cast<int>(m_pDocument->GetPatternLength(static_cast<unsigned int>(pMainFrm->GetSelectedTrack())));
		CString Names;
		int FirstRow = 0;
		if (!HearRecordedChords(m_pDocument, static_cast<unsigned int>(pMainFrm->GetSelectedTrack()), Audio, Timing,
				Take.Frame * Length + Take.Row, Names, FirstRow, Message)) {
			Message += _T(" Press Record to try again.");
			SetStatus(Message);
			return;
		}
		m_sChords = Names;
		m_iChordsRow = FirstRow;
		m_sResult = _T("Heard these chords: ") + Names;
		EndDialog(IDC_REC_CHORDS);
		return;
	}
	const CString ClipName = m_pDocument->GetClips()->NewName(_T("Take"), true);
	if (!InsertTake(pMainFrm, Audio, Timing, Kind, Take.Channel, Take.Expression, Take.Frame, Take.Row, Message,
			Take.AsClip ? static_cast<LPCTSTR>(ClipName) : NULL, Take.Selected ? &Take.Selection : NULL)) {
		Message += _T(" Press Record to try again.");
		SetStatus(Message);
		return;
	}

	m_sResult = Message;
	if (!Saved.IsEmpty())
		m_sResult += _T(" The take is saved in ") + Saved.Left(Saved.ReverseFind(_T('\\'))) + _T(".");
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

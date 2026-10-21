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

// Edit > Sound effect from your voice (see VoiceEffectDlg.h). The dialog
// listens to the microphone all the time, for its level meter and for the
// microphone's own noise, which a take's sound has to rise above.

// Standard and engine headers come before stdafx.h, whose DEBUG_NEW macro
// would otherwise rewrite the placement new inside the standard library.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>
#include "Transcribe/Transcribe.h"
#include "Transcribe/SoundEffect.h"
#include "MicRecorder.h"
#include "stdafx.h"
#include <mmsystem.h>
#include "FamiTracker.h"
#include "FamiTrackerDoc.h"
#include "VoiceEffectDlg.h"
#include "AppFolders.h"

namespace {

const int SAMPLE_RATE = 44100;
const UINT_PTR TMR_POLL = 1;
const UINT TIMER_MS = 10;
LPCTSTR SECTION = _T("Voice sound effects");	// the dialog's options in the registry
LPCTSTR RECORDER_SECTION = _T("Recorder");		// the microphone, picked here or in Record from microphone
const float PREVIEW_GAIN = 2.5f;				// the NES's mix is quiet next to a recording
const size_t AMBIENT_READINGS = 50;				// the latest level readings (half a second), the quietest of
												// which is the microphone's own noise
const double LONGEST_FILE = 20.0;				// seconds: a WAV file for a sound effect is a short sound

struct pitch_choice_t {
	LPCTSTR Name;
	bool Fit;		// moved by octaves to where game sounds sit...
	int Octaves;	// ...or by these
};

const pitch_choice_t PITCHES[] = {
	{ _T("Where game sounds sit (recommended)"), true, 0 },
	{ _T("As you made it"), false, 0 },
	{ _T("1 octave higher"), false, 1 },
	{ _T("2 octaves higher"), false, 2 },
	{ _T("1 octave lower"), false, -1 },
};
const int PITCH_COUNT = sizeof(PITCHES) / sizeof(PITCHES[0]);

// A WAV file's bytes (16-bit mono), for PlaySound
std::vector<char> WavBytes(const std::vector<float> &Samples, int SampleRate, float Gain)
{
	const unsigned int Data = static_cast<unsigned int>(Samples.size() * 2);
	std::vector<char> Out;
	Out.reserve(44 + Data);
	auto Text = [&Out](const char *pText) {
		Out.insert(Out.end(), pText, pText + 4);
	};
	auto Bytes = [&Out](unsigned int Value, int Count) {
		for (int i = 0; i < Count; ++i)
			Out.push_back(static_cast<char>((Value >> (8 * i)) & 0xFF));
	};
	Text("RIFF");
	Bytes(36 + Data, 4);
	Text("WAVE");
	Text("fmt ");
	Bytes(16, 4);
	Bytes(1, 2);	// PCM
	Bytes(1, 2);	// mono
	Bytes(static_cast<unsigned int>(SampleRate), 4);
	Bytes(static_cast<unsigned int>(SampleRate) * 2, 4);
	Bytes(2, 2);
	Bytes(16, 2);
	Text("data");
	Bytes(Data, 4);
	for (size_t i = 0; i < Samples.size(); ++i) {
		const float Value = std::max(-1.0f, std::min(1.0f, Samples[i] * Gain));
		const int Sample = static_cast<int>(std::floor(Value * 32767.0f + 0.5f));
		Bytes(static_cast<unsigned int>(Sample) & 0xFFFF, 2);
	}
	return Out;
}

// Voice <date> <time>.wav in the Recordings folder next to FamiTracker.exe
// (AppFolders.h), with the takes of Record from microphone; the temporary
// folder when there's none
CString NewTakePath()
{
	CString Folder = RecordingsFolder(true);
	if (Folder.IsEmpty()) {
		TCHAR Temp[MAX_PATH];
		const DWORD Length = GetTempPath(MAX_PATH, Temp);
		Folder = (Length > 0 && Length < MAX_PATH) ? CString(Temp) : CString(_T("."));
		Folder.TrimRight(_T('\\'));
	}
	return Folder + _T("\\") + CTime::GetCurrentTime().Format(_T("Voice %Y-%m-%d %H-%M-%S.wav"));
}

// "SFX Pew" for pew.wav
CString NameFromFile(const CString &Path)
{
	CString Base = Path.Mid(Path.ReverseFind(_T('\\')) + 1);
	const int Dot = Base.ReverseFind(_T('.'));
	if (Dot > 0)
		Base = Base.Left(Dot);
	Base.Trim();
	if (Base.IsEmpty())
		return _T("SFX Voice");
	CString First = Base.Left(1);
	First.MakeUpper();
	return _T("SFX ") + First + Base.Mid(1);
}

}	// namespace

class CVoiceEffectDlg : public CDialog
{
public:
	CVoiceEffectDlg(CVoiceEffectMaker *pMaker, CWnd *pParent);
	virtual ~CVoiceEffectDlg();

protected:
	virtual BOOL OnInitDialog();
	virtual void OnOK();
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnDestroy();
	afx_msg void OnRecord();
	afx_msg void OnOpen();
	afx_msg void OnListen();
	afx_msg void OnPlayTake();
	afx_msg void OnAdd();
	afx_msg void OnDeviceChanged();
	afx_msg void OnOptionChanged();

	DECLARE_MESSAGE_MAP()

private:
	void FillDevices();
	void OpenDevice();
	void ShowLevel(float Peak);
	void SetStatus(LPCTSTR Text);
	void EnableControls();
	void StartTake();
	void FinishTake(bool Pressed);
	void UseSound(const transcribe::Audio &Sound, const CString &Path, LPCTSTR Before);
	void ShowEffect(LPCTSTR Before, bool Play);
	const pitch_choice_t &Pitch() const;
	bool Noise() const;
	bool Pal() const;
	void SaveOptions() const;
	void PlaySamples(const std::vector<float> &Samples, int SampleRate, float Gain);
	void StopSound();

	CVoiceEffectMaker *m_pMaker;
	CMicInput *m_pInput;
	transcribe::VoiceTake *m_pTake;		// while recording
	CComboBox m_cDevice;
	CComboBox m_cPitch;
	CProgressCtrl m_cLevel;

	std::vector<short> m_Received;		// from the microphone at the last poll
	std::vector<float> m_Converted;
	std::vector<float> m_Readings;		// the microphone's latest levels
	transcribe::Audio m_Sound;			// the take, or the file's sound
	CString m_sPath;					// its WAV file
	transcribe::SoundEffect m_Effect;	// what it makes
	bool m_bReady;						// m_Effect has a sound
	std::vector<char> m_Playing;		// what PlaySound plays, kept while it plays
	int m_iPlayTicks;					// timer ticks until it has played, and a little after

	CString m_sDevice;					// microphone picked, by name ("" = Windows' choice)
	float m_fLevel;						// level shown, dB
	int m_iLoudTicks;					// ticks left to show "too loud"
	int m_iQuietTicks;					// ticks in a row without sound
	int m_iTick;
};

BEGIN_MESSAGE_MAP(CVoiceEffectDlg, CDialog)
	ON_WM_TIMER()
	ON_WM_DESTROY()
	ON_BN_CLICKED(IDC_VOICE_RECORD, OnRecord)
	ON_BN_CLICKED(IDC_VOICE_OPEN, OnOpen)
	ON_BN_CLICKED(IDC_VOICE_LISTEN, OnListen)
	ON_BN_CLICKED(IDC_VOICE_TAKE, OnPlayTake)
	ON_BN_CLICKED(IDC_VOICE_ADD, OnAdd)
	ON_BN_CLICKED(IDC_VOICE_NOISE, OnOptionChanged)
	ON_CBN_SELCHANGE(IDC_VOICE_PITCH, OnOptionChanged)
	ON_CBN_SELCHANGE(IDC_VOICE_DEVICE, OnDeviceChanged)
END_MESSAGE_MAP()

CVoiceEffectDlg::CVoiceEffectDlg(CVoiceEffectMaker *pMaker, CWnd *pParent) :
	CDialog(IDD_VOICE_EFFECT, pParent),
	m_pMaker(pMaker),
	m_pInput(new CMicInput()),
	m_pTake(NULL),
	m_bReady(false),
	m_iPlayTicks(0),
	m_fLevel(-60.0f),
	m_iLoudTicks(0),
	m_iQuietTicks(0),
	m_iTick(0)
{
}

CVoiceEffectDlg::~CVoiceEffectDlg()
{
	delete m_pTake;
	delete m_pInput;
}

BOOL CVoiceEffectDlg::OnInitDialog()
{
	CDialog::OnInitDialog();

	m_cDevice.SubclassDlgItem(IDC_VOICE_DEVICE, this);
	m_cPitch.SubclassDlgItem(IDC_VOICE_PITCH, this);
	m_cLevel.SubclassDlgItem(IDC_VOICE_LEVEL, this);
	m_cLevel.SetRange(0, 100);
	RecordingsFolder(false);	// takes kept in Documents move next to FamiTracker

	for (int i = 0; i < PITCH_COUNT; ++i)
		m_cPitch.AddString(PITCHES[i].Name);
	m_cPitch.SetCurSel(std::max(0, std::min(PITCH_COUNT - 1, static_cast<int>(theApp.GetProfileInt(SECTION, _T("Pitch"), 0)))));
	CheckDlgButton(IDC_VOICE_NOISE, theApp.GetProfileInt(SECTION, _T("Noise"), 1) ? BST_CHECKED : BST_UNCHECKED);
	static_cast<CEdit*>(GetDlgItem(IDC_VOICE_NAME))->SetLimitText(60);
	SetDlgItemText(IDC_VOICE_NAME, m_pMaker->FreeSoundName(_T("SFX Voice")));

	m_sDevice = theApp.GetProfileString(RECORDER_SECTION, _T("Microphone"), _T(""));
	FillDevices();
	OpenDevice();
	SetTimer(TMR_POLL, TIMER_MS, NULL);
	if (m_pInput->IsOpen()) {
		GotoDlgCtrl(GetDlgItem(IDC_VOICE_RECORD));
		return FALSE;	// the focus is set
	}
	return TRUE;
}

void CVoiceEffectDlg::OnDestroy()
{
	KillTimer(TMR_POLL);
	StopSound();
	delete m_pTake;
	m_pTake = NULL;
	m_pInput->Close();
	CDialog::OnDestroy();
}

// The microphone

void CVoiceEffectDlg::FillDevices()
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

void CVoiceEffectDlg::OpenDevice()
{
	const int Item = m_cDevice.GetCurSel();
	const int Device = (Item != CB_ERR) ? static_cast<int>(m_cDevice.GetItemData(Item)) : -1;
	m_sDevice = (Device >= 0) ? CString(CMicInput::DeviceName(Device).c_str()) : CString();

	std::string Error;
	if (m_pInput->Open(Device, SAMPLE_RATE, Error))
		SetStatus(_T("Press Record and make the sound. Recording stops by itself once you're quiet."));
	else
		SetStatus(CString(Error.c_str()) + _T(" You can still open a WAV file."));
	m_Readings.clear();
	m_iQuietTicks = 0;
	EnableControls();
}

void CVoiceEffectDlg::OnDeviceChanged()
{
	if (m_pTake != NULL)
		return;
	OpenDevice();
	theApp.WriteProfileString(RECORDER_SECTION, _T("Microphone"), m_sDevice);
}

void CVoiceEffectDlg::ShowLevel(float Peak)
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
	GetDlgItemText(IDC_VOICE_LEVELTEXT, Old);
	if (Old != Text)
		SetDlgItemText(IDC_VOICE_LEVELTEXT, Text);
}

void CVoiceEffectDlg::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent != TMR_POLL) {
		CDialog::OnTimer(nIDEvent);
		return;
	}
	m_Received.clear();
	const float Peak = m_pInput->Poll(&m_Received);
	const bool Heard = !m_Received.empty();
	ShowLevel(Heard ? Peak : -1.0f);
	if (m_iPlayTicks > 0)
		--m_iPlayTicks;
	if (!Heard)
		return;
	if (m_pTake == NULL) {
		// The microphone's own noise, from what it hears while nothing plays
		if (m_iPlayTicks == 0 && !theApp.IsPlaying()) {
			m_Readings.push_back(Peak);
			if (m_Readings.size() > AMBIENT_READINGS)
				m_Readings.erase(m_Readings.begin());
		}
		return;
	}

	m_Converted.resize(m_Received.size());
	for (size_t i = 0; i < m_Received.size(); ++i)
		m_Converted[i] = m_Received[i] / 32768.0f;
	switch (m_pTake->Add(&m_Converted[0], m_Converted.size())) {
	case transcribe::VoiceTake::Status::Waiting:
		break;
	case transcribe::VoiceTake::Status::Sounding:
		SetStatus(_T("Recording... it stops by itself once you're quiet."));
		break;
	default:
		FinishTake(false);
		break;
	}
}

// The take

void CVoiceEffectDlg::OnOK()
{
	// Enter: in the name box it adds the sound; elsewhere it's Record or Stop
	if (::GetFocus() == GetDlgItem(IDC_VOICE_NAME)->GetSafeHwnd() && m_bReady && m_pTake == NULL)
		OnAdd();
	else
		OnRecord();
}

void CVoiceEffectDlg::OnRecord()
{
	if (m_pTake != NULL)
		FinishTake(true);
	else
		StartTake();
}

void CVoiceEffectDlg::StartTake()
{
	if (!m_pInput->IsOpen())
		return;
	StopSound();
	if (theApp.IsPlaying())
		theApp.StopPlayer();	// the song would be in the take
	const float Ambient = m_Readings.empty() ? 0.0f : *std::min_element(m_Readings.begin(), m_Readings.end());
	m_pInput->Poll(NULL);		// what came before Record isn't part of the take
	m_pTake = new transcribe::VoiceTake(m_pInput->SampleRate(), Ambient);
	SetDlgItemText(IDC_VOICE_RECORD, _T("Stop"));
	SetStatus(_T("Listening: make the sound now."));
	EnableControls();
	GotoDlgCtrl(GetDlgItem(IDC_VOICE_RECORD));	// Enter or Space stops it
}

// Pressed: Stop was pressed, rather than the take stopping by itself
void CVoiceEffectDlg::FinishTake(bool Pressed)
{
	if (m_pTake == NULL)
		return;
	transcribe::VoiceTake *pTake = m_pTake;
	m_pTake = NULL;
	pTake->Stop();	// when it's still going
	const bool TooLong = pTake->status() == transcribe::VoiceTake::Status::TooLong;
	const transcribe::Audio Sound = pTake->Sound();
	delete pTake;
	SetDlgItemText(IDC_VOICE_RECORD, _T("Record"));
	EnableControls();

	if (Sound.samples.empty()) {
		SetStatus(Pressed ? _T("Stopped before a sound came. Press Record and make the sound.") :
			_T("No sound came. Is the right microphone picked? The level meter moves when it hears you."));
		return;
	}
	const CString Path = NewTakePath();
	std::string Error;
	if (!transcribe::WriteWav(std::string(static_cast<LPCSTR>(CStringA(Path))), Sound, &Error)) {
		SetStatus(_T("The take couldn't be saved as ") + Path + _T(": ") + CString(Error.c_str()));
		return;
	}
	UseSound(Sound, Path, TooLong ? _T("That went on as long as a take can (5 s), so it ends there. ") : _T(""));
}

void CVoiceEffectDlg::OnOpen()
{
	if (m_pTake != NULL)
		return;
	StopSound();
	CFileDialog Dialog(TRUE, _T("wav"), NULL, OFN_HIDEREADONLY | OFN_FILEMUSTEXIST,
		_T("WAV files (*.wav)|*.wav|All files (*.*)|*.*||"), this);
	const CString Takes = RecordingsFolder(false);	// opens where the takes are
	if (GetFileAttributes(Takes) != INVALID_FILE_ATTRIBUTES)
		Dialog.m_ofn.lpstrInitialDir = Takes;
	if (Dialog.DoModal() != IDOK)
		return;
	const CString Path = Dialog.GetPathName();
	transcribe::Audio Sound;
	std::string Error;
	if (!transcribe::ReadWav(std::string(static_cast<LPCSTR>(CStringA(Path))), Sound, &Error)) {
		SetStatus(_T("That file couldn't be read: ") + CString(Error.c_str()));
		return;
	}
	if (Sound.Duration() > LONGEST_FILE) {
		CString Text;
		Text.Format(_T("That recording is %d seconds long. A sound effect comes from a short sound, of a few seconds at most."),
			static_cast<int>(Sound.Duration() + 0.5));
		SetStatus(Text);
		return;
	}
	SetDlgItemText(IDC_VOICE_NAME, m_pMaker->FreeSoundName(NameFromFile(Path)));
	UseSound(Sound, Path, _T(""));
}

// The sound effect

void CVoiceEffectDlg::UseSound(const transcribe::Audio &Sound, const CString &Path, LPCTSTR Before)
{
	m_Sound = Sound;
	m_sPath = Path;
	ShowEffect(Before, true);
}

const pitch_choice_t &CVoiceEffectDlg::Pitch() const
{
	const int Item = m_cPitch.GetCurSel();
	return PITCHES[(Item >= 0 && Item < PITCH_COUNT) ? Item : 0];
}

bool CVoiceEffectDlg::Noise() const
{
	return IsDlgButtonChecked(IDC_VOICE_NOISE) == BST_CHECKED;
}

bool CVoiceEffectDlg::Pal() const
{
	CFamiTrackerDoc *pDoc = CFamiTrackerDoc::GetDoc();
	return pDoc != NULL && pDoc->GetFrameRate() == 50;
}

void CVoiceEffectDlg::SaveOptions() const
{
	theApp.WriteProfileInt(SECTION, _T("Pitch"), std::max(0, m_cPitch.GetCurSel()));
	theApp.WriteProfileInt(SECTION, _T("Noise"), Noise() ? 1 : 0);
}

// Makes the sound effect as make_sound_effect will, says what it is, and plays it when asked
void CVoiceEffectDlg::ShowEffect(LPCTSTR Before, bool Play)
{
	transcribe::VoiceEffectOptions Options;
	Options.fitOctave = Pitch().Fit;
	Options.octaves = Pitch().Octaves;
	Options.noise = Noise();
	Options.pal = Pal();
	transcribe::VoiceEffectInfo Info;
	std::string Problem;
	m_bReady = transcribe::VoiceToEffect(m_Sound, Options, m_Effect, &Info, &Problem);

	CString Text(Before);
	if (!m_bReady) {
		Text += CString(Problem.c_str());
		if (Options.noise)
			Text += _T(" Try again a little louder, or closer to the microphone.");
	}
	else {
		CString Length;
		Length.Format(_T("A %.2f s sound"), Info.seconds);
		CString Moved;
		if (Info.octaves != 0)
			Moved.Format(_T(", %d %s %s"), std::abs(Info.octaves), std::abs(Info.octaves) == 1 ? _T("octave") : _T("octaves"),
				Info.octaves > 0 ? _T("higher") : _T("lower"));
		if (Info.pitched && Info.noisy)
			Text += Length + _T(": your voice's pitch on a pulse channel") + Moved + _T(", and its breathy part on the noise channel.");
		else if (Info.pitched)
			Text += Length + _T(": your voice's pitch on a pulse channel") + Moved + _T(".");
		else
			Text += Length + _T(" on the noise channel (it has no clear pitch).");
		if (Info.cut)
			Text += _T(" It ends where a sound effect has to.");
		Text += _T(" Listen plays it as the NES does; Add to the song makes it a track of its own.");
	}
	SetStatus(Text);
	EnableControls();
	if (m_bReady && Play)
		OnListen();
}

void CVoiceEffectDlg::OnOptionChanged()
{
	SaveOptions();
	if (!m_Sound.samples.empty() && m_pTake == NULL)
		ShowEffect(_T(""), true);
}

void CVoiceEffectDlg::OnListen()
{
	if (!m_bReady)
		return;
	const transcribe::Audio Rendered = transcribe::RenderEffect(m_Effect, SAMPLE_RATE, Pal());
	PlaySamples(Rendered.samples, Rendered.sampleRate, PREVIEW_GAIN);
}

void CVoiceEffectDlg::OnPlayTake()
{
	PlaySamples(m_Sound.samples, m_Sound.sampleRate, 1.0f);
}

void CVoiceEffectDlg::OnAdd()
{
	if (!m_bReady || m_sPath.IsEmpty() || m_pTake != NULL)
		return;
	StopSound();
	SaveOptions();
	CString Name;
	GetDlgItemText(IDC_VOICE_NAME, Name);
	Name.Trim();
	if (Name.IsEmpty())
		Name = m_pMaker->FreeSoundName(_T("SFX Voice"));
	CString Error;
	const int Track = m_pMaker->MakeSoundEffect(m_sPath, Name, Pitch().Fit, Pitch().Octaves, Noise(), Error);
	if (Track < 0) {
		SetStatus(Error);
		return;
	}
	CString Text;
	Text.Format(_T("Added track %d, \"%s\". It plays once and stops, so File > Export for games saves it as a sound effect. ")
		_T("Its instruments, named after it, hold its shape."), Track + 1, static_cast<LPCTSTR>(Name));
	SetStatus(Text);
	SetDlgItemText(IDC_VOICE_NAME, m_pMaker->FreeSoundName(Name));
	m_pMaker->PlaySelectedTrack();	// as FamiTracker plays it
}

// Playing

void CVoiceEffectDlg::PlaySamples(const std::vector<float> &Samples, int SampleRate, float Gain)
{
	StopSound();
	if (Samples.empty() || SampleRate <= 0)
		return;
	m_Playing = WavBytes(Samples, SampleRate, Gain);
	PlaySound(&m_Playing[0], NULL, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
	m_iPlayTicks = static_cast<int>(Samples.size() * 1000 / static_cast<size_t>(SampleRate) / TIMER_MS) + 30;
}

void CVoiceEffectDlg::StopSound()
{
	PlaySound(NULL, NULL, 0);	// returns once it has stopped, so the bytes can go
	m_Playing.clear();
	if (m_iPlayTicks > 30)
		m_iPlayTicks = 30;		// the room's echo of it
}

// The rest

void CVoiceEffectDlg::SetStatus(LPCTSTR Text)
{
	CString Old;
	GetDlgItemText(IDC_VOICE_STATUS, Old);
	if (Old != Text)
		SetDlgItemText(IDC_VOICE_STATUS, Text);
}

void CVoiceEffectDlg::EnableControls()
{
	const BOOL Idle = m_pTake == NULL ? TRUE : FALSE;
	GetDlgItem(IDC_VOICE_RECORD)->EnableWindow(m_pInput->IsOpen() ? TRUE : FALSE);
	GetDlgItem(IDC_VOICE_DEVICE)->EnableWindow(Idle);
	GetDlgItem(IDC_VOICE_OPEN)->EnableWindow(Idle);
	GetDlgItem(IDC_VOICE_PITCH)->EnableWindow(Idle);
	GetDlgItem(IDC_VOICE_NOISE)->EnableWindow(Idle);
	GetDlgItem(IDC_VOICE_NAME)->EnableWindow(Idle);
	GetDlgItem(IDC_VOICE_LISTEN)->EnableWindow(Idle && m_bReady ? TRUE : FALSE);
	GetDlgItem(IDC_VOICE_TAKE)->EnableWindow(Idle && !m_Sound.samples.empty() ? TRUE : FALSE);
	GetDlgItem(IDC_VOICE_ADD)->EnableWindow(Idle && m_bReady ? TRUE : FALSE);
}

void ShowVoiceEffectDialog(CVoiceEffectMaker *pMaker, CWnd *pParent)
{
	CVoiceEffectDlg Dialog(pMaker, pParent);
	Dialog.DoModal();
}

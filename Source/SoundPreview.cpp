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

// A sound heard (see SoundPreview.h). The notes go to the sound thread as the
// note keys' do (CSoundGen::QueueNote), a message-only window's timer timing
// them, so the preview goes on under modal dialogs too.

#include "stdafx.h"
#include <algorithm>
#include <string>
#include <vector>
#include "Api/SoundIndex.h"
#include "FamiTracker.h"
#include "FamiTrackerDoc.h"
#include "FamiTrackerView.h"
#include "MainFrm.h"
#include "SoundGen.h"
#include "TrackerChannel.h"
#include "McpConnection.h"
#include "SoundPreview.h"

namespace {

const UINT_PTR TMR_STEP = 1;
const UINT STEP_MS = 15;
const int KEY_RELEASE_MS = 1500;	// a note key let go: silent this long after, for a sound that holds through its release

// Text from the tools (UTF-8) as FamiTracker shows it (the Windows code page)
CString FromUtf8(const std::string &Text)
{
	if (Text.empty())
		return CString();
	const int Wide = ::MultiByteToWideChar(CP_UTF8, 0, Text.c_str(), static_cast<int>(Text.size()), NULL, 0);
	if (Wide <= 0)
		return CString(Text.c_str());
	std::vector<wchar_t> Buffer(static_cast<size_t>(Wide));
	::MultiByteToWideChar(CP_UTF8, 0, Text.c_str(), static_cast<int>(Text.size()), &Buffer[0], Wide);
	const int Bytes = ::WideCharToMultiByte(CP_ACP, 0, &Buffer[0], Wide, NULL, 0, NULL, NULL);
	if (Bytes <= 0)
		return CString(Text.c_str());
	std::vector<char> Out(static_cast<size_t>(Bytes) + 1, 0);
	::WideCharToMultiByte(CP_ACP, 0, &Buffer[0], Wide, &Out[0], Bytes, NULL, NULL);
	return CString(&Out[0]);
}

}	// namespace

BEGIN_MESSAGE_MAP(CSoundPreview, CWnd)
	ON_WM_TIMER()
END_MESSAGE_MAP()

CSoundPreview::CSoundPreview() :
	m_iNext(0),
	m_dwStart(0),
	m_iChannel(-1),
	m_iInstrument(-1),
	m_iVolume(MAX_VOLUME - 1),
	m_iKeyNote(-1),
	m_bSongStopped(true),
	m_iSerial(0)
{
}

CSoundPreview::~CSoundPreview()
{
	if (m_hWnd != NULL)
		DestroyWindow();
}

void CSoundPreview::Close()
{
	Stop();
	if (m_hWnd != NULL)
		DestroyWindow();
}

// Ready to play the instrument on the channel at the volume, what played before stopped
bool CSoundPreview::Start(int Channel, int Instrument, int Volume)
{
	CFamiTrackerDoc *pDoc = CFamiTrackerDoc::GetDoc();
	if (pDoc == NULL || !pDoc->IsFileLoaded() || Channel < 0 || Channel >= static_cast<int>(pDoc->GetAvailableChannels()) || Instrument < 0 ||
		Instrument >= MAX_INSTRUMENTS || !pDoc->IsInstrumentUsed(static_cast<unsigned int>(Instrument)))
		return false;
	if (m_hWnd == NULL && !CreateEx(0, AfxRegisterWndClass(0), _T("Sound preview"), 0, 0, 0, 0, 0, HWND_MESSAGE, NULL))
		return false;
	if (m_iChannel >= 0 && (m_iChannel != Channel || m_iInstrument != Instrument))
		Stop();
	if (theApp.IsPlaying())
		theApp.StopPlayer();	// the song would play over it
	m_bSongStopped = !theApp.IsPlaying();
	m_iChannel = Channel;
	m_iInstrument = Instrument;
	m_iVolume = std::max(0, std::min(MAX_VOLUME - 1, Volume));
	m_Steps.clear();
	m_iNext = 0;
	++m_iSerial;
	SetTimer(TMR_STEP, STEP_MS, NULL);
	return true;
}

bool CSoundPreview::Play(int Channel, int Instrument, const std::vector<ftapi::PreviewStep> &Steps, int Volume)
{
	if (!Start(Channel, Instrument, Volume))
		return false;
	m_iKeyNote = -1;
	for (size_t i = 0; i < Steps.size(); ++i) {
		const Step S = { Steps[i].At, Steps[i].Note };
		m_Steps.push_back(S);
	}
	m_dwStart = ::GetTickCount();
	OnTimer(TMR_STEP);		// the first note at once
	return true;
}

bool CSoundPreview::KeyDown(int Channel, int Instrument, int Note, int Volume)
{
	if (Note < 0 || Note >= NOTE_RANGE * OCTAVE_RANGE || !Start(Channel, Instrument, Volume))
		return false;
	Queue(Note);
	m_iKeyNote = Note;
	return true;
}

void CSoundPreview::KeyUp(int Note)
{
	if (Note != m_iKeyNote || m_iChannel < 0)
		return;
	Queue(ftapi::PREVIEW_RELEASE);
	m_iKeyNote = -1;
	// and silence a moment later, for a sound that holds through a release
	const Step End = { KEY_RELEASE_MS, ftapi::PREVIEW_HALT };
	m_Steps.assign(1, End);
	m_iNext = 0;
	m_dwStart = ::GetTickCount();
}

void CSoundPreview::Stop()
{
	if (m_iChannel < 0)
		return;
	Queue(ftapi::PREVIEW_HALT);
	Finish();
}

int CSoundPreview::PlayingInstrument() const
{
	return m_iChannel >= 0 ? m_iInstrument : -1;
}

// A note, a release or a halt to the channel
void CSoundPreview::Queue(int Note)
{
	CFamiTrackerDoc *pDoc = CFamiTrackerDoc::GetDoc();
	if (pDoc == NULL || m_iChannel < 0 || m_iChannel >= static_cast<int>(pDoc->GetAvailableChannels()))
		return;
	stChanNote Cell;
	memset(&Cell, 0, sizeof(Cell));
	if (Note == ftapi::PREVIEW_RELEASE)
		Cell.Note = RELEASE;
	else if (Note == ftapi::PREVIEW_HALT)
		Cell.Note = HALT;
	else {
		Cell.Note = static_cast<unsigned char>(Note % NOTE_RANGE + 1);
		Cell.Octave = static_cast<unsigned char>(Note / NOTE_RANGE);
	}
	// A note at the preview's volume (a release or a halt leaves it); a note without one plays at the channel's own,
	// full since the song stopped
	Cell.Vol = static_cast<unsigned char>(Note >= 0 ? m_iVolume : MAX_VOLUME);
	Cell.Instrument = static_cast<unsigned char>(Note == ftapi::PREVIEW_HALT ? MAX_INSTRUMENTS : m_iInstrument);
	theApp.GetSoundGenerator()->QueueNote(m_iChannel, Cell, NOTE_PRIO_2);
}

// Nothing plays any more (the channel left as it is)
void CSoundPreview::Finish()
{
	m_iChannel = -1;
	m_iKeyNote = -1;
	m_Steps.clear();
	m_iNext = 0;
	++m_iSerial;
	if (m_hWnd != NULL)
		KillTimer(TMR_STEP);
}

void CSoundPreview::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent != TMR_STEP) {
		CWnd::OnTimer(nIDEvent);
		return;
	}
	if (m_iChannel < 0) {
		KillTimer(TMR_STEP);
		return;
	}
	// The song started after the preview, or its instrument went: the channel is the song's again
	CFamiTrackerDoc *pDoc = CFamiTrackerDoc::GetDoc();
	if (!m_bSongStopped)
		m_bSongStopped = !theApp.IsPlaying();
	else if (theApp.IsPlaying()) {
		Finish();
		return;
	}
	if (pDoc == NULL || !pDoc->IsFileLoaded() || !pDoc->IsInstrumentUsed(static_cast<unsigned int>(m_iInstrument))) {
		Finish();
		return;
	}
	const DWORD Now = ::GetTickCount() - m_dwStart;
	while (m_iNext < m_Steps.size() && static_cast<DWORD>(m_Steps[m_iNext].At) <= Now) {
		Queue(m_Steps[m_iNext].Note);
		++m_iNext;
	}
	if (!m_Steps.empty() && m_iNext >= m_Steps.size())
		Finish();		// (the steps end with a halt)
}

void ListenToInstrument(CMainFrame *pMainFrame, int Instrument)
{
	CFamiTrackerDoc *pDoc = static_cast<CFamiTrackerDoc*>(pMainFrame->GetActiveDocument());
	if (pDoc == NULL || !pDoc->IsFileLoaded())
		return;
	if (Instrument < 0 || Instrument >= MAX_INSTRUMENTS || !pDoc->IsInstrumentUsed(static_cast<unsigned int>(Instrument))) {
		pMainFrame->SetMessageText(_T("Pick an instrument in the list to hear it."));
		return;
	}
	// The song's own instruments as the sound browser lists them, without the reference library (quick)
	CMcpConnection *pTools = pMainFrame->GetMcpConnection();
	CFamiTrackerView *pView = CFamiTrackerView::GetView();
	const int Cursor = pView != NULL ? static_cast<int>(pView->GetSelectedChannel()) : -1;
	std::vector<ftapi::SoundEntry> All;
	std::string Error;
	if (pTools == NULL || !pTools->Sounds(Cursor, All, Error, false)) {
		pMainFrame->SetMessageText(Error.empty() ? CString(_T("The instrument couldn't be played.")) : FromUtf8(Error));
		return;
	}
	const ftapi::SoundEntry *pSound = NULL;
	for (size_t i = 0; i < All.size(); ++i)
		if (All[i].Source == ftapi::SOURCE_SONG && All[i].Number == Instrument && All[i].Channel >= 0 &&
			(pSound == NULL || (All[i].Home && !pSound->Home)))
			pSound = &All[i];
	char Name[CInstrument::INST_NAME_MAX];
	pDoc->GetInstrumentName(static_cast<unsigned int>(Instrument), Name);
	CString Text;
	if (pSound == NULL) {
		Text.Format(_T("Instrument %02X - %s is for a chip this song doesn't use, so it has no channel to play on."), Instrument, Name);
		pMainFrame->SetMessageText(Text);
		return;
	}
	if (!pMainFrame->GetSoundPreview()->Play(pSound->Channel, Instrument, ftapi::PreviewSteps(*pSound), pSound->Volume)) {
		pMainFrame->SetMessageText(_T("The instrument couldn't be played."));
		return;
	}
	Text.Format(_T("Listening to %02X - %s on %s."), Instrument, Name, static_cast<LPCTSTR>(FromUtf8(pSound->ChannelName)));
	pMainFrame->SetMessageText(Text);
}

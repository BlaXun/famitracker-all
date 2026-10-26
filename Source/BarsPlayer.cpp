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

// Some bars played, with channels alone and then in the mix (see BarsPlayer.h)

#include <algorithm>
#include <vector>
#include "stdafx.h"
#include "FamiTracker.h"
#include "FamiTrackerDoc.h"
#include "FamiTrackerView.h"
#include "MainFrm.h"
#include "PatternEditor.h"
#include "SoundGen.h"
#include "BarsPlayer.h"

namespace {

const DWORD START_WAIT_MS = 2000;	// a start the sound thread never began: given up after this

CMainFrame *Main()
{
	return static_cast<CMainFrame*>(theApp.m_pMainWnd);
}

CFamiTrackerDoc *Doc()
{
	CMainFrame *pMain = Main();
	return pMain != NULL ? static_cast<CFamiTrackerDoc*>(pMain->GetActiveDocument()) : NULL;
}

// The pattern editor's header shows the mutes
void RedrawHeader(CFamiTrackerView *pView)
{
	if (pView == NULL || pView->GetSafeHwnd() == NULL)
		return;
	pView->GetPatternEditor()->InvalidateHeader();
	pView->RedrawWindow(pView->GetPatternEditor()->GetHeaderRect(), NULL, RDW_INVALIDATE);
}

}	// namespace

CBarsPlayer::CBarsPlayer() : m_iFirst(0), m_iEnd(0), m_Order(ALONE_THEN_MIX), m_iPass(-1), m_bStarted(false), m_iStarts(0),
	m_dwStarted(0), m_iSong(0)
{
}

CBarsPlayer::~CBarsPlayer()
{
	// (the owner stops it while the windows are still there)
}

bool CBarsPlayer::Play(int First, int End, const std::vector<int> &Alone, order_t Order)
{
	Stop();
	CFamiTrackerDoc *pDoc = Doc();
	CMainFrame *pMain = Main();
	if (pDoc == NULL || pMain == NULL || !pDoc->IsFileLoaded())
		return false;
	const unsigned int Track = static_cast<unsigned int>(std::max(0, pMain->GetSelectedTrack()));
	const int Rows = static_cast<int>(pDoc->GetPatternLength(Track) * pDoc->GetFrameCount(Track));
	First = std::max(0, First);
	End = std::min(End, Rows);
	if (First >= End)
		return false;
	m_iFirst = First;
	m_iEnd = End;
	m_Order = Order;
	m_iSong = pDoc->GetSongSerial();
	m_Alone.clear();
	const int Channels = static_cast<int>(pDoc->GetAvailableChannels());
	for (size_t i = 0; i < Alone.size(); ++i)
		if (Alone[i] >= 0 && Alone[i] < Channels && std::find(m_Alone.begin(), m_Alone.end(), Alone[i]) == m_Alone.end())
			m_Alone.push_back(Alone[i]);
	// The channels alone first; with none to hear alone (a silent bar), the mix
	return Start(Order != MIX && !m_Alone.empty());
}

void CBarsPlayer::Stop()
{
	if (m_iPass < 0)
		return;
	m_iPass = -1;
	if (theApp.IsPlaying())
		theApp.StopPlayerAndWait();
	RestoreMutes();
}

bool CBarsPlayer::Start(bool Alone)
{
	CFamiTrackerView *pView = CFamiTrackerView::GetView();
	CSoundGen *pSound = theApp.GetSoundGenerator();
	CFamiTrackerDoc *pDoc = Doc();
	CMainFrame *pMain = Main();
	if (pView == NULL || pSound == NULL || pDoc == NULL || pMain == NULL) {
		m_iPass = -1;
		RestoreMutes();
		return false;
	}
	// The cursor doesn't move while the song plays in follow mode, and no note is left sounding on a channel muted now
	if (theApp.IsPlaying())
		theApp.StopPlayerAndWait();
	SetMutes(Alone);
	const unsigned int Track = static_cast<unsigned int>(std::max(0, pMain->GetSelectedTrack()));
	const int Length = static_cast<int>(std::max(1U, pDoc->GetPatternLength(Track)));
	pSound->SetQueueFrame(-1);
	pView->SelectFrame(static_cast<unsigned int>(m_iFirst / Length));
	pView->GetPatternEditor()->MoveToRow(m_iFirst % Length);
	pView->SelectChannel(pView->GetSelectedChannel());	// draws the cursor where it is now
	pSound->SetNextPlayRange(m_iFirst, m_iEnd);
	m_iStarts = pSound->GetPlayStarts();
	m_bStarted = false;
	m_dwStarted = ::GetTickCount();
	m_iPass = Alone ? 0 : 1;
	theApp.StartPlayer(MODE_PLAY_CURSOR);
	return true;
}

bool CBarsPlayer::Tick()
{
	if (m_iPass < 0)
		return false;
	const CFamiTrackerDoc *pDoc = Doc();
	const CSoundGen *pSound = theApp.GetSoundGenerator();
	if (pDoc == NULL || pSound == NULL || pDoc->GetSongSerial() != m_iSong) {
		m_SavedMutes.clear();	// they were another song's channels
		m_iPass = -1;
		return true;
	}
	// The sound thread's count of starts first: a song seen not playing after it moved has stopped (at speed 1 a busy
	// computer may not run this timer while the bars play)
	const bool Started = pSound->GetPlayStarts() != m_iStarts;
	const bool Playing = theApp.IsPlaying();
	if (Playing || Started)
		m_bStarted = true;
	if (Playing || (!m_bStarted && ::GetTickCount() - m_dwStarted < START_WAIT_MS))
		return false;
	// Over: the bars played to their end (on to the mix after the channels alone), or the song was stopped
	if (m_iPass == 0 && m_bStarted && pSound->PlayRangeEnded() && m_Order == ALONE_THEN_MIX) {
		Start(false);
		return true;
	}
	m_iPass = -1;
	RestoreMutes();
	return true;
}

void CBarsPlayer::SetMutes(bool Alone)
{
	if (!Alone) {
		RestoreMutes();
		return;
	}
	CFamiTrackerView *pView = CFamiTrackerView::GetView();
	const CFamiTrackerDoc *pDoc = Doc();
	if (pView == NULL || pDoc == NULL)
		return;
	const unsigned int Count = pDoc->GetAvailableChannels();
	if (m_SavedMutes.empty())
		for (unsigned int c = 0; c < Count; ++c)
			m_SavedMutes.push_back(pView->IsChannelMuted(c));
	for (unsigned int c = 0; c < Count; ++c)
		pView->SetChannelMute(static_cast<int>(c), std::find(m_Alone.begin(), m_Alone.end(), static_cast<int>(c)) == m_Alone.end());
	RedrawHeader(pView);
}

void CBarsPlayer::RestoreMutes()
{
	if (m_SavedMutes.empty())
		return;
	CFamiTrackerView *pView = CFamiTrackerView::GetView();
	const CFamiTrackerDoc *pDoc = Doc();
	if (pView != NULL && pDoc != NULL && pDoc->GetSongSerial() == m_iSong) {
		for (unsigned int c = 0; c < m_SavedMutes.size() && c < pDoc->GetAvailableChannels(); ++c)
			pView->SetChannelMute(static_cast<int>(c), m_SavedMutes[c]);
		RedrawHeader(pView);
	}
	m_SavedMutes.clear();
}

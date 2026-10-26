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

#include <vector>

// Some bars of the selected track, played for the problems panel's Play it and
// Play the fix (ProblemsPanel.cpp) and the What next panel's grown melody
// (WhatNextPanel.cpp): from the first row to the end of the last bar, through
// CSoundGen::SetNextPlayRange, as the example songs' walkthroughs play their
// bars. First, when asked, with some channels alone (the others muted as the
// pattern editor's mutes do, and the mutes put back after), then in the mix.
// The owner's timer calls Tick while it plays.
class CBarsPlayer
{
public:
	enum order_t {
		ALONE_THEN_MIX,		// the channels alone, then the whole song there
		ALONE,
		MIX
	};

	CBarsPlayer();
	~CBarsPlayer();

	// Song rows [First, End) of the selected track, from the start: Alone's
	// channels alone first (when Order asks for them and there are any), then
	// in the mix. False when nothing can play (no song, no rows).
	bool Play(int First, int End, const std::vector<int> &Alone, order_t Order);
	void Stop();						// at once, the mutes put back
	bool IsPlaying() const { return m_iPass >= 0; }
	bool PlaysAlone() const { return m_iPass == 0; }	// the channels alone now, before the mix
	// Called often while it plays (every 100 ms or so): goes on to the mix
	// after the channels alone, and ends when the bars are over or the song
	// was stopped. True when it moved on or ended (to draw it anew).
	bool Tick();
	int First() const { return m_iFirst; }
	int End() const { return m_iEnd; }

private:
	bool Start(bool Alone);
	void SetMutes(bool Alone);
	void RestoreMutes();

	int m_iFirst, m_iEnd;
	std::vector<int> m_Alone;
	order_t m_Order;
	int m_iPass;					// -1 stopped, 0 the channels alone, 1 the mix
	bool m_bStarted;				// the sound thread began this pass
	unsigned int m_iStarts;			// CSoundGen::GetPlayStarts when the pass was started
	unsigned long m_dwStarted;		// when (GetTickCount)
	unsigned int m_iSong;			// the song it plays (CFamiTrackerDoc::GetSongSerial)
	std::vector<bool> m_SavedMutes;	// the channels' mutes before the channels alone

	CBarsPlayer(const CBarsPlayer &);
	CBarsPlayer &operator=(const CBarsPlayer &);
};

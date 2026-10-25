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

// Hearing a sound with FamiTracker's own engine: a preview's notes (the
// phrase of Api/SoundIndex.h's PreviewSteps) or a note key's note, played on
// one of the song's channels with one instrument, a note at a time from a
// timer of its own. One plays at a time: starting another stops it, and so
// does the song starting. The sound browser (SoundBrowserDlg.cpp) and the
// main window's Instrument > Listen (the play button under the instrument
// list, ListenToInstrument) share the main frame's (CMainFrame::GetSoundPreview).

#include <vector>

namespace ftapi {
	struct PreviewStep;
}
class CMainFrame;

class CSoundPreview : public CWnd
{
public:
	CSoundPreview();
	virtual ~CSoundPreview();

	// Plays the steps on the channel (the song's, by index) with the instrument: what played before stops, and the
	// song if it plays. False when it can't (no song, no such channel or instrument). Volume is the volume column of
	// its notes (0-15): the sound's SoundEntry::Volume, about as loud as a 2A03 pulse at 15 on every chip (the VRC7's
	// 13), and never the volume the song happened to leave its channel at
	bool Play(int Channel, int Instrument, const std::vector<ftapi::PreviewStep> &Steps, int Volume = 15);
	// A note key (or a MIDI key) pressed while a sound is picked: the instrument plays the note (FamiTracker's
	// pitch, C-0 = 0; a noise pitch on the noise) at the volume until it's let go, and is silent a moment after that
	bool KeyDown(int Channel, int Instrument, int Note, int Volume = 15);
	void KeyUp(int Note);
	// Silent at once
	void Stop();
	// The instrument playing, -1 while nothing plays
	int PlayingInstrument() const;
	// Counts up each time a preview starts or ends, so a window that shows what plays knows to look again
	unsigned Serial() const { return m_iSerial; }
	// Stopped, and its window gone (the main frame's OnDestroy)
	void Close();

protected:
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	DECLARE_MESSAGE_MAP()

private:
	struct Step {
		int At;			// ms from the start
		int Note;		// a pitch, PREVIEW_RELEASE or PREVIEW_HALT
	};

	bool Start(int Channel, int Instrument, int Volume);
	void Queue(int Note);
	void Finish();

	std::vector<Step> m_Steps;
	size_t m_iNext;
	DWORD m_dwStart;
	int m_iChannel;			// -1 while nothing plays
	int m_iInstrument;
	int m_iVolume;			// the notes' volume column (0-15)
	int m_iKeyNote;			// the note a key holds, -1 for none
	bool m_bSongStopped;	// the song was seen stopped since this began: a song that starts now ends it
	unsigned m_iSerial;
};

// Instrument > Listen and the instrument list's play button: the song's instrument plays the phrase of its kind
// (Api/SoundIndex.h) on the channel the track plays it most, or where its chip's sounds go, at its preview's volume, as
// the sound browser plays the song's own instruments; the status bar says what plays, or why it can't
void ListenToInstrument(CMainFrame *pMainFrame, int Instrument);

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

// View > Mixer: a strip for every channel of the module, in channel order
// (the 2A03's, then each expansion chip's that is on), with the channel's
// name and chip, a level meter while the song plays, mute and solo (the
// pattern editor's own channel mutes, kept in step), a fader in dB (-24 to
// +6, 0 as the channel plays), a pan (left to right, the middle unless
// moved) and two knobs, Soft and Space (0 off to 100: a gentle low-pass,
// and the send to the space every channel shares, whose room, echo and
// repeats are chosen beside the button at the top; APU/Space.h). They're
// saved with the song (Api/ChannelMix.h, the BF MIXER block) and played by
// FamiTracker's mixer (CMixer, through
// CSoundGen::SetChannelMix) in playback, File > Create WAV and the game's
// OGG files: they aren't edits to the notes, and the pan is where a channel
// starts, before any 8xx. NSF files don't have them; "Write into the notes"
// puts the faders into the volume columns and the pans as 8xx on each
// channel's first note, in every track (Claude's set_mixer with
// write_into_notes, one undoable step), and sets the mixer back. Every other
// change is one undoable step too, a fader or pan dragged one for the drag.

#include <vector>
#include "Api/ChannelMix.h"

class CMainFrame;
class CFamiTrackerDoc;

class CMixerPanelWnd : public CWnd
{
public:
	CMixerPanelWnd();
	virtual ~CMixerPanelWnd();

	bool Create(CWnd *pParent, CMainFrame *pMain);
	void Opened(bool Open);					// the meters move while it's shown
	void SongChanged();						// the channels or their settings may have changed

protected:
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC *pDC);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar *pScrollBar);
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnRButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnCaptureChanged(CWnd *pWnd);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnDestroy();
	afx_msg void OnWriteIntoNotes();
	afx_msg void OnSpaceChosen();

	DECLARE_MESSAGE_MAP()

private:
	enum part_t { PART_NONE, PART_MUTE, PART_SOLO, PART_PAN, PART_FADER, PART_SOFT, PART_SPACE };

	CFamiTrackerDoc *Doc() const;
	int ChannelCount() const;
	CRect StripRect(int Channel) const;
	CRect MuteRect(int Channel) const;
	CRect SoloRect(int Channel) const;
	CRect PanRect(int Channel) const;		// the pan's slider
	CRect FaderRect(int Channel) const;		// the fader's track, top to bottom
	CRect MeterRect(int Channel) const;
	CRect KnobRect(int Channel, part_t Part) const;	// Soft's or Space's knob
	int FaderY(int Channel, int Level) const;
	int LevelAt(int Channel, int y, bool Fine) const;
	int PanAt(int Channel, int x) const;
	part_t HitTest(CPoint Point, int &Channel) const;
	void Change(int Channel, int Level, int Pan);	// live, while dragging: the song's settings and the sound
	void Commit();							// the drag's change as one undoable step
	void SetOne(int Channel, int Level, int Pan);	// one undoable step
	void ChangeDepth(int Channel, part_t Part, int Value, bool Live);	// Soft or Space: live while dragging, else one step
	void ShowSpace();						// the room, echo and repeats chosen in their boxes
	void PaintKnob(CDC &dc, const CRect &Knob, int Value, LPCTSTR Label);
	void ResetAll();
	void UpdateScroll();
	void PaintStrip(CDC &dc, int Channel, CFamiTrackerDoc *pDoc);
	void MakeFonts();

	CMainFrame *m_pMain;
	CButton m_wndWrite;
	CComboBox m_wndRoom, m_wndEcho, m_wndRepeats;	// the space
	std::vector<int> m_EchoSteps;			// the echo box's choices, in 48ths of a beat
	CFont m_Font, m_BoldFont, m_SmallFont;
	bool m_bOpen;
	int m_iScroll;							// pixels scrolled to the right
	// A drag: what it changes and the settings before it, for one undoable step
	part_t m_iDragPart;
	int m_iDragChannel;
	int m_iDragOffset;						// from the fader's knob to where it was grabbed
	int m_iDragFrom, m_iDragValue;			// a knob: where the drag started (y) and its value then
	bool m_bDragMoved;
	ftapi::MixStore m_DragBefore;
	// What was drawn, to draw again only when it changes
	std::vector<int> m_Meters;
	std::vector<bool> m_Muted;
	ftapi::MixStore m_Shown;

	CMixerPanelWnd(const CMixerPanelWnd &);
	CMixerPanelWnd &operator=(const CMixerPanelWnd &);
};

// The mixer docked along the bottom of the main window; the line along its
// top edge is dragged to make it taller. Its height and whether it was open
// are remembered.
class CMixerBar : public CControlBar
{
public:
	CMixerBar();
	virtual ~CMixerBar();

	bool Create(CFrameWnd *pFrame);			// hidden at first
	void Opened(bool Open);					// shown or hidden by the main frame
	void SongChanged();
	static bool WasOpen();					// it was open when FamiTracker closed (closed the first time)
	// Along the bottom (CMainFrame::PlaceBottomPanel): the height it was made, and the least it's usable at
	int BottomHeight() const { return m_iHeight; }
	int LeastHeight() const;

	virtual CSize CalcFixedLayout(BOOL bStretch, BOOL bHorz);
	virtual void OnUpdateCmdUI(CFrameWnd *pTarget, BOOL bDisableIfNoHndler);
	virtual BOOL PreTranslateMessage(MSG *pMsg);

protected:
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC *pDC);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg BOOL OnSetCursor(CWnd *pWnd, UINT nHitTest, UINT message);
	afx_msg void OnCaptureChanged(CWnd *pWnd);
	afx_msg void OnDestroy();
	afx_msg LRESULT OnSizeParent(WPARAM wParam, LPARAM lParam);

	DECLARE_MESSAGE_MAP()

private:
	int Divider() const;					// the line along the top edge
	int MaxHeight() const;					// leaving the pattern editor some room

	CMixerPanelWnd m_wndPanel;
	int m_iHeight;
	int m_iRoom;							// for the pattern editor and the bar, at the last layout
	int m_iDragFrom;						// -1, or where the line was grabbed (screen y)
	int m_iDragHeight;
	int m_iDragMax;

	CMixerBar(const CMixerBar &);
	CMixerBar &operator=(const CMixerBar &);
};

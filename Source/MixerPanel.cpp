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

// The mixer (see MixerPanel.h)

// Standard headers come before stdafx.h, whose DEBUG_NEW macro would
// otherwise rewrite the placement new inside the standard library.
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include "stdafx.h"
#pragma push_macro("new")
#undef new
#include <afxpriv.h>		// WM_SIZEPARENT, for laying out the docked bar
#pragma pop_macro("new")
#include "FamiTracker.h"
#include "FamiTrackerDoc.h"
#include "FamiTrackerView.h"
#include "TrackerChannel.h"
#include "MainFrm.h"
#include "McpConnection.h"
#include "InsertRecording.h"
#include "MixerPanel.h"

namespace {

LPCTSTR SECTION = _T("Mixer");			// registry section: open, its height

// Sizes at 96 DPI (scaled with SX and SY)
const int DEFAULT_HEIGHT = 240;
const int MIN_HEIGHT = 200;
const int DIVIDER = 5;
const int TOP_BAR = 28;					// the button and the line beside it
const int STRIP_WIDTH = 68;
const int NAME_HEIGHT = 30;				// the channel's name, its chip under it
const int BUTTON_ROW = 20;				// mute and solo
const int BUTTON_WIDTH = 26;
const int PAN_ROW = 28;					// the pan's slider, where it is under it
const int READOUT = 14;					// the fader's dB under it
const int MARGIN = 4;
const int MIN_FADER = 60;

const UINT_PTR TMR_METERS = 1;
const UINT METER_MS = 40;
const int METER_STEPS = 15;				// as the pattern editor's meters

const COLORREF PANEL_BACK = RGB(232, 234, 238);
const COLORREF STRIP_BACK = RGB(248, 249, 251);
const COLORREF STRIP_LINE = RGB(205, 209, 216);
const COLORREF TEXT_DARK = RGB(30, 32, 40);
const COLORREF TEXT_DIM = RGB(110, 114, 122);
const COLORREF TRACK = RGB(150, 155, 165);
const COLORREF KNOB = RGB(70, 110, 180);
const COLORREF KNOB_OFF = RGB(130, 150, 185);	// a fader or pan at its default
const COLORREF MUTE_ON = RGB(226, 96, 64);
const COLORREF SOLO_ON = RGB(236, 196, 60);
const COLORREF BUTTON_OFF = RGB(225, 228, 233);
const COLORREF METER_BACK = RGB(52, 56, 64);

const int IDC_MIXER_WRITE = 1904;		// the mixer's range of control IDs (1900-1949)
const UINT MENU_LEVEL = 1, MENU_PAN = 2, MENU_CHANNEL = 3, MENU_ALL = 4, MENU_WRITE = 5;

// The pan as 8xx has it from a place between -1 (left) and 1 (right), as the tools' PanParam
int PanParam(double Position)
{
	if (!(Position > -1.0))
		return 0x00;
	if (Position >= 1.0)
		return 0xFF;
	return Position < 0.0 ? 0x80 + static_cast<int>(std::floor(Position * 128.0 + 0.5)) :
		0x80 + static_cast<int>(std::floor(Position * 127.0 + 0.5));
}

double PanPosition(int Param)
{
	Param = std::max(0, std::min(0xFF, Param));
	return Param < 0x80 ? (Param - 0x80) / 128.0 : (Param - 0x80) / 127.0;
}

CString PanLabel(int Pan)
{
	if (Pan == ftapi::MIX_PAN_CENTER)
		return _T("C");
	CString Text;
	const double Position = PanPosition(Pan);
	Text.Format(_T("%s %d"), Position < 0.0 ? _T("L") : _T("R"), std::max(1, static_cast<int>(std::floor(std::fabs(Position) * 100.0 + 0.5))));
	return Text;
}

CString LevelLabel(int Level)
{
	CString Text;
	if (Level == 0)
		Text = _T("0 dB");
	else
		Text.Format(_T("%+.1f dB"), Level / 10.0);
	return Text;
}

LPCTSTR ChipLabel(int Chip)
{
	switch (Chip) {
	case SNDCHIP_VRC6: return _T("VRC6");
	case SNDCHIP_VRC7: return _T("VRC7");
	case SNDCHIP_FDS: return _T("FDS");
	case SNDCHIP_MMC5: return _T("MMC5");
	case SNDCHIP_N163: return _T("N163");
	case SNDCHIP_S5B: return _T("5B");
	}
	return _T("2A03");
}

}	// namespace

// The panel

BEGIN_MESSAGE_MAP(CMixerPanelWnd, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_SIZE()
	ON_WM_HSCROLL()
	ON_WM_MOUSEWHEEL()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONDBLCLK()
	ON_WM_LBUTTONUP()
	ON_WM_MOUSEMOVE()
	ON_WM_RBUTTONUP()
	ON_WM_CAPTURECHANGED()
	ON_WM_TIMER()
	ON_WM_DESTROY()
	ON_BN_CLICKED(IDC_MIXER_WRITE, OnWriteIntoNotes)
END_MESSAGE_MAP()

CMixerPanelWnd::CMixerPanelWnd() : m_pMain(NULL), m_bOpen(false), m_iScroll(0), m_iDragPart(PART_NONE), m_iDragChannel(-1),
	m_iDragOffset(0), m_bDragMoved(false)
{
}

CMixerPanelWnd::~CMixerPanelWnd()
{
}

void CMixerPanelWnd::MakeFonts()
{
	LOGFONT Log;
	memset(&Log, 0, sizeof(Log));
	::GetObject(::GetStockObject(DEFAULT_GUI_FONT), sizeof(Log), &Log);
	Log.lfHeight = -SY(12);
	_tcscpy_s(Log.lfFaceName, LF_FACESIZE, _T("Segoe UI"));
	m_Font.CreateFontIndirect(&Log);
	Log.lfWeight = FW_BOLD;
	m_BoldFont.CreateFontIndirect(&Log);
	Log.lfWeight = FW_NORMAL;
	Log.lfHeight = -SY(11);
	m_SmallFont.CreateFontIndirect(&Log);
}

bool CMixerPanelWnd::Create(CWnd *pParent, CMainFrame *pMain)
{
	m_pMain = pMain;
	MakeFonts();
	const CString Class = AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(NULL, IDC_ARROW), NULL, NULL);
	if (!CWnd::Create(Class, _T("Mixer"), WS_CHILD | WS_VISIBLE | WS_HSCROLL | WS_CLIPCHILDREN, CRect(0, 0, 0, 0), pParent, 1))
		return false;
	if (!m_wndWrite.Create(_T("Write into the notes"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
		CRect(SX(MARGIN + 2), SY(4), SX(MARGIN + 2 + 136), SY(TOP_BAR - 4)), this, IDC_MIXER_WRITE))
		return false;
	m_wndWrite.SetFont(&m_Font);
	UpdateScroll();
	return true;
}

void CMixerPanelWnd::Opened(bool Open)
{
	m_bOpen = Open;
	if (Open) {
		SetTimer(TMR_METERS, METER_MS, NULL);
		SongChanged();
	}
	else
		KillTimer(TMR_METERS);
}

void CMixerPanelWnd::SongChanged()
{
	if (GetSafeHwnd() == NULL)
		return;
	UpdateScroll();
	Invalidate(FALSE);
}

CFamiTrackerDoc *CMixerPanelWnd::Doc() const
{
	CFamiTrackerDoc *pDoc = m_pMain != NULL ? static_cast<CFamiTrackerDoc*>(m_pMain->GetActiveDocument()) : NULL;
	return pDoc != NULL && pDoc->IsFileLoaded() ? pDoc : NULL;
}

int CMixerPanelWnd::ChannelCount() const
{
	const CFamiTrackerDoc *pDoc = Doc();
	return pDoc != NULL ? static_cast<int>(pDoc->GetAvailableChannels()) : 0;
}

// Where things are

CRect CMixerPanelWnd::StripRect(int Channel) const
{
	CRect Client;
	GetClientRect(&Client);
	const int Left = SX(MARGIN) + Channel * SX(STRIP_WIDTH) - m_iScroll;
	const int Least = SY(TOP_BAR + NAME_HEIGHT + BUTTON_ROW + PAN_ROW + 6 + MIN_FADER + 6 + READOUT);
	return CRect(Left, SY(TOP_BAR), Left + SX(STRIP_WIDTH) - SX(2), std::max(Least, static_cast<int>(Client.bottom) - SY(MARGIN)));
}

CRect CMixerPanelWnd::MuteRect(int Channel) const
{
	const CRect Strip = StripRect(Channel);
	const int Top = Strip.top + SY(NAME_HEIGHT), Middle = Strip.CenterPoint().x;
	return CRect(Middle - SX(BUTTON_WIDTH) - SX(2), Top, Middle - SX(2), Top + SY(BUTTON_ROW - 3));
}

CRect CMixerPanelWnd::SoloRect(int Channel) const
{
	CRect Mute = MuteRect(Channel);
	Mute.OffsetRect(SX(BUTTON_WIDTH) + SX(4), 0);
	return Mute;
}

CRect CMixerPanelWnd::PanRect(int Channel) const
{
	const CRect Strip = StripRect(Channel);
	const int Top = Strip.top + SY(NAME_HEIGHT + BUTTON_ROW) + SY(3);
	return CRect(Strip.left + SX(8), Top, Strip.right - SX(8), Top + SY(12));
}

CRect CMixerPanelWnd::FaderRect(int Channel) const
{
	const CRect Strip = StripRect(Channel);
	const int Top = Strip.top + SY(NAME_HEIGHT + BUTTON_ROW + PAN_ROW) + SY(6);
	const int Bottom = std::max(Top + SY(MIN_FADER), static_cast<int>(Strip.bottom) - SY(READOUT) - SY(6));
	const int x = Strip.left + SX(22);
	return CRect(x - SX(2), Top, x + SX(2), Bottom);
}

CRect CMixerPanelWnd::MeterRect(int Channel) const
{
	const CRect Fader = FaderRect(Channel);
	const CRect Strip = StripRect(Channel);
	return CRect(Strip.right - SX(20), Fader.top, Strip.right - SX(10), Fader.bottom);
}

int CMixerPanelWnd::FaderY(int Channel, int Level) const
{
	const CRect Fader = FaderRect(Channel);
	const double Share = static_cast<double>(Level - ftapi::MIX_LEVEL_LOWEST) / (ftapi::MIX_LEVEL_HIGHEST - ftapi::MIX_LEVEL_LOWEST);
	return Fader.bottom - static_cast<int>(std::floor(Share * Fader.Height() + 0.5));
}

int CMixerPanelWnd::LevelAt(int Channel, int y, bool Fine) const
{
	// Half a dB at a time (a tenth with Shift held), from -24 to +6
	const CRect Fader = FaderRect(Channel);
	const double Share = static_cast<double>(Fader.bottom - y) / std::max(1, Fader.Height());
	double Level = ftapi::MIX_LEVEL_LOWEST + Share * (ftapi::MIX_LEVEL_HIGHEST - ftapi::MIX_LEVEL_LOWEST);
	const double Step = Fine ? 1.0 : 5.0;
	Level = std::floor(Level / Step + 0.5) * Step;
	return std::max(ftapi::MIX_LEVEL_LOWEST, std::min(ftapi::MIX_LEVEL_HIGHEST, static_cast<int>(Level)));
}

int CMixerPanelWnd::PanAt(int Channel, int x) const
{
	const CRect Pan = PanRect(Channel);
	const double Half = std::max(1, Pan.Width() / 2);
	const double Position = std::max(-1.0, std::min(1.0, (x - Pan.CenterPoint().x) / Half));
	return std::fabs(Position) < 0.05 ? ftapi::MIX_PAN_CENTER : PanParam(Position);	// it catches in the middle
}

CMixerPanelWnd::part_t CMixerPanelWnd::HitTest(CPoint Point, int &Channel) const
{
	Channel = -1;
	for (int c = 0; c < ChannelCount(); ++c) {
		if (!StripRect(c).PtInRect(Point))
			continue;
		Channel = c;
		CRect Mute = MuteRect(c), Solo = SoloRect(c), Pan = PanRect(c), Fader = FaderRect(c);
		Pan.InflateRect(SX(6), SY(5));
		Fader.InflateRect(SX(12), SY(8));
		if (Mute.PtInRect(Point))
			return PART_MUTE;
		if (Solo.PtInRect(Point))
			return PART_SOLO;
		if (Pan.PtInRect(Point))
			return PART_PAN;
		if (Fader.PtInRect(Point))
			return PART_FADER;
		return PART_NONE;
	}
	return PART_NONE;
}

void CMixerPanelWnd::UpdateScroll()
{
	CRect Client;
	GetClientRect(&Client);
	const int Width = SX(MARGIN) * 2 + ChannelCount() * SX(STRIP_WIDTH);
	const int Most = std::max(0, Width - Client.Width());
	m_iScroll = std::max(0, std::min(m_iScroll, Most));
	SCROLLINFO Info;
	memset(&Info, 0, sizeof(Info));
	Info.cbSize = sizeof(Info);
	Info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
	Info.nMin = 0;
	Info.nMax = std::max(0, Width - 1);
	Info.nPage = static_cast<UINT>(std::max(0, static_cast<int>(Client.Width())));
	Info.nPos = m_iScroll;
	SetScrollInfo(SB_HORZ, &Info, TRUE);
}

// Changes

void CMixerPanelWnd::Change(int Channel, int Level, int Pan)
{
	CFamiTrackerDoc *pDoc = Doc();
	if (pDoc == NULL || Channel < 0 || Channel >= ChannelCount())
		return;
	if (pDoc->GetChannelMix()->Set(pDoc->GetChannelType(Channel), Level, Pan)) {
		pDoc->ChannelMixChanged();	// heard at once; one undoable step when the drag ends
		m_bDragMoved = true;
		Invalidate(FALSE);
	}
}

void CMixerPanelWnd::Commit()
{
	CFamiTrackerDoc *pDoc = Doc();
	if (pDoc != NULL && m_bDragMoved && *pDoc->GetChannelMix() != m_DragBefore)
		SetChannelMix(m_pMain, *pDoc->GetChannelMix(), &m_DragBefore);
	m_bDragMoved = false;
}

void CMixerPanelWnd::SetOne(int Channel, int Level, int Pan)
{
	CFamiTrackerDoc *pDoc = Doc();
	if (pDoc == NULL || Channel < 0 || Channel >= ChannelCount())
		return;
	ftapi::MixStore Mix = *pDoc->GetChannelMix();
	if (Mix.Set(pDoc->GetChannelType(Channel), Level, Pan))
		SetChannelMix(m_pMain, Mix);
	Invalidate(FALSE);
}

void CMixerPanelWnd::ResetAll()
{
	CFamiTrackerDoc *pDoc = Doc();
	if (pDoc == NULL)
		return;
	ftapi::MixStore Mix;
	if (Mix != *pDoc->GetChannelMix())
		SetChannelMix(m_pMain, Mix);
	Invalidate(FALSE);
}

void CMixerPanelWnd::OnWriteIntoNotes()
{
	CFamiTrackerDoc *pDoc = Doc();
	CMcpConnection *pTools = m_pMain != NULL ? m_pMain->GetMcpConnection() : NULL;
	if (pDoc == NULL || pTools == NULL)
		return;
	ftapi::MixStore Mix = *pDoc->GetChannelMix();
	Mix.KeepOnly(pDoc->GetModuleChannelIds());
	if (Mix.Empty()) {
		AfxMessageBox(_T("Every channel is at 0 dB and in the middle: there's nothing to write into the notes."), MB_ICONINFORMATION);
		return;
	}
	if (AfxMessageBox(_T("Write the mixer into the notes, in every track? The faders go into the channels' volume columns and the ")
		_T("pans become 8xx on each channel's first note, so NSF files sound the same; then the faders go back to 0 dB and ")
		_T("the middle.\n\nEdit > Undo puts everything back."), MB_ICONQUESTION | MB_OKCANCEL) != IDOK)
		return;
	std::string Reply;
	if (pTools->IsBusy())
		Reply = "FamiTracker is busy with Claude Code; try again in a moment.";
	else
		pTools->RunTool("set_mixer", "{\"write_into_notes\": true}", Reply);
	CString Text(Reply.c_str());	// the tools' replies are UTF-8; set_mixer's are plain
	Text.Replace(_T("\n"), _T("\r\n"));
	AfxMessageBox(Text, MB_ICONINFORMATION);
	SongChanged();
	// The keyboard back to the pattern editor, not the button
	CView *pView = m_pMain->GetActiveView();
	if (pView != NULL)
		pView->SetFocus();
}

// Drawing

BOOL CMixerPanelWnd::OnEraseBkgnd(CDC *pDC)
{
	return TRUE;
}

void CMixerPanelWnd::OnSize(UINT nType, int cx, int cy)
{
	CWnd::OnSize(nType, cx, cy);
	UpdateScroll();
	Invalidate(FALSE);
}

void CMixerPanelWnd::OnPaint()
{
	CPaintDC Paint(this);
	CRect Client;
	GetClientRect(&Client);
	CDC dc;
	CBitmap Bitmap;
	dc.CreateCompatibleDC(&Paint);
	Bitmap.CreateCompatibleBitmap(&Paint, std::max(1, Client.Width()), std::max(1, Client.Height()));
	CBitmap *pOldBitmap = dc.SelectObject(&Bitmap);
	CFont *pOldFont = dc.SelectObject(&m_Font);
	dc.FillSolidRect(Client, PANEL_BACK);
	dc.SetBkMode(TRANSPARENT);

	CFamiTrackerDoc *pDoc = Doc();
	// Beside the button: what the mixer is for
	CRect Line(SX(MARGIN + 2 + 136 + 10), 0, Client.right - SX(MARGIN), SY(TOP_BAR));
	dc.SetTextColor(TEXT_DIM);
	dc.DrawText(_T("Each channel's level and pan, saved with the song: FamiTracker, WAV and the game's OGG files play them. ")
		_T("NSF files play the notes as written; Write into the notes puts the mix there."), Line,
		DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

	const int Count = ChannelCount();
	m_Meters.resize(static_cast<size_t>(Count));
	m_Muted.resize(static_cast<size_t>(Count));
	for (int c = 0; c < Count; ++c) {
		const CRect Strip = StripRect(c);
		if (Strip.right < Client.left || Strip.left > Client.right)
			continue;
		PaintStrip(dc, c, pDoc);
	}
	if (Count == 0) {
		dc.SetTextColor(TEXT_DIM);
		dc.DrawText(_T("No song is open."), CRect(SX(MARGIN), SY(TOP_BAR), Client.right, Client.bottom), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	}

	Paint.BitBlt(0, 0, Client.Width(), Client.Height(), &dc, 0, 0, SRCCOPY);
	dc.SelectObject(pOldFont);
	dc.SelectObject(pOldBitmap);
}

void CMixerPanelWnd::PaintStrip(CDC &dc, int Channel, CFamiTrackerDoc *pDoc)
{
	const CRect Strip = StripRect(Channel);
	const int ChanId = pDoc->GetChannelType(Channel);
	const ftapi::MixStore &Mix = *pDoc->GetChannelMix();
	const int LevelNow = Mix.Level(ChanId), Pan = Mix.Pan(ChanId);
	const CFamiTrackerView *pView = CFamiTrackerView::GetView();
	const bool Muted = pView != NULL && pView->IsChannelMuted(static_cast<unsigned int>(Channel));
	bool Solo = !Muted;
	for (int c = 0; c < ChannelCount() && Solo; ++c)
		if (c != Channel && pView != NULL && !pView->IsChannelMuted(static_cast<unsigned int>(c)))
			Solo = false;
	if (ChannelCount() < 2)
		Solo = false;

	dc.FillSolidRect(Strip, STRIP_BACK);
	dc.Draw3dRect(Strip, STRIP_LINE, STRIP_LINE);

	// The name and the chip
	CRect Name(Strip.left + SX(2), Strip.top + SY(3), Strip.right - SX(2), Strip.top + SY(18));
	dc.SelectObject(&m_BoldFont);
	dc.SetTextColor(Muted ? TEXT_DIM : TEXT_DARK);
	dc.DrawText(pDoc->GetChannel(Channel)->GetChannelName(), Name, DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
	CRect Chip(Name.left, Name.bottom, Name.right, Name.bottom + SY(13));
	dc.SelectObject(&m_SmallFont);
	dc.SetTextColor(TEXT_DIM);
	dc.DrawText(pDoc->IsVrc6PlusChannel(Channel) ? _T("VRC6 Plus") : ChipLabel(pDoc->GetChipType(Channel)), Chip,
		DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);

	// Mute and solo
	const CRect MuteBox = MuteRect(Channel), SoloBox = SoloRect(Channel);
	dc.FillSolidRect(MuteBox, Muted ? MUTE_ON : BUTTON_OFF);
	dc.Draw3dRect(MuteBox, STRIP_LINE, TRACK);
	dc.FillSolidRect(SoloBox, Solo ? SOLO_ON : BUTTON_OFF);
	dc.Draw3dRect(SoloBox, STRIP_LINE, TRACK);
	dc.SelectObject(&m_BoldFont);
	dc.SetTextColor(Muted ? RGB(255, 255, 255) : TEXT_DARK);
	CRect Text = MuteBox;
	dc.DrawText(_T("M"), Text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	dc.SetTextColor(TEXT_DARK);
	Text = SoloBox;
	dc.DrawText(_T("S"), Text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

	// The pan: a slider, the middle marked
	const CRect PanBox = PanRect(Channel);
	const int PanY = PanBox.CenterPoint().y;
	dc.FillSolidRect(PanBox.left, PanY - 1, PanBox.Width(), 2, TRACK);
	dc.FillSolidRect(PanBox.CenterPoint().x, PanBox.top + SY(1), 1, PanBox.Height() - SY(2), TRACK);
	const int PanX = PanBox.CenterPoint().x + static_cast<int>(std::floor(PanPosition(Pan) * (PanBox.Width() / 2) + 0.5));
	const CRect PanKnob(PanX - SX(4), PanBox.top, PanX + SX(4), PanBox.bottom);
	dc.FillSolidRect(PanKnob, Pan == ftapi::MIX_PAN_CENTER ? KNOB_OFF : KNOB);
	dc.SelectObject(&m_SmallFont);
	dc.SetTextColor(Pan == ftapi::MIX_PAN_CENTER ? TEXT_DIM : TEXT_DARK);
	CRect PanText(Strip.left, PanBox.bottom + SY(1), Strip.right, PanBox.bottom + SY(14));
	dc.DrawText(PanLabel(Pan), PanText, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);

	// The fader: its track with marks at +6, 0, -6, -12, -18 and -24 dB, and its knob
	const CRect Fader = FaderRect(Channel);
	dc.FillSolidRect(Fader, TRACK);
	for (int Mark = ftapi::MIX_LEVEL_HIGHEST; Mark >= ftapi::MIX_LEVEL_LOWEST; Mark -= 60) {
		const int y = FaderY(Channel, Mark);
		dc.FillSolidRect(Strip.left + SX(6), y, Mark == 0 ? SX(26) : SX(9), 1, Mark == 0 ? TEXT_DARK : TRACK);
	}
	const int KnobY = FaderY(Channel, LevelNow);
	const CRect Knob(Fader.CenterPoint().x - SX(9), KnobY - SY(5), Fader.CenterPoint().x + SX(9), KnobY + SY(5));
	dc.FillSolidRect(Knob, LevelNow == 0 ? KNOB_OFF : KNOB);
	dc.FillSolidRect(Knob.left + SX(2), KnobY, Knob.Width() - SX(4), 1, RGB(255, 255, 255));

	// The meter, after the fader (as loud as the channel plays with it)
	const CRect Meter = MeterRect(Channel);
	dc.FillSolidRect(Meter, METER_BACK);
	int Lit = 0;
	if (theApp.IsPlaying()) {
		const double Value = pDoc->GetChannel(Channel)->GetVolumeMeter() * ftapi::MixStore::Gain(LevelNow);
		Lit = std::max(0, std::min(METER_STEPS, static_cast<int>(std::floor(Value + 0.5))));
	}
	const int Step = std::max(2, Meter.Height() / METER_STEPS);
	for (int i = 0; i < Lit; ++i) {
		const int Bottom = Meter.bottom - i * Step;
		const COLORREF Color = i >= 13 ? RGB(232, 72, 56) : i >= 10 ? RGB(236, 200, 60) : RGB(84, 196, 108);
		dc.FillSolidRect(Meter.left + 1, Bottom - Step + 1, Meter.Width() - 2, Step - 1, Color);
	}
	m_Meters[static_cast<size_t>(Channel)] = Lit;
	m_Muted[static_cast<size_t>(Channel)] = Muted;
	m_Shown = Mix;

	// The level, under the fader
	dc.SelectObject(&m_SmallFont);
	dc.SetTextColor(LevelNow == 0 ? TEXT_DIM : TEXT_DARK);
	CRect Readout(Strip.left, Fader.bottom + SY(4), Strip.right, Fader.bottom + SY(4 + READOUT));
	dc.DrawText(LevelLabel(LevelNow), Readout, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);
	dc.SelectObject(&m_Font);
}

// The mouse

void CMixerPanelWnd::OnLButtonDown(UINT nFlags, CPoint point)
{
	int Channel = -1;
	const part_t Part = HitTest(point, Channel);
	CFamiTrackerDoc *pDoc = Doc();
	CFamiTrackerView *pView = CFamiTrackerView::GetView();
	if (pDoc == NULL || Channel < 0)
		return;
	const int ChanId = pDoc->GetChannelType(Channel);
	switch (Part) {
	case PART_MUTE:
		if (pView != NULL)
			pView->ToggleChannel(static_cast<unsigned int>(Channel));
		Invalidate(FALSE);
		return;
	case PART_SOLO:
		if (pView != NULL)
			pView->SoloChannel(static_cast<unsigned int>(Channel));
		Invalidate(FALSE);
		return;
	case PART_PAN:
	case PART_FADER:
		m_iDragPart = Part;
		m_iDragChannel = Channel;
		m_iDragOffset = 0;
		m_bDragMoved = false;
		m_DragBefore = *pDoc->GetChannelMix();
		SetCapture();
		if (Part == PART_PAN)
			Change(Channel, pDoc->GetChannelMix()->Level(ChanId), PanAt(Channel, point.x));
		else {
			// The knob grabbed moves with the mouse from where it is; elsewhere on the track it jumps there
			const int KnobY = FaderY(Channel, pDoc->GetChannelMix()->Level(ChanId));
			if (std::abs(point.y - KnobY) <= SY(6))
				m_iDragOffset = point.y - KnobY;
			else
				Change(Channel, LevelAt(Channel, point.y, (nFlags & MK_SHIFT) != 0), pDoc->GetChannelMix()->Pan(ChanId));
		}
		return;
	default:
		return;
	}
}

void CMixerPanelWnd::OnMouseMove(UINT nFlags, CPoint point)
{
	CFamiTrackerDoc *pDoc = Doc();
	if (m_iDragPart == PART_NONE || pDoc == NULL || m_iDragChannel >= ChannelCount())
		return;
	const int ChanId = pDoc->GetChannelType(m_iDragChannel);
	if (m_iDragPart == PART_PAN)
		Change(m_iDragChannel, pDoc->GetChannelMix()->Level(ChanId), PanAt(m_iDragChannel, point.x));
	else
		Change(m_iDragChannel, LevelAt(m_iDragChannel, point.y - m_iDragOffset, (nFlags & MK_SHIFT) != 0), pDoc->GetChannelMix()->Pan(ChanId));
}

void CMixerPanelWnd::OnLButtonUp(UINT nFlags, CPoint point)
{
	if (m_iDragPart == PART_NONE)
		return;
	m_iDragPart = PART_NONE;	// before ReleaseCapture, whose WM_CAPTURECHANGED would end it too
	ReleaseCapture();
	Commit();
}

void CMixerPanelWnd::OnCaptureChanged(CWnd *pWnd)
{
	if (m_iDragPart != PART_NONE) {
		m_iDragPart = PART_NONE;
		Commit();
	}
	CWnd::OnCaptureChanged(pWnd);
}

void CMixerPanelWnd::OnLButtonDblClk(UINT nFlags, CPoint point)
{
	// Back to 0 dB, or to the middle
	int Channel = -1;
	const part_t Part = HitTest(point, Channel);
	CFamiTrackerDoc *pDoc = Doc();
	if (pDoc == NULL || Channel < 0)
		return;
	const int ChanId = pDoc->GetChannelType(Channel);
	if (Part == PART_FADER)
		SetOne(Channel, 0, pDoc->GetChannelMix()->Pan(ChanId));
	else if (Part == PART_PAN)
		SetOne(Channel, pDoc->GetChannelMix()->Level(ChanId), ftapi::MIX_PAN_CENTER);
	else if (Part == PART_MUTE || Part == PART_SOLO)
		OnLButtonDown(nFlags, point);	// a second click
}

BOOL CMixerPanelWnd::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
	// Over a fader: a dB a notch; elsewhere the strips scroll
	CPoint Point = pt;
	ScreenToClient(&Point);
	int Channel = -1;
	CFamiTrackerDoc *pDoc = Doc();
	if (pDoc != NULL && HitTest(Point, Channel) == PART_FADER && Channel >= 0) {
		const int ChanId = pDoc->GetChannelType(Channel);
		const int Notches = zDelta / WHEEL_DELTA;
		SetOne(Channel, pDoc->GetChannelMix()->Level(ChanId) + Notches * ((nFlags & MK_SHIFT) != 0 ? 1 : 10), pDoc->GetChannelMix()->Pan(ChanId));
		return TRUE;
	}
	m_iScroll = std::max(0, m_iScroll - zDelta / WHEEL_DELTA * SX(STRIP_WIDTH));
	UpdateScroll();
	Invalidate(FALSE);
	return TRUE;
}

void CMixerPanelWnd::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar *pScrollBar)
{
	SCROLLINFO Info;
	memset(&Info, 0, sizeof(Info));
	Info.cbSize = sizeof(Info);
	Info.fMask = SIF_ALL;
	GetScrollInfo(SB_HORZ, &Info);
	int Pos = m_iScroll;
	switch (nSBCode) {
	case SB_LINELEFT: Pos -= SX(STRIP_WIDTH); break;
	case SB_LINERIGHT: Pos += SX(STRIP_WIDTH); break;
	case SB_PAGELEFT: Pos -= static_cast<int>(Info.nPage); break;
	case SB_PAGERIGHT: Pos += static_cast<int>(Info.nPage); break;
	case SB_THUMBTRACK:
	case SB_THUMBPOSITION: Pos = Info.nTrackPos; break;
	case SB_LEFT: Pos = 0; break;
	case SB_RIGHT: Pos = Info.nMax; break;
	default: return;
	}
	m_iScroll = std::max(0, Pos);
	UpdateScroll();
	Invalidate(FALSE);
}

void CMixerPanelWnd::OnRButtonUp(UINT nFlags, CPoint point)
{
	int Channel = -1;
	HitTest(point, Channel);
	CFamiTrackerDoc *pDoc = Doc();
	if (pDoc == NULL)
		return;
	CMenu Menu;
	Menu.CreatePopupMenu();
	if (Channel >= 0) {
		CString Name = pDoc->GetChannel(Channel)->GetChannelName();
		Menu.AppendMenu(MF_STRING, MENU_LEVEL, _T("Level back to 0 dB"));
		Menu.AppendMenu(MF_STRING, MENU_PAN, _T("Pan to the middle"));
		Menu.AppendMenu(MF_STRING, MENU_CHANNEL, _T("Both, for ") + Name);
		Menu.AppendMenu(MF_SEPARATOR, 0, _T(""));
	}
	Menu.AppendMenu(MF_STRING, MENU_ALL, _T("Every channel back to 0 dB and the middle"));
	Menu.AppendMenu(MF_STRING, MENU_WRITE, _T("Write into the notes..."));
	CPoint Screen = point;
	ClientToScreen(&Screen);
	const UINT Choice = Menu.TrackPopupMenu(TPM_RETURNCMD | TPM_RIGHTBUTTON, Screen.x, Screen.y, this);
	const int ChanId = Channel >= 0 ? pDoc->GetChannelType(Channel) : -1;
	switch (Choice) {
	case MENU_LEVEL: SetOne(Channel, 0, pDoc->GetChannelMix()->Pan(ChanId)); break;
	case MENU_PAN: SetOne(Channel, pDoc->GetChannelMix()->Level(ChanId), ftapi::MIX_PAN_CENTER); break;
	case MENU_CHANNEL: SetOne(Channel, 0, ftapi::MIX_PAN_CENTER); break;
	case MENU_ALL: ResetAll(); break;
	case MENU_WRITE: OnWriteIntoNotes(); break;
	}
}

void CMixerPanelWnd::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent != TMR_METERS) {
		CWnd::OnTimer(nIDEvent);
		return;
	}
	// The meters (the sound thread's levels, read without waiting on it) and the mutes, drawn again when they change
	CFamiTrackerDoc *pDoc = Doc();
	const int Count = ChannelCount();
	// (and the settings, which a song opened or Claude changes)
	bool Changed = static_cast<int>(m_Meters.size()) != Count || (pDoc != NULL && *pDoc->GetChannelMix() != m_Shown);
	const CFamiTrackerView *pView = CFamiTrackerView::GetView();
	const bool Playing = theApp.IsPlaying();
	for (int c = 0; c < Count && !Changed && pDoc != NULL; ++c) {
		int Lit = 0;
		if (Playing) {
			const double Value = pDoc->GetChannel(c)->GetVolumeMeter() * ftapi::MixStore::Gain(pDoc->GetChannelMix()->Level(pDoc->GetChannelType(c)));
			Lit = std::max(0, std::min(METER_STEPS, static_cast<int>(std::floor(Value + 0.5))));
		}
		const bool Muted = pView != NULL && pView->IsChannelMuted(static_cast<unsigned int>(c));
		Changed = Lit != m_Meters[static_cast<size_t>(c)] || Muted != m_Muted[static_cast<size_t>(c)];
	}
	if (Changed)
		Invalidate(FALSE);
}

void CMixerPanelWnd::OnDestroy()
{
	KillTimer(TMR_METERS);
	CWnd::OnDestroy();
}

// The bar it is docked in

BEGIN_MESSAGE_MAP(CMixerBar, CControlBar)
	ON_WM_SIZE()
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_MOUSEMOVE()
	ON_WM_SETCURSOR()
	ON_WM_CAPTURECHANGED()
	ON_WM_DESTROY()
	ON_MESSAGE(WM_SIZEPARENT, OnSizeParent)
END_MESSAGE_MAP()

CMixerBar::CMixerBar() : m_iHeight(DEFAULT_HEIGHT), m_iRoom(0), m_iDragFrom(-1), m_iDragHeight(0), m_iDragMax(0)
{
}

CMixerBar::~CMixerBar()
{
}

bool CMixerBar::Create(CFrameWnd *pFrame)
{
	m_iHeight = std::max(SY(MIN_HEIGHT), static_cast<int>(theApp.GetProfileInt(SECTION, _T("Height"), SY(DEFAULT_HEIGHT))));
	m_dwStyle = CBRS_ALIGN_BOTTOM;
	const CString Class = AfxRegisterWndClass(0, ::LoadCursor(NULL, IDC_ARROW), NULL, NULL);
	if (!CWnd::Create(Class, _T("Mixer"), WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, CRect(0, 0, 0, 0), pFrame,
		AFX_IDW_CONTROLBAR_FIRST + 35))
		return false;
	return m_wndPanel.Create(this, static_cast<CMainFrame*>(pFrame));
}

void CMixerBar::Opened(bool Open)
{
	m_wndPanel.Opened(Open);
	theApp.WriteProfileInt(SECTION, _T("Open"), Open ? 1 : 0);
}

void CMixerBar::SongChanged()
{
	m_wndPanel.SongChanged();
}

bool CMixerBar::WasOpen()
{
	return theApp.GetProfileInt(SECTION, _T("Open"), 0) != 0;
}

int CMixerBar::Divider() const
{
	return std::max(4, SY(DIVIDER));
}

int CMixerBar::MaxHeight() const
{
	return std::max(SY(MIN_HEIGHT), m_iRoom / 2);	// the pattern editor keeps half
}

LRESULT CMixerBar::OnSizeParent(WPARAM wParam, LPARAM lParam)
{
	const AFX_SIZEPARENTPARAMS *pLayout = reinterpret_cast<const AFX_SIZEPARENTPARAMS*>(lParam);
	m_iRoom = pLayout->rect.bottom - pLayout->rect.top;
	return CControlBar::OnSizeParent(wParam, lParam);
}

CSize CMixerBar::CalcFixedLayout(BOOL bStretch, BOOL bHorz)
{
	return CSize(32767, std::max(0, std::min(m_iHeight, MaxHeight())));
}

void CMixerBar::OnUpdateCmdUI(CFrameWnd *pTarget, BOOL bDisableIfNoHndler)
{
}

BOOL CMixerBar::PreTranslateMessage(MSG *pMsg)
{
	// Not as a dialog (as control bars are), so FamiTracker's shortcuts work
	return CWnd::PreTranslateMessage(pMsg);
}

void CMixerBar::OnSize(UINT nType, int cx, int cy)
{
	CControlBar::OnSize(nType, cx, cy);
	if (m_wndPanel.GetSafeHwnd() != NULL)
		m_wndPanel.MoveWindow(0, Divider(), cx, std::max(0, cy - Divider()));
}

void CMixerBar::OnPaint()
{
	CPaintDC dc(this);
	CRect Rect;
	GetClientRect(&Rect);
	const int Line = Divider();
	const COLORREF Face = ::GetSysColor(COLOR_3DFACE), Light = ::GetSysColor(COLOR_3DHILIGHT), Shadow = ::GetSysColor(COLOR_3DSHADOW);
	dc.FillSolidRect(Rect.left, Rect.top, Rect.Width(), Line, Face);
	dc.FillSolidRect(Rect.left, Rect.top, Rect.Width(), 1, Light);
	dc.FillSolidRect(Rect.left, Rect.top + Line - 1, Rect.Width(), 1, Shadow);
	const int X = Rect.left + Rect.Width() / 2 - SX(12);
	for (int i = 0; i < 4; ++i)
		dc.FillSolidRect(X + i * SX(7), Rect.top + Line / 2 - 1, SX(3), 2, Shadow);
}

BOOL CMixerBar::OnEraseBkgnd(CDC *pDC)
{
	return TRUE;
}

void CMixerBar::OnLButtonDown(UINT nFlags, CPoint point)
{
	if (point.y >= Divider())
		return;
	CPoint Screen = point;
	ClientToScreen(&Screen);
	m_iDragFrom = static_cast<int>(Screen.y);
	m_iDragHeight = m_iHeight;
	m_iDragMax = MaxHeight();
	SetCapture();
}

void CMixerBar::OnMouseMove(UINT nFlags, CPoint point)
{
	if (m_iDragFrom < 0)
		return;
	CPoint Screen = point;
	ClientToScreen(&Screen);
	const int New = std::max(SY(MIN_HEIGHT), std::min(m_iDragMax, m_iDragHeight - (static_cast<int>(Screen.y) - m_iDragFrom)));
	if (New != m_iHeight) {
		m_iHeight = New;
		CFrameWnd *pFrame = GetParentFrame();
		if (pFrame != NULL)
			pFrame->RecalcLayout();
	}
}

void CMixerBar::OnLButtonUp(UINT nFlags, CPoint point)
{
	if (m_iDragFrom >= 0) {
		m_iDragFrom = -1;
		ReleaseCapture();
		theApp.WriteProfileInt(SECTION, _T("Height"), m_iHeight);
	}
}

void CMixerBar::OnCaptureChanged(CWnd *pWnd)
{
	m_iDragFrom = -1;
	CControlBar::OnCaptureChanged(pWnd);
}

BOOL CMixerBar::OnSetCursor(CWnd *pWnd, UINT nHitTest, UINT message)
{
	if (pWnd == this && nHitTest == HTCLIENT) {
		::SetCursor(::LoadCursor(NULL, IDC_SIZENS));
		return TRUE;
	}
	return CControlBar::OnSetCursor(pWnd, nHitTest, message);
}

void CMixerBar::OnDestroy()
{
	theApp.WriteProfileInt(SECTION, _T("Height"), m_iHeight);
	CControlBar::OnDestroy();
}

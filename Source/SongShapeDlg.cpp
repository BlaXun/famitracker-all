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

// Module > Song shape (see SongShapeDlg.h): the shapes in a list, the one
// picked with its parts and what each is for, and what laying it out does to
// this song; the song_shape tool lays it out, so FamiTracker and Claude Code
// lay a shape out the same way.

// Standard headers come before stdafx.h, whose DEBUG_NEW macro would
// otherwise rewrite the placement new inside the standard library.
#include <algorithm>
#include <string>
#include <vector>
#include "stdafx.h"
#include "Api/Music.h"
#include "Api/SongShapes.h"
#include "FamiTracker.h"
#include "FamiTrackerDoc.h"
#include "MainFrm.h"
#include "McpConnection.h"
#include "SongShapeDlg.h"

namespace {

// Any note in the track's frames (releases and halts aside), as the tools count a track with notes
bool TrackHasNotes(const CFamiTrackerDoc *pDoc, unsigned int Track)
{
	const unsigned int Frames = pDoc->GetFrameCount(Track), Rows = pDoc->GetPatternLength(Track);
	const unsigned int Channels = pDoc->GetAvailableChannels();
	for (unsigned int f = 0; f < Frames; ++f)
		for (unsigned int c = 0; c < Channels; ++c)
			for (unsigned int r = 0; r < Rows; ++r) {
				stChanNote Note;
				pDoc->GetNoteData(Track, f, c, r, &Note);
				if (Note.Note >= C && Note.Note <= B)
					return true;
			}
	return false;
}

ftapi::Timing TrackTiming(const CFamiTrackerDoc *pDoc, unsigned int Track)
{
	return ftapi::Timing(static_cast<int>(pDoc->GetPatternLength(Track)), static_cast<int>(pDoc->GetFrameCount(Track)),
		pDoc->GetFirstHighlight(), pDoc->GetSecondHighlight());
}

double TrackBpm(const CFamiTrackerDoc *pDoc, unsigned int Track, int RowsPerBeat)
{
	int Tempo = static_cast<int>(pDoc->GetSongTempo(Track));
	if (Tempo == 0)
		Tempo = ftapi::DefaultTempo(static_cast<int>(pDoc->GetFrameRate()));
	return ftapi::BeatsPerMinute(static_cast<int>(pDoc->GetSongSpeed(Track)), Tempo, static_cast<int>(pDoc->GetFrameRate()),
		std::max(1, RowsPerBeat));
}

CString Bars(int Count)
{
	CString Text;
	Text.Format(Count == 1 ? _T("%d bar") : _T("%d bars"), Count);
	return Text;
}

}	// namespace

// The dialog

class CSongShapeDlg : public CDialog
{
public:
	CSongShapeDlg(CMainFrame *pMainFrm);

	enum { IDD = IDD_SONG_SHAPE };

	const ftapi::SongShape *GetShape() const { return m_pShape; }
	bool AddBars() const { return m_bAddBars; }

protected:
	virtual BOOL OnInitDialog();
	virtual void OnOK();
	afx_msg void OnShapeChanged();
	afx_msg void OnShapeDoubleClicked();

	DECLARE_MESSAGE_MAP()

private:
	bool Plan(const ftapi::SongShape &Shape, bool AddFrames, ftapi::ShapeLayout &Out) const;
	void ShowShape();

	CMainFrame *m_pMainFrame;
	CFamiTrackerDoc *m_pDocument;
	unsigned int m_iTrack;
	bool m_bNotes;
	const ftapi::SongShape *m_pShape;
	bool m_bAddBars;
	CListBox m_cList;
};

BEGIN_MESSAGE_MAP(CSongShapeDlg, CDialog)
	ON_LBN_SELCHANGE(IDC_SHAPE_LIST, OnShapeChanged)
	ON_LBN_DBLCLK(IDC_SHAPE_LIST, OnShapeDoubleClicked)
END_MESSAGE_MAP()

CSongShapeDlg::CSongShapeDlg(CMainFrame *pMainFrm) : CDialog(IDD_SONG_SHAPE, pMainFrm), m_pMainFrame(pMainFrm),
	m_pDocument(CFamiTrackerDoc::GetDoc()), m_iTrack(static_cast<unsigned int>(pMainFrm->GetSelectedTrack())), m_bNotes(false),
	m_pShape(NULL), m_bAddBars(false)
{
}

bool CSongShapeDlg::Plan(const ftapi::SongShape &Shape, bool AddFrames, ftapi::ShapeLayout &Out) const
{
	const ftapi::Timing Time = TrackTiming(m_pDocument, m_iTrack);
	std::string Error;
	return ftapi::LayOutShape(Shape, !m_bNotes, Time.RowsPerBar, Time.PatternLength, Time.Frames, MAX_FRAMES, AddFrames, Out, Error);
}

BOOL CSongShapeDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	m_cList.SubclassDlgItem(IDC_SHAPE_LIST, this);
	m_bNotes = TrackHasNotes(m_pDocument, m_iTrack);

	// The shapes, the one the track's sections are (or its name says) picked; on a song with notes, those that fit in the
	// bars it has say so, and the longest of them is picked when nothing else says which
	const std::vector<ftapi::SongShape> &All = ftapi::SongShapes();
	const ftapi::Timing Time = TrackTiming(m_pDocument, m_iTrack);
	const ftapi::SongShape *pGuess = ftapi::GuessSongShape(m_pDocument->GetSections()->Get(static_cast<int>(m_iTrack)),
		std::string(static_cast<LPCTSTR>(m_pDocument->GetTrackTitle(m_iTrack))), TrackBpm(m_pDocument, m_iTrack, Time.RowsPerBeat));
	int Picked = 0, Fitting = -1, FittingBars = 0;
	for (size_t i = 0; i < All.size(); ++i) {
		ftapi::ShapeLayout Laid;
		const bool Fits = m_bNotes && Plan(All[i], true, Laid) && Laid.NeededFrames <= Time.Frames;
		CString Item;
		Item.Format(_T("%s (%s%s)"), All[i].Title.c_str(), static_cast<LPCTSTR>(Bars(All[i].Bars())), Fits ? _T("; fits") : _T(""));
		m_cList.AddString(Item);
		if (pGuess == &All[i])
			Picked = static_cast<int>(i);
		if (Fits && Laid.NeededFrames * Laid.BarsPerFrame > FittingBars) {
			Fitting = static_cast<int>(i);
			FittingBars = Laid.NeededFrames * Laid.BarsPerFrame;
		}
	}
	if (pGuess == NULL && Fitting >= 0)
		Picked = Fitting;
	m_cList.SetCurSel(Picked);
	ShowShape();
	m_cList.SetFocus();
	return FALSE;
}

// The shape picked: its parts and what each is for, and what laying it out does to this song
void CSongShapeDlg::ShowShape()
{
	const std::vector<ftapi::SongShape> &All = ftapi::SongShapes();
	const int Index = m_cList.GetCurSel();
	if (Index < 0 || Index >= static_cast<int>(All.size()))
		return;
	const ftapi::SongShape &Shape = All[static_cast<size_t>(Index)];
	CString Parts(Shape.About.c_str());
	Parts += _T("\r\n");
	for (size_t p = 0; p < Shape.Parts.size(); ++p) {
		CString Line;
		Line.Format(_T("\r\n%s (%s): %s"), Shape.Parts[p].Name.c_str(), static_cast<LPCTSTR>(Bars(Shape.Parts[p].Bars)),
			Shape.Parts[p].Line.c_str());
		Parts += Line;
	}
	SetDlgItemText(IDC_SHAPE_PARTS, Parts);

	ftapi::ShapeLayout Laid;
	CString Note;
	const ftapi::Timing Time = TrackTiming(m_pDocument, m_iTrack);
	if (!Plan(Shape, true, Laid))
		Note = _T("This shape doesn't fit in a track's frames here.");
	else if (!m_bNotes) {
		Note.Format(_T("The song has no notes yet: it gets %d frames of %s (%d rows), each part a named section%s. Fill the parts ")
			_T("next; Ctrl+Z takes it back."), Laid.Frames, static_cast<LPCTSTR>(Bars(Laid.BarsPerFrame)), Laid.RowsPerFrame,
			Shape.Loops ? _T(", and the loop after the intro") : _T(", and a stop at its end"));
	}
	else {
		const int Have = Time.BarCount(), Need = Laid.NeededFrames * Laid.BarsPerFrame;
		if (Need > Have)
			Note.Format(_T("Every note of the song stays. It has %s and this shape takes %s: you'll be asked whether to add the %d ")
				_T("empty bars it needs at the end."), static_cast<LPCTSTR>(Bars(Have)), static_cast<LPCTSTR>(Bars(Need)), Need - Have);
		else if (Need < Have)
			Note.Format(_T("Every note of the song stays: its parts are named over its first %s, and the %d bars after them keep no ")
				_T("section."), static_cast<LPCTSTR>(Bars(Need)), Have - Need);
		else
			Note.Format(_T("Every note of the song stays: its parts are named over the %s it has."), static_cast<LPCTSTR>(Bars(Have)));
		if (!Laid.Refitted.empty()) {
			CString Frame;
			Frame.Format(_T(" A frame here is %s, so each part is whole frames."), static_cast<LPCTSTR>(Bars(Laid.BarsPerFrame)));
			Note += Frame;
		}
	}
	const ftapi::SectionList &Own = m_pDocument->GetSections()->Get(static_cast<int>(m_iTrack));
	if (ftapi::MatchSongShape(Own, std::string(), 0.0, &Shape) == &Shape)
		Note = _T("The song is laid out in this shape already.");
	else if (!Own.empty())
		Note += _T(" The song's own section names give way to the shape's.");
	SetDlgItemText(IDC_SHAPE_NOTE, Note);
}

void CSongShapeDlg::OnShapeChanged()
{
	ShowShape();
}

void CSongShapeDlg::OnShapeDoubleClicked()
{
	OnOK();
}

void CSongShapeDlg::OnOK()
{
	const std::vector<ftapi::SongShape> &All = ftapi::SongShapes();
	const int Index = m_cList.GetCurSel();
	if (Index < 0 || Index >= static_cast<int>(All.size()))
		return;
	m_pShape = &All[static_cast<size_t>(Index)];
	m_bAddBars = false;
	// A song with notes, shorter than the shape: the empty bars it needs go at the end only when the user says so
	ftapi::ShapeLayout Laid;
	if (m_bNotes && Plan(*m_pShape, true, Laid) && Laid.FrameChange > 0) {
		const int Have = TrackTiming(m_pDocument, m_iTrack).BarCount(), Need = Laid.NeededFrames * Laid.BarsPerFrame;
		CString Question;
		Question.Format(_T("The song has %s and the %s takes %s.\n\nAdd the %d empty bars it needs at the end of the song? Every note ")
			_T("stays where it is.\n\nYes: add them.\nNo: name the parts over the %s the song has.\nCancel: change nothing."),
			static_cast<LPCTSTR>(Bars(Have)), CString(m_pShape->Title.c_str()).MakeLower().GetString(), static_cast<LPCTSTR>(Bars(Need)),
			Need - Have, static_cast<LPCTSTR>(Bars(Have)));
		const int Answer = AfxMessageBox(Question, MB_YESNOCANCEL | MB_ICONQUESTION);
		if (Answer == IDCANCEL)
			return;
		m_bAddBars = Answer == IDYES;
	}
	CDialog::OnOK();
}

// Module > Song shape

void ShowSongShapes(CMainFrame *pMainFrm)
{
	CFamiTrackerDoc *pDoc = CFamiTrackerDoc::GetDoc();
	CMcpConnection *pTools = pMainFrm != NULL ? pMainFrm->GetMcpConnection() : NULL;
	if (pDoc == NULL || pTools == NULL)
		return;
	CSongShapeDlg Dialog(pMainFrm);
	if (Dialog.DoModal() != IDOK || Dialog.GetShape() == NULL)
		return;
	const ftapi::SongShape &Shape = *Dialog.GetShape();
	CString Arguments;
	Arguments.Format(_T("{\"shape\": \"%s\", \"add_bars\": %s}"), Shape.Id.c_str(), Dialog.AddBars() ? _T("true") : _T("false"));
	std::string Reply;
	if (!pTools->RunTool("song_shape", std::string(static_cast<LPCSTR>(CStringA(Arguments))), Reply)) {
		AfxMessageBox(CString(Reply.c_str()), MB_ICONINFORMATION);
		return;
	}
	pMainFrm->SongChanged();
	pDoc->UpdateAllViews(NULL, UPDATE_FRAME);
	CString Done;
	Done.Format(_T("Laid out a %s. Point at a section in the frame editor to see what it's for; Ctrl+Z takes it back."),
		CString(Shape.Title.c_str()).MakeLower().GetString());
	pMainFrm->SetMessageText(Done);
}

CString SectionHoverLine(const CFamiTrackerDoc *pDoc, unsigned int Track, int Section)
{
	if (pDoc == NULL)
		return CString();
	const ftapi::SectionList &List = pDoc->GetSections()->Get(static_cast<int>(Track));
	if (Section < 0 || Section >= static_cast<int>(List.size()))
		return CString();
	const ftapi::Timing Time = TrackTiming(pDoc, Track);
	const ftapi::Section &Part = List[static_cast<size_t>(Section)];
	const int Bpf = Time.BarsPerFrame();
	const int First = Part.First * Bpf + 1, Last = std::min(Time.BarCount(), (Part.Last + 1) * Bpf);
	const ftapi::SongShape *pShape = ftapi::GuessSongShape(List, std::string(static_cast<LPCTSTR>(pDoc->GetTrackTitle(Track))),
		TrackBpm(pDoc, Track, Time.RowsPerBeat));
	return CString(ftapi::SectionHoverText(Part.Name, First, Last, pShape).c_str());
}

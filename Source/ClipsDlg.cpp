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

// The clip editor and the clip name dialog (see Clips.h); the clips are in the clips panel (ClipsPanel.h).

#include "stdafx.h"
#include "FamiTracker.h"
#include "FamiTrackerDoc.h"
#include "FamiTrackerView.h"
#include "MainFrm.h"
#include "Settings.h"
#include "Clips.h"
#include "InsertRecording.h"
#include "ClipsDlg.h"

namespace {

const TCHAR *const NOTE_NAMES[] = {
	_T("C-"), _T("C#"), _T("D-"), _T("D#"), _T("E-"), _T("F-"), _T("F#"), _T("G-"), _T("G#"), _T("A-"), _T("A#"), _T("B-")
};

bool IsNote(const stChanNote &Cell)
{
	return Cell.Note >= C && Cell.Note <= B;
}

bool IsBlank(const stChanNote &Cell)
{
	if (Cell.Note != NONE || Cell.Vol != MAX_VOLUME || Cell.Instrument != MAX_INSTRUMENTS)
		return false;
	for (int i = 0; i < MAX_EFFECT_COLUMNS; ++i)
		if (Cell.EffNumber[i] != EF_NONE)
			return false;
	return true;
}

// "1 note", "3 notes"
CString Count(int Number, LPCTSTR One, LPCTSTR Many)
{
	CString Text;
	Text.Format(_T("%d %s"), Number, Number == 1 ? One : Many);
	return Text;
}

}	// namespace

// The name dialog

IMPLEMENT_DYNAMIC(CClipNameDlg, CDialog)

CClipNameDlg::CClipNameDlg(LPCTSTR Title, LPCTSTR Prompt, LPCTSTR Name, CWnd *pParent) :
	CDialog(CClipNameDlg::IDD, pParent),
	m_sTitle(Title),
	m_sPrompt(Prompt),
	m_sName(Name)
{
}

BOOL CClipNameDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	SetWindowText(m_sTitle);
	SetDlgItemText(IDC_CLIPNAME_PROMPT, m_sPrompt);
	CEdit *pEdit = static_cast<CEdit*>(GetDlgItem(IDC_CLIPNAME_EDIT));
	pEdit->LimitText(255);
	pEdit->SetWindowText(m_sName);
	pEdit->SetSel(0, -1);
	pEdit->SetFocus();
	return FALSE;	// the focus is on the name
}

void CClipNameDlg::OnOK()
{
	CString Name;
	GetDlgItemText(IDC_CLIPNAME_EDIT, Name);
	Name.Trim();
	if (Name.IsEmpty()) {
		MessageBeep(MB_ICONWARNING);
		GetDlgItem(IDC_CLIPNAME_EDIT)->SetFocus();
		return;
	}
	m_sName = Name;
	CDialog::OnOK();
}

CString CClipNameDlg::GetName() const
{
	return m_sName;
}

// The clip editor

IMPLEMENT_DYNAMIC(CClipEditDlg, CDialog)

CClipEditDlg::CClipEditDlg(CFamiTrackerDoc *pDoc, int Index, CWnd *pParent) :
	CDialog(CClipEditDlg::IDD, pParent),
	m_pDocument(pDoc),
	m_iIndex(Index),
	m_iColumns(pDoc->GetClips()->GetColumns(Index)),
	m_iRows(pDoc->GetClips()->GetRows(Index)),
	m_pCells(NULL),
	m_pItems(NULL),
	m_iItems(0),
	m_bChanged(false)
{
	const CClipLibrary *pClips = pDoc->GetClips();
	const int Cells = m_iColumns * m_iRows;
	m_pCells = new stChanNote[Cells > 0 ? Cells : 1];
	m_pItems = new int[Cells > 0 ? Cells : 1];
	for (int c = 0; c < m_iColumns; ++c)
		for (int r = 0; r < m_iRows; ++r)
			pClips->GetCell(Index, c, r, m_pCells[c * m_iRows + r]);
}

CClipEditDlg::~CClipEditDlg()
{
	delete [] m_pCells;
	delete [] m_pItems;
}

BEGIN_MESSAGE_MAP(CClipEditDlg, CDialog)
	ON_BN_CLICKED(IDC_CLIPED_SETSELECTED, &CClipEditDlg::OnSetSelected)
	ON_BN_CLICKED(IDC_CLIPED_SETALL, &CClipEditDlg::OnSetAll)
	ON_BN_CLICKED(IDC_CLIPED_DOWN12, &CClipEditDlg::OnOctaveDown)
	ON_BN_CLICKED(IDC_CLIPED_DOWN1, &CClipEditDlg::OnSemitoneDown)
	ON_BN_CLICKED(IDC_CLIPED_UP1, &CClipEditDlg::OnSemitoneUp)
	ON_BN_CLICKED(IDC_CLIPED_UP12, &CClipEditDlg::OnOctaveUp)
END_MESSAGE_MAP()

BOOL CClipEditDlg::OnInitDialog()
{
	CDialog::OnInitDialog();

	CString Title;
	Title.Format(_T("Edit clip \"%s\""), static_cast<LPCTSTR>(m_pDocument->GetClips()->GetName(m_iIndex)));
	SetWindowText(Title);
	Report(NULL);

	m_cNotes.SubclassDlgItem(IDC_CLIPED_NOTES, this);
	m_cNotes.SetExtendedStyle(LVS_EX_FULLROWSELECT);
	CRect Rect;
	m_cNotes.GetClientRect(&Rect);
	const int Width = Rect.Width() - ::GetSystemMetrics(SM_CXVSCROLL);
	static const LPCTSTR HEADINGS[] = { _T("Row"), _T("Channel"), _T("Note"), _T("Instrument"), _T("Volume"), _T("Effects") };
	static const int SHARES[] = { 9, 23, 10, 31, 10, 17 };	// percent of the width
	for (int i = 0; i < 6; ++i)
		m_cNotes.InsertColumn(i, HEADINGS[i], LVCFMT_LEFT, Width * SHARES[i] / 100);

	// The song's instruments, starting on the clip's own
	m_cInstrument.SubclassDlgItem(IDC_CLIPED_INSTRUMENT, this);
	int First = -1;
	for (int i = 0; i < m_iColumns * m_iRows && First < 0; ++i)
		if (IsNote(m_pCells[i]) && m_pCells[i].Instrument < MAX_INSTRUMENTS)
			First = m_pCells[i].Instrument;
	for (unsigned int i = 0; i < MAX_INSTRUMENTS; ++i) {
		if (!m_pDocument->IsInstrumentUsed(i))
			continue;
		char Name[256] = "";
		m_pDocument->GetInstrumentName(i, Name);
		CString Text;
		Text.Format(_T("%02X - %s"), i, Name);
		const int Item = m_cInstrument.AddString(Text);
		m_cInstrument.SetItemData(Item, static_cast<DWORD_PTR>(i));
		if (static_cast<int>(i) == First)
			m_cInstrument.SetCurSel(Item);
	}
	if (m_cInstrument.GetCurSel() == CB_ERR && m_cInstrument.GetCount() > 0)
		m_cInstrument.SetCurSel(0);
	const BOOL Instruments = m_cInstrument.GetCount() > 0 ? TRUE : FALSE;
	GetDlgItem(IDC_CLIPED_SETSELECTED)->EnableWindow(Instruments);
	GetDlgItem(IDC_CLIPED_SETALL)->EnableWindow(Instruments);

	FillNotes();
	return TRUE;
}

void CClipEditDlg::FillNotes()
{
	// Every row with something on it, in time order (rows, then channels)
	m_cNotes.DeleteAllItems();
	m_iItems = 0;
	for (int r = 0; r < m_iRows; ++r) {
		for (int c = 0; c < m_iColumns; ++c) {
			const int Cell = c * m_iRows + r;
			if (IsBlank(m_pCells[Cell]))
				continue;
			m_pItems[m_iItems] = Cell;
			m_cNotes.InsertItem(m_iItems, _T(""));
			ShowItem(m_iItems);
			++m_iItems;
		}
	}
}

CString CClipEditDlg::ColumnName(int Column) const
{
	const int ChanID = m_pDocument->GetClips()->GetSource(m_iIndex, Column);
	if (ChanID >= 0) {
		const int Channel = m_pDocument->GetChannelIndex(ChanID);
		if (Channel >= 0 && Channel < static_cast<int>(m_pDocument->GetAvailableChannels()))
			return RecordingChannelName(m_pDocument, Channel);
	}
	CString Name;
	Name.Format(_T("Channel %d"), Column + 1);
	return Name;
}

bool CClipEditDlg::IsNoise(int Column) const
{
	return m_pDocument->GetClips()->GetSource(m_iIndex, Column) == CHANID_NOISE;
}

void CClipEditDlg::ShowItem(int Item)
{
	const int Index = m_pItems[Item];
	const int Column = Index / m_iRows, Row = Index % m_iRows;
	const stChanNote &Cell = m_pCells[Index];

	CString Text;
	Text.Format(theApp.GetSettings()->General.bRowInHex ? _T("%02X") : _T("%03d"), Row);
	m_cNotes.SetItemText(Item, 0, Text);
	m_cNotes.SetItemText(Item, 1, ColumnName(Column));

	Text.Empty();
	if (IsNote(Cell)) {
		if (IsNoise(Column))
			Text.Format(_T("%X-#"), (Cell.Note - 1 + Cell.Octave * 12) & 0x0F);
		else
			Text.Format(_T("%s%d"), NOTE_NAMES[Cell.Note - 1], Cell.Octave);
	}
	else if (Cell.Note == RELEASE)
		Text = _T("===");
	else if (Cell.Note == HALT)
		Text = _T("---");
	m_cNotes.SetItemText(Item, 2, Text);

	Text.Empty();
	if (Cell.Instrument < MAX_INSTRUMENTS) {
		char Name[256] = "(none)";
		if (m_pDocument->IsInstrumentUsed(Cell.Instrument))
			m_pDocument->GetInstrumentName(Cell.Instrument, Name);
		Text.Format(_T("%02X - %s"), Cell.Instrument, Name);
	}
	m_cNotes.SetItemText(Item, 3, Text);

	Text.Empty();
	if (Cell.Vol < MAX_VOLUME)
		Text.Format(_T("%X"), Cell.Vol);
	m_cNotes.SetItemText(Item, 4, Text);

	Text.Empty();
	for (int e = 0; e < MAX_EFFECT_COLUMNS; ++e) {
		if (Cell.EffNumber[e] == EF_NONE || Cell.EffNumber[e] >= EF_COUNT)
			continue;
		const char Letter = EFF_CHAR[Cell.EffNumber[e] - 1];
		CString Effect;
		Effect.Format(_T("%c%02X"), Letter != 0 ? Letter : '?', Cell.EffParam[e]);
		if (!Text.IsEmpty())
			Text += _T(" ");
		Text += Effect;
	}
	m_cNotes.SetItemText(Item, 5, Text);
}

void CClipEditDlg::Report(LPCTSTR Text)
{
	// What just happened, then what OK will do to the song
	const int Links = m_pDocument->GetClips()->GetLinkCount(m_iIndex);
	CString Info;
	if (Text != NULL) {
		Info = Text;
		Info += _T("\n");
	}
	if (Links > 0)
		Info += Count(Links, _T("linked copy"), _T("linked copies")) + _T(" in the song change along when you press OK.");
	else
		Info += _T("The clip isn't linked anywhere in the song, so OK changes only the clip.");
	SetDlgItemText(IDC_CLIPED_INFO, Info);
}

void CClipEditDlg::SetInstrument(bool All)
{
	const int Picked = m_cInstrument.GetCurSel();
	if (Picked == CB_ERR)
		return;
	const unsigned char Instrument = static_cast<unsigned char>(m_cInstrument.GetItemData(Picked));
	int Notes = 0;
	for (int Item = 0; Item < m_iItems; ++Item) {
		if (!All && m_cNotes.GetItemState(Item, LVIS_SELECTED) == 0)
			continue;
		stChanNote &Cell = m_pCells[m_pItems[Item]];
		if (!IsNote(Cell))
			continue;	// releases and effects have no instrument
		++Notes;
		if (Cell.Instrument != Instrument) {
			Cell.Instrument = Instrument;
			m_bChanged = true;
			ShowItem(Item);
		}
	}
	if (Notes == 0) {
		Report(All ? _T("The clip has no notes.") :
			_T("Pick the notes in the list first (Shift+click or Ctrl+click for several), or use For all notes."));
		return;
	}
	CString Text;
	Text.Format(_T("%s now play%s instrument %02X."), static_cast<LPCTSTR>(Count(Notes, _T("note"), _T("notes"))),
		Notes == 1 ? _T("s") : _T(""), Instrument);
	Report(Text);
}

void CClipEditDlg::Transpose(int Semitones)
{
	// The selected notes, or all of them when none are selected
	bool Selection = false;
	for (int Item = 0; Item < m_iItems && !Selection; ++Item)
		Selection = m_cNotes.GetItemState(Item, LVIS_SELECTED) != 0;

	int Moved = 0, Stuck = 0;
	for (int Item = 0; Item < m_iItems; ++Item) {
		if (Selection && m_cNotes.GetItemState(Item, LVIS_SELECTED) == 0)
			continue;
		stChanNote &Cell = m_pCells[m_pItems[Item]];
		if (!IsNote(Cell))
			continue;
		const int Value = Cell.Octave * 12 + (Cell.Note - 1) + Semitones;
		if (Value < 0 || Value >= OCTAVE_RANGE * 12) {
			++Stuck;
			continue;
		}
		Cell.Octave = static_cast<unsigned char>(Value / 12);
		Cell.Note = static_cast<unsigned char>(Value % 12 + 1);
		++Moved;
		ShowItem(Item);
	}
	if (Moved > 0)
		m_bChanged = true;

	LPCTSTR Where = Semitones == 12 ? _T("up an octave") : (Semitones == -12 ? _T("down an octave") :
		(Semitones > 0 ? _T("up a semitone") : _T("down a semitone")));
	CString Text;
	Text.Format(_T("Moved %s %s."), static_cast<LPCTSTR>(Count(Moved, _T("note"), _T("notes"))), Where);
	if (Stuck > 0) {
		CString More;
		More.Format(_T(" %s at the edge of the range stayed where %s."), static_cast<LPCTSTR>(Count(Stuck, _T("note"), _T("notes"))),
			Stuck == 1 ? _T("it was") : _T("they were"));
		Text += More;
	}
	Report(Text);
}

void CClipEditDlg::OnSetSelected()
{
	SetInstrument(false);
}

void CClipEditDlg::OnSetAll()
{
	SetInstrument(true);
}

void CClipEditDlg::OnOctaveDown()
{
	Transpose(-12);
}

void CClipEditDlg::OnSemitoneDown()
{
	Transpose(-1);
}

void CClipEditDlg::OnSemitoneUp()
{
	Transpose(1);
}

void CClipEditDlg::OnOctaveUp()
{
	Transpose(12);
}

const stChanNote *CClipEditDlg::GetCells() const
{
	return m_pCells;
}

int CClipEditDlg::GetCellCount() const
{
	return m_iColumns * m_iRows;
}

bool CClipEditDlg::IsChanged() const
{
	return m_bChanged;
}

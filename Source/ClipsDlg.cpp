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

// Edit > Clips, the clip editor and the clip name dialog (see Clips.h).

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

const TCHAR HOW_CLIPS_WORK[] = _T("A copy's notes are its own. Linked copies change together: edit one of them in the song, ")
	_T("or the clip with Edit..., and all of them follow. A clip goes in at the cursor, on its channel and the ones to the right.");
const TCHAR NO_CLIPS[] = _T("No clips yet. Select rows in the pattern editor and use Edit > Save selection as clip, or tick ")
	_T("\"Save as a clip\" when you record.");

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

// Edit > Clips

IMPLEMENT_DYNAMIC(CClipsDlg, CDialog)

CClipsDlg::CClipsDlg(CFamiTrackerView *pView) :
	CDialog(CClipsDlg::IDD, pView),
	m_pView(pView),
	m_pDocument(pView->GetDocument())
{
}

BEGIN_MESSAGE_MAP(CClipsDlg, CDialog)
	ON_BN_CLICKED(IDC_CLIP_PLACE, &CClipsDlg::OnPlace)
	ON_BN_CLICKED(IDC_CLIP_PLACELINKED, &CClipsDlg::OnPlaceLinked)
	ON_BN_CLICKED(IDC_CLIP_EDIT, &CClipsDlg::OnEditClip)
	ON_BN_CLICKED(IDC_CLIP_DUPLICATE, &CClipsDlg::OnDuplicate)
	ON_BN_CLICKED(IDC_CLIP_RENAME, &CClipsDlg::OnRename)
	ON_BN_CLICKED(IDC_CLIP_UNLINK, &CClipsDlg::OnUnlink)
	ON_BN_CLICKED(IDC_CLIP_DELETE, &CClipsDlg::OnDelete)
	ON_NOTIFY(LVN_ITEMCHANGED, IDC_CLIP_LIST, &CClipsDlg::OnItemChanged)
	ON_NOTIFY(NM_DBLCLK, IDC_CLIP_LIST, &CClipsDlg::OnDoubleClick)
END_MESSAGE_MAP()

BOOL CClipsDlg::OnInitDialog()
{
	CDialog::OnInitDialog();

	m_cList.SubclassDlgItem(IDC_CLIP_LIST, this);
	m_cList.SetExtendedStyle(LVS_EX_FULLROWSELECT);
	CRect Rect;
	m_cList.GetClientRect(&Rect);
	const int Width = Rect.Width() - ::GetSystemMetrics(SM_CXVSCROLL);
	m_cList.InsertColumn(0, _T("Clip"), LVCFMT_LEFT, Width * 46 / 100);
	m_cList.InsertColumn(1, _T("Size"), LVCFMT_LEFT, Width * 36 / 100);
	m_cList.InsertColumn(2, _T("Linked"), LVCFMT_LEFT, Width - Width * 46 / 100 - Width * 36 / 100);

	FillList(m_pDocument->GetClips()->GetCount() - 1);	// the newest
	SetInfo(NULL);
	return TRUE;
}

int CClipsDlg::Selected() const
{
	POSITION Position = m_cList.GetFirstSelectedItemPosition();
	return Position != NULL ? m_cList.GetNextSelectedItem(Position) : -1;
}

void CClipsDlg::FillList(int Select)
{
	const CClipLibrary *pClips = m_pDocument->GetClips();
	m_cList.DeleteAllItems();
	for (int i = 0; i < pClips->GetCount(); ++i) {
		m_cList.InsertItem(i, pClips->GetName(i));
		CString Size;
		if (pClips->GetColumns(i) == 1)
			Size = Count(pClips->GetRows(i), _T("row"), _T("rows"));
		else
			Size.Format(_T("%d channels, %s"), pClips->GetColumns(i), static_cast<LPCTSTR>(Count(pClips->GetRows(i), _T("row"), _T("rows"))));
		m_cList.SetItemText(i, 1, Size);
		const int Links = pClips->GetLinkCount(i);
		CString Linked(_T("-"));
		if (Links > 0)
			Linked = Count(Links, _T("copy"), _T("copies"));
		m_cList.SetItemText(i, 2, Linked);
	}
	if (Select >= pClips->GetCount())
		Select = pClips->GetCount() - 1;
	if (Select >= 0) {
		m_cList.SetItemState(Select, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		m_cList.EnsureVisible(Select, FALSE);
	}
	UpdateButtons();
}

void CClipsDlg::UpdateButtons()
{
	static const int BUTTONS[] = {
		IDC_CLIP_PLACE, IDC_CLIP_PLACELINKED, IDC_CLIP_EDIT, IDC_CLIP_DUPLICATE, IDC_CLIP_RENAME, IDC_CLIP_DELETE
	};
	const int Index = Selected();
	for (size_t i = 0; i < sizeof(BUTTONS) / sizeof(BUTTONS[0]); ++i)
		GetDlgItem(BUTTONS[i])->EnableWindow(Index >= 0 ? TRUE : FALSE);
	GetDlgItem(IDC_CLIP_UNLINK)->EnableWindow(Index >= 0 && m_pDocument->GetClips()->GetLinkCount(Index) > 0 ? TRUE : FALSE);
}

void CClipsDlg::SetInfo(LPCTSTR Note)
{
	if (Note != NULL)
		SetDlgItemText(IDC_CLIP_INFO, Note);
	else
		SetDlgItemText(IDC_CLIP_INFO, m_pDocument->GetClips()->IsEmpty() ? NO_CLIPS : HOW_CLIPS_WORK);
}

void CClipsDlg::Place(bool Linked)
{
	const int Index = Selected();
	if (Index < 0)
		return;
	CString Message;
	if (!PlaceClip(static_cast<CMainFrame*>(m_pView->GetParentFrame()), Index, Linked, Message)) {
		AfxMessageBox(Message, MB_OK | MB_ICONINFORMATION);
		return;
	}
	m_sResult = Message;
	EndDialog(IDOK);
}

void CClipsDlg::OnPlace()
{
	Place(false);
}

void CClipsDlg::OnPlaceLinked()
{
	Place(true);
}

void CClipsDlg::OnEditClip()
{
	const int Index = Selected();
	if (Index < 0)
		return;
	CClipEditDlg Dialog(m_pDocument, Index, this);
	if (Dialog.DoModal() != IDOK || !Dialog.IsChanged())
		return;
	CClipLibrary *pClips = m_pDocument->GetClips();
	const CString Name = pClips->GetName(Index);
	const int Links = pClips->GetLinkCount(Index);
	ApplyClipEdit(static_cast<CMainFrame*>(m_pView->GetParentFrame()), Index, Dialog.GetCells(), Dialog.GetCellCount());
	FillList(Index);

	CString Note;
	if (Links > 0)
		Note.Format(_T("Changed \"%s\" and its %s in the song. Ctrl+Z in the song undoes it."), static_cast<LPCTSTR>(Name),
			static_cast<LPCTSTR>(Count(Links, _T("linked copy"), _T("linked copies"))));
	else
		Note.Format(_T("Changed \"%s\". It isn't linked anywhere in the song, so the song stays as it is."), static_cast<LPCTSTR>(Name));
	SetInfo(Note);
}

void CClipsDlg::OnDuplicate()
{
	const int Index = Selected();
	if (Index < 0)
		return;
	CClipLibrary *pClips = m_pDocument->GetClips();
	CClipNameDlg Dialog(_T("Duplicate clip"), _T("Name of the copy:"), pClips->NewName(pClips->GetName(Index) + _T(" copy"), false), this);
	if (Dialog.DoModal() != IDOK)
		return;
	const int Copy = pClips->Duplicate(Index, Dialog.GetName());
	if (Copy < 0) {
		AfxMessageBox(_T("There are too many clips already. Delete some first."), MB_OK | MB_ICONINFORMATION);
		return;
	}
	m_pDocument->SetModifiedFlag();
	FillList(Copy);
	SetInfo(_T("The copy is a clip of its own: changing it leaves the original and its linked copies alone."));
}

void CClipsDlg::OnRename()
{
	const int Index = Selected();
	if (Index < 0)
		return;
	CClipLibrary *pClips = m_pDocument->GetClips();
	CClipNameDlg Dialog(_T("Rename clip"), _T("New name:"), pClips->GetName(Index), this);
	if (Dialog.DoModal() != IDOK)
		return;
	pClips->SetName(Index, Dialog.GetName());
	m_pDocument->SetModifiedFlag();
	FillList(Index);
}

void CClipsDlg::OnUnlink()
{
	const int Index = Selected();
	if (Index < 0)
		return;
	CClipLibrary *pClips = m_pDocument->GetClips();
	const int Links = pClips->GetLinkCount(Index);
	pClips->Unlink(Index);
	m_pDocument->SetModifiedFlag();
	FillList(Index);
	CString Note;
	Note.Format(Links == 1 ? _T("The linked copy of \"%s\" is plain notes now: it doesn't change along any more.") :
		_T("The linked copies of \"%s\" are plain notes now: they don't change along any more."),
		static_cast<LPCTSTR>(pClips->GetName(Index)));
	SetInfo(Note);
}

void CClipsDlg::OnDelete()
{
	const int Index = Selected();
	if (Index < 0)
		return;
	CClipLibrary *pClips = m_pDocument->GetClips();
	CString Question;
	Question.Format(_T("Delete the clip \"%s\"?"), static_cast<LPCTSTR>(pClips->GetName(Index)));
	if (pClips->GetLinkCount(Index) > 0)
		Question += _T(" Its linked copies stay in the song as plain notes.");
	if (AfxMessageBox(Question, MB_YESNO | MB_ICONQUESTION) != IDYES)
		return;
	pClips->Remove(Index);
	m_pDocument->SetModifiedFlag();
	FillList(Index);
	SetInfo(NULL);
}

void CClipsDlg::OnItemChanged(NMHDR *pNMHDR, LRESULT *pResult)
{
	UpdateButtons();
	*pResult = 0;
}

void CClipsDlg::OnDoubleClick(NMHDR *pNMHDR, LRESULT *pResult)
{
	if (Selected() >= 0)
		Place(false);
	*pResult = 0;
}

CString CClipsDlg::GetResult() const
{
	return m_sResult;
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

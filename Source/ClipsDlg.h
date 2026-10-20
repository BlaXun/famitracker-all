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

class CFamiTrackerView;
class CFamiTrackerDoc;
struct stChanNote;

// Asks for a clip's name: a new clip, a copy, or a new name for one
class CClipNameDlg : public CDialog
{
	DECLARE_DYNAMIC(CClipNameDlg)

public:
	CClipNameDlg(LPCTSTR Title, LPCTSTR Prompt, LPCTSTR Name, CWnd *pParent = NULL);

	enum { IDD = IDD_CLIP_NAME };

	CString GetName() const;

protected:
	virtual BOOL OnInitDialog();
	virtual void OnOK();

private:
	CString m_sTitle;
	CString m_sPrompt;
	CString m_sName;
};

// The clip editor: the clip's notes in a list. The selected notes, or all of
// them, can get another instrument, and move up or down by semitones or
// octaves. The clip changes on OK, and its linked copies in the song with it.
class CClipEditDlg : public CDialog
{
	DECLARE_DYNAMIC(CClipEditDlg)

public:
	CClipEditDlg(CFamiTrackerDoc *pDoc, int Index, CWnd *pParent = NULL);
	virtual ~CClipEditDlg();

	enum { IDD = IDD_CLIP_EDIT };

	// The edited cells: all of the clip's, a column at a time
	const stChanNote *GetCells() const;
	int GetCellCount() const;
	bool IsChanged() const;

protected:
	virtual BOOL OnInitDialog();
	afx_msg void OnSetSelected();
	afx_msg void OnSetAll();
	afx_msg void OnOctaveDown();
	afx_msg void OnSemitoneDown();
	afx_msg void OnSemitoneUp();
	afx_msg void OnOctaveUp();

	DECLARE_MESSAGE_MAP()

private:
	void FillNotes();
	void ShowItem(int Item);
	void SetInstrument(bool All);
	void Transpose(int Semitones);
	void Report(LPCTSTR Text);
	CString ColumnName(int Column) const;
	bool IsNoise(int Column) const;

	CFamiTrackerDoc *m_pDocument;
	int m_iIndex;
	int m_iColumns;
	int m_iRows;
	stChanNote *m_pCells;	// m_iColumns * m_iRows, a column at a time
	int *m_pItems;			// the cell each list item shows
	int m_iItems;
	bool m_bChanged;
	CListCtrl m_cNotes;
	CComboBox m_cInstrument;

	CClipEditDlg(const CClipEditDlg &);
	CClipEditDlg &operator=(const CClipEditDlg &);
};

/*
** FamiTracker - NES/Famicom sound tracker
** Copyright (C) 2005-2014  Jonathan Liss
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


// CMainFrame, the main window class

#include "InstrumentEditDlg.h"
#include "PerformanceDlg.h"
#include "DialogReBar.h"
#include "ControlPanelDlg.h"
#include "CustomControls.h"

enum frame_edit_pos_t { 
	FRAME_EDIT_POS_TOP, 
	FRAME_EDIT_POS_LEFT
};

enum {
	WM_USER_DISPLAY_MESSAGE_STRING = WM_USER,
	WM_USER_DISPLAY_MESSAGE_ID
};

class CVisualizerWnd;
class CInstrumentFileTree;
class CPianoRollBar;
class CPianoRollWnd;
class CClipsBar;
class CProblemsBar;
class CWhatNextBar;
class CMixerBar;
class CDrumGridBar;
struct note_views_t;
class CAction;
class CActionGroup;
class CActionHandler;
class CFrameEditor;
class CMcpConnection;
class CSoundPreview;

class CMainFrame : public CFrameWnd
{
protected: // create from serialization only
	CMainFrame();
	DECLARE_DYNCREATE(CMainFrame)

// Attributes
public:
	CFrameEditor *GetFrameEditor() const;

// Operations
public:
	void	SetStatusText(LPCTSTR Text,...);
	void	ChangeNoteState(int Note);
	
	// Indicators & controls
	void	SetIndicatorTime(int Min, int Sec, int MSec);
	void	SetIndicatorPos(int Frame, int Row);
	void	SetSongInfo(const char *pName, const char *pArtist, const char *pCopyright);
	void	SetupColors();
	void	DisplayOctave();

	void	UpdateTrackBox();
	void	UpdateControls();
	void	ResizeFrameWindow();

	void	SetFirstHighlightRow(int Rows);
	void	SetSecondHighlightRow(int Rows);

	void	UpdateMenus();

	// Instrument
	void	OpenInstrumentEditor();
	void	CloseInstrumentEditor();
	void	UpdateInstrumentList();
	void	SelectInstrument(int Index);
	int		GetSelectedInstrument() const;
	void	SwapInstruments(int First, int Second);

	// Track
	int		GetSelectedTrack() const;
	void	SelectTrack(unsigned int Track);

	// Undo/redo
	bool	AddAction(CAction *pAction);
	CAction *GetLastAction(int Filter) const;
	void	ResetUndo();
	// The actions added from BeginActionGroup to EndActionGroup (they can nest) are undone and redone
	// together, as one Ctrl+Z (the mixer's Write into the notes, in every track)
	void	BeginActionGroup();
	void	EndActionGroup();
	// The undo history for Claude Code's undo (McpConnection.cpp): how many changes have gone into it so far (each
	// action or group counts one; undoing takes none back), and the newest change there is to undo (NULL for none)
	long long GetActionSerial() const { return m_iActionSerial; }
	CAction *GetUndoAction() const;
	// Forgets the actions undone last (nothing for Ctrl+Y to redo): Claude Code's take-back of a call that failed
	void	ForgetRedo();
	void	SyncClips();	// after an edit: linked copies of a clip follow (Clips.h)
	void	SongChanged();	// the song's notes changed: the piano roll reads them again
	// View > Pattern editor and View > Piano roll: one of them always shows, and the piano roll fills the
	// pattern editor's place while it's hidden (NoteViews.h)
	void	ShowPatternEditor(bool Show);
	bool	IsPatternEditorShown() const { return m_bPatternShown; }
	void	ShowPianoRoll(bool Show);	// under the pattern editor (PianoRoll.h)
	CPianoRollWnd *GetPianoRollWnd() const;	// NULL while the piano roll isn't shown
	void	ShowDrumGrid(bool Show);		// along the bottom or the right (DrumGridPanel.h)
	void	ShowClipsPanel(bool Show, bool Focus = false);	// beside the pattern editor (ClipsPanel.h)
	void	ShowProblemsPanel(bool Show);	// along the bottom or the right (ProblemsPanel.h)
	void	ShowWhatNext(bool Show);		// along the bottom or the right (WhatNextPanel.h)
	void	ShowMixer(bool Show);			// along the bottom (MixerPanel.h)
	CMcpConnection *GetMcpConnection() const { return m_pMcpConnection; }	// the tools, for the piano roll's chords
	CSoundPreview *GetSoundPreview();	// what plays a sound for the sound browser and Instrument > Listen (SoundPreview.h)
	void	ApplyProfile();		// the menus and the song settings as the profile has them (Profile.h)

	// Help > Tutorials (Tutorial.h): where a part of the window is on the
	// screen ("pattern", "button" with a command, "menu" with its name, ...),
	// and the pattern editor, clips panel, piano roll, drum grid or mixer
	// shown for a step about it (or about a channel's column)
	bool	GetTutorialRect(LPCTSTR Part, LPCTSTR Name, UINT Command, CRect &Rect);
	void	ShowTutorialPart(LPCTSTR Part);
	// The instrument editor while it's open, else NULL (a walkthrough's note keeps out of its way)
	CWnd	*GetOpenInstrumentEditor() { return m_wndInstEdit.IsOpened() && m_wndInstEdit.IsWindowVisible() ? &m_wndInstEdit : NULL; }

	bool	ChangeAllPatterns() const;

// Overrides
public:

// Implementation
public:
	virtual ~CMainFrame();
#ifdef _DEBUG
	virtual void AssertValid() const;
	virtual void Dump(CDumpContext& dc) const;
#endif

private:
	bool	CreateDialogPanels();
	bool	CreateToolbars();
	bool	CreateInstrumentToolbar();
	bool	CreateVisualizerWindow();
	void	SetSpeed(int Speed);
	void	SetTempo(int Tempo);
	void	SetRowCount(int Count);
	void	SetFrameCount(int Count);

	// The profile's BPM in place of speed and tempo, and fewer menu items (Profile.h)
	void	RebuildMainMenu(bool Fewer);
	void	LayoutSongSettings(bool BPM);
	double	CurrentBPM() const;
	bool	BPMFrames() const;		// the song shows as the frames a row lasts (speed 1, BPMShowsFrames)
	void	ShowBPMLabel();			// the tempo row's label: Speed for such a song, else BPM
	void	ShowBPMText();			// the box's BPM or frames, from its start, and the label
	CString	BPMText() const;
	void	SetBPM(int Bpm);

	// Instrument list operations
	void	NewInstrument(int ChipType);
	void	ClearInstrumentList();
	void	GetInstrumentName(char *pText) const;
	void	SetInstrumentName(char *pText);

	void	UpdateMenu(CMenu *pMenu);
	void	SetFrameEditorPosition(int Position);
	void	SelectInstrumentFolder();

	bool	CheckRepeat() const;

	void	CheckAudioStatus();

	// The views shown (NoteViews.h), and the docked panels laid out in their order
	note_views_t NoteViews() const;
	void	ShowNoteViews(const note_views_t &Views);
	void	ArrangeBars();
	CPianoRollWnd *FocusedPianoRoll() const;	// the piano roll, when it has the keys
	int RollChannelBefore() const;			// Ctrl+Z and Ctrl+Y in the piano roll keep its channel...
	void RollChannelAfter(int Channel);		// ...putting the cursor back on it

private:  // control bar embedded members
	CStatusBar			m_wndStatusBar;
	CToolBar			m_wndToolBar;
	CReBar				m_wndToolBarReBar;
	CDialogReBar		m_wndOctaveBar;
	CDialogBar			m_wndControlBar;	// Parent to frame editor and settings/instrument editor
	CDialogBar			m_wndVerticalControlBar;	// Parent to large frame editor
	CControlPanelDlg	m_wndDialogBar;

	CControlPanelDlg	m_wndFrameControls;		// Contains +, - and change all

	CWnd				m_wndInstToolBarWnd;
	CToolBar			m_wndInstToolBar;
	CReBarCtrl			m_wndInstToolReBar;
	CInstrumentEditDlg	m_wndInstEdit;
	CPerformanceDlg		m_wndPerformanceDlg;

	CFrameEditor		*m_pFrameEditor;
	CInstrumentList		*m_pInstrumentList;
	CImageList			*m_pImageList;
	CVisualizerWnd		*m_pVisualizerWnd;

	CLockedEdit			*m_pLockedEditSpeed;
	CLockedEdit			*m_pLockedEditTempo;
	CLockedEdit			*m_pLockedEditLength;
	CLockedEdit			*m_pLockedEditFrames;
	CLockedEdit			*m_pLockedEditStep;

	CBannerEdit			*m_pBannerEditName;
	CBannerEdit			*m_pBannerEditArtist;
	CBannerEdit			*m_pBannerEditCopyright;

	CBitmap				m_bmToolbar;			// main toolbar
	CImageList			m_ilToolBar;

	CBitmap				m_bmInstToolbar;		// instrument toolbar
	CImageList			m_ilInstToolBar;

	CActionHandler		*m_pActionHandler;

	int					m_iFrameEditorPos;

	CMcpConnection		*m_pMcpConnection;		// File > Connect Claude Code
	CSoundPreview		*m_pSoundPreview;		// a sound heard (SoundPreview.h), made when first needed

	CInstrumentFileTree	*m_pInstrumentFileTree;

	CPianoRollBar		*m_pPianoRoll;			// View > Piano roll, under the pattern editor; created when first shown
	CClipsBar			*m_pClipsBar;			// View > Clips panel, beside the pattern editor; created when first shown
	CProblemsBar		*m_pProblemsBar;		// View > Problems, along the bottom or the right; created when first shown
	CWhatNextBar		*m_pWhatNextBar;		// View > What next, along the bottom or the right; created when first shown
	CMixerBar			*m_pMixerBar;			// View > Mixer, along the bottom; created when first shown
	CDrumGridBar		*m_pDrumGridBar;		// View > Drum grid, along the bottom or the right; created when first shown
	bool				m_bPatternShown;		// View > Pattern editor: the pattern editor shows (else the piano roll fills its place)
	CActionGroup		*m_pActionGroup;		// the actions of a group being added (BeginActionGroup)
	int					m_iActionGroupDepth;
	long long			m_iActionSerial;		// actions and groups pushed onto the undo history (GetActionSerial)

	// The profile as shown (Profile.h)
	bool				m_bFewerMenus;
	bool				m_bBPM;					// the tempo box shows BPM, the speed box is hidden
	bool				m_bSongRectsKept;
	CRect				m_rcSongSettings[13];	// where the song settings were, as the dialog had them
	CString				m_strTempoLabel;

	// State variables
	int					m_iOctave;					// Selected octave
	int					m_iInstrument;				// Selected instrument
	int					m_iTrack;					// Selected track

public:
	virtual BOOL Create(LPCTSTR lpszClassName, LPCTSTR lpszWindowName, DWORD dwStyle = WS_OVERLAPPEDWINDOW, const RECT& rect = rectDefault, CWnd* pParentWnd = NULL, LPCTSTR lpszMenuName = NULL, DWORD dwExStyle = 0, CCreateContext* pContext = NULL);

// Generated message map functions
protected:
	DECLARE_MESSAGE_MAP()
public:
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	virtual BOOL DestroyWindow();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg LRESULT OnMenuChar(UINT nChar, UINT nFlags, CMenu* pMenu);
	afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
	afx_msg void OnShowWindow(BOOL bShow, UINT nStatus);
	virtual BOOL OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* pResult);
	afx_msg BOOL OnCopyData(CWnd* pWnd, COPYDATASTRUCT* pCopyDataStruct);
	afx_msg void OnDeltaposTempoSpin(NMHDR *pNMHDR, LRESULT *pResult);
	afx_msg void OnDeltaposSpeedSpin(NMHDR *pNMHDR, LRESULT *pResult);
	afx_msg void OnDeltaposRowsSpin(NMHDR *pNMHDR, LRESULT *pResult);
	afx_msg void OnDeltaposFrameSpin(NMHDR *pNMHDR, LRESULT *pResult);
	afx_msg void OnDeltaposKeyStepSpin(NMHDR *pNMHDR, LRESULT *pResult);
	afx_msg void OnInstNameChange();
	afx_msg void OnTrackerTogglePlay();
	afx_msg void OnTrackerPlay();
	afx_msg void OnTrackerPlayStart();
	afx_msg void OnTrackerPlayCursor();
	afx_msg void OnTrackerPlaypattern();
	afx_msg void OnTrackerStop();
	afx_msg void OnTrackerKillsound();
	afx_msg void OnCreateNSF();
	afx_msg void OnCreateWAV();
	afx_msg void OnNextFrame();
	afx_msg void OnPrevFrame();
	afx_msg void OnBnClickedIncFrame();
	afx_msg void OnBnClickedDecFrame();
	afx_msg void OnKeyRepeat();
	afx_msg void OnEnKeyStepChange();
	afx_msg void OnHelpPerformance();
	afx_msg void OnUpdateSBTempo(CCmdUI *pCmdUI);
	afx_msg void OnUpdateSBPosition(CCmdUI *pCmdUI);
	afx_msg void OnUpdateSBInstrument(CCmdUI *pCmdUI);
	afx_msg void OnUpdateSBOctave(CCmdUI *pCmdUI);
	afx_msg void OnUpdateSBFrequency(CCmdUI *pCmdUI);
	afx_msg void OnUpdateSBChip(CCmdUI *pCmdUI);
	afx_msg void OnUpdateKeyStepEdit(CCmdUI *pCmdUI);
	afx_msg void OnUpdateSpeedEdit(CCmdUI *pCmdUI);
	afx_msg void OnUpdateTempoEdit(CCmdUI *pCmdUI);
	afx_msg void OnUpdateRowsEdit(CCmdUI *pCmdUI);
	afx_msg void OnUpdateFramesEdit(CCmdUI *pCmdUI);
	afx_msg void OnUpdateKeyRepeat(CCmdUI *pCmdUI);
	afx_msg void OnUpdateInsertFrame(CCmdUI *pCmdUI);
	afx_msg void OnUpdateRemoveFrame(CCmdUI *pCmdUI);
	afx_msg void OnUpdateDuplicateFrame(CCmdUI *pCmdUI);
	afx_msg void OnUpdateModuleMoveframedown(CCmdUI *pCmdUI);
	afx_msg void OnUpdateModuleMoveframeup(CCmdUI *pCmdUI);
	afx_msg void OnUpdateInstrumentNew(CCmdUI *pCmdUI);
	afx_msg void OnUpdateInstrumentRemove(CCmdUI *pCmdUI);
	afx_msg void OnUpdateInstrumentClone(CCmdUI *pCmdUI);
	afx_msg void OnUpdateInstrumentDeepClone(CCmdUI *pCmdUI);
	afx_msg void OnUpdateInstrumentEdit(CCmdUI *pCmdUI);
	afx_msg void OnUpdateInstrumentLoad(CCmdUI *pCmdUI);
	afx_msg void OnUpdateInstrumentSave(CCmdUI *pCmdUI);
	afx_msg void OnUpdateNextSong(CCmdUI *pCmdUI);
	afx_msg void OnUpdatePrevSong(CCmdUI *pCmdUI);
	afx_msg void OnUpdateViewControlpanel(CCmdUI *pCmdUI);
	afx_msg void OnUpdateViewPianoRoll(CCmdUI *pCmdUI);
	afx_msg void OnActivate(UINT nState, CWnd *pWndOther, BOOL bMinimized);	// the piano roll gets the keys back
	afx_msg void OnUpdateViewPatternEditor(CCmdUI *pCmdUI);
	afx_msg void OnUpdateEditDrumGrid(CCmdUI *pCmdUI);
	afx_msg void OnUpdateViewClipsPanel(CCmdUI *pCmdUI);
	afx_msg void OnUpdateViewProblems(CCmdUI *pCmdUI);
	afx_msg void OnUpdateViewWhatNext(CCmdUI *pCmdUI);
	afx_msg void OnUpdateViewMixer(CCmdUI *pCmdUI);
	afx_msg void OnUpdateHighlight(CCmdUI *pCmdUI);
	afx_msg void OnUpdateEditCut(CCmdUI *pCmdUI);
	afx_msg void OnUpdateEditCopy(CCmdUI *pCmdUI);
	afx_msg void OnUpdateEditPaste(CCmdUI *pCmdUI);
	afx_msg void OnUpdateEditDelete(CCmdUI *pCmdUI);
	afx_msg void OnUpdateEditEnablemidi(CCmdUI *pCmdUI);
	afx_msg void OnUpdateSelectionEnabled(CCmdUI *pCmdUI);
	afx_msg void OnTimer(UINT nIDEvent);
	afx_msg void OnFileGeneralsettings();
	afx_msg void OnFileConnectClaude();
	afx_msg void OnFileExportForGames();
	afx_msg void OnEditVoiceEffect();
	afx_msg void OnEditRecordPhone();
	afx_msg void OnInstrumentLibrary();
	afx_msg void OnInstrumentListen();
	afx_msg void OnUpdateInstrumentListen(CCmdUI *pCmdUI);
	afx_msg void OnEditDrumGrid();
	afx_msg void OnHelpTutorials();
	afx_msg void OnHelpGlossary();
	afx_msg void OnUpdateHelpGlossary(CCmdUI *pCmdUI);
	afx_msg void OnHelpHearEffect();
	afx_msg void OnHelpContext();
	afx_msg void OnHelpExample(UINT nID);
	virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam);
	afx_msg void OnViewProfile();
	afx_msg void OnViewShowEverything();
	afx_msg void OnUpdateViewShowEverything(CCmdUI *pCmdUI);
	afx_msg void OnEnSongNameChange();
	afx_msg void OnEnSongArtistChange();
	afx_msg void OnEnSongCopyrightChange();
	afx_msg void OnFileImportText();
	afx_msg void OnFileImportNsf();
	afx_msg void OnFileExportText();
	afx_msg void OnEnKillfocusTempo();
	afx_msg void OnModuleInsertFrame();
	afx_msg void OnModuleRemoveFrame();
	afx_msg void OnModuleDuplicateFrame();
	afx_msg void OnModuleDuplicateFramePatterns();
	afx_msg void OnModuleChannels();
	afx_msg void OnModuleComments();
	afx_msg void OnModuleSongShape();
	afx_msg void OnModuleModuleproperties();
	afx_msg void OnModuleMoveframedown();
	afx_msg void OnModuleMoveframeup();
	afx_msg void OnLoadInstrument();
	afx_msg void OnSaveInstrument();
	afx_msg void OnEditInstrument();
	afx_msg void OnAddInstrument();
	afx_msg void OnRemoveInstrument();
	afx_msg void OnCloneInstrument();
	afx_msg void OnDeepCloneInstrument();
	afx_msg void OnBnClickedEditInst();
	afx_msg void OnCbnSelchangeSong();
	afx_msg void OnCbnSelchangeOctave();
	afx_msg void OnRemoveFocus();
	afx_msg void OnNextSong();
	afx_msg void OnPrevSong();
	afx_msg void OnTrackerSwitchToInstrument();
	afx_msg void OnUpdateTrackerSwitchToInstrument(CCmdUI *pCmdUI);
	afx_msg void OnClickedFollow();
	afx_msg void OnToggleFollow();
	afx_msg void OnViewControlpanel();
	afx_msg void OnViewPianoRoll();
	afx_msg void OnViewPatternEditor();
	afx_msg void OnViewClipsPanel();
	afx_msg void OnViewProblems();
	afx_msg void OnViewWhatNext();
	afx_msg void OnViewMixer();
	afx_msg void OnTrackerDPCM();
	afx_msg void OnTrackerDisplayRegisterState();
	afx_msg void OnSelectPatternEditor();
	afx_msg void OnSelectFrameEditor();
	afx_msg void OnHelpEffecttable();
	afx_msg void OnHelpFAQ();
	afx_msg void OnDestroy();
	afx_msg void OnNextInstrument();
	afx_msg void OnPrevInstrument();
	afx_msg void OnNewInstrumentMenu(NMHDR * pNotifyStruct, LRESULT * result);
	afx_msg void OnLoadInstrumentMenu(NMHDR * pNotifyStruct, LRESULT * result);
	afx_msg void OnAddInstrument2A03();
	afx_msg void OnAddInstrumentVRC6();
	afx_msg void OnAddInstrumentVRC7();
	afx_msg void OnAddInstrumentFDS();
	afx_msg void OnAddInstrumentMMC5();
	afx_msg void OnAddInstrumentN163();
	afx_msg void OnAddInstrumentS5B();
	afx_msg void OnEditUndo();
	afx_msg void OnEditRedo();
	afx_msg void OnEditCut();
	afx_msg void OnEditCopy();
	afx_msg void OnEditPaste();
	afx_msg void OnEditDelete();
	afx_msg void OnEditSelectall();
	afx_msg void OnEditExpandpatterns();
	afx_msg void OnEditShrinkpatterns();
	afx_msg void OnEditClearPatterns();
	afx_msg void OnEditRemoveUnusedInstruments();
	afx_msg void OnEditRemoveUnusedPatterns();
	afx_msg void OnEditMergeDuplicatedPatterns();
	afx_msg void OnEditEnableMIDI();
	afx_msg void OnUpdateEditUndo(CCmdUI *pCmdUI);
	afx_msg void OnUpdateEditRedo(CCmdUI *pCmdUI);
	afx_msg void OnDecayFast();
	afx_msg void OnDecaySlow();
	afx_msg void OnFrameeditorTop();
	afx_msg void OnFrameeditorLeft();
	afx_msg void OnUpdateFrameeditorTop(CCmdUI *pCmdUI);
	afx_msg void OnUpdateFrameeditorLeft(CCmdUI *pCmdUI);
	afx_msg void OnToggleSpeed();
	afx_msg LRESULT OnDisplayMessageString(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnDisplayMessageID(WPARAM wParam, LPARAM lParam);
protected:
	virtual void OnUpdateFrameTitle(BOOL bAddToTitle);
public:
	afx_msg void OnViewToolbar();
};

// Global DPI functions
int  SX(int pt);
int  SY(int pt);
void ScaleMouse(CPoint &pt);

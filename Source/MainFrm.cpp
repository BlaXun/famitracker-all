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
** Any permitted reproduction of these routin, in whole or in part,
** must bear this legend.
*/

#include "stdafx.h"
#include <algorithm>
#include "FamiTracker.h"
#include "FamiTrackerDoc.h"
#include "FamiTrackerView.h"
#include "MainFrm.h"
#include "ExportDialog.h"
#include "CreateWaveDlg.h"
#include "InstrumentEditDlg.h"
#include "ModulePropertiesDlg.h"
#include "ChannelsDlg.h"
#include "VisualizerWnd.h"
#include "TextExporter.h"
#include "ConfigGeneral.h"
#include "ConfigAppearance.h"
#include "ConfigMIDI.h"
#include "ConfigSound.h"
#include "ConfigShortcuts.h"
#include "ConfigWindow.h"
#include "ConfigMixer.h"
#include "Settings.h"
#include "Accelerator.h"
#include "SoundGen.h"
#include "MIDI.h"
#include "TrackerChannel.h"
#include "CommentsDlg.h"
#include "InstrumentFileTree.h"
#include "PatternAction.h"
#include "FrameAction.h"
#include "PatternEditor.h"
#include "FrameEditor.h"
#include "McpConnection.h"
#include "InstrumentLibrary.h"
#include "SoundBrowserDlg.h"
#include "DrumGridPanel.h"
#include "NoteViews.h"
#include "Profile.h"
#include "ImportNsfDlg.h"
#include "Tutorial.h"
#include "APU/APU.h"
#include "Clips.h"
#include "PianoRoll.h"
#include "ClipsPanel.h"
#include "ProblemsPanel.h"
#include "MixerPanel.h"
#include <vector>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// Actions undone and redone together (CMainFrame::BeginActionGroup): the last one first on undo
class CActionGroup : public CAction
{
public:
	CActionGroup() : CAction(0x1401) {}
	virtual ~CActionGroup()
	{
		for (size_t i = 0; i < m_Actions.size(); ++i)
			delete m_Actions[i];
	}
	bool SaveState(CMainFrame *pMainFrm) { return !m_Actions.empty(); }	// each was done as it was added
	void Undo(CMainFrame *pMainFrm)
	{
		for (size_t i = m_Actions.size(); i-- > 0; )
			m_Actions[i]->Undo(pMainFrm);
	}
	void Redo(CMainFrame *pMainFrm)
	{
		for (size_t i = 0; i < m_Actions.size(); ++i)
			m_Actions[i]->Redo(pMainFrm);
	}
	void Add(CAction *pAction) { m_Actions.push_back(pAction); }
	bool Empty() const { return m_Actions.empty(); }

private:
	std::vector<CAction*> m_Actions;
};

static UINT indicators[] =
{
	ID_SEPARATOR,           // status line indicator
	ID_INDICATOR_CHIP,
	ID_INDICATOR_INSTRUMENT, 
	ID_INDICATOR_OCTAVE,
	ID_INDICATOR_RATE,
	ID_INDICATOR_TEMPO,
	ID_INDICATOR_TIME,
	ID_INDICATOR_POS,
	ID_INDICATOR_CAPS,
	ID_INDICATOR_NUM,
	ID_INDICATOR_SCRL,
};

// Timers
enum {
	TMR_WELCOME, 
	TMR_AUDIO_CHECK, 
	TMR_AUTOSAVE
};

// Repeat config
const int REPEAT_DELAY = 20;
const int REPEAT_TIME = 200;

// DPI variables
static const int DEFAULT_DPI = 96;
static int _dpiX, _dpiY;

// DPI scaling functions
int SX(int pt)
{
	return MulDiv(pt, _dpiX, DEFAULT_DPI);
}

int SY(int pt)
{
	return MulDiv(pt, _dpiY, DEFAULT_DPI);
}

void ScaleMouse(CPoint &pt)
{
	pt.x = MulDiv(pt.x, DEFAULT_DPI, _dpiX);
	pt.y = MulDiv(pt.y, DEFAULT_DPI, _dpiY);
}

// CMainFrame

IMPLEMENT_DYNCREATE(CMainFrame, CFrameWnd)

// CMainFrame construction/destruction

CMainFrame::CMainFrame() : 
	m_pVisualizerWnd(NULL), 
	m_pFrameEditor(NULL),
	m_pImageList(NULL),
	m_pLockedEditSpeed(NULL),
	m_pLockedEditTempo(NULL),
	m_pLockedEditLength(NULL),
	m_pLockedEditFrames(NULL),
	m_pLockedEditStep(NULL),
	m_pBannerEditName(NULL),
	m_pBannerEditArtist(NULL),
	m_pBannerEditCopyright(NULL),
	m_pInstrumentList(NULL),
	m_pActionHandler(NULL),
	m_iFrameEditorPos(FRAME_EDIT_POS_TOP),
	m_pMcpConnection(NULL),
	m_pInstrumentFileTree(NULL),
	m_pPianoRoll(NULL),
	m_pClipsBar(NULL),
	m_pProblemsBar(NULL),
	m_pMixerBar(NULL),
	m_pDrumGridBar(NULL),
	m_bPatternShown(true),
	m_pActionGroup(NULL),
	m_iActionGroupDepth(0),
	m_bFewerMenus(false),
	m_bBPM(false),
	m_bSongRectsKept(false),
	m_iInstrument(0),
	m_iTrack(0),
	m_iOctave(3)
{
	_dpiX = DEFAULT_DPI;
	_dpiY = DEFAULT_DPI;
}

CMainFrame::~CMainFrame()
{
	SAFE_RELEASE(m_pImageList);
	SAFE_RELEASE(m_pLockedEditSpeed);
	SAFE_RELEASE(m_pLockedEditTempo);
	SAFE_RELEASE(m_pLockedEditLength);
	SAFE_RELEASE(m_pLockedEditFrames);
	SAFE_RELEASE(m_pLockedEditStep);
	SAFE_RELEASE(m_pBannerEditName);
	SAFE_RELEASE(m_pBannerEditArtist);
	SAFE_RELEASE(m_pBannerEditCopyright);
	SAFE_RELEASE(m_pFrameEditor);
	SAFE_RELEASE(m_pInstrumentList);
	SAFE_RELEASE(m_pVisualizerWnd);
	SAFE_RELEASE(m_pActionHandler);
	SAFE_RELEASE(m_pMcpConnection);
	SAFE_RELEASE(m_pPianoRoll);	// its window went with this one, its parent
	SAFE_RELEASE(m_pClipsBar);
	SAFE_RELEASE(m_pProblemsBar);
	SAFE_RELEASE(m_pMixerBar);
	SAFE_RELEASE(m_pDrumGridBar);
	SAFE_RELEASE(m_pActionGroup);
	SAFE_RELEASE(m_pInstrumentFileTree);
}

BEGIN_MESSAGE_MAP(CMainFrame, CFrameWnd)
	ON_WM_CREATE()
	ON_WM_SIZE()
	ON_WM_TIMER()
	ON_WM_ERASEBKGND()
	ON_WM_SHOWWINDOW()
	ON_WM_DESTROY()
	ON_WM_COPYDATA()
	ON_COMMAND(ID_FILE_GENERALSETTINGS, OnFileGeneralsettings)
	ON_COMMAND(ID_FILE_CONNECTCLAUDE, OnFileConnectClaude)
	ON_COMMAND(ID_FILE_EXPORTFORGAMES, OnFileExportForGames)
	ON_COMMAND(ID_EDIT_VOICEEFFECT, OnEditVoiceEffect)
	ON_COMMAND(ID_INSTRUMENT_LIBRARY, OnInstrumentLibrary)
	ON_COMMAND(ID_EDIT_DRUMGRID, OnEditDrumGrid)
	ON_UPDATE_COMMAND_UI(ID_EDIT_DRUMGRID, OnUpdateEditDrumGrid)
	ON_COMMAND(ID_HELP_TUTORIALS, OnHelpTutorials)
	ON_COMMAND_RANGE(ID_HELP_EXAMPLE_FIRST, ID_HELP_EXAMPLE_SOFT, OnHelpExample)
	ON_COMMAND(ID_VIEW_PROFILE, OnViewProfile)
	ON_COMMAND(ID_VIEW_SHOWEVERYTHING, OnViewShowEverything)
	ON_UPDATE_COMMAND_UI(ID_VIEW_SHOWEVERYTHING, OnUpdateViewShowEverything)
	ON_COMMAND(ID_FILE_IMPORTTEXT, OnFileImportText)
	ON_COMMAND(ID_FILE_IMPORTNSF, OnFileImportNsf)
	ON_COMMAND(ID_FILE_EXPORTTEXT, OnFileExportText)
	ON_COMMAND(ID_FILE_CREATE_NSF, OnCreateNSF)
	ON_COMMAND(ID_FILE_CREATEWAV, OnCreateWAV)
	ON_COMMAND(ID_EDIT_UNDO, OnEditUndo)
	ON_COMMAND(ID_EDIT_REDO, OnEditRedo)
	ON_COMMAND(ID_EDIT_CUT, OnEditCut)
	ON_COMMAND(ID_EDIT_COPY, OnEditCopy)
	ON_COMMAND(ID_EDIT_PASTE, OnEditPaste)
	ON_COMMAND(ID_EDIT_DELETE, OnEditDelete)
	ON_COMMAND(ID_EDIT_SELECTALL, OnEditSelectall)
	ON_COMMAND(ID_EDIT_ENABLEMIDI, OnEditEnableMIDI)
	ON_COMMAND(ID_EDIT_EXPANDPATTERNS, OnEditExpandpatterns)
	ON_COMMAND(ID_EDIT_SHRINKPATTERNS, OnEditShrinkpatterns)
	ON_COMMAND(ID_EDIT_CLEARPATTERNS, OnEditClearPatterns)
	ON_COMMAND(ID_CLEANUP_REMOVEUNUSEDINSTRUMENTS, OnEditRemoveUnusedInstruments)
	ON_COMMAND(ID_CLEANUP_REMOVEUNUSEDPATTERNS, OnEditRemoveUnusedPatterns)
	ON_COMMAND(ID_CLEANUP_MERGEDUPLICATEDPATTERNS, OnEditMergeDuplicatedPatterns)
	ON_COMMAND(ID_INSTRUMENT_NEW, OnAddInstrument)
	ON_COMMAND(ID_INSTRUMENT_REMOVE, OnRemoveInstrument)
	ON_COMMAND(ID_INSTRUMENT_CLONE, OnCloneInstrument)
	ON_COMMAND(ID_INSTRUMENT_DEEPCLONE, OnDeepCloneInstrument)
	ON_COMMAND(ID_INSTRUMENT_SAVE, OnSaveInstrument)
	ON_COMMAND(ID_INSTRUMENT_LOAD, OnLoadInstrument)
	ON_COMMAND(ID_INSTRUMENT_EDIT, OnEditInstrument)
	ON_COMMAND(ID_INSTRUMENT_ADD_2A03, OnAddInstrument2A03)
	ON_COMMAND(ID_INSTRUMENT_ADD_VRC6, OnAddInstrumentVRC6)
	ON_COMMAND(ID_INSTRUMENT_ADD_VRC7, OnAddInstrumentVRC7)
	ON_COMMAND(ID_INSTRUMENT_ADD_FDS, OnAddInstrumentFDS)
	ON_COMMAND(ID_INSTRUMENT_ADD_MMC5, OnAddInstrumentMMC5)
	ON_COMMAND(ID_INSTRUMENT_ADD_N163, OnAddInstrumentN163)
	ON_COMMAND(ID_INSTRUMENT_ADD_S5B, OnAddInstrumentS5B)
	ON_COMMAND(ID_MODULE_MODULEPROPERTIES, OnModuleModuleproperties)
	ON_COMMAND(ID_MODULE_CHANNELS, OnModuleChannels)
	ON_COMMAND(ID_MODULE_COMMENTS, OnModuleComments)
	ON_COMMAND(ID_MODULE_INSERTFRAME, OnModuleInsertFrame)
	ON_COMMAND(ID_MODULE_REMOVEFRAME, OnModuleRemoveFrame)
	ON_COMMAND(ID_MODULE_DUPLICATEFRAME, OnModuleDuplicateFrame)
	ON_COMMAND(ID_MODULE_DUPLICATEFRAMEPATTERNS, OnModuleDuplicateFramePatterns)
	ON_COMMAND(ID_MODULE_MOVEFRAMEDOWN, OnModuleMoveframedown)
	ON_COMMAND(ID_MODULE_MOVEFRAMEUP, OnModuleMoveframeup)
	ON_COMMAND(ID_TRACKER_PLAY, OnTrackerPlay)
	ON_COMMAND(ID_TRACKER_PLAY_START, OnTrackerPlayStart)
	ON_COMMAND(ID_TRACKER_PLAY_CURSOR, OnTrackerPlayCursor)
	ON_COMMAND(ID_TRACKER_STOP, OnTrackerStop)
	ON_COMMAND(ID_TRACKER_TOGGLE_PLAY, OnTrackerTogglePlay)
	ON_COMMAND(ID_TRACKER_PLAYPATTERN, OnTrackerPlaypattern)	
	ON_COMMAND(ID_TRACKER_KILLSOUND, OnTrackerKillsound)
	ON_COMMAND(ID_TRACKER_SWITCHTOTRACKINSTRUMENT, OnTrackerSwitchToInstrument)
	ON_COMMAND(ID_TRACKER_DPCM, OnTrackerDPCM)
	ON_COMMAND(ID_TRACKER_DISPLAYREGISTERSTATE, OnTrackerDisplayRegisterState)
	ON_COMMAND(ID_VIEW_CONTROLPANEL, OnViewControlpanel)
	ON_COMMAND(ID_VIEW_PIANOROLL, OnViewPianoRoll)
	ON_COMMAND(ID_VIEW_PATTERNEDITOR, OnViewPatternEditor)
	ON_COMMAND(ID_VIEW_CLIPSPANEL, OnViewClipsPanel)
	ON_COMMAND(ID_VIEW_PROBLEMS, OnViewProblems)
	ON_COMMAND(ID_VIEW_MIXER, OnViewMixer)
	ON_COMMAND(ID_HELP, CFrameWnd::OnHelp)
	ON_COMMAND(ID_HELP_FINDER, CFrameWnd::OnHelpFinder)
	ON_COMMAND(ID_HELP_PERFORMANCE, OnHelpPerformance)
	ON_COMMAND(ID_HELP_EFFECTTABLE, &CMainFrame::OnHelpEffecttable)
	ON_COMMAND(ID_HELP_FAQ, &CMainFrame::OnHelpFAQ)
	ON_COMMAND(ID_DEFAULT_HELP, CFrameWnd::OnHelpFinder)
	ON_COMMAND(ID_CONTEXT_HELP, CFrameWnd::OnContextHelp)
	ON_COMMAND(ID_FRAMEEDITOR_TOP, OnFrameeditorTop)
	ON_COMMAND(ID_FRAMEEDITOR_LEFT, OnFrameeditorLeft)
	ON_COMMAND(ID_NEXT_FRAME, OnNextFrame)
	ON_COMMAND(ID_PREV_FRAME, OnPrevFrame)
	ON_COMMAND(IDC_KEYREPEAT, OnKeyRepeat)
	ON_COMMAND(ID_NEXT_SONG, OnNextSong)
	ON_COMMAND(ID_PREV_SONG, OnPrevSong)
	ON_COMMAND(IDC_FOLLOW_TOGGLE, OnToggleFollow)
	ON_COMMAND(ID_FOCUS_PATTERN_EDITOR, OnSelectPatternEditor)
	ON_COMMAND(ID_FOCUS_FRAME_EDITOR, OnSelectFrameEditor)
	ON_COMMAND(ID_CMD_NEXT_INSTRUMENT, OnNextInstrument)
	ON_COMMAND(ID_CMD_PREV_INSTRUMENT, OnPrevInstrument)
	ON_COMMAND(ID_TOGGLE_SPEED, OnToggleSpeed)
	ON_COMMAND(ID_DECAY_FAST, OnDecayFast)
	ON_COMMAND(ID_DECAY_SLOW, OnDecaySlow)
	ON_BN_CLICKED(IDC_FRAME_INC, OnBnClickedIncFrame)
	ON_BN_CLICKED(IDC_FRAME_DEC, OnBnClickedDecFrame)
	ON_BN_CLICKED(IDC_FOLLOW, OnClickedFollow)
	ON_NOTIFY(UDN_DELTAPOS, IDC_SPEED_SPIN, OnDeltaposSpeedSpin)
	ON_NOTIFY(UDN_DELTAPOS, IDC_TEMPO_SPIN, OnDeltaposTempoSpin)
	ON_NOTIFY(UDN_DELTAPOS, IDC_ROWS_SPIN, OnDeltaposRowsSpin)
	ON_NOTIFY(UDN_DELTAPOS, IDC_FRAME_SPIN, OnDeltaposFrameSpin)
	ON_NOTIFY(UDN_DELTAPOS, IDC_KEYSTEP_SPIN, OnDeltaposKeyStepSpin)
	ON_EN_CHANGE(IDC_INSTNAME, OnInstNameChange)
	ON_EN_CHANGE(IDC_KEYSTEP, OnEnKeyStepChange)
	ON_EN_CHANGE(IDC_SONG_NAME, OnEnSongNameChange)
	ON_EN_CHANGE(IDC_SONG_ARTIST, OnEnSongArtistChange)
	ON_EN_CHANGE(IDC_SONG_COPYRIGHT, OnEnSongCopyrightChange)
	ON_EN_SETFOCUS(IDC_KEYREPEAT, OnRemoveFocus)
	ON_UPDATE_COMMAND_UI(ID_EDIT_UNDO, OnUpdateEditUndo)
	ON_UPDATE_COMMAND_UI(ID_EDIT_REDO, OnUpdateEditRedo)
	ON_UPDATE_COMMAND_UI(ID_EDIT_CUT, OnUpdateEditCut)
	ON_UPDATE_COMMAND_UI(ID_EDIT_COPY, OnUpdateEditCopy)
	ON_UPDATE_COMMAND_UI(ID_EDIT_PASTE, OnUpdateEditPaste)
	ON_UPDATE_COMMAND_UI(ID_EDIT_DELETE, OnUpdateEditDelete)
	ON_UPDATE_COMMAND_UI(ID_EDIT_ENABLEMIDI, OnUpdateEditEnablemidi)
	ON_UPDATE_COMMAND_UI(ID_MODULE_INSERTFRAME, OnUpdateInsertFrame)
	ON_UPDATE_COMMAND_UI(ID_MODULE_REMOVEFRAME, OnUpdateRemoveFrame)
	ON_UPDATE_COMMAND_UI(ID_MODULE_DUPLICATEFRAME, OnUpdateDuplicateFrame)
	ON_UPDATE_COMMAND_UI(ID_MODULE_MOVEFRAMEDOWN, OnUpdateModuleMoveframedown)
	ON_UPDATE_COMMAND_UI(ID_MODULE_MOVEFRAMEUP, OnUpdateModuleMoveframeup)
	ON_UPDATE_COMMAND_UI(ID_INSTRUMENT_NEW, OnUpdateInstrumentNew)
	ON_UPDATE_COMMAND_UI(ID_INSTRUMENT_REMOVE, OnUpdateInstrumentRemove)
	ON_UPDATE_COMMAND_UI(ID_INSTRUMENT_CLONE, OnUpdateInstrumentClone)
	ON_UPDATE_COMMAND_UI(ID_INSTRUMENT_DEEPCLONE, OnUpdateInstrumentDeepClone)
	ON_UPDATE_COMMAND_UI(ID_INSTRUMENT_EDIT, OnUpdateInstrumentEdit)
	ON_UPDATE_COMMAND_UI(ID_INSTRUMENT_LOAD, OnUpdateInstrumentLoad)
	ON_UPDATE_COMMAND_UI(ID_INSTRUMENT_SAVE, OnUpdateInstrumentSave)
	ON_UPDATE_COMMAND_UI(ID_INDICATOR_INSTRUMENT, OnUpdateSBInstrument)
	ON_UPDATE_COMMAND_UI(ID_INDICATOR_OCTAVE, OnUpdateSBOctave)
	ON_UPDATE_COMMAND_UI(ID_INDICATOR_RATE, OnUpdateSBFrequency)
	ON_UPDATE_COMMAND_UI(ID_INDICATOR_TEMPO, OnUpdateSBTempo)
	ON_UPDATE_COMMAND_UI(ID_INDICATOR_CHIP, OnUpdateSBChip)
	ON_UPDATE_COMMAND_UI(IDC_KEYSTEP, OnUpdateKeyStepEdit)
	ON_UPDATE_COMMAND_UI(IDC_KEYREPEAT, OnUpdateKeyRepeat)
	ON_UPDATE_COMMAND_UI(IDC_SPEED, OnUpdateSpeedEdit)
	ON_UPDATE_COMMAND_UI(IDC_TEMPO, OnUpdateTempoEdit)
	ON_UPDATE_COMMAND_UI(IDC_ROWS, OnUpdateRowsEdit)
	ON_UPDATE_COMMAND_UI(IDC_FRAMES, OnUpdateFramesEdit)
	ON_UPDATE_COMMAND_UI(ID_NEXT_SONG, OnUpdateNextSong)
	ON_UPDATE_COMMAND_UI(ID_PREV_SONG, OnUpdatePrevSong)
	ON_UPDATE_COMMAND_UI(ID_TRACKER_SWITCHTOTRACKINSTRUMENT, OnUpdateTrackerSwitchToInstrument)
	ON_UPDATE_COMMAND_UI(ID_VIEW_CONTROLPANEL, OnUpdateViewControlpanel)
	ON_UPDATE_COMMAND_UI(ID_VIEW_PIANOROLL, OnUpdateViewPianoRoll)
	ON_UPDATE_COMMAND_UI(ID_VIEW_PATTERNEDITOR, OnUpdateViewPatternEditor)
	ON_UPDATE_COMMAND_UI(ID_VIEW_CLIPSPANEL, OnUpdateViewClipsPanel)
	ON_UPDATE_COMMAND_UI(ID_VIEW_PROBLEMS, OnUpdateViewProblems)
	ON_UPDATE_COMMAND_UI(ID_VIEW_MIXER, OnUpdateViewMixer)
	ON_UPDATE_COMMAND_UI(IDC_HIGHLIGHT1, OnUpdateHighlight)
	ON_UPDATE_COMMAND_UI(IDC_HIGHLIGHT2, OnUpdateHighlight)
	ON_UPDATE_COMMAND_UI(ID_EDIT_EXPANDPATTERNS, OnUpdateSelectionEnabled)
	ON_UPDATE_COMMAND_UI(ID_EDIT_SHRINKPATTERNS, OnUpdateSelectionEnabled)
	ON_UPDATE_COMMAND_UI(ID_FRAMEEDITOR_TOP, OnUpdateFrameeditorTop)
	ON_UPDATE_COMMAND_UI(ID_FRAMEEDITOR_LEFT, OnUpdateFrameeditorLeft)
	ON_CBN_SELCHANGE(IDC_SUBTUNE, OnCbnSelchangeSong)
	ON_CBN_SELCHANGE(IDC_OCTAVE, OnCbnSelchangeOctave)
	ON_MESSAGE(WM_USER_DISPLAY_MESSAGE_STRING, OnDisplayMessageString)
	ON_MESSAGE(WM_USER_DISPLAY_MESSAGE_ID, OnDisplayMessageID)
	ON_COMMAND(ID_VIEW_TOOLBAR, &CMainFrame::OnViewToolbar)
	END_MESSAGE_MAP()


BOOL CMainFrame::Create(LPCTSTR lpszClassName, LPCTSTR lpszWindowName, DWORD dwStyle , const RECT& rect , CWnd* pParentWnd , LPCTSTR lpszMenuName , DWORD dwExStyle , CCreateContext* pContext)
{
	CSettings *pSettings = theApp.GetSettings();
	RECT newrect;

	// Load stored position
	newrect.bottom	= pSettings->WindowPos.iBottom;
	newrect.left	= pSettings->WindowPos.iLeft;
	newrect.right	= pSettings->WindowPos.iRight;
	newrect.top		= pSettings->WindowPos.iTop;

	return CFrameWnd::Create(lpszClassName, lpszWindowName, dwStyle, newrect, pParentWnd, lpszMenuName, dwExStyle, pContext);
}

int CMainFrame::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	// Get the DPI setting
	CDC *pDC = GetDC();
	if (pDC) {
		_dpiX = pDC->GetDeviceCaps(LOGPIXELSX);
		_dpiY = pDC->GetDeviceCaps(LOGPIXELSY);
		ReleaseDC(pDC);
	}

	m_pActionHandler = new CActionHandler();

	if (CFrameWnd::OnCreate(lpCreateStruct) == -1)
		return -1;

	if (!CreateToolbars())
		return -1;

	if (!m_wndStatusBar.Create(this) || !m_wndStatusBar.SetIndicators(indicators, sizeof(indicators) / sizeof(UINT))) {
		TRACE0("Failed to create status bar\n");
		return -1;      // fail to create
	}

	if (!CreateDialogPanels())
		return -1;

	if (!CreateInstrumentToolbar()) {
		TRACE0("Failed to create instrument toolbar\n");
		return -1;      // fail to create
	}

	/*
	// TODO: Delete these three lines if you don't want the toolbar to be dockable
	m_wndToolBar.EnableDocking(CBRS_ALIGN_ANY);
	EnableDocking(CBRS_ALIGN_ANY);
	DockControlBar(&m_wndToolBar);
	*/
	
	if (!CreateVisualizerWindow()) {
		TRACE0("Failed to create sample window\n");
		return -1;      // fail to create
	}

	// Initial message, 100ms
	SetTimer(TMR_WELCOME, 100, NULL);

	// Periodic audio check, 500ms
	SetTimer(TMR_AUDIO_CHECK, 500, NULL);

	// Auto save
#ifdef AUTOSAVE
	SetTimer(TMR_AUTOSAVE, 1000, NULL);
#endif

	m_wndOctaveBar.CheckDlgButton(IDC_FOLLOW, theApp.GetSettings()->FollowMode);
	m_wndOctaveBar.SetDlgItemInt(IDC_HIGHLIGHT1, CFamiTrackerDoc::DEFAULT_FIRST_HIGHLIGHT, 0);
	m_wndOctaveBar.SetDlgItemInt(IDC_HIGHLIGHT2, CFamiTrackerDoc::DEFAULT_SECOND_HIGHLIGHT, 0);

	UpdateMenus();

	// The profile's menus and song settings (Profile.h)
	ApplyProfile();

	// Setup saved frame editor position
	SetFrameEditorPosition(theApp.GetSettings()->FrameEditPos);

#ifdef _DEBUG
	m_strTitle.Append(_T(" [DEBUG]"));
#endif
#ifdef WIP
	m_strTitle.Append(_T(" [BETA]"));
#endif

#ifndef RELEASE_BUILD
	if (AfxGetResourceHandle() != AfxGetInstanceHandle()) {
		// Prevent confusing bugs while developing
		m_strTitle.Append(_T(" [Localization enabled]"));
	}
#endif

	// Claude Code, if the user switched it on (File > Connect Claude Code)
	m_pMcpConnection = new CMcpConnection();
	m_pMcpConnection->Start(this);

	return 0;
}

bool CMainFrame::CreateToolbars()
{
	REBARBANDINFO rbi1;

	if (!m_wndToolBarReBar.Create(this)) {
		TRACE0("Failed to create rebar\n");
		return false;      // fail to create
	}

	// Add the toolbar
	if (!m_wndToolBar.CreateEx(this, TBSTYLE_FLAT | TBSTYLE_TRANSPARENT, WS_CHILD | WS_VISIBLE | CBRS_TOP | CBRS_TOOLTIPS | CBRS_FLYBY | CBRS_SIZE_DYNAMIC) ||
		!m_wndToolBar.LoadToolBar(IDR_MAINFRAME))  {
		TRACE0("Failed to create toolbar\n");
		return false;      // fail to create
	}

	m_wndToolBar.SetBarStyle(CBRS_ALIGN_TOP | CBRS_SIZE_DYNAMIC | CBRS_TOOLTIPS | CBRS_FLYBY);

	if (!m_wndOctaveBar.Create(this, (UINT)IDD_OCTAVE, CBRS_TOOLTIPS | CBRS_FLYBY, IDD_OCTAVE)) {
		TRACE0("Failed to create octave bar\n");
		return false;      // fail to create
	}

	rbi1.cbSize		= sizeof(REBARBANDINFO);
	rbi1.fMask		= RBBIM_CHILD | RBBIM_CHILDSIZE | RBBIM_STYLE | RBBIM_SIZE;
	rbi1.fStyle		= RBBS_NOGRIPPER;
	rbi1.hwndChild	= m_wndToolBar;
	rbi1.cxMinChild	= SX(623);	// 554, plus 23 each for the microphone, clips and piano roll buttons
	rbi1.cyMinChild	= SY(22);
	rbi1.cx			= SX(496);

	if (!m_wndToolBarReBar.GetReBarCtrl().InsertBand(-1, &rbi1)) {
		TRACE0("Failed to create rebar\n");
		return false;      // fail to create
	}

	rbi1.cbSize		= sizeof(REBARBANDINFO);
	rbi1.fMask		= RBBIM_CHILD | RBBIM_CHILDSIZE | RBBIM_STYLE | RBBIM_SIZE;
	rbi1.fStyle		= RBBS_NOGRIPPER;
	rbi1.hwndChild	= m_wndOctaveBar;
	rbi1.cxMinChild	= SX(120);
	rbi1.cyMinChild	= SY(22);
	rbi1.cx			= SX(100);

	if (!m_wndToolBarReBar.GetReBarCtrl().InsertBand(-1, &rbi1)) {
		TRACE0("Failed to create rebar\n");
		return false;      // fail to create
	}

	m_wndToolBarReBar.GetReBarCtrl().MinimizeBand(0);

	HBITMAP hbm = (HBITMAP)::LoadImage(AfxGetInstanceHandle(), MAKEINTRESOURCE(IDB_TOOLBAR_256), IMAGE_BITMAP, 0,0, LR_CREATEDIBSECTION);
	m_bmToolbar.Attach(hbm); 
	
	m_ilToolBar.Create(16, 15, ILC_COLOR8 | ILC_MASK, 4, 4);
	m_ilToolBar.Add(&m_bmToolbar, RGB(192, 192, 192));
	m_wndToolBar.GetToolBarCtrl().SetImageList(&m_ilToolBar);

	return true;
}

bool CMainFrame::CreateDialogPanels()
{
	//CDialogBar

	// Top area
	if (!m_wndControlBar.Create(this, IDD_MAINBAR, CBRS_TOP | CBRS_TOOLTIPS | CBRS_FLYBY, IDD_MAINBAR)) {
		TRACE0("Failed to create frame main bar\n");
		return false;
	}

	/////////
	if (!m_wndVerticalControlBar.Create(this, IDD_MAINBAR, CBRS_LEFT | CBRS_TOOLTIPS | CBRS_FLYBY, IDD_MAINBAR)) {
		TRACE0("Failed to create frame main bar\n");
		return false;
	}

	m_wndVerticalControlBar.ShowWindow(SW_HIDE);
	/////////

	// Create frame controls
	m_wndFrameControls.SetFrameParent(this);
	m_wndFrameControls.Create(IDD_FRAMECONTROLS, &m_wndControlBar);
	m_wndFrameControls.ShowWindow(SW_SHOW);

	// Create frame editor
	m_pFrameEditor = new CFrameEditor(this);

	CRect rect(SX(12), SY(10), SX(162), SY(173));

	if (!m_pFrameEditor->CreateEx(WS_EX_STATICEDGE, NULL, _T(""), WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL, rect, (CWnd*)&m_wndControlBar, 0)) {
		TRACE0("Failed to create pattern window\n");
		return false;
	}

	m_wndDialogBar.SetFrameParent(this);

	if (!m_wndDialogBar.Create(IDD_MAINFRAME, &m_wndControlBar)) {
		TRACE0("Failed to create dialog bar\n");
		return false;
	}
	
	m_wndDialogBar.ShowWindow(SW_SHOW);

	// Subclass edit boxes
	m_pLockedEditSpeed	= new CLockedEdit();
	m_pLockedEditTempo	= new CLockedEdit();
	m_pLockedEditLength = new CLockedEdit();
	m_pLockedEditFrames = new CLockedEdit();
	m_pLockedEditStep	= new CLockedEdit();

	m_pLockedEditSpeed->SubclassDlgItem(IDC_SPEED, &m_wndDialogBar);
	m_pLockedEditTempo->SubclassDlgItem(IDC_TEMPO, &m_wndDialogBar);
	m_pLockedEditLength->SubclassDlgItem(IDC_ROWS, &m_wndDialogBar);
	m_pLockedEditFrames->SubclassDlgItem(IDC_FRAMES, &m_wndDialogBar);
	m_pLockedEditStep->SubclassDlgItem(IDC_KEYSTEP, &m_wndDialogBar);

	// Subclass and setup the instrument list
	
	m_pInstrumentList = new CInstrumentList(this);
	m_pInstrumentList->SubclassDlgItem(IDC_INSTRUMENTS, &m_wndDialogBar);

	SetupColors();

	m_pImageList = new CImageList();
	m_pImageList->Create(16, 16, ILC_COLOR32, 1, 1);
	m_pImageList->Add(theApp.LoadIcon(IDI_INST_2A03));
	m_pImageList->Add(theApp.LoadIcon(IDI_INST_VRC6));
	m_pImageList->Add(theApp.LoadIcon(IDI_INST_VRC7));
	m_pImageList->Add(theApp.LoadIcon(IDI_INST_FDS));
	m_pImageList->Add(theApp.LoadIcon(IDI_INST_N163));
	m_pImageList->Add(theApp.LoadIcon(IDI_INST_5B));

	m_pInstrumentList->SetImageList(m_pImageList, LVSIL_NORMAL);
	m_pInstrumentList->SetImageList(m_pImageList, LVSIL_SMALL);

	m_pInstrumentList->SendMessage(LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_FULLROWSELECT);

	// Title, author & copyright
	m_pBannerEditName = new CBannerEdit(IDS_INFO_TITLE);
	m_pBannerEditArtist = new CBannerEdit(IDS_INFO_AUTHOR);
	m_pBannerEditCopyright = new CBannerEdit(IDS_INFO_COPYRIGHT);

	m_pBannerEditName->SubclassDlgItem(IDC_SONG_NAME, &m_wndDialogBar);
	m_pBannerEditArtist->SubclassDlgItem(IDC_SONG_ARTIST, &m_wndDialogBar);
	m_pBannerEditCopyright->SubclassDlgItem(IDC_SONG_COPYRIGHT, &m_wndDialogBar);

	m_pBannerEditName->SetLimitText(31);
	m_pBannerEditArtist->SetLimitText(31);
	m_pBannerEditCopyright->SetLimitText(31);

	CEdit *pInstName = static_cast<CEdit*>(m_wndDialogBar.GetDlgItem(IDC_INSTNAME));
	pInstName->SetLimitText(CInstrument::INST_NAME_MAX - 1);

	// New instrument editor

#ifdef NEW_INSTRUMENTPANEL
/*
	if (!m_wndInstrumentBar.Create(this, IDD_INSTRUMENTPANEL, CBRS_RIGHT | CBRS_TOOLTIPS | CBRS_FLYBY, IDD_INSTRUMENTPANEL)) {
		TRACE0("Failed to create frame instrument bar\n");
	}

	m_wndInstrumentBar.ShowWindow(SW_SHOW);
*/
#endif

	// Frame bar
/*
	if (!m_wndFrameBar.Create(this, IDD_FRAMEBAR, CBRS_LEFT | CBRS_TOOLTIPS | CBRS_FLYBY, IDD_FRAMEBAR)) {
		TRACE0("Failed to create frame bar\n");
	}
	
	m_wndFrameBar.ShowWindow(SW_SHOW);
*/

	return true;
}

bool CMainFrame::CreateVisualizerWindow()
{
	const int POS_X = 138;
	const int POS_Y = 113;
	const int WIDTH = 143;
	const int HEIGHT = 40;

	CRect rect(SX(POS_X), SY(POS_Y), SX(POS_X) + WIDTH, SY(POS_Y) + HEIGHT);

	// Create the sample graph window
	m_pVisualizerWnd = new CVisualizerWnd();

	if (!m_pVisualizerWnd->CreateEx(WS_EX_STATICEDGE, NULL, _T("Samples"), WS_CHILD | WS_VISIBLE, rect, (CWnd*)&m_wndDialogBar, 0))
		return false;

	// Assign this to the sound generator
	CSoundGen *pSoundGen = theApp.GetSoundGenerator();
	
	if (pSoundGen)
		pSoundGen->SetVisualizerWindow(m_pVisualizerWnd);

	return true;
}

bool CMainFrame::CreateInstrumentToolbar()
{
	// Setup the instrument toolbar
	REBARBANDINFO rbi;

	if (!m_wndInstToolBarWnd.CreateEx(0, NULL, _T(""), WS_CHILD | WS_VISIBLE, CRect(SX(288), SY(173), SX(472), SY(199)), (CWnd*)&m_wndDialogBar, 0))
		return false;

	if (!m_wndInstToolReBar.Create(WS_CHILD | WS_VISIBLE, CRect(0, 0, 0, 0), &m_wndInstToolBarWnd, AFX_IDW_REBAR))
		return false;

	if (!m_wndInstToolBar.CreateEx(&m_wndInstToolReBar, TBSTYLE_FLAT | TBSTYLE_TRANSPARENT, WS_CHILD | WS_VISIBLE | CBRS_TOP | CBRS_TOOLTIPS | CBRS_FLYBY | CBRS_SIZE_DYNAMIC) || !m_wndInstToolBar.LoadToolBar(IDT_INSTRUMENT))
		return false;

	m_wndInstToolBar.GetToolBarCtrl().SetExtendedStyle(TBSTYLE_EX_DRAWDDARROWS);

	// Set 24-bit icons
	HBITMAP hbm = (HBITMAP)::LoadImage(AfxGetInstanceHandle(), MAKEINTRESOURCE(IDB_TOOLBAR_INST_256), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
	m_bmInstToolbar.Attach(hbm);
	m_ilInstToolBar.Create(16, 16, ILC_COLOR24 | ILC_MASK, 4, 4);
	m_ilInstToolBar.Add(&m_bmInstToolbar, RGB(255, 0, 255));
	m_wndInstToolBar.GetToolBarCtrl().SetImageList(&m_ilInstToolBar);

	rbi.cbSize		= sizeof(REBARBANDINFO);
	rbi.fMask		= RBBIM_CHILD | RBBIM_CHILDSIZE | RBBIM_STYLE | RBBIM_TEXT;
	rbi.fStyle		= RBBS_NOGRIPPER;
	rbi.cxMinChild	= SX(300);
	rbi.cyMinChild	= SY(30);
	rbi.lpText		= "";
	rbi.cch			= 7;
	rbi.cx			= SX(300);
	rbi.hwndChild	= m_wndInstToolBar;

	m_wndInstToolReBar.InsertBand(-1, &rbi);

	// Route messages to this window
	m_wndInstToolBar.SetOwner(this);

	// Turn add new instrument button into a drop-down list
	m_wndInstToolBar.SetButtonStyle(0, TBBS_DROPDOWN);
	// Turn load instrument button into a drop-down list
	m_wndInstToolBar.SetButtonStyle(4, TBBS_DROPDOWN);

	return true;
}

void CMainFrame::ResizeFrameWindow()
{
	// Called when the number of channels has changed
	ASSERT(m_pFrameEditor != NULL);

	CFamiTrackerDoc *pDocument = static_cast<CFamiTrackerDoc*>(GetActiveDocument());

	if (pDocument != NULL) {

		int Channels = pDocument->GetAvailableChannels();
		int Height(0), Width(0);

		// Located to the right
		if (m_iFrameEditorPos == FRAME_EDIT_POS_TOP) {
			// Frame editor window
			Height = CFrameEditor::DEFAULT_HEIGHT;
			Width = m_pFrameEditor->CalcWidth(Channels);

			m_pFrameEditor->MoveWindow(SX(12), SY(12), SX(Width), SY(Height));

			// Move frame controls
			m_wndFrameControls.MoveWindow(SX(10), SY(Height) + SY(21), SX(150), SY(26));
		}
		// Located to the left
		else {
			// Frame editor window
			CRect rect;
			m_wndVerticalControlBar.GetClientRect(&rect);

			Height = rect.Height() - CPatternEditor::HEADER_HEIGHT - 2;
			Width = m_pFrameEditor->CalcWidth(Channels);

			m_pFrameEditor->MoveWindow(SX(2), SY(CPatternEditor::HEADER_HEIGHT + 1), SX(Width), SY(Height));

			// Move frame controls
			m_wndFrameControls.MoveWindow(SX(4), SY(10), SX(150), SY(26));
		}

		// Vertical control bar
		m_wndVerticalControlBar.m_sizeDefault.cx = Width + 4;
		m_wndVerticalControlBar.CalcFixedLayout(TRUE, FALSE);
		RecalcLayout();
	}

	CRect ChildRect, ParentRect, FrameEditorRect;

	m_wndControlBar.GetClientRect(&ParentRect);
	m_pFrameEditor->GetClientRect(&FrameEditorRect);

	int DialogStartPos;

	if (m_iFrameEditorPos == FRAME_EDIT_POS_TOP)
		DialogStartPos = FrameEditorRect.right + 32;
	else
		DialogStartPos = 0;

	m_wndDialogBar.MoveWindow(DialogStartPos, 2, ParentRect.Width() - DialogStartPos, ParentRect.Height() - 4);
	m_wndDialogBar.GetWindowRect(&ChildRect);
	m_wndDialogBar.GetDlgItem(IDC_INSTRUMENTS)->MoveWindow(SX(288), SY(10), ChildRect.Width() - SX(296), SY(158));
	m_wndDialogBar.GetDlgItem(IDC_INSTNAME)->MoveWindow(SX(478), SY(175), ChildRect.Width() - SX(486), SY(22));

	m_pFrameEditor->RedrawWindow();
}

void CMainFrame::SetupColors()
{
	ASSERT(m_pInstrumentList != NULL);

	// Instrument list colors
	m_pInstrumentList->SetBkColor(theApp.GetSettings()->Appearance.iColBackground);
	m_pInstrumentList->SetTextBkColor(theApp.GetSettings()->Appearance.iColBackground);
	m_pInstrumentList->SetTextColor(theApp.GetSettings()->Appearance.iColPatternText);
	m_pInstrumentList->RedrawWindow();
}

void CMainFrame::SetTempo(int Tempo)
{
	CFamiTrackerDoc *pDoc = static_cast<CFamiTrackerDoc*>(GetActiveDocument());
	int MinTempo = pDoc->GetSpeedSplitPoint();
	Tempo = std::max(Tempo, MinTempo);
	Tempo = std::min(Tempo, MAX_TEMPO);
	pDoc->SetSongTempo(m_iTrack, Tempo);
	theApp.GetSoundGenerator()->ResetTempo();

	if (!m_bBPM && m_wndDialogBar.GetDlgItemInt(IDC_TEMPO) != Tempo)	// (the BPM shows there otherwise)
		m_wndDialogBar.SetDlgItemInt(IDC_TEMPO, Tempo, FALSE);
}

void CMainFrame::SetSpeed(int Speed)
{
	CFamiTrackerDoc *pDoc = static_cast<CFamiTrackerDoc*>(GetActiveDocument());
	int MaxSpeed = pDoc->GetSpeedSplitPoint() - 1;
	Speed = std::max(Speed, MIN_SPEED);
	Speed = std::min(Speed, MaxSpeed);
	pDoc->SetSongSpeed(m_iTrack, Speed);
	theApp.GetSoundGenerator()->ResetTempo();

	if (m_wndDialogBar.GetDlgItemInt(IDC_SPEED) != Speed)
		m_wndDialogBar.SetDlgItemInt(IDC_SPEED, Speed, FALSE);
}

void CMainFrame::SetRowCount(int Count)
{
	CFamiTrackerDoc *pDoc = static_cast<CFamiTrackerDoc*>(GetActiveDocument());
	ASSERT_VALID(pDoc);
	
	if (!pDoc->IsFileLoaded())
		return;

	Count = std::max(Count, 1);
	Count = std::min(Count, MAX_PATTERN_LENGTH);

	if (Count != pDoc->GetPatternLength(m_iTrack)) {

		CPatternAction *pAction = dynamic_cast<CPatternAction*>(GetLastAction(CPatternAction::ACT_PATTERN_LENGTH));

		if (pAction == NULL) {
			// New action
			pAction = new CPatternAction(CPatternAction::ACT_PATTERN_LENGTH);
			pAction->SetPatternLength(Count);
			AddAction(pAction);
		}
		else {
			// Update existing action
			pAction->SetPatternLength(Count);
			pAction->Update(this);
		}
	}

	if (m_wndDialogBar.GetDlgItemInt(IDC_ROWS) != Count)
		m_wndDialogBar.SetDlgItemInt(IDC_ROWS, Count, FALSE);
}

void CMainFrame::SetFrameCount(int Count)
{
	CFamiTrackerDoc *pDoc = static_cast<CFamiTrackerDoc*>(GetActiveDocument());
	ASSERT_VALID(pDoc);

	if (!pDoc->IsFileLoaded())
		return;

	Count = std::max(Count, 1);
	Count = std::min(Count, MAX_FRAMES);

	if (Count != pDoc->GetFrameCount(m_iTrack)) {

		CFrameAction *pAction = static_cast<CFrameAction*>(GetLastAction(CFrameAction::ACT_CHANGE_COUNT));

		if (pAction == NULL) {
			// New action
			pAction = new CFrameAction(CFrameAction::ACT_CHANGE_COUNT);
			pAction->SetFrameCount(Count);
			AddAction(pAction);
		}
		else {
			// Update existing action
			pAction->SetFrameCount(Count);
			pAction->Update(this);
		}
	}

	if (m_wndDialogBar.GetDlgItemInt(IDC_FRAMES) != Count)
		m_wndDialogBar.SetDlgItemInt(IDC_FRAMES, Count, FALSE);
}

void CMainFrame::UpdateControls()
{
	m_wndDialogBar.UpdateDialogControls(&m_wndDialogBar, TRUE);
}

void CMainFrame::SetFirstHighlightRow(int Rows)
{
	m_wndOctaveBar.SetDlgItemInt(IDC_HIGHLIGHT1, Rows);
}

void CMainFrame::SetSecondHighlightRow(int Rows)
{
	m_wndOctaveBar.SetDlgItemInt(IDC_HIGHLIGHT2, Rows);
}

void CMainFrame::DisplayOctave()
{
	CComboBox *pOctaveList = static_cast<CComboBox*>(m_wndOctaveBar.GetDlgItem(IDC_OCTAVE));
	CFamiTrackerView *pView	= static_cast<CFamiTrackerView*>(GetActiveView());
	pOctaveList->SetCurSel(pView->GetOctave());
}

// CMainFrame diagnostics

#ifdef _DEBUG
void CMainFrame::AssertValid() const
{
	CFrameWnd::AssertValid();
}

void CMainFrame::Dump(CDumpContext& dc) const
{
	CFrameWnd::Dump(dc);
}

#endif //_DEBUG


// CMainFrame message handlers

void CMainFrame::SetStatusText(LPCTSTR Text,...)
{
	char	Buf[512];
    va_list argp;
    
	va_start(argp, Text);
    
	if (!Text)
		return;

    vsprintf(Buf, Text, argp);

	m_wndStatusBar.SetWindowText(Buf);
}

void CMainFrame::ClearInstrumentList()
{
	// Remove all items from the instrument list
	m_pInstrumentList->DeleteAllItems();
	SetInstrumentName(_T(""));
}

void CMainFrame::NewInstrument(int ChipType)
{
	// Add new instrument to module
	CFamiTrackerDoc *pDoc = static_cast<CFamiTrackerDoc*>(GetActiveDocument());

	int Index = pDoc->AddInstrument(CFamiTrackerDoc::NEW_INST_NAME, ChipType);

	if (Index == -1) {
		// Out of slots
		AfxMessageBox(IDS_INST_LIMIT, MB_ICONERROR);
		return;
	}

	// Add to list and select
	pDoc->UpdateAllViews(NULL, UPDATE_INSTRUMENT);
	SelectInstrument(Index);
}

void CMainFrame::UpdateInstrumentList()
{
	// Rewrite the instrument list
	ClearInstrumentList();

	for (int i = 0; i < MAX_INSTRUMENTS; ++i) {
		m_pInstrumentList->InsertInstrument(i);
	}
}

void CMainFrame::SelectInstrument(int Index)
{
	// Set the selected instrument
	//
	// This might get called with non-existing instruments, in that case
	// set that instrument and clear the selection in the instrument list
	//

	CFamiTrackerDoc *pDoc = static_cast<CFamiTrackerDoc*>(GetActiveDocument());
	
	if (Index == -1)
		return;

	int ListCount = m_pInstrumentList->GetItemCount();

	// No instruments added
	if (ListCount == 0) {
		m_iInstrument = 0;
		SetInstrumentName(_T(""));
		return;
	}

	if (pDoc->IsInstrumentUsed(Index)) {
		// Select instrument in list
		m_pInstrumentList->SelectInstrument(Index);

		// Set instrument name
		TCHAR Text[CInstrument::INST_NAME_MAX];
		pDoc->GetInstrumentName(Index, Text);
		SetInstrumentName(Text);

		// Update instrument editor
		if (m_wndInstEdit.IsOpened())
			m_wndInstEdit.SetCurrentInstrument(Index);
	}
	else {
		// Remove selection
		m_pInstrumentList->SetSelectionMark(-1);
		m_pInstrumentList->SetItemState(m_iInstrument, 0, LVIS_SELECTED | LVIS_FOCUSED);
		SetInstrumentName(_T(""));
		CloseInstrumentEditor();
	}

	// Save selected instrument
	m_iInstrument = Index;
}

int CMainFrame::GetSelectedInstrument() const
{
	// Returns selected instrument
	return m_iInstrument;
}

void CMainFrame::SwapInstruments(int First, int Second)
{
	// Swap two instruments
	CFamiTrackerDoc *pDoc = static_cast<CFamiTrackerDoc*>(GetActiveDocument());

	pDoc->SwapInstruments(First, Second);
	pDoc->SetModifiedFlag();
	UpdateInstrumentList();
	pDoc->UpdateAllViews(NULL, UPDATE_PATTERN);

	SelectInstrument(Second);
}

void CMainFrame::OnNextInstrument()
{
	// Select next instrument in the list
	m_pInstrumentList->SelectNextItem();
}

void CMainFrame::OnPrevInstrument()
{
	// Select previous instrument in the list
	m_pInstrumentList->SelectPreviousItem();
}

void CMainFrame::GetInstrumentName(char *pText) const
{
	m_wndDialogBar.GetDlgItem(IDC_INSTNAME)->GetWindowText(pText, CInstrument::INST_NAME_MAX);
}

void CMainFrame::SetInstrumentName(char *pText)
{
	m_wndDialogBar.GetDlgItem(IDC_INSTNAME)->SetWindowText(pText);
}

void CMainFrame::SetIndicatorTime(int Min, int Sec, int MSec)
{
	static int LMin, LSec, LMSec;

	if (Min != LMin || Sec != LSec || MSec != LMSec) {
		LMin = Min;
		LSec = Sec;
		LMSec = MSec;
		CString String;
		String.Format(_T("%02i:%02i:%01i0"), Min, Sec, MSec);
		m_wndStatusBar.SetPaneText(6, String);
	}
}

void CMainFrame::SetIndicatorPos(int Frame, int Row)
{
	CString String;
	String.Format(_T("%02i / %02i"), Row, Frame);
	m_wndStatusBar.SetPaneText(7, String);
}

void CMainFrame::OnSize(UINT nType, int cx, int cy)
{
	CFrameWnd::OnSize(nType, cx, cy);

	if (m_wndToolBarReBar.m_hWnd == NULL)
		return;

	m_wndToolBarReBar.GetReBarCtrl().MinimizeBand(0);

	if (nType != SIZE_MINIMIZED)
		ResizeFrameWindow();
}

void CMainFrame::OnInstNameChange()
{
	CFamiTrackerDoc *pDoc = static_cast<CFamiTrackerDoc*>(GetActiveDocument());

	int SelIndex = m_pInstrumentList->GetSelectionMark();
	int SelInstIndex = m_pInstrumentList->GetInstrumentIndex(SelIndex);

	if (SelIndex == -1 || m_pInstrumentList->GetItemCount() == 0)
		return;

	if (SelInstIndex != m_iInstrument)	// Instrument selection out of sync, ignore name change
		return;

	if (!pDoc->IsInstrumentUsed(m_iInstrument))
		return;

	TCHAR Text[CInstrument::INST_NAME_MAX];
	GetInstrumentName(Text);

	// Update instrument list & document
	m_pInstrumentList->SetInstrumentName(m_iInstrument, Text);
	pDoc->SetInstrumentName(m_iInstrument, T2A(Text));
}

void CMainFrame::OnAddInstrument2A03()
{
	NewInstrument(SNDCHIP_NONE);
}

void CMainFrame::OnAddInstrumentVRC6()
{
	NewInstrument(SNDCHIP_VRC6);
}

void CMainFrame::OnAddInstrumentVRC7()
{
	NewInstrument(SNDCHIP_VRC7);
}

void CMainFrame::OnAddInstrumentFDS()
{
	NewInstrument(SNDCHIP_FDS);
}

void CMainFrame::OnAddInstrumentMMC5()
{
	NewInstrument(SNDCHIP_MMC5);
}

void CMainFrame::OnAddInstrumentN163()
{
	NewInstrument(SNDCHIP_N163);
}

void CMainFrame::OnAddInstrumentS5B()
{
	NewInstrument(SNDCHIP_S5B);
}

void CMainFrame::OnAddInstrument()
{
	// Add new instrument to module

	// Chip type depends on selected channel
	CFamiTrackerView *pView = static_cast<CFamiTrackerView*>(GetActiveView());
	int ChipType = pView->GetSelectedChipType();
	NewInstrument(ChipType);
}

void CMainFrame::OnRemoveInstrument()
{
	CFamiTrackerDoc *pDoc = static_cast<CFamiTrackerDoc*>(GetActiveDocument());

	// No instruments in list
	if (m_pInstrumentList->GetItemCount() == 0)
		return;

	int Instrument = m_iInstrument;
	int SelIndex = m_pInstrumentList->GetSelectionMark();

	ASSERT(pDoc->IsInstrumentUsed(Instrument));

	CloseInstrumentEditor();

	// Remove from document
	pDoc->RemoveInstrument(Instrument);
	pDoc->UpdateAllViews(NULL, UPDATE_INSTRUMENT);

	// Remove from list
	m_pInstrumentList->RemoveInstrument(Instrument);

	int Count = m_pInstrumentList->GetItemCount();

	// Select a new instrument
	int NewSelInst = 0;

	if (Count == 0) {
		NewSelInst = 0;
	}
	else if (Count == SelIndex) {
		NewSelInst = m_pInstrumentList->GetInstrumentIndex(SelIndex - 1);
	}
	else {
		NewSelInst = m_pInstrumentList->GetInstrumentIndex(SelIndex);
	}

	SelectInstrument(NewSelInst);
}

void CMainFrame::OnCloneInstrument() 
{
	CFamiTrackerDoc *pDoc = static_cast<CFamiTrackerDoc*>(GetActiveDocument());
	
	// No instruments in list
	if (m_pInstrumentList->GetItemCount() == 0)
		return;

	int Slot = pDoc->CloneInstrument(m_iInstrument);

	if (Slot == -1) {
		AfxMessageBox(IDS_INST_LIMIT, MB_ICONERROR);
		return;
	}

	pDoc->UpdateAllViews(NULL, UPDATE_INSTRUMENT);
	SelectInstrument(Slot);
}

void CMainFrame::OnDeepCloneInstrument()
{
	CFamiTrackerDoc *pDoc = static_cast<CFamiTrackerDoc*>(GetActiveDocument());
	
	// No instruments in list
	if (m_pInstrumentList->GetItemCount() == 0)
		return;

	int Slot = pDoc->DeepCloneInstrument(m_iInstrument);

	if (Slot == -1) {
		AfxMessageBox(IDS_INST_LIMIT, MB_ICONERROR);
		return;
	}

	m_pInstrumentList->InsertInstrument(Slot);
	SelectInstrument(Slot);
}

void CMainFrame::OnBnClickedEditInst()
{
	OpenInstrumentEditor();
}

void CMainFrame::OnEditInstrument()
{
	OpenInstrumentEditor();
}

void CMainFrame::OnLoadInstrument()
{
	// Loads an instrument from a file

	CString filter = LoadDefaultFilter(IDS_FILTER_FTI, _T(".fti"));
	CFamiTrackerDoc *pDoc = static_cast<CFamiTrackerDoc*>(GetActiveDocument());
	CFileDialog FileDialog(TRUE, _T("fti"), 0, OFN_HIDEREADONLY | OFN_ALLOWMULTISELECT, filter);

	FileDialog.m_pOFN->lpstrInitialDir = theApp.GetSettings()->GetPath(PATH_FTI);

	if (FileDialog.DoModal() == IDCANCEL)
		return;

	POSITION pos (FileDialog.GetStartPosition());

	// Load multiple files
	while (pos) {
		CString csFileName(FileDialog.GetNextPathName(pos));
		int Index = pDoc->LoadInstrument(csFileName);
		if (Index == -1)
			return;
		m_pInstrumentList->InsertInstrument(Index);
	}

	theApp.GetSettings()->SetPath(FileDialog.GetPathName(), PATH_FTI);
}

void CMainFrame::OnSaveInstrument()
{
	// Saves instrument to a file

	CFamiTrackerDoc *pDoc = static_cast<CFamiTrackerDoc*>(GetActiveDocument());
	CFamiTrackerView *pView = static_cast<CFamiTrackerView*>(GetActiveView());

	char Name[256];
	CString String;

	int Index = GetSelectedInstrument();

	if (Index == -1)
		return;

	if (!pDoc->IsInstrumentUsed(Index))
		return;

	pDoc->GetInstrumentName(Index, Name);

	// Remove bad characters
	for (char *ptr = Name; *ptr != 0; ++ptr) {
		if (*ptr == '/')
			*ptr = ' ';
	}

	CString filter = LoadDefaultFilter(IDS_FILTER_FTI, _T(".fti"));
	CFileDialog FileDialog(FALSE, _T("fti"), Name, OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT, filter);

	FileDialog.m_pOFN->lpstrInitialDir = theApp.GetSettings()->GetPath(PATH_FTI);

	if (FileDialog.DoModal() == IDCANCEL)
		return;

	pDoc->SaveInstrument(GetSelectedInstrument(), FileDialog.GetPathName());

	theApp.GetSettings()->SetPath(FileDialog.GetPathName(), PATH_FTI);

	if (m_pInstrumentFileTree)
		m_pInstrumentFileTree->Changed();
}

void CMainFrame::OnDeltaposSpeedSpin(NMHDR *pNMHDR, LRESULT *pResult)
{
	int NewSpeed = CFamiTrackerDoc::GetDoc()->GetSongSpeed(m_iTrack) - ((NMUPDOWN*)pNMHDR)->iDelta;
	SetSpeed(NewSpeed);
}

void CMainFrame::OnDeltaposTempoSpin(NMHDR *pNMHDR, LRESULT *pResult)
{
	if (m_bBPM) {
		SetBPM(static_cast<int>(CurrentBPM() + 0.5) - ((NMUPDOWN*)pNMHDR)->iDelta);
		return;
	}
	int NewTempo = CFamiTrackerDoc::GetDoc()->GetSongTempo(m_iTrack) - ((NMUPDOWN*)pNMHDR)->iDelta;
	SetTempo(NewTempo);
}

void CMainFrame::OnDeltaposRowsSpin(NMHDR *pNMHDR, LRESULT *pResult)
{
	int NewRows = CFamiTrackerDoc::GetDoc()->GetPatternLength(m_iTrack) - ((NMUPDOWN*)pNMHDR)->iDelta;
	SetRowCount(NewRows);
}

void CMainFrame::OnDeltaposFrameSpin(NMHDR *pNMHDR, LRESULT *pResult)
{
	int NewFrames = CFamiTrackerDoc::GetDoc()->GetFrameCount(m_iTrack) - ((NMUPDOWN*)pNMHDR)->iDelta;
	SetFrameCount(NewFrames);
}

void CMainFrame::OnTrackerPlay()
{
	// Play
	theApp.StartPlayer(MODE_PLAY);
}

void CMainFrame::OnTrackerPlaypattern()
{
	// Loop pattern
	theApp.StartPlayer(MODE_PLAY_REPEAT);
}

void CMainFrame::OnTrackerPlayStart()
{
	// Play from start of song
	theApp.StartPlayer(MODE_PLAY_START);
}

void CMainFrame::OnTrackerPlayCursor()
{
	// Play from cursor
	theApp.StartPlayer(MODE_PLAY_CURSOR);
}

void CMainFrame::OnTrackerTogglePlay()
{
	// Toggle playback
	theApp.TogglePlayer();
}

void CMainFrame::OnTrackerStop()
{
	// Stop playback
	theApp.StopPlayer();
}

void CMainFrame::OnTrackerKillsound()
{
	theApp.LoadSoundConfig();
	theApp.SilentEverything();
}

bool CMainFrame::CheckRepeat() const
{
	static UINT LastTime, RepeatCounter;
	UINT CurrentTime = GetTickCount();

	if ((CurrentTime - LastTime) < REPEAT_TIME) {
		if (RepeatCounter < REPEAT_DELAY)
			RepeatCounter++;
	}
	else {
		RepeatCounter = 0;
	}

	LastTime = CurrentTime;

	return RepeatCounter == REPEAT_DELAY;
}

void CMainFrame::OnBnClickedIncFrame()
{
	int Add = (CheckRepeat() ? 4 : 1);
	bool bChangeAll = ChangeAllPatterns() != 0;
	CFrameAction *pAction = new CFrameAction(bChangeAll ? CFrameAction::ACT_CHANGE_PATTERN_ALL : CFrameAction::ACT_CHANGE_PATTERN);
	pAction->SetPatternDelta(Add, bChangeAll);
	AddAction(pAction);
}

void CMainFrame::OnBnClickedDecFrame()
{
	int Remove = -(CheckRepeat() ? 4 : 1);
	bool bChangeAll = ChangeAllPatterns() != 0;
	CFrameAction *pAction = new CFrameAction(bChangeAll ? CFrameAction::ACT_CHANGE_PATTERN_ALL : CFrameAction::ACT_CHANGE_PATTERN);
	pAction->SetPatternDelta(Remove, bChangeAll);
	AddAction(pAction);
}

bool CMainFrame::ChangeAllPatterns() const
{
	return m_wndFrameControls.IsDlgButtonChecked(IDC_CHANGE_ALL) != 0;
}

void CMainFrame::OnKeyRepeat()
{
	theApp.GetSettings()->General.bKeyRepeat = (m_wndDialogBar.IsDlgButtonChecked(IDC_KEYREPEAT) == 1);
}

void CMainFrame::OnDeltaposKeyStepSpin(NMHDR *pNMHDR, LRESULT *pResult)
{
	int Pos = m_wndDialogBar.GetDlgItemInt(IDC_KEYSTEP) - ((NMUPDOWN*)pNMHDR)->iDelta;
	Pos = std::max(Pos, 0);
	Pos = std::min(Pos, MAX_PATTERN_LENGTH);
	m_wndDialogBar.SetDlgItemInt(IDC_KEYSTEP, Pos);
}

void CMainFrame::OnEnKeyStepChange()
{
	int Step = m_wndDialogBar.GetDlgItemInt(IDC_KEYSTEP);
	Step = std::max(Step, 0);
	Step = std::min(Step, MAX_PATTERN_LENGTH);
	static_cast<CFamiTrackerView*>(GetActiveView())->SetStepping(Step);
}

void CMainFrame::OnCreateNSF()
{
	CExportDialog ExportDialog(this);
	ExportDialog.DoModal();
}

void CMainFrame::OnCreateWAV()
{
	CCreateWaveDlg WaveDialog;
	WaveDialog.ShowDialog();
}

void CMainFrame::OnNextFrame()
{
	static_cast<CFamiTrackerView*>(GetActiveView())->SelectNextFrame();
}

void CMainFrame::OnPrevFrame()
{
	static_cast<CFamiTrackerView*>(GetActiveView())->SelectPrevFrame();
}

void CMainFrame::OnHelpPerformance()
{
	m_wndPerformanceDlg.Create(MAKEINTRESOURCE(IDD_PERFORMANCE), this);
	m_wndPerformanceDlg.ShowWindow(SW_SHOW);
}

void CMainFrame::OnUpdateSBInstrument(CCmdUI *pCmdUI)
{
	CString String;
	AfxFormatString1(String, ID_INDICATOR_INSTRUMENT, MakeIntString(GetSelectedInstrument(), _T("%02X")));
	pCmdUI->Enable(); 
	pCmdUI->SetText(String);
}

void CMainFrame::OnUpdateSBOctave(CCmdUI *pCmdUI)
{
	CString String;
	const int Octave = static_cast<CFamiTrackerView*>(GetActiveView())->GetOctave();
	AfxFormatString1(String, ID_INDICATOR_OCTAVE, MakeIntString(Octave));
	pCmdUI->Enable(); 
	pCmdUI->SetText(String);
}

void CMainFrame::OnUpdateSBFrequency(CCmdUI *pCmdUI)
{
	CFamiTrackerDoc *pDoc = static_cast<CFamiTrackerDoc*>(GetActiveDocument());
	int Machine = pDoc->GetMachine();
	int EngineSpeed = pDoc->GetEngineSpeed();
	CString String;

	if (EngineSpeed == 0)
		EngineSpeed = (Machine == NTSC) ? CAPU::FRAME_RATE_NTSC : CAPU::FRAME_RATE_PAL;

	String.Format(_T("%i Hz"), EngineSpeed);

	pCmdUI->Enable(); 
	pCmdUI->SetText(String);
}

void CMainFrame::OnUpdateSBTempo(CCmdUI *pCmdUI)
{
	CSoundGen *pSoundGen = theApp.GetSoundGenerator();
	if (pSoundGen && !pSoundGen->IsBackgroundTask()) {
		int Highlight = m_wndOctaveBar.GetDlgItemInt(IDC_HIGHLIGHT1);
		if (Highlight == 0)
			Highlight = 4;
		float BPM = (pSoundGen->GetTempo() * 4.0f) / float(Highlight);
		// Stopped, the song's own speed and tempo: the player's are the defaults
		// until it first plays, whatever the song has (a template's 120 BPM)
		const CFamiTrackerDoc *pDoc = static_cast<CFamiTrackerDoc*>(GetActiveDocument());
		if (!theApp.IsPlaying() && pDoc != NULL && pDoc->GetSongSpeed(m_iTrack) > 0)
			BPM = (pDoc->GetSongTempo(m_iTrack) * 6.0f / pDoc->GetSongSpeed(m_iTrack)) * 4.0f / float(Highlight);
		CString String;
		String.Format(_T("%.2f BPM"), BPM);
		pCmdUI->Enable();
		pCmdUI->SetText(String);
	}
}

void CMainFrame::OnUpdateSBChip(CCmdUI *pCmdUI)
{
	CString String;
	
	CFamiTrackerDoc *pDoc = static_cast<CFamiTrackerDoc*>(GetActiveDocument());
	int Chip = pDoc->GetExpansionChip();

	switch (Chip) {
		case SNDCHIP_NONE:
			String = _T("No expansion chip");
			break;
		case SNDCHIP_VRC6:
			String = _T("Konami VRC6");
			break;
		case SNDCHIP_MMC5:
			String = _T("Nintendo MMC5");
			break;
		case SNDCHIP_FDS:
			String = _T("Nintendo FDS");
			break;
		case SNDCHIP_VRC7:
			String = _T("Konami VRC7");
			break;
		case SNDCHIP_N163:
			String = _T("Namco 163");
			break;
		case SNDCHIP_S5B:
			String = _T("Sunsoft 5B");
			break;
		default: {
			// Several chips: "VRC6+N163+5B" (the pane is as wide as "No expansion chip"); the VRC6 Plus (Api/Vrc6Plus.h)
			// is one: "VRC6 Plus+FDS"
			const struct { int Chip; LPCTSTR pName; } CHIPS[] = {
				{ SNDCHIP_VRC6, _T("VRC6") }, { SNDCHIP_VRC7, _T("VRC7") }, { SNDCHIP_FDS, _T("FDS") },
				{ SNDCHIP_MMC5, _T("MMC5") }, { SNDCHIP_N163, _T("N163") }, { SNDCHIP_S5B, _T("5B") },
			};
			const bool Plus = pDoc->IsVrc6Plus();
			if (Plus)
				String = _T("VRC6 Plus");
			for (size_t i = 0; i < sizeof(CHIPS) / sizeof(CHIPS[0]); ++i) {
				if (Plus && (CHIPS[i].Chip == SNDCHIP_VRC6 || CHIPS[i].Chip == SNDCHIP_N163))
					continue;
				if (Chip & CHIPS[i].Chip) {
					if (!String.IsEmpty())
						String += _T("+");
					String += CHIPS[i].pName;
				}
			}
			break;
		}
	}

	pCmdUI->Enable(); 
	pCmdUI->SetText(String);
}

void CMainFrame::OnUpdateInsertFrame(CCmdUI *pCmdUI)
{
	CFamiTrackerDoc* pDoc = static_cast<CFamiTrackerDoc*>(GetActiveDocument());

	if (!pDoc->IsFileLoaded())
		return;

	pCmdUI->Enable(pDoc->GetFrameCount(m_iTrack) < MAX_FRAMES);
}

void CMainFrame::OnUpdateRemoveFrame(CCmdUI *pCmdUI)
{
	CFamiTrackerDoc* pDoc = static_cast<CFamiTrackerDoc*>(GetActiveDocument());

	if (!pDoc->IsFileLoaded())
		return;

	pCmdUI->Enable(pDoc->GetFrameCount(m_iTrack) > 1);
}

void CMainFrame::OnUpdateDuplicateFrame(CCmdUI *pCmdUI)
{
	CFamiTrackerDoc* pDoc = static_cast<CFamiTrackerDoc*>(GetActiveDocument());

	if (!pDoc->IsFileLoaded())
		return;

	pCmdUI->Enable(pDoc->GetFrameCount(m_iTrack) < MAX_FRAMES);
}

void CMainFrame::OnUpdateModuleMoveframedown(CCmdUI *pCmdUI)
{
	CFamiTrackerDoc* pDoc = static_cast<CFamiTrackerDoc*>(GetActiveDocument());
	CFamiTrackerView *pView = static_cast<CFamiTrackerView*>(GetActiveView());

	if (!pDoc->IsFileLoaded())
		return;

	pCmdUI->Enable(!(pView->GetSelectedFrame() == (pDoc->GetFrameCount(m_iTrack) - 1)));
}

void CMainFrame::OnUpdateModuleMoveframeup(CCmdUI *pCmdUI)
{
	CFamiTrackerView *pView = static_cast<CFamiTrackerView*>(GetActiveView());
	pCmdUI->Enable(pView->GetSelectedFrame() > 0);
}

void CMainFrame::OnUpdateInstrumentNew(CCmdUI *pCmdUI)
{
	pCmdUI->Enable(m_pInstrumentList->GetItemCount() < MAX_INSTRUMENTS);
}

void CMainFrame::OnUpdateInstrumentRemove(CCmdUI *pCmdUI)
{
	pCmdUI->Enable(m_pInstrumentList->GetItemCount() > 0);
}

void CMainFrame::OnUpdateInstrumentClone(CCmdUI *pCmdUI)
{
	pCmdUI->Enable(m_pInstrumentList->GetItemCount() > 0 && m_pInstrumentList->GetItemCount() < MAX_INSTRUMENTS);
}

void CMainFrame::OnUpdateInstrumentDeepClone(CCmdUI *pCmdUI)
{
	pCmdUI->Enable(m_pInstrumentList->GetItemCount() > 0 && m_pInstrumentList->GetItemCount() < MAX_INSTRUMENTS);
}

void CMainFrame::OnUpdateInstrumentLoad(CCmdUI *pCmdUI)
{
	pCmdUI->Enable(m_pInstrumentList->GetItemCount() < MAX_INSTRUMENTS);
}

void CMainFrame::OnUpdateInstrumentSave(CCmdUI *pCmdUI)
{
	pCmdUI->Enable(m_pInstrumentList->GetItemCount() > 0);
}

void CMainFrame::OnUpdateInstrumentEdit(CCmdUI *pCmdUI)
{
	pCmdUI->Enable(m_pInstrumentList->GetItemCount() > 0);
}

void CMainFrame::OnTimer(UINT nIDEvent)
{
	CString text, str;
	switch (nIDEvent) {
		// Welcome message
		case TMR_WELCOME:
			// Not before this frame is the program's window: while the first document opens, a message
			// about it (a file that can't be opened) runs the timer, and the panels would find no view
			if (AfxGetApp()->m_pMainWnd != this)
				break;
			str.Format(_T("%i.%i.%i"), VERSION_MAJ, VERSION_MIN, VERSION_REV);
			AfxFormatString1(text, IDS_WELCOME_VER_FORMAT, str);
			SetMessageText(text);
			KillTimer(TMR_WELCOME);
			// The panels and views come back as they were last time: the clips panel, the problems
			// panel, the mixer and the drum grid, then the piano roll (in the pattern editor's place
			// if that was hidden), which centres on the notes in the room the others leave it
			if (CClipsBar::WasOpen())
				ShowClipsPanel(true);
			if (CProblemsBar::WasOpen())
				ShowProblemsPanel(true);
			if (CMixerBar::WasOpen())
				ShowMixer(true);
			if (CDrumGridBar::WasOpen())
				ShowDrumGrid(true);
			{
				const note_views_t Views = StartingNoteViews(theApp.GetSettings()->Views.bPatternEditor, CPianoRollBar::WasOpen());
				if (Views != NoteViews())
					ShowNoteViews(Views);
			}
			// The first time: a simpler FamiTracker for game music? Then the
			// untouched new song starts again from the template.
			if (theApp.GetSettings()->Profile.iProfile == PROFILE_UNSET && AskForProfile(this)) {
				ApplyProfile();
				CFamiTrackerDoc *pDoc = static_cast<CFamiTrackerDoc*>(GetActiveDocument());
				if (pDoc != NULL && !pDoc->IsModified() && pDoc->GetPathName().IsEmpty() && ProfileSongTemplate())
					SendMessage(WM_COMMAND, ID_FILE_NEW);
			}
			break;
		// Check sound player
		case TMR_AUDIO_CHECK:
			CheckAudioStatus();
			break;
#ifdef AUTOSAVE
		// Auto save
		case TMR_AUTOSAVE: {
			/*
				CFamiTrackerDoc *pDoc = dynamic_cast<CFamiTrackerDoc*>(GetActiveDocument());
				if (pDoc != NULL)
					pDoc->AutoSave();
					*/
			}
			break;
#endif
	}

	CFrameWnd::OnTimer(nIDEvent);
}

void CMainFrame::OnUpdateKeyStepEdit(CCmdUI *pCmdUI)
{
	pCmdUI->SetText(MakeIntString(static_cast<CFamiTrackerView*>(GetActiveView())->GetStepping()));
}

void CMainFrame::OnUpdateSpeedEdit(CCmdUI *pCmdUI)
{
	if (!m_pLockedEditSpeed->IsEditable()) {
		if (m_pLockedEditSpeed->Update())
			SetSpeed(m_pLockedEditSpeed->GetValue());
		else {
			pCmdUI->SetText(MakeIntString(static_cast<CFamiTrackerDoc*>(GetActiveDocument())->GetSongSpeed(m_iTrack)));
		}
	}	
}

void CMainFrame::OnUpdateTempoEdit(CCmdUI *pCmdUI)
{
	if (!m_pLockedEditTempo->IsEditable()) {
		if (m_pLockedEditTempo->Update()) {
			if (m_bBPM)
				SetBPM(m_pLockedEditTempo->GetValue());
			else
				SetTempo(m_pLockedEditTempo->GetValue());
		}
		else if (m_bBPM)
			pCmdUI->SetText(BPMText());
		else {
			pCmdUI->SetText(MakeIntString(static_cast<CFamiTrackerDoc*>(GetActiveDocument())->GetSongTempo(m_iTrack)));
		}
	}
}

void CMainFrame::OnUpdateRowsEdit(CCmdUI *pCmdUI)
{
	if (!m_pLockedEditLength->IsEditable()) {
		if (m_pLockedEditLength->Update())
			SetRowCount(m_pLockedEditLength->GetValue());
		else {
			pCmdUI->SetText(MakeIntString(static_cast<CFamiTrackerDoc*>(GetActiveDocument())->GetPatternLength(m_iTrack)));
		}
	}
}

void CMainFrame::OnUpdateFramesEdit(CCmdUI *pCmdUI)
{
	if (!m_pLockedEditFrames->IsEditable()) {
		if (m_pLockedEditFrames->Update())
			SetFrameCount(m_pLockedEditFrames->GetValue());
		else {
			pCmdUI->SetText(MakeIntString(static_cast<CFamiTrackerDoc*>(GetActiveDocument())->GetFrameCount(m_iTrack)));
		}
	}	
}

void CMainFrame::OnFileConnectClaude()
{
	if (m_pMcpConnection != NULL)
		m_pMcpConnection->ShowDialog();
}

void CMainFrame::OnFileExportForGames()
{
	if (m_pMcpConnection != NULL)
		m_pMcpConnection->ShowGameExport();
}

void CMainFrame::OnEditVoiceEffect()
{
	if (m_pMcpConnection != NULL)
		m_pMcpConnection->ShowVoiceEffect();
}

void CMainFrame::OnInstrumentLibrary()
{
	ShowSoundBrowser(this);
}

void CMainFrame::OnEditDrumGrid()
{
	// View > Drum grid, also in the Edit menu
	ShowDrumGrid(m_pDrumGridBar == NULL || !m_pDrumGridBar->IsVisible());
}

void CMainFrame::OnUpdateEditDrumGrid(CCmdUI *pCmdUI)
{
	pCmdUI->SetCheck(m_pDrumGridBar != NULL && m_pDrumGridBar->IsVisible() ? 1 : 0);
}

// Profiles (Profile.h)

void CMainFrame::ApplyProfile()
{
	const bool Fewer = ProfileFewerMenus();
	if (Fewer != m_bFewerMenus)
		RebuildMainMenu(Fewer);
	const bool BPM = ProfileBPM();
	if (BPM != m_bBPM)
		LayoutSongSettings(BPM);
}

// The main menu again from the resource, the rarely used items left out if
// Fewer. The bar stays the same menu, which MFC keeps as the frame's own.
void CMainFrame::RebuildMainMenu(bool Fewer)
{
	CMenu *pBar = GetMenu();
	CMenu Fresh;
	if (pBar == NULL || !Fresh.LoadMenu(IDR_MAINFRAME))
		return;
#ifndef _DEBUG
	Fresh.RemoveMenu(ID_MODULE_CHANNELS, MF_BYCOMMAND);	// as InitInstance leaves it
#endif
	if (Fewer)
		RemoveAdvancedMenuItems(&Fresh);
	while (pBar->GetMenuItemCount() > 0)
		pBar->DeleteMenu(0, MF_BYPOSITION);
	const int Count = static_cast<int>(Fresh.GetMenuItemCount());
	for (int i = 0; i < Count; ++i) {
		CString Title;
		Fresh.GetMenuString(static_cast<UINT>(i), Title, MF_BYPOSITION);
		pBar->AppendMenu(MF_POPUP | MF_STRING, reinterpret_cast<UINT_PTR>(Fresh.GetSubMenu(i)->GetSafeHmenu()), Title);
	}
	while (Fresh.GetMenuItemCount() > 0)
		Fresh.RemoveMenu(0, MF_BYPOSITION);		// the submenus are the bar's now
	Fresh.DestroyMenu();
	m_bFewerMenus = Fewer;
	UpdateMenus();		// the shortcut keys
	DrawMenuBar();
}

// BPM in place of speed and tempo: the speed row hides, the tempo row shows
// the BPM, and the rows below move up into the gap
void CMainFrame::LayoutSongSettings(bool BPM)
{
	static const UINT IDS[] = {
		IDC_SONG_SETTINGS, IDC_SPEED_LABEL, IDC_SPEED, IDC_SPEED_SPIN, IDC_TEMPO_LABEL, IDC_TEMPO, IDC_TEMPO_SPIN,
		IDC_ROWS_LABEL, IDC_ROWS, IDC_ROWS_SPIN, IDC_FRAMES_LABEL, IDC_FRAMES, IDC_FRAME_SPIN
	};
	const int Count = static_cast<int>(sizeof(IDS) / sizeof(IDS[0]));
	static_assert(sizeof(IDS) / sizeof(IDS[0]) == sizeof(m_rcSongSettings) / sizeof(m_rcSongSettings[0]), "one place for each");
	for (int i = 0; i < Count; ++i)
		if (m_wndDialogBar.GetDlgItem(IDS[i]) == NULL)
			return;
	if (!m_bSongRectsKept) {
		for (int i = 0; i < Count; ++i) {
			m_wndDialogBar.GetDlgItem(IDS[i])->GetWindowRect(&m_rcSongSettings[i]);
			m_wndDialogBar.ScreenToClient(&m_rcSongSettings[i]);
		}
		m_wndDialogBar.GetDlgItemText(IDC_TEMPO_LABEL, m_strTempoLabel);
		m_bSongRectsKept = true;
	}
	const int Row = m_rcSongSettings[5].top - m_rcSongSettings[2].top;	// from the speed box to the tempo box
	for (int i = 0; i < Count; ++i) {
		CWnd *pWnd = m_wndDialogBar.GetDlgItem(IDS[i]);
		CRect Rect = m_rcSongSettings[i];
		const bool Speed = IDS[i] == IDC_SPEED_LABEL || IDS[i] == IDC_SPEED || IDS[i] == IDC_SPEED_SPIN;
		if (BPM && IDS[i] == IDC_SONG_SETTINGS)
			Rect.bottom -= Row;
		else if (BPM && !Speed)
			Rect.OffsetRect(0, -Row);
		pWnd->MoveWindow(&Rect, FALSE);
		pWnd->ShowWindow(BPM && Speed ? SW_HIDE : SW_SHOW);
	}
	m_wndDialogBar.SetDlgItemText(IDC_TEMPO_LABEL, BPM ? CString(_T("BPM")) : m_strTempoLabel);
	m_bBPM = BPM;
	const CFamiTrackerDoc *pDoc = static_cast<CFamiTrackerDoc*>(GetActiveDocument());	// none yet in OnCreate
	if (pDoc != NULL)
		m_wndDialogBar.SetDlgItemText(IDC_TEMPO, BPM ? BPMText() : MakeIntString(pDoc->GetSongTempo(m_iTrack)));
	m_wndDialogBar.RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

double CMainFrame::CurrentBPM() const
{
	const CFamiTrackerDoc *pDoc = static_cast<const CFamiTrackerDoc*>(const_cast<CMainFrame*>(this)->GetActiveDocument());
	if (pDoc == NULL)
		return 0.0;
	const unsigned int Track = static_cast<unsigned int>(m_iTrack);
	return SongBPM(static_cast<int>(pDoc->GetSongSpeed(Track)), static_cast<int>(pDoc->GetSongTempo(Track)),
		static_cast<int>(pDoc->GetFrameRate()), pDoc->GetFirstHighlight());
}

CString CMainFrame::BPMText() const
{
	const double Bpm = CurrentBPM();
	const int Whole = static_cast<int>(Bpm + 0.5);
	CString Text;
	if (Bpm - Whole > -0.05 && Bpm - Whole < 0.05)
		Text.Format(_T("%d"), Whole);
	else
		Text.Format(_T("%.1f"), Bpm);
	return Text;
}

void CMainFrame::SetBPM(int Bpm)
{
	CFamiTrackerDoc *pDoc = static_cast<CFamiTrackerDoc*>(GetActiveDocument());
	if (pDoc == NULL)
		return;
	Bpm = std::max(20, std::min(400, Bpm));
	const unsigned int Track = static_cast<unsigned int>(m_iTrack);
	int Speed = static_cast<int>(pDoc->GetSongSpeed(Track)), Tempo = static_cast<int>(pDoc->GetSongTempo(Track));
	SpeedTempoForBPM(Bpm, pDoc->GetFirstHighlight(), static_cast<int>(pDoc->GetFrameRate()), Speed, pDoc->GetSpeedSplitPoint(),
		Speed, Tempo);
	SetSpeed(Speed);
	SetTempo(Tempo);
	m_wndDialogBar.SetDlgItemText(IDC_TEMPO, BPMText());
	pDoc->SetModifiedFlag();
}

// Help > Tutorials (Tutorial.h)

void CMainFrame::OnHelpTutorials()
{
	ShowTutorials(this);
}

// Help > Example songs: a song that opens as a new one, with its walkthrough
void CMainFrame::OnHelpExample(UINT nID)
{
	OpenExampleSong(this, nID);
}

BOOL CMainFrame::OnCommand(WPARAM wParam, LPARAM lParam)
{
	// A running tutorial may be waiting for this menu item, button or shortcut
	TutorialCommand(LOWORD(wParam));
	return CFrameWnd::OnCommand(wParam, lParam);
}

bool CMainFrame::GetTutorialRect(LPCTSTR pPart, LPCTSTR pName, UINT Command, CRect &Rect)
{
	const CString Part(pPart);
	Rect.SetRectEmpty();
	CWnd *pWnd = NULL;
	if (Part == _T("window"))
		pWnd = this;
	else if (Part == _T("pattern"))
		pWnd = GetActiveView();
	else if (Part == _T("frames"))
		pWnd = m_pFrameEditor;
	else if (Part == _T("instruments"))
		pWnd = m_pInstrumentList;
	else if (Part == _T("song-settings"))
		pWnd = m_wndDialogBar.GetDlgItem(IDC_SONG_SETTINGS);
	else if (Part == _T("octave"))
		pWnd = m_wndOctaveBar.GetDlgItem(IDC_OCTAVE);
	else if (Part == _T("toolbar"))
		pWnd = &m_wndToolBar;
	else if (Part == _T("status-bar"))
		pWnd = &m_wndStatusBar;
	else if (Part == _T("clips"))
		pWnd = m_pClipsBar;
	else if (Part == _T("piano-roll"))
		pWnd = m_pPianoRoll;
	else if (Part == _T("drum-grid"))
		pWnd = m_pDrumGridBar;
	else if (Part == _T("mixer"))
		pWnd = m_pMixerBar;
	else if (Part == _T("song-info")) {
		// The title, author and copyright boxes together
		static const UINT BOXES[] = { IDC_SONG_NAME, IDC_SONG_ARTIST, IDC_SONG_COPYRIGHT };
		for (size_t i = 0; i < sizeof(BOXES) / sizeof(BOXES[0]); ++i) {
			CWnd *pBox = m_wndDialogBar.GetDlgItem(BOXES[i]);
			if (pBox == NULL || !pBox->IsWindowVisible())
				return false;
			CRect Box;
			pBox->GetWindowRect(&Box);
			Rect.UnionRect(&Rect, &Box);
		}
		return true;
	}
	else if (Part == _T("button")) {
		// On the main toolbar or the instrument toolbar
		CToolBar *const BARS[] = { &m_wndToolBar, &m_wndInstToolBar };
		for (size_t i = 0; i < sizeof(BARS) / sizeof(BARS[0]); ++i) {
			const int Index = Command != 0 ? BARS[i]->CommandToIndex(Command) : -1;
			if (Index >= 0 && BARS[i]->IsWindowVisible()) {
				BARS[i]->GetItemRect(Index, &Rect);
				BARS[i]->ClientToScreen(&Rect);
				return true;
			}
		}
		return false;
	}
	else if (Part == _T("menu")) {
		// A menu on the menu bar, by its name
		CMenu *pMenu = GetMenu();
		const int Count = pMenu != NULL ? static_cast<int>(pMenu->GetMenuItemCount()) : 0;
		for (int i = 0; i < Count; ++i) {
			CString Title;
			pMenu->GetMenuString(static_cast<UINT>(i), Title, MF_BYPOSITION);
			Title.Remove(_T('&'));
			if (Title.CompareNoCase(pName) == 0)
				return ::GetMenuItemRect(m_hWnd, pMenu->GetSafeHmenu(), static_cast<UINT>(i), &Rect) != FALSE;
		}
		return false;
	}
	if (pWnd == NULL || pWnd->GetSafeHwnd() == NULL || !pWnd->IsWindowVisible())
		return false;
	pWnd->GetWindowRect(&Rect);
	return true;
}

void CMainFrame::ShowTutorialPart(LPCTSTR pPart)
{
	// A step about a part that's hidden shows it (the pattern editor too, which View > Pattern editor hides)
	const CString Part(pPart);
	if (Part == _T("clips") && (m_pClipsBar == NULL || !m_pClipsBar->IsVisible()))
		ShowClipsPanel(true);
	else if (Part == _T("piano-roll") && (m_pPianoRoll == NULL || !m_pPianoRoll->IsVisible()))
		ShowPianoRoll(true);
	else if ((Part == _T("pattern") || Part == _T("channel")) && !m_bPatternShown)
		ShowPatternEditor(true);	// a channel's column is in the pattern editor
	else if (Part == _T("drum-grid") && (m_pDrumGridBar == NULL || !m_pDrumGridBar->IsVisible()))
		ShowDrumGrid(true);
	else if (Part == _T("mixer") && (m_pMixerBar == NULL || !m_pMixerBar->IsVisible()))
		ShowMixer(true);
}

void CMainFrame::OnViewProfile()
{
	if (ShowProfileDialog(this)) {
		ApplyProfile();
		SetMessageText(ProfileSongTemplate() ? _T("Profile changed. New songs (File > New) start from the game music template.") :
			_T("Profile changed."));
	}
}

void CMainFrame::OnViewShowEverything()
{
	CSettings *pSettings = theApp.GetSettings();
	const bool Everything = pSettings->Profile.iProfile == PROFILE_GAME_MUSIC;
	pSettings->Profile.iProfile = Everything ? PROFILE_EVERYTHING : PROFILE_GAME_MUSIC;
	ApplyProfile();
	SetMessageText(Everything ? _T("All of FamiTracker shows. Untick View > Show everything for the game music profile again.") :
		_T("The game music profile: View > Profile says what it changes."));
}

void CMainFrame::OnUpdateViewShowEverything(CCmdUI *pCmdUI)
{
	pCmdUI->SetCheck(theApp.GetSettings()->Profile.iProfile != PROFILE_GAME_MUSIC ? 1 : 0);
}

void CMainFrame::OnFileGeneralsettings()
{
	CString Title;
	GetMessageString(IDS_CONFIG_WINDOW, Title);
	CPropertySheet ConfigWindow(Title, this, 0);

	CConfigGeneral		TabGeneral;
	CConfigAppearance	TabAppearance;
	CConfigMIDI			TabMIDI;
	CConfigSound		TabSound;
	CConfigShortcuts	TabShortcuts;
	CConfigMixer		TabMixer;

	ConfigWindow.m_psh.dwFlags	&= ~PSH_HASHELP;
	TabGeneral.m_psp.dwFlags	&= ~PSP_HASHELP;
	TabAppearance.m_psp.dwFlags &= ~PSP_HASHELP;
	TabMIDI.m_psp.dwFlags		&= ~PSP_HASHELP;
	TabSound.m_psp.dwFlags		&= ~PSP_HASHELP;
	TabShortcuts.m_psp.dwFlags	&= ~PSP_HASHELP;
	TabMixer.m_psp.dwFlags		&= ~PSP_HASHELP;
	
	ConfigWindow.AddPage(&TabGeneral);
	ConfigWindow.AddPage(&TabAppearance);
	ConfigWindow.AddPage(&TabMIDI);
	ConfigWindow.AddPage(&TabSound);
	ConfigWindow.AddPage(&TabShortcuts);
	ConfigWindow.AddPage(&TabMixer);

	ConfigWindow.DoModal();
}

void CMainFrame::SetSongInfo(const char *pName, const char *pArtist, const char *pCopyright)
{
	m_wndDialogBar.SetDlgItemText(IDC_SONG_NAME, pName);
	m_wndDialogBar.SetDlgItemText(IDC_SONG_ARTIST, pArtist);
	m_wndDialogBar.SetDlgItemText(IDC_SONG_COPYRIGHT, pCopyright);
}

void CMainFrame::OnEnSongNameChange()
{
	char Text[32];
	m_wndDialogBar.GetDlgItemText(IDC_SONG_NAME, Text, 32);
	static_cast<CFamiTrackerDoc*>(GetActiveDocument())->SetSongName(Text);
}

void CMainFrame::OnEnSongArtistChange()
{
	char Text[32];
	m_wndDialogBar.GetDlgItemText(IDC_SONG_ARTIST, Text, 32);
	static_cast<CFamiTrackerDoc*>(GetActiveDocument())->SetSongArtist(Text);
}

void CMainFrame::OnEnSongCopyrightChange()
{
	char Text[32];
	m_wndDialogBar.GetDlgItemText(IDC_SONG_COPYRIGHT, Text, 32);
	static_cast<CFamiTrackerDoc*>(GetActiveDocument())->SetSongCopyright(Text);
}

void CMainFrame::ChangeNoteState(int Note)
{
	m_wndInstEdit.ChangeNoteState(Note);
}

void CMainFrame::OpenInstrumentEditor()
{
	// Bring up the instrument editor for the selected instrument
	CFamiTrackerDoc	*pDoc = static_cast<CFamiTrackerDoc*>(GetActiveDocument());

	int Instrument = GetSelectedInstrument();

	if (pDoc->IsInstrumentUsed(Instrument)) {
		if (m_wndInstEdit.IsOpened() == false) {
			m_wndInstEdit.Create(IDD_INSTRUMENT, this);
			m_wndInstEdit.SetCurrentInstrument(Instrument);
			m_wndInstEdit.ShowWindow(SW_SHOW);
		}
		else
			m_wndInstEdit.SetCurrentInstrument(Instrument);
		m_wndInstEdit.UpdateWindow();
	}
}

void CMainFrame::CloseInstrumentEditor()
{
	// Close the instrument editor, in case it was opened
	if (m_wndInstEdit.IsOpened())
		m_wndInstEdit.DestroyWindow();
}

void CMainFrame::OnUpdateKeyRepeat(CCmdUI *pCmdUI)
{
	if (theApp.GetSettings()->General.bKeyRepeat)
		pCmdUI->SetCheck(1);
	else
		pCmdUI->SetCheck(0);
}

void CMainFrame::OnFileImportText()
{
	CString fileFilter = LoadDefaultFilter(IDS_FILTER_TXT, _T(".txt"));		
	CFileDialog FileDialog(TRUE, 0, 0, OFN_HIDEREADONLY, fileFilter);

	if (GetActiveDocument()->SaveModified() == 0)
		return;

	if (FileDialog.DoModal() == IDCANCEL)
		return;

	CTextExport Exporter;
	CFamiTrackerDoc	*pDoc = static_cast<CFamiTrackerDoc*>(GetActiveDocument());

	CString sResult = Exporter.ImportFile(FileDialog.GetPathName(), pDoc);
	if (sResult.GetLength() > 0)
	{
		AfxMessageBox(sResult, MB_OK | MB_ICONEXCLAMATION);
	}

	SetSongInfo(pDoc->GetSongName(), pDoc->GetSongArtist(), pDoc->GetSongCopyright());
	pDoc->SetModifiedFlag(FALSE);
	// TODO figure out how to handle this case, call OnInitialUpdate??
	//pDoc->UpdateAllViews(NULL, CHANGED_ERASE);		// Remove
	pDoc->UpdateAllViews(NULL, UPDATE_INSTRUMENT);
	//pDoc->UpdateAllViews(NULL, UPDATE_ENTIRE);		// TODO Remove
	theApp.GetSoundGenerator()->DocumentPropertiesChanged(pDoc);
}

void CMainFrame::OnFileImportNsf()
{
	ImportNsfFile(this);
}

void CMainFrame::OnFileExportText()
{
	CFamiTrackerDoc	*pDoc = static_cast<CFamiTrackerDoc*>(GetActiveDocument());
	CString	DefFileName = pDoc->GetFileTitle();

	CString fileFilter = LoadDefaultFilter(IDS_FILTER_TXT, _T(".txt"));
	CFileDialog FileDialog(FALSE, _T(".txt"), DefFileName, OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT, fileFilter);
	FileDialog.m_pOFN->lpstrInitialDir = theApp.GetSettings()->GetPath(PATH_NSF);

	if (FileDialog.DoModal() == IDCANCEL)
		return;

	CTextExport Exporter;
	CString sResult = Exporter.ExportFile(FileDialog.GetPathName(), pDoc);
	if (sResult.GetLength() > 0)
	{
		AfxMessageBox(sResult, MB_OK | MB_ICONEXCLAMATION);
	}
}

BOOL CMainFrame::DestroyWindow()
{
	// Store window position

	CRect WinRect;
	int State = STATE_NORMAL;

	GetWindowRect(WinRect);

	if (IsZoomed()) {
		State = STATE_MAXIMIZED;
		// Ignore window position if maximized
		WinRect.top = theApp.GetSettings()->WindowPos.iTop;
		WinRect.bottom = theApp.GetSettings()->WindowPos.iBottom;
		WinRect.left = theApp.GetSettings()->WindowPos.iLeft;
		WinRect.right = theApp.GetSettings()->WindowPos.iRight;
	}

	if (IsIconic()) {
		WinRect.top = WinRect.left = 100;
		WinRect.bottom = 920;
		WinRect.right = 950;
	}

	// Save window position
	theApp.GetSettings()->SetWindowPos(WinRect.left, WinRect.top, WinRect.right, WinRect.bottom, State);

	return CFrameWnd::DestroyWindow();
}

BOOL CMainFrame::OnEraseBkgnd(CDC* pDC)
{
	return FALSE;
}

void CMainFrame::OnTrackerSwitchToInstrument()
{
	CFamiTrackerView *pView	= static_cast<CFamiTrackerView*>(GetActiveView());
	pView->SwitchToInstrument(!pView->SwitchToInstrument());
}

void CMainFrame::OnTrackerDPCM()
{
	CMenu *pMenu = GetMenu();

	if (pMenu->GetMenuState(ID_TRACKER_DPCM, MF_BYCOMMAND) == MF_CHECKED)
		pMenu->CheckMenuItem(ID_TRACKER_DPCM, MF_UNCHECKED);
	else
		pMenu->CheckMenuItem(ID_TRACKER_DPCM, MF_CHECKED);
}

void CMainFrame::OnTrackerDisplayRegisterState()
{
	CMenu *pMenu = GetMenu();

	if (pMenu->GetMenuState(ID_TRACKER_DISPLAYREGISTERSTATE, MF_BYCOMMAND) == MF_CHECKED)
		pMenu->CheckMenuItem(ID_TRACKER_DISPLAYREGISTERSTATE, MF_UNCHECKED);
	else
		pMenu->CheckMenuItem(ID_TRACKER_DISPLAYREGISTERSTATE, MF_CHECKED);
}

void CMainFrame::OnUpdateTrackerSwitchToInstrument(CCmdUI *pCmdUI)
{
	CFamiTrackerView *pView	= static_cast<CFamiTrackerView*>(GetActiveView());
	pCmdUI->SetCheck(pView->SwitchToInstrument() ? 1 : 0);
}

void CMainFrame::OnModuleModuleproperties()
{
	// Display module properties dialog
	CModulePropertiesDlg propertiesDlg;
	propertiesDlg.DoModal();
}

void CMainFrame::OnModuleChannels()
{
	CChannelsDlg channelsDlg;
	channelsDlg.DoModal();
}

void CMainFrame::OnModuleComments()
{
	CCommentsDlg commentsDlg;
	commentsDlg.DoModal();
}

void CMainFrame::UpdateTrackBox()
{
	// Fill the track box with all songs
	CComboBox		*pTrackBox	= static_cast<CComboBox*>(m_wndDialogBar.GetDlgItem(IDC_SUBTUNE));
	CFamiTrackerDoc	*pDoc		= static_cast<CFamiTrackerDoc*>(GetActiveDocument());
	CString			Text;

	ASSERT(pTrackBox != NULL);
	ASSERT(pDoc != NULL);

	pTrackBox->ResetContent();

	int Count = pDoc->GetTrackCount();

	for (int i = 0; i < Count; ++i) {
		Text.Format(_T("#%i %s"), i + 1, pDoc->GetTrackTitle(i).GetString());
		pTrackBox->AddString(Text);
	}

	if (GetSelectedTrack() > (Count - 1))
		SelectTrack(Count - 1);

	pTrackBox->SetCurSel(GetSelectedTrack());
}

void CMainFrame::OnCbnSelchangeSong()
{
	CComboBox *pTrackBox = static_cast<CComboBox*>(m_wndDialogBar.GetDlgItem(IDC_SUBTUNE));
	int Track = pTrackBox->GetCurSel();
	SelectTrack(Track);
	GetActiveView()->SetFocus();
}

void CMainFrame::OnCbnSelchangeOctave()
{
	CComboBox *pTrackBox	= static_cast<CComboBox*>(m_wndOctaveBar.GetDlgItem(IDC_OCTAVE));
	CFamiTrackerView *pView	= static_cast<CFamiTrackerView*>(GetActiveView());
	unsigned int Octave		= pTrackBox->GetCurSel();

	if (pView->GetOctave() != Octave)
		pView->SetOctave(Octave);
}

void CMainFrame::OnRemoveFocus()
{
	GetActiveView()->SetFocus();
}

void CMainFrame::OnNextSong()
{
	CFamiTrackerDoc *pDoc = static_cast<CFamiTrackerDoc*>(GetActiveDocument());
	int Tracks = pDoc->GetTrackCount();
	int Track = GetSelectedTrack();
	if (Track < (Tracks - 1))
		SelectTrack(Track + 1);
}

void CMainFrame::OnPrevSong()
{
	int Track = GetSelectedTrack();
	if (Track > 0)
		SelectTrack(Track - 1);
}

void CMainFrame::OnUpdateNextSong(CCmdUI *pCmdUI)
{
	CFamiTrackerDoc *pDoc = static_cast<CFamiTrackerDoc*>(GetActiveDocument());
	int Tracks = pDoc->GetTrackCount();

	if (GetSelectedTrack() < (Tracks - 1))
		pCmdUI->Enable(TRUE);
	else
		pCmdUI->Enable(FALSE);
}

void CMainFrame::OnUpdatePrevSong(CCmdUI *pCmdUI)
{
	if (GetSelectedTrack() > 0)
		pCmdUI->Enable(TRUE);
	else
		pCmdUI->Enable(FALSE);
}

void CMainFrame::OnClickedFollow()
{
	CFamiTrackerView *pView	= static_cast<CFamiTrackerView*>(GetActiveView());
	bool FollowMode = m_wndOctaveBar.IsDlgButtonChecked(IDC_FOLLOW) != 0;
	theApp.GetSettings()->FollowMode = FollowMode;
	pView->SetFollowMode(FollowMode);
	pView->SetFocus();
}

void CMainFrame::OnToggleFollow()
{
	m_wndOctaveBar.CheckDlgButton(IDC_FOLLOW, !m_wndOctaveBar.IsDlgButtonChecked(IDC_FOLLOW));
	OnClickedFollow();
}

void CMainFrame::OnViewControlpanel()
{
	if (m_wndControlBar.IsVisible()) {
		m_wndControlBar.ShowWindow(SW_HIDE);
	}
	else {
		m_wndControlBar.ShowWindow(SW_SHOW);
		m_wndControlBar.UpdateWindow();
	}

	RecalcLayout();
	ResizeFrameWindow();
}

void CMainFrame::OnUpdateViewControlpanel(CCmdUI *pCmdUI)
{
	pCmdUI->SetCheck(m_wndControlBar.IsVisible());
}

void CMainFrame::OnViewPianoRoll()
{
	ShowNoteViews(ToggleNoteView(NoteViews(), NOTE_VIEW_ROLL));
}

void CMainFrame::OnUpdateViewPianoRoll(CCmdUI *pCmdUI)
{
	pCmdUI->SetCheck(m_pPianoRoll != NULL && m_pPianoRoll->IsVisible() ? 1 : 0);
}

void CMainFrame::ShowPianoRoll(bool Show)
{
	ShowNoteViews(ShowNoteView(NoteViews(), NOTE_VIEW_ROLL, Show));
}

void CMainFrame::OnViewPatternEditor()
{
	ShowNoteViews(ToggleNoteView(NoteViews(), NOTE_VIEW_PATTERN));
}

void CMainFrame::OnUpdateViewPatternEditor(CCmdUI *pCmdUI)
{
	pCmdUI->SetCheck(m_bPatternShown ? 1 : 0);
}

void CMainFrame::ShowPatternEditor(bool Show)
{
	ShowNoteViews(ShowNoteView(NoteViews(), NOTE_VIEW_PATTERN, Show));
}

note_views_t CMainFrame::NoteViews() const
{
	return note_views_t(m_bPatternShown, m_pPianoRoll != NULL && m_pPianoRoll->GetSafeHwnd() != NULL && m_pPianoRoll->IsVisible());
}

// The pattern editor and the piano roll as Wanted has them (NoteViews.h). The
// one that shows goes first, so the keys always have a view to go to; while
// the pattern editor is hidden, the piano roll fills its place and takes the
// note keys (CPianoRollWnd), and the pattern editor keeps its size for when
// it comes back (CFamiTrackerView::CalcWindowRect).
void CMainFrame::ShowNoteViews(const note_views_t &Wanted)
{
	note_views_t Views = Wanted;
	// The piano roll docked under the pattern editor (see PianoRoll.h), made the first time
	if (Views.Roll && m_pPianoRoll == NULL) {
		m_pPianoRoll = new CPianoRollBar();
		if (!m_pPianoRoll->Create(this)) {
			delete m_pPianoRoll;
			m_pPianoRoll = NULL;
		}
	}
	if (m_pPianoRoll == NULL)
		Views = note_views_t(true, false);		// the notes need somewhere to go
	const note_views_t Before = NoteViews();
	CView *pView = GetActiveView();
	const bool HidePattern = !Views.Pattern && Before.Pattern, ShowPattern = Views.Pattern && !Before.Pattern;

	// Hidden first, so the pattern editor keeps its size as the piano roll takes its place
	if (HidePattern) {
		m_bPatternShown = false;
		theApp.GetSettings()->Views.bPatternEditor = false;
	}
	if (Views.Roll && !Before.Roll) {
		m_pPianoRoll->Fill(!m_bPatternShown);
		ShowControlBar(m_pPianoRoll, TRUE, FALSE);
		m_pPianoRoll->Opened(true);
	}
	if (HidePattern) {
		m_pPianoRoll->Fill(true);
		if (pView != NULL) {
			// The keys go to the piano roll
			if (::GetFocus() == pView->GetSafeHwnd() && GetPianoRollWnd() != NULL)
				GetPianoRollWnd()->SetFocus();
			pView->ShowWindow(SW_HIDE);
		}
	}
	if (ShowPattern) {
		m_bPatternShown = true;
		theApp.GetSettings()->Views.bPatternEditor = true;
		if (m_pPianoRoll != NULL)
			m_pPianoRoll->Fill(false);
		ArrangeBars();		// its place back first, at its size, then it shows
		if (pView != NULL)
			pView->ShowWindow(SW_SHOW);
	}
	if (!Views.Roll && Before.Roll) {
		ShowControlBar(m_pPianoRoll, FALSE, FALSE);
		m_pPianoRoll->Opened(false);
	}
	ArrangeBars();

	if (HidePattern)
		SetMessageText(_T("The piano roll is in the pattern editor's place: the note keys play notes in it, and type them at ")
			_T("the cursor line while editing is on (Space). View > Pattern editor brings the pattern editor back."));
	else if (ShowPattern && !Views.Roll)
		SetMessageText(_T("The pattern editor is back in the piano roll's place: one of the two always shows."));
}

// The panels docked in the main window are laid out in this order, each along
// the edge of the room the ones before it leave: the mixer, the problems panel
// and the drum grid outermost, then the piano roll under the pattern editor
// and the clips panel beside the pattern editor. While the piano roll fills
// the pattern editor's place, the clips panel goes beside the piano roll.
void CMainFrame::ArrangeBars()
{
	const bool Fill = !m_bPatternShown;
	CControlBar *const ORDER[] = {
		m_pMixerBar, m_pProblemsBar, m_pDrumGridBar,
		Fill ? static_cast<CControlBar*>(m_pClipsBar) : static_cast<CControlBar*>(m_pPianoRoll),
		Fill ? static_cast<CControlBar*>(m_pPianoRoll) : static_cast<CControlBar*>(m_pClipsBar),
	};
	for (size_t i = 0; i < sizeof(ORDER) / sizeof(ORDER[0]); ++i)
		if (ORDER[i] != NULL && ORDER[i]->GetSafeHwnd() != NULL)
			ORDER[i]->SetWindowPos(&CWnd::wndBottom, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	RecalcLayout();
}

CPianoRollWnd *CMainFrame::FocusedPianoRoll() const
{
	CPianoRollWnd *pRoll = GetPianoRollWnd();
	return pRoll != NULL && ::GetFocus() == pRoll->GetSafeHwnd() ? pRoll : NULL;
}

void CMainFrame::ShowDrumGrid(bool Show)
{
	// The noise channel's drums a bar at a time, along the bottom of the main window or its right (see
	// DrumGridPanel.h), made the first time
	if (Show && m_pDrumGridBar == NULL) {
		m_pDrumGridBar = new CDrumGridBar();
		if (!m_pDrumGridBar->Create(this)) {
			delete m_pDrumGridBar;
			m_pDrumGridBar = NULL;
			return;
		}
		ArrangeBars();
	}
	if (m_pDrumGridBar == NULL)
		return;
	ShowControlBar(m_pDrumGridBar, Show ? TRUE : FALSE, FALSE);
	m_pDrumGridBar->Opened(Show);
}

CPianoRollWnd *CMainFrame::GetPianoRollWnd() const
{
	return m_pPianoRoll != NULL && m_pPianoRoll->GetSafeHwnd() != NULL && m_pPianoRoll->IsVisible() ? m_pPianoRoll->GetRoll() : NULL;
}

void CMainFrame::OnViewClipsPanel()
{
	ShowClipsPanel(m_pClipsBar == NULL || !m_pClipsBar->IsVisible());
}

void CMainFrame::OnUpdateViewClipsPanel(CCmdUI *pCmdUI)
{
	pCmdUI->SetCheck(m_pClipsBar != NULL && m_pClipsBar->IsVisible() ? 1 : 0);
}

void CMainFrame::ShowClipsPanel(bool Show, bool Focus)
{
	// The song's clips along the right of the main window (see ClipsPanel.h), made the first time
	if (Show && m_pClipsBar == NULL) {
		m_pClipsBar = new CClipsBar();
		if (!m_pClipsBar->Create(this)) {
			delete m_pClipsBar;
			m_pClipsBar = NULL;
			return;
		}
		ArrangeBars();
	}
	if (m_pClipsBar == NULL)
		return;
	ShowControlBar(m_pClipsBar, Show ? TRUE : FALSE, FALSE);
	m_pClipsBar->Opened(Show);
	if (Show && Focus)
		m_pClipsBar->Focus();
}

void CMainFrame::OnViewProblems()
{
	ShowProblemsPanel(m_pProblemsBar == NULL || !m_pProblemsBar->IsVisible());
}

void CMainFrame::OnUpdateViewProblems(CCmdUI *pCmdUI)
{
	pCmdUI->SetCheck(m_pProblemsBar != NULL && m_pProblemsBar->IsVisible() ? 1 : 0);
}

void CMainFrame::ShowProblemsPanel(bool Show)
{
	// What check_song finds, along the bottom of the main window or its right (see ProblemsPanel.h), made the first time
	if (Show && m_pProblemsBar == NULL) {
		m_pProblemsBar = new CProblemsBar();
		if (!m_pProblemsBar->Create(this, m_pMcpConnection)) {
			delete m_pProblemsBar;
			m_pProblemsBar = NULL;
			return;
		}
		// Laid out before the piano roll and the clips panel, so it's along the window's edge, outside them
		ArrangeBars();
	}
	if (m_pProblemsBar == NULL)
		return;
	ShowControlBar(m_pProblemsBar, Show ? TRUE : FALSE, FALSE);
	m_pProblemsBar->Opened(Show);
}

void CMainFrame::OnViewMixer()
{
	ShowMixer(m_pMixerBar == NULL || !m_pMixerBar->IsVisible());
}

void CMainFrame::OnUpdateViewMixer(CCmdUI *pCmdUI)
{
	pCmdUI->SetCheck(m_pMixerBar != NULL && m_pMixerBar->IsVisible() ? 1 : 0);
}

void CMainFrame::ShowMixer(bool Show)
{
	// A strip for every channel along the bottom of the main window (see MixerPanel.h), made the first time
	if (Show && m_pMixerBar == NULL) {
		m_pMixerBar = new CMixerBar();
		if (!m_pMixerBar->Create(this)) {
			delete m_pMixerBar;
			m_pMixerBar = NULL;
			return;
		}
		// Laid out before the piano roll, the problems panel and the clips panel: along the window's bottom edge
		ArrangeBars();
	}
	if (m_pMixerBar == NULL)
		return;
	ShowControlBar(m_pMixerBar, Show ? TRUE : FALSE, FALSE);
	m_pMixerBar->Opened(Show);
}

void CMainFrame::SongChanged()
{
	if (m_pPianoRoll != NULL)
		m_pPianoRoll->SongChanged();
	if (m_pMixerBar != NULL)
		m_pMixerBar->SongChanged();
	if (m_pClipsBar != NULL)
		m_pClipsBar->SongChanged();
	if (m_pProblemsBar != NULL)
		m_pProblemsBar->SongChanged();
	if (m_pDrumGridBar != NULL)
		m_pDrumGridBar->SongChanged();
	// The frame editor shows the sections' column while the module has sections, and where the song loops
	if (m_pFrameEditor != NULL && m_pFrameEditor->GetSafeHwnd() != NULL) {
		if (m_pFrameEditor->NeedsResize())
			ResizeFrameWindow();
		if (m_pFrameEditor->ShowsSections()) {
			m_pFrameEditor->InvalidateFrameData();
			m_pFrameEditor->Invalidate();
		}
	}
}

void CMainFrame::OnUpdateHighlight(CCmdUI *pCmdUI)
{
	// TODO remove static variables
	static int LastHighlight1, LastHighlight2;
	int Highlight1, Highlight2;
	Highlight1 = m_wndOctaveBar.GetDlgItemInt(IDC_HIGHLIGHT1);
	Highlight2 = m_wndOctaveBar.GetDlgItemInt(IDC_HIGHLIGHT2);
	if (Highlight1 != LastHighlight1 || Highlight2 != LastHighlight2) {

		CFamiTrackerDoc *pDoc = static_cast<CFamiTrackerDoc*>(GetActiveDocument());

		pDoc->SetHighlight(Highlight1, Highlight2);
		pDoc->UpdateAllViews(NULL, UPDATE_HIGHLIGHT);

		LastHighlight1 = Highlight1;
		LastHighlight2 = Highlight2;
	}
}

void CMainFrame::OnSelectPatternEditor()
{
	GetActiveView()->SetFocus();
}

void CMainFrame::OnSelectFrameEditor()
{
	m_pFrameEditor->EnableInput();
}

void CMainFrame::OnModuleInsertFrame()
{
	AddAction(new CFrameAction(CFrameAction::ACT_ADD));
}

void CMainFrame::OnModuleRemoveFrame()
{
	AddAction(new CFrameAction(CFrameAction::ACT_REMOVE));
}

void CMainFrame::OnModuleDuplicateFrame()
{
	AddAction(new CFrameAction(CFrameAction::ACT_DUPLICATE));
}

void CMainFrame::OnModuleDuplicateFramePatterns()
{
	AddAction(new CFrameAction(CFrameAction::ACT_DUPLICATE_PATTERNS));
}

void CMainFrame::OnModuleMoveframedown()
{
	AddAction(new CFrameAction(CFrameAction::ACT_MOVE_DOWN));
}

void CMainFrame::OnModuleMoveframeup()
{
	AddAction(new CFrameAction(CFrameAction::ACT_MOVE_UP));
}

// UI updates

void CMainFrame::OnUpdateEditCut(CCmdUI *pCmdUI)
{
	if (FocusedPianoRoll() != NULL)
		pCmdUI->Enable(FocusedPianoRoll()->CanEditCommand(ID_EDIT_CUT) ? 1 : 0);
	else if (GetFocus() == GetActiveView())
		static_cast<CFamiTrackerView*>(GetActiveView())->OnUpdateEditCut(pCmdUI);
	else if (GetFocus() == GetFrameEditor())
		pCmdUI->Enable(TRUE);
}

void CMainFrame::OnUpdateEditCopy(CCmdUI *pCmdUI)
{
	if (FocusedPianoRoll() != NULL) {
		pCmdUI->Enable(FocusedPianoRoll()->CanEditCommand(ID_EDIT_COPY) ? 1 : 0);
		return;
	}
	CFamiTrackerView *pView	= static_cast<CFamiTrackerView*>(GetActiveView());
	pCmdUI->Enable((pView->IsSelecting() || GetFocus() == m_pFrameEditor) ? 1 : 0);
}

void CMainFrame::OnUpdateEditPaste(CCmdUI *pCmdUI)
{
	if (FocusedPianoRoll() != NULL)
		pCmdUI->Enable(FocusedPianoRoll()->CanEditCommand(ID_EDIT_PASTE) ? 1 : 0);
	else if (GetFocus() == GetActiveView())
		pCmdUI->Enable(static_cast<CFamiTrackerView*>(GetActiveView())->IsClipboardAvailable() ? 1 : 0);
	else if (GetFocus() == GetFrameEditor())
		pCmdUI->Enable(GetFrameEditor()->IsClipboardAvailable() ? 1 : 0);
}

void CMainFrame::OnUpdateEditDelete(CCmdUI *pCmdUI)
{
	if (FocusedPianoRoll() != NULL) {
		pCmdUI->Enable(FocusedPianoRoll()->CanEditCommand(ID_EDIT_DELETE) ? 1 : 0);
		return;
	}
	CFamiTrackerView *pView	= static_cast<CFamiTrackerView*>(GetActiveView());
	pCmdUI->Enable((pView->IsSelecting() || GetFocus() == m_pFrameEditor) ? 1 : 0);
}

void CMainFrame::OnHelpEffecttable()
{
	// Display effect table in help
	HtmlHelp((DWORD)_T("effect_list.htm"), HH_DISPLAY_TOPIC);
}

void CMainFrame::OnHelpFAQ()
{
	// Display FAQ in help
	HtmlHelp((DWORD)_T("faq.htm"), HH_DISPLAY_TOPIC);
}

CFrameEditor *CMainFrame::GetFrameEditor() const
{
	return m_pFrameEditor;
}

void CMainFrame::OnEditEnableMIDI()
{
	theApp.GetMIDI()->ToggleInput();
}

void CMainFrame::OnUpdateEditEnablemidi(CCmdUI *pCmdUI)
{
	pCmdUI->Enable(theApp.GetMIDI()->IsAvailable());
	pCmdUI->SetCheck(theApp.GetMIDI()->IsOpened());
}

void CMainFrame::OnShowWindow(BOOL bShow, UINT nStatus)
{
	CFrameWnd::OnShowWindow(bShow, nStatus);

	if (bShow == TRUE) {
		// Set the window state as saved in settings
		if (theApp.GetSettings()->WindowPos.iState == STATE_MAXIMIZED)
			CFrameWnd::ShowWindow(SW_MAXIMIZE);
	}
}

void CMainFrame::OnDestroy()
{
	TRACE("FrameWnd: Destroying main frame window\n");

	if (m_pMcpConnection != NULL)
		m_pMcpConnection->Stop();	// no more requests from Claude Code

	CSoundGen *pSoundGen = theApp.GetSoundGenerator();
	
	KillTimer(TMR_AUDIO_CHECK);

	// Clean up sound stuff
	if (pSoundGen && pSoundGen->IsRunning()) {
		// Remove visualizer window from sound generator
		pSoundGen->SetVisualizerWindow(NULL);
		// Kill the sound interface since the main window is being destroyed
		CEvent *pSoundEvent = new CEvent(FALSE, FALSE);
		pSoundGen->PostThreadMessage(WM_USER_CLOSE_SOUND, (WPARAM)pSoundEvent, NULL);
		// Wait for sound to close
		DWORD dwResult = ::WaitForSingleObject(pSoundEvent->m_hObject, CSoundGen::AUDIO_TIMEOUT + 1000);

		if (dwResult != WAIT_OBJECT_0) {
			// The CEvent object will leak if this happens, but the program won't crash
			TRACE0(_T("MainFrame: Error while waiting for sound to close!\n"));
		}
		else
			delete pSoundEvent;
	}

	CFrameWnd::OnDestroy();
}

int CMainFrame::GetSelectedTrack() const
{
	return m_iTrack;
}

void CMainFrame::SelectTrack(unsigned int Track)
{
	// Select a new track
	ASSERT(Track < MAX_TRACKS);

	CFamiTrackerDoc *pDoc = static_cast<CFamiTrackerDoc*>(GetActiveDocument());
	CComboBox *pTrackBox = static_cast<CComboBox*>(m_wndDialogBar.GetDlgItem(IDC_SUBTUNE));
	CFamiTrackerView *pView = static_cast<CFamiTrackerView*>(GetActiveView());
	
	m_iTrack = Track;

	pTrackBox->SetCurSel(m_iTrack);
	//pDoc->UpdateAllViews(NULL, CHANGED_TRACK);
	pView->TrackChanged(m_iTrack);
	OnUpdateFrameTitle(TRUE);
}

BOOL CMainFrame::OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* pResult)
{
    LPNMTOOLBAR lpnmtb = (LPNMTOOLBAR) lParam;
	
	// Handle new instrument menu
	switch (((LPNMHDR)lParam)->code) {
		case TBN_DROPDOWN:
			switch (lpnmtb->iItem) {
				case ID_INSTRUMENT_NEW:
					OnNewInstrumentMenu((LPNMHDR)lParam, pResult);
					return FALSE;
				case ID_INSTRUMENT_LOAD:
					OnLoadInstrumentMenu((LPNMHDR)lParam, pResult);
					return FALSE;
			}
	}

	return CFrameWnd::OnNotify(wParam, lParam, pResult);
}

void CMainFrame::OnNewInstrumentMenu(NMHDR* pNotifyStruct, LRESULT* result)
{
	CRect rect;
	::GetWindowRect(pNotifyStruct->hwndFrom, &rect);

	CMenu menu;
	menu.CreatePopupMenu();

	CFamiTrackerDoc *pDoc = static_cast<CFamiTrackerDoc*>(GetActiveDocument());
	CFamiTrackerView *pView = static_cast<CFamiTrackerView*>(GetActiveView());
	
	int Chip = pDoc->GetExpansionChip();
	int SelectedChip = pDoc->GetChannel(pView->GetSelectedChannel())->GetChip();	// where the cursor is located

	menu.AppendMenu(MF_STRING, ID_INSTRUMENT_ADD_2A03, _T("New 2A03 instrument"));

	if (Chip & SNDCHIP_VRC6)
		menu.AppendMenu(MF_STRING, ID_INSTRUMENT_ADD_VRC6, _T("New VRC6 instrument"));
	if (Chip & SNDCHIP_VRC7)
		menu.AppendMenu(MF_STRING, ID_INSTRUMENT_ADD_VRC7, _T("New VRC7 instrument"));
	if (Chip & SNDCHIP_FDS)
		menu.AppendMenu(MF_STRING, ID_INSTRUMENT_ADD_FDS, _T("New FDS instrument"));
	if (Chip & SNDCHIP_MMC5)
		menu.AppendMenu(MF_STRING, ID_INSTRUMENT_ADD_MMC5, _T("New MMC5 instrument"));
	if (Chip & SNDCHIP_N163)
		menu.AppendMenu(MF_STRING, ID_INSTRUMENT_ADD_N163, _T("New Namco instrument"));
	if (Chip & SNDCHIP_S5B)
		menu.AppendMenu(MF_STRING, ID_INSTRUMENT_ADD_S5B, _T("New Sunsoft instrument"));
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, ID_INSTRUMENT_LIBRARY, _T("From the sound browser..."));

	switch (SelectedChip) {
		case SNDCHIP_NONE:
			menu.SetDefaultItem(ID_INSTRUMENT_ADD_2A03);
			break;
		case SNDCHIP_VRC6:
			menu.SetDefaultItem(ID_INSTRUMENT_ADD_VRC6);
			break;
		case SNDCHIP_VRC7:
			menu.SetDefaultItem(ID_INSTRUMENT_ADD_VRC7);
			break;
		case SNDCHIP_FDS:
			menu.SetDefaultItem(ID_INSTRUMENT_ADD_FDS);
			break;
		case SNDCHIP_MMC5:
			menu.SetDefaultItem(ID_INSTRUMENT_ADD_MMC5);
			break;
		case SNDCHIP_N163:
			menu.SetDefaultItem(ID_INSTRUMENT_ADD_N163);
			break;
		case SNDCHIP_S5B:
			menu.SetDefaultItem(ID_INSTRUMENT_ADD_S5B);
			break;
	}
	
	menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, rect.left, rect.bottom, this);
}

void CMainFrame::OnLoadInstrumentMenu(NMHDR * pNotifyStruct, LRESULT * result)
{
	CRect rect;
	::GetWindowRect(pNotifyStruct->hwndFrom, &rect);

	// Build menu tree
	if (!m_pInstrumentFileTree) {
		m_pInstrumentFileTree = new CInstrumentFileTree();
		m_pInstrumentFileTree->BuildMenuTree(theApp.GetSettings()->InstrumentMenuPath);
	}
	else if (m_pInstrumentFileTree->ShouldRebuild()) {
		m_pInstrumentFileTree->BuildMenuTree(theApp.GetSettings()->InstrumentMenuPath);
	}

	UINT retValue = m_pInstrumentFileTree->GetMenu()->TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY, rect.left, rect.bottom, this);

	if (retValue == CInstrumentFileTree::MENU_BASE) {
		// Open file
		OnLoadInstrument();
	}
	else if (retValue == CInstrumentFileTree::MENU_BASE + 1) {
		// Select dir
		SelectInstrumentFolder();
	}
	else if (retValue >= CInstrumentFileTree::MENU_BASE + 2) {
		// A file
		CFamiTrackerDoc *pDoc = static_cast<CFamiTrackerDoc*>(GetActiveDocument());

		int Index = pDoc->LoadInstrument(m_pInstrumentFileTree->GetFile(retValue));

		if (Index == -1)
			return;

		m_pInstrumentList->InsertInstrument(Index);
		SelectInstrument(Index);
	}
}

void CMainFrame::SelectInstrumentFolder()
{
	BROWSEINFOA Browse;	
	LPITEMIDLIST lpID;
	char Path[MAX_PATH];
	CString Title;

	Title.LoadString(IDS_INSTRUMENT_FOLDER);
	Browse.lpszTitle	= Title;
	Browse.hwndOwner	= m_hWnd;
	Browse.pidlRoot		= NULL;
	Browse.lpfn			= NULL;
	Browse.ulFlags		= BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
	Browse.pszDisplayName = Path;

	lpID = SHBrowseForFolder(&Browse);

	if (lpID != NULL) {
		SHGetPathFromIDList(lpID, Path);
		theApp.GetSettings()->InstrumentMenuPath = Path;
		m_pInstrumentFileTree->Changed();
	}
}

BOOL CMainFrame::OnCopyData(CWnd* pWnd, COPYDATASTRUCT* pCopyDataStruct)
{
	switch (pCopyDataStruct->dwData) {
		case IPC_LOAD:
			// Load file
			if (_tcslen((LPCTSTR)pCopyDataStruct->lpData) > 0)
				theApp.OpenDocumentFile((LPCTSTR)pCopyDataStruct->lpData);
			return TRUE;
		case IPC_LOAD_PLAY:
			// Load file
			if (_tcslen((LPCTSTR)pCopyDataStruct->lpData) > 0)
				theApp.OpenDocumentFile((LPCTSTR)pCopyDataStruct->lpData);
			// and play
			if (CFamiTrackerDoc::GetDoc()->IsFileLoaded() &&
				!CFamiTrackerDoc::GetDoc()->HasLastLoadFailed())
				theApp.GetSoundGenerator()->StartPlayer(MODE_PLAY_START, 0);
			return TRUE;
	}

	return CFrameWnd::OnCopyData(pWnd, pCopyDataStruct);
}

bool CMainFrame::AddAction(CAction *pAction)
{
	ASSERT(m_pActionHandler != NULL);

	if (!pAction->SaveState(this)) {
		// Operation cancelled
		SAFE_RELEASE(pAction);
		return false;
	}

	if (m_pActionGroup != NULL)
		m_pActionGroup->Add(pAction);	// undone with the rest of its group
	else
		m_pActionHandler->Push(pAction);
	SyncClips();
	SongChanged();

	return true;
}

void CMainFrame::BeginActionGroup()
{
	if (m_iActionGroupDepth++ == 0) {
		SAFE_RELEASE(m_pActionGroup);
		m_pActionGroup = new CActionGroup();
	}
}

void CMainFrame::EndActionGroup()
{
	if (m_iActionGroupDepth <= 0 || --m_iActionGroupDepth > 0)
		return;
	CActionGroup *pGroup = m_pActionGroup;
	m_pActionGroup = NULL;
	if (pGroup != NULL && !pGroup->Empty())
		m_pActionHandler->Push(pGroup);
	else
		SAFE_RELEASE(pGroup);
}

CAction *CMainFrame::GetLastAction(int Filter) const
{
	ASSERT(m_pActionHandler != NULL);
	CAction *pAction = m_pActionHandler->GetLastAction();
	return (pAction == NULL || pAction->GetAction() != Filter) ? NULL : pAction;
}

void CMainFrame::ResetUndo()
{
	ASSERT(m_pActionHandler != NULL);

	m_pActionHandler->Clear();
}

void CMainFrame::OnEditUndo()
{
	ASSERT(m_pActionHandler != NULL);

	CAction *pAction = m_pActionHandler->PopUndo();

	if (pAction != NULL) {
		pAction->Undo(this);
		SyncClips();
		SongChanged();
	}
}

void CMainFrame::OnEditRedo()
{
	ASSERT(m_pActionHandler != NULL);

	CAction *pAction = m_pActionHandler->PopRedo();

	if (pAction != NULL) {
		pAction->Redo(this);
		SyncClips();
		SongChanged();
	}
}

void CMainFrame::SyncClips()
{
	// A linked copy of a clip that was edited becomes the clip, and the other
	// linked copies follow
	CFamiTrackerDoc *pDoc = static_cast<CFamiTrackerDoc*>(GetActiveDocument());
	if (pDoc == NULL || pDoc->GetClips() == NULL)
		return;
	if (pDoc->GetClips()->Sync(pDoc)) {
		pDoc->SetModifiedFlag();
		pDoc->UpdateAllViews(NULL, UPDATE_PATTERN);
	}
}

void CMainFrame::OnUpdateEditUndo(CCmdUI *pCmdUI)
{
	ASSERT(m_pActionHandler != NULL);

	pCmdUI->Enable(m_pActionHandler->CanUndo() ? 1 : 0);
}

void CMainFrame::OnUpdateEditRedo(CCmdUI *pCmdUI)
{
	ASSERT(m_pActionHandler != NULL);

	pCmdUI->Enable(m_pActionHandler->CanRedo() ? 1 : 0);
}

void CMainFrame::UpdateMenus()
{
	// Write new shortcuts to menus
	UpdateMenu(GetMenu());
}

void CMainFrame::UpdateMenu(CMenu *pMenu)
{
	CAccelerator *pAccel = theApp.GetAccelerator();

	const int Count = pMenu->GetMenuItemCount();		// -1 on failure
	for (int i = 0; i < Count; ++i) {
		UINT state = pMenu->GetMenuState(i, MF_BYPOSITION);
		if (state & MF_POPUP) {
			// Update sub menu
			UpdateMenu(pMenu->GetSubMenu(i));
		}
		else if ((state & MF_SEPARATOR) == 0) {
			// Change menu name
			CString shortcut;
			UINT id = pMenu->GetMenuItemID(i);

			if (pAccel->GetShortcutString(id, shortcut)) {
				CString string;
				pMenu->GetMenuString(i, string, MF_BYPOSITION);

				int tab = string.Find(_T('\t'));

				if (tab != -1) {
					string = string.Left(tab);
				}

				string += shortcut;
				pMenu->ModifyMenu(i, MF_BYPOSITION, id, string);
			}
		}
	}
}

void CMainFrame::OnEditCut()
{
	if (FocusedPianoRoll() != NULL)
		FocusedPianoRoll()->EditCommand(ID_EDIT_CUT);
	else if (GetFocus() == GetActiveView())
		static_cast<CFamiTrackerView*>(GetActiveView())->OnEditCut();
	else if (GetFocus() == GetFrameEditor())
		GetFrameEditor()->OnEditCut();
}

void CMainFrame::OnEditCopy()
{
	if (FocusedPianoRoll() != NULL)
		FocusedPianoRoll()->EditCommand(ID_EDIT_COPY);
	else if (GetFocus() == GetActiveView())
		static_cast<CFamiTrackerView*>(GetActiveView())->OnEditCopy();
	else if (GetFocus() == GetFrameEditor())
		GetFrameEditor()->OnEditCopy();
}

void CMainFrame::OnEditPaste()
{
	if (FocusedPianoRoll() != NULL)
		FocusedPianoRoll()->EditCommand(ID_EDIT_PASTE);
	else if (GetFocus() == GetActiveView())
		static_cast<CFamiTrackerView*>(GetActiveView())->OnEditPaste();
	else if (GetFocus() == GetFrameEditor())
		GetFrameEditor()->OnEditPaste();
}

void CMainFrame::OnEditDelete()
{
	if (FocusedPianoRoll() != NULL)
		FocusedPianoRoll()->EditCommand(ID_EDIT_DELETE);
	else if (GetFocus() == GetActiveView())
		static_cast<CFamiTrackerView*>(GetActiveView())->OnEditCut();
	else if (GetFocus() == GetFrameEditor())
		GetFrameEditor()->OnEditDelete();
}

void CMainFrame::OnEditSelectall()
{
	if (FocusedPianoRoll() != NULL)
		FocusedPianoRoll()->EditCommand(ID_EDIT_SELECTALL);
	else if (GetFocus() == GetActiveView())
		static_cast<CFamiTrackerView*>(GetActiveView())->OnEditSelectall();
}

void CMainFrame::OnDecayFast()
{
	// TODO add this
}

void CMainFrame::OnDecaySlow()
{
	// TODO add this
}

void CMainFrame::OnEditExpandpatterns()
{
	AddAction(new CPatternAction(CPatternAction::ACT_EXPAND_PATTERN));
}

void CMainFrame::OnEditShrinkpatterns()
{
	AddAction(new CPatternAction(CPatternAction::ACT_SHRINK_PATTERN));
}

void CMainFrame::OnEditClearPatterns()
{
	CFamiTrackerDoc *pDoc = static_cast<CFamiTrackerDoc*>(GetActiveDocument());
	int Track = GetSelectedTrack();

	if (AfxMessageBox(IDS_CLEARPATTERN, MB_OKCANCEL | MB_ICONWARNING) == IDCANCEL)
		return;

	pDoc->ClearPatterns(Track);
	pDoc->UpdateAllViews(NULL, UPDATE_PATTERN);

	ResetUndo();
}

void CMainFrame::OnEditRemoveUnusedInstruments()
{
	// Removes unused instruments in the module

	CFamiTrackerDoc *pDoc = static_cast<CFamiTrackerDoc*>(GetActiveDocument());

	if (AfxMessageBox(IDS_REMOVE_INSTRUMENTS, MB_YESNO | MB_ICONINFORMATION) == IDNO)
		return;

	// Current instrument might disappear
	CloseInstrumentEditor();

	pDoc->RemoveUnusedInstruments();

	// Update instrument list
	pDoc->UpdateAllViews(NULL, UPDATE_INSTRUMENT);
}

void CMainFrame::OnEditRemoveUnusedPatterns()
{
	// Removes unused patterns in the module

	CFamiTrackerDoc *pDoc = static_cast<CFamiTrackerDoc*>(GetActiveDocument());

	if (AfxMessageBox(IDS_REMOVE_PATTERNS, MB_YESNO | MB_ICONINFORMATION) == IDNO)
		return;

	pDoc->RemoveUnusedPatterns();
	SyncClips();	// links into the removed patterns go
	pDoc->UpdateAllViews(NULL, UPDATE_PATTERN);
}

void CMainFrame::OnEditMergeDuplicatedPatterns()
{
	AddAction(new CFrameAction(CFrameAction::ACT_MERGE_DUPLICATED_PATTERNS));
}

void CMainFrame::OnUpdateSelectionEnabled(CCmdUI *pCmdUI)
{
	CFamiTrackerView *pView	= static_cast<CFamiTrackerView*>(GetActiveView());
	pCmdUI->Enable((pView->IsSelecting()) ? 1 : 0);
}

void CMainFrame::SetFrameEditorPosition(int Position)
{
	// Change frame editor position
	m_iFrameEditorPos = Position;

	m_pFrameEditor->ShowWindow(SW_HIDE);

	switch (Position) {
		case FRAME_EDIT_POS_TOP:
			m_wndVerticalControlBar.ShowWindow(SW_HIDE);
			m_pFrameEditor->SetParent(&m_wndControlBar);
			m_wndFrameControls.SetParent(&m_wndControlBar);
			break;
		case FRAME_EDIT_POS_LEFT:
			m_wndVerticalControlBar.ShowWindow(SW_SHOW);
			m_pFrameEditor->SetParent(&m_wndVerticalControlBar);
			m_wndFrameControls.SetParent(&m_wndVerticalControlBar);
			break;
	}

	ResizeFrameWindow();

	m_pFrameEditor->ShowWindow(SW_SHOW);
	m_pFrameEditor->Invalidate();
	m_pFrameEditor->RedrawWindow();

	ResizeFrameWindow();	// This must be called twice or the editor disappears, I don't know why

	// Save to settings
	theApp.GetSettings()->FrameEditPos = Position;
}

void CMainFrame::OnFrameeditorTop()
{
	SetFrameEditorPosition(FRAME_EDIT_POS_TOP);
}

void CMainFrame::OnFrameeditorLeft()
{
	SetFrameEditorPosition(FRAME_EDIT_POS_LEFT);
}

void CMainFrame::OnUpdateFrameeditorTop(CCmdUI *pCmdUI)
{
	pCmdUI->SetCheck(m_iFrameEditorPos == FRAME_EDIT_POS_TOP);
}

void CMainFrame::OnUpdateFrameeditorLeft(CCmdUI *pCmdUI)
{
	pCmdUI->SetCheck(m_iFrameEditorPos == FRAME_EDIT_POS_LEFT);
}

void CMainFrame::OnToggleSpeed()
{
	CFamiTrackerDoc *pDoc = static_cast<CFamiTrackerDoc*>(GetActiveDocument());
	int Speed = pDoc->GetSpeedSplitPoint();
	
	if (Speed == DEFAULT_SPEED_SPLIT_POINT) 
		Speed = OLD_SPEED_SPLIT_POINT;
	else 
		Speed = DEFAULT_SPEED_SPLIT_POINT;

	pDoc->SetSpeedSplitPoint(Speed);
	theApp.GetSoundGenerator()->DocumentPropertiesChanged(pDoc);

	SetStatusText(_T("Speed/tempo split-point set to %i"), Speed);
}

void CMainFrame::OnUpdateFrameTitle(BOOL bAddToTitle)
{
	CFamiTrackerDoc *pDoc = static_cast<CFamiTrackerDoc*>(GetActiveDocument());

	CString title = pDoc->GetTitle();
	
	// Add a star (*) for unsaved documents
	if (pDoc->IsModified())
		title.Append(_T("*"));

	// Add name of subtune
	title.AppendFormat(_T(" [#%i %s]"), m_iTrack + 1, pDoc->GetTrackTitle(GetSelectedTrack()).GetString());

	UpdateFrameTitleForDocument(title);
}

LRESULT CMainFrame::OnDisplayMessageString(WPARAM wParam, LPARAM lParam)
{
	AfxMessageBox((LPCTSTR)wParam, (UINT)lParam);
	return 0;
}

LRESULT CMainFrame::OnDisplayMessageID(WPARAM wParam, LPARAM lParam)
{
	AfxMessageBox((UINT)wParam, (UINT)lParam);
	return 0;
}

void CMainFrame::CheckAudioStatus()
{
	const DWORD TIMEOUT = 2000; // Display a message for 2 seconds

	// Monitor audio playback
	// TODO remove static variables
	static BOOL DisplayedError;
	static DWORD MessageTimeout;
	
	CSoundGen *pSoundGen = theApp.GetSoundGenerator();

	if (pSoundGen == NULL) {
		// Should really never be displayed (only during debugging)
		SetMessageText(_T("Audio is not working"));
		return;
	}

	// Wait for signals from the player thread
	if (pSoundGen->GetSoundTimeout()) {
		// No events from the audio pump
		SetMessageText(IDS_SOUND_FAIL);
		DisplayedError = TRUE;
		MessageTimeout = GetTickCount() + TIMEOUT;
	}
	else if (pSoundGen->IsBufferUnderrun()) {
		// Buffer underrun
		SetMessageText(IDS_UNDERRUN_MESSAGE);
		DisplayedError = TRUE;
		MessageTimeout = GetTickCount() + TIMEOUT;
	}
	else if (pSoundGen->IsAudioClipping()) {
		// Audio is clipping
		SetMessageText(IDS_CLIPPING_MESSAGE);
		DisplayedError = TRUE;
		MessageTimeout = GetTickCount() + TIMEOUT;
	}
	else {
		if (DisplayedError == TRUE && MessageTimeout < GetTickCount()) {
			// Restore message
			SetMessageText(AFX_IDS_IDLEMESSAGE);
			DisplayedError = FALSE;
		}
	}
}

void CMainFrame::OnViewToolbar()
{
	BOOL Visible = m_wndToolBar.IsVisible();
	m_wndToolBar.ShowWindow(Visible ? SW_HIDE : SW_SHOW);
	m_wndToolBarReBar.ShowWindow(Visible ? SW_HIDE : SW_SHOW);
	RecalcLayout();
}

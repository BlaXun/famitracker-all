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

// Help > Tutorials (see Tutorial.h)

// Standard headers come before stdafx.h, whose DEBUG_NEW macro would
// otherwise rewrite the placement new inside the standard library.
#include <algorithm>
#include <string>
#include <vector>
#include "TutorialScript.h"
#include "stdafx.h"
#include "FamiTracker.h"
#include "FamiTrackerDoc.h"
#include "FamiTrackerView.h"
#include "MainFrm.h"
#include "PatternEditor.h"
#include "SoundGen.h"
#include "TrackerChannel.h"
#include "Clips.h"
#include "Api/Music.h"
#include "Tutorial.h"

namespace {

const UINT_PTR TMR_STEP = 1;			// follows the window, and watches for the step being done
const UINT STEP_MS = 100;
const DWORD SHOW_AT_LEAST_MS = 600;		// a step stays this long even when it's done already
const DWORD DONE_MS = 700;				// the frame turns green this long before the next step

// Sizes at 96 DPI (scaled with SX and SY)
const int BUBBLE_WIDTH = 340;
const int MARGIN = 12;
const int LINE_GAP = 8;
const int BUTTON_WIDTH = 64;
const int BUTTON_HEIGHT = 24;
const int BUTTON_GAP = 6;
const int SPOT_PAD = 4;					// between the part shown and its frame
const int RING = 3;						// the frame
const int GAP = 14;						// between the frame and the note

// Colours
const BYTE DIM_ALPHA = 110;				// how dark the rest of the window gets
const COLORREF RING_COLOUR = RGB(255, 190, 30);
const COLORREF DONE_COLOUR = RGB(80, 200, 90);
const COLORREF BUBBLE_BACK = RGB(255, 251, 234);
const COLORREF BUBBLE_BORDER = RGB(214, 150, 20);
const COLORREF TEXT_COLOUR = RGB(30, 32, 40);
const COLORREF TEXT_DIM = RGB(120, 110, 90);
const COLORREF BUTTON_FILL = RGB(255, 255, 255);	// (BUTTON_BACK is the Back button)
const COLORREF BUTTON_HOT = RGB(255, 233, 160);
const COLORREF BUTTON_MAIN = RGB(240, 180, 40);
const COLORREF BUTTON_BORDER = RGB(200, 170, 110);

// The commands a step can wait for or show the toolbar button of (the names
// are TutorialScript.cpp's)
struct command_id_t {
	const char *Name;
	UINT Id;
};

const command_id_t COMMAND_IDS[] = {
	{ "play", ID_TRACKER_PLAY }, { "play-from-start", ID_TRACKER_PLAY_START }, { "stop", ID_TRACKER_STOP },
	{ "edit-mode", ID_TRACKER_EDIT }, { "new", ID_FILE_NEW }, { "open", ID_FILE_OPEN }, { "save", ID_FILE_SAVE },
	{ "save-as", ID_FILE_SAVE_AS }, { "export-for-games", ID_FILE_EXPORTFORGAMES }, { "create-wav", ID_FILE_CREATEWAV },
	{ "new-instrument", ID_INSTRUMENT_NEW }, { "instrument-library", ID_INSTRUMENT_LIBRARY }, { "edit-instrument", ID_INSTRUMENT_EDIT },
	{ "drum-grid", ID_EDIT_DRUMGRID }, { "save-clip", ID_EDIT_SAVECLIP }, { "clips", ID_EDIT_CLIPS },
	{ "piano-roll", ID_VIEW_PIANOROLL }, { "clips-panel", ID_VIEW_CLIPSPANEL }, { "record", ID_EDIT_RECORD },
	{ "insert-recording", ID_EDIT_INSERTRECORDING }, { "add-backing", ID_EDIT_ADDBACKING }, { "voice-effect", ID_EDIT_VOICEEFFECT },
	{ "insert-frame", ID_MODULE_INSERTFRAME }, { "duplicate-frame", ID_MODULE_DUPLICATEFRAME },
	{ "remove-frame", ID_MODULE_REMOVEFRAME }, { "module-properties", ID_MODULE_MODULEPROPERTIES }, { "undo", ID_EDIT_UNDO },
	{ "redo", ID_EDIT_REDO }, { "profile", ID_VIEW_PROFILE }, { "connect-claude", ID_FILE_CONNECTCLAUDE },
	{ "pattern-editor", ID_VIEW_PATTERNEDITOR },
};

UINT CommandId(const std::string &Name)
{
	for (size_t i = 0; i < sizeof(COMMAND_IDS) / sizeof(COMMAND_IDS[0]); ++i)
		if (Name == COMMAND_IDS[i].Name)
			return COMMAND_IDS[i].Id;
	return 0;
}

// Help > Example songs: the songs in res/examples, in FamiTracker.exe as
// RCDATA, each opened as a new song with its walkthrough
struct example_song_t {
	UINT Command;			// its menu item
	UINT Song;				// the module
	UINT Walkthrough;		// the tutorial that walks through it
	LPCTSTR Title;			// the new song's name until it's saved
};

const example_song_t EXAMPLES[] = {
	{ ID_HELP_EXAMPLE_FIRST, IDR_EXAMPLE_FIRST_SONG, IDR_EXAMPLE_FIRST_WALK, _T("First song (example)") },
	{ ID_HELP_EXAMPLE_TRICKS, IDR_EXAMPLE_TRICKS_SONG, IDR_EXAMPLE_TRICKS_WALK, _T("NES tricks (example)") },
	{ ID_HELP_EXAMPLE_VRC6, IDR_EXAMPLE_VRC6_SONG, IDR_EXAMPLE_VRC6_WALK, _T("The VRC6 (example)") },
	{ ID_HELP_EXAMPLE_SOFT, IDR_EXAMPLE_SOFT_SONG, IDR_EXAMPLE_SOFT_WALK, _T("Soft and wide (example)") },
	{ ID_HELP_EXAMPLE_SFX, IDR_EXAMPLE_SFX_SONG, IDR_EXAMPLE_SFX_WALK, _T("Sound effects (example)") },
};

// The song's channels as a walkthrough names them (TutorialScript.h), in
// column order
std::vector<tutorial_channel_t> SongChannels(const CFamiTrackerDoc *pDoc)
{
	std::vector<tutorial_channel_t> List;
	for (unsigned int i = 0; i < pDoc->GetAvailableChannels(); ++i) {
		const CTrackerChannel *pChannel = pDoc->GetChannel(static_cast<int>(i));
		const char *pChip = "2A03";
		switch (pChannel->GetChip()) {
			case SNDCHIP_VRC6: pChip = "VRC6"; break;
			case SNDCHIP_VRC7: pChip = "VRC7"; break;
			case SNDCHIP_FDS:  pChip = "FDS"; break;
			case SNDCHIP_MMC5: pChip = "MMC5"; break;
			case SNDCHIP_N163: pChip = "N163"; break;
			case SNDCHIP_S5B:  pChip = "5B"; break;
		}
		const CStringA Name(pChannel->GetChannelName());
		List.push_back(tutorial_channel_t(std::string(static_cast<LPCSTR>(Name)), pChip, pDoc->IsVrc6PlusChannel(static_cast<int>(i))));
	}
	return List;
}

// "Pulse 1", "Pulse 1 and Triangle", "Soft 1, 2 and 3" (a name like the one
// before it but for its last word gives that word)
CString ChannelList(const std::vector<std::string> &Names)
{
	CString Text;
	for (size_t i = 0; i < Names.size(); ++i) {
		if (i > 0)
			Text += i + 1 == Names.size() ? _T(" and ") : _T(", ");
		const size_t Space = Names[i].rfind(' ');
		const bool Like = i > 0 && Space != std::string::npos && Space + 1 < Names[i].size() &&
			Names[i - 1].size() > Space && Names[i - 1].compare(0, Space + 1, Names[i], 0, Space + 1) == 0 &&
			Names[i - 1].find(' ', Space + 1) == std::string::npos;
		Text += (Like ? Names[i].substr(Space + 1) : Names[i]).c_str();
	}
	return Text;
}

// Where tutorials come from

const UINT BUILT_IN[] = { IDR_TUTORIAL_FIRST, IDR_TUTORIAL_SOUNDS, IDR_TUTORIAL_CLIPS, IDR_TUTORIAL_GAME };

struct tutorial_entry_t {
	tutorial_t Tutorial;
	CString Name;				// the title, or the file's name when it can't be read
	CString Problem;			// why it can't be read
};

bool BuiltInText(UINT Id, std::string &Text)
{
	// In FamiTracker.exe itself, whatever language.dll holds
	HINSTANCE hInstance = AfxGetInstanceHandle();
	HRSRC hResource = ::FindResource(hInstance, MAKEINTRESOURCE(Id), RT_RCDATA);
	if (hResource == NULL)
		return false;
	HGLOBAL hData = ::LoadResource(hInstance, hResource);
	const void *pData = hData != NULL ? ::LockResource(hData) : NULL;
	if (pData == NULL)
		return false;
	Text.assign(static_cast<const char*>(pData), ::SizeofResource(hInstance, hResource));
	return true;
}

CString TutorialFolder()
{
	TCHAR Path[MAX_PATH] = { 0 };
	::GetModuleFileName(NULL, Path, MAX_PATH);
	CString Folder(Path);
	return Folder.Left(Folder.ReverseFind(_T('\\')) + 1) + _T("Tutorials");
}

bool ReadText(const CString &Path, std::string &Text)
{
	CFile File;
	if (!File.Open(Path, CFile::modeRead | CFile::shareDenyWrite))
		return false;
	const ULONGLONG Length = File.GetLength();
	if (Length > 1024 * 1024)
		return false;
	Text.resize(static_cast<size_t>(Length));
	if (Length > 0 && File.Read(&Text[0], static_cast<UINT>(Length)) != Length)
		return false;
	if (Text.size() >= 3 && Text.compare(0, 3, "\xEF\xBB\xBF") == 0)
		Text.erase(0, 3);		// a UTF-8 mark
	return true;
}

std::vector<tutorial_entry_t> FindTutorials()
{
	std::vector<tutorial_entry_t> List;
	for (size_t i = 0; i < sizeof(BUILT_IN) / sizeof(BUILT_IN[0]); ++i) {
		std::string Text, Error;
		tutorial_entry_t Entry;
		if (BuiltInText(BUILT_IN[i], Text) && ParseTutorial(Text, Entry.Tutorial, Error)) {
			Entry.Name = Entry.Tutorial.Title.c_str();
			List.push_back(Entry);
		}
		ASSERT(Error.empty());
	}
	CFileFind Find;
	BOOL More = Find.FindFile(TutorialFolder() + _T("\\*.txt"));
	while (More) {
		More = Find.FindNextFile();
		if (Find.IsDirectory())
			continue;
		tutorial_entry_t Entry;
		std::string Text, Error;
		if (!ReadText(Find.GetFilePath(), Text))
			Entry.Problem = _T("The file can't be read.");
		else if (!ParseTutorial(Text, Entry.Tutorial, Error))
			Entry.Problem = Error.c_str();
		Entry.Name = Entry.Problem.IsEmpty() ? CString(Entry.Tutorial.Title.c_str()) : Find.GetFileTitle();
		List.push_back(Entry);
	}
	return List;
}

// Paragraphs with a line between them
CString Paragraphs(const std::string &Text)
{
	CString Out(Text.c_str());
	Out.Replace(_T("\n"), _T("\r\n\r\n"));
	return Out;
}

// The dimming: the window darker except for a hole where the part is, with a
// bright frame around it. Layered with per-pixel alpha, and the mouse goes
// through it.

class CTutorialOverlay : public CWnd
{
public:
	CTutorialOverlay() : m_Colour(RING_COLOUR) {}
	bool Create(CWnd *pOwner);
	// Screen coordinates; Keep is the note, left clear whatever the windows'
	// order (some systems put a layered window over the others). True if it changed.
	bool Show(const CRect &Window, const CRect &Spot, const CRect &Keep, COLORREF Colour, CWnd *pBelow);
	void Hide();

private:
	CRect m_Window, m_Spot, m_Keep;
	COLORREF m_Colour;
};

bool CTutorialOverlay::Create(CWnd *pOwner)
{
	const CString Class = AfxRegisterWndClass(0, ::LoadCursor(NULL, IDC_ARROW), NULL, NULL);
	return CreateEx(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, Class, _T(""), WS_POPUP,
		CRect(0, 0, 1, 1), pOwner, 0) != FALSE;
}

bool CTutorialOverlay::Show(const CRect &Window, const CRect &Spot, const CRect &Keep, COLORREF Colour, CWnd *pBelow)
{
	if (GetSafeHwnd() == NULL || Window.IsRectEmpty())
		return false;
	if (Window == m_Window && Spot == m_Spot && Keep == m_Keep && Colour == m_Colour && IsWindowVisible())
		return false;
	m_Window = Window;
	m_Spot = Spot;
	m_Keep = Keep;
	m_Colour = Colour;

	const int Width = Window.Width(), Height = Window.Height();
	BITMAPINFO Info;
	memset(&Info, 0, sizeof(Info));
	Info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	Info.bmiHeader.biWidth = Width;
	Info.bmiHeader.biHeight = -Height;		// top down
	Info.bmiHeader.biPlanes = 1;
	Info.bmiHeader.biBitCount = 32;
	Info.bmiHeader.biCompression = BI_RGB;
	HDC hScreen = ::GetDC(NULL);
	HDC hMemory = ::CreateCompatibleDC(hScreen);
	void *pBits = NULL;
	HBITMAP hBitmap = ::CreateDIBSection(hScreen, &Info, DIB_RGB_COLORS, &pBits, NULL, 0);
	if (hBitmap == NULL || pBits == NULL) {
		::DeleteDC(hMemory);
		::ReleaseDC(NULL, hScreen);
		return false;
	}
	HGDIOBJ hOld = ::SelectObject(hMemory, hBitmap);

	// Premultiplied BGRA: black at DIM_ALPHA, nothing in the hole, the frame opaque
	CRect Hole = Spot - Window.TopLeft();
	Hole.InflateRect(SX(SPOT_PAD), SY(SPOT_PAD));
	CRect Frame = Hole;
	Frame.InflateRect(SX(RING), SY(RING));
	const DWORD Dim = static_cast<DWORD>(DIM_ALPHA) << 24;
	const DWORD Ring = 0xFF000000u | (static_cast<DWORD>(GetRValue(Colour)) << 16) | (static_cast<DWORD>(GetGValue(Colour)) << 8) |
		GetBValue(Colour);
	const CRect Clear = Keep.IsRectEmpty() ? CRect() : Keep - Window.TopLeft();
	DWORD *pPixels = static_cast<DWORD*>(pBits);
	for (int y = 0; y < Height; ++y) {
		DWORD *pRow = pPixels + static_cast<size_t>(y) * static_cast<size_t>(Width);
		std::fill(pRow, pRow + Width, Dim);
		if (y >= Frame.top && y < Frame.bottom) {
			const int Left = std::max(0, static_cast<int>(Frame.left)), Right = std::min(Width, static_cast<int>(Frame.right));
			const bool InHole = y >= Hole.top && y < Hole.bottom;
			for (int x = Left; x < Right; ++x)
				pRow[x] = (InHole && x >= Hole.left && x < Hole.right) ? 0 : Ring;
		}
		if (y >= Clear.top && y < Clear.bottom) {
			const int Left = std::max(0, static_cast<int>(Clear.left)), Right = std::min(Width, static_cast<int>(Clear.right));
			for (int x = Left; x < Right; ++x)
				pRow[x] = 0;
		}
	}

	POINT Destination = { Window.left, Window.top };
	SIZE Size = { Width, Height };
	POINT Source = { 0, 0 };
	BLENDFUNCTION Blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
	::UpdateLayeredWindow(m_hWnd, hScreen, &Destination, &Size, hMemory, &Source, 0, &Blend, ULW_ALPHA);
	::SelectObject(hMemory, hOld);
	::DeleteObject(hBitmap);
	::DeleteDC(hMemory);
	::ReleaseDC(NULL, hScreen);

	if (!IsWindowVisible())
		ShowWindow(SW_SHOWNOACTIVATE);
	if (pBelow != NULL)
		SetWindowPos(pBelow, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);	// under the note
	return true;
}

void CTutorialOverlay::Hide()
{
	if (GetSafeHwnd() != NULL && IsWindowVisible())
		ShowWindow(SW_HIDE);
	m_Window.SetRectEmpty();
}

}	// namespace

// The note: what to do, with Back, Next (Skip while waiting) and Close. It
// never takes the keyboard away from FamiTracker, and can be dragged aside.

class CTutorialBubble : public CWnd
{
public:
	// Example: a walkthrough of the example song that's open, which ends when another song comes
	CTutorialBubble(CMainFrame *pMainFrame, const tutorial_t &Tutorial, bool Example);
	bool Create();
	void Command(UINT nID);
	void Stop();

protected:
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC *pDC);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg LRESULT OnNcHitTest(CPoint point);
	afx_msg int OnMouseActivate(CWnd *pDesktopWnd, UINT nHitTest, UINT message);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg LRESULT OnMouseLeave(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnExitSizeMove(WPARAM wParam, LPARAM lParam);
	afx_msg void OnDestroy();
	virtual void PostNcDestroy();

	DECLARE_MESSAGE_MAP()

private:
	enum { BUTTON_BACK, BUTTON_NEXT, BUTTON_CLOSE, BUTTON_PLAY, BUTTONS };	// Play: the step's bars (play)

	const tutorial_step_t &Step() const { return m_Tutorial.Steps[static_cast<size_t>(m_iStep)]; }
	bool LastStep() const { return m_iStep + 1 >= static_cast<int>(m_Tutorial.Steps.size()); }
	void Go(int Step);
	void Layout();
	void Place(const CRect &Spot);
	CWnd *OpenDialog() const;
	void KeepClear(CWnd *pDialog, bool Front);
	void Tick();
	bool Done() const;
	bool Seen(UINT nID) const;
	int NoteCount() const;
	int ButtonAt(const CPoint &Point) const;
	CString ButtonText(int Button) const;
	bool HasButton(int Button) const;
	CFamiTrackerDoc *Doc() const;
	// Walkthroughs: the rows of some bars, the cursor, the step's channels and bars heard
	bool BarRows(int First, int Last, int &FirstRow, int &EndRow) const;
	void MoveCursor(int Row, int Channel);
	bool ChannelRect(const std::string &Name, CRect &Rect) const;
	void BeginStepSound();
	void EndStepSound();
	void RestoreMutes();
	void PlayBars();

	CMainFrame *m_pMainFrame;
	tutorial_t m_Tutorial;
	int m_iStep;
	CTutorialOverlay m_Overlay;
	CFont m_Font, m_BoldFont, m_SmallFont;
	CSize m_Size;
	CRect m_Buttons[BUTTONS];
	int m_iHot, m_iPressed;
	bool m_bTracking;
	bool m_bMoved;					// dragged aside: it stays there for this step
	CRect m_LastSpot, m_LastWindow;
	CRect m_DialogRect;				// a dialog open in front of FamiTracker, empty for none
	bool m_bWasFront;				// FamiTracker was the active program at the last tick

	// Where the step began
	DWORD m_dwBegan, m_dwDone;		// m_dwDone: when it was found done, 0 until then
	int m_iNotes, m_iInstruments, m_iClips;
	bool m_bWasSaved;
	std::vector<UINT> m_Commands;	// used since

	// A walkthrough of an example song
	bool m_bExample;
	unsigned int m_iSong;			// the song it's about (CFamiTrackerDoc::GetSongSerial)
	std::vector<bool> m_SavedMutes;	// the channels' mutes before the step's solo or mute, empty for none
	bool m_bPlaying;				// the step's bars play (its Play button)
	bool m_bPlayStarted;			// and the sound thread has started them
	unsigned int m_iPlayStarts;		// its count of starts when Play was pressed (CSoundGen::GetPlayStarts)
	DWORD m_dwPlayPressed;
	bool m_bHeard;					// they played to their end
};

namespace {

CTutorialBubble *g_pTutorial = NULL;

}	// namespace

BEGIN_MESSAGE_MAP(CTutorialBubble, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_TIMER()
	ON_WM_NCHITTEST()
	ON_WM_MOUSEACTIVATE()
	ON_WM_MOUSEMOVE()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_MESSAGE(WM_MOUSELEAVE, OnMouseLeave)
	ON_MESSAGE(WM_EXITSIZEMOVE, OnExitSizeMove)
	ON_WM_DESTROY()
END_MESSAGE_MAP()

CTutorialBubble::CTutorialBubble(CMainFrame *pMainFrame, const tutorial_t &Tutorial, bool Example) :
	m_pMainFrame(pMainFrame),
	m_Tutorial(Tutorial),
	m_iStep(0),
	m_Size(0, 0),
	m_iHot(-1),
	m_iPressed(-1),
	m_bTracking(false),
	m_bMoved(false),
	m_bWasFront(false),
	m_dwBegan(0),
	m_dwDone(0),
	m_iNotes(0),
	m_iInstruments(0),
	m_iClips(0),
	m_bWasSaved(false),
	m_bExample(Example),
	m_iSong(0),
	m_bPlaying(false),
	m_bPlayStarted(false),
	m_iPlayStarts(0),
	m_dwPlayPressed(0),
	m_bHeard(false)
{
	const CFamiTrackerDoc *pDoc = Doc();
	if (pDoc != NULL)
		m_iSong = pDoc->GetSongSerial();
}

CFamiTrackerDoc *CTutorialBubble::Doc() const
{
	return static_cast<CFamiTrackerDoc*>(m_pMainFrame->GetActiveDocument());
}

bool CTutorialBubble::Create()
{
	LOGFONT Log;
	memset(&Log, 0, sizeof(Log));
	::GetObject(::GetStockObject(DEFAULT_GUI_FONT), sizeof(Log), &Log);
	Log.lfHeight = -SY(13);
	_tcscpy_s(Log.lfFaceName, LF_FACESIZE, _T("Segoe UI"));
	m_Font.CreateFontIndirect(&Log);
	Log.lfWeight = FW_BOLD;
	m_BoldFont.CreateFontIndirect(&Log);
	Log.lfWeight = FW_NORMAL;
	Log.lfItalic = TRUE;
	Log.lfHeight = -SY(12);
	m_SmallFont.CreateFontIndirect(&Log);

	const CString Class = AfxRegisterWndClass(CS_DROPSHADOW, ::LoadCursor(NULL, IDC_ARROW), NULL, NULL);
	if (!CreateEx(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, Class, _T("Tutorial"), WS_POPUP, CRect(0, 0, 10, 10), m_pMainFrame, 0))
		return false;
	m_Overlay.Create(m_pMainFrame);
	SetTimer(TMR_STEP, STEP_MS, NULL);
	Go(0);
	return true;
}

void CTutorialBubble::Stop()
{
	EndStepSound();		// the song plays on as the user has it
	KillTimer(TMR_STEP);
	if (m_Overlay.GetSafeHwnd() != NULL)
		m_Overlay.DestroyWindow();
	DestroyWindow();		// and PostNcDestroy deletes it
}

void CTutorialBubble::OnDestroy()
{
	KillTimer(TMR_STEP);
	if (m_Overlay.GetSafeHwnd() != NULL)
		m_Overlay.DestroyWindow();
	CWnd::OnDestroy();
}

void CTutorialBubble::PostNcDestroy()
{
	if (g_pTutorial == this)
		g_pTutorial = NULL;
	delete this;
}

void CTutorialBubble::Command(UINT nID)
{
	if (m_Commands.size() < 64)
		m_Commands.push_back(nID);
}

bool CTutorialBubble::Seen(UINT nID) const
{
	return nID != 0 && std::find(m_Commands.begin(), m_Commands.end(), nID) != m_Commands.end();
}

// Notes in the selected track, to see that some were written (only the
// frame shown, if the song is very long)
int CTutorialBubble::NoteCount() const
{
	CFamiTrackerDoc *pDoc = static_cast<CFamiTrackerDoc*>(m_pMainFrame->GetActiveDocument());
	if (pDoc == NULL || !pDoc->IsFileLoaded())
		return 0;
	const unsigned int Track = static_cast<unsigned int>(std::max(0, m_pMainFrame->GetSelectedTrack()));
	const unsigned int Frames = pDoc->GetFrameCount(Track), Channels = pDoc->GetAvailableChannels();
	const unsigned int Rows = pDoc->GetPatternLength(Track);
	unsigned int First = 0, Last = Frames;
	CFamiTrackerView *pView = CFamiTrackerView::GetView();
	if (Frames * Channels * Rows > 60000 && pView != NULL) {
		First = pView->GetSelectedFrame();
		Last = First + 1;
	}
	int Count = 0;
	for (unsigned int f = First; f < Last; ++f) {
		for (unsigned int c = 0; c < Channels; ++c) {
			for (unsigned int r = 0; r < Rows; ++r) {
				stChanNote Cell;
				pDoc->GetNoteData(Track, f, c, r, &Cell);
				if (Cell.Note >= C && Cell.Note <= B)
					++Count;
			}
		}
	}
	return Count;
}

void CTutorialBubble::Go(int Index)
{
	if (Index < 0)
		return;
	if (Index >= static_cast<int>(m_Tutorial.Steps.size())) {
		m_pMainFrame->SetMessageText(m_bExample ? _T("Walkthrough finished. Help > Example songs has the other examples.") :
			_T("Tutorial finished. Help > Tutorials has the others."));
		Stop();
		return;
	}
	EndStepSound();		// the step before: its bars stop, its channels come back
	m_iStep = Index;
	m_bMoved = false;
	m_Commands.clear();
	m_dwBegan = ::GetTickCount();
	m_dwDone = 0;
	m_LastSpot = CRect(-1, -1, -1, -1);
	m_LastWindow.SetRectEmpty();
	m_DialogRect.SetRectEmpty();		// out of a dialog's way again, at its new size

	// What the step waits for, from where things are now
	CFamiTrackerDoc *pDoc = static_cast<CFamiTrackerDoc*>(m_pMainFrame->GetActiveDocument());
	m_iNotes = NoteCount();
	m_iInstruments = pDoc != NULL ? static_cast<int>(pDoc->GetInstrumentCount()) : 0;
	m_iClips = pDoc != NULL && pDoc->GetClips() != NULL ? pDoc->GetClips()->GetCount() : 0;
	m_bWasSaved = pDoc != NULL && !pDoc->IsModified() && !pDoc->GetPathName().IsEmpty();

	// A step about the clips panel, the piano roll or the mixer opens it
	m_pMainFrame->ShowTutorialPart(CString(Step().Show.c_str()));
	// A walkthrough's step: the cursor to its bar and channel, its channels alone, its instrument
	BeginStepSound();

	Layout();
	Invalidate(FALSE);
	Tick();
}

bool CTutorialBubble::Done() const
{
	CFamiTrackerDoc *pDoc = static_cast<CFamiTrackerDoc*>(m_pMainFrame->GetActiveDocument());
	CFamiTrackerView *pView = CFamiTrackerView::GetView();
	// Counting waits for any dialog to close: the instrument library tries
	// sounds as instruments, and Save As asks for a name first
	const bool Idle = m_pMainFrame->IsWindowEnabled() != FALSE;
	switch (Step().Wait) {
	case WAIT_EDIT_MODE:
		return pView != NULL && pView->GetEditMode();
	case WAIT_EDIT_MODE_OFF:
		return pView != NULL && !pView->GetEditMode();
	case WAIT_PLAY:
		return theApp.IsPlaying();
	case WAIT_STOP:
		return !theApp.IsPlaying();
	case WAIT_NOTES:
		return Idle && NoteCount() >= m_iNotes + Step().Count;
	case WAIT_INSTRUMENT:
		return Idle && pDoc != NULL && static_cast<int>(pDoc->GetInstrumentCount()) > m_iInstruments;
	case WAIT_CLIP:
		return Idle && pDoc != NULL && pDoc->GetClips() != NULL && pDoc->GetClips()->GetCount() > m_iClips;
	case WAIT_SAVED:
		return Idle && pDoc != NULL && !pDoc->IsModified() && !pDoc->GetPathName().IsEmpty() &&
			(!m_bWasSaved || Seen(ID_FILE_SAVE) || Seen(ID_FILE_SAVE_AS));
	case WAIT_COMMAND:
		return Seen(CommandId(Step().Command));
	case WAIT_HEARD:
		return m_bHeard;
	default:
		return false;
	}
}

CString CTutorialBubble::ButtonText(int Button) const
{
	switch (Button) {
	case BUTTON_BACK: return _T("Back");
	case BUTTON_NEXT: return LastStep() ? _T("Finish") : (Step().Wait == WAIT_NEXT ? _T("Next") : _T("Skip"));
	case BUTTON_PLAY: {
		// "Play bars 5-8: Triangle alone", or Stop while they play
		if (m_bPlaying)
			return _T("Stop");
		const tutorial_step_t &Now = Step();
		CString Text;
		if (Now.PlayFirst == Now.PlayLast)
			Text.Format(_T("Play bar %d"), Now.PlayFirst);
		else
			Text.Format(_T("Play bars %d-%d"), Now.PlayFirst, Now.PlayLast);
		if (!Now.Solo.empty())
			Text += _T(": ") + ChannelList(Now.Solo) + _T(" alone");
		else if (!Now.Mute.empty())
			Text += _T(" without ") + ChannelList(Now.Mute);
		return Text;
	}
	default: return _T("Close");
	}
}

bool CTutorialBubble::HasButton(int Button) const
{
	if (Button == BUTTON_BACK)
		return m_iStep > 0;
	if (Button == BUTTON_PLAY)
		return Step().PlayFirst > 0;
	return true;
}

void CTutorialBubble::Layout()
{
	const int Width = SX(BUBBLE_WIDTH);
	const int Inner = Width - 2 * SX(MARGIN);
	CClientDC dc(this);
	CFont *pOld = dc.SelectObject(&m_BoldFont);
	const int Header = dc.GetTextExtent(_T("Ag")).cy;
	dc.SelectObject(&m_Font);
	CRect Text(0, 0, Inner, 0);
	dc.DrawText(Paragraphs(Step().Text), &Text, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
	int Height = SY(MARGIN) + Header + SY(LINE_GAP) + Text.Height();
	if (HasButton(BUTTON_PLAY)) {
		// The step's Play button, the width of the text, under it
		Height += SY(LINE_GAP);
		m_Buttons[BUTTON_PLAY] = CRect(SX(MARGIN), Height, Width - SX(MARGIN), Height + SY(BUTTON_HEIGHT));
		Height += SY(BUTTON_HEIGHT);
	}
	else
		m_Buttons[BUTTON_PLAY].SetRectEmpty();
	if (Step().Wait != WAIT_NEXT) {
		dc.SelectObject(&m_SmallFont);
		Height += SY(LINE_GAP) + dc.GetTextExtent(_T("Ag")).cy;
	}
	dc.SelectObject(pOld);
	Height += SY(LINE_GAP) + SY(BUTTON_HEIGHT) + SY(MARGIN);
	m_Size = CSize(Width, Height);

	const int Top = Height - SY(MARGIN) - SY(BUTTON_HEIGHT);
	const int ButtonWidth = SX(BUTTON_WIDTH);
	m_Buttons[BUTTON_BACK] = CRect(SX(MARGIN), Top, SX(MARGIN) + ButtonWidth, Top + SY(BUTTON_HEIGHT));
	m_Buttons[BUTTON_CLOSE] = CRect(Width - SX(MARGIN) - ButtonWidth, Top, Width - SX(MARGIN), Top + SY(BUTTON_HEIGHT));
	m_Buttons[BUTTON_NEXT] = m_Buttons[BUTTON_CLOSE] - CPoint(ButtonWidth + SX(BUTTON_GAP), 0);
	SetWindowPos(NULL, 0, 0, Width, Height, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// Next to the part shown: below it, above, right or left, wherever it fits
// on the screen without covering it; or in its bottom right corner (a big
// part like the pattern editor); or the middle of the window
void CTutorialBubble::Place(const CRect &Spot)
{
	CRect Window;
	m_pMainFrame->GetWindowRect(&Window);
	MONITORINFO Monitor;
	memset(&Monitor, 0, sizeof(Monitor));
	Monitor.cbSize = sizeof(Monitor);
	::GetMonitorInfo(::MonitorFromWindow(m_pMainFrame->GetSafeHwnd(), MONITOR_DEFAULTTONEAREST), &Monitor);
	const CRect Work(Monitor.rcWork);
	const CSize Size = m_Size;
	const auto Within = [&Work](CRect Rect) {
		if (Rect.right > Work.right)
			Rect.OffsetRect(Work.right - Rect.right, 0);
		if (Rect.bottom > Work.bottom)
			Rect.OffsetRect(0, Work.bottom - Rect.bottom);
		if (Rect.left < Work.left)
			Rect.OffsetRect(Work.left - Rect.left, 0);
		if (Rect.top < Work.top)
			Rect.OffsetRect(0, Work.top - Rect.top);
		return Rect;
	};

	CRect Place;
	if (Spot.IsRectEmpty())
		Place = CRect(CPoint(Window.CenterPoint().x - Size.cx / 2, Window.CenterPoint().y - Size.cy / 2), Size);
	else {
		CRect Frame = Spot;
		Frame.InflateRect(SX(SPOT_PAD + RING), SY(SPOT_PAD + RING));
		const int Gap = SX(GAP);
		const CRect Tries[] = {
			CRect(CPoint(Frame.left, Frame.bottom + Gap), Size),
			CRect(CPoint(Frame.left, Frame.top - Gap - Size.cy), Size),
			CRect(CPoint(Frame.right + Gap, Frame.top), Size),
			CRect(CPoint(Frame.left - Gap - Size.cx, Frame.top), Size),
		};
		bool Found = false;
		for (size_t i = 0; i < sizeof(Tries) / sizeof(Tries[0]) && !Found; ++i) {
			const CRect Moved = Within(Tries[i]);
			CRect Overlap;
			if (!Overlap.IntersectRect(&Moved, &Frame)) {
				Place = Moved;
				Found = true;
			}
		}
		if (!Found)
			Place = CRect(CPoint(Frame.right - Gap - Size.cx, Frame.bottom - Gap - Size.cy), Size);
	}
	Place = Within(Place);
	SetWindowPos(NULL, Place.left, Place.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// The dialog or message box FamiTracker has open in front of its window,
// which is disabled meanwhile
CWnd *CTutorialBubble::OpenDialog() const
{
	if (m_pMainFrame->IsWindowEnabled())
		return NULL;
	CWnd *pPopup = m_pMainFrame->GetLastActivePopup();
	if (pPopup == NULL || pPopup == m_pMainFrame || pPopup == this || !pPopup->IsWindowVisible())
		return NULL;
	return pPopup;
}

// While a dialog is open it has the attention: no dimming, and the note out
// of its way, beside it where the screen has room, or else under it
void CTutorialBubble::KeepClear(CWnd *pDialog, bool Front)
{
	m_Overlay.Hide();
	if (!IsWindowVisible())
		ShowWindow(SW_SHOWNOACTIVATE);
	CRect Dialog, Note, Overlap;
	pDialog->GetWindowRect(&Dialog);
	if (Dialog == m_DialogRect)
		return;
	m_DialogRect = Dialog;
	GetWindowRect(&Note);
	if (!Overlap.IntersectRect(&Note, &Dialog))
		return;

	if (!m_bMoved) {
		MONITORINFO Monitor;
		memset(&Monitor, 0, sizeof(Monitor));
		Monitor.cbSize = sizeof(Monitor);
		::GetMonitorInfo(::MonitorFromRect(&Dialog, MONITOR_DEFAULTTONEAREST), &Monitor);
		const CRect Work(Monitor.rcWork);
		const CSize Size = Note.Size();
		const int Gap = SX(GAP);
		const int Top = std::max(static_cast<int>(Work.top), std::min(static_cast<int>(Dialog.top), static_cast<int>(Work.bottom - Size.cy)));
		const int Left = std::max(static_cast<int>(Work.left), std::min(static_cast<int>(Dialog.left), static_cast<int>(Work.right - Size.cx)));
		const CRect Tries[] = {
			CRect(CPoint(Dialog.right + Gap, Top), Size),
			CRect(CPoint(Dialog.left - Gap - Size.cx, Top), Size),
			CRect(CPoint(Left, Dialog.bottom + Gap), Size),
			CRect(CPoint(Left, Dialog.top - Gap - Size.cy), Size),
		};
		for (size_t i = 0; i < sizeof(Tries) / sizeof(Tries[0]); ++i) {
			CRect Inside;
			Inside.IntersectRect(&Tries[i], &Work);
			if (Inside == Tries[i]) {
				SetWindowPos(&wndTop, Tries[i].left, Tries[i].top, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | (Front ? 0 : SWP_NOZORDER));
				return;
			}
		}
	}
	SetWindowPos(pDialog, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	// The dialog on top again, as it should be already (Wine only moves a
	// window up on the screen when it goes to the very top)
	if (Front)
		pDialog->SetWindowPos(&wndTop, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void CTutorialBubble::Tick()
{
	// A walkthrough is about its example song: another song ends it
	const CFamiTrackerDoc *pDoc = Doc();
	if (m_bExample && (pDoc == NULL || pDoc->GetSongSerial() != m_iSong)) {
		m_SavedMutes.clear();	// they were the example's channels
		m_bPlaying = false;
		m_pMainFrame->SetMessageText(_T("The walkthrough ended, since another song is open now. Help > Example songs opens the example again."));
		Stop();
		return;
	}
	// The step's bars: the sound thread starts them, then they're over. Its count of starts tells they started
	// even when this timer didn't run while they played: at speed 1 the sound thread posts a message a row, 60
	// a second, and a busy computer (Wine) then had no time for timers until they had stopped
	if (m_bPlaying) {
		const CSoundGen *pSound = theApp.GetSoundGenerator();
		// (the count first: the sound thread moves it once the song plays, so a song seen not playing after it has
		// moved has stopped)
		const bool Started = pSound != NULL && pSound->GetPlayStarts() != m_iPlayStarts;
		const bool Playing = theApp.IsPlaying();
		if (Playing || Started)
			m_bPlayStarted = true;
		if (!Playing && (m_bPlayStarted || ::GetTickCount() - m_dwPlayPressed > 2000)) {
			m_bPlaying = false;
			if (m_bPlayStarted && pSound != NULL && pSound->PlayRangeEnded())
				m_bHeard = true;
			Invalidate(FALSE);
		}
	}

	if (!m_pMainFrame->IsWindowVisible() || m_pMainFrame->IsIconic()) {
		if (IsWindowVisible())
			ShowWindow(SW_HIDE);
		m_Overlay.Hide();
		return;
	}

	// FamiTracker is the active program: the note may go above its other
	// windows (not above other programs)
	DWORD Process = 0;
	::GetWindowThreadProcessId(::GetForegroundWindow(), &Process);
	const bool Front = Process == ::GetCurrentProcessId();
	// Coming back to FamiTracker, or closing a dialog, raises its window,
	// and on Wine that window can then cover the note and the dimming,
	// which go back on top
	bool Raise = Front && !m_bWasFront;
	m_bWasFront = Front;

	CWnd *pDialog = OpenDialog();
	if (pDialog != NULL)
		KeepClear(pDialog, Front);
	else {
		if (!m_DialogRect.IsRectEmpty()) {
			// The dialog closed: the note goes back beside the part shown
			m_DialogRect.SetRectEmpty();
			m_LastSpot = CRect(-1, -1, -1, -1);
			Raise = true;
		}

		// The part the step shows, framed
		CRect Window, Spot;
		m_pMainFrame->GetWindowRect(&Window);
		const tutorial_step_t &Now = Step();
		const bool HasSpot = Now.Show != "window" && (Now.Show == "channel" ? ChannelRect(Now.ShowName, Spot) :
			m_pMainFrame->GetTutorialRect(CString(Now.Show.c_str()), CString(Now.ShowName.c_str()), CommandId(Now.ShowName), Spot)) &&
			!Spot.IsRectEmpty();
		if (!HasSpot)
			Spot.SetRectEmpty();
		if (!m_bMoved && (Spot != m_LastSpot || Window != m_LastWindow))
			Place(Spot);
		CRect Note;
		GetWindowRect(&Note);
		if (HasSpot) {
			if (m_Overlay.Show(Window, Spot, Note, m_dwDone != 0 ? DONE_COLOUR : RING_COLOUR, Front ? NULL : this))
				Raise = true;
		}
		else
			m_Overlay.Hide();
		m_LastSpot = Spot;
		m_LastWindow = Window;
		if (!IsWindowVisible()) {
			ShowWindow(SW_SHOWNOACTIVATE);
			Raise = true;
		}
		// The dimming over FamiTracker and the note above it, raised in that
		// order: each to the very top, which Wine needs to restack a window
		if (Raise && Front) {
			if (m_Overlay.GetSafeHwnd() != NULL && m_Overlay.IsWindowVisible())
				m_Overlay.SetWindowPos(&wndTop, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
			SetWindowPos(&wndTop, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		}
	}

	// Done: the frame turns green for a moment, then the next step
	const DWORD Time = ::GetTickCount();
	if (m_dwDone != 0) {
		if (Time - m_dwDone >= DONE_MS)
			Go(m_iStep + 1);
	}
	else if (Step().Wait != WAIT_NEXT && Time - m_dwBegan >= SHOW_AT_LEAST_MS && Done()) {
		m_dwDone = Time;
		Invalidate(FALSE);
	}
}

void CTutorialBubble::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == TMR_STEP) {
		Tick();
		return;
	}
	CWnd::OnTimer(nIDEvent);
}

void CTutorialBubble::OnPaint()
{
	CPaintDC dc(this);
	CRect Client;
	GetClientRect(&Client);
	CDC Memory;
	Memory.CreateCompatibleDC(&dc);
	CBitmap Bitmap;
	Bitmap.CreateCompatibleBitmap(&dc, std::max(1, static_cast<int>(Client.Width())), std::max(1, static_cast<int>(Client.Height())));
	CBitmap *pOldBitmap = Memory.SelectObject(&Bitmap);
	Memory.FillSolidRect(&Client, BUBBLE_BACK);
	CBrush Border(m_dwDone != 0 ? DONE_COLOUR : BUBBLE_BORDER);
	Memory.FrameRect(&Client, &Border);
	CRect Inner = Client;
	Inner.DeflateRect(1, 1);
	Memory.FrameRect(&Inner, &Border);
	Memory.SetBkMode(TRANSPARENT);

	// "Your first tune - 2 of 11", or "Done!" for a moment
	const int Width = Client.Width() - 2 * SX(MARGIN);
	int y = SY(MARGIN);
	CFont *pOldFont = Memory.SelectObject(&m_BoldFont);
	CString Header;
	if (m_dwDone != 0)
		Header = _T("Done!");
	else
		Header.Format(_T("%s  -  %d of %d"), static_cast<LPCTSTR>(CString(m_Tutorial.Title.c_str())), m_iStep + 1,
			static_cast<int>(m_Tutorial.Steps.size()));
	Memory.SetTextColor(m_dwDone != 0 ? RGB(40, 140, 50) : TEXT_DIM);
	CRect Line(SX(MARGIN), y, SX(MARGIN) + Width, y + Memory.GetTextExtent(_T("Ag")).cy);
	Memory.DrawText(Header, &Line, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
	y = Line.bottom + SY(LINE_GAP);

	Memory.SelectObject(&m_Font);
	Memory.SetTextColor(TEXT_COLOUR);
	CRect Text(SX(MARGIN), y, SX(MARGIN) + Width, y);
	const CString Body = Paragraphs(Step().Text);
	Memory.DrawText(Body, &Text, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
	Memory.DrawText(Body, &Text, DT_WORDBREAK | DT_NOPREFIX);
	y = Text.bottom + SY(LINE_GAP);
	if (HasButton(BUTTON_PLAY))
		y = m_Buttons[BUTTON_PLAY].bottom + SY(LINE_GAP);

	if (Step().Wait != WAIT_NEXT) {
		Memory.SelectObject(&m_SmallFont);
		Memory.SetTextColor(TEXT_DIM);
		CRect Note(SX(MARGIN), y, SX(MARGIN) + Width, y + Memory.GetTextExtent(_T("Ag")).cy);
		Memory.DrawText(Step().Wait == WAIT_HEARD ? _T("Press Play; it goes on by itself once they've played.") :
			_T("Do it and the tutorial goes on by itself, or press Skip."), &Note, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
	}

	// The buttons
	Memory.SelectObject(&m_Font);
	for (int i = 0; i < BUTTONS; ++i) {
		if (!HasButton(i))
			continue;
		const bool Main = (i == BUTTON_NEXT && (Step().Wait == WAIT_NEXT || LastStep())) ||
			(i == BUTTON_PLAY && Step().Wait == WAIT_HEARD && !m_bPlaying);
		COLORREF Back = Main ? BUTTON_MAIN : BUTTON_FILL;
		if (i == m_iHot)
			Back = Main ? RGB(250, 200, 70) : BUTTON_HOT;
		CBrush Brush(Back);
		CPen Pen(PS_SOLID, 1, Main ? BUBBLE_BORDER : BUTTON_BORDER);
		CBrush *pOldBrush = Memory.SelectObject(&Brush);
		CPen *pOldPen = Memory.SelectObject(&Pen);
		Memory.RoundRect(&m_Buttons[i], CPoint(SX(6), SY(6)));
		Memory.SelectObject(pOldBrush);
		Memory.SelectObject(pOldPen);
		Memory.SetTextColor(TEXT_COLOUR);
		CRect Label = m_Buttons[i];
		if (i == BUTTON_PLAY) {
			// A play triangle, or a stop square while the bars play, then the label
			const int Size = SY(8), Left = Label.left + SX(10), Middle = Label.CenterPoint().y;
			CBrush Mark(TEXT_COLOUR);
			CPen Edge(PS_SOLID, 1, TEXT_COLOUR);
			CBrush *pBrush = Memory.SelectObject(&Mark);
			CPen *pPen = Memory.SelectObject(&Edge);
			if (m_bPlaying)
				Memory.Rectangle(Left, Middle - Size / 2, Left + Size, Middle + Size / 2);
			else {
				const POINT Triangle[] = { { Left, Middle - Size / 2 }, { Left + Size, Middle }, { Left, Middle + Size / 2 } };
				Memory.Polygon(Triangle, 3);
			}
			Memory.SelectObject(pBrush);
			Memory.SelectObject(pPen);
			Label.left = Left + Size + SX(8);
			Label.right -= SX(6);
			Memory.DrawText(ButtonText(i), &Label, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
		}
		else
			Memory.DrawText(ButtonText(i), &Label, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	}

	dc.BitBlt(0, 0, Client.Width(), Client.Height(), &Memory, 0, 0, SRCCOPY);
	Memory.SelectObject(pOldFont);
	Memory.SelectObject(pOldBitmap);
}

BOOL CTutorialBubble::OnEraseBkgnd(CDC *pDC)
{
	return TRUE;
}

int CTutorialBubble::ButtonAt(const CPoint &Point) const
{
	for (int i = 0; i < BUTTONS; ++i) {
		if (!HasButton(i))
			continue;
		if (m_Buttons[i].PtInRect(Point))
			return i;
	}
	return -1;
}

LRESULT CTutorialBubble::OnNcHitTest(CPoint point)
{
	// Anywhere but the buttons drags it
	CPoint Client = point;
	ScreenToClient(&Client);
	return ButtonAt(Client) >= 0 ? HTCLIENT : HTCAPTION;
}

int CTutorialBubble::OnMouseActivate(CWnd *pDesktopWnd, UINT nHitTest, UINT message)
{
	return MA_NOACTIVATE;		// the keyboard stays with FamiTracker
}

void CTutorialBubble::OnMouseMove(UINT nFlags, CPoint point)
{
	const int Hot = ButtonAt(point);
	if (Hot != m_iHot) {
		m_iHot = Hot;
		Invalidate(FALSE);
	}
	if (!m_bTracking) {
		TRACKMOUSEEVENT Track;
		memset(&Track, 0, sizeof(Track));
		Track.cbSize = sizeof(Track);
		Track.dwFlags = TME_LEAVE;
		Track.hwndTrack = m_hWnd;
		m_bTracking = ::TrackMouseEvent(&Track) != FALSE;
	}
}

LRESULT CTutorialBubble::OnMouseLeave(WPARAM wParam, LPARAM lParam)
{
	m_bTracking = false;
	if (m_iHot >= 0) {
		m_iHot = -1;
		Invalidate(FALSE);
	}
	return 0;
}

void CTutorialBubble::OnLButtonDown(UINT nFlags, CPoint point)
{
	m_iPressed = ButtonAt(point);
}

void CTutorialBubble::OnLButtonUp(UINT nFlags, CPoint point)
{
	const int Button = ButtonAt(point);
	if (Button < 0 || Button != m_iPressed)
		return;
	m_iPressed = -1;
	switch (Button) {
	case BUTTON_BACK:
		Go(m_iStep - 1);
		break;
	case BUTTON_NEXT:
		Go(m_iStep + 1);
		break;
	case BUTTON_PLAY:
		PlayBars();
		break;
	default:
		m_pMainFrame->SetMessageText(m_bExample ? _T("Walkthrough closed. Help > Example songs opens the example again.") :
			_T("Tutorial closed. Help > Tutorials starts it again."));
		Stop();
		break;
	}
}

LRESULT CTutorialBubble::OnExitSizeMove(WPARAM wParam, LPARAM lParam)
{
	m_bMoved = true;		// dragged aside
	return 0;
}

// Walkthroughs of the example songs: bars, the cursor, channels alone

// The song rows of bars First to Last (from 1, as the piano roll's ruler
// counts them); false when the song is shorter
bool CTutorialBubble::BarRows(int First, int Last, int &FirstRow, int &EndRow) const
{
	const CFamiTrackerDoc *pDoc = Doc();
	if (pDoc == NULL || !pDoc->IsFileLoaded() || First < 1 || Last < First)
		return false;
	const unsigned int Track = static_cast<unsigned int>(std::max(0, m_pMainFrame->GetSelectedTrack()));
	const ftapi::Timing Time(static_cast<int>(pDoc->GetPatternLength(Track)), static_cast<int>(pDoc->GetFrameCount(Track)),
		pDoc->GetFirstHighlight(), pDoc->GetSecondHighlight());
	FirstRow = Time.BarStart(First);
	EndRow = std::min(Time.BarEnd(Last), Time.TotalRows());
	return FirstRow < EndRow;
}

void CTutorialBubble::MoveCursor(int Row, int Channel)
{
	const CFamiTrackerDoc *pDoc = Doc();
	CFamiTrackerView *pView = CFamiTrackerView::GetView();
	if (pDoc == NULL || pView == NULL)
		return;
	const unsigned int Track = static_cast<unsigned int>(std::max(0, m_pMainFrame->GetSelectedTrack()));
	const int Length = static_cast<int>(std::max(1U, pDoc->GetPatternLength(Track)));
	pView->SelectFrame(static_cast<unsigned int>(Row / Length));
	pView->GetPatternEditor()->MoveToRow(Row % Length);
	// (draws the cursor where it is now)
	pView->SelectChannel(static_cast<unsigned int>(Channel >= 0 ? Channel : static_cast<int>(pView->GetSelectedChannel())));
}

// A channel's column in the pattern editor, on the screen
bool CTutorialBubble::ChannelRect(const std::string &Name, CRect &Rect) const
{
	const CFamiTrackerDoc *pDoc = Doc();
	CFamiTrackerView *pView = CFamiTrackerView::GetView();
	if (pDoc == NULL || pView == NULL || !pDoc->IsFileLoaded() || !pView->IsWindowVisible())
		return false;
	const int Channel = FindTutorialChannel(Name, SongChannels(pDoc));
	if (Channel < 0 || !pView->GetPatternEditor()->GetChannelRect(Channel, Rect))
		return false;
	pView->ClientToScreen(&Rect);
	return true;
}

// As a step begins: the cursor goes to its bar (stopping the song, which
// would move it again) and channel, only its channels are heard, its
// instrument is picked
void CTutorialBubble::BeginStepSound()
{
	CFamiTrackerDoc *pDoc = Doc();
	CFamiTrackerView *pView = CFamiTrackerView::GetView();
	if (pDoc == NULL || pView == NULL || !pDoc->IsFileLoaded())
		return;
	const tutorial_step_t &Now = Step();
	const std::vector<tutorial_channel_t> Channels = SongChannels(pDoc);
	const int Channel = Now.Show == "channel" ? FindTutorialChannel(Now.ShowName, Channels) : -1;
	int Row = 0, End = 0;
	if (Now.CursorBar() > 0 && BarRows(Now.CursorBar(), Now.CursorBar(), Row, End)) {
		if (theApp.IsPlaying())
			theApp.StopPlayerAndWait();
		MoveCursor(Row, Channel);
	}
	else if (Channel >= 0)
		pView->SelectChannel(static_cast<unsigned int>(Channel));

	if (!Now.Solo.empty() || !Now.Mute.empty()) {
		const unsigned int Count = pDoc->GetAvailableChannels();
		std::vector<bool> Heard(Count, Now.Solo.empty());
		bool Found = false;
		for (size_t i = 0; i < Now.Solo.size(); ++i) {
			const int c = FindTutorialChannel(Now.Solo[i], Channels);
			if (c >= 0 && c < static_cast<int>(Count))
				Heard[static_cast<size_t>(c)] = Found = true;
		}
		for (size_t i = 0; i < Now.Mute.size(); ++i) {
			const int c = FindTutorialChannel(Now.Mute[i], Channels);
			if (c >= 0 && c < static_cast<int>(Count)) {
				Heard[static_cast<size_t>(c)] = false;
				Found = true;
			}
		}
		if (Found) {
			if (theApp.IsPlaying())
				theApp.StopPlayerAndWait();		// no note left sounding on a channel muted now
			for (unsigned int c = 0; c < Count; ++c) {
				m_SavedMutes.push_back(pView->IsChannelMuted(c));
				pView->SetChannelMute(static_cast<int>(c), !Heard[c]);
			}
			pView->GetPatternEditor()->InvalidateHeader();
			pView->RedrawWindow(pView->GetPatternEditor()->GetHeaderRect(), NULL, RDW_INVALIDATE);
		}
	}

	if (Now.Instrument >= 0 && pDoc->IsInstrumentUsed(static_cast<unsigned int>(Now.Instrument)))
		m_pMainFrame->SelectInstrument(Now.Instrument);
}

// As a step ends: its bars stop, and its channels are heard as before
void CTutorialBubble::EndStepSound()
{
	if (m_bPlaying && theApp.IsPlaying())
		theApp.StopPlayer();
	m_bPlaying = false;
	m_bPlayStarted = false;
	m_bHeard = false;
	RestoreMutes();
}

void CTutorialBubble::RestoreMutes()
{
	const CFamiTrackerDoc *pDoc = Doc();
	CFamiTrackerView *pView = CFamiTrackerView::GetView();
	if (!m_SavedMutes.empty() && pDoc != NULL && pView != NULL && pDoc->GetSongSerial() == m_iSong) {
		for (unsigned int c = 0; c < m_SavedMutes.size() && c < pDoc->GetAvailableChannels(); ++c)
			pView->SetChannelMute(static_cast<int>(c), m_SavedMutes[c]);
		pView->GetPatternEditor()->InvalidateHeader();
		pView->RedrawWindow(pView->GetPatternEditor()->GetHeaderRect(), NULL, RDW_INVALIDATE);
	}
	m_SavedMutes.clear();
}

// The Play button: the step's bars, from the first, stopping after the last
// (CSoundGen::SetNextPlayRange); Stop while they play
void CTutorialBubble::PlayBars()
{
	CFamiTrackerView *pView = CFamiTrackerView::GetView();
	CSoundGen *pSound = theApp.GetSoundGenerator();
	if (pView == NULL || pSound == NULL)
		return;
	if (m_bPlaying) {
		theApp.StopPlayer();
		m_bPlaying = false;
		Invalidate(FALSE);
		return;
	}
	int First = 0, End = 0;
	if (!BarRows(Step().PlayFirst, Step().PlayLast, First, End)) {
		m_pMainFrame->SetMessageText(_T("The song is shorter than that now."));
		return;
	}
	if (theApp.IsPlaying())
		theApp.StopPlayerAndWait();		// the cursor doesn't move while the song plays in follow mode
	pSound->SetQueueFrame(-1);
	const tutorial_step_t &Now = Step();
	const CFamiTrackerDoc *pDoc = Doc();
	MoveCursor(First, Now.Show == "channel" && pDoc != NULL ? FindTutorialChannel(Now.ShowName, SongChannels(pDoc)) : -1);
	pSound->SetNextPlayRange(First, End);
	m_iPlayStarts = pSound->GetPlayStarts();
	theApp.StartPlayer(MODE_PLAY_CURSOR);
	m_bPlaying = true;
	m_bPlayStarted = false;
	m_dwPlayPressed = ::GetTickCount();
	Invalidate(FALSE);
}

// Help > Tutorials

class CTutorialsDlg : public CDialog
{
public:
	CTutorialsDlg(CWnd *pParent, const std::vector<tutorial_entry_t> &List) :
		CDialog(IDD_TUTORIALS, pParent), m_List(List), m_iChosen(-1) {}
	int Chosen() const { return m_iChosen; }

protected:
	virtual BOOL OnInitDialog();
	virtual void OnOK();
	afx_msg void OnSelChange();

	DECLARE_MESSAGE_MAP()

private:
	const std::vector<tutorial_entry_t> &m_List;
	int m_iChosen;
};

BEGIN_MESSAGE_MAP(CTutorialsDlg, CDialog)
	ON_LBN_SELCHANGE(IDC_TUTORIAL_LIST, OnSelChange)
	ON_LBN_DBLCLK(IDC_TUTORIAL_LIST, OnOK)
END_MESSAGE_MAP()

BOOL CTutorialsDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	CListBox *pList = static_cast<CListBox*>(GetDlgItem(IDC_TUTORIAL_LIST));
	for (size_t i = 0; i < m_List.size(); ++i)
		pList->AddString(m_List[i].Problem.IsEmpty() ? m_List[i].Name : m_List[i].Name + _T(" (can't be read)"));
	pList->SetCurSel(0);
	SetDlgItemText(IDC_TUTORIAL_FOLDER, _T("More tutorials can go in a folder called Tutorials next to FamiTracker.exe, as text ")
		_T("files written like the ones in res\\tutorials of FamiTracker's source."));
	OnSelChange();
	return TRUE;
}

void CTutorialsDlg::OnSelChange()
{
	const int Index = static_cast<CListBox*>(GetDlgItem(IDC_TUTORIAL_LIST))->GetCurSel();
	if (Index < 0 || Index >= static_cast<int>(m_List.size())) {
		SetDlgItemText(IDC_TUTORIAL_ABOUT, _T(""));
		return;
	}
	const tutorial_entry_t &Entry = m_List[static_cast<size_t>(Index)];
	CString About;
	if (!Entry.Problem.IsEmpty())
		About = _T("This tutorial can't be read: ") + Entry.Problem;
	else
		About.Format(_T("%s (%d steps)"), static_cast<LPCTSTR>(CString(Entry.Tutorial.About.c_str())),
			static_cast<int>(Entry.Tutorial.Steps.size()));
	SetDlgItemText(IDC_TUTORIAL_ABOUT, About);
	GetDlgItem(IDOK)->EnableWindow(Entry.Problem.IsEmpty() ? TRUE : FALSE);
}

void CTutorialsDlg::OnOK()
{
	const int Index = static_cast<CListBox*>(GetDlgItem(IDC_TUTORIAL_LIST))->GetCurSel();
	if (Index < 0 || Index >= static_cast<int>(m_List.size()) || !m_List[static_cast<size_t>(Index)].Problem.IsEmpty())
		return;
	m_iChosen = Index;
	CDialog::OnOK();
}

namespace {

void StartTutorial(CMainFrame *pMainFrame, const tutorial_t &Tutorial, bool Example)
{
	if (g_pTutorial != NULL)
		g_pTutorial->Stop();
	CTutorialBubble *pBubble = new CTutorialBubble(pMainFrame, Tutorial, Example);
	g_pTutorial = pBubble;
	if (!pBubble->Create()) {
		g_pTutorial = NULL;
		delete pBubble;
	}
}

}	// namespace

void ShowTutorials(CMainFrame *pMainFrame)
{
	const std::vector<tutorial_entry_t> List = FindTutorials();
	if (List.empty())
		return;
	CTutorialsDlg Dialog(pMainFrame, List);
	if (Dialog.DoModal() != IDOK || Dialog.Chosen() < 0)
		return;
	StartTutorial(pMainFrame, List[static_cast<size_t>(Dialog.Chosen())].Tutorial, false);
}

void OpenExampleSong(CMainFrame *pMainFrame, UINT nID)
{
	const example_song_t *pExample = NULL;
	for (size_t i = 0; i < sizeof(EXAMPLES) / sizeof(EXAMPLES[0]); ++i)
		if (EXAMPLES[i].Command == nID)
			pExample = &EXAMPLES[i];
	std::string Walk, Song, Error;
	tutorial_t Tutorial;
	if (pExample == NULL || !BuiltInText(pExample->Walkthrough, Walk) || !ParseTutorial(Walk, Tutorial, Error) ||
		!BuiltInText(pExample->Song, Song)) {
		ASSERT(FALSE);
		return;
	}

	// The song is opened from a file of its own, as any song is, and becomes a
	// new one without a file: Save asks for a name, and the example stays
	TCHAR Folder[MAX_PATH], Path[MAX_PATH];
	if (::GetTempPath(MAX_PATH, Folder) == 0 || ::GetTempFileName(Folder, _T("ftx"), 0, Path) == 0) {
		AfxMessageBox(_T("The example can't be opened: there's no room for it in the folder for temporary files."), MB_ICONEXCLAMATION);
		return;
	}
	bool Written = false;
	try {
		CFile File(Path, CFile::modeCreate | CFile::modeWrite);
		File.Write(Song.data(), static_cast<UINT>(Song.size()));
		File.Close();
		Written = true;
	}
	catch (CFileException *pException) {
		pException->Delete();
	}
	if (!Written) {
		::DeleteFile(Path);
		AfxMessageBox(_T("The example can't be opened: it can't be written to the folder for temporary files."), MB_ICONEXCLAMATION);
		return;
	}
	// Asks first whether to save the song that's open (a tutorial running goes on if the user keeps it), and not
	// into the recent files
	CDocument *pOpened = theApp.OpenDocumentFile(Path, FALSE);
	::DeleteFile(Path);
	CFamiTrackerDoc *pDoc = static_cast<CFamiTrackerDoc*>(pOpened);
	if (pDoc == NULL)
		return;		// the song that was open stays
	pDoc->MakeUntitled(pExample->Title);
	StartTutorial(pMainFrame, Tutorial, true);
}

void TutorialCommand(UINT nID)
{
	if (g_pTutorial != NULL)
		g_pTutorial->Command(nID);
}

bool TutorialRunning()
{
	return g_pTutorial != NULL;
}

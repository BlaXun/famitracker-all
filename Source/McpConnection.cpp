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

// File > Connect Claude Code (see McpConnection.h): the song as the tools see
// it (Api/Song.h) over the open document, the window on the UI thread that
// answers requests, the settings and the dialog.

// rand_s, for the access token, needs this before stdlib.h
#define _CRT_RAND_S
#include <cstdlib>
// Standard and API headers come before stdafx.h, whose DEBUG_NEW macro would
// otherwise rewrite the placement new inside the standard library.
#include <algorithm>
#include <string>
#include <vector>
#include "Api/Http.h"
#include "Api/Json.h"
#include "Api/Mcp.h"
#include "Api/Music.h"
#include "Api/Presets.h"
#include "Api/Song.h"
#include "Api/Tools.h"
#include "stdafx.h"
#include "FamiTracker.h"
#include "FamiTrackerDoc.h"
#include "FamiTrackerView.h"
#include "MainFrm.h"
#include "PatternEditor.h"
#include "SoundGen.h"
#include "TrackerChannel.h"
#include "Action.h"
#include "Clips.h"
#include "InsertRecording.h"
#include "McpSocket.h"
#include "McpConnection.h"
#include "version.h"

// The API's copies of FamiTracker's values must match
static_assert(ftapi::NOTE_RELEASE == RELEASE && ftapi::NOTE_HALT == HALT && ftapi::NOTE_NONE == NONE, "notes");
static_assert(ftapi::NO_INSTRUMENT == MAX_INSTRUMENTS && ftapi::NO_VOLUME == MAX_VOLUME, "empty columns");
static_assert(ftapi::EFFECT_COLUMNS == MAX_EFFECT_COLUMNS && ftapi::PITCHES == OCTAVE_RANGE * 12, "sizes");
static_assert(ftapi::EFF_COUNT == EF_COUNT && ftapi::EFF_ARPEGGIO == EF_ARPEGGIO && ftapi::EFF_VIBRATO == EF_VIBRATO &&
	ftapi::EFF_SLIDE_UP == EF_SLIDE_UP && ftapi::EFF_SLIDE_DOWN == EF_SLIDE_DOWN && ftapi::EFF_SPEED == EF_SPEED &&
	ftapi::EFF_JUMP == EF_JUMP && ftapi::EFF_SKIP == EF_SKIP && ftapi::EFF_HALT == EF_HALT && ftapi::EFF_NOTE_CUT == EF_NOTE_CUT &&
	ftapi::EFF_VOLUME_SLIDE == EF_VOLUME_SLIDE && ftapi::EFF_PORTAMENTO == EF_PORTAMENTO && ftapi::EFF_TREMOLO == EF_TREMOLO &&
	ftapi::EFF_SUNSOFT_ENV_TYPE == EF_SUNSOFT_ENV_TYPE, "effects");
static_assert(ftapi::SEQUENCE_VOLUME == SEQ_VOLUME && ftapi::SEQUENCE_ARPEGGIO == SEQ_ARPEGGIO && ftapi::SEQUENCE_PITCH == SEQ_PITCH &&
	ftapi::SEQUENCE_HIPITCH == SEQ_HIPITCH && ftapi::SEQUENCE_DUTY == SEQ_DUTYCYCLE, "sequences");

namespace {

const TCHAR SECTION[] = _T("Claude Code");
const int DEFAULT_PORT = 46600;
const int ACT_MCP_TEMPO = 0x1100;		// action ID, away from the others
const UINT WM_MCP_REQUEST = WM_APP + 0x3C1;
const UINT_PTR PLAY_TIMER = 1;
const TCHAR WINDOW_CLASS[] = _T("FamiTrackerClaudeCode");
const TCHAR WSL_HELPER[] = _T("famitracker-mcp.exe");	// for Claude Code in WSL (Api/Bridge.h)

// Text between the API (UTF-8) and FamiTracker (the ANSI code page)
std::string Utf8(LPCSTR pText)
{
	if (pText == NULL || *pText == 0)
		return std::string();
	const int Wide = MultiByteToWideChar(CP_ACP, 0, pText, -1, NULL, 0);
	if (Wide <= 0)
		return std::string();
	std::vector<wchar_t> Buffer(static_cast<size_t>(Wide));
	MultiByteToWideChar(CP_ACP, 0, pText, -1, &Buffer[0], Wide);
	const int Bytes = WideCharToMultiByte(CP_UTF8, 0, &Buffer[0], -1, NULL, 0, NULL, NULL);
	if (Bytes <= 0)
		return std::string();
	std::vector<char> Out(static_cast<size_t>(Bytes));
	WideCharToMultiByte(CP_UTF8, 0, &Buffer[0], -1, &Out[0], Bytes, NULL, NULL);
	return std::string(&Out[0]);
}

CString Ansi(const std::string &Text)
{
	if (Text.empty())
		return CString();
	const int Wide = MultiByteToWideChar(CP_UTF8, 0, Text.c_str(), -1, NULL, 0);
	if (Wide <= 0)
		return CString();
	std::vector<wchar_t> Buffer(static_cast<size_t>(Wide));
	MultiByteToWideChar(CP_UTF8, 0, Text.c_str(), -1, &Buffer[0], Wide);
	const int Bytes = WideCharToMultiByte(CP_ACP, 0, &Buffer[0], -1, NULL, 0, "?", NULL);
	if (Bytes <= 0)
		return CString();
	std::vector<char> Out(static_cast<size_t>(Bytes));
	WideCharToMultiByte(CP_ACP, 0, &Buffer[0], -1, &Out[0], Bytes, "?", NULL);
	return CString(&Out[0]);
}

ftapi::Cell ToCell(const stChanNote &Note)
{
	ftapi::Cell Cell;
	Cell.Note = Note.Note;
	Cell.Octave = Note.Octave;
	Cell.Instrument = Note.Instrument;
	Cell.Volume = Note.Vol;
	for (int k = 0; k < MAX_EFFECT_COLUMNS; ++k) {
		Cell.Effect[k] = Note.EffNumber[k];
		Cell.Param[k] = Note.EffParam[k];
	}
	return Cell;
}

stChanNote ToNote(const ftapi::Cell &Cell)
{
	stChanNote Note;
	memset(&Note, 0, sizeof(Note));
	Note.Note = Cell.Note;
	Note.Octave = Cell.Octave;
	Note.Instrument = Cell.Instrument;
	Note.Vol = Cell.Volume;
	for (int k = 0; k < MAX_EFFECT_COLUMNS; ++k) {
		Note.EffNumber[k] = Cell.Effect[k];
		Note.EffParam[k] = Cell.Param[k];
	}
	return Note;
}

std::string ChipName(int Chip)
{
	switch (Chip) {
	case SNDCHIP_NONE: return "2A03";
	case SNDCHIP_VRC6: return "VRC6";
	case SNDCHIP_VRC7: return "VRC7";
	case SNDCHIP_FDS: return "FDS";
	case SNDCHIP_MMC5: return "MMC5";
	case SNDCHIP_N163: return "N163";
	case SNDCHIP_S5B: return "5B";
	}
	return "?";
}

std::string InstrumentChip(inst_type_t Type)
{
	switch (Type) {
	case INST_2A03: return "2A03";
	case INST_VRC6: return "VRC6";
	case INST_VRC7: return "VRC7";
	case INST_FDS: return "FDS";
	case INST_N163: return "N163";
	case INST_S5B: return "5B";
	default: return "?";
	}
}

int KindOf(int ChanID)
{
	switch (ChanID) {
	case CHANID_SQUARE1: case CHANID_SQUARE2: case CHANID_VRC6_PULSE1: case CHANID_VRC6_PULSE2:
	case CHANID_MMC5_SQUARE1: case CHANID_MMC5_SQUARE2:
		return ftapi::KIND_PULSE;
	case CHANID_TRIANGLE: return ftapi::KIND_TRIANGLE;
	case CHANID_NOISE: return ftapi::KIND_NOISE;
	case CHANID_DPCM: return ftapi::KIND_DPCM;
	case CHANID_VRC6_SAWTOOTH: return ftapi::KIND_SAWTOOTH;
	}
	return ftapi::KIND_OTHER;
}

// 32 hex digits from the system's random numbers
CString NewToken()
{
	CString Token;
	for (int i = 0; i < 4; ++i) {
		unsigned int Value = 0;
		if (rand_s(&Value) != 0)
			Value = static_cast<unsigned int>(GetTickCount()) * 2654435761u + static_cast<unsigned int>(i);
		CString Part;
		Part.Format(_T("%08x"), Value);
		Token += Part;
	}
	return Token;
}

// The tempo, undoably (module settings aren't undoable in FamiTracker 0.4.6)
class CMcpTempoAction : public CAction
{
public:
	CMcpTempoAction(int Speed, int Tempo) :
		CAction(ACT_MCP_TEMPO), m_iTrack(0), m_iSpeed(Speed), m_iTempo(Tempo), m_iOldSpeed(0), m_iOldTempo(0)
	{
	}

	bool SaveState(CMainFrame *pMainFrm)
	{
		CFamiTrackerDoc *pDoc = static_cast<CFamiTrackerDoc*>(pMainFrm->GetActiveDocument());
		m_iTrack = static_cast<unsigned int>(pMainFrm->GetSelectedTrack());
		m_iOldSpeed = static_cast<int>(pDoc->GetSongSpeed(m_iTrack));
		m_iOldTempo = static_cast<int>(pDoc->GetSongTempo(m_iTrack));
		Redo(pMainFrm);
		return true;
	}

	void Undo(CMainFrame *pMainFrm) { Set(pMainFrm, m_iOldSpeed, m_iOldTempo); }
	void Redo(CMainFrame *pMainFrm) { Set(pMainFrm, m_iSpeed, m_iTempo); }

private:
	void Set(CMainFrame *pMainFrm, int Speed, int Tempo)
	{
		CFamiTrackerDoc *pDoc = static_cast<CFamiTrackerDoc*>(pMainFrm->GetActiveDocument());
		if (m_iTrack >= pDoc->GetTrackCount())
			return;
		pDoc->SetSongSpeed(m_iTrack, static_cast<unsigned int>(Speed));
		pDoc->SetSongTempo(m_iTrack, static_cast<unsigned int>(Tempo));
		theApp.GetSoundGenerator()->ResetTempo();
		pDoc->SetModifiedFlag();
		pMainFrm->UpdateControls();
	}

	unsigned int m_iTrack;
	int m_iSpeed, m_iTempo, m_iOldSpeed, m_iOldTempo;
};

}	// namespace

struct mcp_settings_t {
	bool Enabled;
	int Port;
	CString Token;
	bool DirectChanges;
	bool Wsl;			// the dialog shows the setup for Claude Code in WSL
};

// The connection: settings, listener, and the song for the tools
class CMcpLink : public ftapi::SongHost
{
public:
	CMcpLink() : m_pMain(NULL), m_hWindow(NULL), m_Tools(*this), m_bBusy(false), m_bStopping(false), m_iPlayFirst(0),
		m_iPlayEnd(0), m_bPlayStarted(false), m_iPlayTicks(0)
	{
		m_Settings.Enabled = false;
		m_Settings.Port = DEFAULT_PORT;
		m_Settings.DirectChanges = false;
		m_Settings.Wsl = false;
	}

	~CMcpLink()
	{
		Shutdown();
	}

	void Start(CMainFrame *pMainFrame)
	{
		m_pMain = pMainFrame;
		LoadSettings();
		CreateWindowOnce();
		if (m_Settings.Enabled) {
			CString Error;
			if (!Listen(Error))
				m_pMain->SetMessageText(_T("Claude Code connection: ") + Error);
		}
	}

	void Shutdown()
	{
		m_bStopping = true;
		m_Server.Stop();
		if (m_hWindow != NULL) {
			KillTimer(m_hWindow, PLAY_TIMER);
			DestroyWindow(m_hWindow);
			m_hWindow = NULL;
		}
	}

	const mcp_settings_t &GetSettings() const { return m_Settings; }

	CString Status() const
	{
		if (m_Server.IsRunning()) {
			CString Text;
			Text.Format(_T("On: Claude Code can connect at 127.0.0.1:%d."), m_Server.GetPort());
			return Text;
		}
		if (!m_sLastError.IsEmpty())
			return m_sLastError;
		return _T("Off.");
	}

	CString Command(int Port, LPCTSTR Token) const
	{
		ftapi::McpConfig Config;
		Config.Port = Port;
		Config.Token = std::string(static_cast<LPCSTR>(CStringA(Token)));
		return CString(ftapi::ClaudeAddCommand(Config).c_str());
	}

	// Claude Code in WSL starts famitracker-mcp.exe from FamiTracker's
	// folder, which reads the port and the access code from the settings
	CString WslCommand() const
	{
		return CString(ftapi::WslAddCommand(std::string(static_cast<LPCSTR>(CStringA(HelperPath())))).c_str());
	}

	static CString HelperPath()
	{
		TCHAR Path[MAX_PATH];
		const DWORD Length = GetModuleFileName(NULL, Path, MAX_PATH);
		if (Length == 0 || Length >= MAX_PATH)
			return WSL_HELPER;
		const CString Program(Path);
		return Program.Left(Program.ReverseFind(_T('\\')) + 1) + WSL_HELPER;
	}

	static bool HelperFound()
	{
		const DWORD Attributes = GetFileAttributes(HelperPath());
		return Attributes != INVALID_FILE_ATTRIBUTES && (Attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
	}

	// Only the dialog's choice: nothing to start or stop
	void RememberWsl(bool Wsl)
	{
		m_Settings.Wsl = Wsl;
		theApp.WriteProfileInt(SECTION, _T("Claude Code in WSL"), Wsl ? 1 : 0);
	}

	// From the dialog: saves the settings and starts or stops listening
	bool Apply(const mcp_settings_t &Settings, CString &Error)
	{
		const bool Restart = Settings.Port != m_Settings.Port || Settings.Token != m_Settings.Token || !m_Server.IsRunning();
		m_Settings = Settings;
		SaveSettings();
		if (!m_Settings.Enabled) {
			m_Server.Stop();
			m_sLastError.Empty();
			return true;
		}
		if (Restart)
			return Listen(Error);
		return true;
	}

	LRESULT Answer(mcp_exchange_t *pExchange)
	{
		if (m_bStopping || m_bBusy || pExchange == NULL || m_pMain == NULL)
			return 0;	// the client hears that FamiTracker is busy
		m_bBusy = true;
		try {
			ftapi::McpConfig Config;
			Config.Port = m_Server.GetPort();
			Config.Token = std::string(static_cast<LPCSTR>(CStringA(m_Settings.Token)));
			char Version[32];
			_snprintf_s(Version, sizeof(Version), _TRUNCATE, "%d.%d.%d-beginner-friendly", VERSION_MAJ, VERSION_MIN, VERSION_REV);
			Config.Version = Version;
			*pExchange->pResponse = ftapi::HandleMcp(*pExchange->pRequest, Config, m_Tools);
		}
		catch (...) {
			ftapi::HttpResponse Failed;
			Failed.Status = 500;
			Failed.ContentType = "text/plain; charset=utf-8";
			Failed.Body = "FamiTracker couldn't answer that request.\n";
			*pExchange->pResponse = Failed;
		}
		m_bBusy = false;
		return 1;
	}

	// Stops playback after the asked bars
	void CheckPlayback()
	{
		CSoundGen *pSound = theApp.GetSoundGenerator();
		CFamiTrackerDoc *pDoc = Doc();
		if (pSound == NULL || pDoc == NULL || !theApp.IsPlaying()) {
			KillTimer(m_hWindow, PLAY_TIMER);
			return;
		}
		const int Length = static_cast<int>(pDoc->GetPatternLength(static_cast<unsigned int>(pSound->GetPlayerTrack())));
		const int Row = pSound->GetPlayerFrame() * Length + pSound->GetPlayerRow();
		if (!m_bPlayStarted) {
			if (Row >= m_iPlayFirst && Row < m_iPlayEnd)
				m_bPlayStarted = true;
			else if (++m_iPlayTicks > 40) {	// it never got there: leave it be
				KillTimer(m_hWindow, PLAY_TIMER);
				return;
			}
		}
		if (m_bPlayStarted && (Row >= m_iPlayEnd || Row < m_iPlayFirst)) {
			theApp.StopPlayer();
			KillTimer(m_hWindow, PLAY_TIMER);
		}
	}

	// ftapi::SongHost

	bool GetSong(ftapi::SongInfo &Info, std::string &Error)
	{
		CFamiTrackerDoc *pDoc = Doc();
		if (pDoc == NULL || !pDoc->IsFileLoaded()) {
			Error = "No song is open in FamiTracker.";
			return false;
		}
		const unsigned int Track = this->Track();
		Info = ftapi::SongInfo();
		Info.Title = Utf8(pDoc->GetSongName());
		Info.Author = Utf8(pDoc->GetSongArtist());
		Info.Copyright = Utf8(pDoc->GetSongCopyright());
		const CString Path = pDoc->GetPathName();
		Info.FileName = Utf8(Path.Mid(Path.ReverseFind(_T('\\')) + 1));
		Info.Modified = pDoc->IsModified() != FALSE;
		Info.Track = static_cast<int>(Track);
		Info.Tracks = static_cast<int>(pDoc->GetTrackCount());
		Info.TrackName = Utf8(pDoc->GetTrackTitle(Track));
		Info.FrameRate = static_cast<int>(pDoc->GetFrameRate());
		Info.Speed = static_cast<int>(pDoc->GetSongSpeed(Track));
		Info.Tempo = static_cast<int>(pDoc->GetSongTempo(Track));
		Info.SplitPoint = pDoc->GetSpeedSplitPoint();
		Info.PatternLength = static_cast<int>(pDoc->GetPatternLength(Track));
		Info.Frames = static_cast<int>(pDoc->GetFrameCount(Track));
		Info.MaxFrames = MAX_FRAMES;
		Info.FirstHighlight = pDoc->GetFirstHighlight();
		Info.SecondHighlight = pDoc->GetSecondHighlight();
		const unsigned char Chips = pDoc->GetExpansionChip();
		const unsigned char ALL[] = { SNDCHIP_VRC6, SNDCHIP_VRC7, SNDCHIP_FDS, SNDCHIP_MMC5, SNDCHIP_N163, SNDCHIP_S5B };
		for (size_t i = 0; i < sizeof(ALL); ++i) {
			if (Chips & ALL[i])
				Info.Expansion += (Info.Expansion.empty() ? "" : ", ") + ChipName(ALL[i]);
		}

		const int Channels = static_cast<int>(pDoc->GetAvailableChannels());
		for (int c = 0; c < Channels; ++c) {
			ftapi::ChannelInfo Channel;
			Channel.Name = Utf8(pDoc->GetChannel(c)->GetChannelName());
			Channel.Chip = ChipName(pDoc->GetChipType(c));
			Channel.ChanId = pDoc->GetChannelType(c);
			Channel.Kind = KindOf(Channel.ChanId);
			Channel.EffectColumns = static_cast<int>(pDoc->GetEffColumns(Track, static_cast<unsigned int>(c))) + 1;
			Info.Channels.push_back(Channel);
		}
		for (int i = 0; i < MAX_INSTRUMENTS; ++i) {
			if (!pDoc->IsInstrumentUsed(static_cast<unsigned int>(i)))
				continue;
			ftapi::InstrumentInfo Instrument;
			Instrument.Index = i;
			char Name[CInstrument::INST_NAME_MAX];
			pDoc->GetInstrumentName(static_cast<unsigned int>(i), Name);
			Name[CInstrument::INST_NAME_MAX - 1] = 0;
			Instrument.Name = Utf8(Name);
			Instrument.Chip = InstrumentChip(pDoc->GetInstrumentType(static_cast<unsigned int>(i)));
			CInstrument *pInstrument = pDoc->GetInstrument(static_cast<unsigned int>(i));	// retained
			if (pInstrument != NULL) {
				Instrument.Releases = pInstrument->CanRelease();
				pInstrument->Release();
			}
			Info.Instruments.push_back(Instrument);
		}
		const CClipLibrary *pClips = pDoc->GetClips();
		for (int i = 0; pClips != NULL && i < pClips->GetCount(); ++i) {
			ftapi::ClipInfo Clip;
			Clip.Index = i;
			Clip.Id = pClips->GetId(i);
			Clip.Name = Utf8(pClips->GetName(i));
			Clip.Columns = pClips->GetColumns(i);
			Clip.Rows = pClips->GetRows(i);
			Clip.Links = pClips->GetLinkCount(i);
			for (int c = 0; c < Clip.Columns; ++c)
				Clip.Sources.push_back(pClips->GetSource(i, c));
			Info.Clips.push_back(Clip);
		}
		Info.DirectChanges = m_Settings.DirectChanges;
		Info.Playing = theApp.IsPlaying();
		CSoundGen *pSound = theApp.GetSoundGenerator();
		if (Info.Playing && pSound != NULL && static_cast<unsigned int>(pSound->GetPlayerTrack()) == Track)
			Info.PlayRow = pSound->GetPlayerFrame() * Info.PatternLength + pSound->GetPlayerRow();
		return true;
	}

	void ReadCells(int Channel, int First, int Rows, std::vector<ftapi::Cell> &Out)
	{
		Out.clear();
		CFamiTrackerDoc *pDoc = Doc();
		if (pDoc == NULL || Rows <= 0)
			return;
		const unsigned int Track = this->Track();
		const int Length = static_cast<int>(pDoc->GetPatternLength(Track));
		const int Total = Length * static_cast<int>(pDoc->GetFrameCount(Track));
		const bool Valid = Channel >= 0 && Channel < static_cast<int>(pDoc->GetAvailableChannels());
		Out.reserve(static_cast<size_t>(Rows));
		for (int r = First; r < First + Rows; ++r) {
			if (!Valid || r < 0 || r >= Total) {
				Out.push_back(ftapi::Cell());
				continue;
			}
			stChanNote Note;
			pDoc->GetNoteData(Track, static_cast<unsigned int>(r / Length), static_cast<unsigned int>(Channel),
				static_cast<unsigned int>(r % Length), &Note);
			Out.push_back(ToCell(Note));
		}
	}

	bool ReadClip(int Index, std::vector<std::vector<ftapi::Cell> > &Columns)
	{
		CFamiTrackerDoc *pDoc = Doc();
		const CClipLibrary *pClips = pDoc != NULL ? pDoc->GetClips() : NULL;
		if (pClips == NULL || Index < 0 || Index >= pClips->GetCount())
			return false;
		Columns.assign(static_cast<size_t>(pClips->GetColumns(Index)), std::vector<ftapi::Cell>());
		for (int c = 0; c < pClips->GetColumns(Index); ++c) {
			for (int r = 0; r < pClips->GetRows(Index); ++r) {
				stChanNote Note;
				pClips->GetCell(Index, c, r, Note);
				Columns[static_cast<size_t>(c)].push_back(ToCell(Note));
			}
		}
		return true;
	}

	bool CanChange(std::string &Why)
	{
		CFamiTrackerDoc *pDoc = Doc();
		if (pDoc == NULL || !pDoc->IsFileLoaded()) {
			Why = "No song is open in FamiTracker.";
			return false;
		}
		if (!m_pMain->IsWindowEnabled()) {
			Why = "A dialog is open in FamiTracker; ask the user to close it, then try again.";
			return false;
		}
		return true;
	}

	bool WriteSong(const std::vector<ftapi::ChannelCells> &Parts, int First, int LinkClipId, std::string &Error)
	{
		if (!CanChange(Error))
			return false;
		CFamiTrackerDoc *pDoc = Doc();
		const int Length = static_cast<int>(pDoc->GetPatternLength(Track()));
		std::vector<std::vector<stChanNote> > Cells(Parts.size());
		std::vector<song_part_t> Raw;
		for (size_t p = 0; p < Parts.size(); ++p) {
			for (size_t r = 0; r < Parts[p].Cells.size(); ++r)
				Cells[p].push_back(ToNote(Parts[p].Cells[r]));
			if (Cells[p].empty())
				continue;
			song_part_t Part;
			Part.Channel = Parts[p].Channel;
			Part.pCells = &Cells[p][0];
			Part.Rows = static_cast<int>(Cells[p].size());
			Raw.push_back(Part);
		}
		if (Raw.empty()) {
			Error = "There was nothing to write.";
			return false;
		}
		CString Message;
		if (!WriteSongParts(m_pMain, &Raw[0], static_cast<int>(Raw.size()), First / Length, First % Length, LinkClipId, Message)) {
			Error = Utf8(Message);
			return false;
		}
		return true;
	}

	bool AddClip(std::string &Name, const std::vector<int> &Sources, const std::vector<std::vector<ftapi::Cell> > &Columns,
		std::string &Error)
	{
		if (!CanChange(Error))
			return false;
		CFamiTrackerDoc *pDoc = Doc();
		CClipLibrary *pClips = pDoc != NULL ? pDoc->GetClips() : NULL;
		if (pClips == NULL || Columns.empty() || Columns[0].empty()) {
			Error = "There was nothing to keep as a clip.";
			return false;
		}
		CString Base = Ansi(Name);
		Base.Replace(_T('\r'), _T(' '));
		Base.Replace(_T('\n'), _T(' '));
		const CString Unique = pClips->NewName(Base, false);
		const int Rows = static_cast<int>(Columns[0].size());
		const int Index = pClips->Add(Unique, static_cast<int>(Columns.size()), Rows);
		if (Index < 0) {
			Error = "The song has too many clips already; the user can delete some in Edit > Clips.";
			return false;
		}
		for (size_t c = 0; c < Columns.size(); ++c) {
			pClips->SetSource(Index, static_cast<int>(c), c < Sources.size() ? Sources[c] : -1);
			for (int r = 0; r < Rows && r < static_cast<int>(Columns[c].size()); ++r)
				pClips->SetCell(Index, static_cast<int>(c), r, ToNote(Columns[c][static_cast<size_t>(r)]));
		}
		pDoc->SetModifiedFlag();
		Name = Utf8(Unique);
		return true;
	}

	int AddInstrument(const ftapi::Preset &Sound, const std::string &Chip, bool Sawtooth, std::string &Error)
	{
		if (!CanChange(Error))
			return -1;
		CFamiTrackerDoc *pDoc = Doc();
		const bool VRC6 = Chip == "VRC6";
		const inst_type_t Type = VRC6 ? INST_VRC6 : INST_2A03;
		const int ChipType = VRC6 ? SNDCHIP_VRC6 : SNDCHIP_NONE;
		if (VRC6 && !pDoc->ExpansionEnabled(SNDCHIP_VRC6)) {
			Error = "This song has no VRC6 channels.";
			return -1;
		}
		const CString Name = Ansi(Sound.InstrumentName(Sawtooth));
		for (int i = 0; i < MAX_INSTRUMENTS; ++i) {
			if (!pDoc->IsInstrumentUsed(static_cast<unsigned int>(i)) || pDoc->GetInstrumentType(static_cast<unsigned int>(i)) != Type)
				continue;
			char Existing[CInstrument::INST_NAME_MAX];
			pDoc->GetInstrumentName(static_cast<unsigned int>(i), Existing);
			Existing[CInstrument::INST_NAME_MAX - 1] = 0;
			if (Name == Existing)
				return i;
		}
		const int Index = pDoc->AddInstrument(Name, ChipType);
		if (Index == INVALID_INSTRUMENT) {
			Error = "There are no free instrument slots left (a song has at most 64).";
			return -1;
		}
		CInstrument *pInstrument = pDoc->GetInstrument(static_cast<unsigned int>(Index));	// retained
		const std::vector<ftapi::PresetSequence> &Sequences = VRC6 ? Sound.SequencesVRC6 : Sound.Sequences2A03;
		for (size_t s = 0; s < Sequences.size() && pInstrument != NULL; ++s) {
			const ftapi::PresetSequence &Seq = Sequences[s];
			if (Sawtooth && Seq.Type == ftapi::SEQUENCE_DUTY)
				continue;	// on the sawtooth the duty's low bit adds to the volume, which would never fade out
			const int Slot = VRC6 ? pDoc->GetFreeSequenceVRC6(Seq.Type) : pDoc->GetFreeSequence(Seq.Type);
			if (Slot < 0)
				continue;	// all taken: the sound is plainer
			CSequence *pSequence = pDoc->GetSequence(ChipType, static_cast<unsigned int>(Slot), Seq.Type);
			pSequence->Clear();
			for (size_t k = 0; k < Seq.Items.size(); ++k)
				pSequence->SetItem(static_cast<int>(k), static_cast<signed char>(Seq.Items[k]));
			pSequence->SetItemCount(static_cast<unsigned int>(Seq.Items.size()));
			if (Seq.Loop >= 0)
				pSequence->SetLoopPoint(static_cast<unsigned int>(Seq.Loop));
			if (Seq.Release >= 0)
				pSequence->SetReleasePoint(static_cast<unsigned int>(Seq.Release));
			if (VRC6) {
				static_cast<CInstrumentVRC6*>(pInstrument)->SetSeqEnable(Seq.Type, 1);
				static_cast<CInstrumentVRC6*>(pInstrument)->SetSeqIndex(Seq.Type, Slot);
			}
			else {
				static_cast<CInstrument2A03*>(pInstrument)->SetSeqEnable(Seq.Type, 1);
				static_cast<CInstrument2A03*>(pInstrument)->SetSeqIndex(Seq.Type, Slot);
			}
		}
		if (pInstrument != NULL)
			pInstrument->Release();
		pDoc->SetModifiedFlag();
		pDoc->UpdateAllViews(NULL, UPDATE_INSTRUMENT);
		return Index;
	}

	bool SetSpeedTempo(int Speed, int Tempo, std::string &Error)
	{
		if (!CanChange(Error))
			return false;
		if (!m_pMain->AddAction(new CMcpTempoAction(Speed, Tempo))) {
			Error = "The tempo couldn't be changed.";
			return false;
		}
		return true;
	}

	bool Play(int First, int Rows, std::string &Error)
	{
		CFamiTrackerDoc *pDoc = Doc();
		CFamiTrackerView *pView = CFamiTrackerView::GetView();
		if (pDoc == NULL || pView == NULL) {
			Error = "No song is open in FamiTracker.";
			return false;
		}
		const int Length = static_cast<int>(pDoc->GetPatternLength(Track()));
		if (First < 0 || First >= Length * static_cast<int>(pDoc->GetFrameCount(Track()))) {
			Error = "That bar is past the end of the song.";
			return false;
		}
		if (theApp.IsPlaying())
			theApp.StopPlayerAndWait();	// the cursor doesn't move while playing in follow mode
		pView->SelectFrame(static_cast<unsigned int>(First / Length));
		pView->GetPatternEditor()->MoveToRow(First % Length);
		pView->GetPatternEditor()->InvalidateCursor();
		theApp.StartPlayer(MODE_PLAY_CURSOR);
		KillTimer(m_hWindow, PLAY_TIMER);
		if (Rows > 0) {
			m_iPlayFirst = First;
			m_iPlayEnd = First + Rows;
			m_bPlayStarted = false;
			m_iPlayTicks = 0;
			SetTimer(m_hWindow, PLAY_TIMER, 50, NULL);
		}
		return true;
	}

	void Stop()
	{
		KillTimer(m_hWindow, PLAY_TIMER);
		theApp.StopPlayer();
	}

	bool Undo(std::string &Message)
	{
		if (!CanChange(Message))
			return false;
		m_pMain->SendMessage(WM_COMMAND, ID_EDIT_UNDO);
		Message = "Undid the last change in the song (like Ctrl+Z); Ctrl+Y in FamiTracker brings it back.";
		return true;
	}

	bool Save(const std::string &CopyName, std::string &Message)
	{
		if (!CanChange(Message))
			return false;
		CFamiTrackerDoc *pDoc = Doc();
		const CString Path = pDoc->GetPathName();
		if (Path.IsEmpty()) {
			Message = "The song hasn't been saved yet. Ask the user to save it (File > Save As) so it has a place.";
			return false;
		}
		if (CopyName.empty()) {
			m_pMain->SendMessage(WM_COMMAND, ID_FILE_SAVE);
			if (pDoc->IsModified()) {
				Message = "The song couldn't be saved.";
				return false;
			}
			Message = "Saved the song (" + Utf8(Path.Mid(Path.ReverseFind(_T('\\')) + 1)) + ").";
			return true;
		}
		CString File = Ansi(CopyName);
		if (File.Right(4).CompareNoCase(_T(".ftm")) != 0)
			File += _T(".ftm");
		const CString Copy = Path.Left(Path.ReverseFind(_T('\\')) + 1) + File;
		if (GetFileAttributes(Copy) != INVALID_FILE_ATTRIBUTES) {
			Message = "There's already a file called " + Utf8(File) + " next to the song; pick another copy_name.";
			return false;
		}
		const BOOL Modified = pDoc->IsModified();
		if (!pDoc->OnSaveDocument(Copy)) {
			Message = "The copy couldn't be saved.";
			return false;
		}
		pDoc->SetModifiedFlag(Modified);	// the song itself still has what it had unsaved
		Message = "Saved a copy as " + Utf8(File) + " next to the song; the song itself wasn't saved.";
		return true;
	}

private:
	CFamiTrackerDoc *Doc() const
	{
		return m_pMain != NULL ? static_cast<CFamiTrackerDoc*>(m_pMain->GetActiveDocument()) : NULL;
	}

	unsigned int Track() const
	{
		return static_cast<unsigned int>(std::max(0, m_pMain->GetSelectedTrack()));
	}

	void LoadSettings()
	{
		m_Settings.Enabled = theApp.GetProfileInt(SECTION, _T("Enabled"), 0) != 0;
		m_Settings.Port = static_cast<int>(theApp.GetProfileInt(SECTION, _T("Port"), DEFAULT_PORT));
		if (m_Settings.Port < 1024 || m_Settings.Port > 65535)
			m_Settings.Port = DEFAULT_PORT;
		m_Settings.Token = theApp.GetProfileString(SECTION, _T("Token"), _T(""));
		if (m_Settings.Token.GetLength() < 16) {
			m_Settings.Token = NewToken();
			theApp.WriteProfileString(SECTION, _T("Token"), m_Settings.Token);
		}
		m_Settings.DirectChanges = theApp.GetProfileInt(SECTION, _T("Direct changes"), 0) != 0;
		m_Settings.Wsl = theApp.GetProfileInt(SECTION, _T("Claude Code in WSL"), 0) != 0;
	}

	void SaveSettings()
	{
		theApp.WriteProfileInt(SECTION, _T("Enabled"), m_Settings.Enabled ? 1 : 0);
		theApp.WriteProfileInt(SECTION, _T("Port"), m_Settings.Port);
		theApp.WriteProfileString(SECTION, _T("Token"), m_Settings.Token);
		theApp.WriteProfileInt(SECTION, _T("Direct changes"), m_Settings.DirectChanges ? 1 : 0);
		theApp.WriteProfileInt(SECTION, _T("Claude Code in WSL"), m_Settings.Wsl ? 1 : 0);
	}

	static LRESULT CALLBACK WindowProc(HWND hWnd, UINT Message, WPARAM wParam, LPARAM lParam)
	{
		CMcpLink *pLink = reinterpret_cast<CMcpLink*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
		if (Message == WM_MCP_REQUEST) {
			// The listener may have stopped waiting; the exchange lives until both let go
			mcp_exchange_t *pExchange = reinterpret_cast<mcp_exchange_t*>(lParam);
			LRESULT Result = 0;
			if (pLink != NULL && BeginMcpExchange(pExchange))
				Result = pLink->Answer(pExchange);
			EndMcpExchange(pExchange);
			return Result;
		}
		if (pLink != NULL && Message == WM_TIMER && wParam == PLAY_TIMER) {
			pLink->CheckPlayback();
			return 0;
		}
		return DefWindowProc(hWnd, Message, wParam, lParam);
	}

	// A message-only window on the UI thread, where requests are answered
	void CreateWindowOnce()
	{
		if (m_hWindow != NULL)
			return;
		WNDCLASS Class;
		memset(&Class, 0, sizeof(Class));
		Class.lpfnWndProc = &CMcpLink::WindowProc;
		Class.hInstance = AfxGetInstanceHandle();
		Class.lpszClassName = WINDOW_CLASS;
		RegisterClass(&Class);	// fails harmlessly when already registered
		m_hWindow = CreateWindowEx(0, WINDOW_CLASS, _T(""), 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, AfxGetInstanceHandle(), NULL);
		if (m_hWindow != NULL)
			SetWindowLongPtr(m_hWindow, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
	}

	bool Listen(CString &Error)
	{
		m_Server.Stop();
		if (m_hWindow == NULL) {
			Error = _T("FamiTracker couldn't set up the connection.");
			m_sLastError = Error;
			return false;
		}
		char Reason[256] = "";
		if (!m_Server.Start(m_Settings.Port, m_hWindow, WM_MCP_REQUEST, Reason, sizeof(Reason))) {
			Error = Reason;
			m_sLastError = Error;
			return false;
		}
		m_sLastError.Empty();
		return true;
	}

	CMainFrame *m_pMain;
	HWND m_hWindow;
	CMcpSocketServer m_Server;
	ftapi::ToolSet m_Tools;
	mcp_settings_t m_Settings;
	CString m_sLastError;
	bool m_bBusy;		// answering a request (a message box inside one could let another in)
	bool m_bStopping;
	int m_iPlayFirst, m_iPlayEnd;
	bool m_bPlayStarted;
	int m_iPlayTicks;
};

// The dialog

class CConnectClaudeDlg : public CDialog
{
public:
	CConnectClaudeDlg(CMcpLink *pLink, CWnd *pParent) : CDialog(IDD_CONNECT_CLAUDE, pParent), m_pLink(pLink) {}

protected:
	virtual BOOL OnInitDialog();
	virtual void OnOK();
	afx_msg void OnChange();
	afx_msg void OnCopy();
	afx_msg void OnNewToken();

	DECLARE_MESSAGE_MAP()

private:
	int Port() const;
	bool Wsl() const;
	CString CurrentCommand() const;		// empty while the port isn't valid
	void ShowCommand();

	CMcpLink *m_pLink;
	CString m_sToken;
};

BEGIN_MESSAGE_MAP(CConnectClaudeDlg, CDialog)
	ON_EN_CHANGE(IDC_MCP_PORT, OnChange)
	ON_BN_CLICKED(IDC_MCP_ENABLE, OnChange)
	ON_BN_CLICKED(IDC_MCP_WINDOWS, OnChange)
	ON_BN_CLICKED(IDC_MCP_WSL, OnChange)
	ON_BN_CLICKED(IDC_MCP_COPY, OnCopy)
	ON_BN_CLICKED(IDC_MCP_NEWTOKEN, OnNewToken)
END_MESSAGE_MAP()

BOOL CConnectClaudeDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	const mcp_settings_t &Settings = m_pLink->GetSettings();
	m_sToken = Settings.Token;
	CheckDlgButton(IDC_MCP_ENABLE, Settings.Enabled ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_MCP_DIRECT, Settings.DirectChanges ? BST_CHECKED : BST_UNCHECKED);
	CheckRadioButton(IDC_MCP_WINDOWS, IDC_MCP_WSL, Settings.Wsl ? IDC_MCP_WSL : IDC_MCP_WINDOWS);
	SetDlgItemInt(IDC_MCP_PORT, static_cast<UINT>(Settings.Port), FALSE);
	static_cast<CEdit*>(GetDlgItem(IDC_MCP_PORT))->SetLimitText(5);
	SetDlgItemText(IDC_MCP_STATUS, m_pLink->Status());
	ShowCommand();
	return TRUE;
}

int CConnectClaudeDlg::Port() const
{
	BOOL Valid = FALSE;
	const UINT Value = GetDlgItemInt(IDC_MCP_PORT, &Valid, FALSE);
	return (Valid && Value >= 1024 && Value <= 65535) ? static_cast<int>(Value) : 0;
}

bool CConnectClaudeDlg::Wsl() const
{
	return IsDlgButtonChecked(IDC_MCP_WSL) == BST_CHECKED;
}

CString CConnectClaudeDlg::CurrentCommand() const
{
	if (Wsl())
		return m_pLink->WslCommand();	// the helper finds the port by itself
	const int Value = Port();
	return Value > 0 ? m_pLink->Command(Value, m_sToken) : CString();
}

void CConnectClaudeDlg::ShowCommand()
{
	const CString Command = CurrentCommand();
	SetDlgItemText(IDC_MCP_COMMAND, Command.IsEmpty() ? CString(_T("(Pick a port from 1024 to 65535.)")) : Command);
	if (!Wsl()) {
		SetDlgItemText(IDC_MCP_WHERE, _T("Run this in a terminal (PowerShell or Command Prompt):"));
		SetDlgItemText(IDC_MCP_HINT, _T("It needs Claude Code installed. The access code in the command keeps other programs and web pages out. ")
			_T("Then start claude in any folder and ask, for example: \"make the atmosphere darker\" or \"add a bass line\"."));
	}
	else {
		SetDlgItemText(IDC_MCP_WHERE, _T("Run this in your WSL terminal, where you start claude:"));
		if (CMcpLink::HelperFound())
			SetDlgItemText(IDC_MCP_HINT, _T("Claude Code in WSL then starts famitracker-mcp.exe from FamiTracker's folder, which passes its requests ")
				_T("on to FamiTracker here: no network setup, and new access codes work without connecting again. ")
				_T("Then start claude and ask, for example: \"add a bass line\"."));
		else
			SetDlgItemText(IDC_MCP_HINT, _T("famitracker-mcp.exe is missing from FamiTracker's folder. It comes in the same zip as FamiTracker.exe: ")
				_T("put it next to FamiTracker.exe, then run the command."));
	}
}

void CConnectClaudeDlg::OnChange()
{
	ShowCommand();
}

void CConnectClaudeDlg::OnCopy()
{
	const CStringA Text(CurrentCommand());
	if (Text.IsEmpty())
		return;
	if (!OpenClipboard())
		return;
	EmptyClipboard();
	HGLOBAL hMemory = GlobalAlloc(GMEM_MOVEABLE, static_cast<SIZE_T>(Text.GetLength() + 1));
	if (hMemory != NULL) {
		char *pTarget = static_cast<char*>(GlobalLock(hMemory));
		if (pTarget != NULL) {
			memcpy(pTarget, static_cast<LPCSTR>(Text), static_cast<size_t>(Text.GetLength() + 1));
			GlobalUnlock(hMemory);
			if (SetClipboardData(CF_TEXT, hMemory) == NULL)
				GlobalFree(hMemory);
		}
		else
			GlobalFree(hMemory);
	}
	CloseClipboard();
	SetDlgItemText(IDC_MCP_STATUS, Wsl() ? _T("Copied. Paste it into your WSL terminal and press Enter.") :
		_T("Copied. Paste it into a terminal and press Enter."));
	m_pLink->RememberWsl(Wsl());
}

void CConnectClaudeDlg::OnNewToken()
{
	m_sToken = NewToken();
	ShowCommand();
	SetDlgItemText(IDC_MCP_STATUS, Wsl() ? _T("New access code: Claude Code in WSL uses it after OK, without connecting again.") :
		_T("New access code: after OK, connect again with the new command (claude mcp remove famitracker first)."));
}

void CConnectClaudeDlg::OnOK()
{
	mcp_settings_t Settings;
	Settings.Enabled = IsDlgButtonChecked(IDC_MCP_ENABLE) == BST_CHECKED;
	Settings.DirectChanges = IsDlgButtonChecked(IDC_MCP_DIRECT) == BST_CHECKED;
	Settings.Wsl = Wsl();
	Settings.Port = Port();
	Settings.Token = m_sToken;
	if (Settings.Port == 0) {
		AfxMessageBox(_T("Pick a port from 1024 to 65535."), MB_ICONWARNING);
		GetDlgItem(IDC_MCP_PORT)->SetFocus();
		return;
	}
	CString Error;
	if (!m_pLink->Apply(Settings, Error)) {
		SetDlgItemText(IDC_MCP_STATUS, Error);
		AfxMessageBox(Error, MB_ICONWARNING);
		return;
	}
	CDialog::OnOK();
}

// CMcpConnection

struct CMcpConnection::Impl {
	CMcpLink Link;
};

CMcpConnection::CMcpConnection() : m_pImpl(new Impl())
{
}

CMcpConnection::~CMcpConnection()
{
	delete m_pImpl;
}

void CMcpConnection::Start(CMainFrame *pMainFrame)
{
	m_pImpl->Link.Start(pMainFrame);
}

void CMcpConnection::Stop()
{
	m_pImpl->Link.Shutdown();
}

void CMcpConnection::ShowDialog()
{
	CConnectClaudeDlg Dialog(&m_pImpl->Link, AfxGetMainWnd());
	Dialog.DoModal();
}

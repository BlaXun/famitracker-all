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

#include "stdafx.h"
#include <string>
#include <vector>
#include "FamiTracker.h"
#include "FamiTrackerDoc.h"
#include "MainFrm.h"
#include "AppFolders.h"
#include "Clips.h"
#include "InsertRecording.h"
#include "InsertRecordingDlg.h"
#include "McpConnection.h"
#include "PhoneLink.h"
#include "Api/PhoneTakes.h"

namespace {

const UINT PROCESS_MS = 300;	// a take becomes a clip this long after it arrived (the page's answer goes first)
const UINT RETRY_MS = 1500;		// and tries again this often while FamiTracker is busy

// UTF-8 to this computer's code page, as FamiTracker keeps names; Exact is false when a character had to be replaced
CString Ansi(const std::string &Text, bool *pExact = NULL)
{
	if (pExact != NULL)
		*pExact = true;
	if (Text.empty())
		return CString();
	const int Wide = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, Text.c_str(), static_cast<int>(Text.size()), NULL, 0);
	if (Wide <= 0) {
		if (pExact != NULL)
			*pExact = false;
		return CString();
	}
	std::vector<wchar_t> Chars(static_cast<size_t>(Wide));
	MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, Text.c_str(), static_cast<int>(Text.size()), &Chars[0], Wide);
	BOOL Replaced = FALSE;
	const int Size = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, &Chars[0], Wide, NULL, 0, NULL, &Replaced);
	if (Size <= 0) {
		if (pExact != NULL)
			*pExact = false;
		return CString();
	}
	std::vector<char> Bytes(static_cast<size_t>(Size) + 1, 0);
	WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, &Chars[0], Wide, &Bytes[0], Size, NULL, &Replaced);
	if (pExact != NULL && Replaced)
		*pExact = false;
	return CString(&Bytes[0], Size);
}

std::string Utf8(const CString &Text)
{
	if (Text.IsEmpty())
		return std::string();
	const int Wide = MultiByteToWideChar(CP_ACP, 0, Text, Text.GetLength(), NULL, 0);
	if (Wide <= 0)
		return std::string();
	std::vector<wchar_t> Chars(static_cast<size_t>(Wide));
	MultiByteToWideChar(CP_ACP, 0, Text, Text.GetLength(), &Chars[0], Wide);
	const int Size = WideCharToMultiByte(CP_UTF8, 0, &Chars[0], Wide, NULL, 0, NULL, NULL);
	if (Size <= 0)
		return std::string();
	std::vector<char> Bytes(static_cast<size_t>(Size));
	WideCharToMultiByte(CP_UTF8, 0, &Chars[0], Wide, &Bytes[0], Size, NULL, NULL);
	return std::string(&Bytes[0], static_cast<size_t>(Size));
}

class CPhoneLink;
CPhoneLink *g_pLink = NULL;

class CPhoneLink : public ftapi::PhoneHost
{
public:
	explicit CPhoneLink(CMainFrame *pMainFrame) : m_pMainFrame(pMainFrame), m_Takes(*this), m_iTimer(0)
	{
		// The page, built into FamiTracker.exe
		HINSTANCE hInstance = AfxGetInstanceHandle();
		HRSRC hResource = ::FindResource(hInstance, MAKEINTRESOURCE(IDR_PHONE_PAGE), RT_RCDATA);
		HGLOBAL hData = hResource != NULL ? ::LoadResource(hInstance, hResource) : NULL;
		const void *pData = hData != NULL ? ::LockResource(hData) : NULL;
		if (pData != NULL)
			m_Takes.SetPage(std::string(static_cast<const char*>(pData), ::SizeofResource(hInstance, hResource)));
	}

	virtual ~CPhoneLink()
	{
		if (m_iTimer != 0)
			::KillTimer(NULL, m_iTimer);
	}

	ftapi::PhoneTakes &Takes() { return m_Takes; }

	bool SaveTake(const std::vector<std::string> &Names, const std::string &Bytes, std::string &Path, std::string &File,
		std::string &Error)
	{
		const CString Folder = RecordingsFolder(true);
		if (Folder.IsEmpty()) {
			Error = "FamiTracker found no folder for its recordings.";
			return false;
		}
		for (size_t i = 0; i < Names.size(); ++i) {
			bool Exact = true;
			const CString Name = Ansi(Names[i], &Exact);
			if (!Exact || Name.GetLength() < 5)
				continue;	// this computer can't spell it: the next one, plainer
			for (int Number = 1; Number < 100; ++Number) {
				CString Try = Name;
				if (Number > 1)
					Try.Format(_T("%s %d.wav"), static_cast<LPCTSTR>(Name.Left(Name.GetLength() - 4)), Number);
				const CString Full = Folder + _T("\\") + Try;
				// Never over another file
				HANDLE hFile = ::CreateFile(Full, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
				if (hFile == INVALID_HANDLE_VALUE) {
					const DWORD Problem = ::GetLastError();
					if (Problem == ERROR_FILE_EXISTS || Problem == ERROR_ALREADY_EXISTS)
						continue;
					break;
				}
				DWORD Written = 0;
				const BOOL Wrote = ::WriteFile(hFile, Bytes.data(), static_cast<DWORD>(Bytes.size()), &Written, NULL);
				const BOOL Closed = ::CloseHandle(hFile);
				if (!Wrote || !Closed || Written != Bytes.size()) {
					::DeleteFile(Full);
					Error = "Couldn't write all of " + Utf8(Full) + " (is the disk full?).";
					return false;
				}
				Path = Utf8(Full);
				File = Utf8(Try);
				return true;
			}
		}
		Error = "FamiTracker couldn't make a file for the take in " + Utf8(Folder) + ".";
		return false;
	}

	bool CanMakeClip(std::string &Why)
	{
		CFamiTrackerDoc *pDoc = Doc();
		if (pDoc == NULL || !pDoc->IsFileLoaded()) {
			Why = "No song is open in FamiTracker.";
			return false;
		}
		if (!m_pMainFrame->IsWindowEnabled()) {
			Why = "A dialog is open in FamiTracker.";
			return false;
		}
		CMcpConnection *pConnection = m_pMainFrame->GetMcpConnection();
		if (pConnection != NULL && pConnection->IsBusy()) {
			Why = "FamiTracker is busy with Claude Code.";
			return false;
		}
		if (::GetCapture() != NULL) {
			Why = "Something is being dragged in FamiTracker.";
			return false;
		}
		return true;
	}

	bool MakeClip(const std::string &Path, const std::string &Idea, std::string &Clip, std::string &Message)
	{
		CFamiTrackerDoc *pDoc = Doc();
		const CString File = Ansi(Path);
		recording_kind_t Kind = RECORDING_VOICE;
		CString Heard, Problem;
		if (!GuessRecording(File, Kind, Heard, Problem)) {
			Message = "Kept in the Recordings folder, but FamiTracker couldn't read it: " + Utf8(Problem);
			return false;
		}
		// Named after the idea, "Idea 1" without one; a number added when a clip has the name
		CString Base = Ansi(Idea);
		Base.Replace(_T('\r'), _T(' '));
		Base.Replace(_T('\n'), _T(' '));
		Base.Trim();
		CClipLibrary *pClips = pDoc->GetClips();
		const CString Name = Base.IsEmpty() ? pClips->NewName(_T("Idea"), true) : pClips->NewName(Base, false);
		CString Text;
		if (!InsertRecording(m_pMainFrame, File, Kind, DefaultRecordingChannel(pDoc, -1), true, Text, Name)) {
			Message = "Kept in the Recordings folder, but it didn't become a clip: " + Utf8(Text);
			return false;
		}
		// What it holds
		int Notes = 0;
		const int Index = pClips->GetCount() - 1;
		if (Index >= 0 && pClips->GetName(Index) == Name)
			for (int Row = 0; Row < pClips->GetRows(Index); ++Row) {
				stChanNote Cell;
				pClips->GetCell(Index, 0, Row, Cell);
				if (Cell.Note >= C && Cell.Note <= B)
					++Notes;
			}
		CString Holds;
		Holds.Format(Kind == RECORDING_DRUMS ? _T("%d drum hit%s") : _T("%d note%s"), Notes, Notes == 1 ? _T("") : _T("s"));
		Clip = Utf8(Name);
		Message = "In the clips as \"" + Clip + "\": " + Utf8(Holds) + ", heard as " + Utf8(Heard) + ".";
		// In sight at once
		m_pMainFrame->ShowClipsPanel(true);
		m_pMainFrame->SongChanged();
		m_pMainFrame->SetMessageText(_T("An idea from your phone is in the clips: \"") + Name + _T("\" (") + Holds + _T(")."));
		return true;
	}

	std::string Now()
	{
		SYSTEMTIME Time;
		::GetLocalTime(&Time);
		char Text[32];
		_snprintf_s(Text, sizeof(Text), _TRUNCATE, "%04d-%02d-%02d %02d-%02d-%02d", Time.wYear, Time.wMonth, Time.wDay, Time.wHour,
			Time.wMinute, Time.wSecond);
		return Text;
	}

	void Wake()
	{
		Schedule(PROCESS_MS);
	}

private:
	CFamiTrackerDoc *Doc() const
	{
		return static_cast<CFamiTrackerDoc*>(m_pMainFrame->GetActiveDocument());
	}

	void Schedule(UINT Delay)
	{
		m_iTimer = ::SetTimer(NULL, m_iTimer, Delay, &CPhoneLink::OnTimer);
	}

	static void CALLBACK OnTimer(HWND, UINT, UINT_PTR Id, DWORD)
	{
		if (g_pLink == NULL) {
			::KillTimer(NULL, Id);
			return;
		}
		g_pLink->Tick();
	}

	void Tick()
	{
		if (m_iTimer != 0)
			::KillTimer(NULL, m_iTimer);
		m_iTimer = 0;
		if (m_Takes.Process())
			Schedule(RETRY_MS);
	}

	CMainFrame *m_pMainFrame;
	ftapi::PhoneTakes m_Takes;
	UINT_PTR m_iTimer;
};

}	// namespace

ftapi::PhoneTakes *GetPhoneTakes(CMainFrame *pMainFrame)
{
	if (g_pLink == NULL && pMainFrame != NULL)
		g_pLink = new CPhoneLink(pMainFrame);
	return g_pLink != NULL ? &g_pLink->Takes() : NULL;
}

void ClosePhoneTakes()
{
	delete g_pLink;
	g_pLink = NULL;
}

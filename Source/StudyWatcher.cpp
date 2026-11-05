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
#include "StudyWatcher.h"
#include "AppFolders.h"
#include "Api/StudyInbox.h"

#include <algorithm>
#include <ctime>
#include <map>
#include <vector>

namespace {

using namespace ftapi;

const DWORD POLL_MS = 2000;					// how often the folder is looked at
const LPCTSTR DONE = _T("Done"), FAILED = _T("Failed"), LOG = _T("Study log.txt"), ABOUT = _T("What goes here.txt");

HANDLE g_hThread = NULL;
HANDLE g_hStop = NULL;						// set to stop
volatile LONG g_Stopping = 0;
HWND g_hMain = NULL;
CString g_Folder, g_References;

std::string Utf8Of(const CString &Text)
{
	const int Wide = MultiByteToWideChar(CP_ACP, 0, Text, -1, NULL, 0);
	if (Wide <= 0)
		return std::string();
	std::vector<wchar_t> W(static_cast<size_t>(Wide));
	MultiByteToWideChar(CP_ACP, 0, Text, -1, &W[0], Wide);
	const int Bytes = WideCharToMultiByte(CP_UTF8, 0, &W[0], -1, NULL, 0, NULL, NULL);
	if (Bytes <= 0)
		return std::string();
	std::vector<char> Out(static_cast<size_t>(Bytes));
	WideCharToMultiByte(CP_UTF8, 0, &W[0], -1, &Out[0], Bytes, NULL, NULL);
	return std::string(&Out[0]);
}

CString AnsiOf(const std::string &Text)
{
	const int Wide = MultiByteToWideChar(CP_UTF8, 0, Text.c_str(), -1, NULL, 0);
	if (Wide <= 0)
		return CString();
	std::vector<wchar_t> W(static_cast<size_t>(Wide));
	MultiByteToWideChar(CP_UTF8, 0, Text.c_str(), -1, &W[0], Wide);
	return CString(&W[0]);
}

void Tell(const CString &Text)
{
	if (g_hMain != NULL && IsWindow(g_hMain)) {
		CString *pText = new CString(Text);
		if (!PostMessage(g_hMain, WM_STUDY_NEWS, 0, reinterpret_cast<LPARAM>(pText)))
			delete pText;
	}
}

bool ReadWhole(const CString &Path, std::string &Bytes, size_t Most = 64 * 1024 * 1024)
{
	Bytes.clear();
	FILE *pFile = NULL;
	if (_tfopen_s(&pFile, Path, _T("rb")) != 0 || pFile == NULL)
		return false;
	char Buffer[65536];
	size_t Read;
	while ((Read = fread(Buffer, 1, sizeof(Buffer), pFile)) > 0 && Bytes.size() < Most)
		Bytes.append(Buffer, Read);
	fclose(pFile);
	return true;
}

bool WriteWhole(const CString &Path, const std::string &Bytes, bool Append = false)
{
	FILE *pFile = NULL;
	if (_tfopen_s(&pFile, Path, Append ? _T("ab") : _T("wb")) != 0 || pFile == NULL)
		return false;
	const bool Ok = fwrite(Bytes.data(), 1, Bytes.size(), pFile) == Bytes.size();
	return fclose(pFile) == 0 && Ok;
}

bool Exists(const CString &Path)
{
	return GetFileAttributes(Path) != INVALID_FILE_ATTRIBUTES;
}

std::string Now(bool Seconds)
{
	const std::time_t T = std::time(0);
	std::tm Local;
	if (localtime_s(&Local, &T) != 0)
		return std::string();
	char Buffer[32];
	std::strftime(Buffer, sizeof(Buffer), Seconds ? "%Y-%m-%d %H:%M" : "%Y-%m-%d", &Local);
	return Buffer;
}

// The files in the folder (not its folders, the log or the note about it)
std::vector<StudyFolderEntry> ListFolder()
{
	std::vector<StudyFolderEntry> Out;
	WIN32_FIND_DATA Found;
	HANDLE hFind = FindFirstFile(g_Folder + _T("\\*"), &Found);
	if (hFind == INVALID_HANDLE_VALUE)
		return Out;
	do {
		if ((Found.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) != 0)
			continue;
		const CString Name(Found.cFileName);
		if (Name.CompareNoCase(LOG) == 0 || Name.CompareNoCase(ABOUT) == 0 || Name.Left(1) == _T(".") ||
			Name.Left(2) == _T("~$") || Name.Right(4).CompareNoCase(_T(".tmp")) == 0 ||
			Name.Right(11).CompareNoCase(_T(".crdownload")) == 0 || Name.Right(5).CompareNoCase(_T(".part")) == 0)
			continue;
		const unsigned long long Size = (static_cast<unsigned long long>(Found.nFileSizeHigh) << 32) | Found.nFileSizeLow;
		const unsigned long long Time = (static_cast<unsigned long long>(Found.ftLastWriteTime.dwHighDateTime) << 32) |
			Found.ftLastWriteTime.dwLowDateTime;
		Out.push_back(StudyFolderEntry(Utf8Of(Name), Size, Time));
	} while (FindNextFile(hFind, &Found));
	FindClose(hFind);
	return Out;
}

// Not still being written: it opens for reading with nobody writing it
bool Settled(const CString &Path)
{
	HANDLE hFile = CreateFile(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
		return false;
	CloseHandle(hFile);
	return true;
}

// Into Into (Done or Failed) under a name of its own there; returns the name it got, empty when it couldn't move
CString MoveInto(const CString &Path, const CString &Name, LPCTSTR Into)
{
	const CString Folder = g_Folder + _T("\\") + Into;
	CreateDirectory(Folder, NULL);
	const std::string Free = FreeStudyName(Utf8Of(Name), [&](const std::string &Try) {
		return Exists(Folder + _T("\\") + AnsiOf(Try)); });
	const CString Target = AnsiOf(Free);
	return MoveFileEx(Path, Folder + _T("\\") + Target, MOVEFILE_COPY_ALLOWED) ? Target : CString();
}

void Log(const std::string &Line)
{
	WriteWhole(g_Folder + _T("\\") + LOG, Line + "\r\n", true);
}

void StudyOne(const std::string &NameUtf8, std::vector<std::string> &Known)
{
	const CString Name = AnsiOf(NameUtf8);
	const CString Path = g_Folder + _T("\\") + Name;
	if (!Settled(Path))
		return;		// still being copied: the next poll
	std::string Bytes;
	if (!ReadWhole(Path, Bytes))
		return;
	const std::string Key = StudyContentKey(Bytes);
	Bytes.clear();
	if (std::find(Known.begin(), Known.end(), Key) != Known.end()) {
		const CString Moved = MoveInto(Path, Name, DONE);
		Log(StudyLogLine(Now(true), NameUtf8, "studied before (the same file), so not again; moved to Done" +
			(Moved.IsEmpty() ? std::string(" (it couldn't be moved)") : std::string()), Key));
		Tell(_T("Study folder: ") + Name + _T(" was studied before; it's in Done."));
		return;
	}
	// Where it will be once done, for the references' sources (references restudy finds it there)
	const CString DoneFolder = g_Folder + _T("\\") + DONE;
	CreateDirectory(DoneFolder, NULL);
	const std::string DoneName = FreeStudyName(NameUtf8, [&](const std::string &Try) {
		return Exists(DoneFolder + _T("\\") + AnsiOf(Try)); });
	const std::string Source = Utf8Of(DoneFolder) + "\\" + DoneName;

	StudyLibrary Library;
	Library.Read = [](const std::string &File, std::string &Out) {
		return ReadWhole(g_References + _T("\\") + AnsiOf(File), Out, 4 * 1024 * 1024);
	};
	Library.Save = [](const std::string &File, const std::string &Out, std::string &Error) {
		if (WriteWhole(g_References + _T("\\") + AnsiOf(File), Out))
			return true;
		Error = "Couldn't write " + Utf8Of(g_References) + "\\" + File + " (is it open somewhere else?).";
		return false;
	};
	Tell(_T("Study folder: studying ") + Name + _T("..."));
	const StudyFileResult Result = StudyFileIntoLibrary(std::string(static_cast<LPCTSTR>(Path)), NameUtf8, Source, Now(false), Library,
		[&](const std::string &Doing) {
			if (InterlockedCompareExchange(&g_Stopping, 0, 0) != 0)
				return false;
			Tell(_T("Study folder: ") + Name + _T(", ") + AnsiOf(Doing) + _T("..."));
			return true;
		});
	if (!Result.Ok && Result.Error == "stopped")
		return;		// FamiTracker is closing: it stays for next time
	if (Result.Ok) {
		const CString Moved = MoveFileEx(Path, DoneFolder + _T("\\") + AnsiOf(DoneName), MOVEFILE_COPY_ALLOWED) ?
			AnsiOf(DoneName) : CString();
		Log(StudyLogLine(Now(true), NameUtf8, Result.What + " (in the reference library)" +
			(Moved.IsEmpty() ? std::string("; it couldn't be moved to Done") : std::string()), Key));
		Known.push_back(Key);
		Tell(_T("Study folder: ") + Name + _T(": ") + AnsiOf(Result.What) + _T(". The studies are in the reference library."));
		return;
	}
	const CString Moved = MoveInto(Path, Name, FAILED);
	if (!Moved.IsEmpty())
		WriteWhole(g_Folder + _T("\\") + FAILED + _T("\\") + Moved + _T(".txt"), "FamiTracker couldn't study " + NameUtf8 +
			":\r\n" + Result.Error + "\r\n");
	Log(StudyLogLine(Now(true), NameUtf8, "failed: " + Result.Error + (Moved.IsEmpty() ? std::string(" (it couldn't be moved)") :
		std::string("; moved to Failed")), Key));
	Tell(_T("Study folder: ") + Name + _T(" couldn't be studied: ") + AnsiOf(Result.Error));
}

DWORD WINAPI Watch(LPVOID)
{
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
	std::string LogText;
	ReadWhole(g_Folder + _T("\\") + LOG, LogText);
	std::vector<std::string> Known = StudiedKeys(LogText);
	std::vector<StudyFolderEntry> Before;
	std::map<std::string, int> Tries;		// a file that never opens is tried a while, then left
	while (WaitForSingleObject(g_hStop, POLL_MS) == WAIT_TIMEOUT) {
		const std::vector<StudyFolderEntry> Now = ListFolder();
		const std::vector<std::string> Ready = StudyFilesReady(Now, Before);
		Before = Now;
		for (size_t i = 0; i < Ready.size() && InterlockedCompareExchange(&g_Stopping, 0, 0) == 0; ++i) {
			if (++Tries[Ready[i]] > 30)
				continue;
			StudyOne(Ready[i], Known);
			Before.clear();			// time passed while it studied: look again before the next
			break;
		}
	}
	return 0;
}

}	// namespace

void StartStudyWatcher(HWND hMainWindow)
{
	if (g_hThread != NULL)
		return;
	g_Folder = StudyFolder(true);
	g_References = ReferencesFolder(true);
	if (g_Folder.IsEmpty() || g_References.IsEmpty())
		return;
	CreateDirectory(g_Folder + _T("\\") + DONE, NULL);
	CreateDirectory(g_Folder + _T("\\") + FAILED, NULL);
	if (!Exists(g_Folder + _T("\\") + ABOUT))
		WriteWhole(g_Folder + _T("\\") + ABOUT, "Drop files here for FamiTracker to study: " + StudyFileKinds() + ".\r\n\r\n"
			"While FamiTracker runs, each one is studied (every music track of an NSF, every track of a FamiTracker module) and "
			"its studies are kept in the reference library (the References folder next to FamiTracker.exe), where Claude Code's "
			"references tool, Songsmith's Like and the sound browser find them. Then the file moves into Done, or into Failed "
			"with a note why. \"Study log.txt\" lists every file. A file studied before goes to Done without studying it "
			"again.\r\n");
	g_hMain = hMainWindow;
	InterlockedExchange(&g_Stopping, 0);
	g_hStop = CreateEvent(NULL, TRUE, FALSE, NULL);
	g_hThread = CreateThread(NULL, 0, Watch, NULL, 0, NULL);
}

void StopStudyWatcher()
{
	if (g_hThread == NULL)
		return;
	InterlockedExchange(&g_Stopping, 1);
	g_hMain = NULL;
	SetEvent(g_hStop);
	if (WaitForSingleObject(g_hThread, 3000) == WAIT_OBJECT_0) {
		CloseHandle(g_hThread);
		CloseHandle(g_hStop);
	}
	g_hThread = NULL;		// else it ends with FamiTracker, at its next track
}

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
#include <shlobj.h>
#include "AppFolders.h"

namespace {

bool IsFolder(const CString &Path)
{
	const DWORD Attributes = GetFileAttributes(Path);
	return !Path.IsEmpty() && Attributes != INVALID_FILE_ATTRIBUTES && (Attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

// Documents\Name, made when Create; empty when there's no Documents
CString DocumentsFolder(LPCTSTR Name, bool Create)
{
	TCHAR Documents[MAX_PATH];
	if (FAILED(SHGetFolderPath(NULL, CSIDL_PERSONAL | (Create ? CSIDL_FLAG_CREATE : 0), NULL, SHGFP_TYPE_CURRENT, Documents)))
		return CString();
	CString Folder(Documents);
	Folder += _T("\\");
	Folder += Name;
	if (Create)
		CreateDirectory(Folder, NULL);	// fails harmlessly when it's there already
	return Folder;
}

// The files of From that match Pattern move to To, to another drive too (one
// of the same name in To stays where it was), and From goes once it's empty
void MoveFiles(const CString &From, const CString &To, LPCTSTR Pattern)
{
	WIN32_FIND_DATA Found;
	HANDLE hFind = FindFirstFile(From + _T("\\") + Pattern, &Found);
	if (hFind != INVALID_HANDLE_VALUE) {
		do {
			if ((Found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
				MoveFileEx(From + _T("\\") + Found.cFileName, To + _T("\\") + Found.cFileName, MOVEFILE_COPY_ALLOWED);
		} while (FindNextFile(hFind, &Found));
		FindClose(hFind);
	}
	RemoveDirectory(From);	// only when empty
}

// The folder Name next to FamiTracker.exe, which the files of the folders in
// Old (Count of them, the first in Documents, called InDocuments) move into;
// Documents\InDocuments where FamiTracker's folder can't be written
CString FolderBeside(LPCTSTR Name, const CString *Old, int Count, LPCTSTR InDocuments, LPCTSTR Pattern, bool Create)
{
	const CString Exe = ExeFolder();
	const CString Documents = DocumentsFolder(InDocuments, false);
	if (!Exe.IsEmpty()) {
		const CString Beside = Exe + _T("\\") + Name;
		bool OldThere = false;
		for (int i = 0; i < Count; ++i)
			OldThere = OldThere || IsFolder(Old[i]);
		if (!IsFolder(Beside) && (Create || OldThere))
			CreateDirectory(Beside, NULL);	// fails where FamiTracker's folder can't be written
		if (IsFolder(Beside)) {
			for (int i = 0; i < Count; ++i)
				if (IsFolder(Old[i]))
					MoveFiles(Old[i], Beside, Pattern);
			return Beside;
		}
		if (!IsFolder(Documents) && !Create)
			return Beside;	// nothing kept yet: the first goes next to FamiTracker if it can
	}
	return Create ? DocumentsFolder(InDocuments, true) : Documents;
}

}	// namespace

CString ExeFolder()
{
	TCHAR Path[MAX_PATH];
	const DWORD Length = GetModuleFileName(NULL, Path, MAX_PATH);
	if (Length == 0 || Length >= MAX_PATH)
		return CString();
	const CString Full(Path);
	return Full.Left(Full.ReverseFind(_T('\\')));
}

CString ReferencesFolder(bool Create)
{
	const CString Exe = ExeFolder();
	const CString Old[2] = { DocumentsFolder(_T("FamiTracker references"), false),
		Exe.IsEmpty() ? CString() : Exe + _T("\\FamiTracker references") };
	return FolderBeside(_T("References"), Old, 2, _T("FamiTracker references"), _T("*.json"), Create);
}

CString RecordingsFolder(bool Create)
{
	const CString Old[1] = { DocumentsFolder(_T("FamiTracker recordings"), false) };
	return FolderBeside(_T("Recordings"), Old, 1, _T("FamiTracker recordings"), _T("*"), Create);
}

CString InboxFolder(bool Create)
{
	return FolderBeside(_T("Inbox"), NULL, 0, _T("FamiTracker inbox"), _T("*"), Create);
}

CString SongsFolder(bool Create)
{
	return FolderBeside(_T("Songs"), NULL, 0, _T("FamiTracker songs"), _T("*"), Create);
}

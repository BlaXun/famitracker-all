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

// Stand-in for DocumentFile.h: one module block in memory, written and read the
// same way (little-endian ints, NUL-terminated strings)
#pragma once
#include <vector>
#include <string>
// A module file in memory: one block
class CDocumentFile {
public:
	std::vector<unsigned char> data; std::string id; int version = 0; int pos = 0;
	void CreateBlock(const char *ID, int Version) { id = ID; version = Version; data.clear(); }
	void WriteBlockInt(int v) { for (int i = 0; i < 4; ++i) data.push_back((unsigned char)((unsigned)v >> (8 * i))); }
	void WriteBlockChar(char c) { data.push_back((unsigned char)c); }
	void WriteString(CString s) { for (int i = 0; i < s.GetLength(); ++i) WriteBlockChar(s.GetAt(i)); WriteBlockChar(0); }
	bool FlushBlock() { pos = 0; return true; }
	int GetBlockVersion() const { return version; }
	int GetBlockPos() const { return pos; }
	int GetBlockSize() const { return int(data.size()); }
	int GetBlockInt() { unsigned v = 0; for (int i = 0; i < 4; ++i) v |= unsigned(data.at(size_t(pos++))) << (8 * i); return int(v); }
	char GetBlockChar() { return (char)data.at(size_t(pos++)); }
};

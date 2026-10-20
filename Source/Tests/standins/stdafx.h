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

// Stand-in for stdafx.h: the MFC pieces Clips.cpp uses, working, so it runs
// on Linux (see CMakeLists.txt)
#pragma once
#include <string>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <strings.h>
typedef int BOOL; typedef char TCHAR; typedef const char *LPCTSTR;
#define TRUE 1
#define FALSE 0
#define _T(x) x
class CString {
public:
	CString() {}
	CString(const char *s) : m_s(s ? s : "") {}
	CString(const CString &o) : m_s(o.m_s) {}
	CString &operator=(const CString &o) { m_s = o.m_s; return *this; }
	CString &operator=(const char *s) { m_s = s ? s : ""; return *this; }
	CString &operator+=(const char *s) { m_s += s; return *this; }
	CString &operator+=(const CString &o) { m_s += o.m_s; return *this; }
	CString &operator+=(char c) { m_s += c; return *this; }
	friend CString operator+(const CString &a, const char *b) { CString r(a); r += b; return r; }
	friend bool operator==(const CString &a, const char *b) { return a.m_s == b; }
	void Format(const char *f, ...) { char buf[1024]; va_list ap; va_start(ap, f); vsnprintf(buf, sizeof(buf), f, ap); va_end(ap); m_s = buf; }
	CString Left(int n) const { CString r; r.m_s = m_s.substr(0, size_t(n)); return r; }
	int CompareNoCase(const char *s) const { return strcasecmp(m_s.c_str(), s); }
	int GetLength() const { return int(m_s.size()); }
	char GetAt(int i) const { return m_s[size_t(i)]; }
	bool IsEmpty() const { return m_s.empty(); }
	operator const char *() const { return m_s.c_str(); }
	std::string m_s;
};

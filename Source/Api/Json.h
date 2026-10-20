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

// A small JSON value with a strict reader and a compact writer (RFC 8259),
// for the Claude Code connection (MCP). Objects keep their members in the
// order they were added, so what is written is deterministic. Strings are
// UTF-8; the writer replaces bytes that aren't valid UTF-8 with U+FFFD.

#include <string>
#include <utility>
#include <vector>

namespace ftapi {

class Json {
public:
	enum Type { NUL, BOOLEAN, NUMBER, STRING, ARRAY, OBJECT };

	Json();							// null
	Json(bool Value);
	Json(int Value);
	Json(long long Value);
	Json(double Value);
	Json(const char *Value);
	Json(const std::string &Value);

	static Json MakeArray();
	static Json MakeObject();
	static Json NumberText(const std::string &Text);	// a number written exactly as Text (a valid JSON number)

	Type GetType() const { return m_Type; }
	bool IsNull() const { return m_Type == NUL; }
	bool IsBool() const { return m_Type == BOOLEAN; }
	bool IsNumber() const { return m_Type == NUMBER; }
	bool IsString() const { return m_Type == STRING; }
	bool IsArray() const { return m_Type == ARRAY; }
	bool IsObject() const { return m_Type == OBJECT; }
	bool IsInteger() const;			// a number without a fraction, within +-2^53

	bool Bool() const { return m_bBool; }
	double Number() const { return m_dNumber; }
	long long Integer() const;		// the number, rounded towards zero
	const std::string &String() const { return m_sText; }	// a string's value

	// Arrays
	size_t Size() const;			// items of an array, members of an object
	const Json &At(size_t Index) const;
	Json &Push(const Json &Value);	// appends to an array; returns the new item
	const std::vector<Json> &Items() const { return m_Items; }

	// Objects
	const Json *Get(const std::string &Key) const;	// NULL if missing
	Json &Set(const std::string &Key, const Json &Value);	// replaces or appends; returns *this
	const std::vector<std::pair<std::string, Json> > &Members() const { return m_Members; }

	bool operator==(const Json &Other) const;
	bool operator!=(const Json &Other) const { return !(*this == Other); }

private:
	friend std::string WriteJson(const Json &Value);
	friend class JsonReader;

	Type m_Type;
	bool m_bBool;
	double m_dNumber;
	std::string m_sText;			// a string's value, or a number's text
	std::vector<Json> m_Items;
	std::vector<std::pair<std::string, Json> > m_Members;
};

// Reads JSON text: one value with optional white space around it, nested at
// most 64 deep. On failure Error says where and why.
bool ParseJson(const std::string &Text, Json &Out, std::string *pError = 0);

// Compact JSON text
std::string WriteJson(const Json &Value);

// Text that is valid UTF-8: invalid bytes become U+FFFD
std::string ValidUtf8(const std::string &Text);

}	// namespace ftapi

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

#include "Json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace ftapi {

namespace {

const double MAX_SAFE_INTEGER = 9007199254740991.0;	// 2^53 - 1
const int MAX_DEPTH = 64;

const Json &NullValue()
{
	static const Json Null;
	return Null;
}

void AppendUtf8(std::string &Out, unsigned long Code)
{
	if (Code < 0x80)
		Out += static_cast<char>(Code);
	else if (Code < 0x800) {
		Out += static_cast<char>(0xC0 | (Code >> 6));
		Out += static_cast<char>(0x80 | (Code & 0x3F));
	}
	else if (Code < 0x10000) {
		Out += static_cast<char>(0xE0 | (Code >> 12));
		Out += static_cast<char>(0x80 | ((Code >> 6) & 0x3F));
		Out += static_cast<char>(0x80 | (Code & 0x3F));
	}
	else {
		Out += static_cast<char>(0xF0 | (Code >> 18));
		Out += static_cast<char>(0x80 | ((Code >> 12) & 0x3F));
		Out += static_cast<char>(0x80 | ((Code >> 6) & 0x3F));
		Out += static_cast<char>(0x80 | (Code & 0x3F));
	}
}

// The length of the valid UTF-8 sequence at Text[Pos], 0 if it isn't one
size_t Utf8Length(const std::string &Text, size_t Pos)
{
	const unsigned char c = static_cast<unsigned char>(Text[Pos]);
	size_t Length;
	unsigned long Code;
	if (c < 0x80)
		return 1;
	else if (c >= 0xC2 && c <= 0xDF) { Length = 2; Code = c & 0x1F; }
	else if (c >= 0xE0 && c <= 0xEF) { Length = 3; Code = c & 0x0F; }
	else if (c >= 0xF0 && c <= 0xF4) { Length = 4; Code = c & 0x07; }
	else
		return 0;
	if (Pos + Length > Text.size())
		return 0;
	for (size_t i = 1; i < Length; ++i) {
		const unsigned char d = static_cast<unsigned char>(Text[Pos + i]);
		if ((d & 0xC0) != 0x80)
			return 0;
		Code = (Code << 6) | (d & 0x3F);
	}
	if ((Length == 3 && Code < 0x800) || (Length == 4 && (Code < 0x10000 || Code > 0x10FFFF)) || (Code >= 0xD800 && Code <= 0xDFFF))
		return 0;	// overlong, out of range or a surrogate
	return Length;
}

void WriteString(std::string &Out, const std::string &Text)
{
	static const char HEX[] = "0123456789abcdef";
	Out += '"';
	for (size_t i = 0; i < Text.size(); ) {
		const unsigned char c = static_cast<unsigned char>(Text[i]);
		if (c < 0x80) {
			switch (c) {
			case '"': Out += "\\\""; break;
			case '\\': Out += "\\\\"; break;
			case '\b': Out += "\\b"; break;
			case '\f': Out += "\\f"; break;
			case '\n': Out += "\\n"; break;
			case '\r': Out += "\\r"; break;
			case '\t': Out += "\\t"; break;
			default:
				if (c < 0x20 || c == 0x7F) {
					Out += "\\u00";
					Out += HEX[c >> 4];
					Out += HEX[c & 15];
				}
				else
					Out += static_cast<char>(c);
			}
			++i;
			continue;
		}
		const size_t Length = Utf8Length(Text, i);
		if (Length == 0) {
			Out += "\xEF\xBF\xBD";	// U+FFFD
			++i;
		}
		else {
			Out.append(Text, i, Length);
			i += Length;
		}
	}
	Out += '"';
}

void WriteNumber(std::string &Out, const Json &Value)
{
	const double d = Value.Number();
	if (!(d == d) || std::fabs(d) > 1e308) {	// NaN or infinite: JSON has neither
		Out += "null";
		return;
	}
	char Buffer[40];
	if (Value.IsInteger())
		std::snprintf(Buffer, sizeof(Buffer), "%.0f", d);
	else
		std::snprintf(Buffer, sizeof(Buffer), "%.15g", d);
	Out += Buffer;
}

}	// namespace

// Reading

class JsonReader {
public:
	JsonReader(const std::string &Text) : m_Text(Text), m_iPos(0) {}

	bool Read(Json &Out, std::string &Error)
	{
		SkipSpace();
		if (!Value(Out, 0, Error))
			return false;
		SkipSpace();
		if (m_iPos != m_Text.size())
			return Fail(Error, "unexpected text after the value");
		return true;
	}

private:
	bool Fail(std::string &Error, const char *What)
	{
		char Where[48];
		std::snprintf(Where, sizeof(Where), " at character %lu", static_cast<unsigned long>(m_iPos + 1));
		Error = What;
		Error += Where;
		return false;
	}

	void SkipSpace()
	{
		while (m_iPos < m_Text.size()) {
			const char c = m_Text[m_iPos];
			if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
				break;
			++m_iPos;
		}
	}

	bool Literal(const char *Word)
	{
		size_t i = 0;
		for (; Word[i] != 0; ++i)
			if (m_iPos + i >= m_Text.size() || m_Text[m_iPos + i] != Word[i])
				return false;
		m_iPos += i;
		return true;
	}

	bool Value(Json &Out, int Depth, std::string &Error)
	{
		if (Depth > MAX_DEPTH)
			return Fail(Error, "nested too deep");
		if (m_iPos >= m_Text.size())
			return Fail(Error, "unexpected end of text");
		const char c = m_Text[m_iPos];
		switch (c) {
		case '{': return Object(Out, Depth, Error);
		case '[': return Array(Out, Depth, Error);
		case '"': {
			std::string Text;
			if (!String(Text, Error))
				return false;
			Out = Json(Text);
			return true;
		}
		case 't':
			if (!Literal("true"))
				return Fail(Error, "unexpected character");
			Out = Json(true);
			return true;
		case 'f':
			if (!Literal("false"))
				return Fail(Error, "unexpected character");
			Out = Json(false);
			return true;
		case 'n':
			if (!Literal("null"))
				return Fail(Error, "unexpected character");
			Out = Json();
			return true;
		}
		if (c == '-' || (c >= '0' && c <= '9'))
			return Number(Out, Error);
		return Fail(Error, "unexpected character");
	}

	bool Number(Json &Out, std::string &Error)
	{
		const size_t Start = m_iPos;
		if (m_Text[m_iPos] == '-')
			++m_iPos;
		if (m_iPos >= m_Text.size())
			return Fail(Error, "bad number");
		if (m_Text[m_iPos] == '0')
			++m_iPos;
		else if (m_Text[m_iPos] >= '1' && m_Text[m_iPos] <= '9') {
			while (m_iPos < m_Text.size() && m_Text[m_iPos] >= '0' && m_Text[m_iPos] <= '9')
				++m_iPos;
		}
		else
			return Fail(Error, "bad number");
		if (m_iPos < m_Text.size() && m_Text[m_iPos] == '.') {
			++m_iPos;
			if (m_iPos >= m_Text.size() || m_Text[m_iPos] < '0' || m_Text[m_iPos] > '9')
				return Fail(Error, "bad number");
			while (m_iPos < m_Text.size() && m_Text[m_iPos] >= '0' && m_Text[m_iPos] <= '9')
				++m_iPos;
		}
		if (m_iPos < m_Text.size() && (m_Text[m_iPos] == 'e' || m_Text[m_iPos] == 'E')) {
			++m_iPos;
			if (m_iPos < m_Text.size() && (m_Text[m_iPos] == '+' || m_Text[m_iPos] == '-'))
				++m_iPos;
			if (m_iPos >= m_Text.size() || m_Text[m_iPos] < '0' || m_Text[m_iPos] > '9')
				return Fail(Error, "bad number");
			while (m_iPos < m_Text.size() && m_Text[m_iPos] >= '0' && m_Text[m_iPos] <= '9')
				++m_iPos;
		}
		Out = Json::NumberText(m_Text.substr(Start, m_iPos - Start));
		return true;
	}

	bool Hex4(unsigned long &Code, std::string &Error)
	{
		if (m_iPos + 4 > m_Text.size())
			return Fail(Error, "bad \\u escape");
		Code = 0;
		for (int i = 0; i < 4; ++i) {
			const char c = m_Text[m_iPos++];
			Code <<= 4;
			if (c >= '0' && c <= '9') Code |= c - '0';
			else if (c >= 'a' && c <= 'f') Code |= c - 'a' + 10;
			else if (c >= 'A' && c <= 'F') Code |= c - 'A' + 10;
			else
				return Fail(Error, "bad \\u escape");
		}
		return true;
	}

	bool String(std::string &Out, std::string &Error)
	{
		++m_iPos;	// the opening quote
		for (;;) {
			if (m_iPos >= m_Text.size())
				return Fail(Error, "unterminated string");
			const unsigned char c = static_cast<unsigned char>(m_Text[m_iPos]);
			if (c == '"') {
				++m_iPos;
				return true;
			}
			if (c < 0x20)
				return Fail(Error, "control character in a string");
			if (c != '\\') {
				Out += static_cast<char>(c);
				++m_iPos;
				continue;
			}
			++m_iPos;
			if (m_iPos >= m_Text.size())
				return Fail(Error, "unterminated string");
			const char e = m_Text[m_iPos++];
			switch (e) {
			case '"': Out += '"'; break;
			case '\\': Out += '\\'; break;
			case '/': Out += '/'; break;
			case 'b': Out += '\b'; break;
			case 'f': Out += '\f'; break;
			case 'n': Out += '\n'; break;
			case 'r': Out += '\r'; break;
			case 't': Out += '\t'; break;
			case 'u': {
				unsigned long Code;
				if (!Hex4(Code, Error))
					return false;
				if (Code >= 0xD800 && Code <= 0xDBFF) {
					// A high surrogate wants a low one after it
					unsigned long Low = 0;
					if (m_iPos + 1 < m_Text.size() && m_Text[m_iPos] == '\\' && m_Text[m_iPos + 1] == 'u') {
						const size_t Back = m_iPos;
						m_iPos += 2;
						if (!Hex4(Low, Error))
							return false;
						if (Low >= 0xDC00 && Low <= 0xDFFF)
							Code = 0x10000 + ((Code - 0xD800) << 10) + (Low - 0xDC00);
						else {
							m_iPos = Back;
							Code = 0xFFFD;
						}
					}
					else
						Code = 0xFFFD;
				}
				else if (Code >= 0xDC00 && Code <= 0xDFFF)
					Code = 0xFFFD;
				AppendUtf8(Out, Code);
				break;
			}
			default:
				return Fail(Error, "bad escape in a string");
			}
		}
	}

	bool Array(Json &Out, int Depth, std::string &Error)
	{
		++m_iPos;
		Out = Json::MakeArray();
		SkipSpace();
		if (m_iPos < m_Text.size() && m_Text[m_iPos] == ']') {
			++m_iPos;
			return true;
		}
		for (;;) {
			SkipSpace();
			Json Item;
			if (!Value(Item, Depth + 1, Error))
				return false;
			Out.Push(Item);
			SkipSpace();
			if (m_iPos >= m_Text.size())
				return Fail(Error, "unterminated array");
			const char c = m_Text[m_iPos++];
			if (c == ']')
				return true;
			if (c != ',')
				return Fail(Error, "expected , or ] in an array");
		}
	}

	bool Object(Json &Out, int Depth, std::string &Error)
	{
		++m_iPos;
		Out = Json::MakeObject();
		SkipSpace();
		if (m_iPos < m_Text.size() && m_Text[m_iPos] == '}') {
			++m_iPos;
			return true;
		}
		for (;;) {
			SkipSpace();
			if (m_iPos >= m_Text.size() || m_Text[m_iPos] != '"')
				return Fail(Error, "expected a member name");
			std::string Key;
			if (!String(Key, Error))
				return false;
			SkipSpace();
			if (m_iPos >= m_Text.size() || m_Text[m_iPos] != ':')
				return Fail(Error, "expected : after a member name");
			++m_iPos;
			SkipSpace();
			Json Item;
			if (!Value(Item, Depth + 1, Error))
				return false;
			Out.Set(Key, Item);
			SkipSpace();
			if (m_iPos >= m_Text.size())
				return Fail(Error, "unterminated object");
			const char c = m_Text[m_iPos++];
			if (c == '}')
				return true;
			if (c != ',')
				return Fail(Error, "expected , or } in an object");
		}
	}

	const std::string &m_Text;
	size_t m_iPos;
};

// The value

Json::Json() : m_Type(NUL), m_bBool(false), m_dNumber(0.0)
{
}

Json::Json(bool Value) : m_Type(BOOLEAN), m_bBool(Value), m_dNumber(0.0)
{
}

Json::Json(int Value) : m_Type(NUMBER), m_bBool(false), m_dNumber(Value)
{
}

Json::Json(long long Value) : m_Type(NUMBER), m_bBool(false), m_dNumber(static_cast<double>(Value))
{
}

Json::Json(double Value) : m_Type(NUMBER), m_bBool(false), m_dNumber(Value)
{
}

Json::Json(const char *Value) : m_Type(STRING), m_bBool(false), m_dNumber(0.0), m_sText(Value != 0 ? Value : "")
{
}

Json::Json(const std::string &Value) : m_Type(STRING), m_bBool(false), m_dNumber(0.0), m_sText(Value)
{
}

Json Json::MakeArray()
{
	Json Value;
	Value.m_Type = ARRAY;
	return Value;
}

Json Json::MakeObject()
{
	Json Value;
	Value.m_Type = OBJECT;
	return Value;
}

Json Json::NumberText(const std::string &Text)
{
	Json Value;
	Value.m_Type = NUMBER;
	Value.m_dNumber = std::strtod(Text.c_str(), 0);
	Value.m_sText = Text;
	return Value;
}

bool Json::IsInteger() const
{
	return m_Type == NUMBER && std::fabs(m_dNumber) <= MAX_SAFE_INTEGER && m_dNumber == std::floor(m_dNumber);
}

long long Json::Integer() const
{
	if (m_Type != NUMBER || !(m_dNumber == m_dNumber))
		return 0;
	if (m_dNumber > MAX_SAFE_INTEGER)
		return static_cast<long long>(MAX_SAFE_INTEGER);
	if (m_dNumber < -MAX_SAFE_INTEGER)
		return -static_cast<long long>(MAX_SAFE_INTEGER);
	return static_cast<long long>(m_dNumber);
}

size_t Json::Size() const
{
	if (m_Type == ARRAY)
		return m_Items.size();
	if (m_Type == OBJECT)
		return m_Members.size();
	return 0;
}

const Json &Json::At(size_t Index) const
{
	return (m_Type == ARRAY && Index < m_Items.size()) ? m_Items[Index] : NullValue();
}

Json &Json::Push(const Json &Value)
{
	if (m_Type != ARRAY) {
		*this = MakeArray();
	}
	m_Items.push_back(Value);
	return m_Items.back();
}

const Json *Json::Get(const std::string &Key) const
{
	if (m_Type != OBJECT)
		return 0;
	for (size_t i = 0; i < m_Members.size(); ++i)
		if (m_Members[i].first == Key)
			return &m_Members[i].second;
	return 0;
}

Json &Json::Set(const std::string &Key, const Json &Value)
{
	if (m_Type != OBJECT)
		*this = MakeObject();
	for (size_t i = 0; i < m_Members.size(); ++i) {
		if (m_Members[i].first == Key) {
			m_Members[i].second = Value;
			return *this;
		}
	}
	m_Members.push_back(std::make_pair(Key, Value));
	return *this;
}

bool Json::operator==(const Json &Other) const
{
	if (m_Type != Other.m_Type)
		return false;
	switch (m_Type) {
	case NUL: return true;
	case BOOLEAN: return m_bBool == Other.m_bBool;
	case NUMBER: return m_dNumber == Other.m_dNumber;
	case STRING: return m_sText == Other.m_sText;
	case ARRAY: return m_Items == Other.m_Items;
	case OBJECT:
		if (m_Members.size() != Other.m_Members.size())
			return false;
		for (size_t i = 0; i < m_Members.size(); ++i) {
			const Json *pOther = Other.Get(m_Members[i].first);
			if (pOther == 0 || !(*pOther == m_Members[i].second))
				return false;
		}
		return true;
	}
	return false;
}

bool ParseJson(const std::string &Text, Json &Out, std::string *pError)
{
	std::string Error;
	JsonReader Reader(Text);
	Json Value;
	if (!Reader.Read(Value, Error)) {
		if (pError != 0)
			*pError = Error;
		return false;
	}
	Out = Value;
	return true;
}

std::string WriteJson(const Json &Value)
{
	std::string Out;
	switch (Value.m_Type) {
	case Json::NUL: Out = "null"; break;
	case Json::BOOLEAN: Out = Value.m_bBool ? "true" : "false"; break;
	case Json::NUMBER:
		if (!Value.m_sText.empty())
			Out = Value.m_sText;	// exactly as it was read (request IDs)
		else
			WriteNumber(Out, Value);
		break;
	case Json::STRING: WriteString(Out, Value.m_sText); break;
	case Json::ARRAY:
		Out += '[';
		for (size_t i = 0; i < Value.m_Items.size(); ++i) {
			if (i > 0)
				Out += ',';
			Out += WriteJson(Value.m_Items[i]);
		}
		Out += ']';
		break;
	case Json::OBJECT:
		Out += '{';
		for (size_t i = 0; i < Value.m_Members.size(); ++i) {
			if (i > 0)
				Out += ',';
			WriteString(Out, Value.m_Members[i].first);
			Out += ':';
			Out += WriteJson(Value.m_Members[i].second);
		}
		Out += '}';
		break;
	}
	return Out;
}

std::string ValidUtf8(const std::string &Text)
{
	std::string Out;
	Out.reserve(Text.size());
	for (size_t i = 0; i < Text.size(); ) {
		const size_t Length = Utf8Length(Text, i);
		if (Length == 0) {
			Out += "\xEF\xBF\xBD";
			++i;
		}
		else {
			Out.append(Text, i, Length);
			i += Length;
		}
	}
	return Out;
}

}	// namespace ftapi

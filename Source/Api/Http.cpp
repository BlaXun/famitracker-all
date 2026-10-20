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

#include "Http.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ftapi {

namespace {

char Lower(char c)
{
	return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

std::string Trim(const std::string &Text)
{
	size_t First = 0, Last = Text.size();
	while (First < Last && (Text[First] == ' ' || Text[First] == '\t'))
		++First;
	while (Last > First && (Text[Last - 1] == ' ' || Text[Last - 1] == '\t'))
		--Last;
	return Text.substr(First, Last - First);
}

bool IsToken(const std::string &Text)
{
	if (Text.empty())
		return false;
	for (size_t i = 0; i < Text.size(); ++i) {
		const unsigned char c = static_cast<unsigned char>(Text[i]);
		if (c <= 0x20 || c >= 0x7F || std::strchr("()<>@,;:\\\"/[]?={}", c) != 0)
			return false;
	}
	return true;
}

// "a, b" contains "b" (a comma-separated header list), ignoring case
bool ListHas(const std::string &List, const char *Item)
{
	size_t Start = 0;
	while (Start <= List.size()) {
		size_t End = List.find(',', Start);
		if (End == std::string::npos)
			End = List.size();
		if (EqualsNoCase(Trim(List.substr(Start, End - Start)), Item))
			return true;
		Start = End + 1;
	}
	return false;
}

}	// namespace

bool EqualsNoCase(const std::string &a, const char *b)
{
	const size_t Length = std::strlen(b);
	if (a.size() != Length)
		return false;
	for (size_t i = 0; i < Length; ++i)
		if (Lower(a[i]) != Lower(b[i]))
			return false;
	return true;
}

const std::string *HttpRequest::Header(const char *Name) const
{
	for (size_t i = 0; i < Headers.size(); ++i)
		if (EqualsNoCase(Headers[i].first, Name))
			return &Headers[i].second;
	return 0;
}

bool HttpRequest::KeepAlive() const
{
	const std::string *pConnection = Header("Connection");
	if (Version == "HTTP/1.0")
		return pConnection != 0 && ListHas(*pConnection, "keep-alive");
	return pConnection == 0 || !ListHas(*pConnection, "close");
}

std::string HttpRequest::Path() const
{
	const size_t Query = Target.find('?');
	return Query == std::string::npos ? Target : Target.substr(0, Query);
}

HttpRequestParser::HttpRequestParser(size_t MaxBody, size_t MaxHead) :
	m_iMaxBody(MaxBody), m_iMaxHead(MaxHead), m_State(HEAD), m_iBodyLeft(0), m_bContinue(false), m_iError(0)
{
}

HttpRequestParser::Result HttpRequestParser::Fail(int Status)
{
	m_iError = Status;
	return BAD;
}

HttpRequestParser::Result HttpRequestParser::Feed(const char *pData, size_t Size)
{
	if (m_iError != 0)
		return BAD;
	if (Size > 0)
		m_Buffer.append(pData, Size);
	if (m_State == DONE)
		return COMPLETE;
	if (m_State == HEAD) {
		// Empty lines before a request are allowed (RFC 9112, 2.2)
		while (m_Buffer.size() >= 2 && m_Buffer[0] == '\r' && m_Buffer[1] == '\n')
			m_Buffer.erase(0, 2);
		const size_t End = m_Buffer.find("\r\n\r\n");
		if (End == std::string::npos)
			return m_Buffer.size() > m_iMaxHead ? Fail(431) : NEED_MORE;
		if (End > m_iMaxHead)
			return Fail(431);
		const Result Head = ParseHead(End);
		if (Head != NEED_MORE)
			return Head;
	}
	return ParseBody();
}

HttpRequestParser::Result HttpRequestParser::ParseHead(size_t End)
{
	const std::string Head = m_Buffer.substr(0, End);
	m_Buffer.erase(0, End + 4);
	m_Request = HttpRequest();

	size_t LineEnd = Head.find("\r\n");
	const std::string RequestLine = Head.substr(0, LineEnd);
	const size_t Space1 = RequestLine.find(' ');
	const size_t Space2 = Space1 == std::string::npos ? std::string::npos : RequestLine.find(' ', Space1 + 1);
	if (Space1 == std::string::npos || Space2 == std::string::npos || RequestLine.find(' ', Space2 + 1) != std::string::npos)
		return Fail(400);
	m_Request.Method = RequestLine.substr(0, Space1);
	m_Request.Target = RequestLine.substr(Space1 + 1, Space2 - Space1 - 1);
	m_Request.Version = RequestLine.substr(Space2 + 1);
	if (!IsToken(m_Request.Method) || m_Request.Target.empty())
		return Fail(400);
	if (m_Request.Version != "HTTP/1.1" && m_Request.Version != "HTTP/1.0")
		return Fail(505);

	while (LineEnd != std::string::npos) {
		const size_t Start = LineEnd + 2;
		LineEnd = Head.find("\r\n", Start);
		const std::string Line = Head.substr(Start, LineEnd == std::string::npos ? std::string::npos : LineEnd - Start);
		const size_t Colon = Line.find(':');
		if (Colon == std::string::npos || !IsToken(Line.substr(0, Colon)))
			return Fail(400);	// also rejects obsolete line folding
		if (m_Request.Headers.size() >= 100)
			return Fail(431);
		m_Request.Headers.push_back(std::make_pair(Line.substr(0, Colon), Trim(Line.substr(Colon + 1))));
	}

	const std::string *pEncoding = m_Request.Header("Transfer-Encoding");
	const std::string *pLength = m_Request.Header("Content-Length");
	if (pEncoding != 0) {
		if (!EqualsNoCase(*pEncoding, "chunked") || pLength != 0)
			return Fail(pLength != 0 ? 400 : 501);
		m_State = CHUNK_SIZE;
	}
	else if (pLength != 0) {
		if (pLength->empty() || pLength->size() > 12 || pLength->find_first_not_of("0123456789") != std::string::npos)
			return Fail(400);
		unsigned long long Length = 0;
		for (size_t i = 0; i < pLength->size(); ++i)
			Length = Length * 10 + static_cast<unsigned>((*pLength)[i] - '0');
		if (Length > m_iMaxBody)
			return Fail(413);
		m_iBodyLeft = static_cast<size_t>(Length);
		m_State = BODY;
	}
	else {
		m_iBodyLeft = 0;
		m_State = BODY;
	}

	const std::string *pExpect = m_Request.Header("Expect");
	if (pExpect != 0 && EqualsNoCase(*pExpect, "100-continue") && m_Request.Version == "HTTP/1.1")
		m_bContinue = true;
	return NEED_MORE;
}

HttpRequestParser::Result HttpRequestParser::ParseBody()
{
	for (;;) {
		switch (m_State) {
		case HEAD:
			return NEED_MORE;
		case DONE:
			return COMPLETE;
		case BODY: {
			const size_t Take = m_iBodyLeft < m_Buffer.size() ? m_iBodyLeft : m_Buffer.size();
			m_Request.Body.append(m_Buffer, 0, Take);
			m_Buffer.erase(0, Take);
			m_iBodyLeft -= Take;
			if (m_iBodyLeft > 0)
				return NEED_MORE;
			m_State = DONE;
			m_bContinue = false;
			return COMPLETE;
		}
		case CHUNK_SIZE: {
			const size_t End = m_Buffer.find("\r\n");
			if (End == std::string::npos)
				return m_Buffer.size() > 1024 ? Fail(400) : NEED_MORE;
			std::string Line = m_Buffer.substr(0, End);
			m_Buffer.erase(0, End + 2);
			const size_t Extension = Line.find(';');
			if (Extension != std::string::npos)
				Line.erase(Extension);
			Line = Trim(Line);
			if (Line.empty() || Line.size() > 8 || Line.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
				return Fail(400);
			const size_t Size = static_cast<size_t>(std::strtoul(Line.c_str(), 0, 16));
			if (Size == 0) {
				m_State = CHUNK_TRAILER;
				break;
			}
			if (m_Request.Body.size() + Size > m_iMaxBody)
				return Fail(413);
			m_iBodyLeft = Size;
			m_State = CHUNK_DATA;
			break;
		}
		case CHUNK_DATA: {
			if (m_iBodyLeft > 0) {
				const size_t Take = m_iBodyLeft < m_Buffer.size() ? m_iBodyLeft : m_Buffer.size();
				m_Request.Body.append(m_Buffer, 0, Take);
				m_Buffer.erase(0, Take);
				m_iBodyLeft -= Take;
				if (m_iBodyLeft > 0)
					return NEED_MORE;
			}
			if (m_Buffer.size() < 2)
				return NEED_MORE;
			if (m_Buffer[0] != '\r' || m_Buffer[1] != '\n')
				return Fail(400);
			m_Buffer.erase(0, 2);
			m_State = CHUNK_SIZE;
			break;
		}
		case CHUNK_TRAILER: {
			// Trailer fields are skipped up to the empty line
			const size_t End = m_Buffer.find("\r\n");
			if (End == std::string::npos)
				return m_Buffer.size() > m_iMaxHead ? Fail(431) : NEED_MORE;
			m_Buffer.erase(0, End + 2);
			if (End == 0) {
				m_State = DONE;
				m_bContinue = false;
				return COMPLETE;
			}
			break;
		}
		}
	}
}

HttpRequest HttpRequestParser::Take()
{
	HttpRequest Request = m_Request;
	m_Request = HttpRequest();
	m_State = HEAD;
	m_iBodyLeft = 0;
	m_bContinue = false;
	return Request;
}

bool HttpRequestParser::TakeContinue()
{
	if (!m_bContinue || m_State == DONE || m_State == HEAD)
		return false;
	m_bContinue = false;
	return true;
}

const char *HttpReason(int Status)
{
	switch (Status) {
	case 100: return "Continue";
	case 200: return "OK";
	case 202: return "Accepted";
	case 204: return "No Content";
	case 400: return "Bad Request";
	case 401: return "Unauthorized";
	case 403: return "Forbidden";
	case 404: return "Not Found";
	case 405: return "Method Not Allowed";
	case 406: return "Not Acceptable";
	case 408: return "Request Timeout";
	case 413: return "Content Too Large";
	case 415: return "Unsupported Media Type";
	case 431: return "Request Header Fields Too Large";
	case 500: return "Internal Server Error";
	case 501: return "Not Implemented";
	case 503: return "Service Unavailable";
	case 505: return "HTTP Version Not Supported";
	}
	return "Unknown";
}

std::string FormatHttpResponse(const HttpResponse &Response, bool KeepAlive)
{
	char Line[96];
	std::snprintf(Line, sizeof(Line), "HTTP/1.1 %d %s\r\n", Response.Status, HttpReason(Response.Status));
	std::string Out = Line;
	if (!Response.ContentType.empty()) {
		Out += "Content-Type: ";
		Out += Response.ContentType;
		Out += "\r\n";
	}
	std::snprintf(Line, sizeof(Line), "Content-Length: %lu\r\n", static_cast<unsigned long>(Response.Body.size()));
	Out += Line;
	for (size_t i = 0; i < Response.Headers.size(); ++i) {
		Out += Response.Headers[i].first;
		Out += ": ";
		Out += Response.Headers[i].second;
		Out += "\r\n";
	}
	Out += "Cache-Control: no-store\r\n";
	Out += KeepAlive ? "Connection: keep-alive\r\n" : "Connection: close\r\n";
	Out += "\r\n";
	Out += Response.Body;
	return Out;
}

std::string Base64Encode(const std::string &Bytes)
{
	static const char DIGITS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string Out;
	Out.reserve((Bytes.size() + 2) / 3 * 4);
	size_t i = 0;
	for (; i + 2 < Bytes.size(); i += 3) {
		const unsigned long Bits = (static_cast<unsigned long>(static_cast<unsigned char>(Bytes[i])) << 16) |
			(static_cast<unsigned long>(static_cast<unsigned char>(Bytes[i + 1])) << 8) | static_cast<unsigned char>(Bytes[i + 2]);
		Out += DIGITS[(Bits >> 18) & 63];
		Out += DIGITS[(Bits >> 12) & 63];
		Out += DIGITS[(Bits >> 6) & 63];
		Out += DIGITS[Bits & 63];
	}
	if (i + 1 == Bytes.size()) {
		const unsigned long Bits = static_cast<unsigned long>(static_cast<unsigned char>(Bytes[i])) << 16;
		Out += DIGITS[(Bits >> 18) & 63];
		Out += DIGITS[(Bits >> 12) & 63];
		Out += "==";
	}
	else if (i + 2 == Bytes.size()) {
		const unsigned long Bits = (static_cast<unsigned long>(static_cast<unsigned char>(Bytes[i])) << 16) |
			(static_cast<unsigned long>(static_cast<unsigned char>(Bytes[i + 1])) << 8);
		Out += DIGITS[(Bits >> 18) & 63];
		Out += DIGITS[(Bits >> 12) & 63];
		Out += DIGITS[(Bits >> 6) & 63];
		Out += '=';
	}
	return Out;
}

}	// namespace ftapi

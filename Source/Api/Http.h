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

// HTTP/1.1 framing for the Claude Code connection: reading requests as their
// bytes arrive (Content-Length or chunked bodies, keep-alive, pipelining) and
// writing responses. No sockets here; McpSocket.cpp moves the bytes.

#include <string>
#include <utility>
#include <vector>

namespace ftapi {

struct HttpRequest {
	std::string Method;
	std::string Target;		// "/mcp", with any query
	std::string Version;	// "HTTP/1.1"
	std::vector<std::pair<std::string, std::string> > Headers;	// as received, values trimmed
	std::string Body;

	const std::string *Header(const char *Name) const;	// case-insensitive; NULL if missing
	bool KeepAlive() const;
	std::string Path() const;							// the target without its query
};

struct HttpResponse {
	int Status;
	std::string ContentType;	// empty when there's no body
	std::string Body;
	std::vector<std::pair<std::string, std::string> > Headers;

	HttpResponse() : Status(200) {}
};

class HttpRequestParser {
public:
	enum Result { NEED_MORE, COMPLETE, BAD };

	explicit HttpRequestParser(size_t MaxBody = 8u << 20, size_t MaxHead = 32u << 10);

	// Adds received bytes. COMPLETE: Take() the request (more may follow in
	// the bytes already given). BAD: answer with ErrorStatus() and close.
	Result Feed(const char *pData, size_t Size);
	Result Feed() { return Feed(0, 0); }	// goes on with bytes already given
	HttpRequest Take();
	int ErrorStatus() const { return m_iError; }

	// The client sent "Expect: 100-continue" and waits for "100 Continue"
	// before the body; true once, when that's due
	bool TakeContinue();

	bool HasPartial() const { return !m_Buffer.empty() || m_State != HEAD; }

private:
	Result Fail(int Status);
	Result ParseHead(size_t End);
	Result ParseBody();

	enum State { HEAD, BODY, CHUNK_SIZE, CHUNK_DATA, CHUNK_TRAILER, DONE };

	size_t m_iMaxBody, m_iMaxHead;
	std::string m_Buffer;	// bytes not used yet
	State m_State;
	HttpRequest m_Request;
	size_t m_iBodyLeft;		// Content-Length or the current chunk
	bool m_bContinue;
	int m_iError;
};

const char *HttpReason(int Status);

// The response as bytes, with Content-Length and Connection headers
std::string FormatHttpResponse(const HttpResponse &Response, bool KeepAlive);

bool EqualsNoCase(const std::string &a, const char *b);

// Base64 with padding (RFC 4648), for images in answers and Mcp-Name headers
std::string Base64Encode(const std::string &Bytes);

}	// namespace ftapi

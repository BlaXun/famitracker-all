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

// The Claude Code connection for Claude Code in WSL (famitracker-mcp.exe):
// Claude Code starts the program as an MCP server and talks to it in lines of
// JSON-RPC on stdin and stdout. WSL runs Windows programs on Windows, so the
// program reaches FamiTracker's endpoint on Windows' 127.0.0.1, which Linux
// in WSL can't (WSL's network has a 127.0.0.1 of its own). Each line becomes
// the HTTP request Claude Code would have sent itself, and each answer a line
// again. No sockets here; BridgeSocket.cpp moves the bytes. With a host, an
// MCP client that only speaks stdio, on another computer of the user's
// network (famitracker-mcp-linux, built for Linux), reaches FamiTracker there.

#include <string>
#include "Http.h"
#include "Json.h"

namespace ftapi {

// A line from Claude Code on its way to FamiTracker
struct BridgeMessage {
	HttpRequest Request;	// POST /mcp with the headers FamiTracker checks
	bool Readable;			// the line was a JSON-RPC message
	bool WantsAnswer;		// a request: Claude Code waits for the answer with Id
	Json Id;
	std::string Method;		// the request's or notification's method ("initialize", "tools/call"...)

	BridgeMessage() : Readable(false), WantsAnswer(false) {}
};

// The line (without its line break) as the request to FamiTracker on
// Host:Port (127.0.0.1 when Host is empty: an IPv4 address or a computer's
// name) at Path (/mcp, or /mcp/core for the small set of tools) with the
// access token. Version: the protocol version the client agreed in initialize
// (BridgeAgreedVersion), sent as MCP-Protocol-Version with the requests after
// it, as an HTTP client sends it from 2025-06-18 on (FamiTracker then knows
// what its replies may hold, a link to a resource say). False for a blank line.
bool BridgeRequest(const std::string &Line, int Port, const std::string &Token, BridgeMessage &Out,
	const std::string &Host = std::string(), const std::string &Path = std::string("/mcp"),
	const std::string &Version = std::string());

// The protocol version FamiTracker agreed when Sent was an initialize request,
// read from its answer; empty otherwise
std::string BridgeAgreedVersion(const BridgeMessage &Sent, const HttpResponse &Answer);

// What Claude Code gets back: a line ending in '\n', or nothing when no
// answer is due (notifications, Claude Code's own answers)
std::string BridgeAnswer(const BridgeMessage &Sent, const HttpResponse &Answer);

// The same when FamiTracker couldn't be asked; Reason is for the user
std::string BridgeFailure(const BridgeMessage &Sent, const std::string &Reason);

// HTTP/1.1 from the client's side: the request's bytes, with Content-Length
std::string FormatHttpRequest(const HttpRequest &Request);

// Reads a response from the bytes received so far. Closed: the server has
// closed the connection, so a body without a length ends there.
enum HttpResponseRead { RESPONSE_NEED_MORE, RESPONSE_COMPLETE, RESPONSE_BAD };
HttpResponseRead ReadHttpResponse(const std::string &Bytes, bool Closed, HttpResponse &Out);

}	// namespace ftapi

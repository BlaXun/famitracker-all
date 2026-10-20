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

// The Model Context Protocol (MCP) over Streamable HTTP, for Claude Code:
// one endpoint, /mcp, taking JSON-RPC requests by POST and answering each
// with a JSON object. Both eras of the protocol are served: per-request
// metadata (2026-07-28: server/discover, no handshake) and the initialize
// handshake of 2025-03-26 to 2025-11-25. Requests are only accepted from
// this computer (Host and Origin are checked) with the access token.

#include <string>
#include "Http.h"

namespace ftapi {

class ToolSet;

struct McpConfig {
	int Port;
	std::string Token;			// required as "Authorization: Bearer <token>"; without one, nothing is answered
	std::string Version;		// FamiTracker's, for serverInfo

	McpConfig() : Port(0) {}
};

const char *const MCP_MODERN_VERSION = "2026-07-28";
const char *const MCP_LEGACY_VERSION = "2025-11-25";

HttpResponse HandleMcp(const HttpRequest &Request, const McpConfig &Config, ToolSet &Tools);

// What a client runs to connect: claude mcp add ...
std::string ClaudeAddCommand(const McpConfig &Config);

// What Claude Code in WSL runs to connect through famitracker-mcp.exe (see
// Bridge.h), given where that is on Windows. Running it again replaces the
// earlier setup.
std::string WslAddCommand(const std::string &HelperPath);

}	// namespace ftapi

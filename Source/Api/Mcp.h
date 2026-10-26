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
// this computer (Host and Origin are checked) with the access token, and
// from other computers on the user's home network when the user allows it
// (McpConfig::Network, Network.h).
//
// The routes, every one needing the access token:
// - POST /mcp: the tools (ToolSet), their instructions and the recipes.
// - POST /mcp/core: a small set of them for local models, which struggle
//   with 50 tools and long instructions (ToolSet::CoreTools), with short
//   instructions (ToolSet::CoreInstructions) and no recipes.
// - GET /files/<name>: an OGG file export_for_game saved next to the song, for
//   a client on another computer, which gets the link in its reply. Those links carry a code of their own in the
//   address (?code=, FilesCode), so a browser opens them without the access
//   code, a phone's too; the code opens nothing else. Ranges are answered, as
//   a phone's browser asks for them before it plays a sound.
// - PUT /inbox/<name>: a file into the Inbox folder next to FamiTracker.exe
//   (the request's body, as the send_file tool takes it in base64), for the
//   tools that read files on this computer (study_nsf, study_module...).
// - /phone and below: the phone page that records ideas into the clips
//   (PhoneTakes.h), with a code of its own, after the checks of the others.

#include <string>
#include <vector>
#include "Http.h"

namespace ftapi {

class ToolSet;
class PhoneTakes;

struct McpConfig {
	int Port;
	std::string Token;			// required as "Authorization: Bearer <token>"; without one, nothing is answered
	std::string Version;		// FamiTracker's, for serverInfo
	// Other computers may connect (FamiTracker listens on the network too): requests from a home network's
	// addresses are answered (others are refused), and Host headers naming this computer by one of its
	// Addresses or its ComputerName are accepted besides 127.0.0.1 and localhost
	bool Network;
	std::vector<std::string> Addresses;	// this computer's IPv4 addresses ("192.168.1.10")
	std::string ComputerName;			// its name on the network ("desktop-pc")
	PhoneTakes *pPhone;					// the phone page's takes (PhoneTakes.h), or none: no phone page

	McpConfig() : Port(0), Network(false), pPhone(0) {}
};

const char *const MCP_MODERN_VERSION = "2026-07-28";
const char *const MCP_LEGACY_VERSION = "2025-11-25";

HttpResponse HandleMcp(const HttpRequest &Request, const McpConfig &Config, ToolSet &Tools);

// The code a link to an exported file carries in its address (?code=): 24 hex
// digits worked out from the access code (HMAC-SHA-256, as the phone page's
// code is), so a new access code changes it, and it opens only GET /files/
std::string FilesCode(const std::string &Token);

// What a client runs to connect: claude mcp add ...
std::string ClaudeAddCommand(const McpConfig &Config);

// The address another computer connects to: http://192.168.1.10:46600/mcp,
// or /mcp/core for the small set of tools
std::string NetworkUrl(const std::string &Address, int Port, bool Core);

// What Claude Code in WSL runs to connect through famitracker-mcp.exe (see
// Bridge.h), given where that is on Windows. Running it again replaces the
// earlier setup.
std::string WslAddCommand(const std::string &HelperPath);

}	// namespace ftapi

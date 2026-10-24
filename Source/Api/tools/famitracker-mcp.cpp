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

// famitracker-mcp: the Claude Code connection for Claude Code in WSL (see
// Api/Bridge.h). Claude Code starts it as an MCP server and sends it lines of
// JSON-RPC; it passes each to FamiTracker on 127.0.0.1 and writes the answer
// back. File > Connect Claude Code in FamiTracker shows the command that sets
// it up. On Windows it reads FamiTracker's port and access code from
// FamiTracker's settings before every message, so a new access code or port
// needs no new setup. With --host it reaches FamiTracker on another computer
// of the user's network instead (with that computer's access code), for MCP
// clients that only start servers over stdio; famitracker-mcp-linux is this
// program built for Linux.

#include "Bridge.h"
#include "BridgeSocket.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#endif

using namespace ftapi;

namespace {

const int DEFAULT_PORT = 46600;
const int TIMEOUT_MS = 660000;	// FamiTracker answers within 10 minutes (an export for games renders) or says it's busy

// Where FamiTracker listens
struct Target {
	std::string Host;	// empty: this computer (127.0.0.1)
	int Port;
	std::string Token;
	bool SwitchedOff;	// FamiTracker's settings say the connection is off

	Target() : Port(DEFAULT_PORT), SwitchedOff(false) {}
};

int g_iPort = 0;			// from the command line; 0: FamiTracker's setting
std::string g_sToken;		// from the command line; empty: FamiTracker's setting
std::string g_sHost;		// from the command line: FamiTracker on another computer; empty: this one
std::string g_sPath = "/mcp";	// "/mcp/core" with --core

#ifdef _WIN32
// HKEY_CURRENT_USER, where FamiTracker keeps its settings (McpConnection.cpp)
const char SETTINGS_KEY[] = "Software\\FamiTracker\\Claude Code";

bool ReadNumber(const char *pName, DWORD &Value)
{
	DWORD Size = sizeof(Value);
	return RegGetValueA(HKEY_CURRENT_USER, SETTINGS_KEY, pName, RRF_RT_REG_DWORD, NULL, &Value, &Size) == ERROR_SUCCESS;
}

bool ReadText(const char *pName, std::string &Value)
{
	char Buffer[256];
	DWORD Size = sizeof(Buffer);
	if (RegGetValueA(HKEY_CURRENT_USER, SETTINGS_KEY, pName, RRF_RT_REG_SZ, NULL, Buffer, &Size) != ERROR_SUCCESS)
		return false;
	Value = Buffer;
	return true;
}
#endif

// Read again for every message
Target CurrentTarget()
{
	Target Out;
	Out.Host = g_sHost;
#ifdef _WIN32
	// This computer's FamiTracker settings; another computer's FamiTracker has its own
	DWORD Number = 0;
	if (g_sHost.empty()) {
		if (ReadNumber("Port", Number) && Number >= 1024 && Number <= 65535)
			Out.Port = static_cast<int>(Number);
		Out.SwitchedOff = !ReadNumber("Enabled", Number) || Number == 0;
		ReadText("Token", Out.Token);
	}
#endif
	if (g_iPort != 0)
		Out.Port = g_iPort;
	if (!g_sToken.empty())
		Out.Token = g_sToken;
	return Out;
}

// For Claude, who tells the user
std::string Explain(BridgeExchangeResult Result, const Target &Where, const std::string &Error)
{
	char Text[640];
	if (!Where.Host.empty()) {
		const char *const SETUP = "In FamiTracker there, File > Connect Claude Code should have \"Let Claude Code connect\" and "
			"\"Allow connections from other computers on my network\" ticked";
		if (Result == EXCHANGE_UNREACHABLE)
			std::snprintf(Text, sizeof(Text), "FamiTracker on %s doesn't listen on port %d: is it running? %s, with port %d.",
				Where.Host.c_str(), Where.Port, SETUP, Where.Port);
		else if (Result == EXCHANGE_NO_ANSWER)
			std::snprintf(Text, sizeof(Text), "%s didn't answer on port %d. %s, and Windows' firewall has to let FamiTracker "
				"through (Windows asks once; allow it on private networks). Is the computer on, and on this network?",
				Where.Host.c_str(), Where.Port, SETUP);
		else
			std::snprintf(Text, sizeof(Text), "FamiTracker on %s didn't answer: %s", Where.Host.c_str(), Error.c_str());
		return Text;
	}
	if (Result == EXCHANGE_UNREACHABLE && Where.SwitchedOff)
		std::snprintf(Text, sizeof(Text), "FamiTracker's connection for Claude Code is switched off. In FamiTracker, open "
			"File > Connect Claude Code and tick \"Let Claude Code connect\".");
	else if (Result == EXCHANGE_UNREACHABLE)
		std::snprintf(Text, sizeof(Text), "FamiTracker isn't running, or it doesn't listen on port %d. Start FamiTracker; "
			"File > Connect Claude Code there shows whether the connection is on.", Where.Port);
	else
		std::snprintf(Text, sizeof(Text), "FamiTracker didn't answer: %s Try again in a moment.", Error.c_str());
	return Text;
}

// A line from stdin without its line break; false at the end
bool ReadLine(std::string &Line)
{
	Line.clear();
	for (;;) {
		const int c = std::getc(stdin);
		if (c == EOF)
			return !Line.empty();
		if (c == '\n')
			return true;
		Line += static_cast<char>(c);
	}
}

void Usage()
{
	std::printf(
		"Usage: famitracker-mcp [--host ADDRESS] [--port N] [--token CODE] [--core]\n"
		"\n"
		"Connects Claude Code in WSL to FamiTracker on Windows. Claude Code starts it\n"
		"as an MCP server; it passes Claude's messages (JSON-RPC, one per line on stdin\n"
		"and stdout) to FamiTracker on 127.0.0.1. Set it up once in WSL with the command\n"
		"that File > Connect Claude Code in FamiTracker shows when you pick WSL:\n"
		"\n"
		"  claude mcp add --transport stdio --scope user famitracker -- \"$(wslpath 'C:\\...\\famitracker-mcp.exe')\"\n"
		"\n"
		"With --host it connects an MCP client on another computer to FamiTracker on\n"
		"the user's PC instead, over the network, once that FamiTracker allows\n"
		"connections from other computers (File > Connect Claude Code shows its\n"
		"addresses and access code):\n"
		"\n"
		"  famitracker-mcp-linux --host 192.168.1.10 --token CODE --core\n"
		"\n"
		"  --host ADDRESS the computer FamiTracker runs on: its address or name\n"
		"                 (default: this computer, 127.0.0.1)\n"
		"  --port N       FamiTracker's port (default: FamiTracker's setting, 46600)\n"
		"  --token CODE   the access code (default: FamiTracker's setting on this\n"
		"                 computer; with --host, give the one its dialog shows)\n"
		"  --core         the small set of tools for local models (/mcp/core)\n");
}

}	// namespace

int main(int argc, char *argv[])
{
	for (int i = 1; i < argc; ++i) {
		const std::string Arg = argv[i];
		if (Arg == "--help" || Arg == "-h" || Arg == "/?") {
			Usage();
			return 0;
		}
		if (Arg == "--port" && i + 1 < argc) {
			char *pEnd = 0;
			const long Value = std::strtol(argv[++i], &pEnd, 10);
			if (*pEnd != '\0' || Value < 1 || Value > 65535) {
				std::fprintf(stderr, "famitracker-mcp: the port should be a number from 1 to 65535.\n");
				return 2;
			}
			g_iPort = static_cast<int>(Value);
		}
		else if (Arg == "--token" && i + 1 < argc)
			g_sToken = argv[++i];
		else if (Arg == "--host" && i + 1 < argc) {
			g_sHost = argv[++i];
			if (g_sHost.empty() || g_sHost.find_first_of(" /:[]") != std::string::npos) {
				std::fprintf(stderr, "famitracker-mcp: --host takes the computer's IPv4 address or name, like 192.168.1.10.\n");
				return 2;
			}
		}
		else if (Arg == "--core")
			g_sPath = "/mcp/core";
		else {
			std::fprintf(stderr, "famitracker-mcp: unknown option %s (see --help).\n", argv[i]);
			return 2;
		}
	}
#ifdef _WIN32
	// Bytes as they are: no CR LF translation, and Ctrl+Z isn't the end
	_setmode(_fileno(stdin), _O_BINARY);
	_setmode(_fileno(stdout), _O_BINARY);
#endif

	const Target First = CurrentTarget();
	std::fprintf(stderr, "famitracker-mcp: passing the MCP client's messages to FamiTracker on %s:%d%s\n",
		First.Host.empty() ? "127.0.0.1" : First.Host.c_str(), First.Port, g_sPath.c_str());
	if (!First.Host.empty() && First.Token.empty())
		std::fprintf(stderr, "famitracker-mcp: without --token, FamiTracker there refuses every request.\n");
	std::string Line;
	while (ReadLine(Line)) {
		const Target Where = CurrentTarget();
		BridgeMessage Message;
		if (!BridgeRequest(Line, Where.Port, Where.Token, Message, Where.Host, g_sPath))
			continue;
		HttpResponse Answer;
		std::string Error;
		const BridgeExchangeResult Result = BridgeExchange(Where.Host, Where.Port, FormatHttpRequest(Message.Request), TIMEOUT_MS, Answer,
			Error);
		std::string Out;
		if (Result == EXCHANGE_ANSWERED)
			Out = BridgeAnswer(Message, Answer);
		else {
			std::fprintf(stderr, "famitracker-mcp: %s\n", Error.c_str());
			Out = BridgeFailure(Message, Explain(Result, Where, Error));
		}
		if (!Out.empty()) {
			std::fwrite(Out.data(), 1, Out.size(), stdout);
			std::fflush(stdout);
		}
	}
	return 0;
}

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
// needs no new setup.

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
const int TIMEOUT_MS = 60000;	// FamiTracker answers within 30 seconds or says it's busy

// Where FamiTracker listens
struct Target {
	int Port;
	std::string Token;
	bool SwitchedOff;	// FamiTracker's settings say the connection is off

	Target() : Port(DEFAULT_PORT), SwitchedOff(false) {}
};

int g_iPort = 0;			// from the command line; 0: FamiTracker's setting
std::string g_sToken;		// from the command line; empty: FamiTracker's setting

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
#ifdef _WIN32
	DWORD Number = 0;
	if (ReadNumber("Port", Number) && Number >= 1024 && Number <= 65535)
		Out.Port = static_cast<int>(Number);
	Out.SwitchedOff = !ReadNumber("Enabled", Number) || Number == 0;
	ReadText("Token", Out.Token);
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
	char Text[320];
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
		"Usage: famitracker-mcp [--port N] [--token CODE]\n"
		"\n"
		"Connects Claude Code in WSL to FamiTracker on Windows. Claude Code starts it\n"
		"as an MCP server; it passes Claude's messages (JSON-RPC, one per line on stdin\n"
		"and stdout) to FamiTracker on 127.0.0.1. Set it up once in WSL with the command\n"
		"that File > Connect Claude Code in FamiTracker shows when you pick WSL:\n"
		"\n"
		"  claude mcp add --transport stdio --scope user famitracker -- \"$(wslpath 'C:\\...\\famitracker-mcp.exe')\"\n"
		"\n"
		"  --port N       FamiTracker's port (default: FamiTracker's setting, 46600)\n"
		"  --token CODE   the access code (default: FamiTracker's setting)\n");
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

	std::fprintf(stderr, "famitracker-mcp: passing Claude Code's messages to FamiTracker on 127.0.0.1:%d\n", CurrentTarget().Port);
	std::string Line;
	while (ReadLine(Line)) {
		const Target Where = CurrentTarget();
		BridgeMessage Message;
		if (!BridgeRequest(Line, Where.Port, Where.Token, Message))
			continue;
		HttpResponse Answer;
		std::string Error;
		const BridgeExchangeResult Result = BridgeExchange(Where.Port, FormatHttpRequest(Message.Request), TIMEOUT_MS, Answer, Error);
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

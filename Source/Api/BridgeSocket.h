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

// Moves famitracker-mcp's requests to FamiTracker on 127.0.0.1, or on
// another computer, and the answers back (see Bridge.h): Winsock on Windows,
// BSD sockets elsewhere. One connection per request, closed after the answer.

#include <string>
#include "Http.h"

namespace ftapi {

enum BridgeExchangeResult {
	EXCHANGE_ANSWERED,		// Answer holds FamiTracker's response
	EXCHANGE_UNREACHABLE,	// nothing listens on the port: FamiTracker isn't running, or its connection is off
	EXCHANGE_FAILED,		// no proper answer came; Error says why
	EXCHANGE_NO_ANSWER		// the computer didn't answer at all: it's off, or a firewall keeps the port shut
};

const int BRIDGE_CONNECT_MS = 10000;	// how long a connection may take to open

// Sends Bytes (a whole request) to Host:Port (an IPv4 address or a computer's
// name; 127.0.0.1 when empty) and reads the response, waiting at most
// TimeoutMs whenever the connection goes quiet
BridgeExchangeResult BridgeExchange(const std::string &Host, int Port, const std::string &Bytes, int TimeoutMs, HttpResponse &Answer,
	std::string &Error);
// The same on 127.0.0.1
BridgeExchangeResult BridgeExchange(int Port, const std::string &Bytes, int TimeoutMs, HttpResponse &Answer, std::string &Error);

}	// namespace ftapi

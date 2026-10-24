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

// The Claude Code connection's listener (see McpConnection.h): a thread that
// takes HTTP connections on 127.0.0.1 (and, when the user allows other
// computers, on every IPv4 address of this computer, from home networks'
// addresses only: Api/Network.h) and hands every complete request to a
// window on the UI thread with SendMessageTimeout, whose answer goes back on
// the connection. Plain Win32 and Winsock, no MFC.

#include <string>
#include <vector>

namespace ftapi {
struct HttpRequest;
struct HttpResponse;
}

// What the window gets in lParam; it fills in the response and returns 1
// (0: it couldn't answer, the client gets "busy"). The listener stops waiting
// after a while, but the window may still get to it later, so the exchange
// lives until both are done with it:
//
//	if (BeginMcpExchange(pExchange))	// false: nobody waits for the answer
//		Result = Answer(pExchange);
//	EndMcpExchange(pExchange);			// always, exactly once
struct mcp_exchange_t {
	const ftapi::HttpRequest *pRequest;
	ftapi::HttpResponse *pResponse;
};

bool BeginMcpExchange(mcp_exchange_t *pExchange);
void EndMcpExchange(mcp_exchange_t *pExchange);

// One of this computer's IPv4 addresses, which other computers reach it at
struct mcp_address_t {
	std::string Address;	// "192.168.1.10"
	std::string Adapter;	// its network adapter's name as Windows shows it ("Ethernet", "WLAN", "Tailscale"), ANSI
	bool Gateway;			// on a network with a router
};

// This computer's IPv4 addresses on network adapters that are up, loopback
// left out: home networks with a router first, then Tailscale's (100.64/10),
// then the others (virtual machines' networks, link-local, public)
std::vector<mcp_address_t> ListNetworkAddresses();
// This computer's name on the network ("DESKTOP-PC"); empty when unknown
std::string NetworkComputerName();

class CMcpSocketServer
{
public:
	CMcpSocketServer();
	~CMcpSocketServer();

	// Listens on 127.0.0.1:Port, or with Network on Port of every IPv4 address
	// (connections from addresses outside home networks are closed at once);
	// requests go to the window as Message, each with its client's address
	// (HttpRequest::Peer). False with the reason (in English, for the user)
	// when the port can't be used.
	bool Start(int Port, bool Network, void *hWindow, unsigned int Message, char *pError, int ErrorSize);
	// Closes the listener and every connection, and waits for the thread. Sent
	// messages are still answered meanwhile, so call it on the window's thread.
	// A thread still stuck after a few seconds (a client that doesn't read its
	// answer) is left to end by itself, with state of its own.
	void Stop();
	bool IsRunning() const;
	int GetPort() const;
	bool IsNetwork() const;		// listening for other computers too

private:
	struct Impl;
	Impl *m_pImpl;

	CMcpSocketServer(const CMcpSocketServer &);
	CMcpSocketServer &operator=(const CMcpSocketServer &);
};

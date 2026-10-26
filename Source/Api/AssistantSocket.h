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

// The Ask panel's connection to the model's server (Assistant.h): one HTTP
// exchange per request over plain TCP (Winsock on Windows, BSD sockets
// elsewhere), the answer passed on as it arrives, every wait bounded, and Stop
// heard within a fifth of a second. Runs on a thread of its own in FamiTracker.

#include <atomic>
#include <string>
#include "Assistant.h"

namespace ftapi {

enum assistant_net_t {
	NET_DONE,				// the answer came, as far as the server sent it
	NET_STOPPED,			// the user stopped
	NET_NOT_FOUND,			// no computer of that name
	NET_REFUSED,			// the computer is there, but nothing listens on the port
	NET_NO_ANSWER,			// the computer didn't answer in time: it's off, or a firewall keeps the port shut
	NET_SILENT,				// connected, but the server said nothing for too long
	NET_BROKEN				// the connection broke, or what came couldn't be read
};

struct AssistantTimeouts {
	int ConnectMs;			// for the connection to open
	int FirstMs;			// for the answer to begin: a model loading, a long question read
	int QuietMs;			// between its pieces
	AssistantTimeouts() : ConnectMs(8000), FirstMs(300000), QuietMs(120000) {}
};

// Hears the answer as it comes
class AssistantListener {
public:
	virtual ~AssistantListener() {}
	// A piece of it: false when it has all it needs (the exchange ends there)
	virtual bool Received(const char *pData, size_t Size) = 0;
	// The answer has begun (until then FirstMs bounds the wait, after it QuietMs)
	virtual bool Begun() const { return true; }
};

// Sends Request (an HTTP request's bytes) to Host:Port and passes the answer on until the listener has all of it or the
// server closes the connection
assistant_net_t AssistantExchange(const std::string &Host, int Port, const std::string &Request, const AssistantTimeouts &Times,
	const std::atomic<bool> *pStop, AssistantListener &Listener, std::string &Error);

// A whole answer: GET Path when Body is empty, else POST
struct AssistantAnswer {
	int Status;
	std::string ContentType;
	std::string Body;
	AssistantAnswer() : Status(0) {}
};
assistant_net_t AssistantFetch(const AssistantEndpoint &Where, const std::string &Path, const std::string &Key, const std::string &Body,
	const AssistantTimeouts &Times, const std::atomic<bool> *pStop, AssistantAnswer &Out, std::string &Error);

// A chat completion, streamed: what it adds to the answer and to the thinking, as it comes
class AssistantStreamListener {
public:
	virtual ~AssistantStreamListener() {}
	virtual void Started(int Status) { (void)Status; }	// the server answered; the model is reading the question
	virtual void Streamed(const std::string &NewText, const std::string &NewThinking) = 0;
};
// The reply as far as it came (Out.Ended when it's whole); NET_DONE also for an HTTP error (Out.Status, Out.Error)
assistant_net_t AssistantChatExchange(const AssistantEndpoint &Where, const std::string &Key, const std::string &Body,
	const AssistantTimeouts &Times, const std::atomic<bool> *pStop, AssistantStreamListener *pListener, AssistantReply &Out,
	std::string &Error);

// The IPv4 address ("192.168.1.50") of a computer's name or address; false when there's none
bool AssistantResolve(const std::string &Host, std::string &Address);

// A failed exchange as the user is told it, with what to check
std::string AssistantNetProblem(assistant_net_t Result, const AssistantEndpoint &Where, const AssistantTimeouts &Times,
	const std::string &Error);

// The reply to a request (or what came instead) taken into the chat, as the panel and the tests do it: an answer and its
// calls (RunReady runs them next), the user's Stop, or a problem told in a note with what to check. When the server
// refused the tools, the conversation goes on without them (Setup loses its functions and gets the prompt for that,
// a note says what to switch on) and it's true: the request goes again
bool AssistantTakeReply(AssistantChat &Chat, AssistantSetup &Setup, bool SmallSet, const AssistantEndpoint &Where,
	const std::string &Model, const AssistantTimeouts &Times, assistant_net_t Net, const AssistantReply &Reply,
	const std::string &Error);

}	// namespace ftapi

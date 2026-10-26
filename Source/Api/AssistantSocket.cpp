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

// The Ask panel's connection to the model's server (see AssistantSocket.h)

#include "AssistantSocket.h"
#include "Network.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace ftapi {

namespace {

const int TICK_MS = 200;		// how often a wait looks at Stop

#ifdef _WIN32
typedef SOCKET socket_t;
const socket_t NO_SOCKET = INVALID_SOCKET;
const int SEND_FLAGS = 0;

bool StartSockets()
{
	static const bool Started = [] {
		WSADATA Data;
		return WSAStartup(MAKEWORD(2, 2), &Data) == 0;
	}();
	return Started;
}

void CloseSocket(socket_t Socket) { closesocket(Socket); }
int LastError() { return WSAGetLastError(); }
bool Refused(int Code) { return Code == WSAECONNREFUSED; }
bool Connecting(int Code) { return Code == WSAEWOULDBLOCK || Code == WSAEINPROGRESS; }
bool Unreachable(int Code) { return Code == WSAEHOSTUNREACH || Code == WSAENETUNREACH || Code == WSAEHOSTDOWN || Code == WSAETIMEDOUT; }
bool WouldBlock(int Code) { return Code == WSAEWOULDBLOCK; }

void SetBlocking(socket_t Socket, bool Blocking)
{
	u_long NonBlocking = Blocking ? 0 : 1;
	ioctlsocket(Socket, FIONBIO, &NonBlocking);
}
#else
typedef int socket_t;
const socket_t NO_SOCKET = -1;
#ifdef MSG_NOSIGNAL
const int SEND_FLAGS = MSG_NOSIGNAL;	// a closed connection is an error, not a signal
#else
const int SEND_FLAGS = 0;
#endif

bool StartSockets() { return true; }
void CloseSocket(socket_t Socket) { close(Socket); }
int LastError() { return errno; }
bool Refused(int Code) { return Code == ECONNREFUSED; }
bool Connecting(int Code) { return Code == EINPROGRESS; }
bool Unreachable(int Code) { return Code == EHOSTUNREACH || Code == ENETUNREACH || Code == EHOSTDOWN || Code == ETIMEDOUT; }
bool WouldBlock(int Code) { return Code == EAGAIN || Code == EWOULDBLOCK; }

void SetBlocking(socket_t Socket, bool Blocking)
{
	const int Flags = fcntl(Socket, F_GETFL, 0);
	if (Flags >= 0)
		fcntl(Socket, F_SETFL, Blocking ? (Flags & ~O_NONBLOCK) : (Flags | O_NONBLOCK));
}
#endif

long long NowMs()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool Stopped(const std::atomic<bool> *pStop)
{
	return pStop != 0 && pStop->load();
}

// Waits until the socket can be read (Read) or written, at most Ms: 1 ready, 0 not yet, -1 an error
int WaitFor(socket_t Socket, bool Read, int Ms)
{
	fd_set Set, Failed;
	FD_ZERO(&Set);
	FD_ZERO(&Failed);
	FD_SET(Socket, &Set);
	FD_SET(Socket, &Failed);		// where Winsock reports a connection that failed
	timeval Wait;
	Wait.tv_sec = Ms / 1000;
	Wait.tv_usec = (Ms % 1000) * 1000;
	const int Ready = select(static_cast<int>(Socket) + 1, Read ? &Set : 0, Read ? 0 : &Set, &Failed, &Wait);
	return Ready < 0 ? -1 : Ready == 0 ? 0 : 1;
}

std::string Port(int Value)
{
	char Text[16];
	std::snprintf(Text, sizeof(Text), "%d", Value);
	return Text;
}

std::string Seconds(int Ms)
{
	char Text[48];
	if (Ms >= 120000)
		std::snprintf(Text, sizeof(Text), "%d minutes", Ms / 60000);
	else
		std::snprintf(Text, sizeof(Text), "%d seconds", Ms / 1000);
	return Text;
}

// The whole answer of a fetch
class Collector : public AssistantListener {
public:
	bool Received(const char *pData, size_t Size)
	{
		return Reader.Feed(pData, Size, Body) != HttpStreamReader::DONE && Reader.State() != HttpStreamReader::BAD;
	}
	HttpStreamReader Reader;
	std::string Body;
};

// A streamed reply, read as it comes
class ChatListener : public AssistantListener {
public:
	ChatListener(AssistantStreamListener *pListener) : m_pListener(pListener), m_bStarted(false), m_bBegun(false) {}

	bool Received(const char *pData, size_t Size)
	{
		std::string Body;
		const HttpStreamReader::state_t State = Reader.Feed(pData, Size, Body);
		if (State != HttpStreamReader::HEAD && !m_bStarted) {
			m_bStarted = true;
			Stream.Start(Reader.Status(), Reader.ContentType());
			if (m_pListener != 0)
				m_pListener->Started(Reader.Status());
		}
		if (!Body.empty()) {
			std::string Text, Thinking;
			Stream.Feed(Body, Text, Thinking);
			m_bBegun = true;
			if (m_pListener != 0 && (!Text.empty() || !Thinking.empty()))
				m_pListener->Streamed(Text, Thinking);
		}
		// [DONE] ends it, whatever the connection does after
		return State != HttpStreamReader::DONE && State != HttpStreamReader::BAD && !Stream.Reply().Ended;
	}
	bool Begun() const { return m_bBegun; }

	HttpStreamReader Reader;
	AssistantStream Stream;

private:
	AssistantStreamListener *m_pListener;
	bool m_bStarted, m_bBegun;
};

}	// namespace

bool AssistantResolve(const std::string &Host, std::string &Address)
{
	if (!StartSockets() || Host.empty())
		return false;
	addrinfo Hints;
	std::memset(&Hints, 0, sizeof(Hints));
	Hints.ai_family = AF_INET;
	Hints.ai_socktype = SOCK_STREAM;
	addrinfo *pFound = 0;
	if (getaddrinfo(Host.c_str(), 0, &Hints, &pFound) != 0 || pFound == 0)
		return false;
	// As text without inet_ntop, which Windows XP's headers lack
	Address = Ipv4Text(static_cast<unsigned long>(ntohl(reinterpret_cast<const sockaddr_in*>(pFound->ai_addr)->sin_addr.s_addr)));
	freeaddrinfo(pFound);
	return true;
}

assistant_net_t AssistantExchange(const std::string &Host, int PortNumber, const std::string &Request, const AssistantTimeouts &Times,
	const std::atomic<bool> *pStop, AssistantListener &Listener, std::string &Error)
{
	char Text[200];
	if (!StartSockets()) {
		Error = "Windows networking couldn't be started.";
		return NET_BROKEN;
	}
	std::string Address;
	if (!AssistantResolve(Host, Address))
		return NET_NOT_FOUND;
	if (Stopped(pStop))
		return NET_STOPPED;
	sockaddr_in To;
	std::memset(&To, 0, sizeof(To));
	To.sin_family = AF_INET;
	To.sin_port = htons(static_cast<unsigned short>(PortNumber));
	unsigned long Value = 0;
	if (!ParseIpv4(Address, Value))
		return NET_NOT_FOUND;
	To.sin_addr.s_addr = htonl(static_cast<u_long>(Value));
	const socket_t Socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (Socket == NO_SOCKET) {
		std::snprintf(Text, sizeof(Text), "A network socket couldn't be opened (error %d).", LastError());
		Error = Text;
		return NET_BROKEN;
	}
#ifdef SO_NOSIGPIPE
	const int NoSignal = 1;
	setsockopt(Socket, SOL_SOCKET, SO_NOSIGPIPE, &NoSignal, sizeof(NoSignal));
#endif
	// Connecting, a fifth of a second at a time
	SetBlocking(Socket, false);
	if (connect(Socket, reinterpret_cast<const sockaddr*>(&To), sizeof(To)) != 0) {
		int Code = LastError();
		if (!Connecting(Code)) {
			CloseSocket(Socket);
			std::snprintf(Text, sizeof(Text), "error %d", Code);
			Error = Text;
			return Refused(Code) ? NET_REFUSED : Unreachable(Code) ? NET_NO_ANSWER : NET_BROKEN;
		}
		const long long Until = NowMs() + Times.ConnectMs;
		for (;;) {
			if (Stopped(pStop)) {
				CloseSocket(Socket);
				return NET_STOPPED;
			}
			const int Ready = WaitFor(Socket, false, TICK_MS);
			if (Ready != 0) {
				int Result = 0;
				socklen_t Size = sizeof(Result);
				Code = Ready < 0 ? LastError() :
					getsockopt(Socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&Result), &Size) != 0 ? LastError() : Result;
				if (Code == 0)
					break;
				CloseSocket(Socket);
				std::snprintf(Text, sizeof(Text), "error %d", Code);
				Error = Text;
				return Refused(Code) ? NET_REFUSED : Unreachable(Code) ? NET_NO_ANSWER : NET_BROKEN;
			}
			if (NowMs() >= Until) {
				CloseSocket(Socket);
				return NET_NO_ANSWER;
			}
		}
	}
	// Sending
	for (size_t Sent = 0; Sent < Request.size(); ) {
		if (Stopped(pStop)) {
			CloseSocket(Socket);
			return NET_STOPPED;
		}
		const int Chunk = static_cast<int>(std::min<size_t>(Request.size() - Sent, 1u << 16));
		const long Result = static_cast<long>(send(Socket, Request.data() + Sent, Chunk, SEND_FLAGS));
		if (Result > 0) {
			Sent += static_cast<size_t>(Result);
			continue;
		}
		const int Code = LastError();
		if (Result < 0 && WouldBlock(Code)) {
			WaitFor(Socket, false, TICK_MS);
			continue;
		}
		CloseSocket(Socket);
		std::snprintf(Text, sizeof(Text), "The connection broke while the question was sent (error %d).", Code);
		Error = Text;
		return NET_BROKEN;
	}
	// Receiving, as it comes
	long long Last = NowMs();
	char Buffer[16384];
	for (;;) {
		if (Stopped(pStop)) {
			CloseSocket(Socket);
			return NET_STOPPED;
		}
		const int Ready = WaitFor(Socket, true, TICK_MS);
		if (Ready == 0) {
			const int Limit = Listener.Begun() ? Times.QuietMs : Times.FirstMs;
			if (NowMs() - Last >= Limit) {
				CloseSocket(Socket);
				if (Listener.Begun())
					Error = "The answer stopped coming: nothing more for " + Seconds(Times.QuietMs) + ". Try again; the server's "
						"log may say why.";
				return NET_SILENT;
			}
			continue;
		}
		const long Count = Ready < 0 ? -1 : static_cast<long>(recv(Socket, Buffer, static_cast<int>(sizeof(Buffer)), 0));
		if (Count < 0) {
			const int Code = LastError();
			if (Ready > 0 && WouldBlock(Code))
				continue;
			CloseSocket(Socket);
			std::snprintf(Text, sizeof(Text), "The connection broke while the answer came (error %d).", Code);
			Error = Text;
			return NET_BROKEN;
		}
		if (Count == 0) {
			CloseSocket(Socket);
			return NET_DONE;	// closed: the listener's reader knows whether that was the whole answer
		}
		Last = NowMs();
		if (!Listener.Received(Buffer, static_cast<size_t>(Count))) {
			CloseSocket(Socket);
			return NET_DONE;
		}
	}
}

assistant_net_t AssistantFetch(const AssistantEndpoint &Where, const std::string &Path, const std::string &Key, const std::string &Body,
	const AssistantTimeouts &Times, const std::atomic<bool> *pStop, AssistantAnswer &Out, std::string &Error)
{
	Collector Answer;
	const assistant_net_t Result = AssistantExchange(Where.Host, Where.Port, AssistantHttpRequest(Where, Path, Key, Body, false), Times,
		pStop, Answer, Error);
	if (Result != NET_DONE)
		return Result;
	if (Answer.Reader.State() != HttpStreamReader::DONE)
		Answer.Reader.Close(Answer.Body);
	if (Answer.Reader.State() != HttpStreamReader::DONE) {
		Error = "The server's answer couldn't be read: is it a chat server (llama-server, llama-swap, Ollama, LM Studio)?";
		return NET_BROKEN;
	}
	Out.Status = Answer.Reader.Status();
	Out.ContentType = Answer.Reader.ContentType();
	Out.Body = Answer.Body;
	return NET_DONE;
}

assistant_net_t AssistantChatExchange(const AssistantEndpoint &Where, const std::string &Key, const std::string &Body,
	const AssistantTimeouts &Times, const std::atomic<bool> *pStop, AssistantStreamListener *pListener, AssistantReply &Out,
	std::string &Error)
{
	ChatListener Chat(pListener);
	const assistant_net_t Result = AssistantExchange(Where.Host, Where.Port, AssistantHttpRequest(Where, Where.ChatPath(), Key, Body, true),
		Times, pStop, Chat, Error);
	if (Chat.Reader.State() == HttpStreamReader::BODY) {
		std::string Rest;
		Chat.Reader.Close(Rest);
		if (!Rest.empty()) {
			std::string Text, Thinking;
			Chat.Stream.Feed(Rest, Text, Thinking);
			if (pListener != 0 && (!Text.empty() || !Thinking.empty()))
				pListener->Streamed(Text, Thinking);
		}
	}
	Chat.Stream.End();
	Out = Chat.Stream.Reply();
	if (Result != NET_DONE)
		return Result;
	if (Chat.Reader.State() == HttpStreamReader::HEAD || Chat.Reader.State() == HttpStreamReader::BAD) {
		if (Out.Content.empty() && Out.Calls.empty()) {
			Error = "The server's answer couldn't be read: is it a chat server (llama-server, llama-swap, Ollama, LM Studio)?";
			return NET_BROKEN;
		}
	}
	if (!Out.Ended && Out.Error.empty() && Out.Status < 400) {
		Error = "The answer stopped in the middle: the server closed the connection.";
		return NET_BROKEN;
	}
	return NET_DONE;
}

std::string AssistantNetProblem(assistant_net_t Result, const AssistantEndpoint &Where, const AssistantTimeouts &Times,
	const std::string &Error)
{
	const std::string At = Where.Host + ":" + Port(Where.Port);
	switch (Result) {
	case NET_DONE:
	case NET_STOPPED:
		return std::string();
	case NET_NOT_FOUND:
		return "No computer called \"" + Where.Host + "\" was found. Check the name in the Ask panel's settings, or use the "
			"computer's address (like 192.168.1.50).";
	case NET_REFUSED:
		return "Nothing answers at " + At + ". Is the model's server running (llama-server, llama-swap, Ollama or LM Studio), "
			"and is " + Port(Where.Port) + " its port? A server on another computer must listen on the network too "
			"(llama-server --host 0.0.0.0).";
	case NET_NO_ANSWER:
		return Where.Host + " didn't answer within " + Seconds(Times.ConnectMs) + ". Is that computer on and the address "
			"right? A firewall there may keep port " + Port(Where.Port) + " shut.";
	case NET_SILENT:
		if (!Error.empty())
			return Error;
		return "The model said nothing for " + Seconds(Times.FirstMs) + ". It may still be loading, or the question is too "
			"long for it: try again, or look at the server's log.";
	case NET_BROKEN:
	default:
		return "The connection to " + At + " broke" + (Error.empty() ? std::string(".") : ": " + Error) + " Try again; if it "
			"keeps happening, look at the server's log.";
	}
}

bool AssistantTakeReply(AssistantChat &Chat, AssistantSetup &Setup, bool SmallSet, const AssistantEndpoint &Where,
	const std::string &Model, const AssistantTimeouts &Times, assistant_net_t Net, const AssistantReply &Reply,
	const std::string &Error)
{
	if (Net == NET_STOPPED) {
		Chat.Replied(Reply, true);
		return false;
	}
	if (Net != NET_DONE) {
		Chat.Replied(Reply, true, AssistantNetProblem(Net, Where, Times, Error));
		return false;
	}
	if (Reply.Status >= 400 || !Reply.Error.empty()) {
		const std::string Said = Reply.Error.empty() ? std::string() : Reply.Error;
		if (AssistantToolsRefused(Reply.Status, Said) && !Chat.HasToolsOff() && Setup.Functions.Size() > 0) {
			Chat.ToolsOff();
			Setup.Functions = Json::MakeArray();
			Setup.SystemPrompt = AssistantSystemPrompt(SmallSet, false);
			Chat.Note("The server didn't take FamiTracker's tools (it said: " + Said + "). For llama.cpp, start llama-server "
				"with --jinja (llama-swap: in the model's cmd); with Ollama or LM Studio, pick a model that can use tools. Until "
				"then your model answers from the song's summary alone, without reading or changing the song; New question tries the "
				"tools again.");
			return true;
		}
		Chat.Replied(Reply, true, AssistantHttpProblem(Where, Model, Reply.Status, Said));
		return false;
	}
	Chat.Replied(Reply, false);
	return false;
}

}	// namespace ftapi

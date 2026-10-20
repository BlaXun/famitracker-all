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

#include "BridgeSocket.h"
#include "Bridge.h"

#include <algorithm>
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
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace ftapi {

namespace {

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
bool TimedOut(int Code) { return Code == WSAETIMEDOUT || Code == WSAEWOULDBLOCK; }

void SetTimeout(socket_t Socket, int Option, int Ms)
{
	const DWORD Value = static_cast<DWORD>(Ms);
	setsockopt(Socket, SOL_SOCKET, Option, reinterpret_cast<const char*>(&Value), sizeof(Value));
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
bool TimedOut(int Code) { return Code == EAGAIN || Code == EWOULDBLOCK || Code == ETIMEDOUT; }

void SetTimeout(socket_t Socket, int Option, int Ms)
{
	timeval Value;
	Value.tv_sec = Ms / 1000;
	Value.tv_usec = (Ms % 1000) * 1000;
	setsockopt(Socket, SOL_SOCKET, Option, &Value, sizeof(Value));
}
#endif

}	// namespace

BridgeExchangeResult BridgeExchange(int Port, const std::string &Bytes, int TimeoutMs, HttpResponse &Answer, std::string &Error)
{
	char Text[160];
	if (!StartSockets()) {
		Error = "Windows networking couldn't be started.";
		return EXCHANGE_FAILED;
	}
	const socket_t Socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (Socket == NO_SOCKET) {
		std::snprintf(Text, sizeof(Text), "A network socket couldn't be opened (error %d).", LastError());
		Error = Text;
		return EXCHANGE_FAILED;
	}
	SetTimeout(Socket, SO_RCVTIMEO, TimeoutMs);
	SetTimeout(Socket, SO_SNDTIMEO, TimeoutMs);
#ifdef SO_NOSIGPIPE
	const int NoSignal = 1;
	setsockopt(Socket, SOL_SOCKET, SO_NOSIGPIPE, &NoSignal, sizeof(NoSignal));
#endif

	sockaddr_in Address;
	std::memset(&Address, 0, sizeof(Address));
	Address.sin_family = AF_INET;
	Address.sin_port = htons(static_cast<unsigned short>(Port));
	Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (connect(Socket, reinterpret_cast<const sockaddr*>(&Address), sizeof(Address)) != 0) {
		const int Code = LastError();
		CloseSocket(Socket);
		if (Refused(Code)) {
			std::snprintf(Text, sizeof(Text), "Nothing listens on 127.0.0.1:%d.", Port);
			Error = Text;
			return EXCHANGE_UNREACHABLE;
		}
		std::snprintf(Text, sizeof(Text), "127.0.0.1:%d couldn't be reached (error %d).", Port, Code);
		Error = Text;
		return EXCHANGE_FAILED;
	}

	for (size_t Sent = 0; Sent < Bytes.size(); ) {
		const int Chunk = static_cast<int>(std::min<size_t>(Bytes.size() - Sent, 1u << 20));
		const long Result = static_cast<long>(send(Socket, Bytes.data() + Sent, Chunk, SEND_FLAGS));
		if (Result <= 0) {
			const int Code = LastError();
			CloseSocket(Socket);
			std::snprintf(Text, sizeof(Text), "The connection to FamiTracker broke while sending (error %d).", Code);
			Error = TimedOut(Code) ? std::string("FamiTracker didn't take the request in time.") : std::string(Text);
			return EXCHANGE_FAILED;
		}
		Sent += static_cast<size_t>(Result);
	}

	std::string Received;
	char Buffer[16384];
	for (;;) {
		const long Count = static_cast<long>(recv(Socket, Buffer, static_cast<int>(sizeof(Buffer)), 0));
		if (Count < 0) {
			const int Code = LastError();
			CloseSocket(Socket);
			std::snprintf(Text, sizeof(Text), "The connection to FamiTracker broke while waiting for the answer (error %d).", Code);
			Error = TimedOut(Code) ? std::string("FamiTracker didn't answer in time.") : std::string(Text);
			return EXCHANGE_FAILED;
		}
		const bool Closed = Count == 0;
		if (!Closed)
			Received.append(Buffer, static_cast<size_t>(Count));
		const HttpResponseRead Read = ReadHttpResponse(Received, Closed, Answer);
		if (Read == RESPONSE_COMPLETE) {
			CloseSocket(Socket);
			return EXCHANGE_ANSWERED;
		}
		if (Read == RESPONSE_BAD || Closed) {
			CloseSocket(Socket);
			Error = Received.empty() ? "FamiTracker closed the connection without answering." : "FamiTracker's answer couldn't be read.";
			return EXCHANGE_FAILED;
		}
	}
}

}	// namespace ftapi

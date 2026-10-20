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

// The Claude Code connection's listener (see McpSocket.h). No MFC here:
// winsock2.h has to come before windows.h, which MFC's headers include.

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>
#include "Api/Http.h"
#include "Api/Json.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <process.h>

#include "McpSocket.h"

#pragma comment(lib, "ws2_32.lib")

namespace {

const int MAX_CONNECTIONS = 16;
const ULONGLONG IDLE_MS = 120000;		// a connection nobody uses for this long is closed
const UINT ANSWER_MS = 30000;			// how long the UI thread may take to answer
const int SEND_TIMEOUT_MS = 10000;

struct Connection {
	SOCKET Socket;
	ftapi::HttpRequestParser Parser;
	ULONGLONG LastUsed;
	Connection(SOCKET s) : Socket(s), LastUsed(GetTickCount64()) {}
};

bool SendAll(SOCKET Socket, const std::string &Bytes)
{
	size_t Sent = 0;
	while (Sent < Bytes.size()) {
		const int Chunk = static_cast<int>(std::min<size_t>(Bytes.size() - Sent, 1 << 20));
		const int Result = send(Socket, Bytes.data() + Sent, Chunk, 0);
		if (Result == SOCKET_ERROR || Result == 0)
			return false;
		Sent += static_cast<size_t>(Result);
	}
	return true;
}

ftapi::HttpResponse Busy()
{
	ftapi::Json Error = ftapi::Json::MakeObject();
	Error.Set("code", -32000);
	Error.Set("message", "FamiTracker is busy or closing; try again in a moment.");
	ftapi::Json Message = ftapi::Json::MakeObject();
	Message.Set("jsonrpc", "2.0");
	Message.Set("id", ftapi::Json());
	Message.Set("error", Error);
	ftapi::HttpResponse Response;
	Response.Status = 503;
	Response.ContentType = "application/json";
	Response.Body = ftapi::WriteJson(Message);
	return Response;
}

ftapi::HttpResponse Malformed(int Status)
{
	ftapi::HttpResponse Response;
	Response.Status = Status;
	Response.ContentType = "text/plain; charset=utf-8";
	Response.Body = std::string(ftapi::HttpReason(Status)) + "\n";
	return Response;
}

enum { EXCHANGE_WAITING, EXCHANGE_ANSWERING, EXCHANGE_ABANDONED };

// A request on its way to the window, shared by the listener and the window
struct Exchange : mcp_exchange_t {
	ftapi::HttpRequest Request;
	ftapi::HttpResponse Response;
	volatile LONG References;	// the listener's and the window's
	volatile LONG State;

	explicit Exchange(const ftapi::HttpRequest &From) : Request(From), References(2), State(EXCHANGE_WAITING)
	{
		pRequest = &Request;
		pResponse = &Response;
	}
};

void Release(Exchange *pExchange)
{
	if (InterlockedDecrement(&pExchange->References) == 0)
		delete pExchange;
}

// Who frees a server's state: the server object, or the thread when Stop()
// stopped waiting for it
enum { OWNER_SERVER, OWNER_THREAD, OWNER_FINISHED };

}	// namespace

bool BeginMcpExchange(mcp_exchange_t *pExchange)
{
	Exchange *p = static_cast<Exchange*>(pExchange);
	return InterlockedCompareExchange(&p->State, EXCHANGE_ANSWERING, EXCHANGE_WAITING) == EXCHANGE_WAITING;
}

void EndMcpExchange(mcp_exchange_t *pExchange)
{
	Release(static_cast<Exchange*>(pExchange));
}

struct CMcpSocketServer::Impl {
	SOCKET Listener;			// the thread's once it runs
	HANDLE hThread;
	HWND hWindow;
	UINT Message;
	int Port;
	volatile LONG Stopping;
	volatile LONG Owner;

	Impl() : Listener(INVALID_SOCKET), hThread(NULL), hWindow(NULL), Message(0), Port(0), Stopping(0), Owner(OWNER_SERVER) {}

	// The UI thread answers, or the client hears it's busy
	ftapi::HttpResponse Answer(const ftapi::HttpRequest &Request)
	{
		if (Stopping)
			return Busy();
		Exchange *pExchange = new Exchange(Request);
		DWORD_PTR Result = 0;
		const LRESULT Sent = SendMessageTimeout(hWindow, Message, 0, reinterpret_cast<LPARAM>(static_cast<mcp_exchange_t*>(pExchange)),
			SMTO_NORMAL | SMTO_ABORTIFHUNG, ANSWER_MS, &Result);
		const DWORD Error = Sent == 0 ? GetLastError() : 0;
		ftapi::HttpResponse Response = Busy();
		if (Sent != 0 && Result != 0)
			Response = pExchange->Response;	// answered: the window is done with it
		else if (InterlockedCompareExchange(&pExchange->State, EXCHANGE_ABANDONED, EXCHANGE_WAITING) == EXCHANGE_WAITING &&
			Error == ERROR_INVALID_WINDOW_HANDLE)
			Release(pExchange);	// it never reached the window and never will: the window's share
		// Otherwise the window may still be answering, or get it later and see that nobody waits
		Release(pExchange);
		return Response;
	}

	// Reads what arrived; false when the connection should close
	bool Serve(Connection &C)
	{
		char Buffer[16384];
		const int Received = recv(C.Socket, Buffer, sizeof(Buffer), 0);
		if (Received <= 0)
			return false;
		C.LastUsed = GetTickCount64();
		ftapi::HttpRequestParser::Result Result = C.Parser.Feed(Buffer, static_cast<size_t>(Received));
		if (C.Parser.TakeContinue() && !SendAll(C.Socket, "HTTP/1.1 100 Continue\r\n\r\n"))
			return false;
		while (Result == ftapi::HttpRequestParser::COMPLETE) {
			const ftapi::HttpRequest Request = C.Parser.Take();
			const ftapi::HttpResponse Response = Answer(Request);
			const bool KeepAlive = Request.KeepAlive() && !Stopping;
			if (!SendAll(C.Socket, ftapi::FormatHttpResponse(Response, KeepAlive)) || !KeepAlive)
				return false;
			Result = C.Parser.Feed();
		}
		if (Result == ftapi::HttpRequestParser::BAD) {
			SendAll(C.Socket, ftapi::FormatHttpResponse(Malformed(C.Parser.ErrorStatus()), false));
			return false;
		}
		return true;
	}

	void Run()
	{
		std::vector<Connection*> Connections;
		while (!Stopping) {
			fd_set Readable;
			FD_ZERO(&Readable);
			FD_SET(Listener, &Readable);
			for (size_t i = 0; i < Connections.size(); ++i)
				FD_SET(Connections[i]->Socket, &Readable);
			timeval Wait = { 0, 200000 };
			const int Ready = select(0, &Readable, NULL, NULL, &Wait);
			if (Stopping)
				break;
			if (Ready == SOCKET_ERROR) {
				Sleep(50);
				continue;
			}
			if (FD_ISSET(Listener, &Readable)) {
				const SOCKET Socket = accept(Listener, NULL, NULL);
				if (Socket != INVALID_SOCKET) {
					if (static_cast<int>(Connections.size()) >= MAX_CONNECTIONS)
						closesocket(Socket);
					else {
						const BOOL NoDelay = TRUE;
						setsockopt(Socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&NoDelay), sizeof(NoDelay));
						const DWORD Timeout = SEND_TIMEOUT_MS;
						setsockopt(Socket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&Timeout), sizeof(Timeout));
						Connections.push_back(new Connection(Socket));
					}
				}
			}
			const ULONGLONG Now = GetTickCount64();
			for (size_t i = 0; i < Connections.size(); ) {
				Connection *pConnection = Connections[i];
				bool Keep = true;
				if (FD_ISSET(pConnection->Socket, &Readable))
					Keep = Serve(*pConnection);
				else if (Now - pConnection->LastUsed > IDLE_MS)
					Keep = false;
				if (Keep)
					++i;
				else {
					closesocket(pConnection->Socket);
					delete pConnection;
					Connections.erase(Connections.begin() + i);
				}
			}
		}
		for (size_t i = 0; i < Connections.size(); ++i) {
			closesocket(Connections[i]->Socket);
			delete Connections[i];
		}
		closesocket(Listener);
		Listener = INVALID_SOCKET;
		WSACleanup();
	}

	static unsigned __stdcall ThreadProc(void *pParam)
	{
		Impl *pImpl = static_cast<Impl*>(pParam);
		pImpl->Run();
		// Stop() stopped waiting and left the state to this thread
		if (InterlockedCompareExchange(&pImpl->Owner, OWNER_FINISHED, OWNER_SERVER) == OWNER_THREAD)
			delete pImpl;
		return 0;
	}
};

CMcpSocketServer::CMcpSocketServer() : m_pImpl(new Impl())
{
}

CMcpSocketServer::~CMcpSocketServer()
{
	Stop();
	delete m_pImpl;
}

bool CMcpSocketServer::Start(int Port, void *hWindow, unsigned int Message, char *pError, int ErrorSize)
{
	Stop();
	Impl &d = *m_pImpl;
	if (Port < 1 || Port > 65535) {
		std::snprintf(pError, static_cast<size_t>(ErrorSize), "The port should be from 1 to 65535.");
		return false;
	}
	WSADATA Data;
	if (WSAStartup(MAKEWORD(2, 2), &Data) != 0) {
		std::snprintf(pError, static_cast<size_t>(ErrorSize), "Windows networking couldn't be started.");
		return false;
	}
	const SOCKET Listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (Listener == INVALID_SOCKET) {
		std::snprintf(pError, static_cast<size_t>(ErrorSize), "A network socket couldn't be opened (error %d).", WSAGetLastError());
		WSACleanup();
		return false;
	}
	// Nobody else may take the port while it's ours
	const BOOL Exclusive = TRUE;
	setsockopt(Listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&Exclusive), sizeof(Exclusive));
	sockaddr_in Address;
	ZeroMemory(&Address, sizeof(Address));
	Address.sin_family = AF_INET;
	Address.sin_port = htons(static_cast<u_short>(Port));
	Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);	// this computer only
	if (bind(Listener, reinterpret_cast<const sockaddr*>(&Address), sizeof(Address)) == SOCKET_ERROR ||
		listen(Listener, SOMAXCONN) == SOCKET_ERROR) {
		const int Code = WSAGetLastError();
		if (Code == WSAEADDRINUSE)
			std::snprintf(pError, static_cast<size_t>(ErrorSize), "Port %d is already in use, maybe by another FamiTracker. Pick another port.", Port);
		else if (Code == WSAEACCES)
			std::snprintf(pError, static_cast<size_t>(ErrorSize), "Windows doesn't let programs use port %d. Pick another port.", Port);
		else
			std::snprintf(pError, static_cast<size_t>(ErrorSize), "Port %d couldn't be opened (error %d). Pick another port.", Port, Code);
		closesocket(Listener);
		WSACleanup();
		return false;
	}
	d.Listener = Listener;
	d.hWindow = static_cast<HWND>(hWindow);
	d.Message = Message;
	d.Port = Port;
	d.Stopping = 0;
	d.Owner = OWNER_SERVER;
	unsigned ThreadId = 0;
	d.hThread = reinterpret_cast<HANDLE>(_beginthreadex(NULL, 0, &Impl::ThreadProc, &d, 0, &ThreadId));
	if (d.hThread == NULL) {
		std::snprintf(pError, static_cast<size_t>(ErrorSize), "The connection's thread couldn't be started.");
		closesocket(Listener);
		d.Listener = INVALID_SOCKET;
		d.Port = 0;
		WSACleanup();
		return false;
	}
	return true;	// from here on the thread closes the listener and cleans up Winsock
}

void CMcpSocketServer::Stop()
{
	Impl &d = *m_pImpl;
	if (d.hThread == NULL)
		return;
	InterlockedExchange(&d.Stopping, 1);
	// The thread may be waiting for this thread to answer a request: keep
	// answering (the window says it's closing) until the thread is done
	const ULONGLONG Until = GetTickCount64() + 5000;
	for (;;) {
		const DWORD Result = MsgWaitForMultipleObjects(1, &d.hThread, FALSE, 100, QS_SENDMESSAGE);
		if (Result == WAIT_OBJECT_0 || Result == WAIT_FAILED || GetTickCount64() > Until)
			break;
		if (Result == WAIT_OBJECT_0 + 1) {
			MSG Message;
			PeekMessage(&Message, NULL, 0, 0, PM_NOREMOVE);	// delivers sent messages
		}
	}
	const bool Ended = WaitForSingleObject(d.hThread, 0) == WAIT_OBJECT_0;
	CloseHandle(d.hThread);	// only lets go of the handle; a thread still running goes on
	d.hThread = NULL;
	d.Port = 0;
	// Still stuck (sending to a client that doesn't read): it keeps its state,
	// frees it when it ends, and this server starts afresh
	if (!Ended && InterlockedCompareExchange(&d.Owner, OWNER_THREAD, OWNER_SERVER) == OWNER_SERVER)
		m_pImpl = new Impl();
}

bool CMcpSocketServer::IsRunning() const
{
	return m_pImpl->hThread != NULL;
}

int CMcpSocketServer::GetPort() const
{
	return m_pImpl->Port;
}

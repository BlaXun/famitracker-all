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

// A scripted model for the Ask panel's tests (Assistant.h): an OpenAI-style
// chat server on a free port of 127.0.0.1, on a thread of its own, one request
// per connection. Each chat request gets the next answer of the script, sent
// in the pieces given (chunked or not, with pauses between them, a piece cut
// anywhere); /props and /v1/models get theirs, and a request with tools gets
// the refusal when one is set (a llama-server without --jinja). The requests
// are kept for the tests to read.

#include "../Http.h"
#include "../Json.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace fakemodel {

#ifdef _WIN32
typedef SOCKET fm_socket_t;
const fm_socket_t FM_NO_SOCKET = INVALID_SOCKET;
const int FM_SEND_FLAGS = 0;
inline void FmClose(fm_socket_t Socket) { closesocket(Socket); }
#else
typedef int fm_socket_t;
const fm_socket_t FM_NO_SOCKET = -1;
#ifdef MSG_NOSIGNAL
const int FM_SEND_FLAGS = MSG_NOSIGNAL;		// a client gone (stopped) is an error, not a signal
#else
const int FM_SEND_FLAGS = 0;
#endif
inline void FmClose(fm_socket_t Socket) { close(Socket); }
#endif

struct Answer {
	int Status;
	std::string ContentType;
	std::vector<std::string> Pieces;	// the body, sent a piece at a time
	bool Chunked;
	int PauseMs;						// before each piece after the first
	bool Hang;							// after the pieces, nothing more until the client goes
	Answer() : Status(200), ContentType("text/event-stream"), Chunked(true), PauseMs(0), Hang(false) {}
};

inline std::string Event(const ftapi::Json &Data)
{
	return "data: " + ftapi::WriteJson(Data) + "\n\n";
}

inline ftapi::Json Chunk(const ftapi::Json &Delta, const char *pFinish = 0)
{
	ftapi::Json Choice = ftapi::Json::MakeObject();
	Choice.Set("index", 0);
	Choice.Set("delta", Delta);
	Choice.Set("finish_reason", pFinish != 0 ? ftapi::Json(pFinish) : ftapi::Json());
	ftapi::Json Choices = ftapi::Json::MakeArray();
	Choices.Push(Choice);
	ftapi::Json Out = ftapi::Json::MakeObject();
	Out.Set("object", "chat.completion.chunk");
	Out.Set("choices", Choices);
	return Out;
}

inline ftapi::Json TextDelta(const char *pField, const std::string &Text)
{
	ftapi::Json Delta = ftapi::Json::MakeObject();
	Delta.Set(pField, Text);
	return Delta;
}

// The end of a reply: its finish, the tokens counted, [DONE]
inline void Finish(std::string &Stream, const char *pFinish, int PromptTokens)
{
	ftapi::Json Last = Chunk(ftapi::Json::MakeObject(), pFinish);
	ftapi::Json Usage = ftapi::Json::MakeObject();
	Usage.Set("prompt_tokens", PromptTokens);
	Usage.Set("completion_tokens", 20);
	Last.Set("usage", Usage);
	Stream += Event(Last) + "data: [DONE]\n\n";
}

// The stream cut into pieces of Size bytes (the events cut anywhere)
inline std::vector<std::string> Pieces(const std::string &Stream, size_t Size)
{
	std::vector<std::string> Out;
	for (size_t i = 0; i < Stream.size(); i += Size)
		Out.push_back(Stream.substr(i, Size));
	return Out;
}

// A text answer, streamed in the pieces given
inline Answer Text(const std::vector<std::string> &Parts, size_t PieceSize = 7, int PromptTokens = 0)
{
	std::string Stream = Event(Chunk(TextDelta("role", "assistant")));
	for (size_t i = 0; i < Parts.size(); ++i)
		Stream += Event(Chunk(TextDelta("content", Parts[i])));
	Finish(Stream, "stop", PromptTokens);
	Answer A;
	A.Pieces = Pieces(Stream, PieceSize);
	return A;
}

// A call of a tool, its arguments in the pieces given, after some text
inline Answer Call(const std::string &Name, const std::vector<std::string> &Arguments, const std::string &Id = "call_1",
	const std::string &Said = std::string(), int PromptTokens = 0)
{
	std::string Stream = Event(Chunk(TextDelta("role", "assistant")));
	if (!Said.empty())
		Stream += Event(Chunk(TextDelta("content", Said)));
	for (size_t i = 0; i <= Arguments.size(); ++i) {
		ftapi::Json Function = ftapi::Json::MakeObject();
		if (i == 0)
			Function.Set("name", Name);
		Function.Set("arguments", i == 0 ? std::string() : Arguments[i - 1]);
		ftapi::Json Piece = ftapi::Json::MakeObject();
		Piece.Set("index", 0);
		if (i == 0) {
			Piece.Set("id", Id);
			Piece.Set("type", "function");
		}
		Piece.Set("function", Function);
		ftapi::Json List = ftapi::Json::MakeArray();
		List.Push(Piece);
		ftapi::Json Delta = ftapi::Json::MakeObject();
		Delta.Set("tool_calls", List);
		Stream += Event(Chunk(Delta));
	}
	Finish(Stream, "tool_calls", PromptTokens);
	Answer A;
	A.Pieces = Pieces(Stream, 11);
	return A;
}

// An error with a JSON body, as llama-server gives one
inline Answer Error(int Status, const std::string &Message)
{
	ftapi::Json Inside = ftapi::Json::MakeObject();
	Inside.Set("code", Status);
	Inside.Set("message", Message);
	Inside.Set("type", "invalid_request_error");
	ftapi::Json Body = ftapi::Json::MakeObject();
	Body.Set("error", Inside);
	Answer A;
	A.Status = Status;
	A.ContentType = "application/json; charset=utf-8";
	A.Chunked = false;
	A.Pieces.push_back(ftapi::WriteJson(Body));
	return A;
}

inline Answer Plain(const std::string &Json)
{
	Answer A;
	A.ContentType = "application/json";
	A.Chunked = false;
	A.Pieces.push_back(Json);
	return A;
}

class Server {
public:
	Server() : m_Listener(FM_NO_SOCKET), m_iPort(0), m_bStop(false), m_bRefuse(false) {}
	~Server() { Stop(); }

	bool Start()
	{
#ifdef _WIN32
		WSADATA Data;
		if (WSAStartup(MAKEWORD(2, 2), &Data) != 0)
			return false;
#endif
		m_Listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (m_Listener == FM_NO_SOCKET)
			return false;
		sockaddr_in Address;
		std::memset(&Address, 0, sizeof(Address));
		Address.sin_family = AF_INET;
		Address.sin_port = 0;
		Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		socklen_t Size = sizeof(Address);
		if (bind(m_Listener, reinterpret_cast<const sockaddr*>(&Address), sizeof(Address)) != 0 || listen(m_Listener, 8) != 0 ||
			getsockname(m_Listener, reinterpret_cast<sockaddr*>(&Address), &Size) != 0)
			return false;
		m_iPort = ntohs(Address.sin_port);
		m_Thread = std::thread(&Server::Run, this);
		return true;
	}

	void Stop()
	{
		m_bStop = true;
		if (m_Thread.joinable())
			m_Thread.join();
		if (m_Listener != FM_NO_SOCKET) {
			FmClose(m_Listener);
			m_Listener = FM_NO_SOCKET;
		}
	}

	int Port() const { return m_iPort; }

	void Add(const Answer &A)
	{
		std::lock_guard<std::mutex> Lock(m_Lock);
		m_Script.push_back(A);
	}
	void RefuseTools(const Answer &A)
	{
		std::lock_guard<std::mutex> Lock(m_Lock);
		m_Refusal = A;
		m_bRefuse = true;
	}
	void SetProps(const std::string &Json) { std::lock_guard<std::mutex> Lock(m_Lock); m_Props = Json; }
	void SetModels(const std::string &Json) { std::lock_guard<std::mutex> Lock(m_Lock); m_Models = Json; }

	std::vector<ftapi::HttpRequest> Requests() const
	{
		std::lock_guard<std::mutex> Lock(m_Lock);
		return m_Requests;
	}
	size_t Left() const
	{
		std::lock_guard<std::mutex> Lock(m_Lock);
		return m_Script.size();
	}

private:
	static bool SendAll(fm_socket_t Client, const std::string &Bytes)
	{
		for (size_t Sent = 0; Sent < Bytes.size(); ) {
			const long Count = static_cast<long>(send(Client, Bytes.data() + Sent, static_cast<int>(Bytes.size() - Sent), FM_SEND_FLAGS));
			if (Count <= 0)
				return false;
			Sent += static_cast<size_t>(Count);
		}
		return true;
	}

	Answer Pick(const ftapi::HttpRequest &Request)
	{
		std::lock_guard<std::mutex> Lock(m_Lock);
		const std::string Path = Request.Path();
		if (Path.size() >= 6 && Path.compare(Path.size() - 6, 6, "/props") == 0 && !m_Props.empty())
			return Plain(m_Props);
		if (Path.size() >= 7 && Path.compare(Path.size() - 7, 7, "/models") == 0 && !m_Models.empty())
			return Plain(m_Models);
		if (m_bRefuse && Request.Body.find("\"tools\"") != std::string::npos)
			return m_Refusal;
		if (m_Script.empty())
			return Error(500, "The test's script has no more answers.");
		const Answer A = m_Script.front();
		m_Script.pop_front();
		return A;
	}

	void Run()
	{
		while (!m_bStop) {
			fd_set Readable;
			FD_ZERO(&Readable);
			FD_SET(m_Listener, &Readable);
			timeval Wait = { 0, 20000 };
			if (select(static_cast<int>(m_Listener) + 1, &Readable, 0, 0, &Wait) <= 0)
				continue;
			const fm_socket_t Client = accept(m_Listener, 0, 0);
			if (Client == FM_NO_SOCKET)
				continue;
			ftapi::HttpRequestParser Parser;
			ftapi::HttpRequestParser::Result Result = ftapi::HttpRequestParser::NEED_MORE;
			char Buffer[8192];
			while (Result == ftapi::HttpRequestParser::NEED_MORE) {
				const long Count = static_cast<long>(recv(Client, Buffer, static_cast<int>(sizeof(Buffer)), 0));
				if (Count <= 0)
					break;
				Result = Parser.Feed(Buffer, static_cast<size_t>(Count));
			}
			if (Result != ftapi::HttpRequestParser::COMPLETE) {
				FmClose(Client);
				continue;
			}
			const ftapi::HttpRequest Request = Parser.Take();
			{
				std::lock_guard<std::mutex> Lock(m_Lock);
				m_Requests.push_back(Request);
			}
			const Answer A = Pick(Request);
			std::string Body;
			for (size_t i = 0; i < A.Pieces.size(); ++i)
				Body += A.Pieces[i];
			char Head[256];
			std::snprintf(Head, sizeof(Head), "HTTP/1.1 %d %s\r\nContent-Type: %s\r\n", A.Status, ftapi::HttpReason(A.Status),
				A.ContentType.c_str());
			std::string Out = Head;
			if (A.Chunked)
				Out += "Transfer-Encoding: chunked\r\n";
			else {
				std::snprintf(Head, sizeof(Head), "Content-Length: %lu\r\n", static_cast<unsigned long>(Body.size()));
				Out += Head;
			}
			Out += "Connection: close\r\n\r\n";
			bool Going = SendAll(Client, Out);
			for (size_t i = 0; i < A.Pieces.size() && Going && !m_bStop; ++i) {
				if (i > 0 && A.PauseMs > 0)
					std::this_thread::sleep_for(std::chrono::milliseconds(A.PauseMs));
				std::string Piece = A.Pieces[i];
				if (A.Chunked) {
					char Size[16];
					std::snprintf(Size, sizeof(Size), "%lx\r\n", static_cast<unsigned long>(Piece.size()));
					Piece = Size + Piece + "\r\n";
				}
				Going = SendAll(Client, Piece);
			}
			if (Going && A.Hang) {
				// Silent until the client goes (or the test ends)
				while (!m_bStop) {
					fd_set Set;
					FD_ZERO(&Set);
					FD_SET(Client, &Set);
					timeval Tick = { 0, 20000 };
					if (select(static_cast<int>(Client) + 1, &Set, 0, 0, &Tick) > 0 &&
						recv(Client, Buffer, static_cast<int>(sizeof(Buffer)), 0) <= 0)
						break;
				}
			}
			else if (Going && A.Chunked)
				SendAll(Client, "0\r\n\r\n");
			FmClose(Client);
		}
	}

	fm_socket_t m_Listener;
	int m_iPort;
	std::atomic<bool> m_bStop;
	std::thread m_Thread;
	mutable std::mutex m_Lock;
	std::deque<Answer> m_Script;
	Answer m_Refusal;
	bool m_bRefuse;
	std::string m_Props, m_Models;
	std::vector<ftapi::HttpRequest> m_Requests;
};

}	// namespace fakemodel

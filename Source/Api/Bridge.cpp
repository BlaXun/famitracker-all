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

#include "Bridge.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ftapi {

namespace {

const char *const VERSION_KEY = "io.modelcontextprotocol/protocolVersion";
const int BRIDGE_ERROR = -32000;		// JSON-RPC's range for the server's own errors
const size_t MAX_HEAD = 64u << 10;
const size_t MAX_BODY = 64u << 20;

void AddHeader(HttpRequest &Request, const char *pName, const std::string &Value)
{
	Request.Headers.push_back(std::make_pair(std::string(pName), Value));
}

// A name as an Mcp-Name header value: as it is when that's plain text, in
// base64 otherwise, the way the protocol allows ("=?base64?...?=")
std::string HeaderValue(const std::string &Text)
{
	bool Plain = !Text.empty() && Text[0] != ' ' && Text[Text.size() - 1] != ' ' && Text.compare(0, 9, "=?base64?") != 0;
	for (size_t i = 0; Plain && i < Text.size(); ++i) {
		const unsigned char c = static_cast<unsigned char>(Text[i]);
		Plain = c >= 0x20 && c < 0x7F;
	}
	return Plain ? Text : "=?base64?" + Base64Encode(Text) + "?=";
}

// Nothing in a head line may end it early
std::string HeadText(const std::string &Text)
{
	std::string Out;
	for (size_t i = 0; i < Text.size(); ++i)
		if (Text[i] != '\r' && Text[i] != '\n' && Text[i] != '\0')
			Out += Text[i];
	return Out;
}

std::string Trim(const std::string &Text)
{
	size_t First = 0, Last = Text.size();
	while (First < Last && (Text[First] == ' ' || Text[First] == '\t'))
		++First;
	while (Last > First && (Text[Last - 1] == ' ' || Text[Last - 1] == '\t'))
		--Last;
	return Text.substr(First, Last - First);
}

bool ParseSize(const std::string &Text, size_t &Out)
{
	if (Text.empty() || Text.size() > 12)
		return false;
	Out = 0;
	for (size_t i = 0; i < Text.size(); ++i) {
		if (Text[i] < '0' || Text[i] > '9')
			return false;
		Out = Out * 10 + static_cast<size_t>(Text[i] - '0');
	}
	return true;
}

std::string Line(const Json &Message)
{
	return WriteJson(Message) + "\n";
}

std::string ErrorLine(const Json &Id, const std::string &Text)
{
	Json Error = Json::MakeObject();
	Error.Set("code", BRIDGE_ERROR);
	Error.Set("message", Text);
	Json Message = Json::MakeObject();
	Message.Set("jsonrpc", "2.0");
	Message.Set("id", Id);
	Message.Set("error", Error);
	return Line(Message);
}

}	// namespace

bool BridgeRequest(const std::string &Line, int Port, const std::string &Token, BridgeMessage &Out, const std::string &Host,
	const std::string &Path, const std::string &Version)
{
	std::string Text = Line;
	while (!Text.empty() && (Text[Text.size() - 1] == '\r' || Text[Text.size() - 1] == '\n'))
		Text.erase(Text.size() - 1);
	if (Text.find_first_not_of(" \t") == std::string::npos)
		return false;

	Out = BridgeMessage();
	HttpRequest &Request = Out.Request;
	Request.Method = "POST";
	Request.Target = Path.empty() ? std::string("/mcp") : Path;
	Request.Version = "HTTP/1.1";
	char PortText[16];
	std::snprintf(PortText, sizeof(PortText), ":%d", Port);
	AddHeader(Request, "Host", (Host.empty() ? std::string("127.0.0.1") : Host) + PortText);
	if (!Token.empty())
		AddHeader(Request, "Authorization", "Bearer " + Token);
	AddHeader(Request, "Content-Type", "application/json");
	AddHeader(Request, "Accept", "application/json, text/event-stream");
	AddHeader(Request, "Connection", "close");
	Request.Body = Text;

	Json Message;
	if (!ParseJson(Text, Message) || !Message.IsObject())
		return true;	// FamiTracker says what's wrong with it
	const Json *pMethod = Message.Get("method");
	if (pMethod == 0 || !pMethod->IsString()) {
		// Claude Code's answer to a request (FamiTracker never asks any)
		Out.Readable = Message.Get("result") != 0 || Message.Get("error") != 0;
		return true;
	}
	Out.Readable = true;
	Out.Method = pMethod->String();
	const Json *pId = Message.Get("id");
	if (pId != 0 && !pId->IsNull()) {
		Out.WantsAnswer = true;
		Out.Id = *pId;
	}

	// Requests of the 2026-07-28 protocol carry their version in _meta, and
	// over HTTP the headers have to say what the body says
	const Json *pParams = Message.Get("params");
	const Json *pMeta = (pParams != 0 && pParams->IsObject()) ? pParams->Get("_meta") : 0;
	const Json *pVersion = (pMeta != 0 && pMeta->IsObject()) ? pMeta->Get(VERSION_KEY) : 0;
	if (pVersion != 0 && pVersion->IsString()) {
		const std::string &Method = pMethod->String();
		AddHeader(Request, "MCP-Protocol-Version", pVersion->String());
		AddHeader(Request, "Mcp-Method", Method);
		const char *pKey = Method == "resources/read" ? "uri" : (Method == "tools/call" || Method == "prompts/get") ? "name" : 0;
		const Json *pName = pKey != 0 ? pParams->Get(pKey) : 0;
		if (pName != 0 && pName->IsString())
			AddHeader(Request, "Mcp-Name", HeaderValue(pName->String()));
	}
	else if (!Version.empty() && Out.Method != "initialize")
		AddHeader(Request, "MCP-Protocol-Version", Version);
	return true;
}

std::string BridgeAgreedVersion(const BridgeMessage &Sent, const HttpResponse &Answer)
{
	Json Message;
	if (Sent.Method != "initialize" || !Sent.WantsAnswer || Answer.Status != 200 || !ParseJson(Answer.Body, Message) ||
		!Message.IsObject())
		return std::string();
	const Json *pResult = Message.Get("result");
	const Json *pVersion = (pResult != 0 && pResult->IsObject()) ? pResult->Get("protocolVersion") : 0;
	return (pVersion != 0 && pVersion->IsString()) ? HeadText(pVersion->String()) : std::string();
}

std::string BridgeAnswer(const BridgeMessage &Sent, const HttpResponse &Answer)
{
	Json Message;
	if (!Answer.Body.empty() && ParseJson(Answer.Body, Message) && Message.IsObject() && Message.Get("jsonrpc") != 0) {
		if (Sent.WantsAnswer) {
			// Refused before it was read (a wrong access code, say): the
			// answer is still for this request
			const Json *pId = Message.Get("id");
			if (pId == 0 || pId->IsNull())
				Message.Set("id", Sent.Id);
			return Line(Message);
		}
		// Nothing waits for an answer to a notification or to Claude Code's
		// own answer; a line that wasn't JSON-RPC gets its error
		return Sent.Readable ? std::string() : Line(Message);
	}
	if (!Sent.WantsAnswer)
		return std::string();
	std::string Detail = Answer.Body.substr(0, Answer.Body.find('\n'));
	if (Detail.size() > 200)
		Detail = Detail.substr(0, 200) + "...";
	char Text[96];
	std::snprintf(Text, sizeof(Text), "FamiTracker answered HTTP %d %s", Answer.Status, HttpReason(Answer.Status));
	return ErrorLine(Sent.Id, std::string(Text) + (Trim(Detail).empty() ? std::string(".") : ": " + Trim(ValidUtf8(Detail))));
}

std::string BridgeFailure(const BridgeMessage &Sent, const std::string &Reason)
{
	return Sent.WantsAnswer ? ErrorLine(Sent.Id, Reason) : std::string();
}

std::string FormatHttpRequest(const HttpRequest &Request)
{
	std::string Out = HeadText(Request.Method) + " " + HeadText(Request.Target) + " " +
		(Request.Version.empty() ? std::string("HTTP/1.1") : HeadText(Request.Version)) + "\r\n";
	for (size_t i = 0; i < Request.Headers.size(); ++i) {
		if (EqualsNoCase(Request.Headers[i].first, "Content-Length"))
			continue;
		Out += HeadText(Request.Headers[i].first) + ": " + HeadText(Request.Headers[i].second) + "\r\n";
	}
	char Length[48];
	std::snprintf(Length, sizeof(Length), "Content-Length: %lu\r\n\r\n", static_cast<unsigned long>(Request.Body.size()));
	Out += Length;
	Out += Request.Body;
	return Out;
}

HttpResponseRead ReadHttpResponse(const std::string &Bytes, bool Closed, HttpResponse &Out)
{
	size_t Start = 0;
	for (;;) {
		const size_t End = Bytes.find("\r\n\r\n", Start);
		if (End == std::string::npos)
			return (Closed || Bytes.size() - Start > MAX_HEAD) ? RESPONSE_BAD : RESPONSE_NEED_MORE;
		if (End - Start > MAX_HEAD)
			return RESPONSE_BAD;

		// "HTTP/1.1 200 OK"
		const size_t LineEnd = Bytes.find("\r\n", Start);
		const std::string StatusLine = Bytes.substr(Start, LineEnd - Start);
		const size_t Space = StatusLine.find(' ');
		if (StatusLine.compare(0, 5, "HTTP/") != 0 || Space == std::string::npos || Space + 4 > StatusLine.size() ||
			(Space + 4 < StatusLine.size() && StatusLine[Space + 4] != ' '))
			return RESPONSE_BAD;
		HttpResponse Response;
		Response.Status = 0;
		for (size_t i = Space + 1; i < Space + 4; ++i) {
			if (StatusLine[i] < '0' || StatusLine[i] > '9')
				return RESPONSE_BAD;
			Response.Status = Response.Status * 10 + (StatusLine[i] - '0');
		}

		bool HasLength = false, Chunked = false;
		size_t Length = 0;
		for (size_t Position = LineEnd + 2; Position < End; ) {
			const size_t Next = Bytes.find("\r\n", Position);
			const std::string Header = Bytes.substr(Position, Next - Position);
			Position = Next + 2;
			const size_t Colon = Header.find(':');
			if (Colon == std::string::npos || Colon == 0)
				return RESPONSE_BAD;
			const std::string Name = Header.substr(0, Colon), Value = Trim(Header.substr(Colon + 1));
			if (EqualsNoCase(Name, "Content-Length")) {
				size_t Given = 0;
				if (!ParseSize(Value, Given) || (HasLength && Given != Length))
					return RESPONSE_BAD;
				Length = Given;
				HasLength = true;
			}
			else if (EqualsNoCase(Name, "Transfer-Encoding")) {
				if (!EqualsNoCase(Value, "identity"))
					Chunked = true;
			}
			else if (EqualsNoCase(Name, "Content-Type"))
				Response.ContentType = Value;
			else
				Response.Headers.push_back(std::make_pair(Name, Value));
		}

		const size_t BodyStart = End + 4;
		if (Response.Status >= 100 && Response.Status < 200) {
			Start = BodyStart;	// "100 Continue" and the like come before the answer
			continue;
		}
		if (Chunked)
			return RESPONSE_BAD;	// FamiTracker always gives the length
		if (Response.Status == 204 || Response.Status == 304) {
			Out = Response;
			return RESPONSE_COMPLETE;
		}
		if (HasLength) {
			if (Length > MAX_BODY)
				return RESPONSE_BAD;
			if (Bytes.size() - BodyStart < Length)
				return Closed ? RESPONSE_BAD : RESPONSE_NEED_MORE;
			Response.Body = Bytes.substr(BodyStart, Length);
			Out = Response;
			return RESPONSE_COMPLETE;
		}
		if (!Closed)
			return RESPONSE_NEED_MORE;
		Response.Body = Bytes.substr(BodyStart);
		Out = Response;
		return RESPONSE_COMPLETE;
	}
}

}	// namespace ftapi

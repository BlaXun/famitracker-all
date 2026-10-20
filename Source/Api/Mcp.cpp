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

#include "Mcp.h"
#include "Json.h"
#include "Tools.h"

#include <cstdio>

namespace ftapi {

namespace {

const char *const LEGACY_VERSIONS[] = { "2025-11-25", "2025-06-18", "2025-03-26" };
const char *const SERVER_NAME = "famitracker";
const char *const SERVER_TITLE = "FamiTracker";
const char *const SERVER_INFO_KEY = "io.modelcontextprotocol/serverInfo";
const char *const VERSION_KEY = "io.modelcontextprotocol/protocolVersion";
const int CACHE_MS = 3600000;

// JSON-RPC error codes
const int PARSE_ERROR = -32700;
const int INVALID_REQUEST = -32600;
const int METHOD_NOT_FOUND = -32601;
const int INVALID_PARAMS = -32602;
const int ACCESS_DENIED = -32001;		// implementation-defined
const int HEADER_MISMATCH = -32020;
const int UNSUPPORTED_VERSION = -32022;

bool IsLegacyVersion(const std::string &Version)
{
	for (size_t i = 0; i < sizeof(LEGACY_VERSIONS) / sizeof(LEGACY_VERSIONS[0]); ++i)
		if (Version == LEGACY_VERSIONS[i])
			return true;
	return false;
}

HttpResponse JsonResponse(int Status, const Json &Body)
{
	HttpResponse Response;
	Response.Status = Status;
	Response.ContentType = "application/json";
	Response.Body = WriteJson(Body);
	return Response;
}

Json ErrorMessage(const Json &Id, int Code, const std::string &Text, const Json *pData = 0)
{
	Json Error = Json::MakeObject();
	Error.Set("code", Code);
	Error.Set("message", Text);
	if (pData != 0)
		Error.Set("data", *pData);
	Json Message = Json::MakeObject();
	Message.Set("jsonrpc", "2.0");
	Message.Set("id", Id);
	Message.Set("error", Error);
	return Message;
}

HttpResponse Failure(int Status, const Json &Id, int Code, const std::string &Text, const Json *pData = 0)
{
	return JsonResponse(Status, ErrorMessage(Id, Code, Text, pData));
}

HttpResponse Result(const Json &Id, const Json &Value)
{
	Json Message = Json::MakeObject();
	Message.Set("jsonrpc", "2.0");
	Message.Set("id", Id);
	Message.Set("result", Value);
	return JsonResponse(200, Message);
}

std::string Lower(const std::string &Text)
{
	std::string Out = Text;
	for (size_t i = 0; i < Out.size(); ++i)
		if (Out[i] >= 'A' && Out[i] <= 'Z')
			Out[i] = static_cast<char>(Out[i] - 'A' + 'a');
	return Out;
}

bool IsLocalName(const std::string &Name)
{
	const std::string Host = Lower(Name);
	return Host == "127.0.0.1" || Host == "localhost" || Host == "[::1]";
}

// "127.0.0.1:46600", "localhost", "[::1]:46600"
bool IsLocalHost(const std::string &Value)
{
	std::string Host = Value;
	if (!Host.empty() && Host[0] == '[') {
		const size_t Close = Host.find(']');
		if (Close == std::string::npos)
			return false;
		Host = Host.substr(0, Close + 1);
	}
	else {
		const size_t Colon = Host.find(':');
		if (Colon != std::string::npos)
			Host = Host.substr(0, Colon);
	}
	return IsLocalName(Host);
}

// "http://localhost:3000": a page from this computer
bool IsLocalOrigin(const std::string &Origin)
{
	const std::string Text = Lower(Origin);
	size_t Start;
	if (Text.compare(0, 7, "http://") == 0)
		Start = 7;
	else if (Text.compare(0, 8, "https://") == 0)
		Start = 8;
	else
		return false;	// "null" and anything else
	const size_t End = Text.find('/', Start);
	return IsLocalHost(Text.substr(Start, End == std::string::npos ? std::string::npos : End - Start));
}

bool SameText(const std::string &a, const std::string &b)
{
	// The whole length every time, so the token can't be guessed by timing
	unsigned char Difference = static_cast<unsigned char>(a.size() != b.size());
	const size_t Length = a.size() > b.size() ? a.size() : b.size();
	for (size_t i = 0; i < Length; ++i) {
		const unsigned char x = i < a.size() ? static_cast<unsigned char>(a[i]) : 0;
		const unsigned char y = i < b.size() ? static_cast<unsigned char>(b[i]) : 0;
		Difference |= static_cast<unsigned char>(x ^ y);
	}
	return Difference == 0;
}

bool TokenMatches(const std::string *pAuthorization, const std::string &Token)
{
	if (pAuthorization == 0 || pAuthorization->size() < 7 || Lower(pAuthorization->substr(0, 7)) != "bearer ")
		return false;
	size_t Start = 7;
	while (Start < pAuthorization->size() && (*pAuthorization)[Start] == ' ')
		++Start;
	return SameText(pAuthorization->substr(Start), Token);
}

// An Mcp-Name header value: as is, or "=?base64?...?=" decoded
bool DecodeHeaderValue(const std::string &Value, std::string &Out)
{
	const std::string Prefix = "=?base64?", Suffix = "?=";
	if (Value.size() < Prefix.size() + Suffix.size() || Value.compare(0, Prefix.size(), Prefix) != 0 ||
		Value.compare(Value.size() - Suffix.size(), Suffix.size(), Suffix) != 0) {
		Out = Value;
		return true;
	}
	const std::string Data = Value.substr(Prefix.size(), Value.size() - Prefix.size() - Suffix.size());
	Out.clear();
	unsigned long Bits = 0;
	int Count = 0;
	for (size_t i = 0; i < Data.size(); ++i) {
		const char c = Data[i];
		int Digit;
		if (c >= 'A' && c <= 'Z') Digit = c - 'A';
		else if (c >= 'a' && c <= 'z') Digit = c - 'a' + 26;
		else if (c >= '0' && c <= '9') Digit = c - '0' + 52;
		else if (c == '+') Digit = 62;
		else if (c == '/') Digit = 63;
		else if (c == '=') break;
		else
			return false;
		Bits = (Bits << 6) | static_cast<unsigned long>(Digit);
		Count += 6;
		if (Count >= 8) {
			Count -= 8;
			Out += static_cast<char>((Bits >> Count) & 0xFF);
		}
	}
	return true;
}

Json ServerInfo(const McpConfig &Config)
{
	Json Info = Json::MakeObject();
	Info.Set("name", SERVER_NAME);
	Info.Set("title", SERVER_TITLE);
	Info.Set("version", Config.Version.empty() ? std::string("0.4.6") : Config.Version);
	return Info;
}

Json ModernMeta(const McpConfig &Config)
{
	Json Meta = Json::MakeObject();
	Meta.Set(SERVER_INFO_KEY, ServerInfo(Config));
	return Meta;
}

Json ToolsJson(const ToolSet &Tools)
{
	Json List = Json::MakeArray();
	for (size_t i = 0; i < Tools.List().size(); ++i) {
		const ToolInfo &Info = Tools.List()[i];
		Json Tool = Json::MakeObject();
		Tool.Set("name", Info.Name);
		Tool.Set("title", Info.Title);
		Tool.Set("description", Info.Description);
		Tool.Set("inputSchema", Info.InputSchema);
		Json Hints = Json::MakeObject();
		Hints.Set("title", Info.Title);
		Hints.Set("readOnlyHint", Info.ReadOnly);
		Hints.Set("destructiveHint", Info.Destructive);
		Hints.Set("idempotentHint", Info.Idempotent);
		Hints.Set("openWorldHint", false);
		Tool.Set("annotations", Hints);
		List.Push(Tool);
	}
	return List;
}

// tools/call: the result, or false with a JSON-RPC error
bool CallTool(ToolSet &Tools, const Json &Params, Json &Out, int &ErrorCode, std::string &ErrorText)
{
	const Json *pName = Params.Get("name");
	if (pName == 0 || !pName->IsString()) {
		ErrorCode = INVALID_PARAMS;
		ErrorText = "tools/call needs the tool's name.";
		return false;
	}
	if (!Tools.Has(pName->String())) {
		ErrorCode = INVALID_PARAMS;
		ErrorText = "Unknown tool: " + pName->String();
		return false;
	}
	const Json *pArguments = Params.Get("arguments");
	if (pArguments != 0 && !pArguments->IsNull() && !pArguments->IsObject()) {
		ErrorCode = INVALID_PARAMS;
		ErrorText = "The tool's arguments should be an object.";
		return false;
	}
	const ToolResult Called = Tools.Call(pName->String(), pArguments != 0 && pArguments->IsObject() ? *pArguments : Json::MakeObject());
	Json Text = Json::MakeObject();
	Text.Set("type", "text");
	Text.Set("text", Called.Text);
	Json Content = Json::MakeArray();
	Content.Push(Text);
	if (!Called.Image.empty()) {
		Json Image = Json::MakeObject();
		Image.Set("type", "image");
		Image.Set("data", Base64Encode(Called.Image));
		Image.Set("mimeType", "image/png");
		Content.Push(Image);
	}
	Out = Json::MakeObject();
	Out.Set("content", Content);
	Out.Set("isError", Called.IsError);
	return true;
}

Json Capabilities()
{
	Json Tools = Json::MakeObject();
	Tools.Set("listChanged", false);
	Json Capabilities = Json::MakeObject();
	Capabilities.Set("tools", Tools);
	return Capabilities;
}

Json SupportedVersions()
{
	Json Versions = Json::MakeArray();
	Versions.Push(MCP_MODERN_VERSION);
	for (size_t i = 0; i < sizeof(LEGACY_VERSIONS) / sizeof(LEGACY_VERSIONS[0]); ++i)
		Versions.Push(LEGACY_VERSIONS[i]);
	return Versions;
}

Json DiscoverResult(const McpConfig &Config)
{
	Json Result = Json::MakeObject();
	Result.Set("resultType", "complete");
	Json Versions = Json::MakeArray();
	Versions.Push(MCP_MODERN_VERSION);
	Result.Set("supportedVersions", Versions);
	Result.Set("capabilities", Capabilities());
	Result.Set("_meta", ModernMeta(Config));
	Result.Set("instructions", ToolSet::Instructions());
	Result.Set("ttlMs", CACHE_MS);
	Result.Set("cacheScope", "private");
	return Result;
}

// Requests carrying their protocol version in _meta (2026-07-28)
HttpResponse Modern(const HttpRequest &Request, const McpConfig &Config, ToolSet &Tools, const Json &Id, const std::string &Method,
	const Json &Params, const Json &Version)
{
	if (!Version.IsString())
		return Failure(400, Id, INVALID_PARAMS, "The protocol version should be a string.");
	// The headers must say what the body says
	const std::string *pVersion = Request.Header("MCP-Protocol-Version");
	if (pVersion == 0 || *pVersion != Version.String())
		return Failure(400, Id, HEADER_MISMATCH, pVersion == 0 ? "Missing the MCP-Protocol-Version header." :
			"The MCP-Protocol-Version header doesn't match the request's protocol version.");
	const std::string *pMethod = Request.Header("Mcp-Method");
	if (pMethod == 0 || *pMethod != Method)
		return Failure(400, Id, HEADER_MISMATCH, pMethod == 0 ? "Missing the Mcp-Method header." :
			"The Mcp-Method header doesn't match the request's method.");
	if (Method == "tools/call" || Method == "resources/read" || Method == "prompts/get") {
		const Json *pName = Params.Get(Method == "resources/read" ? "uri" : "name");
		const std::string *pHeader = Request.Header("Mcp-Name");
		std::string Decoded;
		if (pHeader == 0 || !DecodeHeaderValue(*pHeader, Decoded) || pName == 0 || !pName->IsString() || Decoded != pName->String())
			return Failure(400, Id, HEADER_MISMATCH, pHeader == 0 ? "Missing the Mcp-Name header." :
				"The Mcp-Name header doesn't match the request's name.");
	}
	if (Version.String() != MCP_MODERN_VERSION) {
		Json Data = Json::MakeObject();
		Data.Set("supported", SupportedVersions());
		Data.Set("requested", Version);
		return Failure(400, Id, UNSUPPORTED_VERSION, "Unsupported protocol version", &Data);
	}

	Json Out;
	if (Method == "server/discover")
		return Result(Id, DiscoverResult(Config));
	if (Method == "tools/list") {
		Out = Json::MakeObject();
		Out.Set("resultType", "complete");
		Out.Set("tools", ToolsJson(Tools));
		Out.Set("ttlMs", CACHE_MS);
		Out.Set("cacheScope", "private");
	}
	else if (Method == "tools/call") {
		int Code;
		std::string Text;
		if (!CallTool(Tools, Params, Out, Code, Text))
			return Failure(200, Id, Code, Text);
		Json Ordered = Json::MakeObject();
		Ordered.Set("resultType", "complete");
		for (size_t i = 0; i < Out.Members().size(); ++i)
			Ordered.Set(Out.Members()[i].first, Out.Members()[i].second);
		Out = Ordered;
	}
	else
		return Failure(404, Id, METHOD_NOT_FOUND, "Method not found: " + Method);
	Out.Set("_meta", ModernMeta(Config));
	return Result(Id, Out);
}

// The initialize handshake's era (2025-03-26 to 2025-11-25)
HttpResponse Legacy(const HttpRequest &Request, const McpConfig &Config, ToolSet &Tools, const Json &Id, const std::string &Method,
	const Json &Params)
{
	const std::string *pVersion = Request.Header("MCP-Protocol-Version");
	if (pVersion != 0 && *pVersion == MCP_MODERN_VERSION)
		return Failure(400, Id, HEADER_MISMATCH, "The request has no protocol version in its _meta.");
	if (pVersion != 0 && !IsLegacyVersion(*pVersion))
		return Failure(400, Id, INVALID_REQUEST, "Unsupported protocol version: " + *pVersion);

	if (Method == "initialize") {
		const Json *pRequested = Params.Get("protocolVersion");
		const std::string Requested = (pRequested != 0 && pRequested->IsString()) ? pRequested->String() : std::string();
		Json Out = Json::MakeObject();
		Out.Set("protocolVersion", IsLegacyVersion(Requested) ? Requested : std::string(MCP_LEGACY_VERSION));
		Out.Set("capabilities", Capabilities());
		Out.Set("serverInfo", ServerInfo(Config));
		Out.Set("instructions", ToolSet::Instructions());
		return Result(Id, Out);
	}
	if (Method == "ping")
		return Result(Id, Json::MakeObject());
	if (Method == "tools/list") {
		Json Out = Json::MakeObject();
		Out.Set("tools", ToolsJson(Tools));
		return Result(Id, Out);
	}
	if (Method == "tools/call") {
		Json Out;
		int Code;
		std::string Text;
		if (!CallTool(Tools, Params, Out, Code, Text))
			return Failure(200, Id, Code, Text);
		return Result(Id, Out);
	}
	if (Method == "server/discover")
		return Result(Id, DiscoverResult(Config));
	return Failure(200, Id, METHOD_NOT_FOUND, "Method not found: " + Method);
}

}	// namespace

HttpResponse HandleMcp(const HttpRequest &Request, const McpConfig &Config, ToolSet &Tools)
{
	const Json NoId;
	const std::string Path = Request.Path();
	if (Path != "/mcp" && Path != "/mcp/") {
		HttpResponse Response;
		Response.Status = 404;
		Response.ContentType = "text/plain; charset=utf-8";
		Response.Body = "FamiTracker's MCP endpoint is /mcp.\n";
		return Response;
	}

	// Only from this computer: a web page can't reach it through a renamed
	// host (DNS rebinding) or from another site
	const std::string *pHost = Request.Header("Host");
	if (pHost != 0 && !IsLocalHost(*pHost))
		return Failure(403, NoId, ACCESS_DENIED, "FamiTracker only answers requests to 127.0.0.1 or localhost.");
	const std::string *pOrigin = Request.Header("Origin");
	if (pOrigin != 0 && !IsLocalOrigin(*pOrigin))
		return Failure(403, NoId, ACCESS_DENIED, "FamiTracker doesn't answer requests from web pages.");

	if (Request.Method != "POST") {
		HttpResponse Response = Failure(405, NoId, INVALID_REQUEST, "FamiTracker's MCP endpoint takes POST requests.");
		Response.Headers.push_back(std::make_pair(std::string("Allow"), std::string("POST")));
		return Response;
	}
	if (Config.Token.empty())
		return Failure(403, NoId, ACCESS_DENIED, "FamiTracker has no access token set, so it answers nothing.");
	if (!TokenMatches(Request.Header("Authorization"), Config.Token))
		return Failure(403, NoId, ACCESS_DENIED, "FamiTracker refused the request: the access token is missing or wrong. "
			"File > Connect Claude Code in FamiTracker shows the command that connects with the right one.");
	const std::string *pType = Request.Header("Content-Type");
	if (pType == 0 || Lower(*pType).compare(0, 16, "application/json") != 0)
		return Failure(415, NoId, INVALID_REQUEST, "Send JSON (Content-Type: application/json).");

	Json Message;
	std::string Error;
	if (!ParseJson(Request.Body, Message, &Error))
		return Failure(400, NoId, PARSE_ERROR, "Parse error: " + Error);
	if (Message.IsArray())
		return Failure(400, NoId, INVALID_REQUEST, "Batched requests aren't supported.");
	if (!Message.IsObject())
		return Failure(400, NoId, INVALID_REQUEST, "The request should be a JSON-RPC object.");
	const Json *pMethod = Message.Get("method");
	if (pMethod == 0) {
		if (Message.Get("result") != 0 || Message.Get("error") != 0) {
			HttpResponse Accepted;	// a response to us; we never ask anything
			Accepted.Status = 202;
			return Accepted;
		}
		return Failure(400, NoId, INVALID_REQUEST, "The request has no method.");
	}
	const Json *pRpc = Message.Get("jsonrpc");
	if (pRpc == 0 || !pRpc->IsString() || pRpc->String() != "2.0" || !pMethod->IsString())
		return Failure(400, NoId, INVALID_REQUEST, "Not a JSON-RPC 2.0 request.");
	const Json *pId = Message.Get("id");
	if (pId == 0) {
		HttpResponse Accepted;	// a notification (initialized, cancelled...)
		Accepted.Status = 202;
		return Accepted;
	}
	if (!pId->IsString() && !pId->IsNumber())
		return Failure(400, NoId, INVALID_REQUEST, "The request's id should be a string or a number.");
	const Json *pParams = Message.Get("params");
	if (pParams != 0 && !pParams->IsObject())
		return Failure(400, *pId, INVALID_PARAMS, "params should be an object.");
	const Json Params = pParams != 0 ? *pParams : Json::MakeObject();

	const Json *pMeta = Params.Get("_meta");
	const Json *pVersion = (pMeta != 0 && pMeta->IsObject()) ? pMeta->Get(VERSION_KEY) : 0;
	if (pVersion != 0)
		return Modern(Request, Config, Tools, *pId, pMethod->String(), Params, *pVersion);
	return Legacy(Request, Config, Tools, *pId, pMethod->String(), Params);
}

std::string ClaudeAddCommand(const McpConfig &Config)
{
	char Url[64];
	std::snprintf(Url, sizeof(Url), "http://127.0.0.1:%d/mcp", Config.Port);
	std::string Command = "claude mcp add --transport http --scope user famitracker ";
	Command += Url;
	if (!Config.Token.empty())
		Command += " --header \"Authorization: Bearer " + Config.Token + "\"";
	return Command;
}

std::string WslAddCommand(const std::string &HelperPath)
{
	// wslpath turns the Windows path into WSL's, wherever its drives are
	std::string Quoted = "'";
	for (size_t i = 0; i < HelperPath.size(); ++i)
		Quoted += HelperPath[i] == '\'' ? std::string("'\\''") : std::string(1, HelperPath[i]);
	Quoted += "'";
	return "claude mcp remove --scope user famitracker >/dev/null 2>&1; "
		"claude mcp add --transport stdio --scope user famitracker -- \"$(wslpath " + Quoted + ")\"";
}

}	// namespace ftapi

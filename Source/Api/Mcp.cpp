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
#include "Network.h"
#include "PhoneTakes.h"
#include "Song.h"
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
const char *const FIRST_VERSION = "2025-03-26";	// a request whose version isn't said is of this era, as the protocol has it
const char *const FILES_CODE_PURPOSE = "FamiTracker exported files";
const size_t FILES_CODE_DIGITS = 24;
// The biggest file resources/read gives (base64 makes it a third more, within what famitracker-mcp reads)
const size_t MAX_RESOURCE_BYTES = 32u << 20;

// JSON-RPC error codes
const int PARSE_ERROR = -32700;
const int INVALID_REQUEST = -32600;
const int METHOD_NOT_FOUND = -32601;
const int INVALID_PARAMS = -32602;
const int ACCESS_DENIED = -32001;		// implementation-defined
const int RESOURCE_NOT_FOUND = -32002;
const int INTERNAL_ERROR = -32603;
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

// A Host header FamiTracker answers: 127.0.0.1 or localhost, and when other
// computers may connect, this computer's addresses and name too
bool IsAcceptedHost(const std::string &Value, const McpConfig &Config)
{
	return IsLocalHost(Value) || (Config.Network && NamesThisComputer(Value, Config.Addresses, Config.ComputerName));
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

// What the request asked for: all the tools (/mcp) or the small set (/mcp/core), who asks, and in which version of
// the protocol
struct Endpoint {
	bool Core;
	Caller Who;
	std::string Version;
	Endpoint() : Core(false) {}
};

// A reply may link a resource (resource_link) from 2025-06-18 on; every version may carry one (an embedded resource)
bool LinksResources(const Endpoint &E)
{
	return E.Version != FIRST_VERSION;
}

std::string HexText(const std::string &Bytes)
{
	static const char DIGITS[] = "0123456789abcdef";
	std::string Out;
	for (size_t i = 0; i < Bytes.size(); ++i) {
		const unsigned char c = static_cast<unsigned char>(Bytes[i]);
		Out += DIGITS[c >> 4];
		Out += DIGITS[c & 15];
	}
	return Out;
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

Json ToolsJson(const ToolSet &Tools, const Endpoint &E)
{
	Json List = Json::MakeArray();
	for (size_t i = 0; i < Tools.List().size(); ++i) {
		const ToolInfo &Info = Tools.List()[i];
		if (E.Core && !ToolSet::IsCoreTool(Info.Name))
			continue;
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
bool CallTool(ToolSet &Tools, const Endpoint &E, const Json &Params, Json &Out, int &ErrorCode, std::string &ErrorText)
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
	if (E.Core && !ToolSet::IsCoreTool(pName->String())) {
		ErrorCode = INVALID_PARAMS;
		ErrorText = "Unknown tool: " + pName->String() + ". This is FamiTracker's small set of tools (/mcp/core); /mcp has all of them.";
		return false;
	}
	const Json *pArguments = Params.Get("arguments");
	if (pArguments != 0 && !pArguments->IsNull() && !pArguments->IsObject()) {
		ErrorCode = INVALID_PARAMS;
		ErrorText = "The tool's arguments should be an object.";
		return false;
	}
	const ToolResult Called = Tools.Call(pName->String(), pArguments != 0 && pArguments->IsObject() ? *pArguments : Json::MakeObject(),
		E.Who);
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
	for (size_t i = 0; i < Called.Files.size(); ++i) {
		const ToolFile &File = Called.Files[i];
		Json Item = Json::MakeObject();
		if (!File.Bytes.empty()) {
			// The file itself (export_for_game's attach), for a client that saves what a reply carries
			Json Resource = Json::MakeObject();
			Resource.Set("uri", File.Uri);
			Resource.Set("mimeType", File.MimeType);
			Resource.Set("blob", Base64Encode(File.Bytes));
			Item.Set("type", "resource");
			Item.Set("resource", Resource);
		}
		else if (LinksResources(E)) {
			// A link to it, which the client reads with resources/read
			Item.Set("type", "resource_link");
			Item.Set("uri", File.Uri);
			Item.Set("name", File.Name);
			Item.Set("mimeType", File.MimeType);
			Item.Set("size", static_cast<long long>(File.Size));
		}
		else
			continue;
		Content.Push(Item);
	}
	Out = Json::MakeObject();
	Out.Set("content", Content);
	Out.Set("isError", Called.IsError);
	return true;
}

// The OGG files export_for_game saved next to the song, as MCP's resources (resources/list)
Json ResourcesJson(ToolSet &Tools)
{
	std::vector<SongFile> Files;
	std::string Error;
	Json List = Json::MakeArray();
	if (!Tools.ListExports(Files, Error))
		return List;
	for (size_t i = 0; i < Files.size(); ++i) {
		Json Item = Json::MakeObject();
		Item.Set("uri", ExportUri(Files[i].Name));
		Item.Set("name", Files[i].Name);
		Item.Set("title", Files[i].Name);
		Item.Set("description", "An OGG file export_for_game saved next to the song");
		Item.Set("mimeType", ExportMimeType(Files[i].Name));
		Item.Set("size", Files[i].Bytes);
		List.Push(Item);
	}
	return List;
}

// How the resources are named (resources/templates/list)
Json TemplatesJson()
{
	Json Template = Json::MakeObject();
	Template.Set("uriTemplate", std::string(EXPORT_URI_PREFIX) + "{name}");
	Template.Set("name", "export");
	Template.Set("title", "A file export_for_game saved");
	Template.Set("description", "An OGG file export_for_game saved next to the song, by its file name (Town.ogg).");
	Json List = Json::MakeArray();
	List.Push(Template);
	return List;
}

// resources/read: the file, its bytes in base64; false with the error's code, text and data
bool ReadResource(ToolSet &Tools, const Json &Params, Json &Out, int &Code, std::string &Text, Json &Data)
{
	const Json *pUri = Params.Get("uri");
	if (pUri == 0 || !pUri->IsString()) {
		Code = INVALID_PARAMS;
		Text = "resources/read needs the resource's uri.";
		return false;
	}
	Data = Json::MakeObject();
	Data.Set("uri", pUri->String());
	std::string Name, Bytes, Error;
	if (!ParseExportUri(pUri->String(), Name)) {
		Code = RESOURCE_NOT_FOUND;
		Text = "FamiTracker's resources are the OGG files export_for_game saved next to the song, as " +
			std::string(EXPORT_URI_PREFIX) + "<file name> (resources/list lists them).";
		return false;
	}
	if (!Tools.ReadExport(Name, Bytes, Error)) {
		Code = Error.empty() ? RESOURCE_NOT_FOUND : INTERNAL_ERROR;
		Text = Error.empty() ? "There's no " + Name + " next to the song (resources/list lists the files there); export_for_game saves "
			"it." : Error;
		return false;
	}
	if (Bytes.size() > MAX_RESOURCE_BYTES) {
		Code = INTERNAL_ERROR;
		Text = Name + " is too big for a reply (over 32 MB); a client on another computer fetches it by the link export_for_game gives.";
		return false;
	}
	Json Content = Json::MakeObject();
	Content.Set("uri", pUri->String());
	Content.Set("mimeType", ExportMimeType(Name));
	Content.Set("blob", Base64Encode(Bytes));
	Json Contents = Json::MakeArray();
	Contents.Push(Content);
	Out = Json::MakeObject();
	Out.Set("contents", Contents);
	return true;
}

// The small set has no recipes, so a client adds no tools of its own to list and get them
Json Capabilities(const Endpoint &E)
{
	Json Tools = Json::MakeObject();
	Tools.Set("listChanged", false);
	Json Capabilities = Json::MakeObject();
	Capabilities.Set("tools", Tools);
	// The files export_for_game saves, for a client that can't fetch them by HTTP
	Json Resources = Json::MakeObject();
	Resources.Set("subscribe", false);
	Resources.Set("listChanged", false);
	Capabilities.Set("resources", Resources);
	if (!E.Core) {
		Json Prompts = Json::MakeObject();
		Prompts.Set("listChanged", false);
		Capabilities.Set("prompts", Prompts);
	}
	return Capabilities;
}

std::string InstructionsFor(const Endpoint &E)
{
	return E.Core ? ToolSet::CoreInstructions() : ToolSet::Instructions();
}

// The recipes, as prompts/list gives them
Json PromptsJson()
{
	Json List = Json::MakeArray();
	const std::vector<PromptInfo> &Prompts = ToolSet::Prompts();
	for (size_t i = 0; i < Prompts.size(); ++i) {
		Json P = Json::MakeObject();
		P.Set("name", Prompts[i].Name);
		P.Set("title", Prompts[i].Title);
		P.Set("description", Prompts[i].Description);
		Json Arguments = Json::MakeArray();
		for (size_t a = 0; a < Prompts[i].Arguments.size(); ++a) {
			Json Argument = Json::MakeObject();
			Argument.Set("name", Prompts[i].Arguments[a].Name);
			Argument.Set("description", Prompts[i].Arguments[a].Description);
			Argument.Set("required", Prompts[i].Arguments[a].Required);
			Arguments.Push(Argument);
		}
		P.Set("arguments", Arguments);
		List.Push(P);
	}
	return List;
}

// prompts/get: the recipe's message; false with the error's code and text
bool GetPrompt(const Json &Params, Json &Out, int &Code, std::string &Text)
{
	const Json *pName = Params.Get("name");
	if (pName == 0 || !pName->IsString()) {
		Code = INVALID_PARAMS;
		Text = "prompts/get needs a name.";
		return false;
	}
	const Json *pArguments = Params.Get("arguments");
	std::string Message, Error;
	if (!ToolSet::PromptText(pName->String(), pArguments != 0 ? *pArguments : Json::MakeObject(), Message, Error)) {
		Code = INVALID_PARAMS;
		Text = Error;
		return false;
	}
	std::string Description;
	const std::vector<PromptInfo> &Prompts = ToolSet::Prompts();
	for (size_t i = 0; i < Prompts.size(); ++i)
		if (Prompts[i].Name == pName->String())
			Description = Prompts[i].Description;
	Json Content = Json::MakeObject();
	Content.Set("type", "text");
	Content.Set("text", Message);
	Json Item = Json::MakeObject();
	Item.Set("role", "user");
	Item.Set("content", Content);
	Json Messages = Json::MakeArray();
	Messages.Push(Item);
	Out = Json::MakeObject();
	Out.Set("description", Description);
	Out.Set("messages", Messages);
	return true;
}

Json SupportedVersions()
{
	Json Versions = Json::MakeArray();
	Versions.Push(MCP_MODERN_VERSION);
	for (size_t i = 0; i < sizeof(LEGACY_VERSIONS) / sizeof(LEGACY_VERSIONS[0]); ++i)
		Versions.Push(LEGACY_VERSIONS[i]);
	return Versions;
}

Json DiscoverResult(const McpConfig &Config, const Endpoint &E)
{
	Json Result = Json::MakeObject();
	Result.Set("resultType", "complete");
	Json Versions = Json::MakeArray();
	Versions.Push(MCP_MODERN_VERSION);
	Result.Set("supportedVersions", Versions);
	Result.Set("capabilities", Capabilities(E));
	Result.Set("_meta", ModernMeta(Config));
	Result.Set("instructions", InstructionsFor(E));
	Result.Set("ttlMs", CACHE_MS);
	Result.Set("cacheScope", "private");
	return Result;
}

// Requests carrying their protocol version in _meta (2026-07-28)
HttpResponse Modern(const HttpRequest &Request, const McpConfig &Config, ToolSet &Tools, const Endpoint &E, const Json &Id,
	const std::string &Method, const Json &Params, const Json &Version)
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
	const bool Prompts = Method == "prompts/list" || Method == "prompts/get";
	if (Method == "server/discover")
		return Result(Id, DiscoverResult(Config, E));
	if (Prompts && E.Core)
		return Failure(404, Id, METHOD_NOT_FOUND, "Method not found: " + Method + " (the small set of tools has no recipes)");
	if (Method == "tools/list") {
		Out = Json::MakeObject();
		Out.Set("resultType", "complete");
		Out.Set("tools", ToolsJson(Tools, E));
		Out.Set("ttlMs", CACHE_MS);
		Out.Set("cacheScope", "private");
	}
	else if (Method == "tools/call" || Method == "prompts/get") {
		int Code;
		std::string Text;
		if (Method == "tools/call" ? !CallTool(Tools, E, Params, Out, Code, Text) : !GetPrompt(Params, Out, Code, Text))
			return Failure(200, Id, Code, Text);
		Json Ordered = Json::MakeObject();
		Ordered.Set("resultType", "complete");
		for (size_t i = 0; i < Out.Members().size(); ++i)
			Ordered.Set(Out.Members()[i].first, Out.Members()[i].second);
		Out = Ordered;
	}
	else if (Method == "prompts/list") {
		Out = Json::MakeObject();
		Out.Set("resultType", "complete");
		Out.Set("prompts", PromptsJson());
		Out.Set("ttlMs", CACHE_MS);
		Out.Set("cacheScope", "private");
	}
	else if (Method == "resources/list" || Method == "resources/templates/list") {
		// The files come and go as songs are exported: the list isn't kept
		const bool Files = Method == "resources/list";
		Out = Json::MakeObject();
		Out.Set("resultType", "complete");
		Out.Set(Files ? "resources" : "resourceTemplates", Files ? ResourcesJson(Tools) : TemplatesJson());
		Out.Set("ttlMs", Files ? 0 : CACHE_MS);
		Out.Set("cacheScope", "private");
	}
	else if (Method == "resources/read") {
		int Code;
		std::string Text;
		Json Data;
		if (!ReadResource(Tools, Params, Out, Code, Text, Data))
			return Failure(200, Id, Code, Text, &Data);
		Json Ordered = Json::MakeObject();
		Ordered.Set("resultType", "complete");
		Ordered.Set("contents", *Out.Get("contents"));
		Out = Ordered;
	}
	else
		return Failure(404, Id, METHOD_NOT_FOUND, "Method not found: " + Method);
	Out.Set("_meta", ModernMeta(Config));
	return Result(Id, Out);
}

// The initialize handshake's era (2025-03-26 to 2025-11-25)
HttpResponse Legacy(const HttpRequest &Request, const McpConfig &Config, ToolSet &Tools, const Endpoint &E, const Json &Id,
	const std::string &Method, const Json &Params)
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
		Out.Set("capabilities", Capabilities(E));
		Out.Set("serverInfo", ServerInfo(Config));
		Out.Set("instructions", InstructionsFor(E));
		return Result(Id, Out);
	}
	if (Method == "ping")
		return Result(Id, Json::MakeObject());
	if (Method == "tools/list") {
		Json Out = Json::MakeObject();
		Out.Set("tools", ToolsJson(Tools, E));
		return Result(Id, Out);
	}
	if (Method == "tools/call") {
		Json Out;
		int Code;
		std::string Text;
		if (!CallTool(Tools, E, Params, Out, Code, Text))
			return Failure(200, Id, Code, Text);
		return Result(Id, Out);
	}
	if ((Method == "prompts/list" || Method == "prompts/get") && E.Core)
		return Failure(200, Id, METHOD_NOT_FOUND, "Method not found: " + Method + " (the small set of tools has no recipes)");
	if (Method == "prompts/list") {
		Json Out = Json::MakeObject();
		Out.Set("prompts", PromptsJson());
		return Result(Id, Out);
	}
	if (Method == "prompts/get") {
		Json Out;
		int Code;
		std::string Text;
		if (!GetPrompt(Params, Out, Code, Text))
			return Failure(200, Id, Code, Text);
		return Result(Id, Out);
	}
	if (Method == "resources/list" || Method == "resources/templates/list") {
		Json Out = Json::MakeObject();
		if (Method == "resources/list")
			Out.Set("resources", ResourcesJson(Tools));
		else
			Out.Set("resourceTemplates", TemplatesJson());
		return Result(Id, Out);
	}
	if (Method == "resources/read") {
		Json Out, Data;
		int Code;
		std::string Text;
		if (!ReadResource(Tools, Params, Out, Code, Text, Data))
			return Failure(200, Id, Code, Text, &Data);
		return Result(Id, Out);
	}
	if (Method == "server/discover")
		return Result(Id, DiscoverResult(Config, E));
	return Failure(200, Id, METHOD_NOT_FOUND, "Method not found: " + Method);
}

HttpResponse TextResponse(int Status, const std::string &Text)
{
	HttpResponse Response;
	Response.Status = Status;
	Response.ContentType = "text/plain; charset=utf-8";
	Response.Body = Text + "\n";
	return Response;
}

HttpResponse WrongMethod(const char *pAllowed, const std::string &Text)
{
	HttpResponse Response = Failure(405, Json(), INVALID_REQUEST, Text);
	Response.Headers.push_back(std::make_pair(std::string("Allow"), std::string(pAllowed)));
	return Response;
}

void AddHeader(HttpResponse &Response, const char *pName, const std::string &Value)
{
	Response.Headers.push_back(std::make_pair(std::string(pName), Value));
}

// GET or HEAD /files/<name>: an OGG file export_for_game saved next to the song, to play where it opens (inline), whole or
// the range asked for: a phone's browser asks for ranges before it plays a sound (iPhones' Safari plays nothing without)
HttpResponse FileRoute(const HttpRequest &Request, ToolSet &Tools, const std::string &Encoded)
{
	std::string Name, Bytes, Error;
	if (!UrlDecode(Encoded, Name) || !IsExportFile(Name))
		return TextResponse(400, "FamiTracker gives only the OGG files export_for_game saved next to the song, by their names.");
	if (!Tools.ReadExport(Name, Bytes, Error))
		return TextResponse(Error.empty() ? 404 : 500, Error.empty() ? "There's no " + Name + " next to the song; export_for_game "
			"saves it there." : Error);
	HttpResponse Response;
	Response.ContentType = ExportMimeType(Name);
	AddHeader(Response, "Accept-Ranges", "bytes");
	bool Plain = true;
	for (size_t i = 0; i < Name.size(); ++i)
		Plain = Plain && static_cast<unsigned char>(Name[i]) < 0x80;
	AddHeader(Response, "Content-Disposition", Plain ? "inline; filename=\"" + Name + "\"" : "inline; filename*=UTF-8''" + UrlEncode(Name));
	AddHeader(Response, "Cache-Control", "no-cache");
	AddHeader(Response, "X-Content-Type-Options", "nosniff");
	AddHeader(Response, "Referrer-Policy", "no-referrer");
	// One range, as browsers ask; anything else (several, If-Range, which FamiTracker can't check) gets the whole file
	const std::string *pRange = Request.Header("Range");
	size_t First = 0, Last = 0;
	const ByteRangeResult Range = pRange != 0 && Request.Header("If-Range") == 0 ? ParseByteRange(*pRange, Bytes.size(), First, Last) :
		RANGE_WHOLE;
	char Text[96];
	if (Range == RANGE_UNSATISFIABLE) {
		Response.Status = 416;
		Response.ContentType.clear();
		std::snprintf(Text, sizeof(Text), "bytes */%lu", static_cast<unsigned long>(Bytes.size()));
		AddHeader(Response, "Content-Range", Text);
		return Response;
	}
	if (Range == RANGE_PART) {
		Response.Status = 206;
		std::snprintf(Text, sizeof(Text), "bytes %lu-%lu/%lu", static_cast<unsigned long>(First), static_cast<unsigned long>(Last),
			static_cast<unsigned long>(Bytes.size()));
		AddHeader(Response, "Content-Range", Text);
		Response.Body = Bytes.substr(First, Last - First + 1);
		return Response;
	}
	Response.Body.swap(Bytes);
	return Response;
}

// The files' links over Tailscale, for a phone away from home: "http://100.101.102.103:46600/files/" when this computer
// has a Tailscale address and the client reached it another way (a home network's address, or a name not Tailscale's)
std::string AwayFilesUrl(const std::string *pHost, const McpConfig &Config)
{
	std::string Host = pHost != 0 ? Lower(*pHost) : std::string();
	Host = Host.substr(0, Host.find(':'));
	if (!Host.empty() && Host[Host.size() - 1] == '.')
		Host.erase(Host.size() - 1);
	unsigned long Address = 0;
	if ((ParseIpv4(Host, Address) && IsTailscaleIpv4(Address)) || (Host.size() > 7 && Host.compare(Host.size() - 7, 7, ".ts.net") == 0))
		return std::string();
	for (size_t i = 0; i < Config.Addresses.size(); ++i)
		if (ParseIpv4(Config.Addresses[i], Address) && IsTailscaleIpv4(Address)) {
			char Port[16];
			std::snprintf(Port, sizeof(Port), ":%d", Config.Port);
			return "http://" + Config.Addresses[i] + Port + "/files/";
		}
	return std::string();
}

// PUT /inbox/<name>: the body as a file in the Inbox folder, as send_file does
HttpResponse InboxRoute(const HttpRequest &Request, ToolSet &Tools, const std::string &Encoded)
{
	std::string Name, Reply;
	if (!UrlDecode(Encoded, Name))
		return TextResponse(400, "That isn't a file's name.");
	if (!Tools.ReceiveFile(Name, Request.Body, Reply))
		return TextResponse(Request.Body.size() > MAX_INBOX_FILE ? 413 : 400, Reply);
	return TextResponse(200, Reply);
}

}	// namespace

HttpResponse HandleMcp(const HttpRequest &Request, const McpConfig &Config, ToolSet &Tools)
{
	const Json NoId;

	// From another computer: only when the user allows it, and only from a
	// home network (or this computer at one of its own addresses)
	unsigned long PeerAddress = 0;
	const bool Remote = !Request.Peer.empty() && !(ParseIpv4(Request.Peer, PeerAddress) && IsLoopbackIpv4(PeerAddress));
	if (Remote && !Config.Network)
		return Failure(403, NoId, ACCESS_DENIED, "FamiTracker only answers programs on its own computer. To let other computers on "
			"your network connect, tick \"Allow connections from other computers on my network\" in File > Connect Claude Code.");
	if (Remote && !MayConnect(Request.Peer, Config.Addresses))
		return Failure(403, NoId, ACCESS_DENIED, "FamiTracker only answers computers on a home network (or Tailscale), not " +
			ValidUtf8(Request.Peer) + ".");

	enum { ROUTE_MCP, ROUTE_CORE, ROUTE_FILES, ROUTE_INBOX, ROUTE_PHONE } Route;
	const std::string Path = Request.Path();
	std::string Rest;
	if (Path == "/mcp" || Path == "/mcp/")
		Route = ROUTE_MCP;
	else if (Path == "/phone" || Path.compare(0, 7, "/phone/") == 0)
		Route = ROUTE_PHONE;
	else if (Path == "/mcp/core" || Path == "/mcp/core/")
		Route = ROUTE_CORE;
	else if (Path.compare(0, 7, "/files/") == 0 && Path.size() > 7) {
		Route = ROUTE_FILES;
		Rest = Path.substr(7);
	}
	else if (Path.compare(0, 7, "/inbox/") == 0 && Path.size() > 7) {
		Route = ROUTE_INBOX;
		Rest = Path.substr(7);
	}
	else
		return TextResponse(404, "FamiTracker's MCP endpoint is /mcp (/mcp/core: a small set of the tools, for local models).");

	// Only to this computer: a web page can't reach it through a renamed host
	// (DNS rebinding) or from another site
	const std::string *pHost = Request.Header("Host");
	if (pHost != 0 && !IsAcceptedHost(*pHost, Config))
		return Failure(403, NoId, ACCESS_DENIED, Config.Network ? "FamiTracker only answers requests to this computer's addresses or "
			"name, 127.0.0.1 or localhost." : "FamiTracker only answers requests to 127.0.0.1 or localhost.");
	// The phone page is a web page: it takes its own requests (same origin) with its own code
	if (Route == ROUTE_PHONE)
		return Config.pPhone != 0 ? Config.pPhone->Handle(Request, Config.Token) :
			TextResponse(404, "This FamiTracker has no phone page.");
	const std::string *pOrigin = Request.Header("Origin");
	if (pOrigin != 0 && !IsLocalOrigin(*pOrigin))
		return Failure(403, NoId, ACCESS_DENIED, "FamiTracker doesn't answer requests from web pages.");

	if ((Route == ROUTE_MCP || Route == ROUTE_CORE) && Request.Method != "POST")
		return WrongMethod("POST", "FamiTracker's MCP endpoint takes POST requests.");
	if (Route == ROUTE_FILES && Request.Method != "GET" && Request.Method != "HEAD")
		return WrongMethod("GET, HEAD", "FamiTracker's /files/ give the files export_for_game saved, by GET.");
	if (Route == ROUTE_INBOX && Request.Method != "PUT" && Request.Method != "POST")
		return WrongMethod("PUT", "FamiTracker's /inbox/ takes a file by PUT (curl -T).");
	if (Config.Token.empty())
		return Failure(403, NoId, ACCESS_DENIED, "FamiTracker has no access token set, so it answers nothing.");
	std::string Code;
	if (Route == ROUTE_FILES && QueryParameter(Request.Target, "code", Code)) {
		// A link export_for_game gave, with its own code for a browser, which can't send the access code; the access code
		// itself never goes in an address (a browser keeps addresses, and may pass them on)
		if (SameText(Code, Config.Token))
			return TextResponse(403, "That's FamiTracker's access code, which never goes in an address: a browser keeps addresses, and "
				"may pass them on. The links export_for_game gives carry a code of their own, which opens only these files.");
		if (!SameText(Code, FilesCode(Config.Token)))
			return TextResponse(403, "This link's code doesn't work (any more): FamiTracker's access code has changed since the link "
				"was made. Export the song again for new links.");
		return FileRoute(Request, Tools, Rest);
	}
	if (!TokenMatches(Request.Header("Authorization"), Config.Token))
		return Failure(403, NoId, ACCESS_DENIED, "FamiTracker refused the request: the access token is missing or wrong. "
			"File > Connect Claude Code in FamiTracker shows the command that connects with the right one.");
	if (Route == ROUTE_FILES)
		return FileRoute(Request, Tools, Rest);
	if (Route == ROUTE_INBOX)
		return InboxRoute(Request, Tools, Rest);

	Endpoint E;
	E.Core = Route == ROUTE_CORE;
	E.Who.Core = E.Core;
	E.Who.Client = true;	// not FamiTracker's own windows: no File > Save As
	E.Who.Remote = Remote;	// open_song takes no path from it
	if (Remote) {
		// Links to what export_for_game saves, as this client reached FamiTracker, with their own code for a browser
		char Port[16];
		std::snprintf(Port, sizeof(Port), ":%d", Config.Port);
		if (pHost != 0 && !pHost->empty())
			E.Who.FilesUrl = "http://" + *pHost + (pHost->find(':') == std::string::npos ? Port : "") + "/files/";
		else if (!Config.Addresses.empty())
			E.Who.FilesUrl = "http://" + Config.Addresses[0] + Port + "/files/";
		E.Who.FilesCode = FilesCode(Config.Token);
		E.Who.AwayUrl = AwayFilesUrl(pHost, Config);
	}
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
	if (pVersion != 0) {
		E.Version = pVersion->IsString() ? pVersion->String() : std::string();
		return Modern(Request, Config, Tools, E, *pId, pMethod->String(), Params, *pVersion);
	}
	// The handshake's era: the version the client agreed in its header, sent from 2025-06-18 on
	const std::string *pAgreed = Request.Header("MCP-Protocol-Version");
	E.Version = pAgreed != 0 ? *pAgreed : std::string(FIRST_VERSION);
	return Legacy(Request, Config, Tools, E, *pId, pMethod->String(), Params);
}

std::string FilesCode(const std::string &Token)
{
	return HexText(HmacSha256(Token, FILES_CODE_PURPOSE)).substr(0, FILES_CODE_DIGITS);
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

std::string NetworkUrl(const std::string &Address, int Port, bool Core)
{
	char Text[96];
	std::snprintf(Text, sizeof(Text), ":%d/mcp%s", Port, Core ? "/core" : "");
	return "http://" + Address + Text;
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

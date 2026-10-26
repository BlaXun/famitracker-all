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

// Tests for the Ask panel's model (Assistant.h, AssistantSocket.h): the
// server's address and the privacy note, server-sent events and HTTP read in
// any pieces, streamed replies (text, thinking, calls in pieces, whole replies,
// errors), answers as the panel shows them, the calls in words and their kinds,
// the conversation kept in the context, the help without a model, and the
// exchange over a real socket with a scripted model: streamed, stopped, off,
// silent, refused. The loop with FamiTracker's tools is in ApiTests.cpp.

#include "../Assistant.h"
#include "../AssistantSocket.h"
#include "../Exercises.h"
#include "../Glossary.h"
#include "../Tools.h"
#include "FakeModel.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using namespace ftapi;

namespace {

int g_Checks = 0, g_Failures = 0;

void Check(bool Condition, const std::string &What, int Line)
{
	++g_Checks;
	if (!Condition) {
		++g_Failures;
		std::printf("FAIL line %d: %s\n", Line, What.c_str());
	}
}

#define CHECK(x) Check((x), #x, __LINE__)
#define CHECK_EQ(a, b) Check((a) == (b), std::string(#a " == " #b), __LINE__)

bool Contains(const std::string &Text, const std::string &Part)
{
	return Text.find(Part) != std::string::npos;
}

#define CHECK_HAS(text, part) Check(Contains((text), (part)), std::string("\"") + (part) + "\" in " #text ": " + (text), __LINE__)

long long NowMs()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// ---- The server's address

void TestEndpoints()
{
	AssistantEndpoint E;
	std::string Error;
	CHECK(ParseAssistantEndpoint("http://192.168.1.50:8081", E, Error));
	CHECK_EQ(E.Host, std::string("192.168.1.50"));
	CHECK_EQ(E.Port, 8081);
	CHECK_EQ(E.ChatPath(), std::string("/v1/chat/completions"));
	CHECK_EQ(E.ModelsPath(), std::string("/v1/models"));
	CHECK_EQ(E.Root, std::string());
	CHECK_EQ(E.HostHeader(), std::string("192.168.1.50:8081"));
	CHECK_EQ(E.Text(), std::string("http://192.168.1.50:8081"));
	CHECK(ParseAssistantEndpoint("strix:8080/v1/", E, Error));
	CHECK(E.Host == "strix" && E.Port == 8080 && E.Base == "/v1");
	CHECK(ParseAssistantEndpoint("  HTTP://strix.tail1234.ts.net:8080/v1/chat/completions  ", E, Error));
	CHECK(E.Host == "strix.tail1234.ts.net" && E.Base == "/v1" && E.Root.empty());
	CHECK(ParseAssistantEndpoint("http://localhost", E, Error));
	CHECK(E.Port == 80 && E.HostHeader() == "localhost");
	CHECK(ParseAssistantEndpoint("http://box:9000/llm/", E, Error));
	CHECK(E.Base == "/llm/v1" && E.Root == "/llm");
	CHECK(!ParseAssistantEndpoint("https://box:8080", E, Error));
	CHECK_HAS(Error, "https");
	const char *const BAD[] = { "", "   ", "http://box:0", "http://box:70000", "http://box:80a", "http://:8080", "ftp://box",
		"http://[::1]:8080", "http://user@box:8080", "http://bo x:8080" };
	for (size_t i = 0; i < sizeof(BAD) / sizeof(BAD[0]); ++i) {
		Error.clear();
		Check(!ParseAssistantEndpoint(BAD[i], E, Error) && !Error.empty(), std::string("refused: ") + BAD[i], __LINE__);
	}

	// The note when the questions leave the house
	const std::vector<std::string> Own(1, "192.168.1.10");
	CHECK(ParseAssistantEndpoint("http://192.168.1.50:8081", E, Error));
	CHECK(AssistantPrivacyNote(E, std::string(), Own).empty());
	CHECK(ParseAssistantEndpoint("http://203.0.113.9:8080", E, Error));
	CHECK_HAS(AssistantPrivacyNote(E, std::string(), Own), "203.0.113.9");
	CHECK_HAS(AssistantPrivacyNote(E, std::string(), Own), "isn't this PC or on your home network");
	CHECK(ParseAssistantEndpoint("http://strix:8080", E, Error));
	CHECK(AssistantPrivacyNote(E, "100.101.102.103", Own).empty());		// Tailscale
	CHECK(AssistantPrivacyNote(E, "127.0.0.1", Own).empty());
	CHECK(AssistantPrivacyNote(E, std::string(), Own).empty());			// not known: no note
	CHECK_HAS(AssistantPrivacyNote(E, "198.51.100.7", Own), "strix (198.51.100.7)");
	CHECK(ParseAssistantEndpoint("http://localhost:8080", E, Error));
	CHECK(AssistantPrivacyNote(E, "198.51.100.7", Own).empty());

	AssistantSettings S;
	CHECK(S.AskRounds == ASSISTANT_ASK_ROUNDS && S.WorkRounds == ASSISTANT_WORK_ROUNDS && !S.SmallSet && S.ContextTokens == 0);
	S.AskRounds = 0;
	S.WorkRounds = 99999;
	S.ContextTokens = 100;
	S.Endpoint = "  http://a:1  ";
	const AssistantSettings Sane = SaneAssistantSettings(S);
	CHECK(Sane.AskRounds == 1 && Sane.WorkRounds == ASSISTANT_MOST_ROUNDS && Sane.ContextTokens == ASSISTANT_LEAST_CONTEXT &&
		Sane.Endpoint == "http://a:1");
}

// ---- Events and HTTP in any pieces

void TestSse()
{
	const std::string Stream = ": a comment\r\ndata: {\"a\":1}\r\n\r\ndata: one\ndata: two\n\nevent: x\ndata:three\r\r"
		"error: {\"message\":\"bad\"}\n\nid: 5\n\ndata: [DONE]\n\n";
	std::vector<std::string> Want;
	Want.push_back("{\"a\":1}");
	Want.push_back("one\ntwo");
	Want.push_back("three");
	Want.push_back("{\"error\":{\"message\":\"bad\"}}");
	Want.push_back("[DONE]");
	for (size_t Cut = 0; Cut <= Stream.size(); ++Cut) {
		SseReader Reader;
		std::vector<std::string> Got;
		Reader.Feed(Stream.substr(0, Cut), Got);
		Reader.Feed(Stream.substr(Cut), Got);
		Reader.End(Got);
		Check(Got == Want, "events cut at " + std::to_string(Cut), __LINE__);
	}
	SseReader Bytes;
	std::vector<std::string> Got;
	for (size_t i = 0; i < Stream.size(); ++i)
		Bytes.Feed(Stream.substr(i, 1), Got);
	CHECK(Got == Want);
	// An event the stream ended without its blank line
	SseReader Unended;
	Got.clear();
	Unended.Feed("data: last", Got);
	CHECK(Got.empty());
	Unended.End(Got);
	CHECK(Got.size() == 1 && Got[0] == "last");
}

void TestHttpStream()
{
	const std::string Chunked = "HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n"
		"Transfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n7;x=y\r\n, world\r\n0\r\nTrailer: z\r\n\r\n";
	for (size_t Cut = 0; Cut <= Chunked.size(); ++Cut) {
		HttpStreamReader Reader;
		std::string Body;
		Reader.Feed(Chunked.data(), Cut, Body);
		const HttpStreamReader::state_t State = Reader.Feed(Chunked.data() + Cut, Chunked.size() - Cut, Body);
		Check(State == HttpStreamReader::DONE && Body == "hello, world" && Reader.Status() == 200 &&
			Reader.ContentType() == "text/event-stream", "chunked cut at " + std::to_string(Cut), __LINE__);
	}
	HttpStreamReader Bytes;
	std::string Body;
	for (size_t i = 0; i < Chunked.size(); ++i)
		Bytes.Feed(Chunked.data() + i, 1, Body);
	CHECK(Bytes.State() == HttpStreamReader::DONE && Body == "hello, world");

	HttpStreamReader Sized;
	Body.clear();
	const std::string Length = "HTTP/1.1 400 Bad Request\r\nContent-Length: 5\r\ncontent-type: application/json\r\n\r\n{\"a\"}more";
	CHECK_EQ(Sized.Feed(Length.data(), Length.size(), Body), HttpStreamReader::DONE);
	CHECK(Body == "{\"a\"}" && Sized.Status() == 400 && Sized.ContentType() == "application/json");

	HttpStreamReader Closing;
	Body.clear();
	const std::string Unsized = "HTTP/1.0 200 OK\r\n\r\nabc";
	CHECK_EQ(Closing.Feed(Unsized.data(), Unsized.size(), Body), HttpStreamReader::BODY);
	CHECK_EQ(Closing.Close(Body), HttpStreamReader::DONE);
	CHECK_EQ(Body, std::string("abc"));

	HttpStreamReader Short;
	Body.clear();
	const std::string Cut = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhel";
	CHECK_EQ(Short.Feed(Cut.data(), Cut.size(), Body), HttpStreamReader::BODY);
	CHECK_EQ(Body, std::string("hel"));
	CHECK_EQ(Short.Close(Body), HttpStreamReader::BAD);

	const char *const BAD[] = { "nonsense\r\n\r\n", "HTTP/1.1 2x0 OK\r\n\r\n", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nzz\r\n",
		"HTTP/1.1 200 OK\r\nContent-Length: -4\r\n\r\n" };
	for (size_t i = 0; i < sizeof(BAD) / sizeof(BAD[0]); ++i) {
		HttpStreamReader Reader;
		std::string Ignored;
		Check(Reader.Feed(BAD[i], std::strlen(BAD[i]), Ignored) == HttpStreamReader::BAD, BAD[i], __LINE__);
	}
	HttpStreamReader Nothing;
	Body.clear();
	CHECK_EQ(Nothing.Close(Body), HttpStreamReader::BAD);
}

// ---- Streamed replies

std::string Events(const char *const *pEvents, size_t Count)
{
	std::string Out;
	for (size_t i = 0; i < Count; ++i)
		Out += std::string("data: ") + pEvents[i] + "\n\n";
	return Out;
}

void TestStream()
{
	// As llama-server streams with --jinja: thinking, text, two calls (the first's arguments in pieces), the count
	const char *const LLAMA[] = {
		"{\"choices\":[{\"index\":0,\"delta\":{\"role\":\"assistant\",\"content\":null}}]}",
		"{\"choices\":[{\"index\":0,\"delta\":{\"reasoning_content\":\"Let me look\"}}]}",
		"{\"choices\":[{\"index\":0,\"delta\":{\"content\":\"Hi! A [[chord]]\"}}]}",
		"{\"choices\":[{\"index\":0,\"delta\":{\"content\":\" is notes together.\"}}]}",
		"{\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":\"abc\",\"type\":\"function\",\"function\":"
			"{\"name\":\"get_song\",\"arguments\":\"\"}}]}}]}",
		"{\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":[{\"index\":0,\"function\":{\"arguments\":\"{\\\"melody_\"}}]}}]}",
		"{\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":[{\"index\":0,\"function\":{\"arguments\":\"channel\\\": 1}\"}}]}}]}",
		"{\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":[{\"index\":1,\"id\":\"def\",\"type\":\"function\",\"function\":"
			"{\"name\":\"check_song\",\"arguments\":\"{}\"}}]}}]}",
		"{\"choices\":[{\"index\":0,\"delta\":{},\"finish_reason\":\"tool_calls\"}],\"usage\":{\"prompt_tokens\":1200,"
			"\"completion_tokens\":40}}",
		"[DONE]" };
	const std::string Stream = Events(LLAMA, sizeof(LLAMA) / sizeof(LLAMA[0]));
	for (size_t Cut = 0; Cut <= Stream.size(); Cut += 3) {
		AssistantStream S;
		S.Start(200, "text/event-stream; charset=utf-8");
		std::string Text, Thinking;
		S.Feed(Stream.substr(0, Cut), Text, Thinking);
		S.Feed(Stream.substr(Cut), Text, Thinking);
		S.End();
		const AssistantReply &R = S.Reply();
		const bool Right = R.Content == "Hi! A [[chord]] is notes together." && Text == R.Content && Thinking == "Let me look" &&
			R.Reasoning == "Let me look" && R.Calls.size() == 2 && R.Calls[0].Id == "abc" && R.Calls[0].Name == "get_song" &&
			R.Calls[0].Arguments == "{\"melody_channel\": 1}" && R.Calls[1].Id == "def" && R.Calls[1].Name == "check_song" &&
			R.Calls[1].Arguments == "{}" && R.Finish == "tool_calls" && R.PromptTokens == 1200 && R.ReplyTokens == 40 && R.Ended &&
			R.Error.empty() && R.Status == 200;
		Check(Right, "llama.cpp's stream cut at " + std::to_string(Cut), __LINE__);
	}

	// Thinking inside the text, its tags cut between pieces
	{
		AssistantStream S;
		S.Start(200, "text/event-stream");
		std::string Text, Thinking;
		const char *const PIECES[] = { "<thi", "nk>ponder", "ing</th", "ink>\n\nThe ans", "wer <b>", "</b>" };
		for (size_t i = 0; i < sizeof(PIECES) / sizeof(PIECES[0]); ++i) {
			const std::string Event = "{\"choices\":[{\"index\":0,\"delta\":{\"content\":" + WriteJson(Json(PIECES[i])) + "}}]}";
			S.Feed("data: " + Event + "\n\n", Text, Thinking);
		}
		S.Feed("data: [DONE]\n\n", Text, Thinking);
		S.End();
		CHECK_EQ(S.Reply().Content, std::string("The answer <b></b>"));
		CHECK_EQ(S.Reply().Reasoning, std::string("pondering"));
		CHECK_EQ(Thinking, std::string("pondering"));
		CHECK_EQ(Text, std::string("The answer <b></b>"));
	}

	// Ollama's way: whole calls without an index, each with an id of its own, and "reasoning"
	{
		const char *const OLLAMA[] = {
			"{\"choices\":[{\"index\":0,\"delta\":{\"role\":\"assistant\",\"content\":\"\",\"reasoning\":\"hm\",\"tool_calls\":"
				"[{\"id\":\"call_a\",\"type\":\"function\",\"function\":{\"name\":\"get_song\",\"arguments\":\"{}\"}}]}}]}",
			"{\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":[{\"id\":\"call_b\",\"type\":\"function\",\"function\":"
				"{\"name\":\"what_next\",\"arguments\":\"{\\\"kind\\\":\\\"calm\\\"}\"}}]}}]}",
			"{\"choices\":[{\"index\":0,\"delta\":{},\"finish_reason\":\"tool_calls\"}]}", "[DONE]" };
		AssistantStream S;
		S.Start(200, "text/event-stream");
		std::string Text, Thinking;
		S.Feed(Events(OLLAMA, 4), Text, Thinking);
		S.End();
		const AssistantReply &R = S.Reply();
		CHECK(R.Calls.size() == 2 && R.Calls[0].Name == "get_song" && R.Calls[1].Name == "what_next" &&
			R.Calls[1].Arguments == "{\"kind\":\"calm\"}" && R.Reasoning == "hm" && R.Ended);
	}

	// A whole reply, when the server didn't stream: arguments as an object too
	{
		AssistantStream S;
		S.Start(200, "application/json");
		std::string Text, Thinking;
		S.Feed("{\"choices\":[{\"index\":0,\"message\":{\"role\":\"assistant\",\"content\":\"OK\",\"tool_calls\":[{\"id\":\"x\","
			"\"type\":\"function\",\"function\":{\"name\":\"get_song\",\"arguments\":{\"melody_channel\":2}}}]},\"finish_reason\":"
			"\"tool_calls\"}],\"usage\":{\"prompt_tokens\":10}}", Text, Thinking);
		S.End();
		const AssistantReply &R = S.Reply();
		CHECK(R.Content == "OK" && R.Calls.size() == 1 && R.Calls[0].Arguments == "{\"melody_channel\":2}" && R.Ended &&
			R.PromptTokens == 10 && R.Finish == "tool_calls");
	}

	// Errors: a refusal as llama-server gives it without --jinja, one inside a stream, and a page that isn't JSON
	{
		AssistantStream S;
		S.Start(400, "application/json; charset=utf-8");
		std::string Text, Thinking;
		S.Feed("{\"error\":{\"code\":400,\"message\":\"tools param requires --jinja flag\",\"type\":\"invalid_request_error\"}}", Text,
			Thinking);
		S.End();
		CHECK_EQ(S.Reply().Error, std::string("tools param requires --jinja flag"));
		CHECK(AssistantToolsRefused(S.Reply().Status, S.Reply().Error));
	}
	{
		AssistantStream S;
		S.Start(200, "text/event-stream");
		std::string Text, Thinking;
		S.Feed("data: {\"choices\":[{\"index\":0,\"delta\":{\"content\":\"Par\"}}]}\n\nerror: {\"code\":500,\"message\":"
			"\"the context is full\"}\n\n", Text, Thinking);
		S.End();
		CHECK(S.Reply().Content == "Par" && S.Reply().Error == "the context is full");
	}
	{
		AssistantStream S;
		S.Start(502, "text/html");
		std::string Text, Thinking;
		S.Feed("<html>\n<h1>Bad gateway</h1></html>", Text, Thinking);
		S.End();
		CHECK_HAS(S.Reply().Error, "Bad gateway");
	}
}

// ---- The server's other answers

void TestServerAnswers()
{
	CHECK_EQ(AssistantContextOfProps("{\"default_generation_settings\":{\"n_ctx\":65536,\"params\":{}},\"total_slots\":1}"), 65536);
	CHECK_EQ(AssistantContextOfProps("{\"n_ctx\":4096}"), 4096);
	CHECK_EQ(AssistantContextOfProps("{\"build_info\":\"b6000\"}"), 0);
	CHECK_EQ(AssistantContextOfProps("not json"), 0);
	const std::vector<std::string> Models = AssistantModelsOf("{\"object\":\"list\",\"data\":[{\"id\":\"qwen3.6-35b-a3b\"},"
		"{\"id\":\"gpt-oss-120b\"},{\"id\":\"qwen3.6-35b-a3b\"},{\"name\":\"no id\"}]}");
	CHECK(Models.size() == 2 && Models[0] == "qwen3.6-35b-a3b" && Models[1] == "gpt-oss-120b");
	CHECK_EQ(AssistantErrorMessage("{\"error\":\"model not loaded\"}"), std::string("model not loaded"));
	CHECK_EQ(AssistantErrorMessage("{\"detail\":\"Not Found\"}"), std::string("Not Found"));
	CHECK_EQ(AssistantErrorMessage("\n  Plain words\nmore"), std::string("Plain words"));
	CHECK(AssistantToolsRefused(400, "registry.ollama.ai/library/gemma:2b does not support tools"));
	CHECK(AssistantToolsRefused(500, "tools param requires --jinja flag"));
	CHECK(!AssistantToolsRefused(200, "tools param requires --jinja flag"));
	CHECK(!AssistantToolsRefused(400, "the request exceeds the available context size"));

	AssistantEndpoint E;
	std::string Error;
	ParseAssistantEndpoint("http://strix:8080", E, Error);
	CHECK_HAS(AssistantHttpProblem(E, std::string(), 401, "Invalid API Key"), "key");
	CHECK_HAS(AssistantHttpProblem(E, std::string(), 404, "File Not Found"), "no chat at http://strix:8080/v1/chat/completions");
	CHECK_HAS(AssistantHttpProblem(E, "qwen9", 400, "could not find suitable inference handler for qwen9"), "no model called \"qwen9\"");
	CHECK_HAS(AssistantHttpProblem(E, std::string(), 400, "the request exceeds the available context size, try increasing it"),
		"bigger context");
	CHECK_HAS(AssistantHttpProblem(E, std::string(), 503, "Loading model"), "log");

	// The requests' bytes
	const std::string Get = AssistantHttpRequest(E, "/props", std::string(), std::string(), false);
	CHECK(Contains(Get, "GET /props HTTP/1.1\r\nHost: strix:8080\r\n") && Contains(Get, "Connection: close\r\n") &&
		!Contains(Get, "Authorization") && Get.size() >= 4 && Get.compare(Get.size() - 4, 4, "\r\n\r\n") == 0);
	const std::string Post = AssistantHttpRequest(E, E.ChatPath(), "sec\r\nret", "{\"a\":1}", true);
	CHECK(Contains(Post, "POST /v1/chat/completions HTTP/1.1\r\n") && Contains(Post, "Authorization: Bearer secret\r\n") &&
		Contains(Post, "Accept: text/event-stream\r\n") && Contains(Post, "Content-Length: 7\r\n\r\n{\"a\":1}"));
	Json Probe;
	CHECK(ParseJson(AssistantProbeBody("m"), Probe));
	CHECK(Probe.Get("model") != 0 && Probe.Get("model")->String() == "m" && Probe.Get("tools") != 0 && Probe.Get("tools")->Size() == 1 &&
		Probe.Get("stream") != 0 && !Probe.Get("stream")->Bool());
	CHECK(ParseJson(AssistantProbeBody(std::string()), Probe) && Probe.Get("model") == 0);

	// Failures in words, with what to check
	AssistantTimeouts Times;
	CHECK_HAS(AssistantNetProblem(NET_REFUSED, E, Times, std::string()), "Nothing answers at strix:8080");
	CHECK_HAS(AssistantNetProblem(NET_REFUSED, E, Times, std::string()), "--host 0.0.0.0");
	CHECK_HAS(AssistantNetProblem(NET_NOT_FOUND, E, Times, std::string()), "No computer called \"strix\"");
	CHECK_HAS(AssistantNetProblem(NET_NO_ANSWER, E, Times, std::string()), "firewall");
	CHECK_HAS(AssistantNetProblem(NET_SILENT, E, Times, std::string()), "5 minutes");
	CHECK(AssistantNetProblem(NET_STOPPED, E, Times, std::string()).empty());
}

// ---- Answers as the panel shows them

void TestRichText()
{
	const GlossaryEntry *pChord = FindGlossaryEntry("chord");
	CHECK(pChord != 0);
	const std::vector<AssistantParagraph> P = AssistantRichText(
		"### Chords\nA **[[chord]]** is notes together.\n\n- Try [[the home note|home-note]] first\n2. Then "
		"[[Exercises|exercise:home-note]], [[a tour|tutorial:Your first tune]] or the [[settings|settings]]\n```\ncode **here**\n"
		"```\n[[no such thing]] and [the docs](http://x.y) and *soft* `code`, 5 * 3\r\n* starred");
	CHECK_EQ(P.size(), static_cast<size_t>(7));
	if (P.size() < 7)
		return;
	CHECK(P[0].Heading && P[0].Runs.size() == 1 && P[0].Runs[0].Text == "Chords" && P[0].Runs[0].Bold);
	CHECK(P[1].Runs.size() == 3 && P[1].Runs[0].Text == "A " && !P[1].Runs[0].Bold && P[1].Runs[1].Text == "chord" &&
		P[1].Runs[1].Bold && pChord != 0 && P[1].Runs[1].Link == pChord->Id && P[1].Runs[2].Text == " is notes together." &&
		!P[1].Runs[2].Bold && !P[1].Gap);
	CHECK(P[2].Gap && P[2].Bullet == "-" && P[2].Runs.size() == 3 && P[2].Runs[1].Text == "the home note" &&
		P[2].Runs[1].Link == "home-note");
	CHECK(P[3].Bullet == "2." && P[3].Runs.size() >= 6 && P[3].Runs[1].Link == "exercise:home-note" &&
		P[3].Runs[3].Link == "tutorial:Your first tune" && P[3].Runs[5].Link == "settings");
	CHECK(P[4].Code && P[4].Runs.size() == 1 && P[4].Runs[0].Text == "code **here**");
	CHECK(P[5].Runs.size() == 1 && P[5].Runs[0].Link.empty() &&
		P[5].Runs[0].Text == "no such thing and the docs (http://x.y) and soft code, 5 * 3");
	CHECK(P[6].Bullet == "-" && P[6].Runs.size() == 1 && P[6].Runs[0].Text == "starred");
	// An exercise that isn't one links nowhere
	const std::vector<AssistantParagraph> Q = AssistantRichText("[[x|exercise:no-such]] [[y|tutorial:]]");
	CHECK(Q.size() == 1 && Q[0].Runs.size() == 1 && Q[0].Runs[0].Link.empty());
	CHECK_EQ(AssistantPlainText("Hello **you**\n\n- a [[chord]]\n- b"), std::string("Hello you\n\n- a chord\n- b"));
	CHECK(AssistantRichText(std::string()).empty());
	CHECK(AssistantRichText("\n\n\n").empty());
}

// ---- Calls in words, and their kinds

void TestCallWords()
{
	Json Args;
	CHECK(ParseJson("{\"channel\":\"Pulse 1\",\"from_bar\":1,\"to_bar\":8,\"notes\":\"D4 G4 A4 B4 - A4 G4 - | B4 - - - . . D5 - | "
		"G4\",\"pan\":\"left\",\"format\":\"text\",\"as_clip\":false}", Args));
	CHECK_EQ(AssistantCallWords("Write a part", Args), std::string("Write a part: Pulse 1, bars 1-8, notes, pan left"));
	CHECK(ParseJson("{\"mood\":\"calm\",\"from_bar\":5,\"to_bar\":5,\"channel\":3,\"loop\":true}", Args));
	CHECK_EQ(AssistantCallWords("Change the mood", Args), std::string("Change the mood: mood calm, bar 5, channel 3, loop"));
	CHECK(ParseJson("{\"to_bar\":12}", Args));
	CHECK_EQ(AssistantCallWords("Add drums", Args), std::string("Add drums: to bar 12"));
	CHECK_EQ(AssistantCallWords("Song overview", Json::MakeObject()), std::string("Song overview"));
	CHECK_EQ(AssistantCallText("write_notes", "{\"channel\": \"Pulse 1\"}"), std::string("write_notes {\"channel\":\"Pulse 1\"}"));
	CHECK_EQ(AssistantCallText("x", "broken {"), std::string("x broken {"));
	CHECK(AssistantCallText("write_notes", "{\"notes\":\"" + std::string(400, 'a') + "\"}").size() <= 160);
	CHECK_EQ(AssistantResultLine("\n\n  Wrote 32 notes on Pulse 1.  \nMore"), std::string("Wrote 32 notes on Pulse 1."));
	CHECK(AssistantResultLine(std::string(500, 'x') + " y").size() <= 150);

	// Reading or changing by the arguments
	Json Empty = Json::MakeObject(), Pick, Add, List, Tag, NoSave;
	ParseJson("{\"pick\":2}", Pick);
	ParseJson("{\"action\":\"add\",\"text\":\"likes echo\"}", Add);
	ParseJson("{\"action\":\"list\"}", List);
	ParseJson("{\"action\":\"tag\"}", Tag);
	ParseJson("{\"save\":false}", NoSave);
	CHECK(!AssistantCallChanges("grow_melody", Empty) && AssistantCallChanges("grow_melody", Pick));
	CHECK(!AssistantCallChanges("soundtrack_notes", Empty) && AssistantCallChanges("soundtrack_notes", Add));
	CHECK(!AssistantCallChanges("references", List) && !AssistantCallChanges("references", Empty) &&
		AssistantCallChanges("references", Tag));
	CHECK(AssistantCallChanges("study_nsf", Empty) && !AssistantCallChanges("study_nsf", NoSave));
	CHECK(!AssistantCallChanges("get_song", Empty) && !AssistantCallChanges("play", Empty) &&
		AssistantCallChanges("write_notes", Empty) && AssistantCallChanges("undo", Empty) && AssistantCallChanges("no_such_tool", Empty));
	CHECK(AssistantLeavesOut("look_at_song") && AssistantLeavesOut("send_file") && !AssistantLeavesOut("get_song"));
}

// ---- The conversation, kept in the context

class FixedTools : public AssistantTools {
public:
	FixedTools() : Calls(0), Fail(false) {}
	bool RunTool(const std::string &Name, const std::string &Arguments, bool Changes, std::string &Reply)
	{
		++Calls;
		Names.push_back(Name + (Changes ? "!" : ""));
		(void)Arguments;
		Reply = Name == "get_song" ? std::string(Size, 's') : "Done: " + Name + ".";
		return !Fail;
	}
	int Calls;
	bool Fail;
	size_t Size = 20000;
	std::vector<std::string> Names;
};

std::vector<ToolInfo> TwoTools()
{
	std::vector<ToolInfo> Tools(3);
	Tools[0].Name = "get_song";
	Tools[0].Title = "Song overview";
	Tools[0].Description = "Overview.";
	Tools[0].InputSchema = Json::MakeObject();
	Tools[0].ReadOnly = true;
	Tools[1].Name = "write_notes";
	Tools[1].Title = "Write a part";
	Tools[1].Description = "Writes notes.";
	Tools[1].InputSchema = Json::MakeObject();
	Tools[1].ReadOnly = false;
	Tools[2].Name = "look_at_song";
	Tools[2].Title = "Look at the song";
	Tools[2].InputSchema = Json::MakeObject();
	Tools[2].ReadOnly = true;
	return Tools;
}

AssistantReply CallReply(const std::string &Name, const std::string &Arguments, const std::string &Id = std::string())
{
	AssistantReply R;
	AssistantCall C;
	C.Id = Id;
	C.Name = Name;
	C.Arguments = Arguments;
	R.Calls.push_back(C);
	R.Finish = "tool_calls";
	R.Ended = true;
	R.Status = 200;
	return R;
}

AssistantReply TextReply(const std::string &Text)
{
	AssistantReply R;
	R.Content = Text;
	R.Finish = "stop";
	R.Ended = true;
	R.Status = 200;
	return R;
}

void TestChat()
{
	const std::vector<ToolInfo> Tools = TwoTools();
	const Json Functions = AssistantFunctions(Tools, false);
	CHECK_EQ(Functions.Size(), static_cast<size_t>(2));		// look_at_song left out
	AssistantSetup Setup;
	Setup.SystemPrompt = "You help.";
	Setup.Functions = Functions;
	Setup.Model = "m";
	Setup.ContextTokens = 32768;
	AssistantChat Chat;
	Chat.SetTools(Tools, false);
	FixedTools Run;
	std::string Body, Problem;
	CHECK(!Chat.Request(Setup, Body, Problem));		// nothing asked yet
	Chat.Ask("  What now?  ", "CONTEXT 1", ASSISTANT_ASK_FIRST, 8);
	CHECK(Chat.State() == AssistantChat::MODEL && Chat.Questions() == 1);
	CHECK(Chat.Request(Setup, Body, Problem));
	Json Sent;
	CHECK(ParseJson(Body, Sent));
	const Json *pMessages = Sent.Get("messages");
	CHECK(pMessages != 0 && pMessages->Size() == 2 && pMessages->At(0).Get("role")->String() == "system" &&
		pMessages->At(0).Get("content")->String() == "You help." && pMessages->At(1).Get("content")->String() ==
		"CONTEXT 1\n\nThe question: What now?");
	CHECK(Sent.Get("tools") != 0 && Sent.Get("tools")->Size() == 2 && Sent.Get("stream")->Bool() && Sent.Get("model")->String() == "m");

	// A read runs at once; a change waits for the user; a broken call and a tool not given fail with the reason
	AssistantReply Reply = CallReply("get_song", "");
	AssistantCall Change;
	Change.Id = "w1";
	Change.Name = "write_notes";
	Change.Arguments = "{\"channel\":\"Pulse 1\",\"from_bar\":1,\"to_bar\":4}";
	Reply.Calls.push_back(Change);
	AssistantCall Broken;
	Broken.Name = "write_notes";
	Broken.Arguments = "{\"channel\": ";
	Reply.Calls.push_back(Broken);
	AssistantCall Missing;
	Missing.Name = "look_at_song";
	Reply.Calls.push_back(Missing);
	Reply.PromptTokens = static_cast<int>(Body.size() / 4);
	Chat.Replied(Reply, false);
	CHECK_EQ(Chat.BytesPerToken(), 4.0);
	CHECK(!Chat.Request(Setup, Body, Problem));		// RunReady first
	Chat.RunReady(Run);
	CHECK(Chat.State() == AssistantChat::USER);
	CHECK(Run.Calls == 1 && Run.Names[0] == "get_song");
	const AssistantMessage *pAnswer = Chat.Answer();
	CHECK(pAnswer != 0 && pAnswer->Calls.size() == 4);
	if (pAnswer == 0 || pAnswer->Calls.size() != 4)
		return;
	CHECK(pAnswer->Calls[0].State == CALL_DONE && pAnswer->Calls[0].Id == "ft0000001" && pAnswer->Calls[0].Arguments == "{}" &&
		!pAnswer->Calls[0].Changes && pAnswer->Calls[0].Words == "Song overview");
	CHECK(pAnswer->Calls[1].State == CALL_PROPOSED && pAnswer->Calls[1].Changes && pAnswer->Calls[1].Words ==
		"Write a part: Pulse 1, bars 1-4");
	CHECK(pAnswer->Calls[2].State == CALL_FAILED && Contains(pAnswer->Calls[2].Result, "weren't a JSON object") &&
		pAnswer->Calls[2].Arguments == "{}");
	CHECK(pAnswer->Calls[3].State == CALL_FAILED && Contains(pAnswer->Calls[3].Result, "no tool called \"look_at_song\""));
	CHECK(!Chat.Request(Setup, Body, Problem));		// the user's turn
	const size_t Message = Chat.Messages().size() - 1;
	CHECK(!Chat.Decide(Message, 0, true, Run));		// not a proposal
	CHECK(Chat.Decide(Message, 1, true, Run));
	CHECK(Run.Calls == 2 && Run.Names[1] == "write_notes!");
	CHECK(Chat.State() == AssistantChat::MODEL);
	CHECK(Chat.Request(Setup, Body, Problem));
	CHECK(ParseJson(Body, Sent));
	pMessages = Sent.Get("messages");
	// system, user, assistant with its four calls, and the four results
	CHECK(pMessages != 0 && pMessages->Size() == 7);
	if (pMessages != 0 && pMessages->Size() == 7) {
		const Json &Assistant = pMessages->At(2);
		CHECK(Assistant.Get("role")->String() == "assistant" && Assistant.Get("tool_calls")->Size() == 4 &&
			Assistant.Get("tool_calls")->At(1).Get("id")->String() == "w1");
		CHECK(pMessages->At(4).Get("role")->String() == "tool" && pMessages->At(4).Get("tool_call_id")->String() == "w1" &&
			pMessages->At(4).Get("content")->String() == "Done: write_notes." && pMessages->At(4).Get("name")->String() ==
			"write_notes");
	}
	Chat.Replied(TextReply("Done! Press Play."), false);
	CHECK(Chat.State() == AssistantChat::IDLE);

	// Skipping: the question ends with the user's turn
	Chat.Ask("Add more?", "CONTEXT 2", ASSISTANT_ASK_FIRST, 8);
	CHECK(Chat.Request(Setup, Body, Problem));
	Chat.Replied(CallReply("write_notes", "{}", "w2"), false);
	Chat.RunReady(Run);
	CHECK(Chat.State() == AssistantChat::USER);
	CHECK(Chat.Decide(Chat.Messages().size() - 1, 0, false, Run));
	CHECK(Chat.State() == AssistantChat::IDLE && Run.Calls == 2);
	CHECK(Contains(Chat.Messages().back().Calls[0].Result, "chose Skip"));
	// A question asked while a proposal waits skips it
	Chat.Ask("And?", std::string(), ASSISTANT_ASK_FIRST, 8);
	CHECK(Chat.Request(Setup, Body, Problem));
	Chat.Replied(CallReply("write_notes", "{}", "w3"), false);
	Chat.RunReady(Run);
	Chat.Ask("Never mind", std::string(), ASSISTANT_ASK_FIRST, 8);
	CHECK(Chat.Messages()[Chat.Messages().size() - 2].Calls[0].State == CALL_SKIPPED);
	Chat.Stop();
	CHECK(Chat.State() == AssistantChat::IDLE);

	// Let it work: changes run at once, until the question's rounds are used
	Chat.Ask("Compose it", "C", ASSISTANT_LET_IT_WORK, 2);
	CHECK(Chat.Request(Setup, Body, Problem));
	Chat.Replied(CallReply("write_notes", "{}"), false);
	Chat.RunReady(Run);
	CHECK(Chat.State() == AssistantChat::MODEL && Run.Names.back() == "write_notes!");
	CHECK(Chat.Request(Setup, Body, Problem));
	Chat.Replied(CallReply("write_notes", "{}"), false);
	Chat.RunReady(Run);
	CHECK(Chat.State() == AssistantChat::IDLE && Chat.Rounds() == 2);
	CHECK_HAS(Chat.Messages().back().Text, "2 rounds of tools");
	CHECK_HAS(Chat.Messages().back().Text, "Let it work");

	// Stop: what was written stays, the calls go
	Chat.Ask("Again", "C", ASSISTANT_LET_IT_WORK, 9);
	CHECK(Chat.Request(Setup, Body, Problem));
	AssistantReply Partial = CallReply("write_notes", "{}");
	Partial.Content = "I'll wri";
	Partial.Ended = false;
	Chat.Replied(Partial, true);
	CHECK(Chat.State() == AssistantChat::IDLE && Chat.Messages().back().Text == "Stopped.");
	const AssistantMessage *pStopped = Chat.Answer();
	CHECK(pStopped != 0 && pStopped->Stopped && pStopped->Text == "I'll wri" && pStopped->Calls.empty());
	Chat.Replied(AssistantReply(), true, "The connection broke.");
	CHECK(Chat.Messages().back().Text == "The connection broke.");

	// Without tools: the calls made so far told as text, no tool messages
	Chat.ToolsOff();
	Chat.Ask("Plain?", "C", ASSISTANT_ASK_FIRST, 8);
	CHECK(Chat.Request(Setup, Body, Problem));
	CHECK(ParseJson(Body, Sent) && Sent.Get("tools") == 0);
	CHECK(!Contains(Body, "\"tool_calls\"") && !Contains(Body, "\"role\":\"tool\"") && Contains(Body, "(Write a part: Done: write_notes.)"));
	Chat.Clear();
	CHECK(Chat.Messages().empty() && Chat.State() == AssistantChat::IDLE && !Chat.HasToolsOff());
}

void TestFitting()
{
	const std::vector<ToolInfo> Tools = TwoTools();
	AssistantSetup Setup;
	Setup.SystemPrompt = "You help.";
	Setup.Functions = AssistantFunctions(Tools, false);
	Setup.ContextTokens = 12000;		// 9000 tokens of room, 27000 bytes at 3 a token
	AssistantChat Chat;
	Chat.SetTools(Tools, false);
	FixedTools Run;
	std::string Body, Problem;
	// Two questions with 20000 bytes of song each, then a third
	for (int q = 0; q < 2; ++q) {
		Chat.Ask("Question " + std::to_string(q + 1), std::string(3000, 'c'), ASSISTANT_ASK_FIRST, 8);
		CHECK(Chat.Request(Setup, Body, Problem));
		CHECK(Body.size() <= 27000);
		Chat.Replied(CallReply("get_song", "{}"), false);
		Chat.RunReady(Run);
		CHECK(Chat.Request(Setup, Body, Problem));
		CHECK(Body.size() <= 27000);
		Chat.Replied(TextReply("Answer " + std::to_string(q + 1)), false);
	}
	const std::vector<AssistantMessage> &M = Chat.Messages();
	// question, call, answer for each: the first question's song and its reply shortened, the second's whole
	CHECK_EQ(M.size(), static_cast<size_t>(6));
	if (M.size() != 6)
		return;
	CHECK(M[0].ContextLeftOut && M[1].Calls.size() == 1 && M[1].Calls[0].Shortened);
	CHECK(!M[3].ContextLeftOut && M[4].Calls.size() == 1 && !M[4].Calls[0].Shortened);
	Chat.Ask("Question 3", std::string(3000, 'c'), ASSISTANT_ASK_FIRST, 8);
	CHECK(Chat.Request(Setup, Body, Problem));
	CHECK(Body.size() <= 27000);
	CHECK(M[3].ContextLeftOut && !M[4].Calls[0].Shortened && M[4].Question == 2);
	// A reply as big as the whole room is cut to fit rather than failing
	Run.Size = 40000;
	Chat.Replied(CallReply("get_song", "{}"), false);
	Chat.RunReady(Run);
	CHECK(Chat.Request(Setup, Body, Problem));
	CHECK(Body.size() <= 27000 && Problem.empty());
	CHECK(Contains(Chat.Answer()->Calls[0].Result, "Cut here"));
	// Older questions left out altogether when that's what it takes
	Setup.ContextTokens = 4000;		// 3000 tokens, 9000 bytes
	Chat.Replied(TextReply("Answer 3"), false);
	Chat.Ask("Question 4", "c", ASSISTANT_ASK_FIRST, 8);
	CHECK(Chat.Request(Setup, Body, Problem));
	CHECK(Body.size() <= 9000);
	CHECK(Chat.Messages()[0].LeftOut);
	// Tools too big for the context: said so, with the small set and a bigger context to try
	Setup.SystemPrompt = std::string(30000, 'p');
	Chat.Ask("Question 5", "c", ASSISTANT_ASK_FIRST, 8);
	CHECK(!Chat.Request(Setup, Body, Problem));
	CHECK_HAS(Problem, "too small for FamiTracker's tools");
	CHECK_HAS(Problem, "small set");
	CHECK(Chat.State() == AssistantChat::IDLE && Chat.Messages().back().Text == Problem);
}

// ---- Help without a model

void TestHelp()
{
	std::vector<AssistantTutorial> Tutorials(1);
	Tutorials[0].Title = "Your first tune";
	Tutorials[0].Text = "Write a melody in the piano roll, then play it.";
	std::vector<AssistantHelpItem> Items = AssistantSearchHelp("How do I make a chord?", Tutorials);
	CHECK(!Items.empty() && Items[0].Kind == "glossary" && Items[0].Id == "chord");
	Items = AssistantSearchHelp("an exercise for the bass line", Tutorials);
	bool Bass = false;
	for (size_t i = 0; i < Items.size(); ++i)
		Bass = Bass || (Items[i].Kind == "exercise" && Items[i].Id == "bass");
	CHECK(Bass);
	Items = AssistantSearchHelp("melody piano roll", Tutorials);
	bool Tutorial = false;
	for (size_t i = 0; i < Items.size(); ++i)
		Tutorial = Tutorial || (Items[i].Kind == "tutorial" && Items[i].Id == "Your first tune");
	CHECK(Tutorial);
	CHECK(Items.size() <= 6);
	CHECK(AssistantSearchHelp("how do I do it?", Tutorials).empty());
	CHECK(!AssistantSearchHelp("what does 4xy do", Tutorials).empty());
	const std::string Answer = AssistantHelpAnswer(AssistantSearchHelp("an exercise for the bass line", Tutorials), false);
	CHECK_HAS(Answer, "No model is set up yet");
	bool Linked = false, Settings = false;
	const std::vector<AssistantParagraph> P = AssistantRichText(Answer);
	for (size_t p = 0; p < P.size(); ++p)
		for (size_t r = 0; r < P[p].Runs.size(); ++r) {
			Linked = Linked || P[p].Runs[r].Link == "exercise:bass";
			Settings = Settings || P[p].Runs[r].Link == "settings";
		}
	CHECK(Linked && Settings);
	CHECK_HAS(AssistantHelpAnswer(std::vector<AssistantHelpItem>(), true), "couldn't be reached");
}

// ---- Over a real socket, with a scripted model

class Heard : public AssistantStreamListener {
public:
	Heard() : Status(0), pStop(0) {}
	void Started(int S) { Status = S; }
	void Streamed(const std::string &NewText, const std::string &NewThinking)
	{
		Text += NewText;
		Thinking += NewThinking;
		if (pStop != 0 && !Text.empty())
			pStop->store(true);		// Stop as soon as the first words show
	}
	int Status;
	std::string Text, Thinking;
	std::atomic<bool> *pStop;
};

void TestExchange()
{
	fakemodel::Server Model;
	CHECK(Model.Start());
	AssistantEndpoint Where;
	std::string Error;
	CHECK(ParseAssistantEndpoint("http://127.0.0.1:" + std::to_string(Model.Port()), Where, Error));
	AssistantTimeouts Times;
	Times.FirstMs = 2000;
	Times.QuietMs = 2000;

	// Streamed in odd pieces with pauses between them
	std::vector<std::string> Parts;
	Parts.push_back("A [[chord]] ");
	Parts.push_back("is three notes ");
	Parts.push_back("or more.");
	fakemodel::Answer Streamed = fakemodel::Text(Parts, 5, 321);
	Streamed.PauseMs = 2;
	Model.Add(Streamed);
	AssistantReply Reply;
	Heard Listener;
	CHECK_EQ(AssistantChatExchange(Where, "key1", "{\"messages\":[]}", Times, 0, &Listener, Reply, Error), NET_DONE);
	CHECK(Reply.Ended && Reply.Content == "A [[chord]] is three notes or more." && Listener.Text == Reply.Content &&
		Listener.Status == 200 && Reply.PromptTokens == 321);
	const std::vector<HttpRequest> Requests = Model.Requests();
	CHECK(Requests.size() == 1 && Requests[0].Method == "POST" && Requests[0].Path() == "/v1/chat/completions" &&
		Requests[0].Header("Authorization") != 0 && *Requests[0].Header("Authorization") == "Bearer key1" &&
		Requests[0].Body == "{\"messages\":[]}");

	// A call streamed, its arguments in pieces, not chunked
	std::vector<std::string> Args;
	Args.push_back("{\"chan");
	Args.push_back("nel\": \"Pulse 1\"}");
	fakemodel::Answer Called = fakemodel::Call("read_notes", Args, "call_9", "Let me read it.");
	Called.Chunked = false;
	Model.Add(Called);
	CHECK_EQ(AssistantChatExchange(Where, std::string(), "{}", Times, 0, 0, Reply, Error), NET_DONE);
	CHECK(Reply.Calls.size() == 1 && Reply.Calls[0].Name == "read_notes" && Reply.Calls[0].Id == "call_9" &&
		Reply.Calls[0].Arguments == "{\"channel\": \"Pulse 1\"}" && Reply.Content == "Let me read it." && Reply.Finish == "tool_calls");

	// Stopped mid-answer: what came stays, and it ends at once
	fakemodel::Answer Long = fakemodel::Text(Parts, 9);
	Long.PauseMs = 30;
	Long.Hang = true;
	Model.Add(Long);
	std::atomic<bool> Stop(false);
	Heard Stopping;
	Stopping.pStop = &Stop;
	long long Start = NowMs();
	CHECK_EQ(AssistantChatExchange(Where, std::string(), "{}", Times, &Stop, &Stopping, Reply, Error), NET_STOPPED);
	CHECK(NowMs() - Start < 1500);
	CHECK(!Reply.Content.empty() && !Reply.Ended && Contains(std::string("A [[chord]] is three notes or more."), Reply.Content));

	// Silent: nothing comes (a model loading too long), then an answer that stops coming
	fakemodel::Answer Nothing;
	Nothing.Pieces.clear();
	Nothing.Hang = true;
	Model.Add(Nothing);
	Times.FirstMs = 300;
	Start = NowMs();
	CHECK_EQ(AssistantChatExchange(Where, std::string(), "{}", Times, 0, 0, Reply, Error), NET_SILENT);
	CHECK(NowMs() - Start >= 250 && NowMs() - Start < 1800);
	fakemodel::Answer Halting = fakemodel::Text(Parts, 40);		// the first event, half of it
	Halting.Pieces.resize(1);
	Halting.Hang = true;
	Model.Add(Halting);
	Times.QuietMs = 300;
	Error.clear();
	CHECK_EQ(AssistantChatExchange(Where, std::string(), "{}", Times, 0, 0, Reply, Error), NET_SILENT);
	CHECK_HAS(Error, "stopped coming");
	CHECK_HAS(AssistantNetProblem(NET_SILENT, Where, Times, Error), "stopped coming");
	Times.FirstMs = Times.QuietMs = 2000;

	// Refused: the reply is the server's error
	Model.Add(fakemodel::Error(400, "tools param requires --jinja flag"));
	CHECK_EQ(AssistantChatExchange(Where, std::string(), "{}", Times, 0, 0, Reply, Error), NET_DONE);
	CHECK(Reply.Status == 400 && Reply.Error == "tools param requires --jinja flag");

	// The server's other answers
	Model.SetProps("{\"default_generation_settings\":{\"n_ctx\":65536}}");
	Model.SetModels("{\"data\":[{\"id\":\"qwen\"}]}");
	AssistantAnswer Answer;
	CHECK_EQ(AssistantFetch(Where, "/props", std::string(), std::string(), Times, 0, Answer, Error), NET_DONE);
	CHECK(Answer.Status == 200 && AssistantContextOfProps(Answer.Body) == 65536);
	CHECK_EQ(AssistantFetch(Where, Where.ModelsPath(), std::string(), std::string(), Times, 0, Answer, Error), NET_DONE);
	CHECK(AssistantModelsOf(Answer.Body) == std::vector<std::string>(1, "qwen"));
	CHECK(Model.Requests().back().Method == "GET");

	// Nothing listens: said at once
	const int Port = Model.Port();
	Model.Stop();
	AssistantEndpoint Off = Where;
	Off.Port = Port;
	Start = NowMs();
	const assistant_net_t Gone = AssistantChatExchange(Off, std::string(), "{}", Times, 0, 0, Reply, Error);
	CHECK(Gone == NET_REFUSED || Gone == NET_NO_ANSWER);
	CHECK(NowMs() - Start < 9000);
	CHECK_HAS(AssistantNetProblem(NET_REFUSED, Off, Times, Error), "Nothing answers at 127.0.0.1");
	// Stop works while it connects too
	std::atomic<bool> Already(true);
	CHECK_EQ(AssistantChatExchange(Off, std::string(), "{}", Times, &Already, 0, Reply, Error), NET_STOPPED);
	std::string Address;
	CHECK(AssistantResolve("127.0.0.1", Address) && Address == "127.0.0.1");
	CHECK(AssistantResolve("localhost", Address) && Address.compare(0, 4, "127.") == 0);
}

// The driver's step after a reply, as the panel takes it
void TestTakeReply()
{
	const std::vector<ToolInfo> Tools = TwoTools();
	AssistantSetup Setup;
	Setup.SystemPrompt = "With tools.";
	Setup.Functions = AssistantFunctions(Tools, false);
	AssistantChat Chat;
	Chat.SetTools(Tools, false);
	AssistantEndpoint Where;
	std::string Error;
	ParseAssistantEndpoint("http://strix:8080", Where, Error);
	AssistantTimeouts Times;
	Chat.Ask("Hi", "C", ASSISTANT_ASK_FIRST, 8);
	std::string Body, Problem;
	CHECK(Chat.Request(Setup, Body, Problem));
	AssistantReply Refused;
	Refused.Status = 400;
	Refused.Error = "tools param requires --jinja flag";
	CHECK(AssistantTakeReply(Chat, Setup, false, Where, std::string(), Times, NET_DONE, Refused, std::string()));
	CHECK(Chat.HasToolsOff() && Setup.Functions.Size() == 0 && Contains(Setup.SystemPrompt, "you have no tools"));
	CHECK_HAS(Chat.Messages().back().Text, "--jinja");
	CHECK(Chat.State() == AssistantChat::MODEL);
	CHECK(Chat.Request(Setup, Body, Problem));
	Json Sent;
	CHECK(ParseJson(Body, Sent) && Sent.Get("tools") == 0);
	// Refused again: no going round
	CHECK(!AssistantTakeReply(Chat, Setup, false, Where, std::string(), Times, NET_DONE, Refused, std::string()));
	CHECK(Chat.State() == AssistantChat::IDLE);
	// Off, and an HTTP error
	Chat.Ask("Again", "C", ASSISTANT_ASK_FIRST, 8);
	CHECK(Chat.Request(Setup, Body, Problem));
	CHECK(!AssistantTakeReply(Chat, Setup, false, Where, std::string(), Times, NET_REFUSED, AssistantReply(), std::string()));
	CHECK_HAS(Chat.Messages().back().Text, "Nothing answers at strix:8080");
	Chat.Ask("Again", "C", ASSISTANT_ASK_FIRST, 8);
	CHECK(Chat.Request(Setup, Body, Problem));
	AssistantReply Missing;
	Missing.Status = 404;
	Missing.Error = "File Not Found";
	CHECK(!AssistantTakeReply(Chat, Setup, false, Where, std::string(), Times, NET_DONE, Missing, std::string()));
	CHECK_HAS(Chat.Messages().back().Text, "no chat at");
	// A proper answer
	Chat.Ask("Again", "C", ASSISTANT_ASK_FIRST, 8);
	CHECK(Chat.Request(Setup, Body, Problem));
	CHECK(!AssistantTakeReply(Chat, Setup, false, Where, std::string(), Times, NET_DONE, TextReply("Fine."), std::string()));
	CHECK(Chat.State() == AssistantChat::IDLE && Chat.Answer() != 0 && Chat.Answer()->Text == "Fine.");
}

}	// namespace

int main()
{
	std::setvbuf(stdout, 0, _IONBF, 0);
	TestEndpoints();
	TestSse();
	TestHttpStream();
	TestStream();
	TestServerAnswers();
	TestRichText();
	TestCallWords();
	TestChat();
	TestFitting();
	TestHelp();
	TestExchange();
	TestTakeReply();
	std::printf("%d checks, %d failed\n", g_Checks, g_Failures);
	return g_Failures == 0 ? 0 : 1;
}

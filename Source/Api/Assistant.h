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

// View > Ask (AskPanel.cpp): someone new to music asks their own model for
// guidance inside FamiTracker, or tells it to do the work. The model is any
// server that speaks OpenAI's Chat Completions API over plain HTTP on the home
// network: llama.cpp's llama-server, llama-swap, Ollama or LM Studio. It gets
// a system prompt (how to help a newcomer inside FamiTracker, then the tools'
// instructions), the song as it is when the question is asked, and
// FamiTracker's tools as functions made from their own schemas (all of them,
// or the small set of /mcp/core), which run in FamiTracker through
// ToolSet::Call. Two modes: Ask first runs the tools that only read at once
// and turns a call that would change the song into a proposal the user does
// or skips; Let it work makes the changes as the model asks for them, each one
// undoable. The answers stream in as server-sent events.
//
// Here: the settings and the server's address, the request, the streamed reply
// (HTTP, chunked or not, and the events cut into any pieces), the tools' kinds
// (reading or changing), the conversation with its rounds of tools, kept inside
// the model's context, and the help searched without a model. Plain C++14,
// ASCII, tested on Linux (tests/AssistantTests.cpp; with the tools in
// tests/ApiTests.cpp). AssistantSocket.h moves the bytes.

#include <string>
#include <vector>
#include "Json.h"

namespace ftapi {

struct ToolInfo;

// ---- The settings, and where the server is

struct AssistantSettings {
	std::string Endpoint;		// "http://192.168.1.50:8081", "http://strix:8080/v1"...; "" for no model (the help is searched)
	std::string Model;			// the model's name, which llama-swap and Ollama pick by; "" for the server's own
	std::string Key;			// sent as "Authorization: Bearer", "" for none
	bool SmallSet;				// the small set of tools (ToolSet::CoreTools), for smaller models; else all of them
	int AskRounds;				// rounds of tools a question may take in Ask first...
	int WorkRounds;				// ...and in Let it work
	int ContextTokens;			// the model's context; 0: as the server says (llama.cpp's /props), else ASSISTANT_CONTEXT
	AssistantSettings();
};

const int ASSISTANT_ASK_ROUNDS = 8;
const int ASSISTANT_WORK_ROUNDS = 300;
const int ASSISTANT_MOST_ROUNDS = 2000;
const int ASSISTANT_CONTEXT = 32768;		// a context the server doesn't tell
const int ASSISTANT_LEAST_CONTEXT = 2048;

// The settings within their limits (rounds 1-2000, a context of 0 or 2048 tokens and more, the address trimmed)
AssistantSettings SaneAssistantSettings(const AssistantSettings &Settings);

struct AssistantEndpoint {
	std::string Host;			// an IPv4 address or a computer's name
	int Port;
	std::string Base;			// where the API is: "/v1"
	std::string Root;			// the server's own paths (llama.cpp's /props) are under this: "" for the root
	AssistantEndpoint() : Port(80) {}
	std::string ChatPath() const { return Base + "/chat/completions"; }
	std::string ModelsPath() const { return Base + "/models"; }
	std::string HostHeader() const;		// "192.168.1.50:8081"
	std::string Text() const;			// "http://192.168.1.50:8081"
};

// The address as the user wrote it: "http://host:port", with or without /v1 or /v1/chat/completions, or just
// "host:port"; false with the reason (https, no computer, a bad port)
bool ParseAssistantEndpoint(const std::string &Url, AssistantEndpoint &Out, std::string &Error);

// The note shown while the questions would leave the house: the server's IPv4 address (as resolved; "" when it isn't
// known) outside this computer and the home network (MayConnect's ranges, Network.h). "" when they stay home
std::string AssistantPrivacyNote(const AssistantEndpoint &Where, const std::string &Address, const std::vector<std::string> &Own);

// ---- The tools as the model gets them

enum assistant_mode_t { ASSISTANT_ASK_FIRST, ASSISTANT_LET_IT_WORK };

enum assistant_kind_t {
	ASSISTANT_READS,			// only reads the song (or plays it): runs at once
	ASSISTANT_CHANGES,			// changes the song, its files or what FamiTracker shows: proposed in Ask first
	ASSISTANT_DEPENDS			// reads with some arguments, changes with others (AssistantCallChanges)
};

struct AssistantToolKind {
	const char *Name;
	int Kind;					// assistant_kind_t
};

// Every tool's kind, in step with ToolSet's list (the tests check that every tool is here, the readers marked
// read-only and the others not)
const std::vector<AssistantToolKind> &AssistantToolKinds();
// Whether a call would change anything: grow_melody offers versions until it picks one, soundtrack_notes reads the
// notes without a text to write, references lists the library unless it tags or removes. A tool the table lacks
// changes
bool AssistantCallChanges(const std::string &Tool, const Json &Arguments);
// The tools the model isn't given: look_at_song's picture (the chat takes text), send_file (for clients on other
// computers)
bool AssistantLeavesOut(const std::string &Tool);

// The functions of the request (OpenAI's "tools"): each tool's name, description and input schema as its
// parameters, in ToolSet's order; the small set's alone when SmallSet
Json AssistantFunctions(const std::vector<ToolInfo> &Tools, bool SmallSet);

// A call in plain words, for a proposal and the steps of a run: the tool's title and its main arguments ("Write a
// part: Pulse 1, bars 1-8, notes")
std::string AssistantCallWords(const std::string &Title, const Json &Arguments);
// The call behind the words: write_notes {"channel":"Pulse 1",...}, cut to Most bytes
std::string AssistantCallText(const std::string &Tool, const std::string &Arguments, size_t Most = 160);
// A tool's reply in a line, for the steps: its first line, cut to Most bytes at a word
std::string AssistantResultLine(const std::string &Result, size_t Most = 150);

// ---- What the model is told

// The system prompt: how to help someone new to music inside FamiTracker (plain words, the real menus, one step at a
// time, glossary words linked, asking before changing), then the tools' instructions (ToolSet::Instructions, or
// CoreInstructions for the small set). Without tools, how to help from the song's summary alone
std::string AssistantSystemPrompt(bool SmallSet, bool WithTools);

// The song as a question is asked, sent before it
struct AssistantFacts {
	std::string Song;			// get_song's overview
	std::string Next;			// what_next's step
	std::string Kind;			// the kind of song chosen in What next for the track (calm, lively or action), "" for none
	std::string Exercise;		// the exercise in progress (Help > Exercises): its title, "" for none...
	std::string ExerciseStep;	// ...what its note asks now...
	std::string ExerciseCheck;	// ...and the check_exercise check of that step, "" for none
	int Bar;					// the cursor's bar, from 1 (0 for none)...
	std::string Channel;		// ...and channel, as the pattern editor names it
	int Mode;					// assistant_mode_t
	bool WithTools;
	AssistantFacts() : Bar(0), Mode(ASSISTANT_ASK_FIRST), WithTools(true) {}
};
const size_t ASSISTANT_SONG_MOST = 6000;	// bytes of get_song's overview a question takes
std::string AssistantContext(const AssistantFacts &Facts);

// ---- An answer as the panel shows it

// Paragraphs of runs: plain, bold or linked. Links: "[[chord]]" opens the glossary entry the words name
// (FindGlossaryEntry), "[[the home note|home-note]]" the entry named after the bar, and the panel's own:
// "[[...|exercise:home-note]]" starts an exercise, "[[...|tutorial:First song]]" a tutorial, "[[...|settings]]" opens
// the settings. Markdown's **bold**, `code`, headings, list marks and code blocks are read; a mark that names nothing
// is its words
struct AssistantRun {
	std::string Text;			// UTF-8
	bool Bold;
	std::string Link;			// "" for plain text
	AssistantRun() : Bold(false) {}
};

struct AssistantParagraph {
	std::vector<AssistantRun> Runs;
	std::string Bullet;			// a list item's mark ("-", "2."), "" otherwise
	bool Heading;
	bool Code;					// a line of a code block, as written
	bool Gap;					// an empty line before it
	AssistantParagraph() : Heading(false), Code(false), Gap(false) {}
};

std::vector<AssistantParagraph> AssistantRichText(const std::string &Text);
// The text without its marks, for copying (links as their words)
std::string AssistantPlainText(const std::string &Text);

// ---- The server's replies

// An HTTP/1.1 response read as its bytes arrive: the status and headers, then the body (by its length, chunked, or
// until the connection closes), each piece of it as soon as it's there
class HttpStreamReader {
public:
	enum state_t { HEAD, BODY, DONE, BAD };

	HttpStreamReader();
	// Adds received bytes, the body's new ones appended to Body
	state_t Feed(const char *pData, size_t Size, std::string &Body);
	// The connection closed: a body without a length ends here (DONE), anything else cut short is BAD
	state_t Close(std::string &Body);
	state_t State() const { return m_State; }
	int Status() const { return m_iStatus; }
	const std::string &ContentType() const { return m_ContentType; }

private:
	state_t Read(std::string &Body);

	state_t m_State;
	std::string m_Buffer;		// bytes not used yet
	int m_iStatus;
	std::string m_ContentType;
	bool m_bChunked, m_bHasLength;
	size_t m_iLeft;				// of the body, or of the chunk
	int m_iChunk;				// chunked: 0 a size line, 1 its data, 2 the line end after it, 3 the trailers
};

// Server-sent events: the data of each one (its "data:" lines joined by line breaks), as their pieces come
class SseReader {
public:
	SseReader() : m_bData(false), m_bCr(false) {}
	void Feed(const std::string &Bytes, std::vector<std::string> &Events);
	void End(std::vector<std::string> &Events);		// an event the stream ended without its blank line

private:
	void Line(std::vector<std::string> &Events);

	std::string m_Line, m_Data;
	bool m_bData, m_bCr;
};

enum assistant_call_state_t {
	CALL_NEW,					// as the model asked for it
	CALL_PROPOSED,				// waits for Do it or Skip
	CALL_DONE,
	CALL_FAILED,				// the tool answered with an error, or couldn't be called
	CALL_SKIPPED,				// the user skipped it, or asked something else first
	CALL_STOPPED				// not run: the user stopped
};

// A tool the model calls
struct AssistantCall {
	std::string Id;				// the model's id for it ("call_abc"), or one of the panel's own
	std::string Name;
	std::string Arguments;		// JSON text as the model wrote it
	int State;					// assistant_call_state_t
	bool Changes;				// AssistantCallChanges
	std::string Title;			// the tool's title
	std::string Words;			// in plain words (AssistantCallWords)
	std::string Result;			// what the model gets back
	bool Shortened;				// Result was shortened to keep the conversation in the model's context
	AssistantCall() : State(CALL_NEW), Changes(false), Shortened(false) {}
};

// What the model answered
struct AssistantReply {
	std::string Content;		// the answer's text (thinking in <think> tags left out)
	std::string Reasoning;		// its thinking, not shown
	std::vector<AssistantCall> Calls;
	std::string Finish;			// "stop", "tool_calls", "length"...
	int PromptTokens, ReplyTokens;	// as the server counted them, -1 when it didn't say
	int Status;					// the HTTP status
	std::string Error;			// the server's error, "" for none
	bool Ended;					// whole: [DONE], or the stream's end after a finish, or a whole reply
	AssistantReply() : PromptTokens(-1), ReplyTokens(-1), Status(0), Ended(false) {}
};

// A chat completion's reply read as its body arrives: server-sent events (or a whole JSON reply, when the server
// didn't stream, and errors)
class AssistantStream {
public:
	AssistantStream();
	void Start(int Status, const std::string &ContentType);
	// The body's new bytes; NewText and NewThinking get what they add, to show as they come
	void Feed(const std::string &Bytes, std::string &NewText, std::string &NewThinking);
	void End();
	const AssistantReply &Reply() const { return m_Reply; }

private:
	void Event(const std::string &Data, std::string &NewText, std::string &NewThinking);
	void Choice(const Json &Choice, bool Whole, std::string &NewText, std::string &NewThinking);
	void Text(const std::string &Piece, std::string &NewText, std::string &NewThinking);

	AssistantReply m_Reply;
	SseReader m_Events;
	bool m_bEvents;				// text/event-stream, else a whole reply
	std::string m_Body;			// a whole reply's body
	bool m_bThinking;			// within <think>...</think> in the text
	std::string m_Held;			// the text's end that may be the start of a tag
	std::vector<int> m_Indexes;	// the calls' indexes as the deltas give them
};

// llama.cpp's /props: the context in tokens (default_generation_settings.n_ctx, or n_ctx), 0 when it doesn't say
int AssistantContextOfProps(const std::string &Body);
// /v1/models: the models' names
std::vector<std::string> AssistantModelsOf(const std::string &Body);
// A reply's error in a line: the server's message ({"error":{"message":...}}, {"error":"..."}, {"detail":...}), or
// its text
std::string AssistantErrorMessage(const std::string &Body);
// Whether a refusal says the server takes no tools (llama.cpp without --jinja: "tools param requires --jinja flag";
// Ollama: "does not support tools")
bool AssistantToolsRefused(int Status, const std::string &Message);
// An HTTP error as the user is told it, with what to check
std::string AssistantHttpProblem(const AssistantEndpoint &Where, const std::string &Model, int Status, const std::string &Message);

// ---- Requests

// The HTTP request's bytes: GET when Body is empty, else POST with the JSON body; the key as a bearer token; the
// connection closed after the answer
std::string AssistantHttpRequest(const AssistantEndpoint &Where, const std::string &Path, const std::string &Key,
	const std::string &Body, bool Stream);
// Test connection's question: a word with one tool, which tells whether the server takes tools
std::string AssistantProbeBody(const std::string &Model);

// ---- The conversation

enum assistant_role_t { ASSISTANT_USER, ASSISTANT_MODEL, ASSISTANT_NOTE };

struct AssistantMessage {
	int Role;					// assistant_role_t; a note is for the user alone (an error, a limit), never sent
	std::string Text;			// the question, the answer (with marks: AssistantRichText) or the note
	std::string Context;		// a question's: the song as it was (AssistantContext)
	bool ContextLeftOut;		// left out of the requests, to save room
	bool LeftOut;				// the whole message left out of the requests, to save room
	std::vector<AssistantCall> Calls;	// an answer's
	bool Stopped;				// an answer the user stopped
	int Question;				// the question it belongs to, from 1
	int Mode;					// a question's assistant_mode_t
	AssistantMessage() : Role(ASSISTANT_NOTE), ContextLeftOut(false), LeftOut(false), Stopped(false), Question(0),
		Mode(ASSISTANT_ASK_FIRST) {}
};

// Where FamiTracker runs the tools: a call's reply, false when it failed. Changes: a change the user allowed (Do it)
// or Let it work makes
class AssistantTools {
public:
	virtual ~AssistantTools() {}
	virtual bool RunTool(const std::string &Name, const std::string &Arguments, bool Changes, std::string &Reply) = 0;
};

// What the requests are made with
struct AssistantSetup {
	std::string Model;			// "" for none
	std::string SystemPrompt;
	Json Functions;				// AssistantFunctions; empty: without tools
	int ContextTokens;			// the model's context
	AssistantSetup() : Functions(Json::MakeArray()), ContextTokens(ASSISTANT_CONTEXT) {}
};

const size_t ASSISTANT_RESULT_MOST = 24000;		// bytes of a tool's reply the model gets at most
const double ASSISTANT_BYTES_PER_TOKEN = 3.5;	// the first guess, until the server's count of a request says
const double ASSISTANT_CUT_BYTES_PER_TOKEN = 5.0;	// more bytes a token than that: the server cut the request

// The bytes a request may take in a context of that many tokens: the context less the room kept for the reply (its
// thinking too: 6144 tokens, a fifth of a small context), at that many bytes a token
double AssistantRequestRoom(int ContextTokens, double BytesPerToken);
const size_t ASSISTANT_SHORT_RESULT = 600;		// an older reply shortened to save room

// A conversation about one song, and the question being worked on: the model's turn (a request is due), the user's
// (proposals wait) or nobody's. The panel and the tests drive it the same way: Ask, then while it's the model's turn
// Request, send it, Replied and RunReady; Decide for each proposal
class AssistantChat {
public:
	enum state_t { IDLE, MODEL, USER };

	AssistantChat();
	void Clear();								// New question: the conversation starts over
	state_t State() const { return m_State; }
	const std::vector<AssistantMessage> &Messages() const { return m_Messages; }
	int Rounds() const { return m_iRounds; }	// of the question being worked on
	int Questions() const { return m_iQuestion; }
	int Mode() const { return m_iMode; }

	// The tools the model is offered (their titles name the calls)
	void SetTools(const std::vector<ToolInfo> &Tools, bool SmallSet);
	// A question: the model's turn. Proposals still waiting are skipped: the user moved on
	void Ask(const std::string &Question, const std::string &Context, int Mode, int MostRounds);
	// The next request's JSON body, the conversation fitted into the model's context (older tool replies shortened,
	// then older questions left out); false when it isn't the model's turn, or when it can't fit (Problem says so)
	bool Request(const AssistantSetup &Setup, std::string &Body, std::string &Problem);
	// The model's reply: its text and the calls it makes (each checked: a tool it wasn't given or arguments that
	// aren't JSON fail at once, with the reason for the model). Stopped: the user stopped it, or (Problem) it was cut
	// short; the text so far is kept, the calls dropped, and the note says which
	void Replied(const AssistantReply &Reply, bool Stopped, const std::string &Problem = std::string());
	// Runs the calls that can run now: the readers, and in Let it work the changes too; in Ask first a change waits
	// for the user (USER). After them the model's turn again, until the question's rounds are used
	void RunReady(AssistantTools &Tools);
	// The user's answer to a proposal (a call of the message): Do it runs it, Skip tells the model. When none waits,
	// the model hears about those done (none done: the question ends). False when it wasn't waiting
	bool Decide(size_t Message, size_t Call, bool Do, AssistantTools &Tools);
	// Stop: the question ends here (the model's reply is cut where it is: Replied with Stopped)
	void Stop();
	// A line for the user, never sent
	void Note(const std::string &Text);
	// A question answered without a model (FamiTracker's own help, AssistantHelpAnswer): shown, never sent
	void Offline(const std::string &Question, const std::string &Answer);
	// The question's last answer, the one being written (NULL for none)
	const AssistantMessage *Answer() const;
	// The server takes no tools: the conversation goes on without them, the calls made so far told as text
	void ToolsOff() { m_bToolsOff = true; }
	bool HasToolsOff() const { return m_bToolsOff; }

	double BytesPerToken() const { return m_dBytesPerToken; }	// as the last reply's count said

private:
	void Run(AssistantMessage &Message, AssistantCall &Call, AssistantTools &Tools);
	void AfterCalls();
	bool Fits(const AssistantSetup &Setup, std::string &Body);
	std::string Build(const AssistantSetup &Setup) const;
	int Waiting() const;		// proposals waiting

	std::vector<AssistantMessage> m_Messages;
	state_t m_State;
	int m_iQuestion;			// questions asked
	int m_iMode;
	int m_iRounds, m_iMostRounds;
	int m_iCalls;				// calls counted, for the panel's own ids
	bool m_bToolsOff;
	double m_dBytesPerToken;
	size_t m_iSent;				// the last request's bytes
	std::vector<std::pair<std::string, std::string> > m_Titles;	// the tools offered: name, title
};

// ---- Help without a model

// FamiTracker's own help that matches a question: the glossary's entries, the exercises and the tutorials (given:
// their titles and texts)
struct AssistantTutorial {
	std::string Title;
	std::string Text;			// its steps' texts
};

struct AssistantHelpItem {
	std::string Kind;			// "glossary", "exercise", "tutorial"
	std::string Id;				// the entry's id, the exercise's id, the tutorial's title
	std::string Title;
	std::string Line;			// a sentence about it
	int Score;
};

std::vector<AssistantHelpItem> AssistantSearchHelp(const std::string &Question, const std::vector<AssistantTutorial> &Tutorials,
	size_t Most = 6);
// The help found, as an answer with links (AssistantRichText); Configured: a model is set up but unreachable
std::string AssistantHelpAnswer(const std::vector<AssistantHelpItem> &Items, bool Configured);
// How to connect a model of one's own, step by step (the panel's ? button): shown, never sent
std::string AssistantSetupGuide();

}	// namespace ftapi

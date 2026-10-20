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

// Tests for the Claude Code connection's core: JSON, HTTP framing, the MCP
// protocol, the musical helpers and every tool, against a song in memory.

#include "../Bridge.h"
#include "../BridgeSocket.h"
#include "../Http.h"
#include "../Json.h"
#include "../Mcp.h"
#include "../Music.h"
#include "../Picture.h"
#include "../Png.h"
#include "../Presets.h"
#include "../Song.h"
#include "../Tools.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
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

using namespace ftapi;

namespace {

int g_Checks = 0, g_Failures = 0;

void Check(bool Condition, const char *What, int Line)
{
	++g_Checks;
	if (!Condition) {
		++g_Failures;
		std::printf("FAIL line %d: %s\n", Line, What);
	}
}

#define CHECK(x) Check((x), #x, __LINE__)
#define CHECK_EQ(a, b) Check((a) == (b), #a " == " #b, __LINE__)

bool Contains(const std::string &Text, const char *Part)
{
	return Text.find(Part) != std::string::npos;
}

#define CHECK_HAS(text, part) Check(Contains((text), (part)), "\"" part "\" in " #text, __LINE__)
#define CHECK_LACKS(text, part) Check(!Contains((text), (part)), "no \"" part "\" in " #text, __LINE__)

// A song in memory: 2A03 and VRC6 channels, 64-row patterns, 4 rows a beat,
// 16 a bar (4 bars a frame)
class FakeSong : public SongHost {
public:
	struct Clip {
		std::string Name;
		int Id;
		std::vector<int> Sources;
		std::vector<std::vector<Cell> > Columns;
	};

	// The module's tracks; the selected one plays from Rows and Info
	struct StoredTrack {
		std::string Name;
		int Speed, Tempo;
		std::vector<std::vector<Cell> > Rows;
	};

	SongInfo Info;
	std::vector<std::vector<Cell> > Rows;	// per channel, the selected track's
	std::vector<StoredTrack> Tracks;
	int Current;
	std::vector<Clip> Clips;
	std::vector<std::vector<std::vector<Cell> > > History;
	std::vector<std::pair<int, int> > TempoUndo;
	int Writes, LastLink, PlayFirst, PlayRows, NextClipId;
	bool Busy, Stopped;
	std::string Saved;

	explicit FakeSong(int Frames = 2) : Current(0), Writes(0), LastLink(-2), PlayFirst(-1), PlayRows(-1), NextClipId(1), Busy(false),
		Stopped(false)
	{
		Info.Title = "Test song";
		Info.TrackName = "Town";
		Info.FileName = "test.ftm";
		Info.PatternLength = 64;
		Info.Frames = Frames;
		Info.FirstHighlight = 4;
		Info.SecondHighlight = 16;
		Info.Expansion = "VRC6";
		const char *const NAMES[] = { "Pulse 1", "Pulse 2", "Triangle", "Noise", "DPCM", "Pulse 1", "Pulse 2", "Sawtooth" };
		const int KINDS[] = { KIND_PULSE, KIND_PULSE, KIND_TRIANGLE, KIND_NOISE, KIND_DPCM, KIND_PULSE, KIND_PULSE, KIND_SAWTOOTH };
		for (int c = 0; c < 8; ++c) {
			ChannelInfo Channel;
			Channel.Name = NAMES[c];
			Channel.Chip = c < 5 ? "2A03" : "VRC6";
			Channel.Kind = KINDS[c];
			Channel.ChanId = c;
			Channel.EffectColumns = 2;
			Info.Channels.push_back(Channel);
			Rows.push_back(std::vector<Cell>(static_cast<size_t>(Frames * 64)));
		}
		AddInstrumentInfo(0, "Lead", "VRC6", true);
		AddInstrumentInfo(1, "Bass", "2A03", true);
		AddInstrumentInfo(2, "Square", "2A03", false);
		StoredTrack First;
		First.Name = Info.TrackName;
		Tracks.push_back(First);
	}

	// The selected track goes back into Tracks, Track comes out
	void Switch(int Track)
	{
		Tracks[Current].Name = Info.TrackName;
		Tracks[Current].Speed = Info.Speed;
		Tracks[Current].Tempo = Info.Tempo;
		Tracks[Current].Rows = Rows;
		Current = Track;
		Info.TrackName = Tracks[Track].Name;
		Info.Speed = Tracks[Track].Speed;
		Info.Tempo = Tracks[Track].Tempo;
		Rows = Tracks[Track].Rows;
	}

	void AddInstrumentInfo(int Index, const char *Name, const char *Chip, bool Releases)
	{
		InstrumentInfo I;
		I.Index = Index;
		I.Name = Name;
		I.Chip = Chip;
		I.Releases = Releases;
		Info.Instruments.push_back(I);
	}

	// Puts a note (by name) on a row
	void Put(int Channel, int Row, const char *Pitch, int Instrument = NO_INSTRUMENT, int Volume = -1)
	{
		int Value = 0;
		if (!ParsePitch(Pitch, Value)) {
			std::string Drum;
			ParseNoisePitch(Pitch, Value, Drum);
		}
		Cell &C = Rows[Channel][Row];
		C.SetPitch(Value);
		if (Instrument != NO_INSTRUMENT)
			C.Instrument = static_cast<unsigned char>(Instrument);
		if (Volume >= 0)
			C.Volume = static_cast<unsigned char>(Volume);
	}

	void End(int Channel, int Row, int Note = NOTE_HALT)
	{
		Rows[Channel][Row].Note = static_cast<unsigned char>(Note);
	}

	bool GetSong(SongInfo &Out, std::string &) override
	{
		Info.Frames = static_cast<int>(Rows[0].size()) / Info.PatternLength;
		Info.Track = Current;
		Info.Tracks = static_cast<int>(Tracks.size());
		Info.TrackNames.clear();
		for (size_t t = 0; t < Tracks.size(); ++t)
			Info.TrackNames.push_back(static_cast<int>(t) == Current ? Info.TrackName : Tracks[t].Name);
		Info.Clips.clear();
		for (size_t i = 0; i < Clips.size(); ++i) {
			ClipInfo C;
			C.Index = static_cast<int>(i);
			C.Id = Clips[i].Id;
			C.Name = Clips[i].Name;
			C.Columns = static_cast<int>(Clips[i].Columns.size());
			C.Rows = static_cast<int>(Clips[i].Columns[0].size());
			C.Sources = Clips[i].Sources;
			Info.Clips.push_back(C);
		}
		Out = Info;
		return true;
	}

	void ReadCells(int Channel, int First, int Count, std::vector<Cell> &Out) override
	{
		Out.clear();
		for (int r = First; r < First + Count; ++r)
			Out.push_back(r < static_cast<int>(Rows[Channel].size()) ? Rows[Channel][r] : Cell());
	}

	bool ReadClip(int Index, std::vector<std::vector<Cell> > &Columns) override
	{
		if (Index < 0 || Index >= static_cast<int>(Clips.size()))
			return false;
		Columns = Clips[Index].Columns;
		return true;
	}

	bool CanChange(std::string &Why) override
	{
		if (Busy)
			Why = "A dialog is open in FamiTracker.";
		return !Busy;
	}

	bool WriteSong(const std::vector<ChannelCells> &Parts, int First, int LinkClipId, std::string &) override
	{
		History.push_back(Rows);
		size_t Needed = Rows[0].size();
		for (size_t p = 0; p < Parts.size(); ++p)
			Needed = std::max(Needed, First + Parts[p].Cells.size());
		Needed = (Needed + 63) / 64 * 64;
		for (size_t c = 0; c < Rows.size(); ++c)
			Rows[c].resize(Needed);
		for (size_t p = 0; p < Parts.size(); ++p)
			for (size_t r = 0; r < Parts[p].Cells.size(); ++r)
				Rows[Parts[p].Channel][First + r] = Parts[p].Cells[r];
		++Writes;
		LastLink = LinkClipId;
		return true;
	}

	bool AddClip(std::string &Name, const std::vector<int> &Sources, const std::vector<std::vector<Cell> > &Columns, std::string &) override
	{
		std::string Unique = Name;
		for (int n = 2; ; ++n) {
			bool Taken = false;
			for (size_t i = 0; i < Clips.size(); ++i)
				if (Clips[i].Name == Unique)
					Taken = true;
			if (!Taken)
				break;
			char Suffix[16];
			std::snprintf(Suffix, sizeof(Suffix), " %d", n);
			Unique = Name + Suffix;
		}
		Name = Unique;
		Clip C;
		C.Name = Name;
		C.Id = NextClipId++;
		C.Sources = Sources;
		C.Columns = Columns;
		Clips.push_back(C);
		return true;
	}

	int AddInstrument(const Preset &Sound, const std::string &Chip, bool Sawtooth, std::string &) override
	{
		const std::string Name = Sound.InstrumentName(Sawtooth);
		int Next = 0;
		for (size_t i = 0; i < Info.Instruments.size(); ++i) {
			if (Info.Instruments[i].Name == Name && Info.Instruments[i].Chip == Chip)
				return Info.Instruments[i].Index;
			Next = std::max(Next, Info.Instruments[i].Index + 1);
		}
		AddInstrumentInfo(Next, Name.c_str(), Chip.c_str(), Sound.Releases());
		return Next;
	}

	bool SetSpeedTempo(int Speed, int Tempo, std::string &) override
	{
		TempoUndo.push_back(std::make_pair(Info.Speed, Info.Tempo));
		Info.Speed = Speed;
		Info.Tempo = Tempo;
		return true;
	}

	bool SelectTrack(int Track, std::string &Error) override
	{
		if (Track < 0 || Track >= static_cast<int>(Tracks.size())) {
			Error = "No such track.";
			return false;
		}
		Switch(Track);
		return true;
	}

	int AddTrack(const std::string &Name, const std::vector<std::vector<Cell> > &Channels, int Speed, int Tempo, std::string &Error) override
	{
		if (Tracks.size() >= 64) {
			Error = "The module has 64 tracks, FamiTracker's most.";
			return -1;
		}
		StoredTrack Track;
		Track.Name = Name;
		Track.Speed = Speed;
		Track.Tempo = Tempo;
		size_t Length = 64;
		for (size_t c = 0; c < Channels.size(); ++c)
			Length = std::max(Length, (Channels[c].size() + 63) / 64 * 64);
		Track.Rows = Channels;
		Track.Rows.resize(Rows.size());
		for (size_t c = 0; c < Track.Rows.size(); ++c)
			Track.Rows[c].resize(Length);
		Tracks.push_back(Track);
		Switch(static_cast<int>(Tracks.size()) - 1);
		return Current;
	}

	bool Play(int First, int Count, std::string &) override
	{
		PlayFirst = First;
		PlayRows = Count;
		return true;
	}

	void Stop() override { Stopped = true; }

	bool Undo(std::string &Message) override
	{
		if (History.empty())
			return false;
		Rows = History.back();
		History.pop_back();
		Message = "Undid it.";
		return true;
	}

	bool Save(const std::string &CopyName, std::string &Message) override
	{
		Saved = CopyName.empty() ? "test.ftm" : CopyName + ".ftm";
		Message = "Saved " + Saved + ".";
		return true;
	}
};

Json Parse(const std::string &Text)
{
	Json Value;
	std::string Error;
	if (!ParseJson(Text, Value, &Error))
		std::printf("JSON error: %s in %s\n", Error.c_str(), Text.c_str());
	return Value;
}

ToolResult Run(FakeSong &Song, const char *Tool, const std::string &Arguments)
{
	ToolSet Tools(Song);
	return Tools.Call(Tool, Parse(Arguments));
}

// The notes a channel holds, as text: "0:C-4 4:=== ..."
std::string Notes(const FakeSong &Song, int Channel, int First = 0, int End = -1)
{
	std::string Out;
	const std::vector<Cell> &Rows = Song.Rows[Channel];
	if (End < 0)
		End = static_cast<int>(Rows.size());
	for (int r = First; r < End && r < static_cast<int>(Rows.size()); ++r) {
		const Cell &C = Rows[r];
		if (C.Note == NOTE_NONE)
			continue;
		char Text[32];
		if (C.HasNote())
			std::snprintf(Text, sizeof(Text), "%d:%s ", r, Song.Info.Channels[Channel].Kind == KIND_NOISE ?
				NoisePitchName(C.Pitch()).c_str() : PitchName(C.Pitch()).c_str());
		else
			std::snprintf(Text, sizeof(Text), "%d:%s ", r, C.Note == NOTE_RELEASE ? "===" : "---");
		Out += Text;
	}
	return Out;
}

bool HasEffectAt(const FakeSong &Song, int Channel, int Row, int Effect, int Param)
{
	const Cell &C = Song.Rows[Channel][Row];
	for (int k = 0; k < EFFECT_COLUMNS; ++k)
		if (C.Effect[k] == Effect && C.Param[k] == Param)
			return true;
	return false;
}

// JSON

void TestJson()
{
	Json Value;
	CHECK(ParseJson("{\"a\": [1, 2.5, -3e2, true, false, null, \"x\\ny\\u00e9\\ud83c\\udfb5\"], \"b\": {}}", Value));
	CHECK(Value.IsObject());
	const Json *pA = Value.Get("a");
	CHECK(pA != 0 && pA->Size() == 7);
	CHECK(pA->At(0).IsInteger() && pA->At(0).Integer() == 1);
	CHECK(pA->At(1).Number() == 2.5);
	CHECK(pA->At(2).Number() == -300.0);
	CHECK(pA->At(3).Bool() && !pA->At(4).Bool() && pA->At(5).IsNull());
	CHECK_EQ(pA->At(6).String(), std::string("x\ny\xC3\xA9\xF0\x9F\x8E\xB5"));
	CHECK_EQ(WriteJson(Value), std::string("{\"a\":[1,2.5,-3e2,true,false,null,\"x\\ny\xC3\xA9\xF0\x9F\x8E\xB5\"],\"b\":{}}"));

	// Numbers keep their text (request IDs), made ones are written plainly
	CHECK_EQ(WriteJson(Parse("12345678901234567890")), std::string("12345678901234567890"));
	CHECK_EQ(WriteJson(Json(42)), std::string("42"));
	CHECK_EQ(WriteJson(Json(0.5)), std::string("0.5"));

	// Strict: these are all errors
	const char *const BAD[] = { "", "{", "[1,]", "{\"a\" 1}", "01", "1.", "-", "\"\\x\"", "\"a", "tru", "[1] 2", "{\"a\":1,}",
		"\"\x01\"", "NaN" };
	for (size_t i = 0; i < sizeof(BAD) / sizeof(BAD[0]); ++i) {
		std::string Error;
		Json Out;
		Check(!ParseJson(BAD[i], Out, &Error) && !Error.empty(), BAD[i], __LINE__);
	}
	// Too deep
	CHECK(!ParseJson(std::string(100, '[') + std::string(100, ']'), Value));
	CHECK(ParseJson(std::string(50, '[') + std::string(50, ']'), Value));

	// Writing escapes control characters and repairs bad UTF-8
	CHECK_EQ(WriteJson(Json(std::string("a\x01\"\\b"))), std::string("\"a\\u0001\\\"\\\\b\""));
	CHECK_EQ(WriteJson(Json(std::string("caf\xE9"))), std::string("\"caf\xEF\xBF\xBD\""));
	CHECK_EQ(ValidUtf8("ok\xC3\xA9\xFF"), std::string("ok\xC3\xA9\xEF\xBF\xBD"));

	// A lone surrogate becomes U+FFFD
	CHECK(ParseJson("\"\\ud83c x\"", Value));
	CHECK_EQ(Value.String(), std::string("\xEF\xBF\xBD x"));

	// Objects: members replaced in place, order kept
	Json Object = Json::MakeObject();
	Object.Set("z", 1).Set("a", 2).Set("z", 3);
	CHECK_EQ(WriteJson(Object), std::string("{\"z\":3,\"a\":2}"));
	CHECK(Parse("{\"x\":[1,{\"y\":2}]}") == Parse("{ \"x\" : [ 1 , { \"y\" : 2 } ] }"));
}

// HTTP

void TestHttp()
{
	{
		HttpRequestParser Parser;
		const std::string Bytes = "POST /mcp HTTP/1.1\r\nHost: 127.0.0.1:46600\r\nContent-Type: application/json\r\n"
			"Content-Length: 7\r\n\r\n{\"a\":1}";
		// Byte by byte
		HttpRequestParser::Result Result = HttpRequestParser::NEED_MORE;
		for (size_t i = 0; i < Bytes.size(); ++i) {
			Result = Parser.Feed(&Bytes[i], 1);
			if (i + 1 < Bytes.size())
				CHECK(Result == HttpRequestParser::NEED_MORE);
		}
		CHECK(Result == HttpRequestParser::COMPLETE);
		const HttpRequest Request = Parser.Take();
		CHECK_EQ(Request.Method, std::string("POST"));
		CHECK_EQ(Request.Path(), std::string("/mcp"));
		CHECK_EQ(Request.Body, std::string("{\"a\":1}"));
		CHECK(Request.Header("content-type") != 0 && *Request.Header("CONTENT-TYPE") == "application/json");
		CHECK(Request.KeepAlive());
	}
	{
		// Chunked, then a second request in the same bytes
		HttpRequestParser Parser;
		const std::string Bytes = "POST /mcp?x=1 HTTP/1.1\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n"
			"3;ext=1\r\nabc\r\n2\r\nde\r\n0\r\nTrailer: x\r\n\r\n"
			"GET /mcp HTTP/1.1\r\n\r\n";
		CHECK(Parser.Feed(Bytes.data(), Bytes.size()) == HttpRequestParser::COMPLETE);
		const HttpRequest First = Parser.Take();
		CHECK_EQ(First.Body, std::string("abcde"));
		CHECK_EQ(First.Path(), std::string("/mcp"));
		CHECK(!First.KeepAlive());
		CHECK(Parser.Feed() == HttpRequestParser::COMPLETE);
		const HttpRequest Second = Parser.Take();
		CHECK_EQ(Second.Method, std::string("GET"));
		CHECK(Second.Body.empty());
		CHECK(Parser.Feed() == HttpRequestParser::NEED_MORE);
	}
	{
		HttpRequestParser Parser(16);
		const std::string Bytes = "POST / HTTP/1.1\r\nContent-Length: 17\r\n\r\n";
		CHECK(Parser.Feed(Bytes.data(), Bytes.size()) == HttpRequestParser::BAD);
		CHECK_EQ(Parser.ErrorStatus(), 413);
	}
	{
		HttpRequestParser Parser;
		const std::string Bytes = "POST / HTTP/1.1\r\nContent-Length: x\r\n\r\n";
		CHECK(Parser.Feed(Bytes.data(), Bytes.size()) == HttpRequestParser::BAD);
		CHECK_EQ(Parser.ErrorStatus(), 400);
	}
	{
		HttpRequestParser Parser(1024, 64);
		const std::string Bytes = "POST / HTTP/1.1\r\nX-Long: " + std::string(100, 'a');
		CHECK(Parser.Feed(Bytes.data(), Bytes.size()) == HttpRequestParser::BAD);
		CHECK_EQ(Parser.ErrorStatus(), 431);
	}
	{
		HttpRequestParser Parser;
		const std::string Bytes = "POST / HTTP/2.0\r\n\r\n";
		CHECK(Parser.Feed(Bytes.data(), Bytes.size()) == HttpRequestParser::BAD);
		CHECK_EQ(Parser.ErrorStatus(), 505);
	}
	{
		// Expect: 100-continue asks for the go-ahead before the body
		HttpRequestParser Parser;
		const std::string Head = "POST /mcp HTTP/1.1\r\nExpect: 100-continue\r\nContent-Length: 2\r\n\r\n";
		CHECK(Parser.Feed(Head.data(), Head.size()) == HttpRequestParser::NEED_MORE);
		CHECK(Parser.TakeContinue());
		CHECK(!Parser.TakeContinue());
		CHECK(Parser.Feed("{}", 2) == HttpRequestParser::COMPLETE);
		CHECK_EQ(Parser.Take().Body, std::string("{}"));
	}
	{
		HttpResponse Response;
		Response.Status = 202;
		const std::string Bytes = FormatHttpResponse(Response, true);
		CHECK_HAS(Bytes, "HTTP/1.1 202 Accepted\r\n");
		CHECK_HAS(Bytes, "Content-Length: 0\r\n");
		CHECK_HAS(Bytes, "Connection: keep-alive\r\n");
		CHECK(Bytes.size() >= 4 && Bytes.compare(Bytes.size() - 4, 4, "\r\n\r\n") == 0);
	}
}

// Music

void TestNames()
{
	for (int p = 0; p < PITCHES; ++p) {
		int Back = -1;
		Check(ParsePitch(PitchName(p), Back) && Back == p, PitchName(p).c_str(), __LINE__);
	}
	int Pitch = -1;
	CHECK(ParsePitch("C-3", Pitch) && Pitch == 36);
	CHECK(ParsePitch("c3", Pitch) && Pitch == 36);
	CHECK(ParsePitch("Db4", Pitch) && Pitch == 49);
	CHECK(ParsePitch("Bb-2", Pitch) && Pitch == 34);
	CHECK(ParsePitch("E\xE2\x99\xAD" "3", Pitch) && Pitch == 39);
	CHECK(ParsePitch("Cb3", Pitch) && Pitch == 35);
	CHECK(ParsePitch(" A#0 ", Pitch) && Pitch == 10);
	CHECK(!ParsePitch("H-4", Pitch));
	CHECK(!ParsePitch("C-8", Pitch));
	CHECK(!ParsePitch("Cb0", Pitch));
	CHECK(!ParsePitch("C", Pitch));
	CHECK(!ParsePitch("C-44", Pitch));
	CHECK_EQ(PitchName(57), std::string("A-4"));
	CHECK_EQ(PitchName(1), std::string("C#0"));

	std::string Drum;
	CHECK(ParseNoisePitch("kick", Pitch, Drum) && Pitch == 4 && Drum == "kick");
	CHECK(ParseNoisePitch("Hi-Hat", Pitch, Drum) && Pitch == 14 && Drum == "hi-hat");
	CHECK(ParseNoisePitch("A-#", Pitch, Drum) && Pitch == 10 && Drum.empty());
	CHECK(ParseNoisePitch("noise 12", Pitch, Drum) && Pitch == 12);
	CHECK(ParseNoisePitch("f", Pitch, Drum) && Pitch == 15);
	CHECK(!ParseNoisePitch("G-#", Pitch, Drum));
	CHECK(!ParseNoisePitch("16", Pitch, Drum));
	CHECK_EQ(NoisePitchName(10), std::string("A-#"));

	int Effect = 0, Param = 0;
	CHECK(ParseEffect("421", Effect, Param) && Effect == EFF_VIBRATO && Param == 0x21);
	CHECK(ParseEffect("q 3f", Effect, Param) && Effect == EFF_SLIDE_UP && Param == 0x3F);
	CHECK(ParseEffect("047", Effect, Param) && Effect == EFF_ARPEGGIO && Param == 0x47);
	CHECK(!ParseEffect("K12", Effect, Param));
	CHECK(!ParseEffect("4xy", Effect, Param));
	CHECK_EQ(EffectText(EFF_SLIDE_DOWN, 0x31), std::string("R31"));
	CHECK_EQ(EffectText(EFF_SPEED, 6), std::string("F06"));
	CHECK(EffectLetter(EFF_DPCM_PITCH) == 'W');
	CHECK_EQ(FormatNumber(1.0), std::string("1"));
	CHECK_EQ(FormatNumber(2.5), std::string("2.5"));
	CHECK_EQ(FormatNumber(0.25), std::string("0.25"));
	CHECK_EQ(FormatNumber(1.0 / 3.0), std::string("0.333"));
}

void TestTiming()
{
	const Timing T(64, 2, 4, 16);
	CHECK_EQ(T.BarsPerFrame(), 4);
	CHECK_EQ(T.BarCount(), 8);
	CHECK_EQ(T.BarStart(1), 0);
	CHECK_EQ(T.BarStart(5), 64);
	CHECK_EQ(T.BarStart(6), 80);
	CHECK_EQ(T.BarOf(79), 5);
	CHECK_EQ(T.BarOf(80), 6);
	CHECK(T.BeatOf(86) == 2.5);
	int Row = 0;
	bool Exact = false;
	CHECK(T.PositionToRow(2, 3.5, Row, Exact) && Row == 26 && Exact);
	CHECK(T.PositionToRow(2, 1.1, Row, Exact) && Row == 16 && !Exact);
	CHECK(!T.PositionToRow(2, 5.0, Row, Exact));
	CHECK(!T.PositionToRow(0, 1.0, Row, Exact));
	CHECK_EQ(T.BeatsToRows(1.5, Exact), 6);
	CHECK(Exact);
	CHECK_EQ(T.BeatsToRows(0.1, Exact), 1);
	CHECK(!Exact);
	CHECK_EQ(T.Position(86), std::string("bar 6 beat 2.5"));
	CHECK_EQ(T.Length(4), std::string("1 beat"));
	CHECK_EQ(T.Length(6), std::string("1.5 beats"));

	// A frame that isn't a whole number of bars: its last bar is shorter
	const Timing Odd(48, 2, 4, 32);
	CHECK_EQ(Odd.BarsPerFrame(), 2);
	CHECK_EQ(Odd.BarLength(2), 16);
	CHECK_EQ(Odd.BarStart(3), 48);
	CHECK_EQ(Odd.BarOf(47), 2);
	// No highlight: 16 rows a bar, 4 a beat
	const Timing None(64, 1, 0, 0);
	CHECK_EQ(None.RowsPerBar, 16);
	CHECK_EQ(None.RowsPerBeat, 4);
}

void TestTempo()
{
	CHECK(RowsPerMinute(6, 150, 60) == 600.0);
	CHECK(BeatsPerMinute(6, 150, 60, 4) == 150.0);
	CHECK(BeatsPerMinute(6, 120, 60, 4) == 120.0);
	int Speed = 0, Tempo = 0;
	ChooseSpeedTempo(120.0, 4, 60, 6, 32, Speed, Tempo);
	CHECK(Speed == 6 && Tempo == 120);
	ChooseSpeedTempo(90.0, 4, 60, 6, 32, Speed, Tempo);
	CHECK(Speed == 6 && Tempo == 90);
	// Too slow for speed 6 (tempo can't go below 32): another speed
	ChooseSpeedTempo(20.0, 4, 60, 6, 32, Speed, Tempo);
	CHECK(Speed != 6 && std::abs(BeatsPerMinute(Speed, Tempo, 60, 4) - 20.0) < 0.5);
	// PAL plays the same BPM with the same speed and tempo
	CHECK(BeatsPerMinute(6, 150, 50, 4) == 150.0);
}

void TestKeys()
{
	Key K;
	CHECK(ParseKey("C major", K) && K.Tonic == 0 && K.Mode == MODE_MAJOR);
	CHECK(ParseKey("Am", K) && K.Tonic == 9 && K.Mode == MODE_MINOR);
	CHECK(ParseKey("f# dorian", K) && K.Tonic == 6 && K.Mode == 1);
	CHECK(ParseKey("Bb minor", K) && K.Tonic == 10 && K.Mode == MODE_MINOR);
	CHECK(ParseKey("E flat major", K) && K.Tonic == 3);
	CHECK(ParseKey("G", K) && K.Tonic == 7 && K.Mode == MODE_MAJOR);
	CHECK(ParseKey("D harmonic minor", K) && K.Mode == 7);
	CHECK(!ParseKey("H major", K));
	CHECK(!ParseKey("C blues", K));
	CHECK_EQ(KeyName(Key(10, MODE_MAJOR)), std::string("Bb major"));
	CHECK_EQ(KeyName(Key(6, MODE_MINOR)), std::string("F# minor"));
	CHECK_EQ(KeyName(Key(2, MODE_MINOR)), std::string("D minor"));
	CHECK_EQ(KeyName(Key(5, 1)), std::string("F dorian"));
	CHECK_EQ(NoteClassName(3, Key(5, MODE_MAJOR)), std::string("Eb"));

	const Key CMajor(0, MODE_MAJOR), CMinor(0, MODE_MINOR), AMinor(9, MODE_MINOR);
	int E4 = 0, G4 = 0, F4 = 0;
	ParsePitch("E-4", E4);
	ParsePitch("G-4", G4);
	ParsePitch("F#4", F4);
	CHECK(InKey(E4, CMajor));
	CHECK(!InKey(F4, CMajor));
	CHECK(!InKey(E4, CMinor));
	int B3 = 0;
	ParsePitch("B-3", B3);
	CHECK(InKey(B3, CMinor));	// the raised 7th of minor counts

	// C major to C minor: E, A and B come down a semitone
	CHECK_EQ(MapToKey(E4, CMajor, CMinor), E4 - 1);
	CHECK_EQ(MapToKey(G4, CMajor, CMinor), G4);
	CHECK_EQ(MapToKey(E4 + 5, CMajor, CMinor), E4 + 4);	// A to Ab
	CHECK_EQ(MapToKey(B3, CMajor, CMinor), B3 - 1);
	// A note between degrees moves with the one below: F# (above F) stays above F
	CHECK_EQ(MapToKey(F4, CMajor, CMinor), F4);
	// And back
	CHECK_EQ(MapToKey(E4 - 1, CMinor, CMajor), E4);
	// To another tonic: C major to A minor moves down a minor third and keeps the shape
	int C4 = 0;
	ParsePitch("C-4", C4);
	CHECK_EQ(MapToKey(C4, CMajor, AMinor), C4 - 3);
	CHECK_EQ(MapToKey(E4, CMajor, AMinor), C4);	// the third of A minor
	// Same key: the same note
	for (int p = 0; p < 24; ++p)
		CHECK_EQ(MapToKey(p + 36, CMajor, CMajor), p + 36);
	// Lydian raises the 4th
	CHECK_EQ(MapToKey(C4 + 5, CMajor, Key(0, 3)), C4 + 6);
}

// Reading and writing a channel

void TestReadNotes()
{
	std::vector<Cell> Cells(32);
	Cells[0].SetPitch(48);
	Cells[0].Instrument = 1;
	Cells[0].Volume = 12;
	Cells[2].Effect[0] = EFF_VIBRATO;
	Cells[2].Param[0] = 0x31;
	Cells[4].SetPitch(50);
	Cells[8].Note = NOTE_RELEASE;
	Cells[12].SetPitch(52);
	Cells[12].Instrument = 2;
	const std::vector<NoteInfo> Notes = ReadNotes(Cells, 2);
	CHECK_EQ(Notes.size(), static_cast<size_t>(3));
	CHECK(Notes[0].Row == 0 && Notes[0].Rows == 4 && Notes[0].Pitch == 48 && Notes[0].Instrument == 1 && Notes[0].Volume == 12);
	CHECK(Notes[0].Effects.size() == 1 && Notes[0].Effects[0].first == 2);
	CHECK(Notes[1].Rows == 4 && Notes[1].EndNote == NOTE_RELEASE && Notes[1].Instrument == 1 && !Notes[1].InstrumentSet);
	CHECK(Notes[2].Rows == 20 && Notes[2].Instrument == 2);

	const ChannelState State = StateAt(Cells, 6, 2);
	CHECK(State.Sounding && State.Instrument == 1 && State.Volume == 12 && State.Sticky[EFF_VIBRATO] == 0x31);
	CHECK(!StateAt(Cells, 10, 2).Sounding);
	// Effects in hidden columns don't play
	Cells[2].Effect[0] = EFF_NONE;
	Cells[2].Effect[2] = EFF_VIBRATO;
	CHECK(StateAt(Cells, 6, 2).Sticky[EFF_VIBRATO] == 0);
}

PartNote MakeNote(int Row, int Rows, int Pitch, int Instrument = NO_INSTRUMENT)
{
	PartNote N;
	N.Row = Row;
	N.Rows = Rows;
	N.Pitch = Pitch;
	N.Instrument = Instrument;
	return N;
}

void TestBuildPart()
{
	PartOptions Direct, Clip;
	Direct.EffectColumns = Clip.EffectColumns = 2;
	Direct.Releases.assign(64, false);
	Direct.Releases[1] = true;
	Clip.Releases.assign(64, false);
	Clip.Releases[1] = true;
	Clip.KeepFollowing = false;
	Clip.StopAtEnd = true;
	std::vector<std::string> Warnings;
	std::vector<Cell> Out;

	{
		// Two notes and a rest on an empty channel: each ends with a halt
		std::vector<Cell> Old(64);
		std::vector<PartNote> Notes;
		Notes.push_back(MakeNote(0, 4, 48, 2));
		Notes.push_back(MakeNote(8, 8, 50));
		BuildPart(Old, 0, 16, Notes, Direct, Out, Warnings);
		CHECK_EQ(Out.size(), static_cast<size_t>(17));	// the last note's halt goes after the range
		CHECK(Out[0].Pitch() == 48 && Out[0].Instrument == 2);
		CHECK(Out[4].Note == NOTE_HALT);
		CHECK(Out[8].Pitch() == 50 && Out[8].Instrument == NO_INSTRUMENT);
		CHECK(Out[16].Note == NOTE_HALT);
		// As a clip it stays inside the range, the halt on its last row
		BuildPart(Old, 0, 16, Notes, Clip, Out, Warnings);
		CHECK_EQ(Out.size(), static_cast<size_t>(16));
		CHECK(Out[15].Note == NOTE_HALT);
	}
	{
		// Released instruments end with ===; legato notes need nothing
		std::vector<Cell> Old(64);
		std::vector<PartNote> Notes;
		Notes.push_back(MakeNote(0, 4, 48, 1));
		Notes.push_back(MakeNote(4, 4, 50));
		BuildPart(Old, 0, 16, Notes, Direct, Out, Warnings);
		CHECK(Out[4].Pitch() == 50);
		CHECK(Out[8].Note == NOTE_RELEASE);
	}
	{
		// What was there goes, but the song's jumps stay; a note held into the
		// range stops at its start; vibrato still on is turned off there
		std::vector<Cell> Old(64);
		Old[10].SetPitch(40);
		Old[10].Instrument = 1;
		Old[11].Effect[0] = EFF_VIBRATO;
		Old[11].Param[0] = 0x44;
		Old[20].SetPitch(41);
		Old[20].Volume = 3;
		Old[24].Effect[1] = EFF_JUMP;
		Old[24].Param[1] = 0x01;
		std::vector<PartNote> Notes;
		Notes.push_back(MakeNote(18, 2, 60, 2));
		BuildPart(Old, 16, 32, Notes, Direct, Out, Warnings);
		CHECK(Out[0].Note == NOTE_RELEASE);	// instrument 1 releases
		CHECK(HasEffect(Out[0], EFF_VIBRATO, 2));
		CHECK(Out[0].Effect[0] == EFF_VIBRATO && Out[0].Param[0] == 0);
		CHECK(Out[2].Pitch() == 60);
		CHECK(Out[4].Note == NOTE_HALT && Out[4].Volume == NO_VOLUME);	// the new note's end, the old note gone
		CHECK(Out[6].Note == NOTE_NONE);
		CHECK(Out[8].Effect[1] == EFF_JUMP && Out[8].Param[1] == 1);
	}
	{
		// The next note after the range plays with the instrument, volume and
		// arpeggio it had before
		std::vector<Cell> Old(64);
		Old[0].SetPitch(48);
		Old[0].Instrument = 1;
		Old[0].Volume = 9;
		Old[0].Effect[0] = EFF_ARPEGGIO;
		Old[0].Param[0] = 0x47;
		Old[40].SetPitch(50);
		std::vector<PartNote> Notes;
		Notes.push_back(MakeNote(16, 8, 55, 2));
		Notes[0].Volume = 15;
		BuildPart(Old, 16, 32, Notes, Direct, Out, Warnings);
		CHECK_EQ(Out.size(), static_cast<size_t>(25));	// up to the next note
		const Cell &Next = Out[24];
		CHECK(Next.Pitch() == 50 && Next.Instrument == 1 && Next.Volume == 9);
		CHECK(HasEffect(Next, EFF_ARPEGGIO, 2));
		// Entering the range: the arpeggio was turned off there, and the old note stopped
		CHECK(Out[0].Pitch() == 55 && HasEffect(Out[0], EFF_ARPEGGIO, 2) && Out[0].Param[0] == 0);
		// A clip leaves what follows alone
		BuildPart(Old, 16, 32, Notes, Clip, Out, Warnings);
		CHECK_EQ(Out.size(), static_cast<size_t>(16));
	}
	{
		// A note running to the range's end, with a note right after it: nothing to add
		std::vector<Cell> Old(64);
		Old[32].SetPitch(30);
		std::vector<PartNote> Notes;
		Notes.push_back(MakeNote(16, 16, 55, 2));
		BuildPart(Old, 16, 32, Notes, Direct, Out, Warnings);
		CHECK_EQ(Out.size(), static_cast<size_t>(16));
		// At the song's end, nothing either
		std::vector<Cell> Short(32);
		BuildPart(Short, 16, 32, Notes, Direct, Out, Warnings);
		CHECK_EQ(Out.size(), static_cast<size_t>(16));
	}
	{
		// Effects that don't fit in the visible columns are reported
		std::vector<Cell> Old(64);
		std::vector<PartNote> Notes;
		Notes.push_back(MakeNote(0, 8, 50, 2));
		Notes[0].Effects.push_back(std::make_pair(int(EFF_SLIDE_UP), 0x31));
		Notes[0].Effects.push_back(std::make_pair(int(EFF_VOLUME_SLIDE), 0x02));
		Notes[0].Effects.push_back(std::make_pair(int(EFF_TREMOLO), 0x22));
		std::vector<std::string> Lost;
		BuildPart(Old, 0, 16, Notes, Direct, Out, Lost);
		CHECK_EQ(Lost.size(), static_cast<size_t>(1));
	}
}

// The tools

void TestGetSong()
{
	FakeSong Song;
	// C major melody on VRC6 Pulse 1: C E G | F A C | G B D | C
	const char *const MELODY[] = { "C-4", "E-4", "G-4", "E-4", "F-4", "A-4", "C-5", "A-4", "G-4", "B-4", "D-5", "B-4", "C-5" };
	for (int i = 0; i < 13; ++i)
		Song.Put(5, i * 4, MELODY[i], i == 0 ? 0 : NO_INSTRUMENT);
	Song.End(5, 56, NOTE_RELEASE);
	Song.Put(2, 0, "C-2", 1);
	Song.End(2, 16);
	const ToolResult Result = Run(Song, "get_song", "{}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "\"Test song\"");
	CHECK_HAS(Result.Text, "150 BPM");
	CHECK_HAS(Result.Text, "8 bars (2 frames of 64 rows)");
	CHECK_HAS(Result.Text, "6 VRC6 Pulse 1: 13 notes in bars 1-4, C-4 to D-5, instruments \"Lead\" (00)");
	CHECK_HAS(Result.Text, "2 2A03 Pulse 2: empty");
	CHECK_HAS(Result.Text, "Key: C major");
	CHECK_HAS(Result.Text, "| C |");
	CHECK_HAS(Result.Text, "Changes: as clips");
	CHECK_HAS(Result.Text, "soft lead");
	Song.Info.DirectChanges = true;
	CHECK_HAS(Run(Song, "get_song", "{}").Text, "Changes: direct");
}

void TestReadNotesTool()
{
	FakeSong Song;
	Song.Put(0, 16, "C-4", 2, 12);
	Song.Rows[0][18].Effect[0] = EFF_VIBRATO;
	Song.Rows[0][18].Param[0] = 0x21;
	Song.Put(0, 22, "D#4");
	Song.End(0, 24);
	const ToolResult Result = Run(Song, "read_notes", "{\"channel\": \"pulse 1\", \"from_bar\": 1, \"to_bar\": 3}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "2A03 Pulse 1, bars 1-3");
	CHECK_HAS(Result.Text, "bar 1: rest");
	CHECK_HAS(Result.Text, "bar 2 beat 1: C-4, 1.5 beats, \"Square\" (02), volume 12, effects 421 at beat 1.5");
	CHECK_HAS(Result.Text, "bar 2 beat 2.5: D#4, 0.5 beat");
	CHECK_HAS(Result.Text, "bar 3: rest");
	// By number, and errors
	CHECK(!Run(Song, "read_notes", "{\"channel\": 1}").IsError);
	CHECK(Run(Song, "read_notes", "{\"channel\": \"Kazoo\"}").IsError);
	CHECK_HAS(Run(Song, "read_notes", "{\"channel\": 9}").Text, "1 2A03 Pulse 1");
	CHECK(Run(Song, "read_notes", "{}").IsError);
	CHECK(Run(Song, "read_notes", "{\"channel\": 1, \"from_bar\": 9}").IsError);
	// A note held into the range
	CHECK_HAS(Run(Song, "read_notes", "{\"channel\": 1, \"from_bar\": 2, \"to_bar\": 2}").Text, "bar 2 beat 1: C-4");
	FakeSong Held;
	Held.Put(0, 12, "G-3", 2);
	Held.End(0, 20);
	CHECK_HAS(Run(Held, "read_notes", "{\"channel\": 1, \"from_bar\": 2, \"to_bar\": 2}").Text, "(held from bar 1 beat 4: G-3, 1 beat more)");
}

void TestWriteNotes()
{
	const std::string Part = "{\"channel\": \"VRC6 Pulse 1\", \"instrument\": \"Lead\", \"notes\": ["
		"{\"bar\": 2, \"beat\": 1, \"pitch\": \"C-4\", \"beats\": 1},"
		"{\"bar\": 2, \"beat\": 2, \"pitch\": \"E-4\", \"beats\": 0.5, \"volume\": 10},"
		"{\"bar\": 2, \"beat\": 3, \"pitch\": \"G-4\", \"beats\": 2, \"effects\": [\"Q31\"]}]}";
	{
		// By default, a clip: the song stays as it was
		FakeSong Song;
		const ToolResult Result = Run(Song, "write_notes", Part);
		CHECK(!Result.IsError);
		CHECK_EQ(Song.Writes, 0);
		CHECK_EQ(Song.Clips.size(), static_cast<size_t>(1));
		CHECK_HAS(Result.Text, "Saved as the clip \"Claude: 3 notes on VRC6 Pulse 1, bar 2\"");
		CHECK_HAS(Result.Text, "VRC6 Pulse 1 at bar 2 beat 1 (frame 00, row 10)");
		CHECK_HAS(Result.Text, "hasn't allowed direct changes");
		const FakeSong::Clip &Clip = Song.Clips[0];
		CHECK(Clip.Columns.size() == 1 && Clip.Columns[0].size() == 16 && Clip.Sources[0] == 5);
		CHECK(Clip.Columns[0][0].Pitch() == 48 && Clip.Columns[0][0].Instrument == 0 && Clip.Columns[0][0].Volume == 15);
		CHECK(Clip.Columns[0][4].Pitch() == 52 && Clip.Columns[0][4].Volume == 10);
		CHECK(Clip.Columns[0][6].Note == NOTE_RELEASE);	// Lead releases
		CHECK(Clip.Columns[0][8].Pitch() == 55 && Clip.Columns[0][8].Effect[0] == EFF_SLIDE_UP && Clip.Columns[0][8].Param[0] == 0x31);
		CHECK(Clip.Columns[0][15].Note == NOTE_RELEASE);
		// A second one gets its own name
		Run(Song, "write_notes", Part);
		CHECK_EQ(Song.Clips[1].Name, std::string("Claude: 3 notes on VRC6 Pulse 1, bar 2 2"));
	}
	{
		// Direct changes write into the song, readable again
		FakeSong Song;
		Song.Info.DirectChanges = true;
		Song.Put(5, 40, "A-3", 0);
		const ToolResult Result = Run(Song, "write_notes", Part);
		CHECK(!Result.IsError);
		CHECK_HAS(Result.Text, "Changed the song: 3 notes on VRC6 Pulse 1, bar 2. One Ctrl+Z");
		CHECK_EQ(Song.Writes, 1);
		CHECK_EQ(Notes(Song, 5), std::string("16:C-4 20:E-4 22:=== 24:G-4 32:=== 40:A-3 "));
		const std::string Read = Run(Song, "read_notes", "{\"channel\": 6, \"to_bar\": 2}").Text;
		CHECK_HAS(Read, "bar 2 beat 3: G-4, 2 beats");
		CHECK_HAS(Read, "effects Q31");
		// Asked for as a clip anyway
		CHECK_HAS(Run(Song, "write_notes", Part.substr(0, Part.size() - 1) + ", \"as_clip\": true}").Text, "Saved as the clip");
		CHECK_EQ(Song.Writes, 1);
		// Undo goes back
		CHECK(!Run(Song, "undo", "{}").IsError);
		CHECK_EQ(Notes(Song, 5), std::string("40:A-3 "));
		// Not while a dialog is open
		Song.Busy = true;
		CHECK_HAS(Run(Song, "write_notes", Part).Text, "A dialog is open");
	}
	{
		// Drums by name on the noise channel get drum instruments
		FakeSong Song;
		Song.Info.DirectChanges = true;
		const ToolResult Result = Run(Song, "write_notes", "{\"channel\": \"noise\", \"notes\": ["
			"{\"bar\": 1, \"beat\": 1, \"pitch\": \"kick\", \"beats\": 0.5},"
			"{\"bar\": 1, \"beat\": 2, \"pitch\": \"snare\", \"beats\": 0.5},"
			"{\"bar\": 1, \"beat\": 2.5, \"pitch\": \"hihat\", \"beats\": 0.25}]}");
		CHECK(!Result.IsError);
		CHECK_HAS(Result.Text, "Added the instrument \"Kick\" (03) (2A03) from the preset \"kick\"");
		CHECK_EQ(Notes(Song, 3, 0, 16), std::string("0:4-# 2:--- 4:A-# 6:E-# 7:--- "));
	}
	{
		// Mistakes come back as errors Claude can fix
		FakeSong Song;
		CHECK_HAS(Run(Song, "write_notes", "{\"channel\": 1, \"notes\": [{\"bar\": 1, \"pitch\": \"X-4\", \"beats\": 1}]}").Text,
			"isn't a note");
		CHECK_HAS(Run(Song, "write_notes", "{\"channel\": 1, \"notes\": [{\"bar\": 1, \"pitch\": \"C-4\"}]}").Text, "length in beats");
		CHECK_HAS(Run(Song, "write_notes", "{\"channel\": 1, \"notes\": [{\"bar\": 1, \"beat\": 5, \"pitch\": \"C-4\", \"beats\": 1}]}").Text,
			"past the end of bar 1");
		CHECK_HAS(Run(Song, "write_notes", "{\"channel\": 1, \"notes\": [{\"bar\": 1, \"pitch\": \"C-4\", \"beats\": 1}, "
			"{\"bar\": 1, \"pitch\": \"D-4\", \"beats\": 1}]}").Text, "Two notes start");
		CHECK_HAS(Run(Song, "write_notes", "{\"channel\": 1, \"notes\": [{\"bar\": 1, \"pitch\": \"C-4\", \"beats\": 1}]}").Text,
			"no instrument to play these notes with");
		CHECK_HAS(Run(Song, "write_notes", "{\"channel\": 1, \"instrument\": \"Lead\", \"notes\": [{\"bar\": 1, \"pitch\": \"C-4\", "
			"\"beats\": 1}]}").Text, "is a VRC6 instrument; 2A03 Pulse 1 needs a 2A03 one");
		CHECK_HAS(Run(Song, "write_notes", "{\"channel\": 1, \"instrument\": \"kick\", \"notes\": []}").Text, "drum for the noise");
		CHECK_HAS(Run(Song, "write_notes", "{\"channel\": \"dpcm\", \"notes\": []}").Text, "DPCM channel plays samples");
		CHECK(Song.Clips.empty());
		// Overlaps are shortened, with a note about it
		const ToolResult Overlap = Run(Song, "write_notes", "{\"channel\": 1, \"instrument\": \"Square\", \"notes\": ["
			"{\"bar\": 1, \"pitch\": \"C-4\", \"beats\": 2}, {\"bar\": 1, \"beat\": 2, \"pitch\": \"D-4\", \"beats\": 1}]}");
		CHECK(!Overlap.IsError);
		CHECK_HAS(Overlap.Text, "overlapped the next one");
		// A preset as the instrument is added for the channel's chip
		const ToolResult Preset = Run(Song, "write_notes", "{\"channel\": \"sawtooth\", \"instrument\": \"plucky bass\", \"notes\": ["
			"{\"bar\": 1, \"pitch\": \"C-2\", \"beats\": 1}]}");
		CHECK(!Preset.IsError);
		CHECK_HAS(Preset.Text, "\"Plucky bass (saw)\"");
		CHECK_EQ(Song.Info.Instruments.back().Chip, std::string("VRC6"));
		// Clearing bars: no notes, a range
		FakeSong Clear;
		Clear.Info.DirectChanges = true;
		Clear.Put(0, 20, "C-4", 2);
		Clear.Put(0, 40, "D-4");
		CHECK(!Run(Clear, "write_notes", "{\"channel\": 1, \"from_bar\": 2, \"to_bar\": 2, \"notes\": []}").IsError);
		CHECK_EQ(Notes(Clear, 0), std::string("40:D-4 "));
		CHECK(Clear.Rows[0][40].Instrument == 2);	// D-4 keeps its instrument
	}
	{
		// Past the end of the song: frames are added
		FakeSong Song;
		Song.Info.DirectChanges = true;
		CHECK(!Run(Song, "write_notes", "{\"channel\": 1, \"instrument\": \"Square\", \"notes\": [{\"bar\": 10, \"pitch\": \"C-4\", \"beats\": 4}]}").IsError);
		CHECK_EQ(Song.Rows[0].size(), static_cast<size_t>(192));
		CHECK(Song.Rows[0][144].Pitch() == 48);
	}
}

void TestPlaceClip()
{
	FakeSong Song;
	Run(Song, "write_notes", "{\"channel\": 6, \"instrument\": \"Lead\", \"notes\": [{\"bar\": 1, \"pitch\": \"C-4\", \"beats\": 1}]}");
	CHECK_HAS(Run(Song, "place_clip", "{\"clip\": 1, \"channel\": 7, \"bar\": 3}").Text, "hasn't allowed direct changes");
	Song.Info.DirectChanges = true;
	const ToolResult Result = Run(Song, "place_clip", "{\"clip\": \"Claude: 1 note on VRC6 Pulse 1, bar 1\", \"channel\": 7, \"bar\": 3, \"linked\": true}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "Placed the clip \"Claude: 1 note on VRC6 Pulse 1, bar 1\" linked on VRC6 Pulse 2 at bar 3 beat 1");
	CHECK_EQ(Song.LastLink, Song.Clips[0].Id);
	CHECK(Song.Rows[6][32].Pitch() == 48);
	CHECK(Run(Song, "place_clip", "{\"clip\": 5, \"channel\": 1, \"bar\": 1}").IsError);
}

void TestInstruments()
{
	FakeSong Song;
	ToolResult Result = Run(Song, "add_instrument", "{\"preset\": \"soft lead\", \"channel\": \"VRC6 Pulse 2\"}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "Added \"Soft lead\" (03) (VRC6 instrument");
	Result = Run(Song, "add_instrument", "{\"preset\": \"Flute\", \"chip\": \"vrc6\"}");
	CHECK_HAS(Result.Text, "already has \"Soft lead\" (03)");
	CHECK(Run(Song, "add_instrument", "{\"preset\": \"kazoo\"}").IsError);
	CHECK_HAS(Run(Song, "add_instrument", "{\"preset\": \"snare\", \"channel\": 1}").Text, "drum for the noise channel");

	// Presets: every one has a volume envelope in range and sane points
	for (size_t i = 0; i < Presets().size(); ++i) {
		const Preset &P = Presets()[i];
		CHECK(FindPreset(P.Name) == &P);
		for (int Chip = 0; Chip < 2; ++Chip) {
			const std::vector<PresetSequence> &List = Chip == 0 ? P.Sequences2A03 : P.SequencesVRC6;
			if (Chip == 1 && P.Drum) {
				CHECK(List.empty());
				continue;
			}
			bool Volume = false;
			for (size_t s = 0; s < List.size(); ++s) {
				const PresetSequence &Seq = List[s];
				CHECK(!Seq.Items.empty() && Seq.Items.size() <= 252);
				CHECK(Seq.Release < static_cast<int>(Seq.Items.size()) && Seq.Loop < static_cast<int>(Seq.Items.size()));
				for (size_t k = 0; k < Seq.Items.size(); ++k) {
					if (Seq.Type == SEQUENCE_VOLUME)
						CHECK(Seq.Items[k] >= 0 && Seq.Items[k] <= 15);
					if (Seq.Type == SEQUENCE_DUTY)
						CHECK(Seq.Items[k] >= 0 && Seq.Items[k] <= (Chip == 0 ? 3 : 7));
				}
				if (Seq.Type == SEQUENCE_VOLUME) {
					Volume = true;
					CHECK(Seq.Items.back() == 0);	// fades to silence
				}
			}
			CHECK(Volume);
		}
	}
	CHECK(FindPreset("Pad") != 0 && FindPreset("Pad")->Name == "warm pad");
	CHECK(FindPreset("hi hat") != 0);

	// set_instrument changes every note in the bars
	Song.Info.DirectChanges = true;
	Song.Put(0, 0, "C-4", 2);
	Song.Put(0, 4, "D-4");
	Song.Put(0, 20, "E-4");
	Result = Run(Song, "set_instrument", "{\"channel\": 1, \"instrument\": \"bright lead\", \"to_bar\": 1}");
	CHECK(!Result.IsError);
	const int Bright = Song.Info.Instruments.back().Index;
	CHECK(Song.Rows[0][0].Instrument == Bright && Song.Rows[0][4].Instrument == Bright);
	CHECK(Song.Rows[0][20].Instrument == 2);	// the next note keeps playing with the old one
	CHECK_HAS(Result.Text, "(2 notes)");
}

void TestTranspose()
{
	FakeSong Song;
	Song.Info.DirectChanges = true;
	const char *const MELODY[] = { "C-4", "D-4", "E-4", "F-4", "G-4", "A-4", "B-4", "C-5" };
	for (int i = 0; i < 8; ++i)
		Song.Put(5, i * 4, MELODY[i], i == 0 ? 0 : NO_INSTRUMENT);
	Song.Put(1, 0, "C-3", 2);
	Song.Rows[1][0].Effect[0] = EFF_ARPEGGIO;
	Song.Rows[1][0].Param[0] = 0x47;	// C major chord
	Song.Put(3, 0, "kick", 1);

	ToolResult Result = Run(Song, "transpose", "{\"channels\": [\"VRC6 Pulse 1\"], \"semitones\": 12, \"as_clip\": true}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "up 12 semitones, 1 channel, bars 1-8");
	CHECK(Song.Clips.size() == 1 && Song.Clips[0].Columns[0][0].Pitch() == 60);
	CHECK(Song.Rows[5][0].Pitch() == 48);	// the song stays

	Result = Run(Song, "transpose", "{\"to_mode\": \"minor\", \"key\": \"C major\"}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "from C major to C minor, 2 channels");
	CHECK_HAS(Result.Text, "E to Eb");
	CHECK_EQ(Notes(Song, 5, 0, 32), std::string("0:C-4 4:D-4 8:D#4 12:F-4 16:G-4 20:G#4 24:A#4 28:C-5 "));
	CHECK(Song.Rows[1][0].Param[0] == 0x37);	// the chord turned minor
	CHECK(Song.Rows[3][0].Pitch() == 4);	// drums stay
	CHECK_EQ(Song.Writes, 1);	// one undoable change for all channels

	CHECK(Run(Song, "transpose", "{\"semitones\": 2, \"to_mode\": \"major\"}").IsError);
	CHECK(Run(Song, "transpose", "{}").IsError);
	CHECK(Run(Song, "transpose", "{\"channels\": \"noise\", \"semitones\": 1}").IsError);
	CHECK(Run(Song, "transpose", "{\"to_key\": \"H minor\"}").IsError);
	CHECK_HAS(Run(Song, "transpose", "{\"key\": \"C minor\", \"to_mode\": \"minor\"}").Text, "already in C minor. Pick another");
	CHECK_HAS(Run(Song, "transpose", "{\"channels\": 2, \"semitones\": 1, \"from_bar\": 3}").Text, "No notes to transpose in bars 3-8");
	Result = Run(Song, "transpose", "{\"channels\": 6, \"semitones\": 40}");
	CHECK_HAS(Result.Text, "would have left FamiTracker's range");
}

void TestTempoTool()
{
	FakeSong Song;
	CHECK_HAS(Run(Song, "set_tempo", "{\"bpm\": 120}").Text, "speed and tempo boxes");
	Song.Info.DirectChanges = true;
	ToolResult Result = Run(Song, "set_tempo", "{\"bpm\": 120}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "Tempo set to 120 BPM (speed 6, tempo 120); it was 150 BPM");
	CHECK(Song.Info.Speed == 6 && Song.Info.Tempo == 120);
	Song.Rows[2][70].Effect[0] = EFF_SPEED;
	Song.Rows[2][70].Param[0] = 0x05;
	CHECK_HAS(Run(Song, "set_tempo", "{\"bpm\": 100}").Text, "F05 effect at bar 5 beat 2.5 on 2A03 Triangle");
	CHECK(Run(Song, "set_tempo", "{}").IsError);
}

void TestBacking()
{
	FakeSong Song;
	Song.Info.DirectChanges = true;
	// C major melody over 4 bars on VRC6 Pulse 1
	const char *const MELODY[] = { "E-4", "G-4", "C-5", "G-4", "F-4", "A-4", "C-5", "A-4", "D-4", "G-4", "B-4", "G-4", "C-4", "E-4", "G-4", "C-5" };
	for (int i = 0; i < 16; ++i)
		Song.Put(5, i * 4, MELODY[i], i == 0 ? 0 : NO_INSTRUMENT);
	Song.End(5, 64, NOTE_RELEASE);
	const ToolResult Result = Run(Song, "add_chords_and_bass", "{}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "chords on 2A03 Pulse 1 and a bass line on 2A03 Triangle, bars 1-4, in C major");
	CHECK_HAS(Result.Text, "Chords: | C");
	// Chords: notes with arpeggios; bass: notes in a low register
	CHECK(Song.Rows[0][0].HasNote() && Song.Rows[0][0].Effect[0] == EFF_ARPEGGIO);
	CHECK(Song.Rows[2][0].HasNote() && Song.Rows[2][0].Pitch() < 36);
	CHECK_EQ(Song.Writes, 1);
	bool Soft = false;
	for (size_t i = 0; i < Song.Info.Instruments.size(); ++i)
		Soft = Soft || Song.Info.Instruments[i].Name == "Soft chords";
	CHECK(Soft);
	// With every channel busy it says so
	FakeSong Busy;
	Busy.Info.DirectChanges = true;
	for (int c = 0; c < 8; ++c)
		if (c != 3 && c != 4)
			Busy.Put(c, 0, "C-4", c < 5 ? 2 : 0);
	CHECK_HAS(Run(Busy, "add_chords_and_bass", "{\"melody_channel\": 6}").Text, "already plays in those bars");
	CHECK(!Run(Busy, "add_chords_and_bass", "{\"melody_channel\": 6, \"chord_channel\": \"none\", \"bass_channel\": \"sawtooth\"}").IsError);
}

void TestEchoAndVibrato()
{
	FakeSong Song;
	Song.Info.DirectChanges = true;
	Song.Put(5, 0, "C-4", 0, 12);
	Song.Put(5, 8, "E-4");
	Song.End(5, 16, NOTE_RELEASE);
	ToolResult Result = Run(Song, "add_echo", "{\"channel\": \"VRC6 Pulse 1\", \"to_bar\": 1}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "an echo of VRC6 Pulse 1 on VRC6 Pulse 2, 0.75 beats later at volume 5");
	CHECK(Song.Rows[6][3].Pitch() == 48 && Song.Rows[6][3].Instrument == 0 && Song.Rows[6][3].Volume == 4);
	CHECK(Song.Rows[6][11].Pitch() == 52);
	CHECK(Song.Rows[6][19].Note == NOTE_RELEASE);
	CHECK_HAS(Run(Song, "add_echo", "{\"channel\": 1}").Text, "has no notes");
	{
		// At the song's end the echo stops: the song doesn't grow (which would move its loop)
		FakeSong Tail;
		Tail.Info.DirectChanges = true;
		Tail.Put(5, 120, "G-4", 0);
		Tail.Put(5, 124, "A-4");
		Tail.Put(5, 126, "B-4");	// starts too late for an echo before the end
		CHECK(!Run(Tail, "add_echo", "{\"channel\": \"VRC6 Pulse 1\"}").IsError);
		CHECK_EQ(Tail.Rows[6].size(), static_cast<size_t>(128));
		CHECK_EQ(Notes(Tail, 6), std::string("123:G-4 127:A-4 "));
		CHECK_HAS(Run(Tail, "add_echo", "{\"channel\": \"VRC6 Pulse 1\", \"to_channel\": \"VRC6 Pulse 2\", \"from_bar\": 8}").Text,
			"The echo is already there");
	}

	Song.Put(0, 0, "C-4", 2);
	Song.Put(0, 2, "D-4");
	Song.End(0, 4);
	Song.Put(0, 16, "E-4");
	Song.End(0, 32);
	Result = Run(Song, "add_vibrato", "{\"channel\": 1, \"speed\": \"slow\", \"depth\": \"strong\"}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "vibrato 437");
	CHECK(HasEffectAt(Song, 0, 17, EFF_VIBRATO, 0x37));	// a quarter beat in
	CHECK(HasEffectAt(Song, 0, 31, EFF_VIBRATO, 0x00));	// off on its last row
	CHECK(!HasEffectAt(Song, 0, 1, EFF_VIBRATO, 0x37));	// too short
	CHECK_HAS(Result.Text, "(1 note)");
	Result = Run(Song, "add_vibrato", "{\"channel\": 1, \"depth\": \"none\"}");
	CHECK(!Result.IsError);
	CHECK(!HasEffectAt(Song, 0, 17, EFF_VIBRATO, 0x37));
	CHECK(Run(Song, "add_vibrato", "{\"channel\": \"noise\"}").IsError);
}

// change_mood: every step on one working copy, so they arrive together
void TestMood()
{
	FakeSong Song;
	const char *const MELODY[] = { "C-4", "D-4", "E-4", "F-4", "G-4", "A-4", "B-4", "C-5" };
	for (int i = 0; i < 8; ++i)
		Song.Put(5, i * 4, MELODY[i], i == 0 ? 0 : NO_INSTRUMENT);
	Song.End(5, 32, NOTE_RELEASE);
	Song.Put(2, 0, "C-3", 1);
	Song.End(2, 32);
	const std::vector<std::vector<Cell> > Before = Song.Rows;
	const std::string Underwater = "{\"mood\": \"underwater\", \"key\": \"C major\", \"to_mode\": \"minor\", "
		"\"moves\": [{\"channel\": \"Triangle\", \"semitones\": -12}], "
		"\"sounds\": [{\"channel\": \"VRC6 Pulse 1\", \"instrument\": \"warm pad\"}], "
		"\"vibrato\": {\"channel\": \"VRC6 Pulse 1\", \"speed\": \"slow\", \"depth\": \"strong\"}, "
		"\"echo\": {\"channel\": \"VRC6 Pulse 1\"}, \"tempo_percent\": 80, \"to_bar\": 2}";

	// As a clip (the default): one clip with all of it, the song as it was
	ToolResult Result = Run(Song, "change_mood", Underwater);
	CHECK(!Result.IsError);
	CHECK(Song.Rows == Before);
	CHECK(Song.Info.Tempo == 150 && Song.TempoUndo.empty());
	CHECK_EQ(Song.Clips.size(), static_cast<size_t>(1));
	CHECK_EQ(Song.Clips[0].Name, std::string("Claude: underwater, bars 1-2"));
	CHECK_HAS(Result.Text, "from C major to C minor (E to Eb, A to Ab, B to Bb), 2A03 Triangle down 1 octave, VRC6 Pulse 1 played by "
		"\"Warm pad\" (03), vibrato 437 on VRC6 Pulse 1 and an echo of VRC6 Pulse 1 on VRC6 Pulse 2, bars 1-2");
	CHECK_HAS(Result.Text, "put the pattern cursor on 2A03 Triangle at bar 1 beat 1");
	CHECK_HAS(Result.Text, "Meant to play at 120 BPM: suggest the user sets speed 6 and tempo 120");
	const std::vector<std::vector<Cell> > &Clip = Song.Clips[0].Columns;
	CHECK_EQ(Clip.size(), static_cast<size_t>(5));	// Triangle to VRC6 Pulse 2
	CHECK(Song.Clips[0].Sources[0] == 2 && Song.Clips[0].Sources[4] == 6);
	CHECK(Clip[0][0].Pitch() == 24);	// the bass an octave lower
	CHECK(Clip[3][8].Pitch() == 51 && Clip[3][8].Instrument == 3);	// E-4 became D#4, played by the pad
	CHECK(Clip[3][1].Effect[0] == EFF_VIBRATO && Clip[3][1].Param[0] == 0x37);
	CHECK(Clip[1][0].IsEmpty() && Clip[2][0].IsEmpty());	// noise and DPCM come along as they are
	CHECK(Clip[4][3].Pitch() == 48 && Clip[4][3].Instrument == 3 && Clip[4][3].Volume == 5);	// the echo of the changed part
	CHECK(Clip[4][11].Pitch() == 51);
	CHECK(Clip[4][4].Effect[0] == EFF_VIBRATO && Clip[4][4].Param[0] == 0x37);
	CHECK(Clip[4].size() == 36 && Clip[4][35].Note == NOTE_RELEASE);

	// Directly: one undoable change for the notes, another for the tempo
	Song.Info.DirectChanges = true;
	Result = Run(Song, "change_mood", Underwater);
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "Changed the song: from C major to C minor");
	CHECK_HAS(Result.Text, "The tempo is now 120 BPM (speed 6, tempo 120), its own Ctrl+Z step.");
	CHECK_EQ(Song.Writes, 1);
	CHECK(Song.Info.Tempo == 120 && Song.TempoUndo.size() == 1);
	CHECK_EQ(Notes(Song, 5, 0, 33), std::string("0:C-4 4:D-4 8:D#4 12:F-4 16:G-4 20:G#4 24:A#4 28:C-5 32:=== "));
	CHECK(Song.Rows[2][0].Pitch() == 24 && Song.Rows[6][3].Pitch() == 48 && Song.Rows[6][35].Note == NOTE_RELEASE);
	CHECK_EQ(Song.Clips.size(), static_cast<size_t>(1));
	CHECK(Run(Song, "undo", "{}").Text == "Undid it.");
	CHECK(Song.Rows == Before);	// one Ctrl+Z takes all of the notes back

	// Mistakes change nothing, not even the instruments
	const size_t Instruments = Song.Info.Instruments.size();
	Result = Run(Song, "change_mood", "{\"sounds\": [{\"channel\": 6, \"instrument\": \"bell\"}], \"moves\": [{\"channel\": \"noise\", \"semitones\": 1}]}");
	CHECK(Result.IsError);
	CHECK_HAS(Result.Text, "2A03 Noise has no notes to move up or down");
	CHECK_EQ(Song.Info.Instruments.size(), Instruments);
	CHECK_HAS(Run(Song, "change_mood", "{\"moves\": [{\"channel\": 3}]}").Text, "Each move needs a channel and semitones");
	CHECK_HAS(Run(Song, "change_mood", "{\"sounds\": {\"channel\": 6}}").Text, "sounds should be a list");
	CHECK_HAS(Run(Song, "change_mood", "{\"vibrato\": [1]}").Text, "vibrato should be like");
	CHECK_HAS(Run(Song, "change_mood", "{\"key\": \"C minor\", \"to_mode\": \"minor\"}").Text, "already in C minor");
	Result = Run(Song, "change_mood", "{\"key\": \"C major\", \"to_mode\": \"mixolydian\", \"to_bar\": 1}");
	CHECK(Result.IsError);	// C major and C mixolydian differ in B, which bar 1 doesn't play
	CHECK_HAS(Result.Text, "Nothing changed in bar 1");
	CHECK_HAS(Result.Text, "The notes stayed as they were in C mixolydian");
	CHECK_HAS(Run(Song, "change_mood", "{}").Text, "Nothing changed in bars 1-8");
	CHECK(Song.Rows == Before && Song.Writes == 1);

	// An echo of nothing is a remark, the rest still happens
	Result = Run(Song, "change_mood", "{\"moves\": [{\"channel\": \"VRC6 Pulse 1\", \"semitones\": 12}], \"echo\": {\"channel\": \"Pulse 2\"}, "
		"\"to_bar\": 2}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "VRC6 Pulse 1 up 1 octave");
	CHECK_HAS(Result.Text, "2A03 Pulse 2 has no notes there to echo");
	CHECK(Song.Rows[5][0].Pitch() == 60);

	// The tempo alone, as a clip: a suggestion
	FakeSong Plain;
	Result = Run(Plain, "change_mood", "{\"bpm\": 120}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "suggest the user sets speed 6 and tempo 120");
	CHECK(Plain.Clips.empty() && Plain.Info.Tempo == 150);
}

void TestCheck()
{
	FakeSong Song;
	Song.Put(5, 0, "C-4", 0);
	Song.Put(5, 4, "E-4");
	Song.Put(5, 8, "G-4");
	Song.Put(5, 12, "C-5");
	Song.Put(5, 16, "F#4");	// outside C major
	Song.End(5, 20);
	Song.Put(6, 0, "C#4", 0);	// a semitone against C-4 for a beat
	Song.End(6, 4);
	Song.Put(0, 48, "G-0", 2);	// below a 2A03 pulse's lowest
	Song.End(0, 52);
	const ToolResult Result = Run(Song, "check_song", "{\"key\": \"C major\"}");
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "Outside C major: F#4 at VRC6 Pulse 1 bar 2 beat 1");
	CHECK_HAS(Result.Text, "Clash: C-4 on VRC6 Pulse 1 against C#4 on VRC6 Pulse 2 at bar 1 beat 1 for 1 beat");
	CHECK_HAS(Result.Text, "Range: G-0 at 2A03 Pulse 1 bar 4 beat 1 is below A-0");
	CHECK_HAS(Result.Text, "Silent: nothing plays in bar 3");
	CHECK_HAS(Result.Text, "Unused channels");
	CHECK_HAS(Result.Text, "2A03 Pulse 2");
}

void TestPlayback()
{
	FakeSong Song;
	CHECK(!Run(Song, "play", "{\"from_bar\": 2, \"bars\": 2}").IsError);
	CHECK(Song.PlayFirst == 16 && Song.PlayRows == 32);
	CHECK(!Run(Song, "play", "{}").IsError);
	CHECK(Song.PlayFirst == 0 && Song.PlayRows == 0);
	CHECK(!Run(Song, "stop", "{}").IsError && Song.Stopped);
	CHECK_HAS(Run(Song, "undo", "{}").Text, "hasn't allowed direct changes");
	CHECK_HAS(Run(Song, "save_song", "{\"copy_name\": \"Darker\"}").Text, "Saved Darker.ftm");
	CHECK(Run(Song, "save_song", "{\"copy_name\": \"..\\\\x\"}").IsError);
	CHECK(Run(Song, "nonsense", "{}").IsError);
}

// The protocol

HttpRequest Post(const std::string &Body, const std::string &Token = "secret")
{
	HttpRequest Request;
	Request.Method = "POST";
	Request.Target = "/mcp";
	Request.Version = "HTTP/1.1";
	Request.Headers.push_back(std::make_pair(std::string("Host"), std::string("127.0.0.1:46600")));
	Request.Headers.push_back(std::make_pair(std::string("Content-Type"), std::string("application/json")));
	Request.Headers.push_back(std::make_pair(std::string("Accept"), std::string("application/json, text/event-stream")));
	if (!Token.empty())
		Request.Headers.push_back(std::make_pair(std::string("Authorization"), "Bearer " + Token));
	Request.Body = Body;
	return Request;
}

HttpRequest ModernPost(const std::string &Method, const std::string &Params, const std::string &Name = std::string(),
	const std::string &Version = MCP_MODERN_VERSION)
{
	HttpRequest Request = Post("{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"" + Method + "\",\"params\":{" + Params +
		(Params.empty() ? "" : ",") + "\"_meta\":{\"io.modelcontextprotocol/protocolVersion\":\"" + Version + "\","
		"\"io.modelcontextprotocol/clientCapabilities\":{}}}}");
	Request.Headers.push_back(std::make_pair(std::string("MCP-Protocol-Version"), Version));
	Request.Headers.push_back(std::make_pair(std::string("Mcp-Method"), Method));
	if (!Name.empty())
		Request.Headers.push_back(std::make_pair(std::string("Mcp-Name"), Name));
	return Request;
}

Json Body(const HttpResponse &Response)
{
	return Parse(Response.Body);
}

int ErrorCode(const HttpResponse &Response)
{
	const Json Message = Body(Response);
	const Json *pError = Message.Get("error");
	const Json *pCode = pError != 0 ? pError->Get("code") : 0;
	return pCode != 0 ? static_cast<int>(pCode->Integer()) : 0;
}

void TestProtocol()
{
	FakeSong Song;
	Song.Put(5, 0, "C-4", 0);
	ToolSet Tools(Song);
	McpConfig Config;
	Config.Port = 46600;
	Config.Token = "secret";
	Config.Version = "0.4.6-bf";

	// The handshake era: initialize, initialized, tools
	HttpResponse Response = HandleMcp(Post("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{"
		"\"protocolVersion\":\"2025-06-18\",\"capabilities\":{},\"clientInfo\":{\"name\":\"claude-code\",\"version\":\"2\"}}}"),
		Config, Tools);
	CHECK_EQ(Response.Status, 200);
	CHECK_EQ(Response.ContentType, std::string("application/json"));
	Json Message = Body(Response);
	CHECK(Message.Get("id") != 0 && Message.Get("id")->Integer() == 1);
	const Json *pResult = Message.Get("result");
	CHECK(pResult != 0 && pResult->Get("protocolVersion")->String() == "2025-06-18");
	CHECK(pResult->Get("serverInfo")->Get("name")->String() == "famitracker");
	CHECK(pResult->Get("capabilities")->Get("tools") != 0);
	CHECK_HAS(pResult->Get("instructions")->String(), "C-3 is middle C");
	// An unknown version gets ours
	Response = HandleMcp(Post("{\"jsonrpc\":\"2.0\",\"id\":\"a\",\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2024-01-01\"}}"),
		Config, Tools);
	CHECK(Body(Response).Get("result")->Get("protocolVersion")->String() == MCP_LEGACY_VERSION);
	CHECK(Body(Response).Get("id")->String() == "a");

	Response = HandleMcp(Post("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}"), Config, Tools);
	CHECK_EQ(Response.Status, 202);
	CHECK(Response.Body.empty());

	HttpRequest List = Post("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\"}");
	List.Headers.push_back(std::make_pair(std::string("MCP-Protocol-Version"), std::string("2025-06-18")));
	Response = HandleMcp(List, Config, Tools);
	Message = Body(Response);
	const Json *pTools = Message.Get("result")->Get("tools");
	CHECK(pTools != 0 && pTools->Size() == Tools.List().size() && pTools->Size() >= 15);
	CHECK(pTools->At(0).Get("name")->String() == "get_song");
	CHECK(pTools->At(0).Get("inputSchema")->Get("type")->String() == "object");
	CHECK(pTools->At(0).Get("annotations")->Get("readOnlyHint")->Bool());
	CHECK(Message.Get("result")->Get("resultType") == 0);	// not in the old shape
	// Every tool's schema is an object with properties, descriptions and no surprises
	for (size_t i = 0; i < pTools->Size(); ++i) {
		const Json &Tool = pTools->At(i);
		CHECK(!Tool.Get("description")->String().empty());
		const Json *pProperties = Tool.Get("inputSchema")->Get("properties");
		CHECK(pProperties != 0 && pProperties->IsObject());
		for (size_t p = 0; p < pProperties->Members().size(); ++p)
			CHECK(pProperties->Members()[p].second.Get("description") != 0 || pProperties->Members()[p].first == "notes");
	}

	Response = HandleMcp(Post("{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\",\"params\":{\"name\":\"get_song\",\"arguments\":{}}}"),
		Config, Tools);
	Message = Body(Response);
	CHECK(!Message.Get("result")->Get("isError")->Bool());
	CHECK_HAS(Message.Get("result")->Get("content")->At(0).Get("text")->String(), "VRC6 Pulse 1: 1 note");
	// A tool's own failure is a result with isError, an unknown tool a protocol error
	Response = HandleMcp(Post("{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"tools/call\",\"params\":{\"name\":\"read_notes\",\"arguments\":{}}}"),
		Config, Tools);
	CHECK(Body(Response).Get("result")->Get("isError")->Bool());
	Response = HandleMcp(Post("{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"tools/call\",\"params\":{\"name\":\"nope\"}}"), Config, Tools);
	CHECK_EQ(ErrorCode(Response), -32602);
	Response = HandleMcp(Post("{\"jsonrpc\":\"2.0\",\"id\":6,\"method\":\"resources/list\"}"), Config, Tools);
	CHECK_EQ(ErrorCode(Response), -32601);
	CHECK_EQ(Response.Status, 200);
	Response = HandleMcp(Post("{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"ping\"}"), Config, Tools);
	CHECK(Body(Response).Get("result")->IsObject());

	// Per-request metadata (2026-07-28)
	Response = HandleMcp(ModernPost("server/discover", ""), Config, Tools);
	CHECK_EQ(Response.Status, 200);
	Message = Body(Response);
	CHECK(Message.Get("result")->Get("resultType")->String() == "complete");
	CHECK(Message.Get("result")->Get("supportedVersions")->At(0).String() == MCP_MODERN_VERSION);
	CHECK(Message.Get("result")->Get("_meta")->Get("io.modelcontextprotocol/serverInfo")->Get("version")->String() == "0.4.6-bf");
	Response = HandleMcp(ModernPost("tools/list", ""), Config, Tools);
	Message = Body(Response);
	CHECK(Message.Get("result")->Get("tools")->Size() == Tools.List().size());
	CHECK(Message.Get("result")->Get("ttlMs") != 0 && Message.Get("result")->Get("cacheScope")->String() == "private");
	Response = HandleMcp(ModernPost("tools/call", "\"name\":\"get_song\",\"arguments\":{}", "get_song"), Config, Tools);
	Message = Body(Response);
	CHECK(Message.Get("result")->Get("resultType")->String() == "complete");
	CHECK_HAS(Message.Get("result")->Get("content")->At(0).Get("text")->String(), "Test song");
	// The name header may come in base64
	Response = HandleMcp(ModernPost("tools/call", "\"name\":\"get_song\",\"arguments\":{}", "=?base64?Z2V0X3Nvbmc=?="), Config, Tools);
	CHECK_EQ(Response.Status, 200);
	// Headers that don't match the body
	Response = HandleMcp(ModernPost("tools/call", "\"name\":\"get_song\",\"arguments\":{}", "stop"), Config, Tools);
	CHECK(Response.Status == 400 && ErrorCode(Response) == -32020);
	Response = HandleMcp(ModernPost("tools/call", "\"name\":\"get_song\",\"arguments\":{}"), Config, Tools);
	CHECK(Response.Status == 400 && ErrorCode(Response) == -32020);
	HttpRequest Mismatch = ModernPost("tools/list", "");
	Mismatch.Headers.back().second = "tools/call";
	CHECK_EQ(ErrorCode(HandleMcp(Mismatch, Config, Tools)), -32020);
	// A version we don't speak
	Response = HandleMcp(ModernPost("tools/list", "", "", "2099-01-01"), Config, Tools);
	CHECK(Response.Status == 400 && ErrorCode(Response) == -32022);
	CHECK(Body(Response).Get("error")->Get("data")->Get("supported")->Size() == 4);
	// Unknown methods: 404
	Response = HandleMcp(ModernPost("subscriptions/listen", ""), Config, Tools);
	CHECK(Response.Status == 404 && ErrorCode(Response) == -32601);
	// A modern header on a request without _meta
	HttpRequest NoMeta = Post("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\"}");
	NoMeta.Headers.push_back(std::make_pair(std::string("MCP-Protocol-Version"), std::string(MCP_MODERN_VERSION)));
	CHECK_EQ(HandleMcp(NoMeta, Config, Tools).Status, 400);
	NoMeta.Headers.back().second = "1999-01-01";
	CHECK_EQ(HandleMcp(NoMeta, Config, Tools).Status, 400);

	// Who may ask
	CHECK_EQ(HandleMcp(Post("{}", ""), Config, Tools).Status, 403);
	CHECK_EQ(HandleMcp(Post("{}", "wrong"), Config, Tools).Status, 403);
	{
		McpConfig Open = Config;	// without a token nothing is answered, not everything
		Open.Token.clear();
		CHECK_EQ(HandleMcp(Post("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}", ""), Open, Tools).Status, 403);
	}
	HttpRequest Lowercase = Post("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}", "");
	Lowercase.Headers.push_back(std::make_pair(std::string("authorization"), std::string("bearer   secret")));
	CHECK_EQ(HandleMcp(Lowercase, Config, Tools).Status, 200);
	HttpRequest Page = Post("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}");
	Page.Headers.push_back(std::make_pair(std::string("Origin"), std::string("https://evil.example")));
	CHECK_EQ(HandleMcp(Page, Config, Tools).Status, 403);
	Page.Headers.back().second = "null";
	CHECK_EQ(HandleMcp(Page, Config, Tools).Status, 403);
	Page.Headers.back().second = "http://localhost:3000";
	CHECK_EQ(HandleMcp(Page, Config, Tools).Status, 200);
	HttpRequest Rebound = Post("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}");
	Rebound.Headers[0].second = "attacker.example:46600";
	CHECK_EQ(HandleMcp(Rebound, Config, Tools).Status, 403);
	Rebound.Headers[0].second = "[::1]:46600";
	CHECK_EQ(HandleMcp(Rebound, Config, Tools).Status, 200);
	Rebound.Headers[0].second = "LOCALHOST";
	CHECK_EQ(HandleMcp(Rebound, Config, Tools).Status, 200);

	// What's malformed
	HttpRequest Get = Post("");
	Get.Method = "GET";
	Response = HandleMcp(Get, Config, Tools);
	CHECK_EQ(Response.Status, 405);
	CHECK(Response.Headers.size() == 1 && Response.Headers[0].second == "POST");
	HttpRequest Elsewhere = Post("{}");
	Elsewhere.Target = "/other";
	CHECK_EQ(HandleMcp(Elsewhere, Config, Tools).Status, 404);
	HttpRequest Text = Post("{}");
	Text.Headers[1].second = "text/plain";
	CHECK_EQ(HandleMcp(Text, Config, Tools).Status, 415);
	CHECK_EQ(ErrorCode(HandleMcp(Post("{nope"), Config, Tools)), -32700);
	CHECK_EQ(ErrorCode(HandleMcp(Post("[{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}]"), Config, Tools)), -32600);
	CHECK_EQ(ErrorCode(HandleMcp(Post("{\"jsonrpc\":\"1.0\",\"id\":1,\"method\":\"ping\"}"), Config, Tools)), -32600);
	CHECK_EQ(ErrorCode(HandleMcp(Post("{\"jsonrpc\":\"2.0\",\"id\":{},\"method\":\"ping\"}"), Config, Tools)), -32600);
	CHECK_EQ(HandleMcp(Post("{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{}}"), Config, Tools).Status, 202);

	// The command that connects Claude Code
	CHECK_EQ(ClaudeAddCommand(Config), std::string("claude mcp add --transport http --scope user famitracker "
		"http://127.0.0.1:46600/mcp --header \"Authorization: Bearer secret\""));
}

// A session as Claude Code would run it, from the handshake to a melody in the song
void TestSession()
{
	FakeSong Song;
	Song.Info.DirectChanges = true;
	ToolSet Tools(Song);
	McpConfig Config;
	Config.Port = 46600;
	Config.Token = "t";
	int Id = 0;
	auto Call = [&](const std::string &Tool, const std::string &Arguments) {
		char Number[16];
		std::snprintf(Number, sizeof(Number), "%d", ++Id);
		HttpRequest Request = Post(std::string("{\"jsonrpc\":\"2.0\",\"id\":") + Number + ",\"method\":\"tools/call\",\"params\":{\"name\":\"" +
			Tool + "\",\"arguments\":" + Arguments + "}}", "t");
		const Json Message = Body(HandleMcp(Request, Config, Tools));
		const Json *pResult = Message.Get("result");
		if (pResult == 0)
			return std::string("PROTOCOL ERROR");
		return std::string(pResult->Get("isError")->Bool() ? "ERROR: " : "") + pResult->Get("content")->At(0).Get("text")->String();
	};
	CHECK_HAS(Call("get_song", "{}"), "Key: none yet");
	CHECK_HAS(Call("write_notes", "{\"channel\":\"VRC6 Pulse 1\",\"instrument\":\"soft lead\",\"notes\":["
		"{\"bar\":1,\"beat\":1,\"pitch\":\"E-4\",\"beats\":1},{\"bar\":1,\"beat\":2,\"pitch\":\"G-4\",\"beats\":1},"
		"{\"bar\":1,\"beat\":3,\"pitch\":\"C-5\",\"beats\":2},{\"bar\":2,\"beat\":1,\"pitch\":\"A-4\",\"beats\":4}]}"), "Changed the song");
	CHECK_HAS(Call("add_chords_and_bass", "{\"melody_channel\":\"VRC6 Pulse 1\"}"), "Chords:");
	CHECK_HAS(Call("check_song", "{}"), "Checked bars 1-8");
	CHECK_HAS(Call("transpose", "{\"to_mode\":\"minor\"}"), "to C minor");
	CHECK_HAS(Call("add_vibrato", "{\"channel\":\"VRC6 Pulse 1\",\"speed\":\"slow\",\"depth\":\"strong\"}"), "vibrato 437");
	CHECK_HAS(Call("add_echo", "{\"channel\":\"VRC6 Pulse 1\"}"), "an echo of VRC6 Pulse 1 on VRC6 Pulse 2");
	CHECK_HAS(Call("set_tempo", "{\"bpm\":110}"), "Tempo set to 110 BPM");
	CHECK_HAS(Call("change_mood", "{\"mood\":\"darker\",\"to_mode\":\"phrygian\",\"moves\":[{\"channel\":\"Triangle\",\"semitones\":-12}],"
		"\"sounds\":[{\"channel\":\"VRC6 Pulse 1\",\"instrument\":\"warm pad\"}],\"tempo_percent\":85}"),
		"The tempo is now 93 BPM (speed 6, tempo 93)");
	CHECK_HAS(Call("play", "{\"from_bar\":1,\"bars\":2}"), "Playing for the user from bar 1 for 2 bars");
	CHECK_HAS(Call("read_notes", "{\"channel\":\"VRC6 Pulse 1\",\"to_bar\":2}"), "bar 1 beat 1: D#4");
}

// The song's flow: jumps, skips and halts

void TestFlow()
{
	const int PL = 16;
	auto Make = [](int Frames) { return std::vector<std::vector<Cell> >(2, std::vector<Cell>(static_cast<size_t>(Frames) * 16)); };
	auto Put = [](std::vector<std::vector<Cell> > &C, int Channel, int Row, int Column, int Effect, int Param) {
		C[Channel][Row].Effect[Column] = static_cast<unsigned char>(Effect);
		C[Channel][Row].Param[Column] = static_cast<unsigned char>(Param);
	};
	std::vector<int> Columns(2, 2);

	// Every frame, then back to the first
	std::vector<std::vector<Cell> > C = Make(3);
	SongFlow F = FollowSong(C, Columns, PL, 3);
	CHECK(F.Steps.size() == 3 && F.LoopStep == 0 && !F.Halts());
	CHECK(F.LoopRow(PL) == 0 && F.EndRow(PL) == 48 && F.PlayedRows() == 48 && F.LoopRows() == 48);

	// An intro: B01 at the end jumps back to frame 1
	Put(C, 1, 2 * PL + 15, 0, EFF_JUMP, 1);
	F = FollowSong(C, Columns, PL, 3);
	CHECK(F.Steps.size() == 3 && F.LoopStep == 1);
	CHECK(F.LoopRow(PL) == 16 && F.EndRow(PL) == 48 && F.PlayedRows() == 48 && F.LoopRows() == 32);

	// Jumping earlier in the frame: the rows after it never play
	C = Make(3);
	Put(C, 1, 2 * PL + 7, 0, EFF_JUMP, 1);
	F = FollowSong(C, Columns, PL, 3);
	CHECK(F.EndRow(PL) == 40 && F.LoopRows() == 24);
	std::vector<bool> Played = F.Played(48, PL);
	CHECK(Played[39] && !Played[40] && !Played[47] && Played[16]);

	// A skip starts the next frame later: D04 in frame 0
	Put(C, 0, 11, 0, EFF_SKIP, 4);
	F = FollowSong(C, Columns, PL, 3);
	CHECK(F.Steps.size() == 4 && F.Steps[0].EndRow == 12 && F.Steps[1].Frame == 1 && F.Steps[1].FirstRow == 4);
	// Frame 2 jumps to frame 1, which plays from its first row this time, then frame 2 again: that's the loop
	CHECK(F.Steps[3].Frame == 1 && F.Steps[3].FirstRow == 0 && F.Steps[3].EndRow == 16 && F.LoopStep == 2);
	CHECK_EQ(F.LoopRow(PL), 32);
	Played = F.Played(48, PL);
	CHECK(!Played[12] && Played[16] && Played[20]);

	// A halt ends it
	C = Make(3);
	Put(C, 0, PL + 3, 1, EFF_HALT, 0);
	F = FollowSong(C, Columns, PL, 3);
	CHECK(F.Halts() && F.LoopRow(PL) == -1 && F.EndRow(PL) == 20 && F.LoopRows() == 0 && F.PlayedRows() == 20);
	// ...but not from a column the channel doesn't show
	Columns[0] = 1;
	F = FollowSong(C, Columns, PL, 3);
	CHECK(!F.Halts() && F.EndRow(PL) == 48);
	Columns[0] = 2;

	// A jump and a skip on one row: the jump goes first; past the last frame it goes to the last one
	C = Make(3);
	Put(C, 0, 5, 0, EFF_SKIP, 2);
	Put(C, 1, 5, 0, EFF_JUMP, 9);
	F = FollowSong(C, Columns, PL, 3);
	CHECK(F.Steps.size() >= 2 && F.Steps[1].Frame == 2 && F.Steps[1].FirstRow == 0);
	// The last one read wins
	C = Make(3);
	Put(C, 0, 47, 0, EFF_JUMP, 0);
	Put(C, 1, 47, 1, EFF_JUMP, 1);
	F = FollowSong(C, Columns, PL, 3);
	CHECK(F.Steps.size() == 3 && F.LoopStep == 1);
	CHECK(FollowSong(C, Columns, PL, 0).Steps.empty());
}

// PNG

const int TEST_LENGTH_BASE[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115,
	131, 163, 195, 227, 258 };
const int TEST_LENGTH_EXTRA[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
const int TEST_DISTANCE_BASE[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537,
	2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
const int TEST_DISTANCE_EXTRA[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12,
	13, 13 };

// Inflates a zlib stream of fixed-code blocks (what ZlibCompress writes);
// false when it isn't one or its checksum is wrong
bool Inflate(const std::string &Zlib, std::string &Out)
{
	Out.clear();
	if (Zlib.size() < 7 || static_cast<unsigned char>(Zlib[0]) != 0x78 ||
		((static_cast<unsigned char>(Zlib[0]) << 8) | static_cast<unsigned char>(Zlib[1])) % 31 != 0)
		return false;
	const size_t DataEnd = Zlib.size() - 4;
	size_t Byte = 2;
	int Bit = 0;
	bool Short = false;
	auto Bits = [&](int Count) {		// lowest bit first
		long Value = 0;
		for (int i = 0; i < Count; ++i) {
			if (Byte >= DataEnd) {
				Short = true;
				return 0L;
			}
			Value |= static_cast<long>((static_cast<unsigned char>(Zlib[Byte]) >> Bit) & 1) << i;
			if (++Bit == 8) {
				Bit = 0;
				++Byte;
			}
		}
		return Value;
	};
	auto Code = [&](int Count) {		// a Huffman code, highest bit first
		long Value = 0;
		for (int i = 0; i < Count; ++i)
			Value = (Value << 1) | Bits(1);
		return Value;
	};
	for (bool Last = false; !Last; ) {
		Last = Bits(1) != 0;
		if (Bits(2) != 1 || Short)
			return false;
		for (;;) {
			long Value = Code(7), Symbol;
			if (Value <= 23)
				Symbol = 256 + Value;
			else {
				Value = (Value << 1) | Bits(1);
				if (Value >= 0x30 && Value <= 0xBF)
					Symbol = Value - 0x30;
				else if (Value >= 0xC0 && Value <= 0xC7)
					Symbol = 280 + Value - 0xC0;
				else {
					Value = (Value << 1) | Bits(1);
					if (Value < 0x190 || Value > 0x1FF)
						return false;
					Symbol = 144 + Value - 0x190;
				}
			}
			if (Short)
				return false;
			if (Symbol < 256)
				Out += static_cast<char>(Symbol);
			else if (Symbol == 256)
				break;
			else {
				const int L = static_cast<int>(Symbol - 257);
				if (L > 28)
					return false;
				const long Length = TEST_LENGTH_BASE[L] + Bits(TEST_LENGTH_EXTRA[L]);
				const long D = Code(5);
				if (D > 29)
					return false;
				const long Distance = TEST_DISTANCE_BASE[D] + Bits(TEST_DISTANCE_EXTRA[D]);
				if (Short || Distance > static_cast<long>(Out.size()))
					return false;
				for (long k = 0; k < Length; ++k)
					Out += Out[Out.size() - static_cast<size_t>(Distance)];
			}
		}
	}
	if (Bit != 0)
		++Byte;
	if (Byte != DataEnd)
		return false;
	unsigned long Stored = 0;
	for (int i = 0; i < 4; ++i)
		Stored = (Stored << 8) | static_cast<unsigned char>(Zlib[DataEnd + static_cast<size_t>(i)]);
	return Stored == Adler32(Out);
}

unsigned long BigEndian(const std::string &Data, size_t At)
{
	unsigned long Value = 0;
	for (size_t i = 0; i < 4; ++i)
		Value = (Value << 8) | static_cast<unsigned char>(Data[At + i]);
	return Value;
}

// A PNG read back: its size, palette and pixels (palette indices); false if
// it isn't one as EncodePng writes them
struct DecodedPng {
	int Width, Height;
	std::vector<Rgb> Palette;
	std::string Pixels;
};

bool DecodePng(const std::string &File, DecodedPng &Out)
{
	if (File.size() < 8 || File.compare(0, 8, std::string("\x89PNG\r\n\x1A\n", 8)) != 0)
		return false;
	std::string Data;
	bool Ended = false;
	for (size_t At = 8; At + 12 <= File.size() && !Ended; ) {
		const size_t Length = BigEndian(File, At);
		if (At + 12 + Length > File.size())
			return false;
		const std::string Type = File.substr(At + 4, 4);
		const std::string Body = File.substr(At + 8, Length);
		if (BigEndian(File, At + 8 + Length) != Crc32(Type + Body))
			return false;
		if (Type == "IHDR") {
			Out.Width = static_cast<int>(BigEndian(Body, 0));
			Out.Height = static_cast<int>(BigEndian(Body, 4));
			if (Body[8] != 8 || Body[9] != 3)
				return false;
		}
		else if (Type == "PLTE") {
			for (size_t i = 0; i + 2 < Body.size(); i += 3) {
				Rgb Color = { static_cast<unsigned char>(Body[i]), static_cast<unsigned char>(Body[i + 1]), static_cast<unsigned char>(Body[i + 2]) };
				Out.Palette.push_back(Color);
			}
		}
		else if (Type == "IDAT")
			Data += Body;
		else if (Type == "IEND")
			Ended = true;
		At += 12 + Length;
	}
	std::string Raw;
	if (!Ended || !Inflate(Data, Raw) || Raw.size() != static_cast<size_t>(Out.Height) * (static_cast<size_t>(Out.Width) + 1))
		return false;
	Out.Pixels.clear();
	for (int y = 0; y < Out.Height; ++y) {
		const size_t Row = static_cast<size_t>(y) * (static_cast<size_t>(Out.Width) + 1);
		if (Raw[Row] != 0)
			return false;
		Out.Pixels += Raw.substr(Row + 1, static_cast<size_t>(Out.Width));
	}
	return true;
}

// How many pixels have this color
int CountColor(const DecodedPng &Image, int R, int G, int B)
{
	int Count = 0;
	for (size_t i = 0; i < Image.Pixels.size(); ++i) {
		const Rgb &C = Image.Palette[static_cast<unsigned char>(Image.Pixels[i])];
		if (C.R == R && C.G == G && C.B == B)
			++Count;
	}
	return Count;
}

void TestPng()
{
	CHECK_EQ(Crc32("123456789"), 0xCBF43926ul);
	CHECK_EQ(Adler32("Wikipedia"), 0x11E60398ul);
	CHECK_EQ(Base64Encode(""), std::string());
	CHECK_EQ(Base64Encode("f"), std::string("Zg=="));
	CHECK_EQ(Base64Encode("fo"), std::string("Zm8="));
	CHECK_EQ(Base64Encode("foobar"), std::string("Zm9vYmFy"));

	// Compression that comes back the same: nothing, a little, runs, noise,
	// repeats far apart and more than the window holds
	std::vector<std::string> Samples;
	Samples.push_back(std::string());
	Samples.push_back("a");
	Samples.push_back("abcabcabcabcabcabcabcabcabcabcabcx");
	Samples.push_back(std::string(100000, 'z'));
	std::string Noise;
	unsigned long Seed = 12345;
	for (int i = 0; i < 70000; ++i) {
		Seed = Seed * 1103515245ul + 12345ul;
		Noise += static_cast<char>((Seed >> 16) & 0xFF);
	}
	Samples.push_back(Noise);
	Samples.push_back(Noise.substr(0, 3000) + std::string(500, 'q') + Noise.substr(0, 3000) + Noise.substr(10000, 40000) +
		Noise.substr(0, 3000));
	std::string AllBytes;
	for (int i = 0; i < 256; ++i)
		AllBytes += static_cast<char>(i);
	Samples.push_back(AllBytes + AllBytes + AllBytes);
	for (size_t i = 0; i < Samples.size(); ++i) {
		std::string Back;
		Check(Inflate(ZlibCompress(Samples[i]), Back) && Back == Samples[i], "compressed data comes back", __LINE__);
	}
	CHECK(ZlibCompress(std::string(100000, 'z')).size() < 1000);

	// A small image
	std::vector<Rgb> Palette;
	Rgb Black = { 0, 0, 0 }, Red = { 255, 0, 0 };
	Palette.push_back(Black);
	Palette.push_back(Red);
	std::vector<unsigned char> Pixels(12, 0);
	Pixels[5] = 1;
	DecodedPng Image;
	CHECK(DecodePng(EncodePng(4, 3, Pixels, Palette), Image));
	CHECK(Image.Width == 4 && Image.Height == 3 && Image.Palette.size() == 2 && Image.Pixels.size() == 12);
	CHECK(Image.Pixels[5] == 1 && Image.Pixels[4] == 0);
}

// The picture of the notes

std::string Environment(const char *pName)
{
#ifdef _MSC_VER
	char *pValue = 0;
	size_t Size = 0;
	if (_dupenv_s(&pValue, &Size, pName) != 0 || pValue == 0)
		return std::string();
	const std::string Value(pValue);
	std::free(pValue);
	return Value;
#else
	const char *pValue = std::getenv(pName);
	return pValue != 0 ? std::string(pValue) : std::string();
#endif
}

void TestPicture()
{
	FakeSong Song;
	// A melody on VRC6 Pulse 1, the last note quiet
	Song.Put(5, 0, "C-4", 0, 15);
	Song.Put(5, 4, "E-4");
	Song.Put(5, 8, "G-4", NO_INSTRUMENT, 6);
	Song.End(5, 16, NOTE_RELEASE);
	// Chords as an arpeggio on Pulse 2, a bass on the triangle, drums
	Song.Put(1, 0, "C-3", 2);
	Song.Rows[1][0].Effect[0] = EFF_ARPEGGIO;
	Song.Rows[1][0].Param[0] = 0x47;
	Song.End(1, 16);
	Song.Put(2, 0, "C-2", 1);
	Song.Put(2, 8, "G-1");
	Song.Put(3, 0, "kick");
	Song.Put(3, 4, "hihat");
	Song.Put(3, 8, "snare");
	// An intro: the end jumps back to the second frame (bar 5)
	Song.Rows[0][127].Effect[0] = EFF_JUMP;
	Song.Rows[0][127].Param[0] = 1;

	ToolSet Tools(Song);
	ToolResult Result = Tools.Call("look_at_song", Parse("{}"));
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "Piano roll of bars 1-8");
	CHECK_HAS(Result.Text, "blue = 2A03 Pulse 2, green = 2A03 Triangle, orange = VRC6 Pulse 1");
	CHECK_HAS(Result.Text, "DRUMS strip below is 2A03 Noise (brown)");
	CHECK_HAS(Result.Text, "arpeggio chord");
	CHECK_HAS(Result.Text, "Green line: where the song loops back to (bar 5 beat 1)");
	CHECK_HAS(Result.Text, "Nothing here on: 2A03 Pulse 1, 2A03 DPCM, VRC6 Pulse 2, VRC6 Sawtooth.");
	DecodedPng Image;
	CHECK(DecodePng(Result.Image, Image));
	CHECK(Image.Width > 1000 && Image.Width < 1200 && Image.Height > 300 && Image.Height < 800);
	CHECK(CountColor(Image, 255, 127, 14) > 100);		// the loud melody notes, orange
	CHECK(CountColor(Image, 255, 185, 122) > 10);		// the quieter one, paler (orange at 55%)
	CHECK(CountColor(Image, 31, 119, 180) > 50);		// the chord's root, blue
	CHECK(CountColor(Image, 192, 217, 234) > 50);		// its other notes, faint blue
	CHECK(CountColor(Image, 44, 160, 44) > 50);			// the bass, green
	CHECK(CountColor(Image, 140, 86, 75) > 5);			// drum hits, brown
	CHECK(CountColor(Image, 0, 155, 70) > 100);			// the loop's start
	CHECK(CountColor(Image, 210, 30, 30) > 100);		// where it jumps back
	CHECK(Result.Image.size() < 60000);
	const std::string Directory = Environment("FTAPI_PICTURES");	// to look at it
	if (!Directory.empty())
		std::ofstream(Directory + "/look_at_song.png", std::ios::binary) << Result.Image;

	// Fewer channels, other bars; in the protocol, as an image after the text
	Result = Tools.Call("look_at_song", Parse("{\"from_bar\": 5, \"to_bar\": 6, \"channels\": [\"VRC6 Pulse 1\"]}"));
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "Piano roll of bars 5-6");
	CHECK_HAS(Result.Text, "Nothing here on: VRC6 Pulse 1.");
	CHECK(DecodePng(Result.Image, Image));
	CHECK(Tools.Call("look_at_song", Parse("{\"from_bar\": 1, \"to_bar\": 40}")).IsError);
	CHECK(Tools.Call("look_at_song", Parse("{\"from_bar\": 3, \"to_bar\": 2}")).IsError);
	CHECK(Tools.Call("look_at_song", Parse("{\"channels\": \"nope\"}")).IsError);

	McpConfig Config;
	Config.Token = "t";
	HttpRequest Request = Post("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":\"look_at_song\",\"arguments\":{}}}", "t");
	const Json Message = Parse(HandleMcp(Request, Config, Tools).Body);
	const Json *pContent = Message.Get("result") != 0 ? Message.Get("result")->Get("content") : 0;
	CHECK(pContent != 0 && pContent->Size() == 2);
	if (pContent != 0 && pContent->Size() == 2) {
		CHECK(pContent->At(0).Get("type")->String() == "text");
		CHECK(pContent->At(1).Get("type")->String() == "image" && pContent->At(1).Get("mimeType")->String() == "image/png");
		CHECK_EQ(pContent->At(1).Get("data")->String().compare(0, 8, "iVBORw0K"), 0);
	}
}

// Versions as tracks of their own

int InstrumentIndex(const FakeSong &Song, const char *Name)
{
	for (size_t i = 0; i < Song.Info.Instruments.size(); ++i)
		if (Song.Info.Instruments[i].Name == Name)
			return Song.Info.Instruments[i].Index;
	return -1;
}

bool HasEffectNear(const std::vector<Cell> &Cells, int First, int End, int Effect)
{
	for (int r = First; r < End && r < static_cast<int>(Cells.size()); ++r)
		for (int k = 0; k < EFFECT_COLUMNS; ++k)
			if (Cells[static_cast<size_t>(r)].Effect[k] == Effect && Cells[static_cast<size_t>(r)].Param[k] != 0)
				return true;
	return false;
}

void TestVersions()
{
	FakeSong Song;
	// A melody with long notes, chords as an arpeggio, a bass, drums on and between the beats
	Song.Put(5, 0, "C-4", 0);
	Song.Put(5, 8, "E-4");
	Song.Put(5, 16, "G-4", NO_INSTRUMENT, 12);
	Song.Put(5, 32, "E-4");
	Song.End(5, 48, NOTE_RELEASE);
	Song.Put(0, 0, "C-3", 2);
	Song.Rows[0][0].Effect[0] = EFF_ARPEGGIO;
	Song.Rows[0][0].Param[0] = 0x47;
	Song.End(0, 64);
	Song.Put(2, 0, "C-2", 1);
	for (int Row = 0; Row < 64; Row += 2)
		Song.Put(3, Row, Row % 4 == 0 ? "kick" : "hihat", 2);
	// Speed and tempo commands at the start
	Song.Rows[3][0].Effect[0] = EFF_SPEED;
	Song.Rows[3][0].Param[0] = 6;
	Song.Rows[3][0].Effect[1] = EFF_SPEED;
	Song.Rows[3][0].Param[1] = 150;
	const std::vector<std::vector<Cell> > Day = Song.Rows;

	ToolSet Tools(Song);
	ToolResult Result = Tools.Call("make_version", Parse("{\"version\": \"night\"}"));
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "Made track 2 \"Town (night)\" from track 1 \"Town\", which stays as it was.");
	CHECK_HAS(Result.Text, "120 BPM instead of 150 (speed 6, tempo 120; 1 tempo command in the patterns changed too)");
	CHECK_HAS(Result.Text, "volumes at 75%");
	CHECK_HAS(Result.Text, "sparser drums (16 hits left out, the rest softer)");
	CHECK_HAS(Result.Text, "VRC6 Pulse 1 played by \"Soft lead\"");
	CHECK_HAS(Result.Text, "2A03 Pulse 1 played by \"Soft chords\"");
	CHECK_HAS(Result.Text, "vibrato 432 on VRC6 Pulse 1");
	CHECK_HAS(Result.Text, "an echo of VRC6 Pulse 1 on VRC6 Pulse 2");
	CHECK(Song.Tracks.size() == 2 && Song.Current == 1 && Song.Info.TrackName == "Town (night)");
	CHECK(Song.Info.Speed == 6 && Song.Info.Tempo == 120);
	// The day version is as it was
	CHECK(Song.Tracks[0].Rows == Day && Song.Tracks[0].Speed == 6 && Song.Tracks[0].Tempo == 150);
	// The night version: quieter, other sounds, fewer and softer drums, slower tempo command
	const std::vector<std::vector<Cell> > &Night = Song.Rows;
	CHECK_EQ(Night[5][0].Volume, 11);			// 15 at 75%
	CHECK_EQ(Night[5][16].Volume, 9);			// 12 at 75%
	CHECK(Night[5][0].Instrument == InstrumentIndex(Song, "Soft lead"));
	CHECK(Night[0][0].Instrument == InstrumentIndex(Song, "Soft chords") && Night[0][0].Param[0] == 0x47);
	CHECK(Night[2][0].Volume == NO_VOLUME && Night[2][0].Pitch() == Day[2][0].Pitch());	// the triangle has no volume
	CHECK(Night[3][0].HasNote() && !Night[3][2].HasNote() && Night[3][4].HasNote() && !Night[3][6].HasNote());
	CHECK_EQ(Night[3][0].Volume, 9);			// the drums at 60%
	CHECK(Night[3][0].Param[0] == 6 && Night[3][0].Param[1] == 120);
	CHECK(HasEffectNear(Night[5], 0, 8, EFF_VIBRATO));
	CHECK(Night[6][3].HasNote() && Night[6][3].Pitch() == Day[5][0].Pitch());	// the echo, 3 rows later
	CHECK_HAS(Tools.Call("get_song", Parse("{}")).Text, "Tracks: 1 \"Town\", 2 \"Town (night)\" (selected)");

	// Switching tracks
	CHECK_HAS(Tools.Call("select_track", Parse("{\"track\": 1}")).Text, "Track 1 \"Town\" is selected");
	CHECK(Song.Current == 0 && Song.Rows == Day && Song.Info.Tempo == 150);
	CHECK(!Tools.Call("select_track", Parse("{\"track\": \"night\"}")).IsError && Song.Current == 1);
	CHECK(!Tools.Call("select_track", Parse("{\"track\": \"town\"}")).IsError && Song.Current == 0);
	CHECK_HAS(Tools.Call("select_track", Parse("{\"track\": 5}")).Text, "There's no track 5. The tracks: 1 \"Town\", 2 \"Town (night)\".");
	CHECK(Tools.Call("select_track", Parse("{\"track\": \"rain\"}")).IsError);
	CHECK(Tools.Call("select_track", Parse("{}")).IsError);

	// Another version changes only what it's given; names stay unique
	Result = Tools.Call("make_version", Parse("{\"version\": \"tense\", \"to_mode\": \"minor\"}"));
	CHECK(!Result.IsError);
	CHECK_HAS(Result.Text, "Made track 3 \"Town (tense)\" from track 1 \"Town\"");
	CHECK_HAS(Result.Text, "from C major to C minor");
	CHECK(Song.Info.Tempo == 150 && Song.Rows[5][8].Pitch() == Day[5][8].Pitch() - 1 && Song.Rows[3][2].HasNote());
	Tools.Call("select_track", Parse("{\"track\": 1}"));
	Result = Tools.Call("make_version", Parse("{\"version\": \"night\", \"echo\": false, \"vibrato\": false, \"sounds\": false, "
		"\"drums\": \"off\", \"tempo_percent\": 90}"));
	CHECK_HAS(Result.Text, "Made track 4 \"Town (night 2)\"");
	CHECK_HAS(Result.Text, "no drums");
	CHECK_LACKS(Result.Text, "echo");
	CHECK_LACKS(Result.Text, "vibrato");
	CHECK(!Song.Rows[3][0].HasNote() && Song.Rows[5][0].Instrument == 0 && Song.Info.Tempo == 135);
	// A version of a version keeps one bracket
	CHECK_HAS(Tools.Call("make_version", Parse("{\"version\": \"rain\"}")).Text, "\"Town (rain)\"");
	CHECK(Tools.Call("make_version", Parse("{}")).IsError);
	CHECK(Tools.Call("make_version", Parse("{\"version\": \"night\", \"drums\": \"loud\"}")).IsError);
	Song.Busy = true;
	CHECK_HAS(Tools.Call("make_version", Parse("{\"version\": \"night\"}")).Text, "A dialog is open");
	Song.Busy = false;
	while (Song.Tracks.size() < 64)
		Song.Tracks.push_back(Song.Tracks[0]);
	CHECK_HAS(Tools.Call("make_version", Parse("{\"version\": \"night\"}")).Text, "64 tracks");
}

// The connection for Claude Code in WSL (famitracker-mcp): lines in, HTTP to
// FamiTracker, lines out

std::string HeaderOf(const HttpRequest &Request, const char *pName)
{
	const std::string *pValue = Request.Header(pName);
	return pValue != 0 ? *pValue : std::string("(none)");
}

// A line through the bridge and FamiTracker's endpoint, as bytes both ways
std::string ThroughEndpoint(const std::string &Line, ToolSet &Tools, const McpConfig &Config, const std::string &Token)
{
	BridgeMessage Message;
	if (!BridgeRequest(Line, Config.Port, Token, Message))
		return "(blank)";
	const std::string Bytes = FormatHttpRequest(Message.Request);
	HttpRequestParser Parser;
	if (Parser.Feed(Bytes.data(), Bytes.size()) != HttpRequestParser::COMPLETE)
		return "(unreadable request)";
	const HttpRequest Request = Parser.Take();
	const std::string Wire = FormatHttpResponse(HandleMcp(Request, Config, Tools), Request.KeepAlive());
	HttpResponse Answer;
	if (ReadHttpResponse(Wire, false, Answer) != RESPONSE_COMPLETE)
		return "(unreadable answer)";
	return BridgeAnswer(Message, Answer);
}

// One line, ending in a line break: what Claude Code reads
bool OneLine(const std::string &Text)
{
	return !Text.empty() && Text.find('\n') == Text.size() - 1 && Text.find('\r') == std::string::npos;
}

const char MODERN_META[] = "\"_meta\":{\"io.modelcontextprotocol/protocolVersion\":\"2026-07-28\"}";

void TestBridge()
{
	// Lines become requests
	BridgeMessage Message;
	CHECK(!BridgeRequest("", 46600, "t", Message));
	CHECK(!BridgeRequest(" \t\r", 46600, "t", Message));
	const std::string Initialize = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-06-18\","
		"\"capabilities\":{},\"clientInfo\":{\"name\":\"claude-code\",\"version\":\"2.0\"}}}";
	CHECK(BridgeRequest(Initialize + "\r", 46601, "secret", Message));
	CHECK(Message.Readable && Message.WantsAnswer && Message.Id == Json(1));
	CHECK_EQ(Message.Request.Method, std::string("POST"));
	CHECK_EQ(Message.Request.Target, std::string("/mcp"));
	CHECK_EQ(Message.Request.Body, Initialize);
	CHECK_EQ(HeaderOf(Message.Request, "Host"), std::string("127.0.0.1:46601"));
	CHECK_EQ(HeaderOf(Message.Request, "Authorization"), std::string("Bearer secret"));
	CHECK_EQ(HeaderOf(Message.Request, "Content-Type"), std::string("application/json"));
	CHECK_EQ(HeaderOf(Message.Request, "Connection"), std::string("close"));
	CHECK_EQ(HeaderOf(Message.Request, "MCP-Protocol-Version"), std::string("(none)"));
	const std::string Bytes = FormatHttpRequest(Message.Request);
	CHECK_EQ(Bytes.compare(0, 22, "POST /mcp HTTP/1.1\r\nHo"), 0);
	char Length[48];
	std::snprintf(Length, sizeof(Length), "\r\nContent-Length: %d\r\n\r\n{", static_cast<int>(Initialize.size()));
	Check(Contains(Bytes, Length), "the length before the body", __LINE__);
	CHECK(BridgeRequest(Initialize, 46600, "", Message));
	CHECK_EQ(HeaderOf(Message.Request, "Authorization"), std::string("(none)"));

	// The 2026-07-28 protocol: the headers say what the body says
	CHECK(BridgeRequest(std::string("{\"jsonrpc\":\"2.0\",\"id\":\"x\",\"method\":\"tools/call\",\"params\":{\"name\":\"get_song\","
		"\"arguments\":{},") + MODERN_META + "}}", 46600, "t", Message));
	CHECK(Message.WantsAnswer && Message.Id == Json("x"));
	CHECK_EQ(HeaderOf(Message.Request, "MCP-Protocol-Version"), std::string("2026-07-28"));
	CHECK_EQ(HeaderOf(Message.Request, "Mcp-Method"), std::string("tools/call"));
	CHECK_EQ(HeaderOf(Message.Request, "Mcp-Name"), std::string("get_song"));
	CHECK(BridgeRequest(std::string("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\",\"params\":{\"name\":\"caf\\u00e9 x\",") +
		MODERN_META + "}}", 46600, "t", Message));
	CHECK_EQ(HeaderOf(Message.Request, "Mcp-Name"), std::string("=?base64?Y2Fmw6kgeA==?="));
	CHECK(BridgeRequest(std::string("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"resources/read\",\"params\":{\"uri\":\"=?base64?x\",") +
		MODERN_META + "}}", 46600, "t", Message));
	CHECK_EQ(HeaderOf(Message.Request, "Mcp-Name"), std::string("=?base64?PT9iYXNlNjQ/eA==?="));
	CHECK(BridgeRequest(std::string("{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/list\",\"params\":{") + MODERN_META + "}}",
		46600, "t", Message));
	CHECK_EQ(HeaderOf(Message.Request, "Mcp-Method"), std::string("tools/list"));
	CHECK_EQ(HeaderOf(Message.Request, "Mcp-Name"), std::string("(none)"));
	// Nothing in a value ends the head early
	CHECK(BridgeRequest(std::string("{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"a\\r\\nX-Injected: 1\",\"params\":{") + MODERN_META + "}}",
		46600, "t", Message));
	CHECK_LACKS(FormatHttpRequest(Message.Request), "\r\nX-Injected");

	// Notifications and Claude Code's own answers wait for nothing
	CHECK(BridgeRequest("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}", 46600, "t", Message));
	CHECK(Message.Readable && !Message.WantsAnswer);
	CHECK(BridgeRequest("{\"jsonrpc\":\"2.0\",\"id\":9,\"result\":{}}", 46600, "t", Message));
	CHECK(Message.Readable && !Message.WantsAnswer);
	CHECK(BridgeRequest("{nope", 46600, "t", Message));
	CHECK(!Message.Readable && !Message.WantsAnswer);

	// Through FamiTracker's endpoint
	FakeSong Song;
	Song.Put(5, 0, "C-4", 0);
	ToolSet Tools(Song);
	McpConfig Config;
	Config.Port = 46600;
	Config.Token = "secret";
	std::string Out = ThroughEndpoint(Initialize, Tools, Config, "secret");
	CHECK(OneLine(Out));
	Json Answer = Parse(Out);
	CHECK(Answer.Get("id") != 0 && *Answer.Get("id") == Json(1));
	CHECK(Answer.Get("result") != 0 && Answer.Get("result")->Get("protocolVersion") != 0 &&
		Answer.Get("result")->Get("protocolVersion")->String() == "2025-06-18");
	CHECK_EQ(ThroughEndpoint("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}", Tools, Config, "secret"), std::string());
	Out = ThroughEndpoint("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\"}", Tools, Config, "secret");
	Answer = Parse(Out);
	CHECK(OneLine(Out) && Answer.Get("result") != 0 && Answer.Get("result")->Get("tools")->Size() == Tools.List().size());
	Out = ThroughEndpoint("{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\",\"params\":{\"name\":\"get_song\",\"arguments\":{}}}",
		Tools, Config, "secret");
	CHECK(OneLine(Out));
	CHECK_HAS(Out, "\"isError\":false");
	CHECK_HAS(Out, "VRC6 Pulse 1");
	Out = ThroughEndpoint(std::string("{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"server/discover\",\"params\":{") + MODERN_META + "}}",
		Tools, Config, "secret");
	CHECK_HAS(Out, "\"supportedVersions\":[\"2026-07-28\"]");
	Out = ThroughEndpoint(std::string("{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"tools/call\",\"params\":{\"name\":\"get_song\","
		"\"arguments\":{},") + MODERN_META + "}}", Tools, Config, "secret");
	CHECK_HAS(Out, "\"resultType\":\"complete\"");
	CHECK_HAS(Out, "\"isError\":false");
	// A name sent in base64 still matches: the tool is unknown, the headers fine
	Out = ThroughEndpoint(std::string("{\"jsonrpc\":\"2.0\",\"id\":6,\"method\":\"tools/call\",\"params\":{\"name\":\"caf\\u00e9\","
		"\"arguments\":{},") + MODERN_META + "}}", Tools, Config, "secret");
	CHECK_HAS(Out, "Unknown tool");
	CHECK_LACKS(Out, "Mcp-Name");
	// Refused before it was read: still the answer to that request
	for (int Wrong = 0; Wrong < 2; ++Wrong) {
		Out = ThroughEndpoint("{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"tools/list\"}", Tools, Config, Wrong == 0 ? "nope" : "");
		Answer = Parse(Out);
		CHECK(OneLine(Out) && Answer.Get("id") != 0 && *Answer.Get("id") == Json(7));
		CHECK_HAS(Out, "access token");
	}
	// A line that isn't JSON gets its parse error
	Out = ThroughEndpoint("{nope", Tools, Config, "secret");
	CHECK(OneLine(Out));
	CHECK_HAS(Out, "\"id\":null");
	CHECK_HAS(Out, "-32700");
	CHECK_EQ(ThroughEndpoint("{\"jsonrpc\":\"2.0\",\"id\":9,\"result\":{}}", Tools, Config, "secret"), std::string());
	CHECK_EQ(ThroughEndpoint("", Tools, Config, "secret"), std::string("(blank)"));

	// Answers that aren't JSON-RPC become errors for the request
	HttpResponse NotFound;
	NotFound.Status = 404;
	NotFound.ContentType = "text/plain; charset=utf-8";
	NotFound.Body = "FamiTracker's MCP endpoint is /mcp.\n";
	CHECK(BridgeRequest("{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"ping\"}", 46600, "t", Message));
	Out = BridgeAnswer(Message, NotFound);
	CHECK(OneLine(Out));
	CHECK_HAS(Out, "\"id\":3");
	CHECK_HAS(Out, "HTTP 404 Not Found: FamiTracker's MCP endpoint is /mcp.");
	HttpResponse Empty;
	Empty.Status = 202;
	CHECK_HAS(BridgeAnswer(Message, Empty), "HTTP 202 Accepted.");
	Out = BridgeFailure(Message, "FamiTracker isn't running.");
	CHECK(OneLine(Out));
	CHECK_HAS(Out, "{\"jsonrpc\":\"2.0\",\"id\":3,\"error\":{\"code\":-32000,\"message\":\"FamiTracker isn't running.\"}}");
	CHECK(BridgeRequest("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/cancelled\"}", 46600, "t", Message));
	CHECK_EQ(BridgeAnswer(Message, NotFound), std::string());
	CHECK_EQ(BridgeAnswer(Message, Empty), std::string());
	CHECK_EQ(BridgeFailure(Message, "FamiTracker isn't running."), std::string());

	// Responses read as their bytes arrive
	HttpResponse Response;
	Response.ContentType = "application/json";
	Response.Body = "{\"a\":1}";
	const std::string Wire = FormatHttpResponse(Response, false);
	HttpResponse Read;
	bool Waits = true;
	for (size_t Size = 0; Size < Wire.size(); ++Size)
		Waits = Waits && ReadHttpResponse(Wire.substr(0, Size), false, Read) == RESPONSE_NEED_MORE;
	CHECK(Waits);
	CHECK_EQ(ReadHttpResponse(Wire, false, Read), RESPONSE_COMPLETE);
	CHECK(Read.Status == 200 && Read.ContentType == "application/json" && Read.Body == "{\"a\":1}");
	CHECK_EQ(ReadHttpResponse(Wire, true, Read), RESPONSE_COMPLETE);
	CHECK_EQ(ReadHttpResponse(Wire.substr(0, Wire.size() - 1), true, Read), RESPONSE_BAD);
	CHECK_EQ(ReadHttpResponse("HTTP/1.1 100 Continue\r\n\r\n" + Wire, false, Read), RESPONSE_COMPLETE);
	CHECK(Read.Status == 200 && Read.Body == "{\"a\":1}");
	const std::string Unsized = "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\nX-Other:  yes \r\n\r\nhello";
	CHECK_EQ(ReadHttpResponse(Unsized, false, Read), RESPONSE_NEED_MORE);
	CHECK_EQ(ReadHttpResponse(Unsized, true, Read), RESPONSE_COMPLETE);
	CHECK(Read.Body == "hello" && Read.ContentType == "text/plain" && Read.Headers.size() == 1 && Read.Headers[0].second == "yes");
	CHECK_EQ(ReadHttpResponse("HTTP/1.1 204 No Content\r\n\r\n", false, Read), RESPONSE_COMPLETE);
	CHECK(Read.Status == 204 && Read.Body.empty());
	CHECK_EQ(ReadHttpResponse("HTTP/1.1 503\r\nContent-Length: 2\r\n\r\nno", false, Read), RESPONSE_COMPLETE);
	CHECK(Read.Status == 503 && Read.Body == "no");
	const char *const BROKEN[] = {
		"HTTP/1.1 2x0 OK\r\n\r\n", "FTP/1.1 200 OK\r\n\r\n", "HTTP/1.1 2000 OK\r\n\r\n", "HTTP/1.1 200 OK\r\nBroken\r\n\r\n",
		"HTTP/1.1 200 OK\r\n: x\r\n\r\n", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2\r\nno\r\n0\r\n\r\n",
		"HTTP/1.1 200 OK\r\nContent-Length: x\r\n\r\n", "HTTP/1.1 200 OK\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\nab" };
	for (size_t i = 0; i < sizeof(BROKEN) / sizeof(BROKEN[0]); ++i)
		Check(ReadHttpResponse(BROKEN[i], false, Read) == RESPONSE_BAD, BROKEN[i], __LINE__);
	CHECK_EQ(ReadHttpResponse("HTTP/1.1 200 OK\r\n" + std::string(70000, 'x'), false, Read), RESPONSE_BAD);
	CHECK_EQ(ReadHttpResponse("", true, Read), RESPONSE_BAD);

	// The setup command for WSL
	CHECK_EQ(WslAddCommand("C:\\Users\\Me\\FamiTracker\\famitracker-mcp.exe"),
		std::string("claude mcp remove --scope user famitracker >/dev/null 2>&1; claude mcp add --transport stdio --scope user "
		"famitracker -- \"$(wslpath 'C:\\Users\\Me\\FamiTracker\\famitracker-mcp.exe')\""));
	CHECK_HAS(WslAddCommand("C:\\Users\\O'Neil\\FT\\famitracker-mcp.exe"), "$(wslpath 'C:\\Users\\O'\\''Neil\\FT\\famitracker-mcp.exe')");
}

// FamiTracker's endpoint on a free port of 127.0.0.1, on a thread of its own,
// one request per connection (as famitracker-mcp asks)
#ifdef _WIN32
typedef SOCKET test_socket_t;
const test_socket_t NO_TEST_SOCKET = INVALID_SOCKET;
void CloseTestSocket(test_socket_t Socket) { closesocket(Socket); }
#else
typedef int test_socket_t;
const test_socket_t NO_TEST_SOCKET = -1;
void CloseTestSocket(test_socket_t Socket) { close(Socket); }
#endif

class TestEndpoint {
public:
	TestEndpoint(ToolSet &Tools, const McpConfig &Config) : m_Tools(Tools), m_Config(Config), m_Listener(NO_TEST_SOCKET), m_iPort(0),
		m_bStop(false), m_iAnswered(0) {}
	~TestEndpoint() { Stop(); }

	bool Start()
	{
#ifdef _WIN32
		WSADATA Data;
		if (WSAStartup(MAKEWORD(2, 2), &Data) != 0)
			return false;
#endif
		m_Listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (m_Listener == NO_TEST_SOCKET)
			return false;
		sockaddr_in Address;
		std::memset(&Address, 0, sizeof(Address));
		Address.sin_family = AF_INET;
		Address.sin_port = 0;	// any free port
		Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		socklen_t Size = sizeof(Address);
		if (bind(m_Listener, reinterpret_cast<const sockaddr*>(&Address), sizeof(Address)) != 0 || listen(m_Listener, 8) != 0 ||
			getsockname(m_Listener, reinterpret_cast<sockaddr*>(&Address), &Size) != 0)
			return false;
		m_iPort = ntohs(Address.sin_port);
		m_Thread = std::thread(&TestEndpoint::Run, this);
		return true;
	}

	void Stop()
	{
		m_bStop = true;
		if (m_Thread.joinable())
			m_Thread.join();
		if (m_Listener != NO_TEST_SOCKET) {
			CloseTestSocket(m_Listener);
			m_Listener = NO_TEST_SOCKET;
		}
	}

	int Port() const { return m_iPort; }
	int Answered() const { return m_iAnswered; }

private:
	void Run()
	{
		while (!m_bStop) {
			fd_set Readable;
			FD_ZERO(&Readable);
			FD_SET(m_Listener, &Readable);
			timeval Wait = { 0, 20000 };
			if (select(static_cast<int>(m_Listener) + 1, &Readable, 0, 0, &Wait) <= 0)
				continue;
			const test_socket_t Client = accept(m_Listener, 0, 0);
			if (Client == NO_TEST_SOCKET)
				continue;
			HttpRequestParser Parser;
			HttpRequestParser::Result Result = HttpRequestParser::NEED_MORE;
			char Buffer[4096];
			while (Result == HttpRequestParser::NEED_MORE) {
				const long Count = static_cast<long>(recv(Client, Buffer, static_cast<int>(sizeof(Buffer)), 0));
				if (Count <= 0)
					break;
				Result = Parser.Feed(Buffer, static_cast<size_t>(Count));
			}
			if (Result == HttpRequestParser::COMPLETE) {
				const std::string Bytes = FormatHttpResponse(HandleMcp(Parser.Take(), m_Config, m_Tools), false);
				for (size_t Sent = 0; Sent < Bytes.size(); ) {
					const long Count = static_cast<long>(send(Client, Bytes.data() + Sent, static_cast<int>(Bytes.size() - Sent), 0));
					if (Count <= 0)
						break;
					Sent += static_cast<size_t>(Count);
				}
				++m_iAnswered;
			}
			CloseTestSocket(Client);
		}
	}

	ToolSet &m_Tools;
	McpConfig m_Config;
	test_socket_t m_Listener;
	int m_iPort;
	std::atomic<bool> m_bStop;
	std::atomic<int> m_iAnswered;
	std::thread m_Thread;
};

// famitracker-mcp's side over real sockets
void TestBridgeSockets()
{
	FakeSong Song;
	ToolSet Tools(Song);
	McpConfig Config;
	Config.Token = "t";
	TestEndpoint Endpoint(Tools, Config);
	if (!Endpoint.Start()) {
		Check(false, "the test endpoint couldn't start", __LINE__);
		return;
	}
	const int Port = Endpoint.Port();
	auto Exchange = [&](const std::string &Line, std::string &Error) {
		BridgeMessage Message;
		BridgeRequest(Line, Port, "t", Message);
		HttpResponse Answer;
		const BridgeExchangeResult Result = BridgeExchange(Port, FormatHttpRequest(Message.Request), 5000, Answer, Error);
		return Result == EXCHANGE_ANSWERED ? BridgeAnswer(Message, Answer) : "(" + Error + ")";
	};
	std::string Error;
	std::string Out = Exchange("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-11-25\"}}", Error);
	CHECK(OneLine(Out));
	CHECK_HAS(Out, "\"protocolVersion\":\"2025-11-25\"");
	CHECK_HAS(Out, "\"name\":\"famitracker\"");
	// The tool list is longer than one read
	Out = Exchange("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\"}", Error);
	CHECK(OneLine(Out) && Out.size() > 16384);
	CHECK_HAS(Out, "\"name\":\"write_notes\"");
	// So is a long request
	Out = Exchange("{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"ping\",\"params\":{\"padding\":\"" + std::string(200000, 'x') + "\"}}", Error);
	CHECK_EQ(Out, std::string("{\"jsonrpc\":\"2.0\",\"id\":3,\"result\":{}}\n"));
	CHECK_EQ(Endpoint.Answered(), 3);
	Endpoint.Stop();

	// Nobody listens: FamiTracker isn't running
	BridgeMessage Message;
	BridgeRequest("{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"ping\"}", Port, "t", Message);
	HttpResponse Answer;
	CHECK_EQ(BridgeExchange(Port, FormatHttpRequest(Message.Request), 5000, Answer, Error), EXCHANGE_UNREACHABLE);
	CHECK_HAS(Error, "Nothing listens on 127.0.0.1:");
}

}	// namespace

int main()
{
	TestJson();
	TestHttp();
	TestNames();
	TestTiming();
	TestTempo();
	TestKeys();
	TestReadNotes();
	TestBuildPart();
	TestGetSong();
	TestReadNotesTool();
	TestWriteNotes();
	TestPlaceClip();
	TestInstruments();
	TestTranspose();
	TestTempoTool();
	TestBacking();
	TestEchoAndVibrato();
	TestMood();
	TestCheck();
	TestPlayback();
	TestProtocol();
	TestSession();
	TestBridge();
	TestBridgeSockets();
	TestFlow();
	TestPng();
	TestPicture();
	TestVersions();
	std::printf("%d checks, %d failed\n", g_Checks, g_Failures);
	return g_Failures == 0 ? 0 : 1;
}

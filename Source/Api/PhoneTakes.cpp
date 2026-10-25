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

#include "PhoneTakes.h"
#include "Json.h"
#include "Network.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace ftapi {

namespace {

const char *const PHONE_CODE_PURPOSE = "FamiTracker phone page";
const size_t PHONE_CODE_DIGITS = 24;

// SHA-256's round constants: the first 32 bits of the fractional parts of the cube roots of the first 64 primes
const std::uint32_t ROUNDS[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

std::uint32_t Rotate(std::uint32_t x, int n)
{
	return (x >> n) | (x << (32 - n));
}

std::string Hex(const std::string &Bytes)
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

std::string Lower(const std::string &Text)
{
	std::string Out = Text;
	for (size_t i = 0; i < Out.size(); ++i)
		if (Out[i] >= 'A' && Out[i] <= 'Z')
			Out[i] = static_cast<char>(Out[i] - 'A' + 'a');
	return Out;
}

// The whole length every time, so a code can't be guessed by timing
bool SameCode(const std::string &a, const std::string &b)
{
	unsigned char Difference = static_cast<unsigned char>(a.size() != b.size());
	const size_t Length = a.size() > b.size() ? a.size() : b.size();
	for (size_t i = 0; i < Length; ++i) {
		const unsigned char x = i < a.size() ? static_cast<unsigned char>(a[i]) : 0;
		const unsigned char y = i < b.size() ? static_cast<unsigned char>(b[i]) : 0;
		Difference |= static_cast<unsigned char>(x ^ y);
	}
	return Difference == 0;
}

std::string Trimmed(const std::string &Text)
{
	const size_t First = Text.find_first_not_of(' ');
	if (First == std::string::npos)
		return std::string();
	return Text.substr(First, Text.find_last_not_of(' ') - First + 1);
}

enum { QUERY_MISSING, QUERY_FOUND, QUERY_BROKEN };

// A value of the target's query ("?name=Town%20melody&code=..."), '+' a space as forms send it
int QueryValue(const std::string &Target, const char *pKey, std::string &Value)
{
	Value.clear();
	const size_t Mark = Target.find('?');
	if (Mark == std::string::npos)
		return QUERY_MISSING;
	const std::string Query = Target.substr(Mark + 1);
	const std::string Key = pKey;
	size_t At = 0;
	while (At <= Query.size()) {
		size_t End = Query.find('&', At);
		if (End == std::string::npos)
			End = Query.size();
		const std::string Pair = Query.substr(At, End - At);
		const size_t Equals = Pair.find('=');
		if (Pair.substr(0, Equals) == Key) {
			std::string Raw = Equals == std::string::npos ? std::string() : Pair.substr(Equals + 1);
			for (size_t i = 0; i < Raw.size(); ++i)
				if (Raw[i] == '+')
					Raw[i] = ' ';
			return UrlDecode(Raw, Value) ? QUERY_FOUND : QUERY_BROKEN;
		}
		At = End + 1;
	}
	return QUERY_MISSING;
}

// The code the request carries: "Authorization: Bearer CODE", or ?code=CODE in its address
std::string CodeOf(const HttpRequest &Request)
{
	const std::string *pAuthorization = Request.Header("Authorization");
	if (pAuthorization != 0 && pAuthorization->size() > 7 && Lower(pAuthorization->substr(0, 7)) == "bearer ")
		return Trimmed(pAuthorization->substr(7));
	std::string Code;
	return QueryValue(Request.Target, "code", Code) == QUERY_FOUND ? Code : std::string();
}

// The page's own requests: an Origin of the Host they were sent to (http, or https through a proxy such as Tailscale's)
bool SameOrigin(const std::string &Origin, const std::string *pHost)
{
	if (pHost == 0 || pHost->empty())
		return false;
	const std::string Text = Lower(Origin), Host = Lower(*pHost);
	return Text == "http://" + Host || Text == "https://" + Host;
}

void AddCommonHeaders(HttpResponse &Response)
{
	Response.Headers.push_back(std::make_pair(std::string("Cache-Control"), std::string("no-store")));
	Response.Headers.push_back(std::make_pair(std::string("X-Content-Type-Options"), std::string("nosniff")));
	Response.Headers.push_back(std::make_pair(std::string("Referrer-Policy"), std::string("no-referrer")));
}

HttpResponse PlainText(int Status, const std::string &Text)
{
	HttpResponse Response;
	Response.Status = Status;
	Response.ContentType = "text/plain; charset=utf-8";
	Response.Body = Text + "\n";
	AddCommonHeaders(Response);
	return Response;
}

HttpResponse JsonReply(int Status, const Json &Body)
{
	HttpResponse Response;
	Response.Status = Status;
	Response.ContentType = "application/json";
	Response.Body = WriteJson(Body);
	AddCommonHeaders(Response);
	return Response;
}

HttpResponse Refused(int Status, const std::string &Text)
{
	Json Body = Json::MakeObject();
	Body.Set("error", Text);
	return JsonReply(Status, Body);
}

HttpResponse WrongMethod(const char *pAllowed, const std::string &Text)
{
	HttpResponse Response = Refused(405, Text);
	Response.Headers.push_back(std::make_pair(std::string("Allow"), std::string(pAllowed)));
	return Response;
}

unsigned int Read16(const std::string &Bytes, size_t At)
{
	return static_cast<unsigned int>(static_cast<unsigned char>(Bytes[At])) |
		static_cast<unsigned int>(static_cast<unsigned char>(Bytes[At + 1])) << 8;
}

unsigned long Read32(const std::string &Bytes, size_t At)
{
	return static_cast<unsigned long>(Read16(Bytes, At)) | static_cast<unsigned long>(Read16(Bytes, At + 2)) << 16;
}

const char *StateName(PhoneTakeState State)
{
	switch (State) {
	case PHONE_CLIP: return "clip";
	case PHONE_FAILED: return "failed";
	default: return "waiting";
	}
}

Json TakeJson(const PhoneTake &Take)
{
	Json Item = Json::MakeObject();
	Item.Set("id", Take.Id);
	Item.Set("idea", Take.Idea);
	Item.Set("state", StateName(Take.State));
	Item.Set("clip", Take.Clip);
	Item.Set("message", Take.Message);
	Item.Set("seconds", static_cast<double>(static_cast<long long>(Take.Seconds * 10.0 + 0.5)) / 10.0);
	Item.Set("arrived", Take.Arrived);
	Item.Set("file", Take.File);
	return Item;
}

}	// namespace

std::string Sha256(const std::string &Bytes)
{
	std::uint32_t State[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
	std::string Message = Bytes;
	const unsigned long long Bits = static_cast<unsigned long long>(Bytes.size()) * 8;
	Message += static_cast<char>(0x80);
	while (Message.size() % 64 != 56)
		Message += '\0';
	for (int i = 7; i >= 0; --i)
		Message += static_cast<char>((Bits >> (8 * i)) & 0xFF);
	for (size_t Block = 0; Block < Message.size(); Block += 64) {
		std::uint32_t w[64];
		for (int t = 0; t < 16; ++t) {
			w[t] = 0;
			for (int k = 0; k < 4; ++k)
				w[t] = w[t] << 8 | static_cast<unsigned char>(Message[Block + static_cast<size_t>(t * 4 + k)]);
		}
		for (int t = 16; t < 64; ++t) {
			const std::uint32_t s0 = Rotate(w[t - 15], 7) ^ Rotate(w[t - 15], 18) ^ (w[t - 15] >> 3);
			const std::uint32_t s1 = Rotate(w[t - 2], 17) ^ Rotate(w[t - 2], 19) ^ (w[t - 2] >> 10);
			w[t] = w[t - 16] + s0 + w[t - 7] + s1;
		}
		std::uint32_t a = State[0], b = State[1], c = State[2], d = State[3], e = State[4], f = State[5], g = State[6], h = State[7];
		for (int t = 0; t < 64; ++t) {
			const std::uint32_t t1 = h + (Rotate(e, 6) ^ Rotate(e, 11) ^ Rotate(e, 25)) + ((e & f) ^ (~e & g)) + ROUNDS[t] + w[t];
			const std::uint32_t t2 = (Rotate(a, 2) ^ Rotate(a, 13) ^ Rotate(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
			h = g;
			g = f;
			f = e;
			e = d + t1;
			d = c;
			c = b;
			b = a;
			a = t1 + t2;
		}
		State[0] += a;
		State[1] += b;
		State[2] += c;
		State[3] += d;
		State[4] += e;
		State[5] += f;
		State[6] += g;
		State[7] += h;
	}
	std::string Digest;
	for (int i = 0; i < 8; ++i)
		for (int k = 3; k >= 0; --k)
			Digest += static_cast<char>((State[i] >> (8 * k)) & 0xFF);
	return Digest;
}

std::string HmacSha256(const std::string &Key, const std::string &Message)
{
	std::string Block = Key.size() > 64 ? Sha256(Key) : Key;
	Block.resize(64, '\0');
	std::string Inner(64, '\0'), Outer(64, '\0');
	for (size_t i = 0; i < 64; ++i) {
		Inner[i] = static_cast<char>(Block[i] ^ 0x36);
		Outer[i] = static_cast<char>(Block[i] ^ 0x5C);
	}
	return Sha256(Outer + Sha256(Inner + Message));
}

std::string PhoneCode(const std::string &Token)
{
	return Hex(HmacSha256(Token, PHONE_CODE_PURPOSE)).substr(0, PHONE_CODE_DIGITS);
}

std::string PhonePageUrl(const std::string &Address, int Port, const std::string &Token)
{
	char Text[32];
	std::snprintf(Text, sizeof(Text), ":%d/phone?code=", Port);
	return "http://" + Address + Text + PhoneCode(Token);
}

bool CheckPhoneWav(const std::string &Bytes, PhoneWav &Info, std::string &Why)
{
	Info = PhoneWav();
	if (Bytes.size() > MAX_PHONE_TAKE_BYTES) {
		Why = "That take is too big for FamiTracker (it takes up to 3 minutes).";
		return false;
	}
	if (Bytes.size() < 12 || Bytes.compare(0, 4, "RIFF") != 0 || Bytes.compare(8, 4, "WAVE") != 0) {
		Why = "That isn't a WAV file.";
		return false;
	}
	bool Format = false, Sound = false;
	unsigned int Tag = 0, Channels = 0, Align = 0, Bits = 0;
	unsigned long Rate = 0, ByteRate = 0, SoundBytes = 0;
	size_t At = 12;
	while (!Sound && At + 8 <= Bytes.size()) {
		const std::string Id = Bytes.substr(At, 4);
		const unsigned long Size = Read32(Bytes, At + 4);
		const size_t Start = At + 8;
		if (Size > Bytes.size() - Start) {
			Why = "The WAV file is cut short.";
			return false;
		}
		if (Id == "fmt ") {
			if (Size < 16) {
				Why = "The WAV file's format is damaged.";
				return false;
			}
			Tag = Read16(Bytes, Start);
			Channels = Read16(Bytes, Start + 2);
			Rate = Read32(Bytes, Start + 4);
			ByteRate = Read32(Bytes, Start + 8);
			Align = Read16(Bytes, Start + 12);
			Bits = Read16(Bytes, Start + 14);
			Format = true;
		}
		else if (Id == "data") {
			if (!Format) {
				Why = "The WAV file's sound comes before its format.";
				return false;
			}
			SoundBytes = Size;
			Sound = true;
		}
		At = Start + Size + (Size & 1);
	}
	if (!Format || !Sound) {
		Why = "The WAV file has no sound in it.";
		return false;
	}
	if (Tag != 1 || Channels != 1 || Bits != 16) {
		Why = "FamiTracker takes 16-bit mono WAV files from the phone page (it makes them so).";
		return false;
	}
	if (Rate < 8000 || Rate > 48000) {
		Why = "The WAV file's sample rate is outside 8000-48000 Hz.";
		return false;
	}
	if (Align != 2 || ByteRate != Rate * 2 || SoundBytes % 2 != 0) {
		Why = "The WAV file's format is damaged.";
		return false;
	}
	Info.SampleRate = static_cast<int>(Rate);
	Info.Samples = SoundBytes / 2;
	Info.Seconds = static_cast<double>(Info.Samples) / static_cast<double>(Rate);
	if (Info.Seconds < MIN_PHONE_TAKE_SECONDS) {
		Why = "That take is too short (under half a second).";
		return false;
	}
	if (Info.Seconds > MAX_PHONE_TAKE_SECONDS) {
		Why = "That take is longer than 3 minutes; FamiTracker takes up to 3 minutes.";
		return false;
	}
	return true;
}

bool IsIdeaName(const std::string &Name)
{
	if (Name.size() > MAX_IDEA_NAME || ValidUtf8(Name) != Name)
		return false;
	for (size_t i = 0; i < Name.size(); ++i) {
		const unsigned char c = static_cast<unsigned char>(Name[i]);
		if (c < 0x20 || c == 0x7F || std::strchr("\\/:*?\"<>|", c) != 0)
			return false;
	}
	return true;
}

PhoneTakes::PhoneTakes(PhoneHost &Host) : m_Host(Host), m_iNextId(1), m_bProcessing(false)
{
}

void PhoneTakes::SetPage(const std::string &Html)
{
	m_sPage = Html;
}

HttpResponse PhoneTakes::Handle(const HttpRequest &Request, const std::string &Token)
{
	enum { ROUTE_PAGE, ROUTE_LIST, ROUTE_TAKE } Route;
	const std::string Path = Request.Path();
	if (Path == "/phone" || Path == "/phone/")
		Route = ROUTE_PAGE;
	else if (Path == "/phone/takes")
		Route = ROUTE_LIST;
	else if (Path == "/phone/take")
		Route = ROUTE_TAKE;
	else
		return PlainText(404, "FamiTracker's phone page is /phone (the QR code in Edit > Record from your phone has its address).");

	const bool Reading = Request.Method == "GET" || Request.Method == "HEAD";
	if (Route != ROUTE_TAKE && !Reading)
		return WrongMethod("GET", "This address is read with GET.");
	if (Route == ROUTE_TAKE && Request.Method != "POST" && Request.Method != "PUT")
		return WrongMethod("POST", "A take is sent with POST, the WAV file as the body.");
	// Only the page itself: another site's page can't send takes or read the list
	const std::string *pOrigin = Request.Header("Origin");
	if (pOrigin != 0 && !SameOrigin(*pOrigin, Request.Header("Host")))
		return Refused(403, "FamiTracker's phone page only answers itself, not other web pages.");
	if (Token.empty())
		return PlainText(403, "FamiTracker has no access code set, so it answers nothing.");
	if (!SameCode(CodeOf(Request), PhoneCode(Token)))
		return Route == ROUTE_PAGE ?
			PlainText(403, "This address's code doesn't work (any more). In FamiTracker, open Edit > Record from your phone and scan "
				"the QR code again.") :
			Refused(403, "The page's code doesn't work any more. In FamiTracker, open Edit > Record from your phone and scan the QR "
				"code again.");

	if (Route == ROUTE_PAGE) {
		if (m_sPage.empty())
			return PlainText(404, "This FamiTracker has no phone page.");
		HttpResponse Response;
		Response.ContentType = "text/html; charset=utf-8";
		Response.Body = m_sPage;
		AddCommonHeaders(Response);
		// Nothing from elsewhere: the page's own script and style, its own requests, its recordings
		Response.Headers.push_back(std::make_pair(std::string("Content-Security-Policy"), std::string("default-src 'none'; "
			"script-src 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'self'; media-src 'self' blob:; img-src 'self' data:; "
			"base-uri 'none'; form-action 'none'; frame-ancestors 'none'")));
		return Response;
	}
	if (Route == ROUTE_LIST)
		return JsonReply(200, ListJson());
	std::string Name;
	if (QueryValue(Request.Target, "name", Name) == QUERY_BROKEN)
		return Refused(400, "The idea's name doesn't read as text.");
	return Receive(Request, Trimmed(Name));
}

HttpResponse PhoneTakes::Receive(const HttpRequest &Request, const std::string &Name)
{
	if (!IsIdeaName(Name))
		return Refused(400, "An idea's name can't hold \\ / : * ? \" < > | or be longer than 60 letters.");
	PhoneWav Wav;
	std::string Why;
	if (!CheckPhoneWav(Request.Body, Wav, Why))
		return Refused(Request.Body.size() > MAX_PHONE_TAKE_BYTES || Wav.Seconds > MAX_PHONE_TAKE_SECONDS ? 413 : 400, Why);

	PhoneTake Take;
	Take.Id = m_iNextId++;
	Take.Idea = Name;
	Take.Seconds = Wav.Seconds;
	const std::string Now = m_Host.Now();	// "2026-10-25 14-03-12"
	Take.Arrived = Now.size() >= 16 ? Now.substr(11, 2) + ":" + Now.substr(14, 2) : Now;
	std::vector<std::string> Names;
	const std::string Plain = "Phone " + Now + ".wav";
	if (!Name.empty() && IsPlainFileName("Phone " + Now + " " + Name + ".wav"))
		Names.push_back("Phone " + Now + " " + Name + ".wav");
	if (IsPlainFileName(Plain))
		Names.push_back(Plain);
	Names.push_back("Phone take.wav");
	std::string Error;
	if (!m_Host.SaveTake(Names, Request.Body, Take.Path, Take.File, Error))
		return Refused(500, Error.empty() ? std::string("FamiTracker couldn't keep the take.") : Error);
	Take.State = PHONE_WAITING;
	Take.Message = "Arrived; it becomes a clip in a moment.";
	m_Takes.push_back(Take);
	// The list keeps the newest; older takes stay in the folder
	for (size_t i = 0; m_Takes.size() > MAX_PHONE_TAKES && i < m_Takes.size(); )
		if (m_Takes[i].State != PHONE_WAITING)
			m_Takes.erase(m_Takes.begin() + static_cast<std::ptrdiff_t>(i));
		else
			++i;
	m_Host.Wake();
	Json Body = Json::MakeObject();
	Body.Set("take", TakeJson(m_Takes.back()));
	return JsonReply(202, Body);
}

bool PhoneTakes::Process()
{
	if (m_bProcessing)
		return Waiting();	// a clip being made let this in
	m_bProcessing = true;
	for (size_t i = 0; i < m_Takes.size(); ++i) {
		if (m_Takes[i].State != PHONE_WAITING)
			continue;
		std::string Why;
		if (!m_Host.CanMakeClip(Why)) {
			m_Takes[i].Message = "Waiting: " + Why + " It becomes a clip once FamiTracker is free; the take is safe in the "
				"Recordings folder.";
			break;
		}
		const int Id = m_Takes[i].Id;
		std::string Clip, Message;
		const bool Made = m_Host.MakeClip(m_Takes[i].Path, m_Takes[i].Idea, Clip, Message);
		// Found again by its ID, in case a take arrived meanwhile and the list moved
		for (size_t k = 0; k < m_Takes.size(); ++k)
			if (m_Takes[k].Id == Id) {
				m_Takes[k].State = Made ? PHONE_CLIP : PHONE_FAILED;
				m_Takes[k].Clip = Made ? Clip : std::string();
				m_Takes[k].Message = Message;
				i = k;
			}
	}
	m_bProcessing = false;
	return Waiting();
}

bool PhoneTakes::Waiting() const
{
	for (size_t i = 0; i < m_Takes.size(); ++i)
		if (m_Takes[i].State == PHONE_WAITING)
			return true;
	return false;
}

Json PhoneTakes::ListJson() const
{
	Json List = Json::MakeArray();
	for (size_t i = m_Takes.size(); i-- > 0; )
		List.Push(TakeJson(m_Takes[i]));
	Json Body = Json::MakeObject();
	Body.Set("takes", List);
	Body.Set("waiting", Waiting());
	return Body;
}

}	// namespace ftapi

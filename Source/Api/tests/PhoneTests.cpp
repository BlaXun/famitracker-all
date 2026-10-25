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

// The phone page (PhoneTakes.h) and its QR code (QrCode.h): the codes
// against a reference encoder's, the page's code, the takes' checks and
// refusals, takes received and made clips (a hummed melody through the
// transcription as Edit > Record from microphone transcribes a take), and the
// page itself (res/phone.html). The route through HandleMcp, with the
// network's checks and place_clip, is TestPhoneRoute in ApiTests.cpp.

#include "../Http.h"
#include "../Json.h"
#include "../Network.h"
#include "../PhoneTakes.h"
#include "../QrCode.h"
#include "../../Transcribe/Transcribe.h"
#include "QrReference.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

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
#define CHECK_HAS(text, part) Check((text).find(part) != std::string::npos, "\"" part "\" in " #text, __LINE__)

const double PI = 3.14159265358979323846;

std::string Hex(const std::string &Bytes)
{
	static const char DIGITS[] = "0123456789abcdef";
	std::string Out;
	for (size_t i = 0; i < Bytes.size(); ++i) {
		Out += DIGITS[static_cast<unsigned char>(Bytes[i]) >> 4];
		Out += DIGITS[static_cast<unsigned char>(Bytes[i]) & 15];
	}
	return Out;
}

// The bytes make_qr_reference.py made for the longer codes
std::string MadeBytes(int Count, int Seed)
{
	std::string Out;
	unsigned long x = static_cast<unsigned long>(Seed);
	for (int i = 0; i < Count; ++i) {
		x = (x * 1103515245UL + 12345UL) & 0x7FFFFFFFUL;
		Out += static_cast<char>((x >> 16) & 0xFF);
	}
	return Out;
}

QrLevel LevelOf(char Letter)
{
	return Letter == 'L' ? QR_LOW : Letter == 'Q' ? QR_QUARTILE : Letter == 'H' ? QR_HIGH : QR_MEDIUM;
}

void TestQrReferences()
{
	const size_t Cases = sizeof(QR_REFERENCES) / sizeof(QR_REFERENCES[0]);
	CHECK(Cases >= 20);
	for (size_t c = 0; c < Cases; ++c) {
		const QrReference &R = QR_REFERENCES[c];
		const std::string Bytes = R.pBytes != 0 ? std::string(R.pBytes, R.Length) : MadeBytes(R.Count, R.Seed);
		CHECK_EQ(Bytes.size(), R.Length);
		QrSymbol S;
		if (!MakeQrSymbol(Bytes, LevelOf(R.Level), S, R.Mask, R.Boost)) {
			Check(false, "the reference's bytes make a code", __LINE__);
			continue;
		}
		char What[64];
		std::snprintf(What, sizeof(What), "QR case %d: version, level, mask", static_cast<int>(c));
		Check(S.Version == R.Version && S.Level == LevelOf(R.MadeLevel) && S.Mask == R.MadeMask && S.Size == 17 + 4 * R.Version,
			What, __LINE__);
		if (S.Version != R.Version)
			continue;
		std::snprintf(What, sizeof(What), "QR case %d: the modules", static_cast<int>(c));
		if (R.pModules[0] != 0) {
			bool Same = true;
			for (int i = 0; i < S.Size * S.Size; ++i) {
				const char Digit = R.pModules[i / 4];
				const int Value = Digit <= '9' ? Digit - '0' : Digit - 'A' + 10;
				const bool Dark = ((Value >> (3 - i % 4)) & 1) != 0;
				Same = Same && Dark == S.Dark(i % S.Size, i / S.Size);
			}
			Check(Same, What, __LINE__);
		}
		else {
			unsigned long long Hash = 0xcbf29ce484222325ULL;
			for (int i = 0; i < S.Size * S.Size; ++i) {
				Hash ^= S.Dark(i % S.Size, i / S.Size) ? 1u : 0u;
				Hash *= 0x100000001b3ULL;
			}
			Check(Hash == R.Hash, What, __LINE__);
		}
	}
}

void TestQrBasics()
{
	QrSymbol S;
	CHECK(MakeQrSymbol("http://192.168.178.20:46600/phone?code=0123456789abcdef01234567", QR_MEDIUM, S));
	CHECK(S.Size == 17 + 4 * S.Version && S.Modules.size() == static_cast<size_t>(S.Size * S.Size));
	CHECK(S.Version >= 4 && S.Version <= 5);	// the address the phone scans: a small code
	// Finders in three corners: dark rings with a light ring inside, the quiet zone light
	CHECK(S.Dark(0, 0) && S.Dark(6, 6) && !S.Dark(1, 1) && S.Dark(3, 3) && !S.Dark(7, 7));
	CHECK(S.Dark(S.Size - 1, 0) && S.Dark(0, S.Size - 1) && !S.Dark(S.Size - 8, 7));
	CHECK(!S.Dark(-1, 0) && !S.Dark(0, -1) && !S.Dark(S.Size, 0) && !S.Dark(0, S.Size));
	CHECK(S.Dark(8, S.Size - 8));	// the module that's always dark
	// Too much for version 40, and what can't be asked
	CHECK(!MakeQrSymbol(std::string(2954, 'x'), QR_LOW, S));
	CHECK(MakeQrSymbol(std::string(2953, 'x'), QR_LOW, S) && S.Version == 40);
	CHECK(!MakeQrSymbol("x", QR_MEDIUM, S, 8) && !MakeQrSymbol("x", QR_MEDIUM, S, -2));
	CHECK(MakeQrSymbol("x", QR_MEDIUM, S, -1, true, 7) && S.Version == 7);
	CHECK(!MakeQrSymbol("x", QR_MEDIUM, S, -1, true, 41));
	// Boost: a short text gets the highest level that fits its version
	CHECK(MakeQrSymbol("hello", QR_LOW, S) && S.Version == 1 && S.Level == QR_HIGH);
	CHECK(MakeQrSymbol("hello", QR_LOW, S, -1, false) && S.Level == QR_LOW);
}

void TestDigests()
{
	CHECK_EQ(Hex(Sha256("")), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
	CHECK_EQ(Hex(Sha256("abc")), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
	CHECK_EQ(Hex(Sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")),
		std::string("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
	CHECK_EQ(Hex(Sha256(std::string(1000000, 'a'))), std::string("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
	// Lengths around a block's end (55, 56 and 64 bytes need one, two and two blocks)
	CHECK_EQ(Hex(Sha256(std::string(55, 'a'))), std::string("9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"));
	CHECK_EQ(Hex(Sha256(std::string(56, 'a'))), std::string("b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a"));
	CHECK_EQ(Hex(Sha256(std::string(64, 'a'))), std::string("ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"));
	// RFC 4231, test cases 1, 2 and 6
	CHECK_EQ(Hex(HmacSha256(std::string(20, '\x0b'), "Hi There")),
		std::string("b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"));
	CHECK_EQ(Hex(HmacSha256("Jefe", "what do ya want for nothing?")),
		std::string("5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"));
	CHECK_EQ(Hex(HmacSha256(std::string(131, '\xaa'), "Test Using Larger Than Block-Size Key - Hash Key First")),
		std::string("60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54"));

	// The page's code: 24 hex digits, the same for an access code, another for another, never the access code
	const std::string Code = PhoneCode("1f3a5b7c9d1e3f5a7b9c1d3e5f7a9b1c");
	CHECK_EQ(Code.size(), 24u);
	CHECK(Code.find_first_not_of("0123456789abcdef") == std::string::npos);
	CHECK_EQ(Code, PhoneCode("1f3a5b7c9d1e3f5a7b9c1d3e5f7a9b1c"));
	CHECK(Code != PhoneCode("1f3a5b7c9d1e3f5a7b9c1d3e5f7a9b1d"));
	CHECK(std::string("1f3a5b7c9d1e3f5a7b9c1d3e5f7a9b1c").find(Code) == std::string::npos);
	CHECK_EQ(Code, Hex(HmacSha256("1f3a5b7c9d1e3f5a7b9c1d3e5f7a9b1c", "FamiTracker phone page")).substr(0, 24));
	CHECK_EQ(PhonePageUrl("192.168.1.10", 46600, "1f3a5b7c9d1e3f5a7b9c1d3e5f7a9b1c"), "http://192.168.1.10:46600/phone?code=" + Code);
}

void Put16(std::string &Out, unsigned long Value)
{
	Out += static_cast<char>(Value & 0xFF);
	Out += static_cast<char>((Value >> 8) & 0xFF);
}

void Put32(std::string &Out, unsigned long Value)
{
	Put16(Out, Value & 0xFFFF);
	Put16(Out, (Value >> 16) & 0xFFFF);
}

// A WAV file as the page writes it (16-bit mono PCM), or of another format for the refusals
std::string WavFile(const std::vector<float> &Samples, unsigned long Rate, unsigned long Channels = 1, unsigned long Bits = 16,
	unsigned long Tag = 1)
{
	const unsigned long Bytes = static_cast<unsigned long>(Samples.size()) * Channels * (Bits / 8);
	std::string Out = "RIFF";
	Put32(Out, 36 + Bytes);
	Out += "WAVEfmt ";
	Put32(Out, 16);
	Put16(Out, Tag);
	Put16(Out, Channels);
	Put32(Out, Rate);
	Put32(Out, Rate * Channels * Bits / 8);
	Put16(Out, Channels * Bits / 8);
	Put16(Out, Bits);
	Out += "data";
	Put32(Out, Bytes);
	for (size_t i = 0; i < Samples.size(); ++i)
		for (unsigned long c = 0; c < Channels; ++c) {
			const long Value = static_cast<long>(std::floor(Samples[i] * 32767.0 + 0.5));
			if (Bits == 16)
				Put16(Out, static_cast<unsigned long>(Value) & 0xFFFF);
			else
				for (unsigned long b = 0; b < Bits / 8; ++b)
					Out += static_cast<char>(b + 1 == Bits / 8 ? (Value >> 8) & 0xFF : 0);
		}
	return Out;
}

std::vector<float> Tone(double Seconds, unsigned long Rate, double Hz = 220.0)
{
	std::vector<float> Out(static_cast<size_t>(Seconds * static_cast<double>(Rate)));
	for (size_t i = 0; i < Out.size(); ++i)
		Out[i] = static_cast<float>(0.4 * std::sin(2.0 * PI * Hz * static_cast<double>(i) / static_cast<double>(Rate)));
	return Out;
}

void TestPhoneWav()
{
	PhoneWav Info;
	std::string Why;
	CHECK(CheckPhoneWav(WavFile(Tone(2.0, 22050), 22050), Info, Why));
	CHECK(Info.SampleRate == 22050 && Info.Samples == 44100 && std::fabs(Info.Seconds - 2.0) < 1e-9);
	CHECK(CheckPhoneWav(WavFile(Tone(1.0, 8000), 8000), Info, Why) && CheckPhoneWav(WavFile(Tone(1.0, 48000), 48000), Info, Why));
	CHECK(CheckPhoneWav(WavFile(Tone(180.0, 22050), 22050), Info, Why));	// three minutes at the page's rate fit
	CHECK(CheckPhoneWav(WavFile(Tone(0.5, 22050), 22050), Info, Why));

	// Other chunks around the sound, an odd-sized one padded to an even length
	std::string Extra = WavFile(Tone(1.0, 22050), 22050);
	std::string List = "LIST";
	Put32(List, 5);
	List += std::string("INFO\0", 5) + '\0';
	Extra.insert(36, List);
	CHECK(CheckPhoneWav(Extra, Info, Why) && Info.Samples == 22050);
	CHECK(CheckPhoneWav(WavFile(Tone(1.0, 22050), 22050) + std::string("LIST\x04\0\0\0abcd", 12), Info, Why));

	struct Refusal { std::string Bytes; const char *pWhy; };
	std::vector<Refusal> Refusals;
	const std::string Good = WavFile(Tone(1.0, 22050), 22050);
	Refusals.push_back(Refusal{ std::string(), "isn't a WAV file" });
	Refusals.push_back(Refusal{ "RIFF", "isn't a WAV file" });
	Refusals.push_back(Refusal{ "RIFX" + Good.substr(4), "isn't a WAV file" });
	Refusals.push_back(Refusal{ Good.substr(0, 8) + "AVI " + Good.substr(12), "isn't a WAV file" });
	Refusals.push_back(Refusal{ Good.substr(0, Good.size() - 2), "cut short" });		// the sound shorter than it says
	Refusals.push_back(Refusal{ Good.substr(0, 12) + Good.substr(36), "sound comes before its format" });
	Refusals.push_back(Refusal{ Good.substr(0, 36), "no sound in it" });
	std::string ShortFormat = Good;
	ShortFormat[16] = 12;
	Refusals.push_back(Refusal{ ShortFormat, "format is damaged" });
	std::string Huge = Good;
	Huge[40] = '\xFF';
	Huge[41] = '\xFF';
	Huge[42] = '\xFF';
	Huge[43] = '\x7F';
	Refusals.push_back(Refusal{ Huge, "cut short" });
	Refusals.push_back(Refusal{ WavFile(Tone(1.0, 22050), 22050, 2), "16-bit mono" });
	Refusals.push_back(Refusal{ WavFile(Tone(1.0, 22050), 22050, 1, 8), "16-bit mono" });
	Refusals.push_back(Refusal{ WavFile(Tone(1.0, 22050), 22050, 1, 24), "16-bit mono" });
	Refusals.push_back(Refusal{ WavFile(Tone(1.0, 22050), 22050, 1, 32, 3), "16-bit mono" });	// floats
	Refusals.push_back(Refusal{ WavFile(Tone(1.0, 4000), 4000), "sample rate" });
	Refusals.push_back(Refusal{ WavFile(Tone(1.0, 96000), 96000), "sample rate" });
	std::string Align = Good;
	Align[32] = 4;
	Refusals.push_back(Refusal{ Align, "format is damaged" });
	std::string Odd = WavFile(Tone(1.0, 22050), 22050) + "x";
	Odd[40] = static_cast<char>(Odd[40] + 1);	// an odd number of bytes of sound
	Refusals.push_back(Refusal{ Odd, "format is damaged" });
	Refusals.push_back(Refusal{ WavFile(Tone(0.4, 22050), 22050), "too short" });
	Refusals.push_back(Refusal{ WavFile(Tone(181.0, 8000), 8000), "longer than 3 minutes" });
	Refusals.push_back(Refusal{ WavFile(Tone(182.0, 22050), 22050), "too big" });
	for (size_t i = 0; i < Refusals.size(); ++i) {
		const bool Refused = !CheckPhoneWav(Refusals[i].Bytes, Info, Why);
		Check(Refused && Why.find(Refusals[i].pWhy) != std::string::npos, Refusals[i].pWhy, __LINE__);
		if (Refused && Why.find(Refusals[i].pWhy) == std::string::npos)
			std::printf("  case %d said: %s\n", static_cast<int>(i), Why.c_str());
	}
	// Cut short at every byte of the header and the first sound
	for (size_t Length = 0; Length < 60; ++Length)
		Check(!CheckPhoneWav(Good.substr(0, Length), Info, Why), "cut short", __LINE__);
}

void TestIdeaNames()
{
	const char *const GOOD[] = { "", "Town melody", "\xC3\x9C" "ber den Wolken", "Night 2 (slow)", "a.b", "Caf\xC3\xA9 & tea" };
	for (size_t i = 0; i < sizeof(GOOD) / sizeof(GOOD[0]); ++i)
		Check(IsIdeaName(GOOD[i]), GOOD[i], __LINE__);
	const char *const BAD[] = { "a/b", "..\\x", "C:x", "x*", "x?", "\"x\"", "<x>", "x|y", "tab\there", "line\nbreak", "\x7F",
		"bad\xFF", "\xC3", "\xED\xA0\x80" };
	for (size_t i = 0; i < sizeof(BAD) / sizeof(BAD[0]); ++i)
		Check(!IsIdeaName(BAD[i]), BAD[i], __LINE__);
	CHECK(IsIdeaName(std::string(60, 'a')) && !IsIdeaName(std::string(61, 'a')));
	CHECK(!IsIdeaName(std::string("x\0y", 3)));
}

// The host for the takes' tests: files kept in memory (or written, for the transcription), a song that's busy or free
class FakePhoneHost : public PhoneHost {
public:
	FakePhoneHost() : Busy(false), Fail(false), WriteFiles(false), Wakes(0), Clips(0) {}

	bool SaveTake(const std::vector<std::string> &Names, const std::string &Bytes, std::string &Path, std::string &File,
		std::string &Error) override
	{
		NamesAsked = Names;
		for (size_t i = 0; i < Names.size(); ++i)
			if (!IsPlainFileName(Names[i])) {
				Error = "not a plain name: " + Names[i];
				return false;
			}
		File = Names[0];
		Path = WriteFiles ? "phone-test " + File : "Recordings/" + File;
		if (WriteFiles) {
			std::ofstream Out(Path.c_str(), std::ios::binary);
			Out.write(Bytes.data(), static_cast<std::streamsize>(Bytes.size()));
		}
		Saved[Path] = Bytes;
		return true;
	}

	bool CanMakeClip(std::string &Why) override
	{
		if (Busy)
			Why = "A dialog is open in FamiTracker.";
		return !Busy;
	}

	// As InsertTake (InsertRecording.cpp) transcribes a take into a clip: what it holds guessed, its notes found, put in
	// the lead's register and on the song's rows (speed 6, tempo 150: 10 rows a second)
	bool MakeClip(const std::string &Path, const std::string &Idea, std::string &Clip, std::string &Message) override
	{
		if (Fail) {
			Message = "Kept in the Recordings folder, but no notes were found in it. Is it loud enough?";
			return false;
		}
		Clip = Idea.empty() ? "Idea " + std::to_string(++Clips) : Idea;
		Message = "In the clips as \"" + Clip + "\".";
		if (!WriteFiles)
			return true;
		transcribe::Audio Audio;
		std::string Error;
		if (!transcribe::ReadWav(Path, Audio, &Error)) {
			Message = Error;
			return false;
		}
		Heard = transcribe::GuessSource(Audio).source;
		const std::vector<transcribe::PitchFrame> Frames = transcribe::TrackPitch(Audio, transcribe::PitchOptionsFor(Heard));
		std::vector<transcribe::Note> Notes = transcribe::SegmentNotes(Frames, transcribe::NoteOptionsFor(Heard));
		if (Notes.empty()) {
			Message = "No notes were found in the recording.";
			return false;
		}
		transcribe::DescribeExpression(Notes, Frames, transcribe::ExpressionOptions());
		transcribe::Transpose(Notes, transcribe::OctaveShiftFor(Notes, 67));
		transcribe::QuantizeOptions Rules;
		Rules.alignToStart = true;
		Melody = transcribe::Quantize(Notes, transcribe::Grid(), Rules);
		return true;
	}

	std::string Now() override { return "2026-10-25 14-03-12"; }
	void Wake() override { ++Wakes; }

	bool Busy, Fail, WriteFiles;
	int Wakes, Clips;
	std::vector<std::string> NamesAsked;
	std::map<std::string, std::string> Saved;
	transcribe::Source Heard;
	std::vector<transcribe::GridNote> Melody;
};

const std::string TOKEN = "1f3a5b7c9d1e3f5a7b9c1d3e5f7a9b1c";

HttpRequest PhoneRequest(const std::string &Method, const std::string &Target, const std::string &Body = std::string(),
	bool WithCode = true)
{
	HttpRequest Request;
	Request.Method = Method;
	Request.Target = Target;
	Request.Version = "HTTP/1.1";
	Request.Headers.push_back(std::make_pair(std::string("Host"), std::string("192.168.1.10:46600")));
	if (WithCode)
		Request.Headers.push_back(std::make_pair(std::string("Authorization"), "Bearer " + PhoneCode(TOKEN)));
	Request.Body = Body;
	Request.Peer = "192.168.1.20";
	return Request;
}

Json Reply(const HttpResponse &Response)
{
	Json Out;
	ParseJson(Response.Body, Out);
	return Out;
}

std::string Text(const Json &Value, const char *pKey)
{
	const Json *pItem = Value.Get(pKey);
	return pItem != 0 && pItem->IsString() ? pItem->String() : std::string();
}

void TestPhoneTakes()
{
	FakePhoneHost Host;
	PhoneTakes Takes(Host);
	Takes.SetPage("<!DOCTYPE html><title>page</title>");
	const std::string Take = WavFile(Tone(1.5, 22050), 22050);

	// The page, by its address's code; the list
	HttpResponse Response = Takes.Handle(PhoneRequest("GET", "/phone?code=" + PhoneCode(TOKEN), "", false), TOKEN);
	CHECK_EQ(Response.Status, 200);
	CHECK_EQ(Response.Body, std::string("<!DOCTYPE html><title>page</title>"));
	bool Policy = false, NoStore = false, NoReferrer = false;
	for (size_t i = 0; i < Response.Headers.size(); ++i) {
		Policy = Policy || (Response.Headers[i].first == "Content-Security-Policy" &&
			Response.Headers[i].second.find("default-src 'none'") != std::string::npos);
		NoStore = NoStore || (Response.Headers[i].first == "Cache-Control" && Response.Headers[i].second == "no-store");
		NoReferrer = NoReferrer || (Response.Headers[i].first == "Referrer-Policy" && Response.Headers[i].second == "no-referrer");
	}
	CHECK(Policy && NoStore && NoReferrer);
	CHECK_EQ(Takes.Handle(PhoneRequest("HEAD", "/phone"), TOKEN).Status, 200);
	CHECK_EQ(Takes.Handle(PhoneRequest("GET", "/phone/"), TOKEN).Status, 200);
	Response = Takes.Handle(PhoneRequest("GET", "/phone/takes"), TOKEN);
	CHECK_EQ(Response.Status, 200);
	CHECK(Reply(Response).Get("takes") != 0 && Reply(Response).Get("takes")->Size() == 0);

	// Refusals: no code, a wrong one, the access code itself, no access code set, other web pages, other methods and routes
	CHECK_EQ(Takes.Handle(PhoneRequest("GET", "/phone", "", false), TOKEN).Status, 403);
	CHECK_HAS(Takes.Handle(PhoneRequest("GET", "/phone", "", false), TOKEN).Body, "scan the QR code again");
	CHECK_EQ(Takes.Handle(PhoneRequest("GET", "/phone?code=" + PhoneCode(TOKEN).substr(0, 23), "", false), TOKEN).Status, 403);
	CHECK_EQ(Takes.Handle(PhoneRequest("GET", "/phone?code=" + PhoneCode(TOKEN) + "0", "", false), TOKEN).Status, 403);
	CHECK_EQ(Takes.Handle(PhoneRequest("GET", "/phone?code=" + TOKEN, "", false), TOKEN).Status, 403);
	CHECK_EQ(Takes.Handle(PhoneRequest("GET", "/phone?code=" + PhoneCode("other"), "", false), TOKEN).Status, 403);
	CHECK_EQ(Takes.Handle(PhoneRequest("GET", "/phone"), std::string()).Status, 403);
	CHECK_EQ(Takes.Handle(PhoneRequest("GET", "/phone?other=1&code=" + PhoneCode(TOKEN) + "&x", "", false), TOKEN).Status, 200);
	HttpRequest Foreign = PhoneRequest("POST", "/phone/take?name=Town", Take);
	Foreign.Headers.push_back(std::make_pair(std::string("Origin"), std::string("http://evil.example")));
	CHECK_EQ(Takes.Handle(Foreign, TOKEN).Status, 403);
	Foreign.Headers.back().second = "http://192.168.1.10:46601";	// another port is another site
	CHECK_EQ(Takes.Handle(Foreign, TOKEN).Status, 403);
	Foreign.Headers.back().second = "null";
	CHECK_EQ(Takes.Handle(Foreign, TOKEN).Status, 403);
	HttpRequest Lister = PhoneRequest("GET", "/phone/takes");
	Lister.Headers.push_back(std::make_pair(std::string("Origin"), std::string("https://evil.example")));
	CHECK_EQ(Takes.Handle(Lister, TOKEN).Status, 403);
	CHECK(Host.Saved.empty());
	CHECK_EQ(Takes.Handle(PhoneRequest("POST", "/phone", Take), TOKEN).Status, 405);
	CHECK_EQ(Takes.Handle(PhoneRequest("DELETE", "/phone/takes"), TOKEN).Status, 405);
	CHECK_EQ(Takes.Handle(PhoneRequest("GET", "/phone/take"), TOKEN).Status, 405);
	CHECK_EQ(Takes.Handle(PhoneRequest("OPTIONS", "/phone/take"), TOKEN).Status, 405);
	CHECK_EQ(Takes.Handle(PhoneRequest("GET", "/phone/../mcp"), TOKEN).Status, 404);
	CHECK_EQ(Takes.Handle(PhoneRequest("GET", "/phone/files/x.wav"), TOKEN).Status, 404);
	CHECK(Host.Saved.empty() && Host.Wakes == 0);

	// Names that can't name a file and a clip, and takes that aren't what the page sends
	const char *const BAD_NAMES[] = { "..%5Cx", "a%2Fb", "x%3A", "%00", "tab%09", "%FF", "bad%2", "%22quoted%22" };
	for (size_t i = 0; i < sizeof(BAD_NAMES) / sizeof(BAD_NAMES[0]); ++i)
		Check(Takes.Handle(PhoneRequest("POST", std::string("/phone/take?name=") + BAD_NAMES[i], Take), TOKEN).Status == 400,
			BAD_NAMES[i], __LINE__);
	CHECK_EQ(Takes.Handle(PhoneRequest("POST", "/phone/take?name=" + std::string(61, 'a'), Take), TOKEN).Status, 400);
	CHECK_EQ(Takes.Handle(PhoneRequest("POST", "/phone/take", "not a wav"), TOKEN).Status, 400);
	CHECK_EQ(Takes.Handle(PhoneRequest("POST", "/phone/take", WavFile(Tone(1.0, 22050), 22050, 2)), TOKEN).Status, 400);
	CHECK_EQ(Takes.Handle(PhoneRequest("POST", "/phone/take", WavFile(Tone(0.2, 22050), 22050)), TOKEN).Status, 400);
	CHECK_EQ(Takes.Handle(PhoneRequest("POST", "/phone/take", WavFile(Tone(181.0, 8000), 8000)), TOKEN).Status, 413);
	CHECK_EQ(Takes.Handle(PhoneRequest("POST", "/phone/take", std::string(MAX_PHONE_TAKE_BYTES + 1, 'x')), TOKEN).Status, 413);
	CHECK_HAS(Takes.Handle(PhoneRequest("POST", "/phone/take", WavFile(Tone(0.2, 22050), 22050)), TOKEN).Body, "too short");
	CHECK(Host.Saved.empty() && Host.Wakes == 0 && Takes.Takes().empty());

	// A take arrives: kept under its idea's name, waiting to become a clip
	Response = Takes.Handle(PhoneRequest("POST", "/phone/take?name=%20Town%20melody%20", Take), TOKEN);
	CHECK_EQ(Response.Status, 202);
	const Json Arrived = Reply(Response);
	const Json *pTake = Arrived.Get("take");
	CHECK(pTake != 0 && Text(*pTake, "idea") == "Town melody" && Text(*pTake, "state") == "waiting" &&
		Text(*pTake, "arrived") == "14:03" && Text(*pTake, "file") == "Phone 2026-10-25 14-03-12 Town melody.wav");
	CHECK(pTake != 0 && pTake->Get("seconds") != 0 && pTake->Get("seconds")->Number() == 1.5);
	CHECK(Host.NamesAsked.size() == 3 && Host.NamesAsked[1] == "Phone 2026-10-25 14-03-12.wav");
	CHECK(Host.Saved.size() == 1 && Host.Saved.begin()->second == Take && Host.Wakes == 1);
	CHECK(Takes.Waiting());
	// While FamiTracker is busy it waits, and the page hears why
	Host.Busy = true;
	CHECK(Takes.Process());
	CHECK(Takes.Takes()[0].State == PHONE_WAITING);
	CHECK_HAS(Takes.Takes()[0].Message, "A dialog is open in FamiTracker.");
	CHECK_HAS(Takes.Takes()[0].Message, "safe in the Recordings folder");
	Response = Takes.Handle(PhoneRequest("GET", "/phone/takes"), TOKEN);
	CHECK(Reply(Response).Get("waiting") != 0 && Reply(Response).Get("waiting")->Bool());
	// Then a clip of its name
	Host.Busy = false;
	CHECK(!Takes.Process());
	CHECK(Takes.Takes()[0].State == PHONE_CLIP && Takes.Takes()[0].Clip == "Town melody");
	CHECK_EQ(Takes.Takes()[0].Message, std::string("In the clips as \"Town melody\"."));

	// Without a name it's numbered; a name that can't end a file's name leaves it out of the file's; one that fails says why
	Response = Takes.Handle(PhoneRequest("POST", "/phone/take", Take), TOKEN);
	CHECK(Response.Status == 202 && Host.NamesAsked.size() == 2 && Host.NamesAsked[0] == "Phone 2026-10-25 14-03-12.wav");
	Response = Takes.Handle(PhoneRequest("POST", "/phone/take?name=" + std::string(60, 'n'), Take), TOKEN);
	CHECK(Response.Status == 202 && Host.NamesAsked[0] == "Phone 2026-10-25 14-03-12 " + std::string(60, 'n') + ".wav");
	Response = Takes.Handle(PhoneRequest("POST", "/phone/take?name=Caf%C3%A9+tune", Take), TOKEN);
	CHECK(Response.Status == 202 && Text(*Reply(Response).Get("take"), "idea") == "Caf\xC3\xA9 tune");
	CHECK(!Takes.Process());
	CHECK(Takes.Takes()[1].Clip == "Idea 1" && Takes.Takes()[3].Clip == "Caf\xC3\xA9 tune");
	Host.Fail = true;
	Takes.Handle(PhoneRequest("POST", "/phone/take?name=Quiet", Take), TOKEN);
	CHECK(!Takes.Process());
	CHECK(Takes.Takes().back().State == PHONE_FAILED && Takes.Takes().back().Clip.empty());
	CHECK_HAS(Takes.Takes().back().Message, "Is it loud enough?");
	Host.Fail = false;

	// The list: newest first, as JSON the page reads
	Response = Takes.Handle(PhoneRequest("GET", "/phone/takes"), TOKEN);
	const Json List = Reply(Response);
	CHECK(List.Get("takes") != 0 && List.Get("takes")->Size() == 5 && List.Get("waiting") != 0 && !List.Get("waiting")->Bool());
	CHECK(Text(List.Get("takes")->At(0), "idea") == "Quiet" && Text(List.Get("takes")->At(0), "state") == "failed");
	CHECK(Text(List.Get("takes")->At(4), "clip") == "Town melody" && Text(List.Get("takes")->At(4), "state") == "clip");
	CHECK(Response.ContentType == "application/json");

	// It keeps the newest takes; the others stay in the folder
	for (int i = 0; i < 50; ++i) {
		Takes.Handle(PhoneRequest("POST", "/phone/take?name=Many", Take), TOKEN);
		Takes.Process();
	}
	CHECK(Takes.Takes().size() <= MAX_PHONE_TAKES && Takes.Takes().back().Idea == "Many");
	CHECK(Takes.Takes().front().Id > 5);
	// Takes waiting while busy are never dropped
	Host.Busy = true;
	for (int i = 0; i < 45; ++i)
		Takes.Handle(PhoneRequest("POST", "/phone/take?name=Later", Take), TOKEN);
	CHECK(Takes.Takes().size() == 45);
	Host.Busy = false;
	CHECK(!Takes.Process());
}

// Humming: a voice's tone (a fundamental with falling overtones and a slight vibrato), each note sung on its own breath
std::vector<float> Hum(const int *pMidi, size_t Count, double NoteSeconds, double BreathSeconds, unsigned long Rate)
{
	std::vector<float> Out;
	double Phase = 0.0;
	for (size_t n = 0; n < Count; ++n) {
		const double Hz = 440.0 * std::pow(2.0, (pMidi[n] - 69) / 12.0);
		const size_t Length = static_cast<size_t>(NoteSeconds * static_cast<double>(Rate));
		for (size_t i = 0; i < Length; ++i) {
			const double t = static_cast<double>(i) / static_cast<double>(Rate);
			Phase += 2.0 * PI * Hz * (1.0 + 0.004 * std::sin(2.0 * PI * 5.5 * t)) / static_cast<double>(Rate);
			const double Envelope = std::min(1.0, std::min(t / 0.03, (NoteSeconds - t) / 0.04));
			double Value = 0.0;
			for (int h = 1; h <= 6; ++h)
				Value += std::sin(h * Phase) / h;
			Out.push_back(static_cast<float>(0.25 * Envelope * Value));
		}
		Out.insert(Out.end(), static_cast<size_t>(BreathSeconds * static_cast<double>(Rate)), 0.0f);
	}
	return Out;
}

// A hummed idea, sent as the page sends it, comes out as its notes
void TestTakeBecomesClip()
{
	FakePhoneHost Host;
	Host.WriteFiles = true;
	PhoneTakes Takes(Host);
	const int MELODY[] = { 60, 62, 64, 67, 64, 60 };	// C D E G E C
	const size_t Notes = sizeof(MELODY) / sizeof(MELODY[0]);
	const std::string Bytes = WavFile(Hum(MELODY, Notes, 0.45, 0.12, 22050), 22050);
	CHECK_EQ(Takes.Handle(PhoneRequest("POST", "/phone/take?name=Town%20melody", Bytes), TOKEN).Status, 202);
	CHECK(!Takes.Process());
	CHECK(Takes.Takes().size() == 1 && Takes.Takes()[0].State == PHONE_CLIP && Takes.Takes()[0].Clip == "Town melody");
	CHECK(Host.Heard == transcribe::Source::Voice);
	CHECK_EQ(Host.Melody.size(), Notes);
	if (Host.Melody.size() == Notes) {
		bool Same = Host.Melody[0].midi % 12 == 0;
		for (size_t i = 0; i < Notes; ++i)
			Same = Same && Host.Melody[i].midi - Host.Melody[0].midi == MELODY[i] - MELODY[0];
		CHECK(Same);
		// On the song's rows: 10 a second, a note every 0.57 seconds from the first row
		CHECK_EQ(Host.Melody[0].row, 0);
		bool Rows = true;
		for (size_t i = 1; i < Notes; ++i)
			Rows = Rows && std::abs(Host.Melody[i].row - static_cast<int>(std::floor(5.7 * i + 0.5))) <= 1 &&
				Host.Melody[i - 1].length >= 3;
		CHECK(Rows);
	}
	for (std::map<std::string, std::string>::const_iterator i = Host.Saved.begin(); i != Host.Saved.end(); ++i)
		std::remove(i->first.c_str());
}

// The page as FamiTracker.exe serves it
void TestPhonePage()
{
	std::ifstream File(PHONE_PAGE, std::ios::binary);
	std::stringstream Read;
	Read << File.rdbuf();
	const std::string Page = Read.str();
	CHECK(Page.size() > 2000 && Page.size() < 40000);
	bool Ascii = true;
	for (size_t i = 0; i < Page.size(); ++i)
		Ascii = Ascii && static_cast<unsigned char>(Page[i]) < 0x80;
	CHECK(Ascii);
	// Nothing from elsewhere: no addresses of other sites, no scripts, styles or pictures to fetch
	CHECK(Page.find("http://") == std::string::npos && Page.find("https://") == std::string::npos);
	CHECK(Page.find("src=") == std::string::npos && Page.find("<link") == std::string::npos && Page.find("@import") == std::string::npos);
	CHECK(Page.find("innerHTML") == std::string::npos);	// names are shown as text
	// What it uses: its routes with the code, the microphone where the page is secure, the phone's recorder elsewhere,
	// and the WAV file FamiTracker takes
	CHECK_HAS(Page, "'/phone/take?name=' + encodeURIComponent(");
	CHECK_HAS(Page, "'/phone/takes'");
	CHECK_HAS(Page, "'Authorization': 'Bearer ' + code");
	CHECK_HAS(Page, "window.isSecureContext");
	CHECK_HAS(Page, "getUserMedia");
	CHECK_HAS(Page, "MediaRecorder");
	CHECK_HAS(Page, "accept=\"audio/*\" capture");
	CHECK_HAS(Page, "decodeAudioData");
	CHECK_HAS(Page, "OfflineAudioContext");
	CHECK_HAS(Page, "RATE = 22050, MAX_SECONDS = 180, MIN_SECONDS = 0.5, MAX_NAME_BYTES = 60");
	CHECK_HAS(Page, "<meta name=\"viewport\"");
	CHECK(22050.0 * 2.0 * MAX_PHONE_TAKE_SECONDS + 44.0 <= static_cast<double>(MAX_PHONE_TAKE_BYTES));
	CHECK(MAX_IDEA_NAME == 60 && MAX_PHONE_TAKE_SECONDS == 180.0 && MIN_PHONE_TAKE_SECONDS == 0.5);
}

}	// namespace

int main()
{
	TestQrReferences();
	TestQrBasics();
	TestDigests();
	TestPhoneWav();
	TestIdeaNames();
	TestPhoneTakes();
	TestTakeBecomesClip();
	TestPhonePage();
	std::printf("%d checks, %d failed\n", g_Checks, g_Failures);
	return g_Failures == 0 ? 0 : 1;
}

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

#include "Network.h"
#include "Json.h"

#include <cstdio>
#include <cstring>

namespace ftapi {

namespace {

std::string Lower(const std::string &Text)
{
	std::string Out = Text;
	for (size_t i = 0; i < Out.size(); ++i)
		if (Out[i] >= 'A' && Out[i] <= 'Z')
			Out[i] = static_cast<char>(Out[i] - 'A' + 'a');
	return Out;
}

// The name and its extension's lower case letters ("Song.NSF" -> "nsf"); empty without one
std::string Extension(const std::string &Name)
{
	const size_t Dot = Name.rfind('.');
	return Dot == std::string::npos ? std::string() : Lower(Name.substr(Dot + 1));
}

int HexDigit(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

}	// namespace

bool ParseIpv4(const std::string &Text, unsigned long &Address)
{
	unsigned long Value = 0;
	int Parts = 0;
	size_t i = 0;
	while (Parts < 4) {
		const size_t Start = i;
		unsigned long Part = 0;
		while (i < Text.size() && Text[i] >= '0' && Text[i] <= '9' && i - Start < 3)
			Part = Part * 10 + static_cast<unsigned long>(Text[i++] - '0');
		const size_t Digits = i - Start;
		if (Digits == 0 || Part > 255 || (Digits > 1 && Text[Start] == '0'))
			return false;
		Value = (Value << 8) | Part;
		if (++Parts < 4) {
			if (i >= Text.size() || Text[i] != '.')
				return false;
			++i;
		}
	}
	if (i != Text.size())
		return false;
	Address = Value & 0xFFFFFFFFUL;
	return true;
}

std::string Ipv4Text(unsigned long Address)
{
	char Text[16];
	std::snprintf(Text, sizeof(Text), "%lu.%lu.%lu.%lu", (Address >> 24) & 0xFF, (Address >> 16) & 0xFF, (Address >> 8) & 0xFF,
		Address & 0xFF);
	return Text;
}

bool IsLoopbackIpv4(unsigned long Address)
{
	return (Address & 0xFF000000UL) == 0x7F000000UL;
}

bool IsPrivateIpv4(unsigned long Address)
{
	return IsLoopbackIpv4(Address) ||
		(Address & 0xFF000000UL) == 0x0A000000UL ||		// 10.0.0.0/8
		(Address & 0xFFF00000UL) == 0xAC100000UL ||		// 172.16.0.0/12
		(Address & 0xFFFF0000UL) == 0xC0A80000UL ||		// 192.168.0.0/16
		(Address & 0xFFFF0000UL) == 0xA9FE0000UL ||		// 169.254.0.0/16, link-local
		(Address & 0xFFC00000UL) == 0x64400000UL;		// 100.64.0.0/10, shared (Tailscale)
}

bool MayConnect(const std::string &Peer, const std::vector<std::string> &Own)
{
	unsigned long Address = 0;
	if (!ParseIpv4(Peer, Address))
		return false;
	if (IsPrivateIpv4(Address))
		return true;
	for (size_t i = 0; i < Own.size(); ++i) {
		unsigned long Mine = 0;
		if (ParseIpv4(Own[i], Mine) && Mine == Address)
			return true;
	}
	return false;
}

bool NamesThisComputer(const std::string &Host, const std::vector<std::string> &Own, const std::string &ComputerName)
{
	if (Host.empty() || Host[0] == '[')
		return false;	// an IPv6 address: FamiTracker listens on IPv4 only
	std::string Name = Lower(Host.substr(0, Host.find(':')));
	if (!Name.empty() && Name[Name.size() - 1] == '.')
		Name.erase(Name.size() - 1);	// "desktop-pc.fritz.box."
	if (Name.empty())
		return false;
	unsigned long Address = 0;
	if (ParseIpv4(Name, Address)) {
		for (size_t i = 0; i < Own.size(); ++i) {
			unsigned long Mine = 0;
			if (ParseIpv4(Own[i], Mine) && Mine == Address)
				return true;
		}
		return false;
	}
	const std::string Computer = Lower(ComputerName);
	if (Computer.empty())
		return false;
	return Name == Computer || (Name.size() > Computer.size() + 1 && Name.compare(0, Computer.size(), Computer) == 0 &&
		Name[Computer.size()] == '.');
}

bool IsPlainFileName(const std::string &Name)
{
	if (Name.empty() || Name.size() > 200 || Name == "." || Name == ".." || ValidUtf8(Name) != Name)
		return false;
	for (size_t i = 0; i < Name.size(); ++i) {
		const unsigned char c = static_cast<unsigned char>(Name[i]);
		if (c < 0x20 || c == 0x7F || std::strchr("\\/:*?\"<>|", c) != 0)
			return false;
	}
	if (Name[0] == ' ' || Name[Name.size() - 1] == ' ' || Name[Name.size() - 1] == '.')
		return false;	// Windows drops them, so the file would be another
	// Devices, with any extension: "NUL.ogg" is the NUL device on Windows
	const std::string Base = Lower(Name.substr(0, Name.find('.')));
	static const char *const DEVICES[] = { "con", "prn", "aux", "nul", "conin$", "conout$" };
	for (size_t i = 0; i < sizeof(DEVICES) / sizeof(DEVICES[0]); ++i)
		if (Base == DEVICES[i])
			return false;
	if (Base.size() == 4 && (Base.compare(0, 3, "com") == 0 || Base.compare(0, 3, "lpt") == 0) && Base[3] >= '0' && Base[3] <= '9')
		return false;
	return true;
}

bool IsInboxFile(const std::string &Name)
{
	static const char *const KINDS[] = { "nsf", "nsfe", "m3u", "mod", "s3m", "xm", "it", "mid", "midi", "rmi", "ftm", "wav" };
	const std::string Kind = Extension(Name);
	for (size_t i = 0; i < sizeof(KINDS) / sizeof(KINDS[0]); ++i)
		if (Kind == KINDS[i])
			return IsPlainFileName(Name) && Name.size() > Kind.size() + 1;
	return false;
}

bool IsExportFile(const std::string &Name)
{
	return Extension(Name) == "ogg" && Name.size() > 4 && IsPlainFileName(Name);
}

const char *ExportMimeType(const std::string &Name)
{
	(void)Name;
	return "audio/ogg";
}

std::string ExportUri(const std::string &Name)
{
	return EXPORT_URI_PREFIX + UrlEncode(Name);
}

bool ParseExportUri(const std::string &Uri, std::string &Name)
{
	const std::string Prefix = EXPORT_URI_PREFIX;
	Name.clear();
	return Uri.size() > Prefix.size() && Lower(Uri.substr(0, Prefix.size())) == Prefix && Uri.find_first_of("?#") == std::string::npos &&
		UrlDecode(Uri.substr(Prefix.size()), Name) && IsExportFile(Name);
}

bool IsTailscaleIpv4(unsigned long Address)
{
	return (Address & 0xFFC00000UL) == 0x64400000UL;
}

bool QueryParameter(const std::string &Target, const char *pKey, std::string &Value)
{
	Value.clear();
	const size_t Mark = Target.find('?');
	if (Mark == std::string::npos)
		return false;
	const std::string Query = Target.substr(Mark + 1, Target.find('#') == std::string::npos ? std::string::npos :
		Target.find('#') - Mark - 1);
	size_t At = 0;
	while (At <= Query.size()) {
		size_t End = Query.find('&', At);
		if (End == std::string::npos)
			End = Query.size();
		const std::string Pair = Query.substr(At, End - At);
		const size_t Equals = Pair.find('=');
		if (Pair.substr(0, Equals) == pKey) {
			std::string Raw = Equals == std::string::npos ? std::string() : Pair.substr(Equals + 1);
			for (size_t i = 0; i < Raw.size(); ++i)
				if (Raw[i] == '+')
					Raw[i] = ' ';
			return UrlDecode(Raw, Value);
		}
		At = End + 1;
	}
	return false;
}

namespace {

// Digits as a number, as big as a size_t holds (more saturate); false without digits or with anything else
bool RangeNumber(const std::string &Text, size_t &Value)
{
	Value = 0;
	if (Text.empty())
		return false;
	const size_t Most = static_cast<size_t>(-1);
	for (size_t i = 0; i < Text.size(); ++i) {
		if (Text[i] < '0' || Text[i] > '9')
			return false;
		const size_t Digit = static_cast<size_t>(Text[i] - '0');
		Value = Value > (Most - Digit) / 10 ? Most : Value * 10 + Digit;
	}
	return true;
}

std::string TrimSpaces(const std::string &Text)
{
	const size_t First = Text.find_first_not_of(" \t");
	return First == std::string::npos ? std::string() : Text.substr(First, Text.find_last_not_of(" \t") - First + 1);
}

}	// namespace

ByteRangeResult ParseByteRange(const std::string &Header, size_t Size, size_t &First, size_t &Last)
{
	First = 0;
	Last = Size > 0 ? Size - 1 : 0;
	const std::string Text = TrimSpaces(Header);
	const size_t Equals = Text.find('=');
	if (Equals == std::string::npos || Lower(TrimSpaces(Text.substr(0, Equals))) != "bytes")
		return RANGE_WHOLE;
	const std::string Spec = TrimSpaces(Text.substr(Equals + 1));
	const size_t Dash = Spec.find('-');
	if (Spec.find(',') != std::string::npos || Dash == std::string::npos)
		return RANGE_WHOLE;
	const std::string From = TrimSpaces(Spec.substr(0, Dash)), To = TrimSpaces(Spec.substr(Dash + 1));
	size_t Start = 0, End = 0;
	if (From.empty()) {
		// The last bytes: "-500"
		if (!RangeNumber(To, End))
			return RANGE_WHOLE;
		if (End == 0 || Size == 0)
			return RANGE_UNSATISFIABLE;
		First = End >= Size ? 0 : Size - End;
		Last = Size - 1;
		return RANGE_PART;
	}
	if (!RangeNumber(From, Start) || (!To.empty() && (!RangeNumber(To, End) || End < Start)))
		return RANGE_WHOLE;
	if (Start >= Size)
		return RANGE_UNSATISFIABLE;
	First = Start;
	Last = To.empty() || End >= Size ? Size - 1 : End;
	return RANGE_PART;
}

std::string UrlEncode(const std::string &Text)
{
	static const char HEX[] = "0123456789ABCDEF";
	std::string Out;
	for (size_t i = 0; i < Text.size(); ++i) {
		const unsigned char c = static_cast<unsigned char>(Text[i]);
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' || c == '~')
			Out += static_cast<char>(c);
		else {
			Out += '%';
			Out += HEX[c >> 4];
			Out += HEX[c & 15];
		}
	}
	return Out;
}

bool UrlDecode(const std::string &Text, std::string &Out)
{
	Out.clear();
	for (size_t i = 0; i < Text.size(); ++i) {
		if (Text[i] != '%') {
			Out += Text[i];
			continue;
		}
		if (i + 2 >= Text.size())
			return false;
		const int High = HexDigit(Text[i + 1]), Low = HexDigit(Text[i + 2]);
		if (High < 0 || Low < 0)
			return false;
		Out += static_cast<char>(High * 16 + Low);
		i += 2;
	}
	return true;
}

bool Base64Decode(const std::string &Text, std::string &Bytes)
{
	Bytes.clear();
	Bytes.reserve(Text.size() / 4 * 3 + 3);
	unsigned long Bits = 0;
	int Count = 0;			// bits waiting in Bits
	size_t Digits = 0, Padding = 0;
	for (size_t i = 0; i < Text.size(); ++i) {
		const char c = Text[i];
		int Digit;
		if (c >= 'A' && c <= 'Z') Digit = c - 'A';
		else if (c >= 'a' && c <= 'z') Digit = c - 'a' + 26;
		else if (c >= '0' && c <= '9') Digit = c - '0' + 52;
		else if (c == '+' || c == '-') Digit = 62;
		else if (c == '/' || c == '_') Digit = 63;
		else if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
			continue;
		else if (c == '=') {
			++Padding;
			continue;
		}
		else
			return false;
		if (Padding > 0)
			return false;	// digits after the padding
		++Digits;
		Bits = ((Bits << 6) | static_cast<unsigned long>(Digit)) & 0xFFFFFFUL;
		Count += 6;
		if (Count >= 8) {
			Count -= 8;
			Bytes += static_cast<char>((Bits >> Count) & 0xFF);
		}
	}
	// A last group of one digit can't be; padding only fills a group of four
	if (Digits % 4 == 1 || Padding > 2 || (Padding > 0 && (Digits + Padding) % 4 != 0))
		return false;
	return true;
}

}	// namespace ftapi

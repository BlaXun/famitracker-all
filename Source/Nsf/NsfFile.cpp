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

#include "NsfFile.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace nsf {

namespace {

const size_t HEADER_SIZE = 0x80;
const int NTSC_SPEED = 16639;	// what most NSFs give for 60 Hz
const int PAL_SPEED = 19997;

uint16_t Word(const uint8_t *p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t Long(const uint8_t *p) { return uint32_t(p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24)); }

// A fixed-size string field, up to its first NUL
std::string Field(const uint8_t *p, size_t Size)
{
	size_t n = 0;
	while (n < Size && p[n] != 0)
		++n;
	return std::string(reinterpret_cast<const char *>(p), n);
}

// NUL-separated strings in a chunk
std::vector<std::string> Strings(const uint8_t *p, size_t Size)
{
	std::vector<std::string> Out;
	size_t Start = 0;
	for (size_t i = 0; i < Size; ++i) {
		if (p[i] == 0) {
			Out.push_back(std::string(reinterpret_cast<const char *>(p + Start), i - Start));
			Start = i + 1;
		}
	}
	if (Start < Size)
		Out.push_back(std::string(reinterpret_cast<const char *>(p + Start), Size - Start));
	return Out;
}

// The chunks of NSFe metadata (also used after an NSF2's data): track names,
// times, the authors. Chunks that are part of NSFe itself are handled by the
// caller. False when they're cut short.
bool ReadMetadataChunk(const char *Id, const uint8_t *p, size_t Size, NsfFile &Out)
{
	if (!std::memcmp(Id, "tlbl", 4))
		Out.TrackNames = Strings(p, Size);
	else if (!std::memcmp(Id, "auth", 4)) {
		const std::vector<std::string> s = Strings(p, Size);
		if (s.size() > 0 && !s[0].empty()) Out.Title = s[0];
		if (s.size() > 1 && !s[1].empty()) Out.Artist = s[1];
		if (s.size() > 2 && !s[2].empty()) Out.Copyright = s[2];
		if (s.size() > 3) Out.Ripper = s[3];
	}
	else if (!std::memcmp(Id, "time", 4)) {
		Out.TrackTimes.clear();
		for (size_t i = 0; i + 4 <= Size; i += 4)
			Out.TrackTimes.push_back(int(Long(p + i)));
	}
	else if (!std::memcmp(Id, "fade", 4)) {
		Out.TrackFades.clear();
		for (size_t i = 0; i + 4 <= Size; i += 4)
			Out.TrackFades.push_back(int(Long(p + i)));
	}
	else if (!std::memcmp(Id, "plst", 4)) {
		Out.Playlist.assign(p, p + Size);
	}
	else if (!std::memcmp(Id, "RATE", 4)) {
		if (Size >= 2 && Word(p) != 0) Out.SpeedNtsc = Word(p);
		if (Size >= 4 && Word(p + 2) != 0) Out.SpeedPal = Word(p + 2);
	}
	return true;
}

bool ParseChunks(const uint8_t *p, size_t Size, NsfFile &Out, bool Nsfe, std::string &Error)
{
	bool HaveInfo = !Nsfe, HaveData = !Nsfe;
	size_t Pos = 0;
	while (Pos + 8 <= Size) {
		const uint32_t Length = Long(p + Pos);
		const char *Id = reinterpret_cast<const char *>(p + Pos + 4);
		Pos += 8;
		if (Length > Size - Pos) {
			Error = "a chunk runs past the end of the file";
			return false;
		}
		const uint8_t *c = p + Pos;
		if (Nsfe && !std::memcmp(Id, "INFO", 4)) {
			if (Length < 9) {
				Error = "the INFO chunk is too short";
				return false;
			}
			Out.LoadAddress = Word(c);
			Out.InitAddress = Word(c + 2);
			Out.PlayAddress = Word(c + 4);
			Out.Pal = (c[6] & 0x03) == 1;
			Out.Dual = (c[6] & 0x02) != 0;
			Out.Chips = c[7];
			Out.Songs = Length > 8 ? c[8] : 1;
			Out.StartSong = Length > 9 ? c[9] : 0;
			HaveInfo = true;
		}
		else if (Nsfe && !std::memcmp(Id, "DATA", 4)) {
			Out.Data.assign(c, c + Length);
			HaveData = true;
		}
		else if (Nsfe && !std::memcmp(Id, "BANK", 4)) {
			for (uint32_t i = 0; i < 8; ++i)
				Out.Banks[i] = i < Length ? c[i] : 0;
			Out.Bankswitched = true;
		}
		else if (!std::memcmp(Id, "NEND", 4)) {
			break;
		}
		else
			ReadMetadataChunk(Id, c, Length, Out);
		Pos += Length;
	}
	if (!HaveInfo || !HaveData) {
		Error = "the NSFe file has no INFO or DATA chunk";
		return false;
	}
	return true;
}

}	// namespace

NsfFile::NsfFile() :
	Nsfe(false), Version(1), Songs(1), StartSong(0), LoadAddress(0x8000), InitAddress(0x8000), PlayAddress(0x8000),
	SpeedNtsc(NTSC_SPEED), SpeedPal(PAL_SPEED), Pal(false), Dual(false), Chips(0), Bankswitched(false)
{
	std::memset(Banks, 0, sizeof(Banks));
}

std::string NsfFile::TrackName(int Track) const
{
	return Track >= 0 && Track < int(TrackNames.size()) ? TrackNames[Track] : std::string();
}

int NsfFile::TrackTime(int Track) const
{
	return Track >= 0 && Track < int(TrackTimes.size()) ? TrackTimes[Track] : -1;
}

double NsfFile::PlayRate() const
{
	const int Speed = Pal ? SpeedPal : SpeedNtsc;
	return Speed > 0 ? 1000000.0 / Speed : (Pal ? 50.0 : 60.0);
}

std::string NsfFile::ChipNames() const
{
	static const char *const NAMES[] = { "VRC6", "VRC7", "FDS", "MMC5", "N163", "5B" };
	std::string s;
	for (int i = 0; i < 6; ++i) {
		if (Chips & (1 << i)) {
			if (!s.empty())
				s += ", ";
			s += NAMES[i];
		}
	}
	return s;
}

bool ParseNsf(const std::vector<uint8_t> &Bytes, NsfFile &Out, std::string &Error)
{
	Out = NsfFile();
	const uint8_t *p = Bytes.data();
	if (Bytes.size() >= 4 && !std::memcmp(p, "NSFE", 4)) {
		Out.Nsfe = true;
		if (!ParseChunks(p + 4, Bytes.size() - 4, Out, true, Error))
			return false;
	}
	else {
		if (Bytes.size() < HEADER_SIZE || std::memcmp(p, "NESM\x1A", 5)) {
			Error = "it isn't an NSF file";
			return false;
		}
		Out.Version = p[5];
		Out.Songs = p[6];
		Out.StartSong = p[7] > 0 ? p[7] - 1 : 0;
		Out.LoadAddress = Word(p + 8);
		Out.InitAddress = Word(p + 0x0A);
		Out.PlayAddress = Word(p + 0x0C);
		Out.Title = Field(p + 0x0E, 32);
		Out.Artist = Field(p + 0x2E, 32);
		Out.Copyright = Field(p + 0x4E, 32);
		if (Word(p + 0x6E) != 0)
			Out.SpeedNtsc = Word(p + 0x6E);
		for (int i = 0; i < 8; ++i) {
			Out.Banks[i] = p[0x70 + i];
			if (Out.Banks[i] != 0)
				Out.Bankswitched = true;
		}
		if (Word(p + 0x78) != 0)
			Out.SpeedPal = Word(p + 0x78);
		Out.Pal = (p[0x7A] & 0x03) == 1;
		Out.Dual = (p[0x7A] & 0x02) != 0;
		Out.Chips = p[0x7B];

		// NSF2 may say how long the program is, with metadata after it
		size_t Length = Bytes.size() - HEADER_SIZE;
		if (Out.Version >= 2) {
			const size_t Given = size_t(p[0x7D]) | (size_t(p[0x7E]) << 8) | (size_t(p[0x7F]) << 16);
			if (Given != 0 && Given <= Length) {
				if (!ParseChunks(p + HEADER_SIZE + Given, Length - Given, Out, false, Error))
					return false;
				Length = Given;
			}
		}
		Out.Data.assign(p + HEADER_SIZE, p + HEADER_SIZE + Length);
	}

	if (Out.Songs < 1) {
		Error = "the file has no tracks";
		return false;
	}
	if (Out.StartSong >= Out.Songs)
		Out.StartSong = 0;
	if (Out.Data.empty()) {
		Error = "the file has no music data";
		return false;
	}
	if (Out.LoadAddress < 0x6000 && !(Out.Chips & CHIP_FDS)) {
		Error = "its load address is below $6000";
		return false;
	}
	return true;
}

namespace {

// "1:29.547", "0:01:29.547", "89" as milliseconds; -1 when it isn't a time
int PlaylistTime(const std::string &Text)
{
	if (Text.empty())
		return -1;
	double Seconds = 0;
	size_t Pos = 0;
	while (Pos <= Text.size()) {
		size_t End = Text.find(':', Pos);
		if (End == std::string::npos)
			End = Text.size();
		const std::string Part = Text.substr(Pos, End - Pos);
		char *pEnd = nullptr;
		const double v = std::strtod(Part.c_str(), &pEnd);
		if (Part.empty() || *pEnd)
			return -1;
		Seconds = Seconds * 60 + v;
		Pos = End + 1;
	}
	return int(Seconds * 1000 + 0.5);
}

// A playlist line's fields, split at commas that aren't escaped (\,)
std::vector<std::string> PlaylistFields(const std::string &Line)
{
	std::vector<std::string> Fields(1);
	for (size_t i = 0; i < Line.size(); ++i) {
		if (Line[i] == '\\' && i + 1 < Line.size()) {
			Fields.back() += Line[++i];
			continue;
		}
		if (Line[i] == ',')
			Fields.push_back(std::string());
		else
			Fields.back() += Line[i];
	}
	return Fields;
}

}	// namespace

int ApplyPlaylist(const std::string &Text, NsfFile &File)
{
	struct Entry { int Track; std::string Name; int Ms; };
	std::vector<Entry> Entries;
	bool ZeroBased = false;
	size_t Pos = 0;
	while (Pos < Text.size()) {
		size_t End = Text.find('\n', Pos);
		if (End == std::string::npos)
			End = Text.size();
		std::string Line = Text.substr(Pos, End - Pos);
		Pos = End + 1;
		if (!Line.empty() && Line.back() == '\r')
			Line.pop_back();
		const size_t Type = Line.find("::");
		if (Line.empty() || Line[0] == '#' || Type == std::string::npos)
			continue;
		std::vector<std::string> f = PlaylistFields(Line.substr(Type + 2));
		if (f.size() < 2)
			continue;
		std::string Kind = f[0];
		for (size_t i = 0; i < Kind.size(); ++i)
			Kind[i] = char(std::toupper(static_cast<unsigned char>(Kind[i])));
		if (Kind != "NSF" && Kind != "NSFE")
			continue;
		char *pEnd = nullptr;
		const long Track = std::strtol(f[1].c_str(), &pEnd, 10);
		if (f[1].empty() || *pEnd || Track < 0)
			continue;
		Entry e;
		e.Track = int(Track);
		e.Name = f.size() > 2 ? f[2] : std::string();
		e.Ms = f.size() > 3 ? PlaylistTime(f[3]) : -1;
		if (e.Track == 0)
			ZeroBased = true;
		Entries.push_back(e);
	}
	// "Game - Composer - Title" for every track: just the titles
	if (Entries.size() >= 2) {
		std::string Common = Entries[0].Name;
		for (size_t i = 1; i < Entries.size(); ++i) {
			size_t n = 0;
			while (n < Common.size() && n < Entries[i].Name.size() && Common[n] == Entries[i].Name[n])
				++n;
			Common.resize(n);
		}
		const size_t Dash = Common.rfind(" - ");
		if (Dash != std::string::npos) {
			const size_t Cut = Dash + 3;
			bool Rest = true;
			for (size_t i = 0; i < Entries.size() && Rest; ++i)
				Rest = Entries[i].Name.size() > Cut;
			if (Rest)
				for (size_t i = 0; i < Entries.size(); ++i)
					Entries[i].Name.erase(0, Cut);
		}
	}
	int Named = 0;
	std::vector<int> Order;
	for (size_t i = 0; i < Entries.size(); ++i) {
		const int t = Entries[i].Track - (ZeroBased ? 0 : 1);
		if (t < 0 || t >= File.Songs)
			continue;
		Order.push_back(t);
		if (int(File.TrackNames.size()) < File.Songs)
			File.TrackNames.resize(File.Songs);
		if (File.TrackNames[t].empty() && !Entries[i].Name.empty()) {
			File.TrackNames[t] = Entries[i].Name;
			++Named;
		}
		if (Entries[i].Ms > 0) {
			if (int(File.TrackTimes.size()) < File.Songs)
				File.TrackTimes.resize(File.Songs, -1);
			if (File.TrackTimes[t] <= 0)
				File.TrackTimes[t] = Entries[i].Ms;
		}
	}
	if (File.Playlist.empty())
		File.Playlist = Order;
	return Named;
}

namespace {

// fopen_s where the CRT deprecates fopen (MSVC), plain fopen elsewhere
FILE *OpenFile(const std::string &Path, const char *Mode)
{
#ifdef _MSC_VER
	FILE *f = nullptr;
	return fopen_s(&f, Path.c_str(), Mode) == 0 ? f : nullptr;
#else
	return std::fopen(Path.c_str(), Mode);
#endif
}

}	// namespace

bool ReadNsfFile(const std::string &Path, NsfFile &Out, std::string &Error)
{
	FILE *f = OpenFile(Path, "rb");
	if (!f) {
		Error = "can't open " + Path;
		return false;
	}
	std::vector<uint8_t> Bytes;
	uint8_t Buffer[65536];
	size_t n;
	while ((n = std::fread(Buffer, 1, sizeof(Buffer), f)) > 0) {
		Bytes.insert(Bytes.end(), Buffer, Buffer + n);
		if (Bytes.size() > (16u << 20)) {
			std::fclose(f);
			Error = "the file is too big for an NSF";
			return false;
		}
	}
	std::fclose(f);
	if (!ParseNsf(Bytes, Out, Error))
		return false;

	// A playlist of the same name next to it
	const size_t Dot = Path.find_last_of('.');
	const size_t Slash = Path.find_last_of("/\\");
	const std::string Base = Dot != std::string::npos && (Slash == std::string::npos || Dot > Slash) ? Path.substr(0, Dot) : Path;
	static const char *const EXTENSIONS[] = { ".m3u", ".M3U" };
	for (int i = 0; i < 2; ++i) {
		FILE *m = OpenFile(Base + EXTENSIONS[i], "rb");
		if (!m)
			continue;
		std::string Text;
		char Chunk[4096];
		while ((n = std::fread(Chunk, 1, sizeof(Chunk), m)) > 0 && Text.size() < (1u << 20))
			Text.append(Chunk, n);
		std::fclose(m);
		ApplyPlaylist(Text, Out);
		break;
	}
	return true;
}

}	// namespace nsf

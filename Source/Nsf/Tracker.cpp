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

#include "Tracker.h"
#include "MidiFile.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>

namespace nsf {

namespace {

// Bytes read with their bounds checked: past the end reads as zero and marks
// the reader as cut short
struct Reader {
	const std::vector<uint8_t> &B;
	bool Short;
	explicit Reader(const std::vector<uint8_t> &Bytes) : B(Bytes), Short(false) {}
	bool Has(size_t Pos, size_t Size) const { return Pos <= B.size() && Size <= B.size() - Pos; }
	unsigned U8(size_t Pos) { if (!Has(Pos, 1)) { Short = true; return 0; } return B[Pos]; }
	unsigned U16(size_t Pos) { return U8(Pos) | (U8(Pos + 1) << 8); }
	unsigned U16BE(size_t Pos) { return (U8(Pos) << 8) | U8(Pos + 1); }
	unsigned long U32(size_t Pos) { return U16(Pos) | (static_cast<unsigned long>(U16(Pos + 2)) << 16); }
	// A fixed-size text field: printable ASCII, other bytes as spaces, trimmed
	std::string Text(size_t Pos, size_t Size)
	{
		std::string s;
		for (size_t i = 0; i < Size && Has(Pos + i, 1); ++i) {
			const unsigned char c = B[Pos + i];
			if (c == 0)
				break;
			s += c >= 0x20 && c < 0x7F ? char(c) : ' ';
		}
		const size_t First = s.find_first_not_of(' ');
		if (First == std::string::npos)
			return std::string();
		return s.substr(First, s.find_last_not_of(' ') - First + 1);
	}
};

int Clamp(int v, int Lo, int Hi) { return std::max(Lo, std::min(Hi, v)); }


// The most a module has of each, past what the trackers allow (a broken file
// can't ask for more)
const int MAX_ORDERS = 256, MAX_PATTERNS = 256, MAX_NAMES = 256;
// The most cells a study's track is made of (a byte or two of the file can
// ask for many)
const long MAX_CELLS = 2000000;

// MOD's and XM's effects (0-F and XM's letters past them) as FamiTracker's
void ModEffect(int Command, int Param, TrackerCell &c)
{
	switch (Command) {
		case 0x0: if (Param) { c.Effect = '0'; c.Param = Param; } break;
		case 0x1: c.Effect = '1'; c.Param = Param; break;
		case 0x2: c.Effect = '2'; c.Param = Param; break;
		case 0x3: c.Effect = '3'; c.Param = Param; break;
		case 0x4: c.Effect = '4'; c.Param = Param; break;
		case 0x5: case 0x6: case 0xA: if (Param) { c.Effect = 'A'; c.Param = Param; } break;	// a volume slide (going on with a slide or vibrato)
		case 0x7: c.Effect = '7'; c.Param = Param; break;
		case 0xB: c.Effect = 'B'; c.Param = Param; break;
		case 0xC: c.Volume = std::min(Param, 64); break;
		case 0xD: c.Effect = 'D'; c.Param = (Param >> 4) * 10 + (Param & 15); break;
		case 0xE:
			if ((Param >> 4) == 0xC) { c.Effect = 'S'; c.Param = Param & 15; }
			else if ((Param >> 4) == 0xD) { c.Effect = 'G'; c.Param = Param & 15; }
			break;
		case 0xF: if (Param) { c.Effect = 'F'; c.Param = Param; } break;
		case 20: if (c.Note == TRACKER_NONE) c.Note = TRACKER_OFF; break;	// XM's Kxx: a key off
		default: break;
	}
}

// S3M's and IT's effects (A-Z as 1-26) as FamiTracker's
void LetterEffect(int Command, int Param, TrackerCell &c)
{
	switch (Command) {
		case 1: if (Param) { c.Effect = 'F'; c.Param = std::min(Param, 0x1F); } break;	// Axx speed
		case 2: c.Effect = 'B'; c.Param = Param; break;
		case 3: c.Effect = 'D'; c.Param = Param; break;
		case 4: if (Param) { c.Effect = 'A'; c.Param = Param; } break;
		case 5: c.Effect = '2'; c.Param = Param; break;		// Exx: down
		case 6: c.Effect = '1'; c.Param = Param; break;		// Fxx: up
		case 7: c.Effect = '3'; c.Param = Param; break;
		case 8: case 21: c.Effect = '4'; c.Param = Param; break;	// Hxy, Uxy (fine)
		case 10: if (Param) { c.Effect = '0'; c.Param = Param; } break;	// Jxy
		case 11: case 12: if (Param) { c.Effect = 'A'; c.Param = Param; } break;	// Kxy, Lxy: a volume slide on top
		case 18: c.Effect = '7'; c.Param = Param; break;	// Rxy tremolo
		case 19:
			if ((Param >> 4) == 0xC) { c.Effect = 'S'; c.Param = Param & 15; }
			else if ((Param >> 4) == 0xD) { c.Effect = 'G'; c.Param = Param & 15; }
			break;
		case 20: if (Param >= 0x20) { c.Effect = 'F'; c.Param = Param; } break;	// Txx tempo
		default: break;
	}
}

// --- MOD ---

int ModChannels(const char *Sig)
{
	static const char *const FOUR[] = { "M.K.", "M!K!", "M&K!", "N.T.", "FLT4", "4CHN" };
	for (size_t i = 0; i < sizeof(FOUR) / sizeof(FOUR[0]); ++i)
		if (!std::memcmp(Sig, FOUR[i], 4))
			return 4;
	if (!std::memcmp(Sig, "OCTA", 4) || !std::memcmp(Sig, "CD81", 4) || !std::memcmp(Sig, "FLT8", 4))
		return 8;
	if (Sig[0] >= '1' && Sig[0] <= '9' && !std::memcmp(Sig + 1, "CHN", 3))
		return Sig[0] - '0';
	if (std::isdigit(static_cast<unsigned char>(Sig[0])) && std::isdigit(static_cast<unsigned char>(Sig[1])) &&
		(!std::memcmp(Sig + 2, "CH", 2) || !std::memcmp(Sig + 2, "CN", 2)))
		return (Sig[0] - '0') * 10 + (Sig[1] - '0');
	if (!std::memcmp(Sig, "TDZ", 3) && Sig[3] >= '1' && Sig[3] <= '9')
		return Sig[3] - '0';
	return 0;
}

bool ParseMod(const std::vector<uint8_t> &Bytes, TrackerSong &Out, std::string &Error)
{
	Reader R(Bytes);
	char Sig[4];
	for (int i = 0; i < 4; ++i)
		Sig[i] = char(R.U8(1080 + i));
	const int Channels = ModChannels(Sig);
	if (R.Short || Channels < 1 || Channels > 32) {
		Error = "not a module FamiTracker knows (MOD, S3M, XM or IT)";
		return false;
	}
	Out.Format = "MOD";
	Out.Title = R.Text(0, 20);
	Out.Channels = Channels;
	for (int s = 0; s < 31; ++s)
		Out.Instruments.push_back(R.Text(20 + s * 30, 22));
	const int Length = Clamp(int(R.U8(950)), 1, 128);
	const int Restart = int(R.U8(951));
	Out.Restart = Restart < Length ? Restart : 0;
	int Patterns = 0;
	for (int i = 0; i < 128; ++i)
		Patterns = std::max(Patterns, int(R.U8(952 + i)) + 1);
	for (int i = 0; i < Length; ++i)
		Out.Orders.push_back(int(R.U8(952 + i)));
	const size_t PatternSize = size_t(64) * Channels * 4;
	for (int p = 0; p < Patterns; ++p) {
		const size_t At = 1084 + p * PatternSize;
		TrackerPattern Pat;
		Pat.Rows = 64;
		Pat.Cells.assign(64, std::vector<TrackerCell>(Channels));
		if (!R.Has(At, PatternSize)) {
			// Cut short: the patterns played come first, keep what's there
			if (p == 0) {
				Error = "the module is cut short before its first pattern";
				return false;
			}
			Out.Patterns.push_back(Pat);
			continue;
		}
		for (int r = 0; r < 64; ++r)
			for (int c = 0; c < Channels; ++c) {
				const size_t x = At + (size_t(r) * Channels + c) * 4;
				const unsigned b0 = Bytes[x], b1 = Bytes[x + 1], b2 = Bytes[x + 2], b3 = Bytes[x + 3];
				TrackerCell &Cl = Pat.Cells[r][c];
				const int Sample = int((b0 & 0xF0) | (b2 >> 4));
				const int Period = int(((b0 & 0x0F) << 8) | b1);
				if (Period > 0)
					Cl.Note = Clamp(48 + int(std::floor(12.0 * std::log(428.0 / Period) / std::log(2.0) + 0.5)), 0, 119);
				if (Sample > 0)
					Cl.Instrument = Sample - 1;
				ModEffect(int(b2 & 0x0F), int(b3), Cl);
			}
		Out.Patterns.push_back(Pat);
	}
	return true;
}

// --- S3M ---

bool ParseS3m(const std::vector<uint8_t> &Bytes, TrackerSong &Out, std::string &Error)
{
	Reader R(Bytes);
	Out.Format = "S3M";
	Out.Title = R.Text(0, 28);
	// The counts as the file has them place its lists; at most the trackers' limits are read
	const int AllOrders = int(R.U16(0x20)), AllInstruments = int(R.U16(0x22));
	const int Orders = std::min(AllOrders, MAX_ORDERS), Instruments = std::min(AllInstruments, MAX_NAMES);
	const int Patterns = std::min(int(R.U16(0x24)), MAX_PATTERNS);
	const unsigned Made = R.U16(0x28);
	const int Speed = int(R.U8(0x31)), Tempo = int(R.U8(0x32));
	if (Speed > 0 && Speed < 0x20)
		Out.Speed = Speed;
	if (Tempo >= 0x20)
		Out.Tempo = Tempo;
	if ((Made >> 12) == 1)
		Out.Tracker = "Scream Tracker 3";
	else if ((Made >> 12) == 3)
		Out.Tracker = "Impulse Tracker";
	else if ((Made >> 12) == 4)
		Out.Tracker = "Schism Tracker";
	else if ((Made >> 12) == 5)
		Out.Tracker = "OpenMPT";
	// The channels in use: 0-15 are sample channels, 16-31 AdLib
	std::vector<int> Column(32, -1);
	for (int c = 0; c < 32; ++c) {
		if (R.U8(0x40 + c) < 32)		// 128 and up: off
			Column[c] = Out.Channels++;
	}
	if (R.Short || Out.Channels == 0) {
		Error = "the S3M module is cut short or has no channels";
		return false;
	}
	for (int i = 0; i < Orders; ++i) {
		const int o = int(R.U8(0x60 + i));
		Out.Orders.push_back(o == 255 ? -1 : (o == 254 ? -2 : o));
	}
	const size_t Pointers = 0x60 + AllOrders;
	for (int i = 0; i < Instruments; ++i) {
		const size_t At = size_t(R.U16(Pointers + i * 2)) * 16;
		Out.Instruments.push_back(R.Text(At + 0x30, 28));
	}
	for (int p = 0; p < Patterns; ++p) {
		const size_t At = size_t(R.U16(Pointers + AllInstruments * 2 + p * 2)) * 16;
		TrackerPattern Pat;
		Pat.Rows = 64;
		Pat.Cells.assign(64, std::vector<TrackerCell>(Out.Channels));
		if (At > 0 && R.Has(At, 2)) {
			const size_t End = std::min(Bytes.size(), At + 2 + R.U16(At));
			size_t x = At + 2;
			for (int r = 0; r < 64 && x < End; ) {
				const unsigned What = Bytes[x++];
				if (What == 0) {
					++r;
					continue;
				}
				TrackerCell Dummy, &Cl = Column[What & 31] >= 0 ? Pat.Cells[r][Column[What & 31]] : Dummy;
				if (What & 32) {
					const unsigned Note = R.U8(x), Inst = R.U8(x + 1);
					x += 2;
					if (Note == 254)
						Cl.Note = TRACKER_CUT;
					else if (Note < 0xA0 && (Note & 15) < 12)
						Cl.Note = Clamp(int(Note >> 4) * 12 + int(Note & 15), 0, 119);
					if (Inst > 0)
						Cl.Instrument = int(Inst) - 1;
				}
				if (What & 64)
					Cl.Volume = std::min(int(R.U8(x++)), 64);
				if (What & 128) {
					const unsigned Command = R.U8(x), Param = R.U8(x + 1);
					x += 2;
					if (Command == 3)	// Cxx: the row, in decimal digits
						LetterEffect(int(Command), int((Param >> 4) * 10 + (Param & 15)), Cl);
					else
						LetterEffect(int(Command), int(Param), Cl);
				}
			}
		}
		Out.Patterns.push_back(Pat);
	}
	if (R.Short && Out.Patterns.empty()) {
		Error = "the S3M module is cut short";
		return false;
	}
	return true;
}

// --- XM ---

bool ParseXm(const std::vector<uint8_t> &Bytes, TrackerSong &Out, std::string &Error)
{
	Reader R(Bytes);
	Out.Format = "XM";
	Out.Title = R.Text(17, 20);
	Out.Tracker = R.Text(38, 20);
	const size_t HeaderSize = R.U32(60);
	const int Length = int(R.U16(64)), Restart = int(R.U16(66)), Channels = int(R.U16(68));
	const int Patterns = std::min(int(R.U16(70)), MAX_PATTERNS), Instruments = std::min(int(R.U16(72)), MAX_NAMES);
	const int Speed = int(R.U16(76)), Tempo = int(R.U16(78));
	if (R.Short || Channels < 1 || Channels > 64 || HeaderSize < 20) {
		Error = "the XM module's header is broken";
		return false;
	}
	Out.Channels = Channels;
	if (Speed > 0 && Speed < 0x20)
		Out.Speed = Speed;
	if (Tempo >= 0x20 && Tempo <= 0xFF)
		Out.Tempo = Tempo;
	for (int i = 0; i < std::min(Length, 256); ++i)
		Out.Orders.push_back(int(R.U8(80 + i)));
	Out.Restart = Restart < Length ? Restart : 0;
	size_t x = 60 + HeaderSize;
	for (int p = 0; p < Patterns; ++p) {
		const size_t Head = R.U32(x);
		const int Rows = int(R.U16(x + 5));
		const size_t Packed = R.U16(x + 7);
		if (R.Short || Head < 7) {
			if (Out.Patterns.empty()) {
				Error = "the XM module is cut short in its patterns";
				return false;
			}
			return true;	// the patterns there are, without the instruments' names
		}
		TrackerPattern Pat;
		Pat.Rows = Clamp(Rows, 1, 256);
		Pat.Cells.assign(Pat.Rows, std::vector<TrackerCell>(Channels));
		size_t y = x + Head;
		const size_t End = std::min(Bytes.size(), y + Packed);
		for (int r = 0; r < Pat.Rows && y < End; ++r)
			for (int c = 0; c < Channels && y < End; ++c) {
				unsigned Flags = Bytes[y];
				unsigned Note = 0, Inst = 0, Vol = 0, Command = 0, Param = 0;
				if (Flags & 0x80) {
					++y;
					if (Flags & 1) Note = R.U8(y++);
					if (Flags & 2) Inst = R.U8(y++);
					if (Flags & 4) Vol = R.U8(y++);
					if (Flags & 8) Command = R.U8(y++);
					if (Flags & 16) Param = R.U8(y++);
				}
				else {
					Note = R.U8(y);
					Inst = R.U8(y + 1);
					Vol = R.U8(y + 2);
					Command = R.U8(y + 3);
					Param = R.U8(y + 4);
					y += 5;
				}
				TrackerCell &Cl = Pat.Cells[r][c];
				if (Note == 97)
					Cl.Note = TRACKER_OFF;
				else if (Note >= 1 && Note <= 96)
					Cl.Note = int(Note) - 1;
				if (Inst > 0)
					Cl.Instrument = int(Inst) - 1;
				if (Vol >= 0x10 && Vol <= 0x50)
					Cl.Volume = int(Vol) - 0x10;
				ModEffect(int(Command), int(Param), Cl);
				if (Cl.Effect == 0) {
					// The volume column's slides and vibrato
					if ((Vol >> 4) == 0x6 || (Vol >> 4) == 0x7) {
						Cl.Effect = 'A';
						Cl.Param = (Vol >> 4) == 0x7 ? int(Vol & 15) << 4 : int(Vol & 15);
					}
					else if ((Vol >> 4) == 0xB) {
						Cl.Effect = '4';
						Cl.Param = int(Vol & 15);
					}
					else if ((Vol >> 4) == 0xF) {
						Cl.Effect = '3';
						Cl.Param = int(Vol & 15) << 4;
					}
				}
			}
		Out.Patterns.push_back(Pat);
		x += Head + Packed;
	}
	// The instruments: their names, then their samples' headers and data
	for (int i = 0; i < Instruments && R.Has(x, 29); ++i) {
		const size_t Size = R.U32(x);
		Out.Instruments.push_back(R.Text(x + 4, 22));
		const int Samples = int(R.U16(x + 27));
		size_t Next = x + std::max<size_t>(Size, 29);
		std::vector<std::pair<int, int> > Map(120);
		for (int n = 0; n < 120; ++n)
			Map[n] = std::make_pair(-1, n);
		if (Samples > 0 && Samples <= 16) {
			const size_t SampleHead = R.U32(x + 29);
			const int First = int(Out.Samples.size());
			for (int n = 0; n < 96; ++n) {
				const int s = int(R.U8(x + 33 + n));
				if (s < Samples)
					Map[n] = std::make_pair(First + s, n);
			}
			size_t Data = 0;
			for (int s = 0; s < Samples; ++s) {
				const size_t h = Next + s * SampleHead;
				Data += R.U32(h);
				Out.Samples.push_back(R.Text(h + 18, 22));
			}
			Next += Samples * SampleHead + Data;
		}
		Out.KeyMaps.push_back(Map);
		if (R.Short || Next <= x)
			break;
		x = Next;
	}
	return true;
}

// --- IT ---

bool ParseIt(const std::vector<uint8_t> &Bytes, TrackerSong &Out, std::string &Error)
{
	Reader R(Bytes);
	Out.Format = "IT";
	Out.Title = R.Text(4, 26);
	const int PerBeat = int(R.U8(0x1E));
	if (PerBeat >= 2 && PerBeat <= 16)
		Out.RowsPerBeat = PerBeat;
	// The counts as the file has them place its lists; at most the trackers' limits are read
	const int AllOrders = int(R.U16(0x20)), AllInstruments = int(R.U16(0x22)), AllSamples = int(R.U16(0x24));
	const int Orders = std::min(AllOrders, MAX_ORDERS), Instruments = std::min(AllInstruments, MAX_NAMES);
	const int Samples = std::min(AllSamples, MAX_NAMES), Patterns = std::min(int(R.U16(0x26)), MAX_PATTERNS);
	const unsigned Made = R.U16(0x28), Flags = R.U16(0x2C), Special = R.U16(0x2E);
	const int Speed = int(R.U8(0x32)), Tempo = int(R.U8(0x33));
	if (R.Short) {
		Error = "the IT module's header is cut short";
		return false;
	}
	if (Speed > 0 && Speed < 0x20)
		Out.Speed = Speed;
	if (Tempo >= 0x20)
		Out.Tempo = Tempo;
	Out.Tracker = (Made >> 12) == 5 ? "OpenMPT" : ((Made >> 12) == 1 ? "Schism Tracker" : "Impulse Tracker");
	if (Special & 1) {
		const size_t Length = R.U16(0x36), At = R.U32(0x38);
		std::string Msg;
		for (size_t i = 0; i < Length && R.Has(At + i, 1); ++i) {
			const unsigned char c = Bytes[At + i];
			if (c == 0)
				break;
			Msg += c == '\r' ? '\n' : (c >= 0x20 && c < 0x7F ? char(c) : ' ');
		}
		Out.Message = Msg;
	}
	for (int i = 0; i < Orders; ++i) {
		const int o = int(R.U8(0xC0 + i));
		Out.Orders.push_back(o == 255 ? -1 : (o == 254 ? -2 : o));
	}
	const size_t InstPointers = 0xC0 + AllOrders, SamplePointers = InstPointers + size_t(AllInstruments) * 4,
		PatternPointers = SamplePointers + size_t(AllSamples) * 4;
	std::vector<std::string> InstNames, SampleNames;
	for (int i = 0; i < Instruments; ++i)
		InstNames.push_back(R.Text(R.U32(InstPointers + i * 4) + 0x20, 26));
	for (int i = 0; i < Samples; ++i)
		SampleNames.push_back(R.Text(R.U32(SamplePointers + i * 4) + 0x14, 26));
	if (Flags & 4) {
		Out.Instruments = InstNames;
		Out.Samples = SampleNames;
		// Each instrument's keyboard: IT's note and sample for each note (our
		// note n is IT's n + 12)
		for (int i = 0; i < Instruments; ++i) {
			const size_t At = R.U32(InstPointers + i * 4);
			std::vector<std::pair<int, int> > Map(120);
			for (int n = 0; n < 120; ++n) {
				Map[n] = std::make_pair(-1, n);
				if (n + 12 >= 120)
					continue;
				const int To = int(R.U8(At + 0x40 + (n + 12) * 2)), Sample = int(R.U8(At + 0x41 + (n + 12) * 2));
				if (Sample > 0 && Sample <= Samples)
					Map[n] = std::make_pair(Sample - 1, To < 120 ? std::max(0, To - 12) : n);
			}
			Out.KeyMaps.push_back(Map);
		}
	}
	else
		Out.Instruments = SampleNames;
	// The patterns, unpacked into all 64 channels, then the channels used kept
	std::vector<TrackerPattern> Full;
	std::vector<bool> Used(64, false);
	for (int p = 0; p < Patterns; ++p) {
		const size_t At = R.U32(PatternPointers + p * 4);
		TrackerPattern Pat;
		Pat.Rows = 64;
		if (At > 0 && R.Has(At, 8))
			Pat.Rows = Clamp(int(R.U16(At + 2)), 1, 200);
		Pat.Cells.assign(Pat.Rows, std::vector<TrackerCell>(64));
		if (At > 0 && R.Has(At, 8)) {
			const size_t End = std::min(Bytes.size(), At + 8 + R.U16(At));
			size_t x = At + 8;
			unsigned LastMask[64] = { 0 }, LastNote[64] = { 0 }, LastInst[64] = { 0 }, LastVol[64] = { 0 }, LastCommand[64] = { 0 },
				LastParam[64] = { 0 };
			for (int r = 0; r < Pat.Rows && x < End; ) {
				const unsigned What = Bytes[x++];
				if (What == 0) {
					++r;
					continue;
				}
				const int c = int((What - 1) & 63);
				unsigned Mask = LastMask[c];
				if (What & 128)
					Mask = LastMask[c] = R.U8(x++);
				bool HasNote = false, HasInst = false, HasVol = false, HasCommand = false;
				if (Mask & 1) { LastNote[c] = R.U8(x++); HasNote = true; }
				if (Mask & 2) { LastInst[c] = R.U8(x++); HasInst = true; }
				if (Mask & 4) { LastVol[c] = R.U8(x++); HasVol = true; }
				if (Mask & 8) { LastCommand[c] = R.U8(x); LastParam[c] = R.U8(x + 1); x += 2; HasCommand = true; }
				HasNote = HasNote || (Mask & 16);
				HasInst = HasInst || (Mask & 32);
				HasVol = HasVol || (Mask & 64);
				HasCommand = HasCommand || (Mask & 128);
				TrackerCell &Cl = Pat.Cells[r][c];
				if (HasNote) {
					const unsigned n = LastNote[c];
					if (n == 254)
						Cl.Note = TRACKER_CUT;
					else if (n >= 120)
						Cl.Note = TRACKER_OFF;	// off, or a fade
					else
						Cl.Note = Clamp(int(n) - 12, 0, 119);	// IT's middle C is C-5
				}
				if (HasInst && LastInst[c] > 0)
					Cl.Instrument = int(LastInst[c]) - 1;
				if (HasVol) {
					const unsigned v = LastVol[c];
					if (v <= 64)
						Cl.Volume = int(v);
					else if (v >= 193 && v <= 202) {
						Cl.Effect = '3';
						Cl.Param = int(v - 193) * 16;
					}
					else if (v >= 203 && v <= 212) {
						Cl.Effect = '4';
						Cl.Param = int(v - 203);
					}
					else if (v >= 85 && v <= 104) {
						Cl.Effect = 'A';
						Cl.Param = v <= 94 ? int(v - 85) << 4 : int(v - 95);
					}
				}
				if (HasCommand) {
					TrackerCell e;
					LetterEffect(int(LastCommand[c]), int(LastParam[c]), e);
					if (e.Effect) {
						Cl.Effect = e.Effect;
						Cl.Param = e.Param;
					}
				}
				if (Cl.Note != TRACKER_NONE || Cl.Instrument >= 0 || Cl.Volume >= 0 || Cl.Effect)
					Used[c] = true;
			}
		}
		Full.push_back(Pat);
	}
	std::vector<int> Keep;
	for (int c = 0; c < 64; ++c)
		if (Used[c] && R.U8(0x40 + c) < 128)		// a pan of 128 and up: the channel is off
			Keep.push_back(c);
	if (Keep.empty())
		Keep.push_back(0);
	Out.Channels = int(Keep.size());
	for (size_t p = 0; p < Full.size(); ++p) {
		TrackerPattern Pat;
		Pat.Rows = Full[p].Rows;
		Pat.Cells.assign(Pat.Rows, std::vector<TrackerCell>(Keep.size()));
		for (int r = 0; r < Pat.Rows; ++r)
			for (size_t k = 0; k < Keep.size(); ++k)
				Pat.Cells[r][k] = Full[p].Cells[r][Keep[k]];
		Out.Patterns.push_back(Pat);
	}
	Out.Restart = 0;
	return true;
}

// Words in a name, lower case, digits at their ends dropped ("SD1" -> "sd")
std::vector<std::string> Words(const std::string &Name)
{
	std::vector<std::string> Out;
	std::string w;
	for (size_t i = 0; i <= Name.size(); ++i) {
		const unsigned char c = i < Name.size() ? static_cast<unsigned char>(Name[i]) : ' ';
		if (std::isalpha(c)) {
			w += char(std::tolower(c));
			continue;
		}
		if (std::isdigit(c) && !w.empty())
			continue;	// "bd909" -> "bd"
		if (!w.empty())
			Out.push_back(w);
		w.clear();
	}
	return Out;
}

bool HasWord(const std::vector<std::string> &w, const char *const *List, size_t Count)
{
	for (size_t i = 0; i < w.size(); ++i)
		for (size_t k = 0; k < Count; ++k)
			if (w[i] == List[k])
				return true;
	return false;
}

// An instrument's name says it plays notes: a bass, a lead, a pad...
bool MelodicName(const std::string &Name)
{
	static const char *const WORDS[] = { "bass", "lead", "pad", "pads", "string", "strings", "piano", "organ", "synth", "guitar",
		"gtr", "choir", "brass", "flute", "sax", "bell", "bells", "chord", "chords", "arp", "pluck", "vox", "voice", "horn", "trumpet",
		"violin", "cello", "harp", "marimba", "vibes", "epiano", "rhodes", "saw", "square", "sine", "melody", "keys" };
	return HasWord(Words(Name), WORDS, sizeof(WORDS) / sizeof(WORDS[0]));
}

}	// namespace

std::string DrumOfName(const std::string &Name)
{
	const std::vector<std::string> w = Words(Name);
	std::string Lower;
	for (size_t i = 0; i < Name.size(); ++i)
		Lower += char(std::tolower(static_cast<unsigned char>(Name[i])));
	static const char *const KICK[] = { "kick", "kik", "kck", "bd", "bassdrum", "bdrum", "kickdrum" };
	static const char *const SNARE[] = { "snare", "snr", "sd", "clap", "claps", "snaredrum" };
	static const char *const HAT[] = { "hat", "hats", "hh", "hihat", "chh", "ohh", "ch", "oh", "closedhat", "openhat" };
	static const char *const CYMBAL[] = { "crash", "ride", "cymbal", "cym", "splash", "china" };
	static const char *const TOM[] = { "tom", "toms", "floortom", "tomh", "toml", "tomm" };
	static const char *const PERC[] = { "perc", "percussion", "conga", "congas", "bongo", "bongos", "tamb", "tambourine", "shaker",
		"cowbell", "cabasa", "clave", "claves", "timbale", "timbales", "agogo", "guiro", "rim", "rimshot", "drum", "drums", "woodblock",
		"breakbeat", "drumloop", "timpani" };
	if (HasWord(w, KICK, sizeof(KICK) / sizeof(KICK[0])) || Lower.find("bass drum") != std::string::npos ||
		Lower.find("kick") != std::string::npos)
		return "kick";
	if (HasWord(w, SNARE, sizeof(SNARE) / sizeof(SNARE[0])) || Lower.find("snare") != std::string::npos)
		return "snare";
	if (HasWord(w, HAT, sizeof(HAT) / sizeof(HAT[0])) || Lower.find("hihat") != std::string::npos ||
		Lower.find("hi-hat") != std::string::npos)
		return "hi-hat";
	if (HasWord(w, CYMBAL, sizeof(CYMBAL) / sizeof(CYMBAL[0])))
		return "cymbal";
	if (HasWord(w, TOM, sizeof(TOM) / sizeof(TOM[0])))
		return "tom";
	if (HasWord(w, PERC, sizeof(PERC) / sizeof(PERC[0])))
		return "percussion";
	return std::string();
}

std::string DescribeTracker(const TrackerSong &Song)
{
	if (Song.Format == "MIDI") {
		// A MIDI file: its meter, its channels and the instruments they play
		std::set<int> Used;
		for (size_t p = 0; p < Song.Patterns.size(); ++p)
			for (size_t r = 0; r < Song.Patterns[p].Cells.size(); ++r)
				for (size_t c = 0; c < Song.Patterns[p].Cells[r].size(); ++c)
					if (Song.Patterns[p].Cells[r][c].Instrument >= 0)
						Used.insert(Song.Patterns[p].Cells[r][c].Instrument);
		std::vector<std::string> Names;
		bool Drums = false;
		for (std::set<int>::const_iterator it = Used.begin(); it != Used.end(); ++it) {
			if (*it < MIDI_DRUM_INSTRUMENTS && *it < int(Song.Instruments.size()))
				Names.push_back(Song.Instruments[*it]);
			else
				Drums = true;
		}
		if (Names.size() > 12) {
			const size_t More = Names.size() - 11;
			Names.resize(11);
			Names.push_back(std::to_string(More) + " more");
		}
		if (Drums)
			Names.push_back("drums");
		std::string List;
		for (size_t i = 0; i < Names.size(); ++i)
			List += (i == 0 ? "" : (i + 1 == Names.size() ? " and " : ", ")) + Names[i];
		char Buf[256];
		std::snprintf(Buf, sizeof(Buf), "a MIDI file (%s)%s%s: %d channel%s of a note at a time, %d rows a beat", Song.Tracker.c_str(),
			Song.Meter.empty() ? "" : " in ", Song.Meter.c_str(), Song.Channels, Song.Channels == 1 ? "" : "s", Song.RowsPerBeat);
		std::string s = (Song.Title.empty() ? std::string("Untitled") : Song.Title) + ": " + Buf + (List.empty() ? "" : ", playing " + List) + ".";
		if (!Song.Message.empty())
			s += " " + Song.Message;
		return s;
	}
	int Named = 0;
	for (size_t i = 0; i < Song.Instruments.size(); ++i)
		Named += !Song.Instruments[i].empty();
	int Orders = 0;
	for (size_t o = 0; o < Song.Orders.size() && Song.Orders[o] != -1; ++o)
		Orders += Song.Orders[o] >= 0;
	char Buf[256];
	std::snprintf(Buf, sizeof(Buf), "a%s %s module of %d channel%s%s%s%s, %d order%s, %d named instrument%s",
		Song.Format == "MOD" ? "" : "n", Song.Format.c_str(), Song.Channels, Song.Channels == 1 ? "" : "s",
		Song.Tracker.empty() ? "" : " (", Song.Tracker.c_str(), Song.Tracker.empty() ? "" : ")", Orders, Orders == 1 ? "" : "s", Named,
		Named == 1 ? "" : "s");
	return (Song.Title.empty() ? std::string("Untitled") : Song.Title) + ": " + Buf;
}

bool ParseTracker(const std::vector<uint8_t> &Bytes, TrackerSong &Out, std::string &Error)
{
	Out = TrackerSong();
	if (IsMidi(Bytes))
		return ParseMidi(Bytes, Out, Error);
	if (Bytes.size() >= 0x60 && !std::memcmp(&Bytes[0], "Extended Module:", 16))
		return ParseXm(Bytes, Out, Error);
	if (Bytes.size() >= 0xC0 && !std::memcmp(&Bytes[0], "IMPM", 4))
		return ParseIt(Bytes, Out, Error);
	if (Bytes.size() >= 0x60 && !std::memcmp(&Bytes[0x2C], "SCRM", 4))
		return ParseS3m(Bytes, Out, Error);
	if (Bytes.size() >= 1084)
		return ParseMod(Bytes, Out, Error);
	Error = "not a module FamiTracker knows (MOD, S3M, XM or IT) or a MIDI file";
	return false;
}

bool ReadTrackerFile(const std::string &Path, TrackerSong &Out, std::string &Error)
{
	FILE *f = 0;
#ifdef _MSC_VER
	if (fopen_s(&f, Path.c_str(), "rb") != 0)
		f = 0;
#else
	f = std::fopen(Path.c_str(), "rb");
#endif
	if (!f) {
		Error = "can't open " + Path;
		return false;
	}
	std::vector<uint8_t> Bytes;
	unsigned char Buf[65536];
	size_t n;
	while ((n = std::fread(Buf, 1, sizeof(Buf), f)) > 0 && Bytes.size() < size_t(64) << 20)
		Bytes.insert(Bytes.end(), Buf, Buf + n);
	std::fclose(f);
	return ParseTracker(Bytes, Out, Error);
}

bool TrackerToModule(const TrackerSong &Song, Module &Out, TrackReport &Report, std::string &Error)
{
	Out = Module();
	Report = TrackReport();
	Out.Title = Song.Title;
	const int Channels = Song.Channels;
	const int Instruments = int(Song.Instruments.size());

	// The song in playing order: a stretch of a pattern for each order, from
	// the row a break led to, to the row with a break or a jump or the end,
	// following jumps until a stretch comes round again (the loop)
	struct Stretch { int Pattern, First, Last; };
	std::vector<Stretch> Stretches;
	int LoopTo = -1;
	{
		std::map<std::pair<int, int>, int> Seen;	// (order, row) -> stretch
		int Order = 0, Row = 0;
		bool Ended = false;
		while (Stretches.size() < 1024) {
			if (Order < 0 || Order >= int(Song.Orders.size()) || Song.Orders[Order] == -1) {
				// The end: back to the restart order, or it stops
				if (Ended || Song.Restart < 0 || Stretches.empty())
					break;
				Ended = true;
				Order = Song.Restart;
				Row = 0;
				continue;
			}
			const int p = Song.Orders[Order];
			if (p < 0 || p >= int(Song.Patterns.size())) {
				++Order;
				Row = 0;
				continue;
			}
			const TrackerPattern &P = Song.Patterns[p];
			Row = std::min(Row, P.Rows - 1);
			const std::pair<int, int> Here(Order, Row);
			if (Seen.count(Here)) {
				LoopTo = Seen[Here];
				break;
			}
			Seen[Here] = int(Stretches.size());
			int Last = P.Rows - 1, Jump = -1, Break = -1;
			for (int r = Row; r < P.Rows && Jump < 0 && Break < 0; ++r)
				for (int c = 0; c < Channels; ++c) {
					const TrackerCell &Cl = P.Cells[r][c];
					if (Cl.Effect == 'B') {
						Jump = Cl.Param;
						Last = r;
					}
					else if (Cl.Effect == 'D') {
						Break = Cl.Param;
						Last = r;
					}
				}
			Stretch St = { p, Row, Last };
			Stretches.push_back(St);
			if (Jump >= 0) {
				Order = Jump;
				Row = Break >= 0 ? Break : 0;
			}
			else {
				++Order;
				Row = Break >= 0 ? Break : 0;
			}
		}
	}
	if (Stretches.empty()) {
		Error = "the module plays no patterns";
		return false;
	}

	// What a note plays: the sample its instrument plays it with (by the
	// instrument's keymap) and its note; false when no sample plays it
	auto Sound = [&](int Inst, int Note, int &Sample, int &Pitch) -> bool {
		Sample = Inst;
		Pitch = Note;
		if (!Song.KeyMaps.empty()) {
			if (Inst < int(Song.KeyMaps.size())) {
				const std::pair<int, int> &k = Song.KeyMaps[Inst][Clamp(Note, 0, 119)];
				if (k.first < 0)
					return false;
				Sample = k.first;
				Pitch = k.second;
			}
			else
				Sample = -1;	// past the instruments the file has: as it is
		}
		Pitch = Clamp(Pitch, 0, 119);
		return true;
	};

	// The voices: an instrument with a sample (instruments with samples of
	// their own can be a drum kit, a sample a key). Each is drums or notes: its
	// names say it, or else playing a note or two over and over.
	std::map<std::pair<int, int>, int> VoiceOf;
	std::vector<std::pair<int, int> > Voices;
	std::vector<std::set<int> > Pitches;
	std::vector<int> Uses;
	std::set<int> PatternsPlayed;
	for (size_t s = 0; s < Stretches.size(); ++s)
		PatternsPlayed.insert(Stretches[s].Pattern);
	for (std::set<int>::const_iterator it = PatternsPlayed.begin(); it != PatternsPlayed.end(); ++it) {
		const TrackerPattern &P = Song.Patterns[*it];
		std::vector<int> Inst(Channels, -1);
		for (int r = 0; r < P.Rows; ++r)
			for (int c = 0; c < Channels; ++c) {
				const TrackerCell &Cl = P.Cells[r][c];
				if (Cl.Instrument >= 0)
					Inst[c] = Cl.Instrument;
				int Sample, Pitch;
				if (Cl.Note < 0 || Inst[c] < 0 || !Sound(Inst[c], Cl.Note, Sample, Pitch))
					continue;
				const std::pair<int, int> Key(Inst[c], Sample);
				if (!VoiceOf.count(Key)) {
					VoiceOf[Key] = int(Voices.size());
					Voices.push_back(Key);
					Pitches.push_back(std::set<int>());
					Uses.push_back(0);
				}
				const int v = VoiceOf[Key];
				Pitches[v].insert(Pitch);
				++Uses[v];
			}
	}
	auto InstName = [&](int i) { return i >= 0 && i < Instruments ? Song.Instruments[i] : std::string(); };
	auto SampleName = [&](const std::pair<int, int> &v) {
		if (Song.KeyMaps.empty())
			return InstName(v.first);
		return v.second >= 0 && v.second < int(Song.Samples.size()) ? Song.Samples[v.second] : std::string();
	};
	std::map<int, int> VoicesOfInst;
	for (size_t v = 0; v < Voices.size(); ++v)
		++VoicesOfInst[Voices[v].first];
	std::vector<bool> Drum(Voices.size(), false);
	std::vector<std::string> DrumKind(Voices.size());
	for (size_t v = 0; v < Voices.size(); ++v) {
		const std::string Name = InstName(Voices[v].first), Sample = SampleName(Voices[v]);
		DrumKind[v] = DrumOfName(Sample);
		if (DrumKind[v].empty())
			DrumKind[v] = DrumOfName(Name);
		if (!DrumKind[v].empty())
			Drum[v] = true;
		else if (Song.Format != "MIDI" && !MelodicName(Name) && !MelodicName(Sample) && Uses[v] >= 4 && Pitches[v].size() <= 2) {
			// (MIDI's programs play notes: its drums say so by their names)
			Drum[v] = true;
			DrumKind[v] = "drum";
		}
	}

	// The channels: the notes of each of the module's channels, and its drums
	std::vector<bool> HasNotes(Channels, false), HasDrums(Channels, false);
	for (std::set<int>::const_iterator it = PatternsPlayed.begin(); it != PatternsPlayed.end(); ++it) {
		const TrackerPattern &P = Song.Patterns[*it];
		std::vector<int> Inst(Channels, -1);
		for (int r = 0; r < P.Rows; ++r)
			for (int c = 0; c < Channels; ++c) {
				const TrackerCell &Cl = P.Cells[r][c];
				if (Cl.Instrument >= 0)
					Inst[c] = Cl.Instrument;
				int Sample, Pitch;
				if (Cl.Note < 0 || Inst[c] < 0 || !Sound(Inst[c], Cl.Note, Sample, Pitch))
					continue;
				if (Drum[VoiceOf[std::make_pair(Inst[c], Sample)]])
					HasDrums[c] = true;
				else
					HasNotes[c] = true;
			}
	}
	std::vector<int> NoteColumn(Channels, -1), DrumColumn(Channels, -1);
	const bool Named = Song.ChannelNames.size() == size_t(Channels);
	for (int c = 0; c < Channels; ++c) {
		const std::string Name = Named ? Song.ChannelNames[c] : "Channel " + std::to_string(c + 1);
		if (HasNotes[c]) {
			NoteColumn[c] = int(Out.ChannelNames.size());
			Out.ChannelNames.push_back(Name);
			Out.ChannelKinds.push_back(LAYOUT_MELODIC);
		}
		if (HasDrums[c]) {
			DrumColumn[c] = int(Out.ChannelNames.size());
			Out.ChannelNames.push_back(HasNotes[c] ? Name + " drums" : (Named ? Name : Name + " (drums)"));
			Out.ChannelKinds.push_back(LAYOUT_DRUMS);
		}
	}
	if (Out.ChannelNames.empty()) {
		Error = "the module plays no notes";
		return false;
	}
	const int Columns = int(Out.ChannelNames.size());

	// An instrument for each voice, named after its instrument, and its sample
	// when the instrument has more than one playing
	for (size_t v = 0; v < Voices.size(); ++v) {
		Instrument I;
		I.Kind = INST_2A03;
		const int Inst = Voices[v].first;
		const std::string Name = InstName(Inst), Sample = SampleName(Voices[v]);
		I.Name = Name.empty() ? (Sample.empty() || Song.KeyMaps.empty() ? "Instrument " + std::to_string(Inst + 1) : Sample) : Name;
		if (!Song.KeyMaps.empty() && VoicesOfInst[Inst] > 1 && Sample != I.Name)
			I.Name += " (" + (Sample.empty() ? "sample " + std::to_string(Voices[v].second + 1) : Sample) + ")";
		if (Drum[v])
			I.Drum = DrumKind[v];
		Out.Instruments.push_back(I);
	}

	// The track: a frame for each stretch, its rows from the top, every column
	// playing its part; a break ends the shorter frames, and the last jumps
	// back to where the loop starts
	Track T;
	T.Name = Song.Title.empty() ? "Song" : Song.Title;
	int Length = 1;
	for (size_t s = 0; s < Stretches.size(); ++s)
		Length = std::max(Length, Stretches[s].Last - Stretches[s].First + 1);
	T.PatternLength = std::min(Length, 256);
	// At most MAX_CELLS cells: past them the song is cut short, without its loop
	{
		std::set<std::pair<int, std::pair<int, int> > > Unique;
		for (size_t s = 0; s < Stretches.size(); ++s) {
			Unique.insert(std::make_pair(Stretches[s].Pattern, std::make_pair(Stretches[s].First, Stretches[s].Last)));
			if ((long(Unique.size()) + 1) * Columns * T.PatternLength > MAX_CELLS) {
				Stretches.resize(std::max<size_t>(s, 1));
				LoopTo = -1;
				break;
			}
		}
	}
	T.Columns.assign(Columns, 2);
	T.Patterns.assign(Columns, std::vector<std::vector<Cell> >());
	std::map<std::pair<int, std::pair<int, int> >, int> Made;	// stretch -> pattern
	for (size_t s = 0; s < Stretches.size(); ++s) {
		const Stretch &St = Stretches[s];
		const bool LastFrame = s + 1 == Stretches.size();
		const std::pair<int, std::pair<int, int> > Key(St.Pattern, std::make_pair(St.First, St.Last));
		if (!LastFrame && Made.count(Key)) {
			T.Frames.push_back(std::vector<int>(Columns, Made[Key]));
			continue;
		}
		const int Index = int(T.Patterns[0].size());
		for (int c = 0; c < Columns; ++c)
			T.Patterns[c].push_back(std::vector<Cell>(T.PatternLength));
		if (!LastFrame)
			Made[Key] = Index;
		T.Frames.push_back(std::vector<int>(Columns, Index));
		const TrackerPattern &P = Song.Patterns[St.Pattern];
		// The channels' instruments and columns as the stretch starts
		std::vector<int> Inst(Channels, -1), Last(Channels, -1);
		for (int c = 0; c < Channels; ++c)
			Last[c] = NoteColumn[c] >= 0 ? NoteColumn[c] : DrumColumn[c];
		for (int r = 0; r < St.First; ++r)
			for (int c = 0; c < Channels; ++c)
				if (P.Cells[r][c].Instrument >= 0)
					Inst[c] = P.Cells[r][c].Instrument;
		const int Rows = std::min(St.Last - St.First + 1, T.PatternLength);
		for (int r = 0; r < Rows; ++r) {
			for (int c = 0; c < Channels; ++c) {
				const TrackerCell &Tc = P.Cells[St.First + r][c];
				if (Tc.Instrument >= 0)
					Inst[c] = Tc.Instrument;
				int Col = Last[c], Voice = -1, Pitch = 0, Sample = 0;
				if (Tc.Note >= 0 && Inst[c] >= 0 && Sound(Inst[c], Tc.Note, Sample, Pitch)) {
					Voice = VoiceOf[std::make_pair(Inst[c], Sample)];
					Col = Drum[Voice] ? DrumColumn[c] : NoteColumn[c];
					Last[c] = Col;
				}
				const bool Flow = Tc.Effect == 'B' || Tc.Effect == 'D';	// the frames' ends say it
				if (Col < 0) {
					// A channel without notes: its speed changes go to the first column
					if (Tc.Effect == 'F')
						T.Patterns[0][Index][r].AddEffect('F', Tc.Param);
					continue;
				}
				Cell &Cl = T.Patterns[Col][Index][r];
				if (Voice >= 0) {
					while (Pitch > 95)
						Pitch -= 12;
					Cl.Note = Pitch;
					Cl.Instrument = Voice;
				}
				else if (Tc.Note == TRACKER_OFF)
					Cl.Note = NOTE_RELEASE;
				else if (Tc.Note == TRACKER_CUT)
					Cl.Note = NOTE_HALT;
				if (Tc.Volume >= 0)
					Cl.Volume = (Tc.Volume * 15 + 32) / 64;
				if (Tc.Effect && !Flow)
					Cl.AddEffect(Tc.Effect, Tc.Param);
			}
		}
		// The frame's end: the loop or the song's end on the last, a break on a short one
		char End = 0;
		if (LastFrame)
			End = LoopTo >= 0 ? 'B' : 'C';
		else if (Rows < T.PatternLength)
			End = 'D';
		if (End)
			for (int c = 0; c < Columns; ++c)
				if (T.Patterns[c][Index][Rows - 1].AddEffect(End, End == 'B' ? LoopTo : 0))
					break;
	}

	// The tempo: the module's, or what its first row sets
	int Speed = Song.Speed, Tempo = Song.Tempo;
	{
		const TrackerPattern &P = Song.Patterns[Stretches[0].Pattern];
		for (int c = 0; c < Channels && P.Rows > 0; ++c)
			if (P.Cells[Stretches[0].First][c].Effect == 'F') {
				const int v = P.Cells[Stretches[0].First][c].Param;
				if (v > 0 && v < 0x20)
					Speed = v;
				else if (v >= 0x20)
					Tempo = v;
			}
	}
	T.Speed = Speed;
	T.Tempo = Tempo;
	Out.Tracks.push_back(T);
	Report.Name = T.Name;
	Report.Speed = Speed;
	Report.Tempo = Tempo;
	Report.TicksPerRow = Speed;
	// A beat of 4 rows, or the module's highlight; doubled or halved when the
	// tempo would be far out of the usual. A MIDI file's beat is its own.
	const double RowsPerSecond = Tempo * 2.0 / 5.0 / std::max(1, Speed);
	int PerBeat = Song.RowsPerBeat > 0 ? Song.RowsPerBeat : 4;
	if (Song.Format != "MIDI" && (Song.Format != "IT" || Song.RowsPerBeat == 4)) {
		while (RowsPerSecond * 60 / PerBeat > 190 && PerBeat * 2 <= 16)
			PerBeat *= 2;
		while (RowsPerSecond * 60 / PerBeat < 60 && PerBeat % 2 == 0 && PerBeat > 1)
			PerBeat /= 2;
	}
	Report.RowsPerBeat = PerBeat;
	Report.Bpm = RowsPerSecond * 60 / PerBeat;
	Report.BeatsPerBar = Song.BeatsPerBar > 0 ? Song.BeatsPerBar : 4;
	Report.FirstBarRow = Song.FirstBarRow;
	Report.Meter = Song.Meter;
	return true;
}

}	// namespace nsf

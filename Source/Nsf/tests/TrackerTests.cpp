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

// Tests for reading tracker modules (Tracker.h): MOD, S3M, XM and IT files
// built here byte by byte, their notes, instruments and effects, files cut
// short at every length, drums told by their names, the song followed
// through its breaks and jumps, and a study of the result.

#include "../Study.h"
#include "../Tracker.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace nsf;

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

void Put(std::vector<uint8_t> &b, size_t At, const char *Text, size_t Size)
{
	if (b.size() < At + Size)
		b.resize(At + Size, 0);
	for (size_t i = 0; i < Size && Text[i]; ++i)
		b[At + i] = uint8_t(Text[i]);
}
void Put8(std::vector<uint8_t> &b, size_t At, unsigned v)
{
	if (b.size() < At + 1)
		b.resize(At + 1, 0);
	b[At] = uint8_t(v);
}
void Put16(std::vector<uint8_t> &b, size_t At, unsigned v) { Put8(b, At, v & 255); Put8(b, At + 1, v >> 8); }
void Put32(std::vector<uint8_t> &b, size_t At, unsigned long v) { Put16(b, At, v & 0xFFFF); Put16(b, At + 2, (v >> 16) & 0xFFFF); }

// --- MOD: four channels, a bass, a kick and a snare, a lead, hats ---

const int PERIODS[] = { 856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453 };	// C-1 to B-1 (ProTracker)

// A ProTracker cell: Note from C-1 = 0 (our C-3)
void ModCell(std::vector<uint8_t> &b, int Pattern, int Row, int Channel, int Sample, int Note, int Effect = 0, int Param = 0)
{
	const size_t At = 1084 + size_t(Pattern) * 64 * 4 * 4 + (size_t(Row) * 4 + Channel) * 4;
	int Period = 0;
	if (Note >= 0) {
		Period = PERIODS[Note % 12];
		for (int o = 0; o < Note / 12; ++o)
			Period /= 2;
	}
	Put8(b, At, (Sample & 0xF0) | ((Period >> 8) & 15));
	Put8(b, At + 1, Period & 255);
	Put8(b, At + 2, ((Sample & 15) << 4) | (Effect & 15));
	Put8(b, At + 3, Param);
}

std::vector<uint8_t> MakeMod()
{
	std::vector<uint8_t> b(1084 + 2 * 1024, 0);
	Put(b, 0, "Test MOD", 20);
	const char *Names[] = { "Bass", "Kick", "Snare", "Lead", "noise 1" };
	for (int s = 0; s < 5; ++s) {
		Put(b, 20 + s * 30, Names[s], 22);
		Put8(b, 20 + s * 30 + 25, 64);		// volume
	}
	Put8(b, 950, 2);		// song length
	Put8(b, 951, 127);		// no restart: from the top
	Put8(b, 952, 0);
	Put8(b, 953, 1);
	Put(b, 1080, "M.K.", 4);
	// A minor: the bass on A and E, a bar each; kick on 1 and 3, snare on 2
	// and 4; a lead with vibrato; hats on every eighth (one note: drums)
	const int Lead[] = { 9 + 12, 12 + 12, 16 + 12, 14 + 12, 12 + 12, 11 + 12, 9 + 12, 7 + 12 };	// A-2 C-3 E-3 D-3 C-3 B-2 A-2 G-2 (ProTracker)
	for (int p = 0; p < 2; ++p)
		for (int r = 0; r < 64; ++r) {
			const int Bar = r / 16, InBar = r % 16;
			if (InBar % 4 == 0)
				ModCell(b, p, r, 0, 1, Bar % 2 == 0 ? 9 : 4);	// A-1, E-1
			if (InBar == 0 || InBar == 8)
				ModCell(b, p, r, 1, 2, 12);
			else if (InBar == 4 || InBar == 12)
				ModCell(b, p, r, 1, 3, 12);
			if (r % 8 == 0)
				ModCell(b, p, r, 2, 4, Lead[(r / 8) % 8], 4, r % 16 == 0 ? 0x46 : 0);
			if (r % 2 == 0)
				ModCell(b, p, r, 3, 5, 24);
		}
	ModCell(b, 0, 1, 2, 0, -1, 0xF, 6);		// speed 6
	ModCell(b, 1, 32, 3, 5, 24, 0xD, 0x00);	// pattern 1 breaks at row 32
	return b;
}

// --- S3M: two channels, a piano ---

std::vector<uint8_t> MakeS3m()
{
	std::vector<uint8_t> b(0x200, 0);
	Put(b, 0, "Test S3M", 28);
	Put8(b, 28, 0x1A);
	Put8(b, 29, 16);
	Put16(b, 0x20, 2);		// orders
	Put16(b, 0x22, 1);		// instruments
	Put16(b, 0x24, 1);		// patterns
	Put16(b, 0x28, 0x1320);
	Put16(b, 0x2A, 2);
	Put(b, 0x2C, "SCRM", 4);
	Put8(b, 0x30, 64);
	Put8(b, 0x31, 4);		// speed
	Put8(b, 0x32, 140);		// tempo
	for (int c = 0; c < 32; ++c)
		Put8(b, 0x40 + c, c < 2 ? c : (c == 2 ? 0x82 : 255));	// two channels, one more switched off
	Put8(b, 0x60, 0);
	Put8(b, 0x61, 255);
	Put16(b, 0x62, 0x100 / 16);		// the instrument at 0x100
	Put16(b, 0x64, 0x160 / 16);		// the pattern at 0x160
	Put8(b, 0x100, 1);
	Put(b, 0x101, "piano.raw", 12);
	Put(b, 0x100 + 0x30, "Grand piano", 28);
	Put(b, 0x100 + 0x4C, "SCRS", 4);
	std::vector<uint8_t> p;
	// Row 0: channel 0 C-4 piano at volume 32 with H44; channel 1 E-4
	p.push_back(32 | 64 | 128 | 0); p.push_back(0x40); p.push_back(1); p.push_back(32); p.push_back(8); p.push_back(0x44);
	p.push_back(32 | 1); p.push_back(0x44); p.push_back(1);
	p.push_back(0);
	// Row 1: speed A03 on channel 1, and a note on the switched-off channel 2 (dropped)
	p.push_back(128 | 1); p.push_back(1); p.push_back(3);
	p.push_back(32 | 2); p.push_back(0x47); p.push_back(1);
	p.push_back(0);
	// Row 2: a note cut, a break to row 16 (C16, decimal digits)
	p.push_back(32 | 0); p.push_back(254); p.push_back(0);
	p.push_back(128 | 1); p.push_back(3); p.push_back(0x16);
	p.push_back(0);
	for (int r = 3; r < 64; ++r)
		p.push_back(0);
	Put16(b, 0x160, unsigned(p.size()));
	for (size_t i = 0; i < p.size(); ++i)
		Put8(b, 0x162 + i, p[i]);
	return b;
}

// --- XM: two channels, an instrument with a sample ---

std::vector<uint8_t> MakeXm()
{
	std::vector<uint8_t> b;
	Put(b, 0, "Extended Module: ", 17);
	Put(b, 17, "Test XM", 20);
	Put8(b, 37, 0x1A);
	Put(b, 38, "FastTracker v2.00", 20);
	Put16(b, 58, 0x0104);
	Put32(b, 60, 276);		// header size
	Put16(b, 64, 2);		// song length
	Put16(b, 66, 1);		// restart at order 1
	Put16(b, 68, 2);		// channels
	Put16(b, 70, 1);		// patterns
	Put16(b, 72, 2);		// instruments
	Put16(b, 74, 1);
	Put16(b, 76, 3);		// speed
	Put16(b, 78, 150);		// BPM
	Put8(b, 80, 0);
	Put8(b, 81, 0);
	size_t x = 60 + 276;
	// The pattern: 4 rows
	std::vector<uint8_t> p;
	// Row 0: channel 0 C-4 (49) instrument 1, volume 0x40 (48), no effect, written in full; channel 1 packed: note A-4, instrument 2
	p.push_back(49); p.push_back(1); p.push_back(0x40); p.push_back(0); p.push_back(0);
	p.push_back(0x80 | 1 | 2); p.push_back(58); p.push_back(2);
	// Row 1: channel 0 volume-column vibrato (0xB3); channel 1 0x47 arpeggio
	p.push_back(0x80 | 4); p.push_back(0xB3);
	p.push_back(0x80 | 8 | 16); p.push_back(0); p.push_back(0x47);
	// Row 2: key off on channel 0; nothing on channel 1
	p.push_back(0x80 | 1); p.push_back(97);
	p.push_back(0x80);
	// Row 3: nothing, nothing
	p.push_back(0x80); p.push_back(0x80);
	Put32(b, x, 9);
	Put8(b, x + 4, 0);
	Put16(b, x + 5, 4);
	Put16(b, x + 7, unsigned(p.size()));
	for (size_t i = 0; i < p.size(); ++i)
		Put8(b, x + 9 + i, p[i]);
	x += 9 + p.size();
	// Two instruments, each with one sample of 4 bytes
	const char *Names[] = { "Slap bass", "BD 909" };
	for (int i = 0; i < 2; ++i) {
		Put32(b, x, 263);
		Put(b, x + 4, Names[i], 22);
		Put16(b, x + 27, 1);
		Put32(b, x + 29, 40);
		x += 263;
		Put32(b, x, 4);			// length
		Put(b, x + 18, i ? "kick sample" : "bass sample", 22);
		x += 40;
		Put32(b, x, 0x01020304);	// the data
		x += 4;
	}
	return b;
}

// --- IT: channels 1 and 6 used, an instrument and a sample ---

std::vector<uint8_t> MakeIt()
{
	std::vector<uint8_t> b(0x300, 0);
	Put(b, 0, "IMPM", 4);
	Put(b, 4, "Test IT", 26);
	Put8(b, 0x1E, 4);		// rows a beat
	Put8(b, 0x1F, 16);
	Put16(b, 0x20, 2);		// orders
	Put16(b, 0x22, 1);		// instruments
	Put16(b, 0x24, 1);		// samples
	Put16(b, 0x26, 1);		// patterns
	Put16(b, 0x28, 0x5130);	// OpenMPT
	Put16(b, 0x2A, 0x0214);
	Put16(b, 0x2C, 4 | 1);	// instruments in use
	Put16(b, 0x2E, 1);		// a message
	Put8(b, 0x32, 5);		// speed
	Put8(b, 0x33, 120);		// tempo
	Put16(b, 0x36, 12);
	Put32(b, 0x38, 0x2F0);
	Put(b, 0x2F0, "Hello\rworld!", 12);
	Put8(b, 0xC0, 0);
	Put8(b, 0xC1, 255);
	Put32(b, 0xC2, 0x100);	// instrument
	Put32(b, 0xC6, 0x180);	// sample
	Put32(b, 0xCA, 0x200);	// pattern
	Put(b, 0x100, "IMPI", 4);
	Put(b, 0x100 + 0x20, "Warm strings", 26);
	Put(b, 0x180, "IMPS", 4);
	Put(b, 0x180 + 0x14, "strings.wav", 26);
	std::vector<uint8_t> p;
	// Row 0: channel 1 (cv 1): note C-5 (60), instrument 1, volume 48, command H (8) 0x34
	p.push_back(128 | 1); p.push_back(1 | 2 | 4 | 8); p.push_back(60); p.push_back(1); p.push_back(48); p.push_back(8); p.push_back(0x34);
	// channel 6 (cv 6): note E-5 (64), instrument 1, volume-column vibrato 206 (203 + 3)
	p.push_back(128 | 6); p.push_back(1 | 2 | 4); p.push_back(64); p.push_back(1); p.push_back(206);
	p.push_back(0);
	// Row 1: channel 1 with the last mask but new values; then the same values again (masks 16-128)
	p.push_back(1); p.push_back(62); p.push_back(1); p.push_back(40); p.push_back(10); p.push_back(0x37);
	p.push_back(128 | 6); p.push_back(16 | 32 | 64);
	p.push_back(0);
	// Row 2: a note cut on channel 1, a note off on channel 6
	p.push_back(128 | 1); p.push_back(1); p.push_back(254);
	p.push_back(128 | 6); p.push_back(1); p.push_back(255);
	p.push_back(0);
	// Row 3: nothing
	p.push_back(0);
	Put16(b, 0x200, unsigned(p.size()));
	Put16(b, 0x202, 4);		// rows
	for (size_t i = 0; i < p.size(); ++i)
		Put8(b, 0x208 + i, p[i]);
	return b;
}

void TestMod()
{
	const std::vector<uint8_t> Bytes = MakeMod();
	TrackerSong S;
	std::string Error;
	CHECK(ParseTracker(Bytes, S, Error));
	CHECK(S.Format == "MOD");
	CHECK(S.Title == "Test MOD");
	CHECK(S.Channels == 4);
	CHECK(S.Orders.size() == 2 && S.Orders[0] == 0 && S.Orders[1] == 1);
	CHECK(S.Patterns.size() == 2);
	CHECK(S.Instruments.size() == 31 && S.Instruments[0] == "Bass" && S.Instruments[4] == "noise 1");
	CHECK(S.Restart == 0);
	// Notes: ProTracker's C-1 is our C-3; the sample from 1
	const TrackerCell &Bass = S.Patterns[0].Cells[0][0];
	CHECK(Bass.Note == 36 + 9 && Bass.Instrument == 0);
	CHECK(S.Patterns[0].Cells[0][1].Note == 48 && S.Patterns[0].Cells[0][1].Instrument == 1);
	CHECK(S.Patterns[0].Cells[0][2].Effect == '4' && S.Patterns[0].Cells[0][2].Param == 0x46);
	CHECK(S.Patterns[0].Cells[1][2].Effect == 'F' && S.Patterns[0].Cells[1][2].Param == 6);
	CHECK(S.Patterns[1].Cells[32][3].Effect == 'D' && S.Patterns[1].Cells[32][3].Param == 0);
	CHECK(S.Patterns[0].Cells[1][0].Note == TRACKER_NONE && S.Patterns[0].Cells[1][0].Instrument == -1);

	// As a module: the bass, the drums of channel 2, the lead, the hats (one note: drums)
	Module M;
	TrackReport R;
	CHECK(TrackerToModule(S, M, R, Error));
	CHECK(M.ChannelNames.size() == 4);
	CHECK(M.ChannelNames.size() == 4 && M.ChannelNames[0] == "Channel 1" && M.ChannelNames[1] == "Channel 2 (drums)" &&
		M.ChannelNames[2] == "Channel 3" && M.ChannelNames[3] == "Channel 4 (drums)");
	CHECK(M.ChannelKinds.size() == 4 && M.ChannelKinds[0] == LAYOUT_MELODIC && M.ChannelKinds[1] == LAYOUT_DRUMS);
	CHECK(M.ChannelCount() == 4);
	// An instrument for each sound played, in the order they first play
	CHECK(M.Instruments.size() == 5);
	if (M.Instruments.size() == 5) {
		CHECK(M.Instruments[0].Name == "Bass" && M.Instruments[0].Drum.empty());
		CHECK(M.Instruments[1].Name == "Kick" && M.Instruments[1].Drum == "kick");
		CHECK(M.Instruments[2].Name == "Lead" && M.Instruments[2].Drum.empty());
		CHECK(M.Instruments[3].Name == "noise 1" && M.Instruments[3].Drum == "drum");
		CHECK(M.Instruments[4].Name == "Snare" && M.Instruments[4].Drum == "snare");
	}
	CHECK(R.Speed == 6 && R.Tempo == 125 && R.RowsPerBeat == 4);
	CHECK(R.Bpm > 124.9 && R.Bpm < 125.1);
	CHECK(M.Tracks.size() == 1);
	if (M.Tracks.size() == 1) {
		const Track &T = M.Tracks[0];
		CHECK(T.Frames.size() == 2 && T.PatternLength == 64);
		// The second frame stops at the break (33 rows) and loops back to the top
		if (T.Frames.size() == 2) {
			const int p = T.Frames[1][0];
			bool Loop = false;
			for (int c = 0; c < 4; ++c)
				for (int e = 0; e < MAX_EFFECTS; ++e)
					Loop = Loop || (T.Patterns[c][p][32].Effects[e].Letter == 'B' && T.Patterns[c][p][32].Effects[e].Param == 0);
			CHECK(Loop);
			CHECK(T.Patterns[0][T.Frames[0][0]][0].Note == 45 && T.Patterns[0][T.Frames[0][0]][0].Instrument == 0);
			CHECK(T.Patterns[1][T.Frames[0][0]][4].Instrument == 4);
		}
	}

	// The study: tempo, key, the lead's vibrato, the drums by name and on the backbeat
	const TrackStudy St = StudyTrack(M, 0, R);
	const std::string Text = DescribeStudy(St);
	CHECK(St.Tracker && St.Speed == 6 && St.Tempo == 125);
	CHECK(Text.find("(speed 6, tempo 125)") != std::string::npos);
	CHECK(St.Key == "A minor");
	CHECK(St.FirstBarRow == 0);
	CHECK(Text.find("kick 'Kick'") != std::string::npos);
	CHECK(Text.find("snare 'Snare'") != std::string::npos);
	CHECK(Text.find("Channel 3, the lead") != std::string::npos);
	CHECK(Text.find("Channel 1, bass") != std::string::npos);
	CHECK(Text.find("vibrato on") != std::string::npos);
	CHECK(Text.find("its instruments: 'Lead'") != std::string::npos);
	CHECK(Text.find("drum, steady eighths or faster 'noise 1'") != std::string::npos);
	CHECK(St.Sounds.empty());
	if (g_Failures)
		std::printf("%s\n", Text.c_str());
}

void TestS3m()
{
	TrackerSong S;
	std::string Error;
	CHECK(ParseTracker(MakeS3m(), S, Error));
	CHECK(S.Format == "S3M" && S.Title == "Test S3M");
	CHECK(S.Channels == 2);
	CHECK(S.Speed == 4 && S.Tempo == 140);
	CHECK(S.Orders.size() == 2 && S.Orders[0] == 0 && S.Orders[1] == -1);
	CHECK(S.Instruments.size() == 1 && S.Instruments[0] == "Grand piano");
	CHECK(S.Tracker == "Scream Tracker 3");
	CHECK(S.Patterns.size() == 1);
	if (S.Patterns.size() == 1) {
		const TrackerPattern &P = S.Patterns[0];
		CHECK(P.Cells[0][0].Note == 48 && P.Cells[0][0].Instrument == 0 && P.Cells[0][0].Volume == 32);
		CHECK(P.Cells[0][0].Effect == '4' && P.Cells[0][0].Param == 0x44);
		CHECK(P.Cells[0][1].Note == 52 && P.Cells[0][1].Volume == -1);
		CHECK(P.Cells[1][1].Effect == 'F' && P.Cells[1][1].Param == 3);
		CHECK(P.Cells[2][0].Note == TRACKER_CUT);
		CHECK(P.Cells[2][1].Effect == 'D' && P.Cells[2][1].Param == 16);
	}
	// It stops at the end mark, then plays again from the top
	Module M;
	TrackReport R;
	CHECK(TrackerToModule(S, M, R, Error));
	CHECK(M.Tracks.size() == 1 && M.Tracks[0].Frames.size() == 1 && M.Tracks[0].PatternLength == 3);
}

void TestXm()
{
	TrackerSong S;
	std::string Error;
	CHECK(ParseTracker(MakeXm(), S, Error));
	CHECK(S.Format == "XM" && S.Title == "Test XM" && S.Tracker == "FastTracker v2.00");
	CHECK(S.Channels == 2 && S.Speed == 3 && S.Tempo == 150 && S.Restart == 1);
	CHECK(S.Orders.size() == 2);
	CHECK(S.Instruments.size() == 2 && S.Instruments[0] == "Slap bass" && S.Instruments[1] == "BD 909");
	CHECK(S.Samples.size() == 2 && S.Samples[1] == "kick sample");
	CHECK(S.Patterns.size() == 1 && S.Patterns[0].Rows == 4);
	if (S.Patterns.size() == 1) {
		const TrackerPattern &P = S.Patterns[0];
		CHECK(P.Cells[0][0].Note == 48 && P.Cells[0][0].Instrument == 0 && P.Cells[0][0].Volume == 48);
		CHECK(P.Cells[0][1].Note == 57 && P.Cells[0][1].Instrument == 1);
		CHECK(P.Cells[1][0].Effect == '4' && P.Cells[1][0].Param == 3);
		CHECK(P.Cells[1][1].Effect == '0' && P.Cells[1][1].Param == 0x47);
		CHECK(P.Cells[2][0].Note == TRACKER_OFF);
		CHECK(P.Cells[3][1].Note == TRACKER_NONE);
	}
	CHECK(DrumOfName(S.Instruments[1]) == "kick");
	// Speed 3 at 150: 8 rows a beat keeps the tempo in the usual range
	Module M;
	TrackReport R;
	CHECK(TrackerToModule(S, M, R, Error));
	CHECK(R.RowsPerBeat == 8 && R.Bpm > 149 && R.Bpm < 151);
	// Order 0, order 1, then back to the restart order 1: a loop to frame 1
	CHECK(M.Tracks.size() == 1 && M.Tracks[0].Frames.size() == 2);
}

void TestIt()
{
	TrackerSong S;
	std::string Error;
	CHECK(ParseTracker(MakeIt(), S, Error));
	CHECK(S.Format == "IT" && S.Title == "Test IT" && S.Tracker == "OpenMPT");
	CHECK(S.Message == "Hello\nworld!");
	CHECK(S.RowsPerBeat == 4 && S.Speed == 5 && S.Tempo == 120);
	CHECK(S.Channels == 2);
	CHECK(S.Instruments.size() == 1 && S.Instruments[0] == "Warm strings");
	CHECK(S.Samples.size() == 1 && S.Samples[0] == "strings.wav");
	CHECK(S.Orders.size() == 2 && S.Orders[1] == -1);
	CHECK(S.Patterns.size() == 1 && S.Patterns[0].Rows == 4);
	if (S.Patterns.size() == 1) {
		const TrackerPattern &P = S.Patterns[0];
		CHECK(P.Cells[0][0].Note == 48 && P.Cells[0][0].Instrument == 0 && P.Cells[0][0].Volume == 48);
		CHECK(P.Cells[0][0].Effect == '4' && P.Cells[0][0].Param == 0x34);
		CHECK(P.Cells[0][1].Note == 52 && P.Cells[0][1].Effect == '4' && P.Cells[0][1].Param == 3);
		CHECK(P.Cells[1][0].Note == 50 && P.Cells[1][0].Volume == 40 && P.Cells[1][0].Effect == '0' && P.Cells[1][0].Param == 0x37);
		CHECK(P.Cells[1][1].Note == 52 && P.Cells[1][1].Instrument == 0 && P.Cells[1][1].Effect == '4');
		CHECK(P.Cells[2][0].Note == TRACKER_CUT);
		CHECK(P.Cells[2][1].Note == TRACKER_OFF);
	}
}

void TestNames()
{
	CHECK(DrumOfName("BD 909") == "kick");
	CHECK(DrumOfName("Kick drum") == "kick");
	CHECK(DrumOfName("808 bass drum") == "kick");
	CHECK(DrumOfName("SD1") == "snare");
	CHECK(DrumOfName("Snr") == "snare");
	CHECK(DrumOfName("handclap") == "");
	CHECK(DrumOfName("Clap") == "snare");
	CHECK(DrumOfName("hh closed") == "hi-hat");
	CHECK(DrumOfName("OH") == "hi-hat");
	CHECK(DrumOfName("HiHat 2") == "hi-hat");
	CHECK(DrumOfName("Crash") == "cymbal");
	CHECK(DrumOfName("tom2") == "tom");
	CHECK(DrumOfName("Conga") == "percussion");
	CHECK(DrumOfName("Bass") == "");
	CHECK(DrumOfName("Strings") == "");
	CHECK(DrumOfName("tomato") == "");
	CHECK(DrumOfName("") == "");
}

// Files cut short at every length, and bytes changed, read without harm
void TestBroken()
{
	const std::vector<uint8_t> Files[] = { MakeMod(), MakeS3m(), MakeXm(), MakeIt() };
	int Read = 0;
	for (size_t f = 0; f < sizeof(Files) / sizeof(Files[0]); ++f) {
		for (size_t n = 0; n < Files[f].size(); n += (n < 1200 ? 1 : 37)) {
			const std::vector<uint8_t> Cut(Files[f].begin(), Files[f].begin() + n);
			TrackerSong S;
			std::string Error;
			if (ParseTracker(Cut, S, Error)) {
				++Read;
				Module M;
				TrackReport R;
				if (TrackerToModule(S, M, R, Error))
					StudyTrack(M, 0, R);
			}
		}
		// Every byte of the first 0x300 set to 0xFF in turn
		for (size_t i = 0; i < Files[f].size() && i < 0x300; ++i) {
			std::vector<uint8_t> Bad = Files[f];
			Bad[i] = 0xFF;
			TrackerSong S;
			std::string Error;
			if (ParseTracker(Bad, S, Error)) {
				Module M;
				TrackReport R;
				if (TrackerToModule(S, M, R, Error))
					StudyTrack(M, 0, R);
			}
		}
	}
	CHECK(Read > 0);
	TrackerSong S;
	std::string Error;
	CHECK(!ParseTracker(std::vector<uint8_t>(100, 0), S, Error) && !Error.empty());
}

}	// namespace

int main(int argc, char **argv)
{
	// With a folder: the modules built here written to it, to look at
	if (argc > 1) {
		const char *Names[] = { "test.mod", "test.s3m", "test.xm", "test.it" };
		const std::vector<uint8_t> Files[] = { MakeMod(), MakeS3m(), MakeXm(), MakeIt() };
		for (int i = 0; i < 4; ++i) {
			const std::string Path = std::string(argv[1]) + "/" + Names[i];
			std::ofstream Out(Path.c_str(), std::ios::binary);
			Out.write(reinterpret_cast<const char *>(Files[i].data()), static_cast<std::streamsize>(Files[i].size()));
		}
	}
	TestMod();
	TestS3m();
	TestXm();
	TestIt();
	TestNames();
	TestBroken();
	std::printf("%d checks, %d failed\n", g_Checks, g_Failures);
	return g_Failures ? 1 : 0;
}

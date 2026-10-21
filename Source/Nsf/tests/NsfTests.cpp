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

// Tests for the NSF importer: NSF and NSFe files and their playlists, the
// 6502 and the player on NSFs built here (a little player program with its
// register writes frame by frame), finding the loop and the rows, the
// conversion of melodies, legato notes, chords, drums and duties, and the
// FamiTracker model of the result playing like the NSF.

#include "../Convert.h"
#include "../Cpu6502.h"
#include "../Import.h"
#include "../Module.h"
#include "../NsfFile.h"
#include "../Player.h"
#include "../Simulate.h"
#include "TestNsf.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
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

// NSFs built here: tests/TestNsf.h
using namespace nsftest;

std::vector<int> Envelope(std::initializer_list<int> v) { return std::vector<int>(v); }

// Plays and converts an NSF built here
struct Converted {
	NsfFile File;
	Recording Rec;
	Module Song;
	ConvertReport Report;
	bool Ok;
	Converted() : Ok(false) {}
};

void Convert(const std::vector<uint8_t> &Bytes, Converted &Out)
{
	std::string Error;
	Out.Ok = false;
	if (!ParseNsf(Bytes, Out.File, Error)) {
		std::printf("  parse: %s\n", Error.c_str());
		return;
	}
	RecordOptions Options;
	Options.MaxSeconds = 60;
	if (!RecordTrack(Out.File, 0, Options, Out.Rec, Error)) {
		std::printf("  record: %s\n", Error.c_str());
		return;
	}
	std::vector<TrackSource> Sources(1);
	Sources[0].Name = "Test";
	Sources[0].Rec = &Out.Rec;
	if (!ConvertTracks(Out.File, Sources, ConvertOptions(), Out.Song, Out.Report, Error)) {
		std::printf("  convert: %s\n", Error.c_str());
		return;
	}
	Out.Ok = true;
}

// How closely the module plays what the NSF did, over the loop
Agreement PlaysLike(const Converted &c)
{
	const int N = c.Rec.LoopStart >= 0 ? c.Rec.LoopStart + c.Rec.LoopLength : c.Rec.Length;
	Recording Sim;
	std::string Error;
	if (!SimulateTrack(c.Song, 0, N + 32, Sim, Error)) {
		std::printf("  simulate: %s\n", Error.c_str());
		return Agreement();
	}
	return Compare(c.Rec, Sim, N, BestOffset(c.Rec, Sim, N, 8));
}

// The notes of a channel's column through the track, in row order
std::vector<std::pair<int, Cell> > ColumnNotes(const Module &M, int Column)
{
	std::vector<std::pair<int, Cell> > Out;
	const Track &T = M.Tracks[0];
	for (size_t f = 0; f < T.Frames.size(); ++f) {
		const std::vector<Cell> &P = T.Patterns[Column][T.Frames[f][Column]];
		for (int r = 0; r < T.PatternLength; ++r)
			if (P[r].Note >= 0)
				Out.push_back(std::make_pair(int(f) * T.PatternLength + r, P[r]));
	}
	return Out;
}

bool HasEffect(const Cell &c, char Letter, int Param = -1)
{
	for (int e = 0; e < MAX_EFFECTS; ++e)
		if (c.Effects[e].Letter == Letter && (Param < 0 || c.Effects[e].Param == Param))
			return true;
	return false;
}

// ---------------------------------------------------------------------------
// Files and lists
// ---------------------------------------------------------------------------

void TestFiles()
{
	std::vector<Frame> Frames(4);
	const std::vector<uint8_t> Bytes = BuildNsf(Frames, 0, "Header test");
	NsfFile f;
	std::string Error;
	CHECK(ParseNsf(Bytes, f, Error));
	CHECK(f.Title == "Header test");
	CHECK(f.Artist == "Tester");
	CHECK(f.Songs == 1 && f.StartSong == 0);
	CHECK(f.LoadAddress == 0x8000 && f.InitAddress == 0x8000);
	CHECK(std::fabs(f.PlayRate() - 60.0) < 0.05);
	CHECK(f.Chips == 0 && !f.Pal && !f.Bankswitched);

	// Not an NSF, cut short
	std::vector<uint8_t> Bad(Bytes.begin(), Bytes.begin() + 40);
	CHECK(!ParseNsf(Bad, f, Error));
	Bad = Bytes;
	Bad[0] = 'X';
	CHECK(!ParseNsf(Bad, f, Error));

	// NSFe: INFO, DATA, tlbl, NEND
	std::vector<uint8_t> e;
	auto Chunk = [&](const char *Id, const std::vector<uint8_t> &Data) {
		const uint32_t n = uint32_t(Data.size());
		e.push_back(uint8_t(n)); e.push_back(uint8_t(n >> 8)); e.push_back(uint8_t(n >> 16)); e.push_back(uint8_t(n >> 24));
		e.insert(e.end(), Id, Id + 4);
		e.insert(e.end(), Data.begin(), Data.end());
	};
	e.insert(e.end(), { 'N', 'S', 'F', 'E' });
	std::vector<uint8_t> Info = { 0x00, 0x80, 0x00, 0x80, 0x10, 0x80, 0, 0, 2, 0 };	// load, init, play, PAL/NTSC, chips, songs, start
	Chunk("INFO", Info);
	Chunk("DATA", std::vector<uint8_t>(Bytes.begin() + 128, Bytes.end()));
	const std::string Labels = std::string("Opening") + '\0' + "Battle" + '\0';
	Chunk("tlbl", std::vector<uint8_t>(Labels.begin(), Labels.end()));
	Chunk("NEND", std::vector<uint8_t>());
	CHECK(ParseNsf(e, f, Error));
	CHECK(f.Nsfe && f.Songs == 2);
	CHECK(f.TrackName(0) == "Opening" && f.TrackName(1) == "Battle");
	CHECK(TrackTitle(f, 1) == "Battle");

	// A playlist names the tracks of an NSF without names
	NsfFile p;
	CHECK(ParseNsf(Bytes, p, Error));
	p.Songs = 3;
	const std::string M3u = "# Test\r\n"
		"Test.nsf::NSF,3,Boss\\, the second,0:01:29.5,,0:00:05\r\n"
		"Test.nsf::NSF,1,Title,0:03,,\r\n"
		"Other.spc::SPC,2,Not this one,1:00,,\r\n";
	CHECK(ApplyPlaylist(M3u, p) == 2);
	CHECK(p.TrackName(0) == "Title");
	CHECK(p.TrackName(2) == "Boss, the second");
	CHECK(p.TrackTime(2) == 89500 && p.TrackTime(0) == 3000);
	CHECK(p.TrackName(1).empty() && TrackTitle(p, 1) == "Track 2");
	CHECK(p.Playlist.size() == 2 && p.Playlist[0] == 2 && p.Playlist[1] == 0);
	// Counting from 0 when a track 0 is there
	NsfFile z;
	CHECK(ParseNsf(Bytes, z, Error));
	z.Songs = 2;
	CHECK(ApplyPlaylist("a.nsf::NSF,0,First,,\na.nsf::NSF,1,Second,,\n", z) == 2);
	CHECK(z.TrackName(0) == "First" && z.TrackName(1) == "Second");
	// The game and composer before every title go
	NsfFile q;
	CHECK(ParseNsf(Bytes, q, Error));
	q.Songs = 2;
	CHECK(ApplyPlaylist("x.nsf::NSF,1,Game - A. Composer - Flash Man,,\nx.nsf::NSF,2,Game - A. Composer - Wood Man,,\n", q) == 2);
	CHECK(q.TrackName(0) == "Flash Man" && q.TrackName(1) == "Wood Man");

	// Track lists
	std::vector<int> t;
	CHECK(ParseTrackList("1,3-5", 8, t, Error) && t.size() == 4 && t[0] == 0 && t[1] == 2 && t[3] == 4);
	CHECK(ParseTrackList("all", 3, t, Error) && t.size() == 3);
	CHECK(ParseTrackList("", 2, t, Error) && t.size() == 2);
	CHECK(!ParseTrackList("9", 8, t, Error));
	CHECK(!ParseTrackList("5-3", 8, t, Error));
	CHECK(!ParseTrackList("two", 8, t, Error));
}

// ---------------------------------------------------------------------------
// The 6502
// ---------------------------------------------------------------------------

class Ram : public Bus {
public:
	uint8_t Mem[65536];
	Ram() { std::memset(Mem, 0, sizeof(Mem)); }
	uint8_t Read(uint16_t Address, uint64_t) { return Mem[Address]; }
	void Write(uint16_t Address, uint8_t Value, uint64_t) { Mem[Address] = Value; }
};

void TestCpu()
{
	Ram r;
	Cpu6502 c(r);
	// $8000: LDA #$7F; CLC; ADC #$01; STA $10 (128: overflow); PHP; PLA; STA $11;
	//        SEC; LDA #$00; SBC #$01; STA $12 (255, borrow); JSR $8100; STX $13; RTS
	// $8100: LDX #$42; RTS
	const uint8_t Code[] = { 0xA9, 0x7F, 0x18, 0x69, 0x01, 0x85, 0x10, 0x08, 0x68, 0x85, 0x11,
		0x38, 0xA9, 0x00, 0xE9, 0x01, 0x85, 0x12, 0x20, 0x00, 0x81, 0x86, 0x13, 0x60 };
	std::memcpy(&r.Mem[0x8000], Code, sizeof(Code));
	r.Mem[0x8100] = 0xA2; r.Mem[0x8101] = 0x42; r.Mem[0x8102] = 0x60;
	CHECK(c.Call(0x8000, 0, 0, 0, 10000));
	CHECK(r.Mem[0x10] == 0x80);
	CHECK((r.Mem[0x11] & 0x40) != 0);	// overflow
	CHECK((r.Mem[0x11] & 0x80) != 0);	// negative
	CHECK(r.Mem[0x12] == 0xFF);
	CHECK(r.Mem[0x13] == 0x42);
	CHECK(!c.Jammed());

	// A loop counting down: LDX #5; loop: DEX; BNE loop; STX $20; RTS
	const uint8_t Loop[] = { 0xA2, 0x05, 0xCA, 0xD0, 0xFD, 0x86, 0x20, 0x60 };
	std::memcpy(&r.Mem[0x9000], Loop, sizeof(Loop));
	r.Mem[0x20] = 99;
	CHECK(c.Call(0x9000, 0, 0, 0, 10000));
	CHECK(r.Mem[0x20] == 0);
	// One that never returns stops at the cycle limit
	r.Mem[0x9100] = 0x4C; r.Mem[0x9101] = 0x00; r.Mem[0x9102] = 0x91;	// JMP $9100
	CHECK(!c.Call(0x9100, 0, 0, 0, 5000));
}

// ---------------------------------------------------------------------------
// Loops and rows
// ---------------------------------------------------------------------------

void TestLoopsAndRows()
{
	// An intro of 10 ticks, then 20 repeating
	std::vector<uint64_t> v;
	for (int i = 0; i < 10; ++i)
		v.push_back(1000 + i);
	for (int k = 0; k < 4; ++k)
		for (int i = 0; i < 20; ++i)
			v.push_back(i * 7 % 13);
	int Start = -1, Period = -1;
	CHECK(FindLoop(v, 8, 4, Start, Period));
	CHECK(Start == 10 && Period == 20);
	std::vector<uint64_t> None;
	for (int i = 0; i < 50; ++i)
		None.push_back(i);
	CHECK(!FindLoop(None, 8, 4, Start, Period));

	// Starts every 6 ticks from tick 3, a few off the rows
	std::vector<std::pair<int, double> > On;
	for (int i = 0; i < 40; ++i)
		On.push_back(std::make_pair(3 + 6 * i, 1.0));
	On.push_back(std::make_pair(50, 0.4));
	double u = 0, Phase = 0, Aligned = 0;
	CHECK(FindGrid(On, u, Phase, Aligned));
	CHECK(std::fabs(u - 6) < 0.01 || std::fabs(u - 12) < 0.01 || std::fabs(u - 18) < 0.01);
	CHECK(std::fmod(Phase, 6.0) > 2.9 && std::fmod(Phase, 6.0) < 3.1);
	CHECK(Aligned > 0.95);

	int Speed = 0, Tempo = 0;
	SpeedTempo(6, 60, Speed, Tempo);
	CHECK(Speed == 6 && Tempo == 150);
	SpeedTempo(4.5, 60, Speed, Tempo);
	CHECK(std::fabs(2.5 * 60 * Speed / Tempo - 4.5) < 0.01);
	SpeedTempo(5, 50, Speed, Tempo);
	CHECK(Speed == 5 && Tempo == 125);
}

// ---------------------------------------------------------------------------
// Conversions
// ---------------------------------------------------------------------------

// A melody: a note every 12 frames, cut 2 frames before the next
void TestMelody()
{
	const int Notes[] = { 48, 50, 52, 53, 55, 57, 59, 60, 59, 57, 55, 53, 52, 50, 48, 43 };	// C-4 up and down, G-3
	std::vector<Tone> Tones;
	for (int i = 0; i < 16; ++i) {
		Tone t = { 12 * i, 10, Notes[i], Envelope({ 15, 14, 13, 12 }), 2, true };
		Tones.push_back(t);
	}
	Converted c;
	Convert(BuildNsf(PulseFrames(Tones, 192), 0, "Melody"), c);
	CHECK(c.Ok);
	if (!c.Ok)
		return;
	CHECK(c.Rec.LoopStart >= 0 && c.Rec.LoopLength == 192);
	CHECK(c.Report.Tracks.size() == 1);
	const TrackReport &t = c.Report.Tracks[0];
	CHECK(t.Speed == 6 && t.Tempo == 150);
	CHECK(t.RowsPerBeat == 4);
	CHECK(t.Rows == 32);
	CHECK(t.Notes == 16);
	CHECK(t.Delayed == 0);
	const std::vector<std::pair<int, Cell> > n = ColumnNotes(c.Song, 0);
	CHECK(n.size() == 16);
	bool Right = n.size() == 16;
	for (size_t i = 0; i < n.size() && Right; ++i)
		Right = n[i].first == int(i) * 2 && n[i].second.Note == Notes[i];
	CHECK(Right);
	// Cut short of the next note: a note cut after 10 ticks
	CHECK(n.size() == 16 && HasEffect(n[0].second, 'S', 10));
	// One instrument: its envelope, at full volume
	CHECK(c.Song.Instruments.size() == 1);
	const Macro &v = c.Song.Instruments[0].Macros[SEQ_VOLUME];
	CHECK(!v.Items.empty() && v.Items[0] == 15 && v.Items.back() == 12);
	const Agreement a = PlaysLike(c);
	CHECK(a.Sound > 0.99);
	CHECK(a.Pitch > 0.99);

	// The text FamiTracker reads
	const std::string Text = WriteText(c.Song);
	CHECK(Text.find("# FamiTracker text export") != std::string::npos);
	CHECK(Text.find("TRACK  64   6 150 \"Test\"") != std::string::npos);
	CHECK(Text.find("PATTERN 00") != std::string::npos);
	CHECK(Text.find("ROW 00 : C-4 00 F") != std::string::npos);
	CHECK(Text.find("TITLE           \"Melody\"") != std::string::npos);
}

// Legato notes: the pitch steps every 5 frames without a new attack or a
// restart of the channel
void TestLegato()
{
	// Around A-4, where the periods are under 256 and $4003 stays as it is
	const int Steps[] = { 0, 3, 7, 5, 3, 2, 0, -2, 0, 2, 3, 5, 7, 8, 7, 5, 3, 2, 3, 5, 2, 0, -2, -4,
		-2, 0, 2, 0, 3, 7, 10, 7 };
	std::vector<Tone> Tones;
	for (int i = 0; i < 32; ++i) {
		Tone t = { 5 * i, 5, 57 + Steps[i], Envelope({ 11, 10 }), 1, i == 0 };
		Tones.push_back(t);
	}
	Converted c;
	Convert(BuildNsf(PulseFrames(Tones, 160), 0, "Legato"), c);
	CHECK(c.Ok);
	if (!c.Ok)
		return;
	CHECK(c.Rec.LoopLength == 160);
	CHECK(c.Report.Tracks[0].Speed == 5 && c.Report.Tracks[0].Tempo == 150);
	const std::vector<std::pair<int, Cell> > n = ColumnNotes(c.Song, 0);
	CHECK(n.size() == 32);
	bool Right = n.size() == 32;
	for (size_t i = 0; i < n.size() && Right; ++i)
		Right = n[i].first == int(i) && n[i].second.Note == 57 + Steps[i];
	CHECK(Right);
	const Agreement a = PlaysLike(c);
	CHECK(a.Pitch > 0.99);
}

// Chords as arpeggios a step a frame, struck every 12 frames: each a note
// with 0xy
void TestChord()
{
	std::vector<Frame> F(192);
	for (int i = 0; i < 16; ++i) {
		const int Root = i < 8 ? 48 : 53;		// C-4, then F-4
		const int Chord[] = { Root, Root + 4, Root + 7 };
		for (int k = 0; k < 10; ++k) {
			const int Period = PulsePeriod(Chord[k % 3]);
			Frame &f = F[12 * i + k];
			f.push_back(std::make_pair(0x00, 0x80 | 0x30 | (k < 2 ? 15 : 12)));
			f.push_back(std::make_pair(0x02, Period & 0xFF));
			if (k == 0) {
				f.push_back(std::make_pair(0x01, 0x08));
				f.push_back(std::make_pair(0x03, ((Period >> 8) & 7) | 0x08));
			}
		}
		F[12 * i + 10].push_back(std::make_pair(0x00, 0x80 | 0x30));
	}
	Converted c;
	Convert(BuildNsf(F, 0, "Chord"), c);
	CHECK(c.Ok);
	if (!c.Ok)
		return;
	const std::vector<std::pair<int, Cell> > n = ColumnNotes(c.Song, 0);
	CHECK(n.size() == 16);
	CHECK(n.size() == 16 && n[0].second.Note == 48 && HasEffect(n[0].second, '0', 0x47));
	CHECK(n.size() == 16 && n[8].second.Note == 53);
	const Agreement a = PlaysLike(c);
	CHECK(a.Sound > 0.99 && a.Pitch > 0.99);
}

// The same envelope at two duties: one instrument, the duty set with Vxx
void TestDuties()
{
	std::vector<Tone> Tones;
	for (int i = 0; i < 16; ++i) {
		Tone t = { 12 * i, 10, 40 + i, Envelope({ 15, 12, 10, 9 }), (i / 2) % 2 ? 2 : 1, true };
		Tones.push_back(t);
	}
	Converted c;
	Convert(BuildNsf(PulseFrames(Tones, 192), 0, "Duties"), c);
	CHECK(c.Ok);
	if (!c.Ok)
		return;
	CHECK(c.Song.Instruments.size() == 1);
	CHECK(c.Song.Instruments.size() == 1 && c.Song.Instruments[0].Macros[SEQ_DUTY].Empty());
	const std::vector<std::pair<int, Cell> > n = ColumnNotes(c.Song, 0);
	CHECK(n.size() == 16);
	if (n.size() == 16) {
		CHECK(HasEffect(n[0].second, 'V', 1));		// the first duty, said where the loop starts
		CHECK(!HasEffect(n[1].second, 'V'));		// the same again: nothing
		CHECK(HasEffect(n[2].second, 'V', 2));
		CHECK(HasEffect(n[4].second, 'V', 1));
	}
	const Agreement a = PlaysLike(c);
	CHECK(a.Sound > 0.99 && a.Pitch > 0.99);
}

// Drums on the noise channel: a hit every 6 frames, the second half softer
void TestDrums()
{
	std::vector<Frame> F(192);
	for (int i = 0; i < 32; ++i) {
		const bool Snare = i % 4 == 2;
		const int Top = i < 16 ? 15 : 12;
		const int Envelope[] = { Top, Top * 2 / 3, Top / 3, Top / 6, 1 };
		for (int k = 0; k < 5; ++k) {
			Frame &f = F[6 * i + k];
			f.push_back(std::make_pair(0x0C, 0x30 | (Snare ? Envelope[k] : Envelope[k] * 2 / 3)));
			if (k == 0) {
				f.push_back(std::make_pair(0x0E, Snare ? 0x05 : 0x0C));
				f.push_back(std::make_pair(0x0F, 0x08));
			}
		}
		F[6 * i + 5].push_back(std::make_pair(0x0C, 0x30));
	}
	Converted c;
	Convert(BuildNsf(F, 0, "Drums"), c);
	CHECK(c.Ok);
	if (!c.Ok)
		return;
	const std::vector<std::pair<int, Cell> > n = ColumnNotes(c.Song, 3);
	CHECK(n.size() == 32);
	bool OnRows = n.size() == 32;
	for (size_t i = 0; i < n.size() && OnRows; ++i)
		OnRows = n[i].first == int(i);
	CHECK(OnRows);
	CHECK(n.size() == 32 && n[2].second.Note != n[0].second.Note);
	const Agreement a = PlaysLike(c);
	CHECK(a.Sound > 0.97);
}

// The whole import, in words
void TestImport()
{
	std::vector<Tone> Tones;
	for (int i = 0; i < 8; ++i) {
		Tone t = { 12 * i, 10, 48 + i, Envelope({ 15, 13, 12 }), 2, true };
		Tones.push_back(t);
	}
	NsfFile f;
	std::string Error;
	CHECK(ParseNsf(BuildNsf(PulseFrames(Tones, 96), 0, "Import"), f, Error));
	ImportOptions o;
	o.MaxSeconds = 30;
	int Calls = 0;
	o.Progress = [&](int, int, const std::string &) { ++Calls; return true; };
	ImportResult r;
	CHECK(ImportNsf(f, o, r, Error));
	CHECK(Calls == 1);
	CHECK(r.Song.Tracks.size() == 1);
	CHECK(r.Summary.find("Imported 1 track of \"Import\"") != std::string::npos);
	CHECK(r.Summary.find("150 BPM") != std::string::npos);
	CHECK(!r.Text.empty());
	CHECK(DescribeNsf(f).find("\"Import\" by Tester") != std::string::npos);

	// Stopping
	o.Progress = [](int, int, const std::string &) { return false; };
	CHECK(!ImportNsf(f, o, r, Error) && Error == "stopped");
	// Too many tracks
	o.Progress = nullptr;
	o.Tracks.assign(65, 0);
	CHECK(!ImportNsf(f, o, r, Error));
}

}	// namespace

int main()
{
	TestFiles();
	TestCpu();
	TestLoopsAndRows();
	TestMelody();
	TestLegato();
	TestChord();
	TestDuties();
	TestDrums();
	TestImport();
	std::printf("%d checks, %d failed\n", g_Checks, g_Failures);
	return g_Failures == 0 ? 0 : 1;
}

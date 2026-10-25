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
#include "../Study.h"
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

	// The rippers' placeholders for an unknown artist or copyright
	std::vector<uint8_t> Unknown = Bytes;
	std::memset(&Unknown[0x2E], 0, 32);
	std::memcpy(&Unknown[0x2E], "<?>", 3);
	std::memset(&Unknown[0x4E], 0, 32);
	std::memcpy(&Unknown[0x4E], "1990 Konami ", 12);
	CHECK(ParseNsf(Unknown, f, Error));
	CHECK(f.Artist.empty() && f.Copyright == "1990 Konami");

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


// ---------------------------------------------------------------------------
// The importer's fixes of October 2026: what the studies of real soundtracks
// got wrong, each rebuilt here
// ---------------------------------------------------------------------------

// Plays a frame list as an NSF (looping back to LoopFrame) for Seconds
bool Record(const std::vector<Frame> &Frames, int LoopFrame, double Seconds, Recording &Out, NsfFile &File,
	const std::vector<uint8_t> &Samples = std::vector<uint8_t>())
{
	std::string Error;
	if (!ParseNsf(BuildNsf(Frames, LoopFrame, "Fixes", Samples), File, Error))
		return false;
	RecordOptions Options;
	Options.MaxSeconds = Seconds;
	return RecordTrack(File, 0, Options, Out, Error);
}

// Pulse 1 playing a tune of 16 notes, a note every 12 frames, from frame
// Start (volume Volume), and the frames up to Total
void AddTune(std::vector<Frame> &F, int Start, int Volume, int Shift = 0)
{
	static const int NOTES[16] = { 48, 52, 55, 60, 59, 55, 52, 50, 48, 53, 57, 60, 59, 57, 55, 52 };
	for (int i = 0; i < 16; ++i) {
		const int Period = PulsePeriod(NOTES[i] + Shift);
		for (int k = 0; k < 10; ++k) {
			Frame &f = F[Start + 12 * i + k];
			f.push_back(std::make_pair(0x00, 0x80 | 0x30 | (k < 2 ? Volume : Volume * 3 / 4)));
			if (k == 0) {
				f.push_back(std::make_pair(0x01, 0x08));
				f.push_back(std::make_pair(0x02, Period & 0xFF));
				f.push_back(std::make_pair(0x03, ((Period >> 8) & 7) | 0x08));
			}
		}
		F[Start + 12 * i + 10].push_back(std::make_pair(0x00, 0x80 | 0x30));
	}
}

// A sampled bass: one wave (192 bits a period) on the DPCM played at seven
// rates as a line, with a drum hit among its notes. The module has the wave
// on the keys of the notes it sounds and the drum on the kit's C-3, and
// plays like the NSF; the study calls the line the bass.
void TestDpcmBass()
{
	std::vector<uint8_t> Samples;
	for (int k = 0; k < 10; ++k) {
		Samples.insert(Samples.end(), 12, 0xAB);	// rising 4 steps a byte
		Samples.insert(Samples.end(), 12, 0x54);	// falling
	}
	Samples.push_back(0x55);				// 241 bytes ($4013 = 15)
	const std::vector<uint8_t> Wave(Samples);
	while (Samples.size() % 64)
		Samples.push_back(0x55);
	const int DrumAt = int(Samples.size()) / 64;
	std::vector<uint8_t> Drum;
	for (int i = 0; i < 17; ++i)
		Drum.push_back(uint8_t((i * 73 + 41) & 0xFF));	// 17 bytes of noise ($4013 = 1)
	Samples.insert(Samples.end(), Drum.begin(), Drum.end());
	CHECK(std::fabs(SamplePeriod(Wave) - 192) < 1);
	CHECK(SamplePeriod(Drum) == 0);
	CHECK(std::fabs(SampleNote(192, 12, false) - 17.1) < 0.1);	// F-1 at rate 12

	// Twelve notes of the line at rates 9 to 15, every fourth place a drum
	const int RATES[12] = { 12, 9, 10, 13, 14, 15, 11, 12, 10, 9, 12, 14 };
	const int NOTES[16] = { 9, 10, 12, 14, 17, 21, 24, 29 };	// the notes the rates 8-15 sound, from 9
	std::vector<Frame> F(192);
	AddTune(F, 0, 10, 12);		// a tune over the line
	std::vector<int> Expected;
	for (int i = 0, Bass = 0; i < 16; ++i) {
		Frame &f = F[12 * i];
		const bool Hit = i % 4 == 3;
		const int Rate = Hit ? 15 : RATES[Bass++];
		f.push_back(std::make_pair(0x15, 0x0F));
		f.push_back(std::make_pair(0x10, Rate));
		f.push_back(std::make_pair(0x12, Hit ? DrumAt : 0));
		f.push_back(std::make_pair(0x13, Hit ? 1 : 15));
		f.push_back(std::make_pair(0x15, 0x1F));
		Expected.push_back(Hit ? 36 : NOTES[Rate - 8]);
	}
	Converted c;
	std::string Error;
	CHECK(ParseNsf(BuildNsf(F, 0, "Bass", Samples), c.File, Error));
	RecordOptions Options;
	Options.MaxSeconds = 60;
	CHECK(RecordTrack(c.File, 0, Options, c.Rec, Error));
	std::vector<TrackSource> Sources(1);
	Sources[0].Name = "Test";
	Sources[0].Rec = &c.Rec;
	c.Ok = ConvertTracks(c.File, Sources, ConvertOptions(), c.Song, c.Report, Error);
	CHECK(c.Ok);
	if (!c.Ok)
		return;
	// The keys: the wave at seven notes on the bass's instrument, the drum on the kit's
	const Instrument *Bass = nullptr, *Kit = nullptr;
	for (size_t i = 0; i < c.Song.Instruments.size(); ++i) {
		if (c.Song.Instruments[i].Name == "DPCM bass")
			Bass = &c.Song.Instruments[i];
		if (c.Song.Instruments[i].Name == "DPCM samples")
			Kit = &c.Song.Instruments[i];
	}
	CHECK(Bass != nullptr && Kit != nullptr);
	if (Bass == nullptr || Kit == nullptr)
		return;
	CHECK(Bass->Keys.size() == 7 && Kit->Keys.size() == 1 && Kit->Keys[0].Note == 36 && Kit->Keys[0].Pitch == 15);
	bool KeysRight = Bass->Keys.size() == 7;
	for (size_t k = 0; k < Bass->Keys.size(); ++k)
		KeysRight = KeysRight && Bass->Keys[k].Note == NOTES[Bass->Keys[k].Pitch - 8] && c.Song.Samples[Bass->Keys[k].Sample] == Wave;
	CHECK(KeysRight);
	// The notes on the DPCM's column: the line's pitches, the drum on C-3
	const std::vector<std::pair<int, Cell> > n = ColumnNotes(c.Song, 4);
	CHECK(n.size() == 16);
	bool NotesRight = n.size() == 16;
	for (size_t i = 0; i < n.size() && NotesRight; ++i)
		NotesRight = n[i].second.Note == Expected[i];
	CHECK(NotesRight);
	// It plays as the NSF did: each sample at its rate
	Recording Sim;
	CHECK(SimulateTrack(c.Song, 0, 192 + 32, Sim, Error));
	const Agreement a = Compare(c.Rec, Sim, 192, BestOffset(c.Rec, Sim, 192, 8), CH_DPCM);
	CHECK(a.Sound > 0.98 && a.Pitch > 0.99);
	// The study: the DPCM the bass, a line from A#0 to F-2, its samples
	const TrackStudy St = StudyTrack(c.Song, 0, c.Report.Tracks[0]);
	const std::string Text = DescribeStudy(St);
	CHECK(Text.find("DPCM, bass: 12 notes from A#0 to F-2") != std::string::npos);
	CHECK(Text.find("a sampled bass: 1 sample (241 bytes) played at DPCM rates 9 to 15, each at the pitches it sounds (7 different pitches)") !=
		std::string::npos);
	CHECK(Text.find("DPCM drums: ") != std::string::npos);
	CHECK(Text.find("a sampled bass on the DPCM") != std::string::npos);
}

// A track that plays two seconds and then stays silent, though its playlist
// gives it 40: the survey and the study go by what it plays
void TestShortOfPlaylist()
{
	std::vector<Frame> F(500);
	AddTune(F, 0, 12);		// 16 notes over 192 frames with a bass on the triangle, then silence
	for (int i = 0; i < 4; ++i) {
		const double Hz = 440.0 * std::pow(2.0, (24 + 5 * (i % 2) - 45) / 12.0);
		const int Period = int(1789773.0 / (32.0 * Hz) - 1.0 + 0.5);
		F[48 * i].push_back(std::make_pair(0x08, 0xFF));
		F[48 * i].push_back(std::make_pair(0x0A, Period & 0xFF));
		F[48 * i].push_back(std::make_pair(0x0B, ((Period >> 8) & 7) | 0x08));
		F[48 * i + 40].push_back(std::make_pair(0x08, 0x80));
	}
	for (size_t i = 192; i < F.size(); ++i)
		F[i].push_back(std::make_pair(0x00, 0x30));
	// And a tick of noise: a sound
	std::vector<Frame> G(400);
	G[0].push_back(std::make_pair(0x0C, 0x3F));
	G[0].push_back(std::make_pair(0x0E, 0x05));
	G[0].push_back(std::make_pair(0x0F, 0x08));
	G[3].push_back(std::make_pair(0x0C, 0x30));
	for (int Which = 0; Which < 2; ++Which) {
		NsfFile File;
		std::string Error;
		CHECK(ParseNsf(BuildNsf(Which ? G : F, Which ? 399 : 499, "Short"), File, Error));
		CHECK(ApplyPlaylist("short.nsf::NSF,1,Short,0:00:40,,\n", File) == 1);
		CHECK(File.TrackTime(0) == 40000);
		const std::vector<TrackSurvey> Survey = SurveyTracks(File);
		CHECK(Survey.size() == 1);
		if (Survey.size() != 1)
			continue;
		CHECK(Survey[0].Kind == (Which ? "sound effect" : "jingle"));
		CHECK(Which ? Survey[0].Seconds < 0.5 : std::fabs(Survey[0].Seconds - 3.2) < 0.2);
		std::vector<StudiedTrack> Studied;
		StudyOptions Options;
		Options.MaxSeconds = 60;
		CHECK(StudyTracks(File, std::vector<int>(1, 0), Options, Studied, Error) && Studied.size() == 1);
		if (Studied.size() != 1 || !Studied[0].Problem.empty())
			continue;
		const TrackStudy &S = Studied[0].Study;
		CHECK(S.Kind == (Which ? "sound" : "sting") && S.LoopBars == 0 && S.PlaylistSeconds == 40);
		const std::string Text = DescribeStudy(S);
		CHECK(Text.find(Which ? "a sound effect rather than music: it plays " : "a sting, not music to loop: it plays 3.2 seconds and ends") !=
			std::string::npos);
		CHECK(Text.find("though its playlist gives it 40 seconds") != std::string::npos);
	}
	// A track the playlist says is music, and that plays on, stays music
	std::vector<Frame> H(192);
	AddTune(H, 0, 12);
	NsfFile File;
	std::string Error;
	CHECK(ParseNsf(BuildNsf(H, 0, "Tune"), File, Error));
	ApplyPlaylist("tune.nsf::NSF,1,Tune,0:00:40,,\n", File);
	const std::vector<TrackSurvey> Survey = SurveyTracks(File);
	CHECK(Survey.size() == 1 && Survey[0].Kind == "music" && Survey[0].Seconds == 40);
}

// A loop whose first time through differs from the rest only in a quiet
// pulse (tails at volume 2, another duty at 6): it loops from the start.
// The same notes louder are an intro.
void TestQuietFirstPass()
{
	for (int Loud = 0; Loud < 2; ++Loud) {
		std::vector<Frame> F(384);
		AddTune(F, 0, 12);
		AddTune(F, 192, 12);
		// The first time through, Pulse 2 plays an octave down at volume 2 (or 10), and at another duty
		for (int i = 0; i < 16; ++i) {
			Frame &f = F[12 * i + 2];
			const int Period = PulsePeriod(36 + i);
			f.push_back(std::make_pair(0x04, (i % 2 ? 0x40 : 0x00) | 0x30 | (Loud ? 10 : 2)));
			f.push_back(std::make_pair(0x05, 0x08));
			f.push_back(std::make_pair(0x06, Period & 0xFF));
			f.push_back(std::make_pair(0x07, ((Period >> 8) & 7) | 0x08));
			F[12 * i + 6].push_back(std::make_pair(0x04, 0x30));
		}
		Recording R;
		NsfFile File;
		CHECK(Record(F, 192, 90, R, File));
		CHECK(R.LoopLength == 192);
		CHECK(Loud ? R.LoopStart >= 180 && R.LoopStart <= 192 : R.LoopStart <= 2);
	}
	// Directly: tails at volume 2 the first time through are barely heard
	std::vector<Frame> F(384);
	AddTune(F, 0, 12);
	AddTune(F, 192, 12);
	Recording R;
	NsfFile File;
	CHECK(Record(F, 192, 60, R, File));
	CHECK(AudibleLoopStart(R, 100, 192) <= 1);
}

// A piece that plays once: an opening, a passage played four times, an
// ending, then silence. The passage repeats long enough for the old finder
// to take it for the loop; it isn't one, since the piece moves on.
void TestPassageRepeatedOnce()
{
	std::vector<Frame> F(360 + 4 * 192 + 360 + 400);
	AddTune(F, 0, 12, 3);			// the opening, higher
	AddTune(F, 192, 12, 5);
	for (int k = 0; k < 4; ++k)		// the passage, four times
		AddTune(F, 360 + 192 * k, 12);
	AddTune(F, 360 + 4 * 192, 12, -5);	// the ending, lower, and silence
	Recording R;
	NsfFile File;
	CHECK(Record(F, int(F.size()) - 1, 120, R, File));
	CHECK(R.LoopStart < 0 && R.EndsInSilence);
	// The fingerprints of such a piece: the old rule took the passage for a
	// loop while it repeated; confirmed, it isn't until it has gone on
	std::vector<uint64_t> v;
	for (int i = 0; i < 30; ++i)
		v.push_back(1000 + i);
	for (int k = 0; k < 4; ++k)
		for (int i = 0; i < 20; ++i)
			v.push_back(i * 7 % 13);
	int Start = -1, Period = -1;
	CHECK(FindLoop(v, 8, 4, Start, Period) && Start == 30 && Period == 20);
	CHECK(!FindLoop(v, 8, 4, Start, Period, 200));
	CHECK(FindLoop(v, 8, 4, Start, Period, 40) && Start == 30 && Period == 20);
}

// A tune that loops whole, with a passage played four times inside it (Dr.
// Mario's Chill): the old finder took the passage for the loop as soon as
// it had repeated; the loop is the whole tune, from its start.
void TestLoopAroundARepeat()
{
	std::vector<Frame> F(1152);
	AddTune(F, 0, 12, 3);			// the opening, higher
	for (int k = 0; k < 4; ++k)		// the passage, four times
		AddTune(F, 192 + 192 * k, 12);
	AddTune(F, 960, 12, -5);		// the ending, lower, then back to the start
	Recording R;
	NsfFile File;
	CHECK(Record(F, 0, 120, R, File));
	CHECK(R.LoopLength == 1152 && R.LoopStart >= 0 && R.LoopStart <= 2);
}

// A loop the old finder missed: the tune repeats every 144 frames, the
// noise's hi-hats every 150, so the whole only repeats after a minute, too
// long to hear twice. The tune's loop is the loop, the drums on a cycle of
// their own.
void TestDrumsOnTheirOwn()
{
	static const int NOTES[16] = { 48, 52, 55, 60, 59, 55, 52, 50, 48, 53, 57, 60, 59, 57, 55, 52 };
	std::vector<Frame> F(3600);
	for (int t = 0; t < int(F.size()); t += 9) {
		const int Period = PulsePeriod(NOTES[(t / 9) % 16]);
		F[t].push_back(std::make_pair(0x00, 0x80 | 0x30 | 12));
		F[t].push_back(std::make_pair(0x01, 0x08));
		F[t].push_back(std::make_pair(0x02, Period & 0xFF));
		F[t].push_back(std::make_pair(0x03, ((Period >> 8) & 7) | 0x08));
		F[t + 8].push_back(std::make_pair(0x00, 0x80 | 0x30));
	}
	static const bool REST[25] = { false, true, true, false, false, false, false, true, false, false, false, false, false, false, false,
		true, true, false, false, false, false, false, true, false, false };	// 25 places, six of them left out
	for (int t = 0; t < int(F.size()); ++t) {
		const int Step = t % 150;
		if (REST[Step / 6])
			continue;
		static const int DECAY[5] = { 10, 7, 4, 2, 0 };
		if (Step % 6 < 5)
			F[t].push_back(std::make_pair(0x0C, 0x30 | DECAY[Step % 6]));
		if (Step % 6 == 0) {
			F[t].push_back(std::make_pair(0x0E, 0x02));
			F[t].push_back(std::make_pair(0x0F, 0x08));
		}
	}
	Recording R;
	NsfFile File;
	CHECK(Record(F, 0, 60, R, File));
	CHECK(R.LoopStart == 0 && R.LoopLength == 144);
	CHECK(R.DrumsApart && R.DrumLoop == 150);
	CHECK(!R.Problems.empty() && R.Problems[0].find("on a cycle of their own") != std::string::npos);
}

// A module built here: a track of Bars bars of 16 rows, Pulse 1's tune (a
// note a beat), Pulse 2's chord (its notes in turn, a beat each) and the
// triangle's bass (a note a beat, Short: cut after two rows), looping to
// its start (Once: it stops at its end)
Module KeyTrack(const std::vector<std::vector<int> > &Tune, const std::vector<std::vector<int> > &Chords, const std::vector<int> &Bass,
	bool Short, bool Once = false)
{
	Module M;
	Instrument I;
	I.Macros[SEQ_VOLUME] = Macro(std::vector<int>(1, 15));
	M.Instruments.push_back(I);
	Track T;
	T.Name = "Key";
	T.PatternLength = 16;
	T.Speed = 6;
	T.Tempo = 150;
	T.Columns.assign(5, 1);
	T.Patterns.assign(5, std::vector<std::vector<Cell> >());
	for (size_t b = 0; b < Bass.size(); ++b) {
		for (int c = 0; c < 5; ++c)
			T.Patterns[c].push_back(std::vector<Cell>(16));
		for (int k = 0; k < 4; ++k) {
			Cell &m = T.Patterns[0][b][k * 4], &h = T.Patterns[1][b][k * 4], &t = T.Patterns[2][b][k * 4];
			m.Note = Tune[b][k % Tune[b].size()];
			h.Note = Chords[b][k % Chords[b].size()];
			t.Note = Bass[b];
			m.Instrument = h.Instrument = t.Instrument = 0;
			if (Short)
				T.Patterns[2][b][k * 4 + 2].Note = NOTE_HALT;
		}
		T.Frames.push_back(std::vector<int>(5, int(b)));
	}
	T.Patterns[4][Bass.size() - 1][15].AddEffect(Once ? 'C' : 'B', 0);
	M.Tracks.push_back(T);
	return M;
}

// Keys from the chords, the bass and where the phrases come to rest: a minor
// piece the pitch profile alone calls major (its relative major; its
// dominant's major key, a toll on G over C minor), and chords that drift
// with no key
void TestKeys()
{
	TrackReport R;
	R.RowsPerBeat = 4;
	R.TicksPerRow = 6;
	R.Bpm = 150;
	// D minor, then A, F and Bb minor, the dominant at the end (the profile: F major)
	const std::vector<std::vector<int> > RisingTune = { { 50, 53, 57, 60 }, { 57, 53, 60, 57 }, { 60, 52, 60, 57 }, { 60, 57, 52, 60 },
		{ 60, 53, 60, 53 }, { 53, 58, 53, 60 }, { 60, 53, 60, 53 }, { 52, 57, 61, 64 } };
	const std::vector<std::vector<int> > RisingChords = { { 38, 41, 45 }, { 38, 41, 45 }, { 45, 48, 52 }, { 45, 48, 52 },
		{ 41, 44, 48 }, { 46, 49, 53 }, { 41, 44, 48 }, { 45, 49, 52 } };
	const std::vector<int> RisingBass = { 26, 26, 21, 21, 29, 22, 29, 21 };
	TrackStudy S = StudyTrack(KeyTrack(RisingTune, RisingChords, RisingBass, false), 0, R);
	CHECK(S.Key == "D minor");
	CHECK(DescribeStudy(S).find("Key: D minor.") != std::string::npos);
	// C minor three bars, G the fourth, a toll on G every beat (the profile: G major)
	const std::vector<std::vector<int> > TollTune(4, std::vector<int>(1, 55));
	const std::vector<std::vector<int> > TollChords = { { 48, 51, 55, 51 }, { 48, 51, 55, 51 }, { 48, 51, 55, 51 }, { 47, 50, 55, 50 } };
	const std::vector<int> TollBass = { 36, 36, 36, 31 };
	S = StudyTrack(KeyTrack(TollTune, TollChords, TollBass, true), 0, R);
	CHECK(S.Key == "C minor");
	// Chords drifting by semitones and tritones, A at either end: no clear key
	const std::vector<std::vector<int> > DriftTune = { { 57, 60, 64 }, { 59, 63, 66 }, { 55, 58, 62 }, { 57, 61, 64 },
		{ 53, 56, 60 }, { 52, 56, 59 }, { 57, 61, 64 }, { 57, 60, 64 } };
	const std::vector<std::vector<int> > DriftChords = { { 45, 48, 52 }, { 47, 51, 54 }, { 43, 46, 50 }, { 45, 49, 52 },
		{ 41, 44, 48 }, { 40, 44, 47 }, { 45, 49, 52 }, { 45, 48, 52 } };
	const std::vector<int> DriftBass = { 21, 23, 19, 21, 17, 16, 21, 21 };
	S = StudyTrack(KeyTrack(DriftTune, DriftChords, DriftBass, false), 0, R);
	CHECK(S.Key == "no clear key (around A)");
	CHECK(DescribeStudy(S).find("No clear key (around A).") != std::string::npos);
	// A plain major tune stays major
	const std::vector<std::vector<int> > MajorTune = { { 60, 64, 67, 64 }, { 65, 69, 72, 69 }, { 67, 71, 74, 71 }, { 72, 67, 64, 60 } };
	const std::vector<std::vector<int> > MajorChords = { { 48, 52, 55 }, { 53, 57, 60 }, { 55, 59, 62 }, { 48, 52, 55 } };
	const std::vector<int> MajorBass = { 36, 41, 43, 36 };
	S = StudyTrack(KeyTrack(MajorTune, MajorChords, MajorBass, false), 0, R);
	CHECK(S.Key == "C major");
	// Played once, it ends at home: still C major
	S = StudyTrack(KeyTrack(MajorTune, MajorChords, MajorBass, false, true), 0, R);
	CHECK(S.LoopBars == 0 && S.Key == "C major");
	// A fanfare on G that stops on the chord a tritone away (C#) has no key,
	// though its notes would make one of G: at most a home
	const std::vector<std::vector<int> > FanfareTune = { { 67, 71, 74, 79 }, { 79, 74, 71, 74 }, { 73, 77, 80, 85 } };
	const std::vector<std::vector<int> > FanfareChords = { { 55, 59, 62 }, { 55, 59, 62 }, { 61, 65, 68 } };
	const std::vector<int> FanfareBass = { 43, 43, 37 };
	S = StudyTrack(KeyTrack(FanfareTune, FanfareChords, FanfareBass, false, true), 0, R);
	CHECK(S.LoopBars == 0 && S.Key == "no clear key (around G)");
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
	TestDpcmBass();
	TestShortOfPlaylist();
	TestQuietFirstPass();
	TestPassageRepeatedOnce();
	TestLoopAroundARepeat();
	TestDrumsOnTheirOwn();
	TestKeys();
	std::printf("%d checks, %d failed\n", g_Checks, g_Failures);
	return g_Failures == 0 ? 0 : 1;
}

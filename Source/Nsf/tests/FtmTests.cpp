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

// Tests for reading FamiTracker modules (FtmFile.h): modules built here
// block by block, as FamiTracker 0.4.6 writes them (every block, each
// expansion chip, instruments in slots with gaps, DPCM samples on keys,
// comments, several tracks) and as the releases before it did (0.2.2's
// first blocks and sequences, 0.2.4's HEADER, 0.2.6's tracks, 0.3.5's
// release points, the VRC7's and the FDS's old effects and octaves), read
// into a Module and written as FamiTracker's text; then files cut short at
// every length and bytes changed at random, read without harm, and studied
// when they read.

#include "../FtmFile.h"
#include "../Module.h"
#include "../Study.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <string>
#include <utility>
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

// --- Writing modules ---

// A module written block by block, as CDocumentFile writes one: ints
// little-endian, names in 16 NUL-padded bytes, each block's size after its
// version, "END" at the end
struct Writer {
	std::vector<uint8_t> Bytes;
	size_t SizeAt;

	explicit Writer(unsigned Version = 0x0440) : SizeAt(0)
	{
		Text("FamiTracker Module");
		Int(long(Version));
	}
	Writer &U8(int v)
	{
		Bytes.push_back(uint8_t(v & 0xFF));
		return *this;
	}
	Writer &Int(long v)
	{
		const unsigned long u = static_cast<unsigned long>(v);
		for (int i = 0; i < 4; ++i)
			U8(int((u >> (8 * i)) & 0xFF));
		return *this;
	}
	Writer &Text(const std::string &s)
	{
		for (size_t i = 0; i < s.size(); ++i)
			U8(s[i]);
		return *this;
	}
	Writer &String(const std::string &s) { return Text(s).U8(0); }		// WriteString: up to a NUL
	Writer &Name(const std::string &s) { return Int(long(s.size())).Text(s); }	// a length, then the characters
	Writer &Fixed(const std::string &s, size_t Size)
	{
		for (size_t i = 0; i < Size; ++i)
			U8(i < s.size() ? s[i] : 0);
		return *this;
	}
	Writer &Begin(const char *Id, int Version)
	{
		Fixed(Id, 16).Int(Version);
		SizeAt = Bytes.size();
		return Int(0);
	}
	Writer &End()
	{
		const unsigned long Size = Bytes.size() - SizeAt - 4;
		for (int i = 0; i < 4; ++i)
			Bytes[SizeAt + i] = uint8_t((Size >> (8 * i)) & 0xFF);
		return *this;
	}
	std::vector<uint8_t> Finish() { return Text("END").Bytes; }
};

// FamiTracker's numbers: instrument types, effects, sequence kinds
enum { T_2A03 = 1, T_VRC6, T_VRC7, T_FDS, T_N163, T_S5B };
enum { E_SPEED = 1, E_JUMP, E_SKIP, E_HALT, E_VOLUME, E_PORTAMENTO, E_PORTAOFF, E_SWEEPUP, E_SWEEPDOWN, E_ARPEGGIO, E_VIBRATO,
	E_TREMOLO, E_PITCH, E_DELAY, E_DAC, E_PORTA_UP, E_PORTA_DOWN, E_DUTY, E_FDS_MOD_DEPTH = 26 };
const int NO_INST = 0x40, NO_VOL = 0x10, RELEASE = 13, HALT = 14;

// PARAMS 6, as 0.4.0 on write it
void Params(Writer &w, int Chip, int Channels, int Machine = 0, int EngineSpeed = 0, int Vibrato = 1, int Namco = 1, int Split = 32,
	int First = 4, int Second = 16)
{
	w.Begin("PARAMS", 6).U8(Chip).Int(Channels).Int(Machine).Int(EngineSpeed).Int(Vibrato).Int(First).Int(Second);
	if (Chip & EXP_N163)
		w.Int(Namco);
	w.Int(Split).End();
}

void Info(Writer &w, const std::string &Title, const std::string &Author, const std::string &Copyright)
{
	w.Begin("INFO", 1).Fixed(Title, 32).Fixed(Author, 32).Fixed(Copyright, 32).End();
}

// HEADER 3: the tracks' names, then each channel's type and its effect
// columns (less one) in each track, Columns[track][channel]
void Header(Writer &w, const std::vector<std::string> &Names, const std::vector<std::vector<int> > &Columns)
{
	w.Begin("HEADER", 3).U8(int(Names.size()) - 1);
	for (size_t t = 0; t < Names.size(); ++t)
		w.String(Names[t]);
	for (size_t c = 0; c < Columns[0].size(); ++c) {
		w.U8(int(c));
		for (size_t t = 0; t < Names.size(); ++t)
			w.U8(Columns[t][c]);
	}
	w.End();
}

// The sequences of a 2A03, VRC6 or N163 instrument: an index for each kind, -1 for none
void InstSequences(Writer &w, std::initializer_list<int> Indices)
{
	w.Int(5);
	for (std::initializer_list<int>::const_iterator i = Indices.begin(); i != Indices.end(); ++i)
		w.U8(*i >= 0 ? 1 : 0).U8(*i >= 0 ? *i : 0);
}

// A DPCM key of a 2A03 instrument: Note 0-95, Sample 1-64 (0 none), Pitch 0-15 and 0x80 to loop, Delta -1 for none
struct Key {
	int Note, Sample, Pitch, Delta;
};

void InstKeys(Writer &w, const std::vector<Key> &Keys, int Octaves = 8, bool Deltas = true)
{
	for (int Note = 0; Note < Octaves * 12; ++Note) {
		const Key *k = 0;
		for (size_t i = 0; i < Keys.size(); ++i)
			if (Keys[i].Note == Note)
				k = &Keys[i];
		w.U8(k ? k->Sample : 0).U8(k ? k->Pitch : 0);
		if (Deltas)
			w.U8(k ? k->Delta : -1);
	}
}

struct Seq {
	int Index, Type;
	std::vector<int> Items;
	int Loop, Release, Setting;
};

// SEQUENCES 6 or SEQUENCES_VRC6 6: the sequences, then their release points and settings
void Sequences6(Writer &w, const char *Id, const std::vector<Seq> &List)
{
	w.Begin(Id, 6).Int(long(List.size()));
	for (size_t i = 0; i < List.size(); ++i) {
		w.Int(List[i].Index).Int(List[i].Type).U8(int(List[i].Items.size())).Int(List[i].Loop);
		for (size_t k = 0; k < List[i].Items.size(); ++k)
			w.U8(List[i].Items[k]);
	}
	for (size_t i = 0; i < List.size(); ++i)
		w.Int(List[i].Release).Int(List[i].Setting);
	w.End();
}

void SequencesN163(Writer &w, const std::vector<Seq> &List, const char *Id = "SEQUENCES_N163")
{
	w.Begin(Id, 1).Int(long(List.size()));
	for (size_t i = 0; i < List.size(); ++i) {
		const Seq &s = List[i];
		w.Int(s.Index).Int(s.Type).U8(int(s.Items.size())).Int(s.Loop).Int(s.Release).Int(s.Setting);
		for (size_t k = 0; k < s.Items.size(); ++k)
			w.U8(s.Items[k]);
	}
	w.End();
}

struct TrackFrames {
	int Speed, Tempo, Rows;
	std::vector<std::vector<int> > Frames;	// a pattern for each channel the module has data for
};

void Frames3(Writer &w, const std::vector<TrackFrames> &Tracks)
{
	w.Begin("FRAMES", 3);
	for (size_t t = 0; t < Tracks.size(); ++t) {
		w.Int(long(Tracks[t].Frames.size())).Int(Tracks[t].Speed).Int(Tracks[t].Tempo).Int(Tracks[t].Rows);
		for (size_t f = 0; f < Tracks[t].Frames.size(); ++f)
			for (size_t c = 0; c < Tracks[t].Frames[f].size(); ++c)
				w.U8(Tracks[t].Frames[f][c]);
	}
	w.End();
}

struct RowData {
	int Row, Note, Octave, Inst, Vol;
	std::vector<std::pair<int, int> > Effects;	// (effect, parameter) by column
};

// A pattern in PATTERNS: the track (when the version has it), the channel
// and the pattern, then its rows with Columns effect columns; RowBytes 1
// for 2.00's rows of a byte
void PatternRows(Writer &w, int Track, int Channel, int Pattern, const std::vector<RowData> &Rows, int Columns, int RowBytes = 4)
{
	if (Track >= 0)
		w.Int(Track);
	w.Int(Channel).Int(Pattern).Int(long(Rows.size()));
	for (size_t i = 0; i < Rows.size(); ++i) {
		const RowData &r = Rows[i];
		if (RowBytes == 1)
			w.U8(r.Row);
		else
			w.Int(r.Row);
		w.U8(r.Note).U8(r.Octave).U8(r.Inst).U8(r.Vol);
		for (int e = 0; e < Columns; ++e) {
			if (e < int(r.Effects.size()))
				w.U8(r.Effects[e].first).U8(r.Effects[e].second);
			else
				w.U8(0).U8(0);
		}
	}
}

struct SampleDef {
	int Index;
	std::string Name;
	std::vector<uint8_t> Data;
};

void Samples(Writer &w, const std::vector<SampleDef> &List)
{
	w.Begin("DPCM SAMPLES", 1).U8(int(List.size()));
	for (size_t i = 0; i < List.size(); ++i) {
		w.U8(List[i].Index).Name(List[i].Name).Int(long(List[i].Data.size()));
		for (size_t k = 0; k < List[i].Data.size(); ++k)
			w.U8(List[i].Data[k]);
	}
	w.End();
}

// --- Checking ---

bool Items(const Macro &m, std::initializer_list<int> Values)
{
	return m.Items == std::vector<int>(Values);
}

bool Has(const std::string &Text, const std::string &Line)
{
	return Text.find(Line) != std::string::npos;
}

bool ReadModule(const std::vector<uint8_t> &Bytes, Module &M, FtmInfo &Info)
{
	std::string Error;
	if (!ParseFtm(Bytes, M, Info, Error)) {
		std::printf("  read: %s\n", Error.c_str());
		return false;
	}
	return true;
}

std::string ReadError(const std::vector<uint8_t> &Bytes)
{
	Module M;
	FtmInfo Info;
	std::string Error;
	return ParseFtm(Bytes, M, Info, Error) ? std::string() : Error;
}

// --- FamiTracker 0.4.6: the 2A03 alone ---

std::vector<uint8_t> MakeModern()
{
	Writer w;
	Params(w, 0, 5);
	Info(w, "Test module", "Tester", "2026");
	Header(w, { "Main", "Second" }, { { 1, 0, 0, 0, 0 }, { 0, 0, 0, 0, 3 } });

	// Instruments in slots 0, 2 and 5
	w.Begin("INSTRUMENTS", 6).Int(3);
	w.Int(0).U8(T_2A03);
	w.Int(5);
	w.U8(1).U8(0);		// volume 0
	w.U8(1).U8(1);		// arpeggio 1
	w.U8(0).U8(3);		// pitch: off (sequence 3 kept)
	w.U8(0).U8(0);
	w.U8(1).U8(0);		// duty 0
	InstKeys(w, std::vector<Key>());
	w.Name("Lead");
	w.Int(2).U8(T_2A03);
	InstSequences(w, { 2, -1, -1, -1, -1 });		// a volume sequence the module doesn't have
	InstKeys(w, { { 36, 1, 0x8F, 64 }, { 38, 4, 0x0E, -1 } });
	w.Name("Drums");
	w.Int(5).U8(T_2A03);
	InstSequences(w, { 3, -1, -1, -1, 1 });		// volume 3: the same as 0
	InstKeys(w, std::vector<Key>());
	w.Name("Copy");
	w.End();

	Sequences6(w, "SEQUENCES", {
		{ 0, SEQ_VOLUME, { 15, 12, 8, 4, 0 }, -1, 2, 0 },
		{ 0, SEQ_DUTY, { 2, 1 }, 1, -1, 0 },
		{ 1, SEQ_ARPEGGIO, { 0, 4, 7 }, 0, -1, 0 },
		{ 1, SEQ_DUTY, { 0 }, -1, -1, 0 },
		{ 3, SEQ_VOLUME, { 15, 12, 8, 4, 0 }, -1, 2, 0 },
	});

	Frames3(w, { { 6, 150, 16, { { 0, 0, 0, 0, 0 }, { 1, 0, 1, 0, 0 } } }, { 3, 140, 8, { { 0, 0, 0, 0, 2 } } } });

	w.Begin("PATTERNS", 5);
	PatternRows(w, 0, 0, 0, {
		{ 0, 2, 4, 0, NO_VOL, { { E_VIBRATO, 0x46 }, { E_SPEED, 5 } } },
		{ 4, RELEASE, 0, NO_INST, NO_VOL, {} },
		{ 8, HALT, 0, NO_INST, NO_VOL, {} },
		{ 12, 12, 7, NO_INST, 3, {} },
	}, 2);
	PatternRows(w, 0, 0, 1, { { 0, 10, 3, 5, NO_VOL, { { 0, 0 }, { E_JUMP, 0 } } } }, 2);
	PatternRows(w, 0, 1, 7, { { 3, 5, 2, 0, 8, {} } }, 1);		// in no frame
	PatternRows(w, 0, 2, 1, { { 15, 8, 1, NO_INST, NO_VOL, { { E_SKIP, 0 } } } }, 1);
	PatternRows(w, 0, 3, 0, { { 0, 5, 1, NO_INST, NO_VOL, {} }, { 2, 12, 0, 0, 0xF, {} } }, 1);
	PatternRows(w, 0, 4, 0, { { 0, 1, 3, 2, NO_VOL, {} } }, 1);
	PatternRows(w, 1, 0, 0, { { 0, 1, 4, 0, NO_VOL, {} } }, 1);
	PatternRows(w, 1, 4, 2, { { 7, 3, 3, 2, NO_VOL, { { E_DAC, 0x40 }, { 0, 0 }, { 0, 0 }, { E_HALT, 0 } } } }, 4);
	w.End();

	std::vector<uint8_t> Snare;
	for (int i = 0; i < 33; ++i)
		Snare.push_back(uint8_t(i));
	Samples(w, { { 0, "kick", { 0x55, 0xAA, 0x01 } }, { 3, "snare", Snare } });

	w.Begin("COMMENTS", 1).Int(1).String("First line\r\nSecond \"quoted\" line").End();
	return w.Finish();
}

void TestModern()
{
	Module M;
	FtmInfo Info;
	CHECK(IsFtm(MakeModern()));
	if (!ReadModule(MakeModern(), M, Info)) {
		CHECK(false);
		return;
	}
	CHECK(Info.Version == 0x0440 && Info.Problems.empty());
	CHECK(Info.FirstHighlight == 4 && Info.SecondHighlight == 16 && Info.SplitPoint == 32 && Info.VibratoStyle == 1 && Info.EngineSpeed == 0);
	CHECK(M.Title == "Test module" && M.Author == "Tester" && M.Copyright == "2026");
	CHECK(M.Comment.size() == 2 && M.Comment[0] == "First line" && M.Comment[1] == "Second \"quoted\" line");
	CHECK(M.Machine == 0 && M.FrameRate == 0 && M.Expansion == EXP_NONE);

	// Instruments keep their slots; the empty ones are marked
	CHECK(M.Instruments.size() == 6);
	if (M.Instruments.size() == 6) {
		CHECK(!M.Instruments[0].Unused && M.Instruments[1].Unused && !M.Instruments[2].Unused && M.Instruments[3].Unused &&
			M.Instruments[4].Unused && !M.Instruments[5].Unused);
		const Instrument &Lead = M.Instruments[0], &Drums = M.Instruments[2], &Copy = M.Instruments[5];
		CHECK(Lead.Name == "Lead" && Lead.Kind == INST_2A03);
		CHECK(Lead.MacroIndex[SEQ_VOLUME] == 0 && Lead.MacroIndex[SEQ_ARPEGGIO] == 1 && Lead.MacroIndex[SEQ_PITCH] == -1 &&
			Lead.MacroIndex[SEQ_HIPITCH] == -1 && Lead.MacroIndex[SEQ_DUTY] == 0);
		CHECK(Items(Lead.Macros[SEQ_VOLUME], { 15, 12, 8, 4, 0 }) && Lead.Macros[SEQ_VOLUME].Release == 2 && Lead.Macros[SEQ_VOLUME].Loop == -1);
		CHECK(Items(Lead.Macros[SEQ_ARPEGGIO], { 0, 4, 7 }) && Lead.Macros[SEQ_ARPEGGIO].Loop == 0);
		CHECK(Lead.Macros[SEQ_PITCH].Empty());
		CHECK(Items(Lead.Macros[SEQ_DUTY], { 2, 1 }) && Lead.Macros[SEQ_DUTY].Loop == 1);
		// A sequence it plays that isn't there: the number, no macro
		CHECK(Drums.MacroIndex[SEQ_VOLUME] == 2 && Drums.Macros[SEQ_VOLUME].Empty());
		CHECK(Drums.Keys.size() == 2);
		if (Drums.Keys.size() == 2) {
			CHECK(Drums.Keys[0].Note == 36 && Drums.Keys[0].Sample == 0 && Drums.Keys[0].Pitch == 15 && Drums.Keys[0].Loop &&
				Drums.Keys[0].Delta == 64);
			CHECK(Drums.Keys[1].Note == 38 && Drums.Keys[1].Sample == 3 && Drums.Keys[1].Pitch == 14 && !Drums.Keys[1].Loop &&
				Drums.Keys[1].Delta == -1);
		}
		// Two sequences alike keep their own numbers
		CHECK(Copy.MacroIndex[SEQ_VOLUME] == 3 && Copy.MacroIndex[SEQ_DUTY] == 1 && Items(Copy.Macros[SEQ_DUTY], { 0 }));
	}

	// Macros by their numbers, gaps empty
	CHECK(M.Macros[MACROS_2A03][SEQ_VOLUME].size() == 4);
	if (M.Macros[MACROS_2A03][SEQ_VOLUME].size() == 4)
		CHECK(!M.Macros[MACROS_2A03][SEQ_VOLUME][0].Empty() && M.Macros[MACROS_2A03][SEQ_VOLUME][1].Empty() &&
			M.Macros[MACROS_2A03][SEQ_VOLUME][2].Empty() && M.Macros[MACROS_2A03][SEQ_VOLUME][3] == M.Macros[MACROS_2A03][SEQ_VOLUME][0]);
	CHECK(M.Macros[MACROS_2A03][SEQ_ARPEGGIO].size() == 2 && M.Macros[MACROS_2A03][SEQ_PITCH].empty());
	CHECK(M.Macros[MACROS_VRC6][SEQ_VOLUME].empty() && M.Macros[MACROS_N163][SEQ_VOLUME].empty());

	// Samples by their numbers
	CHECK(M.Samples.size() == 4 && M.SampleNames.size() == 4);
	if (M.Samples.size() == 4) {
		CHECK(M.Samples[0] == std::vector<uint8_t>({ 0x55, 0xAA, 0x01 }) && M.SampleNames[0] == "kick");
		CHECK(M.Samples[1].empty() && M.Samples[2].empty() && M.Samples[3].size() == 33 && M.SampleNames[3] == "snare");
	}

	// Tracks
	CHECK(M.Tracks.size() == 2);
	if (M.Tracks.size() != 2)
		return;
	const Track &T = M.Tracks[0], &U = M.Tracks[1];
	CHECK(T.Name == "Main" && T.PatternLength == 16 && T.Speed == 6 && T.Tempo == 150);
	CHECK(T.Columns == std::vector<int>({ 2, 1, 1, 1, 1 }));
	CHECK(T.Frames.size() == 2 && T.Frames[1] == std::vector<int>({ 1, 0, 1, 0, 0 }));
	CHECK(T.Patterns.size() == 5);
	if (T.Patterns.size() == 5) {
		CHECK(T.Patterns[0].size() == 2 && T.Patterns[1].size() == 8 && T.Patterns[2].size() == 2);
		const std::vector<Cell> &P = T.Patterns[0][0];
		CHECK(P.size() == 16);
		if (P.size() == 16) {
			CHECK(P[0].Note == 49 && P[0].Instrument == 0 && P[0].Volume == -1);
			CHECK(P[0].Effects[0] == Effect('4', 0x46) && P[0].Effects[1] == Effect('F', 5));
			CHECK(P[4].Note == NOTE_RELEASE && P[8].Note == NOTE_HALT && P[1].Empty());
			CHECK(P[12].Note == 95 && P[12].Instrument == -1 && P[12].Volume == 3);
		}
		CHECK(T.Patterns[0][1].size() == 16 && T.Patterns[0][1][0].Note == 45 && T.Patterns[0][1][0].Effects[0].Letter == 0 &&
			T.Patterns[0][1][0].Effects[1] == Effect('B', 0));
		// The noise channel's notes are what it plays, 0-15
		CHECK(T.Patterns[3][0].size() == 16 && T.Patterns[3][0][0].Note == 0 && T.Patterns[3][0][2].Note == 11);
	}
	CHECK(U.Name == "Second" && U.PatternLength == 8 && U.Speed == 3 && U.Tempo == 140 && U.Columns == std::vector<int>({ 1, 1, 1, 1, 4 }));

	// As FamiTracker's File > Export text writes it
	const std::string Text = WriteText(M);
	CHECK(Text.compare(0, 52, "# FamiTracker text export 0.4.2\n\n# Song information\n") == 0);
	CHECK(Has(Text, "TITLE           \"Test module\"\nAUTHOR          \"Tester\"\nCOPYRIGHT       \"2026\"\n"));
	CHECK(Has(Text, "# Song comment\nCOMMENT \"First line\"\nCOMMENT \"Second \"\"quoted\"\" line\"\n\n"));
	CHECK(Has(Text, "MACHINE         0\nFRAMERATE       0\nEXPANSION       0\nVIBRATO         1\nSPLIT           32\n"));
	CHECK(Has(Text, "# Macros\n"
		"MACRO       0   0  -1   2   0 : 15 12 8 4 0\n"
		"MACRO       0   3  -1   2   0 : 15 12 8 4 0\n"
		"MACRO       1   1   0  -1   0 : 0 4 7\n"
		"MACRO       4   0   1  -1   0 : 2 1\n"
		"MACRO       4   1  -1  -1   0 : 0\n\n"));
	CHECK(Has(Text, "# DPCM samples\nDPCMDEF   0     3 \"kick\"\nDPCM : 55 AA 01\nDPCMDEF   3    33 \"snare\"\n"
		"DPCM : 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F 10 11 12 13 14 15 16 17 18 19 1A 1B 1C 1D 1E 1F\nDPCM : 20\n\n"));
	CHECK(Has(Text, "# Instruments\n"
		"INST2A03   0     0   1  -1  -1   0 \"Lead\"\n"
		"INST2A03   2     2  -1  -1  -1  -1 \"Drums\"\n"
		"KEYDPCM   2   3   0     0  15   1     0  64\n"
		"KEYDPCM   2   3   2     3  14   0     0  -1\n"
		"INST2A03   5     3  -1  -1  -1   1 \"Copy\"\n\n"));
	CHECK(Has(Text, "TRACK  16   6 150 \"Main\"\nCOLUMNS : 2 1 1 1 1\n\nORDER 00 : 00 00 00 00 00\nORDER 01 : 01 00 01 00 00\n\nPATTERN 00\n"));
	CHECK(Has(Text, "ROW 00 : C#4 00 . 446 F05 : ... .. . ... : ... .. . ... : 0-# .. . ... : C-3 02 . ...\n"));
	CHECK(Has(Text, "ROW 02 : ... .. . ... ... : ... .. . ... : ... .. . ... : B-# 00 F ... : ... .. . ...\n"));
	CHECK(Has(Text, "ROW 04 : === .. . ... ... : "));
	CHECK(Has(Text, "ROW 08 : --- .. . ... ... : "));
	CHECK(Has(Text, "ROW 0C : B-7 .. 3 ... ... : "));
	CHECK(Has(Text, "PATTERN 01\nROW 00 : A-3 05 . ... B00 : "));
	CHECK(Has(Text, "ROW 0F : ... .. . ... ... : ... .. . ... : G-1 .. . D00 : ... .. . ... : ... .. . ...\n"));
	// A pattern no frame plays is written, empty ones aren't
	CHECK(Has(Text, "PATTERN 07\n") && Has(Text, "ROW 03 : ... .. . ... ... : E-2 00 8 ... : ... .. . ... : ... .. . ... : ... .. . ...\n"));
	CHECK(!Has(Text, "PATTERN 02\nROW 00 : ... .. . ... ... :"));
	CHECK(Has(Text, "TRACK   8   3 140 \"Second\"\nCOLUMNS : 1 1 1 1 4\n\nORDER 00 : 00 00 00 00 02\n\n"));
	CHECK(Has(Text, "ROW 07 : ... .. . ... : ... .. . ... : ... .. . ... : ... .. . ... : D-3 02 . Z40 ... ... C00\n"));
	CHECK(!Has(Text, "INST2A03   1") && !Has(Text, "DPCMDEF   1"));
	CHECK(Has(Text, "\n# End of export\n"));

	// The study tools read it (an empty slot is no instrument to them)
	TrackReport Report;
	Report.Speed = T.Speed;
	Report.Tempo = T.Tempo;
	Report.TicksPerRow = 6;
	CHECK(!DescribeStudy(StudyTrack(M, 0, Report)).empty());
}

// --- Expansion chips ---

void TestVrc6()
{
	Writer w;
	Params(w, EXP_VRC6, 8, 1);		// PAL asked: expansion chips play at NTSC's rate
	Info(w, "VRC6", "", "");
	Header(w, { "Saw" }, { { 0, 0, 0, 0, 0, 0, 0, 1 } });
	w.Begin("INSTRUMENTS", 6).Int(2);
	w.Int(0).U8(T_VRC6);
	InstSequences(w, { 0, -1, -1, -1, 2 });
	w.Name("Saw lead");
	// A count of 4: 0.4.6 reads the VRC6's five all the same
	w.Int(1).U8(T_VRC6).Int(4);
	for (int i = 0; i < 5; ++i)
		w.U8(i == 4 ? 1 : 0).U8(i == 4 ? 2 : 0);
	w.Name("Pulse");
	w.End();
	Sequences6(w, "SEQUENCES", std::vector<Seq>());
	Frames3(w, { { 6, 150, 4, { { 0, 0, 0, 0, 0, 0, 0, 0 } } } });
	w.Begin("PATTERNS", 5);
	PatternRows(w, 0, 7, 0, { { 1, 6, 2, 0, 0xA, { { E_ARPEGGIO, 0x37 }, { E_PORTA_UP, 0x04 } } } }, 2);
	w.End();
	Sequences6(w, "SEQUENCES_VRC6", {
		{ 0, SEQ_VOLUME, { 63, 40, 20 }, -1, -1, 0 },
		{ 2, SEQ_DUTY, { 7, 3 }, 0, -1, 0 },
	});

	Module M;
	FtmInfo Info;
	if (!ReadModule(w.Finish(), M, Info)) {
		CHECK(false);
		return;
	}
	CHECK(M.Expansion == EXP_VRC6 && M.Machine == 0 && M.ChannelCount() == 8);
	CHECK(M.Instruments.size() == 2 && M.Instruments[0].Kind == INST_VRC6 && M.Instruments[0].MacroIndex[SEQ_DUTY] == 2);
	CHECK(M.Instruments.size() == 2 && Items(M.Instruments[0].Macros[SEQ_VOLUME], { 63, 40, 20 }));
	CHECK(M.Instruments.size() == 2 && M.Instruments[1].Name == "Pulse" && M.Instruments[1].MacroIndex[SEQ_DUTY] == 2);
	CHECK(M.Macros[MACROS_VRC6][SEQ_DUTY].size() == 3 && M.Macros[MACROS_2A03][SEQ_DUTY].empty());
	CHECK(M.Tracks.size() == 1 && M.Tracks[0].Patterns.size() == 8 && M.Tracks[0].Patterns[7][0][1].Note == 29);
	const std::string Text = WriteText(M);
	CHECK(Has(Text, "MACHINE         0\nFRAMERATE       0\nEXPANSION       1\n"));
	CHECK(Has(Text, "MACROVRC6   0   0  -1  -1   0 : 63 40 20\nMACROVRC6   4   2   0  -1   0 : 7 3\n"));
	CHECK(Has(Text, "INSTVRC6   0     0  -1  -1  -1   2 \"Saw lead\"\n"));
	CHECK(Has(Text, "COLUMNS : 1 1 1 1 1 1 1 2\n"));
	CHECK(Has(Text, "ROW 01 : ... .. . ... : ... .. . ... : ... .. . ... : ... .. . ... : ... .. . ... : ... .. . ... : ... .. . ... : F-2 00 A 037 104\n"));
}

void TestVrc7()
{
	Writer w;
	Params(w, EXP_VRC7, 11);
	Info(w, "VRC7", "", "");
	Header(w, { "FM" }, { std::vector<int>(11, 0) });
	w.Begin("INSTRUMENTS", 6).Int(2);
	w.Int(1).U8(T_VRC7).Int(0);
	const int Regs[8] = { 0x80, 0xFF, 0x1C, 0x01, 0xF0, 0xF2, 0x01, 0x01 };
	for (int i = 0; i < 8; ++i)
		w.U8(Regs[i]);
	w.Name("Piano");
	w.Int(3).U8(T_VRC7).Int(5);
	for (int i = 0; i < 8; ++i)
		w.U8(0);
	w.Name("Patch 5");
	w.End();
	Frames3(w, { { 6, 150, 4, { std::vector<int>(11, 0) } } });
	w.Begin("PATTERNS", 5);
	PatternRows(w, 0, 6, 0, { { 0, 1, 4, 1, NO_VOL, { { E_PORTA_DOWN, 0x20 } } } }, 1);
	w.End();

	Module M;
	FtmInfo Info;
	if (!ReadModule(w.Finish(), M, Info)) {
		CHECK(false);
		return;
	}
	CHECK(M.ChannelCount() == 11 && M.Instruments.size() == 4 && M.Instruments[0].Unused && M.Instruments[2].Unused);
	if (M.Instruments.size() == 4) {
		CHECK(M.Instruments[1].Kind == INST_VRC7 && M.Instruments[1].Patch == 0 && M.Instruments[1].Regs[1] == 0xFF);
		CHECK(M.Instruments[3].Patch == 5);
	}
	const std::string Text = WriteText(M);
	CHECK(Has(Text, "INSTVRC7   1     0 80 FF 1C 01 F0 F2 01 01 \"Piano\"\nINSTVRC7   3     5 00 00 00 00 00 00 00 00 \"Patch 5\"\n"));
	CHECK(Has(Text, ": C-4 01 . 220 : ... .. . ... : ... .. . ... : ... .. . ... : ... .. . ...\n"));
}

// An FDS instrument's data, as InstrumentFDS.cpp stores it: the wave, the
// modulation table, its speed, depth and delay, and the sequences
void FdsInstrument(Writer &w, int Speed, int Depth, int Delay, const std::vector<Seq> &Sequences)
{
	for (int i = 0; i < 64; ++i)
		w.U8(i < 32 ? i * 2 : 127 - i * 2 + 1);
	for (int i = 0; i < 32; ++i)
		w.U8(i % 8);
	w.Int(Speed).Int(Depth).Int(Delay);
	for (size_t i = 0; i < Sequences.size(); ++i) {
		w.U8(int(Sequences[i].Items.size())).Int(Sequences[i].Loop).Int(Sequences[i].Release).Int(Sequences[i].Setting);
		for (size_t k = 0; k < Sequences[i].Items.size(); ++k)
			w.U8(Sequences[i].Items[k]);
	}
}

void TestFds()
{
	Writer w;
	Params(w, EXP_FDS, 6);
	Info(w, "FDS", "", "");
	Header(w, { "Disk" }, { { 0, 0, 0, 0, 0, 0 } });
	w.Begin("INSTRUMENTS", 6).Int(1);
	w.Int(0).U8(T_FDS);
	FdsInstrument(w, 300, 12, 4, {
		{ 0, 0, { 32, 24, 16, 8 }, -1, 1, 0 },
		{ 0, 1, { 0, 12 }, 0, -1, ARP_FIXED },
		{ 0, 2, {}, -1, -1, 0 },
	});
	w.Name("Disk bass");
	w.End();
	Frames3(w, { { 6, 150, 4, { { 0, 0, 0, 0, 0, 0 } } } });
	w.Begin("PATTERNS", 5);
	PatternRows(w, 0, 5, 0, { { 0, 1, 3, 0, NO_VOL, { { E_FDS_MOD_DEPTH, 0x3F } } } }, 1);
	w.End();

	Module M;
	FtmInfo Info;
	if (!ReadModule(w.Finish(), M, Info)) {
		CHECK(false);
		return;
	}
	CHECK(M.Instruments.size() == 1);
	if (M.Instruments.size() != 1)
		return;
	const Instrument &I = M.Instruments[0];
	CHECK(I.Kind == INST_FDS && I.ModRate == 300 && I.ModDepth == 12 && I.ModDelay == 4);
	CHECK(I.FdsWave.size() == 64 && I.FdsWave[1] == 2 && I.FdsWave[63] == 2 && I.FdsMod.size() == 32 && I.FdsMod[9] == 1);
	CHECK(Items(I.Macros[SEQ_VOLUME], { 32, 24, 16, 8 }) && I.Macros[SEQ_VOLUME].Release == 1);
	CHECK(Items(I.Macros[SEQ_ARPEGGIO], { 0, 12 }) && I.Macros[SEQ_ARPEGGIO].Setting == ARP_FIXED && I.Macros[SEQ_PITCH].Empty());
	// 0.4.6's patterns are version 5: no octaves moved
	CHECK(M.Tracks.size() == 1 && M.Tracks[0].Patterns[5][0][0].Note == 36 && M.Tracks[0].Patterns[5][0][0].Effects[0] == Effect('H', 0x3F));
	const std::string Text = WriteText(M);
	CHECK(Has(Text, "INSTFDS    0     1 300  12   4 \"Disk bass\"\nFDSWAVE    0 :  0  2  4"));
	CHECK(Has(Text, "\nFDSMOD     0 :  0  1  2  3  4  5  6  7  0  1"));
	CHECK(Has(Text, "\nFDSMACRO   0   0  -1   1   0 : 32 24 16 8\nFDSMACRO   0   1   0  -1   1 : 0 12\n"));
	CHECK(!Has(Text, "FDSMACRO   0   2"));
	CHECK(Has(Text, ": C-3 00 . H3F\n"));
}

// A study keeps an FDS sound's wave and modulator besides its envelope, so
// reference_sound can make the instrument again (Api's references)
void TestFdsStudy()
{
	Writer w;
	Params(w, EXP_FDS, 6);
	Info(w, "FDS study", "", "");
	Header(w, { "Disk" }, { { 0, 0, 0, 0, 0, 0 } });
	w.Begin("INSTRUMENTS", 6).Int(1);
	w.Int(0).U8(T_FDS);
	FdsInstrument(w, 34, 5, 2, {
		{ 0, 0, { 10, 20, 31, 28, 26 }, -1, 4, 0 },
		{ 0, 1, {}, -1, -1, 0 },
		{ 0, 2, {}, -1, -1, 0 },
	});
	w.Name("Disk lead");
	w.End();
	Frames3(w, { { 6, 150, 4, { { 0, 0, 0, 0, 0, 0 } } } });
	w.Begin("PATTERNS", 5);
	std::vector<RowData> Rows;
	for (int r = 0; r < 16; ++r)
		Rows.push_back(RowData{ r * 4, 1 + (r % 3) * 2, 5, 0, NO_VOL, {} });
	PatternRows(w, 0, 5, 0, Rows, 1);
	w.End();
	Module M;
	FtmInfo Info;
	if (!ReadModule(w.Finish(), M, Info)) {
		CHECK(false);
		return;
	}
	const TrackStudy St = StudyTrack(M, 0, FtmTrackReport(M, Info, 0));
	const StudySound *pFds = 0;
	for (size_t i = 0; i < St.Sounds.size(); ++i)
		if (St.Sounds[i].Kind == INST_FDS)
			pFds = &St.Sounds[i];
	CHECK(pFds != 0);
	if (pFds == 0)
		return;
	CHECK(pFds->FdsWave.size() == 64 && pFds->FdsWave[1] == 2 && pFds->FdsWave[63] == 2);
	CHECK(pFds->FdsMod.size() == 32 && pFds->FdsMod[9] == 1);
	CHECK(pFds->ModRate == 34 && pFds->ModDepth == 5 && pFds->ModDelay == 2);
	CHECK(Items(pFds->Macros[SEQ_VOLUME], { 10, 20, 31, 28, 26 }));
	const std::string Text = DescribeStudy(St, true);
	CHECK(Has(Text, "      FDS wave 0 2 4 6 8"));
	CHECK(Has(Text, "; modulation 0 1 2 3 4 5 6 7 0 1"));
	CHECK(Has(Text, ", speed 34, depth 5, delay 2\n"));
}

void TestN163()
{
	Writer w;
	Params(w, EXP_N163, 8, 0, 0, 1, 3);		// three N163 channels: 8 with the 2A03's
	Info(w, "N163", "", "");
	Header(w, { "Namco" }, { { 0, 0, 0, 0, 0, 1, 0, 0 } });
	w.Begin("INSTRUMENTS", 6).Int(1);
	w.Int(4).U8(T_N163);
	InstSequences(w, { 1, 0, -1, -1, -1 });
	w.Int(16).Int(32).Int(2);		// waves of 16 samples, at 32, two of them
	for (int i = 0; i < 32; ++i)
		w.U8(i % 16);
	w.Name("Wave");
	w.End();
	Frames3(w, { { 6, 150, 4, { { 0, 0, 0, 0, 0, 1, 2, 0 }, { 0, 0, 0, 0, 0, 0, 0, 3 } } } });
	w.Begin("PATTERNS", 5);
	PatternRows(w, 0, 5, 1, { { 3, 1, 5, 4, 7, { { 0, 0 }, { E_VOLUME, 2 } } } }, 2);
	PatternRows(w, 0, 7, 3, { { 0, 12, 2, 4, NO_VOL, {} } }, 1);
	w.End();
	SequencesN163(w, {
		{ 0, SEQ_ARPEGGIO, { 0, -12 }, -1, -1, ARP_RELATIVE },
		{ 1, SEQ_VOLUME, { 15, 10, 5 }, 1, 2, 0 },
	});

	Module M;
	FtmInfo Info;
	if (!ReadModule(w.Finish(), M, Info)) {
		CHECK(false);
		return;
	}
	// FamiTracker's columns: all eight N163 channels, the unused ones empty
	CHECK(M.Expansion == EXP_N163 && M.N163Channels == 3 && M.ChannelCount() == 13);
	CHECK(M.Instruments.size() == 5 && M.Instruments[4].Kind == INST_N163);
	if (M.Instruments.size() == 5) {
		const Instrument &I = M.Instruments[4];
		CHECK(I.WaveSize == 16 && I.WavePos == 32 && I.Waves.size() == 2 && I.Waves[1][15] == 15);
		CHECK(Items(I.Macros[SEQ_VOLUME], { 15, 10, 5 }) && I.Macros[SEQ_VOLUME].Loop == 1 && I.Macros[SEQ_VOLUME].Release == 2);
		CHECK(Items(I.Macros[SEQ_ARPEGGIO], { 0, -12 }) && I.Macros[SEQ_ARPEGGIO].Setting == ARP_RELATIVE);
	}
	CHECK(M.Tracks.size() == 1);
	if (M.Tracks.size() == 1) {
		const Track &T = M.Tracks[0];
		CHECK(T.Columns.size() == 13 && T.Columns[5] == 2 && T.Frames.size() == 2 && T.Frames[0].size() == 13);
		CHECK(T.Frames[1] == std::vector<int>({ 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0 }));
		CHECK(T.Patterns.size() == 13 && T.Patterns[12].size() == 1 && T.Patterns[12][0].size() == 4);
		CHECK(T.Patterns[5].size() == 2 && T.Patterns[5][1][3].Note == 60 && T.Patterns[5][1][3].Effects[1] == Effect('E', 2));
	}
	const std::string Text = WriteText(M);
	CHECK(Has(Text, "EXPANSION       16\nVIBRATO         1\nSPLIT           32\n\n# Namco 163 global settings\nN163CHANNELS    3\n\n"));
	CHECK(Has(Text, "MACRON163   0   1   1   2   0 : 15 10 5\nMACRON163   1   0  -1  -1   2 : 0 -12\n"));
	CHECK(Has(Text, "INSTN163   4     1   0  -1  -1  -1  16  32   2 \"Wave\"\n"
		"N163WAVE   4   0 : 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15\nN163WAVE   4   1 : 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15\n"));
	CHECK(Has(Text, "COLUMNS : 1 1 1 1 1 2 1 1 1 1 1 1 1\n"));
	CHECK(Has(Text, "ORDER 00 : 00 00 00 00 00 01 02 00 00 00 00 00 00\n"));

	// The block's name before 0.4.0
	Writer x;
	SequencesN163(x, { { 2, SEQ_PITCH, { 1, -1 }, 0, -1, 0 } }, "SEQUENCES_N106");
	CHECK(ReadModule(x.Finish(), M, Info) && M.Macros[MACROS_N163][SEQ_PITCH].size() == 3 &&
		Items(M.Macros[MACROS_N163][SEQ_PITCH][2], { 1, -1 }) && Info.Problems.empty());
}

void TestMmc5()
{
	Writer w;
	Params(w, EXP_MMC5, 7);
	Header(w, { "" }, { std::vector<int>(7, 0) });
	Frames3(w, { { 6, 150, 2, { std::vector<int>(7, 0) } } });
	w.Begin("PATTERNS", 5);
	PatternRows(w, 0, 6, 0, { { 1, 3, 3, NO_INST, NO_VOL, { { E_DUTY, 2 } } } }, 1);
	w.End();
	Module M;
	FtmInfo Info;
	if (!ReadModule(w.Finish(), M, Info)) {
		CHECK(false);
		return;
	}
	CHECK(M.ChannelCount() == 7 && M.Instruments.empty() && M.Tracks.size() == 1 && M.Tracks[0].Name.empty());
	CHECK(Has(WriteText(M), "ROW 01 : ... .. . ... : ... .. . ... : ... .. . ... : ... .. . ... : ... .. . ... : ... .. . ... : D-3 .. . V02\n"));
}

// --- Older releases ---

// FamiTracker 0.2.2's module (file version 2.00): sequences numbered alone
// as a value and its length, rows of a byte, one effect, speeds and volumes
// off by one
std::vector<uint8_t> Make022()
{
	Writer w(0x0200);
	w.Begin("PARAMS", 1).Int(5).Int(5).Int(0).Int(0).End();		// speed 5 (6 now), 5 channels, NTSC
	Info(w, "Old", "Someone", "2005");
	w.Begin("INSTRUMENTS", 1).Int(2);
	w.Int(0).U8(1).Int(5);
	const int Seqs0[5][2] = { { 1, 3 }, { 1, 1 }, { 0, 0 }, { 0, 0 }, { 1, 2 } };
	for (int i = 0; i < 5; ++i)
		w.U8(Seqs0[i][0]).U8(Seqs0[i][1]);
	InstKeys(w, { { 24, 1, 0x0F, 0 } }, 6, false);
	w.Name("Square");
	w.Int(1).U8(1).Int(5);
	const int Seqs1[5][2] = { { 1, 3 }, { 0, 0 }, { 1, 4 }, { 0, 0 }, { 0, 0 } };
	for (int i = 0; i < 5; ++i)
		w.U8(Seqs1[i][0]).U8(Seqs1[i][1]);
	InstKeys(w, std::vector<Key>(), 6, false);
	w.Name("Bass");
	w.End();
	// Value and length less one; a negative length loops back over that many
	w.Begin("SEQUENCES", 1).Int(4);
	w.Int(1).U8(3).U8(0).U8(1).U8(12).U8(0).U8(0).U8(-2);		// arpeggio: 0 0 12, looping
	w.Int(2).U8(1).U8(5).U8(1);									// duty: 5, kept to 3
	w.Int(3).U8(3).U8(15).U8(0).U8(10).U8(2).U8(20).U8(0);		// volume: 15 10 10 10 20, kept to 15
	w.Int(4).U8(2).U8(3).U8(2).U8(-1).U8(0);					// pitch: 3 then 0s, -1
	w.End();
	w.Begin("FRAMES", 1).Int(2).Int(5);
	for (int i = 0; i < 5; ++i)
		w.U8(0);
	w.U8(1).U8(0).U8(0).U8(0).U8(0);
	w.End();
	w.Begin("PATTERNS", 1).Int(32);
	PatternRows(w, -1, 0, 0, {
		{ 0, 1, 4, 0, 0, { { E_SPEED, 5 } } },
		{ 3, 0, 0, 7, 9, { { E_PORTAOFF, 0x33 } } },
	}, 1, 1);
	PatternRows(w, -1, 0, 1, { { 31, 5, 2, 1, NO_VOL, { { E_PORTAMENTO, 0x10 } } } }, 1, 1);
	PatternRows(w, -1, 3, 0, { { 1, 3, 0, 1, 1, {} } }, 1, 1);
	w.End();
	Samples(w, { { 0, "s", { 1, 2 } } });
	return w.Finish();
}

void TestVersion022()
{
	Module M;
	FtmInfo Info;
	if (!ReadModule(Make022(), M, Info)) {
		CHECK(false);
		return;
	}
	CHECK(Info.Version == 0x0200 && Info.VibratoStyle == 0 && Info.SplitPoint == 21);
	CHECK(M.Title == "Old" && M.Tracks.size() == 1);
	// Sequences numbered by kind in the order the instruments use them, turned into today's
	CHECK(M.Macros[MACROS_2A03][SEQ_VOLUME].size() == 1 && Items(M.Macros[MACROS_2A03][SEQ_VOLUME][0], { 15, 10, 10, 10, 15 }));
	CHECK(M.Macros[MACROS_2A03][SEQ_ARPEGGIO].size() == 1 && Items(M.Macros[MACROS_2A03][SEQ_ARPEGGIO][0], { 0, 0, 12 }) &&
		M.Macros[MACROS_2A03][SEQ_ARPEGGIO][0].Loop == 0);
	CHECK(M.Macros[MACROS_2A03][SEQ_PITCH].size() == 1 && Items(M.Macros[MACROS_2A03][SEQ_PITCH][0], { 3, 0, 0, -1 }));
	CHECK(M.Macros[MACROS_2A03][SEQ_DUTY].size() == 1 && Items(M.Macros[MACROS_2A03][SEQ_DUTY][0], { 3, 3 }));
	CHECK(M.Instruments.size() == 2);
	if (M.Instruments.size() == 2) {
		CHECK(M.Instruments[0].MacroIndex[SEQ_VOLUME] == 0 && M.Instruments[0].MacroIndex[SEQ_ARPEGGIO] == 0 &&
			M.Instruments[0].MacroIndex[SEQ_DUTY] == 0 && M.Instruments[0].MacroIndex[SEQ_PITCH] == -1);
		CHECK(M.Instruments[1].MacroIndex[SEQ_VOLUME] == 0 && M.Instruments[1].MacroIndex[SEQ_PITCH] == 0);
		// Six octaves of keys and no delta
		CHECK(M.Instruments[0].Keys.size() == 1 && M.Instruments[0].Keys[0].Note == 24 && M.Instruments[0].Keys[0].Delta == -1);
	}
	if (M.Tracks.size() != 1)
		return;
	const Track &T = M.Tracks[0];
	CHECK(T.Name == "New song" && T.Speed == 6 && T.Tempo == 150 && T.PatternLength == 32);
	CHECK(T.Frames.size() == 2 && T.Frames[1] == std::vector<int>({ 1, 0, 0, 0, 0 }));
	const std::string Text = WriteText(M);
	CHECK(Has(Text, "MACRO       0   0  -1  -1   0 : 15 10 10 10 15\nMACRO       1   0   0  -1   0 : 0 0 12\n"
		"MACRO       2   0  -1  -1   0 : 3 0 0 -1\nMACRO       4   0  -1  -1   0 : 3 3\n"));
	CHECK(Has(Text, "INST2A03   0     0   0  -1  -1   0 \"Square\"\nKEYDPCM   0   2   0     0  15   0     0  -1\n"
		"INST2A03   1     0  -1   0  -1  -1 \"Bass\"\n"));
	CHECK(Has(Text, "TRACK  32   6 150 \"New song\"\n"));
	// F05 was speed 5 (now F06); volume 0 none and 9 is 8; no instrument without a note; 7xx portamento off is 300
	CHECK(Has(Text, "ROW 00 : C-4 00 . F06 : "));
	CHECK(Has(Text, "ROW 01 : ... .. . ... : ... .. . ... : ... .. . ... : 2-# 01 0 ... : "));
	CHECK(Has(Text, "ROW 03 : ... .. 8 300 : "));
	CHECK(Has(Text, "ROW 1F : E-2 01 F 311 : "));
}

// 0.2.4 (2.01): a HEADER, rows of an int and the effect columns it gives;
// sequences still numbered alone
void TestVersion024()
{
	Writer w(0x0201);
	w.Begin("PARAMS", 1).Int(30).Int(5).Int(1).Int(0).End();		// a speed of 30: the tempo
	w.Begin("HEADER", 1);
	for (int c = 0; c < 5; ++c)
		w.U8(c).U8(c == 1 ? 2 : 0);
	w.End();
	w.Begin("INSTRUMENTS", 1).Int(1);
	w.Int(0).U8(1).Int(5);
	for (int i = 0; i < 5; ++i)
		w.U8(i == 0 ? 1 : 0).U8(i == 0 ? 5 : 0);
	InstKeys(w, std::vector<Key>(), 6, false);
	w.Name("Tone");
	w.End();
	w.Begin("SEQUENCES", 1).Int(1);
	w.Int(5).U8(1).U8(12).U8(3);		// 12 for four frames
	w.End();
	w.Begin("FRAMES", 1).Int(1).Int(5).U8(0).U8(0).U8(0).U8(0).U8(0).End();
	w.Begin("PATTERNS", 1).Int(16);
	PatternRows(w, -1, 1, 0, { { 2, 1, 3, NO_INST, NO_VOL, { { 0, 0 }, { 0, 0 }, { E_PORTAMENTO, 0x0F } } } }, 3);
	w.End();
	Module M;
	FtmInfo Info;
	if (!ReadModule(w.Finish(), M, Info)) {
		CHECK(false);
		return;
	}
	CHECK(M.Machine == 1 && M.Tracks.size() == 1);
	if (M.Tracks.size() == 1) {
		CHECK(M.Tracks[0].Speed == 6 && M.Tracks[0].Tempo == 30 && M.Tracks[0].Columns[1] == 3);
		CHECK(M.Tracks[0].Patterns[1][0][2].Effects[2] == Effect('3', 0x10));
	}
	CHECK(M.Instruments.size() == 1 && M.Instruments[0].MacroIndex[SEQ_VOLUME] == 0 && Items(M.Instruments[0].Macros[SEQ_VOLUME], { 12, 12, 12, 12 }));
}

// 0.2.6 (2.10): tracks, the frames' speed or tempo, sequences numbered by
// kind (still a value and its length)
void TestVersion026()
{
	Writer w(0x0210);
	w.Begin("PARAMS", 2).U8(0).Int(5).Int(1).Int(0).End();		// PAL
	w.Begin("HEADER", 2).U8(1);
	for (int c = 0; c < 5; ++c)
		w.U8(c).U8(0).U8(0);
	w.End();
	w.Begin("INSTRUMENTS", 2).Int(1);
	w.Int(0).U8(1).Int(5);
	const int Seqs[5][2] = { { 1, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 1, 2 } };
	for (int i = 0; i < 5; ++i)
		w.U8(Seqs[i][0]).U8(Seqs[i][1]);
	InstKeys(w, std::vector<Key>(), 8, false);
	w.Name("Pulse");
	w.End();
	w.Begin("SEQUENCES", 2).Int(2);
	w.Int(0).Int(SEQ_VOLUME).U8(2).U8(8).U8(1).U8(4).U8(0);	// 8 8 4
	w.Int(2).Int(SEQ_DUTY).U8(2).U8(1).U8(0).U8(2).U8(-1);	// 1, looping back over it: duties aren't kept to 0-3 any more
	w.End();
	// Track 1: speed 8 at PAL's tempo; track 2: a speed of 150 is the tempo
	w.Begin("FRAMES", 2);
	w.Int(1).Int(8).Int(64);
	for (int c = 0; c < 5; ++c)
		w.U8(0);
	w.Int(1).Int(150).Int(32);
	for (int c = 0; c < 5; ++c)
		w.U8(0);
	w.End();
	// and on its own, a speed of 20 is a tempo too
	{
		Writer x(0x0210);
		x.Begin("FRAMES", 2).Int(1).Int(20).Int(64).U8(0).U8(0).U8(0).U8(0).U8(0).End();
		Module M;
		FtmInfo Info;
		CHECK(ReadModule(x.Finish(), M, Info) && M.Tracks.size() == 1 && M.Tracks[0].Speed == 6 && M.Tracks[0].Tempo == 20);
	}
	w.Begin("PATTERNS", 2);
	PatternRows(w, 1, 0, 0, { { 0, 1, 3, 0, NO_VOL, { { E_PORTAMENTO, 0xFF } } } }, 1);
	w.End();
	Module M;
	FtmInfo Info;
	if (!ReadModule(w.Finish(), M, Info)) {
		CHECK(false);
		return;
	}
	CHECK(M.Machine == 1 && M.Tracks.size() == 2);
	if (M.Tracks.size() == 2) {
		CHECK(M.Tracks[0].Speed == 8 && M.Tracks[0].Tempo == 125 && M.Tracks[0].PatternLength == 64);
		CHECK(M.Tracks[1].Speed == 6 && M.Tracks[1].Tempo == 150 && M.Tracks[1].PatternLength == 32);
		CHECK(M.Tracks[1].Patterns[0][0][0].Effects[0] == Effect('3', 0xFF));
	}
	CHECK(M.Macros[MACROS_2A03][SEQ_VOLUME].size() == 1 && Items(M.Macros[MACROS_2A03][SEQ_VOLUME][0], { 8, 8, 4 }));
	CHECK(M.Macros[MACROS_2A03][SEQ_DUTY].size() == 3 && Items(M.Macros[MACROS_2A03][SEQ_DUTY][2], { 1 }) &&
		M.Macros[MACROS_2A03][SEQ_DUTY][2].Loop == 0);
	CHECK(M.Instruments.size() == 1 && M.Instruments[0].MacroIndex[SEQ_DUTY] == 2);
}

// 0.2.7 (3.00): today's sequences, a loop point at the end meaning none; and
// a file that ends between blocks, without END, which FamiTracker reads
void TestVersion027()
{
	Writer w(0x0300);
	w.Begin("PARAMS", 2).U8(0).Int(5).Int(0).Int(0).End();
	w.Begin("SEQUENCES", 3).Int(2);
	w.Int(0).Int(SEQ_VOLUME).U8(3).Int(3).U8(9).U8(6).U8(3);		// loop at 3 of 3: none
	w.Int(1).Int(SEQ_VOLUME).U8(3).Int(1).U8(9).U8(6).U8(3);
	w.End();
	Module M;
	FtmInfo Info;
	if (!ReadModule(w.Bytes, M, Info)) {
		CHECK(false);
		return;
	}
	CHECK(M.Macros[MACROS_2A03][SEQ_VOLUME].size() == 2 && M.Macros[MACROS_2A03][SEQ_VOLUME][0].Loop == -1 &&
		M.Macros[MACROS_2A03][SEQ_VOLUME][1].Loop == 1);
	// No track block: one track as FamiTracker makes it
	CHECK(M.Tracks.size() == 1 && M.Tracks[0].Name == "New song" && M.Tracks[0].PatternLength == 64 && M.Tracks[0].Frames.size() == 1);
	CHECK(Info.Problems.size() == 1 && Info.Problems[0].find("END") != std::string::npos);
}

// Release points: SEQUENCES 4 kept them with each sequence, 5 (0.3.5) in a
// list of every slot after them
void TestReleasePoints()
{
	for (int Version = 4; Version <= 5; ++Version) {
		Writer w(0x0420);
		Params(w, EXP_VRC6, 8);
		const char *Ids[2] = { "SEQUENCES", "SEQUENCES_VRC6" };
		for (int b = 0; b < 2; ++b) {
			w.Begin(Ids[b], Version).Int(2);
			w.Int(0).Int(SEQ_VOLUME).U8(3).Int(1);
			if (Version == 4)
				w.Int(2).Int(0);
			w.U8(10).U8(8).U8(6);
			w.Int(3).Int(SEQ_ARPEGGIO).U8(2).Int(-1);
			if (Version == 4)
				w.Int(-1).Int(ARP_FIXED);
			w.U8(0).U8(7);
			if (Version == 5)
				for (int i = 0; i < 128; ++i)
					for (int j = 0; j < 5; ++j) {
						w.Int(i == 0 && j == SEQ_VOLUME ? 2 : -1);
						w.Int(i == 3 && j == SEQ_ARPEGGIO ? ARP_FIXED : 0);
					}
			w.End();
		}
		Module M;
		FtmInfo Info;
		if (!ReadModule(w.Finish(), M, Info)) {
			CHECK(false);
			continue;
		}
		for (int Chip = 0; Chip < 2; ++Chip) {
			const std::vector<Macro> &Vol = M.Macros[Chip][SEQ_VOLUME], &Arp = M.Macros[Chip][SEQ_ARPEGGIO];
			CHECK(Vol.size() == 1 && Items(Vol[0], { 10, 8, 6 }) && Vol[0].Loop == 1 && Vol[0].Release == 2);
			CHECK(Arp.size() == 4 && Items(Arp[3], { 0, 7 }) && Arp[3].Setting == ARP_FIXED);
		}
	}
	// A loop point not before the release point goes (CSequence)
	Writer w;
	Sequences6(w, "SEQUENCES", { { 0, SEQ_VOLUME, { 5, 4, 3, 2 }, 2, 2, 0 } });
	Module M;
	FtmInfo Info;
	if (ReadModule(w.Finish(), M, Info))
		CHECK(M.Macros[MACROS_2A03][SEQ_VOLUME].size() == 1 && M.Macros[MACROS_2A03][SEQ_VOLUME][0].Loop == -1 &&
			M.Macros[MACROS_2A03][SEQ_VOLUME][0].Release == 2);
}

// 0.3.5's VRC7 and FDS patterns (version 3): the VRC7's 1xx and 2xx swapped
// since, and the FDS's Pxx turned round; before version 5 the FDS played two
// octaves lower, its fixed arpeggios with it
void TestOldExpansionPatterns()
{
	{
		Writer w(0x0420);
		w.Begin("PARAMS", 3).U8(EXP_VRC7).Int(11).Int(0).Int(0).Int(1).End();
		w.Begin("PATTERNS", 3);
		PatternRows(w, 0, 0, 0, { { 0, 1, 3, NO_INST, NO_VOL, { { E_PORTA_UP, 1 } } } }, 1);
		PatternRows(w, 0, 5, 0, { { 0, 1, 3, NO_INST, NO_VOL, { { E_PORTA_UP, 1 } } }, { 1, 0, 0, NO_INST, NO_VOL, { { E_PORTA_DOWN, 2 } } } }, 1);
		w.End();
		Module M;
		FtmInfo Info;
		if (ReadModule(w.Finish(), M, Info) && M.Tracks.size() == 1) {
			const Track &T = M.Tracks[0];
			CHECK(T.Patterns[0][0][0].Effects[0] == Effect('1', 1));
			CHECK(T.Patterns[5][0][0].Effects[0] == Effect('2', 1) && T.Patterns[5][0][1].Effects[0] == Effect('1', 2));
			CHECK(Info.VibratoStyle == 1 && Info.SplitPoint == 21);
		}
		else
			CHECK(false);
	}
	for (int Version = 3; Version <= 5; ++Version) {
		Writer w(0x0420);
		w.Begin("PARAMS", 4).U8(EXP_FDS).Int(6).Int(0).Int(0).Int(1).Int(4).Int(16).End();
		// INSTRUMENTS 4: the FDS's version from 0.3.5, with all three sequences
		w.Begin("INSTRUMENTS", 4).Int(1);
		w.Int(0).U8(T_FDS);
		FdsInstrument(w, 0, 0, 0, { { 0, 0, { 20 }, -1, -1, 0 }, { 0, 1, { 0, 12, 110 }, -1, -1, ARP_FIXED }, { 0, 2, {}, -1, -1, 0 } });
		w.Name("FDS");
		w.End();
		w.Begin("PATTERNS", Version);
		PatternRows(w, 0, 5, 0, {
			{ 0, 1, 3, 0, NO_VOL, { { E_PITCH, 0x10 } } },
			{ 1, 1, 6, 0, NO_VOL, { { E_PITCH, 0x80 } } },
		}, 1);
		w.End();
		Module M;
		FtmInfo Info;
		if (!ReadModule(w.Finish(), M, Info) || M.Tracks.size() != 1 || M.Instruments.size() != 1) {
			CHECK(false);
			continue;
		}
		const std::vector<Cell> &P = M.Tracks[0].Patterns[5][0];
		CHECK(P[0].Note == (Version < 5 ? 60 : 36) && P[1].Note == 72);
		CHECK(P[0].Effects[0] == Effect('P', Version == 3 ? 0xF0 : 0x10) && P[1].Effects[0] == Effect('P', 0x80));
		const Macro &Arp = M.Instruments[0].Macros[SEQ_ARPEGGIO];
		if (Version < 5)
			CHECK(Items(Arp, { 24, 36, -122 }));		// as bytes: 110 + 24 wraps round
		else
			CHECK(Items(Arp, { 0, 12, 110 }));
		CHECK(Items(M.Instruments[0].Macros[SEQ_VOLUME], { 20 }));
	}
}

// The first FDS instruments: no sequences (their name follows the
// modulation), and volumes to 15 until INSTRUMENTS 4
void TestOldFdsInstruments()
{
	Writer w(0x0420);
	w.Begin("PARAMS", 3).U8(EXP_FDS).Int(6).Int(0).Int(0).Int(1).End();
	w.Begin("INSTRUMENTS", 2).Int(2);
	w.Int(0).U8(T_FDS);
	FdsInstrument(w, 1, 2, 3, std::vector<Seq>());
	w.Name("Plain");
	w.Int(1).U8(T_FDS);
	FdsInstrument(w, 0, 0, 0, { { 0, 0, { 15, 8, 0 }, -1, -1, 0 }, { 0, 1, {}, -1, -1, 0 } });		// no pitch before version 3
	w.Name("Shaped");
	w.End();
	Module M;
	FtmInfo Info;
	if (!ReadModule(w.Finish(), M, Info) || M.Instruments.size() != 2) {
		CHECK(false);
		return;
	}
	CHECK(M.Instruments[0].Name == "Plain" && M.Instruments[0].ModRate == 1 && M.Instruments[0].ModDelay == 3 &&
		M.Instruments[0].Macros[SEQ_VOLUME].Empty());
	CHECK(M.Instruments[1].Name == "Shaped" && Items(M.Instruments[1].Macros[SEQ_VOLUME], { 30, 16, 0 }));
}

// --- Damaged and other files ---

void TestOtherFiles()
{
	Module M;
	FtmInfo Info;
	std::string Error;
	CHECK(!ParseFtm(std::vector<uint8_t>(), M, Info, Error) && !Error.empty());
	CHECK(!IsFtm(std::vector<uint8_t>(100, 0)));
	CHECK(ReadError(std::vector<uint8_t>(100, 0)).find("not a FamiTracker module") != std::string::npos);

	// Too new or too old; a low byte of 0x80 or more reads as too new, as in FamiTracker
	CHECK(ReadError(Writer(0x0450).Finish()).find("newer") != std::string::npos);
	CHECK(ReadError(Writer(0x0100).Finish()).find("first format") != std::string::npos);
	CHECK(ReadError(Writer(0x0280).Finish()).find("newer") != std::string::npos);
	CHECK(ReadError(Writer(0x0440).Finish()).empty());

	// Blocks it doesn't know are skipped and said; beginner-friendly FamiTracker's clips and sections quietly
	{
		Writer w;
		w.Begin("GROOVES", 1).Int(7).End();
		w.Begin("BF CLIPS", 1).Int(0).End();
		w.Begin("BF SECTIONS", 1).Int(0).End();
		w.Begin("PATTERNS", 6).End();
		if (ReadModule(w.Finish(), M, Info)) {
			CHECK(Info.Problems.size() == 2);
			CHECK(Info.Problems.size() == 2 && Info.Problems[0].find("GROOVES") != std::string::npos &&
				Info.Problems[1].find("PATTERNS block is version 6") != std::string::npos);
		}
		else
			CHECK(false);
	}
	// What comes after END isn't read, once the name has ended (the name is
	// 16 bytes: END and more letters is another block)
	{
		std::vector<uint8_t> b = Writer().Finish();
		b.push_back(0);
		b.insert(b.end(), 40, 0xEE);
		CHECK(ReadError(b).empty());
		b.erase(b.begin() + 25);
		CHECK(!ReadError(b).empty());
	}
	// Blocks that can't be right
	{
		Writer w;
		w.Begin("PARAMS", 6).End();
		w.Bytes[w.SizeAt + 3] = 0x40;		// a size of a gigabyte
		CHECK(ReadError(w.Finish()).find("too big") != std::string::npos);
		Writer x;
		x.Begin("INFO", 1).Fixed("cut", 20).End();
		CHECK(ReadError(x.Finish()).find("cut short") != std::string::npos);
		Writer y;
		y.Begin("INSTRUMENTS", 6).Int(1).Int(64).U8(T_2A03).End();
		CHECK(ReadError(y.Finish()).find("damaged") != std::string::npos);
		Writer z;
		z.Begin("INSTRUMENTS", 6).Int(1).Int(0).U8(T_S5B).End();
		CHECK(ReadError(z.Finish()).find("Sunsoft 5B") != std::string::npos);
		Writer v;
		Params(v, 0, 5, 2);
		CHECK(ReadError(v.Finish()).find("machine") != std::string::npos);
	}
	// "Sometimes expansion chip is set to 0xFF in files": with the 2A03's 5 channels, none
	{
		Writer w;
		Params(w, 0xFF, 5);
		CHECK(ReadModule(w.Finish(), M, Info) && M.Expansion == EXP_NONE && M.ChannelCount() == 5);
	}
	// Patterns for tracks and channels the module doesn't have
	{
		Writer w;
		Params(w, 0, 5);
		w.Begin("PATTERNS", 5);
		PatternRows(w, 3, 0, 0, { { 0, 1, 4, NO_INST, NO_VOL, {} } }, 1);
		PatternRows(w, 0, 20, 0, { { 0, 1, 4, NO_INST, NO_VOL, {} } }, 1);
		PatternRows(w, 0, 40, 0, { { 0, 1, 4, NO_INST, NO_VOL, {} } }, 1);
		w.End();
		if (ReadModule(w.Finish(), M, Info))
			CHECK(Info.Problems.size() == 3 && M.Tracks.size() == 1 && M.Tracks[0].Patterns.size() == 5);
		else
			CHECK(false);
	}
	// Garbage that some files have in their rows: effects past FamiTracker's 32, notes past octave 7
	{
		Writer w;
		Params(w, 0, 5);
		w.Begin("PATTERNS", 5);
		PatternRows(w, 0, 0, 0, { { 0, 6, 93, 0x80, 0, { { 148, 1 } } }, { 1, 1, 2, NO_INST, NO_VOL, { { E_PORTAOFF, 0 } } } }, 1);
		PatternRows(w, 0, 3, 0, { { 0, 6, 93, NO_INST, NO_VOL, {} } }, 1);
		w.End();
		if (ReadModule(w.Finish(), M, Info) && M.Tracks.size() == 1) {
			const Track &T = M.Tracks[0];
			CHECK(T.Patterns[0][0][0].Note == NOTE_NONE && T.Patterns[0][0][0].Instrument == 0x80 && T.Patterns[0][0][0].Volume == 0);
			CHECK(T.Patterns[0][0][0].Effects[0].Letter == 0 && T.Patterns[0][0][1].Effects[0].Letter == 0);
			CHECK(T.Patterns[3][0][0].Note == ((5 + 93 * 12) & 15));		// the noise plays any octave
			CHECK(Info.Problems.size() == 2 && Has(WriteText(M), "ROW 00 : ... 80 0 ... : "));
		}
		else
			CHECK(false);
	}
	// A frame list that asks for more patterns than are read: 64 tracks of 128 frames of 13 different patterns of 256 rows
	{
		Writer w;
		Params(w, EXP_N163, 13, 0, 0, 1, 8);
		w.Begin("HEADER", 3).U8(63);
		for (int t = 0; t < 64; ++t)
			w.String("");
		for (int c = 0; c < 13; ++c) {
			w.U8(c);
			for (int t = 0; t < 64; ++t)
				w.U8(0);
		}
		w.End();
		w.Begin("FRAMES", 3);
		for (int t = 0; t < 64; ++t) {
			w.Int(128).Int(6).Int(150).Int(256);
			for (int f = 0; f < 128; ++f)
				for (int c = 0; c < 13; ++c)
					w.U8(f);
		}
		w.End();
		CHECK(ReadError(w.Finish()).find("too big") != std::string::npos);
	}
}

// Every module built here cut short at every length, and with bytes changed
// at random: read without harm (the sanitizers tell), and when one reads,
// written and studied
std::vector<std::vector<uint8_t> > AllModules()
{
	std::vector<std::vector<uint8_t> > Files;
	Files.push_back(MakeModern());
	Files.push_back(Make022());
	Writer w;
	Params(w, EXP_N163, 8, 0, 0, 1, 3);
	w.Begin("INSTRUMENTS", 6).Int(2);
	w.Int(0).U8(T_N163);
	InstSequences(w, { 0, -1, -1, -1, -1 });
	w.Int(4).Int(0).Int(1).U8(1).U8(2).U8(3).U8(4);
	w.Name("N");
	w.Int(1).U8(T_FDS);
	FdsInstrument(w, 5, 6, 7, { { 0, 0, { 1, 2 }, 0, -1, 0 }, { 0, 1, { 3 }, -1, -1, 1 }, { 0, 2, { 4 }, -1, -1, 0 } });
	w.Name("F");
	w.End();
	SequencesN163(w, { { 0, SEQ_VOLUME, { 9, 8 }, 0, -1, 0 } });
	Frames3(w, { { 6, 150, 8, { { 0, 0, 0, 0, 0, 1, 1, 1 } } } });
	w.Begin("PATTERNS", 4);
	PatternRows(w, 0, 5, 1, { { 0, 1, 3, 0, 15, { { E_ARPEGGIO, 0x47 } } } }, 1);
	w.End();
	w.Begin("COMMENTS", 1).Int(0).String("x\r\ny").End();
	Files.push_back(w.Finish());
	return Files;
}

void ReadAndUse(const std::vector<uint8_t> &Bytes, int &Readable)
{
	Module M;
	FtmInfo Info;
	std::string Error;
	if (!ParseFtm(Bytes, M, Info, Error)) {
		if (Error.empty())
			Check(false, "a module that doesn't read says why", __LINE__);
		return;
	}
	++Readable;
	WriteText(M);
	for (size_t t = 0; t < M.Tracks.size() && t < 2; ++t) {
		TrackReport Report;
		Report.Speed = M.Tracks[t].Speed;
		Report.Tempo = M.Tracks[t].Tempo;
		Report.TicksPerRow = 6;
		StudyTrack(M, int(t), Report);
	}
}

void TestBroken()
{
	const std::vector<std::vector<uint8_t> > Files = AllModules();
	int Readable = 0;
	for (size_t f = 0; f < Files.size(); ++f)
		for (size_t n = 0; n < Files[f].size(); ++n)
			ReadAndUse(std::vector<uint8_t>(Files[f].begin(), Files[f].begin() + n), Readable);
	CHECK(Readable > 0);

	// Bytes changed at random (the same each run)
	unsigned long Seed = 12345;
	auto Next = [&Seed]() {
		Seed = (Seed * 1103515245ul + 12345ul) & 0x7FFFFFFFul;
		return Seed >> 8;
	};
	Readable = 0;
	for (int i = 0; i < 30000; ++i) {
		std::vector<uint8_t> b = Files[size_t(i) % Files.size()];
		const int Changes = 1 + int(Next() % 6);
		for (int k = 0; k < Changes; ++k) {
			const size_t At = 22 + Next() % (b.size() - 22);	// past the header and version, to reach the blocks
			b[At] = Next() % 3 == 0 ? uint8_t(0xFF) : uint8_t(Next());
		}
		ReadAndUse(b, Readable);
	}
	CHECK(Readable > 1000);
}

}	// namespace

// The vibrato style and split point kept and written; a track's tempo and
// beat for the study, from its speed, tempo and highlights
void TestSettingsAndReport()
{
	auto Make = [](int Vibrato, int Split, int First, int Second, int Speed, int Tempo) {
		Writer w;
		Params(w, 0, 5, 0, 0, Vibrato, 1, Split, First, Second);
		Info(w, "Settings", "", "");
		Header(w, { "Main" }, { { 0, 0, 0, 0, 0 } });
		w.Begin("INSTRUMENTS", 6).Int(0).End();
		Sequences6(w, "SEQUENCES", std::vector<Seq>());
		Frames3(w, { { Speed, Tempo, 64, { { 0, 0, 0, 0, 0 } } } });
		w.Begin("PATTERNS", 5).End();
		return w.Finish();
	};
	Module M;
	FtmInfo Info;
	std::string Error;
	CHECK(ParseFtm(Make(0, 21, 8, 32, 3, 150), M, Info, Error));
	CHECK(M.VibratoStyle == 0 && M.SplitPoint == 21);
	CHECK(Has(WriteText(M), "VIBRATO         0\nSPLIT           21\n"));
	// Speed 3 at tempo 150 is 1200 rows a minute: a beat of 8 rows, as the highlight says, is 150 BPM
	TrackReport R = FtmTrackReport(M, Info, 0);
	CHECK(R.Written && R.Speed == 3 && R.Tempo == 150 && R.RowsPerBeat == 8 && R.BeatsPerBar == 4 && R.FirstBarRow == 0);
	CHECK(std::fabs(R.Bpm - 150.0) < 0.01 && std::fabs(R.TicksPerRow - 3.0) < 0.01);
	// A highlight every 4 rows there would make 300 BPM: a beat of 8 rows instead
	CHECK(ParseFtm(Make(1, 32, 4, 16, 3, 150), M, Info, Error));
	R = FtmTrackReport(M, Info, 0);
	CHECK(R.RowsPerBeat == 8 && std::fabs(R.Bpm - 150.0) < 0.01 && M.VibratoStyle == 1 && M.SplitPoint == 32);
	// Bars of three beats when the second highlight is three of the first
	CHECK(ParseFtm(Make(1, 32, 4, 12, 6, 150), M, Info, Error));
	R = FtmTrackReport(M, Info, 0);
	CHECK(R.RowsPerBeat == 4 && R.BeatsPerBar == 3 && std::fabs(R.Bpm - 150.0) < 0.01);
	// Slow: speed 12 is 75 BPM at 4 rows a beat; a track that isn't there gives the defaults
	CHECK(ParseFtm(Make(1, 32, 4, 16, 12, 150), M, Info, Error));
	R = FtmTrackReport(M, Info, 0);
	CHECK(R.RowsPerBeat == 4 && std::fabs(R.Bpm - 75.0) < 0.01);
	CHECK(FtmTrackReport(M, Info, 5).Written && FtmTrackReport(M, Info, 5).Bpm == 0);
}

int main(int argc, char **argv)
{
	// With a folder: the modules built here written to it, to look at
	if (argc > 1) {
		const std::vector<std::vector<uint8_t> > Files = AllModules();
		for (size_t i = 0; i < Files.size(); ++i) {
			const std::string Path = std::string(argv[1]) + "/test" + std::to_string(i + 1) + ".ftm";
			std::ofstream Out(Path.c_str(), std::ios::binary);
			Out.write(reinterpret_cast<const char *>(Files[i].data()), static_cast<std::streamsize>(Files[i].size()));
		}
	}
	TestModern();
	TestVrc6();
	TestVrc7();
	TestFds();
	TestFdsStudy();
	TestN163();
	TestMmc5();
	TestVersion022();
	TestVersion024();
	TestVersion026();
	TestVersion027();
	TestReleasePoints();
	TestOldExpansionPatterns();
	TestOldFdsInstruments();
	TestOtherFiles();
	TestBroken();
	TestSettingsAndReport();
	std::printf("%d checks, %d failed\n", g_Checks, g_Failures);
	return g_Failures ? 1 : 0;
}

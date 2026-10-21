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

#include "Module.h"

#include <cstdio>
#include <set>
#include <sstream>

namespace nsf {

namespace {

const char *const NOTE_NAMES[12] = { "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-" };
const int NOISE_CHANNEL = 3;

// FamiTracker reads the text in the Windows code page: keep to ASCII
std::string Ascii(const std::string &s, size_t MaxLength)
{
	std::string r;
	for (size_t i = 0; i < s.size() && r.size() < MaxLength; ++i) {
		const unsigned char c = static_cast<unsigned char>(s[i]);
		if (c >= 0x20 && c < 0x7F)
			r += char(c);
		else if (c >= 0x80) {
			// One '?' for each UTF-8 character
			if ((c & 0xC0) != 0x80)
				r += '?';
		}
		else
			r += ' ';
	}
	return r;
}

std::string Quote(const std::string &s, size_t MaxLength)
{
	const std::string a = Ascii(s, MaxLength);
	std::string r("\"");
	for (size_t i = 0; i < a.size(); ++i) {
		if (a[i] == '"')
			r += '"';
		r += a[i];
	}
	return r + "\"";
}

std::string Printf(const char *Format, int a)
{
	char Buffer[32];
	std::snprintf(Buffer, sizeof(Buffer), Format, a);
	return Buffer;
}

std::string CellText(const Cell &c, int Columns, bool Noise)
{
	std::string s;
	if (c.Note == NOTE_NONE)
		s = "...";
	else if (c.Note == NOTE_HALT)
		s = "---";
	else if (c.Note == NOTE_RELEASE)
		s = "===";
	else if (Noise)
		s = Printf("%X", c.Note & 0x0F) + "-#";
	else
		s = NoteName(c.Note);
	s += c.Instrument < 0 ? " .." : Printf(" %02X", c.Instrument);
	s += c.Volume < 0 ? " ." : Printf(" %X", c.Volume);
	for (int e = 0; e < Columns && e < MAX_EFFECTS; ++e) {
		if (c.Effects[e].Letter == 0)
			s += " ...";
		else
			s += std::string(" ") + c.Effects[e].Letter + Printf("%02X", c.Effects[e].Param & 0xFF);
	}
	return s;
}

void WriteMacroLine(std::ostringstream &Out, const char *Command, int Type, int Index, const Macro &M)
{
	char Head[64];
	std::snprintf(Head, sizeof(Head), "%-9s %3d %3d %3d %3d %3d :", Command, Type, Index, M.Loop, M.Release, M.Setting);
	Out << Head;
	for (size_t i = 0; i < M.Items.size(); ++i)
		Out << " " << M.Items[i];
	Out << "\n";
}

}	// namespace

std::string NoteName(int Note)
{
	if (Note < 0)
		return "...";
	return std::string(NOTE_NAMES[Note % 12]) + char('0' + std::min(7, Note / 12));
}

bool Cell::Empty() const
{
	if (Note != NOTE_NONE || Instrument >= 0 || Volume >= 0)
		return false;
	for (int e = 0; e < MAX_EFFECTS; ++e)
		if (Effects[e].Letter != 0)
			return false;
	return true;
}

int Cell::EffectCount() const
{
	int n = 0;
	for (int e = 0; e < MAX_EFFECTS; ++e)
		if (Effects[e].Letter != 0)
			n = e + 1;
	return n;
}

bool Cell::AddEffect(char Letter, int Param)
{
	for (int e = 0; e < MAX_EFFECTS; ++e) {
		if (Effects[e].Letter == 0) {
			Effects[e] = Effect(Letter, Param);
			return true;
		}
	}
	return false;
}

bool Cell::operator==(const Cell &o) const
{
	if (Note != o.Note || Instrument != o.Instrument || Volume != o.Volume)
		return false;
	for (int e = 0; e < MAX_EFFECTS; ++e)
		if (!(Effects[e] == o.Effects[e]))
			return false;
	return true;
}

int Module::ChannelCount() const
{
	int n = 5;
	if (Expansion & EXP_VRC6) n += 3;
	if (Expansion & EXP_MMC5) n += 2;
	if (Expansion & EXP_N163) n += 8;		// FamiTracker lists all eight, used or not
	if (Expansion & EXP_FDS) n += 1;
	if (Expansion & EXP_VRC7) n += 6;
	return n;
}

int Module::AddMacro(int Chip, int Type, const Macro &M)
{
	std::vector<Macro> &List = Macros[Chip][Type];
	for (size_t i = 0; i < List.size(); ++i)
		if (List[i] == M)
			return int(i);
	List.push_back(M);
	return int(List.size()) - 1;
}

std::vector<std::string> ModuleChannelNames(int Expansion, int N163Channels)
{
	std::vector<std::string> Names = { "Pulse 1", "Pulse 2", "Triangle", "Noise", "DPCM" };
	if (Expansion & EXP_VRC6) {
		Names.push_back("VRC6 Pulse 1");
		Names.push_back("VRC6 Pulse 2");
		Names.push_back("Sawtooth");
	}
	if (Expansion & EXP_MMC5) {
		Names.push_back("MMC5 Pulse 1");
		Names.push_back("MMC5 Pulse 2");
	}
	if (Expansion & EXP_N163)
		for (int i = 0; i < 8; ++i)
			Names.push_back(i < N163Channels ? "Namco " + std::to_string(i + 1) : "(unused)");
	if (Expansion & EXP_FDS)
		Names.push_back("FDS");
	if (Expansion & EXP_VRC7)
		for (int i = 0; i < 6; ++i)
			Names.push_back("FM " + std::to_string(i + 1));
	return Names;
}

std::string WriteText(const Module &M)
{
	std::ostringstream Out;
	char Line[256];
	const int Channels = M.ChannelCount();

	Out << "# FamiTracker text export 0.4.2\n";
	Out << "# Imported from an NSF by beginner-friendly FamiTracker\n\n";
	Out << "# Song information\n";
	std::snprintf(Line, sizeof(Line), "%-15s %s\n%-15s %s\n%-15s %s\n\n", "TITLE", Quote(M.Title, 31).c_str(),
		"AUTHOR", Quote(M.Author, 31).c_str(), "COPYRIGHT", Quote(M.Copyright, 31).c_str());
	Out << Line;
	Out << "# Song comment\n";
	if (M.Comment.empty())
		Out << "COMMENT \"\"\n";
	for (size_t i = 0; i < M.Comment.size(); ++i)
		Out << "COMMENT " << Quote(M.Comment[i], 250) << "\n";
	Out << "\n";

	Out << "# Global settings\n";
	std::snprintf(Line, sizeof(Line), "%-15s %d\n%-15s %d\n%-15s %d\n%-15s %d\n%-15s %d\n\n", "MACHINE", M.Machine,
		"FRAMERATE", M.FrameRate, "EXPANSION", M.Expansion, "VIBRATO", 1, "SPLIT", 32);
	Out << Line;
	if (M.Expansion & EXP_N163) {
		std::snprintf(Line, sizeof(Line), "# Namco 163 global settings\n%-15s %d\n\n", "N163CHANNELS", std::max(1, M.N163Channels));
		Out << Line;
	}

	Out << "# Macros\n";
	static const char *const MACRO_COMMANDS[3] = { "MACRO", "MACROVRC6", "MACRON163" };
	for (int Chip = 0; Chip < 3; ++Chip)
		for (int Type = 0; Type < SEQ_TYPES; ++Type)
			for (size_t i = 0; i < M.Macros[Chip][Type].size(); ++i)
				WriteMacroLine(Out, MACRO_COMMANDS[Chip], Type, int(i), M.Macros[Chip][Type][i]);
	Out << "\n";

	Out << "# DPCM samples\n";
	for (size_t s = 0; s < M.Samples.size(); ++s) {
		const std::vector<uint8_t> &Data = M.Samples[s];
		std::snprintf(Line, sizeof(Line), "%s %3d %5d %s\n", "DPCMDEF", int(s), int(Data.size()),
			Quote(s < M.SampleNames.size() ? M.SampleNames[s] : "Sample " + std::to_string(s), 60).c_str());
		Out << Line;
		for (size_t i = 0; i < Data.size(); i += 32) {
			Out << "DPCM :";
			for (size_t j = i; j < i + 32 && j < Data.size(); ++j)
				Out << Printf(" %02X", Data[j]);
			Out << "\n";
		}
	}
	Out << "\n";

	Out << "# Instruments\n";
	for (size_t i = 0; i < M.Instruments.size(); ++i) {
		const Instrument &Inst = M.Instruments[i];
		const std::string Name = Quote(Inst.Name, 60);
		// The macros' indices: an instrument's own macros are added to its chip's list by the converter
		int Seq[SEQ_TYPES];
		const int Chip = Inst.Kind == INST_VRC6 ? MACROS_VRC6 : (Inst.Kind == INST_N163 ? MACROS_N163 : MACROS_2A03);
		for (int t = 0; t < SEQ_TYPES; ++t) {
			Seq[t] = -1;
			if (Inst.Macros[t].Empty())
				continue;
			const std::vector<Macro> &List = M.Macros[Chip][t];
			for (size_t k = 0; k < List.size(); ++k)
				if (List[k] == Inst.Macros[t])
					Seq[t] = int(k);
		}
		switch (Inst.Kind) {
			case INST_2A03:
			case INST_VRC6:
				std::snprintf(Line, sizeof(Line), "%-8s %3d   %3d %3d %3d %3d %3d %s\n", Inst.Kind == INST_VRC6 ? "INSTVRC6" : "INST2A03",
					int(i), Seq[0], Seq[1], Seq[2], Seq[3], Seq[4], Name.c_str());
				Out << Line;
				for (size_t k = 0; k < Inst.Keys.size(); ++k) {
					const DpcmKey &Key = Inst.Keys[k];
					std::snprintf(Line, sizeof(Line), "%s %3d %3d %3d   %3d %3d %3d %5d %3d\n", "KEYDPCM", int(i), Key.Note / 12, Key.Note % 12,
						Key.Sample, Key.Pitch & 0x0F, Key.Loop ? 1 : 0, 0, Key.Delta);
					Out << Line;
				}
				break;
			case INST_VRC7:
				std::snprintf(Line, sizeof(Line), "%-8s %3d   %3d %02X %02X %02X %02X %02X %02X %02X %02X %s\n", "INSTVRC7", int(i), Inst.Patch,
					Inst.Regs[0], Inst.Regs[1], Inst.Regs[2], Inst.Regs[3], Inst.Regs[4], Inst.Regs[5], Inst.Regs[6], Inst.Regs[7], Name.c_str());
				Out << Line;
				break;
			case INST_FDS: {
				std::snprintf(Line, sizeof(Line), "%-8s %3d   %3d %3d %3d %3d %s\n", "INSTFDS", int(i), Inst.ModDepth > 0 ? 1 : 0,
					Inst.ModRate, Inst.ModDepth, Inst.ModDelay, Name.c_str());
				Out << Line;
				std::snprintf(Line, sizeof(Line), "%-8s %3d :", "FDSWAVE", int(i));
				Out << Line;
				for (int s = 0; s < 64; ++s)
					Out << Printf(" %2d", s < int(Inst.FdsWave.size()) ? Inst.FdsWave[s] : 0);
				Out << "\n";
				std::snprintf(Line, sizeof(Line), "%-8s %3d :", "FDSMOD", int(i));
				Out << Line;
				for (int s = 0; s < 32; ++s)
					Out << Printf(" %2d", s < int(Inst.FdsMod.size()) ? Inst.FdsMod[s] : 0);
				Out << "\n";
				for (int t = 0; t < 3; ++t) {
					const Macro &Mac = Inst.Macros[t];
					if (Mac.Empty())
						continue;
					std::snprintf(Line, sizeof(Line), "%-8s %3d %3d %3d %3d %3d :", "FDSMACRO", int(i), t, Mac.Loop, Mac.Release, Mac.Setting);
					Out << Line;
					for (size_t k = 0; k < Mac.Items.size(); ++k)
						Out << " " << Mac.Items[k];
					Out << "\n";
				}
				break;
			}
			case INST_N163: {
				std::snprintf(Line, sizeof(Line), "%-8s %3d   %3d %3d %3d %3d %3d %3d %3d %3d %s\n", "INSTN163", int(i), Seq[0], Seq[1], Seq[2],
					Seq[3], Seq[4], Inst.WaveSize, Inst.WavePos, int(Inst.Waves.size()), Name.c_str());
				Out << Line;
				for (size_t w = 0; w < Inst.Waves.size(); ++w) {
					std::snprintf(Line, sizeof(Line), "%s %3d %3d :", "N163WAVE", int(i), int(w));
					Out << Line;
					for (int s = 0; s < Inst.WaveSize; ++s)
						Out << " " << (s < int(Inst.Waves[w].size()) ? Inst.Waves[w][s] : 0);
					Out << "\n";
				}
				break;
			}
		}
	}
	Out << "\n";

	Out << "# Tracks\n\n";
	for (size_t t = 0; t < M.Tracks.size(); ++t) {
		const Track &T = M.Tracks[t];
		std::snprintf(Line, sizeof(Line), "%s %3d %3d %3d %s\n", "TRACK", T.PatternLength, T.Speed, T.Tempo, Quote(T.Name, 60).c_str());
		Out << Line;
		Out << "COLUMNS :";
		for (int c = 0; c < Channels; ++c)
			Out << " " << (c < int(T.Columns.size()) ? T.Columns[c] : 1);
		Out << "\n\n";
		for (size_t f = 0; f < T.Frames.size(); ++f) {
			Out << "ORDER " << Printf("%02X", int(f)) << " :";
			for (int c = 0; c < Channels; ++c)
				Out << " " << Printf("%02X", c < int(T.Frames[f].size()) ? T.Frames[f][c] : 0);
			Out << "\n";
		}
		Out << "\n";

		// A PATTERN block for every pattern number any channel uses
		std::set<int> Used;
		for (size_t f = 0; f < T.Frames.size(); ++f)
			for (size_t c = 0; c < T.Frames[f].size(); ++c)
				Used.insert(T.Frames[f][c]);
		const Cell Empty;
		for (std::set<int>::const_iterator p = Used.begin(); p != Used.end(); ++p) {
			Out << "PATTERN " << Printf("%02X", *p) << "\n";
			for (int r = 0; r < T.PatternLength; ++r) {
				Out << "ROW " << Printf("%02X", r);
				for (int c = 0; c < Channels; ++c) {
					const Cell *pCell = &Empty;
					if (c < int(T.Patterns.size()) && *p < int(T.Patterns[c].size()) && r < int(T.Patterns[c][*p].size()))
						pCell = &T.Patterns[c][*p][r];
					Out << " : " << CellText(*pCell, c < int(T.Columns.size()) ? T.Columns[c] : 1, c == NOISE_CHANNEL);
				}
				Out << "\n";
			}
			Out << "\n";
		}
	}
	Out << "# End of export\n";
	return Out.str();
}

}	// namespace nsf

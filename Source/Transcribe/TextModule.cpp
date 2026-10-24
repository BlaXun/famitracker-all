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

// Writes transcribed notes as a FamiTracker 0.4.6 text module, in the same
// layout CTextExport::ExportFile produces, and validates text against the
// rules of CTextExport::ImportFile.

#include "Transcribe.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <sstream>

namespace transcribe {

namespace {

const int TRIANGLE_CHANNEL = 2;
const int NOISE_CHANNEL = 3;
const int DPCM_CHANNEL = 4;
const int SAWTOOTH_CHANNEL = 7;
const int MAX_FRAMES = 128;
const int MAX_PATTERN_LENGTH = 256;

// Volume envelopes. Both hold at 10 on their release point for as long as the
// note lasts, then fade out on the note release (===). The lead swells in over
// a few frames like a whistle; the bass starts with a small accent.
const char LEAD_VOLUME[] = "11 14 15 15 14 13 12 11 10 7 4 2 0";	// release point: item 8
const int LEAD_RELEASE = 8;
const char BASS_VOLUME[] = "15 14 13 12 11 10 7 4 2 0";				// release point: item 5
const int BASS_RELEASE = 5;
const int LEAD_VOLUME_MACRO = 0;
const int BASS_VOLUME_MACRO = 4;
// Chords (a backing's arpeggios) are softer: they hold at 6 under the melody
const char CHORD_VOLUME[] = "10 9 8 8 7 7 6 6 4 2 1 0";				// release point: item 7
const int CHORD_RELEASE = 7;
const int CHORD_VOLUME_MACRO = 5;
const int CHORD_DUTY_MACRO = 1;
// and thin: 12.5% duty on either chip
const char CHORD_DUTY_2A03[] = "0";
const char CHORD_DUTY_VRC6[] = "1";

// Duty cycle of the lead: 25% on the 2A03 pulses; on the VRC6 pulses a slow
// shimmer between 4/16 and 3/16, looping
const char LEAD_DUTY_2A03[] = "1";
const char LEAD_DUTY_VRC6[] = "3 3 3 3 3 3 2 2 2 2 2 2";

// Effect letters, as EFF_CHAR in FamiTrackerTypes.h (index = effect - 1); 8 is the pan, this FamiTracker's own.
const char EFFECT_LETTERS[] = "FBDCE3\0HI047PGZ12VYQRASX\0HIJWHIJ8";
const int EFFECT_LETTER_COUNT = 33;

std::string Quote(const std::string &s)
{
	std::string r("\"");
	for (size_t i = 0; i < s.size(); ++i) {
		if (s[i] == '"')
			r += '"';
		r += s[i];
	}
	return r + "\"";
}

std::string Format(const char *fmt, int a)
{
	char buf[32];
	std::snprintf(buf, sizeof(buf), fmt, a);
	return buf;
}

std::string Padded(const char *command, int width)
{
	std::string s(command);
	while (int(s.size()) < width)
		s += ' ';
	return s;
}

struct Cell {
	std::string note = "...";
	int instrument = -1;
	int volume = -1;
	char effect = 0;	// letter, 0 = none
	int effectParam = 0;
	std::string Text() const
	{
		std::string s = note;
		s += instrument < 0 ? " .." : Format(" %02X", instrument);
		s += volume < 0 ? " ." : Format(" %X", volume);
		if (effect == 0)
			return s + " ...";
		return s + " " + effect + Format("%02X", effectParam);
	}
};

ChannelVoice VoiceOf(int channel)
{
	return channel == TRIANGLE_CHANNEL ? ChannelVoice::Triangle :
		(channel == SAWTOOTH_CHANNEL ? ChannelVoice::Sawtooth : ChannelVoice::Pulse);
}

// Drum sounds on the noise channel: noise pitch (0-F) and instrument slot.
int DrumNoisePitch(Drum d) { return d == Drum::Kick ? 0x4 : (d == Drum::Snare ? 0xA : 0xE); }
int DrumInstrument(Drum d) { return d == Drum::Kick ? 1 : (d == Drum::Snare ? 2 : 3); }

}	// namespace

int ChannelCount(Expansion expansion)
{
	return expansion == Expansion::VRC6 ? 8 : 5;
}

std::string WriteTextModule(const std::vector<GridNote> &melody, const std::vector<GridHit> &drums,
	const TextModuleOptions &options)
{
	std::vector<MelodyPart> parts;
	if (!melody.empty()) {
		MelodyPart lead;
		lead.channel = options.melodyChannel;
		lead.notes = melody;
		parts.push_back(lead);
	}
	return WriteTextModule(parts, drums, options);
}

std::string WriteTextModule(const std::vector<MelodyPart> &parts, const std::vector<GridHit> &drums,
	const TextModuleOptions &options)
{
	const int channels = ChannelCount(options.expansion);
	const int length = std::max(1, std::min(MAX_PATTERN_LENGTH, options.patternLength));
	const bool haveDrums = !drums.empty();
	bool leadOn2A03 = false, leadOnVrc6 = false;	// a melody on a pulse channel of that chip
	bool bassOn2A03 = false, bassOnVrc6 = false;	// one on the triangle or sawtooth
	bool chordsOn2A03 = false, chordsOnVrc6 = false;	// chords on a pulse channel
	for (size_t p = 0; p < parts.size(); ++p) {
		if (parts[p].notes.empty() && parts[p].cells.empty())
			continue;
		const int c = parts[p].channel;
		const bool bass = VoiceOf(c) != ChannelVoice::Pulse;
		if (parts[p].chords && !bass)
			(c >= 5 ? chordsOnVrc6 : chordsOn2A03) = true;
		else if (c >= 5)
			(bass ? bassOnVrc6 : leadOnVrc6) = true;
		else
			(bass ? bassOn2A03 : leadOn2A03) = true;
	}

	// Lay the cells out on one long row line per channel
	std::map<int, std::map<int, Cell> > cells;	// channel -> absolute row -> cell
	int lastRow = 0;
	for (size_t p = 0; p < parts.size(); ++p) {
		std::map<int, Cell> &line = cells[parts[p].channel];
		const std::vector<MelodyCell> melody = parts[p].cells.empty() ?
			MelodyCells(parts[p].notes, VoiceOf(parts[p].channel), options.grid.frameRate) : parts[p].cells;
		for (size_t i = 0; i < melody.size(); ++i) {
			const MelodyCell &m = melody[i];
			Cell &c = line[m.row];
			if (m.kind == MelodyCell::Note) {
				c.note = FamiTrackerNoteName(m.midi);
				c.instrument = parts[p].instrument;
				c.volume = m.volume;
			}
			else if (m.kind == MelodyCell::Release) {
				// Where the sound stopped: the instrument fades out over a few frames,
				// like the end of a whistle, instead of the hard cut of ---
				c.note = "===";
			}
			if (m.effect != 0) {
				c.effect = m.effect;
				c.effectParam = m.effectParam;
			}
			lastRow = std::max(lastRow, m.row);
		}
	}
	for (size_t i = 0; i < drums.size(); ++i) {
		Cell c;
		c.note = Format("%X", DrumNoisePitch(drums[i].drum)) + "-#";
		c.instrument = DrumInstrument(drums[i].drum);
		c.volume = drums[i].volume;
		cells[NOISE_CHANNEL][drums[i].row] = c;
		lastRow = std::max(lastRow, drums[i].row);
	}
	const int frames = std::max(1, lastRow / length + 1);

	std::ostringstream out;
	out << "# FamiTracker text export 0.4.2\n";
	out << "# Generated by wav2ftm (beginner-friendly FamiTracker recorder)\n\n";

	std::string title = options.title.substr(0, 31);
	out << "# Song information\n";
	out << Padded("TITLE", 15) << " " << Quote(title) << "\n";
	out << Padded("AUTHOR", 15) << " " << Quote("") << "\n";
	out << Padded("COPYRIGHT", 15) << " " << Quote("") << "\n\n";

	out << "# Song comment\n";
	{
		std::istringstream lines(options.comment);
		std::string line;
		bool any = false;
		while (std::getline(lines, line)) {
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			out << "COMMENT " << Quote(line) << "\n";
			any = true;
		}
		if (!any)
			out << "COMMENT " << Quote("") << "\n";
	}
	out << "\n";

	out << "# Global settings\n";
	out << Padded("MACHINE", 15) << " 0\n";
	out << Padded("FRAMERATE", 15) << " 0\n";
	out << Padded("EXPANSION", 15) << " " << int(options.expansion) << "\n";
	out << Padded("VIBRATO", 15) << " 1\n";
	out << Padded("SPLIT", 15) << " 32\n\n";

	// Macros: sustaining melody envelopes (see LEAD_VOLUME) and three short drum
	// envelopes
	out << "# Macros\n";
	char macro[128];
	if (leadOn2A03) {
		std::snprintf(macro, sizeof(macro), "MACRO     %3d %3d %3d %3d %3d : %s\n", 0, LEAD_VOLUME_MACRO, -1, LEAD_RELEASE, 0, LEAD_VOLUME);
		out << macro;
	}
	if (haveDrums) {
		out << "MACRO       0   1  -1  -1   0 : 15 12 8 5 3 1 0\n";
		out << "MACRO       0   2  -1  -1   0 : 14 11 9 7 5 3 2 1 0\n";
		out << "MACRO       0   3  -1  -1   0 : 10 6 3 1 0\n";
	}
	if (bassOn2A03) {
		std::snprintf(macro, sizeof(macro), "MACRO     %3d %3d %3d %3d %3d : %s\n", 0, BASS_VOLUME_MACRO, -1, BASS_RELEASE, 0, BASS_VOLUME);
		out << macro;
	}
	if (chordsOn2A03) {
		std::snprintf(macro, sizeof(macro), "MACRO     %3d %3d %3d %3d %3d : %s\n", 0, CHORD_VOLUME_MACRO, -1, CHORD_RELEASE, 0, CHORD_VOLUME);
		out << macro;
	}
	if (leadOn2A03) {
		std::snprintf(macro, sizeof(macro), "MACRO     %3d %3d %3d %3d %3d : %s\n", 4, 0, -1, -1, 0, LEAD_DUTY_2A03);
		out << macro;
	}
	if (chordsOn2A03) {
		std::snprintf(macro, sizeof(macro), "MACRO     %3d %3d %3d %3d %3d : %s\n", 4, CHORD_DUTY_MACRO, -1, -1, 0, CHORD_DUTY_2A03);
		out << macro;
	}
	if (leadOnVrc6) {
		std::snprintf(macro, sizeof(macro), "MACROVRC6 %3d %3d %3d %3d %3d : %s\n", 0, LEAD_VOLUME_MACRO, -1, LEAD_RELEASE, 0, LEAD_VOLUME);
		out << macro;
	}
	if (bassOnVrc6) {
		std::snprintf(macro, sizeof(macro), "MACROVRC6 %3d %3d %3d %3d %3d : %s\n", 0, BASS_VOLUME_MACRO, -1, BASS_RELEASE, 0, BASS_VOLUME);
		out << macro;
	}
	if (chordsOnVrc6) {
		std::snprintf(macro, sizeof(macro), "MACROVRC6 %3d %3d %3d %3d %3d : %s\n", 0, CHORD_VOLUME_MACRO, -1, CHORD_RELEASE, 0, CHORD_VOLUME);
		out << macro;
	}
	if (leadOnVrc6) {
		std::snprintf(macro, sizeof(macro), "MACROVRC6 %3d %3d %3d %3d %3d : %s\n", 4, 0, 0, -1, 0, LEAD_DUTY_VRC6);	// loops from item 0
		out << macro;
	}
	if (chordsOnVrc6) {
		std::snprintf(macro, sizeof(macro), "MACROVRC6 %3d %3d %3d %3d %3d : %s\n", 4, CHORD_DUTY_MACRO, -1, -1, 0, CHORD_DUTY_VRC6);
		out << macro;
	}
	out << "\n";

	out << "# DPCM samples\n\n";

	// Instruments, in slot order: melody parts use their own slots (0 for the
	// lead), the drums 1-3.
	std::map<int, std::string> instruments;
	for (size_t p = 0; p < parts.size(); ++p) {
		if (parts[p].notes.empty() && parts[p].cells.empty())
			continue;
		const int channel = parts[p].channel;
		const bool bass = VoiceOf(channel) != ChannelVoice::Pulse;	// no duty cycle there
		const bool chords = parts[p].chords && !bass;
		const int volume = chords ? CHORD_VOLUME_MACRO : (bass ? BASS_VOLUME_MACRO : LEAD_VOLUME_MACRO);
		const int duty = bass ? -1 : (chords ? CHORD_DUTY_MACRO : 0);
		char line[96];
		std::snprintf(line, sizeof(line), "%-8s %3d   %3d %3d %3d %3d %3d %s\n",
			channel >= 5 ? "INSTVRC6" : "INST2A03", parts[p].instrument, volume, -1, -1, -1, duty, Quote(parts[p].name).c_str());
		instruments[parts[p].instrument] = line;
	}
	if (haveDrums) {
		const char *names[3] = { "Kick", "Snare", "Hi-hat" };
		for (int d = 0; d < 3; ++d) {
			char line[96];
			std::snprintf(line, sizeof(line), "%-8s %3d   %3d %3d %3d %3d %3d %s\n",
				"INST2A03", d + 1, d + 1, -1, -1, -1, -1, Quote(names[d]).c_str());
			instruments[d + 1] = line;
		}
	}
	out << "# Instruments\n";
	for (std::map<int, std::string>::const_iterator it = instruments.begin(); it != instruments.end(); ++it)
		out << it->second;
	out << "\n";

	out << "# Tracks\n\n";
	{
		char line[96];
		std::snprintf(line, sizeof(line), "%s %3d %3d %3d %s\n", "TRACK", length, options.grid.speed, options.grid.tempo, Quote("Recording").c_str());
		out << line;
	}
	out << "COLUMNS :";
	for (int c = 0; c < channels; ++c)
		out << " 1";
	out << "\n\n";

	// Channels with content get one pattern per frame; the rest stay on empty pattern 00.
	for (int f = 0; f < frames; ++f) {
		out << "ORDER " << Format("%02X", f) << " :";
		for (int c = 0; c < channels; ++c)
			out << " " << Format("%02X", cells.count(c) && !cells[c].empty() ? f : 0);
		out << "\n";
	}
	out << "\n";

	const Cell empty;
	for (int f = 0; f < frames; ++f) {
		out << "PATTERN " << Format("%02X", f) << "\n";
		for (int r = 0; r < length; ++r) {
			const int absolute = f * length + r;
			out << "ROW " << Format("%02X", r);
			for (int c = 0; c < channels; ++c) {
				const Cell *cell = &empty;
				std::map<int, std::map<int, Cell> >::const_iterator line = cells.find(c);
				if (line != cells.end()) {
					std::map<int, Cell>::const_iterator it = line->second.find(absolute);
					if (it != line->second.end())
						cell = &it->second;
				}
				out << " : " << cell->Text();
			}
			out << "\n";
		}
		out << "\n";
	}
	out << "# End of export\n";
	return out.str();
}

// ---------------------------------------------------------------------------
// Validator: a port of the checks in CTextExport::ImportFile (FamiTracker 0.4.6)
// ---------------------------------------------------------------------------

namespace {

class Tokenizer {
public:
	explicit Tokenizer(const std::string &text) : text_(text) {}

	bool Finished() const { return pos_ >= text_.size(); }
	int Line() const { return line_; }

	void ConsumeSpace()
	{
		while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\t'))
			++pos_;
	}

	void FinishLine()
	{
		while (pos_ < text_.size() && text_[pos_] != '\n')
			++pos_;
		if (pos_ < text_.size())
			++pos_;
		++line_;
	}

	std::string ReadToken()
	{
		ConsumeSpace();
		std::string t;
		bool inQuote = false, lastQuote = false;
		while (pos_ < text_.size()) {
			const char c = text_[pos_];
			if ((c == ' ' && !inQuote) || c == '\t' || c == '\r' || c == '\n')
				break;
			if (c == '"') {
				if (!inQuote && t.empty())
					inQuote = true;
				else if (lastQuote) {
					t += c;
					lastQuote = false;
				}
				else
					lastQuote = true;
			}
			else {
				lastQuote = false;
				t += c;
			}
			++pos_;
		}
		return t;
	}

	bool IsEOL()
	{
		ConsumeSpace();
		if (Finished())
			return true;
		if (text_[pos_] == '\r' || text_[pos_] == '\n') {
			FinishLine();
			return true;
		}
		return false;
	}

	bool ReadEOL(std::string &err)
	{
		const std::string s = ReadToken();
		if (!s.empty())
			return Error(err, "expected end of line, '" + s + "' found");
		if (Finished())
			return true;
		if (text_[pos_] != '\r' && text_[pos_] != '\n')
			return Error(err, "expected end of line");
		FinishLine();
		return true;
	}

	bool ReadInt(int &value, int lo, int hi, std::string &err)
	{
		const std::string t = ReadToken();
		if (t.empty())
			return Error(err, "expected integer, no token found");
		char *end = nullptr;
		const long v = std::strtol(t.c_str(), &end, 10);
		if (end == t.c_str())
			return Error(err, "expected integer, '" + t + "' found");
		if (v < lo || v > hi)
			return Error(err, "integer " + t + " out of range");
		value = int(v);
		return true;
	}

	bool ReadHex(int &value, int lo, int hi, std::string &err)
	{
		const std::string t = ReadToken();
		if (t.empty())
			return Error(err, "expected hexadecimal, no token found");
		char *end = nullptr;
		const long v = std::strtol(t.c_str(), &end, 16);
		if (end == t.c_str())
			return Error(err, "expected hexadecimal, '" + t + "' found");
		if (v < lo || v > hi)
			return Error(err, "hexadecimal " + t + " out of range");
		value = int(v);
		return true;
	}

	bool Symbol(const char *expected, std::string &err)
	{
		const std::string t = ReadToken();
		if (t != expected)
			return Error(err, std::string("expected '") + expected + "', '" + t + "' found");
		return true;
	}

	bool Error(std::string &err, const std::string &message) const
	{
		err = "line " + std::to_string(line_) + ": " + message;
		return false;
	}

private:
	const std::string &text_;
	size_t pos_ = 0;
	int line_ = 1;
};

bool IsHexDigit(char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; }

bool CheckCell(Tokenizer &t, int channel, int effectColumns, std::string &err)
{
	const std::string note = t.ReadToken();
	if (note != "..." && note != "---" && note != "===") {
		if (note.size() != 3)
			return t.Error(err, "note column should be 3 characters wide, '" + note + "' found");
		if (channel == NOISE_CHANNEL) {
			if (!IsHexDigit(note[0]))
				return t.Error(err, "noise note must start with a hex digit, '" + note + "' found");
		}
		else {
			const char l = char(std::toupper(static_cast<unsigned char>(note[0])));
			if (l < 'A' || l > 'G')
				return t.Error(err, "unrecognized note '" + note + "'");
			const char a = note[1];
			if (a != '-' && a != '.' && a != '#' && a != '+' && a != 'b' && a != 'f')
				return t.Error(err, "unrecognized note '" + note + "'");
			if (note[2] < '0' || note[2] > '7')
				return t.Error(err, "unrecognized octave '" + note + "'");
		}
	}
	const std::string inst = t.ReadToken();
	if (inst != "..") {
		if (inst.size() != 2 || !IsHexDigit(inst[0]) || !IsHexDigit(inst[1]))
			return t.Error(err, "instrument column should be 2 hex characters, '" + inst + "' found");
		if (std::strtol(inst.c_str(), nullptr, 16) >= 64)
			return t.Error(err, "instrument '" + inst + "' is out of bounds");
	}
	const std::string vol = t.ReadToken();
	if (!(vol == "." || (vol.size() == 1 && IsHexDigit(vol[0]))))
		return t.Error(err, "unrecognized volume token '" + vol + "'");
	for (int e = 0; e < effectColumns; ++e) {
		const std::string eff = t.ReadToken();
		if (eff == "...")
			continue;
		if (eff.size() != 3)
			return t.Error(err, "effect column should be 3 characters wide, '" + eff + "' found");
		const char letter = char(std::toupper(static_cast<unsigned char>(eff[0])));
		bool known = false;
		for (int i = 0; i < EFFECT_LETTER_COUNT; ++i)
			if (EFFECT_LETTERS[i] != 0 && EFFECT_LETTERS[i] == letter)
				known = true;
		if (!known)
			return t.Error(err, "unrecognized effect '" + eff + "'");
		if (!IsHexDigit(eff[1]) || !IsHexDigit(eff[2]))
			return t.Error(err, "effect parameter must be hexadecimal, '" + eff + "' found");
	}
	return true;
}

}	// namespace

bool ValidateTextModule(const std::string &text, std::string *error)
{
	std::string err;
	Tokenizer t(text);
	int channels = ChannelCount(Expansion::None);
	int tracks = 0;
	int patternLength = 64;
	std::vector<int> effectColumns(8, 1);
	int value = 0;

	auto fail = [&](const std::string &message) {
		if (error)
			*error = message;
		return false;
	};

	while (!t.Finished()) {
		if (t.IsEOL())
			continue;
		std::string command = t.ReadToken();
		for (size_t i = 0; i < command.size(); ++i)
			command[i] = char(std::toupper(static_cast<unsigned char>(command[i])));

		if (command == "#") {
			t.FinishLine();
		}
		else if (command == "TITLE" || command == "AUTHOR" || command == "COPYRIGHT" || command == "COMMENT") {
			const std::string s = t.ReadToken();
			if (command != "COMMENT" && s.size() > 31)
				{ t.Error(err, command + " longer than 31 characters"); return fail(err); }
			if (!t.ReadEOL(err)) return fail(err);
		}
		else if (command == "MACHINE") {
			if (!t.ReadInt(value, 0, 1, err) || !t.ReadEOL(err)) return fail(err);
		}
		else if (command == "FRAMERATE") {
			if (!t.ReadInt(value, 0, 800, err) || !t.ReadEOL(err)) return fail(err);
		}
		else if (command == "EXPANSION") {
			if (!t.ReadInt(value, 0, 255, err)) return fail(err);
			if (value != 0 && value != 1)
				return fail("line " + std::to_string(t.Line()) + ": validator supports EXPANSION 0 (2A03) and 1 (VRC6) only");
			channels = ChannelCount(value == 1 ? Expansion::VRC6 : Expansion::None);
			if (!t.ReadEOL(err)) return fail(err);
		}
		else if (command == "VIBRATO") {
			if (!t.ReadInt(value, 0, 1, err) || !t.ReadEOL(err)) return fail(err);
		}
		else if (command == "SPLIT") {
			if (!t.ReadInt(value, 0, 255, err) || !t.ReadEOL(err)) return fail(err);
		}
		else if (command == "MACRO" || command == "MACROVRC6" || command == "MACRON163" || command == "MACROS5B") {
			if (!t.ReadInt(value, 0, 4, err) || !t.ReadInt(value, 0, 127, err) ||
				!t.ReadInt(value, -1, 253, err) || !t.ReadInt(value, -1, 253, err) ||
				!t.ReadInt(value, 0, 255, err) || !t.Symbol(":", err))
				return fail(err);
			int count = 0;
			while (!t.IsEOL()) {
				if (!t.ReadInt(value, -128, 127, err)) return fail(err);
				if (++count > 253) { t.Error(err, "macro overflow"); return fail(err); }
			}
		}
		else if (command == "INST2A03" || command == "INSTVRC6") {
			if (!t.ReadInt(value, 0, 63, err)) return fail(err);
			for (int s = 0; s < 5; ++s)
				if (!t.ReadInt(value, -1, 127, err)) return fail(err);
			t.ReadToken();	// name
			if (!t.ReadEOL(err)) return fail(err);
		}
		else if (command == "TRACK") {
			if (!t.ReadInt(patternLength, 0, MAX_PATTERN_LENGTH, err) || !t.ReadInt(value, 0, 255, err) ||
				!t.ReadInt(value, 0, 255, err))
				return fail(err);
			t.ReadToken();	// title
			if (!t.ReadEOL(err)) return fail(err);
			++tracks;
			std::fill(effectColumns.begin(), effectColumns.end(), 1);
		}
		else if (command == "COLUMNS") {
			if (!t.Symbol(":", err)) return fail(err);
			for (int c = 0; c < channels; ++c)
				if (!t.ReadInt(effectColumns[c], 1, 4, err)) return fail(err);
			if (!t.ReadEOL(err)) return fail(err);
		}
		else if (command == "ORDER") {
			if (tracks == 0) { t.Error(err, "ORDER before TRACK"); return fail(err); }
			if (!t.ReadHex(value, 0, MAX_FRAMES - 1, err) || !t.Symbol(":", err)) return fail(err);
			for (int c = 0; c < channels; ++c)
				if (!t.ReadHex(value, 0, 127, err)) return fail(err);
			if (!t.ReadEOL(err)) return fail(err);
		}
		else if (command == "PATTERN") {
			if (!t.ReadHex(value, 0, 127, err) || !t.ReadEOL(err)) return fail(err);
		}
		else if (command == "ROW") {
			if (tracks == 0) { t.Error(err, "no TRACK defined, cannot add ROW data"); return fail(err); }
			int row = 0;
			if (!t.ReadHex(row, 0, MAX_PATTERN_LENGTH - 1, err)) return fail(err);
			if (row >= patternLength) { t.Error(err, "row beyond the pattern length"); return fail(err); }
			for (int c = 0; c < channels; ++c) {
				if (!t.Symbol(":", err)) return fail(err);
				if (!CheckCell(t, c, effectColumns[c], err)) return fail(err);
			}
			if (!t.ReadEOL(err)) return fail(err);
		}
		else {
			return fail("line " + std::to_string(t.Line()) + ": unrecognized command '" + command + "'");
		}
	}
	if (tracks == 0)
		return fail("no TRACK found");
	return true;
}

}	// namespace transcribe

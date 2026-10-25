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

#pragma once

// NSFs built for the tests (the NSF importer's and the Claude Code
// connection's): a little 6502 program whose PLAY writes the sound
// registers from a list, a frame at a time.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace nsftest {

// A little 6502 assembler: opcodes and labels
struct Asm {
	struct Fix {
		size_t At;
		std::string Label;
		int Kind;	// 0 absolute, 1 relative branch, 2 low byte, 3 high byte
	};
	uint16_t Origin;
	std::vector<uint8_t> Bytes;
	std::map<std::string, uint16_t> Labels;
	std::vector<Fix> Fixes;

	explicit Asm(uint16_t At) : Origin(At) {}
	uint16_t Here() const { return uint16_t(Origin + Bytes.size()); }
	void Label(const std::string &Name) { Labels[Name] = Here(); }
	void Op(int Code) { Bytes.push_back(uint8_t(Code)); }
	void Op(int Code, int Arg) { Op(Code); Op(Arg & 0xFF); }
	void Abs(int Code, int Address) { Op(Code); Op(Address & 0xFF); Op((Address >> 8) & 0xFF); }
	void To(int Code, const std::string &Name, int Kind)
	{
		Op(Code);
		Fix f = { Bytes.size(), Name, Kind };
		Fixes.push_back(f);
		Op(0);
		if (Kind == 0)
			Op(0);
	}
	void Resolve()
	{
		for (size_t i = 0; i < Fixes.size(); ++i) {
			const Fix &f = Fixes[i];
			const int Target = Labels.at(f.Label);
			if (f.Kind == 0) {
				Bytes[f.At] = uint8_t(Target & 0xFF);
				Bytes[f.At + 1] = uint8_t(Target >> 8);
			}
			else if (f.Kind == 1)
				Bytes[f.At] = uint8_t(Target - int(Origin + f.At + 1));
			else
				Bytes[f.At] = uint8_t(f.Kind == 2 ? Target & 0xFF : Target >> 8);
		}
	}
};

// The register writes of a frame: (register - $4000, value)
typedef std::vector<std::pair<int, int> > Frame;

// An NSF whose PLAY writes a frame's registers each call from a list (a
// count, then register and value pairs), going back to frame LoopFrame
// after the last; with DPCM samples at $C000 (Samples, for $4012 = 0)
inline std::vector<uint8_t> BuildNsf(const std::vector<Frame> &Frames, int LoopFrame, const char *Title,
	const std::vector<uint8_t> &Samples = std::vector<uint8_t>())
{
	Asm a(0x8000);
	// INIT: the list's start, the channels on
	a.To(0xA9, "data", 2); a.Op(0x85, 0x00);	// LDA #<data, STA $00
	a.To(0xA9, "data", 3); a.Op(0x85, 0x01);	// LDA #>data, STA $01
	a.Op(0xA9, 0x0F); a.Abs(0x8D, 0x4015);		// LDA #$0F, STA $4015
	a.Op(0x60);									// RTS
	// PLAY
	a.Label("play");
	a.Op(0xA0, 0x00);							// LDY #0
	a.Op(0xB1, 0x00);							// LDA ($00),Y
	a.Op(0xC9, 0xFF);							// CMP #$FF: the end
	a.To(0xD0, "count", 1);						// BNE count
	a.To(0xA9, "loop", 2); a.Op(0x85, 0x00);
	a.To(0xA9, "loop", 3); a.Op(0x85, 0x01);
	a.Op(0xB1, 0x00);							// LDA ($00),Y
	a.Label("count");
	a.Op(0xAA);									// TAX
	a.Op(0xC8);									// INY
	a.Label("next");
	a.Op(0xE0, 0x00);							// CPX #0
	a.To(0xF0, "done", 1);						// BEQ done
	a.Op(0xB1, 0x00); a.Op(0x85, 0x02);			// the register: LDA ($00),Y, STA $02
	a.Op(0xC8);									// INY
	a.Op(0xB1, 0x00);							// the value: LDA ($00),Y
	a.Op(0x84, 0x03); a.Op(0xA4, 0x02);			// STY $03, LDY $02
	a.Abs(0x99, 0x4000);						// STA $4000,Y
	a.Op(0xA4, 0x03); a.Op(0xC8);				// LDY $03, INY
	a.Op(0xCA);									// DEX
	a.To(0x4C, "next", 0);						// JMP next
	a.Label("done");
	a.Op(0x98); a.Op(0x18);						// TYA, CLC
	a.Op(0x65, 0x00); a.Op(0x85, 0x00);			// ADC $00, STA $00
	a.Op(0xA5, 0x01); a.Op(0x69, 0x00); a.Op(0x85, 0x01);	// LDA $01, ADC #0, STA $01
	a.Op(0x60);									// RTS
	a.Label("data");
	for (size_t f = 0; f < Frames.size(); ++f) {
		if (int(f) == LoopFrame)
			a.Label("loop");
		a.Op(int(Frames[f].size()));
		for (size_t w = 0; w < Frames[f].size(); ++w) {
			a.Op(Frames[f][w].first);
			a.Op(Frames[f][w].second);
		}
	}
	a.Op(0xFF);
	if (!Samples.empty()) {
		while (a.Here() < 0xC000)
			a.Op(0);
		a.Bytes.insert(a.Bytes.end(), Samples.begin(), Samples.end());
	}
	a.Resolve();

	std::vector<uint8_t> h(128, 0);
	std::memcpy(&h[0], "NESM\x1A", 5);
	h[5] = 1;		// version
	h[6] = 1;		// songs
	h[7] = 1;		// the first
	h[8] = 0x00; h[9] = 0x80;		// load
	h[10] = 0x00; h[11] = 0x80;		// init
	h[12] = uint8_t(a.Labels.at("play") & 0xFF); h[13] = uint8_t(a.Labels.at("play") >> 8);
	std::memcpy(&h[0x0E], Title, std::min<size_t>(31, std::strlen(Title)));
	std::memcpy(&h[0x2E], "Tester", 6);
	h[0x6E] = 0x1A; h[0x6F] = 0x41;	// 16666 us
	h[0x78] = 0x20; h[0x79] = 0x4E;	// 20000 us
	h.insert(h.end(), a.Bytes.begin(), a.Bytes.end());
	return h;
}

// A pulse's period for a FamiTracker note (A-3, note 45, is 440 Hz)
inline int PulsePeriod(int Note)
{
	const double f = 440.0 * std::pow(2.0, (Note - 45) / 12.0);
	return int(std::floor(1789773.0 / (16.0 * f) - 1.0 + 0.5));
}

struct Tone {
	int Start, Length, Note;
	std::vector<int> Envelope;	// a volume a frame, the last held
	int Duty;
	bool Trigger;				// writes $4003, restarting the note
};

// Frames playing tones on Pulse 1, silent after each one
inline std::vector<Frame> PulseFrames(const std::vector<Tone> &Tones, int Total)
{
	std::vector<Frame> F(Total);
	for (size_t i = 0; i < Tones.size(); ++i) {
		const Tone &n = Tones[i];
		const int Period = PulsePeriod(n.Note);
		for (int k = 0; k < n.Length && n.Start + k < Total; ++k) {
			Frame &f = F[n.Start + k];
			const int Volume = k < int(n.Envelope.size()) ? n.Envelope[k] : n.Envelope.back();
			f.push_back(std::make_pair(0x00, (n.Duty << 6) | 0x30 | Volume));
			if (k == 0) {
				f.push_back(std::make_pair(0x01, 0x08));		// sweep off
				f.push_back(std::make_pair(0x02, Period & 0xFF));
				if (n.Trigger)
					f.push_back(std::make_pair(0x03, ((Period >> 8) & 7) | 0x08));
			}
		}
		if (n.Start + n.Length < Total)
			F[n.Start + n.Length].push_back(std::make_pair(0x00, (n.Duty << 6) | 0x30));
	}
	return F;
}

}	// namespace nsftest

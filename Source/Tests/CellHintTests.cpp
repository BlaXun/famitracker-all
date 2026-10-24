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

// Tests for the words for the cell under the cursor (CellHint.cpp)

#include <cstddef>
#include <cstdio>
#include <string>
#include "FamiTrackerTypes.h"
#include "CellHint.h"

namespace {

int g_checks = 0, g_failures = 0;

#define CHECK(x) do { ++g_checks; if (!(x)) { ++g_failures; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

bool Has(const std::string &Text, const char *pPart)
{
	const bool Found = Text.find(pPart) != std::string::npos;
	if (!Found)
		std::printf("  \"%s\" lacks \"%s\"\n", Text.c_str(), pPart);
	return Found;
}

hint_cell_t Note(int Note, int Octave, int Instrument = MAX_INSTRUMENTS, int Volume = MAX_VOLUME)
{
	hint_cell_t Cell;
	Cell.Note = Note;
	Cell.Octave = Octave;
	Cell.Instrument = Instrument;
	Cell.Volume = Volume;
	return Cell;
}

void TestNotes()
{
	CHECK(HintNoteName(Cs, 4, HINT_PULSE) == "C#4");
	CHECK(HintNoteName(C, 0, HINT_NOISE) == "0-#");
	CHECK(HintNoteName(A, 0, HINT_NOISE) == "9-#");
	CHECK(HintNoteName(Cs, 1, HINT_NOISE) == "D-#");	// pitch 13
	CHECK(HintNoteName(RELEASE, 0, HINT_PULSE) == "===" && HintNoteName(HALT, 0, HINT_PULSE) == "---");

	const std::string Lead = DescribeCell(Note(Cs, 4, 2, 8), 0, HINT_PULSE, true, "Bass");
	CHECK(Has(Lead, "C#4: C sharp in octave 4 (C-3 is middle C)"));
	CHECK(Has(Lead, "with instrument 02 \"Bass\""));
	CHECK(Has(Lead, "at volume 8 of F."));
	CHECK(Has(DescribeCell(Note(C, 3), 0, HINT_TRIANGLE, false, ""), "which the triangle plays an octave lower"));
	CHECK(Has(DescribeCell(Note(C, 3), 0, HINT_PULSE, false, ""), "with the instrument the channel has."));
	CHECK(Has(DescribeCell(Note(C, 3, 5), 0, HINT_PULSE, false, ""), "with instrument 05, which the song doesn't have."));
	// An instrument for another chip
	CHECK(DescribeCell(Note(C, 3, 6), 0, HINT_PULSE, true, "Warm pad", 0x20, "VRC6", "2A03") ==
		"C-3: its instrument, 06 \"Warm pad\", is for the VRC6, so it shows in red: this channel needs a 2A03 instrument.");
	CHECK(DescribeCell(Note(C, 3, 2), 0, HINT_PULSE, true, "Bass", 0x20, "2A03", "2A03").find("needs") == std::string::npos);
	CHECK(DescribeCell(Note(C, 3, 2), 0, HINT_PULSE, true, "Bass", 0x20, "VRC6", "").find("needs") == std::string::npos);
	CHECK(Has(DescribeCell(Note(E, 0, 3), 0, HINT_NOISE, true, "Kick"), "4-#: noise at pitch 4 of 0-F"));
	CHECK(Has(DescribeCell(Note(RELEASE, 0), 0, HINT_PULSE, false, ""), "a release"));
	CHECK(Has(DescribeCell(Note(HALT, 0), 0, HINT_PULSE, false, ""), "a halt"));
	CHECK(Has(DescribeCell(hint_cell_t(), 0, HINT_PULSE, false, ""), "No note"));
}

void TestColumns()
{
	CHECK(Has(DescribeCell(Note(C, 3, 2), 1, HINT_PULSE, true, "Bass"), "Instrument 02: \"Bass\""));
	CHECK(Has(DescribeCell(Note(C, 3, 0x3F), 1, HINT_PULSE, false, ""), "Instrument 3F isn't in the song"));
	CHECK(Has(DescribeCell(Note(C, 3, 6), 1, HINT_SAWTOOTH, true, "Bright lead", 0x20, "2A03", "VRC6"),
		"Instrument 06: \"Bright lead\", for the 2A03, so it shows in red: this channel needs a VRC6 instrument."));
	CHECK(Has(DescribeCell(Note(C, 3), 1, HINT_PULSE, false, ""), "No instrument here"));
	CHECK(Has(DescribeCell(Note(C, 3, 2, 0xA), 2, HINT_PULSE, true, "x"), "Volume A of F"));
	CHECK(Has(DescribeCell(Note(C, 3, 2, 0), 2, HINT_PULSE, true, "x"), "silent"));
	CHECK(Has(DescribeCell(Note(C, 3), 2, HINT_PULSE, true, "x"), "No volume here"));
	hint_cell_t Cell;
	CHECK(Has(DescribeCell(Cell, 3, HINT_PULSE, false, ""), "No effect here"));
	Cell.Effect[2] = EF_VIBRATO;
	Cell.Param[2] = 0x52;
	CHECK(Has(DescribeCell(Cell, 5, HINT_PULSE, false, ""), "452: vibrato"));
	CHECK(Has(DescribeCell(Cell, 4, HINT_PULSE, false, ""), "No effect here"));
	CHECK(DescribeCell(Cell, 9, HINT_PULSE, false, "").empty());
}

void TestEffects()
{
	CHECK(Has(DescribeEffect(EF_SPEED, 6, HINT_PULSE), "F06: speed 6"));
	CHECK(Has(DescribeEffect(EF_SPEED, 0x96, HINT_PULSE), "F96: tempo 150"));
	CHECK(Has(DescribeEffect(EF_SPEED, 0x10, HINT_PULSE, 0x10), "tempo 16"));		// another split point
	CHECK(Has(DescribeEffect(EF_JUMP, 0, HINT_PULSE), "B00: after this row the song jumps to frame 00"));
	CHECK(Has(DescribeEffect(EF_SKIP, 0x10, HINT_PULSE), "D10:"));
	CHECK(Has(DescribeEffect(EF_HALT, 0, HINT_PULSE), "C00: the song stops"));
	CHECK(Has(DescribeEffect(EF_PORTAMENTO, 0, HINT_PULSE), "300: stops the glide"));
	CHECK(Has(DescribeEffect(EF_PORTAMENTO, 0x10, HINT_PULSE), "speed 16"));
	CHECK(Has(DescribeEffect(EF_ARPEGGIO, 0x47, HINT_PULSE), "047: an arpeggio"));
	CHECK(Has(DescribeEffect(EF_ARPEGGIO, 0x47, HINT_PULSE), "a major chord"));
	CHECK(Has(DescribeEffect(EF_ARPEGGIO, 0x37, HINT_PULSE), "a minor chord"));
	CHECK(Has(DescribeEffect(EF_ARPEGGIO, 0x0C, HINT_PULSE), "an octave"));
	CHECK(Has(DescribeEffect(EF_ARPEGGIO, 0x00, HINT_PULSE), "stops the arpeggio"));
	CHECK(Has(DescribeEffect(EF_VIBRATO, 0x52, HINT_PULSE), "speed 5 and depth 2: gentle"));
	CHECK(Has(DescribeEffect(EF_VIBRATO, 0x6A, HINT_PULSE), "strong"));
	CHECK(Has(DescribeEffect(EF_VIBRATO, 0x50, HINT_PULSE), "stops the vibrato"));
	CHECK(Has(DescribeEffect(EF_TREMOLO, 0x45, HINT_PULSE), "tremolo"));
	CHECK(Has(DescribeEffect(EF_PITCH, 0x80, HINT_PULSE), "in tune"));
	CHECK(Has(DescribeEffect(EF_PITCH, 0x82, HINT_PULSE), "higher"));
	CHECK(Has(DescribeEffect(EF_PITCH, 0x7E, HINT_PULSE), "lower"));
	CHECK(Has(DescribeEffect(EF_DELAY, 3, HINT_PULSE), "G03: this row plays 3 ticks late"));
	CHECK(Has(DescribeEffect(EF_DELAY, 1, HINT_PULSE), "G01: this row plays 1 tick late"));
	CHECK(Has(DescribeEffect(EF_PORTA_UP, 0x20, HINT_PULSE), "120: the pitch slides up"));
	CHECK(Has(DescribeEffect(EF_PORTA_DOWN, 0, HINT_PULSE), "200: stops the slide"));
	CHECK(Has(DescribeEffect(EF_DUTY_CYCLE, 1, HINT_PULSE), "V01: the pulse's shape 25%"));
	CHECK(Has(DescribeEffect(EF_DUTY_CYCLE, 5, HINT_VRC6_PULSE), "5 of 0-7"));
	CHECK(Has(DescribeEffect(EF_DUTY_CYCLE, 1, HINT_NOISE), "metallic"));
	CHECK(Has(DescribeEffect(EF_SLIDE_UP, 0x37, HINT_PULSE), "Q37: the note slides up 7 semitones at speed 3"));
	CHECK(Has(DescribeEffect(EF_SLIDE_DOWN, 0x21, HINT_PULSE), "R21: the note slides down 1 semitone at speed 2"));
	CHECK(Has(DescribeEffect(EF_VOLUME_SLIDE, 0x04, HINT_PULSE), "A04: the volume falls, at speed 4 (a fade out)"));
	CHECK(Has(DescribeEffect(EF_VOLUME_SLIDE, 0x30, HINT_PULSE), "a fade in"));
	CHECK(Has(DescribeEffect(EF_NOTE_CUT, 2, HINT_PULSE), "S02: the note stops after 2 ticks"));
	CHECK(Has(DescribeEffect(EF_SAMPLE_OFFSET, 2, HINT_DPCM), "128 bytes"));
	CHECK(Has(DescribeEffect(EF_FDS_MOD_DEPTH, 0x10, HINT_OTHER), "H10: the FDS modulation"));
	CHECK(Has(DescribeEffect(EF_DPCM_PITCH, 0x0F, HINT_DPCM), "W0F"));
	CHECK(Has(DescribeEffect(EF_PAN, 0x00, HINT_PULSE), "800: the channel plays on the left only"));
	CHECK(Has(DescribeEffect(EF_PAN, 0x80, HINT_DPCM), "in the middle"));
	CHECK(Has(DescribeEffect(EF_PAN, 0xC0, HINT_PULSE), "toward the right"));
	// Every effect says something, starting with its code
	for (int e = EF_SPEED; e < EF_COUNT; ++e) {
		const std::string Text = DescribeEffect(e, 0x12, HINT_PULSE);
		CHECK(!Text.empty());
		if (EFF_CHAR[e - 1] != 0)
			CHECK(Text[0] == EFF_CHAR[e - 1]);
	}
}

}	// namespace

int main()
{
	TestNotes();
	TestColumns();
	TestEffects();
	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}

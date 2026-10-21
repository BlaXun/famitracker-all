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

// The cell under the cursor in words (see CellHint.h)

#include <cstddef>
#include <cstdarg>
#include <cstdio>
#include <string>
#include "FamiTrackerTypes.h"
#include "CellHint.h"

namespace {

const char *const NOTE_TEXT[] = { "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-" };
const char *const NOTE_WORDS[] = { "C", "C sharp", "D", "D sharp", "E", "F", "F sharp", "G", "G sharp", "A", "A sharp", "B" };

std::string Format(const char *pFormat, ...)
{
	char Buffer[512];
	va_list Args;
	va_start(Args, pFormat);
	std::vsnprintf(Buffer, sizeof(Buffer), pFormat, Args);
	va_end(Args);
	return Buffer;
}

char EffectLetter(int Effect)
{
	if (Effect < 1 || Effect >= EF_COUNT || Effect - 1 >= static_cast<int>(sizeof(EFF_CHAR)))
		return 0;
	return EFF_CHAR[Effect - 1];
}

// "gentle", "medium" or "strong", for a vibrato's or tremolo's depth
const char *Strength(int Depth)
{
	return Depth <= 3 ? "gentle" : (Depth <= 7 ? "medium" : "strong");
}

// A chord's name from an arpeggio's two steps, empty if it's none of the usual ones
std::string ChordName(int x, int y)
{
	struct chord_t { int x, y; const char *Name; };
	static const chord_t CHORDS[] = {
		{ 4, 7, "a major chord" }, { 3, 7, "a minor chord" }, { 7, 0, "a power chord (a fifth)" },
		{ 7, 12, "a power chord with the octave" }, { 12, 0, "an octave" }, { 5, 7, "a sus4 chord" }, { 2, 7, "a sus2 chord" },
		{ 3, 6, "a diminished chord" }, { 4, 8, "an augmented chord" }, { 4, 0, "a major third" }, { 3, 0, "a minor third" },
		{ 0, 12, "an octave" },
	};
	for (size_t i = 0; i < sizeof(CHORDS) / sizeof(CHORDS[0]); ++i)
		if (CHORDS[i].x == x && CHORDS[i].y == y)
			return CHORDS[i].Name;
	return std::string();
}

// "1 tick", "3 ticks"
std::string Count(int Number, const char *pThing)
{
	return Format("%d %s%s", Number, pThing, Number == 1 ? "" : "s");
}

std::string Code(int Effect, int Param)
{
	return Format("%c%02X", EffectLetter(Effect), Param & 0xFF);
}

}	// namespace

hint_cell_t::hint_cell_t() : Note(NONE), Octave(0), Instrument(MAX_INSTRUMENTS), Volume(MAX_VOLUME)
{
	for (int i = 0; i < 4; ++i) {
		Effect[i] = EF_NONE;
		Param[i] = 0;
	}
}

std::string HintNoteName(int Note, int Octave, hint_channel_t Kind)
{
	if (Note == RELEASE)
		return "===";
	if (Note == HALT)
		return "---";
	if (Note < C || Note > B)
		return "...";
	if (Kind == HINT_NOISE) {
		const int Pitch = (Octave * 12 + Note - 1) & 0x0F;
		return Format("%X-#", Pitch);
	}
	return Format("%s%d", NOTE_TEXT[Note - 1], Octave);
}

std::string DescribeEffect(int Effect, int Param, hint_channel_t Kind, int SplitPoint)
{
	const int x = (Param >> 4) & 0x0F, y = Param & 0x0F;
	const std::string Head = Code(Effect, Param);
	switch (Effect) {
	case EF_SPEED:
		if (Param == 0)
			return Head + ": does nothing (speeds start at 1)";
		if (Param < SplitPoint)
			return Head + Format(": speed %d from here on: the higher the speed, the slower the song", Param);
		return Head + Format(": tempo %d from here on: the higher the tempo, the faster the song", Param);
	case EF_JUMP:
		return Head + Format(": after this row the song jumps to frame %02X; jumping back makes it loop", Param);
	case EF_SKIP:
		return Head + Format(": after this row the song goes on to the next frame, starting at its row %02X", Param);
	case EF_HALT:
		return Head + ": the song stops after this row (the end of a jingle or a sound effect)";
	case EF_VOLUME:
		return Head + Format(": sets the volume to %X, an old way of doing it (the volume column is the usual one)", Param & 0x0F);
	case EF_PORTAMENTO:
		if (Param == 0)
			return Head + ": stops the glide between notes";
		return Head + Format(": each new note glides from the one before, at speed %d (higher is faster); 300 stops it", Param);
	case EF_SWEEPUP:
		return Head + Format(": the pulse's hardware sweep moves the pitch up (speed %d, shift %d)", x, y);
	case EF_SWEEPDOWN:
		return Head + Format(": the pulse's hardware sweep moves the pitch down (speed %d, shift %d)", x, y);
	case EF_ARPEGGIO: {
		if (Param == 0)
			return Head + ": stops the arpeggio";
		const std::string Chord = ChordName(x, y);
		return Head + Format(": an arpeggio, the note and %d and %d semitones above it in quick turns", x, y) +
			(Chord.empty() ? std::string() : ": " + Chord) + "; 000 stops it";
	}
	case EF_VIBRATO:
		if (x == 0 || y == 0)
			return Head + ": stops the vibrato";
		return Head + Format(": vibrato, the pitch wobbling at speed %d and depth %d: %s; 400 stops it", x, y, Strength(y));
	case EF_TREMOLO:
		if (x == 0 || y == 0)
			return Head + ": stops the tremolo";
		return Head + Format(": tremolo, the volume wobbling at speed %d and depth %d: %s; 700 stops it", x, y, Strength(y));
	case EF_PITCH:
		if (Param == 0x80)
			return Head + ": fine pitch in tune (the usual)";
		return Head + Format(": fine pitch a little %s (P80 is in tune)", Param > 0x80 ? "higher" : "lower");
	case EF_DELAY:
		if (Param == 0)
			return Head + ": does nothing";
		return Head + ": this row plays " + Count(Param, "tick") + " late";
	case EF_DAC:
		return Head + Format(": sets the DPCM channel's level to %02X", Param & 0x7F);
	case EF_PORTA_UP:
		if (Param == 0)
			return Head + ": stops the slide";
		return Head + Format(": the pitch slides up and keeps going, at speed %d; 100 stops it", Param);
	case EF_PORTA_DOWN:
		if (Param == 0)
			return Head + ": stops the slide";
		return Head + Format(": the pitch slides down and keeps going, at speed %d; 200 stops it", Param);
	case EF_DUTY_CYCLE:
		if (Kind == HINT_NOISE)
			return Head + (Param == 0 ? ": the noise as a hiss (the usual)" : ": the noise as a metallic, buzzing tone");
		if (Kind == HINT_PULSE) {
			static const char *const DUTIES[] = { "12.5%: thin and nasal", "25%: the classic NES sound", "50%: round and hollow",
				"75%: like 25%" };
			return Head + ": the pulse's shape " + DUTIES[Param & 3];
		}
		if (Kind == HINT_VRC6_PULSE)
			return Head + Format(": the pulse's shape, %d of 0-7: thin at 0, round and hollow at 7", Param & 7);
		return Head + ": the channel's timbre";
	case EF_SAMPLE_OFFSET:
		return Head + Format(": the DPCM sample starts %d bytes in", Param * 64);
	case EF_SLIDE_UP:
		return Head + ": the note slides up " + Count(y, "semitone") + Format(" at speed %d", x);
	case EF_SLIDE_DOWN:
		return Head + ": the note slides down " + Count(y, "semitone") + Format(" at speed %d", x);
	case EF_VOLUME_SLIDE:
		if (Param == 0)
			return Head + ": stops the volume slide";
		if (y == 0)
			return Head + Format(": the volume rises, at speed %d (a fade in)", x);
		if (x == 0)
			return Head + Format(": the volume falls, at speed %d (a fade out); A00 stops it", y);
		return Head + Format(": the volume slides, up at speed %d and down at %d", x, y);
	case EF_NOTE_CUT:
		return Head + ": the note stops after " + Count(Param, "tick");
	case EF_RETRIGGER:
		return Head + ": the DPCM sample plays again every " + Count(Param + 1, "tick");
	case EF_FDS_MOD_DEPTH:
		return Head + Format(": the FDS modulation's depth, %d", Param & 0x3F);
	case EF_FDS_MOD_SPEED_HI:
		return Head + ": the FDS modulation's speed (the high part)";
	case EF_FDS_MOD_SPEED_LO:
		return Head + ": the FDS modulation's speed (the low part)";
	case EF_DPCM_PITCH:
		return Head + Format(": the DPCM sample plays at pitch %X", Param & 0x0F);
	case EF_SUNSOFT_ENV_LO:
	case EF_SUNSOFT_ENV_HI:
		return Head + ": the 5B envelope's speed";
	case EF_SUNSOFT_ENV_TYPE:
		return Head + ": the 5B envelope's shape";
	default:
		return EffectLetter(Effect) != 0 ? Head + ": an effect" : std::string("An effect FamiTracker doesn't use");
	}
}

std::string DescribeCell(const hint_cell_t &Cell, int Column, hint_channel_t Kind, bool InstrumentExists,
	const std::string &InstrumentName, int SplitPoint)
{
	const bool HasInstrument = Cell.Instrument >= 0 && Cell.Instrument < MAX_INSTRUMENTS;
	if (Column == 0) {
		if (Cell.Note == RELEASE)
			return "===: a release. The note fades out the way its instrument ends.";
		if (Cell.Note == HALT)
			return "---: a halt. The note stops here.";
		if (Cell.Note < C || Cell.Note > B)
			return "No note on this row.";
		std::string Text = HintNoteName(Cell.Note, Cell.Octave, Kind) + ": ";
		if (Kind == HINT_NOISE)
			Text += Format("noise at pitch %X of 0-F (the higher, the brighter)", (Cell.Octave * 12 + Cell.Note - 1) & 0x0F);
		else if (Kind == HINT_DPCM)
			Text += "plays the DPCM sample its instrument gives this note";
		else {
			Text += Format("%s in octave %d (C-3 is middle C)", NOTE_WORDS[Cell.Note - 1], Cell.Octave);
			if (Kind == HINT_TRIANGLE)
				Text += ", which the triangle plays an octave lower";
		}
		if (!HasInstrument)
			Text += ", with the instrument the channel has";
		else if (InstrumentExists)
			Text += Format(", with instrument %02X \"", Cell.Instrument) + InstrumentName + "\"";
		else
			Text += Format(", with instrument %02X, which the song doesn't have", Cell.Instrument);
		if (Cell.Volume >= 0 && Cell.Volume < MAX_VOLUME)
			Text += Format(", at volume %X of F", Cell.Volume);
		return Text + ".";
	}
	if (Column == 1) {
		if (!HasInstrument)
			return "No instrument here: a note keeps the instrument its channel has.";
		if (InstrumentExists)
			return Format("Instrument %02X: \"", Cell.Instrument) + InstrumentName + "\". The instrument list chooses the one new notes get.";
		return Format("Instrument %02X isn't in the song, so it shows in red: add it in the instrument list, or choose another.",
			Cell.Instrument);
	}
	if (Column == 2) {
		if (Cell.Volume < 0 || Cell.Volume >= MAX_VOLUME)
			return "No volume here: the channel keeps the volume it has (F, the loudest, to start with).";
		if (Cell.Volume == 0)
			return "Volume 0: silent.";
		return Format("Volume %X of F (0 is silent, F the loudest).", Cell.Volume);
	}
	const int Index = Column - 3;
	if (Index < 0 || Index > 3)
		return std::string();
	if (Cell.Effect[Index] == EF_NONE)
		return "No effect here. Effects change how notes play: 4xy a vibrato, 0xy a chord as an arpeggio, Axy a fade, "
			"Bxx a loop. Help > Effect table lists them all.";
	return DescribeEffect(Cell.Effect[Index], Cell.Param[Index], Kind, SplitPoint) + ".";
}

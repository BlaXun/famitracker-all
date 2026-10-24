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

#include "Picture.h"
#include "Sections.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace ftapi {

namespace {

// The palette: plain colors first, then each channel color in shades
enum {
	MARGIN, WHITE_KEY, BLACK_KEY, BEAT_LINE, BAR_LINE, FRAME_LINE, TEXT, OCTAVE_LINE, HATCH, LOOP_MARK, END_MARK, STRIP,
	PLAIN_COLORS
};
const Rgb PLAIN[PLAIN_COLORS] = {
	{ 255, 255, 255 }, { 247, 247, 247 }, { 232, 232, 238 }, { 214, 214, 222 }, { 150, 150, 162 }, { 70, 70, 82 },
	{ 25, 25, 25 }, { 194, 194, 206 }, { 200, 200, 204 }, { 0, 155, 70 }, { 210, 30, 30 }, { 240, 240, 244 },
};

// Volume 13-15, 9-12, 5-8, 0-4; a chord's other notes (arpeggio) and a drum's
// sound after its hit; the outline
enum { SHADE_FULL, SHADE_LOUD, SHADE_MEDIUM, SHADE_QUIET, SHADE_GHOST, SHADE_DARK, SHADES };
const double SHADE_STRENGTH[SHADES - 1] = { 1.0, 0.75, 0.55, 0.4, 0.28 };

struct NamedColor {
	const char *Name;
	Rgb Color;
};
const NamedColor CHANNEL_COLORS[] = {
	{ "red", { 214, 39, 40 } }, { "blue", { 31, 119, 180 } }, { "green", { 44, 160, 44 } }, { "orange", { 255, 127, 14 } },
	{ "purple", { 148, 103, 189 } }, { "teal", { 23, 170, 190 } }, { "pink", { 227, 119, 194 } }, { "olive", { 160, 160, 30 } },
	{ "brown", { 140, 86, 75 } }, { "gray", { 120, 120, 120 } },
};
const int COLOR_COUNT = static_cast<int>(sizeof(CHANNEL_COLORS) / sizeof(CHANNEL_COLORS[0]));

int Shade(int Color, int Which)
{
	return PLAIN_COLORS + (Color % COLOR_COUNT) * SHADES + Which;
}

int VolumeShade(int Volume)
{
	return Volume >= 13 ? SHADE_FULL : Volume >= 9 ? SHADE_LOUD : Volume >= 5 ? SHADE_MEDIUM : SHADE_QUIET;
}

// A 5x7 font for bar numbers, note names and strip labels
struct Glyph {
	char Character;
	const char *Rows[7];
};
const Glyph FONT[] = {
	{ '0', { ".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###." } },
	{ '1', { "..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###." } },
	{ '2', { ".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####" } },
	{ '3', { "#####", "...#.", "..#..", "...#.", "....#", "#...#", ".###." } },
	{ '4', { "...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#." } },
	{ '5', { "#####", "#....", "####.", "....#", "....#", "#...#", ".###." } },
	{ '6', { "..##.", ".#...", "#....", "####.", "#...#", "#...#", ".###." } },
	{ '7', { "#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..." } },
	{ '8', { ".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###." } },
	{ '9', { ".###.", "#...#", "#...#", ".####", "....#", "...#.", ".##.." } },
	{ 'A', { ".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#" } },
	{ 'B', { "####.", "#...#", "#...#", "####.", "#...#", "#...#", "####." } },
	{ 'C', { ".###.", "#...#", "#....", "#....", "#....", "#...#", ".###." } },
	{ 'D', { "###..", "#..#.", "#...#", "#...#", "#...#", "#..#.", "###.." } },
	{ 'E', { "#####", "#....", "#....", "####.", "#....", "#....", "#####" } },
	{ 'F', { "#####", "#....", "#....", "####.", "#....", "#....", "#...." } },
	{ 'G', { ".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".####" } },
	{ 'H', { "#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#" } },
	{ 'I', { ".###.", "..#..", "..#..", "..#..", "..#..", "..#..", ".###." } },
	{ 'J', { "..###", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##.." } },
	{ 'K', { "#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#" } },
	{ 'L', { "#....", "#....", "#....", "#....", "#....", "#....", "#####" } },
	{ 'M', { "#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#" } },
	{ 'N', { "#...#", "#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#" } },
	{ 'O', { ".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###." } },
	{ 'P', { "####.", "#...#", "#...#", "####.", "#....", "#....", "#...." } },
	{ 'Q', { ".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#" } },
	{ 'R', { "####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#" } },
	{ 'S', { ".####", "#....", "#....", ".###.", "....#", "....#", "####." } },
	{ 'T', { "#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.." } },
	{ 'U', { "#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###." } },
	{ 'V', { "#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.." } },
	{ 'W', { "#...#", "#...#", "#...#", "#.#.#", "#.#.#", "#.#.#", ".#.#." } },
	{ 'X', { "#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#" } },
	{ 'Y', { "#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.." } },
	{ 'Z', { "#####", "....#", "...#.", "..#..", ".#...", "#....", "#####" } },
	{ '#', { ".#.#.", ".#.#.", "#####", ".#.#.", "#####", ".#.#.", ".#.#." } },
	{ '-', { ".....", ".....", ".....", "#####", ".....", ".....", "....." } },
};

const Glyph *FindGlyph(char Character)
{
	for (size_t i = 0; i < sizeof(FONT) / sizeof(FONT[0]); ++i)
		if (FONT[i].Character == Character)
			return &FONT[i];
	return 0;
}

const int GLYPH_WIDTH = 5, GLYPH_HEIGHT = 7, GLYPH_ADVANCE = 6;

class Canvas {
public:
	Canvas(int Width, int Height) : m_Width(Width), m_Height(Height), m_Pixels(static_cast<size_t>(Width) * static_cast<size_t>(Height), MARGIN) {}

	void Fill(int x0, int y0, int x1, int y1, int Color)
	{
		x0 = std::max(x0, 0);
		y0 = std::max(y0, 0);
		x1 = std::min(x1, m_Width);
		y1 = std::min(y1, m_Height);
		for (int y = y0; y < y1; ++y)
			for (int x = x0; x < x1; ++x)
				m_Pixels[static_cast<size_t>(y) * static_cast<size_t>(m_Width) + static_cast<size_t>(x)] = static_cast<unsigned char>(Color);
	}

	// Every few pixels on the diagonals, over what's there
	void Hatch(int x0, int y0, int x1, int y1, int Color)
	{
		x0 = std::max(x0, 0);
		y0 = std::max(y0, 0);
		x1 = std::min(x1, m_Width);
		y1 = std::min(y1, m_Height);
		for (int y = y0; y < y1; ++y)
			for (int x = x0; x < x1; ++x)
				if ((x + y) % 6 == 0)
					m_Pixels[static_cast<size_t>(y) * static_cast<size_t>(m_Width) + static_cast<size_t>(x)] = static_cast<unsigned char>(Color);
	}

	void Outline(int x0, int y0, int x1, int y1, int Color, bool Left)
	{
		Fill(x0, y0, x1, y0 + 1, Color);
		Fill(x0, y1 - 1, x1, y1, Color);
		Fill(x1 - 1, y0, x1, y1, Color);
		if (Left)
			Fill(x0, y0, x0 + 1, y1, Color);
	}

	int TextWidth(const std::string &Text, int Scale) const
	{
		return Text.empty() ? 0 : (static_cast<int>(Text.size()) * GLYPH_ADVANCE - 1) * Scale;
	}

	void Text(int x, int y, const std::string &Text, int Scale, int Color)
	{
		for (size_t i = 0; i < Text.size(); ++i) {
			const Glyph *pGlyph = FindGlyph(Text[i]);
			if (pGlyph != 0)
				for (int Row = 0; Row < GLYPH_HEIGHT; ++Row)
					for (int Column = 0; Column < GLYPH_WIDTH; ++Column)
						if (pGlyph->Rows[Row][Column] == '#')
							Fill(x + Column * Scale, y + Row * Scale, x + (Column + 1) * Scale, y + (Row + 1) * Scale, Color);
			x += GLYPH_ADVANCE * Scale;
		}
	}

	std::vector<unsigned char> &Pixels() { return m_Pixels; }

private:
	int m_Width, m_Height;
	std::vector<unsigned char> m_Pixels;
};

bool IsBlackKey(int Pitch)
{
	const int Class = ((Pitch % 12) + 12) % 12;
	return Class == 1 || Class == 3 || Class == 6 || Class == 8 || Class == 10;
}

bool IsStrip(int Kind)
{
	return Kind == KIND_NOISE || Kind == KIND_DPCM;
}

// A note's pan (stereo, 8xx): an L at its left end or an R at its right end, or a bar there when the letter doesn't fit;
// nothing in the middle
void MarkPan(Canvas &C, const NoteInfo &Note, int x0, int y0, int x1, int y1)
{
	if (Note.Pan == PAN_CENTER)
		return;
	const bool Right = Note.Pan > PAN_CENTER;
	if (y1 - y0 >= GLYPH_HEIGHT + 1 && x1 - x0 >= GLYPH_WIDTH + 4) {
		const int y = y0 + (y1 - y0 - GLYPH_HEIGHT) / 2;
		C.Text(Right ? x1 - 2 - GLYPH_WIDTH : x0 + 2, y, Right ? "R" : "L", 1, TEXT);
	}
	else if (x1 - x0 >= 3)
		C.Fill(Right ? x1 - 3 : x0 + 1, y0, Right ? x1 - 1 : x0 + 3, y1, TEXT);
}

// "C-4" without the dash when there's no room isn't needed: C labels only
std::string OctaveLabel(int Pitch)
{
	return PitchName(Pitch);
}

// The arpeggio (0xy) in effect over a note's rows: (first row, rows, param)
struct ArpeggioSpan {
	int Row, Rows, Param;
};

// The arpeggio carries on from note to note until 000, as FamiTracker plays it
std::vector<ArpeggioSpan> Arpeggios(const std::vector<NoteInfo> &Notes)
{
	std::vector<ArpeggioSpan> Spans;
	int Param = 0;
	for (size_t i = 0; i < Notes.size(); ++i) {
		const NoteInfo &Note = Notes[i];
		std::vector<std::pair<int, int> > Changes;	// row offset, param
		for (size_t e = 0; e < Note.Effects.size(); ++e)
			if (Note.Effects[e].second.first == EFF_ARPEGGIO)
				Changes.push_back(std::make_pair(Note.Effects[e].first, Note.Effects[e].second.second));
		std::stable_sort(Changes.begin(), Changes.end(),
			[](const std::pair<int, int> &a, const std::pair<int, int> &b) { return a.first < b.first; });
		int From = 0;
		for (size_t c = 0; c <= Changes.size(); ++c) {
			const int To = c < Changes.size() ? std::min(Changes[c].first, Note.Rows) : Note.Rows;
			if (To > From && Param != 0) {
				ArpeggioSpan Span = { Note.Row + From, To - From, Param };
				Spans.push_back(Span);
			}
			if (c < Changes.size()) {
				Param = Changes[c].second;
				From = std::max(From, To);
			}
		}
	}
	return Spans;
}

}	// namespace

int PictureColors()
{
	return COLOR_COUNT;
}

const char *PictureColorName(int Color)
{
	return CHANNEL_COLORS[((Color % COLOR_COUNT) + COLOR_COUNT) % COLOR_COUNT].Name;
}

Picture DrawNotes(const std::vector<PictureChannel> &Channels, const PictureSpec &Spec)
{
	Picture Out;
	const int First = std::max(0, Spec.First);
	const int Rows = std::max(1, Spec.End - First);
	const int End = First + Rows;

	// The pitches to show: the notes there and their chords, with some room
	int Low = 1000, High = -1000;
	for (size_t c = 0; c < Channels.size(); ++c) {
		const PictureChannel &Channel = Channels[c];
		if (IsStrip(Channel.Kind))
			continue;
		for (size_t i = 0; i < Channel.Notes.size(); ++i) {
			const NoteInfo &Note = Channel.Notes[i];
			if (Note.Row < End && Note.Row + Note.Rows > First) {
				Low = std::min(Low, Note.Pitch);
				High = std::max(High, Note.Pitch);
			}
		}
		const std::vector<ArpeggioSpan> Spans = Arpeggios(Channel.Notes);
		for (size_t s = 0; s < Spans.size(); ++s)
			if (Spans[s].Row < End && Spans[s].Row + Spans[s].Rows > First)
				for (size_t i = 0; i < Channel.Notes.size(); ++i) {
					const NoteInfo &Note = Channel.Notes[i];
					if (Note.Row <= Spans[s].Row && Note.Row + Note.Rows > Spans[s].Row)
						High = std::max(High, Note.Pitch + std::max(Spans[s].Param >> 4, Spans[s].Param & 15));
				}
	}
	if (Low > High) {
		Low = 36;	// C-3 to C-5 when there's nothing
		High = 60;
	}
	Low -= 2;
	High += 2;
	while (High - Low + 1 < 25) {
		--Low;
		if (High - Low + 1 < 25)
			++High;
	}
	Low = std::max(0, Low);
	High = std::min(PITCHES - 1, std::max(High, Low + 24));
	const int Semitones = High - Low + 1;

	bool Drums = false, Samples = false;
	for (size_t c = 0; c < Channels.size(); ++c) {
		Drums = Drums || Channels[c].Kind == KIND_NOISE;
		Samples = Samples || Channels[c].Kind == KIND_DPCM;
	}

	// The layout: bar numbers on top, then the sections' band when there are sections
	bool Named = false;
	for (size_t i = 0; i < Spec.Sections.size(); ++i)
		Named = Named || (Spec.Sections[i].First < End && Spec.Sections[i].End > First);
	const int RowWidth = std::max(2, std::min(16, 1024 / Rows));
	const int PitchHeight = std::max(5, std::min(14, 560 / Semitones));
	const int NUMBERS = 26, BAND = 13, LEFT = 56, TOP = NUMBERS + (Named ? BAND + 3 : 0), RIGHT = 8, BOTTOM = 8, GAP = 10,
		DRUM_LANE = 4, SAMPLE_HEIGHT = 14;
	const int PlotTop = TOP, PlotHeight = Semitones * PitchHeight, PlotLeft = LEFT, PlotWidth = Rows * RowWidth;
	const int DrumTop = PlotTop + PlotHeight + GAP, DrumHeight = Drums ? 16 * DRUM_LANE : 0;
	const int SampleTop = DrumTop + DrumHeight + (Drums ? GAP : 0), SampleHeight = Samples ? SAMPLE_HEIGHT : 0;
	Out.Width = PlotLeft + PlotWidth + RIGHT;
	Out.Height = (Samples ? SampleTop + SampleHeight : Drums ? DrumTop + DrumHeight : PlotTop + PlotHeight) + BOTTOM;
	Out.LowPitch = Low;
	Out.HighPitch = High;
	Out.DrumStrip = Drums;
	Out.SampleStrip = Samples;

	Canvas C(Out.Width, Out.Height);
	auto X = [&](int Row) { return PlotLeft + (Row - First) * RowWidth; };
	auto Y = [&](int Pitch) { return PlotTop + (High - Pitch) * PitchHeight; };

	// Lanes: black keys darker, a line under each C
	for (int p = Low; p <= High; ++p) {
		C.Fill(PlotLeft, Y(p), PlotLeft + PlotWidth, Y(p) + PitchHeight, IsBlackKey(p) ? BLACK_KEY : WHITE_KEY);
		if (p % 12 == 0)
			C.Fill(PlotLeft, Y(p) + PitchHeight - 1, PlotLeft + PlotWidth, Y(p) + PitchHeight, OCTAVE_LINE);
	}
	if (Drums)
		C.Fill(PlotLeft, DrumTop, PlotLeft + PlotWidth, DrumTop + DrumHeight, STRIP);
	if (Samples)
		C.Fill(PlotLeft, SampleTop, PlotLeft + PlotWidth, SampleTop + SampleHeight, STRIP);

	// Rows that never play
	if (!Spec.Played.empty())
		for (int r = First; r < End; ++r)
			if (r >= static_cast<int>(Spec.Played.size()) || !Spec.Played[static_cast<size_t>(r)]) {
				C.Hatch(X(r), PlotTop, X(r) + RowWidth, PlotTop + PlotHeight, HATCH);
				if (Drums)
					C.Hatch(X(r), DrumTop, X(r) + RowWidth, DrumTop + DrumHeight, HATCH);
				if (Samples)
					C.Hatch(X(r), SampleTop, X(r) + RowWidth, SampleTop + SampleHeight, HATCH);
			}

	// Beats, bars and frames
	const Timing &T = Spec.Time;
	auto Vertical = [&](int x, int Width, int Color) {
		C.Fill(x, PlotTop, x + Width, PlotTop + PlotHeight, Color);
		if (Drums)
			C.Fill(x, DrumTop, x + Width, DrumTop + DrumHeight, Color);
		if (Samples)
			C.Fill(x, SampleTop, x + Width, SampleTop + SampleHeight, Color);
	};
	int LastLabelEnd = -1000;
	for (int r = First; r < End; ++r) {
		const int Bar = T.BarOf(r);
		const bool BarStart = T.BarStart(Bar) == r;
		const bool FrameStart = r % T.PatternLength == 0;
		const bool BeatStart = (r - T.BarStart(Bar)) % std::max(1, T.RowsPerBeat) == 0;
		if (FrameStart && r > First)
			Vertical(X(r), 2, FRAME_LINE);
		else if (BarStart)
			Vertical(X(r), 1, BAR_LINE);
		else if (BeatStart && RowWidth * T.RowsPerBeat >= 4)
			Vertical(X(r), 1, BEAT_LINE);
		if (BarStart || r == First) {
			const std::string Number = FormatNumber(Bar);
			const int BarWidth = T.BarLength(Bar) * RowWidth;
			const int Scale = BarWidth >= static_cast<int>(Number.size()) * GLYPH_ADVANCE * 2 + 4 ? 2 : 1;
			const int x = X(r) + 2;
			if (x > LastLabelEnd + 4) {
				C.Text(x, NUMBERS - 4 - GLYPH_HEIGHT * Scale, Number, Scale, TEXT);
				LastLabelEnd = x + C.TextWidth(Number, Scale);
			}
		}
	}

	// The sections: a band each in its color, named in capitals as far as there's room
	const int SectionShades = PLAIN_COLORS + COLOR_COUNT * SHADES;
	for (size_t i = 0; Named && i < Spec.Sections.size(); ++i) {
		const PictureSection &Section = Spec.Sections[i];
		const int From = std::max(Section.First, First), To = std::min(Section.End, End);
		if (To <= From)
			continue;
		const int x0 = X(From), x1 = X(To), y0 = NUMBERS, y1 = NUMBERS + BAND;
		const int Color = SectionShades + ((Section.Color % SECTION_COLORS) + SECTION_COLORS) % SECTION_COLORS;
		C.Fill(x0, y0, x1 - 1, y1, Color);
		C.Fill(x0, y0, x0 + 1, y1, TEXT);
		std::string Name;
		for (size_t k = 0; k < Section.Name.size(); ++k)
			Name += static_cast<char>(std::toupper(static_cast<unsigned char>(Section.Name[k])));
		while (!Name.empty() && C.TextWidth(Name, 1) > x1 - x0 - 6)
			Name.erase(Name.size() - 1);
		C.Text(x0 + 3, y0 + (BAND - GLYPH_HEIGHT) / 2, Name, 1, TEXT);
	}

	// Where the song loops back to, and where it jumps back or stops
	if (Spec.LoopRow >= First && Spec.LoopRow < End)
		Vertical(X(Spec.LoopRow), 2, LOOP_MARK);
	if (Spec.EndRow > First && Spec.EndRow <= End)
		Vertical(X(Spec.EndRow) - (Spec.EndRow == End ? 2 : 0), 2, END_MARK);

	// Chords played as arpeggios: their other notes, faint and thin
	for (size_t c = 0; c < Channels.size(); ++c) {
		const PictureChannel &Channel = Channels[c];
		if (IsStrip(Channel.Kind))
			continue;
		const std::vector<ArpeggioSpan> Spans = Arpeggios(Channel.Notes);
		for (size_t s = 0; s < Spans.size(); ++s) {
			const ArpeggioSpan &Span = Spans[s];
			for (size_t i = 0; i < Channel.Notes.size(); ++i) {
				const NoteInfo &Note = Channel.Notes[i];
				const int From = std::max(std::max(Span.Row, Note.Row), First);
				const int To = std::min(std::min(Span.Row + Span.Rows, Note.Row + Note.Rows), End);
				if (To <= From)
					continue;
				const int Offsets[2] = { Span.Param >> 4, Span.Param & 15 };
				for (int k = 0; k < 2; ++k) {
					const int Pitch = Note.Pitch + Offsets[k];
					if (Offsets[k] == 0 || Pitch > High)
						continue;
					const int Inset = PitchHeight / 4;
					C.Fill(X(From), Y(Pitch) + 1 + Inset, X(To) - 1, Y(Pitch) + PitchHeight - Inset, Shade(Channel.Color, SHADE_GHOST));
				}
			}
		}
	}

	// The notes; on a pitch two channels share, the louder one stays in sight
	std::vector<std::pair<const NoteInfo*, const PictureChannel*> > Melodic;
	for (size_t c = 0; c < Channels.size(); ++c) {
		const PictureChannel &Channel = Channels[c];
		for (size_t i = 0; i < Channel.Notes.size(); ++i) {
			const NoteInfo &Note = Channel.Notes[i];
			const int From = std::max(Note.Row, First), To = std::min(Note.Row + Note.Rows, End);
			if (To <= From)
				continue;
			if (!IsStrip(Channel.Kind)) {
				Melodic.push_back(std::make_pair(&Note, &Channel));
				continue;
			}
			// A drum hit or a sample, then its sound fading until the next one
			const int Top = Channel.Kind == KIND_NOISE ? DrumTop + (15 - (Note.Pitch & 15)) * DRUM_LANE : SampleTop + 2;
			const int Height = Channel.Kind == KIND_NOISE ? DRUM_LANE : SampleHeight - 4;
			const int HitEnd = std::min(X(To), X(From) + std::max(RowWidth, 4));
			if (X(To) > HitEnd)
				C.Fill(HitEnd, Top, X(To) - 1, Top + Height, Shade(Channel.Color, SHADE_GHOST));
			if (Note.Row >= First) {
				C.Fill(X(From), Top, HitEnd, Top + Height,
					Shade(Channel.Color, Channel.Kind == KIND_NOISE ? VolumeShade(Note.Volume) : SHADE_DARK));
				MarkPan(C, Note, X(From), Top, std::max(X(To) - 1, HitEnd), Top + Height);
			}
		}
	}
	std::stable_sort(Melodic.begin(), Melodic.end(),
		[](const std::pair<const NoteInfo*, const PictureChannel*> &a, const std::pair<const NoteInfo*, const PictureChannel*> &b) {
			return a.first->Volume < b.first->Volume;
		});
	for (size_t i = 0; i < Melodic.size(); ++i) {
		const NoteInfo &Note = *Melodic[i].first;
		const PictureChannel &Channel = *Melodic[i].second;
		if (Note.Pitch < Low || Note.Pitch > High)
			continue;
		const int From = std::max(Note.Row, First), To = std::min(Note.Row + Note.Rows, End);
		const bool Starts = Note.Row >= First;
		const int Dark = Shade(Channel.Color, SHADE_DARK);
		const int x0 = X(From), x1 = std::max(X(To) - 1, x0 + 1), y0 = Y(Note.Pitch) + 1, y1 = Y(Note.Pitch) + PitchHeight;
		C.Fill(x0, y0, x1, y1, Shade(Channel.Color, VolumeShade(Note.Volume)));
		C.Outline(x0, y0, x1, y1, Dark, Starts);
		if (Starts && RowWidth >= 6)
			C.Fill(x0 + 1, y0, x0 + 2, y1, Dark);
		MarkPan(C, Note, x0 + (Starts && RowWidth >= 6 ? 2 : 0), y0, x1, y1);
	}

	// Labels: each C on the left, the strips' names
	for (int p = Low; p <= High; ++p)
		if (p % 12 == 0) {
			const std::string Label = OctaveLabel(p);
			const int Scale = PitchHeight >= 7 ? 2 : 1;
			const int y = Y(p) + PitchHeight / 2 - (GLYPH_HEIGHT * Scale) / 2;
			C.Text(PlotLeft - 6 - C.TextWidth(Label, Scale), std::max(TOP, y), Label, Scale, TEXT);
		}
	if (Drums)
		C.Text(PlotLeft - 6 - C.TextWidth("DRUMS", 1), DrumTop + DrumHeight / 2 - GLYPH_HEIGHT / 2, "DRUMS", 1, TEXT);
	if (Samples)
		C.Text(PlotLeft - 6 - C.TextWidth("DPCM", 1), SampleTop + SampleHeight / 2 - GLYPH_HEIGHT / 2, "DPCM", 1, TEXT);

	// The palette
	Out.Palette.assign(PLAIN, PLAIN + PLAIN_COLORS);
	for (int k = 0; k < COLOR_COUNT; ++k) {
		const Rgb Base = CHANNEL_COLORS[k].Color;
		for (int s = 0; s < SHADES; ++s) {
			Rgb Color;
			if (s == SHADE_DARK) {
				Color.R = static_cast<unsigned char>(Base.R * 55 / 100);
				Color.G = static_cast<unsigned char>(Base.G * 55 / 100);
				Color.B = static_cast<unsigned char>(Base.B * 55 / 100);
			}
			else {
				const double Strength = SHADE_STRENGTH[s];
				Color.R = static_cast<unsigned char>(255 - (255 - Base.R) * Strength + 0.5);
				Color.G = static_cast<unsigned char>(255 - (255 - Base.G) * Strength + 0.5);
				Color.B = static_cast<unsigned char>(255 - (255 - Base.B) * Strength + 0.5);
			}
			Out.Palette.push_back(Color);
		}
	}
	for (int k = 0; k < SECTION_COLORS; ++k) {
		int Red, Green, Blue;
		SectionRgb(k, Red, Green, Blue);
		Rgb Color;
		Color.R = static_cast<unsigned char>(255 - (255 - Red) * 45 / 100);
		Color.G = static_cast<unsigned char>(255 - (255 - Green) * 45 / 100);
		Color.B = static_cast<unsigned char>(255 - (255 - Blue) * 45 / 100);
		Out.Palette.push_back(Color);
	}
	Out.Pixels.swap(C.Pixels());
	return Out;
}

}	// namespace ftapi

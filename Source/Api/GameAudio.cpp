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

#include "GameAudio.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ftapi {

namespace {

// CutForGame on a render's sides (one, or the left and the right), as long as each other
bool CutSides(const std::vector<const std::vector<float>*> &Sides, int SampleRate, const std::vector<TimedRow> &Rows,
	const std::vector<std::pair<int, long long> > &Marks, bool Halts, bool CleanSeam, GameCut &Out, std::string &Error,
	bool StartStep, bool Tail)
{
	const long long Fade = StartStep ? std::max(1, SampleRate / 25) : 0;	// 40 ms
	Out = GameCut();
	long long Size = static_cast<long long>(Sides[0]->size());
	for (size_t s = 1; s < Sides.size(); ++s)
		Size = std::min(Size, static_cast<long long>(Sides[s]->size()));
	if (Rows.empty() || Marks.empty() || Size == 0) {
		Error = "Nothing was rendered.";
		return false;
	}
	const size_t Common = std::min(Rows.size(), Marks.size());
	for (size_t i = 0; i < Common; ++i)
		if (Marks[i].first != Rows[i].Row) {
			char Text[160];
			std::snprintf(Text, sizeof(Text), "The render played row %d where the song should play row %d.", Marks[i].first, Rows[i].Row);
			Error = Text;
			return false;
		}
	Out.Begin = std::max(0LL, std::min(Size, Marks[0].second));
	if (Halts) {
		// Up to the row with the halt, where the sound stops at once
		Out.End = Size;
		if (Marks.size() >= Rows.size())
			Out.End = std::max(Out.Begin, std::min(Size, Marks[Rows.size() - 1].second));
		if (Tail) {
			// The space's tail after the halt, up to where it's 80 dB under full scale
			long long Last = Out.End;
			for (size_t s = 0; s < Sides.size(); ++s) {
				const std::vector<float> &Render = *Sides[s];
				for (long long k = Size - 1; k >= Last; --k)
					if (std::fabs(Render[static_cast<size_t>(k)]) >= 1e-4f) {
						Last = k + 1;
						break;
					}
			}
			Out.Tail = Last > Out.End;
			Out.End = Last;
		}
		Out.FadeIn = std::min(Fade, Out.Length());
		return true;
	}

	// The loop's start the first time, the second, and the third if rendered
	size_t First = Rows.size(), Second = Rows.size();
	for (size_t i = 0; i < Rows.size(); ++i)
		if (Rows[i].LoopStart) {
			if (First == Rows.size())
				First = i;
			else if (Second == Rows.size())
				Second = i;
		}
	if (Second >= Marks.size()) {
		Error = "The render stopped before the loop came round.";
		return false;
	}
	const long long S1 = Marks[First].second, S2 = Marks[Second].second;
	const long long S3 = Marks.size() > Rows.size() ? Marks[Rows.size()].second : -1;
	if (S1 < Out.Begin || S2 <= S1 || S2 > Size) {
		Error = "The render's rows don't fit its length.";
		return false;
	}

	// How the loop's two starts compare
	const long long Compared = std::min(std::min(static_cast<long long>(SampleRate) * 2, S2 - S1), Size - S2);
	double Most = 0.0;
	for (size_t s = 0; s < Sides.size(); ++s) {
		const std::vector<float> &Render = *Sides[s];
		for (long long k = 0; k < Compared; ++k)
			Most = std::max(Most, static_cast<double>(std::fabs(Render[static_cast<size_t>(S1 + k)] - Render[static_cast<size_t>(S2 + k)])));
	}
	Out.Difference = Most / 2.0;
	Out.Loops = true;
	long long From = S1, To = S2;
	const bool Rendered = S3 >= 0 && S3 <= Size;
	if (Fade > 0 && S1 - Out.Begin < Fade && Rendered) {
		// The loop starts with the song, whose start pops: it repeats from the second time through
		From = S2;
		To = S3;
		Out.SecondPass = true;
		Out.StartPops = true;
		Out.Tail = Tail;
	}
	else if (Tail && Rendered) {
		// The space rings on: the second time through starts with the first's tail, as the loop's start must
		From = S2;
		To = S3;
		Out.SecondPass = true;
		Out.Tail = true;
	}
	else if (!(Most < 0.01 || CleanSeam || !Rendered)) {
		From = S2;
		To = S3;
		Out.SecondPass = true;
	}

	// The jump back lands where the waves match best, within a couple of
	// milliseconds: both points move together, so the loop keeps its length
	// and its beat, and the oscillators don't click at the seam
	const long long Reach = std::max(1, SampleRate / 500), Window = 6;
	long long Best = 0;
	double Lowest = -1.0;
	for (long long d = -Reach; d <= Reach; ++d) {
		if (From + d - Window < Out.Begin || To + d + Window >= Size)
			continue;
		double Cost = 0.0;
		for (size_t s = 0; s < Sides.size(); ++s) {
			const std::vector<float> &Render = *Sides[s];
			for (long long k = -Window; k <= Window; ++k) {
				const double Gap = Render[static_cast<size_t>(From + d + k)] - Render[static_cast<size_t>(To + d + k)];
				Cost += Gap * Gap;
			}
		}
		Cost += 1e-9 * static_cast<double>(d < 0 ? -d : d);	// the nearest of equals
		if (Lowest < 0.0 || Cost < Lowest) {
			Lowest = Cost;
			Best = d;
		}
	}
	Out.LoopStart = From + Best - Out.Begin;
	Out.End = To + Best;
	Out.FadeIn = std::min(Fade, Out.LoopStart);
	return true;
}

// The K-weighted squares of a side's samples, added to Squared
void AddKWeighted(const std::vector<float> &Samples, int SampleRate, size_t First, size_t Count, std::vector<double> &Squared)
{
	// K-weighting: a high shelf (+4 dB over about 1.7 kHz), then a high-pass at about 38 Hz, for this sample rate
	const double Pi = 3.14159265358979323846;
	double Shelf[5], Pass[5];	// b0 b1 b2 a1 a2
	{
		const double K = std::tan(Pi * 1681.974450955533 / SampleRate), Q = 0.7071752369554196;
		const double Vh = std::pow(10.0, 3.999843853973347 / 20.0), Vb = std::pow(Vh, 0.4996667741545416);
		const double a0 = 1.0 + K / Q + K * K;
		Shelf[0] = (Vh + Vb * K / Q + K * K) / a0;
		Shelf[1] = 2.0 * (K * K - Vh) / a0;
		Shelf[2] = (Vh - Vb * K / Q + K * K) / a0;
		Shelf[3] = 2.0 * (K * K - 1.0) / a0;
		Shelf[4] = (1.0 - K / Q + K * K) / a0;
	}
	{
		const double K = std::tan(Pi * 38.13547087602444 / SampleRate), Q = 0.5003270373238773;
		const double a0 = 1.0 + K / Q + K * K;
		Pass[0] = 1.0;
		Pass[1] = -2.0;
		Pass[2] = 1.0;
		Pass[3] = 2.0 * (K * K - 1.0) / a0;
		Pass[4] = (1.0 - K / Q + K * K) / a0;
	}
	double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0, z1 = 0.0, z2 = 0.0;
	for (size_t i = 0; i < Count; ++i) {
		const double x = Samples[First + i];
		const double y = Shelf[0] * x + Shelf[1] * x1 + Shelf[2] * x2 - Shelf[3] * y1 - Shelf[4] * y2;
		const double z = Pass[0] * y + Pass[1] * y1 + Pass[2] * y2 - Pass[3] * z1 - Pass[4] * z2;
		x2 = x1;
		x1 = x;
		y2 = y1;
		y1 = y;
		z2 = z1;
		z1 = z;
		Squared[i] += z * z;
	}
}

// The mean squares of the K-weighted squares' blocks of 400 ms, one every 100 ms (from running sums), added to Blocks
void AddBlocks(const std::vector<double> &Squared, int SampleRate, std::vector<double> &Blocks)
{
	const size_t Count = Squared.size();
	const size_t Block = static_cast<size_t>(SampleRate) * 4 / 10, Hop = static_cast<size_t>(SampleRate) / 10;
	if (Count < Block || Hop == 0)
		return;
	std::vector<double> Sum(Count + 1, 0.0);
	for (size_t i = 0; i < Count; ++i)
		Sum[i + 1] = Sum[i] + Squared[i];
	for (size_t b = 0; b + Block <= Count; b += Hop)
		Blocks.push_back((Sum[b + Block] - Sum[b]) / static_cast<double>(Block));
}

// The integrated loudness of blocks' mean squares
double GatedBlocks(const std::vector<double> &Blocks)
{
	const double Silent = -70.0;
	auto Lufs = [](double MeanSquare) { return MeanSquare > 0.0 ? -0.691 + 10.0 * std::log10(MeanSquare) : -1000.0; };
	// Gated: blocks over -70 LUFS, then those over 10 LU under their loudness
	double Total = 0.0;
	size_t Kept = 0;
	for (size_t b = 0; b < Blocks.size(); ++b)
		if (Lufs(Blocks[b]) > Silent) {
			Total += Blocks[b];
			++Kept;
		}
	if (Kept == 0)
		return Silent;
	const double Relative = Lufs(Total / static_cast<double>(Kept)) - 10.0;
	Total = 0.0;
	Kept = 0;
	for (size_t b = 0; b < Blocks.size(); ++b)
		if (Lufs(Blocks[b]) > Silent && Lufs(Blocks[b]) > Relative) {
			Total += Blocks[b];
			++Kept;
		}
	return Kept == 0 ? Silent : Lufs(Total / static_cast<double>(Kept));
}

// The integrated loudness from the K-weighted squares (both sides' added)
double GatedLoudness(const std::vector<double> &Squared, int SampleRate)
{
	std::vector<double> Blocks;
	AddBlocks(Squared, SampleRate, Blocks);
	return GatedBlocks(Blocks);
}

}	// namespace

bool CutForGame(const std::vector<float> &Render, int SampleRate, const std::vector<TimedRow> &Rows,
	const std::vector<std::pair<int, long long> > &Marks, bool Halts, bool CleanSeam, GameCut &Out, std::string &Error,
	bool StartStep, bool Tail)
{
	return CutSides(std::vector<const std::vector<float>*>(1, &Render), SampleRate, Rows, Marks, Halts, CleanSeam, Out, Error, StartStep,
		Tail);
}

bool CutForGame(const std::vector<float> &Left, const std::vector<float> &Right, int SampleRate, const std::vector<TimedRow> &Rows,
	const std::vector<std::pair<int, long long> > &Marks, bool Halts, bool CleanSeam, GameCut &Out, std::string &Error,
	bool StartStep, bool Tail)
{
	std::vector<const std::vector<float>*> Sides;
	Sides.push_back(&Left);
	Sides.push_back(&Right);
	return CutSides(Sides, SampleRate, Rows, Marks, Halts, CleanSeam, Out, Error, StartStep, Tail);
}

std::string GameMakerName(const std::string &FileName)
{
	std::string Base = FileName;
	const size_t Dot = Base.rfind('.');
	if (Dot != std::string::npos)
		Base = Base.substr(0, Dot);
	std::string Name = "snd_";
	bool Gap = false;
	for (size_t i = 0; i < Base.size(); ++i) {
		const char c = Base[i];
		if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z')) {
			if (Gap && Name.size() > 4)
				Name += '_';
			Name += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
			Gap = false;
		}
		else
			Gap = true;
	}
	return Name.size() > 4 ? Name : "snd_music";
}

std::string GameMakerCode(const std::string &SoundName, const GameCut &Cut, int SampleRate)
{
	char Text[512];
	const double Rate = SampleRate > 0 ? SampleRate : 1;
	if (!Cut.Loops) {
		std::snprintf(Text, sizeof(Text), "audio_play_sound(%s, 0, false);", SoundName.c_str());
		return Text;
	}
	std::snprintf(Text, sizeof(Text),
		"audio_sound_loop_start(%s, %.6f);\n"
		"audio_sound_loop_end(%s, %.6f);\n"
		"audio_play_sound(%s, 0, true);",
		SoundName.c_str(), Cut.LoopStart / Rate, SoundName.c_str(), Cut.Length() / Rate, SoundName.c_str());
	return Text;
}

double IntegratedLoudness(const std::vector<float> &Samples, int SampleRate, size_t First, size_t Count)
{
	const double Silent = -70.0;
	if (SampleRate <= 0 || First >= Samples.size())
		return Silent;
	Count = std::min(Count, Samples.size() - First);
	std::vector<double> Squared(Count, 0.0);
	AddKWeighted(Samples, SampleRate, First, Count, Squared);
	return GatedLoudness(Squared, SampleRate);
}

double IntegratedLoudness(const std::vector<float> &Left, const std::vector<float> &Right, int SampleRate, size_t First,
	size_t Count)
{
	const double Silent = -70.0;
	const size_t Size = std::min(Left.size(), Right.size());
	if (SampleRate <= 0 || First >= Size)
		return Silent;
	Count = std::min(Count, Size - First);
	std::vector<double> Squared(Count, 0.0);
	AddKWeighted(Left, SampleRate, First, Count, Squared);
	AddKWeighted(Right, SampleRate, First, Count, Squared);
	return GatedLoudness(Squared, SampleRate);
}

double SpansLoudness(const std::vector<float> &Left, const std::vector<float> &Right, int SampleRate,
	const std::vector<std::pair<size_t, size_t> > &Spans)
{
	const size_t Size = std::min(Left.size(), Right.size());
	std::vector<double> Blocks;
	for (size_t i = 0; i < Spans.size() && SampleRate > 0; ++i) {
		const size_t First = std::min(Spans[i].first, Size), End = std::min(Spans[i].second, Size);
		if (End <= First)
			continue;
		std::vector<double> Squared(End - First, 0.0);
		AddKWeighted(Left, SampleRate, First, End - First, Squared);
		AddKWeighted(Right, SampleRate, First, End - First, Squared);
		AddBlocks(Squared, SampleRate, Blocks);
	}
	return GatedBlocks(Blocks);
}

double SoundLoudness(const std::vector<float> &Left, const std::vector<float> &Right, int SampleRate, size_t First, size_t Count,
	bool &Short)
{
	const double Silent = -70.0;
	Short = false;
	const size_t Size = std::min(Left.size(), Right.size());
	if (SampleRate <= 0 || First >= Size)
		return Silent;
	Count = std::min(Count, Size - First);
	if (Count >= static_cast<size_t>(SampleRate) * 4 / 10)
		return IntegratedLoudness(Left, Right, SampleRate, First, Count);
	// Shorter than a block: the whole of it as one
	Short = true;
	std::vector<double> Squared(Count, 0.0);
	AddKWeighted(Left, SampleRate, First, Count, Squared);
	AddKWeighted(Right, SampleRate, First, Count, Squared);
	double Sum = 0.0;
	for (size_t i = 0; i < Count; ++i)
		Sum += Squared[i];
	const double MeanSquare = Count > 0 ? Sum / static_cast<double>(Count) : 0.0;
	const double Lufs = MeanSquare > 0.0 ? -0.691 + 10.0 * std::log10(MeanSquare) : -1000.0;
	return Lufs > Silent ? Lufs : Silent;
}

double LoudnessGain(double Measured, double Target, double Peak, bool &Held)
{
	const double Gain = Target - Measured;
	const double Headroom = Peak > 0.0 ? -1.0 - 20.0 * std::log10(Peak) : Gain;
	Held = Gain > Headroom;
	return Held ? Headroom : Gain;
}

std::string SafeFileName(const std::string &Name)
{
	std::string Out;
	for (size_t i = 0; i < Name.size(); ++i) {
		const unsigned char c = static_cast<unsigned char>(Name[i]);
		if (c < 32 || std::string("\\/:*?\"<>|").find(static_cast<char>(c)) != std::string::npos)
			Out += ' ';
		else
			Out += static_cast<char>(c);
	}
	// No spaces or dots at the ends, nor names Windows keeps for devices
	size_t First = Out.find_first_not_of(" ."), Last = Out.find_last_not_of(" .");
	Out = First == std::string::npos ? std::string() : Out.substr(First, Last - First + 1);
	std::string Upper;
	for (size_t i = 0; i < Out.size(); ++i)
		Upper += static_cast<char>(Out[i] >= 'a' && Out[i] <= 'z' ? Out[i] - 'a' + 'A' : Out[i]);
	static const char *const DEVICES[] = { "CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "LPT1", "LPT2", "LPT3" };
	for (size_t i = 0; i < sizeof(DEVICES) / sizeof(DEVICES[0]); ++i)
		if (Upper == DEVICES[i])
			Out += " music";
	return Out.empty() ? std::string("music") : Out;
}

}	// namespace ftapi

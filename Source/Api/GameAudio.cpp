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

bool CutForGame(const std::vector<float> &Render, int SampleRate, const std::vector<TimedRow> &Rows,
	const std::vector<std::pair<int, long long> > &Marks, bool Halts, bool CleanSeam, GameCut &Out, std::string &Error)
{
	Out = GameCut();
	const long long Size = static_cast<long long>(Render.size());
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
		Out.End = Size;
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
	for (long long k = 0; k < Compared; ++k)
		Most = std::max(Most, static_cast<double>(std::fabs(Render[static_cast<size_t>(S1 + k)] - Render[static_cast<size_t>(S2 + k)])));
	Out.Difference = Most / 2.0;
	Out.Loops = true;
	long long From = S1, To = S2;
	if (!(Most < 0.01 || CleanSeam || S3 < 0 || S3 > Size)) {
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
		for (long long k = -Window; k <= Window; ++k) {
			const double Gap = Render[static_cast<size_t>(From + d + k)] - Render[static_cast<size_t>(To + d + k)];
			Cost += Gap * Gap;
		}
		Cost += 1e-9 * static_cast<double>(d < 0 ? -d : d);	// the nearest of equals
		if (Lowest < 0.0 || Cost < Lowest) {
			Lowest = Cost;
			Best = d;
		}
	}
	Out.LoopStart = From + Best - Out.Begin;
	Out.End = To + Best;
	return true;
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

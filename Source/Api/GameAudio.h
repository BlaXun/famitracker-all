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

// Songs for games (File > Export for games, export_for_game): where to cut a
// rendered song so that it loops without a seam, and what a game engine
// needs to know about it. FamiTracker renders the track until a little past
// its second time through the loop, noting the sample where each row began;
// this works out the rest. Plain C++14.

#include <string>
#include <utility>
#include <vector>
#include "Music.h"

namespace ftapi {

struct GameCut {
	long long Begin, End;		// the render's samples kept: [Begin, End)
	long long LoopStart;		// in the kept audio; the loop goes on to its end
	bool Loops;
	bool SecondPass;			// the loop is the second time through, since the first starts differently
	double Difference;			// the biggest difference between the loop's two starts, 0-1 of full scale
	long long FadeIn;			// samples at the start that fade in over a step from silence (before the loop)
	bool StartPops;				// the loop is the second time through since the song's start pops
	bool Tail;					// the mixer's space rings on: the loop is the second time through, or the halt keeps it

	GameCut() : Begin(0), End(0), LoopStart(0), Loops(false), SecondPass(false), Difference(0.0), FadeIn(0), StartPops(false),
		Tail(false) {}
	long long Length() const { return End - Begin; }
};

// Rows: the song's rows in playing order, the loop twice (TimeRows with two
// passes); Marks: the rendered rows in order, each with the sample where it
// began (more than Rows when the render went on). The loop is the first time
// through when that sounds the same as the second, or when the notes start
// it the same way both times (CleanSeam; the noise channel never sounds quite
// the same), else the second time through. Its two ends move together by up
// to 2 ms to where the waves match, so the jump back doesn't click. A song
// that halts is kept up to the row with the halt, where FamiTracker silences
// it. StartStep: the song steps the DMC's level up from silence as it starts
// (a Zxx on its first row), a pop the repeats don't have; the file fades in
// over its first 40 ms, and a loop that starts with the song is taken from
// its second time through, so the fade stays out of it. Tail: the mixer's
// space (Space, a reverb and an echo) rings on after the notes, so the
// loop's end carries into its start: the loop is its second time through,
// whose start has the first's tail in it as the jump back needs, and a song
// that halts keeps its tail (the render goes on until it dies away) up to
// where it's 80 dB under full scale. False with the reason when the render
// doesn't fit the song.
bool CutForGame(const std::vector<float> &Render, int SampleRate, const std::vector<TimedRow> &Rows,
	const std::vector<std::pair<int, long long> > &Marks, bool Halts, bool CleanSeam, GameCut &Out, std::string &Error,
	bool StartStep = false, bool Tail = false);
// The same for a stereo render, its left and right sides (as long as each
// other): the loop's two starts are compared, and its ends matched, on both
bool CutForGame(const std::vector<float> &Left, const std::vector<float> &Right, int SampleRate, const std::vector<TimedRow> &Rows,
	const std::vector<std::pair<int, long long> > &Marks, bool Halts, bool CleanSeam, GameCut &Out, std::string &Error,
	bool StartStep = false, bool Tail = false);

// The sound's name in GameMaker for an exported file: "Town (night).ogg"
// becomes snd_town_night
std::string GameMakerName(const std::string &FileName);

// GameMaker code (version 2023.2 or newer) that plays the sound with its loop
std::string GameMakerCode(const std::string &SoundName, const GameCut &Cut, int SampleRate);

// A file name from a track's name, without what Windows doesn't allow
std::string SafeFileName(const std::string &Name);

// The integrated loudness of Count samples from First, in LUFS, as EBU R128
// (ITU-R BS.1770) measures it: K-weighted, in blocks of 400 ms every 100 ms,
// gated at -70 LUFS and 10 LU under the ungated loudness. One channel counts
// once, as ffmpeg's ebur128 measures it. -70 or less when it's (nearly)
// silent.
double IntegratedLoudness(const std::vector<float> &Samples, int SampleRate, size_t First, size_t Count);
// The same over a stereo sound's two sides, which count together as ffmpeg's
// ebur128 counts them: a sound the same on both measures 3 LU louder than one
// side of it alone
double IntegratedLoudness(const std::vector<float> &Left, const std::vector<float> &Right, int SampleRate, size_t First,
	size_t Count);
// The same over stretches of it ([first, end) in samples, in order), as one
// program: each stretch K-weighted from its start, as ffmpeg measures a
// stretch cut out of a file, and the blocks of all of them gated together.
// One stretch measures as IntegratedLoudness measures it.
double SpansLoudness(const std::vector<float> &Left, const std::vector<float> &Right, int SampleRate,
	const std::vector<std::pair<size_t, size_t> > &Spans);

// The gain in dB that brings audio measuring Measured LUFS to Target, as far
// as its loudest peak (Peak, 0-1 of full scale) may rise: to -1 dBFS. Held
// when the peak holds it back.
double LoudnessGain(double Measured, double Target, double Peak, bool &Held);

}	// namespace ftapi

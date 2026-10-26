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

// Tests for listening to a song as it sounds (Listen.h), on sounds made here:
// a held note with a 14.5 Hz warble of 160 cents either way (an FDS
// modulator at speed 34 and depth 5), a gentle vibrato that passes, notes 30
// cents flat, two parts at one pitch 20 cents apart and 7 cents apart, clicks
// inside a held note (a pulse restarting its wave) and a duty change that
// isn't one, a sound that holds on after its release and one that fades, and
// a harsh stretch. The listen tool's side (renders, notes, fixes) is in
// ApiTests.cpp.

#include "../Listen.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace ftapi;

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
#define CHECK_EQ(a, b) Check((a) == (b), #a " == " #b, __LINE__)

bool Contains(const std::string &Text, const char *Part)
{
	return Text.find(Part) != std::string::npos;
}

#define CHECK_HAS(text, part) Check(Contains((text), (part)), "\"" part "\" in " #text, __LINE__)

const int RATE = 44100;
const double PI = 3.14159265358979323846;

enum shape_t { RAMP, PULSE, SINE };

// A wave's value at a phase (0-1): a falling ramp's first eight harmonics (an FDS lead's wave, as its mixer softens it), a
// pulse of a duty (from -1 to 1), a sine
double Wave(int Shape, double Phase, double Duty)
{
	if (Shape == SINE)
		return std::sin(2.0 * PI * Phase);
	if (Shape == PULSE)
		return Phase < Duty ? 1.0 : -1.0;
	double Sum = 0.0;
	for (int h = 1; h <= 8; ++h)
		Sum += std::sin(2.0 * PI * h * Phase) / h;
	return Sum * 0.55;
}

// A part's sound, made a note at a time
class Synth {
public:
	std::vector<float> Samples;
	double Phase;
	Synth() : Phase(0.0) {}

	// Seconds of silence
	void Rest(double Seconds)
	{
		Samples.resize(Samples.size() + static_cast<size_t>(Seconds * RATE), 0.0f);
	}
	// A note: Seconds at Hz, its pitch swinging Cents either way Rate times a second from From seconds in (a sine's swing),
	// at Level; Resets: the wave restarts every so many seconds (0: never); DutyAt: the pulse's duty changes to Duty2 there
	ListenNote Note(double Seconds, double Hz, int Shape, double Level, double Rate = 0.0, double Cents = 0.0, double From = 0.0,
		double Resets = 0.0, double Duty = 0.25, double DutyAt = -1.0, double Duty2 = 0.5)
	{
		ListenNote N;
		N.Begin = static_cast<long long>(Samples.size());
		N.Hz = Hz;
		const size_t Count = static_cast<size_t>(Seconds * RATE);
		double Wobble = 0.0, NextReset = Resets;
		for (size_t n = 0; n < Count; ++n) {
			const double t = static_cast<double>(n) / RATE;
			double f = Hz;
			if (Rate > 0.0 && t >= From) {
				f = Hz * std::pow(2.0, Cents * std::sin(Wobble) / 1200.0);
				Wobble += 2.0 * PI * Rate / RATE;
			}
			if (Resets > 0.0 && t >= NextReset) {
				Phase = 0.0;
				NextReset += Resets;
			}
			const double d = DutyAt >= 0.0 && t >= DutyAt ? Duty2 : Duty;
			Samples.push_back(static_cast<float>(Level * Wave(Shape, Phase, d)));
			Phase = std::fmod(Phase + f / RATE, 1.0);
		}
		N.End = static_cast<long long>(Samples.size());
		return N;
	}
	// A sound held on at Level for Seconds after a note (a release that holds), its pitch Hz; or fading by FadeDb a second
	void Hold(double Seconds, double Hz, int Shape, double Level, double FadeDb)
	{
		const size_t Count = static_cast<size_t>(Seconds * RATE);
		for (size_t n = 0; n < Count; ++n) {
			const double t = static_cast<double>(n) / RATE;
			const double Gain = Level * std::pow(10.0, -FadeDb * t / 20.0);
			Samples.push_back(static_cast<float>(Gain * Wave(Shape, Phase, 0.5)));
			Phase = std::fmod(Phase + Hz / RATE, 1.0);
		}
	}
};

ListenPart Part(const char *Name, const Synth &Made, const std::vector<ListenNote> &Notes, bool Melody = true)
{
	ListenPart P;
	P.Name = Name;
	P.Samples = Made.Samples;
	P.Notes = Notes;
	P.Melody = Melody;
	P.Restarts = std::string(Name).find("Pulse") != std::string::npos;	// as the 2A03's pulses: their wave restarts
	return P;
}

// A song of these parts: the mix their sum, bars of two seconds
ListenSong SongOf(const std::vector<ListenPart> &Parts)
{
	ListenSong S;
	S.Rate = RATE;
	S.Parts = Parts;
	size_t Length = 0;
	for (size_t p = 0; p < Parts.size(); ++p)
		Length = std::max(Length, Parts[p].Samples.size());
	S.Left.assign(Length, 0.0f);
	for (size_t p = 0; p < Parts.size(); ++p)
		for (size_t n = 0; n < Parts[p].Samples.size(); ++n)
			S.Left[n] += Parts[p].Samples[n];
	S.Right = S.Left;
	for (long long b = 0; b <= static_cast<long long>(Length); b += 2 * RATE)
		S.Bars.push_back(b);
	S.SectionStarts.assign(S.Bars.size(), false);
	S.SectionStarts[0] = true;
	return S;
}

int CountKind(const std::vector<ListenFinding> &Found, int Kind)
{
	int N = 0;
	for (size_t i = 0; i < Found.size(); ++i)
		N += Found[i].Kind == Kind ? 1 : 0;
	return N;
}

const ListenFinding *FindKind(const std::vector<ListenFinding> &Found, int Kind)
{
	for (size_t i = 0; i < Found.size(); ++i)
		if (Found[i].Kind == Kind)
			return &Found[i];
	return 0;
}

// 38's lead before the fix: long notes right for their first 0.47 s, then a 14.5 Hz warble of 160 cents either way
void TestWarble()
{
	Synth Lead;
	std::vector<ListenNote> Notes;
	for (int i = 0; i < 5; ++i) {
		ListenNote N = Lead.Note(1.4, 440.0, RAMP, 0.3, 14.5, 160.0, 0.47);
		N.Motion = LISTEN_MODULATOR;
		Notes.push_back(N);
		Lead.Rest(0.2);
	}
	const ListenPitch P = MeasurePitch(Lead.Samples, RATE, Notes[0].Begin, Notes[0].End, 440.0);
	CHECK(P.Valid && P.Wobbles);
	CHECK(P.Rate > 13.5 && P.Rate < 15.5);
	CHECK(P.Low < -130.0 && P.Low > -175.0 && P.High > 130.0 && P.High < 175.0);
	CHECK(P.WobbleFrom > 0.4 && P.WobbleFrom < 0.56);
	CHECK(std::fabs(P.Steady) < 3.0);	// in tune where it holds

	ListenSong S = SongOf(std::vector<ListenPart>(1, Part("FDS", Lead, Notes)));
	const std::vector<ListenFinding> Found = ListenToSong(S);
	const ListenFinding *pF = FindKind(Found, LISTEN_WOBBLE);
	CHECK(pF != 0);
	if (pF != 0) {
		CHECK_EQ(pF->What, std::string("warble"));
		CHECK_EQ(pF->Severity, 3);
		CHECK(pF->Number("notes") == 5.0);
		CHECK(pF->Number("rate") > 13.5 && pF->Number("rate") < 15.5);
		CHECK(pF->Number("motion") == LISTEN_MODULATOR);
		CHECK_HAS(pF->Heard, "about a semitone and a half up and down");
		CHECK_HAS(pF->Heard, "times a second");
		CHECK_HAS(pF->Heard, "a short right note, then a fluttering echo of it");
		CHECK_HAS(pF->Heard, "a warble, not a vibrato");
		CHECK(pF->Begin == Notes[0].Begin && pF->End == Notes[4].End);
	}
	CHECK_EQ(CountKind(Found, LISTEN_TUNING), 0);
	CHECK_EQ(CountKind(Found, LISTEN_CLICK), 0);
}

// A gentle vibrato, 5 times a second, 15 cents either way, passes; so does the vibrato that waits (38's fix: 456 in place of
// H05, about 15 cents either way 4.7 times a second)
void TestGentleVibrato()
{
	Synth Lead;
	std::vector<ListenNote> Notes;
	for (int i = 0; i < 4; ++i) {
		ListenNote N = Lead.Note(1.2, 440.0, PULSE, 0.3, 5.0, 15.0, 0.0);
		N.Motion = LISTEN_VIBRATO_EFFECT;
		Notes.push_back(N);
		Lead.Rest(0.1);
	}
	for (int i = 0; i < 4; ++i) {
		ListenNote N = Lead.Note(1.4, 392.0, RAMP, 0.3, 4.7, 15.0, 0.47);
		N.Motion = LISTEN_VIBRATO_EFFECT;
		Notes.push_back(N);
		Lead.Rest(0.1);
	}
	const ListenPitch P = MeasurePitch(Lead.Samples, RATE, Notes[0].Begin, Notes[0].End, 440.0);
	CHECK(P.Valid && P.Wobbles);
	CHECK(P.Rate > 4.5 && P.Rate < 5.5);
	CHECK(P.Swing > 11.0 && P.Swing < 18.0);
	CHECK(std::fabs(P.Steady) < 4.0);
	const ListenPitch Q = MeasurePitch(Lead.Samples, RATE, Notes[4].Begin, Notes[4].End, 392.0);
	CHECK(Q.Valid && Q.Wobbles && Q.WobbleFrom > 0.4 && Q.WobbleFrom < 0.56);

	const std::vector<ListenFinding> Found = ListenToSong(SongOf(std::vector<ListenPart>(1, Part("Pulse 1", Lead, Notes))));
	CHECK_EQ(CountKind(Found, LISTEN_WOBBLE), 0);
	CHECK_EQ(CountKind(Found, LISTEN_TUNING), 0);
	CHECK_EQ(CountKind(Found, LISTEN_CLICK), 0);
}

// Too deep a vibrato: 5 times a second, 90 cents either way
void TestDeepVibrato()
{
	Synth Lead;
	std::vector<ListenNote> Notes;
	for (int i = 0; i < 3; ++i) {
		ListenNote N = Lead.Note(1.2, 330.0, PULSE, 0.3, 5.0, 90.0, 0.2);
		N.Motion = LISTEN_VIBRATO_EFFECT;
		Notes.push_back(N);
		Lead.Rest(0.1);
	}
	const std::vector<ListenFinding> Found = ListenToSong(SongOf(std::vector<ListenPart>(1, Part("Pulse 1", Lead, Notes))));
	const ListenFinding *pF = FindKind(Found, LISTEN_WOBBLE);
	CHECK(pF != 0 && pF->What == "vibrato");
	if (pF != 0)
		CHECK_HAS(pF->Heard, "out of tune rather than sung");
}

// Notes 30 cents flat
void TestOutOfTune()
{
	Synth Lead;
	std::vector<ListenNote> Notes;
	const double Flat = std::pow(2.0, -30.0 / 1200.0);
	for (int i = 0; i < 3; ++i) {
		ListenNote N = Lead.Note(0.8, 440.0 * Flat, RAMP, 0.3);
		N.Hz = 440.0;	// written as an A, where the other parts are tuned
		Notes.push_back(N);
		Lead.Rest(0.1);
	}
	const ListenPitch P = MeasurePitch(Lead.Samples, RATE, Notes[0].Begin, Notes[0].End, 440.0);
	CHECK(P.Valid && !P.Wobbles && P.Steady < -28.0 && P.Steady > -32.0);
	std::vector<ListenFinding> Found = ListenToSong(SongOf(std::vector<ListenPart>(1, Part("FDS", Lead, Notes))));
	const ListenFinding *pF = FindKind(Found, LISTEN_TUNING);
	CHECK(pF != 0);
	if (pF != 0) {
		CHECK(pF->Number("cents") <= -28.0 && pF->Number("cents") >= -32.0);
		CHECK_EQ(pF->What, std::string("sound"));
		CHECK_HAS(pF->Heard, "3 held notes sound flat: about a quarter of a semitone flat (-30 cents)");
	}
	// Where the chip's register puts the notes, it's the chip's own tuning
	for (size_t i = 0; i < Notes.size(); ++i)
		Notes[i].ChipHz = 440.0 * Flat;
	Found = ListenToSong(SongOf(std::vector<ListenPart>(1, Part("Pulse 1", Lead, Notes))));
	pF = FindKind(Found, LISTEN_TUNING);
	CHECK(pF != 0 && pF->What == "chip");
	if (pF != 0)
		CHECK_HAS(pF->Heard, "The chip can't play them closer");
	// 10 cents off passes
	Synth Near;
	std::vector<ListenNote> Close;
	for (int i = 0; i < 3; ++i) {
		ListenNote N = Near.Note(0.8, 440.0 * std::pow(2.0, 10.0 / 1200.0), RAMP, 0.3);
		N.Hz = 440.0;
		Close.push_back(N);
	}
	CHECK_EQ(CountKind(ListenToSong(SongOf(std::vector<ListenPart>(1, Part("FDS", Near, Close)))), LISTEN_TUNING), 0);
	// Some notes sharp and some flat: each way said apart, not a middle of a few cents
	Synth Both;
	std::vector<ListenNote> Mixed;
	for (int i = 0; i < 4; ++i) {
		ListenNote N = Both.Note(0.8, 440.0 * std::pow(2.0, (i % 2 == 0 ? 25.0 : -25.0) / 1200.0), RAMP, 0.3);
		N.Hz = 440.0;
		Mixed.push_back(N);
		Both.Rest(0.1);
	}
	Found = ListenToSong(SongOf(std::vector<ListenPart>(1, Part("FDS", Both, Mixed))));
	CHECK_EQ(CountKind(Found, LISTEN_TUNING), 2);
	for (size_t i = 0; i < Found.size(); ++i)
		if (Found[i].Kind == LISTEN_TUNING)
			CHECK(std::fabs(Found[i].Number("cents")) >= 22.0 && std::fabs(Found[i].Number("cents")) <= 28.0);
}

// Two parts at one pitch: 20 cents apart they beat; 7 apart (a chorus, 38's echo detuned with P81) they pass. The detune is
// asked for (Hz where it should sound), so neither is out of tune alone
void TestBeating()
{
	for (int Apart = 0; Apart < 2; ++Apart) {
		const double Cents = Apart == 0 ? 20.0 : 7.0;
		Synth A, B;
		std::vector<ListenNote> NotesA, NotesB;
		const double Hz[] = { 440.0, 392.0, 349.2, 392.0 };
		for (int i = 0; i < 4; ++i) {
			NotesA.push_back(A.Note(1.0, Hz[i], RAMP, 0.3));
			const double Sharp = Hz[i] * std::pow(2.0, Cents / 1200.0);
			NotesB.push_back(B.Note(1.0, Sharp, PULSE, 0.12));
			A.Rest(0.1);
			B.Rest(0.1);
		}
		std::vector<ListenPart> Parts;
		Parts.push_back(Part("FDS", A, NotesA));
		Parts.push_back(Part("Namco 3", B, NotesB, false));
		const std::vector<ListenFinding> Found = ListenToSong(SongOf(Parts));
		CHECK_EQ(CountKind(Found, LISTEN_TUNING), 0);
		const ListenFinding *pF = FindKind(Found, LISTEN_BEATING);
		if (Apart == 0) {
			CHECK(pF != 0);
			if (pF != 0) {
				CHECK(pF->Part == 0 && pF->Other == 1);
				CHECK(pF->Number("cents") >= 18.0 && pF->Number("cents") <= 22.0);
				CHECK(pF->Number("beats") > 3.5 && pF->Number("beats") < 6.0);
				CHECK_HAS(pF->Heard, "FDS and Namco 3 play the same notes together");
				CHECK_HAS(pF->Heard, "they beat");
			}
		}
		else
			CHECK(pF == 0);
	}
}

// Clicks inside a held note: a pulse that restarts its wave 10 times a second (a vibrato across a multiple of 256 in its
// period). A duty change in the middle of a note changes its sound, which isn't a click, and a steady pulse has none
void TestClicks()
{
	Synth Lead;
	std::vector<ListenNote> Notes;
	for (int i = 0; i < 2; ++i) {
		Notes.push_back(Lead.Note(2.0, 220.0, PULSE, 0.3, 0.0, 0.0, 0.0, 0.1037));
		Lead.Rest(0.1);
	}
	const std::vector<ListenFinding> Found = ListenToSong(SongOf(std::vector<ListenPart>(1, Part("Pulse 1", Lead, Notes))));
	const ListenFinding *pF = FindKind(Found, LISTEN_CLICK);
	CHECK(pF != 0);
	if (pF != 0) {
		CHECK_EQ(pF->What, std::string("inside"));
		CHECK(pF->Number("clicks") >= 30.0 && pF->Number("clicks") <= 40.0);
		CHECK_HAS(pF->Heard, "inside held notes");
	}
	Synth Plain;
	std::vector<ListenNote> Still;
	for (int i = 0; i < 2; ++i) {
		Still.push_back(Plain.Note(2.0, 220.0, PULSE, 0.3, 0.0, 0.0, 0.0, 0.0, 0.25, 1.0, 0.5));
		Plain.Rest(0.1);
	}
	CHECK_EQ(CountKind(ListenToSong(SongOf(std::vector<ListenPart>(1, Part("Pulse 1", Plain, Still)))), LISTEN_CLICK), 0);
}

// A sound that holds on after its release (a release that ends above silence) is a drone; one that fades isn't
void TestDrone()
{
	for (int Fades = 0; Fades < 2; ++Fades) {
		Synth Pad;
		std::vector<ListenNote> Notes;
		for (int i = 0; i < 3; ++i) {
			ListenNote N = Pad.Note(0.8, 220.0, SINE, 0.3);
			N.Released = true;
			Notes.push_back(N);
			Pad.Hold(1.6, 220.0, SINE, 0.15, Fades ? 40.0 : 0.0);
		}
		ListenPart P = Part("Namco 1", Pad, Notes, false);
		const std::vector<ListenFinding> Found = ListenToSong(SongOf(std::vector<ListenPart>(1, P)));
		const ListenFinding *pF = FindKind(Found, LISTEN_DRONE);
		if (Fades)
			CHECK(pF == 0);
		else {
			CHECK(pF != 0);
			if (pF != 0) {
				CHECK(pF->Number("notes") == 3.0);
				CHECK(pF->Number("db_under") > 4.0 && pF->Number("db_under") < 8.0);
				CHECK_HAS(pF->Heard, "its sound holds on");
			}
		}
	}
}

// A harsh stretch: a thin, high pulse and noise loud over everything; a warm sine isn't
void TestHarsh()
{
	Synth Thin, Warm;
	std::vector<ListenNote> High, Low;
	for (int i = 0; i < 4; ++i)
		High.push_back(Thin.Note(2.0, 1760.0, PULSE, 0.3, 0.0, 0.0, 0.0, 0.0, 0.125));
	for (size_t n = 0; n < Thin.Samples.size(); ++n)
		Thin.Samples[n] += static_cast<float>(0.15 * (std::rand() / static_cast<double>(RAND_MAX) - 0.5));
	for (int i = 0; i < 4; ++i)
		Low.push_back(Warm.Note(2.0, 220.0, SINE, 0.3));
	const std::vector<ListenFinding> Bright = ListenToSong(SongOf(std::vector<ListenPart>(1, Part("Pulse 1", Thin, High))));
	const ListenFinding *pF = FindKind(Bright, LISTEN_HARSH);
	CHECK(pF != 0);
	if (pF != 0) {
		CHECK(pF->Other == 0 && pF->Number("bars") >= 3.0);
		CHECK_HAS(pF->Heard, "bright and edgy");
		CHECK_HAS(pF->Heard, "most of it is Pulse 1");
	}
	CHECK_EQ(CountKind(ListenToSong(SongOf(std::vector<ListenPart>(1, Part("Triangle", Warm, Low)))), LISTEN_HARSH), 0);
}

void TestWords()
{
	CHECK_EQ(ListenCentsWords(5.0), std::string("a few cents"));
	CHECK_EQ(ListenCentsWords(-30.0), std::string("about a quarter of a semitone"));
	CHECK_EQ(ListenCentsWords(160.0), std::string("about a semitone and a half"));
	CHECK_EQ(ListenCentsWords(410.0), std::string("about 4 semitones"));
	CHECK_EQ(ListenTimesWords(14.2), std::string("14.2 times a second"));
	CHECK_EQ(ListenTimesWords(4.72), std::string("4.7 times a second"));
	CHECK_EQ(ListenTimesWords(1.0), std::string("about once a second"));
	CHECK_EQ(ListenTimesWords(0.0), std::string("unevenly"));
	CHECK_EQ(std::string(ListenKindId(LISTEN_WOBBLE)), std::string("wobble"));
	CHECK_EQ(std::string(ListenKindId(LISTEN_MUDDY)), std::string("muddy"));
	CHECK_EQ(std::string(ListenKindTitle(LISTEN_DRONE)), std::string("Sound that holds on"));
}

}	// namespace

int main()
{
	std::setvbuf(stdout, 0, _IONBF, 0);
	TestWarble();
	TestGentleVibrato();
	TestDeepVibrato();
	TestOutOfTune();
	TestBeating();
	TestClicks();
	TestDrone();
	TestHarsh();
	TestWords();
	std::printf("%d checks, %d failed\n", g_Checks, g_Failures);
	return g_Failures == 0 ? 0 : 1;
}

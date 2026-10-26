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

// Tests for reading tutorials (TutorialScript.cpp), and the built-in ones
// in res/tutorials (TUTORIALS_DIR) all read; the example songs' walkthroughs
// in res/examples (EXAMPLES_DIR) too, with the bars and channels they name
// found in their songs; and Help > Exercises' scripts in res/exercises
// (EXERCISES_DIR), whose checks and starter songs Source/Api's tests run

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "TutorialScript.h"

namespace {

int g_checks = 0, g_failures = 0;

#define CHECK(x) do { ++g_checks; if (!(x)) { ++g_failures; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

void TestReading()
{
	const std::string Text =
		"; a comment\r\n"
		"title: Your first tune\r\n"
		"about: Type a melody\r\n"
		"  and hear it.\r\n"
		"\r\n"
		"step\r\n"
		"show: pattern\r\n"
		"text: This is the pattern editor.\r\n"
		"  Time runs down the page.\r\n"
		"\r\n"
		"  A second paragraph: with a colon in it.\r\n"
		"wait: edit-mode\r\n"
		"\r\n"
		"[step]\r\n"
		"show: button play\r\n"
		"text: Press play.\r\n"
		"wait: notes 4\r\n"
		"step\r\n"
		"text: Save it.\r\n"
		"wait: command Save\r\n"
		"step\r\n"
		"show: menu Edit\r\n"
		"text: The end.\r\n";
	tutorial_t T;
	std::string Error;
	CHECK(ParseTutorial(Text, T, Error));
	CHECK(Error.empty());
	CHECK(T.Title == "Your first tune");
	CHECK(T.About == "Type a melody and hear it.");
	CHECK(T.Steps.size() == 4);
	if (T.Steps.size() != 4)
		return;
	CHECK(T.Steps[0].Show == "pattern" && T.Steps[0].ShowName.empty());
	CHECK(T.Steps[0].Text == "This is the pattern editor. Time runs down the page.\nA second paragraph: with a colon in it.");
	CHECK(T.Steps[0].Wait == WAIT_EDIT_MODE);
	CHECK(T.Steps[1].Show == "button" && T.Steps[1].ShowName == "play");
	CHECK(T.Steps[1].Wait == WAIT_NOTES && T.Steps[1].Count == 4);
	CHECK(T.Steps[2].Show == "window");		// nothing to show: the whole window
	CHECK(T.Steps[2].Wait == WAIT_COMMAND && T.Steps[2].Command == "save");
	CHECK(T.Steps[3].Show == "menu" && T.Steps[3].ShowName == "Edit" && T.Steps[3].Wait == WAIT_NEXT);
}

void TestWaits()
{
	const char *const Waits[] = { "next", "edit-mode", "edit-mode-off", "play", "stop", "instrument", "clip", "saved", "notes" };
	const tutorial_wait_t Expected[] = { WAIT_NEXT, WAIT_EDIT_MODE, WAIT_EDIT_MODE_OFF, WAIT_PLAY, WAIT_STOP, WAIT_INSTRUMENT,
		WAIT_CLIP, WAIT_SAVED, WAIT_NOTES };
	for (size_t i = 0; i < sizeof(Waits) / sizeof(Waits[0]); ++i) {
		tutorial_t T;
		std::string Error;
		CHECK(ParseTutorial(std::string("title: T\nstep\ntext: x\nwait: ") + Waits[i] + "\n", T, Error));
		CHECK(T.Steps.size() == 1 && T.Steps[0].Wait == Expected[i] && T.Steps[0].Count == 1);
	}
	for (size_t i = 0; i < TutorialCommandNames().size(); ++i) {
		tutorial_t T;
		std::string Error;
		const std::string &Name = TutorialCommandNames()[i];
		CHECK(ParseTutorial("title: T\nstep\nshow: button " + Name + "\ntext: x\nwait: command " + Name + "\n", T, Error));
	}
	CHECK(IsTutorialPart("piano-roll") && !IsTutorialPart("kitchen"));
	CHECK(IsTutorialPart("drum-grid"));
	{
		// The drum grid's panel and the pattern editor's menu item
		tutorial_t T;
		std::string Error;
		CHECK(ParseTutorial("title: T\nstep\nshow: drum-grid\ntext: x\nwait: command pattern-editor\n", T, Error));
		CHECK(T.Steps.size() == 1 && T.Steps[0].Show == "drum-grid" && T.Steps[0].Command == "pattern-editor");
	}
}

void TestErrors()
{
	struct { const char *Text; const char *Error; } Cases[] = {
		{ "step\ntext: x\n", "The tutorial has no title" },
		{ "title: T\n", "The tutorial has no steps." },
		{ "title: T\nstep\nshow: pattern\n", "Step 1 has no text." },
		{ "title: T\nstep\nshow: kitchen\ntext: x\n", "Line 3: a step can't show \"kitchen\"." },
		{ "title: T\nstep\nshow: button fly\ntext: x\n", "Line 3: there's no toolbar button called \"fly\"." },
		{ "title: T\nstep\nshow: menu\ntext: x\n", "Line 3: \"menu\" takes the menu's name" },
		{ "title: T\nstep\nshow: pattern now\ntext: x\n", "Line 3: \"pattern\" takes nothing after it." },
		{ "title: T\nstep\ntext: x\nwait: lunch\n", "Line 4: a step can't wait for \"lunch\"." },
		{ "title: T\nstep\ntext: x\nwait: notes many\n", "Line 4: \"notes\" takes a number of notes" },
		{ "title: T\nstep\ntext: x\nwait: notes 0\n", "Line 4: \"notes\" takes a number of notes" },
		{ "title: T\nstep\ntext: x\nwait: play now\n", "Line 4: \"play\" takes nothing after it." },
		{ "title: T\nstep\ntext: x\nwait: command fly\n", "Line 4: \"fly\" isn't a command" },
		{ "title: T\ntext: x\n", "Line 2: \"text\" belongs to a step" },
		{ "title: T\nstep\ntext: x\ntitle: again\n", "Line 4: the title goes before the first step." },
		{ "hello\n", "Line 1: \"hello\" is neither" },
	};
	for (size_t i = 0; i < sizeof(Cases) / sizeof(Cases[0]); ++i) {
		tutorial_t T;
		std::string Error;
		const bool Read = ParseTutorial(Cases[i].Text, T, Error);
		CHECK(!Read);
		CHECK(Error.compare(0, std::string(Cases[i].Error).size(), Cases[i].Error) == 0);
		if (Read || Error.compare(0, std::string(Cases[i].Error).size(), Cases[i].Error) != 0)
			std::printf("  case %d: \"%s\"\n", static_cast<int>(i), Error.c_str());
	}
}

// The tutorials that go into FamiTracker.exe
void TestBuiltIn()
{
	const char *const Files[] = { "first-tune.txt", "sounds-and-drums.txt", "clips-and-piano-roll.txt", "game-music.txt" };
	for (size_t i = 0; i < sizeof(Files) / sizeof(Files[0]); ++i) {
		std::ifstream In((std::string(TUTORIALS_DIR) + "/" + Files[i]).c_str(), std::ios::binary);
		CHECK(In.good());
		std::stringstream Text;
		Text << In.rdbuf();
		tutorial_t T;
		std::string Error;
		const bool Read = ParseTutorial(Text.str(), T, Error);
		CHECK(Read);
		if (!Read)
			std::printf("  %s: %s\n", Files[i], Error.c_str());
		CHECK(T.Steps.size() >= 3 && !T.About.empty());
		// Plain ASCII, as FamiTracker shows it with the ANSI code page
		bool Ascii = true;
		for (size_t k = 0; k < Text.str().size(); ++k)
			if (static_cast<unsigned char>(Text.str()[k]) > 126)
				Ascii = false;
		CHECK(Ascii);
	}
}


// The lines of the example songs' walkthroughs
void TestWalkthroughLines()
{
	const std::string Text =
		"title: First song\r\n"
		"about: A tune, a bass and drums.\r\n"
		"step\r\n"
		"show: channel Triangle\r\n"
		"play: bars 5-8\r\n"
		"solo: Triangle\r\n"
		"text: The bass alone.\r\n"
		"wait: heard\r\n"
		"step\r\n"
		"show: channel vrc6 pulse 1\r\n"
		"bar: 3\r\n"
		"play: bar 12\r\n"
		"mute: Noise, DPCM,VRC6 Plus Soft 2\r\n"
		"instrument: 0A\r\n"
		"text: Without the drums.\r\n"
		"step\r\n"
		"show: mixer\r\n"
		"play: bars 2 - 2\r\n"
		"text: The mixer.\r\n";
	tutorial_t T;
	std::string Error;
	CHECK(ParseTutorial(Text, T, Error));
	CHECK(Error.empty());
	CHECK(T.Steps.size() == 3);
	if (T.Steps.size() != 3)
		return;
	const tutorial_step_t &A = T.Steps[0], &B = T.Steps[1], &C = T.Steps[2];
	CHECK(A.Show == "channel" && A.ShowName == "Triangle");
	CHECK(A.PlayFirst == 5 && A.PlayLast == 8 && A.Bar == 0 && A.CursorBar() == 5);
	CHECK(A.Solo.size() == 1 && A.Solo[0] == "Triangle" && A.Mute.empty());
	CHECK(A.Wait == WAIT_HEARD && A.Instrument == -1);
	CHECK(B.Show == "channel" && B.ShowName == "vrc6 pulse 1");
	CHECK(B.Bar == 3 && B.PlayFirst == 12 && B.PlayLast == 12 && B.CursorBar() == 3);
	CHECK(B.Mute.size() == 3 && B.Mute[0] == "Noise" && B.Mute[1] == "DPCM" && B.Mute[2] == "VRC6 Plus Soft 2");
	CHECK(B.Solo.empty() && B.Instrument == 0x0A && B.Wait == WAIT_NEXT);
	CHECK(C.Show == "mixer" && C.ShowName.empty() && C.PlayFirst == 2 && C.PlayLast == 2);
	// A step without any of them, as before
	CHECK(ParseTutorial("title: T\nstep\ntext: x\n", T, Error));
	CHECK(T.Steps.size() == 1 && T.Steps[0].Bar == 0 && T.Steps[0].PlayFirst == 0 && T.Steps[0].CursorBar() == 0 &&
		T.Steps[0].Instrument == -1 && T.Steps[0].Solo.empty() && T.Steps[0].Mute.empty());
	CHECK(IsTutorialPart("mixer") && IsTutorialPart("channel"));
}

void TestWalkthroughErrors()
{
	struct { const char *Text; const char *Error; } Cases[] = {
		{ "title: T\nstep\ntext: x\nbar: 0\n", "Line 4: \"bar\" takes the bar's number" },
		{ "title: T\nstep\ntext: x\nbar: five\n", "Line 4: \"bar\" takes the bar's number" },
		{ "title: T\nstep\ntext: x\nbar: 5 6\n", "Line 4: \"bar\" takes the bar's number" },
		{ "title: T\nstep\ntext: x\nplay: 5\n", "Line 4: \"play\" takes the bars to play" },
		{ "title: T\nstep\ntext: x\nplay: bars 8-5\n", "Line 4: \"play\" takes the bars to play" },
		{ "title: T\nstep\ntext: x\nplay: bars 5\n", "Line 4: \"play\" takes the bars to play" },
		{ "title: T\nstep\ntext: x\nplay: bar 0\n", "Line 4: \"play\" takes the bars to play" },
		{ "title: T\nstep\ntext: x\nsolo: Triangel\n", "Line 4: there's no channel called \"Triangel\"." },
		{ "title: T\nstep\ntext: x\nmute: Pulse 1,\n", "Line 4: \"mute\" takes channels apart by commas" },
		{ "title: T\nstep\ntext: x\nmute: VRC9 Pulse 1\n", "Line 4: there's no channel called \"VRC9 Pulse 1\"." },
		{ "title: T\nstep\nshow: channel\ntext: x\n", "Line 3: \"channel\" takes the channel's name" },
		{ "title: T\nstep\nshow: channel Kazoo\ntext: x\n", "Line 3: there's no channel called \"Kazoo\"." },
		{ "title: T\nstep\nshow: mixer now\ntext: x\n", "Line 3: \"mixer\" takes nothing after it." },
		{ "title: T\nstep\ntext: x\ninstrument: 40\n", "Line 4: \"instrument\" takes the instrument's number" },
		{ "title: T\nstep\ntext: x\ninstrument: lead\n", "Line 4: \"instrument\" takes the instrument's number" },
		{ "title: T\nplay: bar 1\n", "Line 2: \"play\" belongs to a step" },
		{ "title: T\nstep\ntext: x\nwait: heard\n", "Step 1 waits for its bars to be heard, but has no \"play\" line." },
		{ "title: T\nstep\ntext: x\nsolo: Noise\nmute: DPCM\n", "Step 1 both solos and mutes channels" },
	};
	for (size_t i = 0; i < sizeof(Cases) / sizeof(Cases[0]); ++i) {
		tutorial_t T;
		std::string Error;
		const bool Read = ParseTutorial(Cases[i].Text, T, Error);
		CHECK(!Read);
		const bool Same = Error.compare(0, std::string(Cases[i].Error).size(), Cases[i].Error) == 0;
		CHECK(Same);
		if (Read || !Same)
			std::printf("  walkthrough case %d: \"%s\"\n", static_cast<int>(i), Error.c_str());
	}
}

// The 2A03's channels, then each chip's in FamiTracker's order (as
// ChipSwitch.cpp lists them); the N163's renamed for the VRC6 Plus
std::vector<tutorial_channel_t> SongChannels(int Chips, int Namco, bool Plus)
{
	std::vector<tutorial_channel_t> List;
	const char *const BASE[] = { "Pulse 1", "Pulse 2", "Triangle", "Noise", "DPCM" };
	for (int i = 0; i < 5; ++i)
		List.push_back(tutorial_channel_t(BASE[i], "2A03"));
	if (Chips & 1) {
		const char *const VRC6[] = { "Pulse 1", "Pulse 2", "Sawtooth" };
		for (int i = 0; i < 3; ++i)
			List.push_back(tutorial_channel_t(VRC6[i], "VRC6", Plus));
	}
	if (Chips & 8) {
		List.push_back(tutorial_channel_t("Pulse 1", "MMC5"));
		List.push_back(tutorial_channel_t("Pulse 2", "MMC5"));
	}
	if (Chips & 16) {
		for (int i = 0; i < Namco; ++i) {
			char Name[16];
			std::snprintf(Name, sizeof(Name), Plus ? "Soft %d" : "Namco %d", i + 1);
			List.push_back(tutorial_channel_t(Name, "N163", Plus));
		}
	}
	if (Chips & 4)
		List.push_back(tutorial_channel_t("FDS", "FDS"));
	if (Chips & 2) {
		for (int i = 0; i < 6; ++i) {
			char Name[16];
			std::snprintf(Name, sizeof(Name), "FM Channel %d", i + 1);
			List.push_back(tutorial_channel_t(Name, "VRC7"));
		}
	}
	if (Chips & 32) {
		for (int i = 0; i < 3; ++i) {
			char Name[16];
			std::snprintf(Name, sizeof(Name), "Square %d", i + 1);
			List.push_back(tutorial_channel_t(Name, "5B"));
		}
	}
	return List;
}

void TestChannels()
{
	const std::vector<tutorial_channel_t> Vrc6 = SongChannels(1, 0, false);
	CHECK(FindTutorialChannel("Pulse 1", Vrc6) == 0);
	CHECK(FindTutorialChannel("2A03 Pulse 2", Vrc6) == 1);
	CHECK(FindTutorialChannel("triangle", Vrc6) == 2);
	CHECK(FindTutorialChannel("DPCM", Vrc6) == 4);
	CHECK(FindTutorialChannel("VRC6 Pulse 1", Vrc6) == 5);
	CHECK(FindTutorialChannel("vrc6 pulse 2", Vrc6) == 6);
	CHECK(FindTutorialChannel("Sawtooth", Vrc6) == 7);
	CHECK(FindTutorialChannel("VRC6 Sawtooth", Vrc6) == 7);
	CHECK(FindTutorialChannel("2A03 Sawtooth", Vrc6) == -1);
	CHECK(FindTutorialChannel("MMC5 Pulse 1", Vrc6) == -1);
	CHECK(FindTutorialChannel("Soft 1", Vrc6) == -1);
	CHECK(FindTutorialChannel("VRC6 Plus Pulse 1", Vrc6) == -1);	// not a VRC6 Plus song
	// The VRC6 Plus: its VRC6 channels and the soft layer by either name
	const std::vector<tutorial_channel_t> Plus = SongChannels(1 | 16, 4, true);
	CHECK(Plus.size() == 12);
	CHECK(FindTutorialChannel("Soft 1", Plus) == 8);
	CHECK(FindTutorialChannel("VRC6 Plus Soft 2", Plus) == 9);
	CHECK(FindTutorialChannel("N163 Soft 4", Plus) == 11);
	CHECK(FindTutorialChannel("VRC6 Plus Pulse 1", Plus) == 5);
	CHECK(FindTutorialChannel("VRC6 Pulse 2", Plus) == 6);
	CHECK(FindTutorialChannel("VRC6 Plus Triangle", Plus) == -1);
	CHECK(FindTutorialChannel("Namco 1", Plus) == -1);
	// Several chips: the name the 2A03 has too goes to it, the chip finds the others
	const std::vector<tutorial_channel_t> Many = SongChannels(1 | 8 | 32, 0, false);
	CHECK(FindTutorialChannel("MMC5 Pulse 2", Many) == 9);
	CHECK(FindTutorialChannel("5B Square 3", Many) == 12);
	CHECK(FindTutorialChannel("Square 1", Many) == 10);
	CHECK(IsTutorialChannel("Namco 8") && IsTutorialChannel("FM Channel 6") && IsTutorialChannel("FDS") &&
		IsTutorialChannel("5B Square 2") && IsTutorialChannel("VRC6 Plus Soft 4"));
	CHECK(!IsTutorialChannel("Triangel") && !IsTutorialChannel("Pulse 3") && !IsTutorialChannel("") &&
		!IsTutorialChannel("VRC6"));
}

// What the walkthrough needs to know of an example song: its bars (counted as
// the piano roll's ruler counts them), channels and notes, read from the .ftm
struct cell_t {
	int Note, Instrument, Volume;	// 0 / 0x40 / 0x10 for none; notes 1-12, 13 release, 14 halt
	int Effect[4], Param[4];
	cell_t() : Note(0), Instrument(0x40), Volume(0x10) { for (int i = 0; i < 4; ++i) Effect[i] = Param[i] = 0; }
};

struct song_facts_t {
	int Bars, Length, BarRows, Frames;
	std::vector<tutorial_channel_t> Channels;
	bool Comments;			// shown when it's opened
	std::string Title;
	std::vector<std::vector<cell_t> > Rows;	// each channel's song rows (track 1)
	std::vector<bool> Duty;	// each instrument: plays a duty sequence of its own (2A03, VRC6)
	std::vector<bool> Instruments;	// each instrument: the song has it
	song_facts_t() : Bars(0), Length(0), BarRows(16), Frames(0), Comments(false), Duty(64, false), Instruments(64, false) {}
	int BarStart(int Bar) const {
		const int PerFrame = (Length + BarRows - 1) / BarRows;
		return (Bar - 1) / PerFrame * Length + (Bar - 1) % PerFrame * BarRows;
	}
};

int ReadInt(const std::string &Data, size_t At)
{
	if (At + 4 > Data.size())
		return -1;
	return static_cast<int>(static_cast<unsigned char>(Data[At]) | (static_cast<unsigned char>(Data[At + 1]) << 8) |
		(static_cast<unsigned char>(Data[At + 2]) << 16) | (static_cast<unsigned int>(static_cast<unsigned char>(Data[At + 3])) << 24));
}

// The instruments block: which instruments play a duty sequence of their own
// (index 4 of a 2A03 or VRC6 instrument's sequences); false for a layout it
// doesn't know
bool ReadInstruments(const std::string &Body, song_facts_t &Facts)
{
	const int Count = ReadInt(Body, 0);
	size_t At = 4;
	for (int i = 0; i < Count; ++i) {
		const int Index = ReadInt(Body, At);
		if (Index < 0 || Index >= 64 || At + 9 > Body.size())
			return false;
		const int Type = static_cast<unsigned char>(Body[At + 4]);
		Facts.Instruments[static_cast<size_t>(Index)] = true;
		At += 5;
		if (Type == 1 || Type == 2 || Type == 5 || Type == 6) {
			const int Sequences = ReadInt(Body, At);
			if (Sequences < 0 || Sequences > 5 || At + 4 + 2 * Sequences > Body.size())
				return false;
			if ((Type == 1 || Type == 2) && Sequences > 4)
				Facts.Duty[static_cast<size_t>(Index)] = Body[At + 4 + 2 * 4] != 0;
			At += 4 + 2 * static_cast<size_t>(Sequences);
			if (Type == 1)
				At += 8 * 12 * 3;	// the DPCM samples of each key
			else if (Type == 5) {
				const int Size = ReadInt(Body, At), Waves = ReadInt(Body, At + 8);
				if (Size < 0 || Waves < 0 || Size * Waves > 4096)
					return false;
				At += 12 + static_cast<size_t>(Size * Waves);
			}
		}
		else if (Type == 3)
			At += 4 + 8;			// the VRC7's patch and registers
		else
			return false;			// the FDS's
		const int Name = ReadInt(Body, At);
		if (Name < 0 || Name > 256 || At + 4 + static_cast<size_t>(Name) > Body.size())
			return false;
		At += 4 + static_cast<size_t>(Name);
	}
	return true;
}

bool ReadSong(const std::string &Data, song_facts_t &Facts)
{
	if (Data.size() < 25 || Data.compare(0, 18, "FamiTracker Module") != 0 || Data.compare(Data.size() - 3, 3, "END") != 0)
		return false;
	int Chips = -1, Namco = 0, Second = 0, Available = 0;
	bool Plus = false;
	std::string Header, Frames, Patterns;
	for (size_t At = 22; At + 24 <= Data.size() - 3; ) {
		const std::string Id = Data.substr(At, 16).c_str();
		const int Size = ReadInt(Data, At + 20);
		if (Size < 0 || At + 24 + static_cast<size_t>(Size) > Data.size() - 3)
			return false;
		const std::string Body = Data.substr(At + 24, static_cast<size_t>(Size));
		if (Id == "PARAMS" && Body.size() >= 25) {
			Chips = static_cast<unsigned char>(Body[0]);
			Available = ReadInt(Body, 1);
			Second = ReadInt(Body, 21);
			Namco = (Chips & 16) != 0 ? ReadInt(Body, 25) : 0;
		}
		else if (Id == "INFO" && Body.size() >= 32)
			Facts.Title = Body.substr(0, 32).c_str();
		else if (Id == "HEADER")
			Header = Body;
		else if (Id == "INSTRUMENTS" && !ReadInstruments(Body, Facts))
			return false;
		else if (Id == "FRAMES" && Body.size() >= 16) {
			Facts.Frames = ReadInt(Body, 0);
			Facts.Length = ReadInt(Body, 12);
			Frames = Body;
		}
		else if (Id == "PATTERNS")
			Patterns = Body;
		else if (Id == "COMMENTS" && Body.size() >= 5)
			Facts.Comments = ReadInt(Body, 0) == 1 && Body[4] != 0;
		else if (Id == "BF VRC6PLUS")
			Plus = true;
		At += 24 + static_cast<size_t>(Size);
	}
	if (Chips < 0 || Facts.Frames < 1 || Facts.Length < 1 || Facts.Length > 256 || Available < 5 || Available > 28 || Header.empty())
		return false;
	Facts.BarRows = Second > 0 && Second <= Facts.Length ? Second : 16;
	Facts.Bars = Facts.Frames * ((Facts.Length + Facts.BarRows - 1) / Facts.BarRows);
	Facts.Channels = SongChannels(Chips, Namco, Plus && (Chips & 17) == 17 && Namco == 4);

	// The effect columns each channel shows in track 1 (after the tracks' names)
	const int Tracks = static_cast<unsigned char>(Header[0]) + 1;
	size_t At = 1;
	for (int t = 0; t < Tracks; ++t) {
		At = Header.find('\0', At);
		if (At == std::string::npos)
			return false;
		++At;
	}
	std::vector<int> Columns;
	for (int c = 0; c < Available; ++c, At += 1 + static_cast<size_t>(Tracks)) {
		if (At + 1 >= Header.size())
			return false;
		Columns.push_back(static_cast<unsigned char>(Header[At + 1]) + 1);
	}
	if (Frames.size() < 16 + static_cast<size_t>(Facts.Frames * Available))
		return false;

	// Track 1's patterns, then its song rows frame by frame
	std::vector<std::vector<std::vector<cell_t> > > Pattern(static_cast<size_t>(Available),
		std::vector<std::vector<cell_t> >(128, std::vector<cell_t>(static_cast<size_t>(Facts.Length))));
	for (size_t p = 0; p + 16 <= Patterns.size(); ) {
		const int Track = ReadInt(Patterns, p), Channel = ReadInt(Patterns, p + 4), Index = ReadInt(Patterns, p + 8);
		const int Items = ReadInt(Patterns, p + 12);
		if (Channel < 0 || Channel >= Available || Index < 0 || Index >= 128 || Items < 0)
			return false;
		p += 16;
		const size_t ItemSize = 8 + 2 * static_cast<size_t>(Columns[static_cast<size_t>(Channel)]);
		for (int i = 0; i < Items; ++i, p += ItemSize) {
			if (p + ItemSize > Patterns.size())
				return false;
			const int Row = ReadInt(Patterns, p);
			if (Track != 0 || Row < 0 || Row >= Facts.Length)
				continue;
			cell_t &Cell = Pattern[static_cast<size_t>(Channel)][static_cast<size_t>(Index)][static_cast<size_t>(Row)];
			Cell.Note = static_cast<unsigned char>(Patterns[p + 4]);
			Cell.Instrument = static_cast<unsigned char>(Patterns[p + 6]);
			Cell.Volume = static_cast<unsigned char>(Patterns[p + 7]);
			for (int e = 0; e < Columns[static_cast<size_t>(Channel)] && e < 4; ++e) {
				Cell.Effect[e] = static_cast<unsigned char>(Patterns[p + 8 + 2 * static_cast<size_t>(e)]);
				Cell.Param[e] = static_cast<unsigned char>(Patterns[p + 9 + 2 * static_cast<size_t>(e)]);
			}
		}
	}
	Facts.Rows.assign(static_cast<size_t>(Available), std::vector<cell_t>());
	for (int f = 0; f < Facts.Frames; ++f)
		for (int c = 0; c < Available; ++c) {
			const int Index = static_cast<unsigned char>(Frames[16 + static_cast<size_t>(f * Available + c)]);
			if (Index >= 128)
				return false;
			const std::vector<cell_t> &Rows = Pattern[static_cast<size_t>(c)][static_cast<size_t>(Index)];
			Facts.Rows[static_cast<size_t>(c)].insert(Facts.Rows[static_cast<size_t>(c)].end(), Rows.begin(), Rows.end());
		}
	return true;
}

// What playing from song row Start leaves out that the rows before it set:
// each channel's first note there names its instrument and volume, and sets
// again the lasting effects it would play with (the duty, unless its
// instrument has a duty sequence; the pan, the arpeggio, portamento, vibrato,
// tremolo, detune and volume slide). Empty when nothing is left out.
std::string CarriedIn(const song_facts_t &Song, int Start)
{
	// EF_DUTY_CYCLE, EF_PAN, EF_ARPEGGIO, EF_PORTAMENTO, EF_VIBRATO, EF_TREMOLO, EF_PITCH, EF_VOLUME_SLIDE, and
	// the parameter that's none of them
	const int LASTING[] = { 18, 33, 10, 6, 11, 12, 13, 22 };
	const int OFF[] = { -1, 0x80, 0, 0, 0, 0, 0x80, 0 };
	std::string Problems;
	for (size_t c = 0; c < Song.Rows.size(); ++c) {
		const std::vector<cell_t> &Rows = Song.Rows[c];
		int Instrument = -1, Volume = -1, Lasting[8];
		for (int k = 0; k < 8; ++k)
			Lasting[k] = -1;
		for (int r = 0; r < Start && r < static_cast<int>(Rows.size()); ++r) {
			const cell_t &Cell = Rows[static_cast<size_t>(r)];
			if (Cell.Instrument < 0x40)
				Instrument = Cell.Instrument;
			if (Cell.Volume < 0x10)
				Volume = Cell.Volume;
			for (int e = 0; e < 4; ++e)
				for (int k = 0; k < 8; ++k)
					if (Cell.Effect[e] == LASTING[k])
						Lasting[k] = Cell.Param[e];
		}
		bool Played[8] = { false, false, false, false, false, false, false, false };
		for (int r = Start; r < static_cast<int>(Rows.size()); ++r) {
			const cell_t &Cell = Rows[static_cast<size_t>(r)];
			for (int e = 0; e < 4; ++e)
				for (int k = 0; k < 8; ++k)
					if (Cell.Effect[e] == LASTING[k])
						Played[k] = true;
			if (Cell.Note < 1 || Cell.Note > 12)
				continue;
			char Text[160];
			if (Instrument >= 0 && Cell.Instrument >= 0x40) {
				std::snprintf(Text, sizeof(Text), " channel %d: no instrument (row %d);", static_cast<int>(c + 1), r);
				Problems += Text;
			}
			if (Volume >= 0 && Volume != 15 && Cell.Volume >= 0x10) {
				std::snprintf(Text, sizeof(Text), " channel %d: no volume (row %d);", static_cast<int>(c + 1), r);
				Problems += Text;
			}
			const int Plays = Cell.Instrument < 0x40 ? Cell.Instrument : Instrument;
			for (int k = 0; k < 8; ++k) {
				if (Lasting[k] < 0 || Lasting[k] == OFF[k] || Played[k])
					continue;
				if (LASTING[k] == 18 && Plays >= 0 && Song.Duty[static_cast<size_t>(Plays)])
					continue;
				std::snprintf(Text, sizeof(Text), " channel %d: effect %d %02X from before (row %d);", static_cast<int>(c + 1),
					LASTING[k], Lasting[k], r);
				Problems += Text;
			}
			break;
		}
	}
	return Problems;
}

std::string ReadFile(const std::string &Path)
{
	std::ifstream In(Path.c_str(), std::ios::binary);
	std::stringstream Text;
	Text << In.rdbuf();
	return In.good() || !Text.str().empty() ? Text.str() : std::string();
}

// The example songs that go into FamiTracker.exe (Tutorial.cpp's EXAMPLES):
// each walkthrough read, about 6 to 12 steps, plain ASCII, and every bar,
// channel and instrument it names in its song, a small module that shows its
// comments; the bars a step plays sound as they do in the whole song
// (FamiTracker starts each channel afresh where it starts playing). The sound
// effects take a step for each of their ten sounds, after one that plays them
// all and before two on making your own and getting them into a game: 13.
void TestExamples()
{
	const char *const Names[] = { "first-song", "nes-tricks", "vrc6", "soft-and-wide", "sound-effects" };
	for (size_t i = 0; i < sizeof(Names) / sizeof(Names[0]); ++i) {
		const size_t MostSteps = std::string(Names[i]) == "sound-effects" ? 13 : 12;
		const std::string Base = std::string(EXAMPLES_DIR) + "/" + Names[i];
		const std::string Text = ReadFile(Base + ".txt"), Song = ReadFile(Base + ".ftm");
		CHECK(!Text.empty() && !Song.empty());
		tutorial_t T;
		std::string Error;
		const bool Read = ParseTutorial(Text, T, Error);
		CHECK(Read);
		if (!Read)
			std::printf("  %s.txt: %s\n", Names[i], Error.c_str());
		CHECK(T.Steps.size() >= 6 && T.Steps.size() <= MostSteps && !T.About.empty());
		bool Ascii = true;
		for (size_t k = 0; k < Text.size(); ++k)
			if (static_cast<unsigned char>(Text[k]) > 126)
				Ascii = false;
		CHECK(Ascii);
		song_facts_t Facts;
		const bool Opened = ReadSong(Song, Facts);
		CHECK(Opened);
		if (!Opened) {
			std::printf("  %s.ftm can't be read\n", Names[i]);
			continue;
		}
		CHECK(Song.size() <= 24 * 1024);
		CHECK(Facts.Bars >= 8 && Facts.Bars <= 24);
		CHECK(Facts.Comments && !Facts.Title.empty());
		bool Plays = false;
		for (size_t s = 0; s < T.Steps.size(); ++s) {
			const tutorial_step_t &Step = T.Steps[s];
			const bool Bars = Step.Bar <= Facts.Bars && Step.PlayLast <= Facts.Bars;
			CHECK(Bars);
			if (!Bars)
				std::printf("  %s step %d: a bar past the song's %d\n", Names[i], static_cast<int>(s + 1), Facts.Bars);
			std::vector<std::string> Named(Step.Solo);
			Named.insert(Named.end(), Step.Mute.begin(), Step.Mute.end());
			if (Step.Show == "channel")
				Named.push_back(Step.ShowName);
			for (size_t n = 0; n < Named.size(); ++n) {
				const bool Found = FindTutorialChannel(Named[n], Facts.Channels) >= 0;
				CHECK(Found);
				if (!Found)
					std::printf("  %s step %d: the song has no channel \"%s\"\n", Names[i], static_cast<int>(s + 1), Named[n].c_str());
			}
			// and the instrument it picks
			const bool Instrument = Step.Instrument < 0 || (Step.Instrument < 64 && Facts.Instruments[static_cast<size_t>(Step.Instrument)]);
			CHECK(Instrument);
			if (!Instrument)
				std::printf("  %s step %d: the song has no instrument %02X\n", Names[i], static_cast<int>(s + 1), Step.Instrument);
			Plays = Plays || Step.PlayFirst > 0;
			// The bars play as they do in the whole song
			if (Step.PlayFirst > 0) {
				const std::string Left = CarriedIn(Facts, Facts.BarStart(Step.PlayFirst));
				CHECK(Left.empty());
				if (!Left.empty())
					std::printf("  %s step %d, from bar %d:%s\n", Names[i], static_cast<int>(s + 1), Step.PlayFirst, Left.c_str());
			}
		}
		CHECK(Plays);	// a walkthrough lets the user hear the song
	}
	// The reader itself: a module cut short isn't one
	const std::string Song = ReadFile(std::string(EXAMPLES_DIR) + "/first-song.ftm");
	song_facts_t Facts;
	CHECK(!Song.empty() && !ReadSong(Song.substr(0, Song.size() / 2), Facts));
	// and the check of what playing from a bar leaves out: a volume, a duty and a pan set before it
	song_facts_t Made;
	Made.Length = 32;
	Made.Rows.assign(2, std::vector<cell_t>(64));
	Made.Rows[0][0].Note = 1;
	Made.Rows[0][0].Instrument = 0;
	Made.Rows[0][0].Volume = 9;
	Made.Rows[0][0].Effect[0] = 18;
	Made.Rows[0][0].Param[0] = 2;
	Made.Rows[0][20].Note = 3;
	Made.Rows[0][20].Instrument = 0;
	Made.Rows[1][4].Note = 5;
	Made.Rows[1][4].Instrument = 1;
	Made.Rows[1][4].Effect[0] = 33;
	Made.Rows[1][4].Param[0] = 0xC0;
	Made.Rows[1][40].Note = 5;
	Made.Rows[1][40].Instrument = 1;
	CHECK(CarriedIn(Made, 0).empty());
	const std::string Left = CarriedIn(Made, 16);
	CHECK(Left.find("channel 1: no volume") != std::string::npos && Left.find("channel 1: effect 18 02") != std::string::npos);
	CHECK(Left.find("channel 2: effect 33 C0") != std::string::npos);
	Made.Duty[0] = true;	// an instrument's own duty sequence takes over from V
	CHECK(CarriedIn(Made, 16).find("effect 18") == std::string::npos);
	Made.Rows[0][20].Volume = 9;
	Made.Rows[1][36].Effect[1] = 33;
	Made.Rows[1][36].Param[1] = 0xC0;	// set again before the note
	CHECK(CarriedIn(Made, 16).empty());
}

// Help > Exercises' check steps: check, hint and answer
void TestExerciseLines()
{
	const std::string Text =
		"title: The home note\r\n"
		"about: End a tune at home.\r\n"
		"step\r\n"
		"show: piano-roll\r\n"
		"play: bars 1-4\r\n"
		"check: home-note\r\n"
		"text: End it on the [[home note]].\r\n"
		"hint: The tune is in C major,\r\n"
		"  so its home note is C.\r\n"
		"answer: Pulse 1, bar 3: E3 - D3 - | C3 - - -\r\n"
		"  - - - -\r\n"
		"answer: Triangle, bar 4, Bass: C3 - - - - - - -\r\n"
		"step\r\n"
		"show: what-next\r\n"
		"check: first-song\r\n"
		"text: Make a song.\r\n"
		"step\r\n"
		"show: problems\r\n"
		"text: Done.\r\n"
		"wait: command exercises\r\n";
	tutorial_t T;
	std::string Error;
	CHECK(ParseTutorial(Text, T, Error));
	CHECK(Error.empty());
	CHECK(T.Steps.size() == 3);
	if (T.Steps.size() != 3)
		return;
	const tutorial_step_t &A = T.Steps[0], &B = T.Steps[1], &C = T.Steps[2];
	CHECK(A.Check == "home-note" && A.Wait == WAIT_CHECK);
	CHECK(A.Text == "End it on the [[home note]].");
	CHECK(A.Hint == "The tune is in C major, so its home note is C.");
	CHECK(A.Answers.size() == 2);
	if (A.Answers.size() == 2) {
		CHECK(A.Answers[0].Channel == "Pulse 1" && A.Answers[0].Bar == 3 && A.Answers[0].Notes == "E3 - D3 - | C3 - - - - - - -");
		CHECK(A.Answers[0].LastBar() == 4);
		CHECK(A.Answers[0].Instrument.empty());
		CHECK(A.Answers[1].Channel == "Triangle" && A.Answers[1].Bar == 4 && A.Answers[1].LastBar() == 4);
		CHECK(A.Answers[1].Instrument == "Bass" && A.Answers[1].Notes == "C3 - - - - - - -");
	}
	int First = 0, Last = 0;
	CHECK(A.HearBars(First, Last) && First == 1 && Last == 4);	// the step's own bars
	CHECK(B.Check == "first-song" && B.Wait == WAIT_CHECK && B.Show == "what-next" && B.Hint.empty() && B.Answers.empty());
	CHECK(!B.HearBars(First, Last));
	CHECK(C.Check.empty() && C.Show == "problems" && C.Wait == WAIT_COMMAND && C.Command == "exercises");
	// Without a play line, Hear it plays the answer's bars
	CHECK(ParseTutorial("title: T\nstep\ncheck: hook\ntext: x\nanswer: Pulse 1, bar 3: E3 - | D3 - | C3 -\nanswer: Pulse 2, "
		"bar 2: C3\n", T, Error));
	CHECK(T.Steps.size() == 1 && T.Steps[0].HearBars(First, Last) && First == 2 && Last == 5);
	CHECK(IsTutorialPart("what-next") && IsTutorialPart("problems"));

	struct { const char *Text; const char *Error; } Cases[] = {
		{ "title: T\nstep\ntext: x\ncheck:\n", "Line 4: \"check\" takes the check's name" },
		{ "title: T\nstep\ntext: x\ncheck: Home Note\n", "Line 4: \"check\" takes the check's name" },
		{ "title: T\nstep\ntext: x\nwait: play\ncheck: home-note\n", "Line 5: a step with a check goes on once its check passes" },
		{ "title: T\nstep\ntext: x\ncheck: home-note\nwait: play\n", "Line 5: a step with a check goes on once its check passes" },
		{ "title: T\nstep\ntext: x\ncheck: hook\nanswer: Pulse 1: C3\n", "Line 5: \"answer\" takes a channel, the bar" },
		{ "title: T\nstep\ntext: x\ncheck: hook\nanswer: Pulse 1, bar: C3\n", "Line 5: \"answer\" takes a channel, the bar" },
		{ "title: T\nstep\ntext: x\ncheck: hook\nanswer: Pulse 1, beat 3: C3\n", "Line 5: \"answer\" takes a channel, the bar" },
		{ "title: T\nstep\ntext: x\ncheck: hook\nanswer: Kazoo, bar 3: C3\n", "Line 5: there's no channel called \"Kazoo\"." },
		{ "title: T\nstep\ntext: x\ncheck: hook\nanswer: Pulse 1, bar 3, : C3\n", "Line 5: \"answer\" takes a channel, the bar" },
		{ "title: T\nstep\ntext: x\ncheck: hook\nanswer: Pulse 1, bar 3:\n", "Line 5: \"answer\" takes the notes in steps" },
		{ "title: T\nstep\ntext: x\nanswer: Pulse 1, bar 3: C3\n", "Step 1 has an answer but no check" },
		{ "title: T\nhint: x\n", "Line 2: \"hint\" belongs to a step" },
	};
	for (size_t i = 0; i < sizeof(Cases) / sizeof(Cases[0]); ++i) {
		const bool Read = ParseTutorial(Cases[i].Text, T, Error);
		CHECK(!Read);
		const bool Same = Error.compare(0, std::string(Cases[i].Error).size(), Cases[i].Error) == 0;
		CHECK(Same);
		if (Read || !Same)
			std::printf("  exercise case %d: \"%s\"\n", static_cast<int>(i), Error.c_str());
	}
}

// Help > Exercises' scripts: each read, plain ASCII, a handful of steps with one check or more, every channel and bar
// they name a channel FamiTracker has and a bar from 1 (Source/Api's tests run each check on its starter song, and on
// its answers)
void TestExerciseScripts()
{
	const char *const Names[] = { "first-note", "high-low", "long-short", "steady-beat", "loud-soft", "stairs", "rhythm",
		"copy-repeat", "new-sound", "two-parts", "tiny-tune", "home-note", "four-bars", "strong-notes", "phrases", "bass", "beat",
		"minor", "hook", "song-shape", "first-song" };
	for (size_t i = 0; i < sizeof(Names) / sizeof(Names[0]); ++i) {
		const std::string Text = ReadFile(std::string(EXERCISES_DIR) + "/" + Names[i] + ".txt");
		CHECK(!Text.empty());
		tutorial_t T;
		std::string Error;
		const bool Read = ParseTutorial(Text, T, Error);
		CHECK(Read);
		if (!Read)
			std::printf("  %s.txt: %s\n", Names[i], Error.c_str());
		CHECK(T.Steps.size() >= 2 && T.Steps.size() <= 8 && !T.About.empty());
		bool Ascii = true;
		for (size_t k = 0; k < Text.size(); ++k)
			if (static_cast<unsigned char>(Text[k]) > 126)
				Ascii = false;
		CHECK(Ascii);
		int Checks = 0;
		for (size_t s = 0; s < T.Steps.size(); ++s) {
			const tutorial_step_t &Step = T.Steps[s];
			Checks += Step.Check.empty() ? 0 : 1;
			// A check step lets the user hear something: their own bars (play), a good answer, or the song as it grows
			CHECK(Step.Check.empty() || Step.PlayFirst > 0 || !Step.Answers.empty() || std::string(Names[i]) == "song-shape" ||
				std::string(Names[i]) == "first-song");
			CHECK(Step.Check.empty() || !Step.Hint.empty());	// and every check step has a hint
		}
		CHECK(Checks >= 1);
	}
}

}	// namespace

int main()
{
	TestReading();
	TestWaits();
	TestErrors();
	TestBuiltIn();
	TestWalkthroughLines();
	TestWalkthroughErrors();
	TestChannels();
	TestExamples();
	TestExerciseLines();
	TestExerciseScripts();
	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}

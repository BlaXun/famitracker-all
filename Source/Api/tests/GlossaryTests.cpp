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

// Tests for the glossary (Glossary.h): every entry read (two to four
// sentences, ASCII, no tool names), every see-also and every name the
// tooltips use found, the terms a newcomer meets and every effect there, the
// searches, the effects described with their own numbers, and the Hear it
// examples (each one promised there, in time order, on the right channels,
// every channel ending halted). The explain tool's side is in ApiTests.cpp.

#include "../Glossary.h"
#include "../Effects.h"
#include "../Music.h"
#include "../Song.h"

#include <cctype>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace ftapi;

namespace {

int g_Checks = 0, g_Failures = 0;

void Check(bool Condition, const std::string &What, int Line)
{
	++g_Checks;
	if (!Condition) {
		++g_Failures;
		std::printf("FAIL line %d: %s\n", Line, What.c_str());
	}
}

#define CHECK(x) Check((x), #x, __LINE__)
#define CHECK_EQ(a, b) Check((a) == (b), std::string(#a " == " #b), __LINE__)

bool Contains(const std::string &Text, const std::string &Part)
{
	return Text.find(Part) != std::string::npos;
}

#define CHECK_HAS(text, part) Check(Contains((text), (part)), std::string("\"") + (part) + "\" in " #text ": " + (text), __LINE__)

bool Ascii(const std::string &Text)
{
	for (size_t i = 0; i < Text.size(); ++i)
		if (static_cast<unsigned char>(Text[i]) >= 128 || (Text[i] < 32 && Text[i] != '\n'))
			return false;
	return true;
}

// Sentences: each ends with a full stop (or ! or ?) before a space or the end
int Sentences(const std::string &Text)
{
	int Count = 0;
	for (size_t i = 0; i < Text.size(); ++i)
		if ((Text[i] == '.' || Text[i] == '!' || Text[i] == '?') && (i + 1 == Text.size() || Text[i + 1] == ' '))
			++Count;
	return Count;
}

std::string Squeezed(const std::string &Text)
{
	std::string Out;
	for (size_t i = 0; i < Text.size(); ++i)
		if (std::isalnum(static_cast<unsigned char>(Text[i])))
			Out += static_cast<char>(std::tolower(static_cast<unsigned char>(Text[i])));
	return Out.empty() ? "=" + Text : Out;
}

const GlossaryEntry &Entry(const std::string &Name, int Line)
{
	static GlossaryEntry None;
	const GlossaryEntry *pEntry = FindGlossaryEntry(Name);
	Check(pEntry != 0, "an entry for \"" + Name + "\"", Line);
	return pEntry != 0 ? *pEntry : None;
}

ChannelInfo Channel(int Kind, const char *Chip)
{
	ChannelInfo C;
	C.Kind = Kind;
	C.Chip = Chip;
	C.Name = "Test";
	return C;
}

void TestEntriesRead()
{
	const std::vector<GlossaryEntry> &All = GlossaryEntries();
	CHECK(All.size() > 130);
	std::set<std::string> Ids;
	std::map<std::string, std::string> Keys;	// every id, term and name, to the entry that has it
	int Counts[GLOSSARY_CATEGORIES] = { 0 };
	int Last = 0;
	for (size_t i = 0; i < All.size(); ++i) {
		const GlossaryEntry &E = All[i];
		Check(!E.Id.empty() && !E.Term.empty() && !E.Text.empty(), "an entry has its id, term and text: " + E.Id, __LINE__);
		Check(E.Category >= 0 && E.Category < GLOSSARY_CATEGORIES, E.Id + " has a category", __LINE__);
		Check(E.Category >= Last, E.Id + " comes in its category's place", __LINE__);
		Last = E.Category;
		++Counts[E.Category >= 0 && E.Category < GLOSSARY_CATEGORIES ? E.Category : 0];
		Check(Ids.insert(E.Id).second, "one entry with the id " + E.Id, __LINE__);
		const int Said = Sentences(E.Text);
		Check(Said >= 2 && Said <= 4, E.Id + ": two to four sentences, not " + std::to_string(Said) + ": " + E.Text, __LINE__);
		Check(Ascii(E.Term) && Ascii(E.Text) && Ascii(E.HearHow), E.Id + " is ASCII", __LINE__);
		// Written for the user, not for Claude: no tool names (set_swing, add_echo)
		Check(E.Text.find('_') == std::string::npos && E.HearHow.find('_') == std::string::npos, E.Id + " names no tool",
			__LINE__);
		Check(E.Text.find("  ") == std::string::npos, E.Id + " has single spaces", __LINE__);
		Check(E.Hear.empty() || E.HearHow.empty(), E.Id + ": an example or a way to hear it, not both", __LINE__);
		std::vector<std::string> Own = E.Names;
		Own.push_back(E.Id);
		Own.push_back(E.Term);
		for (size_t n = 0; n < Own.size(); ++n) {
			Check(Ascii(Own[n]) && !Own[n].empty(), E.Id + " has a name in ASCII: " + Own[n], __LINE__);
			const std::string Key = Squeezed(Own[n]);
			std::map<std::string, std::string>::const_iterator Found = Keys.find(Key);
			Check(Found == Keys.end() || Found->second == E.Id, "\"" + Own[n] + "\" names " + E.Id + " and " +
				(Found == Keys.end() ? std::string() : Found->second), __LINE__);
			Keys[Key] = E.Id;
			// Every name finds its entry
			const GlossaryEntry *pFound = FindGlossaryEntry(Own[n]);
			Check(pFound != 0 && pFound->Id == E.Id, "\"" + Own[n] + "\" finds " + E.Id, __LINE__);
		}
		// Every see-also is an entry, not itself
		for (size_t s = 0; s < E.SeeAlso.size(); ++s) {
			const GlossaryEntry *pOther = FindGlossaryEntry(E.SeeAlso[s]);
			Check(pOther != 0 && pOther->Id == E.SeeAlso[s], E.Id + " sees " + E.SeeAlso[s] + ", an entry's id", __LINE__);
			Check(E.SeeAlso[s] != E.Id, E.Id + " doesn't see itself", __LINE__);
		}
		Check(!E.SeeAlso.empty(), E.Id + " sees others", __LINE__);
		// The first sentence for a tooltip
		const std::string First = GlossaryFirstSentence(E);
		Check(!First.empty() && First.size() <= E.Text.size() && Sentences(First) == 1, E.Id + "'s first sentence: " + First,
			__LINE__);
		// Within each category but the effects', in alphabetical order
		if (i > 0 && All[i - 1].Category == E.Category && E.Category != GLOSSARY_EFFECTS)
			Check(Lowercase(All[i - 1].Term) <= Lowercase(E.Term), All[i - 1].Term + " before " + E.Term, __LINE__);
	}
	CHECK(Counts[GLOSSARY_MUSIC] >= 50 && Counts[GLOSSARY_SOUNDS] >= 20 && Counts[GLOSSARY_TRACKER] >= 25 &&
		Counts[GLOSSARY_EFFECTS] >= 30);
	CHECK_EQ(std::string(GlossaryCategoryName(GLOSSARY_MUSIC)), std::string("Making music"));
	CHECK_EQ(std::string(GlossaryCategoryName(GLOSSARY_TRACKER)), std::string("The tracker"));
}

void TestNewcomerTerms()
{
	// The terms the plan names (ROADMAP section 16, the user's "basic music/composing terms"), each where it belongs
	const char *const MUSIC[] = { "note", "pitch", "high and low", "octave", "scale", "key", "major", "minor", "home note",
		"interval", "step", "leap", "chord", "triad", "root", "third", "fifth", "seventh", "chord progression", "melody", "hook",
		"motif", "phrase", "rest", "rhythm", "beat", "bar", "time signature", "4/4", "3/4", "6/8", "tempo", "BPM", "swing",
		"syncopation", "accent", "dynamics", "loud", "staccato", "legato", "bass line", "harmony", "counter-melody", "arpeggio",
		"drums", "drum kit", "kick", "snare", "hi-hat", "crash", "tom", "fill", "groove", "intro", "A part", "B part", "bridge",
		"climax", "break", "outro", "loop", "repetition", "variation", "tension and release", "layers", "echo", "reverb",
		"panning", "stereo", "mix", "balance" };
	for (size_t i = 0; i < sizeof(MUSIC) / sizeof(MUSIC[0]); ++i)
		Check(Entry(MUSIC[i], __LINE__).Category == GLOSSARY_MUSIC, std::string(MUSIC[i]) + " is about making music", __LINE__);
	const char *const SOUNDS[] = { "timbre", "sound", "envelope", "attack", "decay", "sustain", "vibrato", "pitch slide",
		"detune", "pulse width", "duty", "pulse", "triangle", "noise", "sawtooth", "DPCM", "samples", "2A03", "VRC6",
		"VRC6 Plus", "VRC7", "FDS", "N163", "MMC5", "5B", "expansion chip" };
	for (size_t i = 0; i < sizeof(SOUNDS) / sizeof(SOUNDS[0]); ++i)
		Check(Entry(SOUNDS[i], __LINE__).Category == GLOSSARY_SOUNDS, std::string(SOUNDS[i]) + " is about sounds", __LINE__);
	const char *const TRACKER[] = { "channel", "row", "pattern", "frame", "speed", "tempo setting", "highlight", "instrument",
		"sequence", "macro", "volume column", "effect columns", "release", "halt", "===", "---", "piano roll", "sections",
		"clips", "tick", "mixer", "soft", "space" };
	for (size_t i = 0; i < sizeof(TRACKER) / sizeof(TRACKER[0]); ++i)
		Check(Entry(TRACKER[i], __LINE__).Category == GLOSSARY_TRACKER, std::string(TRACKER[i]) + " is the tracker's", __LINE__);
	// Case, spaces and punctuation aside
	CHECK_EQ(Entry("Pulse Width", __LINE__).Id, std::string("pulse-width"));
	CHECK_EQ(Entry("  HI-HAT ", __LINE__).Id, std::string("hi-hat"));
	CHECK_EQ(Entry("chords", __LINE__).Id, std::string("chord"));
	CHECK_EQ(Entry("counter melody", __LINE__).Id, std::string("counter-melody"));
	CHECK_EQ(Entry("portamento", __LINE__).Id, std::string("effect-3xx"));
	CHECK(FindGlossaryEntry("") == 0 && FindGlossaryEntry("xyzzy") == 0);
}

void TestEffects()
{
	// Every effect of the effects tool's table has its entry, in the same words
	std::vector<EffectInfo> Effects = EffectReference();
	const std::vector<EffectInfo> &Song = SongEffectReference();
	Effects.insert(Effects.end(), Song.begin(), Song.end());
	CHECK_EQ(Song.size(), static_cast<size_t>(5));
	for (size_t i = 0; i < Effects.size(); ++i) {
		const EffectInfo &I = Effects[i];
		const std::string Name = I.Name;
		const char *Chip = Name.compare(0, 3, "FDS") == 0 ? "FDS" : Name.compare(0, 2, "5B") == 0 ? "5B" : "2A03";
		const int Kind = std::string(Chip) == "5B" ? KIND_SQUARE : std::string(Chip) == "FDS" ? KIND_OTHER : KIND_PULSE;
		int Effect = EFF_NONE, Param = 0;
		std::string Problem;
		ParseChannelEffect(std::string(1, I.Form[0]) + "00", Channel(Kind, Chip), 0, Effect, Param, Problem);
		if (Effect == EFF_NONE)
			ParseEffect(std::string(1, I.Form[0]) + "00", Effect, Param);
		const std::string Id = EffectGlossaryId(Effect, Channel(Kind, Chip));
		Check(!Id.empty(), std::string(I.Form) + " " + I.Name + " has an entry", __LINE__);
		const GlossaryEntry *pEntry = FindGlossaryEntry(Id);
		Check(pEntry != 0 && pEntry->Category == GLOSSARY_EFFECTS && pEntry->Text == I.Plain && Contains(pEntry->Term, I.Form),
			std::string(I.Form) + " " + I.Name + ": the table's own words", __LINE__);
		const int Said = Sentences(I.Plain);
		Check(Said >= 2 && Said <= 4 && Ascii(I.Plain), std::string(I.Form) + ": two to four sentences in ASCII", __LINE__);
		Check(pEntry != 0 && (!pEntry->Hear.empty() || !pEntry->HearHow.empty()), std::string(I.Form) + " can be heard", __LINE__);
	}
	// Every effect FamiTracker plays (each with a letter) is described, on a channel of its chip
	for (int Effect = EFF_NONE + 1; Effect < EFF_COUNT; ++Effect) {
		if (EffectLetter(Effect) == 0)
			continue;
		const bool Fds = Effect == EFF_FDS_MOD_DEPTH || Effect == EFF_FDS_MOD_SPEED_HI || Effect == EFF_FDS_MOD_SPEED_LO;
		const bool S5B = Effect == EFF_SUNSOFT_ENV_TYPE || Effect == EFF_SUNSOFT_ENV_HI || Effect == EFF_SUNSOFT_ENV_LO;
		const ChannelInfo C = Channel(Fds ? KIND_OTHER : S5B ? KIND_SQUARE : KIND_PULSE, Fds ? "FDS" : S5B ? "5B" : "2A03");
		Check(!EffectGlossaryId(Effect, C).empty(), "an entry for " + EffectText(Effect, 0), __LINE__);
		Check(!DescribeEffect(Effect, 0x12, C).empty() && Ascii(DescribeEffect(Effect, 0x12, C)), "words for " +
			EffectText(Effect, 0x12), __LINE__);
	}
	// By what's written
	CHECK_EQ(Entry("4xy", __LINE__).Id, std::string("effect-4xy"));
	CHECK_EQ(Entry("447", __LINE__).Id, std::string("effect-4xy"));
	CHECK_EQ(Entry("Q31", __LINE__).Id, std::string("effect-qxy"));
	CHECK_EQ(Entry("q", __LINE__).Id, std::string("effect-qxy"));
	CHECK_EQ(Entry("4", __LINE__).Id, std::string("effect-4xy"));
	CHECK_EQ(Entry("H", __LINE__).Id, std::string("effect-hxy"));
	CHECK_EQ(Entry("FDS Hxx", __LINE__).Id, std::string("effect-hxx-fds"));
	CHECK_EQ(Entry("5b hxy", __LINE__).Id, std::string("effect-hxy-5b"));
	CHECK_EQ(Entry("tremolo", __LINE__).Id, std::string("effect-7xy"));
	CHECK_EQ(Entry("Fxx", __LINE__).Id, std::string("effect-fxx"));
	CHECK_EQ(Entry("vibrato", __LINE__).Id, std::string("vibrato"));	// the sound's own, which leads to 4xy
	CHECK(FindGlossaryEntry("add") == 0 || FindGlossaryEntry("add")->Id != "effect-axy");
	// On the FDS and the 5B, H, I and J are theirs
	CHECK_EQ(EffectGlossaryId(EFF_SWEEPUP, Channel(KIND_OTHER, "FDS")), std::string("effect-hxx-fds"));
	CHECK_EQ(EffectGlossaryId(EFF_FDS_MOD_SPEED_LO, Channel(KIND_SQUARE, "5B")), std::string("effect-jxx-5b"));
	CHECK_EQ(EffectGlossaryId(EFF_SWEEPUP, Channel(KIND_PULSE, "2A03")), std::string("effect-hxy"));
	CHECK_EQ(EffectGlossaryId(EFF_PAN, Channel(KIND_NOISE, "2A03")), std::string("effect-8xx"));
}

void TestDescriptions()
{
	const ChannelInfo Pulse = Channel(KIND_PULSE, "2A03");
	CHECK_EQ(DescribeEffect(EFF_VIBRATO, 0x47, Pulse), std::string("Vibrato: the pitch wobbles, speed 4, depth 7"));
	CHECK_EQ(DescribeEffect(EFF_VIBRATO, 0x40, Pulse), std::string("Vibrato off"));
	CHECK_HAS(DescribeEffect(EFF_ARPEGGIO, 0x47, Pulse), "a major chord");
	CHECK_HAS(DescribeEffect(EFF_ARPEGGIO, 0x37, Pulse), "a minor chord");
	CHECK_HAS(DescribeEffect(EFF_ARPEGGIO, 0x00, Pulse), "off");
	CHECK_HAS(DescribeEffect(EFF_ARPEGGIO, 0x29, Pulse), "2 and 9 semitones");
	CHECK_EQ(DescribeEffect(EFF_PAN, 0x00, Pulse), std::string("Pan: hard left"));
	CHECK_EQ(DescribeEffect(EFF_PAN, 0x80, Pulse), std::string("Pan: the middle"));
	CHECK_EQ(DescribeEffect(EFF_PAN, 0xFF, Pulse), std::string("Pan: hard right"));
	CHECK_HAS(DescribeEffect(EFF_PAN, 0x40, Pulse), "left, 50%");
	CHECK_HAS(DescribeEffect(EFF_DUTY_CYCLE, 0x02, Pulse), "50%");
	CHECK_HAS(DescribeEffect(EFF_DUTY_CYCLE, 0x03, Channel(KIND_PULSE, "VRC6")), "4/16");
	CHECK_HAS(DescribeEffect(EFF_DUTY_CYCLE, 0x01, Channel(KIND_NOISE, "2A03")), "metallic");
	CHECK_HAS(DescribeEffect(EFF_DUTY_CYCLE, 0x05, Channel(KIND_OTHER, "N163")), "wave 5");
	CHECK_HAS(DescribeEffect(EFF_DUTY_CYCLE, 0x03, Channel(KIND_SQUARE, "5B")), "the square and the noise");
	CHECK_EQ(DescribeEffect(EFF_SPEED, 0x06, Pulse), std::string("Speed 6: each row lasts 6 ticks"));
	CHECK_EQ(DescribeEffect(EFF_SPEED, 0x96, Pulse), std::string("Tempo 150"));
	CHECK_EQ(DescribeEffect(EFF_SPEED, 0x18, Pulse, 0x10), std::string("Tempo 24"));		// the module's own split point
	CHECK_HAS(DescribeEffect(EFF_SLIDE_UP, 0x31, Pulse), "1 semitone up, speed 3");
	CHECK_HAS(DescribeEffect(EFF_SLIDE_DOWN, 0x5C, Pulse), "12 semitones down");
	CHECK_HAS(DescribeEffect(EFF_NOTE_CUT, 0x03, Pulse), "after 3 ticks");
	CHECK_HAS(DescribeEffect(EFF_DELAY, 0x01, Pulse), "1 tick late");
	CHECK_HAS(DescribeEffect(EFF_VOLUME_SLIDE, 0x02, Pulse), "fading down");
	CHECK_HAS(DescribeEffect(EFF_VOLUME_SLIDE, 0x30, Pulse), "swelling up");
	CHECK_HAS(DescribeEffect(EFF_PITCH, 0x82, Pulse), "higher, 2 steps");
	CHECK_HAS(DescribeEffect(EFF_PITCH, 0x7F, Pulse), "lower, 1 step");
	CHECK_HAS(DescribeEffect(EFF_SWEEPUP, 0x71, Pulse), "rises by itself");
	CHECK_HAS(DescribeEffect(EFF_SWEEPUP, 0x71, Channel(KIND_PULSE, "VRC6")), "only the 2A03's pulses sweep");
	CHECK_HAS(DescribeEffect(EFF_SWEEPUP, 0x20, Channel(KIND_OTHER, "FDS")), "FDS modulation depth: 32");
	CHECK_HAS(DescribeEffect(EFF_SWEEPUP, 0xC8, Channel(KIND_SQUARE, "5B")), "a falling saw");
	CHECK_HAS(DescribeEffect(EFF_SUNSOFT_ENV_TYPE, 0x0E, Channel(KIND_SQUARE, "5B")), "Ixx and Jxx");
	CHECK_HAS(DescribeEffect(EFF_JUMP, 0x03, Pulse), "frame 03");
	CHECK_HAS(DescribeEffect(EFF_HALT, 0x00, Pulse), "stops");
	CHECK_HAS(DescribeEffect(EFF_DAC, 0x30, Channel(KIND_DPCM, "2A03")), "48 of 127");
	CHECK_HAS(DescribeEffect(EFF_SAMPLE_OFFSET, 0x02, Channel(KIND_DPCM, "2A03")), "128 bytes");
	CHECK(DescribeEffect(EFF_NONE, 0, Pulse).empty());
}

void TestSearches()
{
	std::vector<const GlossaryEntry *> Found = SearchGlossary("chord");
	CHECK(!Found.empty() && Found[0]->Id == "chord");
	Found = SearchGlossary("duty");
	CHECK(!Found.empty() && Found[0]->Id == "pulse-width");
	Found = SearchGlossary("wobble");
	bool Vibrato = false;
	for (size_t i = 0; i < Found.size(); ++i)
		Vibrato = Vibrato || Found[i]->Id == "vibrato";
	CHECK(Vibrato);
	// Words found in the text too, after the titles
	Found = SearchGlossary("drum");
	std::set<std::string> Ids;
	for (size_t i = 0; i < Found.size(); ++i)
		Ids.insert(Found[i]->Id);
	CHECK(Ids.count("drum-kit") && Ids.count("kick") && Ids.count("snare") && Ids.count("fill") && Ids.count("noise"));
	CHECK(!Found.empty() && Found[0]->Id == "drum-kit");
	// A category alone, in its order; nothing found for nonsense
	Found = SearchGlossary("", GLOSSARY_EFFECTS);
	CHECK(Found.size() >= 30 && Found[0]->Id == "effect-0xy");
	for (size_t i = 0; i < Found.size(); ++i)
		Check(Found[i]->Category == GLOSSARY_EFFECTS, Found[i]->Id + " is an effect", __LINE__);
	CHECK_EQ(SearchGlossary("").size(), GlossaryEntries().size());
	CHECK(SearchGlossary("zzzqqq").empty());
	Found = SearchGlossary("slide", GLOSSARY_EFFECTS);
	CHECK(!Found.empty());
	for (size_t i = 0; i < Found.size(); ++i)
		Check(Found[i]->Category == GLOSSARY_EFFECTS, Found[i]->Id + ": only effects", __LINE__);
	// Two words both found
	Found = SearchGlossary("major minor");
	CHECK(!Found.empty() && Found[0]->Id == "major-minor");
	Found = SearchGlossary("447");
	CHECK(!Found.empty() && Found[0]->Id == "effect-4xy");
}

void TestTooltipLabels()
{
	// Every label FamiTracker explains has its entry, and the plan's labels are there
	const std::vector<GlossaryLabel> &All = GlossaryLabels();
	for (size_t i = 0; i < All.size(); ++i) {
		const GlossaryEntry *pEntry = FindGlossaryEntry(All[i].Id);
		Check(pEntry != 0 && pEntry->Id == All[i].Id, std::string(All[i].Where) + " " + All[i].Label + ": " + All[i].Id, __LINE__);
		CHECK_EQ(GlossaryLabelId(All[i].Where, All[i].Label), std::string(All[i].Id));
	}
	CHECK_EQ(GlossaryLabelId("song settings", "Speed"), std::string("speed"));
	CHECK_EQ(GlossaryLabelId("song settings", "Tempo"), std::string("tempo-setting"));
	CHECK_EQ(GlossaryLabelId("song settings", "BPM"), std::string("tempo"));
	CHECK_EQ(GlossaryLabelId("song settings", "Rows"), std::string("pattern-length"));
	CHECK_EQ(GlossaryLabelId("song settings", "Frames"), std::string("frame"));
	CHECK_EQ(GlossaryLabelId("instrument editor", "Volume"), std::string("volume-sequence"));
	CHECK_EQ(GlossaryLabelId("instrument editor", "Arpeggio"), std::string("arpeggio-sequence"));
	CHECK_EQ(GlossaryLabelId("instrument editor", "Pitch"), std::string("pitch-sequence"));
	CHECK_EQ(GlossaryLabelId("instrument editor", "Hi-Pitch"), std::string("hi-pitch-sequence"));
	CHECK_EQ(GlossaryLabelId("instrument editor", "Duty / Noise"), std::string("duty-sequence"));
	CHECK_EQ(GlossaryLabelId("mixer", "Soft"), std::string("soft"));
	CHECK_EQ(GlossaryLabelId("mixer", "Space"), std::string("space"));
	CHECK_EQ(GlossaryLabelId("piano roll", "Key"), std::string("key"));
	CHECK_EQ(GlossaryLabelId("piano roll", "Chords"), std::string("chord"));
	CHECK(GlossaryLabelId("mixer", "Key").empty() && GlossaryLabelId("song settings", "Nothing").empty());
}

void CheckExample(const HearExample &E)
{
	Check(!E.Says.empty() && Ascii(E.Says) && Sentences(E.Says) >= 1, E.Id + " says what it plays", __LINE__);
	Check(!E.Events.empty() && E.Length() > 300 && E.Length() < 10000, E.Id + " is short: " + std::to_string(E.Length()) +
		" ms", __LINE__);
	std::map<int, int> Last;	// by voice: the last event's time
	std::map<int, int> LastNote;
	for (size_t i = 0; i < E.Events.size(); ++i) {
		const HearEvent &V = E.Events[i];
		Check(i == 0 || V.At >= E.Events[i - 1].At, E.Id + ": in time order", __LINE__);
		Check(V.At >= 0 && V.Voice >= 0 && V.Voice < HEAR_VOICES, E.Id + ": a time and a voice", __LINE__);
		Check(V.Volume >= -1 && V.Volume <= 15, E.Id + ": a volume 0-15", __LINE__);
		Check(V.Note == HEAR_HALT || V.Note == HEAR_NONE || (V.Note >= 0 && V.Note <= (V.Voice == HEAR_NOISE ? 15 : 95)),
			E.Id + ": a note in range", __LINE__);
		Check(V.Effect == EFF_NONE || (V.Effect > EFF_NONE && V.Effect < EFF_COUNT && EffectLetter(V.Effect) != 0 &&
			!IsSongEffect(V.Effect)), E.Id + ": an effect a channel plays", __LINE__);
		Check(V.Note != HEAR_NONE || V.Volume >= 0 || V.Effect != EFF_NONE, E.Id + ": an event that does something", __LINE__);
		// A channel takes a note a frame: its events are apart
		std::map<int, int>::const_iterator Before = Last.find(V.Voice);
		Check(Before == Last.end() || V.At - Before->second >= 20, E.Id + ": voice " + std::to_string(V.Voice) +
			"'s events 20 ms apart or more, at " + std::to_string(V.At), __LINE__);
		Last[V.Voice] = V.At;
		if (V.Note >= 0)
			LastNote[V.Voice] = V.At;
		// The chips' voices only in examples that ask for the chip
		const bool Own = V.Voice <= HEAR_NOISE || V.Voice == HEAR_HERE;
		Check(Own || !E.Chip.empty(), E.Id + ": another chip's voice names its chip", __LINE__);
	}
	// Every voice ends halted, its last event at the example's end or before
	std::map<int, bool> Halted;
	for (size_t i = 0; i < E.Events.size(); ++i)
		Halted[E.Events[i].Voice] = E.Events[i].Note == HEAR_HALT || (Halted[E.Events[i].Voice] && E.Events[i].Note == HEAR_NONE);
	for (std::map<int, bool>::const_iterator h = Halted.begin(); h != Halted.end(); ++h)
		Check(h->second, E.Id + ": voice " + std::to_string(h->first) + " ends halted", __LINE__);
}

bool HasEvent(const HearExample &E, int Voice, int Note, int Effect = -1, int Param = -1)
{
	for (size_t i = 0; i < E.Events.size(); ++i)
		if (E.Events[i].Voice == Voice && (Note == HEAR_NONE || E.Events[i].Note == Note) &&
			(Effect < 0 || E.Events[i].Effect == Effect) && (Param < 0 || E.Events[i].Param == Param))
			return true;
	return false;
}

int Pitch(const char *Name)
{
	int Out = -1;
	ParsePitch(Name, Out);
	return Out;
}

void TestExamples()
{
	// Every example an entry names exists and plays well
	const std::vector<std::string> Ids = GlossaryExampleIds();
	CHECK(Ids.size() >= 70);
	for (size_t i = 0; i < Ids.size(); ++i) {
		HearExample E;
		Check(GlossaryExample(Ids[i], E), "the example " + Ids[i], __LINE__);
		CHECK_EQ(E.Id, Ids[i]);
		CheckExample(E);
	}
	HearExample None;
	CHECK(!GlossaryExample("nothing", None));
	// The ones the plan promises, each where it was promised
	const struct { const char *Entry, *Example; } PROMISED[] = {
		{ "major and minor", "major-minor" }, { "scale", "scale" }, { "step and leap", "step-leap" },
		{ "staccato and legato", "staccato-legato" }, { "swing", "swing" }, { "vibrato", "vibrato" }, { "pitch slide", "slide" },
		{ "pulse width", "pulse-width" }, { "phrase", "phrase" }, { "fill", "fill" }, { "chord", "chord" }, { "echo", "echo" },
		{ "4xy", "effect-4xy" }, { "Qxy", "effect-qxy" }, { "0xy", "effect-0xy" }, { "Vxx", "effect-vxx" },
	};
	for (size_t i = 0; i < sizeof(PROMISED) / sizeof(PROMISED[0]); ++i)
		Check(Entry(PROMISED[i].Entry, __LINE__).Hear == PROMISED[i].Example, std::string(PROMISED[i].Entry) + " plays " +
			PROMISED[i].Example, __LINE__);
	// What they play
	HearExample E;
	CHECK(GlossaryExample("major-minor", E));
	CHECK(HasEvent(E, HEAR_PULSE1, Pitch("E-3")) && HasEvent(E, HEAR_PULSE1, Pitch("Eb3")) && E.Chip.empty());
	CHECK(GlossaryExample("pulse-width", E));
	for (int w = 0; w < 4; ++w)
		Check(HasEvent(E, HEAR_PULSE1, HEAR_NONE, EFF_DUTY_CYCLE, w), "width " + std::to_string(w), __LINE__);
	CHECK(GlossaryExample("vibrato", E) && HasEvent(E, HEAR_PULSE1, HEAR_NONE, EFF_VIBRATO));
	CHECK(GlossaryExample("slide", E) && HasEvent(E, HEAR_PULSE1, HEAR_NONE, EFF_SLIDE_UP) &&
		HasEvent(E, HEAR_PULSE1, HEAR_NONE, EFF_PORTAMENTO));
	{
		// Swing: the second half's off-beat notes come later than the first half's
		CHECK(GlossaryExample("swing", E));
		std::vector<int> Notes;
		for (size_t i = 0; i < E.Events.size(); ++i)
			if (E.Events[i].Voice == HEAR_PULSE1 && E.Events[i].Note >= 0)
				Notes.push_back(E.Events[i].At);
		CHECK_EQ(Notes.size(), static_cast<size_t>(32));
		if (Notes.size() == 32) {
			CHECK_EQ(Notes[1] - Notes[0], 250);		// straight: even
			CHECK_EQ(Notes[17] - Notes[16], 333);		// swung: long, then short
			CHECK_EQ(Notes[18] - Notes[17], 167);
		}
	}
	// The chips' examples need their chips
	const struct { const char *Id, *Chip; int Voice; } CHIPS[] = {
		{ "sawtooth", "VRC6", HEAR_SAWTOOTH }, { "vrc6", "VRC6", HEAR_VRC6_PULSE }, { "vrc6-plus", "VRC6 Plus", HEAR_N163 },
		{ "vrc7", "VRC7", HEAR_VRC7 }, { "fds", "FDS", HEAR_FDS }, { "n163", "N163", HEAR_N163 }, { "mmc5", "MMC5", HEAR_MMC5 },
		{ "5b", "5B", HEAR_5B }, { "effect-hxy-5b", "5B", HEAR_5B },
	};
	for (size_t i = 0; i < sizeof(CHIPS) / sizeof(CHIPS[0]); ++i) {
		CHECK(GlossaryExample(CHIPS[i].Id, E));
		Check(E.Chip == CHIPS[i].Chip && HasEvent(E, CHIPS[i].Voice, HEAR_NONE), std::string(CHIPS[i].Id) + " on the " +
			CHIPS[i].Chip, __LINE__);
		CHECK_HAS(HearChipHow(E.Chip), "Sound browser");
	}
	// The entries of the chips and channels each have one
	const char *const HEARD[] = { "2A03", "VRC6", "VRC6 Plus", "VRC7", "FDS", "N163", "MMC5", "5B", "pulse", "triangle", "noise",
		"sawtooth", "kick", "snare", "hi-hat", "crash", "tom", "groove", "echo", "panning", "tension and release", "clash" };
	for (size_t i = 0; i < sizeof(HEARD) / sizeof(HEARD[0]); ++i)
		Check(!Entry(HEARD[i], __LINE__).Hear.empty(), std::string(HEARD[i]) + " has an example", __LINE__);
	// No example where there's no sound to hear, but the way to hear it
	CHECK(Entry("reverb", __LINE__).Hear.empty() && !Entry("reverb", __LINE__).HearHow.empty());
	CHECK(Entry("dpcm", __LINE__).Hear.empty() && Contains(Entry("dpcm", __LINE__).HearHow, "Sound browser"));
	CHECK(Entry("Zxx", __LINE__).Hear.empty() && !Entry("Zxx", __LINE__).HearHow.empty());
	CHECK(Entry("Fxx", __LINE__).Hear.empty() && !Entry("Fxx", __LINE__).HearHow.empty());
}

void TestEffectHearing()
{
	const ChannelInfo Pulse = Channel(KIND_PULSE, "2A03");
	HearExample E;
	std::string Why;
	// With its own numbers, on its channel: plain, then with it
	CHECK(EffectHearExample(EFF_VIBRATO, 0x47, Pulse, Pitch("E-3"), 10, E, Why) && Why.empty());
	CheckExample(E);
	CHECK(HasEvent(E, HEAR_HERE, Pitch("E-3"), EFF_NONE) && HasEvent(E, HEAR_HERE, Pitch("E-3"), EFF_VIBRATO, 0x47));
	CHECK_HAS(E.Says, "447");
	CHECK_HAS(E.Says, "speed 4, depth 7");
	for (size_t i = 0; i < E.Events.size(); ++i)
		Check(E.Events[i].Voice == HEAR_HERE, "on the effect's own channel", __LINE__);
	// The slides and the arpeggio too, the cut on every note, the delay against a beat
	CHECK(EffectHearExample(EFF_ARPEGGIO, 0x37, Pulse, -1, -1, E, Why) && HasEvent(E, HEAR_HERE, Pitch("C-3"), EFF_ARPEGGIO, 0x37));
	CheckExample(E);
	CHECK(EffectHearExample(EFF_NOTE_CUT, 0x02, Pulse, Pitch("C-4"), 12, E, Why));
	CheckExample(E);
	CHECK(EffectHearExample(EFF_DELAY, 0x03, Pulse, Pitch("C-4"), 12, E, Why) && HasEvent(E, HEAR_NOISE, HEAR_NONE));
	CheckExample(E);
	CHECK(EffectHearExample(EFF_DELAY, 0x03, Channel(KIND_NOISE, "2A03"), 0x8, 12, E, Why) && !HasEvent(E, HEAR_NOISE, HEAR_NONE));
	CHECK(EffectHearExample(EFF_PORTAMENTO, 0x08, Pulse, Pitch("C-3"), 12, E, Why));
	CheckExample(E);
	CHECK(EffectHearExample(EFF_DPCM_PITCH, 0x0F, Channel(KIND_DPCM, "2A03"), Pitch("C-3"), -1, E, Why));
	CHECK(EffectHearExample(EFF_SWEEPUP, 0x20, Channel(KIND_OTHER, "FDS"), Pitch("C-5"), -1, E, Why) &&
		HasEvent(E, HEAR_HERE, HEAR_NONE, EFF_FDS_MOD_DEPTH, 0x20));
	// Not as a note: the song's own effects, an effect turned off, the middle, the DPCM's level, a sweep off the 2A03
	CHECK(!EffectHearExample(EFF_SPEED, 0x06, Pulse, -1, -1, E, Why) && Contains(Why, "whole song"));
	CHECK(!EffectHearExample(EFF_VIBRATO, 0x00, Pulse, -1, -1, E, Why) && Contains(Why, "off"));
	CHECK(!EffectHearExample(EFF_PAN, 0x80, Pulse, -1, -1, E, Why) && Contains(Why, "middle"));
	CHECK(!EffectHearExample(EFF_DAC, 0x30, Channel(KIND_DPCM, "2A03"), -1, -1, E, Why) && !Why.empty());
	CHECK(!EffectHearExample(EFF_SWEEPUP, 0x71, Channel(KIND_PULSE, "VRC6"), -1, -1, E, Why) && Contains(Why, "2A03"));
	CHECK(!EffectHearExample(EFF_NONE, 0, Pulse, -1, -1, E, Why));
	CHECK(!EffectHearExample(EFF_VOLUME, 0x05, Pulse, -1, -1, E, Why) && Contains(Why, "volume column"));
}

void TestExplainText()
{
	const std::string Chord = GlossaryText(Entry("chord", __LINE__));
	CHECK_HAS(Chord, "Chord (also: chords");
	CHECK_HAS(Chord, "Making music.");
	CHECK_HAS(Chord, "Hear it");
	CHECK_HAS(Chord, "See also: Triad, Root, Chord progression");
	CHECK(Ascii(Chord));
	CHECK_HAS(GlossaryText(Entry("reverb", __LINE__)), "To hear it: Turn a channel's Space knob up");
	CHECK_HAS(GlossaryText(Entry("sawtooth", __LINE__)), "It needs the VRC6.");
	CHECK_HAS(GlossaryText(Entry("4xy", __LINE__)), "Vibrato (4xy)");
	CHECK_EQ(GlossaryFirstSentence(Entry("chord", __LINE__)), std::string("A chord is three or more notes played together, "
		"like a hand on the piano keys."));
	// Every entry's text for the tool reads
	const std::vector<GlossaryEntry> &All = GlossaryEntries();
	for (size_t i = 0; i < All.size(); ++i) {
		const std::string Text = GlossaryText(All[i]);
		Check(Ascii(Text) && Contains(Text, All[i].Text), All[i].Id + "'s text for the tool", __LINE__);
	}
}

}	// namespace

int main()
{
	std::setvbuf(stdout, 0, _IONBF, 0);
	TestEntriesRead();
	TestNewcomerTerms();
	TestEffects();
	TestDescriptions();
	TestSearches();
	TestTooltipLabels();
	TestExamples();
	TestEffectHearing();
	TestExplainText();
	std::printf("%d checks, %d failed\n", g_Checks, g_Failures);
	return g_Failures == 0 ? 0 : 1;
}

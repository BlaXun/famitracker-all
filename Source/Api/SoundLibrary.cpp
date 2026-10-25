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

#include "SoundLibrary.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <map>

namespace ftapi {

namespace {

std::vector<std::string> &Problems()
{
	static std::vector<std::string> List;
	return List;
}

std::string Simplified(const std::string &Text)
{
	std::string Out;
	for (size_t i = 0; i < Text.size(); ++i) {
		const unsigned char c = static_cast<unsigned char>(Text[i]);
		if (std::isalnum(c))
			Out += static_cast<char>(std::tolower(c));
	}
	return Out;
}

// A sound written in a few words, each a part of it:
//   a<n> f<n> p<n> d<n> s<n> r<n>	its envelope (SoundDesign): the attack's frames, the level it starts from, the peak,
//									the decay's frames, the level it holds (0: it dies away) and the release's frames
//   steps, x<n>, e					the decay in steps; struck again every n frames while it holds; an echo after it
//   u<n>, w<n>						the 2A03's duty (0-3), the VRC6's (0-7)
//   sweep, wobble, fade, k			the duty opening on the attack, moving to and fro, thinning as it fades; a duty click
//   blip, scoop, drop, flick, thump	the attack's pitch
//   arp<a,b,...>@<n>				a chord arpeggio looped, n frames a note
//   vib<1-3|faint|soft>@<delay>/<rate>	a vibrato that waits delay frames: gentle, medium or strong (the period a step a
//									frame either way), faint (a step flatter half the time) or soft (two steps flatter half
//									the time: on the 2A03's pulses, neither takes a note across a multiple of 256, so neither
//									clicks)
//   short							meant to be short: its notes are gone within a few frames, on purpose
//   V: A: P: H: D: W: M:			a sequence of its own in place of the design's (volume, arpeggio, pitch, hi-pitch, the
//									2A03's duty or the noise's mode, the VRC6's duty, the 5B's noise and mode): values apart by
//									commas ("15x6" six of them), then /L<n> its loop, /R<n> its release, /F fixed notes; the
//									5B's modes as s (the square), n<pitch> (the noise), e (the envelope), together ("sn20")
struct Spec {
	SoundDesign D;
	bool Designed;
	bool Short;							// meant to be short
	int SoftDelay, SoftRate;			// a soft vibrato (SoftRate 0: none)
	std::map<char, PresetSequence> Own;	// sequences of its own, by letter
	Spec() : Designed(false), Short(false), SoftDelay(0), SoftRate(0)
	{
		// What the words leave out: struck at once at full, held there, a short release, a 50% pulse
		D.Attack = 0;
		D.AttackFrom = 0;
		D.Peak = 15;
		D.Decay = 0;
		D.Sustain = 15;
		D.Release = 4;
		D.Duty = 2;
		D.Vibrato = 0;
		D.VibratoDelay = 0;
		D.VibratoRate = 12;
	}
};

int Number(const std::string &Text, size_t From, const std::string &Name)
{
	if (From >= Text.size() || Text.find_first_not_of("-0123456789", From) != std::string::npos) {
		Problems().push_back(Name + ": \"" + Text + "\" isn't a number");
		return 0;
	}
	return std::atoi(Text.c_str() + From);
}

// A 5B mode in letters: s, n<pitch>, e, together
int ModeValue(const std::string &Item, const std::string &Name)
{
	if (!Item.empty() && (std::isdigit(static_cast<unsigned char>(Item[0])) || Item[0] == '-'))
		return Number(Item, 0, Name);
	int Value = 0;
	for (size_t i = 0; i < Item.size(); ++i) {
		if (Item[i] == 's')
			Value |= S5B_SQUARE;
		else if (Item[i] == 'e')
			Value |= S5B_ENVELOPE;
		else if (Item[i] == 'n') {
			size_t End = i + 1;
			while (End < Item.size() && std::isdigit(static_cast<unsigned char>(Item[End])))
				++End;
			Value |= S5B_NOISE | (End > i + 1 ? std::max(0, std::min(31, std::atoi(Item.substr(i + 1, End - i - 1).c_str()))) : 0);
			i = End - 1;
		}
		else
			Problems().push_back(Name + ": the 5B mode \"" + Item + "\"");
	}
	return Value;
}

PresetSequence ReadOwn(int Type, const std::string &Text, bool Mode, const std::string &Name)
{
	PresetSequence Seq;
	Seq.Type = Type;
	Seq.Loop = -1;
	Seq.Release = -1;
	Seq.Setting = SEQUENCE_ABSOLUTE;
	size_t Slash = Text.find('/');
	const std::string Values = Text.substr(0, Slash);
	size_t At = 0;
	while (At < Values.size()) {
		size_t Comma = Values.find(',', At);
		if (Comma == std::string::npos)
			Comma = Values.size();
		const std::string Item = Values.substr(At, Comma - At);
		At = Comma + 1;
		if (Item.empty())
			continue;
		const size_t Times = Item.find('x');
		const std::string One = Times != std::string::npos ? Item.substr(0, Times) : Item;
		const int Count = Times != std::string::npos ? Number(Item, Times + 1, Name) : 1;
		const int Value = Mode ? ModeValue(One, Name) : Number(One, 0, Name);
		Seq.Items.insert(Seq.Items.end(), static_cast<size_t>(std::max(1, Count)), Value);
	}
	while (Slash != std::string::npos) {
		const size_t Next = Text.find('/', Slash + 1);
		const std::string Part = Text.substr(Slash + 1, Next == std::string::npos ? std::string::npos : Next - Slash - 1);
		if (!Part.empty() && Part[0] == 'L')
			Seq.Loop = Number(Part, 1, Name);
		else if (!Part.empty() && Part[0] == 'R')
			Seq.Release = Number(Part, 1, Name);
		else if (Part == "F")
			Seq.Setting = SEQUENCE_FIXED;
		else
			Problems().push_back(Name + ": \"/" + Part + "\"");
		Slash = Next;
	}
	if (Seq.Items.empty() || Seq.Items.size() > 252 || Seq.Loop >= static_cast<int>(Seq.Items.size()) ||
		Seq.Release >= static_cast<int>(Seq.Items.size()))
		Problems().push_back(Name + ": the sequence \"" + Text + "\"");
	return Seq;
}

Spec ReadSpec(const std::string &Text, const std::string &Name)
{
	Spec S;
	SoundDesign &D = S.D;
	size_t At = 0;
	while (At < Text.size()) {
		size_t End = Text.find(' ', At);
		if (End == std::string::npos)
			End = Text.size();
		const std::string W = Text.substr(At, End - At);
		At = End + 1;
		if (W.empty())
			continue;
		if (W.size() > 2 && W[1] == ':') {
			static const char LETTERS[] = "VAPHDWM";
			static const int TYPES[] = { SEQUENCE_VOLUME, SEQUENCE_ARPEGGIO, SEQUENCE_PITCH, SEQUENCE_HIPITCH, SEQUENCE_DUTY, SEQUENCE_DUTY,
				SEQUENCE_DUTY };
			const char *pLetter = std::strchr(LETTERS, W[0]);
			if (pLetter == 0)
				Problems().push_back(Name + ": \"" + W + "\"");
			else
				S.Own[W[0]] = ReadOwn(TYPES[pLetter - LETTERS], W.substr(2), W[0] == 'M', Name);
			continue;
		}
		if (W == "short") {
			S.Short = true;
			continue;
		}
		S.Designed = true;
		if (W == "steps")
			D.DecayStyle = DECAY_STEPS;
		else if (W == "e")
			D.Echo = true;
		else if (W == "k")
			D.Click = true;
		else if (W == "sweep")
			D.Motion = DUTY_SWEEP;
		else if (W == "wobble")
			D.Motion = DUTY_WOBBLE;
		else if (W == "fade")
			D.Motion = DUTY_FADE;
		else if (W == "blip")
			D.AttackPitch = PITCH_BLIP;
		else if (W == "scoop")
			D.AttackPitch = PITCH_SCOOP;
		else if (W == "drop")
			D.AttackPitch = PITCH_DROP;
		else if (W == "flick")
			D.AttackPitch = PITCH_FLICK;
		else if (W == "thump")
			D.AttackPitch = PITCH_THUMP;
		else if (W.compare(0, 3, "arp") == 0) {
			const size_t Sign = W.find('@');
			D.Arpeggio = ReadOwn(SEQUENCE_ARPEGGIO, W.substr(3, Sign == std::string::npos ? std::string::npos : Sign - 3), false, Name).Items;
			D.ArpeggioFrames = Sign != std::string::npos ? Number(W, Sign + 1, Name) : 1;
		}
		else if (W.compare(0, 3, "vib") == 0) {
			const size_t Sign = W.find('@'), Over = W.find('/');
			const std::string Depth = W.substr(3, Sign == std::string::npos ? std::string::npos : Sign - 3);
			const int Delay = Sign != std::string::npos ? Number(W.substr(Sign + 1, Over == std::string::npos ? std::string::npos :
				Over - Sign - 1), 0, Name) : 0;
			const int Rate = Over != std::string::npos ? Number(W, Over + 1, Name) : 12;
			if (Depth == "soft") {
				S.SoftDelay = Delay;
				S.SoftRate = std::max(4, Rate);
			}
			else {
				D.Vibrato = Depth == "faint" ? VIBRATO_FAINT : Number(Depth, 0, Name);
				D.VibratoDelay = Delay;
				D.VibratoRate = Rate;
			}
		}
		else if (W.size() >= 2 && std::strchr("afpdsrxuw", W[0]) != 0 && W.find_first_not_of("0123456789", 1) == std::string::npos) {
			const int Value = std::atoi(W.c_str() + 1);
			switch (W[0]) {
			case 'a': D.Attack = Value; break;
			case 'f': D.AttackFrom = Value; break;
			case 'p': D.Peak = Value; break;
			case 'd': D.Decay = Value; break;
			case 's': D.Sustain = Value; break;
			case 'r': D.Release = Value; break;
			case 'x': D.Restrike = Value; break;
			case 'u': D.Duty = Value; break;
			case 'w': D.Vrc6Duty = Value; break;
			}
		}
		else
			Problems().push_back(Name + ": \"" + W + "\"");
	}
	return S;
}

// The soft vibrato: Delay frames of nothing, then the period up to two steps longer for half of each cycle of Rate
// frames, in and out over a quarter each, looping; summing to nothing each cycle
std::vector<int> SoftVibratoItems(int Delay, int Rate, int &Loop)
{
	std::vector<int> Items(static_cast<size_t>(std::max(0, Delay)), 0);
	Loop = static_cast<int>(Items.size());
	const int Quarter = std::max(1, Rate / 4);
	std::vector<int> Cycle(static_cast<size_t>(4 * Quarter), 0);
	Cycle[0] = 1;
	Cycle[static_cast<size_t>(Quarter / 2)] += 1;
	Cycle[static_cast<size_t>(2 * Quarter)] = -1;
	Cycle[static_cast<size_t>(2 * Quarter + Quarter / 2)] += -1;
	Items.insert(Items.end(), Cycle.begin(), Cycle.end());
	return Items;
}

void Replace(std::vector<PresetSequence> &List, const PresetSequence &Seq)
{
	for (size_t i = 0; i < List.size(); ++i)
		if (List[i].Type == Seq.Type) {
			List[i] = Seq;
			return;
		}
	List.push_back(Seq);
}

// The 2A03's nearest duty to the VRC6's (0-7), and the VRC6's to the 2A03's (0-3)
int DutyOf2A03(int Vrc6Duty)
{
	return Vrc6Duty <= 1 ? 0 : Vrc6Duty <= 4 ? 1 : 2;
}
int DutyOfVrc6(int Duty)
{
	static const int NEAREST[4] = { 1, 3, 7, 3 };
	return NEAREST[std::max(0, std::min(3, Duty))];
}

PresetSequence DutyTranslated(const PresetSequence &From, bool ToVrc6)
{
	PresetSequence Seq = From;
	for (size_t i = 0; i < Seq.Items.size(); ++i)
		Seq.Items[i] = ToVrc6 ? DutyOfVrc6(Seq.Items[i]) : DutyOf2A03(Seq.Items[i]);
	return Seq;
}

struct PresetRow {
	int Home;				// library_home
	const char *Kind;		// a SoundKinds() id
	const char *Name;
	const char *About;		// what it sounds like and what it's for
	const char *Words;		// more words it's found by
	const char *Spec;		// ReadSpec's ("short": meant to be short)
};

LibraryPreset MakePreset(const PresetRow &Row)
{
	LibraryPreset L;
	L.Kind = Row.Kind;
	L.Words = Row.Words;
	L.Home = Row.Home;
	Spec S = ReadSpec(Row.Spec, Row.Name);
	L.Short = S.Short;
	if (Row.Home == HOME_VRC6_PULSE && S.D.Vrc6Duty >= 0)
		S.D.Duty = DutyOf2A03(S.D.Vrc6Duty);	// the 2A03's nearest, for the 2A03's sequences
	Preset P;
	if (S.Designed)
		P = DesignPreset(S.D);
	else {
		P.Drum = false;
		P.Vrc7Patch = 0;
	}
	P.Name = Row.Name;
	P.Description = Row.About;
	P.Drum = Row.Home == HOME_NOISE;
	P.Vrc7Patch = 0;
	if (P.Drum) {
		// The noise plays its volume, its pitches (fixed or from its note) and its mode (the 2A03's duty sequence)
		if (!S.Designed)
			P.Sequences2A03.clear();
		P.SequencesVRC6.clear();
	}
	if (S.SoftRate > 0) {
		// The soft vibrato after the attack's pitch (a fall or a flick) where the design has one
		std::vector<int> Pitch;
		if (S.D.AttackPitch == PITCH_DROP)
			Pitch = std::vector<int>{ 0, 2, 3, 4, 5, 6, 7, 8 };
		else if (S.D.AttackPitch == PITCH_FLICK)
			Pitch = std::vector<int>{ -4, 3, 1 };
		int Loop;
		const std::vector<int> Wave = SoftVibratoItems(std::max(0, S.SoftDelay - static_cast<int>(Pitch.size())), S.SoftRate, Loop);
		PresetSequence Seq;
		Seq.Type = SEQUENCE_PITCH;
		Seq.Loop = static_cast<int>(Pitch.size()) + Loop;
		Seq.Release = -1;
		Seq.Setting = SEQUENCE_ABSOLUTE;
		Seq.Items = Pitch;
		Seq.Items.insert(Seq.Items.end(), Wave.begin(), Wave.end());
		Replace(P.Sequences2A03, Seq);
		Replace(P.SequencesVRC6, Seq);
	}
	for (std::map<char, PresetSequence>::const_iterator it = S.Own.begin(); it != S.Own.end(); ++it) {
		const PresetSequence &Seq = it->second;
		switch (it->first) {
		case 'D':	// the 2A03's duty (the noise's mode), and the VRC6's nearest unless it has its own
			Replace(P.Sequences2A03, Seq);
			if (S.Own.count('W') == 0 && !P.Drum)
				Replace(P.SequencesVRC6, DutyTranslated(Seq, true));
			break;
		case 'W':	// the VRC6's, and the 2A03's nearest unless it has its own
			Replace(P.SequencesVRC6, Seq);
			if (S.Own.count('D') == 0)
				Replace(P.Sequences2A03, DutyTranslated(Seq, false));
			break;
		case 'M':	// the 5B's noise and mode
			break;
		default:
			Replace(P.Sequences2A03, Seq);
			if (!P.Drum)
				Replace(P.SequencesVRC6, Seq);
			break;
		}
	}
	if (Row.Home == HOME_5B) {
		// The 5B's: a design's volume as loud on the 5B (Volume5B), or its own as written, and its noise and mode
		std::vector<PresetSequence> List;
		for (size_t i = 0; i < P.Sequences2A03.size(); ++i) {
			PresetSequence Seq = P.Sequences2A03[i];
			if (Seq.Type == SEQUENCE_DUTY)
				continue;
			if (Seq.Type == SEQUENCE_VOLUME && S.Own.count('V') == 0)
				for (size_t k = 0; k < Seq.Items.size(); ++k)
					Seq.Items[k] = Volume5B(Seq.Items[k]);
			List.push_back(Seq);
		}
		if (S.Own.count('M') != 0)
			List.push_back(S.Own['M']);
		P.Sequences5B = List;
		P.Sequences2A03.clear();
		P.SequencesVRC6.clear();
	}
	bool HasVolume = false;
	const std::vector<PresetSequence> &Check = Row.Home == HOME_5B ? P.Sequences5B : P.Sequences2A03;
	for (size_t i = 0; i < Check.size(); ++i)
		HasVolume = HasVolume || (Check[i].Type == SEQUENCE_VOLUME && !Check[i].Items.empty());
	if (!HasVolume)
		Problems().push_back(std::string(Row.Name) + ": no volume");
	L.Sound = P;
	return L;
}

// The presets of the library, by where they're made to play. Their names are their own: no preset's, and none of the
// words FindPreset, Find5BSound or FindVrc7Patch take for another sound
const PresetRow PRESET_ROWS[] = {
	// The 2A03's pulses: leads
	{ HOME_PULSE, "lead", "bloom lead", "blooms in with a duty click, struck again as it holds, a soft vibrato that waits: a "
		"lead that speaks on every note, for action stages", "bloom click restrike vibrato action", "a2 f10 p15 d6 s12 r6 u1 k x12 "
		"vibsoft@24/12" },
	{ HOME_PULSE, "lead", "stepped lead", "holds full a moment, then steps down a few levels at a time: a punchy lead for fast "
		"tunes", "stepped decay action fast", "p15 d36 steps s9 r7 u2" },
	{ HOME_PULSE, "lead", "clipped lead", "a bright 25% lead that speaks with a click and steps down to a quiet hold: clipped and "
		"busy", "stepped click bright", "p13 d28 steps s7 r5 u1 k" },
	{ HOME_PULSE, "lead", "nasal lead", "a thin, nasal 12.5% lead with a soft, quick vibrato", "thin reedy 12.5", "a1 p13 d12 s9 r5 u0 "
		"vibsoft@12/8" },
	{ HOME_PULSE, "lead", "swell lead", "a round lead that swells in over 8 frames, a soft vibrato after half a second: slow "
		"melodies", "swell round slow vibrato", "a8 p13 s12 r10 u2 vibsoft@30/16" },
	{ HOME_PULSE, "lead", "chorus lead", "a 25% lead whose tone shimmers to and fro, like two players together", "shimmer chorus "
		"duty wobble", "a1 p13 d10 s11 r6 u1 wobble" },
	{ HOME_PULSE, "lead", "scoop lead", "a round lead that bends up into every note from a whole step below: bluesy, singing",
		"scoop bend slide", "a2 p14 d8 s12 r6 u2 scoop" },
	{ HOME_PULSE, "lead", "thump lead", "a lead whose notes drop in from a fourth above: an accent on every note", "thump accent punchy",
		"p15 d10 s11 r5 u1 thump" },
	{ HOME_PULSE, "lead", "echo lead", "a 25% lead that leaves a soft echo of itself when it's let go", "echo envelope", "a1 p14 d10 "
		"s10 r4 u1 e" },
	{ HOME_PULSE, "lead", "flutter lead", "a round lead struck again every 6 frames: a quick flutter on long notes", "flutter "
		"tremolo restrike", "a1 p13 d4 s11 r6 u2 x6" },
	{ HOME_PULSE, "lead", "whistle lead", "a pure, soft lead with a quick vibrato from the start, like someone whistling",
		"whistling pure soft", "a3 p12 s11 r8 u2 vibsoft@6/8" },
	{ HOME_PULSE, "lead", "staccato lead", "speaks with a click and drops to a soft tail at once, so fast runs stay clear", "short "
		"staccato click fast", "p15 d8 s4 r2 u1 k" },
	{ HOME_PULSE, "lead", "fanfare lead", "a bright lead that opens up and scoops into its notes: fanfares and heroes",
		"fanfare heroic bright scoop open", "a2 f9 p15 d6 s12 r6 u1 sweep scoop" },
	{ HOME_PULSE, "lead", "glass lead", "a thin lead struck at 14 that falls to a quiet hold over 24 frames, with a faint vibrato:"
		" a calm lead that never presses", "struck thin calm falling", "p14 d24 s8 r10 u0 vibfaint@30/12" },
	{ HOME_PULSE, "lead", "opening lead", "a lead whose tone opens from thin to round as each note starts, with a click",
		"duty sweep open click", "p14 d10 s11 r6 u2 sweep k" },
	{ HOME_PULSE, "lead", "patient lead", "a 25% lead whose soft vibrato waits most of a second: plain short notes, singing long "
		"ones", "late vibrato singing", "a2 p13 d12 s10 r8 u1 vibsoft@36/16" },
	{ HOME_PULSE, "lead", "beat lead", "a round lead struck again every 12 frames (an eighth at 150 BPM), so held notes pulse",
		"pulse restrike beat eighths", "a1 p14 d6 s10 x12 r6 u2" },

	// The 2A03's pulses: basses
	{ HOME_PULSE, "bass", "pick bass", "a round bass that speaks with a click and settles: clear, punchy lines", "click picked",
		"p15 d6 s10 r3 u2 k" },
	{ HOME_PULSE, "bass", "thin bass", "a buzzy 12.5% bass that cuts through a busy mix", "buzzy thin nasal", "p14 d4 s12 r3 u0" },
	{ HOME_PULSE, "bass", "octave bass", "each note starts an octave up for two frames: a bouncy, plucked bass", "octave blip bouncy",
		"p15 d6 s12 r3 u2 blip" },
	{ HOME_PULSE, "bass", "punch bass", "a 25% bass whose notes drop in from a fourth above: a punch on every note", "thump punchy",
		"p15 d8 s11 r3 u1 thump" },
	{ HOME_PULSE, "bass", "wobble bass", "a held bass whose tone moves to and fro between round and bright", "wobble duty moving",
		"p14 s13 r3 u2 wobble" },
	{ HOME_PULSE, "bass", "staccato bass", "short, bouncy notes that die within 10 frames by themselves: a busy bass that never "
		"blurs", "staccato short bouncy", "p15 d10 s0 u2" },
	{ HOME_PULSE, "bass", "driving bass", "a bass struck again every 8 frames, which drives its held notes along", "restrike driving "
		"pulsing", "p14 d4 s11 x8 r3 u2" },
	{ HOME_PULSE, "bass", "gentle bass", "a soft, round bass that eases in: quiet songs", "soft round quiet calm", "a2 p11 s11 r6 u2" },
	{ HOME_PULSE, "bass", "pop bass", "a bright pop: an octave blip and a click, then a quick fall to a hold", "slap funky blip",
		"p15 d3 s9 r3 u1 k blip" },

	// The 2A03's pulses: pads
	{ HOME_PULSE, "pad", "glass pad", "a thin, glassy pad that swells in slowly, a faint vibrato late in it", "thin glassy slow",
		"a16 p10 s10 r16 u0 vibfaint@40/16" },
	{ HOME_PULSE, "pad", "breathing pad", "a soft pad struck again every 24 frames, so long chords breathe", "restrike breathing slow",
		"a10 p10 s9 x24 r16 u2" },
	{ HOME_PULSE, "pad", "shimmer pad", "a pad whose tone shimmers to and fro as it holds", "shimmer chorus wobble", "a12 p10 s10 r14 "
		"u1 wobble" },
	{ HOME_PULSE, "pad", "echo chords", "a round pad that swells in and echoes softly after it's let go", "echo swell", "a14 p10 s10 "
		"r10 u2 e" },
	{ HOME_PULSE, "pad", "dusk pad", "a round pad that thins to 25% after a third of a second, as if the light went: quiet chords "
		"under a busy lead", "quiet darkening thinning", "a6 p9 s9 r12 D:2x20,1" },
	{ HOME_PULSE, "pad", "opening pad", "a pad that opens slowly from thin to round as it swells: rising chords", "opening sweep slow",
		"a20 p11 s11 r20 D:0x10,1x10,2" },
	{ HOME_PULSE, "pad", "voices pad", "a 25% pad that rises slowly, a slow soft vibrato in it, like distant voices", "voices choir "
		"slow", "a18 p11 s11 r18 u1 vibsoft@20/24" },
	{ HOME_PULSE, "pad", "sinking pad", "a pad that swells quickly, then sinks to a low hold over 40 frames, out of the melody's way",
		"sinking fading low", "a4 p11 d40 s6 r12 u2" },

	// The 2A03's pulses: plucks
	{ HOME_PULSE, "pluck", "click pluck", "a short bright note that speaks with a click and holds low: staccato runs", "click short",
		"p15 d4 s3 r2 u1 k" },
	{ HOME_PULSE, "pluck", "echo pluck", "a pluck that dies away, then a soft echo of itself", "echo", "p14 d12 s0 u1 e" },
	{ HOME_PULSE, "pluck", "lyre", "a round plucked string that rings out over half a second", "harp ringing round", "p15 d30 s0 u2" },
	{ HOME_PULSE, "pluck", "pizzicato", "a short round pluck with a click, like plucked violins", "plucked strings short", "p14 d12 s0 "
		"u2 k" },
	{ HOME_PULSE, "pluck", "koto", "a thin pluck that bends up into its note", "bend thin eastern", "p15 d20 s2 r4 u0 scoop" },
	{ HOME_PULSE, "pluck", "muted pluck", "a quick fall to a quiet hold: muted strings and steady rhythms", "muted quiet rhythm",
		"p13 d6 s5 r2 u2" },
	{ HOME_PULSE, "pluck", "banjo", "re-picked every 6 frames while it's held, like a rolling banjo", "rolling repicked bluegrass",
		"p15 d16 s0 u1 x6" },
	{ HOME_PULSE, "pluck", "steel string", "a bright pluck whose tone thins as it rings down", "guitar bright thinning", "p15 d24 s3 r4 "
		"u1 fade" },
	{ HOME_PULSE, "pluck", "wire pluck", "a thin, metallic pluck with an octave blip", "metallic thin blip", "p15 d14 s0 u0 blip" },

	// The 2A03's pulses: bells
	{ HOME_PULSE, "bell", "thin chime", "a thin bell with an octave blip that rings out slowly", "chime thin ringing", "p15 d40 s0 u0 "
		"blip" },
	{ HOME_PULSE, "bell", "tubular bell", "a round bell that settles to a soft ring and fades long after it's let go", "church tubular "
		"long", "p15 d24 s4 r20 u2 blip" },
	{ HOME_PULSE, "bell", "tinkle bell", "a small bell struck again every 8 frames while it's held: tinkling", "tinkling restrike small",
		"p13 d16 s0 u1 x8 blip" },
	{ HOME_PULSE, "bell", "echo bell", "a bell that rings, then echoes softly", "echo ringing", "p15 d20 s0 u1 e blip" },
	{ HOME_PULSE, "bell", "gong", "a long, round ring whose tone thins as it fades", "long ringing thinning", "p15 d60 s0 u2 fade" },
	{ HOME_PULSE, "bell", "glock", "a short, bright bell for quick melodies on top", "glockenspiel bright short", "p15 d14 s0 u1 blip" },
	{ HOME_PULSE, "bell", "lullaby bell", "a soft, round bell that eases in: lullabies and night", "soft round calm night", "a1 p11 d36 "
		"s0 u2 blip" },

	// The 2A03's pulses: keys
	{ HOME_PULSE, "keys", "pulse piano", "a struck tone that thins and fades like a piano's", "piano struck fading", "p14 d40 s3 r6 u2 "
		"fade" },
	{ HOME_PULSE, "keys", "electric keys", "an electric piano: a bright strike that mellows to a soft hold, a faint vibrato in it",
		"electric piano mellow", "p13 d30 s6 r8 u1 fade vibfaint@30/12" },
	{ HOME_PULSE, "keys", "harpsichord", "a thin, bright, short note with a click: baroque runs", "baroque bright click", "p15 d10 s2 r2 "
		"u0 k" },
	{ HOME_PULSE, "keys", "clavinet", "a funky thin stab with a click that holds low", "funky stab click", "p14 d5 s5 r1 u0 k" },
	{ HOME_PULSE, "keys", "toy piano", "a tinny strike with an octave blip that dies away", "toy tinny blip", "p15 d18 s0 u2 blip" },
	{ HOME_PULSE, "keys", "accordion", "a reedy tone whose shimmer moves like an accordion's", "reedy shimmer wobble", "a2 p12 s11 r4 u0 "
		"wobble" },

	// The 2A03's pulses: effects
	{ HOME_PULSE, "effect", "blip", "a tiny blip, gone within a few frames: a cursor, a tick, a counter (short on purpose)",
		"tick cursor menu short", "p13 d3 s0 u1 short" },
	{ HOME_PULSE, "effect", "coin", "a coin: two quick notes, the second a fifth up, that ring out", "coin pickup item glint",
		"p15 d24 s0 u1 A:0x3,7" },
	{ HOME_PULSE, "effect", "power up", "a chord that climbs two octaves as it rings: a power-up", "powerup climb rising chord",
		"p14 d40 s0 u2 A:0x2,4x2,7x2,12x2,16x2,19x2,24" },
	{ HOME_PULSE, "effect", "zap", "a falling zap: lasers and shots", "laser shot falling", "p15 d14 s0 u0 P:0,4,8,12,16,20,24,28,32" },
	{ HOME_PULSE, "effect", "alarm", "an alarm hopping an octave every 4 frames", "alarm warning siren", "p12 s11 r4 u1 arp0,12@4" },
	{ HOME_PULSE, "effect", "trill", "a fast trill to the note a whole step above", "trill shake ornament", "p13 s11 r4 u2 arp0,2@2" },
	{ HOME_PULSE, "effect", "echo blips", "a blip, then its echo (short on purpose)", "echo tick short", "p12 d2 s0 u1 e short" },
	{ HOME_PULSE, "effect", "riser", "a long swell from silence: tension before a drop", "swell rising tension", "a40 p12 s12 r10 u0" },

	// The triangle: no volume of its own (a level only starts or stops it), so its sounds are pitch and length
	{ HOME_TRIANGLE, "drums", "triangle kick", "a kick on the triangle: its pitch drops two octaves in 6 frames (pitched by its note; "
		"C-3 is a good one)", "kick drum thump", "V:15x6,0 A:24,18,12,7,3,0" },
	{ HOME_TRIANGLE, "drums", "triangle tom", "a tom on the triangle that falls almost an octave as it rings (pitched by its note)",
		"tom fill falling", "V:15x12,0 A:7,6,5,4,3,2,1,0,-1,-2,-3,-4" },
	{ HOME_TRIANGLE, "drums", "triangle snap", "a tiny high click on the triangle, two frames long: a tick when the noise is busy "
		"(short on purpose)", "click tick hat short", "V:15x2,0 A:36 short" },
	{ HOME_TRIANGLE, "bass", "staccato triangle", "the triangle cut after 7 frames: the bouncy staccato bass of the action "
		"soundtracks (short on purpose)", "staccato short bouncy action", "V:15x7,0 short" },
	{ HOME_TRIANGLE, "bass", "chopped triangle", "a bass chopped every 6 frames: held notes become a driving pulse", "chopped pulsing "
		"gated", "V:15x5,0/L0" },
	{ HOME_TRIANGLE, "bass", "octave triangle", "a bubbly bass that bounces an octave every 3 frames", "bubbly octave bounce",
		"A:0x3,12x3/L0 V:15" },
	{ HOME_TRIANGLE, "bass", "slide triangle", "a bass that slides up into each note from a whole step below", "slide scoop",
		"V:15 A:-2,-1,0" },
	{ HOME_TRIANGLE, "bass", "fifth thump", "a bass that drops in from a fifth above: a soft thud on each note", "thump thud",
		"V:15 A:7,0" },
	{ HOME_TRIANGLE, "bass", "warble triangle", "a bass whose pitch wobbles widely and quickly: a warble", "wobble warble wah",
		"V:15 vib3@0/8" },
	{ HOME_TRIANGLE, "lead", "triangle flute", "the triangle as a soft flute, a vibrato after a third of a second: high, pure tunes",
		"flute pure soft high", "V:15 vib1@20/12" },
	{ HOME_TRIANGLE, "effect", "triangle laser", "a laser falling three octaves on the triangle", "laser falling zap",
		"V:15x18,0 A:36,34,32,30,28,26,24,22,20,18,16,14,12,10,8,6,4,2" },

	// The noise: drums (most at fixed pitches, whatever note plays them) and sound effects
	{ HOME_NOISE, "drums", "deep kick", "a dark, long noise kick that falls to a low rumble", "kick bass drum dark", "V:15,14,11,8,5,3,1,0 "
		"A:9,5,3,2,1,0/F" },
	{ HOME_NOISE, "drums", "tight kick", "a short, punchy noise kick", "kick bass drum short punchy", "V:15,12,7,3,1,0 A:13,7,3,1/F" },
	{ HOME_NOISE, "drums", "click kick", "a noise kick with a metallic click in front", "kick bass drum click", "V:15,13,9,6,3,1,0 "
		"A:15,10,4,2,1,0/F D:1,0" },
	{ HOME_NOISE, "drums", "tight snare", "a short, bright snare", "snare short crisp", "V:15,12,9,6,4,2,1,0 A:13,11,10,10/F" },
	{ HOME_NOISE, "drums", "fat snare", "a snare with a long, full body", "snare long big", "V:15,14,13,11,10,9,8,7,6,5,4,3,3,2,2,1,1,0 "
		"A:11,10,9,9,8/F" },
	{ HOME_NOISE, "drums", "rim shot", "a metallic knock on the rim, short and dry", "rim snare metallic", "V:14,9,4,1,0 A:14,12/F "
		"D:1,1,0" },
	{ HOME_NOISE, "drums", "hand clap", "three quick bursts of noise, like hands clapping", "clap claps", "V:13,4,12,4,13,9,7,5,4,3,2,1,0 "
		"A:12/F" },
	{ HOME_NOISE, "drums", "brush", "a soft swish, like a brush on a snare: quiet grooves", "brush swish soft jazz",
		"V:4,6,7,7,6,6,5,5,4,4,3,3,2,2,1,1,0 A:14/F" },
	{ HOME_NOISE, "drums", "tick hat", "a tiny, soft tick: the quietest hi-hat", "hihat hat tick soft", "V:7,3,1,0 A:15/F" },
	{ HOME_NOISE, "drums", "metal hat", "a metallic closed hi-hat", "hihat hat metallic closed", "V:10,6,3,1,0 A:14/F D:1" },
	{ HOME_NOISE, "drums", "pedal hat", "a soft chick, like a hi-hat closed with the foot", "hihat hat pedal soft", "V:6,8,5,2,0 A:13/F" },
	{ HOME_NOISE, "drums", "shaker", "a shaker that swells and fades: steady eighths or sixteenths", "shaker shake maraca",
		"V:3,6,8,7,5,3,2,1,0 A:15/F" },
	{ HOME_NOISE, "drums", "sizzle hat", "a bright open hi-hat that sizzles out", "hihat hat open sizzle", "V:10,8,7,6,6,5,5,4,4,3,3,2,2,"
		"1,1,0 A:15/F" },
	{ HOME_NOISE, "drums", "ride", "a metallic ride cymbal's ping that rings", "ride cymbal ping metallic",
		"V:12,9,8,8,7,7,6,6,5,5,5,4,4,4,3,3,3,2,2,2,1,1,1,0 A:14/F D:1" },
	{ HOME_NOISE, "drums", "trash crash", "a dark, trashy crash cymbal", "crash cymbal dark china", "V:15,13,12,11,10,9,9,8,8,7,7,6,6,"
		"5,5,4,4,3,3,2,2,1,1,0 A:10/F" },
	{ HOME_NOISE, "drums", "splash", "a short, bright crash", "crash cymbal splash short", "V:13,11,9,8,7,6,5,4,3,2,1,0 A:13/F" },
	{ HOME_NOISE, "drums", "deep tom", "a low tom that falls as it rings, pitched by its note", "tom floor low fill",
		"V:15,14,12,11,10,9,8,7,6,5,4,3,2,1,0 A:0,0,-1,-1,-2,-2,-3,-3,-4" },
	{ HOME_NOISE, "drums", "cowbell", "a metallic knock with a tone in it", "cowbell metallic latin", "V:13,11,9,7,5,4,3,2,1,0 A:9/F D:1" },
	{ HOME_NOISE, "drums", "woodblock", "a short, hollow knock", "woodblock knock wood", "V:13,6,2,0 A:12/F D:1" },
	{ HOME_NOISE, "drums", "tambourine", "a jingle: bright noise with a metallic shimmer", "tambourine jingle", "V:9,7,10,7,5,4,3,2,1,0 "
		"A:15/F D:0,1,0" },
	{ HOME_NOISE, "drums", "flam snare", "a soft grace hit, then the snare", "snare flam grace", "V:9,3,15,13,11,9,8,7,6,5,4,3,2,1,0 "
		"A:13,13,12,11,11,10/F" },
	{ HOME_NOISE, "drums", "bongo", "a low, tonal knock like a hand drum", "bongo conga hand drum", "V:12,9,5,2,0 A:8,7/F D:1" },
	{ HOME_NOISE, "effect", "reverse cymbal", "a cymbal that swells up and stops: into a new section", "reverse swell cymbal",
		"V:1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,14,15,0 A:12/F" },
	{ HOME_NOISE, "effect", "wind", "a gust: rises slowly and washes out after a release; its note picks how high it hisses",
		"wind gust air weather", "a30 p10 s10 r40 u0" },
	{ HOME_NOISE, "effect", "rain", "a soft, steady hiss that flickers like rain until it's let go", "rain weather hiss",
		"V:6,4,7,5,6,4,7,5,3,2,1,0/L0/R7 A:15,14/F" },
	{ HOME_NOISE, "effect", "surf", "waves washing in and out, slowly, until it's let go", "surf waves sea ocean water",
		"V:1x4,2x4,3x4,4x4,5x4,6x4,7x4,8x4,9x4,10x4,10x4,9x4,8x4,7x4,6x4,5x4,4x4,3x4,2x4,1x4,1x8,1,0/L0/R87 A:8/F" },
	{ HOME_NOISE, "effect", "explosion", "a big, low boom that rumbles out", "explosion boom blast bomb",
		"V:15,15,14,14,13,12,12,11,10,10,9,8,8,7,7,6,6,5,5,4,4,3,3,2,2,1,1,0 A:3,2,1,0/F" },
	{ HOME_NOISE, "effect", "thunder", "a low rumble that rolls in a few waves", "thunder storm rumble weather",
		"V:12,10,8,14,12,10,9,8,13,11,9,8,7,6,5,5,4,4,3,3,2,2,1,1,0 A:1,0/F" },
	{ HOME_NOISE, "effect", "steam", "a hiss of steam that bursts in and fades", "steam hiss burst", "V:4,8,12,12,11,11,10,10,9,9,8,8,7,"
		"7,6,6,5,5,4,4,3,3,2,2,1,1,0 A:15,14/F" },
	{ HOME_NOISE, "effect", "radio static", "a high, buzzing static held until it's let go", "static radio buzz noise",
		"V:8,0/R0 A:15/F D:1" },
	{ HOME_NOISE, "effect", "footstep", "a soft, dull step", "step walk footsteps", "V:9,6,3,1,0 A:4,3/F" },
	{ HOME_NOISE, "effect", "engine hum", "a low, buzzing hum held until it's let go: engines and machines", "engine machine hum buzz",
		"V:9,0/R0 A:1/F D:1" },

	// The VRC6's pulses: their eight widths, and vibratos (a VRC6 pulse doesn't click)
	{ HOME_VRC6_PULSE, "lead", "pwm lead", "a lead whose pulse narrows and widens smoothly as it holds, a vibrato after 24 frames",
		"pwm pulse width sweep", "a1 p13 d8 s11 r6 vib1@24/12 W:1x2,2x2,3x2,4x2,5x2,6x2,7x2,6x2,5x2,4x2,3x2,2x2/L0" },
	{ HOME_VRC6_PULSE, "lead", "reed lead", "the VRC6's thinnest pulse (6.25%): a reedy, nasal lead with a vibrato", "reedy nasal thin "
		"oboe", "a1 p13 d6 s11 r6 w0 vib1@20/10" },
	{ HOME_VRC6_PULSE, "lead", "clarinet lead", "a woody 37.5% lead that eases in, a vibrato after half a second", "woody clarinet",
		"a3 p12 s11 r8 w5 vib1@28/12" },
	{ HOME_VRC6_PULSE, "lead", "castle lead", "an 18.75% lead that blooms in with a click, struck again as it holds, a vibrato and "
		"an echo, a lead that speaks in a castle", "bloom click restrike echo vibrato castle action", "a2 f10 p15 d6 s12 "
		"r6 w2 k x12 e vib2@24/12" },
	{ HOME_VRC6_PULSE, "lead", "aria lead", "a round 50% lead with a wide, singing vibrato after 18 frames", "singing vibrato wide",
		"a4 p13 d6 s12 r10 w7 vib2@18/12" },
	{ HOME_VRC6_PULSE, "lead", "horn lead", "a mellow horn whose pulse opens from 25% to round, with a slow vibrato", "horn brass "
		"mellow open", "a3 p14 s13 r8 vib1@20/16 W:3x2,5x2,7" },
	{ HOME_VRC6_PULSE, "lead", "canyon lead", "a 25% lead with a gentle vibrato and an echo after each note", "echo vibrato",
		"a1 p14 d10 s10 r4 w3 e vib1@24/12" },
	{ HOME_VRC6_PULSE, "pad", "pwm pad", "a pad whose pulse width drifts slowly as it holds, a vibrato late in it", "pwm slow drifting",
		"a16 p10 s10 r16 vib1@30/16 W:1x4,2x4,3x4,4x4,5x4,6x4,7x4,6x4,5x4,4x4,3x4,2x4/L0" },
	{ HOME_VRC6_PULSE, "pad", "string section", "strings swelling in, their tone moving between two widths, a vibrato in them",
		"strings violins section", "a14 p11 s11 r14 vib1@20/12 W:6x6,4x6/L0" },
	{ HOME_VRC6_PULSE, "pad", "hollow choir", "a hollow 31.25% pad that rises slowly with a slow vibrato, like a choir far off",
		"choir voices hollow slow", "a20 p10 s10 r20 w4 vib1@24/20" },
	{ HOME_VRC6_PULSE, "pad", "major arp", "every note plays a major chord as a quick arpeggio", "arpeggio chord major chiptune",
		"p12 s12 r6 w3 arp0,4,7@2" },
	{ HOME_VRC6_PULSE, "pad", "minor arp", "every note plays a minor chord as a quick arpeggio", "arpeggio chord minor chiptune",
		"p12 s12 r6 w3 arp0,3,7@2" },
	{ HOME_VRC6_PULSE, "pluck", "closing pluck", "a pluck whose tone closes like a filter as it decays", "filter closing pluck",
		"p15 d24 s0 W:7,7,6,6,5,5,4,4,3,3,2,2,1,1,0" },
	{ HOME_VRC6_PULSE, "pluck", "warm harp", "a warm 37.5% pluck that rings for more than half a second", "harp warm ringing",
		"p14 d36 s0 w5" },
	{ HOME_VRC6_PULSE, "bell", "needle bell", "a bell on the thinnest pulse, an octave blip on the strike", "thin bell blip",
		"p15 d40 s0 w0 blip" },
	{ HOME_VRC6_PULSE, "keys", "velvet keys", "an electric piano whose tone closes as it fades to a soft hold", "electric piano mellow",
		"p14 d36 s4 r8 W:6x4,5x4,4x4,3x4,2x4,1" },
	{ HOME_VRC6_PULSE, "effect", "siren", "a siren: the pitch swings slowly and widely", "siren alarm police", "p12 s11 r4 w7 vib3@0/24" },
	{ HOME_VRC6_PULSE, "effect", "level up", "a quick chord climbing two octaves: a level-up", "powerup levelup climb",
		"p14 d30 s0 w3 A:0x2,4x2,7x2,12x2,16x2,19x2,24" },

	// The VRC6's sawtooth: its own buzz, without a duty (FamiTracker names them "(saw)"); the presets and the pulse
	// sounds play there too, so these are what only pitch can make of it
	{ HOME_SAWTOOTH, "bass", "snarl bass", "a sawtooth bass whose pitch shakes fast: a growl", "growl snarl dirty", "p14 s13 r3 "
		"vib3@0/4" },
	{ HOME_SAWTOOTH, "bass", "dive bass", "a sawtooth bass whose notes sink after a moment, like a tired string", "sinking falling "
		"tired", "p15 d6 s12 r3 P:0x16,4x12" },
	{ HOME_SAWTOOTH, "lead", "glide lead", "a sawtooth lead that slides smoothly up into each note", "glide slide portamento smooth",
		"a2 p13 d8 s11 r6 P:12,-2,-2,-2,-2,-2,-2" },
	{ HOME_SAWTOOTH, "lead", "octave saw", "a sawtooth lead flickering between two octaves every frame: both at once, a buzzy organ",
		"octave organ buzzy double", "p13 d8 s11 r6 arp0,12@1" },
	{ HOME_SAWTOOTH, "lead", "power saw", "a sawtooth flickering between a note and its fifth: a power chord", "power chord fifth rock",
		"p14 d6 s12 r4 arp0,7@1" },
	{ HOME_SAWTOOTH, "lead", "brass fall", "a brassy sawtooth that falls off in pitch as it's let go", "brass fall doit jazz",
		"a3 p15 d6 s13 r8 P:0,2,3,4,5,6,7,8,9,10/R0" },
	{ HOME_SAWTOOTH, "pad", "cello section", "low strings swelling in, a slow, wide vibrato after a third of a second", "cello strings "
		"section", "a14 p11 s11 r14 vib2@20/16" },
	{ HOME_SAWTOOTH, "pad", "tide pad", "a slow sawtooth pad struck again every 24 frames, like a tide", "slow breathing", "a30 p10 s10 "
		"r30 x24" },
	{ HOME_SAWTOOTH, "effect", "saw dive", "a sawtooth falling more than an octave in half a second: a dive or a crash",
		"dive falling crash bomb", "p15 d30 s0 P:0,4,8,12,16,20,24,28,32,36,40,44,48,52,56,60" },

	// The 5B: its squares (always 50%), its noise and its envelope; volumes in its own steps of 3 dB
	{ HOME_5B, "lead", "bloom square", "a square that blooms in, settles a little and holds, a vibrato after 24 frames", "bloom vibrato",
		"V:12,14,15,15,14,14,13,13,11,9,6,3,0/R7 vib1@24/12" },
	{ HOME_5B, "lead", "echo square", "a square lead that echoes softly after it's let go", "echo", "V:15,14,13,13,12,10,8,0,0,0,9,8,6,4,"
		"0/R4" },
	{ HOME_5B, "lead", "tremolo square", "a square lead with a dip every 6 frames as it holds: a tremolo", "tremolo restrike flutter",
		"V:15,14,13,12,12,12,9,12,12,12,12,12,9,6,0/L6/R11" },
	{ HOME_5B, "lead", "breathy square", "a square with a breath of noise as each note starts", "breathy chiff noise", "V:14,15,15,14,14,"
		"13,11,9,6,0/R5 M:sn18x3,s" },
	{ HOME_5B, "lead", "glide square", "a square lead that glides up into each note from a whole step below", "glide scoop slide",
		"V:15,15,14,14,13,11,9,7,4,0/R4 A:-2,-1,0" },
	{ HOME_5B, "bass", "deep square", "a full, held square bass", "round held", "V:15,15,14,14,12,9,0/R3" },
	{ HOME_5B, "bass", "wooden square", "a square bass with a knock of noise on each note and a quiet hold", "woody knock plucky",
		"V:15,13,12,11,11,11,9,6,0/R5 M:sn10,s" },
	{ HOME_5B, "bass", "thump square", "a square bass whose notes drop in from a fourth above", "thump punchy", "V:15,15,14,14,13,11,8,0/R4 "
		"A:5,3,1,0" },
	{ HOME_5B, "bass", "warble bass", "a square bass whose pitch wobbles quickly: a warble", "wobble warble", "V:15,15,14,12,8,0/R2 "
		"vib3@0/8" },
	{ HOME_5B, "pad", "drifting square", "a soft square pad with a slow vibrato from the start", "slow vibrato soft",
		"V:9,10,11,11,12,12,11,10,9,8,6,4,0/R5 vib1@0/20" },
	{ HOME_5B, "pad", "swelling square", "a square that swells in slowly from almost nothing", "swell slow", "V:3,4,5,6,7,8,9,10,11,11,"
		"12,12,11,10,9,8,7,6,5,4,3,0/R11" },
	{ HOME_5B, "pad", "pulsing square", "a square pad with a soft dip every 12 frames, so long chords pulse", "pulsing restrike slow",
		"V:11,12,12,12,12,12,12,12,12,12,12,12,10,11,12,12,12,12,12,12,12,12,12,12,10,8,6,4,0/L12/R23" },
	{ HOME_5B, "pluck", "square harp", "a square pluck that rings out slowly to silence", "harp ringing", "V:15,15,14,14,13,13,13,12,12,"
		"12,11,11,11,10,10,10,9,9,9,8,8,7,7,6,5,4,3,2,1,0" },
	{ HOME_5B, "pluck", "chiff pluck", "a pluck with a breath of noise on the strike", "noise chiff breathy", "V:15,14,13,13,12,12,11,11,"
		"10,10,9,8,7,6,5,4,3,2,1,0 M:sn24,s" },
	{ HOME_5B, "pluck", "staccato square", "a short square note: busy rhythms", "short staccato", "V:15,14,13,12,11,10,9,7,5,0" },
	{ HOME_5B, "bell", "square chime", "a chime: an octave blip, then a long ring", "chime ringing blip", "V:15,14,13,12,12,11,11,10,10,9,"
		"9,8,8,7,7,6,6,5,5,4,4,3,3,2,2,1,0 A:12,12,0" },
	{ HOME_5B, "bell", "echo chime", "a struck bell that echoes once", "echo bell", "V:15,13,12,11,10,9,8,0,0,0,10,9,8,7,6,5,4,0 A:12,12,0" },
	{ HOME_5B, "drums", "rumble kick", "a kick of low noise", "kick bass drum low", "V:15,13,11,8,5,2,0 M:n2,n1,n0" },
	{ HOME_5B, "drums", "noise clapper", "three quick bursts of noise, like a clap", "clap claps", "V:13,6,12,6,13,10,8,6,4,2,0 M:n20" },
	{ HOME_5B, "drums", "noise shaker", "a soft shake of high noise", "shaker shake", "V:6,9,11,10,8,6,4,2,0 M:n30" },
	{ HOME_5B, "effect", "noise wind", "wind rising and falling on the 5B's noise", "wind gust weather", "V:2,3,4,5,6,7,8,9,10,10,9,8,7,6,"
		"5,4,3,2,1,0/R9 M:n16" },
	{ HOME_5B, "effect", "square siren", "a siren: the square's pitch swings slowly and widely", "siren alarm", "V:13,13,0/R1 vib3@0/24" },
};

std::vector<LibraryPreset> MakePresets()
{
	std::vector<LibraryPreset> Out;
	for (size_t i = 0; i < sizeof(PRESET_ROWS) / sizeof(PRESET_ROWS[0]); ++i)
		Out.push_back(MakePreset(PRESET_ROWS[i]));
	return Out;
}

}	// namespace

const std::vector<LibraryPreset> &LibraryPresets()
{
	static const std::vector<LibraryPreset> All = MakePresets();
	return All;
}

const LibraryPreset *FindLibraryPreset(const std::string &Name, int Home)
{
	const std::string Wanted = Simplified(Name);
	if (Wanted.empty())
		return 0;
	const std::vector<LibraryPreset> &All = LibraryPresets();
	for (size_t i = 0; i < All.size(); ++i)
		if ((Home < 0 || All[i].Home == Home) && Simplified(All[i].Sound.Name) == Wanted)
			return &All[i];
	return 0;
}

namespace {

struct Vrc7Row {
	const char *Kind;
	const char *Name;
	const char *About;
	const char *Words;
	int Regs[8];
};

// Measured with emu2413 as FamiTracker plays them (a C-3 struck, held a second, released): their levels a frame at a
// time and how much of each frame's sound is over its fundamental. Names and words apart from the chip's patches'
const Vrc7Row VRC7_ROWS[] = {
	{ "lead", "fm square lead", "a hollow, square-toned lead in odd harmonics that holds steady, with the chip's vibrato",
		"square hollow clarinet", { 0x22, 0x61, 0x1A, 0x00, 0xF0, 0xF0, 0x07, 0x07 } },
	{ "lead", "fm reed", "a buzzy reed that swells in over a fifth of a second and mellows as it holds, with the chip's vibrato",
		"reedy buzzy oboe nasal", { 0x21, 0x61, 0x14, 0x0C, 0xF2, 0xF0, 0x27, 0x07 } },
	{ "lead", "fm saw lead", "a bright, buzzing synthesizer lead that holds steady, with the chip's vibrato", "bright buzzing synth "
		"sawtooth", { 0x21, 0x61, 0x06, 0x05, 0xF0, 0xF0, 0x07, 0x07 } },
	{ "lead", "fm whistle", "a pure whistle, a sine with the chip's vibrato", "whistling pure sine", { 0x21, 0x61, 0x3F, 0x00, 0xF0,
		0xC0, 0x0F, 0x07 } },
	{ "bass", "fm slap bass", "a slap: a bright snap that turns into a round bass and fades 10 dB over a second", "slap snap funky",
		{ 0x01, 0x21, 0x08, 0x03, 0xF8, 0xF3, 0x7F, 0x35 } },
	{ "bass", "fm sub bass", "a deep, almost pure bass that holds", "sub deep pure", { 0x21, 0x21, 0x2C, 0x00, 0xF0, 0xF1, 0x05, 0x15 } },
	{ "bass", "fm wah bass", "a bass whose tone opens up over a quarter of a second, like a wah pedal", "wah opening funky",
		{ 0x21, 0x21, 0x0C, 0x06, 0x40, 0xF1, 0x05, 0x15 } },
	{ "pad", "fm strings", "bright strings that swell in over 7 frames and hold, with the chip's vibrato", "strings violins section "
		"bright", { 0x61, 0x61, 0x0A, 0x06, 0x40, 0x40, 0x05, 0x05 } },
	{ "pad", "fm glass pad", "a glassy pad of even overtones that swells in over 7 frames", "glassy shimmering", { 0x23, 0x21, 0x22, 0x00,
		0x40, 0x40, 0x05, 0x05 } },
	{ "pad", "fm breath pad", "a breathy pad that rises over 9 frames with a slow tremolo in it", "breathy airy tremolo", { 0x21, 0xA1,
		0x24, 0x07, 0x50, 0x40, 0x05, 0x05 } },
	{ "pluck", "fm guitar", "a plucked guitar string that dies down over a second", "guitar plucked string", { 0x01, 0x01, 0x10, 0x05,
		0xF6, 0xF3, 0x55, 0xF4 } },
	{ "pluck", "fm koto", "a twangy pluck, bright for an instant and gone within half a second", "twang plucked eastern", { 0x03, 0x01,
		0x14, 0x02, 0xF8, 0xF4, 0xF5, 0xF5 } },
	{ "pluck", "fm harp", "a soft harp string that rings for seconds", "harp soft ringing", { 0x01, 0x01, 0x24, 0x00, 0xF4, 0xF2, 0xF4,
		0xF3 } },
	{ "bell", "fm tubular bell", "a deep bell, rich in overtones, that rings for seconds", "tubular church deep", { 0x03, 0x02, 0x18, 0x00,
		0xF3, 0xF2, 0xF3, 0xF2 } },
	{ "bell", "fm tine", "a tine: a bright metallic ping that fades into a pure tone", "tine metallic ping", { 0x0E, 0x01, 0x20, 0x00,
		0xF7, 0xF3, 0xF4, 0xF3 } },
	{ "bell", "fm kalimba", "a thumb piano: a quick, bright strike that dies within half a second", "kalimba thumb piano", { 0x04, 0x01,
		0x1C, 0x00, 0xF6, 0xF4, 0xF5, 0xF5 } },
	{ "keys", "fm clav", "a funky, nasal clavinet, short and bright", "clavinet funky nasal stab", { 0x01, 0x01, 0x0C, 0x18, 0xF5, 0xF4,
		0xF6, 0xF6 } },
	{ "keys", "fm harpsichord", "a buzzy plucked harpsichord that dies quickly", "harpsichord baroque buzzy", { 0x01, 0x01, 0x0E, 0x0F,
		0xF7, 0xF5, 0xF5, 0xF5 } },
	{ "keys", "fm toy piano", "a toy piano: a tinny strike gone within a fifth of a second", "toy tinny", { 0x07, 0x01, 0x1C, 0x00, 0xF7,
		0xF6, 0xF5, 0xF5 } },
	{ "effect", "fm metal hit", "a metallic clang that rings out: hits and impacts", "metal clang impact hit", { 0x0F, 0x01, 0x00, 0x07,
		0xF5, 0xF4, 0xF4, 0xF4 } },
	{ "effect", "fm wind", "a noisy, breathing wind that rises slowly: ambience", "wind noise ambience weather", { 0x0F, 0xA1, 0x00, 0x07,
		0x40, 0x30, 0x05, 0x05 } },
};

std::vector<LibraryVrc7> MakeVrc7()
{
	std::vector<LibraryVrc7> Out;
	for (size_t i = 0; i < sizeof(VRC7_ROWS) / sizeof(VRC7_ROWS[0]); ++i) {
		LibraryVrc7 L;
		L.Kind = VRC7_ROWS[i].Kind;
		L.Words = VRC7_ROWS[i].Words;
		L.Sound.Name = VRC7_ROWS[i].Name;
		L.Sound.Description = VRC7_ROWS[i].About;
		for (int r = 0; r < 8; ++r)
			L.Sound.Regs[r] = VRC7_ROWS[i].Regs[r];
		Out.push_back(L);
	}
	return Out;
}

}	// namespace

const std::vector<LibraryVrc7> &LibraryVrc7Sounds()
{
	static const std::vector<LibraryVrc7> All = MakeVrc7();
	return All;
}

const LibraryVrc7 *FindLibraryVrc7(const std::string &Name)
{
	const std::string Wanted = Simplified(Name);
	if (Wanted.empty())
		return 0;
	const std::vector<LibraryVrc7> &All = LibraryVrc7Sounds();
	for (size_t i = 0; i < All.size(); ++i)
		if (Simplified(All[i].Sound.Name) == Wanted)
			return &All[i];
	return 0;
}

const std::vector<std::string> &LibraryProblems()
{
	LibraryPresets();
	LibraryVrc7Sounds();
	return Problems();
}

}	// namespace ftapi

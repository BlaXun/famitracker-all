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

// FamiTracker 0.4.6's effects for Claude: see Effects.h

#include "Effects.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include "Music.h"

namespace ftapi {

namespace {

std::string Squeezed(const std::string &Text)
{
	std::string Out;
	for (size_t i = 0; i < Text.size(); ++i) {
		const unsigned char c = static_cast<unsigned char>(Text[i]);
		if (std::isalnum(c))
			Out += static_cast<char>(std::tolower(c));
	}
	return Out;
}

std::vector<EffectInfo> MakeReference()
{
	std::vector<EffectInfo> All;
	All.push_back({ "0xy", "arpeggio",
		"Plays the note, then x semitones above it, then y above it, a tick each, over and over: a chord on one channel.",
		"every channel with a pitch (not the DPCM's)", "until 000",
		"037 a minor chord, 047 major, 057 a sus4, 0C0 octaves; so fast it sounds like a chord rather than a run, the classic chip "
		"chord. Turn it off (000) before a note that shouldn't arpeggiate." });
	All.push_back({ "1xx", "slide up",
		"Slides the pitch up, xx pitch units every tick, without end.", "every channel with a pitch", "until 100, or another slide",
		"risers, sirens and sound effects; 101-104 climb slowly. With a halt after it, a note that flies off." });
	All.push_back({ "2xx", "slide down",
		"Slides the pitch down, xx pitch units every tick, without end.", "every channel with a pitch", "until 200, or another slide",
		"falls, bombs dropping, a dive at the end of a phrase (with a halt a row or two later)." });
	All.push_back({ "3xx", "portamento",
		"Each new note glides from the pitch before to its own, xx pitch units a tick.", "every channel with a pitch",
		"until 300: every note glides while it's on",
		"legato leads and bass glides: 304-310 smooth, 320 and up quick. Turn it off (300) before notes that should jump." });
	All.push_back({ "4xy", "vibrato",
		"Wobbles the pitch: x is how fast, y how deep (1-F each).", "every channel with a pitch", "until 400",
		"443-454 a singing lead, 4A2 a fast shimmer, 486 a wide wobble. Bring it in a beat after a long note starts, not on "
		"every short note (see the technique \"delayed vibrato\")." });
	All.push_back({ "7xy", "tremolo",
		"Wobbles the volume: x is how fast, y how deep.", "channels with a volume (not the triangle or the DPCM)", "until 700",
		"744 a gentle shimmer on pads and organs, 7A8 a fast throb." });
	All.push_back({ "Axy", "volume slide",
		"Slides the volume: A0y up, Ax0 down, by x or y eighths of a step a tick (one of them, the other 0).",
		"channels with a volume (not the triangle or the DPCM)", "until A00",
		"A20-A40 fade a held note out, A02-A04 swell one in (start it soft, at volume 3 or so); a fade at the end of a song." });
	All.push_back({ "Gxx", "note delay",
		"Holds this row's note back xx ticks, less than the speed (the ticks a row lasts).", "every channel", "this row only",
		"swing (G02 or G03 on the off-beats at speed 6), flams and rolls on drums, an echo that lags, a less mechanical feel." });
	All.push_back({ "Hxy", "hardware sweep up",
		"Starts the 2A03 pulse's own sweep: the pitch rises by itself, x how often (0 fastest to 7 slowest), y how much each "
		"time (1 the most to 7 the least).", "the 2A03 pulses only", "until the next note",
		"a rising zap or \"boing\", laser shots from a high note, quick glissandi. A sweep that runs off the top falls silent." });
	All.push_back({ "Ixy", "hardware sweep down",
		"Starts the 2A03 pulse's own sweep downward: the pitch falls by itself, x how often (0 fastest), y how much (1 the most).",
		"the 2A03 pulses only", "until the next note",
		"I12 a quick drop: a kick drum on a pulse (a low note, halted a row or two later); I71 a slow fall; dives." });
	All.push_back({ "Pxx", "fine pitch",
		"Tunes the channel a little: P80 is in tune, P81-P8F a little higher, P7F-P70 a little lower (a period step each).",
		"every channel with a pitch", "until changed",
		"a chorus: double a part on two channels with P81 or P82 on one; fine tuning one chip against another." });
	All.push_back({ "Qxy", "note slide up",
		"Slides from the note up y semitones (1-F), at speed x (0-F, faster as it rises), and stays there.",
		"every channel with a pitch", "this row only",
		"bends and scoops: to land on a note, write the one y semitones below it with Qxy. Q31 a quick lift of a semitone, Q72 a "
		"fast whole-step bend, Q1C a slow rise of an octave." });
	All.push_back({ "Rxy", "note slide down",
		"Slides from the note down y semitones (1-F), at speed x, and stays there.", "every channel with a pitch", "this row only",
		"falls at the end of a phrase (R5C, an octave down quickly), sighs (R22), sound effects." });
	All.push_back({ "Sxx", "note cut",
		"Cuts the note off after xx ticks.", "every channel", "this row only",
		"staccato: S02-S04 make short, plucky notes; gated chords with 0xy; short drum hits on melodic channels." });
	All.push_back({ "Vxx", "timbre",
		"Changes the tone: a pulse's duty (2A03 and MMC5 pulses 00 12.5% thin, 01 25%, 02 50% round, 03 75%; VRC6 pulses "
		"00-07, 1/16 to 8/16), the noise's mode (00 hiss, 01 metallic), an N163 channel's wave (00-0F).",
		"pulses, the noise and the N163 channels", "until changed; an instrument's duty sequence takes over while it runs",
		"a brighter or thinner lead for a section, duties alternating for a \"talking\" lead, metallic noise for hats and zaps." });
	All.push_back({ "Wxx", "DPCM pitch",
		"Plays the sample at rate xx (00-0F) instead of the instrument's.", "the DPCM channel", "this note",
		"the same drum sample higher or lower: toms from one sample, a tighter snare." });
	All.push_back({ "Xxx", "DPCM retrigger",
		"Plays the sample again every xx+1 ticks.", "the DPCM channel", "until the next note", "drum rolls and stutters (X03-X06)." });
	All.push_back({ "Yxx", "DPCM sample offset",
		"Starts the sample xx times 64 bytes in.", "the DPCM channel", "this note",
		"a punchier kick from a longer sample; one long sample cut into slices." });
	All.push_back({ "Zxx", "DPCM delta counter",
		"Sets the DPCM channel's output level (00-7F) directly. The NES mixes the triangle, the noise and the DPCM together, so a "
		"high level makes the triangle and the noise quieter.", "the DPCM channel", "until changed",
		"Z00 at the song's start keeps the triangle and the noise at full volume; a jump in level pops, which some games use as a "
		"kick." });
	All.push_back({ "Hxx", "FDS modulation depth",
		"How deep the FDS's frequency modulator wobbles the tone (00-3F).", "the FDS channel", "until changed",
		"bell and vibrato tones on the FDS, with Ixx and Jxx for the speed." });
	All.push_back({ "Ixx", "FDS modulation speed, high",
		"The high part (00-0F) of the FDS modulator's speed.", "the FDS channel", "until changed", "with Jxx, how fast the FDS tone wobbles." });
	All.push_back({ "Jxx", "FDS modulation speed, low",
		"The low part (00-FF) of the FDS modulator's speed.", "the FDS channel", "until changed", "with Ixx, how fast the FDS tone wobbles." });
	return All;
}

std::vector<Technique> MakeTechniques()
{
	std::vector<Technique> All;
	All.push_back({ "echo", "delay, echo channel, reverb",
		"The lead's notes again on a free channel, an eighth or a dotted eighth later and 5-8 steps quieter, often with a thinner "
		"duty: space without effects. add_echo makes one. With no channel free, repeat a long note softly on the rows after it." });
	All.push_back({ "chorus", "detune, pxx, double, unison, thick lead",
		"Two channels play the same part, one a little sharp: P81 or P82 on the second (P80 is in tune). The slow beat between them "
		"shimmers and thickens a lead; two pulses with different duties (or a 2A03 and a VRC6 pulse) even more." });
	All.push_back({ "delayed vibrato", "vibrato, singing lead",
		"Start long notes plain and bring the vibrato in after a beat: the note, then 443-454 a row or two later (4xy on that row), "
		"and 400 before the next note if it shouldn't wobble. add_vibrato does it with delay_beats, and design_instrument can build "
		"it into an instrument." });
	All.push_back({ "swing", "shuffle, groove, gxx",
		"Delay every off-beat note: at speed 6, G02 or G03 on the \"and\" of each beat (a third to a half of a row); all of them, "
		"or it sounds like mistakes. Drums and bass swing with the melody." });
	All.push_back({ "staccato", "short notes, sxx, gated, stabs",
		"Cut notes short with Sxx: S02-S04 make plucky, bouncing notes; an arpeggio chord (037) with S03 sounds gated. A halt (---) "
		"or release (===) a row after the note does the same on the row grid." });
	All.push_back({ "fades and swells", "fade, swell, crescendo, decrescendo, volume slide, axy",
		"A held note fades out with Ax0 (A20 slowly, A80 quickly) and swells in with A0y from a soft start (volume 3, A02-A04); "
		"end with A00. The volume column on later notes does it a note at a time." });
	All.push_back({ "tremolo", "7xy, shimmer, throb",
		"7xy shakes the volume: 744 a gentle shimmer on pads, 7A8 a fast throb; 700 stops it. Not on the triangle, which has no "
		"volume." });
	All.push_back({ "sweeps", "hardware sweep, hxy, ixy, laser, zap, dive",
		"Hxy and Ixy start the 2A03 pulses' own sweep: I71-I73 a falling laser or a dive, H71 a rising zap. A kick on a pulse: a "
		"low note with I12, halted two rows later. The next note ends the sweep." });
	All.push_back({ "duty changes", "duty, timbre, vxx, tone color",
		"Vxx changes a pulse's tone: V00 thin and nasal (leads that cut through), V01 bright, V02 round and hollow (soft leads, "
		"flutes, pulse bass). Change it per section for color, or alternate per note for a \"talking\" lead. On the noise, V01 is "
		"metallic: hats, zaps, buzzes." });
	All.push_back({ "slides and grace notes", "bend, scoop, glide, portamento, qxy, rxy, fall",
		"A scoop into a note: write it a semitone or two below and slide up with Q21/Q32. A fall at a phrase's end: Rxy (R5C, an "
		"octave down). For every note gliding into the next, 3xx portamento (306 smooth), off again with 300." });
	All.push_back({ "arpeggio chords", "arpeggio, chords on one channel, 0xy",
		"0xy plays a chord on one channel: 037 minor, 047 major, 0C0 octaves, 059 or 057 open. It frees the other channels for "
		"a melody and a bass; change the arpeggio with the chords, and 000 for notes that should stand alone." });
	All.push_back({ "triangle kick", "kick on the triangle, triangle drums",
		"A kick on the triangle frees the noise for snares and hats: a short triangle note that drops fast, a C-3 or so with "
		"an instrument whose arpeggio or pitch falls an octave in a few ticks (or 2xx), cut after a row with S02 or a halt. A "
		"staple of Capcom and Konami tracks." });
	All.push_back({ "dpcm tricks", "dpcm, samples, zxx, yxx, wxx, xxx",
		"Z00 at the start keeps the triangle and the noise at full volume (a higher Z quiets them). Yxx starts a sample later, "
		"Wxx plays it higher or lower, Xxx retriggers it for rolls. add_sample_drums adds a kit." });
	All.push_back({ "noise drums", "drums on the noise, hi-hat, snare",
		"The noise's pitch makes the drum: low (0-4) thuds and kicks, the middle (5-9) snares, high (A-F) hats and cymbals; V01 "
		"for metallic hats. Short volume envelopes for hats, longer for snares and crashes; the drum presets and add_drums do "
		"this." });
	return All;
}

bool Is2A03Pulse(const ChannelInfo &C)
{
	return C.Kind == KIND_PULSE && C.Chip == "2A03";
}

bool HasPitch(const ChannelInfo &C)
{
	return C.Kind != KIND_DPCM;
}

// The highest Vxx the channel takes, -1 when it has nothing to change
int MostTimbre(const ChannelInfo &C)
{
	if (C.Kind == KIND_PULSE)
		return C.Chip == "VRC6" ? 7 : 3;
	if (C.Kind == KIND_NOISE)
		return 1;
	if (C.Chip == "N163")
		return 15;
	return -1;
}

std::string ChannelName(const ChannelInfo &C)
{
	return C.Name.empty() ? std::string("this channel") : C.FullName();
}

// Why an effect can't be played on the channel, or "" when it can
std::string ChannelProblem(const ChannelInfo &C, int Effect, int Param, int Speed, const std::string &Written)
{
	const int x = (Param >> 4) & 15, y = Param & 15;
	char Text[512];
	Text[0] = 0;
	switch (Effect) {
	case EFF_ARPEGGIO: case EFF_PORTA_UP: case EFF_PORTA_DOWN: case EFF_PORTAMENTO: case EFF_VIBRATO: case EFF_PITCH:
	case EFF_SLIDE_UP: case EFF_SLIDE_DOWN:
		if (!HasPitch(C))
			return "The DPCM channel plays samples at their own rates, so pitch effects don't reach it; Wxx sets a sample's "
				"rate.";
		if ((Effect == EFF_SLIDE_UP || Effect == EFF_SLIDE_DOWN) && y == 0)
			std::snprintf(Text, sizeof(Text), "%s slides nowhere: y is how many semitones (1-F), x how fast (0-F).",
				Written.c_str());
		break;
	case EFF_TREMOLO: case EFF_VOLUME_SLIDE:
		if (C.Kind == KIND_TRIANGLE)
			return "The triangle has no volume, only on and off, so 7xy and Axy can only cut it out. End its notes with a halt, "
				"a release or Sxx instead.";
		if (C.Kind == KIND_DPCM)
			return "The DPCM channel plays samples at their own loudness, so 7xy and Axy don't reach it.";
		if (Effect == EFF_VOLUME_SLIDE && x != 0 && y != 0)
			return "Axy slides one way at a time: A0y up, Ax0 down.";
		break;
	case EFF_SWEEPUP: case EFF_SWEEPDOWN:
		if (!Is2A03Pulse(C))
			std::snprintf(Text, sizeof(Text), "Hxy and Ixy start the sweep of the 2A03's pulses, which %s doesn't have; slide with "
				"1xx/2xx or Qxy/Rxy instead.", ChannelName(C).c_str());
		else if (x > 7 || y < 1 || y > 7)
			std::snprintf(Text, sizeof(Text), "%s: x is how often the sweep moves (0-7) and y how much (1-7), like %c71.",
				Written.c_str(), Effect == EFF_SWEEPUP ? 'H' : 'I');
		break;
	case EFF_DUTY_CYCLE: {
		const int Most = MostTimbre(C);
		if (Most < 0)
			std::snprintf(Text, sizeof(Text), "Vxx changes the tone of pulses, the noise and the N163; %s has none to change.",
				ChannelName(C).c_str());
		else if (Param > Most)
			std::snprintf(Text, sizeof(Text), "%s takes V00-V%02X%s.", ChannelName(C).c_str(), Most,
				C.Kind == KIND_PULSE && Most == 3 ? " (12.5%, 25%, 50%, 75%)" : (C.Kind == KIND_NOISE ? " (hiss, metallic)" : ""));
		break;
	}
	case EFF_DAC: case EFF_SAMPLE_OFFSET: case EFF_RETRIGGER: case EFF_DPCM_PITCH:
		if (C.Kind != KIND_DPCM)
			return "Wxx, Xxx, Yxx and Zxx are the DPCM channel's, for its samples.";
		if (Effect == EFF_DAC && Param > 0x7F)
			return "Zxx goes from Z00 to Z7F.";
		if (Effect == EFF_DPCM_PITCH && Param > 0x0F)
			return "Wxx goes from W00 to W0F.";
		break;
	case EFF_FDS_MOD_DEPTH: case EFF_FDS_MOD_SPEED_HI: case EFF_FDS_MOD_SPEED_LO:
		if (C.Chip != "FDS")
			return "Hxx, Ixx and Jxx are the FDS's modulator here.";
		if (Effect == EFF_FDS_MOD_DEPTH && Param > 0x3F)
			return "Hxx (the FDS's modulation depth) goes from H00 to H3F.";
		if (Effect == EFF_FDS_MOD_SPEED_HI && Param > 0x0F)
			return "Ixx (the FDS's modulation speed, high) goes from I00 to I0F.";
		break;
	case EFF_DELAY:
		if (Speed > 0 && Param >= Speed)
			std::snprintf(Text, sizeof(Text), "Gxx holds a note back xx ticks, and at speed %d a row lasts %d, so %s would push it "
				"past its row. Use G01-G%02X, or put the note on a later row.", Speed, Speed, Written.c_str(), Speed - 1);
		break;
	case EFF_VOLUME:
		return "Exx (volume) is kept in 0.4.6 only for old songs; give the note a volume (1-15) instead.";
	case EFF_SPEED: case EFF_JUMP: case EFF_SKIP: case EFF_HALT:
		return "Fxx, Bxx, Cxx and Dxx steer the whole song: set_tempo sets the speed and tempo, set_loop the loop and the end.";
	case EFF_NOTE_CUT:
		break;
	default:
		std::snprintf(Text, sizeof(Text), "%s isn't one of FamiTracker 0.4.6's effects.", Written.c_str());
	}
	return Text;
}

// Letters other trackers and later FamiTrackers use that 0.4.6 doesn't have
std::string ForeignEffect(char Letter, const std::string &Written)
{
	switch (Letter) {
	case 'L':
		return Written + " (release the note after xx ticks) comes from later FamiTrackers (0CC-FamiTracker), not 0.4.6. End the "
			"note with a release (===) on a later row, or cut it with Sxx.";
	case 'M':
		return Written + " (a volume change xx ticks later) comes from 0CC-FamiTracker, not 0.4.6. Give the next note its volume, "
			"or slide with Axy.";
	case 'O':
		return Written + " (a groove) comes from 0CC-FamiTracker, not 0.4.6. For a swing, hold the off-beat notes back with Gxx.";
	case 'T':
		return Written + " (a transpose xx ticks later) comes from 0CC-FamiTracker, not 0.4.6. Write the next note, or slide to it "
			"with Qxy or Rxy.";
	case '5':
		return Written + " (portamento and volume slide at once) is a MOD and XM effect. In FamiTracker put 3xx and Axy in two "
			"effect columns.";
	case '6':
		return Written + " (vibrato and volume slide at once) is a MOD and XM effect. In FamiTracker put 4xy and Axy in two effect "
			"columns.";
	case '8':
		return Written + " (panning) is a MOD and XM effect; the NES plays in mono.";
	case '9':
		return Written + " (sample offset) is a MOD effect; FamiTracker's is Yxx, on the DPCM channel.";
	}
	return Written + " isn't one of FamiTracker 0.4.6's effects (later FamiTrackers and other trackers have more).";
}

const char *const OVERVIEW =
	"FamiTracker 0.4.6's effects: a letter or digit and two hex digits in a note's effect column, like Q31. Most stay on until "
	"turned off with 00; Gxx, Qxy, Rxy and Sxx act once, on their row.\n"
	"Pitch: 0xy arpeggio, 1xx and 2xx slide up and down, 3xx portamento, 4xy vibrato, Pxx fine pitch, Qxy and Rxy note slide "
	"up and down, Hxy and Ixy the hardware sweep (2A03 pulses).\n"
	"Volume and time: 7xy tremolo, Axy volume slide, Gxx note delay, Sxx note cut.\n"
	"Tone: Vxx timbre (a pulse's duty, the noise's mode, an N163 wave).\n"
	"Samples (the DPCM channel): Wxx pitch, Xxx retrigger, Yxx sample offset, Zxx delta counter.\n"
	"The FDS: Hxx modulation depth, Ixx and Jxx modulation speed.\n"
	"The song (set_tempo and set_loop write these): Fxx speed or tempo, Bxx jump, Dxx skip, Cxx halt. Exx (volume) is only kept "
	"for old songs: use the volume column.\n"
	"Not in 0.4.6: Lxx, Mxy, Oxx, Txy and the like come from later FamiTrackers (0CC); 5xy, 6xy, 8xx and 9xx from MOD and XM. "
	"write_notes refuses them and effects a channel can't play, and says what to use.";

}	// namespace

const std::vector<EffectInfo> &EffectReference()
{
	static const std::vector<EffectInfo> All = MakeReference();
	return All;
}

const std::vector<Technique> &Techniques()
{
	static const std::vector<Technique> All = MakeTechniques();
	return All;
}

bool EffectsText(const std::string &Effect, const std::string &Technique, const ChannelInfo *pChannel, std::string &Text)
{
	Text.clear();
	const std::vector<EffectInfo> &Effects = EffectReference();
	const std::vector<ftapi::Technique> &Ways = Techniques();
	std::string Names;
	for (size_t i = 0; i < Ways.size(); ++i)
		Names += (i == 0 ? "" : ", ") + std::string(Ways[i].Name);

	if (!Technique.empty()) {
		const std::string Wanted = Squeezed(Technique);
		for (size_t i = 0; i < Ways.size(); ++i) {
			bool Match = Squeezed(Ways[i].Name) == Wanted;
			std::string Words = Ways[i].Words;
			size_t From = 0;
			while (!Match && From <= Words.size()) {
				const size_t Comma = Words.find(',', From);
				const std::string Word = Words.substr(From, Comma == std::string::npos ? std::string::npos : Comma - From);
				Match = !Squeezed(Word).empty() && Squeezed(Word) == Wanted;
				if (Comma == std::string::npos)
					break;
				From = Comma + 1;
			}
			if (Match) {
				Text = std::string(Ways[i].Name) + ": " + Ways[i].Text;
				Text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(Text[0])));
				return true;
			}
		}
		Text = "No technique \"" + Technique + "\". The techniques: " + Names + ".";
		return false;
	}

	if (!Effect.empty()) {
		std::string Letter = Effect;
		Letter.erase(std::remove_if(Letter.begin(), Letter.end(), [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }),
			Letter.end());
		const char First = Letter.empty() ? 0 : static_cast<char>(std::toupper(static_cast<unsigned char>(Letter[0])));
		const bool Fds = pChannel != 0 && pChannel->Chip == "FDS";
		for (size_t i = 0; i < Effects.size(); ++i) {
			const EffectInfo &E = Effects[i];
			if (E.Form[0] != First)
				continue;
			// H, I and J: the sweep on the 2A03 pulses, the modulator on the FDS
			const bool FdsEntry = std::string(E.Name).compare(0, 3, "FDS") == 0;
			if ((First == 'H' || First == 'I') && FdsEntry != Fds)
				continue;
			Text += (Text.empty() ? "" : "\n") + std::string(E.Form) + " " + E.Name + ": " + E.Does + " Channels: " + E.Channels +
				". Lasts: " + E.Lasts + ". Good for: " + E.Uses;
		}
		if (First == 'H' || First == 'I')
			Text += Fds ? "" : std::string("\n(On the FDS channel, ") + First + "xx is its modulator: ask with that channel.)";
		if (!Text.empty())
			return true;
		if (First == 'F' || First == 'B' || First == 'C' || First == 'D' || First == 'E') {
			Text = ChannelProblem(ChannelInfo(), First == 'F' ? EFF_SPEED : First == 'E' ? EFF_VOLUME : EFF_JUMP, 0, 0, Letter);
			return true;
		}
		Text = ForeignEffect(First, std::string(1, First) + "xx");
		return false;
	}

	if (pChannel != 0) {
		const ChannelInfo &C = *pChannel;
		std::vector<std::string> Can;
		const bool Fds = C.Chip == "FDS";
		for (size_t i = 0; i < Effects.size(); ++i) {
			const EffectInfo &E = Effects[i];
			// H, I and J: the sweep on the 2A03 pulses, the modulator on the FDS
			if (std::string(E.Name).compare(0, 3, "FDS") == 0 ? !Fds : (Fds && (E.Form[0] == 'H' || E.Form[0] == 'I')))
				continue;
			const bool Pair = E.Form[0] == 'Q' || E.Form[0] == 'R' || (!Fds && (E.Form[0] == 'H' || E.Form[0] == 'I'));
			int Id = 0, Param = 0;
			std::string Problem;
			if (ParseChannelEffect(std::string(1, E.Form[0]) + (Pair ? "11" : "01"), C, 0, Id, Param, Problem))
				Can.push_back(E.Form);
		}
		Text = ChannelName(C) + " plays ";
		for (size_t i = 0; i < Can.size(); ++i)
			Text += (i == 0 ? "" : (i + 1 == Can.size() ? " and " : ", ")) + Can[i];
		Text += ".";
		if (C.Kind == KIND_TRIANGLE)
			Text += " The triangle has no volume (only on and off), so no tremolo or volume slides.";
		else if (C.Kind == KIND_DPCM)
			Text += " The DPCM channel plays samples: no pitch or volume effects.";
		if (C.Kind == KIND_PULSE && C.Chip != "2A03")
			Text += " Only the 2A03's pulses have the hardware sweep (Hxy, Ixy).";
		if (MostTimbre(C) >= 0) {
			char Line[64];
			std::snprintf(Line, sizeof(Line), " Vxx: V00-V%02X.", MostTimbre(C));
			Text += Line;
		}
		return true;
	}

	Text = std::string(OVERVIEW) + "\nTechniques: " + Names + ". Ask for one effect (\"Q\"), a technique, or a channel's effects.";
	return true;
}

bool ParseChannelEffect(const std::string &Input, const ChannelInfo &Channel, int Speed, int &Effect, int &Param, std::string &Problem)
{
	std::string Text;
	for (size_t i = 0; i < Input.size(); ++i)
		if (!std::isspace(static_cast<unsigned char>(Input[i])))
			Text += Input[i];
	for (size_t i = 0; i < Text.size(); ++i)
		Text[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(Text[i])));
	if (Text.size() != 3 || !std::isxdigit(static_cast<unsigned char>(Text[1])) || !std::isxdigit(static_cast<unsigned char>(Text[2]))) {
		Problem = "\"" + Input + "\": effects are written as in FamiTracker, a letter or digit and two hex digits, like \"Q31\" or "
			"\"047\".";
		return false;
	}
	const char Letter = Text[0];
	Param = static_cast<int>(std::strtol(Text.substr(1).c_str(), 0, 16));
	if (Channel.Chip == "FDS" && (Letter == 'H' || Letter == 'I' || Letter == 'J'))
		Effect = Letter == 'H' ? EFF_FDS_MOD_DEPTH : (Letter == 'I' ? EFF_FDS_MOD_SPEED_HI : EFF_FDS_MOD_SPEED_LO);
	else if (Letter == 'J')
		Effect = EFF_FDS_MOD_SPEED_LO;	// its ChannelProblem says it's the FDS's
	else {
		int Ignored = 0;
		if (!ParseEffect(Text, Effect, Ignored)) {
			Problem = ForeignEffect(Letter, Text);
			return false;
		}
	}
	Problem = ChannelProblem(Channel, Effect, Param, Speed, Text);
	return Problem.empty();
}

}	// namespace ftapi
